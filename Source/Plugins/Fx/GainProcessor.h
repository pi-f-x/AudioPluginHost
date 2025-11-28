/*
  ==============================================================================

    GainProcessor

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <memory>
#include "FxCommon.h"

//==============================================================================
// ProCo Rat inspired distortion processor
//==============================================================================

class GainProcessor final : public AudioProcessor
{
public:
    //==============================================================================
    GainProcessor()
        : AudioProcessor(BusesProperties().withInput("Input", AudioChannelSet::mono())
            .withOutput("Output", AudioChannelSet::mono()))
    {
        addParameter(drive = new AudioParameterFloat({ "drive", 1 }, "Drive", 0.0f, 1.0f, 0.5f));
        addParameter(filter = new AudioParameterFloat({ "filter", 1 }, "Filter", 0.0f, 1.0f, 0.5f));
        addParameter(volume = new AudioParameterFloat({ "volume", 1 }, "Volume", 0.0f, 1.0f, 0.8f));
        addParameter(bypass = new AudioParameterBool({ "bypass", 1 }, "Bypass", false));
    }

    //==============================================================================
    void prepareToPlay(double sampleRateIn, int /*samplesPerBlock*/) override
    {
        sampleRate = sampleRateIn;
        const int numCh = jmax(1, getTotalNumInputChannels());
        lowpassState.assign(numCh, 0.0);
        lastCutoff = -1.0;
        updateFilterCoeffs();
    }

    void releaseResources() override {}

    // shared per-sample processing function (templated)
    template<typename SampleType>
    inline SampleType processSampleInternal(SampleType in, int ch)
    {
        // 1) Pre-gain (Drive)
        // map drive [0..1] to dB range (0..+36 dB typical for RAT)
        const float driveVal = *drive;
        const float driveDb = (driveVal * 36.0f) - 6.0f; // [-6 dB .. +30 dB]
        const float preGain = std::pow(10.0f, driveDb / 20.0f);

        // apply pre-gain
        double x = static_cast<double>(in) * preGain;

        // 2) Simulate the LM308 op-amp + diode clipping to ground
        const double opAmpDrive = 2.0; // controls how hard op amp saturates
        x = std::tanh(opAmpDrive * x);

        // Diode-like clamp to ground: model threshold ~0.4..0.7; implement smooth knee
        const double diodeThresh = 0.6; // volts equivalent
        const double knee = 0.2;

        auto diodeClamp = [&](double v)
        {
            double absV = std::abs(v);
            if (absV <= diodeThresh - knee)
                return v; // linear region
            // smooth transition region
            double sign = (v >= 0.0) ? 1.0 : -1.0;
            double over = absV - (diodeThresh - knee);
            // soft approach to clamp value
            double clampAmount = (std::tanh(over / knee)) * (absV - (diodeThresh - knee));
            double outAbs = (diodeThresh - knee) + clampAmount;
            return sign * outAbs;
        };

        // mix some clipped component to produce the classic RAT character
        const double diodeMix = 0.85; // how strong the diode clamp contributes
        double clipped = diodeClamp(x);
        x = (1.0 - diodeMix) * x + diodeMix * clipped;

        // 3) Tone / Filter stage (simple 1-pole lowpass whose cutoff is set by filter knob)
        updateFilterCoeffsIfNeeded();
        // One-pole lowpass: y[n] = a * x[n] + (1-a) * y[n-1], a = 1 - exp(-2*pi*fc/fs)
        double a = lpAlpha;
        double y = a * x + (1.0 - a) * lowpassState[ch];
        lowpassState[ch] = y;

        // 4) Output Volume
        const float volVal = *volume;
        const float volDb = (volVal * 66.0f) - 60.0f; // [-60 .. +6]
        const double outGain = std::pow(10.0, volDb / 20.0);

        double out = y * outGain;

        // final soft clip to avoid digital overs (tanh limiter)
        out = std::tanh(out * 10.0); // gentle limiting

        return static_cast<SampleType>(out);
    }

    void processBlock(AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        // If bypass is enabled, pass through without processing
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
        // If bypass is enabled, pass through without processing
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
    AudioProcessorEditor* createEditor() override { return new Editor(*this, drive, filter, volume, bypass); }
    bool hasEditor() const override { return true; }

    //==============================================================================
    const String getName() const override { return "RAT"; }
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
        stream.writeFloat(*drive);
        stream.writeFloat(*filter);
        stream.writeFloat(*volume);
        // save bypass as float (0.0 / 1.0)
        stream.writeFloat(static_cast<float>(bypass ? static_cast<float>(*bypass) : 0.0f));
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
        drive->setValueNotifyingHost(stream.readFloat());
        filter->setValueNotifyingHost(stream.readFloat());
        volume->setValueNotifyingHost(stream.readFloat());
        if (bypass)
            bypass->setValueNotifyingHost(stream.readFloat());
        updateFilterCoeffs(); // ensure filter reflects restored state
    }

    //==============================================================================
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        const auto& mainInLayout = layouts.getChannelSet(true, 0);
        const auto& mainOutLayout = layouts.getChannelSet(false, 0);

        return (mainInLayout == mainOutLayout && (!mainInLayout.isDisabled()));
    }

    //==============================================================================
    // Editor: GUI implementation, visually inspired by the ProCo RAT Pedal
    class Editor final : public AudioProcessorEditor,
                         private Slider::Listener,
                         private Timer
    {
    public:
        Editor(GainProcessor& p,
               AudioParameterFloat* driveParam,
               AudioParameterFloat* filterParam,
               AudioParameterFloat* volumeParam,
               AudioParameterBool* bypassParam)
            : AudioProcessorEditor(&p), processor(p),
              driveParameter(driveParam), filterParameter(filterParam), volumeParameter(volumeParam),
              bypassParameter(bypassParam)
        {
            // Use RAT-specific LookAndFeel (adds marker points)
            setLookAndFeel(&ratLaf);

            // Base size (pedal-like proportions)
            setSize(300, 380);

            // Rotary params: base sweep kept ~240°, then rotate +90° CCW
            const float baseStart = 2.09439510239319549f;  
            const float baseEnd   = -2.09439510239319549f;   
            const float halfPi = static_cast<float>(double_Pi * 0.5f);
            const float startAngle = baseStart - halfPi;  
            const float endAngle = baseEnd - halfPi;    

            // Slider basic setup
            for (auto* s : { &distortionSlider, &filterSlider, &volumeSlider })
            {
                s->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
                s->setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
                s->setRange(0.0, 1.0, 0.001);
                s->setRotaryParameters(startAngle, endAngle, true); 
                s->addListener(this);
                addAndMakeVisible(s);
            }

            // Read initial parameter values without sending notifications
            distortionSlider.setValue(driveParameter ? *driveParameter : 0.0f, dontSendNotification);
            filterSlider.setValue(filterParameter ? *filterParameter : 0.0f, dontSendNotification);
            volumeSlider.setValue(volumeParameter ? *volumeParameter : 0.0f, dontSendNotification);

            // Labels above knobs
            addAndMakeVisible(distLabel);
            addAndMakeVisible(filtLabel);
            addAndMakeVisible(volLabel);
            distLabel.setText("DISTORTION", dontSendNotification);
            filtLabel.setText("FILTER", dontSendNotification);
            volLabel.setText("VOLUME", dontSendNotification);
            distLabel.setJustificationType(Justification::centred);
            filtLabel.setJustificationType(Justification::centred);
            volLabel.setJustificationType(Justification::centred);
            distLabel.setColour(Label::textColourId, Colours::white);
            filtLabel.setColour(Label::textColourId, Colours::white);
            volLabel.setColour(Label::textColourId, Colours::white);
            distLabel.setFont(Font(12.0f, Font::bold));
            filtLabel.setFont(Font(12.0f, Font::bold));
            volLabel.setFont(Font(12.0f, Font::bold));

            // Bypass footswitch button - invisible, placed on top of drawn footswitch
            bypassButton.setClickingTogglesState(true);
            bypassButton.setToggleState(bypassParameter ? static_cast<bool>(*bypassParameter) : false, dontSendNotification);
            bypassButton.onClick = [this]()
            {
                if (!bypassParameter)
                    return;
                bool newBypass = bypassButton.getToggleState();
                // ToggleButton stores `true` when clicked; we want `true` to mean bypass active
                bypassParameter->setValueNotifyingHost(newBypass ? 1.0f : 0.0f);
            };
            bypassButton.setColour(ToggleButton::textColourId, Colours::transparentBlack);
            bypassButton.setColour(ToggleButton::tickColourId, Colours::transparentBlack);
            addAndMakeVisible(bypassButton);

            // Ensure UI reflects parameter changes
            startTimerHz(30);

            setWantsKeyboardFocus(false);
        }

        ~Editor() override
        {
            stopTimer();
            distortionSlider.removeListener(this);
            filterSlider.removeListener(this);
            volumeSlider.removeListener(this);
            setLookAndFeel(nullptr);
        }

        void paint(Graphics& g) override
        {
            // Pedal body background (black)
            auto bounds = getLocalBounds().toFloat();
            g.fillAll(Colours::black.brighter(0.02f));
            g.setColour(Colours::black);
            g.fillRoundedRectangle(bounds.reduced(8.0f), 6.0f);

            // Top bar with white outline, divided into three fields
            Rectangle<float> topBar = { bounds.getX() + 18.0f, bounds.getY() + 10.0f, bounds.getWidth() - 36.0f, 46.0f };
            g.setColour(Colours::black);
            g.fillRect(topBar);
            g.setColour(Colours::white);
            g.drawRoundedRectangle(topBar, 3.0f, 1.5f);

            // Dividers for three label boxes
            const float thirdW = (topBar.getWidth() - 8.0f) / 3.0f;
            for (int i = 0; i < 3; ++i)
            {
                Rectangle<float> r = topBar.withX(topBar.getX() + i * (thirdW + 4.0f)).withWidth(thirdW);
                g.drawRect(r, 1.0f);
            }

            // RAT logo box
            g.setColour(Colours::white);
            Font big = Font(36.0f, Font::bold);
            g.setFont(big);
            Rectangle<float> ratBox = { bounds.getCentreX() - 90.0f, bounds.getCentreY() - 16.0f, 180.0f, 48.0f }; // moved down
            g.fillRoundedRectangle(ratBox, 4.0f);
            g.setColour(Colours::black);
            g.drawFittedText("RAT", Rectangle<int>((int)ratBox.getX(), (int)ratBox.getY(), (int)ratBox.getWidth(), (int)ratBox.getHeight()), Justification::centred, 1);

            // Footswitch (painted metallic)
            Point<float> footCentre(bounds.getCentreX(), bounds.getBottom() - 54.0f);
            float footR = 24.0f;
            Colour metal = Colour::fromRGB(200, 200, 200);
            g.setColour(metal.overlaidWith(Colours::white.withAlpha(0.15f)));
            g.fillEllipse(footCentre.x - footR, footCentre.y - footR, footR * 2.0f, footR * 2.0f);
            g.setColour(metal.contrasting(0.4f));
            g.drawEllipse(footCentre.x - footR, footCentre.y - footR, footR * 2.0f, footR * 2.0f, 2.0f);

            // LED indicator (red when effect is ON)
            bool isBypassed = (bypassParameter ? static_cast<bool>(*bypassParameter) : false);
            // LED in real RAT lights when effect is engaged; we show red when NOT bypassed
            bool ledOn = !isBypassed;
            float ledR = 8.0f;
            Point<float> ledPos(footCentre.x, footCentre.y - 48.0f);
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
            auto r = getLocalBounds().reduced(18);
            // top bar height
            int topBarH = 46;
            Rectangle<int> topBar = r.removeFromTop(topBarH);

            // place three knobs below the top bar
            int knobSize = 96;
            int gap = (r.getWidth() - (knobSize * 3)) / 4;
            int yKnob = r.getY() + 8;

            distortionSlider.setBounds(r.getX() + gap, yKnob, knobSize, knobSize);
            filterSlider.setBounds(r.getX() + gap * 2 + knobSize, yKnob, knobSize, knobSize);
            volumeSlider.setBounds(r.getX() + gap * 3 + knobSize * 2, yKnob, knobSize, knobSize);

            // labels above knobs
            distLabel.setBounds(distortionSlider.getX(), topBar.getY() + 6, distortionSlider.getWidth(), 18);
            filtLabel.setBounds(filterSlider.getX(), topBar.getY() + 6, filterSlider.getWidth(), 18);
            volLabel.setBounds(volumeSlider.getX(), topBar.getY() + 6, volumeSlider.getWidth(), 18);

            // place bypass button over the footswitch area
            int centreX = getWidth() / 2;
            int footY = getHeight() - 54;
            int btnSize = 48;
            bypassButton.setBounds(centreX - btnSize / 2, footY - btnSize / 2, btnSize, btnSize);
        }

    private:
        // Timer: poll parameters and update sliders / LED
        void timerCallback() override
        {
            if (driveParameter && filterParameter && volumeParameter && bypassParameter)
            {
                const float pDrive = *driveParameter;
                const float pFilter = *filterParameter;
                const float pVolume = *volumeParameter;
                const bool pBypass = static_cast<bool>(*bypassParameter);

                if (std::abs((float)distortionSlider.getValue() - pDrive) > 0.001f)
                    distortionSlider.setValue(pDrive, dontSendNotification);
                if (std::abs((float)filterSlider.getValue() - pFilter) > 0.001f)
                    filterSlider.setValue(pFilter, dontSendNotification);
                if (std::abs((float)volumeSlider.getValue() - pVolume) > 0.001f)
                    volumeSlider.setValue(pVolume, dontSendNotification);

                // keep ToggleButton in sync with parameter (no host notify)
                if (bypassButton.getToggleState() != pBypass)
                    bypassButton.setToggleState(pBypass, dontSendNotification);

                // repaint for LED state changes
                repaint();
            }
        }

        // Slider listener -> parameter updates (with host notification)
        void sliderValueChanged(Slider* s) override
        {
            if (!isVisible())
                return;

            if (s == &distortionSlider && driveParameter)
            {
                driveParameter->setValueNotifyingHost(static_cast<float>(distortionSlider.getValue()));
            }
            else if (s == &filterSlider && filterParameter)
            {
                filterParameter->setValueNotifyingHost(static_cast<float>(filterSlider.getValue()));
            }
            else if (s == &volumeSlider && volumeParameter)
            {
                volumeParameter->setValueNotifyingHost(static_cast<float>(volumeSlider.getValue()));
            }
        }

        GainProcessor& processor;

        // parameter pointers
        AudioParameterFloat* driveParameter = nullptr;
        AudioParameterFloat* filterParameter = nullptr;
        AudioParameterFloat* volumeParameter = nullptr;
        AudioParameterBool* bypassParameter = nullptr;

        // UI controls
        Slider distortionSlider;
        Slider filterSlider;
        Slider volumeSlider;

        Label distLabel;
        Label filtLabel;
        Label volLabel;

        ToggleButton bypassButton; // invisible clickable area for footswitch

        // RAT-specific LookAndFeel that adds marker points for better readability
        struct RATLookAndFeel : public FxCommon::PedalLookAndFeel
        {
            void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                  float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle, juce::Slider& s) override
            {
                // draw base pedal knob
                FxCommon::PedalLookAndFeel::drawRotarySlider(g, x, y, width, height, sliderPosProportional, rotaryStartAngle, rotaryEndAngle, s);

                // add small marker dots around knob for RAT readability
                const float cx = x + width * 0.5f;
                const float cy = y + height * 0.5f;
                const float radius = jmin(width, height) * 0.5f - 6.0f;
                const int marks = 9;
                const float markRadius = radius + 6.0f;
                const float markSize = 3.2f;

                g.setColour(juce::Colours::white);
                for (int i = 0; i < marks; ++i)
                {
                    float t = (marks == 1) ? 0.5f : (float)i / (marks - 1);
                    float a = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
                    float mx = cx + std::cos(a) * markRadius;
                    float my = cy + std::sin(a) * markRadius;
                    g.fillEllipse(mx - markSize * 0.5f, my - markSize * 0.5f, markSize, markSize);
                }
            }
        };

        RATLookAndFeel ratLaf;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
    };

