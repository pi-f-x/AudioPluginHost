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
            setSize (28, 28);
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
struct GraphEditorPanel::ControlPinComponent final : public Component,
                                                     public SettableTooltipClient
    {
        ControlPinComponent (Colour colourToUse, const String& tipText, bool inputStyle)
            : colour (colourToUse), isInput (inputStyle)
        {
            setTooltip (tipText);
            setSize (28, 28);
            setInterceptsMouseClicks (false, false);
        }

        void paint (Graphics& g) override
        {
            auto w = (float) getWidth();
            auto h = (float) getHeight();

            Path p;
            p.addEllipse (w * 0.25f, h * 0.25f, w * 0.5f, h * 0.5f);
            p.addRectangle (w * 0.4f, isInput ? (0.5f * h) : 0.0f, w * 0.2f, h * 0.5f);

            p.applyTransform (AffineTransform::rotation (-MathConstants<float>::halfPi, w * 0.5f, h * 0.5f));

            g.setColour (colour);
            g.fillPath (p);
        }

        Colour colour;
        const bool isInput;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlPinComponent)
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
            setSize (220, 100);
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
            
            // Check if in delete mode
            if (panel.isDeleteMode())
            {
                // Check if this is Audio Input or Audio Output - these cannot be deleted
                bool isEssentialNode = false;
                if (auto* processor = getProcessor())
                {
                    String name = processor->getName();
                    isEssentialNode = (name == "Audio Input" || name == "Audio Output");
                }
                
                if (!isEssentialNode)
                {
                    // Delete this plugin immediately
                    graph.graph.removeNode (pluginID);
                }
                return; // Don't proceed with normal drag behavior
            }

            // Normal behavior when not in delete mode
            startTimer (500);

            // Removed right-click menu functionality
        }

        void mouseDrag (const MouseEvent& e) override
        {
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

            return x >= 3 && x < getWidth() - 6 && y >= pinSize && y < getHeight() - pinSize;
        }

        void paint (Graphics& g) override
        {
            auto boxArea = getLocalBounds().reduced (4, pinSize).withTrimmedLeft (controlPinOffset);
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

                    if (! controlPins.isEmpty())
                    {
                        const int availableHeight = getHeight() - (pinSize * 2);
                        const float step = (controlPins.size() + 1) > 0 ? (availableHeight / (float) (controlPins.size() + 1)) : 0.0f;
                        const int controlPinX = controlPinOffset;

                        for (int i = 0; i < controlPins.size(); ++i)
                        {
                            const int centreY = roundToInt (pinSize + ((float) (i + 1) * step));
                            controlPins[i]->setBounds (controlPinX, centreY - controlPinSize / 2, controlPinSize, controlPinSize);
                        }
                    }
                }
            }
        }

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

            w = jmax (w, (jmax (numIns, numOuts) + 1) * 32);

            const auto textWidth = GlyphArrangement::getStringWidthInt (font, processor.getName());
            w = jmax (w, 24 + jmin (textWidth, 400));
            if (textWidth > 400)
                h = 120;

            setSize (w + controlPinOffset, h);
            setName (processor.getName() + formatSuffix);

            {
                auto p = graph.getNodePosition (pluginID);
                setCentreRelative ((float) p.x, (float) p.y);
            }

            if (numIns != numOutputs)
            {
                numOutputs = numIns;

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

            const bool isEssentialNode = (processor.getName() == "Audio Input" || processor.getName() == "Audio Output");
            const bool hasBypassPin = (processor.getBypassParameter() != nullptr);
            const int totalParams = processor.getParameters().size();
            const int bluePinCount = jmax (0, totalParams - (hasBypassPin ? 1 : 0));
            const int desiredControlPins = isEssentialNode ? 0 : (bluePinCount + (hasBypassPin ? 1 : 0));

            if (controlPins.size() != desiredControlPins || controlBluePins != bluePinCount || controlHasBypass != hasBypassPin)
            {
                controlPins.clear();

                if (! isEssentialNode)
                {
                    for (int i = 0; i < bluePinCount; ++i)
                        addAndMakeVisible (controlPins.add (new ControlPinComponent (Colours::blue, "Control " + String (i + 1), true)));

                    if (hasBypassPin)
                        addAndMakeVisible (controlPins.add (new ControlPinComponent (Colours::red, "Bypass", true)));
                }

                controlBluePins = bluePinCount;
                controlHasBypass = hasBypassPin;
            }

            resized();
        }

        AudioProcessor* getProcessor() const
        {
            if (auto node = graph.graph.getNodeForId (pluginID))
                return node->getProcessor();

            return {};
        }

        bool isNodeUsingARA() const
        {
            if (auto node = graph.graph.getNodeForId (pluginID))
                return node->properties["useARA"];

            return false;
        }

        void showPopupMenu()
        {
            menu.reset (new PopupMenu);
            
            // Check if this is Audio Input or Audio Output - these cannot be deleted
            bool isEssentialNode = false;
            if (auto* processor = getProcessor())
            {
                String name = processor->getName();
                isEssentialNode = (name == "Audio Input" || name == "Audio Output");
            }
            
            // Only add delete option if not an essential node
            if (!isEssentialNode)
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
            menu->addItem ("Show debug log", [this] { showWindow (PluginWindow::Type::log); });

            menu->addSeparator();
            menu->addItem ("Close menu", [this] { PopupMenu::dismissAllActiveMenus(); });

            menu->show();
        }

    private:
        void parameterValueChanged (AudioProcessorParameter* param) override
        {
            if (param->getUserData() == nullptr)
                return;

            // Control pins and bypass pin use the same listener
            if (auto* value = (float*) param->getUserData())
            {
                // Value changes should be reflected in the control pin graphics
                // as well as the bypass button.
                update();
            }
        }

        void handleAsyncUpdate() override
        {
            update();
        }

        GraphEditorPanel& panel;
        PluginGraph& graph;
        AudioProcessorGraph::NodeID pluginID;

        OwnedArray<PinComponent> pins;
        OwnedArray<ControlPinComponent> controlPins;

        int numInputs = 0, numOutputs = 0;
        int numIns = 0, numOuts = 0;

        int pinSize = 20;
        int controlPinSize = 18;

        String formatSuffix;

        FontOptions font;

        DropShadowEffect shadow;

        int controlBluePins = 0;
        bool controlHasBypass = false;

        std::unique_ptr<PopupMenu> menu;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginComponent)
    };

