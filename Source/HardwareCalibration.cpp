/*
  ==============================================================================

    HardwareCalibration.cpp
    Created: 28 Mar 2026 2:11:20pm
    Author:  motzi

  ==============================================================================
*/

#include "HardwareCalibration.h"

namespace Hardware
{
    float applyCalibration (float raw, const AnalogCalibration& calibration)
    {
        const auto minRaw = calibration.minRaw;
        const auto maxRaw = juce::jmax (minRaw + 0.000001f, calibration.maxRaw);

        float normalised = (raw - minRaw) / (maxRaw - minRaw);
        normalised = juce::jlimit (0.0f, 1.0f, normalised);

        if (calibration.invert)
            normalised = 1.0f - normalised;

        const auto dz = juce::jlimit (0.0f, 0.45f, calibration.deadZone);
        if (normalised < dz)
            normalised = 0.0f;
        else if (normalised > 1.0f - dz)
            normalised = 1.0f;
        else
            normalised = (normalised - dz) / (1.0f - 2.0f * dz);

        return juce::jlimit (0.0f, 1.0f, normalised);
    }
}
