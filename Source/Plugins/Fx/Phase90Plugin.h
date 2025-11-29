/*
  ==============================================================================

    Phase90Plugin

  ============================================================================== 
*/

#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <memory>
#include "FxCommon.h"

//==============================================================================
// MXR Phase 90 style phaser processor with simple GUI (one SPEED knob + footswitch)
// NOTE: Audio is ALWAYS wet. Bypass parameter only affects UI/LED state, not audio path.
class Phase90Processor final : public juce::AudioProcessor
{
public:
    Phase90Processor()
        : AudioProcessor(BusesProperties()
            .withInput("Input", juce::AudioChannelSet::mono(), true)
            .withOutput("Output", juce::AudioChannelSet::mono(), true))
    {
        addParameter(rate = new juce::AudioParameterFloat({ "rate", 1 }, "Rate",
            0.05f, 6.0f, 0.6f));
        addParameter(bypass = new juce::AudioParameterBool({ "bypass", 1 }, "Bypass", false));

        sampleRate = 44100.0;
        resetState();
    }

    ~Phase90Processor() override = default;

    //============================================================================== 
    void prepareToPlay(double newSampleRate, int /*samplesPerBlock*/) override
    {
        juce::ScopedNoDenormals noDenormals;
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        resetState();

        // DC‑blocker coefficient (first order). cutOff default 20 Hz (tunable)
        const double hpCutoff = 20.0;
        hpCoeff = std::exp(-2.0 * juce::MathConstants<double>::pi * hpCutoff / sampleRate);
    }

    void releaseResources() override {}

    // main processing (float)
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        // IMPORTANT: bypass parameter does NOT bypass audio. Signal is ALWAYS wet as per Schaltplan.
        const int numChannels = jmin(2, buffer.getNumChannels());
        const int numSamples = buffer.getNumSamples();

        // read rate once per block for stability (LFO increment computed per-sample)
        const float rateHz = rate ? *rate : 0.6f;
        const double twoPi = juce::MathConstants<double>::twoPi;
        const double phaseInc = (twoPi * (double)rateHz) / sampleRate;

        // fixed base frequencies for the four allpass stages (tuned to emulate Phase 90)
        constexpr double baseFreqs[4] = { 700.0, 1000.0, 1300.0, 1700.0 };
        constexpr double depth = 0.85;

        for (int n = 0; n < numSamples; ++n)
        {
            // sine LFO
            const double lfo = std::sin(lfoPhase);
            lfoPhase += phaseInc;
            if (lfoPhase >= twoPi) lfoPhase -= twoPi;

            // compute instantaneous stage frequencies and coefficients
            double aCoeffs[4];
            for (int s = 0; s < 4; ++s)
            {
                const double f = baseFreqs[s] * (1.0 + depth * lfo);
                const double fClamped = juce::jlimit(5.0, sampleRate * 0.49, f);
                const double omega = 2.0 * juce::MathConstants<double>::pi * fClamped / sampleRate;
                const double t = std::tan(omega * 0.5);
                const double tClamped = juce::jlimit(1e-8, 1e8, t);
                const double a = (1.0 - tClamped) / (1.0 + tClamped);
                aCoeffs[s] = a;
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* channelData = buffer.getWritePointer(ch);
                const double xRaw = (double)channelData[n];

                // Simple 1‑pole DC blocker: y = c*(y_prev + x - x_prev)
                double x = hpCoeff * (hpPrevOut[ch] + xRaw - hpPrevIn[ch]);
                hpPrevIn[ch] = xRaw;
                hpPrevOut[ch] = x;

                // 4 cascaded first-order allpass stages
                // Correct allpass difference equation (y = -a*x + x1 + a*y1)

                FxCommon::AllpassState& s0 = allpassStates[ch][0];
                const double a0 = aCoeffs[0];
                double y0 = -a0 * x + s0.x1 + a0 * s0.y1;
                s0.x1 = x; s0.y1 = y0;

                FxCommon::AllpassState& s1 = allpassStates[ch][1];
                const double a1 = aCoeffs[1];
                double y1 = -a1 * y0 + s1.x1 + a1 * s1.y1;
                s1.x1 = y0; s1.y1 = y1;

                FxCommon::AllpassState& s2 = allpassStates[ch][2];
                const double a2 = aCoeffs[2];
                double y2 = -a2 * y1 + s2.x1 + a2 * s2.y1;
                s2.x1 = y1; s2.y1 = y2;

                FxCommon::AllpassState& s3 = allpassStates[ch][3];
                const double a3 = aCoeffs[3];
                double y3 = -a3 * y2 + s3.x1 + a3 * s3.y1;
                s3.x1 = y2; s3.y1 = y3;

                // always wet output (Schaltplan: cascaded allpass -> output mixer fixed to wet)
                channelData[n] = (float)y3;
            }
        }

