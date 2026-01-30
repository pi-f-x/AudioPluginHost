/*
  ==============================================================================

    GainBoost.h
    Created: 30 Jan 2026 8:53:22am
    Author:  motzi

    MXR Micro Amp inspired gain booster plugin
    Circuit-accurate simulation based on TL061 op-amp design

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "FxCommon.h"

using namespace juce;

//==============================================================================
class GainBoostProcessor final : public AudioProcessor
{
public:
    //==============================================================================
    GainBoostProcessor()
        : AudioProcessor (BusesProperties().withInput  ("Input",  AudioChannelSet::mono())
                                           .withOutput ("Output", AudioChannelSet::mono()))
    {
        addParameter (gainParam = new AudioParameterFloat (
            { "gain", 1 }, 
            "Gain", 
            0.0f, 1.0f, 0.5f
        ));
        
        addParameter (bypassParam = new AudioParameterBool (
            { "bypass", 1 }, 
            "Bypass", 
            false
        ));
    }

    //==============================================================================
    const String getName() const override { return "Micro Amp"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==============================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const String&) override {}

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        ignoreUnused (samplesPerBlock);
        
        currentSampleRate = sampleRate;
        smoothedGain.reset (sampleRate, 0.05);
    }

    void releaseResources() override {}

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        if (bypassParam && static_cast<bool>(*bypassParam))
            return;
            
        auto totalNumInputChannels  = getTotalNumInputChannels();
        auto totalNumOutputChannels = getTotalNumOutputChannels();

        for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
            buffer.clear (i, 0, buffer.getNumSamples());

        float targetGain = calculateCircuitGain (*gainParam);
        smoothedGain.setTargetValue (targetGain);

        for (int channel = 0; channel < totalNumInputChannels; ++channel)
        {
            auto* channelData = buffer.getWritePointer (channel);
            
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                channelData[sample] *= smoothedGain.getNextValue();
            }
        }
    }

    void processBlock (AudioBuffer<double>& buffer, MidiBuffer&) override
    {
        if (bypassParam && static_cast<bool>(*bypassParam))
            return;
            
        AudioBuffer<float> floatBuffer (buffer.getNumChannels(), buffer.getNumSamples());
        
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                floatBuffer.setSample (ch, i, (float) buffer.getSample (ch, i));
        
        MidiBuffer dummyMidi;
        processBlock (floatBuffer, dummyMidi);
        
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (ch, i, (double) floatBuffer.getSample (ch, i));
    }

    //==============================================================================
    AudioProcessorEditor* createEditor() override 
    { 
        return new Editor (*this, gainParam, bypassParam); 
    }
    
    bool hasEditor() const override { return true; }

    //==============================================================================
    void getStateInformation (MemoryBlock& destData) override
    {
        MemoryOutputStream stream (destData, true);
        stream.writeFloat (*gainParam);
        stream.writeFloat (static_cast<float>(bypassParam ? static_cast<float>(*bypassParam) : 0.0f));
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        MemoryInputStream stream (data, static_cast<size_t> (sizeInBytes), false);
        gainParam->setValueNotifyingHost (stream.readFloat());
        if (bypassParam)
            bypassParam->setValueNotifyingHost (stream.readFloat());
    }

    //==============================================================================
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto& mainInLayout  = layouts.getChannelSet (true,  0);
        const auto& mainOutLayout = layouts.getChannelSet (false, 0);

        return (mainInLayout == mainOutLayout && (! mainInLayout.isDisabled()));
    }

    //==============================================================================
    // Editor: Custom GUI inspired by MXR Micro Amp pedal design
    class Editor final : public AudioProcessorEditor,
                         private Slider::Listener,
                         private Timer
    {
    public:
        Editor (GainBoostProcessor& p,
                AudioParameterFloat* gainParameter,
                AudioParameterBool* bypassParameter)
            : AudioProcessorEditor (&p), processor (p),
              gainParam (gainParameter),
              bypassParam (bypassParameter)
        {
            setLookAndFeel (&pedalLaf);
            
            setSize (240, 380);

            const float baseStart = 2.09439510239319549f;
            const float baseEnd = -2.09439510239319549f;
            const float halfPi = static_cast<float>(MathConstants<double>::pi * 0.5);
            const float startAngle = baseStart - halfPi;
            const float endAngle = baseEnd - halfPi;

            gainSlider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
            gainSlider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
            gainSlider.setRange (0.0, 1.0, 0.001);
            gainSlider.setRotaryParameters (startAngle, endAngle, true);
            gainSlider.addListener (this);
            addAndMakeVisible (gainSlider);

            if (gainParam)
                gainSlider.setValue (*gainParam, dontSendNotification);

            gainLabel.setText ("GAIN", dontSendNotification);
            gainLabel.setJustificationType (Justification::centred);
            gainLabel.setColour (Label::textColourId, Colours::black);
            gainLabel.setFont (Font (14.0f, Font::bold));
            addAndMakeVisible (gainLabel);

            bypassButton.setClickingTogglesState (true);
            bypassButton.setToggleState (bypassParam ? static_cast<bool>(*bypassParam) : false, dontSendNotification);
            bypassButton.onClick = [this]()
            {
                if (!bypassParam) return;
                bool newBypass = bypassButton.getToggleState();
                bypassParam->setValueNotifyingHost (newBypass ? 1.0f : 0.0f);
            };
            bypassButton.setColour (ToggleButton::textColourId, Colours::transparentBlack);
            bypassButton.setColour (ToggleButton::tickColourId, Colours::transparentBlack);
            addAndMakeVisible (bypassButton);

            startTimerHz (30);
            setWantsKeyboardFocus (false);
        }

        ~Editor() override
        {
            stopTimer();
            gainSlider.removeListener (this);
            setLookAndFeel (nullptr);
        }

        void paint (Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            
            Colour creamColor = Colour::fromRGB (235, 225, 205);
            g.fillAll (creamColor);

            g.setColour (Colours::black.withAlpha (0.2f));
            g.drawRoundedRectangle (bounds.reduced (4.0f), 6.0f, 2.0f);

            Rectangle<float> whiteArea = { bounds.getX() + 15.0f, bounds.getY() + 15.0f, 
                                          bounds.getWidth() - 30.0f, 140.0f };
            g.setColour (Colours::white);
            g.fillRect (whiteArea);
            g.setColour (Colours::black);
            g.drawRect (whiteArea, 1.5f);

            Rectangle<float> logoArea = { bounds.getCentreX() - 90.0f, bounds.getCentreY() - 20.0f, 
                                         180.0f, 80.0f };
            g.setColour (Colours::black);
            Font logoFont (32.0f, Font::bold);
            g.setFont (logoFont);
            g.drawFittedText ("MXR", Rectangle<int>((int)logoArea.getX(), (int)logoArea.getY(), 
                                                     (int)logoArea.getWidth(), 28), 
                              Justification::centred, 1);
            
            Font subtitleFont (18.0f, Font::plain);
            g.setFont (subtitleFont);
            g.drawFittedText ("micro amp", Rectangle<int>((int)logoArea.getX(), 
                                                          (int)logoArea.getY() + 32, 
                                                          (int)logoArea.getWidth(), 22), 
                              Justification::centred, 1);

            Point<float> footCentre (bounds.getCentreX(), bounds.getBottom() - 50.0f);
            float footR = 24.0f;
            Colour chrome = Colour::fromRGB (200, 200, 200);
            g.setColour (chrome.overlaidWith (Colours::white.withAlpha (0.14f)));
            g.fillEllipse (footCentre.x - footR, footCentre.y - footR, footR * 2.0f, footR * 2.0f);
            g.setColour (chrome.contrasting (0.45f));
            g.drawEllipse (footCentre.x - footR, footCentre.y - footR, footR * 2.0f, footR * 2.0f, 2.0f);

            bool isBypassed = (bypassParam ? static_cast<bool>(*bypassParam) : false);
            bool ledOn = !isBypassed;
            float ledR = 6.0f;
            Point<float> ledPos (footCentre.x, footCentre.y - 38.0f);
            g.setColour (ledOn ? Colours::red.brighter (0.0f) : Colours::darkred.darker (0.75f));
            g.fillEllipse (ledPos.x - ledR, ledPos.y - ledR, ledR * 2.0f, ledR * 2.0f);
            g.setColour (Colours::black.withAlpha (0.6f));
            g.drawEllipse (ledPos.x - ledR, ledPos.y - ledR, ledR * 2.0f, ledR * 2.0f, 1.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (15);
            Rectangle<int> topBar = r.removeFromTop (140);

            int knobSize = 90;
            int xKnob = topBar.getCentreX() - knobSize / 2;
            int yKnob = topBar.getY() + (topBar.getHeight() - knobSize) / 2 + 8;

            gainSlider.setBounds (xKnob, yKnob, knobSize, knobSize);
            
            gainLabel.setBounds (gainSlider.getX(), topBar.getY() + 5, 
                                gainSlider.getWidth(), 18);

            int centreX = getWidth() / 2;
            int footY = getHeight() - 50;
            int btnSize = 48;
            bypassButton.setBounds (centreX - btnSize / 2, footY - btnSize / 2, btnSize, btnSize);
        }

    private:
        void timerCallback() override
        {
            if (gainParam && bypassParam)
            {
                const float pGain = *gainParam;
                const bool pBypass = static_cast<bool>(*bypassParam);

                if (std::abs ((float)gainSlider.getValue() - pGain) > 0.0005f)
                    gainSlider.setValue (pGain, dontSendNotification);

                if (bypassButton.getToggleState() != pBypass)
                    bypassButton.setToggleState (pBypass, dontSendNotification);

                repaint();
            }
        }

        void sliderValueChanged (Slider* s) override
        {
            if (!isVisible())
                return;

            if (s == &gainSlider && gainParam)
            {
                const float sliderVal = static_cast<float>(gainSlider.getValue());
                gainParam->setValueNotifyingHost (sliderVal);
            }
        }

        GainBoostProcessor& processor;
        AudioParameterFloat* gainParam = nullptr;
        AudioParameterBool* bypassParam = nullptr;

        Slider gainSlider;
        Label gainLabel;
        ToggleButton bypassButton;

        FxCommon::PedalLookAndFeel pedalLaf;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
    };

private:
    //==============================================================================
    float calculateCircuitGain (float knobPosition) const
    {
        const float R4 = 56000.0f;
        const float R5_max = 500000.0f;
        const float R6 = 2700.0f;
        
        float R5 = knobPosition * R5_max;
        float gain = 1.0f + (R4 + R5) / R6;
        
        gain = jmin (gain, 20.0f);
        
        return gain;
    }

    //==============================================================================
    AudioParameterFloat* gainParam;
    AudioParameterBool* bypassParam;
    SmoothedValue<float> smoothedGain;
    
    double currentSampleRate = 44100.0;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainBoostProcessor)
};


