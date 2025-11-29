/*
  ==============================================================================

    PitchShifter.h
    Created: 28 Nov 2025 1:51:55pm
    Author:  motzi

  ============================================================================== 
*/

#pragma once

#include <JuceHeader.h>
#include "FxCommon.h"
#include <vector>
#include <cmath>

// Vereinfachter PitchShifter: 1 Blend-Regler + 4 Oktav-Tasten (+2, +1, -1, -2).
// Alle Tasten können parallel aktiv sein; das Wet-Signal ist die (normierte) Summe
// der aktiven Stimmen. Aufbau und Stil orientieren sich an GainProcessor.h.
// Zusätzlich: einfache LPF + HPF auf dem Wet-Signal, um klicks/Artefakte zu reduzieren.
class PitchShifter final : public AudioProcessor
{
public:
    //==============================================================================
    PitchShifter()
        : AudioProcessor(BusesProperties().withInput("Input", AudioChannelSet::mono())
                                       .withOutput("Output", AudioChannelSet::mono()))
    {
        addParameter(blend = new AudioParameterFloat({ "blend", 1 }, "Blend", 0.0f, 1.0f, 0.5f));
        addParameter(up2 = new AudioParameterBool({ "up2", 1 }, "+2Oct", false));
        addParameter(up1 = new AudioParameterBool({ "up1", 1 }, "+1Oct", false));
        addParameter(down1 = new AudioParameterBool({ "down1", 1 }, "-1Oct", false));
        addParameter(down2 = new AudioParameterBool({ "down2", 1 }, "-2Oct", false));
        addParameter(bypass = new AudioParameterBool({ "bypass", 1 }, "Bypass", false));
    }

    //==============================================================================
    void prepareToPlay(double sampleRateIn, int /*samplesPerBlock*/) override
    {
        sampleRate = sampleRateIn;

        // Ringbuffer
        bufferLen = 4096;
        buffer.assign(bufferLen, 0.0);
        writeIndex = 0;

        readOffset = 64; // kleiner Offset, um "future-read" zu vermeiden (samples)

        voicePosUp2 = voicePosUp1 = voicePosDown1 = voicePosDown2 = static_cast<double>(bufferIdxWrap(writeIndex - readOffset));

        stepUp2 = stepUp1 = stepDown1 = stepDown2 = 1.0;

        semisUp2 = 24.0;
        semisUp1 = 12.0;
        semisDown1 = -12.0;
        semisDown2 = -24.0;

        recomputeRatios();
        updateFilterCoeffs();
    }

    void releaseResources() override {}

    //==============================================================================

    template<typename SampleType>
    inline SampleType processSampleInternal(SampleType in)
    {
        if (bypass && static_cast<bool>(*bypass))
            return in;

        // write input into ring buffer
        buffer[writeIndex] = static_cast<double>(in);
        writeIndex = bufferIdxWrap(writeIndex + 1);

        double sUp2 = 0.0, sUp1 = 0.0, sDown1 = 0.0, sDown2 = 0.0;
        int activeCount = 0;

        auto processVoice = [&](AudioParameterBool* param, double& pos, double step, double& outSample)
        {
            if (!param || !static_cast<bool>(*param))
                return;

            // ensure read head stays behind write head by at least readOffset
            const double behindPos = static_cast<double>(bufferIdxWrap(writeIndex - readOffset));
            // If pos is ahead of behindPos by too much (wrap issues), reset to behindPos
            // This prevents reading "future" samples and reduces discontinuities on toggles.
            // Note: this simple clamp reduces zipper clicks when enabling/disabling voices.
            double diff = pos - behindPos;
            if (diff > bufferLen * 0.5 || diff < -bufferLen * 0.5)
                pos = behindPos;

            outSample = readInterpolated(pos);

            pos += step;
            if (pos >= bufferLen) pos = std::fmod(pos, static_cast<double>(bufferLen));
        };

        if (static_cast<bool>(*up2)) { processVoice(up2, voicePosUp2, stepUp2, sUp2); ++activeCount; }
        if (static_cast<bool>(*up1)) { processVoice(up1, voicePosUp1, stepUp1, sUp1); ++activeCount; }
        if (static_cast<bool>(*down1)) { processVoice(down1, voicePosDown1, stepDown1, sDown1); ++activeCount; }
        if (static_cast<bool>(*down2)) { processVoice(down2, voicePosDown2, stepDown2, sDown2); ++activeCount; }

        double sum = sUp2 + sUp1 + sDown1 + sDown2;
        double wet = 0.0;
        if (activeCount > 0)
            wet = sum / static_cast<double>(activeCount);
        else
            wet = 0.0;

        // --- Filtering to reduce clicks/artifacts ---
        // 1) gentle lowpass (anti-alias / soften transients)
        double lpOut = lpfAlpha * wet + (1.0 - lpfAlpha) * wetLpState;
        wetLpState = lpOut;

        // 2) DC-blocking highpass to remove slow offsets after transients
        // y[n] = hpAlpha * (y[n-1] + x[n] - x[n-1])
        double hpOut = hpfAlpha * (wetHpState + lpOut - lastHpIn);
        wetHpState = hpOut;
        lastHpIn = lpOut;

        // blend wet/dry
        const double blendVal = static_cast<double>(*blend);
        double out = static_cast<double>(in) * (1.0 - blendVal) + hpOut * blendVal;

        // soft limit
        out = std::tanh(out * 5.0) * 0.999;

        return static_cast<SampleType>(out);
    }