        // leave extra channels untouched (if any)
    }

    // double precision not implemented
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override
    {
        jassertfalse;
    }

    //============================================================================== 
    juce::AudioProcessorEditor* createEditor() override { return new Editor(*this, rate, bypass); }
    bool hasEditor() const override { return true; }

    //============================================================================== 
    const juce::String getName() const override { return "Phase 90"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //============================================================================== 
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    //============================================================================== 
    void getStateInformation(juce::MemoryBlock& destData) override
    {
        juce::MemoryOutputStream stream(destData, true);
        stream.writeFloat(rate ? *rate : 0.6f);
        stream.writeFloat(bypass ? static_cast<float>(*bypass) : 0.0f);
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
        if (rate)
        {
            // stream.readFloat() returns the real parameter value; convert to normalized 0..1 before setting
            const float storedVal = stream.readFloat();
            rate->setValueNotifyingHost(static_cast<float>(rate->getNormalisableRange().convertTo0to1(storedVal)));
        }
        if (bypass) bypass->setValueNotifyingHost(stream.readFloat());
        resetState();
    }

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        const auto& mainInLayout = layouts.getChannelSet(true, 0);
        const auto& mainOutLayout = layouts.getChannelSet(false, 0);
        return (mainInLayout == mainOutLayout && (!mainInLayout.isDisabled()));
    }

    //============================================================================== 
    // Editor: simple single-knob script-logo style pedal
    class Editor final : public juce::AudioProcessorEditor,
                         private juce::Slider::Listener,
                         private juce::Timer
    {
    public:
        Editor(Phase90Processor& p, juce::AudioParameterFloat* rateParam, juce::AudioParameterBool* bypassParam)
            : AudioProcessorEditor(&p), processor(p), rateParameter(rateParam), bypassParameter(bypassParam)
        {
            setLookAndFeel(&laf);
            setSize(220, 340);

            // rotary slider
            rateSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            rateSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            rateSlider.setRange(0.05, 6.0, 0.01);
            // nice sweep similar to hardware: ~240deg
            // SWAPPED start/end to correct vertical mirror
            const float startA = -2.09439510239319549f - juce::MathConstants<float>::halfPi;
            const float endA   =  2.09439510239319549f - juce::MathConstants<float>::halfPi;
            rateSlider.setRotaryParameters(startA, endA, true);
            rateSlider.addListener(this);
            addAndMakeVisible(rateSlider);

            rateSlider.setValue(rateParameter ? *rateParameter : 0.6f, juce::dontSendNotification);

            // label
            addAndMakeVisible(speedLabel);
            speedLabel.setText("SPEED", juce::dontSendNotification);
            speedLabel.setJustificationType(juce::Justification::centred);
            speedLabel.setColour(juce::Label::textColourId, juce::Colours::white);
            speedLabel.setFont(juce::Font(14.0f, juce::Font::bold));

            // bypass footswitch - invisible button on top of painted footswitch
            // Note: toggling only changes UI state (LED). Audio path remains wet.
            bypassButton.setClickingTogglesState(true);
            bypassButton.setToggleState(bypassParameter ? static_cast<bool>(*bypassParameter) : false, juce::dontSendNotification);
            bypassButton.onClick = [this]()
            {
                if (!bypassParameter) return;
                bool newVal = bypassButton.getToggleState();
                // keep parameter for host automation/preset recall, but do NOT alter audio path
                bypassParameter->setValueNotifyingHost(newVal ? 1.0f : 0.0f);
            };
            bypassButton.setColour(juce::ToggleButton::textColourId, juce::Colours::transparentBlack);
            bypassButton.setColour(juce::ToggleButton::tickColourId, juce::Colours::transparentBlack);
            addAndMakeVisible(bypassButton);

            // Start polling timer (same approach as GainProcessor)
            startTimerHz(30);
            setWantsKeyboardFocus(false);
        }

        ~Editor() override
        {
            stopTimer();
            rateSlider.removeListener(this);
            setLookAndFeel(nullptr);
        }

        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            // orange pedal background
            g.fillAll(juce::Colour::fromRGB(235, 122, 0));
            // subtle inner panel
            g.setColour(juce::Colours::black.withAlpha(0.08f));
            g.fillRoundedRectangle(bounds.reduced(12.0f), 6.0f);

            // Title / logo (script)
            g.setColour(juce::Colours::black);
            g.setFont(juce::Font(20.0f, juce::Font::bold));
            g.drawFittedText("Phase 90", getWidth() / 2 - 80, 18, 160, 30, juce::Justification::centred, 1);

            // draw footswitch
            juce::Point<float> footCentre(bounds.getCentreX(), bounds.getBottom() - 72.0f);
            float footR = 26.0f;
            juce::Colour metal = juce::Colour::fromRGB(200, 200, 200);
            g.setColour(metal.overlaidWith(juce::Colours::white.withAlpha(0.15f)));
            g.fillEllipse(footCentre.x - footR, footCentre.y - footR, footR * 2.0f, footR * 2.0f);
            g.setColour(metal.contrasting(0.4f));
            g.drawEllipse(footCentre.x - footR, footCentre.y - footR, footR * 2.0f, footR * 2.0f, 2.0f);

            // LED: lights when effect engaged (not bypassed) — purely visual
            bool isBypassed = (bypassParameter ? static_cast<bool>(*bypassParameter) : false);
            bool ledOn = !isBypassed;
            float ledR = 7.0f;
            juce::Point<float> ledPos(footCentre.x, footCentre.y - 52.0f);
            if (ledOn) g.setColour(juce::Colours::red.brighter(0.0f));
            else g.setColour(juce::Colours::darkred.darker(0.7f));
            g.fillEllipse(ledPos.x - ledR, ledPos.y - ledR, ledR * 2.0f, ledR * 2.0f);
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.drawEllipse(ledPos.x - ledR, ledPos.y - ledR, ledR * 2.0f, ledR * 2.0f, 1.0f);

            // small border
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.drawRoundedRectangle(bounds.reduced(10.0f), 6.0f, 2.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced(18);
            int knobSize = 120;
            int cx = r.getCentreX();
            rateSlider.setBounds(cx - knobSize/2, r.getY() + 48, knobSize, knobSize);

            speedLabel.setBounds(rateSlider.getX(), rateSlider.getY() - 26, rateSlider.getWidth(), 22);

            // place bypass button over the footswitch area
            int centreX = getWidth() / 2;
            int footY = getHeight() - 72;
            int btnSize = 56;
            bypassButton.setBounds(centreX - btnSize / 2, footY - btnSize / 2, btnSize, btnSize);
        }

    private:
        void timerCallback() override
        {
            // Poll parameters and update UI like GainProcessor
            if (rateParameter && bypassParameter)
            {
                const float pRate = *rateParameter;
                const bool pBy = static_cast<bool>(*bypassParameter);

                if (std::abs((float)rateSlider.getValue() - pRate) > 0.001f)
                    rateSlider.setValue(pRate, juce::dontSendNotification);

                if (bypassButton.getToggleState() != pBy)
                    bypassButton.setToggleState(pBy, juce::dontSendNotification);

                repaint();
            }
        }

        // Slider::Listener -> update parameter immediately (same as GainProcessor)
        void sliderValueChanged(juce::Slider* s) override
        {
            if (!isVisible())
                return;

            if (s == &rateSlider && rateParameter)
            {
                // IMPORTANT:
                // AudioProcessorParameter::setValueNotifyingHost expects the normalized value (0..1).
                // The slider uses the real Hz range (0.05..6.0). Convert to 0..1 here.
                const float realVal = static_cast<float>(rateSlider.getValue());
                const float normalized = static_cast<float>(rateParameter->getNormalisableRange().convertTo0to1(realVal));
                rateParameter->setValueNotifyingHost(normalized);
            }
        }

        Phase90Processor& processor;
        juce::AudioParameterFloat* rateParameter = nullptr;
        juce::AudioParameterBool* bypassParameter = nullptr;

        juce::Slider rateSlider;
        juce::Label speedLabel;
        juce::ToggleButton bypassButton;

        // Use shared pedal look-and-feel from FxCommon
        FxCommon::PedalLookAndFeel laf;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
    };

private:
    void resetState()
    {
        lfoPhase = 0.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int s = 0; s < 4; ++s)
                allpassStates[ch][s] = FxCommon::AllpassState();

            hpPrevIn[ch] = 0.0;
            hpPrevOut[ch] = 0.0;
        }
    }

    // parameters
    juce::AudioParameterFloat* rate = nullptr;
    juce::AudioParameterBool* bypass = nullptr;

    FxCommon::AllpassState allpassStates[2][4];
    double sampleRate = 44100.0;
    double lfoPhase = 0.0;

    // DC blocker state per channel
    double hpPrevIn[2] = { 0.0, 0.0 };
    double hpPrevOut[2] = { 0.0, 0.0 };
    double hpCoeff = 0.995;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Phase90Processor)
};
