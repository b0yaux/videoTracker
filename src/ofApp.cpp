//
//  ofApp.cpp
//
//  Audiovisual Sequencer Example - Time + Sound + Video coordination
//

#include "ofApp.h"
#include "AudioOutput.h"
#include "VideoOutput.h"
#include "ofLog.h"
#include "gui/GUIConstants.h"
#include <imgui.h>
#include <iomanip>
#include <sstream>
#include <cstdint>

//--------------------------------------------------------------
ofApp::ofApp() : assetLibrary(&projectManager, &mediaConverter, &moduleRegistry), assetLibraryGUI(&assetLibrary) {
    // AssetLibrary and AssetLibraryGUI initialized in member initializer list
}

//--------------------------------------------------------------
ofApp::~ofApp() noexcept {
    // Cleanup handled by member destructors
}

//--------------------------------------------------------------
void ofApp::setup() {
    // Window and app configuration
    ofSetFrameRate(60);
    ofSetVerticalSync(true);
    ofSetLogLevel(OF_LOG_NOTICE);
    ofSetEscapeQuitsApp(false);
    
    // ============================================================
    // PHASE 1: Foundation Setup
    // ============================================================
    
    // Setup Clock first (foundation for timing - single source of truth for transport)
    clock.setup();
    
    // Setup core systems - CRITICAL: ParameterRouter needs registry BEFORE SessionManager uses it
    // Use setRegistry() instead of reassigning to avoid destroying the object that SessionManager points to
    parameterRouter.setRegistry(&moduleRegistry);
    connectionManager.setRegistry(&moduleRegistry);
    connectionManager.setParameterRouter(&parameterRouter);
    
    // Initialize SessionManager with dependencies
    sessionManager = SessionManager(
        &projectManager,
        &clock,
        &moduleRegistry,
        &moduleFactory,
        &parameterRouter,
        &connectionManager,
        &viewManager
    );
    sessionManager.setConnectionManager(&connectionManager);
    // Set callback to notify FileBrowser and AssetLibrary when project is opened from session
    sessionManager.setProjectOpenedCallback([this]() { onProjectOpened(); });
    
    // Setup GUI managers (before session load so GUIs can be restored)
    guiManager.setRegistry(&moduleRegistry);
    guiManager.setParameterRouter(&parameterRouter);
    
    // ============================================================
    // PHASE 2: Master Outputs (must exist before session load)
    // ============================================================
    
    // Create master outputs using ModuleFactory (system modules)
    moduleFactory.ensureSystemModules(&moduleRegistry, MASTER_AUDIO_OUT_NAME, MASTER_VIDEO_OUT_NAME);
    
    // Get master outputs from registry
    masterAudioOut = std::dynamic_pointer_cast<AudioOutput>(moduleRegistry.getModule(MASTER_AUDIO_OUT_NAME));
    masterVideoOut = std::dynamic_pointer_cast<VideoOutput>(moduleRegistry.getModule(MASTER_VIDEO_OUT_NAME));
    
    if (!masterAudioOut || !masterVideoOut) {
        ofLogError("ofApp") << "Failed to create master outputs";
        return;
    }
    
    // Initialize master outputs (before session load)
    masterAudioOut->initialize(&clock, &moduleRegistry, &connectionManager, &parameterRouter, false);
    masterVideoOut->initialize(&clock, &moduleRegistry, &connectionManager, &parameterRouter, false);
    
    // ============================================================
    // PHASE 3: GUI Setup (before session load for state restoration)
    // ============================================================
    
    // Setup GUI
    setupGUI();
    
    // Setup ViewManager with all required dependencies
    viewManager.setup(
        &clock,
        &clockGUI,
        &soundOutput,
        &guiManager,
        &fileBrowser,
        &console,
        &commandBar,
        &assetLibraryGUI
    );
    
    // Setup InputRouter
    inputRouter.setup(
        &clock,
        &moduleRegistry,
        &guiManager,
        &viewManager,
        &console,
        &commandBar
    );
    
    inputRouter.setupWithCallbacks(
        &clock,
        &moduleRegistry,
        &guiManager,
        &viewManager,
        &console,
        &commandBar,
        &sessionManager,
        &projectManager,
        &menuBar,
        [this]() { updateWindowTitle(); },
        &currentStep,
        &lastTriggeredStep,
        &showGUI
    );
    
    // Setup CommandExecutor (backend for command execution)
    commandExecutor.setup(&moduleRegistry, &guiManager, &connectionManager);
    commandExecutor.setOnAddModule([this](const std::string& moduleType) {
        moduleRegistry.addModule(
            moduleFactory,
            moduleType,
            &clock,
            &connectionManager,
            &parameterRouter,
            &guiManager,
            MASTER_AUDIO_OUT_NAME,
            MASTER_VIDEO_OUT_NAME
        );
        // Refresh command bar commands when module is added
        commandBar.refreshCommands();
    });
    commandExecutor.setOnRemoveModule([this](const std::string& instanceName) {
        moduleRegistry.removeModule(
            instanceName,
            &connectionManager,
            &guiManager,
            MASTER_AUDIO_OUT_NAME,
            MASTER_VIDEO_OUT_NAME
        );
        // Refresh command bar commands when module is removed
        commandBar.refreshCommands();
    });
    
    // Setup Console (text-based UI) - delegates to CommandExecutor
    console.setup(&moduleRegistry, &guiManager);
    console.setCommandExecutor(&commandExecutor);  // This sets up the output callback
    
    // Setup CommandBar (palette-based UI) - delegates to CommandExecutor
    commandBar.setup(&commandExecutor, &viewManager, &guiManager);
    
    // Setup SessionManager GUI coordination (initializes all modules)
    sessionManager.setupGUI(&guiManager);
    
    // ============================================================
    // PHASE 4: Project & Session Initialization
    // ============================================================
    
    // Initialize project and session (opens/creates project, loads default session)
    // This must be called after GUI setup so that modules can be properly restored
    std::string dataPath = ofToDataPath("", true);
    bool sessionLoaded = sessionManager.initializeProjectAndSession(dataPath);
    
    // Notify FileBrowser and AssetLibrary that project is now open
    if (projectManager.isProjectOpen()) {
        onProjectOpened();
    }
    
    if (!sessionLoaded) {
        // If no session was loaded, ensure default modules exist
        // This creates TrackerSequencer and MediaPool if they don't exist
        sessionManager.ensureDefaultModules({"TrackerSequencer", "MediaPool"});
    }
    
    // CRITICAL: Refresh master outputs after session load
    // Session load clears the registry and creates new module instances,
    // so we need to update our pointers to point to the new instances
    masterAudioOut = std::dynamic_pointer_cast<AudioOutput>(moduleRegistry.getModule(MASTER_AUDIO_OUT_NAME));
    masterVideoOut = std::dynamic_pointer_cast<VideoOutput>(moduleRegistry.getModule(MASTER_VIDEO_OUT_NAME));
    
    if (!masterAudioOut || !masterVideoOut) {
        ofLogError("ofApp") << "[CHECKPOINT] ✗ CRITICAL: Failed to refresh master outputs after session load";
        return;
    }
    
    // Re-initialize master outputs after session load (they were already initialized during session restore,
    // but we need to ensure they have the correct dependencies)
    masterAudioOut->initialize(&clock, &moduleRegistry, &connectionManager, &parameterRouter, true);
    masterVideoOut->initialize(&clock, &moduleRegistry, &connectionManager, &parameterRouter, true);
    
    // CRITICAL: Set audio output for AssetLibrary preview routing
    // This must be done after masterAudioOut is refreshed after session load
    assetLibraryGUI.setAudioMixer(masterAudioOut.get());
    
    // Setup default connections for any modules not restored from session
    // This ensures new modules get auto-connected to masters
    connectionManager.setupDefaultConnections(&clock, MASTER_AUDIO_OUT_NAME, MASTER_VIDEO_OUT_NAME);
    
    // DEBUG: Verify audio connections after session load and default connections
    if (masterAudioOut) {
        // Get instance ID for debugging
        uintptr_t instanceId = reinterpret_cast<uintptr_t>(masterAudioOut.get()) & 0xFFFF;
        size_t numConnections = masterAudioOut->getNumConnections();
        ofLogNotice("ofApp") << "[CHECKPOINT] After session load, masterAudioOut (Instance:0x" 
                            << std::hex << instanceId << std::dec 
                            << ") has " << numConnections << " connections";
        
        // Also check mixer directly
        auto& mixer = masterAudioOut->getSoundMixer();
        size_t mixerConnections = mixer.getNumConnections();
        ofLogNotice("ofApp") << "[CHECKPOINT] After session load, mixer has " << mixerConnections << " connections";
        
        if (numConnections != mixerConnections) {
            ofLogError("ofApp") << "[CHECKPOINT] ⚠ MISMATCH: getNumConnections()=" << numConnections 
                               << " but mixer.getNumConnections()=" << mixerConnections;
        }
    }
    
    // ============================================================
    // PHASE 5: Audio/Video Setup (after session load)
    // ============================================================
    
    // Setup audio (after session load so audio device preferences are restored)
    setupSoundObjects();
    
    // Setup visual objects
    setupVisualObjects();
    
    // ============================================================
    // PHASE 6: Final Configuration
    // ============================================================
    
    // Enable auto-save
    sessionManager.enableAutoSave(30.0f, [this]() { updateWindowTitle(); });
    
    // Setup default layout
    setupDefaultLayout();
    
    // Update window title
    updateWindowTitle();
}