//==============================================================================
GraphEditorPanel::GraphEditorPanel (MainHostWindow& owner)
    : ownerWindow (owner),
      graph (owner.getGraph())
{
    // Make this panel non-resizable
    setResizeLimits (800, 480, 800, 480);
        
    // Default size for touch-friendly interface
    setSize (800, 480);
    
    setOpaque (true);

    addMouseListener (this, false);
}

GraphEditorPanel::~GraphEditorPanel()
{
    // Ensure all timers are stopped
    stopTimer();
}

void GraphEditorPanel::paint (Graphics& g)
{
    g.fillAll (findColour (Props::backgroundColourId));

    // Optionally draw a grid or other background elements
}

void GraphEditorPanel::resized()
{
    // In an effort to reduce flickering, the panel is now invalidated
    // only when absolutely necessary.
    if (! needsToUpdate)
        return;

    needsToUpdate = false;

    // Call to rearrange/connect all nodes and pins
    updateComponents();
}

void GraphEditorPanel::updateComponents()
{
    if (graph == nullptr)
        return;

    // Update the position and state of all plugins and cables
    for (auto* node : graph->graph.getNodes())
    {
        if (node != nullptr)
        {
            if (auto* pluginComp = findChildComponent<PluginComponent> (*node))
            {
                pluginComp->update();
                pluginComp->toFront (false);
            }
        }
    }

    // Only need to do this once we have valid plugin components
    for (auto* node : graph->graph.getNodes())
    {
        if (node != nullptr)
        {
            if (auto* pluginComp = findChildComponent<PluginComponent> (*node))
            {
                // Connectors are now drawn in the PluginComponent class
            }
        }
    }

    // If there are no plugins, show background image/text
    if (graph->graph.getNodes().size() == 0)
    {
        if (noPluginsImage.isNull())
        {
            noPluginsImage = ImageCache::getFromMemory (BinaryData::no_plugins_svg,
                                                        BinaryData::no_plugins_svg_size);
        }

        if (noPluginsImage.isValid())
        {
            Graphics g (getGraphics());
            g.drawImage (noPluginsImage, getLocalBounds().toFloat());
        }
    }
}

void GraphEditorPanel::showPluginEditorInSidePanel (AudioProcessorGraph::NodeID pluginID)
{
    if (auto* editorPanel = ownerWindow.getPluginEditorPanel())
    {
        editorPanel->setProcessor (graph.getNodeProcessor (pluginID));

        ownerWindow.setCentralPanel (editorPanel);
    }
}

