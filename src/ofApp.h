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
#include "MultiSampler.h"
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
#include "core/Engine.h"
#include "core/ProjectManager.h"
#include "core/SessionManager.h"
#include "MediaConverter.h"
#include "AssetLibrary.h"
#include "shell/Shell.h"
#include "shell/EditorShell.h"
#include "shell/CLIShell.h"
#include "shell/CommandShell.h"
#include <memory>
#include <vector>

class ofApp : public ofBaseApp {
public:
    ofApp();
    ~ofApp() noexcept;
    void setup();
    void update();
    void draw();
    void exit();
    
    void keyPressed(ofKeyEventArgs& keyEvent);
    void mousePressed(int x, int y, int button);
    void mouseDragged(int x, int y, int button);
    void mouseReleased(int x, int y, int button);
    void windowResized(int w, int h);
    
    // Drag and drop support
    void dragEvent(ofDragInfo dragInfo);
    
    // Audio callbacks
    void audioOut(ofSoundBuffer& buffer);
    
private:
    // ═══════════════════════════════════════════════════════════
    // THE ENGINE (core logic)
    // ═══════════════════════════════════════════════════════════
    vt::Engine engine_;
    
    // ═══════════════════════════════════════════════════════════
    // SHELLS (UI interaction modes)
    // ═══════════════════════════════════════════════════════════
    std::vector<std::unique_ptr<vt::shell::Shell>> shells_;
    vt::shell::Shell* activeShell_ = nullptr;
    vt::shell::EditorShell* editorShell_ = nullptr;  // Pointer to EditorShell in shells_ vector
    vt::shell::CommandShell* commandShell_ = nullptr;      // Pointer to CommandShell in shells_ vector
    
    // Time objects (for GUI)
    ClockGUI clockGUI;
    
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
    
    // Performance monitoring
    float lastFrameTime_ = 0.0f;
    float frameTimeAccumulator_ = 0.0f;
    int frameCount_ = 0;
    float lastFpsLogTime_ = 0.0f;
    static constexpr float FPS_LOG_INTERVAL = 5.0f; // Log FPS every 5 seconds
    
    // Layout restoration state (simplified)
    bool layoutNeedsLoad_ = false;  // True when layout should be loaded after windows are drawn
    bool layoutLoaded_ = false;     // True after layout has been successfully loaded
    bool windowsDrawnOnce_ = false; // Track if windows have been drawn at least once
    bool layoutNeedsSave_ = false; // True when layout should be saved (deferred from window resize)
    
    // Methods
    void setupSoundObjects();  // Minimal setup - audio device management is in ViewManager
    void setupVisualObjects();
    void setupGUI();
    
    void drawGUI();
    void setupDefaultLayout(bool forceReset = false);
    
    // Layout management
    void saveLayout();              // Save current layout to imgui.ini (session layout)
    void loadLayout();              // Load layout from imgui.ini (session layout)
    void saveDefaultLayout();       // Save current layout to imgui.default.ini (true default)
    void loadDefaultLayout();       // Load layout from imgui.default.ini (true default)
    
    // Module management
    void addModule(const std::string& moduleType);
    void removeModule(const std::string& instanceName);
    
    // Window title management
    void updateWindowTitle();
    
    // Project state management
    void onProjectOpened();
    void onProjectClosed();
    
    // Shell management
    void setupShells(const std::string& cliCommandOrFile = "");
    void switchShell(vt::shell::Shell* shell);
    void switchToEditor();
    void switchToCommand();
    bool handleShellKeyPress(int key);
    vt::shell::Shell* findShellByName(const std::string& name);
};

