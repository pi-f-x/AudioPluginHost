/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

#include <JuceHeader.h>
#include "GraphEditorPanel.h"
#include "../Plugins/InternalPlugins.h"
#include "MainHostWindow.h"

namespace
{
    constexpr int touchPinSize = 56;
}

//==============================================================================
// NEW: Floating Plugin Menu Component
struct GraphEditorPanel::FloatingPluginMenu final : public Component
{
    FloatingPluginMenu (GraphEditorPanel& p, Array<PluginDescriptionAndPreference> plugins)
        : panel (p), pluginList (plugins)
    {
        setAlwaysOnTop (true);
        
        // Create a container for the buttons
        buttonContainer.reset (new Component());
        
        // Calculate grid layout
        int itemsPerRow = 3;
        int numRows = (pluginList.size() + itemsPerRow - 1) / itemsPerRow;
        
        // Create buttons for each plugin
        for (int i = 0; i < pluginList.size(); ++i)
        {
            auto* button = new TextButton (pluginList[i].pluginDescription.name);
            button->setClickingTogglesState (false);
            button->onClick = [this, i]()
            {
                // Place plugin in center of screen
                auto centerPos = panel.getLocalBounds().getCentre();
                panel.createNewPlugin (pluginList[i], centerPos);
                panel.hidePluginMenu();
            };
            
            buttons.add (button);
            buttonContainer->addAndMakeVisible (button);
        }
        
        // Size calculation: padding + grid of buttons
        int buttonWidth = 180;
        int buttonHeight = 50;
        int padding = 20;
        int gap = 10;
        
        // Container needs full height for all buttons
        int containerWidth = itemsPerRow * buttonWidth + (itemsPerRow - 1) * gap;
        int containerHeight = numRows * buttonHeight + (numRows - 1) * gap;
        buttonContainer->setSize (containerWidth, containerHeight);
        
        // Position all buttons in the container
        int currentRow = 0;
        int currentCol = 0;
        
        for (auto* button : buttons)
        {
            int x = currentCol * (buttonWidth + gap);
            int y = currentRow * (buttonHeight + gap);
            
            button->setBounds (x, y, buttonWidth, buttonHeight);
            
            currentCol++;
            if (currentCol >= itemsPerRow)
            {
                currentCol = 0;
                currentRow++;
            }
        }
        
        // Add container to viewport
        viewport.setViewedComponent (buttonContainer.get(), false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        
        // Menu size: Larger to show more content, but limit max height for scrolling
        int titleHeight = 60;
        int maxViewportHeight = 400; // Maximum height before scrolling kicks in
        int actualContentHeight = jmin (containerHeight, maxViewportHeight);
        
        int width = padding * 2 + containerWidth;
        int height = titleHeight + padding * 2 + actualContentHeight;
        
        setSize (width, height);
    }
    
    void paint (Graphics& g) override
    {
        // Elegant semi-transparent background with gradient
        g.setGradientFill (ColourGradient (Colour (0xdd000000), 0, 0,
                                          Colour (0xee222222), 0, (float) getHeight(),
                                          false));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 15.0f);
        
        // Outer glow effect
        g.setColour (Colours::cyan.withAlpha (0.3f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2), 15.0f, 3.0f);
        
        // Title at top
        g.setColour (Colours::white);
        g.setFont (Font (24.0f, Font::bold));
        g.drawText ("Select Effect", getLocalBounds().removeFromTop (60), Justification::centred);
    }
    
    void resized() override
    {
        auto bounds = getLocalBounds().reduced (20);
        bounds.removeFromTop (60); // Space for title
        
        // Viewport takes remaining space
        viewport.setBounds (bounds);
    }
    
    GraphEditorPanel& panel;
    Array<PluginDescriptionAndPreference> pluginList;
    OwnedArray<TextButton> buttons;
    std::unique_ptr<Component> buttonContainer;
    Viewport viewport;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FloatingPluginMenu)
};

//==============================================================================
struct GraphEditorPanel::PinComponent final : public Component,
                                              public SettableTooltipClient
    {
        PinComponent (GraphEditorPanel& p, AudioProcessorGraph::NodeAndChannel pinToUse, bool isIn)
            : panel (p), graph (p.graph), pin (pinToUse), isInput (isIn)
        {
            if (auto node = graph.graph.getNodeForId (pin.nodeID))
            {
                String tip;

                if (pin.isMIDI())
                {
                    tip = isInput ? "MIDI Input"
                                  : "MIDI Output";
                }
                else
                {
                    auto& processor = *node->getProcessor();
                    auto channel = processor.getOffsetInBusBufferForAbsoluteChannelIndex (isInput, pin.channelIndex, busIdx);

                    if (auto* bus = processor.getBus (isInput, busIdx))
                        tip = bus->getName() + ": " + AudioChannelSet::getAbbreviatedChannelTypeName (bus->getCurrentLayout().getTypeOfChannel (channel));
                    else
                        tip = (isInput ? "Main Input: "
                                       : "Main Output: ") + String (pin.channelIndex + 1);

                }

                setTooltip (tip);
            }

            // Fixed size for 800x480 touchscreen - larger pins
            setSize (touchPinSize, touchPinSize);
        }

        void paint (Graphics& g) override
        {
            auto w = (float) getWidth();
            auto h = (float) getHeight();

            Path p;
            p.addEllipse (w * 0.25f, h * 0.25f, w * 0.5f, h * 0.5f);
            p.addRectangle (w * 0.4f, isInput ? (0.5f * h) : 0.0f, w * 0.2f, h * 0.5f);

            auto colour = (pin.isMIDI() ? Colours::red : Colours::green);

            g.setColour (colour.withRotatedHue ((float) busIdx / 5.0f));
            g.fillPath (p);
        }

        void mouseDown (const MouseEvent& e) override
        {
            AudioProcessorGraph::NodeAndChannel dummy { {}, 0 };

            panel.beginConnectorDrag (isInput ? dummy : pin,
                                      isInput ? pin : dummy,
                                      e);
        }

        void mouseDrag (const MouseEvent& e) override
        {
            panel.dragConnector (e);
        }

        void mouseUp (const MouseEvent& e) override
        {
            panel.endDraggingConnector (e);
        }

        GraphEditorPanel& panel;
        PluginGraph& graph;
        AudioProcessorGraph::NodeAndChannel pin;
        const bool isInput;
        int busIdx = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PinComponent)
    };