void GraphEditorPanel::setGraph (ManagedGraph* newGraph)
{
    if (graph != newGraph)
    {
        graph = newGraph;
        
        clearComponents();   // Removes and deletes all child components
        resetActions();      // Resets the history actions (undo/redo)
        
        if (graph != nullptr)
        {
            graph->addListener (this);
            
            // Create all nodes
            for (auto* node : graph->graph.getNodes())
                createNodeComponent (*node);
        }

        resized();
    }
}

void GraphEditorPanel::createNodeComponent (AudioProcessorGraph::Node& node)
{
    if (node.getProcessor() != nullptr)
    {
        if (auto* pluginComp = new PluginComponent (*this, node.getId()))
        {
            pluginComp->setName (node.getProcessor()->getName());
            addAndMakeVisible (pluginComp);

            // Set the initial position based on the graph's data
            auto pos = graph->getNodePosition (node.getId());
            pluginComp->setTopLeftPosition (roundToInt (pos.x * getWidth()),
                                            roundToInt (pos.y * getHeight()));
        }
    }
}

void GraphEditorPanel::removeNodeComponent (AudioProcessorGraph::Node& node)
{
    if (auto* pluginComp = findChildComponent<PluginComponent> (node))
    {
        pluginComp->stopTimer();
        pluginComp->clearComponentEffect();
        pluginComp->removeFromParentComponent ();
    }
}

void GraphEditorPanel::setNodePosition (AudioProcessorGraph::NodeID nodeID, Point<double> newPos)
{
    if (graph != nullptr)
    {
        graph->setNodePosition (nodeID, newPos);
        
        if (auto* pluginComp = findChildComponent<PluginComponent> (nodeID))
        {
            pluginComp->setTopLeftPosition (roundToInt (newPos.x * getWidth()),
                                            roundToInt (newPos.y * getHeight()));
        }
    }
}

void GraphEditorPanel::beginConnectorDrag (AudioProcessorGraph::NodeAndChannel startPin,
                                           AudioProcessorGraph::NodeAndChannel endPin,
                                           const MouseEvent& e)
{
    if (dragConnection != nullptr)
        return;

    dragConnection.reset (new ConnectorLine (startPin, endPin, true));

    // Use the mouse cursor position as the initial control point
    dragConnection->setControlPoint (e.getPosition().toFloat());

    addAndMakeVisible (dragConnection.get());
}

void GraphEditorPanel::dragConnector (const MouseEvent& e)
{
    if (dragConnection != nullptr)
    {
        // Update the control point of the connection line to follow the mouse
        dragConnection->setControlPoint (e.getPosition().toFloat());
    }
}

void GraphEditorPanel::endDraggingConnector (const MouseEvent& e)
{
    if (dragConnection != nullptr)
    {
        // Finalize the connection
        dragConnection->setEnd (e.getPosition().toFloat());

        // Here you would typically invoke the code to create a real audio connection
        // in the audio processor graph, using the start and end pins of the connection.

        // For demonstration, we'll just log the connection info:
        DBG ("Connecting " << dragConnection->getStartPin() << " to " << dragConnection->getEndPin());

        // Clean up
        dragConnection.reset ();
    }
}

void GraphEditorPanel::mouseDown (const MouseEvent& e)
{
    // Check for middle mouse button - toggle delete mode
    if (e.mods.isMiddleButton())
    {
        toggleDeleteMode();
        return;
    }

    // Ignore other mouse events for now
}

void GraphEditorPanel::toggleDeleteMode()
{
    deleteMode = ! deleteMode;

    // Update the UI to reflect the delete mode status
    for (auto* comp : getChildren())
    {
        if (auto* pluginComp = dynamic_cast<PluginComponent*> (comp))
        {
            // Optionally change the appearance of plugins in delete mode
            pluginComp->setColour (TextEditor::backgroundColourId, deleteMode ? Colours::red : Colours::white);
        }
    }

    // Change the mouse cursor to indicate delete mode
    Window::setMouseCursor (deleteMode ? MouseCursor::NoPointer : MouseCursor::Normal);
}

void GraphEditorPanel::showPluginMenu (const Array<PluginDescriptionAndPreference>& plugins,
                                        const Point<float>& position)
{
    // Hide any existing menu
    hidePluginMenu();

    auto* menu = new FloatingPluginMenu (*this, plugins);
    addAndMakeVisible (menu);

    menu->setTopLeftPosition (position.x, position.y);
    menu->toFront (true);
}

void GraphEditorPanel::hidePluginMenu()
{
    for (auto* comp : getChildren())
    {
        if (dynamic_cast<FloatingPluginMenu*> (comp))
        {
            comp->removeFromParentComponent ();
            break;
        }
    }
}