    void processBlock(AudioBuffer<float>& bufferIn, MidiBuffer&) override
    {
        if (bypass && static_cast<bool>(*bypass))
            return;

        const int numSamples = bufferIn.getNumSamples();
        auto* ch0 = bufferIn.getWritePointer(0);

        for (int i = 0; i < numSamples; ++i)
            ch0[i] = processSampleInternal<float>(ch0[i]);
    }

    void processBlock(AudioBuffer<double>& bufferIn, MidiBuffer&) override
    {
        if (bypass && static_cast<bool>(*bypass))
            return;

        const int numSamples = bufferIn.getNumSamples();
        auto* ch0 = bufferIn.getWritePointer(0);

        for (int i = 0; i < numSamples; ++i)
            ch0[i] = processSampleInternal<double>(ch0[i]);
    }

    //==============================================================================
    AudioProcessorEditor* createEditor() override { return new Editor(*this, blend, up2, up1, down1, down2, bypass); }
    bool hasEditor() const override { return true; }

    //==============================================================================
    const String getName() const override { return "PitchShifter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

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
        stream.writeFloat(*blend);
        stream.writeFloat(static_cast<float>(up2 ? static_cast<float>(*up2) : 0.0f));
        stream.writeFloat(static_cast<float>(up1 ? static_cast<float>(*up1) : 0.0f));
        stream.writeFloat(static_cast<float>(down1 ? static_cast<float>(*down1) : 0.0f));
        stream.writeFloat(static_cast<float>(down2 ? static_cast<float>(*down2) : 0.0f));
        stream.writeFloat(static_cast<float>(bypass ? static_cast<float>(*bypass) : 0.0f));
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
        blend->setValueNotifyingHost(stream.readFloat());
        if (up2) up2->setValueNotifyingHost(stream.readFloat());
        if (up1) up1->setValueNotifyingHost(stream.readFloat());
        if (down1) down1->setValueNotifyingHost(stream.readFloat());
        if (down2) down2->setValueNotifyingHost(stream.readFloat());
        if (bypass) bypass->setValueNotifyingHost(stream.readFloat());
        recomputeRatios();
    }

    //==============================================================================
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        const auto& mainInLayout = layouts.getChannelSet(true, 0);
        const auto& mainOutLayout = layouts.getChannelSet(false, 0);