//==============================================================================
struct GraphEditorPanel::PluginComponent final : public Component,
                                                 public Timer,
                                                 private AudioProcessorParameter::Listener,
                                                 private AsyncUpdater
    {
        PluginComponent (GraphEditorPanel& p, AudioProcessorGraph::NodeID id)  : panel (p), graph (p.graph), pluginID (id)
        {
            shadow.setShadowProperties (DropShadow (Colours::black.withAlpha (0.5f), 3, { 0, 1 }));
            setComponentEffect (&shadow);

            if (auto f = graph.graph.getNodeForId (pluginID))
            {
                if (auto* processor = f->getProcessor())
                {
                    if (auto* bypassParam = processor->getBypassParameter())
                        bypassParam->addListener (this);
                }
            }

            // Fixed for 800x480 touchscreen - larger font and component
            font = FontOptions { 18.0f, Font::bold };
            setSize (220, 150);
        }

        PluginComponent (const PluginComponent&) = delete;
        PluginComponent& operator= (const PluginComponent&) = delete;

        ~PluginComponent() override
        {
            if (auto f = graph.graph.getNodeForId (pluginID))
            {
                if (auto* processor = f->getProcessor())
                {
                    if (auto* bypassParam = processor->getBypassParameter())
                        bypassParam->removeListener (this);
                }
            }
        }

        void mouseDown (const MouseEvent& e) override
        {
            originalPos = localPointToGlobal (Point<int>());

            toFront (true);

            if (isEssentialNode())
                return;
            
            // Check if in delete mode
            if (panel.isDeleteMode())
            {
                // Delete this plugin immediately
                graph.graph.removeNode (pluginID);
                return; // Don't proceed with normal drag behavior
            }

            // Normal behavior when not in delete mode
            startTimer (500);

            // Removed right-click menu functionality
        }

        void mouseDrag (const MouseEvent& e) override
        {
            if (isEssentialNode())
                return;

            // Don't drag in delete mode
            if (panel.isDeleteMode())
                return;
                
            if (e.getDistanceFromDragStart() > 5)
                stopTimer();

            if (! e.mods.isPopupMenu())
            {
                auto pos = originalPos + e.getOffsetFromDragStart();

                if (getParentComponent() != nullptr)
                    pos = getParentComponent()->getLocalPoint (nullptr, pos);

                pos += getLocalBounds().getCentre();

                graph.setNodePosition (pluginID,
                                       { pos.x / (double) getParentWidth(),
                                         pos.y / (double) getParentHeight() });

                panel.updateComponents();
            }
        }

        void mouseUp (const MouseEvent& e) override
        {
            if (isEssentialNode())
                return;

            // Don't do anything in delete mode (deletion happens in mouseDown)
            if (panel.isDeleteMode())
                return;
                
            stopTimer();
            callAfterDelay (250, []() { PopupMenu::dismissAllActiveMenus(); });

            if (e.mouseWasDraggedSinceMouseDown())
            {
                graph.setChangedFlag (true);
            }
            else if (e.getNumberOfClicks() == 2)
            {
                // Double click opens the plugin editor in sidepanel
                panel.showPluginEditorInSidePanel (pluginID);
            }
            else if (e.getNumberOfClicks() == 1 && ! e.mouseWasDraggedSinceMouseDown())
            {
                // Single click also opens sidepanel (for touch-friendly operation)
                panel.showPluginEditorInSidePanel (pluginID);
            }
        }

        bool hitTest (int x, int y) override
        {
            for (auto* child : getChildren())
                if (child->getBounds().contains (x, y))
                    return true;

            if (isEssentialNode())
                return false;

            return x >= 3 && x < getWidth() - 6 && y >= pinSize && y < getHeight() - pinSize;
        }

        void paint (Graphics& g) override
        {
            auto boxArea = getLocalBounds().reduced (4, pinSize);
            bool isBypassed = false;

            if (auto* f = graph.graph.getNodeForId (pluginID))
                isBypassed = f->isBypassed();

            auto boxColour = findColour (TextEditor::backgroundColourId);

            if (isBypassed)
                boxColour = boxColour.brighter();

            g.setColour (boxColour);
            g.fillRect (boxArea.toFloat());

            // Draw bright border if this plugin is currently shown in sidepanel
            bool isOpenInSidePanel = (panel.currentlyShowingNodeID == pluginID && panel.pluginEditorPanel != nullptr);
            if (isOpenInSidePanel)
            {
                g.setColour (Colours::cyan.brighter());
                g.drawRect (boxArea.toFloat(), 3.0f);
            }

            g.setColour (findColour (TextEditor::textColourId));
            g.setFont (font);
            // Add padding around text for better readability
            g.drawFittedText (getName(), boxArea.reduced (10, 6), Justification::centred, 3);
        }

        void resized() override
        {
            if (auto f = graph.graph.getNodeForId (pluginID))
            {
                if (auto* processor = f->getProcessor())
                {
                    for (auto* pin : pins)
                    {
                        const bool isInput = pin->isInput;
                        auto channelIndex = pin->pin.channelIndex;
                        int busIdx = 0;
                        processor->getOffsetInBusBufferForAbsoluteChannelIndex (isInput, channelIndex, busIdx);

                        const int total = isInput ? numIns : numOuts;
                        const int index = pin->pin.isMIDI() ? (total - 1) : channelIndex;

                        auto totalSpaces = static_cast<float> (total) + (static_cast<float> (jmax (0, processor->getBusCount (isInput) - 1)) * 0.5f);
                        auto indexPos = static_cast<float> (index) + (static_cast<float> (busIdx) * 0.5f);

                        pin->setBounds (proportionOfWidth ((1.0f + indexPos) / (totalSpaces + 1.0f)) - pinSize / 2,
                                        pin->isInput ? 0 : (getHeight() - pinSize),
                                        pinSize, pinSize);
                    }
                }
            }
        }

        Point<float> getPinPos (int index, bool isInput) const;

        void update()
        {
            const AudioProcessorGraph::Node::Ptr f (graph.graph.getNodeForId (pluginID));
            jassert (f != nullptr);

            auto& processor = *f->getProcessor();

            numIns = processor.getTotalNumInputChannels();
            if (processor.acceptsMidi())
                ++numIns;

            numOuts = processor.getTotalNumOutputChannels();
            if (processor.producesMidi())
                ++numOuts;

            // Larger sizes for 800x480 touchscreen
            int w = 150;
            int h = 90;

            h = jmax (h, pinSize * 2 + 34);
            w = jmax (w, (jmax (numIns, numOuts) + 1) * 64);

            const auto textWidth = GlyphArrangement::getStringWidthInt (font, processor.getName());
            w = jmax (w, 24 + jmin (textWidth, 400));
            if (textWidth > 400)
                h = jmax (h, pinSize * 2 + 64);

            setSize (w, h);
            setName (processor.getName() + formatSuffix);

            {
                auto p = graph.getNodePosition (pluginID);
                setCentreRelative ((float) p.x, (float) p.y);
            }

            if (numIns != numInputs || numOuts != numOutputs)
            {
                numInputs = numIns;
                numOutputs = numOuts;

                pins.clear();

                for (int i = 0; i < processor.getTotalNumInputChannels(); ++i)
                    addAndMakeVisible (pins.add (new PinComponent (panel, { pluginID, i }, true)));

                if (processor.acceptsMidi())
                    addAndMakeVisible (pins.add (new PinComponent (panel, { pluginID, AudioProcessorGraph::midiChannelIndex }, true)));

                for (int i = 0; i < processor.getTotalNumOutputChannels(); ++i)
                    addAndMakeVisible (pins.add (new PinComponent (panel, { pluginID, i }, false)));

                if (processor.producesMidi())
                    addAndMakeVisible (pins.add (new PinComponent (panel, { pluginID, AudioProcessorGraph::midiChannelIndex }, false)));
            }

            resized();
        }

        AudioProcessor* getProcessor() const
        {
            if (auto node = graph.graph.getNodeForId (pluginID))
                return node->getProcessor();

            return {};
        }

        bool isEssentialNode() const
        {
            if (auto* processor = getProcessor())
            {
                const auto name = processor->getName();
                return name == "Audio Input" || name == "Audio Output";
            }

            return false;
        }

        void showPopupMenu()
        {
            menu.reset (new PopupMenu);
            
            // Only add delete option if not an essential node
            if (! isEssentialNode())
            {
                menu->addItem ("Delete this filter", [this] { graph.graph.removeNode (pluginID); });
            }
            
            menu->addItem ("Disconnect all pins", [this] { graph.graph.disconnectNode (pluginID); });
            menu->addItem ("Toggle Bypass", [this]
            {
                if (auto* node = graph.graph.getNodeForId (pluginID))
                    node->setBypassed (! node->isBypassed());

                repaint();
            });

            menu->addSeparator();
            if (getProcessor()->hasEditor())
                menu->addItem ("Show plugin GUI", [this] { showWindow (PluginWindow::Type::normal); });

            menu->addItem ("Show all programs", [this] { showWindow (PluginWindow::Type::programs); });
            menu->addItem ("Show all parameters", [this] { showWindow (PluginWindow::Type::generic); });
            menu->addItem ("Show debug log", [this] { showWindow (PluginWindow::Type::debug); });

           #if JUCE_PLUGINHOST_ARA && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX)
            if (auto* instance = dynamic_cast<AudioPluginInstance*> (getProcessor()))
                if (instance->getPluginDescription().hasARAExtension && isNodeUsingARA())
                    menu->addItem ("Show ARA host controls", [this] { showWindow (PluginWindow::Type::araHost); });
           #endif

            if (autoScaleOptionAvailable)
                addPluginAutoScaleOptionsSubMenu (dynamic_cast<AudioPluginInstance*> (getProcessor()), *menu);

            menu->addSeparator();
            menu->addItem ("Configure Audio I/O", [this] { showWindow (PluginWindow::Type::audioIO); });
            menu->addItem ("Test state save/load", [this] { testStateSaveLoad(); });

           #if ! JUCE_IOS && ! JUCE_ANDROID
            menu->addSeparator();
            menu->addItem ("Save plugin state", [this] { savePluginState(); });
            menu->addItem ("Load plugin state", [this] { loadPluginState(); });
           #endif

            menu->showMenuAsync ({});
        }

        void testStateSaveLoad()
        {
            if (auto* processor = getProcessor())
            {
                MemoryBlock state;
                processor->getStateInformation (state);
                processor->setStateInformation (state.getData(), (int) state.getSize());
            }
        }

        void showWindow (PluginWindow::Type type)
        {
            if (auto node = graph.graph.getNodeForId (pluginID))
                if (auto* w = graph.getOrCreateWindowFor (node, type))
                    w->toFront (true);
        }

        void timerCallback() override
        {
            stopTimer();
            // Removed: No longer showing popup menu on timer
        }

        void parameterValueChanged (int, float) override
        {
            triggerAsyncUpdate();
        }

        void parameterGestureChanged (int, bool) override  {}

        void handleAsyncUpdate() override { repaint(); }

        void savePluginState()
        {
            fileChooser = std::make_unique<FileChooser> ("Save plugin state");

            const auto onChosen = [ref = SafePointer<PluginComponent> (this)] (const FileChooser& chooser)
            {
                if (ref == nullptr)
                    return;

                const auto result = chooser.getResult();

                if (result == File())
                    return;

                if (auto* node = ref->graph.graph.getNodeForId (ref->pluginID))
                {
                    MemoryBlock block;
                    node->getProcessor()->getStateInformation (block);
                    result.replaceWithData (block.getData(), block.getSize());
                }
            };

            fileChooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting, onChosen);
        }

        void loadPluginState()
        {
            fileChooser = std::make_unique<FileChooser> ("Load plugin state");

            const auto onChosen = [ref = SafePointer<PluginComponent> (this)] (const FileChooser& chooser)
            {
                if (ref == nullptr)
                    return;

                const auto result = chooser.getResult();

                if (result == File())
                    return;

                if (auto* node = ref->graph.graph.getNodeForId (ref->pluginID))
                {
                    if (auto stream = result.createInputStream())
                    {
                        MemoryBlock block;
                        stream->readIntoMemoryBlock (block);
                        node->getProcessor()->setStateInformation (block.getData(), (int) block.getSize());
                    }
                }
            };

            fileChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, onChosen);
        }

        GraphEditorPanel& panel;
        PluginGraph& graph;
        const AudioProcessorGraph::NodeID pluginID;
        OwnedArray<PinComponent> pins;
        int numInputs = 0, numOutputs = 0;
        // Fixed larger pin size for 800x480 touchscreen
        int pinSize = touchPinSize;
        Point<int> originalPos;
        Font font;
        int numIns = 0, numOuts = 0;
        DropShadowEffect shadow;
        std::unique_ptr<PopupMenu> menu;
        std::unique_ptr<FileChooser> fileChooser;
        const String formatSuffix = getFormatSuffix (getProcessor());
    };

