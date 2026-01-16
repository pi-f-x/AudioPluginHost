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

        // Always use touch-friendly delays for fixed touchscreen
        startTimer (500);

        if (! isOnTouchDevice() && e.mods.isPopupMenu())
            showPopupMenu();
    }

    void mouseDrag (const MouseEvent& e) override
    {
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
        stopTimer();
        callAfterDelay (250, []() { PopupMenu::dismissAllActiveMenus(); });

        if (e.mouseWasDraggedSinceMouseDown())
        {
            graph.setChangedFlag (true);
        }
        else if (e.getNumberOfClicks() == 2)
        {
            if (auto f = graph.graph.getNodeForId (pluginID))
                if (auto* w = graph.getOrCreateWindowFor (f, PluginWindow::Type::normal))
                    w->toFront (true);
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
        auto boxArea = getLocalBounds().reduced (4, pinSize);
        bool isBypassed = false;

        if (auto* f = graph.graph.getNodeForId (pluginID))
            isBypassed = f->isBypassed();

        auto boxColour = findColour (TextEditor::backgroundColourId);

        if (isBypassed)
            boxColour = boxColour.brighter();

        g.setColour (boxColour);
        g.fillRect (boxArea.toFloat());

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

    Point<float> getPinPos (int index, bool isInput) const
    {
        for (auto* pin : pins)
            if (pin->pin.channelIndex == index && isInput == pin->isInput)
                return getPosition().toFloat() + pin->getBounds().getCentre().toFloat();

        return {};
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

            resized();
        }
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
        menu->addItem ("Delete this filter", [this] { graph.graph.removeNode (pluginID); });
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
        showPopupMenu();
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
    int pinSize = 28;
    Point<int> originalPos;
    Font font;
    int numIns = 0, numOuts = 0;
    DropShadowEffect shadow;
    std::unique_ptr<PopupMenu> menu;
    std::unique_ptr<FileChooser> fileChooser;
    const String formatSuffix = getFormatSuffix (getProcessor());
};


//==============================================================================
struct GraphEditorPanel::ConnectorComponent final : public Component,
                                                    public SettableTooltipClient
{
    explicit ConnectorComponent (GraphEditorPanel& p)
        : panel (p), graph (p.graph)
    {
        setAlwaysOnTop (true);
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
        lastInputPos = pos;
        resizeToFit();
    }

    void dragEnd (Point<float> pos)
    {
        lastOutputPos = pos;
        resizeToFit();
    }

    void update()
    {
        Point<float> p1, p2;
        getPoints (p1, p2);

        if (lastInputPos != p1 || lastOutputPos != p2)
            resizeToFit();
    }

    void resizeToFit()
    {
        Point<float> p1, p2;
        getPoints (p1, p2);

        auto newBounds = Rectangle<float> (p1, p2).expanded (4.0f).getSmallestIntegerContainer();

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
            p1 = src->getPinPos (connection.source.channelIndex, false);

        if (auto* dest = panel.getComponentForPlugin (connection.destination.nodeID))
            p2 = dest->getPinPos (connection.destination.channelIndex, true);
    }

    void paint (Graphics& g) override
    {
        if (connection.source.isMIDI() || connection.destination.isMIDI())
            g.setColour (Colours::red);
        else
            g.setColour (Colours::green);

        g.fillPath (linePath);
    }

    bool hitTest (int x, int y) override
    {
        auto pos = Point<int> (x, y).toFloat();

        if (hitPath.contains (pos))
        {
            double distanceFromStart, distanceFromEnd;
            getDistancesFromEnds (pos, distanceFromStart, distanceFromEnd);

            // avoid clicking the connector when over a pin
            return distanceFromStart > 7.0 && distanceFromEnd > 7.0;
        }

        return false;
    }

    void mouseDown (const MouseEvent&) override
    {
        dragging = false;
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (dragging)
        {
            panel.dragConnector (e);
        }
        else if (e.mouseWasDraggedSinceMouseDown())
        {
            dragging = true;

            graph.graph.removeConnection (connection);

            double distanceFromStart, distanceFromEnd;
            getDistancesFromEnds (getPosition().toFloat() + e.position, distanceFromStart, distanceFromEnd);
            const bool isNearerSource = (distanceFromStart < distanceFromEnd);

            AudioProcessorGraph::NodeAndChannel dummy { {}, 0 };

            panel.beginConnectorDrag (isNearerSource ? dummy : connection.source,
                                      isNearerSource ? connection.destination : dummy,
                                      e);
        }
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (dragging)
            panel.endDraggingConnector (e);
    }

    void resized() override
    {
        Point<float> p1, p2;
        getPoints (p1, p2);

        lastInputPos = p1;
        lastOutputPos = p2;

        p1 -= getPosition().toFloat();
        p2 -= getPosition().toFloat();

        linePath.clear();
        linePath.startNewSubPath (p1);
        linePath.cubicTo (p1.x, p1.y + (p2.y - p1.y) * 0.33f,
                          p2.x, p1.y + (p2.y - p1.y) * 0.66f,
                          p2.x, p2.y);

        PathStrokeType wideStroke (8.0f);
        wideStroke.createStrokedPath (hitPath, linePath);

        PathStrokeType stroke (2.5f);
        stroke.createStrokedPath (linePath, linePath);

        auto arrowW = 5.0f;
        auto arrowL = 4.0f;

        Path arrow;
        arrow.addTriangle (-arrowL, arrowW,
                           -arrowL, -arrowW,
                           arrowL, 0.0f);

        arrow.applyTransform (AffineTransform()
                                .rotated (MathConstants<float>::halfPi - (float) atan2 (p2.x - p1.x, p2.y - p1.y))
                                .translated ((p1 + p2) * 0.5f));

        linePath.addPath (arrow);
        linePath.setUsingNonZeroWinding (true);
    }

    void getDistancesFromEnds (Point<float> p, double& distanceFromStart, double& distanceFromEnd) const
    {
        Point<float> p1, p2;
        getPoints (p1, p2);

        distanceFromStart = p1.getDistanceFrom (p);
        distanceFromEnd   = p2.getDistanceFrom (p);
    }

    GraphEditorPanel& panel;
    PluginGraph& graph;
    AudioProcessorGraph::Connection connection { { {}, 0 }, { {}, 0 } };
    Point<float> lastInputPos, lastOutputPos;
    Path linePath, hitPath;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConnectorComponent)
};