void GraphEditorPanel::togglePluginMenu (const Array<PluginDescriptionAndPreference>& plugins,
                                          const Point<float>& position)
{
    for (auto* comp : getChildren())
    {
        if (auto* menu = dynamic_cast<FloatingPluginMenu*> (comp))
        {
            if (menu->isVisible())
            {
                menu->hidePluginMenu();
                return;
            }
        }
    }

    showPluginMenu (plugins, position);
}

void GraphEditorPanel::setDeleteMode (bool shouldDelete)
{
    deleteMode = shouldDelete;
}

void GraphEditorPanel::handleAsyncUpdate()
{
    // Repaint the panel to reflect any changes
    repaint();
}

void GraphEditorPanel::parameterValueChanged (AudioProcessorParameter* param)
{
    // Respond to parameter changes from plugins
    // For example, we might want to update knobs or displays in response to automation
}

//==============================================================================
FloatingPluginMenu::FloatingPluginMenu (GraphEditorPanel& p, Array<PluginDescriptionAndPreference> plugins)
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

void FloatingPluginMenu::paint (Graphics& g)
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

void FloatingPluginMenu::resized()
{
    auto bounds = getLocalBounds().reduced (20);
    bounds.removeFromTop (60); // Space for title
    
    // Viewport takes remaining space
    viewport.setBounds (bounds);
}

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
            setSize (28, 28);
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
struct GraphEditorPanel::ControlPinComponent final : public Component,
                                                     public SettableTooltipClient
    {
        ControlPinComponent (Colour colourToUse, const String& tipText, bool inputStyle)
            : colour (colourToUse), isInput (inputStyle)
        {
            setTooltip (tipText);
            setSize (28, 28);
            setInterceptsMouseClicks (false, false);
        }

        void paint (Graphics& g) override
        {
            auto w = (float) getWidth();
            auto h = (float) getHeight();

            Path p;
            p.addEllipse (w * 0.25f, h * 0.25f, w * 0.5f, h * 0.5f);
            p.addRectangle (w * 0.4f, isInput ? (0.5f * h) : 0.0f, w * 0.2f, h * 0.5f);

            p.applyTransform (AffineTransform::rotation (-MathConstants<float>::halfPi, w * 0.5f, h * 0.5f));

            g.setColour (colour);
            g.fillPath (p);
        }

        Colour colour;
        const bool isInput;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlPinComponent)
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
            setSize (220, 100);
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
            
            // Check if in delete mode
            if (panel.isDeleteMode())
            {
                // Check if this is Audio Input or Audio Output - these cannot be deleted
                bool isEssentialNode = false;
                if (auto* processor = getProcessor())
                {
                    String name = processor->getName();
                    isEssentialNode = (name == "Audio Input" || name == "Audio Output");
                }
                
                if (!isEssentialNode)
                {
                    // Delete this plugin immediately
                    graph.graph.removeNode (pluginID);
                }
                return; // Don't proceed with normal drag behavior
            }

            // Normal behavior when not in delete mode
            startTimer (500);

            // Removed right-click menu functionality
        }

        void mouseDrag (const MouseEvent& e) override
        {
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

            return x >= 3 && x < getWidth() - 6 && y >= pinSize && y < getHeight() - pinSize;
        }

        void paint (Graphics& g) override
        {
            auto boxArea = getLocalBounds().reduced (4, pinSize).withTrimmedLeft (controlPinOffset);
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

                    if (! controlPins.isEmpty())
                    {
                        const int availableHeight = getHeight() - (pinSize * 2);
                        const float step = (controlPins.size() + 1) > 0 ? (availableHeight / (float) (controlPins.size() + 1)) : 0.0f;
                        const int controlPinX = controlPinOffset;

                        for (int i = 0; i < controlPins.size(); ++i)
                        {
                            const int centreY = roundToInt (pinSize + ((float) (i + 1) * step));
                            controlPins[i]->setBounds (controlPinX, centreY - controlPinSize / 2, controlPinSize, controlPinSize);
                        }
                    }
                }
            }
        }

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

            w = jmax (w, (jmax (numIns, numOuts) + 1) * 32);

            const auto textWidth = GlyphArrangement::getStringWidthInt (font, processor.getName());
            w = jmax (w, 24 + jmin (textWidth, 400));
            if (textWidth > 400)
                h = 120;

            setSize (w + controlPinOffset, h);
            setName (processor.getName() + formatSuffix);

            {
                auto p = graph.getNodePosition (pluginID);
                setCentreRelative ((float) p.x, (float) p.y);
            }

            if (numIns != numOutputs)
            {
                numOutputs = numIns;

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

            const bool isEssentialNode = (processor.getName() == "Audio Input" || processor.getName() == "Audio Output");
            const bool hasBypassPin = (processor.getBypassParameter() != nullptr);
            const int totalParams = processor.getParameters().size();
            const int bluePinCount = jmax (0, totalParams - (hasBypassPin ? 1 : 0));
            const int desiredControlPins = isEssentialNode ? 0 : (bluePinCount + (hasBypassPin ? 1 : 0));

            if (controlPins.size() != desiredControlPins || controlBluePins != bluePinCount || controlHasBypass != hasBypassPin)
            {
                controlPins.clear();

                if (! isEssentialNode)
                {
                    for (int i = 0; i < bluePinCount; ++i)
                        addAndMakeVisible (controlPins.add (new ControlPinComponent (Colours::blue, "Control " + String (i + 1), true)));

                    if (hasBypassPin)
                        addAndMakeVisible (controlPins.add (new ControlPinComponent (Colours::red, "Bypass", true)));
                }

                controlBluePins = bluePinCount;
                controlHasBypass = hasBypassPin;
            }

            resized();
        }

        AudioProcessor* getProcessor() const
        {
            if (auto node = graph.graph.getNodeForId (pluginID))
                return node->getProcessor();

            return {};
        }

        bool isNodeUsingARA() const
        {
            if (auto node = graph.graph.getNodeForId (pluginID))
                return node->properties["useARA"];

            return false;
        }

        void showPopupMenu()
        {
            menu.reset (new PopupMenu);
            
            // Check if this is Audio Input or Audio Output - these cannot be deleted
            bool isEssentialNode = false;
            if (auto* processor = getProcessor())
            {
                String name = processor->getName();
                isEssentialNode = (name == "Audio Input" || name == "Audio Output");
            }
            
            // Only add delete option if not an essential node
            if (!isEssentialNode)
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
            menu->addItem ("Show debug log", [this] { showWindow (PluginWindow::Type::log); });

            menu->addSeparator();
            menu->addItem ("Close menu", [this] { PopupMenu::dismissAllActiveMenus(); });

            menu->show();
        }

    private:
        void parameterValueChanged (AudioProcessorParameter* param) override
        {
            if (param->getUserData() == nullptr)
                return;

            // Control pins and bypass pin use the same listener
            if (auto* value = (float*) param->getUserData())
            {
                // Value changes should be reflected in the control pin graphics
                // as well as the bypass button.
                update();
            }
        }

        void handleAsyncUpdate() override
        {
            update();
        }

        GraphEditorPanel& panel;
        PluginGraph& graph;
        AudioProcessorGraph::NodeID pluginID;

        OwnedArray<PinComponent> pins;
        OwnedArray<ControlPinComponent> controlPins;

        int numInputs = 0, numOutputs = 0;
        int numIns = 0, numOuts = 0;

        int pinSize = 20;
        int controlPinSize = 18;

        String formatSuffix;

        FontOptions font;

        DropShadowEffect shadow;

        int controlBluePins = 0;
        bool controlHasBypass = false;

        std::unique_ptr<PopupMenu> menu;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginComponent)
    };

