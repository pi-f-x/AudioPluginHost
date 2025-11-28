/*
  ==============================================================================

    ChorusCE2.h
    Created: 28 Nov 2025
    Author:  motzi (generated)

  ============================================================================== 
*/

#pragma once

#include <JuceHeader.h>
#include "FxCommon.h"
#include <vector>
#include <cmath>

//==============================================================================
// ChorusCE2 - kompakter Chorus im Stil der Boss CE-2 (struktur ähnlich GainProcessor)
//==============================================================================

class ChorusCE2 final : public AudioProcessor
{
public:
    ChorusCE2()
        : AudioProcessor(BusesProperties().withInput("Input", AudioChannelSet::mono())
                                        .withOutput("Output", AudioChannelSet::mono()))
    {
        // rate default changed to geometric mean (~0.5477226 Hz) so normalized=0.5 => 12 o'clock
        addParameter(rate = new AudioParameterFloat({ "rate", 1 }, "Rate", 0.05f, 6.0f, 0.54772256f)); // Hz
        // depth default changed to 0.5 so normalized=0.5 => 12 o'clock
        addParameter(depth = new AudioParameterFloat({ "depth", 1 }, "Depth", 0.0f, 1.0f, 0.5f)); // normalized
        addParameter(bypass = new AudioParameterBool({ "bypass", 1 }, "Bypass", false));
    }

    //============================================================================== 
    void prepareToPlay(double sampleRateIn, int /*samplesPerBlock*/) override
    {
        sampleRate = sampleRateIn;

        // Delay buffer sizing: allow up to maxDelayMs + safety for interpolation
        const double maxDelayMs = maxDelayMilliseconds;
        const int maxSamples = static_cast<int>(std::ceil(maxDelayMs * 0.001 * sampleRate)) + 4;

        delayBuffer.assign( std::max(1, getTotalNumInputChannels()), std::vector<double>(maxSamples, 0.0) );
        writeIndex.assign(getTotalNumInputChannels(), 0);
        lfoPhase.assign(getTotalNumInputChannels(), 0.0);

        // base delay (static centre of modulation) - CE-2 style ~10 ms
        baseDelayMs = 10.0;

        // reset states
        for (auto& buf : delayBuffer)
            std::fill(buf.begin(), buf.end(), 0.0);

        updateLfoIncrement();
    }

    void releaseResources() override {}

    // shared per-sample processing (templated)
    template<typename SampleType>
    inline SampleType processSampleInternal(SampleType inSample, int ch)
    {
        // Bypass
        if (bypass && static_cast<bool>(*bypass))
            return inSample;

        // write input into circular delay buffer
        auto& buf = delayBuffer[ch];
        const int bufSize = static_cast<int>(buf.size());
        int w = writeIndex[ch];
        buf[w] = static_cast<double>(inSample);

        // compute LFO (sine) - per channel phase offset to simulate stereo-like spread if needed
        double phase = lfoPhase[ch];
        double lfo = std::sin(phase); // in [-1..1]

        // mapped depth: map depth param [0..1] to modulation amplitude in ms
        const double depthVal = static_cast<double>(*depth);
        const double modMs = depthVal * maxModMs; // e.g. up to ~6 ms

        // total delay in samples = base + mod
        const double delayMs = baseDelayMs + (lfo * modMs * 0.5); // symmetric around base
        const double delaySamples = (delayMs * 0.001) * sampleRate;

        // read position (fractional)
        double readPos = static_cast<double>(w) - delaySamples;
        while (readPos < 0.0) readPos += bufSize;

        // integer + frac
        int i1 = static_cast<int>(std::floor(readPos));
        int i2 = (i1 + 1) % bufSize;
        double frac = readPos - std::floor(readPos);

        // linear interpolation (simple, low cost; suitable for chorus)
        double delayed = (1.0 - frac) * buf[i1] + frac * buf[i2];

        // advance write index and LFO phase
        if (++w >= bufSize) w = 0;
        writeIndex[ch] = w;

        const double phaseInc = lfoInc; // precomputed from rate parameter
        phase += phaseInc;
        if (phase >= twoPi) phase -= twoPi;
        lfoPhase[ch] = phase;

        // mix dry and wet. Depth also influences wet level for a more natural control:
        const double wetLevel = 0.6 * depthVal; // wet scaled by depth
        const double dryLevel = 1.0 - wetLevel;

        double out = dryLevel * static_cast<double>(inSample) + wetLevel * delayed;

        // gentle output limiting
        out = std::tanh(out * 4.0) / 4.0;

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
                data[i] = processSampleInternal<float>(data[i], ch);
        }