//--------------------------------------------------------------
void ofApp::update() {
    // Update session manager (handles auto-save)
    sessionManager.update();
    
    // Update input router
    inputRouter.update();
    
    // Update asset library (processes conversion status updates)
    assetLibrary.update();
    
    // Update all modules (MediaPool, TrackerSequencer, etc.)
    // This is critical for MediaPool to process its event queue
    moduleRegistry.forEachModule([this](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
        if (module) {
            try {
                module->update();
            } catch (const std::exception& e) {
                ofLogError("ofApp") << "Error updating module '" << name << "': " << e.what();
            } catch (...) {
                ofLogError("ofApp") << "Unknown error updating module '" << name << "'";
            }
        }
    });
}

//--------------------------------------------------------------
void ofApp::draw() {
    // Clear background first (important for video output)
    ofClear(0, 0, 0, 255);
    
    // Draw video output (fills entire window, drawn behind GUI)
    if (masterVideoOut) {
        masterVideoOut->draw();
    }
    
    // Draw GUI on top of video
    if (showGUI) {
        drawGUI();
    }
}

//--------------------------------------------------------------
void ofApp::exit() {
    // Auto-save full session before exiting
    if (sessionManager.saveSession("session.json")) {
        ofLogNotice("ofApp") << "Session saved to file";
    }
    
    // Cleanup ImGui (wrapper handles ImPlot and ImGui cleanup)
    gui.shutdown();
    
    // Stop clock and close audio stream
    clock.stop();
    if (masterAudioOut) {
        // AudioOutput manages its own soundStream_, but we should stop it
        // The soundStream_ is closed automatically when AudioOutput is destroyed
    }
}

