/*
  ==============================================================================

    HardwareCalibration.h
    Created: 28 Mar 2026 2:11:20pm
    Author:  motzi

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace Hardware
{
    struct AnalogCalibration
    {
        float minRaw = 0.0f;
        float maxRaw = 1.0f;
        float deadZone = 0.0f;
        bool invert = false;
    };

    class SmoothedAnalogValue
    {
    public:
        void reset (float initialValue = 0.0f)
        {
            current = juce::jlimit (0.0f, 1.0f, initialValue);
            initialised = true;
        }

        float process (float input, float alpha)
        {
            input = juce::jlimit (0.0f, 1.0f, input);
            alpha = juce::jlimit (0.0f, 1.0f, alpha);

            if (! initialised)
            {
                reset (input);
                return current;
            }

            current += alpha * (input - current);
            current = juce::jlimit (0.0f, 1.0f, current);
            return current;
        }

        float getCurrent() const { return current; }

    private:
        float current = 0.0f;
        bool initialised = false;
    };

    float applyCalibration (float raw, const AnalogCalibration& calibration);
}