Point<float> GraphEditorPanel::PluginComponent::getPinPos (int index, bool isInput) const
{
    for (auto* pin : pins)
        if (pin->pin.channelIndex == index && isInput == pin->isInput)
            return getPosition().toFloat() + pin->getBounds().getCentre().toFloat();

    return {};
}

//==============================================================================
struct GraphEditorPanel::ConnectorComponent final : public Component,
                                                    public SettableTooltipClient
{
    explicit ConnectorComponent (GraphEditorPanel& p)
        : panel (p), graph (p.graph)
    {
    }

    void setInput (AudioProcessorGraph::NodeAndChannel newSource)
    {
        if (connection.source != newSource)
        {
            connection.source = newSource;
            update();
        }
    }

    void setOutput (AudioProcessorGraph::NodeAndChannel newDest)
    {
        if (connection.destination != newDest)
        {
            connection.destination = newDest;
            update();
        }
    }

    void dragStart (Point<float> pos)
    {
        dragging = true;
        lastInputPos = pos;
        resizeToFit();
    }

    void dragEnd (Point<float> pos)
    {
        dragging = true;
        lastOutputPos = pos;
        resizeToFit();
    }

    void setLineColour (bool shouldUse, Colour colourToUse = Colours::transparentBlack)
    {
        useLineColour = shouldUse;
        lineColour = colourToUse;
        repaint();
    }

    void update()
    {
        Point<float> p1, p2;
        getPoints (p1, p2);

        dragging = (connection.source.nodeID == AudioProcessorGraph::NodeID()
                    || connection.destination.nodeID == AudioProcessorGraph::NodeID());

        if (lastInputPos != p1 || lastOutputPos != p2)
            resizeToFit();
    }

    void resizeToFit()
    {
        Point<float> p1, p2;
        getPoints (p1, p2);

        auto boundsArea = Rectangle<float> (p1, p2).expanded (18.0f);

        if (boundsArea.getWidth() < 24.0f)
            boundsArea = boundsArea.withWidth (24.0f).withCentre (boundsArea.getCentre());

        if (boundsArea.getHeight() < 24.0f)
            boundsArea = boundsArea.withHeight (24.0f).withCentre (boundsArea.getCentre());

        auto newBounds = boundsArea.getSmallestIntegerContainer();

        if (newBounds != getBounds())
            setBounds (newBounds);
        else
            resized();

        repaint();
    }

    void getPoints (Point<float>& p1, Point<float>& p2) const
    {
        p1 = lastInputPos;
        p2 = lastOutputPos;

        if (auto* src = panel.getComponentForPlugin (connection.source.nodeID))
        {
            auto srcPos = src->getPinPos (connection.source.channelIndex, false);
            if (srcPos != Point<float>())
                p1 = srcPos;
        }

        if (auto* dest = panel.getComponentForPlugin (connection.destination.nodeID))
        {
            auto dstPos = dest->getPinPos (connection.destination.channelIndex, true);
            if (dstPos != Point<float>())
                p2 = dstPos;
        }
    }

    void resized() override
    {
        Point<float> p1, p2;
        getPoints (p1, p2);

        p1 -= getPosition().toFloat();
        p2 -= getPosition().toFloat();

        const Point<float> c1 (p1.x, (p1.y + p2.y) * 0.5f);
        const Point<float> c2 (p2.x, (p1.y + p2.y) * 0.5f);

        Path curve;
        curve.startNewSubPath (p1);
        curve.cubicTo (c1, c2, p2);

        linePath.clear();
        hitPath.clear();
        arrowPath.clear();

        PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded).createStrokedPath (linePath, curve);
        PathStrokeType (10.0f, PathStrokeType::curved, PathStrokeType::rounded).createStrokedPath (hitPath, curve);

        const auto t = 0.5f;
        const auto oneMinusT = 1.0f - t;

        const auto pointOnCurve = p1 * (oneMinusT * oneMinusT * oneMinusT)
                                + c1 * (3.0f * oneMinusT * oneMinusT * t)
                                + c2 * (3.0f * oneMinusT * t * t)
                                + p2 * (t * t * t);

        auto tangent = (c1 - p1) * (3.0f * oneMinusT * oneMinusT)
                     + (c2 - c1) * (6.0f * oneMinusT * t)
                     + (p2 - c2) * (3.0f * t * t);

        if (tangent.getDistanceFromOrigin() < 0.001f)
            tangent = { 0.0f, 1.0f };
        else
            tangent /= tangent.getDistanceFromOrigin();

        const auto arrowLength = 13.0f;
        const auto arrowHalfWidth = 6.0f;
        const auto tip = pointOnCurve + tangent * 6.0f;
        const auto base = tip - tangent * arrowLength;
        const Point<float> normal (-tangent.y, tangent.x);

        arrowPath.startNewSubPath (tip);
        arrowPath.lineTo (base + normal * arrowHalfWidth);
        arrowPath.lineTo (base - normal * arrowHalfWidth);
        arrowPath.closeSubPath();
    }

    void paint (Graphics& g) override
    {
        if (useLineColour)
            g.setColour (lineColour);
        else if (connection.source.isMIDI() || connection.destination.isMIDI())
            g.setColour (Colours::red);
        else
            g.setColour (Colours::green);

        g.fillPath (linePath);

        if (! dragging
            && connection.source.nodeID != AudioProcessorGraph::NodeID()
            && connection.destination.nodeID != AudioProcessorGraph::NodeID())
        {
            g.fillPath (arrowPath);
            g.setColour (Colours::black.withAlpha (0.8f));
            g.strokePath (arrowPath, PathStrokeType (1.0f));
        }
    }

    bool hitTest (int x, int y) override
    {
        return hitPath.contains ((float) x, (float) y);
    }

    void mouseDown (const MouseEvent&) override
    {
        if (panel.isDeleteMode() && ! dragging)
            graph.graph.removeConnection (connection);
    }

    GraphEditorPanel& panel;
    PluginGraph& graph;
    AudioProcessorGraph::Connection connection { { {}, 0 }, { {}, 0 } };
    Point<float> lastInputPos, lastOutputPos;
    Path linePath, hitPath, arrowPath;
    bool dragging = false;
    bool useLineColour = false;
    Colour lineColour = Colours::transparentBlack;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConnectorComponent)
};


