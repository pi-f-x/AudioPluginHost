/*
  ==============================================================================

    FxCommon.h
    Created: 28 Nov 2025 10:53:50am
    Author:  motzi

  ============================================================================== 
*/

#pragma once

#include <JuceHeader.h>
#include <unordered_map>
#include <mutex>

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

    struct LfoDefinition
    {
        enum class Waveform
        {
            sine,
            triangle,
            square,
            saw,
            random
        };

        Waveform waveform = Waveform::sine;
        float frequencyHz = 0.5f;
        float depthPercent = 50.0f;
        float offsetPercent = 50.0f;
    };

    enum class ModulationSource
    {
        none,
        poti1,
        poti2,
        lfo,
        footswitch1,
        footswitch2,
        footswitch3
    };

    struct ParameterAssignment
    {
        ModulationSource source = ModulationSource::none;
        int lfoIndex = 0;
    };

    inline juce::String toString(ModulationSource source)
    {
        switch (source)
        {
            case ModulationSource::poti1: return "Poti1";
            case ModulationSource::poti2: return "Poti2";
            case ModulationSource::lfo: return "LFO";
            case ModulationSource::footswitch1: return "Footswitch1";
            case ModulationSource::footswitch2: return "Footswitch2";
            case ModulationSource::footswitch3: return "Footswitch3";
            default: return "None";
        }
    }

    inline ModulationSource modulationSourceFromString(const juce::String& s)
    {
        if (s == "Poti1") return ModulationSource::poti1;
        if (s == "Poti2") return ModulationSource::poti2;
        if (s == "LFO") return ModulationSource::lfo;
        if (s == "Footswitch1") return ModulationSource::footswitch1;
        if (s == "Footswitch2") return ModulationSource::footswitch2;
        if (s == "Footswitch3") return ModulationSource::footswitch3;
        return ModulationSource::none;
    }

    inline juce::String makeParameterKey(const juce::String& nodeId, const juce::String& parameterId)
    {
        return nodeId + "::" + parameterId;
    }

    class SessionModulationModel
    {
    public:
        static SessionModulationModel& instance()
        {
            static SessionModulationModel model;
            return model;
        }

        std::vector<LfoDefinition> getLfos() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return lfos;
        }

        void setLfos(const std::vector<LfoDefinition>& newLfos)
        {
            std::lock_guard<std::mutex> lock(mutex);
            lfos = newLfos;
        }

        int addDefaultLfo()
        {
            std::lock_guard<std::mutex> lock(mutex);
            lfos.push_back({ LfoDefinition::Waveform::sine, 0.5f, 50.0f, 50.0f });
            return static_cast<int>(lfos.size()) - 1;
        }

        void setAssignment(const juce::String& parameterKey, ParameterAssignment assignment)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (assignment.source == ModulationSource::none)
                assignments.erase(parameterKey);
            else
                assignments[parameterKey] = assignment;
        }

        ParameterAssignment getAssignment(const juce::String& parameterKey) const
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (auto it = assignments.find(parameterKey); it != assignments.end())
                return it->second;
            return {};
        }

        juce::ValueTree toValueTree() const
        {
            std::lock_guard<std::mutex> lock(mutex);

            juce::ValueTree root("Modulation");
            juce::ValueTree lfoRoot("Lfos");
            for (const auto& lfo : lfos)
            {
                juce::ValueTree n("Lfo");
                n.setProperty("waveform", static_cast<int>(lfo.waveform), nullptr);
                n.setProperty("frequencyHz", lfo.frequencyHz, nullptr);
                n.setProperty("depthPercent", lfo.depthPercent, nullptr);
                n.setProperty("offsetPercent", lfo.offsetPercent, nullptr);
                lfoRoot.addChild(n, -1, nullptr);
            }
            root.addChild(lfoRoot, -1, nullptr);

            juce::ValueTree mapRoot("Assignments");
            for (const auto& [key, assignment] : assignments)
            {
                juce::ValueTree n("Assignment");
                n.setProperty("key", key, nullptr);
                n.setProperty("source", toString(assignment.source), nullptr);
                n.setProperty("lfoIndex", assignment.lfoIndex, nullptr);
                mapRoot.addChild(n, -1, nullptr);
            }
            root.addChild(mapRoot, -1, nullptr);

            return root;
        }

        void fromValueTree(const juce::ValueTree& root)
        {
            if (! root.hasType("Modulation"))
                return;

            std::lock_guard<std::mutex> lock(mutex);
            lfos.clear();
            assignments.clear();

            if (auto lfoRoot = root.getChildWithName("Lfos"); lfoRoot.isValid())
            {
                for (int i = 0; i < lfoRoot.getNumChildren(); ++i)
                {
                    auto n = lfoRoot.getChild(i);
                    LfoDefinition lfo;
                    lfo.waveform = static_cast<LfoDefinition::Waveform>((int) n.getProperty("waveform", 0));
                    lfo.frequencyHz = static_cast<float>((double) n.getProperty("frequencyHz", 0.5));
                    lfo.depthPercent = static_cast<float>((double) n.getProperty("depthPercent", 50.0));
                    lfo.offsetPercent = static_cast<float>((double) n.getProperty("offsetPercent", 50.0));
                    lfos.push_back(lfo);
                }
            }

            if (auto mapRoot = root.getChildWithName("Assignments"); mapRoot.isValid())
            {
                for (int i = 0; i < mapRoot.getNumChildren(); ++i)
                {
                    auto n = mapRoot.getChild(i);
                    const auto key = n.getProperty("key").toString();
                    if (key.isEmpty())
                        continue;

                    ParameterAssignment a;
                    a.source = modulationSourceFromString(n.getProperty("source").toString());
                    a.lfoIndex = static_cast<int>(n.getProperty("lfoIndex", 0));
                    if (a.source != ModulationSource::none)
                        assignments[key] = a;
                }
            }
        }

    private:
        mutable std::mutex mutex;
        std::vector<LfoDefinition> lfos;
        std::unordered_map<juce::String, ParameterAssignment> assignments;
    };

    inline void setAssignmentFromDropdown(const juce::String& nodeId,
                                          const juce::String& parameterId,
                                          const juce::String& dropdownValue,
                                          int selectedLfoIndex = 0)
    {
        ParameterAssignment assignment;
        assignment.source = modulationSourceFromString(dropdownValue);
        assignment.lfoIndex = juce::jmax(0, selectedLfoIndex);

        SessionModulationModel::instance().setAssignment(makeParameterKey(nodeId, parameterId), assignment);
    }

    inline juce::String getDropdownValueForParameter(const juce::String& nodeId,
                                                     const juce::String& parameterId)
    {
        const auto assignment = SessionModulationModel::instance().getAssignment(makeParameterKey(nodeId, parameterId));
        return toString(assignment.source);
    }

    inline int getAssignedLfoIndexForParameter(const juce::String& nodeId,
                                               const juce::String& parameterId)
    {
        const auto assignment = SessionModulationModel::instance().getAssignment(makeParameterKey(nodeId, parameterId));
        return juce::jmax(0, assignment.lfoIndex);
    }

    inline juce::String makeRuntimeNodeId(const juce::AudioProcessor* processor)
    {
        return processor != nullptr
            ? juce::String::toHexString((juce::pointer_sized_int) processor)
            : "invalid";
    }

    inline void setupHardwareMappingButton(juce::TextButton& button)
    {
        button.setButtonText("Hardware Mapping");
    }

    inline void layoutHardwareMappingButton(juce::TextButton& button,
                                            int editorWidth,
                                            int footswitchCenterX,
                                            int footswitchCenterY,
                                            int footswitchButtonSize,
                                            int rightPadding = 10)
    {
        constexpr int mappingW = 130;
        constexpr int mappingH = 24;

        int mappingX = footswitchCenterX + (footswitchButtonSize / 2) + 18;
        if (mappingX + mappingW > editorWidth - rightPadding)
            mappingX = editorWidth - rightPadding - mappingW;

        button.setBounds(mappingX, footswitchCenterY - mappingH / 2, mappingW, mappingH);
    }

    class HardwareMappingPopup final : public juce::Component
    {
    public:
        HardwareMappingPopup()
        {
            addAndMakeVisible(closeButton);
            closeButton.setButtonText("X");
            closeButton.onClick = [this]() { close(); };
            setInterceptsMouseClicks(true, true);
        }

        void setParameters(const juce::String& nodeIdIn, juce::AudioProcessor* processor)
        {
            nodeId = nodeIdIn;
            parameterLabels.clear();
            mappingBoxes.clear();
            parameterIds.clear();

            if (processor == nullptr)
                return;

            for (auto* p : processor->getParameters())
            {
                if (p == nullptr)
                    continue;

                juce::String parameterId;
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
                    parameterId = withId->paramID;

                if (parameterId.isEmpty())
                    parameterId = p->getName(64).replaceCharacters(" ", "_").toLowerCase();

                auto* label = parameterLabels.add(new juce::Label());
                label->setText(p->getName(64), juce::dontSendNotification);
                label->setColour(juce::Label::textColourId, juce::Colours::white);
                label->setJustificationType(juce::Justification::centredLeft);
                addAndMakeVisible(label);

                auto* combo = mappingBoxes.add(new juce::ComboBox());
                combo->addItem("None", 1);

                const auto lowerName = p->getName(64).trim().toLowerCase();
                const bool isBypass = (lowerName == "bypass");

                if (isBypass)
                {
                    combo->addItem("Footswitch1", 2);
                    combo->addItem("Footswitch2", 3);
                    combo->addItem("Footswitch3", 4);
                }
                else
                {
                    combo->addItem("Poti1", 2);
                    combo->addItem("Poti2", 3);
                    combo->addItem("LFO", 4);
                }

                const auto saved = getDropdownValueForParameter(nodeId, parameterId);
                combo->setText(saved.isNotEmpty() ? saved : "None", juce::dontSendNotification);

                const int index = parameterIds.size();
                parameterIds.push_back(parameterId);
                combo->onChange = [this, index, combo]()
                {
                    if (index < 0 || index >= parameterIds.size())
                        return;
                    setAssignmentFromDropdown(nodeId, parameterIds[(size_t)index], combo->getText(), 0);
                };

                addAndMakeVisible(combo);
            }

            resized();
        }

        void open()
        {
            setVisible(true);
            setAlwaysOnTop(true);
            toFront(true);
        }

        void close()
        {
            setVisible(false);
            setAlwaysOnTop(false);
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colours::black.withAlpha(0.55f));

            auto inner = getLocalBounds().reduced(16).toFloat();
            g.setColour(juce::Colour::fromRGB(36, 36, 36));
            g.fillRoundedRectangle(inner, 10.0f);

            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.drawRoundedRectangle(inner, 10.0f, 1.5f);

            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(18.0f, juce::Font::bold));
            g.drawText("Hardware Mapping", getLocalBounds().removeFromTop(44), juce::Justification::centred);
        }

        void resized() override
        {
            closeButton.setBounds(getWidth() - 36, 8, 28, 24);

            auto content = getLocalBounds().reduced(32);
            content.removeFromTop(36);

            const int rowH = 30;
            const int gap = 8;
            const int comboW = 140;

            for (int i = 0; i < parameterLabels.size(); ++i)
            {
                auto row = content.removeFromTop(rowH);
                parameterLabels[i]->setBounds(row.removeFromLeft(juce::jmax(120, row.getWidth() - comboW - 12)));
                mappingBoxes[i]->setBounds(row.removeFromRight(comboW));
                content.removeFromTop(gap);
            }
        }

    private:
        juce::String nodeId;
        juce::TextButton closeButton;
        juce::OwnedArray<juce::Label> parameterLabels;
        juce::OwnedArray<juce::ComboBox> mappingBoxes;
        std::vector<juce::String> parameterIds;
    };

    inline void initialiseHardwareMappingUI(juce::Component& owner,
                                            juce::TextButton& button,
                                            HardwareMappingPopup& popup,
                                            juce::AudioProcessor* processor)
    {
        setupHardwareMappingButton(button);
        popup.setVisible(false);

        button.onClick = [&owner, &popup, processor]()
        {
            popup.setParameters(makeRuntimeNodeId(processor), processor);

            if (auto* topLevel = owner.getTopLevelComponent())
            {
                if (popup.getParentComponent() != topLevel)
                {
                    if (auto* currentParent = popup.getParentComponent())
                        currentParent->removeChildComponent(&popup);

                    topLevel->addAndMakeVisible(popup);
                }

                popup.setBounds(topLevel->getLocalBounds());
            }

            popup.open();
        };
    }

    inline void layoutHardwareMappingPopup(juce::Component& owner,
                                           HardwareMappingPopup& popup)
    {
        if (auto* topLevel = owner.getTopLevelComponent())
            popup.setBounds(topLevel->getLocalBounds());
        else
            popup.setBounds(owner.getLocalBounds());
    }

} // namespace FxCommon
