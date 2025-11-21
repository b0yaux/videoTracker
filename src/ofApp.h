//
//  ofApp.h
//
//  Audiovisual Sequencer Example - Time + Sound + Video coordination
//

#pragma once

#include "ofMain.h"
#include "ofxSoundObjects.h"
#include "ofxVisualObjects.h"
#include "MediaPlayer.h"
#include "MediaPool.h"
#include "MediaPoolGUI.h"
#include "TrackerSequencer.h"
#include "TrackerSequencerGUI.h"
#include "Pattern.h"
#include "Clock.h"
#include "ClockGUI.h"
#include "ImGuiIntegration.h"  // Direct ImGui integration wrapper (replaces ofxImGui)
#include <imgui_internal.h> // For DockBuilder API (now resolves to addons/imgui/ version 1.92.5)
#include <implot.h>
#include "gui/MenuBar.h"
#include "gui/ViewManager.h"
#include "gui/GUIManager.h"
#include "gui/Console.h"
#include "gui/FileBrowser.h"
#include "input/InputRouter.h"
#include "Module.h"
#include "core/ModuleFactory.h"
#include "core/ModuleRegistry.h"
#include "core/ParameterRouter.h"
#include "core/SessionManager.h"

class ofApp : public ofBaseApp {
public:
    ~ofApp() noexcept;
    void setup();
    void update();
    void draw();
    void exit();
    
    void keyPressed(ofKeyEventArgs& keyEvent);
    void mousePressed(int x, int y, int button);
    void windowResized(int w, int h);
    
    // Drag and drop support
    void dragEvent(ofDragInfo dragInfo);
    
    // Audio callbacks
    void audioOut(ofSoundBuffer& buffer);
    
    // Step event handler for TrackerSequencer
    void onTrackerStepEvent(int step, float duration, const PatternCell& cell);
    
private:
    // Time objects
    Clock clock;
    ClockGUI clockGUI;
    
    // Module management system (Phase 1: Core Architecture)
    ModuleFactory moduleFactory;
    ModuleRegistry moduleRegistry;
    ParameterRouter parameterRouter;
    SessionManager sessionManager;
    
    // Module instances (stored as shared_ptr, accessed via registry)
    // Keep raw pointers for backward compatibility with existing code during migration
    std::shared_ptr<TrackerSequencer> trackerSequencer;
    std::shared_ptr<MediaPool> mediaPool;
    
    // GUI management (Phase 3: Multiple Instances)
    GUIManager guiManager;
    
    // GUI components (legacy - kept for backward compatibility during migration)
    MediaPoolGUI mediaPoolGUI;
    TrackerSequencerGUI trackerSequencerGUI;
    
    // Sound objects
    ofxSoundOutput soundOutput;
    
    // Visual objects
    ofxVisualOutput visualOutput;
    
    // No bridge system needed - direct connections only
    
    // Audio system
    ofSoundStream soundStream;
    ofSoundBuffer soundBuffer;
    
    // GUI system (using direct ImGui integration via wrapper)
    ImGuiIntegration gui;
    
    // GUI managers
    MenuBar menuBar;
    ViewManager viewManager;
    InputRouter inputRouter;
    Console console;
    FileBrowser fileBrowser;  // File browser panel
    
    // GUI state
    bool showGUI = true;
    bool showDemoWindow = false;
    // Note: isPlaying removed - use clock.isPlaying() directly (Clock is single source of truth for transport)
    int numSteps = 16;
    
    // Note: Audio state (devices, volume, level) is now managed by ViewManager
    
    // Current step for GUI display (last triggered step)
    int currentStep = 0;
    int lastTriggeredStep = 0;  // Track the last step that was actually triggered
    
    // Methods
    void setupSoundObjects();  // Minimal setup - audio device management is in ViewManager
    void setupVisualObjects();
    void setupGUI();
    
    void drawGUI();
    void setupDefaultLayout(bool forceReset = false);
    
    // Layout management
    void saveLayout();
    void loadLayout();
    
    // Module management
    void addModule(const std::string& moduleType);
    void removeModule(const std::string& instanceName);
    
    // Module instance names (for registry lookup)
    static constexpr const char* TRACKER_INSTANCE_NAME = "tracker1";
    static constexpr const char* MEDIAPOOL_INSTANCE_NAME = "pool1";
};