//==============================================================================
struct GraphEditorPanel::PluginEditorSidePanel final : public Component
{
    PluginEditorSidePanel (GraphEditorPanel& p, AudioProcessorGraph::NodeID nodeID)
        : panel (p), pluginNodeID (nodeID)
    {
        setOpaque (true);
        setSize (380, 600);

        addAndMakeVisible (closeButton);
        closeButton.setButtonText ("X");
        closeButton.onClick = [this] { panel.closePluginEditorSidePanel(); };

        if (auto* node = panel.graph.graph.getNodeForId (pluginNodeID))
        {
            if (auto* processor = node->getProcessor())
            {
                if (processor->hasEditor())
                {
                    editor.reset (processor->createEditorIfNeeded());
                    if (editor != nullptr)
                    {
                        addAndMakeVisible (editor.get());

                        auto editorBounds = editor->getBounds();
                        int panelWidth = jmax (380, editorBounds.getWidth() + 20);
                        int panelHeight = jmax (600, editorBounds.getHeight() + 80);

                        setSize (panelWidth, panelHeight);
                    }
                }
            }
        }
    }

    ~PluginEditorSidePanel() override
    {
        if (editor != nullptr && panel.graph.graph.getNodeForId (pluginNodeID))
        {
            if (auto* node = panel.graph.graph.getNodeForId (pluginNodeID))
            {
                if (auto* processor = node->getProcessor())
                    processor->editorBeingDeleted (editor.get());
            }
        }

        editor = nullptr;
    }