//--------------------------------------------------------------
void ofApp::keyPressed(ofKeyEventArgs& keyEvent) {
    inputRouter.handleKeyPress(keyEvent);
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button) {
    // Mouse handling can be added here if needed
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h) {
    // ImGuiIntegration wrapper handles window resize automatically via event listeners
    gui.onWindowResized(w, h);
    
    // Update master video output viewport to match window size
    // VideoOutput will auto-update viewport in draw(), but we log here for debugging
    if (masterVideoOut) {
        ofLogNotice("ofApp") << "Window resized to " << w << "x" << h;
    }
    if (masterVideoOut) {
        masterVideoOut->handleWindowResize(w, h);
    }
    
    ofLogNotice("ofApp") << "Window resized to " << w << "x" << h;
    
    // Save layout after resize
    saveLayout();
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo) {
    // Handle drag and drop
    inputRouter.handleDragEvent(dragInfo, &assetLibrary, &projectManager);
}

//--------------------------------------------------------------
void ofApp::audioOut(ofSoundBuffer& buffer) {
    // CRITICAL: Process Clock first to generate timing events
    // Clock must be called before any modules that depend on timing
    clock.audioOut(buffer);
    
    // Then process audio through master output (which mixes all connected modules)
    if (masterAudioOut) {
        masterAudioOut->audioOut(buffer);
    }
}

//--------------------------------------------------------------
void ofApp::setupSoundObjects() {
    // Setup audio stream through AudioOutput
    // AudioOutput manages its own soundStream_ internally
    if (masterAudioOut) {
        // DEBUG: Check connections before setup
        size_t connectionsBefore = masterAudioOut->getNumConnections();
        auto& mixer = masterAudioOut->getSoundMixer();
        size_t mixerBefore = mixer.getNumConnections();
        ofLogNotice("ofApp") << "[CHECKPOINT] setupSoundObjects(): Before setup - getNumConnections(): " << connectionsBefore 
                            << ", mixer.getNumConnections(): " << mixerBefore;
        
        masterAudioOut->setupAudioStream(static_cast<ofBaseApp*>(this));
        
        // DEBUG: Check connections after setup
        size_t connectionsAfter = masterAudioOut->getNumConnections();
        size_t mixerAfter = mixer.getNumConnections();
        ofLogNotice("ofApp") << "[CHECKPOINT] setupSoundObjects(): After setup - getNumConnections(): " << connectionsAfter 
                            << ", mixer.getNumConnections(): " << mixerAfter;
        
        if (mixerBefore != mixerAfter) {
            ofLogError("ofApp") << "[CHECKPOINT] ⚠ CRITICAL: Mixer connections changed: " << mixerBefore << " -> " << mixerAfter;
        }
        if (connectionsBefore != connectionsAfter) {
            ofLogWarning("ofApp") << "[CHECKPOINT] ⚠ getNumConnections() changed: " << connectionsBefore << " -> " << connectionsAfter;
        }
    } else {
        ofLogError("ofApp") << "[CHECKPOINT] ✗ Cannot setup audio: masterAudioOut is null";
    }
}