        return (mainInLayout == mainOutLayout && (!mainInLayout.isDisabled()));
    }

    //==============================================================================
    // Editor: kreativeres Pedal-Layout mit Trident-Graphic und LED-Status pro Oktave
    class Editor final : public AudioProcessorEditor,
                         private Slider::Listener,
                         private Button::Listener,
                         private Timer
    {
    public:
        Editor(PitchShifter& p,
               AudioParameterFloat* blendParam,
               AudioParameterBool* up2Param,
               AudioParameterBool* up1Param,
               AudioParameterBool* down1Param,
               AudioParameterBool* down2Param,
               AudioParameterBool* bypassParam)
            : AudioProcessorEditor(&p),
              processor(p),
              blendParameter(blendParam),
              up2Parameter(up2Param),
              up1Parameter(up1Param),
              down1Parameter(down1Param),
              down2Parameter(down2Param),
              bypassParameter(bypassParam)
        {
            setLookAndFeel(&pedalLaf);
            setSize(320, 420);

            const float baseStart = 2.09439510239319549f;
            const float baseEnd   = -2.09439510239319549f;
            const float halfPi = static_cast<float>(double_Pi * 0.5f);
            const float startAngle = baseStart - halfPi;
            const float endAngle = baseEnd - halfPi;

            blendSlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
            blendSlider.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
            blendSlider.setRange(0.0, 1.0, 0.001);
            blendSlider.setRotaryParameters(startAngle, endAngle, true);
            blendSlider.addListener(this);
            addAndMakeVisible(blendSlider);

            blendLabel.setText("BLEND", dontSendNotification);
            blendLabel.setJustificationType(Justification::centred);
            blendLabel.setColour(Label::textColourId, Colours::white);
            blendLabel.setFont(Font(12.0f, Font::bold));
            addAndMakeVisible(blendLabel);

            up2Button.setButtonText("+2 OCT");
            up1Button.setButtonText("+1 OCT");
            down1Button.setButtonText("-1 OCT");
            down2Button.setButtonText("-2 OCT");

            for (auto* b : { &up2Button, &up1Button, &down1Button, &down2Button })
            {
                b->setClickingTogglesState(true);
                b->setColour(TextButton::buttonColourId, Colours::darkgrey);
                b->setColour(TextButton::textColourOffId, Colours::white);
                b->addListener(this);
                addAndMakeVisible(b);
            }

            // LED small indicators
            addAndMakeVisible(ledUp2);
            addAndMakeVisible(ledUp1);
            addAndMakeVisible(ledDown1);
            addAndMakeVisible(ledDown2);

            // bypass footswitch (invisible)
            bypassToggle.setClickingTogglesState(true);
            bypassToggle.addListener(this);
            addAndMakeVisible(bypassToggle);

            if (blendParameter) blendSlider.setValue(*blendParameter, dontSendNotification);
            if (up2Parameter) up2Button.setToggleState(static_cast<bool>(*up2Parameter), dontSendNotification);
            if (up1Parameter) up1Button.setToggleState(static_cast<bool>(*up1Parameter), dontSendNotification);
            if (down1Parameter) down1Button.setToggleState(static_cast<bool>(*down1Parameter), dontSendNotification);
            if (down2Parameter) down2Button.setToggleState(static_cast<bool>(*down2Parameter), dontSendNotification);
            if (bypassParameter) bypassToggle.setToggleState(static_cast<bool>(*bypassParameter), dontSendNotification);

            startTimerHz(30);
            setWantsKeyboardFocus(false);
        }

        ~Editor() override
        {
            stopTimer();
            blendSlider.removeListener(this);
            up2Button.removeListener(this);
            up1Button.removeListener(this);
            down1Button.removeListener(this);
            down2Button.removeListener(this);
            bypassToggle.removeListener(this);
            setLookAndFeel(nullptr);
        }

        void paint(Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            g.fillAll(Colours::black.brighter(0.02f));
            g.setColour(Colours::black);
            g.fillRoundedRectangle(bounds.reduced(8.0f), 6.0f);

            // Trident graphic (stylised)
            Path trident;
            const float cx = bounds.getCentreX();
            const float top = bounds.getY() + 24.0f;
            trident.startNewSubPath(cx - 26.0f, top + 6.0f);
            trident.lineTo(cx - 4.0f, top + 6.0f);
            trident.startNewSubPath(cx - 12.0f, top + 6.0f);
            trident.lineTo(cx - 12.0f, top + 110.0f);

            trident.startNewSubPath(cx + 26.0f, top + 6.0f);
            trident.lineTo(cx + 4.0f, top + 6.0f);
            trident.startNewSubPath(cx + 12.0f, top + 6.0f);
            trident.lineTo(cx + 12.0f, top + 110.0f);

            g.setColour(Colours::red.darker(0.1f));
            g.strokePath(trident, PathStrokeType(6.0f, PathStrokeType::curved, PathStrokeType::rounded));

            // Title box
            Rectangle<float> title = { bounds.getCentreX() - 90.0f, bounds.getY() + 16.0f, 180.0f, 44.0f };
            g.setColour(Colours::white);
            g.fillRoundedRectangle(title, 4.0f);
            g.setColour(Colours::black);
            g.setFont(Font(18.0f, Font::bold));
            g.drawFittedText("PITCH FORK", Rectangle<int>((int)title.getX(), (int)title.getY(), (int)title.getWidth(), (int)title.getHeight()), Justification::centred, 1);

            // LED cluster for active voices
            // (drawn by Timer via repaint to reflect toggles)
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced(18);

            int knobSize = 110;
            int gap = (r.getWidth() - knobSize) / 2;
            int yKnob = r.getY() + 90;

            blendSlider.setBounds(r.getX() + gap, yKnob, knobSize, knobSize);
            blendLabel.setBounds(blendSlider.getX(), r.getY() + 58, blendSlider.getWidth(), 18);

            int btnW = 110;
            int btnH = 36;
            int btnX = r.getX() + (r.getWidth() - (btnW * 2 + 12)) / 2;
            int btnY = blendSlider.getBottom() + 16;

            up2Button.setBounds(btnX, btnY, btnW, btnH);
            up1Button.setBounds(btnX + btnW + 12, btnY, btnW, btnH);
            down1Button.setBounds(btnX, btnY + btnH + 10, btnW, btnH);
            down2Button.setBounds(btnX + btnW + 12, btnY + btnH + 10, btnW, btnH);

            // LEDs near buttons
            ledUp2.setBounds(up2Button.getX() + up2Button.getWidth() - 18, up2Button.getY() + 6, 12, 12);
            ledUp1.setBounds(up1Button.getX() + up1Button.getWidth() - 18, up1Button.getY() + 6, 12, 12);
            ledDown1.setBounds(down1Button.getX() + down1Button.getWidth() - 18, down1Button.getY() + 6, 12, 12);
            ledDown2.setBounds(down2Button.getX() + down2Button.getWidth() - 18, down2Button.getY() + 6, 12, 12);

            int centreX = getWidth() / 2;
            int footY = getHeight() - 54;
            int btnSize = 48;
            bypassToggle.setBounds(centreX - btnSize / 2, footY - btnSize / 2, btnSize, btnSize);
        }

    private:
        void timerCallback() override
        {
            if (!blendParameter) return;

            const float pBlend = *blendParameter;
            if (std::abs((float)blendSlider.getValue() - pBlend) > 0.001f)
                blendSlider.setValue(pBlend, dontSendNotification);

            if (up2Parameter && up2Button.getToggleState() != static_cast<bool>(*up2Parameter))
                up2Button.setToggleState(static_cast<bool>(*up2Parameter), dontSendNotification);
            if (up1Parameter && up1Button.getToggleState() != static_cast<bool>(*up1Parameter))
                up1Button.setToggleState(static_cast<bool>(*up1Parameter), dontSendNotification);
            if (down1Parameter && down1Button.getToggleState() != static_cast<bool>(*down1Parameter))
                down1Button.setToggleState(static_cast<bool>(*down1Parameter), dontSendNotification);
            if (down2Parameter && down2Button.getToggleState() != static_cast<bool>(*down2Parameter))
                down2Button.setToggleState(static_cast<bool>(*down2Parameter), dontSendNotification);

            if (bypassParameter && bypassToggle.getToggleState() != static_cast<bool>(*bypassParameter))
                bypassToggle.setToggleState(static_cast<bool>(*bypassParameter), dontSendNotification);

            // update small LED components
            ledUp2.setColour(Label::backgroundColourId, up2Button.getToggleState() ? Colours::red : Colours::darkred);
            ledUp1.setColour(Label::backgroundColourId, up1Button.getToggleState() ? Colours::orange : Colours::darkred);
            ledDown1.setColour(Label::backgroundColourId, down1Button.getToggleState() ? Colours::yellow : Colours::darkred);
            ledDown2.setColour(Label::backgroundColourId, down2Button.getToggleState() ? Colours::green : Colours::darkred);

            repaint();
        }

        void sliderValueChanged(Slider* s) override
        {
            if (s == &blendSlider && blendParameter)
                blendParameter->setValueNotifyingHost(static_cast<float>(blendSlider.getValue()));
        }

        void buttonClicked(Button* b) override
        {
            if (b == &up2Button && up2Parameter)
                up2Parameter->setValueNotifyingHost(up2Button.getToggleState() ? 1.0f : 0.0f);
            else if (b == &up1Button && up1Parameter)
                up1Parameter->setValueNotifyingHost(up1Button.getToggleState() ? 1.0f : 0.0f);
            else if (b == &down1Button && down1Parameter)
                down1Parameter->setValueNotifyingHost(down1Button.getToggleState() ? 1.0f : 0.0f);
            else if (b == &down2Button && down2Parameter)
                down2Parameter->setValueNotifyingHost(down2Button.getToggleState() ? 1.0f : 0.0f);
            else if (b == &bypassToggle && bypassParameter)
                bypassParameter->setValueNotifyingHost(bypassToggle.getToggleState() ? 1.0f : 0.0f);
        }

        PitchShifter& processor;

        AudioParameterFloat* blendParameter = nullptr;
        AudioParameterBool* up2Parameter = nullptr;
        AudioParameterBool* up1Parameter = nullptr;
        AudioParameterBool* down1Parameter = nullptr;
        AudioParameterBool* down2Parameter = nullptr;
        AudioParameterBool* bypassParameter = nullptr;

        Slider blendSlider;
        Label blendLabel;

        TextButton up2Button;
        TextButton up1Button;
        TextButton down1Button;
        TextButton down2Button;
        ToggleButton bypassToggle;

        // small LED indicators implemented as Labels (background colour)
        Label ledUp2, ledUp1, ledDown1, ledDown2;

        FxCommon::PedalLookAndFeel pedalLaf;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
    };