    void paint (Graphics& g) override
    {
        g.fillAll (getLookAndFeel().findColour (ResizableWindow::backgroundColourId));
        g.setColour (Colours::white.withAlpha (0.1f));
        g.drawRect (getLocalBounds(), 2);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();

        auto header = bounds.removeFromTop (42);
        closeButton.setBounds (header.removeFromRight (60).reduced (5));

        if (editor != nullptr)
        {
            bounds.reduce (10, 10);

            auto editorBounds = editor->getBounds();
            int editorWidth = editorBounds.getWidth();
            int editorHeight = editorBounds.getHeight();

            if (editorHeight < bounds.getHeight())
            {
                int yOffset = (bounds.getHeight() - editorHeight) / 2;
                editor->setBounds (bounds.getX(),
                                  bounds.getY() + yOffset,
                                  jmin (editorWidth, bounds.getWidth()),
                                  editorHeight);
            }
            else
            {
                editor->setBounds (bounds);
            }
        }
    }

    GraphEditorPanel& panel;
    AudioProcessorGraph::NodeID pluginNodeID;
    std::unique_ptr<AudioProcessorEditor> editor;
    TextButton closeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditorSidePanel)
};

//==============================================================================
GraphEditorPanel::GraphEditorPanel (PluginGraph& g)  : graph (g)
{
    graph.addChangeListener (this);
    setOpaque (true);
    currentlyShowingNodeID = AudioProcessorGraph::NodeID();
    
    // Setup + Button
    addPluginButton.setButtonText ("+");
    addPluginButton.setColour (TextButton::buttonColourId, Colours::green);
    addPluginButton.setColour (TextButton::textColourOffId, Colours::white);
    addPluginButton.onClick = [this]() { showPluginMenu(); };
    addAndMakeVisible (addPluginButton);
    
    // Setup Delete Button (???)
    deleteButton.setButtonText ("-");
    deleteButton.setColour (TextButton::buttonColourId, Colours::grey);
    deleteButton.setColour (TextButton::textColourOffId, Colours::white);
    deleteButton.onClick = [this]() { toggleDeleteMode(); };
    addAndMakeVisible (deleteButton);
}

// Destructor is defined later, after PluginListBoxModel is complete.
GraphEditorPanel::~GraphEditorPanel()
{
    graph.removeChangeListener (this);
    draggingConnector = nullptr;
    nodes.clear();
    connectors.clear();
}

void GraphEditorPanel::paint (Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (ResizableWindow::backgroundColourId));
    
    // Draw red border if in delete mode
    if (deleteMode)
    {
        g.setColour (Colours::red);
        g.drawRect (getLocalBounds(), 5);
        
        // Draw warning text
        g.setFont (Font (20.0f, Font::bold));
        g.drawText ("DELETE MODE - Click connections or effects to delete", 
                   getLocalBounds().removeFromTop (20), 
                   Justification::centred);
    }
}

void GraphEditorPanel::mouseDown (const MouseEvent& e)
{
    // Entferne Rechtsklick-Menü Funktionalität komplett
    originalTouchPos = e.position.toInt();
    // Kein Timer, kein Popup-Menü mehr
}

void GraphEditorPanel::mouseUp (const MouseEvent&)
{
    stopTimer();
    callAfterDelay (250, []() { PopupMenu::dismissAllActiveMenus(); });
}

void GraphEditorPanel::mouseDrag (const MouseEvent& e)
{
    if (e.getDistanceFromDragStart() > 5)
        stopTimer();
}

void GraphEditorPanel::createNewPlugin (const PluginDescriptionAndPreference& desc, Point<int> position)
{
    graph.addPlugin (desc, position.toDouble() / Point<double> ((double) getWidth(), (double) getHeight()));
}

