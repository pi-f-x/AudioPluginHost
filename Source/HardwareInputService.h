/*
  ==============================================================================

    HardwareInputService.h
    Created: 28 Mar 2026 2:10:38pm
    Author:  motzi

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>

#include "GpioBackend.h"
#include "HardwareCalibration.h"

class HardwareInputService final : private juce::Timer
{
public:
    struct Settings
    {
        int pollIntervalMs = 10;
        float analogSmoothingAlpha = 0.2f;
        GpioBackend::Config backendConfig;
        Hardware::AnalogCalibration poti1Calibration;
        Hardware::AnalogCalibration poti2Calibration;
    };

    explicit HardwareInputService (Settings settings = Settings());
    ~HardwareInputService() override;

    bool start();
    void stop();

    bool isRunning() const { return running.load(); }

    float getPoti1() const { return poti1.load(); }
    float getPoti2() const { return poti2.load(); }

    bool getFootswitch1() const { return fs1.load(); }
    bool getFootswitch2() const { return fs2.load(); }
    bool getFootswitch3() const { return fs3.load(); }

    void setLedStates (bool led1On, bool led2On, bool led3On);

    void setPoti1Calibration (const Hardware::AnalogCalibration& calibration);
    void setPoti2Calibration (const Hardware::AnalogCalibration& calibration);

private:
    void timerCallback() override;

    Settings settings;
    GpioBackend backend;

    std::atomic<bool> running { false };

    std::atomic<float> poti1 { 0.0f };
    std::atomic<float> poti2 { 0.0f };

    std::atomic<bool> fs1 { false };
    std::atomic<bool> fs2 { false };
    std::atomic<bool> fs3 { false };

    Hardware::SmoothedAnalogValue poti1Smoother;
    Hardware::SmoothedAnalogValue poti2Smoother;

    juce::CriticalSection calibrationLock;
    Hardware::AnalogCalibration poti1Calibration;
    Hardware::AnalogCalibration poti2Calibration;
};