private:
    //==============================================================================
    AudioParameterFloat* drive;
    AudioParameterFloat* filter;
    AudioParameterFloat* volume;
    AudioParameterBool* bypass;

    double sampleRate{ 44100.0 };
    std::vector<double> lowpassState;
    double lastCutoff{ -1.0 };
    double lpAlpha{ 1.0 };

    //==============================================================================
    void updateFilterCoeffs()
    {
        // Map filter param [0..1] to cutoff frequency: RAT "Filter" near mid ~= 2k
        const float fVal = *filter;
        const double minF = 475.0;
        const double maxF = 32000.0;
        const double cutoff = minF * std::pow(maxF / minF, fVal);
        lastCutoff = cutoff;
        lpAlpha = 1.0 - std::exp(-2.0 * double_Pi * cutoff / sampleRate);
        if (lpAlpha < 0.0) lpAlpha = 0.0;
        if (lpAlpha > 1.0) lpAlpha = 1.0;
    }

    void updateFilterCoeffsIfNeeded()
    {
        const float fVal = *filter;
        const double minF = 475.0;
        const double maxF = 32000.0;
        const double cutoff = minF * std::pow(maxF / minF, fVal);
        if (std::abs(cutoff - lastCutoff) > 1.0)
        {
            lastCutoff = cutoff;
            lpAlpha = 1.0 - std::exp(-2.0 * double_Pi * cutoff / sampleRate);
            if (lpAlpha < 0.0) lpAlpha = 0.0;
            if (lpAlpha > 1.0) lpAlpha = 1.0;
        }
    }

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainProcessor)
};