GraphEditorPanel::PluginComponent* GraphEditorPanel::getComponentForPlugin (AudioProcessorGraph::NodeID nodeID) const
{
    for (auto* fc : nodes)
       if (fc->pluginID == nodeID)
            return fc;

    return nullptr;
}

GraphEditorPanel::ConnectorComponent* GraphEditorPanel::getComponentForConnection (const AudioProcessorGraph::Connection& conn) const
{
    for (auto* cc : connectors)
        if (cc->connection == conn)
            return cc;

    return nullptr;
}

GraphEditorPanel::PinComponent* GraphEditorPanel::findPinAt (Point<float> pos) const
{
    for (auto* fc : nodes)
    {
        auto* comp = fc->getComponentAt (pos.toInt() - fc->getPosition());

        if (auto* pin = dynamic_cast<PinComponent*> (comp))
            return pin;
    }

    return nullptr;
}

void GraphEditorPanel::resized()
{
    auto bounds = getLocalBounds();

    // Position buttons in TOP-LEFT corner
    int buttonWidth = 80;
    int buttonHeight = 40;
    int padding = 10;

    auto topLeft = bounds.removeFromTop (buttonHeight + padding * 2).removeFromLeft (buttonWidth * 2 + padding * 3);

    addPluginButton.setBounds (topLeft.removeFromLeft (buttonWidth).reduced (padding));
    deleteButton.setBounds (topLeft.removeFromLeft (buttonWidth).reduced (padding));

    // Reset bounds to full height for sidepanel
    bounds = getLocalBounds();

    if (pluginEditorPanel != nullptr)
    {
        int panelWidth = pluginEditorPanel->getWidth();
        pluginEditorPanel->setBounds (bounds.removeFromRight (panelWidth));
    }

    updateComponents();
}

void GraphEditorPanel::updateComponents()
{
    for (int i = nodes.size(); --i >= 0;)
        if (graph.graph.getNodeForId (nodes.getUnchecked (i)->pluginID) == nullptr)
            nodes.remove (i);

    if (pluginEditorPanel != nullptr &&
        graph.graph.getNodeForId (currentlyShowingNodeID) == nullptr)
    {
        closePluginEditorSidePanel();
    }

    for (int i = connectors.size(); --i >= 0;)
        if (! graph.graph.isConnected (connectors.getUnchecked (i)->connection))
            connectors.remove (i);

    for (auto* fc : nodes)
        fc->update();

    for (auto* cc : connectors)
        cc->update();

    for (auto* f : graph.graph.getNodes())
    {
        if (getComponentForPlugin (f->nodeID) == nullptr)
        {
            auto* comp = nodes.add (new PluginComponent (*this, f->nodeID));
            addAndMakeVisible (comp);
            comp->update();
        }
    }

    for (auto& c : graph.graph.getConnections())
    {
        if (getComponentForConnection (c) == nullptr)
        {
            auto* comp = connectors.add (new ConnectorComponent (*this));
            addAndMakeVisible (comp);

            comp->setInput (c.source);
            comp->setOutput (c.destination);
        }
    }

    for (auto* node : nodes)
        node->toFront (false);

    addPluginButton.toFront (false);
    deleteButton.toFront (false);

    if (pluginEditorPanel != nullptr)
        pluginEditorPanel->toFront (false);
}

void GraphEditorPanel::dragConnector (const MouseEvent& e)
{
    auto e2 = e.getEventRelativeTo (this);

    if (draggingConnector != nullptr)
    {
        draggingConnector->setTooltip ({});

        auto pos = e2.position;

        if (auto* pin = findPinAt (pos))
        {
            draggingConnector->setLineColour (false);
            auto connection = draggingConnector->connection;

            if (connection.source.nodeID == AudioProcessorGraph::NodeID() && ! pin->isInput)
            {
                connection.source = pin->pin;
                draggingConnector->setInput (connection.source);
                draggingConnector->setTooltip (pin->getTooltip());
            }
            else if (connection.destination.nodeID == AudioProcessorGraph::NodeID() && pin->isInput)
            {
                connection.destination = pin->pin;
                draggingConnector->setOutput (connection.destination);
                draggingConnector->setTooltip (pin->getTooltip());
            }
        }

        if (draggingConnector->connection.source.nodeID == AudioProcessorGraph::NodeID())
            draggingConnector->dragStart (pos);
        else
            draggingConnector->dragEnd (pos);
    }
}

void GraphEditorPanel::showPluginEditorInSidePanel (AudioProcessorGraph::NodeID nodeID)
{
    // If clicking on the same plugin that's already open, CLOSE IT (toggle behavior)
    if (currentlyShowingNodeID == nodeID && pluginEditorPanel != nullptr)
    {
        closePluginEditorSidePanel();
        return;
    }
    
    auto oldNodeID = currentlyShowingNodeID;

    if (pluginEditorPanel != nullptr && currentlyShowingNodeID != nodeID)
        closePluginEditorSidePanel();

    currentlyShowingNodeID = nodeID;
    pluginEditorPanel.reset (new PluginEditorSidePanel (*this, nodeID));
    addAndMakeVisible (pluginEditorPanel.get());

    if (auto* oldComp = getComponentForPlugin (oldNodeID))
        oldComp->repaint();

    if (auto* newComp = getComponentForPlugin (nodeID))
        newComp->repaint();

    resized();
}

void GraphEditorPanel::closePluginEditorSidePanel()
{
    auto oldNodeID = currentlyShowingNodeID;

    pluginEditorPanel = nullptr;
    currentlyShowingNodeID = AudioProcessorGraph::NodeID();

    if (auto* comp = getComponentForPlugin (oldNodeID))
        comp->repaint();

    resized();
}

void GraphEditorPanel::changeListenerCallback (ChangeBroadcaster*)
{
    updateComponents();
}

void GraphEditorPanel::showPopupMenu (Point<int> mousePos)
{
    menu.reset (new PopupMenu);

    if (auto* mainWindow = findParentComponentOfClass<MainHostWindow>())
    {
        mainWindow->addPluginsToMenu (*menu);

        menu->showMenuAsync ( {},
                             ModalCallbackFunction::create ([this, mousePos] (int r)
                                                            {
                                                                if (auto* mainWin = findParentComponentOfClass<MainHostWindow>())
                                                                    if (const auto chosen = mainWin->getChosenType (r))
                                                                        createNewPlugin (*chosen, mousePos);
                                                            }));
    }
}

