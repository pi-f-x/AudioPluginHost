/*
  ==============================================================================

    GpioBackend.h
    Created: 28 Mar 2026 2:11:03pm
    Author:  motzi

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class GpioBackend
{
public:
    struct Config
    {
        juce::String i2cDevice = "/dev/i2c-1";
        int i2cAddress = 0x48;

        int gpioLed1 = 4;
        int gpioLed2 = 17;
        int gpioLed3 = 22;

        int gpioFootswitch1 = 27;
        int gpioFootswitch2 = 23;
        int gpioFootswitch3 = 24;
    };

    struct InputState
    {
        float poti1 = 0.0f;
        float poti2 = 0.0f;
        bool footswitch1 = false;
        bool footswitch2 = false;
        bool footswitch3 = false;
        double timestampSeconds = 0.0;
    };

    GpioBackend();
    explicit GpioBackend (Config cfg);
    ~GpioBackend();

    bool initialise();
    void shutdown();
    bool isInitialised() const;

    bool pollInputs (InputState& state);
    bool setLedStates (bool led1On, bool led2On, bool led3On);

private:
    Config config;
    bool initialised = false;

   #if JUCE_LINUX
    int i2cFd = -1;
   #endif

    bool pollInputsImpl (InputState& state);
    bool setLedStatesImpl (bool led1On, bool led2On, bool led3On);
};