//==============================================================================
GraphEditorPanel::GraphEditorPanel (MainHostWindow& owner)
    : ownerWindow (owner),
      graph (owner.getGraph())
{
    // Make this panel non-resizable
    setResizeLimits (800, 480, 800, 480);
        
    // Default size for touch-friendly interface
    setSize (800, 480);
    
    setOpaque (true);

    addMouseListener (this, false);
}

GraphEditorPanel::~GraphEditorPanel()
{
    // Ensure all timers are stopped
    stopTimer();
}

void GraphEditorPanel::paint (Graphics& g)
{
    g.fillAll (findColour (Props::backgroundColourId));

    // Optionally draw a grid or other background elements
}

void GraphEditorPanel::resized()
{
    // In an effort to reduce flickering, the panel is now invalidated
    // only when absolutely necessary.
    if (! needsToUpdate)
        return;

    needsToUpdate = false;

    // Call to rearrange/connect all nodes and pins
    updateComponents();
}

void GraphEditorPanel::updateComponents()
{
    if (graph == nullptr)
        return;

    // Update the position and state of all plugins and cables
    for (auto* node : graph->graph.getNodes())
    {
        if (node != nullptr)
        {
            if (auto* pluginComp = findChildComponent<PluginComponent> (*node))
            {
                pluginComp->update();
                pluginComp->toFront (false);
            }
        }
    }

    // Only need to do this once we have valid plugin components
    for (auto* node : graph->graph.getNodes())
    {
        if (node != nullptr)
        {
            if (auto* pluginComp = findChildComponent<PluginComponent> (*node))
            {
                // Connectors are now drawn in the PluginComponent class
            }
        }
    }

    // If there are no plugins, show background image/text
    if (graph->graph.getNodes().size() == 0)
    {
        if (noPluginsImage.isNull())
        {
            noPluginsImage = ImageCache::getFromMemory (BinaryData::no_plugins_svg,
                                                        BinaryData::no_plugins_svg_size);
        }

        if (noPluginsImage.isValid())
        {
            Graphics g (getGraphics());
            g.drawImage (noPluginsImage, getLocalBounds().toFloat());
        }
    }
}