void GraphEditorPanel::beginConnectorDrag (AudioProcessorGraph::NodeAndChannel source,
                                           AudioProcessorGraph::NodeAndChannel dest,
                                           const MouseEvent& e)
{
    auto* c = dynamic_cast<ConnectorComponent*> (e.originalComponent);
    connectors.removeObject (c, false);
    draggingConnector.reset (c);

    if (draggingConnector == nullptr)
        draggingConnector.reset (new ConnectorComponent (*this));

    draggingConnector->setInput (source);
    draggingConnector->setOutput (dest);

    addAndMakeVisible (draggingConnector.get());
    draggingConnector->toFront (false);

    dragConnector (e);
}

void GraphEditorPanel::endDraggingConnector (const MouseEvent& e)
{
    if (draggingConnector == nullptr)
        return;

    draggingConnector->setTooltip ({});

    auto e2 = e.getEventRelativeTo (this);
    auto connection = draggingConnector->connection;

    draggingConnector = nullptr;

    if (auto* pin = findPinAt (e2.position))
    {
        if (connection.source.nodeID == AudioProcessorGraph::NodeID())
        {
            if (pin->isInput)
                return;

            connection.source = pin->pin;
        }
        else
        {
            if (! pin->isInput)
                return;

            connection.destination = pin->pin;
        }

        if (! graph.graph.addConnection (connection))
            return;

        AudioProcessorGraph::NodeID inputNodeID, outputNodeID;

        for (auto* node : graph.graph.getNodes())
        {
            if (auto* processor = node->getProcessor())
            {
                if (processor->getName() == "Audio Input")
                    inputNodeID = node->nodeID;
                else if (processor->getName() == "Audio Output")
                    outputNodeID = node->nodeID;
            }
        }

        const auto isDirectInputToOutput = [inputNodeID, outputNodeID] (const AudioProcessorGraph::Connection& c)
        {
            return c.source.nodeID == inputNodeID
                && c.destination.nodeID == outputNodeID
                && ! c.source.isMIDI()
                && ! c.destination.isMIDI();
        };

        if (inputNodeID != AudioProcessorGraph::NodeID()
            && outputNodeID != AudioProcessorGraph::NodeID()
            && ! isDirectInputToOutput (connection))
        {
            Array<AudioProcessorGraph::Connection> connectionsToRemove;

            for (auto& c : graph.graph.getConnections())
                if (isDirectInputToOutput (c))
                    connectionsToRemove.add (c);

            for (auto& c : connectionsToRemove)
                graph.graph.removeConnection (c);
        }
    }
}

void GraphEditorPanel::timerCallback()
{
    stopTimer();
}

void GraphEditorPanel::showPluginMenu()
{
    if (floatingMenu != nullptr)
    {
        hidePluginMenu();
        return;
    }

    Array<PluginDescriptionAndPreference> availablePlugins;

    if (auto* mainWindow = findParentComponentOfClass<MainHostWindow>())
    {
        auto& internalTypes = mainWindow->internalTypes;
        for (const auto& desc : internalTypes)
            if (desc.name != "Audio Input" && desc.name != "Audio Output")
                availablePlugins.add (PluginDescriptionAndPreference { desc });
    }

    if (availablePlugins.isEmpty())
        return;

    floatingMenu.reset (new FloatingPluginMenu (*this, availablePlugins));
    addAndMakeVisible (floatingMenu.get());
    floatingMenu->setCentrePosition (getWidth() / 2, getHeight() / 2);
}

void GraphEditorPanel::hidePluginMenu()
{
    floatingMenu = nullptr;
}

void GraphEditorPanel::toggleDeleteMode()
{
    deleteMode = !deleteMode;

    if (deleteMode)
        deleteButton.setColour (TextButton::buttonColourId, Colours::red);
    else
        deleteButton.setColour (TextButton::buttonColourId, Colours::grey);

    repaint();
}

//==============================================================================
struct GraphDocumentComponent::TooltipBar final : public Component,
                                                  private Timer
{
    TooltipBar()
    {
        startTimer (100);
    }

    void paint (Graphics& g) override
    {
        g.setFont (FontOptions ((float) getHeight() * 0.75f, Font::bold));
        g.setColour (Colours::black);
        g.drawFittedText (tip, 12, 0, getWidth() - 16, getHeight(), Justification::centredLeft, 1);
    }

    void timerCallback() override
    {
        String newTip;

        if (auto* underMouse = Desktop::getInstance().getMainMouseSource().getComponentUnderMouse())
            if (auto* ttc = dynamic_cast<TooltipClient*> (underMouse))
                if (! (underMouse->isMouseButtonDown() || underMouse->isCurrentlyBlockedByAnotherModalComponent()))
                    newTip = ttc->getTooltip();

        if (newTip != tip)
        {
            tip = newTip;
            repaint();
        }
    }

    String tip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TooltipBar)
};

class GraphDocumentComponent::PluginListBoxModel final : public ListBoxModel,
                                                          public ChangeListener,
                                                          public MouseListener
{
public:
    PluginListBoxModel (ListBox& lb, KnownPluginList& kpl)
        : owner (lb),
          knownPlugins (kpl)
    {
        knownPlugins.addChangeListener (this);
        owner.addMouseListener (this, true);

       #if JUCE_IOS
        scanner.reset (new AUScanner (knownPlugins));
       #endif
    }

    int getNumRows() override
    {
        return knownPlugins.getNumTypes();
    }

    void paintListBoxItem (int rowNumber, Graphics& g,
                           int width, int height, bool rowIsSelected) override
    {
        g.fillAll (rowIsSelected ? Colour (0xff42A2C8)
                                 : Colour (0xff263238));

        g.setColour (rowIsSelected ? Colours::black : Colours::white);

        if (rowNumber < knownPlugins.getNumTypes())
            g.drawFittedText (knownPlugins.getTypes()[rowNumber].name, { 8, 0, width - 16, height - 2 }, Justification::centred, 1);

        g.setColour (Colours::black.withAlpha (0.4f));
        g.drawRect (0, height - 1, width, 1);
    }

    var getDragSourceDescription (const SparseSet<int>& selectedRows) override
    {
        if (! isOverSelectedRow)
            return var();

        return String ("PLUGIN: " + String (selectedRows[0]));
    }

    void changeListenerCallback (ChangeBroadcaster*) override
    {
        owner.updateContent();
    }

    void mouseDown (const MouseEvent& e) override
    {
        isOverSelectedRow = owner.getRowPosition (owner.getSelectedRow(), true)
                                 .contains (e.getEventRelativeTo (&owner).getMouseDownPosition());
    }

private:
    ListBox& owner;
    KnownPluginList& knownPlugins;

    bool isOverSelectedRow = false;

   #if JUCE_IOS
    std::unique_ptr<AUScanner> scanner;
   #endif

    JUCE_DECLARE_NON_COPYABLE (PluginListBoxModel)
};

