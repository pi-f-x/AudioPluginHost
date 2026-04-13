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
#include <optional>
#include <atomic>

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
            for (const auto& lfo :
                 lfos)
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

    class HardwareMappingPopup final : public juce::Component,
                                    private juce::Timer
    {
    public:
        HardwareMappingPopup()
        {
            addAndMakeVisible(closeButton);
            closeButton.setButtonText("X");
            closeButton.onClick = [this]() { close(); };

            addAndMakeVisible(backButton);
            backButton.setButtonText("Back");
            backButton.onClick = [this]()
            {
                if (currentView == View::lfoDetail)
                    showLfoGridView();
                else if (currentView == View::lfoGrid)
                    showMappingView();
            };

            addAndMakeVisible(mappingView);
            addAndMakeVisible(lfoGridView);
            addAndMakeVisible(lfoDetailView);

            lfoGridView.addAndMakeVisible(newLfoButton);
            newLfoButton.setButtonText("NEW");
            newLfoButton.onClick = [this]()
            {
                SessionModulationModel::instance().addDefaultLfo();
                lfoDeleteMode = false;
                rebuildLfoGrid();
                resized();
                repaint();
            };

            lfoGridView.addAndMakeVisible(deleteLfoButton);
            deleteLfoButton.setButtonText("DELETE");
            deleteLfoButton.onClick = [this]()
            {
                lfoDeleteMode = !lfoDeleteMode;
                deleteLfoButton.setColour(juce::TextButton::buttonColourId,
                                      lfoDeleteMode ? juce::Colours::red : juce::Colours::darkgrey);
                repaint();
            };
            deleteLfoButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);

            lfoDetailView.addAndMakeVisible(waveformCombo);
            waveformCombo.addItem("Sine", 1);
            waveformCombo.addItem("Triangle", 2);
            waveformCombo.addItem("Square", 3);
            waveformCombo.addItem("Saw", 4);
            waveformCombo.addItem("Random", 5);
            waveformCombo.onChange = [this]()
            {
                if (isUpdatingDetailControls)
                    return;

                if (auto lfo = getSelectedLfo())
                {
                    lfo->waveform = waveformFromId(waveformCombo.getSelectedId());
                    setSelectedLfo(* lfo);
                }

                waveformPreview.repaint();
                rebuildLfoGrid();
            };

            lfoDetailView.addAndMakeVisible(waveformPreview);

            setupKnob(freqKnob, "FREQ", freqValueLabel);
            setupKnob(depthKnob, "DEPTH", depthValueLabel);
            setupKnob(offsetKnob, "OFFSET", offsetValueLabel);

            freqKnob.setRange(0.0, 1.0, 0.0001);
            depthKnob.setRange(0.0, 100.0, 0.1);
            offsetKnob.setRange(0.0, 100.0, 0.1);

            freqKnob.onValueChange = [this]()
            {
                if (isUpdatingDetailControls)
                    return;
                if (auto lfo = getSelectedLfo())
                {
                    lfo->frequencyHz = frequencyFromKnob((float) freqKnob.getValue());
                    setSelectedLfo(* lfo);
                }
                updateDetailValueLabels();
                rebuildLfoGrid();
            };

            depthKnob.onValueChange = [this]()
            {
                if (isUpdatingDetailControls)
                    return;
                if (auto lfo = getSelectedLfo())
                {
                    lfo->depthPercent = (float) depthKnob.getValue();
                    setSelectedLfo(* lfo);
                }
                updateDetailValueLabels();
                rebuildLfoGrid();
            };

            offsetKnob.onValueChange = [this]()
            {
                if (isUpdatingDetailControls)
                    return;
                if (auto lfo = getSelectedLfo())
                {
                    lfo->offsetPercent = (float) offsetKnob.getValue();
                    setSelectedLfo(* lfo);
                }
                updateDetailValueLabels();
                rebuildLfoGrid();
            };

            setInterceptsMouseClicks(true, true);
            startTimerHz(30);
            showMappingView();
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
                mappingView.addAndMakeVisible(label);

                auto* combo = mappingBoxes.add(new MappingComboBox());
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

                const int index = (int) parameterIds.size();
                parameterIds.push_back(parameterId);

                combo->onOpenLfoWindow = [this, index]()
                {
                    activeMappingParameterIndex = index;
                    showLfoGridView();
                };

                combo->onChange = [this, index, combo]()
                {
                    if (index < 0 || index >= (int) parameterIds.size())
                        return;

                    combo->lfoReopenArmed = false;

                    const bool isLfo = combo->getText() == "LFO";
                    if (isLfo)
                        activeMappingParameterIndex = index;

                    setAssignmentFromDropdown(nodeId,
                              parameterIds[(size_t) index],
                              combo->getText(),
                              juce::jmax(0, selectedLfoIndex));

                    if (isLfo)
                        showLfoGridView();
                };

                mappingView.addAndMakeVisible(combo);
            }

            resized();
        }

        void open()
        {
            setVisible(true);
            setAlwaysOnTop(true);
            toFront(true);
            showMappingView();
        }

        void close()
        {
            setVisible(false);
            setAlwaysOnTop(false);
            showMappingView();
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
            g.drawText(getTitleText(), getLocalBounds().removeFromTop(44), juce::Justification::centred);

            if (currentView == View::lfoGrid && lfoDeleteMode)
            {
                g.setColour(juce::Colours::red.withAlpha(0.75f));
                g.setFont(juce::Font(14.0f, juce::Font::bold));
                g.drawText("DELETE MODE - Click LFO", getLocalBounds().removeFromTop(70), juce::Justification::centred);
            }
        }

        void resized() override
        {
            closeButton.setBounds(getWidth() - 36, 8, 28, 24);
            backButton.setBounds(getWidth() - 96, 8, 56, 24);

            auto content = getLocalBounds().reduced(32);
            content.removeFromTop(36);

            mappingView.setBounds(content);
            lfoGridView.setBounds(content);
            lfoDetailView.setBounds(content);

            layoutMappingView();
            layoutLfoGridView();
            layoutLfoDetailView();
        }

    private:
        enum class View { mapping, lfoGrid, lfoDetail };

        struct MappingComboBox final : public juce::ComboBox
        {
            std::function<void()> onOpenLfoWindow;
            bool lfoReopenArmed = false;

            void mouseDown(const juce::MouseEvent& e) override
            {
                if (getText() == "LFO")
                {
                    if (lfoReopenArmed && onOpenLfoWindow)
                    {
                        lfoReopenArmed = false;
                        onOpenLfoWindow();
                        return;
                    }

                    lfoReopenArmed = true;
                    juce::ComboBox::mouseDown(e);
                    return;
                }

                lfoReopenArmed = false;
                juce::ComboBox::mouseDown(e);
            }
        };

        struct LfoCard final : public juce::Component
        {
            LfoCard(std::function<void(bool)> onToggleAssignmentFn,
                    std::function<void()> onOpenDetailFn)
                : onToggleAssignment(std::move(onToggleAssignmentFn)),
                  onOpenDetail(std::move(onOpenDetailFn))
            {
                addAndMakeVisible(assignToggle);
                assignToggle.setClickingTogglesState(true);
                assignToggle.setButtonText({});
                assignToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::transparentBlack);
                assignToggle.onClick = [this]()
                {
                    if (onToggleAssignment)
                        onToggleAssignment(assignToggle.getToggleState());
                };
            }

            void setData(int i, const LfoDefinition& d, bool assignedForActiveParameter)
            {
                index = i;
                lfo = d;
                assignToggle.setToggleState(assignedForActiveParameter, juce::dontSendNotification);
                repaint();
            }

            void resized() override
            {
                assignToggle.setBounds(getWidth() - 34, 6, 28, 28);
            }

            void paint(juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat();
                g.setColour(juce::Colour::fromRGB(52, 52, 52));
                g.fillRoundedRectangle(r, 8.0f);
                g.setColour(juce::Colours::white.withAlpha(0.75f));
                g.drawRoundedRectangle(r, 8.0f, 1.2f);

                auto cb = juce::Rectangle<float>(assignToggle.getX(), assignToggle.getY(), assignToggle.getWidth(), assignToggle.getHeight()).toFloat();
                g.setColour(juce::Colours::white.withAlpha(0.95f));
                g.drawRoundedRectangle(cb.reduced(2.0f), 3.0f, 1.6f);
                if (assignToggle.getToggleState())
                {
                    g.setColour(juce::Colours::limegreen);
                    juce::Path tick;
                    auto t = cb.reduced(6.0f);
                    tick.startNewSubPath(t.getX(), t.getCentreY());
                    tick.lineTo(t.getCentreX() - 1.0f, t.getBottom());
                    tick.lineTo(t.getRight(), t.getY());
                    g.strokePath(tick, juce::PathStrokeType(2.4f));
                }

                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(13.0f, juce::Font::bold));
                g.drawText("LFO " + juce::String(index + 1), 8, 6, getWidth() - 46, 18, juce::Justification::centredLeft);

                const auto preview = juce::Rectangle<float>(r.getX() + 12.0f, r.getCentreY() - 22.0f, r.getWidth() - 54.0f, 44.0f);
                drawWaveform(g, preview, lfo, juce::Time::getMillisecondCounterHiRes() * 0.001);

                g.setFont(juce::Font(12.0f));
                g.drawText("Hz: " + juce::String(lfo.frequencyHz, 2), 8, getHeight() - 40, getWidth() / 3, 18, juce::Justification::centredLeft);
                g.drawText("Depth: " + juce::String(lfo.depthPercent, 0) + "%", getWidth() / 3, getHeight() - 40, getWidth() / 3, 18, juce::Justification::centredLeft);
                g.drawText("Offset: " + juce::String(lfo.offsetPercent, 0) + "%", (getWidth() * 2) / 3, getHeight() - 40, getWidth() / 3 - 8, 18, juce::Justification::centredLeft);
            }

            void mouseUp(const juce::MouseEvent& e) override
            {
                if (assignToggle.getBounds().contains(e.getPosition()))
                    return;

                if (onOpenDetail)
                    onOpenDetail();
            }

            static float waveformValue(LfoDefinition::Waveform wf, float t)
            {
                switch (wf)
                {
                    case LfoDefinition::Waveform::sine: return std::sin(t * juce::MathConstants<float>::twoPi);
                    case LfoDefinition::Waveform::triangle: return 1.0f - 4.0f * std::abs(t - 0.5f);
                    case LfoDefinition::Waveform::square: return t < 0.5f ? 1.0f : -1.0f;
                    case LfoDefinition::Waveform::saw: return (2.0f * t) - 1.0f;
                    case LfoDefinition::Waveform::random:
                    {
                        const int step = juce::jlimit(0, 15, (int) (t * 16.0f));
                        static constexpr float seq[16] = { 0.84f, -0.18f, 0.35f, -0.92f, 0.11f, 0.72f, -0.47f, 0.28f,
                                                           -0.73f, 0.64f, -0.05f, 0.51f, -0.88f, 0.22f, -0.31f, 0.94f };
                        return seq[step];
                    }
                }
                return 0.0f;
            }

            static float modulationValue(const LfoDefinition& lfo, float t)
            {
                const float wrapped = t - std::floor(t);
                const float base = waveformValue(lfo.waveform, wrapped);
                const float depth = juce::jlimit(0.0f, 1.0f, lfo.depthPercent / 100.0f);
                const float offset = juce::jlimit(-1.0f, 1.0f, (lfo.offsetPercent / 100.0f) * 2.0f - 1.0f);
                return juce::jlimit(-1.0f, 1.0f, base * depth + offset);
            }

            static void drawWaveform(juce::Graphics& g, juce::Rectangle<float> r, const LfoDefinition& lfo, double time)
            {
                g.setColour(juce::Colours::black.withAlpha(0.45f));
                g.fillRoundedRectangle(r, 6.0f);
                g.setColour(juce::Colours::cyan.withAlpha(0.95f));

                juce::Path p;
                const int points = 96;
                for (int i = 0; i < points; ++i)
                {
                    const float t = (float) i / (float) (points - 1);
                    const float phase = (float) std::fmod(time * juce::jmax(0.01f, lfo.frequencyHz), 1.0);
                    const float yNorm = modulationValue(lfo, t + phase);
                    const float x = r.getX() + t * r.getWidth();
                    const float y = r.getCentreY() - yNorm * (r.getHeight() * 0.42f);

                    if (i == 0) p.startNewSubPath(x, y);
                    else p.lineTo(x, y);
                }
                g.strokePath(p, juce::PathStrokeType(2.0f));
            }

            bool isAssigned = false;
            int index = -1;
            LfoDefinition lfo;
            juce::ToggleButton assignToggle;
            std::function<void(bool)> onToggleAssignment;
            std::function<void()> onOpenDetail;
        };

        struct WaveformPreviewComponent final : public juce::Component
        {
            explicit WaveformPreviewComponent(HardwareMappingPopup& o) : owner(o) {}

            void paint(juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat().reduced(8.0f);
                g.setColour(juce::Colours::black.withAlpha(0.4f));
                g.fillRoundedRectangle(r, 7.0f);
                if (auto lfo = owner.getSelectedLfo())
                    LfoCard::drawWaveform(g, r.reduced(8.0f), *lfo, juce::Time::getMillisecondCounterHiRes() * 0.001);
            }

            HardwareMappingPopup& owner;
        };

        static float frequencyFromKnob(float norm)
        {
            constexpr float minHz = 0.01f;
            constexpr float maxHz = 20.0f;
            return minHz * std::pow(maxHz / minHz, juce::jlimit(0.0f, 1.0f, norm));
        }

        static float knobFromFrequency(float hz)
        {
            constexpr float minHz = 0.01f;
            constexpr float maxHz = 20.0f;
            const float clamped = juce::jlimit(minHz, maxHz, hz);
            return std::log(clamped / minHz) / std::log(maxHz / minHz);
        }

        static int waveformToId(LfoDefinition::Waveform wf)
        {
            switch (wf)
            {
                case LfoDefinition::Waveform::sine: return 1;
                case LfoDefinition::Waveform::triangle: return 2;
                case LfoDefinition::Waveform::square: return 3;
                case LfoDefinition::Waveform::saw: return 4;
                case LfoDefinition::Waveform::random: return 5;
            }
            return 1;
        }

        static LfoDefinition::Waveform waveformFromId(int id)
        {
            switch (id)
            {
                case 2: return LfoDefinition::Waveform::triangle;
                case 3: return LfoDefinition::Waveform::square;
                case 4: return LfoDefinition::Waveform::saw;
                case 5: return LfoDefinition::Waveform::random;
                default: return LfoDefinition::Waveform::sine;
            }
        }

        std::optional<LfoDefinition> getSelectedLfo() const
        {
            const auto lfos = SessionModulationModel::instance().getLfos();
            if (selectedLfoIndex < 0 || selectedLfoIndex >= (int) lfos.size())
                return std::nullopt;
            return lfos[(size_t) selectedLfoIndex];
        }

        void setSelectedLfo(const LfoDefinition& lfo)
        {
            auto lfos = SessionModulationModel::instance().getLfos();
            if (selectedLfoIndex < 0 || selectedLfoIndex >= (int) lfos.size())
                return;
            lfos[(size_t) selectedLfoIndex] = lfo;
            SessionModulationModel::instance().setLfos(lfos);
        }

        juce::String getTitleText() const
        {
            if (currentView == View::lfoGrid) return "LFOs";
            if (currentView == View::lfoDetail) return "LFO Settings";
            return "Hardware Mapping";
        }

        void showMappingView()
        {
            activeMappingParameterIndex = -1;
            currentView = View::mapping;
            mappingView.setVisible(true);
            lfoGridView.setVisible(false);
            lfoDetailView.setVisible(false);
            closeButton.setVisible(true);
            backButton.setVisible(false);
            repaint();
        }

        void showLfoGridView()
        {
            currentView = View::lfoGrid;
            mappingView.setVisible(false);
            lfoGridView.setVisible(true);
            lfoDetailView.setVisible(false);
            closeButton.setVisible(false);
            backButton.setVisible(true);
            lfoDeleteMode = false;
            deleteLfoButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
            rebuildLfoGrid();
            resized();
            repaint();
        }

        void showLfoDetailView(int index)
        {
            selectedLfoIndex = index;
            currentView = View::lfoDetail;
            mappingView.setVisible(false);
            lfoGridView.setVisible(false);
            lfoDetailView.setVisible(true);
            closeButton.setVisible(false);
            backButton.setVisible(true);
            updateDetailControlsFromModel();
            resized();
            repaint();
        }

        void rebuildLfoGrid()
        {
            lfoCards.clear();

            int assignedIndex = -1;
            if (activeMappingParameterIndex >= 0 && activeMappingParameterIndex < (int) parameterIds.size())
            {
                const auto a = SessionModulationModel::instance().getAssignment(makeParameterKey(nodeId, parameterIds[(size_t) activeMappingParameterIndex]));
                if (a.source == ModulationSource::lfo)
                    assignedIndex = a.lfoIndex;
            }

            const auto lfos = SessionModulationModel::instance().getLfos();
            for (int i = 0; i < (int) lfos.size(); ++i)
            {
                auto* card = lfoCards.add(new LfoCard(
                    [this, i](bool shouldAssign)
                    {
                        if (lfoDeleteMode)
                            return;

                        if (activeMappingParameterIndex < 0 || activeMappingParameterIndex >= (int) parameterIds.size())
                            return;

                        const auto parameterId = parameterIds[(size_t) activeMappingParameterIndex];
                        if (!shouldAssign)
                        {
                            setAssignmentFromDropdown(nodeId, parameterId, "None", 0);
                            if (activeMappingParameterIndex >= 0 && activeMappingParameterIndex < mappingBoxes.size())
                                mappingBoxes[activeMappingParameterIndex]->setText("None", juce::dontSendNotification);
                        }
                        else
                        {
                            selectedLfoIndex = i;
                            setAssignmentFromDropdown(nodeId, parameterId, "LFO", selectedLfoIndex);
                            if (activeMappingParameterIndex >= 0 && activeMappingParameterIndex < mappingBoxes.size())
                                mappingBoxes[activeMappingParameterIndex]->setText("LFO", juce::dontSendNotification);
                        }

                        juce::MessageManager::callAsync([this]()
                        {
                            rebuildLfoGrid();
                            resized();
                            repaint();
                        });
                    },
                    [this, i]()
                    {
                        auto lfosLocal = SessionModulationModel::instance().getLfos();
                        if (i < 0 || i >= (int) lfosLocal.size())
                            return;

                        if (lfoDeleteMode)
                        {
                            lfosLocal.erase(lfosLocal.begin() + i);
                            SessionModulationModel::instance().setLfos(lfosLocal);
                            if (selectedLfoIndex >= (int) lfosLocal.size())
                                selectedLfoIndex = (int) lfosLocal.size() - 1;
                            lfoDeleteMode = false;
                            deleteLfoButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);

                            juce::MessageManager::callAsync([this]()
                            {
                                rebuildLfoGrid();
                                resized();
                                repaint();
                            });
                            return;
                        }

                        selectedLfoIndex = i;
                        showLfoDetailView(i);
                    }));

                card->setData(i, lfos[(size_t) i], i == assignedIndex);
                lfoGridView.addAndMakeVisible(card);
            }

            deleteLfoButton.setVisible(!lfos.empty());
        }

        void setupKnob(juce::Slider& knob, const juce::String& title, juce::Label& valueLabel)
        {
            lfoDetailView.addAndMakeVisible(knob);
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

            auto* titleLabel = detailTitleLabels.add(new juce::Label());
            titleLabel->setText(title, juce::dontSendNotification);
            titleLabel->setJustificationType(juce::Justification::centred);
            titleLabel->setColour(juce::Label::textColourId, juce::Colours::white);
            lfoDetailView.addAndMakeVisible(titleLabel);

            valueLabel.setJustificationType(juce::Justification::centred);
            valueLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.9f));
            lfoDetailView.addAndMakeVisible(valueLabel);
        }

        void updateDetailControlsFromModel()
        {
            auto lfo = getSelectedLfo();
            if (!lfo.has_value())
                return;

            isUpdatingDetailControls = true;
            waveformCombo.setSelectedId(waveformToId(lfo->waveform), juce::dontSendNotification);
            freqKnob.setValue(knobFromFrequency(lfo->frequencyHz), juce::dontSendNotification);
            depthKnob.setValue(lfo->depthPercent, juce::dontSendNotification);
            offsetKnob.setValue(lfo->offsetPercent, juce::dontSendNotification);
            isUpdatingDetailControls = false;

            updateDetailValueLabels();
            waveformPreview.repaint();
        }

        void updateDetailValueLabels()
        {
            auto lfo = getSelectedLfo();
            if (!lfo.has_value())
                return;

            freqValueLabel.setText(juce::String(lfo->frequencyHz, 2) + " Hz", juce::dontSendNotification);
            depthValueLabel.setText(juce::String(lfo->depthPercent, 0) + " %", juce::dontSendNotification);
            offsetValueLabel.setText(juce::String(lfo->offsetPercent, 0) + " %", juce::dontSendNotification);
        }

        void layoutMappingView()
        {
            auto content = mappingView.getLocalBounds();
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

        void layoutLfoGridView()
        {
            auto area = lfoGridView.getLocalBounds();
            auto topRow = area.removeFromTop(48);

            const auto lfos = SessionModulationModel::instance().getLfos();
            if (lfos.empty())
            {
                deleteLfoButton.setVisible(false);
                newLfoButton.setBounds(area.getCentreX() - 110, area.getCentreY() - 45, 220, 90);
                for (auto* c : lfoCards)
                    c->setVisible(false);
                return;
            }

            newLfoButton.setBounds(topRow.getX() + 8, topRow.getY() + 6, 110, 34);
            deleteLfoButton.setBounds(newLfoButton.getRight() + 10, topRow.getY() + 6, 110, 34);

            const int columns = 3;
            const int gap = 12;
            const int cardW = (area.getWidth() - (columns + 1) * gap) / columns;
            const int cardH = juce::jmax(120, (area.getHeight() - 3 * gap) / 2);

            for (int i = 0; i < lfoCards.size(); ++i)
            {
                const int row = i / columns;
                const int col = i % columns;
                const int x = area.getX() + gap + col * (cardW + gap);
                const int y = area.getY() + gap + row * (cardH + gap);
                lfoCards[i]->setVisible(true);
                lfoCards[i]->setBounds(x, y, cardW, cardH);
            }
        }

        void layoutLfoDetailView()
        {
            auto area = lfoDetailView.getLocalBounds().reduced(6);

            waveformCombo.setBounds(area.getX(), area.getY(), 180, 30);
            area.removeFromTop(40);

            waveformPreview.setBounds(area.removeFromTop(120));
            area.removeFromTop(10);

            const int knobW = 110;
            const int knobH = 110;
            const int totalW = knobW * 3 + 20 * 2;
            const int startX = area.getCentreX() - totalW / 2;
            const int y = area.getY() + 10;

            freqKnob.setBounds(startX, y, knobW, knobH);
            depthKnob.setBounds(startX + knobW + 20, y, knobW, knobH);
            offsetKnob.setBounds(startX + (knobW + 20) * 2, y, knobW, knobH);

            if (detailTitleLabels.size() >= 3)
            {
                detailTitleLabels[0]->setBounds(freqKnob.getX(), freqKnob.getY() - 22, knobW, 20);
                detailTitleLabels[1]->setBounds(depthKnob.getX(), depthKnob.getY() - 22, knobW, 20);
                detailTitleLabels[2]->setBounds(offsetKnob.getX(), offsetKnob.getY() - 22, knobW, 20);
            }

            freqValueLabel.setBounds(freqKnob.getX(), freqKnob.getBottom() + 2, knobW, 20);
            depthValueLabel.setBounds(depthKnob.getX(), depthKnob.getBottom() + 2, knobW, 20);
            offsetValueLabel.setBounds(offsetKnob.getX(), offsetKnob.getBottom() + 2, knobW, 20);
        }

        void timerCallback() override
        {
            if (isVisible() && (currentView == View::lfoGrid || currentView == View::lfoDetail))
                repaint();
        }

        juce::String nodeId;
        View currentView = View::mapping;
        bool lfoDeleteMode = false;
        bool isUpdatingDetailControls = false;
        int selectedLfoIndex = -1;
        int activeMappingParameterIndex = -1;

        juce::TextButton closeButton;
        juce::TextButton backButton;

        juce::Component mappingView;
        juce::OwnedArray<juce::Label> parameterLabels;
        juce::OwnedArray<MappingComboBox> mappingBoxes;
        std::vector<juce::String> parameterIds;

        juce::Component lfoGridView;
        juce::TextButton newLfoButton;
        juce::TextButton deleteLfoButton;
        juce::OwnedArray<LfoCard> lfoCards;

        juce::Component lfoDetailView;
        juce::ComboBox waveformCombo;
        WaveformPreviewComponent waveformPreview{ *this };
        juce::Slider freqKnob, depthKnob, offsetKnob;
        juce::OwnedArray<juce::Label> detailTitleLabels;
        juce::Label freqValueLabel, depthValueLabel, offsetValueLabel;
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

    inline juce::String parameterIdFromParameter(const juce::AudioProcessorParameter* p)
    {
        if (p == nullptr)
            return {};

        if (auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*>(p))
            if (withId->paramID.isNotEmpty())
                return withId->paramID;

        return p->getName(64).replaceCharacters(" ", "_").toLowerCase();
    }

    inline ParameterAssignment getAssignmentForParameter(const juce::AudioProcessor* processor,
                                                         const juce::AudioProcessorParameter* parameter)
    {
        if (processor == nullptr || parameter == nullptr)
            return {};

        const auto nodeId = makeRuntimeNodeId(processor);
        const auto parameterId = parameterIdFromParameter(parameter);
        return SessionModulationModel::instance().getAssignment(makeParameterKey(nodeId, parameterId));
    }

    inline bool isManualControlAllowed(const juce::AudioProcessor* processor,
                                       const juce::AudioProcessorParameter* parameter)
    {
        return getAssignmentForParameter(processor, parameter).source == ModulationSource::none;
    }

    inline float evaluateLfoWave(const LfoDefinition& lfo, double timeSec)
    {
        const float phase = static_cast<float>(std::fmod(timeSec * juce::jmax(0.01f, lfo.frequencyHz), 1.0));

        float wave = 0.0f;
        switch (lfo.waveform)
        {
            case LfoDefinition::Waveform::sine: wave = std::sin(phase * juce::MathConstants<float>::twoPi); break;
            case LfoDefinition::Waveform::triangle: wave = 1.0f - 4.0f * std::abs(phase - 0.5f); break;
            case LfoDefinition::Waveform::square: wave = (phase < 0.5f ? 1.0f : -1.0f); break;
            case LfoDefinition::Waveform::saw: wave = (2.0f * phase) - 1.0f; break;
            case LfoDefinition::Waveform::random:
            {
                const int step = juce::jlimit(0, 15, static_cast<int>(phase * 16.0f));
                static constexpr float seq[16] = { 0.84f, -0.18f, 0.35f, -0.92f, 0.11f, 0.72f, -0.47f, 0.28f,
                                                   -0.73f, 0.64f, -0.05f, 0.51f, -0.88f, 0.22f, -0.31f, 0.94f };
                wave = seq[step];
                break;
            }
        }

        const float depth = juce::jlimit(0.0f, 1.0f, lfo.depthPercent / 100.0f);
        const float offset = juce::jlimit(-1.0f, 1.0f, (lfo.offsetPercent / 100.0f) * 2.0f - 1.0f);
        return juce::jlimit(-1.0f, 1.0f, wave * depth + offset);
    }

    inline std::atomic<float>& hardwarePoti1Value()
    {
        static std::atomic<float> v { 0.0f };
        return v;
    }

    inline std::atomic<float>& hardwarePoti2Value()
    {
        static std::atomic<float> v { 0.0f };
        return v;
    }

    inline std::atomic<bool>& hardwareFootswitch1Value()
    {
        static std::atomic<bool> v { false };
        return v;
    }

    inline std::atomic<bool>& hardwareFootswitch2Value()
    {
        static std::atomic<bool> v { false };
        return v;
    }

    inline std::atomic<bool>& hardwareFootswitch3Value()
    {
        static std::atomic<bool> v { false };
        return v;
    }

    inline void setHardwareInputSnapshot(float poti1, float poti2,
                                     bool footswitch1, bool footswitch2, bool footswitch3)
    {
        hardwarePoti1Value().store(juce::jlimit(0.0f, 1.0f, poti1));
        hardwarePoti2Value().store(juce::jlimit(0.0f, 1.0f, poti2));
        hardwareFootswitch1Value().store(footswitch1);
        hardwareFootswitch2Value().store(footswitch2);
        hardwareFootswitch3Value().store(footswitch3);
    }

    inline float getHardwareSourceNormalised(ModulationSource source)
    {
        switch (source)
        {
            case ModulationSource::poti1: return hardwarePoti1Value().load();
            case ModulationSource::poti2: return hardwarePoti2Value().load();
            case ModulationSource::footswitch1: return hardwareFootswitch1Value().load() ? 1.0f : 0.0f;
            case ModulationSource::footswitch2: return hardwareFootswitch2Value().load() ? 1.0f : 0.0f;
            case ModulationSource::footswitch3: return hardwareFootswitch3Value().load() ? 1.0f : 0.0f;
            default: return 0.0f;
        }
    }

    inline float getDisplayValueForParameter(const juce::AudioProcessor* processor,
                                             const juce::AudioParameterFloat* parameter)
    {
        if (processor == nullptr || parameter == nullptr)
            return 0.0f;

        const float baseValue = parameter->get();
        const auto assignment = getAssignmentForParameter(processor, parameter);

        if (assignment.source == ModulationSource::none)
            return baseValue;

        const auto& range = parameter->getNormalisableRange();

        if (assignment.source == ModulationSource::lfo)
        {
            auto lfos = SessionModulationModel::instance().getLfos();
            if (lfos.empty())
                return baseValue;

            const int idx = juce::jlimit(0, static_cast<int>(lfos.size()) - 1, assignment.lfoIndex);
            const auto lfo = lfos[(size_t) idx];

            const double timeSec = juce::Time::getMillisecondCounterHiRes() * 0.001;
            const float mod = evaluateLfoWave(lfo, timeSec); // -1..1

            const float modNorm = juce::jlimit(0.0f, 1.0f, (mod + 1.0f) * 0.5f);
            return static_cast<float>(range.convertFrom0to1(modNorm));
        }

        const float hwNorm = getHardwareSourceNormalised(assignment.source);
        return static_cast<float>(range.convertFrom0to1(juce::jlimit(0.0f, 1.0f, hwNorm)));
    }

} // namespace FxCommon
