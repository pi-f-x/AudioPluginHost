/*
  ==============================================================================

    Tuner.h
    Created: 30 Jan 2026 8:38:01am
    Author:  motzi

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <memory>
#include <vector>
#include <atomic>

//==============================================================================
// Chromatic Tuner - Detects pitch and displays note name and cents deviation
//==============================================================================

class ChromaticTuner final : public juce::AudioProcessor
{
public:
    //==============================================================================
    ChromaticTuner()
        : juce::AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::mono())
            .withOutput("Output", juce::AudioChannelSet::mono()))
    {
        addParameter(useFlats = new juce::AudioParameterBool({ "useflats", 1 }, "Use Flats", false));
        
        // Initialize autocorrelation buffer
        bufferSize = 8192;
        circularBuffer.resize(bufferSize, 0.0f);
        writePos = 0;
        
        detectedFrequency = 0.0f;
        detectedNote = "";
        detectedCents = 0.0f;
    }

    //==============================================================================
    void prepareToPlay(double sampleRateIn, int /*samplesPerBlock*/) override
    {
        sampleRate = sampleRateIn;
        circularBuffer.assign(bufferSize, 0.0f);
        writePos = 0;
        detectedFrequency = 0.0f;
    }

    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int numSamples = buffer.getNumSamples();
        const float* inputData = buffer.getReadPointer(0);
        
        // Analyze input signal (take mono or first channel)
        for (int i = 0; i < numSamples; ++i)
        {
            circularBuffer[writePos] = inputData[i];
            writePos = (writePos + 1) % bufferSize;
        }
        
        // Detect pitch every block
        detectPitch();
    }

    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) override
    {
        const int numSamples = buffer.getNumSamples();
        const double* inputData = buffer.getReadPointer(0);
        
        // Convert to float and analyze
        for (int i = 0; i < numSamples; ++i)
        {
            circularBuffer[writePos] = static_cast<float>(inputData[i]);
            writePos = (writePos + 1) % bufferSize;
        }
        
        detectPitch();
    }

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override { return new Editor(*this, useFlats); }
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return "Tuner"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    //==============================================================================
    // Public getters for editor
    float getDetectedFrequency() const { return detectedFrequency; }
    juce::String getDetectedNote() const { return detectedNote; }
    float getDetectedCents() const { return detectedCents; }

