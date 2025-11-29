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
#include "TrackerSequencer.h"
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
#include "gui/CommandBar.h"
#include "gui/FileBrowser.h"
#include "gui/AssetLibraryGUI.h"
#include "input/InputRouter.h"
#include "core/CommandExecutor.h"
#include "Module.h"
#include "core/ModuleFactory.h"
#include "core/ModuleRegistry.h"
#include "core/ParameterRouter.h"
#include "core/ConnectionManager.h"
#include "core/ProjectManager.h"
#include "core/SessionManager.h"
#include "MediaConverter.h"
#include "AssetLibrary.h"

class ofApp : public ofBaseApp {
public:
    ofApp();  // Constructor to initialize AssetLibrary
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
    
private:
    // Time objects
    Clock clock;
    ClockGUI clockGUI;
    
    // Project and session management
    ProjectManager projectManager;
    MediaConverter mediaConverter;  // Background video conversion service
    AssetLibrary assetLibrary;      // Project asset management (initialized in constructor)
    
    // Module management system (Phase 1: Core Architecture)
    ModuleFactory moduleFactory;
    ModuleRegistry moduleRegistry;
    ParameterRouter parameterRouter;
    ConnectionManager connectionManager;
    SessionManager sessionManager;
    
    // Master outputs (created on startup, include mixer functionality internally)
    std::shared_ptr<class AudioOutput> masterAudioOut;
    std::shared_ptr<class VideoOutput> masterVideoOut;
    
    // GUI management (Phase 3: Multiple Instances)
    GUIManager guiManager;
    
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
    
    // Command system
    CommandExecutor commandExecutor;  // Backend for command execution
    
    // GUI managers
    MenuBar menuBar;
    ViewManager viewManager;
    InputRouter inputRouter;
    Console console;  // Text-based command UI
    CommandBar commandBar;  // Palette-based command UI
    FileBrowser fileBrowser;  // File browser panel
    AssetLibraryGUI assetLibraryGUI;  // Asset library panel
    
    // GUI state
    bool showGUI = true;
    bool showDemoWindow = false;
    // Note: isPlaying removed - use clock.isPlaying() directly (Clock is single source of truth for transport)
    // Note: stepCount is now per-pattern (use getCurrentPattern().getStepCount() on TrackerSequencer)
    
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
    
    // Window title management
    void updateWindowTitle();
    
    // Project state management
    void onProjectOpened();
    void onProjectClosed();
    
    // Module instance names (for registry lookup)
    // All instance names use camelCase for consistency
    // Note: tracker1/pool1 are default instances but not hardcoded - use registry to access
    static constexpr const char* MASTER_AUDIO_OUT_NAME = "masterAudioOut";
    static constexpr const char* MASTER_VIDEO_OUT_NAME = "masterVideoOut";
};