        // keep LFO increment in sync if rate parameter changed
        updateLfoIncrement();
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
                data[i] = processSampleInternal<double>(data[i], ch);
        }

        updateLfoIncrement();
    }

    //==============================================================================
    AudioProcessorEditor* createEditor() override { return new Editor(*this, rate, depth, bypass); }
    bool hasEditor() const override { return true; }

    //==============================================================================
    const String getName() const override { return "Chorus CE-2"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }

    //==============================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const String getProgramName(int) override { return {}; }
    void changeProgramName(int, const String&) override {}

    //==============================================================================
    void getStateInformation(MemoryBlock& destData) override
    {
        MemoryOutputStream stream(destData, true);
        stream.writeFloat(*rate);
        stream.writeFloat(*depth);
        stream.writeFloat(static_cast<float>(bypass ? static_cast<float>(*bypass) : 0.0f));
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
        // stored value is the real rate in Hz; convert to normalized before setting
        if (rate)
        {
            const float storedRate = stream.readFloat();
            const float normalized = static_cast<float>(rate->getNormalisableRange().convertTo0to1(storedRate));
            rate->setValueNotifyingHost(normalized);
        }
        else
        {
            // consume value
            (void)stream.readFloat();
        }

        if (depth)
            depth->setValueNotifyingHost(stream.readFloat());
        else
            (void)stream.readFloat();

        if (bypass)
            bypass->setValueNotifyingHost(stream.readFloat());

        updateLfoIncrement();
    }

    //==============================================================================
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        const auto& mainInLayout = layouts.getChannelSet(true, 0);
        const auto& mainOutLayout = layouts.getChannelSet(false, 0);

        return (mainInLayout == mainOutLayout && (!mainInLayout.isDisabled()));
    }

    //==============================================================================
    // Editor: GUI implementation, anpassung an Bild (Rate, Depth, Fußschalter + LED)
    class Editor final : public AudioProcessorEditor,
                         private Slider::Listener,
                         private Timer
    {
    public:
        Editor(ChorusCE2& p,
               AudioParameterFloat* rateParam,
               AudioParameterFloat* depthParam,
               AudioParameterBool* bypassParam)
            : AudioProcessorEditor(&p), processor(p),
              rateParameter(rateParam), depthParameter(depthParam), bypassParameter(bypassParam)
        {
            setLookAndFeel(&pedalLaf);

            // Pedal proportions similar to reference
            setSize(260, 360);

            // rotary setup - reuse pedal look and set range
            for (auto* s : { &rateSlider, &depthSlider })
            {
                s->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
                s->setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
                s->setRange(0.0, 1.0, 0.001);
                // Make the rotary arc symmetric around -pi/2 so normalized=0.5 points to 12 o'clock.
                // start = -2.0943951 - 1.5707963  (~ -3.66519), end = 2.0943951 - 1.5707963 (~ 0.523599)
                s->setRotaryParameters(-2.0943951f - 1.5707963f, 2.0943951f - 1.5707963f, true);
                s->addListener(this);
                addAndMakeVisible(s);
            }

            // Map parameter values to slider positions without notifications
            rateSlider.setValue(rateParameter ? normalizedFromRate(*rateParameter) : 0.0, dontSendNotification);
            depthSlider.setValue(depthParameter ? *depthParameter : 0.0, dontSendNotification);

            // Labels
            addAndMakeVisible(rateLabel);
            addAndMakeVisible(depthLabel);
            rateLabel.setText("RATE", dontSendNotification);
            depthLabel.setText("DEPTH", dontSendNotification);
            rateLabel.setJustificationType(Justification::centred);
            depthLabel.setJustificationType(Justification::centred);
            rateLabel.setColour(Label::textColourId, Colours::black);
            depthLabel.setColour(Label::textColourId, Colours::black);
            rateLabel.setFont(Font(12.0f, Font::bold));
            depthLabel.setFont(Font(12.0f, Font::bold));

            // Chorus name label that must appear above footswitch & LED
            addAndMakeVisible(chorusLabel);
            chorusLabel.setText("Chorus CE-2", dontSendNotification);
            chorusLabel.setJustificationType(Justification::centred);
            chorusLabel.setColour(Label::textColourId, Colours::black);
            // vergrößerte Schrift, Label wird jetzt im oberen Header platziert
            chorusLabel.setFont(Font(20.0f, Font::bold));

            // Bypass footswitch (invisible button over painted footswitch)
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

            startTimerHz(30); // parameter polling for UI sync
        }

        ~Editor() override
        {
            stopTimer();
            rateSlider.removeListener(this);
            depthSlider.removeListener(this);
            setLookAndFeel(nullptr);
        }

        void paint(Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            g.fillAll(Colours::lightblue.brighter(0.16f));

            // Pedal front panel
            Rectangle<float> body = bounds.reduced(10.0f);
            g.setColour(Colour::fromRGB(132, 201, 233)); // CE-2 blue
            g.fillRoundedRectangle(body, 6.0f);

            // Header box (kept minimal)
            Rectangle<float> top = body.removeFromTop(84.0f).reduced(12.0f);
            g.setColour(Colour::fromRGB(180, 230, 245));
            g.fillRoundedRectangle(top, 4.0f);

            // Footswitch area - use cached centre computed in resized()
            Point<float> footCentre = footCentreCached;
            float footR = 28.0f;
            Colour metal = Colour::fromRGB(200, 200, 200);
            g.setColour(metal.overlaidWith(Colours::white.withAlpha(0.15f)));
            g.fillEllipse(footCentre.x - footR, footCentre.y - footR, footR * 2.0f, footR * 2.0f);
            g.setColour(metal.contrasting(0.45f));
            g.drawEllipse(footCentre.x - footR, footCentre.y - footR, footR * 2.0f, footR * 2.0f, 2.0f);

            // LED (red when effect engaged i.e. bypass == false)
            bool isBypassed = (bypassParameter ? static_cast<bool>(*bypassParameter) : false);
            bool ledOn = !isBypassed;
            float ledR = 6.0f;
            Point<float> ledPos(footCentre.x, footCentre.y - 46.0f);
            g.setColour(ledOn ? Colours::red.brighter(0.0f) : Colours::darkred.darker(0.6f));
            g.fillEllipse(ledPos.x - ledR, ledPos.y - ledR, ledR * 2.0f, ledR * 2.0f);
            g.setColour(Colours::black.withAlpha(0.6f));
            g.drawEllipse(ledPos.x - ledR, ledPos.y - ledR, ledR * 2.0f, ledR * 2.0f, 1.0f);

            // subtle border
            g.setColour(Colours::black.withAlpha(0.2f));
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 6.0f, 2.0f);
        }

        void resized() override
        {
            // Layout now:
            // 1) top header (chorusLabel placed here, größer)
            // 2) knobs area (zwischen Header/Label und Footswitch)
            // 3) footswitch area (mit LED und clickable bypass)

            auto area = getLocalBounds().reduced(18);

            // top header (chorus label goes here)
            Rectangle<int> header = area.removeFromTop(84);
            // center and enlarge the chorus label in the header
            chorusLabel.setBounds(header.getX() + 8, header.getY() + 8, header.getWidth() - 16, header.getHeight() - 16);

            // footswitch area - place at bottom now
            Rectangle<int> footArea = area.removeFromBottom(120);

            // compute and cache foot centre for paint()
            footCentreCached = Point<float>((float)getWidth() * 0.5f, (float)footArea.getCentreY() + 8.0f);

            // remaining middle area is for knobs (between header and footswitch)
            Rectangle<int> knobsArea = area; // leftover middle area

            // layout knobs similar to before but vertically centered in knobsArea
            int knobLabelH = 18;
            int knobSize = 96;
            // if the area is narrow vertically, reduce knob size to fit
            if (knobsArea.getHeight() < (knobSize + knobLabelH + 20))
                knobSize = juce::jlimit(56, 96, knobsArea.getHeight() - knobLabelH - 20);

            int gap = (knobsArea.getWidth() - (knobSize * 2)) / 3;
            int labelsY = knobsArea.getY() + 8;
            int yKnob = labelsY + knobLabelH + 6;

            // place labels and knobs in the middle section (between header and footswitch)
            rateLabel.setBounds(knobsArea.getX() + gap, labelsY, knobSize, knobLabelH);
            depthLabel.setBounds(knobsArea.getX() + gap * 2 + knobSize, labelsY, knobSize, knobLabelH);

            rateSlider.setBounds(knobsArea.getX() + gap, yKnob, knobSize, knobSize);
            depthSlider.setBounds(knobsArea.getX() + gap * 2 + knobSize, yKnob, knobSize, knobSize);

            // place bypass clickable area over footswitch centre
            int centreX = static_cast<int>(footCentreCached.x);
            int centreY = static_cast<int>(footCentreCached.y);
            int btnSize = 56;
            bypassButton.setBounds(centreX - btnSize / 2, centreY - btnSize / 2, btnSize, btnSize);
        }

    private:
        void timerCallback() override
        {
            if (rateParameter && depthParameter && bypassParameter)
            {
                // update sliders from parameters
                const float pRate = *rateParameter;
                const float pDepth = *depthParameter;
                const bool pBypass = static_cast<bool>(*bypassParameter);

                if (std::abs((float)rateSlider.getValue() - normalizedFromRate(pRate)) > 0.001f)
                    rateSlider.setValue(normalizedFromRate(pRate), dontSendNotification);
                if (std::abs((float)depthSlider.getValue() - pDepth) > 0.001f)
                    depthSlider.setValue(pDepth, dontSendNotification);

                if (bypassButton.getToggleState() != pBypass)
                    bypassButton.setToggleState(pBypass, dontSendNotification);

                repaint();
            }
        }

        void sliderValueChanged(Slider* s) override
        {
            if (!isVisible()) return;

            if (s == &rateSlider && rateParameter)
            {
                // slider stores a custom normalized value (0..1), we first map that to logical Hz
                const float logicalRate = rateFromNormalized(static_cast<float>(rateSlider.getValue()));
                // AudioParameter::setValueNotifyingHost expects a normalized 0..1 value in the parameter's range,
                // so convert the real Hz value into the parameter's normalized space.
                const float paramNormalized = static_cast<float>(rateParameter->getNormalisableRange().convertTo0to1(logicalRate));
                rateParameter->setValueNotifyingHost(paramNormalized);
            }
            else if (s == &depthSlider && depthParameter)
            {
                // depth is linear 0..1 so we can pass slider value directly (already normalized)
                const float d = static_cast<float>(depthSlider.getValue());
                const float paramNormalized = static_cast<float>(depthParameter->getNormalisableRange().convertTo0to1(d));
                depthParameter->setValueNotifyingHost(paramNormalized);
            }
        }

        static float normalizedFromRate(float rateHz)
        {
            // map rate [min..max] to normalized [0..1] for slider
            const float minR = 0.05f;
            const float maxR = 6.0f;
        #if 1
            // logarithmic-ish mapping for better control at low speeds
            float t = std::log(rateHz / minR) / std::log(maxR / minR);
            return juce::jlimit(0.0f, 1.0f, t);
        #else
            return (rateHz - minR) / (maxR - minR);
        #endif
        }

        static float rateFromNormalized(float norm)
        {
            const float minR = 0.05f;
            const float maxR = 6.0f;
            // inverse of normalizedFromRate
            float r = minR * std::pow(maxR / minR, juce::jlimit(0.0f, 1.0f, norm));
            return r;
        }

        ChorusCE2& processor;

        AudioParameterFloat* rateParameter = nullptr;
        AudioParameterFloat* depthParameter = nullptr;
        AudioParameterBool* bypassParameter = nullptr;

        Slider rateSlider;
        Slider depthSlider;

        Label rateLabel;
        Label depthLabel;

        // new label placed above footswitch & LED
        Label chorusLabel;

        ToggleButton bypassButton;

        FxCommon::PedalLookAndFeel pedalLaf;

        // cached footCentre computed in resized() for paint()
        Point<float> footCentreCached{};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
    };

private:
    // parameters
    AudioParameterFloat* rate = nullptr;
    AudioParameterFloat* depth = nullptr;
    AudioParameterBool* bypass = nullptr;

    // internal state
    double sampleRate{ 44100.0 };
    const double twoPi = 2.0 * double_Pi;

    // delay/modulation
    std::vector<std::vector<double>> delayBuffer; // per channel
    std::vector<int> writeIndex;                  // per channel
    std::vector<double> lfoPhase;                 // per channel
    double lfoInc{ 0.0 };

    double baseDelayMs{ 10.0 };   // center delay in ms
    const double maxModMs{ 6.5 }; // modulation amplitude in ms (depth * this)
    const double maxDelayMilliseconds{ 30.0 };

    void updateLfoIncrement()
    {
        const double rateHz = (rate ? static_cast<double>(*rate) : 0.8);
        lfoInc = (twoPi * rateHz) / (sampleRate > 0.0 ? sampleRate : 44100.0);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChorusCE2)
};