private:
    //==============================================================================
    // Pitch detection using autocorrelation (YIN-like algorithm)
    void detectPitch()
    {
        // Create a linear buffer from circular buffer
        std::vector<float> linearBuffer(bufferSize);
        for (int i = 0; i < bufferSize; ++i)
        {
            linearBuffer[i] = circularBuffer[(writePos + i) % bufferSize];
        }
        
        // Calculate RMS to check if signal is strong enough
        float rms = 0.0f;
        for (int i = 0; i < bufferSize; ++i)
            rms += linearBuffer[i] * linearBuffer[i];
        rms = std::sqrt(rms / bufferSize);
        
        if (rms < 0.01f) // Signal too weak
        {
            detectedFrequency = 0.0f;
            detectedNote = "";
            detectedCents = 0.0f;
            return;
        }
        
        // Autocorrelation
        const int minPeriod = static_cast<int>(sampleRate / 1200.0f); // ~82 Hz (E2)
        const int maxPeriod = static_cast<int>(sampleRate / 60.0f);   // ~60 Hz (B1)
        
        float bestCorr = 0.0f;
        int bestPeriod = 0;
        
        for (int lag = minPeriod; lag < maxPeriod && lag < bufferSize / 2; ++lag)
        {
            float corr = 0.0f;
            for (int i = 0; i < bufferSize / 2; ++i)
            {
                corr += linearBuffer[i] * linearBuffer[i + lag];
            }
            
            if (corr > bestCorr)
            {
                bestCorr = corr;
                bestPeriod = lag;
            }
        }
        
        if (bestPeriod > 0)
        {
            detectedFrequency = static_cast<float>(sampleRate / bestPeriod);
            
            // Convert frequency to note name and cents
            frequencyToNote(detectedFrequency);
        }
        else
        {
            detectedFrequency = 0.0f;
            detectedNote = "";
            detectedCents = 0.0f;
        }
    }
    
    void frequencyToNote(float frequency)
    {
        if (frequency < 20.0f || frequency > 5000.0f)
        {
            detectedNote = "";
            detectedCents = 0.0f;
            return;
        }
        
        // A4 = 440 Hz, MIDI note 69
        // Formula: n = 69 + 12 * log2(f / 440)
        float midiNote = 69.0f + 12.0f * std::log2(frequency / 440.0f);
        int nearestNote = static_cast<int>(std::round(midiNote));
        
        // Calculate cents deviation (-50 to +50)
        detectedCents = (midiNote - nearestNote) * 100.0f;
        
        // Get note name
        int noteInOctave = nearestNote % 12;
        int octave = (nearestNote / 12) - 1;
        
        bool useFlatsMode = *useFlats;
        
        // Note names (C=0, C#=1, D=2, etc.)
        static const char* noteNamesSharps[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        static const char* noteNamesFlats[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
        
        const char** noteNames = useFlatsMode ? noteNamesFlats : noteNamesSharps;
        
        detectedNote = juce::String(noteNames[noteInOctave]) + juce::String(octave);
    }

    //==============================================================================
    // Editor component
    class Editor : public juce::AudioProcessorEditor, private juce::Timer
    {
    public:
        Editor(ChromaticTuner& p, juce::AudioParameterBool* useFlatsParam)
            : juce::AudioProcessorEditor(&p), processor(p), useFlats(useFlatsParam)
        {
            setSize(400, 300);
            
            // Add button to toggle between sharps and flats
            addAndMakeVisible(toggleButton);
            toggleButton.setButtonText("Sharp #");
            toggleButton.setToggleState(!useFlatsParam->get(), juce::dontSendNotification);
            toggleButton.onClick = [this, useFlatsParam]
            {
                bool newState = !toggleButton.getToggleState();
                useFlatsParam->setValueNotifyingHost(newState ? 0.0f : 1.0f);
                toggleButton.setButtonText(newState ? "Sharp #" : "Flat b");
            };
            
            startTimerHz(30); // Update display 30 times per second
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colours::black);
            
            // Title
            g.setColour(juce::Colours::white);
            g.setFont(20.0f);
            g.drawText("CHROMATIC TUNER", getLocalBounds().removeFromTop(40), juce::Justification::centred);
            
            float freq = processor.getDetectedFrequency();
            juce::String note = processor.getDetectedNote();
            float cents = processor.getDetectedCents();
            
            if (freq > 0.0f && !note.isEmpty())
            {
                // Display note name
                g.setFont(60.0f);
                g.setColour(juce::Colours::cyan);
                g.drawText(note, getLocalBounds().withTrimmedTop(50).withTrimmedBottom(150), juce::Justification::centred);
                
                // Display frequency
                g.setFont(18.0f);
                g.setColour(juce::Colours::lightgrey);
                juce::String freqText = juce::String(freq, 1) + " Hz";
                g.drawText(freqText, getLocalBounds().withTrimmedTop(130).withTrimmedBottom(120), juce::Justification::centred);
                
                // Draw cents meter
                juce::Rectangle<float> meterArea(50.0f, 180.0f, 300.0f, 40.0f);
                
                // Background
                g.setColour(juce::Colours::darkgrey);
                g.fillRect(meterArea);
                
                // Center line
                g.setColour(juce::Colours::white);
                g.drawLine(200.0f, 180.0f, 200.0f, 220.0f, 2.0f);
                
                // Tick marks at -50, -25, 0, +25, +50
                for (int i = -50; i <= 50; i += 25)
                {
                    float x = 200.0f + (i * 100.0f / 50.0f);
                    g.drawLine(x, 215.0f, x, 220.0f, 1.0f);
                }
                
                // Cents indicator
                float centsX = 200.0f + juce::jlimit(-50.0f, 50.0f, cents) * 100.0f / 50.0f;
                
                // Color based on accuracy
                if (std::abs(cents) < 5.0f)
                    g.setColour(juce::Colours::green);
                else if (std::abs(cents) < 15.0f)
                    g.setColour(juce::Colours::yellow);
                else
                    g.setColour(juce::Colours::red);
                
                g.fillEllipse(centsX - 8.0f, 190.0f, 16.0f, 16.0f);
                
                // Cents text
                g.setColour(juce::Colours::white);
                g.setFont(16.0f);
                juce::String centsText = juce::String(cents > 0 ? "+" : "") + juce::String(cents, 0) + " cents";
                g.drawText(centsText, getLocalBounds().withTrimmedTop(230).withTrimmedBottom(20), juce::Justification::centred);
            }
            else
            {
                // No signal
                g.setFont(24.0f);
                g.setColour(juce::Colours::grey);
                g.drawText("No signal detected", getLocalBounds(), juce::Justification::centred);
            }
        }

        void resized() override
        {
            toggleButton.setBounds(getWidth() - 100, 10, 90, 25);
        }

        void timerCallback() override
        {
            // Update display
            repaint();
            
            // Update button text if parameter changed externally
            bool isFlats = useFlats->get();
            toggleButton.setButtonText(isFlats ? "Flat b" : "Sharp #");
            toggleButton.setToggleState(!isFlats, juce::dontSendNotification);
        }

    private:
        ChromaticTuner& processor;
        juce::AudioParameterBool* useFlats;
        juce::TextButton toggleButton;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
    };

    //==============================================================================
    double sampleRate = 44100.0;
    juce::AudioParameterBool* useFlats = nullptr;
    
    // Pitch detection buffers
    std::vector<float> circularBuffer;
    int bufferSize;
    int writePos;
    
    // Detection results
    std::atomic<float> detectedFrequency;
    juce::String detectedNote;
    std::atomic<float> detectedCents;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChromaticTuner)
};