//--------------------------------------------------------------
void ofApp::setupVisualObjects() {
    // Visual objects setup handled by VideoOutput
}

//--------------------------------------------------------------
void ofApp::setupGUI() {
    // Initialize ImGui with docking enabled and ini file handling
    // ImGuiIntegration wrapper handles window context automatically
    gui.setup(nullptr, true, ImGuiConfigFlags_DockingEnable);
    
    // Set ini filename for auto-loading on startup
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "imgui.ini";
    
    // Enable keyboard navigation
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // Apply centralized color theme from GUIConstants
    GUIConstants::applyImGuiStyle();
    
    // Log ImGui version for verification
    ofLogNotice("ofApp") << "ImGui Version: " << ImGui::GetVersion();
    
    // Setup MenuBar with callbacks
    menuBar.setup(
        []() {}, // onSavePattern
        []() {}, // onLoadPattern
        [this]() { saveLayout(); }, // onSaveLayout
        [this]() { loadLayout(); }, // onLoadLayout
        [this](const std::string& moduleType) {
            moduleRegistry.addModule(
                moduleFactory,
                moduleType,
                &clock,
                &connectionManager,
                &parameterRouter,
                &guiManager,
                MASTER_AUDIO_OUT_NAME,
                MASTER_VIDEO_OUT_NAME
            );
        }, // onAddModule
        [this]() { viewManager.setFileBrowserVisible(!viewManager.isFileBrowserVisible()); }, // onToggleFileBrowser
        [this]() { viewManager.setConsoleVisible(!viewManager.isConsoleVisible()); }, // onToggleConsole
        [this]() { viewManager.setAssetLibraryVisible(!viewManager.isAssetLibraryVisible()); }, // onToggleAssetLibrary
        [this]() { showDemoWindow = !showDemoWindow; }, // onToggleDemoWindow
        [this]() { sessionManager.saveSession("session"); }, // onSaveSession
        [this]() { /* TODO: Save session as */ }, // onSaveSessionAs
        [this]() { sessionManager.loadSession("session"); }, // onOpenSession
        [this](const std::string& sessionPath) { /* TODO: Open recent session */ }, // onOpenRecentSession
        [this]() { /* TODO: New session */ }, // onNewSession
        [this]() { return sessionManager.getCurrentSessionName(); }, // getCurrentSessionName
        [this]() { 
            // onOpenProject (TODO: implement file dialog)
            if (projectManager.openProject("")) {
                onProjectOpened();
            }
        },
        [this]() { 
            // onNewProject (TODO: implement file dialog)
            if (projectManager.createProject("", "New Project")) {
                onProjectOpened();
            }
        },
        [this]() { 
            // onCloseProject
            onProjectClosed();
            projectManager.closeProject();
        },
        [this]() { return projectManager.getProjectName(); }, // getCurrentProjectName
        [this]() { return projectManager.listSessions(); }, // getProjectSessions
        [this](const std::string& sessionName) { 
            std::string sessionPath = projectManager.getSessionPath(sessionName);
            if (!sessionPath.empty()) {
                sessionManager.loadSession(sessionPath);
            }
        }, // onOpenProjectSession
        [this]() { assetLibrary.importFile(""); }, // onImportFile
        [this]() { assetLibrary.importFolder(""); } // onImportFolder
    );
    
    menuBar.setupWithDependencies(
        &sessionManager,
        &projectManager,
        &assetLibrary,
        &viewManager,
        &fileBrowser,
        [this](const std::string& moduleType) {
            moduleRegistry.addModule(
                moduleFactory,
                moduleType,
                &clock,
                &connectionManager,
                &parameterRouter,
                &guiManager,
                MASTER_AUDIO_OUT_NAME,
                MASTER_VIDEO_OUT_NAME
            );
        },
        [this]() { saveLayout(); },
        [this]() { loadLayout(); },
        [this]() { updateWindowTitle(); },
        &showDemoWindow
    );
    
    // Setup Console (callbacks set in setup())
    console.setup(&moduleRegistry, &guiManager);
    
    // Setup FileBrowser and AssetLibraryGUI (no setup methods needed - they're initialized in constructor)
    // AssetLibraryGUI is initialized with AssetLibrary pointer in constructor
    
    // Note: AssetLibrary initialization is deferred until after project is opened
    // See onProjectOpened() which is called after initializeProjectAndSession()
}

