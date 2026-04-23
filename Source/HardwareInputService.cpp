/*
  ==============================================================================

    HardwareInputService.cpp
    Created: 28 Mar 2026 2:10:38pm
    Author:  motzi

  ==============================================================================
*/

#include "HardwareInputService.h"
#include "Plugins/Fx/FxCommon.h"

HardwareInputService::HardwareInputService()
    : HardwareInputService (Settings {})
{
}

HardwareInputService::HardwareInputService (Settings s)
    : settings (std::move (s)),
      backend (settings.backendConfig),
      poti1Calibration (settings.poti1Calibration),
      poti2Calibration (settings.poti2Calibration)
{
}

HardwareInputService::~HardwareInputService()
{
    stop();
}

bool HardwareInputService::start()
{
    if (running.load())
        return true;

    if (! backend.initialise())
        return false;

    running.store (true);
    startTimer (juce::jmax (1, settings.pollIntervalMs));
    return true;
}

void HardwareInputService::stop()
{
    stopTimer();
    running.store (false);
    backend.shutdown();
}

void HardwareInputService::setLedStates (bool led1On, bool led2On, bool led3On)
{
    backend.setLedStates (led1On, led2On, led3On);
}

void HardwareInputService::setPoti1Calibration (const Hardware::AnalogCalibration& calibration)
{
    const juce::ScopedLock lock (calibrationLock);
    poti1Calibration = calibration;
}

void HardwareInputService::setPoti2Calibration (const Hardware::AnalogCalibration& calibration)
{
    const juce::ScopedLock lock (calibrationLock);
    poti2Calibration = calibration;
}

void HardwareInputService::timerCallback()
{
    if (! running.load())
        return;

    GpioBackend::InputState raw;
    if (! backend.pollInputs (raw))
        return;

    Hardware::AnalogCalibration c1;
    Hardware::AnalogCalibration c2;
    {
        const juce::ScopedLock lock (calibrationLock);
        c1 = poti1Calibration;
        c2 = poti2Calibration;
    }

    const auto p1Cal = Hardware::applyCalibration (raw.poti1, c1);
    const auto p2Cal = Hardware::applyCalibration (raw.poti2, c2);

    const auto alpha = juce::jlimit (0.0f, 1.0f, settings.analogSmoothingAlpha);

    const auto p1 = poti1Smoother.process (p1Cal, alpha);
    const auto p2 = poti2Smoother.process (p2Cal, alpha);

    poti1.store (p1);
    poti2.store (p2);

    fs1.store (raw.footswitch1);
    fs2.store (raw.footswitch2);
    fs3.store (raw.footswitch3);

    bool led1 = false;
    bool led2 = false;
    bool led3 = false;
    FxCommon::getRequestedHardwareLedStates(led1, led2, led3);
    backend.setLedStates (led1, led2, led3);

    FxCommon::setHardwareInputSnapshot (p1, p2,
                                        raw.footswitch1,
                                        raw.footswitch2,
                                        raw.footswitch3);
}