//==============================================================================
GraphEditorPanel::GraphEditorPanel (PluginGraph& g)  : graph (g)
{
    graph.addChangeListener (this);
    setOpaque (true);
}

GraphEditorPanel::~GraphEditorPanel()
{
    graph.removeChangeListener (this);
    draggingConnector = nullptr;
    nodes.clear();
    connectors.clear();
}

// Zusätzliche Implementierungen für GraphDocumentComponent (Linker-Fehler beheben)
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

void GraphEditorPanel::paint (Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (ResizableWindow::backgroundColourId));
}

void GraphEditorPanel::mouseDown (const MouseEvent& e)
{
    originalTouchPos = e.position.toInt();
    startTimer (750);

    if (e.mods.isPopupMenu())
        showPopupMenu (e.position.toInt());
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
    updateComponents();
}

void GraphEditorPanel::changeListenerCallback (ChangeBroadcaster*)
{
    updateComponents();
}

void GraphEditorPanel::updateComponents()
{
    for (int i = nodes.size(); --i >= 0;)
        if (graph.graph.getNodeForId (nodes.getUnchecked (i)->pluginID) == nullptr)
            nodes.remove (i);

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
}

void GraphEditorPanel::showPopupMenu (Point<int> mousePos)
{
    menu.reset (new PopupMenu);

    if (auto* mainWindow = findParentComponentOfClass<MainHostWindow>())
    {
        mainWindow->addPluginsToMenu (*menu);

        menu->showMenuAsync ({},
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

void GraphEditorPanel::dragConnector (const MouseEvent& e)
{
    auto e2 = e.getEventRelativeTo (this);

    if (draggingConnector != nullptr)
    {
        draggingConnector->setTooltip ({});

        auto pos = e2.position;

        if (auto* pin = findPinAt (pos))
        {
            auto connection = draggingConnector->connection;

            if (connection.source.nodeID == AudioProcessorGraph::NodeID() && ! pin->isInput)
            {
                connection.source = pin->pin;
            }
            else if (connection.destination.nodeID == AudioProcessorGraph::NodeID() && pin->isInput)
            {
                connection.destination = pin->pin;
            }

            if (graph.graph.canConnect (connection))
            {
                pos = (pin->getParentComponent()->getPosition() + pin->getBounds().getCentre()).toFloat();
                draggingConnector->setTooltip (pin->getTooltip());
            }
        }

        if (draggingConnector->connection.source.nodeID == AudioProcessorGraph::NodeID())
            draggingConnector->dragStart (pos);
        else
            draggingConnector->dragEnd (pos);
    }
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

        graph.graph.addConnection (connection);
    }
}

void GraphEditorPanel::timerCallback()
{
    stopTimer();
    showPopupMenu (originalTouchPos);
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
        // Larger font for 800x480 touchscreen
        g.setFont (FontOptions ((float) getHeight() * 0.75f, Font::bold));
        g.setColour (Colours::black);
        // More padding for better readability
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

//==============================================================================
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
            // Add padding for better readability
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

    // Fixed sizes for 800x480 touchscreen - no title bar
    const int statusHeight = 26;

    statusBar->setBounds (r.removeFromBottom (statusHeight));
    graphPanel->setBounds (r);

    checkAvailableWidth();
}

void GraphDocumentComponent::changeListenerCallback (ChangeBroadcaster* source)
{
    if (source == &deviceManager)
    {
        // Device manager changed
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
    if (graph)
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