private:
    // parameters
    AudioParameterFloat* blend = nullptr;
    AudioParameterBool* up2 = nullptr;
    AudioParameterBool* up1 = nullptr;
    AudioParameterBool* down1 = nullptr;
    AudioParameterBool* down2 = nullptr;
    AudioParameterBool* bypass = nullptr;

    // ring buffer
    std::vector<double> buffer;
    int bufferLen = 0;
    int writeIndex = 0;
    int readOffset = 64;

    double sampleRate{ 44100.0 };

    // voice read positions
    double voicePosUp2{ 0.0 };
    double voicePosUp1{ 0.0 };
    double voicePosDown1{ 0.0 };
    double voicePosDown2{ 0.0 };

    // step ratios per voice
    double stepUp2{ 1.0 };
    double stepUp1{ 1.0 };
    double stepDown1{ 1.0 };
    double stepDown2{ 1.0 };

    // semitone values
    double semisUp2{ 24.0 };
    double semisUp1{ 12.0 };
    double semisDown1{ -12.0 };
    double semisDown2{ -24.0 };

    // --- Wet-signal filters (LPF then HPF) to remove clicking/artifacts ---
    double wetLpState{ 0.0 };
    double wetHpState{ 0.0 };
    double lastHpIn{ 0.0 };
    double lpfAlpha{ 1.0 };
    double hpfAlpha{ 0.99 };

    // desired cutoffs (internal)
    const double lpfCutHz = 8000.0; // soft lowpass to smooth transients
    const double hpfCutHz = 60.0;   // DC-block / low-frequency highpass

    //==============================================================================

    // Read with linear interpolation from circular buffer
    double readInterpolated(double& pos)
    {
        if (pos < 0.0) pos += std::ceil(std::abs(pos / bufferLen)) * bufferLen;
        if (pos >= bufferLen) pos = std::fmod(pos, static_cast<double>(bufferLen));

        const int i1 = static_cast<int>(std::floor(pos)) % bufferLen;
        const int i2 = (i1 + 1) % bufferLen;
        const double frac = pos - std::floor(pos);
        const double s1 = buffer[i1];
        const double s2 = buffer[i2];
        return s1 + frac * (s2 - s1);
    }

    // Wrap index into [0, bufferLen)
    int bufferIdxWrap(int idx) const
    {
        if (bufferLen <= 0) return 0;
        int i = idx % bufferLen;
        if (i < 0) i += bufferLen;
        return i;
    }

    void recomputeRatios()
    {
        stepUp2 = std::pow(2.0, semisUp2 / 12.0);
        stepUp1 = std::pow(2.0, semisUp1 / 12.0);
        stepDown1 = std::pow(2.0, semisDown1 / 12.0);
        stepDown2 = std::pow(2.0, semisDown2 / 12.0);
    }

    void updateFilterCoeffs()
    {
        // LPF coefficient like in GainProcessor: a = 1 - exp(-2*pi*fc/fs)
        if (sampleRate <= 0.0) sampleRate = 44100.0;
        lpfAlpha = 1.0 - std::exp(-2.0 * double_Pi * (lpfCutHz) / sampleRate);

        // HPF: first-order using alpha = rc / (rc + dt) where rc = 1/(2*pi*fc), dt = 1/fs
        const double dt = 1.0 / sampleRate;
        const double rc = 1.0 / (2.0 * double_Pi * hpfCutHz);
        hpfAlpha = rc / (rc + dt);
        // clamp
        if (lpfAlpha < 0.0) lpfAlpha = 0.0;
        if (lpfAlpha > 1.0) lpfAlpha = 1.0;
        if (hpfAlpha < 0.0) hpfAlpha = 0.0;
        if (hpfAlpha > 1.0) hpfAlpha = 1.0;
    }

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchShifter)
};