//--------------------------------------------------------------
void ofApp::drawGUI() {
    gui.begin();
    
    // Create main docking space
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | 
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_DockNodeHost;
    
    if (ImGui::Begin("DockSpace", nullptr, window_flags)) {
        // Draw menu bar inside dockspace window (so it's above the dockspace)
        menuBar.draw();
        
        // Create dock space below menu bar
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    }
    ImGui::End();
    
    ImGui::PopStyleVar(3);
    
    // Draw demo window if enabled
    if (showDemoWindow) {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }
    
    // Draw view manager (handles all views)
    viewManager.draw();
    
    gui.end();
}

//--------------------------------------------------------------
void ofApp::setupDefaultLayout(bool forceReset) {
    // Layout setup handled by ViewManager
    // This can be customized if needed
}

//--------------------------------------------------------------
void ofApp::saveLayout() {
    std::string layoutPath = ofToDataPath("imgui.ini", true);
    ImGui::SaveIniSettingsToDisk(layoutPath.c_str());
    ofLogNotice("ofApp") << "Layout saved to " << layoutPath;
}

//--------------------------------------------------------------
void ofApp::loadLayout() {
    std::string layoutPath = ofToDataPath("imgui.ini", true);
    if (ofFile::doesFileExist(layoutPath)) {
        ImGui::LoadIniSettingsFromDisk(layoutPath.c_str());
        ofLogNotice("ofApp") << "Layout loaded from " << layoutPath;
    }
}

//--------------------------------------------------------------
void ofApp::addModule(const std::string& moduleType) {
    // Delegate to ModuleRegistry lifecycle method
    moduleRegistry.addModule(
        moduleFactory,
        moduleType,
        &clock,
        &connectionManager,
        &parameterRouter,
        &guiManager,
        MASTER_AUDIO_OUT_NAME,
        MASTER_VIDEO_OUT_NAME
    );
}

//--------------------------------------------------------------
void ofApp::removeModule(const std::string& instanceName) {
    // Delegate to ModuleRegistry lifecycle method
    moduleRegistry.removeModule(
        instanceName,
        &connectionManager,
        &guiManager,
        MASTER_AUDIO_OUT_NAME,
        MASTER_VIDEO_OUT_NAME
    );
}

//--------------------------------------------------------------
void ofApp::updateWindowTitle() {
    // App name ?
    std::string title = "";
    // Project name
    if (projectManager.isProjectOpen()) {
        title += projectManager.getProjectName();
        
        // Session name if available
        std::string sessionName = sessionManager.getCurrentSessionName();
        if (!sessionName.empty()) {
            title += " - " + sessionName;
        }
    }
    // Note: Unsaved changes tracking would need to be implemented in SessionManager
    // For now, we skip the "*" indicator
    ofSetWindowTitle(title);
}

//--------------------------------------------------------------
void ofApp::onProjectOpened() {
    if (!projectManager.isProjectOpen()) {
        return;
    }
    
    std::string projectRoot = projectManager.getProjectRoot();
    
    // Set FileBrowser to project root directory
    fileBrowser.setProjectDirectory(projectRoot);
    ofLogNotice("ofApp") << "FileBrowser set to project directory: " << projectRoot;
    
    // Initialize AssetLibrary with project assets
    assetLibrary.initialize();
    ofLogNotice("ofApp") << "AssetLibrary initialized for project: " << projectManager.getProjectName();
}

//--------------------------------------------------------------
void ofApp::onProjectClosed() {
    // Reset FileBrowser to user home directory
    fileBrowser.setProjectDirectory(ofFilePath::getUserHomeDir());
    ofLogNotice("ofApp") << "FileBrowser reset to user home directory";
    
    // AssetLibrary doesn't need explicit cleanup - it will reinitialize when project opens
    // But we could clear its state if needed in the future
}
