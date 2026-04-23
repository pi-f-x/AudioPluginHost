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
        bool useBoardPinNumbers = true;

        int gpioLed1 = 7;
        int gpioLed2 = 11;
        int gpioLed3 = 17;

        int gpioFootswitch1 = 9;
        int gpioFootswitch2 = 13;
        int gpioFootswitch3 = 15;
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

    int gpioLed1Resolved = -1;
    int gpioLed2Resolved = -1;
    int gpioLed3Resolved = -1;

    int gpioFootswitch1Resolved = -1;
    int gpioFootswitch2Resolved = -1;
    int gpioFootswitch3Resolved = -1;
   #endif

    bool pollInputsImpl (InputState& state);
    bool setLedStatesImpl (bool led1On, bool led2On, bool led3On);
};
