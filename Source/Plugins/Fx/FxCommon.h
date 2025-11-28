/*
  ==============================================================================

    FxCommon.h
    Created: 28 Nov 2025 10:53:50am
    Author:  motzi

  ============================================================================== 
*/

#pragma once

#include <JuceHeader.h>

namespace FxCommon
{
    // einfacher Allpass-Zustand (wurde in Phase90 verwendet)
    struct AllpassState
    {
        double x1 = 0.0;
        double y1 = 0.0;
    };

    // gemeinsames Pedal-LookAndFeel für rotierende Regler (verwendet von beiden UI)
    // Zeichnet Basiselemente 
    struct PedalLookAndFeel : public juce::LookAndFeel_V4
    {
        PedalLookAndFeel()
        {
            setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::white);
            setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colours::black);
        }

        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle, juce::Slider& /*s*/) override
        {
            const float cx = x + width * 0.5f;
            const float cy = y + height * 0.5f;
            const float radius = jmin(width, height) * 0.5f - 6.0f;
            const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

            // outer ring
            g.setColour(juce::Colours::black.brighter(0.08f));
            g.fillEllipse(cx - radius - 4.0f, cy - radius - 4.0f, (radius + 4.0f) * 2.0f, (radius + 4.0f) * 2.0f);

            // thin white outer ring
            g.setColour(juce::Colours::white);
            g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 2.2f);

            // inner knob
            g.setColour(juce::Colours::black);
            g.fillEllipse(cx - radius * 0.7f, cy - radius * 0.7f, radius * 1.4f, radius * 1.4f);

            // pointer
            juce::Path p;
            float pointerLength = radius * 0.72f;
            float px = cx + std::cos(angle) * pointerLength;
            float py = cy + std::sin(angle) * pointerLength;
            g.setColour(juce::Colours::white);
            p.startNewSubPath(cx, cy);
            p.lineTo(px, py);
            g.strokePath(p, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    };

} // namespace FxCommon