GraphDocumentComponent::~GraphDocumentComponent()
{
    deviceManager.removeChangeListener (this);

    if (graphPanel)
        deviceManager.removeChangeListener (graphPanel.get());

    deviceManager.removeAudioCallback (&graphPlayer);
    graphPlayer.setProcessor (nullptr);

    if (graph)
        graph->closeAnyOpenPluginWindows();

    graph.reset();
    pluginListBoxModel.reset();
    statusBar.reset();
    graphPanel.reset();
}

GraphDocumentComponent::GraphDocumentComponent (AudioPluginFormatManager& fm,
                                                AudioDeviceManager& dm,
                                                KnownPluginList& kpl)
    : graph (new PluginGraph (fm, kpl)),
      deviceManager (dm),
      pluginList (kpl),
      graphPlayer (getAppProperties().getUserSettings()->getBoolValue ("doublePrecisionProcessing", false))
{
    init();

    deviceManager.addChangeListener (graphPanel.get());
    deviceManager.addAudioCallback (&graphPlayer);

    deviceManager.addChangeListener (this);
}

void GraphDocumentComponent::init()
{
    graphPanel.reset (new GraphEditorPanel (*graph));
    addAndMakeVisible (graphPanel.get());
    graphPlayer.setProcessor (&graph->graph);

    statusBar.reset (new TooltipBar());
    addAndMakeVisible (statusBar.get());

    graphPanel->updateComponents();

    pluginListBoxModel.reset (new PluginListBoxModel (pluginListBox, pluginList));
    pluginListBox.setModel (pluginListBoxModel.get());
    pluginListBox.setRowHeight (48);

    pluginListSidePanel.setContent (&pluginListBox, false);

    mobileSettingsSidePanel.setContent (new AudioDeviceSelectorComponent (deviceManager,
                                                                          0, 2, 0, 2,
                                                                          true, true, true, false));

    addAndMakeVisible (pluginListSidePanel);
    addAndMakeVisible (mobileSettingsSidePanel);
}

void GraphDocumentComponent::resized()
{
    auto r = [this]
    {
        auto bounds = getLocalBounds();

        if (auto* display = Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
            return display->safeAreaInsets.subtractedFrom (bounds);

        return bounds;
    }();

    const int statusHeight = 26;

    statusBar->setBounds (r.removeFromBottom (statusHeight));
    graphPanel->setBounds (r);

    checkAvailableWidth();
}

void GraphDocumentComponent::changeListenerCallback (ChangeBroadcaster* source)
{
    if (source == &deviceManager)
    {
    }
    else if (graphPanel)
    {
        graphPanel->updateComponents();
    }
}

void GraphDocumentComponent::checkAvailableWidth()
{
    const int minWidthForPanels = 600;
    if (getWidth() < minWidthForPanels)
    {
        pluginListSidePanel.setVisible (false);
        mobileSettingsSidePanel.setVisible (false);
        lastOpenedSidePanel = nullptr;
    }
}

void GraphDocumentComponent::createNewPlugin (const PluginDescriptionAndPreference& desc, Point<int> position)
{
    if (graphPanel)
        graphPanel->createNewPlugin (desc, position);
}

void GraphDocumentComponent::setDoublePrecision (bool)
{
    graphPlayer.setProcessor (nullptr);
    graphPlayer.setProcessor (&graph->graph);
}

bool GraphDocumentComponent::closeAnyOpenPluginWindows()
{
    if (graph)
        return graph->closeAnyOpenPluginWindows();

    return true;
}

void GraphDocumentComponent::releaseGraph()
{
    graphPlayer.setProcessor (nullptr);

    if (graph)
    {
        graph->closeAnyOpenPluginWindows();
        graph.reset();
    }
}

bool GraphDocumentComponent::isInterestedInDragSource (const SourceDetails& sd)
{
    if (sd.description.isString())
    {
        auto s = sd.description.toString();
        return s.startsWith ("PLUGIN:");
    }

    return false;
}

void GraphDocumentComponent::itemDropped (const SourceDetails& sd)
{
    if (! sd.description.isString())
        return;

    auto s = sd.description.toString();

    if (s.startsWith ("PLUGIN:"))
    {
        auto token = s.fromFirstOccurrenceOf ("PLUGIN:", false, false).trim();
        const int index = token.getIntValue();

        if (index >= 0 && index < pluginList.getNumTypes())
        {
            PluginDescription pd = pluginList.getTypes()[index];
            createNewPlugin (PluginDescriptionAndPreference (std::move (pd)), sd.localPosition.toInt());
        }
    }
}

void GraphDocumentComponent::showSidePanel (bool isSettingsPanel)
{
    SidePanel* panel = isSettingsPanel ? &mobileSettingsSidePanel : &pluginListSidePanel;

    if (lastOpenedSidePanel != panel)
    {
        if (lastOpenedSidePanel != nullptr)
            lastOpenedSidePanel->setVisible (false);

        panel->setVisible (true);
        lastOpenedSidePanel = panel;
    }
    else
    {
        const bool nowVisible = ! panel->isVisible();
        panel->setVisible (nowVisible);
        if (! nowVisible)
            lastOpenedSidePanel = nullptr;
    }
}

void GraphDocumentComponent::hideLastSidePanel()
{
    if (lastOpenedSidePanel != nullptr)
    {
        lastOpenedSidePanel->setVisible (false);
        lastOpenedSidePanel = nullptr;
    }
}
