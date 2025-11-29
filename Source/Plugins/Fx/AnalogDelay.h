/*
  ==============================================================================

    AnalogDelay.h
    Created: 29 Nov 2025 1:33:43pm
    Author:  motzi

    Analog-style delay plugin (struktur angelehnt an GainProcessor.h)
    - Parameter: Delay (Zeit), Mix, Regen (Feedback), Bypass
    - Mono I/O wie GainProcessor
    - Einfacher BBD/Bucket-bridges-artiger Algorithmus mit
      linearer Fraktions-Interpolation und einer Feedback-Lowpass
    - Editor: Pedal-Style GUI basierend auf FxCommon::PedalLookAndFeel

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <memory>
#include <vector>
#include "FxCommon.h"

//==============================================================================
// Einfaches analoges Delay (mono) - Aufbau & UI wie GainProcessor
//==============================================================================

class AnalogDelay final : public AudioProcessor
{
public:
    //==============================================================================
    AnalogDelay()
        : AudioProcessor(BusesProperties().withInput("Input", AudioChannelSet::mono())
                                         .withOutput("Output", AudioChannelSet::mono()))
    {
        // Alle Regler standardmäßig auf 0.5 (12 Uhr)
        addParameter(delay = new AudioParameterFloat({ "delay", 1 }, "Delay", 0.0f, 1.0f, 0.5f));
        addParameter(mix = new AudioParameterFloat({ "mix", 1 }, "Mix", 0.0f, 1.0f, 0.5f));
        addParameter(regen = new AudioParameterFloat({ "regen", 1 }, "Regen", 0.0f, 1.0f, 0.5f));
        addParameter(bypass = new AudioParameterBool({ "bypass", 1 }, "Bypass", false));
    }

    //==============================================================================

    void prepareToPlay(double sampleRateIn, int /*samplesPerBlock*/) override
    {
        sampleRate = sampleRateIn;
        // maximaler Delaybereich (ms) -> Buffergröße berechnen
        const double maxDelayMs = maxDelayMilliseconds;
        const int maxSamples = static_cast<int>(std::ceil(maxDelayMs * sampleRate / 1000.0)) + 4;
        delayBuffer.assign(maxSamples, 0.0);
        bufferSize = (int)delayBuffer.size();
        writeIndex = 0;

        // Feedback filter state (mono, aber als vector für späteren Multichannel-Support)
        fbState.assign(1, 0.0);
        lastFbCutoff = -1.0;
        updateFeedbackCoeffs();
    }

    void releaseResources() override {}

    // shared per-sample processing
    template<typename SampleType>
    inline SampleType processSampleInternal(SampleType in, int /*ch*/)
    {
        // Bypass early out (parameter is checked by processBlock caller)
        // map delay param [0..1] to delay ms range
        const float dVal = *delay;
        const double minMs = minDelayMilliseconds;
        const double maxMs = maxDelayMilliseconds;
        const double delayMs = minMs * std::pow(maxMs / minMs, dVal); // logarithmisch
        const double delaySamples = delayMs * sampleRate / 1000.0;

        // compute read position (fractional)
        double readPos = static_cast<double>(writeIndex) - delaySamples;
        while (readPos < 0.0)
            readPos += bufferSize;
        const int idxA = static_cast<int>(std::floor(readPos)) % bufferSize;
        const int idxB = (idxA + 1) % bufferSize;
        const double frac = readPos - std::floor(readPos);

        // linear interpolation
        const double delayed = delayBuffer[idxA] * (1.0 - frac) + delayBuffer[idxB] * frac;

        // feedback path: regen controls amount, pass through simple one-pole lowpass
        // NOTE: regen is scaled to reduce overall strength. The current value that used to be at 0.5
        // now corresponds to regen==1.0 (regenScale = 0.5) as requested.
        const double regenGain = static_cast<double>(*regen) * regenScale;
        updateFeedbackCoeffsIfNeeded(regenGain);

        double fbIn = delayed * regenGain;
        // one-pole smoothing: y = a*x + (1-a)*y_prev  (we store in fbState[0])
        double a = fbAlpha;
        double fbFiltered = a * fbIn + (1.0 - a) * fbState[0];
        fbState[0] = fbFiltered;

        // write into buffer: input plus feedback (emulates BBD summing node)
        // apply slight attenuation to avoid runaway
        double toWrite = static_cast<double>(in) + fbFiltered * 0.95;
        // a tiny soft clip to emulate analog saturation
        toWrite = std::tanh(toWrite * 3.0);

        delayBuffer[writeIndex] = toWrite;
        ++writeIndex;
        if (writeIndex >= bufferSize)
            writeIndex = 0;

        // mix dry/wet
        const double mixVal = static_cast<double>(*mix);
        const double out = (1.0 - mixVal) * static_cast<double>(in) + mixVal * delayed;

        // final gentle limiter to avoid extreme peaks
        const double outLimited = std::tanh(out * 10.0);

        return static_cast<SampleType>(outLimited);
    }

    void processBlock(AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        if (bypass && static_cast<bool>(*bypass))
            return;

        const int numCh = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        // Ensure fbState size matches channels (mono plugin normally 1)
        if ((int)fbState.size() < numCh)
            fbState.resize(numCh, 0.0);

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                data[i] = processSampleInternal<float>(data[i], ch);
            }
        }
    }

    void processBlock(AudioBuffer<double>& buffer, MidiBuffer&) override
    {
        if (bypass && static_cast<bool>(*bypass))
            return;

        const int numCh = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        if ((int)fbState.size() < numCh)
            fbState.resize(numCh, 0.0);

        for (int ch = 0; ch < numCh; ++ch)
        {
            double* data = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                data[i] = processSampleInternal<double>(data[i], ch);
            }
        }
    }

    //==============================================================================
    AudioProcessorEditor* createEditor() override { return new Editor(*this, delay, mix, regen, bypass); }
    bool hasEditor() const override { return true; }

    //==============================================================================
    const String getName() const override { return "AnalogDelay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return maxDelayMilliseconds / 1000.0; }

    //==============================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const String getProgramName(int) override { return "None"; }
    void changeProgramName(int, const String&) override {}

    //==============================================================================
    void getStateInformation(MemoryBlock& destData) override
    {
        MemoryOutputStream stream(destData, true);
        stream.writeFloat(*delay);
        stream.writeFloat(*mix);
        stream.writeFloat(*regen);
        // save bypass as float (0.0 / 1.0)
        stream.writeFloat(static_cast<float>(bypass ? static_cast<float>(*bypass) : 0.0f));
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
        delay->setValueNotifyingHost(stream.readFloat());
        mix->setValueNotifyingHost(stream.readFloat());
        regen->setValueNotifyingHost(stream.readFloat());
        if (bypass)
            bypass->setValueNotifyingHost(stream.readFloat());
    }

    //==============================================================================

    // Editor: Pedal-styled UI (basierend auf FxCommon::PedalLookAndFeel)
    class Editor final : public AudioProcessorEditor,
                         private Slider::Listener,
                         private Timer
    {
    public:
        Editor(AnalogDelay& p,
               AudioParameterFloat* delayParam,
               AudioParameterFloat* mixParam,
               AudioParameterFloat* regenParam,
               AudioParameterBool* bypassParam)
            : AudioProcessorEditor(&p), processor(p),
              delayParameter(delayParam), mixParameter(mixParam), regenParameter(regenParam),
              bypassParameter(bypassParam)
        {
                    // Pedal LookAndFeel from FxCommon
            setLookAndFeel(&pedalLaf);

            setSize(340, 210);

            // rotary knob geometry: configure so 0.5 -> 12:00, 0 -> ~7:00, 1 -> ~5:00
            // startAngle = -pi/2 - 2.618...  (~ -240° -> equivalent to 120° / 7 Uhr)
            // endAngle   = -pi/2 + 2.618...  (~  60°  -> 5 Uhr)
            const float startAngle = -4.1887902047863905f; // -240°
            const float endAngle   =  1.0471975511965976f; //  60°

            for (auto* s : { &delaySlider, &mixSlider, &regenSlider })
            {
                s->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
                s->setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
                s->setRange(0.0, 1.0, 0.001);
                s->setRotaryParameters(startAngle, endAngle, true);
                s->addListener(this);
                addAndMakeVisible(s);
            }

            // initial values from params: all params default to 0.5 -> knobs at 12 Uhr
            if (delayParameter) delaySlider.setValue(*delayParameter, dontSendNotification);
            if (mixParameter)  mixSlider.setValue(*mixParameter, dontSendNotification);
            if (regenParameter) regenSlider.setValue(*regenParameter, dontSendNotification);

            // Labels
            addAndMakeVisible(lblDelay);
            addAndMakeVisible(lblMix);
            addAndMakeVisible(lblRegen);
            lblDelay.setText("DELAY", dontSendNotification);
            lblMix.setText("MIX", dontSendNotification);
            lblRegen.setText("REGEN", dontSendNotification);
            lblDelay.setJustificationType(Justification::centred);
            lblMix.setJustificationType(Justification::centred);
            lblRegen.setJustificationType(Justification::centred);
            lblDelay.setColour(Label::textColourId, Colours::white);
            lblMix.setColour(Label::textColourId, Colours::white);
            lblRegen.setColour(Label::textColourId, Colours::white);
            lblDelay.setFont(Font(12.0f, Font::bold));
            lblMix.setFont(Font(12.0f, Font::bold));
            lblRegen.setFont(Font(12.0f, Font::bold));

            // visible label "analog delay"
            addAndMakeVisible(analogLabel);
            analogLabel.setText("analog delay", dontSendNotification);
            analogLabel.setJustificationType(Justification::centredLeft);
            analogLabel.setColour(Label::textColourId, Colours::white);
            analogLabel.setFont(Font(14.0f, Font::bold));

            // bypass footswitch (invisible ToggleButton area - painting handles visuals)
            bypassButton.setClickingTogglesState(true);
            bypassButton.setToggleState(bypassParameter ? static_cast<bool>(*bypassParameter) : false, dontSendNotification);
            bypassButton.onClick = [this]()
            {
                if (!bypassParameter) return;
                bool newBypass = bypassButton.getToggleState();
                bypassParameter->setValueNotifyingHost(newBypass ? 1.0f : 0.0f);
            };
            bypassButton.setColour(ToggleButton::textColourId, Colours::transparentBlack);
            bypassButton.setColour(ToggleButton::tickColourId, Colours::transparentBlack);
            addAndMakeVisible(bypassButton);

            // Override knob colours to black knobs with white accents (original look)
            pedalLaf.setColour(juce::Slider::rotarySliderFillColourId, Colours::black);
            pedalLaf.setColour(juce::Slider::rotarySliderOutlineColourId, Colours::white);

            startTimerHz(30);
            setWantsKeyboardFocus(false);
        }

        ~Editor() override
        {
            stopTimer();
            delaySlider.removeListener(this);
            mixSlider.removeListener(this);
            regenSlider.removeListener(this);
            setLookAndFeel(nullptr);
        }

        void paint(Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();

            // Dark green base (dunkelgrün) with white accents and black knobs as requested
            const Colour baseGreen = Colour::fromRGB(18, 85, 50);
            g.fillAll(baseGreen);

            // subtle inner shade for panel (use a slightly darker green instead of black)
            g.setColour(baseGreen.darker(0.10f));
            g.fillRoundedRectangle(bounds.reduced(8.0f), 6.0f);

            // Top area for knobs: use same green family (no black)
            Rectangle<float> topBar = { bounds.getX() + 12.0f, bounds.getY() + 10.0f, bounds.getWidth() - 24.0f, 110.0f };
            g.setColour(baseGreen.darker(0.06f));
            g.fillRect(topBar);
            g.setColour(Colours::white);
            g.drawRoundedRectangle(topBar, 4.0f, 1.5f);

            // central foot area with white outline
            Rectangle<float> foot = { bounds.getX() + 12.0f, bounds.getBottom() - 78.0f, bounds.getWidth() - 24.0f, 64.0f };
            g.setColour(baseGreen.darker(0.03f));
            g.fillRoundedRectangle(foot, 4.0f);
            g.setColour(Colours::white);
            g.drawRoundedRectangle(foot, 4.0f, 1.4f);

            // footswitch circle (metallic)
            Point<float> footCentre(bounds.getCentreX(), foot.getCentreY());
            float footR = 20.0f;
            Colour metal = Colour::fromRGB(200, 200, 200);
            g.setColour(metal.overlaidWith(Colours::white.withAlpha(0.15f)));
            g.fillEllipse(footCentre.x - footR, footCentre.y - footR, footR * 2.0f, footR * 2.0f);
            g.setColour(metal.contrasting(0.4f));
            g.drawEllipse(footCentre.x - footR, footCentre.y - footR, footR * 2.0f, footR * 2.0f, 2.0f);

            // LED indicator (rot wenn Effekt aktiviert) - jetzt links neben Footswitch
            bool isBypassed = (bypassParameter ? static_cast<bool>(*bypassParameter) : false);
            bool ledOn = !isBypassed;
            float ledR = 7.0f;
            // LED links neben den Fußschalter platzieren
            Point<float> ledPos(footCentre.x - footR - 18.0f, footCentre.y);
            if (ledOn)
                g.setColour(Colours::red.brighter(0.0f));
            else
                g.setColour(Colours::darkred.darker(0.7f));
            g.fillEllipse(ledPos.x - ledR, ledPos.y - ledR, ledR * 2.0f, ledR * 2.0f);
            g.setColour(Colours::black.withAlpha(0.6f));
            g.drawEllipse(ledPos.x - ledR, ledPos.y - ledR, ledR * 2.0f, ledR * 2.0f, 1.0f);

            // subtle border
            g.setColour(Colours::black.withAlpha(0.35f));
            g.drawRoundedRectangle(bounds.reduced(8.0f), 6.0f, 2.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced(12);
            Rectangle<int> top = r.removeFromTop(122);

            int knobSize = 92;
            int gap = (top.getWidth() - knobSize * 3) / 4;
            int yKnob = top.getY() + 10;

            // Keep original visual order
            delaySlider.setBounds(top.getX() + gap, yKnob, knobSize, knobSize);
            mixSlider.setBounds(top.getX() + gap * 2 + knobSize, yKnob, knobSize, knobSize);
            regenSlider.setBounds(top.getX() + gap * 3 + knobSize * 2, yKnob, knobSize, knobSize);

            // Labels above knobs
            lblDelay.setBounds(delaySlider.getX(), top.getY() - 2, delaySlider.getWidth(), 18);
            lblMix.setBounds(mixSlider.getX(), top.getY() - 2, mixSlider.getWidth(), 18);
            lblRegen.setBounds(regenSlider.getX(), top.getY() - 2, regenSlider.getWidth(), 18);

            // foot area and label placement
            Rectangle<int> foot = r.removeFromBottom(78);

            // place analogLabel to the right of the footswitch
            const int centreX = getWidth() / 2;
            const int btnSize = 48;
            const int labelW = 140;
            const int labelH = 20;
            int labelX = centreX + (btnSize / 2) + 15;
            if (labelX + labelW > getWidth() - 12)
                labelX = getWidth() - 12 - labelW;
            int labelY = foot.getY() + (foot.getHeight() - labelH) / 2;
            analogLabel.setBounds(labelX, labelY, labelW, labelH);

            // bypass clickable area (centered on footswitch)
            int footY = getHeight() - 44;
            bypassButton.setBounds(centreX - btnSize / 2, footY - btnSize / 2, btnSize, btnSize);
        }

    private:
        void timerCallback() override
        {
            if (delayParameter && mixParameter && regenParameter && bypassParameter)
            {
                const float pDelay = *delayParameter;
                const float pMix = *mixParameter;
                const float pRegen = *regenParameter;
                const bool pBypass = static_cast<bool>(*bypassParameter);

                if (std::abs((float)delaySlider.getValue() - pDelay) > 0.001f)
                    delaySlider.setValue(pDelay, dontSendNotification);
                if (std::abs((float)mixSlider.getValue() - pMix) > 0.001f)
                    mixSlider.setValue(pMix, dontSendNotification);
                if (std::abs((float)regenSlider.getValue() - pRegen) > 0.001f)
                    regenSlider.setValue(pRegen, dontSendNotification);

                if (bypassButton.getToggleState() != pBypass)
                    bypassButton.setToggleState(pBypass, dontSendNotification);

                repaint();
            }
        }

        void sliderValueChanged(Slider* s) override
        {
            if (!isVisible()) return;

            if (s == &delaySlider && delayParameter)
            {
                delayParameter->setValueNotifyingHost(static_cast<float>(delaySlider.getValue()));
            }
            else if (s == &mixSlider && mixParameter)
            {
                mixParameter->setValueNotifyingHost(static_cast<float>(mixSlider.getValue()));
            }
            else if (s == &regenSlider && regenParameter)
            {
                regenParameter->setValueNotifyingHost(static_cast<float>(regenSlider.getValue()));
            }
        }

        AnalogDelay& processor;

        AudioParameterFloat* delayParameter = nullptr;
        AudioParameterFloat* mixParameter = nullptr;
        AudioParameterFloat* regenParameter = nullptr;
        AudioParameterBool* bypassParameter = nullptr;

        Slider delaySlider;
        Slider mixSlider;
        Slider regenSlider;

        Label lblDelay;
        Label lblMix;
        Label lblRegen;

        Label analogLabel;

        ToggleButton bypassButton;

        FxCommon::PedalLookAndFeel pedalLaf;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
    };

private:
    // parameters
    AudioParameterFloat* delay = nullptr;
    AudioParameterFloat* mix = nullptr;
    AudioParameterFloat* regen = nullptr;
    AudioParameterBool* bypass = nullptr;

    // internal buffer & state
    std::vector<double> delayBuffer;
    int bufferSize = 0;
    int writeIndex = 0;

    double sampleRate{ 44100.0 };

    // feedback filter
    std::vector<double> fbState;
    double lastFbCutoff{ -1.0 };
    double fbAlpha{ 1.0 };

    // constants (tunable)
    static constexpr double minDelayMilliseconds = 20.0;   // kleinste Verzögerung (ms)
    static constexpr double maxDelayMilliseconds = 650.0;  // maximale Verzögerung (ms)

    // regen scaling: reduziert die Stärke des REGEN-Parameters.
    // Damit entspricht der bisherige Wert bei 0.33 jetzt dem neuen Wert bei 1.0
    static constexpr double regenScale = 0.33;

    // update feedback lowpass (cutoff mapped from regen param)
    void updateFeedbackCoeffs()
    {
        // default regen->cutoff mapping (höherer regen -> dunklerer cutoff)
        // map regen [0..1] -> cutoff [maxFc .. minFc] using scaled regen
        const double r = (regen ? static_cast<double>(*regen) * regenScale : 0.0);
        const double minFc = 800.0;
        const double maxFc = 6000.0;
        const double cutoff = maxFc * (1.0 - r) + minFc * r;
        lastFbCutoff = cutoff;
        fbAlpha = 1.0 - std::exp(-2.0 * double_Pi * cutoff / sampleRate);
        if (fbAlpha < 0.0) fbAlpha = 0.0;
        if (fbAlpha > 1.0) fbAlpha = 1.0;
    }

    void updateFeedbackCoeffsIfNeeded(double regenVal)
    {
        // regenVal is already expected to be scaled when passed in
        const double minFc = 800.0;
        const double maxFc = 6000.0;
        const double cutoff = maxFc * (1.0 - regenVal) + minFc * regenVal;
        if (std::abs(cutoff - lastFbCutoff) > 1.0)
        {
            lastFbCutoff = cutoff;
            fbAlpha = 1.0 - std::exp(-2.0 * double_Pi * cutoff / sampleRate);
            if (fbAlpha < 0.0) fbAlpha = 0.0;
            if (fbAlpha > 1.0) fbAlpha = 1.0;
        }
    }

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogDelay)
};