void GraphEditorPanel::showPluginEditorInSidePanel (AudioProcessorGraph::NodeID pluginID)
{
    if (auto* editorPanel = ownerWindow.getPluginEditorPanel())
    {
        editorPanel->setProcessor (graph.getNodeProcessor (pluginID));

        ownerWindow.setCentralPanel (editorPanel);
    }
}

void GraphEditorPanel::setGraph (ManagedGraph* newGraph)
{
    if (graph != newGraph)
    {
        graph = newGraph;
        
        clearComponents();   // Removes and deletes all child components
        resetActions();      // Resets the history actions (undo/redo)
        
        if (graph != nullptr)
        {
            graph->addListener (this);
            
            // Create all nodes
            for (auto* node : graph->graph.getNodes())
                createNodeComponent (*node);
        }

        resized();
    }
}

void GraphEditorPanel::createNodeComponent (AudioProcessorGraph::Node& node)
{
    if (node.getProcessor() != nullptr)
    {
        if (auto* pluginComp = new PluginComponent (*this, node.getId()))
        {
            pluginComp->setName (node.getProcessor()->getName());
            addAndMakeVisible (pluginComp);

            // Set the initial position based on the graph's data
            auto pos = graph->getNodePosition (node.getId());
            pluginComp->setTopLeftPosition (roundToInt (pos.x * getWidth()),
                                            roundToInt (pos.y * getHeight()));
        }
    }
}

void GraphEditorPanel::removeNodeComponent (AudioProcessorGraph::Node& node)
{
    if (auto* pluginComp = findChildComponent<PluginComponent> (node))
    {
        pluginComp->stopTimer();
        pluginComp->clearComponentEffect();
        pluginComp->removeFromParentComponent ();
    }
}

void GraphEditorPanel::setNodePosition (AudioProcessorGraph::NodeID nodeID, Point<double> newPos)
{
    if (graph != nullptr)
    {
        graph->setNodePosition (nodeID, newPos);
        
        if (auto* pluginComp = findChildComponent<PluginComponent> (nodeID))
        {
            pluginComp->setTopLeftPosition (roundToInt (newPos.x * getWidth()),
                                            roundToInt (newPos.y * getHeight()));
        }
    }
}

void GraphEditorPanel::beginConnectorDrag (AudioProcessorGraph::NodeAndChannel startPin,
                                           AudioProcessorGraph::NodeAndChannel endPin,
                                           const MouseEvent& e)
{
    if (dragConnection != nullptr)
        return;

    dragConnection.reset (new ConnectorLine (startPin, endPin, true));

    // Use the mouse cursor position as the initial control point
    dragConnection->setControlPoint (e.getPosition().toFloat());

    addAndMakeVisible (dragConnection.get());
}

void GraphEditorPanel::dragConnector (const MouseEvent& e)
{
    if (dragConnection != nullptr)
    {
        // Update the control point of the connection line to follow the mouse
        dragConnection->setControlPoint (e.getPosition().toFloat());
    }
}

void GraphEditorPanel::endDraggingConnector (const MouseEvent& e)
{
    if (dragConnection != nullptr)
    {
        // Finalize the connection
        dragConnection->setEnd (e.getPosition().toFloat());

        // Here you would typically invoke the code to create a real audio connection
        // in the audio processor graph, using the start and end pins of the connection.

        // For demonstration, we'll just log the connection info:
        DBG ("Connecting " << dragConnection->getStartPin() << " to " << dragConnection->getEndPin());

        // Clean up
        dragConnection.reset ();
    }
}

void GraphEditorPanel::mouseDown (const MouseEvent& e)
{
    // Check for middle mouse button - toggle delete mode
    if (e.mods.isMiddleButton())
    {
        toggleDeleteMode();
        return;
    }

    // Ignore other mouse events for now
}

