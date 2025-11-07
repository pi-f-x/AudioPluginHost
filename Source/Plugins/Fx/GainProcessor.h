/*
  ==============================================================================

    GainProcessor.h
    Created: 7 Nov 2025 10:36:05am
    Author:  motzi

  ==============================================================================
*/

#pragma once

//==============================================================================
class GainProcessor final : public AudioProcessor
{
public:

    //==============================================================================
    GainProcessor()
        : AudioProcessor(BusesProperties().withInput("Input", AudioChannelSet::stereo())
            .withOutput("Output", AudioChannelSet::stereo()))
    {
        addParameter(gain = new AudioParameterFloat({ "gain", 1 }, "Gain", 0.0f, 1.0f, 0.5f));
    }

    //==============================================================================
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}

    void processBlock(AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        buffer.applyGain(*gain);
    }

    void processBlock(AudioBuffer<double>& buffer, MidiBuffer&) override
    {
        buffer.applyGain((float)*gain);
    }

    //==============================================================================
    AudioProcessorEditor* createEditor() override { return new GenericAudioProcessorEditor(*this); }
    bool hasEditor() const override { return true; }

    //==============================================================================
    const String getName() const override { return "Overdrive"; }
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
        MemoryOutputStream(destData, true).writeFloat(*gain);
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        gain->setValueNotifyingHost(MemoryInputStream(data, static_cast<size_t> (sizeInBytes), false).readFloat());
    }

    //==============================================================================
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        const auto& mainInLayout = layouts.getChannelSet(true, 0);
        const auto& mainOutLayout = layouts.getChannelSet(false, 0);

        return (mainInLayout == mainOutLayout && (!mainInLayout.isDisabled()));
    }

private:
    //==============================================================================
    AudioParameterFloat* gain;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainProcessor)
};
