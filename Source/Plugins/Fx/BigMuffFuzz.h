/*
  ==============================================================================

    BigMuffFuzz.h
    Created: 3 Dec 2025 5:38:19pm
    Author:  motzi

  ============================================================================== 
*/

#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <memory>
#include <algorithm>
#include "FxCommon.h"

//==============================================================================
// Electro-Harmonix Big Muff Pi inspired fuzz processor
// Structure and UI follow the RatDistortion.h layout and conventions.
// Controls: Sustain (gain), Tone, Volume, Bypass
//==============================================================================

class BigMuffFuzz final : public AudioProcessor
{
public:
    //==============================================================================
    BigMuffFuzz()
        : AudioProcessor(BusesProperties().withInput("Input", AudioChannelSet::mono())
            .withOutput("Output", AudioChannelSet::mono()))
    {
        addParameter(sustain = new AudioParameterFloat({ "sustain", 1 }, "Sustain", 0.0f, 1.0f, 0.6f));
        addParameter(tone = new AudioParameterFloat({ "tone", 1 }, "Tone", 0.0f, 1.0f, 0.5f));
        addParameter(volume = new AudioParameterFloat({ "volume", 1 }, "Volume", 0.0f, 1.0f, 0.8f));
        addParameter(bypass = new AudioParameterBool({ "bypass", 1 }, "Bypass", false));
    }

    //==============================================================================
    void prepareToPlay(double sampleRateIn, int /*samplesPerBlock*/) override
    {
        sampleRate = sampleRateIn;
        const int numCh = jmax(1, getTotalNumInputChannels());
        // allocate per-channel filter states
        lpState.assign(numCh, 0.0);
        hpState.assign(numCh, 0.0);
        midState.assign(numCh, 0.0);
        lastTone = -1.0;
        updateToneCoeffs();
    }

    void releaseResources() override {}

    // shared per-sample processing function (templated)
    template<typename SampleType>
    inline SampleType processSampleInternal(SampleType in, int ch)
    {
        // If bypass is enabled just return input
        if (bypass && static_cast<bool>(*bypass))
            return in;

        // 1) Input booster (pre-gain) controlled by Sustain knob
        // Adjusted mapping to be more aggressive (closer to actual Big Muff behaviour)
        // Map sustain [0..1] to dB range [-10 .. +46]
        const float sVal = *sustain;
        const float sustainDb = (sVal * 56.0f) - 10.0f; // [-10 .. +46]
        const double preGain = std::pow(10.0, sustainDb / 20.0);

        double x = static_cast<double>(in) * preGain;

        // 2) First clipping amplifier stage (fuzzy transistor / op-amp + diode behaviour)
        // Softer threshold for earlier breakup which Big Muff exhibits
        x = softDiodeClip(x, 0.75, 0.20);

        // small inter-stage attenuation/emulation of coupling caps / bias
        x *= 0.88;

        // 3) Second clipping amplifier stage (adds gain and asymmetry to emulate diode pairs)
        // stronger stage2Gain for more saturated fuzz at higher sustain
        const double stage2Gain = 2.0 + (sVal * 3.0); // more sustain = stronger second stage
        x = std::tanh(stage2Gain * x);

        // emulate diode pair clipping mix (symmetrical-ish but with soft knee)
        x = softDiodeClip(x, 0.6, 0.16) * 0.90 + x * 0.10;

        // 4) Tone stage (passive Big Muff tone-sack approx)
        // Improved passive tone approximation with explicit mid-scoop control
        updateToneIfNeeded();

        // one-pole lowpass (for lows)
        double low = lpAlpha * x + (1.0 - lpAlpha) * lpState[ch];
        lpState[ch] = low;

        // one-pole highpass implemented as x - lowpassed(x)
        double hp_in = x;
        double lowForHP = hpAlpha * hp_in + (1.0 - hpAlpha) * hpState[ch];
        hpState[ch] = lowForHP;
        double high = hp_in - lowForHP;

        // mid content approximate (band around center)
        double mid = x - (low + high);

        // Tone knob mixes low <-> high and applies mid attenuation for the characteristic scoop
        const double tVal = *tone;
        const double lowAmount = 1.0 - tVal;
        const double highAmount = tVal;

        // Mid-cut factor: peaks at center (t=0.5). Clamp to [0..1].
        double midCutFactor = 1.0 - 4.0 * std::abs(tVal - 0.5); // center -> 1.0, edges -> <=0
        midCutFactor = std::clamp(midCutFactor, 0.0, 1.0);
        // How strongly mids are cut at centre (0.85 = 85% cut)
        const double maxMidCut = 0.85;
        double midGain = 1.0 - midCutFactor * maxMidCut;

        double toneOut = low * lowAmount + high * highAmount + mid * midGain;

        // small smoothing to avoid zippering
        double y = 0.6 * toneOut + 0.4 * midState[ch];
        midState[ch] = toneOut;

        // 5) Output booster (volume)
        const float vVal = *volume;
        const float volDb = (vVal * 66.0f) - 60.0f; // [-60 .. +6]
        const double outGain = std::pow(10.0, volDb / 20.0);

        double out = y * outGain;

        // final soft limiting to avoid digital clipping but preserve fuzz texture
        out = std::tanh(out * 8.0);

        return static_cast<SampleType>(out);
    }