void GraphEditorPanel::toggleDeleteMode()
{
    deleteMode = ! deleteMode;

    // Update the UI to reflect the delete mode status
    for (auto* comp : getChildren())
    {
        if (auto* pluginComp = dynamic_cast<PluginComponent*> (comp))
        {
            // Optionally change the appearance of plugins in delete mode
            pluginComp->setColour (TextEditor::backgroundColourId, deleteMode ? Colours::red : Colours::white);
        }
    }

    // Change the mouse cursor to indicate delete mode
    Window::setMouseCursor (deleteMode ? MouseCursor::NoPointer : MouseCursor::Normal);
}

void GraphEditorPanel::showPluginMenu (const Array<PluginDescriptionAndPreference>& plugins,
                                        const Point<float>& position)
{
    // Hide any existing menu
    hidePluginMenu();

    auto* menu = new FloatingPluginMenu (*this, plugins);
    addAndMakeVisible (menu);

    menu->setTopLeftPosition (position.x, position.y);
    menu->toFront (true);
}

void GraphEditorPanel::hidePluginMenu()
{
    for (auto* comp : getChildren())
    {
        if (dynamic_cast<FloatingPluginMenu*> (comp))
        {
            comp->removeFromParentComponent ();
            break;
        }
    }
}

void GraphEditorPanel::togglePluginMenu (const Array<PluginDescriptionAndPreference>& plugins,
                                          const Point<float>& position)
{
    for (auto* comp : getChildren())
    {
        if (auto* menu = dynamic_cast<FloatingPluginMenu*> (comp))
        {
            if (menu->isVisible())
            {
                menu->hidePluginMenu();
                return;
            }
        }
    }

    showPluginMenu (plugins, position);
}

void GraphEditorPanel::setDeleteMode (bool shouldDelete)
{
    deleteMode = shouldDelete;
}

void GraphEditorPanel::handleAsyncUpdate()
{
    // Repaint the panel to reflect any changes
    repaint();
}

void GraphEditorPanel::parameterValueChanged (AudioProcessorParameter* param)
{
    // Respond to parameter changes from plugins
    // For example, we might want to update knobs or displays in response to automation
}

//==============================================================================
FloatingPluginMenu::FloatingPluginMenu (GraphEditorPanel& p, Array<PluginDescriptionAndPreference> plugins)
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

void FloatingPluginMenu::paint (Graphics& g)
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

void FloatingPluginMenu::resized()
{
    auto bounds = getLocalBounds().reduced (20);
    bounds.removeFromTop (60); // Space for title
    
    // Viewport takes remaining space
    viewport.setBounds (bounds);
}

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
            setSize (28, 28);
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
struct GraphEditorPanel::ControlPinComponent final : public Component,
                                                     public SettableTooltipClient
    {
        ControlPinComponent (Colour colourToUse, const String& tipText, bool inputStyle)
            : colour (colourToUse), isInput (inputStyle)
        {
            setTooltip (tipText);
            setSize (28, 28);
            setInterceptsMouseClicks (false, false);
        }

        void paint (Graphics& g) override
        {
            auto w = (float) getWidth();
            auto h = (float) getHeight();

            Path p;
            p.addEllipse (w * 0.25f, h * 0.25f, w * 0.5f, h * 0.5f);
            p.addRectangle (w * 0.4f, isInput ? (0.5f * h) : 0.0f, w * 0.2f, h * 0.5f);

            p.applyTransform (AffineTransform::rotation (-MathConstants<float>::halfPi, w * 0.5f, h * 0.5f));

            g.setColour (colour);
            g.fillPath (p);
        }

        Colour colour;
        const bool isInput;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlPinComponent)
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
            setSize (220, 100);
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
            
            // Check if in delete mode
            if (panel.isDeleteMode())
            {
                // Check if this is Audio Input or Audio Output - these cannot be deleted
                bool isEssentialNode = false;
                if (auto* processor = getProcessor())
                {
                    String name = processor->getName();
                    isEssentialNode = (name == "Audio Input" || name == "Audio Output");
                }
                
                if (!isEssentialNode)
                {
                    // Delete this plugin immediately
                    graph.graph.removeNode (pluginID);
                }
                return; // Don't proceed with normal drag behavior
            }

            // Normal behavior when not in delete mode
            startTimer (500);

            // Removed right-click menu functionality
        }

        void mouseDrag (const MouseEvent& e) override
        {
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

            return x >= 3 && x < getWidth() - 6 && y >= pinSize && y < getHeight() - pinSize;
        }

        void paint (Graphics& g) override
        {
            auto boxArea = getLocalBounds().reduced (4, pinSize).withTrimmedLeft (controlPinOffset);
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

                    if (! controlPins.isEmpty())
                    {
                        const int availableHeight = getHeight() - (pinSize * 2);
                        const float step = (controlPins.size() + 1) > 0 ? (availableHeight / (float) (controlPins.size() + 1)) : 0.0f;
                        const int controlPinX = controlPinOffset;

                        for (int i = 0; i < controlPins.size(); ++i)
                        {
                            const int centreY = roundToInt (pinSize + ((float) (i + 1) * step));
                            controlPins[i]->setBounds (controlPinX, centreY - controlPinSize / 2, controlPinSize, controlPinSize);
                        }
                    }
                }
            }
        }

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

            w = jmax (w, (jmax (numIns, numOuts) + 1) * 32);

            const auto textWidth = GlyphArrangement::getStringWidthInt (font, processor.getName());
            w = jmax (w, 24 + jmin (textWidth, 400));
            if (textWidth > 400)
                h = 120;

            setSize (w + controlPinOffset, h);
            setName (processor.getName() + formatSuffix);

            {
                auto p = graph.getNodePosition (pluginID);
                setCentreRelative ((float) p.x, (float) p.y);
            }

            if (numIns != numOutputs)
            {
                numOutputs = numIns;

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

            const bool isEssentialNode = (processor.getName() == "Audio Input" || processor.getName() == "Audio Output");
            const bool hasBypassPin = (processor.getBypassParameter() != nullptr);
            const int totalParams = processor.getParameters().size();
            const int bluePinCount = jmax (0, totalParams - (hasBypassPin ? 1 : 0));
            const int desiredControlPins = isEssentialNode ? 0 : (bluePinCount + (hasBypassPin ? 1 : 0));

            if (controlPins.size() != desiredControlPins || controlBluePins != bluePinCount || controlHasBypass != hasBypassPin)
            {
                controlPins.clear();

                if (! isEssentialNode)
                {
                    for (int i = 0; i < bluePinCount; ++i)
                        addAndMakeVisible (controlPins.add (new ControlPinComponent (Colours::blue, "Control " + String (i + 1), true)));

                    if (hasBypassPin)
                        addAndMakeVisible (controlPins.add (new ControlPinComponent (Colours::red, "Bypass", true)));
                }

                controlBluePins = bluePinCount;
                controlHasBypass = hasBypassPin;
            }

            resized();
        }

        AudioProcessor* getProcessor() const
        {
            if (auto node = graph.graph.getNodeForId (pluginID))
                return node->getProcessor();

            return {};
        }

        bool isNodeUsingARA() const
        {
            if (auto node = graph.graph.getNodeForId (pluginID))
                return node->properties["useARA"];

            return false;
        }

        void showPopupMenu()
        {
            menu.reset (new PopupMenu);
            
            // Check if this is Audio Input or Audio Output - these cannot be deleted
            bool isEssentialNode = false;
            if (auto* processor = getProcessor())
            {
                String name = processor->getName();
                isEssentialNode = (name == "Audio Input" || name == "Audio Output");
            }
            
            // Only add delete option if not an essential node
            if (!isEssentialNode)
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
            menu->addItem ("Show debug log", [this] { showWindow (PluginWindow::Type::log); });

            menu->addSeparator();
            menu->addItem ("Close menu", [this] { PopupMenu::dismissAllActiveMenus(); });

            menu->show();
        }

    private:
        void parameterValueChanged (AudioProcessorParameter* param) override
        {
            if (param->getUserData() == nullptr)
                return;

            // Control pins and bypass pin use the same listener
            if (auto* value = (float*) param->getUserData())
            {
                // Value changes should be reflected in the control pin graphics
                // as well as the bypass button.
                update();
            }
        }

        void handleAsyncUpdate() override
        {
            update();
        }

        GraphEditorPanel& panel;
        PluginGraph& graph;
        AudioProcessorGraph::NodeID pluginID;

        OwnedArray<PinComponent> pins;
        OwnedArray<ControlPinComponent> controlPins;

        int numInputs = 0, numOutputs = 0;
        int numIns = 0, numOuts = 0;

        int pinSize = 20;
        int controlPinSize = 18;

        String formatSuffix;

        FontOptions font;

        DropShadowEffect shadow;

        int controlBluePins = 0;
        bool controlHasBypass = false;

        std::unique_ptr<PopupMenu> menu;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginComponent)
    };