    void processBlock(AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        if (bypass && static_cast<bool>(*bypass))
            return;

        const int numCh = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

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
    AudioProcessorEditor* createEditor() override { return new Editor(*this, sustain, tone, volume, bypass); }
    bool hasEditor() const override { return true; }

    //==============================================================================
    const String getName() const override { return "BigMuff"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }

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
        stream.writeFloat(*sustain);
        stream.writeFloat(*tone);
        stream.writeFloat(*volume);
        stream.writeFloat(static_cast<float>(bypass ? static_cast<float>(*bypass) : 0.0f));
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
        sustain->setValueNotifyingHost(stream.readFloat());
        tone->setValueNotifyingHost(stream.readFloat());
        volume->setValueNotifyingHost(stream.readFloat());
        if (bypass)
            bypass->setValueNotifyingHost(stream.readFloat());
        updateToneCoeffs();
    }

    //==============================================================================
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        const auto& mainInLayout = layouts.getChannelSet(true, 0);
        const auto& mainOutLayout = layouts.getChannelSet(false, 0);

        return (mainInLayout == mainOutLayout && (!mainInLayout.isDisabled()));
    }

    //==============================================================================
    // Editor: GUI implementation inspired by the Big Muff layout and FxCommon::PedalLookAndFeel
    class Editor final : public AudioProcessorEditor,
                         private Slider::Listener,
                         private Timer
    {
    public:
        Editor(BigMuffFuzz& p,
               AudioParameterFloat* sustainParam,
               AudioParameterFloat* toneParam,
               AudioParameterFloat* volumeParam,
               AudioParameterBool* bypassParam)
            : AudioProcessorEditor(&p), processor(p),
              sustainParameter(sustainParam), toneParameter(toneParam), volumeParameter(volumeParam),
              bypassParameter(bypassParam)
        {
            // Use shared pedal LookAndFeel
            setLookAndFeel(&pedalLaf);

            // pedal-like proportions
            setSize(320, 420);

            // rotary sweep similar to Rat (nice sweep)
            const float baseStart = 2.09439510239319549f;
            const float baseEnd   = -2.09439510239319549f;
            const float halfPi = static_cast<float>(double_Pi * 0.5f);
            const float startAngle = baseStart - halfPi;
            const float endAngle = baseEnd - halfPi;

            for (auto* s : { &sustainSlider, &toneSlider, &volumeSlider })
            {
                s->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
                s->setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
                s->setRange(0.0, 1.0, 0.001);
                s->setRotaryParameters(startAngle, endAngle, true);
                s->addListener(this);
                addAndMakeVisible(s);
            }

            // These knobs are mirrored along the vertical axis on the artwork:
            // physical positions: 7 o'clock => 0, 5 o'clock => 1
            // So we invert slider <-> parameter mapping for all three knobs.
            invertSustain = true;
            invertTone = true;
            invertVolume = true;

            // initialise slider positions to parameter values (apply inversion display)
            if (sustainParameter) sustainSlider.setValue(invertSustain ? (1.0f - *sustainParameter) : *sustainParameter, dontSendNotification);
            if (toneParameter)    toneSlider.setValue(invertTone    ? (1.0f - *toneParameter)    : *toneParameter,    dontSendNotification);
            if (volumeParameter)  volumeSlider.setValue(invertVolume  ? (1.0f - *volumeParameter)  : *volumeParameter,  dontSendNotification);

            // Labels
            sustainLabel.setText("SUSTAIN", dontSendNotification);
            toneLabel.setText("TONE", dontSendNotification);
            volumeLabel.setText("VOLUME", dontSendNotification);
            for (auto* l : { &sustainLabel, &toneLabel, &volumeLabel })
            {
                addAndMakeVisible(*l);
                l->setJustificationType(Justification::centred);
                // Kontrast verbessern: Labels ¸ber dem weiﬂen Feld schwarz statt weiﬂ
                l->setColour(Label::textColourId, Colours::black);
                l->setFont(Font(12.0f, Font::bold));
            }

            // Bypass footswitch overlay
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

            startTimerHz(30);
            setWantsKeyboardFocus(false);
        }

        ~Editor() override
        {
            stopTimer();
            sustainSlider.removeListener(this);
            toneSlider.removeListener(this);
            volumeSlider.removeListener(this);
            setLookAndFeel(nullptr);
        }

        void paint(Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            // metallic pedal background (silver)
            Colour metal = Colour::fromRGB(210, 210, 210);
            g.fillAll(metal.darker(0.02f));

            // draw screws / border
            g.setColour(Colours::black.withAlpha(0.25f));
            g.drawRoundedRectangle(bounds.reduced(6.0f), 6.0f, 3.0f);

            // Top control area (white background with three knobs)
            Rectangle<float> topArea = { bounds.getX() + 18.0f, bounds.getY() + 10.0f, bounds.getWidth() - 36.0f, 110.0f };
            g.setColour(Colours::white);
            g.fillRect(topArea);
            g.setColour(Colours::black);
            g.drawRect(topArea, 1.6f);

            // Big Muff logo box (red lettering area)
            Rectangle<float> logoBox = { bounds.getCentreX() - 120.0f, bounds.getCentreY() - 38.0f, 240.0f, 120.0f };
            g.setColour(Colour::fromRGB(180, 10, 10));
            g.fillRoundedRectangle(logoBox, 6.0f);
            g.setColour(Colours::white);
            Font logoFont(40.0f, Font::bold);
            g.setFont(logoFont);
            g.drawFittedText("BIG MUFF", Rectangle<int>((int)logoBox.getX(), (int)logoBox.getY(), (int)logoBox.getWidth(), (int)logoBox.getHeight()), Justification::centred, 1);

            // footswitch knob (metal)
            Point<float> footCentre(bounds.getCentreX(), bounds.getBottom() - 64.0f);
            float footR = 28.0f;
            Colour chrome = Colour::fromRGB(200, 200, 200);
            g.setColour(chrome.overlaidWith(Colours::white.withAlpha(0.14f)));
            g.fillEllipse(footCentre.x - footR, footCentre.y - footR, footR * 2.0f, footR * 2.0f);
            g.setColour(chrome.contrasting(0.45f));
            g.drawEllipse(footCentre.x - footR, footCentre.y - footR, footR * 2.0f, footR * 2.0f, 2.0f);

            // LED (red when engaged)
            bool isBypassed = (bypassParameter ? static_cast<bool>(*bypassParameter) : false);
            bool ledOn = !isBypassed;
            float ledR = 7.0f;
            Point<float> ledPos(footCentre.x, footCentre.y - 46.0f);
            g.setColour(ledOn ? Colours::red.brighter(0.0f) : Colours::darkred.darker(0.75f));
            g.fillEllipse(ledPos.x - ledR, ledPos.y - ledR, ledR * 2.0f, ledR * 2.0f);
            g.setColour(Colours::black.withAlpha(0.6f));
            g.drawEllipse(ledPos.x - ledR, ledPos.y - ledR, ledR * 2.0f, ledR * 2.0f, 1.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced(18);
            Rectangle<int> topBar = r.removeFromTop(110);

            int knobSize = 78;
            int gap = (topBar.getWidth() - (knobSize * 3)) / 4;
            // Knobs vertikal mittig in der weiﬂen Box platzieren
            int yKnob = topBar.getY() + (topBar.getHeight() - knobSize) / 2;

            sustainSlider.setBounds(topBar.getX() + gap, yKnob, knobSize, knobSize);
            toneSlider.setBounds(topBar.getX() + gap * 2 + knobSize, yKnob, knobSize, knobSize);
            volumeSlider.setBounds(topBar.getX() + gap * 3 + knobSize * 2, yKnob, knobSize, knobSize);

            // Labels innerhalb der weiﬂen Box (konstanter Abstand)
            int labelY = topBar.getY() + 2;
            sustainLabel.setBounds(sustainSlider.getX(), labelY, sustainSlider.getWidth(), 18);
            toneLabel.setBounds(toneSlider.getX(), labelY, toneSlider.getWidth(), 18);
            volumeLabel.setBounds(volumeSlider.getX(), labelY, volumeSlider.getWidth(), 18);

            int centreX = getWidth() / 2;
            int footY = getHeight() - 64;
            int btnSize = 56;
            bypassButton.setBounds(centreX - btnSize / 2, footY - btnSize / 2, btnSize, btnSize);
        }

    private:
        // Timer: keep sliders / bypass in sync with parameters
        void timerCallback() override
        {
            if (sustainParameter && toneParameter && volumeParameter && bypassParameter)
            {
                const float pSustain = *sustainParameter;
                const float pTone = *toneParameter;
                const float pVolume = *volumeParameter;
                const bool pBypass = static_cast<bool>(*bypassParameter);

                // display inverted if needed
                const float displaySustain = invertSustain ? (1.0f - pSustain) : pSustain;
                const float displayTone = invertTone ? (1.0f - pTone) : pTone;
                const float displayVolume = invertVolume ? (1.0f - pVolume) : pVolume;

                if (std::abs((float)sustainSlider.getValue() - displaySustain) > 0.0005f)
                    sustainSlider.setValue(displaySustain, dontSendNotification);
                if (std::abs((float)toneSlider.getValue() - displayTone) > 0.0005f)
                    toneSlider.setValue(displayTone, dontSendNotification);
                if (std::abs((float)volumeSlider.getValue() - displayVolume) > 0.0005f)
                    volumeSlider.setValue(displayVolume, dontSendNotification);

                if (bypassButton.getToggleState() != pBypass)
                    bypassButton.setToggleState(pBypass, dontSendNotification);

                repaint();
            }
        }

        void sliderValueChanged(Slider* s) override
        {
            if (!isVisible())
                return;

            if (s == &sustainSlider && sustainParameter)
            {
                const float sliderVal = static_cast<float>(sustainSlider.getValue());
                sustainParameter->setValueNotifyingHost(invertSustain ? (1.0f - sliderVal) : sliderVal);
            }
            else if (s == &toneSlider && toneParameter)
            {
                const float sliderVal = static_cast<float>(toneSlider.getValue());
                toneParameter->setValueNotifyingHost(invertTone ? (1.0f - sliderVal) : sliderVal);
            }
            else if (s == &volumeSlider && volumeParameter)
            {
                const float sliderVal = static_cast<float>(volumeSlider.getValue());
                volumeParameter->setValueNotifyingHost(invertVolume ? (1.0f - sliderVal) : sliderVal);
            }
        }

        BigMuffFuzz& processor;

        AudioParameterFloat* sustainParameter = nullptr;
        AudioParameterFloat* toneParameter = nullptr;
        AudioParameterFloat* volumeParameter = nullptr;
        AudioParameterBool* bypassParameter = nullptr;

        Slider sustainSlider;
        Slider toneSlider;
        Slider volumeSlider;

        Label sustainLabel;
        Label toneLabel;
        Label volumeLabel;

        ToggleButton bypassButton;

        // Inversion flags because artwork/knob orientation is mirrored vertically:
        // physical 7h -> logical 0, 5h -> logical 1
        bool invertSustain = false;
        bool invertTone = false;
        bool invertVolume = false;

        FxCommon::PedalLookAndFeel pedalLaf;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
    };

private:
    //==============================================================================
    AudioParameterFloat* sustain;
    AudioParameterFloat* tone;
    AudioParameterFloat* volume;
    AudioParameterBool* bypass;

    double sampleRate{ 44100.0 };

    // tone filter states per channel
    std::vector<double> lpState;
    std::vector<double> hpState;
    std::vector<double> midState;

    double lastTone{ -1.0 };
    double lpAlpha{ 1.0 };   // for lowpass
    double hpAlpha{ 1.0 };   // used to compute running low for HP
    double toneCenterFreq{ 800.0 };

    //==============================================================================
    static double softDiodeClip(double v, double threshold, double knee)
    {
        // smooth diode-like clamp around +/-threshold with softness 'knee'
        double absV = std::abs(v);
        if (absV <= threshold - knee)
            return v;
        double sign = (v >= 0.0) ? 1.0 : -1.0;
        double over = absV - (threshold - knee);
        double clampAmount = (std::tanh(over / knee)) * (absV - (threshold - knee));
        double outAbs = (threshold - knee) + clampAmount;
        return sign * outAbs;
    }

    void updateToneCoeffs()
    {
        // map tone [0..1] to a center frequency for the passive network
        const float t = tone ? *tone : 0.5f;
        // center sweep roughly 250 Hz .. 3500 Hz (Big Muff mid scoop area)
        const double minF = 250.0;
        const double maxF = 3500.0;
        toneCenterFreq = minF * std::pow(maxF / minF, t);
        // lowpass alpha for one-pole (for low content)
        lpAlpha = 1.0 - std::exp(-2.0 * double_Pi * (toneCenterFreq * 0.45) / sampleRate); // low cutoff ~ 0.45 * center
        lpAlpha = std::clamp(lpAlpha, 0.0, 1.0);
        // hpAlpha to compute highpass (we use a lowpass to subtract)
        double hpCut = toneCenterFreq * 0.9;
        hpAlpha = 1.0 - std::exp(-2.0 * double_Pi * hpCut / sampleRate);
        hpAlpha = std::clamp(hpAlpha, 0.0, 1.0);

        lastTone = toneCenterFreq;
    }

    void updateToneIfNeeded()
    {
        const float t = tone ? *tone : 0.5f;
        const double minF = 250.0;
        const double maxF = 3500.0;
        const double newCenter = minF * std::pow(maxF / minF, t);
        if (std::abs(newCenter - lastTone) > 1.0)
            updateToneCoeffs();
    }

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BigMuffFuzz)
};
