//
//  ofApp.cpp
//
//  Audiovisual Sequencer Example - Time + Sound + Video coordination
//

#include "ofApp.h"
#include "modules/AudioOutput.h"
#include "modules/VideoOutput.h"
#include "ofLog.h"
#include "gui/GUIConstants.h"
#include "shell/EditorShell.h"
#include "shell/CLIShell.h"
#include "shell/CommandShell.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <iomanip>
#include <sstream>
#include <cstdint>
#include <chrono>
#include <fstream>

// Static variable to pass CLI command from main to ofApp
std::string g_cliCommandOrFile;

//--------------------------------------------------------------
ofApp::ofApp() : assetLibraryGUI(&engine_.getAssetLibrary()) {
    // Engine owns AssetLibrary, AssetLibraryGUI references it
}

//--------------------------------------------------------------
ofApp::~ofApp() noexcept {
    // Cleanup handled by member destructors
}

//--------------------------------------------------------------
void ofApp::setup() {
    // Window and app configuration
    ofSetFrameRate(0);
    ofSetVerticalSync(true);
    ofSetLogLevel(OF_LOG_NOTICE);
    ofSetEscapeQuitsApp(false);
    
    // ============================================================
    // PHASE 1: Engine Setup
    // ============================================================
    
    // Setup Engine with callbacks
    engine_.setOnProjectOpened([this]() { onProjectOpened(); });
    engine_.setOnUpdateWindowTitle([this]() { updateWindowTitle(); });
    engine_.setup();
    
    // Setup GUI managers (before session load so GUIs can be restored)
    engine_.setupGUIManagers(&guiManager, &viewManager);
    
    // Ensure GUIs are created for all modules (including master oscilloscope and spectrogram)
    // This must happen after modules are created
    guiManager.syncWithRegistry();
    
    // Make master oscilloscope and spectrogram visible by default
    guiManager.setInstanceVisible("masterOscilloscope", true);
    guiManager.setInstanceVisible("masterSpectrogram", true);
    
    // ============================================================
    // PHASE 3: GUI Setup (before session load for state restoration)
    // ============================================================
    
    // Setup GUI
    setupGUI();
    
    // Setup ViewManager with all required dependencies
    viewManager.setup(
        &engine_.getClock(),
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
        &engine_.getClock(),
        &engine_.getModuleRegistry(),
        &guiManager,
        &viewManager,
        &console,
        &commandBar
    );
    
    inputRouter.setupWithCallbacks(
        &engine_.getClock(),
        &engine_.getModuleRegistry(),
        &guiManager,
        &viewManager,
        &console,
        &commandBar,
        &engine_.getSessionManager(),
        &engine_.getProjectManager(),
        [this]() { updateWindowTitle(); },
        &currentStep,
        &lastTriggeredStep,
        &showGUI
    );
    
    // CommandExecutor callbacks are already set up in Engine::setupCommandExecutor()
    // We just need to refresh command bar when modules change
    // (This will be handled via Engine state observers in Phase 2)
    
    // Setup Console (text-based UI) - delegates to Engine's CommandExecutor
    console.setup(&engine_.getModuleRegistry(), &guiManager);
    console.setCommandExecutor(&engine_.getCommandExecutor());  // This sets up the output callback
    
    // Setup CommandBar (palette-based UI) - delegates to Engine's CommandExecutor
    commandBar.setup(&engine_.getCommandExecutor(), &viewManager, &guiManager);
    commandBar.refreshCommands();  // Initial refresh after setup
    
    // ============================================================
    // PHASE 4: Audio/Video Setup (after session load)
    // ============================================================
    
    // Setup audio (after session load so audio device preferences are restored)
    setupSoundObjects();
    
    // Setup visual objects
    setupVisualObjects();
    
    // CRITICAL: Set audio output for AssetLibrary preview routing
    // This must be done after masterAudioOut is available
    auto masterAudioOut = engine_.getMasterAudioOut();
    if (masterAudioOut) {
        assetLibraryGUI.setAudioMixer(masterAudioOut.get());
    }
    
    // ============================================================
    // PHASE 5: Shell Setup (UI interaction modes)
    // ============================================================
    
    setupShells();
    
    // ============================================================
    // PHASE 6: Final Configuration
    // ============================================================
    
    // Mark that layout should be loaded after windows are drawn
    // This ensures dockspace and windows are created before layout restoration
    layoutNeedsLoad_ = true;
    layoutLoaded_ = false;
    
    // Update window title
    updateWindowTitle();
}

//--------------------------------------------------------------
void ofApp::update() {
    // Performance monitoring: Start update timing
    float updateStartTime = ofGetElapsedTimef();
    
    // Update Engine (handles session manager, asset library, command executor, and all modules)
    float engineStartTime = ofGetElapsedTimef();
    engine_.update(ofGetLastFrameTime());
    float engineTime = (ofGetElapsedTimef() - engineStartTime) * 1000.0f;
    
    // Update active shell
    if (activeShell_) {
        activeShell_->update(ofGetLastFrameTime());
        
        // Check if CLI shell wants to exit
        if (activeShell_->getName() == "CLI") {
            auto* cliShell = dynamic_cast<vt::shell::CLIShell*>(activeShell_);
            if (cliShell && cliShell->shouldExit()) {
                ofLogNotice("ofApp") << "CLI shell completed, exiting application";
                ofExit(0);
            }
        }
    }
    
    // Update input router
    float inputStartTime = ofGetElapsedTimef();
    inputRouter.update();
    float inputTime = (ofGetElapsedTimef() - inputStartTime) * 1000.0f;
    
    // Log slow update cycles
    float totalUpdateTime = (ofGetElapsedTimef() - updateStartTime) * 1000.0f;
    if (totalUpdateTime > 5.0f) { // Warn if update takes > 5ms
        ofLogWarning("ofApp") << "[PERF] Slow update: " << std::fixed << std::setprecision(2) << totalUpdateTime 
                             << "ms (engine: " << engineTime << "ms, input: " << inputTime << "ms)";
    }
}

//--------------------------------------------------------------
void ofApp::draw() {
    // Performance monitoring: Start frame timing
    float frameStartTime = ofGetElapsedTimef();
    
    // Clear background first (important for video output)
    ofClear(0, 0, 0, 255);
    
    // Draw video output (fills entire window, drawn behind GUI)
    float videoStartTime = ofGetElapsedTimef();
    auto masterVideoOut = engine_.getMasterVideoOut();
    if (masterVideoOut) {
        masterVideoOut->draw();
    }
    float videoTime = (ofGetElapsedTimef() - videoStartTime) * 1000.0f;
    
    // Draw active shell (or fall back to legacy GUI)
    float guiStartTime = ofGetElapsedTimef();
    if (activeShell_) {
        // CommandShell uses custom rendering (no ImGui needed)
        // EditorShell handles ImGui internally via drawGUI()
        if (activeShell_->getName() == "Command") {
            // Custom rendering - no ImGui wrapping needed
            activeShell_->draw();
        } else {
            activeShell_->draw();
        }
    } else if (showGUI) {
        // Fallback to legacy GUI if no shell is active
        drawGUI();
    }
    float guiTime = (ofGetElapsedTimef() - guiStartTime) * 1000.0f;
    
    // Calculate total frame time
    float frameTime = (ofGetElapsedTimef() - frameStartTime) * 1000.0f; // Convert to ms
    lastFrameTime_ = frameTime;
    
    // Accumulate for FPS calculation
    frameTimeAccumulator_ += frameTime;
    frameCount_++;
    
    // Log performance stats periodically
    float currentTime = ofGetElapsedTimef();
    if (currentTime - lastFpsLogTime_ >= FPS_LOG_INTERVAL) {
        if (frameCount_ > 0) {
            float avgFrameTime = frameTimeAccumulator_ / frameCount_;
            float avgFps = 1000.0f / avgFrameTime;
            float currentFps = 1000.0f / frameTime;
            
            ofLogNotice("ofApp") << "[PERF] Overall FPS: " << std::fixed << std::setprecision(1) << currentFps
                                << " (avg: " << avgFps << ")"
                                << " | Frame: " << std::setprecision(2) << frameTime << "ms"
                                << " (video: " << videoTime << "ms, GUI: " << guiTime << "ms)";
            
            // Reset accumulator
            frameTimeAccumulator_ = 0.0f;
            frameCount_ = 0;
            lastFpsLogTime_ = currentTime;
        }
    }
    
    // Log slow frames (warn if frame takes > 20ms, which is < 50fps)
    if (frameTime > 20.0f) {
        ofLogWarning("ofApp") << "[PERF] Slow frame detected: " << std::fixed << std::setprecision(2) 
                             << frameTime << "ms (video: " << videoTime << "ms, GUI: " << guiTime << "ms)";
    }
}

//--------------------------------------------------------------
void ofApp::exit() {
    ofLogNotice("ofApp") << "Exiting application...";
    
    // Step 1: Stop clock first to stop all timing-dependent operations
    engine_.getClock().stop();
    
    // Step 2: Auto-save session FIRST (before any modifications)
    try {
        if (engine_.saveSession("session.json")) {
            ofLogNotice("ofApp") << "Session saved to file";
        }
    } catch (...) {
        ofLogWarning("ofApp") << "Error saving session during exit";
    }
    
    // Step 3: Close audio stream explicitly BEFORE module destruction
    auto masterAudioOut = engine_.getMasterAudioOut();
    if (masterAudioOut) {
        try {
            if (masterAudioOut->getSoundStream().getNumOutputChannels() > 0) {
                masterAudioOut->getSoundStream().close();
                ofLogNotice("ofApp") << "Audio stream closed";
            }
        } catch (...) {
            ofLogWarning("ofApp") << "Error closing audio stream during exit";
        }
    }
    
    // Step 4: Cleanup ImGui (wrapper handles ImPlot and ImGui cleanup)
    try {
        gui.shutdown();
        ofLogNotice("ofApp") << "ImGui shutdown complete";
    } catch (...) {
        ofLogWarning("ofApp") << "Error during ImGui shutdown";
    }
    
    // Step 5: Engine destructor will handle module cleanup
    // Background threads (CommandExecutor, MediaConverter) are joined
    // in their destructors, which are called when Engine members are destroyed
    
    ofLogNotice("ofApp") << "Exit cleanup complete";
}

//--------------------------------------------------------------
void ofApp::keyPressed(ofKeyEventArgs& keyEvent) {
    // Handle shell switching first
    if (handleShellKeyPress(keyEvent.key)) {
        return;  // Shell switching handled the key
    }
    
    // Delegate to active shell or input router
    if (activeShell_) {
        // For CommandShell, we need to pass modifier info for copy/paste
        // Check if it's CommandShell and pass full keyEvent if needed
        if (activeShell_->getName() == "Command") {
            // CommandShell handles its own modifier detection via ofGetKeyPressed
            if (activeShell_->handleKeyPress(keyEvent.key)) {
                return;  // Shell handled the key
            }
        } else {
            if (activeShell_->handleKeyPress(keyEvent.key)) {
                return;  // Shell handled the key
            }
        }
    }
    
    // Fall back to input router (for Editor shell compatibility)
    inputRouter.handleKeyPress(keyEvent);
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button) {
    // Route mouse events to active shell for text selection and interaction
    if (activeShell_) {
        if (activeShell_->handleMousePress(x, y, button)) {
            return;  // Shell handled the mouse event
        }
    }
}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button) {
    // Route mouse drag events to active shell (for text selection)
    if (activeShell_) {
        if (activeShell_->handleMouseDrag(x, y, button)) {
            return;  // Shell handled the mouse drag
        }
    }
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button) {
    // Route mouse release events to active shell (for text selection)
    if (activeShell_) {
        if (activeShell_->handleMouseRelease(x, y, button)) {
            return;  // Shell handled the mouse release
        }
    }
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h) {
    // ImGuiIntegration wrapper handles window resize automatically via event listeners
    gui.onWindowResized(w, h);
    
    // Update master video output viewport to match window size
    auto masterVideoOut = engine_.getMasterVideoOut();
    
    if (masterVideoOut) {
        masterVideoOut->handleWindowResize(w, h);
        ofLogNotice("ofApp") << "Window resized to " << w << "x" << h;
    }
    
    // Notify active shell of window resize
    if (activeShell_) {
        activeShell_->handleWindowResize(w, h);
    }
    
    // CRITICAL FIX: Don't save layout during startup or during active resize
    // Only save layout after the app is fully initialized (layout has been loaded)
    // CRITICAL FIX: Defer layout save to avoid crash - ImGui state may be inconsistent during resize
    // Save layout on next frame instead of immediately during resize event
    if (layoutLoaded_) {
        // Mark that layout needs to be saved on next frame
        // This avoids accessing ImGui internals during window resize when state may be inconsistent
        layoutNeedsSave_ = true;
    }
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo) {
    // Handle drag and drop
    inputRouter.handleDragEvent(dragInfo, &engine_.getAssetLibrary(), &engine_.getProjectManager());
}

//--------------------------------------------------------------
void ofApp::audioOut(ofSoundBuffer& buffer) {
    // Delegate to Engine (handles Clock and master audio output)
    engine_.audioOut(buffer);
}

//--------------------------------------------------------------
void ofApp::setupSoundObjects() {
    // Setup audio stream through AudioOutput
    // AudioOutput manages its own soundStream_ internally
    auto masterAudioOut = engine_.getMasterAudioOut();
    if (masterAudioOut) {
        masterAudioOut->setupAudioStream(static_cast<ofBaseApp*>(this));
    } else {
        ofLogError("ofApp") << "Cannot setup audio: masterAudioOut is null";
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
        [this]() { saveDefaultLayout(); }, // onSaveLayout -> Save Layout as Default
        [this]() { loadDefaultLayout(); }, // onLoadLayout -> Load Default Layout
        [this](const std::string& moduleType) {
            engine_.getModuleRegistry().addModule(
                engine_.getModuleFactory(),
                moduleType,
                &engine_.getClock(),
                &engine_.getConnectionManager(),
                &engine_.getParameterRouter(),
                &engine_.getPatternRuntime(),
                &guiManager,
                "masterAudioOut",
                "masterVideoOut"
            );
            commandBar.refreshCommands();
        }, // onAddModule
        [this]() { viewManager.setFileBrowserVisible(!viewManager.isFileBrowserVisible()); }, // onToggleFileBrowser
        [this]() { viewManager.setConsoleVisible(!viewManager.isConsoleVisible()); }, // onToggleConsole
        [this]() { viewManager.setAssetLibraryVisible(!viewManager.isAssetLibraryVisible()); }, // onToggleAssetLibrary
        [this]() { showDemoWindow = !showDemoWindow; }, // onToggleDemoWindow
        [this]() { engine_.saveSession("session"); }, // onSaveSession
        [this]() { /* TODO: Save session as */ }, // onSaveSessionAs
        [this]() { 
            engine_.loadSession("session");
            // Trigger layout loading after session load
            layoutNeedsLoad_ = true;
            layoutLoaded_ = false;
        }, // onOpenSession
        [this](const std::string& sessionPath) { /* TODO: Open recent session */ }, // onOpenRecentSession
        [this]() { /* TODO: New session */ }, // onNewSession
        [this]() { return engine_.getSessionManager().getCurrentSessionName(); }, // getCurrentSessionName
        [this]() { 
            // onOpenProject (TODO: implement file dialog)
            if (engine_.getProjectManager().openProject("")) {
                onProjectOpened();
            }
        },
        [this]() { 
            // onNewProject (TODO: implement file dialog)
            if (engine_.getProjectManager().createProject("", "New Project")) {
                onProjectOpened();
            }
        },
        [this]() { 
            // onCloseProject
            onProjectClosed();
            engine_.getProjectManager().closeProject();
        },
        [this]() { return engine_.getProjectManager().getProjectName(); }, // getCurrentProjectName
        [this]() { return engine_.getProjectManager().listSessions(); }, // getProjectSessions
        [this](const std::string& sessionName) { 
            std::string sessionPath = engine_.getProjectManager().getSessionPath(sessionName);
            if (!sessionPath.empty()) {
                engine_.loadSession(sessionPath);
                // Trigger layout loading after session load
                layoutNeedsLoad_ = true;
                layoutLoaded_ = false;
            }
        }, // onOpenProjectSession
        [this]() { engine_.getAssetLibrary().importFile(""); }, // onImportFile
        [this]() { engine_.getAssetLibrary().importFolder(""); } // onImportFolder
    );
    
    menuBar.setupWithDependencies(
        &engine_.getSessionManager(),
        &engine_.getProjectManager(),
        &engine_.getAssetLibrary(),
        &viewManager,
        &fileBrowser,
        [this](const std::string& moduleType) {
            engine_.executeCommand("add " + moduleType);
        },
        [this]() { saveDefaultLayout(); }, // Save Layout as Default
        [this]() { loadDefaultLayout(); }, // Load Default Layout
        [this]() { updateWindowTitle(); },
        &showDemoWindow
    );
    
    // Setup Console (callbacks set in setup())
    console.setup(&engine_.getModuleRegistry(), &guiManager);
    
    // Setup FileBrowser and AssetLibraryGUI (no setup methods needed - they're initialized in constructor)
    // AssetLibraryGUI is initialized with AssetLibrary pointer in constructor
    
    // Note: AssetLibrary initialization is deferred until after project is opened
    // See onProjectOpened() which is called after initializeProjectAndSession()
}

//--------------------------------------------------------------
void ofApp::drawGUI() {
    gui.begin();
    
    // Calculate menu bar height to reserve space at bottom
    float menuBarHeight = ImGui::GetFrameHeight();
    
    // Create main docking space
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    // Reduce window height by menu bar height to leave space at bottom
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - menuBarHeight));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.f));
    
    // Remove ImGuiWindowFlags_MenuBar since we're drawing menu bar separately at bottom
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | 
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_DockNodeHost;
    
    if (ImGui::Begin("DockSpace", nullptr, window_flags)) {
        // Create dock space (reduced size to account for menu bar at bottom)
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
        
        // #region agent log
        ImGuiContext* ctx = ImGui::GetCurrentContext();
        if (ctx) {
            ImGuiDockNode* node = ImGui::DockContextFindNodeByID(ctx, dockspace_id);
            int windowCount = node ? node->Windows.Size : 0;
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                logFile << "{\"location\":\"ofApp.cpp:492\",\"message\":\"DockSpace created\",\"data\":{\"dockspaceId\":" << dockspace_id << ",\"nodeExists\":" << (node ? "true" : "false") << ",\"windowCount\":" << windowCount << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << ",\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"F\"}\n";
                logFile.close();
            }
        }
        // #endregion
        
        // Layout loading logic: Wait for windows to be drawn first, then load layout
        // This ensures windows are registered with ImGui before layout is applied
        // Layout loading happens AFTER windows are drawn (see below after viewManager.draw())
    }
    ImGui::End();
    
    ImGui::PopStyleVar(3);
    
    // Draw menu bar at bottom (after dockspace so it appears on top)
    menuBar.draw();
    
    // Draw demo window if enabled
    if (showDemoWindow) {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }
    
    // Draw view manager (handles all views)
    viewManager.draw();
    
    // CRITICAL: Mark that windows have been drawn at least once
    // This is needed so layout can be loaded AFTER windows are registered with ImGui
    if (!windowsDrawnOnce_) {
        windowsDrawnOnce_ = true;
        ofLogNotice("ofApp") << "Windows drawn for first time";
    }
    
    // Load layout after windows have been drawn and registered with ImGui
    // This ensures docking works properly - ImGui needs windows to exist when layout is loaded
    if (layoutNeedsLoad_ && !layoutLoaded_ && windowsDrawnOnce_) {
        ofLogNotice("ofApp") << "Loading layout (windows are now registered with ImGui)";
        
        // Ensure all windows are synced before loading layout
        guiManager.syncWithRegistry();
        
        // Try to load layout from session (loadPendingImGuiState handles session layout or imgui.ini fallback)
        engine_.getSessionManager().loadPendingImGuiState();
        
        // Check if layout was actually loaded using SessionManager's tracking flag
        // This is more reliable than checking docked windows, especially when Command Shell is active
        // and Editor Shell windows aren't drawn yet
        bool layoutWasLoaded = engine_.getSessionManager().wasSessionLayoutLoaded();
        
        if (layoutWasLoaded) {
            ofLogNotice("ofApp") << "Layout loaded successfully from session";
        } else {
            ofLogNotice("ofApp") << "No layout loaded from session";
        }
        
        // Only setup default layout if no layout was actually loaded AND Editor Shell is active
        // If Command Shell is active, don't setup default layout (windows aren't drawn yet)
        if (!layoutWasLoaded) {
            if (activeShell_ && activeShell_->getName() == "Editor") {
                setupDefaultLayout(false);
                ofLogNotice("ofApp") << "Setup default layout (no saved layout found)";
            } else {
                ofLogNotice("ofApp") << "Skipping default layout setup (Command Shell active, Editor Shell not active)";
            }
        }
        
        layoutLoaded_ = true;
        layoutNeedsLoad_ = false;
        ofLogNotice("ofApp") << "Layout loading complete";
        
        // CRITICAL FIX: Re-draw Clock window after layout is loaded to ensure docking state is properly applied
        // The Clock window was drawn before the layout was loaded, so we need to re-draw it now
        // so that ImGui can properly apply the docking state from the loaded layout
        // This ensures the Clock window's docking state matches what was saved in the layout
        if (viewManager.isMasterModulesVisible()) {
            viewManager.drawClockPanel();
        }
        
        // Notify ViewManager that layout is loaded
        viewManager.setLayoutLoaded(true);
    }
    
    // CRITICAL FIX: Deferred layout save (from window resize)
    // Save layout on next frame after resize completes to avoid accessing ImGui during inconsistent state
    if (layoutNeedsSave_ && layoutLoaded_) {
        saveLayout();
        layoutNeedsSave_ = false;
    }
    
    gui.end();
}

//--------------------------------------------------------------
void ofApp::setupDefaultLayout(bool forceReset) {
    // Check if ImGui is initialized
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) {
        ofLogWarning("ofApp") << "Cannot setup default layout: ImGui not initialized";
        return;
    }
    
    // Get the dockspace ID
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    
    // Clear existing layout if force reset or if dockspace is empty
    if (forceReset) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);
    } else {
        // Check if dockspace already has windows docked
        // Use DockContextFindNodeByID to check existing state (not during building)
        ImGuiContext* ctx = ImGui::GetCurrentContext();
        if (ctx) {
            ImGuiDockNode* existing_node = ImGui::DockContextFindNodeByID(ctx, dockspace_id);
            // Check if node exists and has windows (not empty)
            if (existing_node && existing_node->Windows.Size > 0) {
                ofLogNotice("ofApp") << "Dockspace already has layout, skipping default setup";
                return;
            }
        }
    }
    
    // Start building the default layout
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);
    
    // Split the dockspace into left and right
    ImGuiID dock_id_left;
    ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.7f, nullptr, &dock_id_left);
    
    // Split left side into top and bottom
    ImGuiID dock_id_left_top;
    ImGuiID dock_id_left_bottom = ImGui::DockBuilderSplitNode(dock_id_left, ImGuiDir_Down, 0.6f, nullptr, &dock_id_left_top);
    
    // Split right side into top and bottom
    ImGuiID dock_id_right_top;
    ImGuiID dock_id_right_bottom = ImGui::DockBuilderSplitNode(dock_id_right, ImGuiDir_Down, 0.5f, nullptr, &dock_id_right_top);
    
    // Dock windows to specific locations
    // Left top: Clock and master outputs
    ImGui::DockBuilderDockWindow("Clock ", dock_id_left_top);
    ImGui::DockBuilderDockWindow("masterAudioOut", dock_id_left_top);
    ImGui::DockBuilderDockWindow("masterVideoOut", dock_id_left_top);
    
    // Left bottom: Module panels (TrackerSequencer, MultiSampler, etc.)
    // These will be docked as they are created
    
    // Right top: Utility panels
    ImGui::DockBuilderDockWindow("File Browser", dock_id_right_top);
    ImGui::DockBuilderDockWindow("Asset Library", dock_id_right_top);
    
    // Right bottom: Console and Command Bar
    ImGui::DockBuilderDockWindow("Console", dock_id_right_bottom);
    
    // Finish building
    ImGui::DockBuilderFinish(dockspace_id);
    
    ofLogNotice("ofApp") << "Default layout setup complete";
}

//--------------------------------------------------------------
void ofApp::saveLayout() {
    // Save current layout to imgui.ini (session layout file)
    // This is the layout for the current session, not the default layout
    
    // Don't save during startup
    if (!layoutLoaded_) {
        ofLogNotice("ofApp") << "Skipping layout save during startup (layout not loaded yet)";
        return;
    }
    
    // Check if ImGui is initialized
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) {
        ofLogNotice("ofApp") << "Cannot save layout: ImGui not initialized yet";
        return;
    }
    
    // Check if dockspace exists
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGuiDockNode* node = ImGui::DockContextFindNodeByID(ctx, dockspace_id);
    if (!node) {
        ofLogNotice("ofApp") << "Cannot save layout: Dockspace doesn't exist yet";
        return;
    }
    
    // Only save if windows are actually docked
    if (node->Windows.Size == 0) {
        ofLogNotice("ofApp") << "Cannot save layout: No windows are docked. Please dock windows before saving layout.";
        return;
    }
    
    // Save to imgui.ini (session layout)
    std::string layoutPath = ofToDataPath("imgui.ini", true);
    ImGui::SaveIniSettingsToDisk(layoutPath.c_str());
    ofLogNotice("ofApp") << "Session layout saved to " << layoutPath;
    
    // Also update ImGui state in current session JSON
    engine_.getSessionManager().updateImGuiStateInSession();
}

//--------------------------------------------------------------
void ofApp::loadLayout() {
    // Load layout from imgui.ini (session layout file)
    // This loads the layout for the current session, not the default layout
    
    // Check if ImGui is initialized
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) {
        ofLogWarning("ofApp") << "Cannot load layout: ImGui not initialized";
        return;
    }
    
    // Ensure windows are registered before loading layout
    if (!windowsDrawnOnce_) {
        ofLogNotice("ofApp") << "Cannot load layout yet: Windows need to be drawn first. Deferring layout load.";
        layoutNeedsLoad_ = true;
        layoutLoaded_ = false;
        return;
    }
    
    // Ensure all windows are synced before loading
    guiManager.syncWithRegistry();
    
    std::string layoutPath = ofToDataPath("imgui.ini", true);
    if (ofFile::doesFileExist(layoutPath)) {
        try {
            ImGui::LoadIniSettingsFromDisk(layoutPath.c_str());
            ofLogNotice("ofApp") << "Session layout loaded from " << layoutPath;
            
            // Update session with loaded layout
            engine_.getSessionManager().updateImGuiStateInSession();
        } catch (const std::exception& e) {
            ofLogError("ofApp") << "Failed to load layout: " << e.what();
        }
    } else {
        ofLogWarning("ofApp") << "Session layout file not found: " << layoutPath;
    }
}

//--------------------------------------------------------------
void ofApp::saveDefaultLayout() {
    // Save current layout to imgui.default.ini (true default layout)
    // This is separate from the session layout and can be manually loaded
    
    // Don't save during startup
    if (!layoutLoaded_) {
        ofLogNotice("ofApp") << "Skipping default layout save during startup (layout not loaded yet)";
        return;
    }
    
    // Check if ImGui is initialized
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) {
        ofLogNotice("ofApp") << "Cannot save default layout: ImGui not initialized yet";
        return;
    }
    
    // Check if dockspace exists
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGuiDockNode* node = ImGui::DockContextFindNodeByID(ctx, dockspace_id);
    if (!node) {
        ofLogNotice("ofApp") << "Cannot save default layout: Dockspace doesn't exist yet";
        return;
    }
    
    // Only save if windows are actually docked
    if (node->Windows.Size == 0) {
        ofLogNotice("ofApp") << "Cannot save default layout: No windows are docked. Please dock windows before saving.";
        return;
    }
    
    // Save to imgui.default.ini (default layout)
    std::string defaultLayoutPath = ofToDataPath("imgui.default.ini", true);
    ImGui::SaveIniSettingsToDisk(defaultLayoutPath.c_str());
    ofLogNotice("ofApp") << "Default layout saved to " << defaultLayoutPath;
}

//--------------------------------------------------------------
void ofApp::loadDefaultLayout() {
    // Load layout from imgui.default.ini (true default layout)
    // This loads the default layout, separate from session layout
    
    // Check if ImGui is initialized
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) {
        ofLogWarning("ofApp") << "Cannot load default layout: ImGui not initialized";
        return;
    }
    
    // Ensure windows are registered before loading layout
    if (!windowsDrawnOnce_) {
        ofLogNotice("ofApp") << "Cannot load default layout yet: Windows need to be drawn first. Deferring layout load.";
        layoutNeedsLoad_ = true;
        layoutLoaded_ = false;
        return;
    }
    
    // Ensure all windows are synced before loading
    guiManager.syncWithRegistry();
    
    std::string defaultLayoutPath = ofToDataPath("imgui.default.ini", true);
    if (ofFile::doesFileExist(defaultLayoutPath)) {
        try {
            ImGui::LoadIniSettingsFromDisk(defaultLayoutPath.c_str());
            ofLogNotice("ofApp") << "Default layout loaded from " << defaultLayoutPath;
            
            // Update session with loaded default layout
            engine_.getSessionManager().updateImGuiStateInSession();
        } catch (const std::exception& e) {
            ofLogError("ofApp") << "Failed to load default layout: " << e.what();
        }
    } else {
        ofLogWarning("ofApp") << "Default layout file not found: " << defaultLayoutPath;
    }
}

//--------------------------------------------------------------
void ofApp::addModule(const std::string& moduleType) {
    // Delegate to Engine command execution
    engine_.executeCommand("add " + moduleType);
}

//--------------------------------------------------------------
void ofApp::removeModule(const std::string& instanceName) {
    // Delegate to Engine command execution
    engine_.executeCommand("remove " + instanceName);
}

//--------------------------------------------------------------
void ofApp::updateWindowTitle() {
    // App name ?
    std::string title = "";
    // Project name
    if (engine_.getProjectManager().isProjectOpen()) {
        title += engine_.getProjectManager().getProjectName();
        
        // Session name if available
        std::string sessionName = engine_.getSessionManager().getCurrentSessionName();
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
    if (!engine_.getProjectManager().isProjectOpen()) {
        return;
    }
    
    std::string projectRoot = engine_.getProjectManager().getProjectRoot();
    
    // Set FileBrowser to project root directory
    fileBrowser.setProjectDirectory(projectRoot);
    ofLogNotice("ofApp") << "FileBrowser set to project directory: " << projectRoot;
    
    // Initialize AssetLibrary with project assets
    engine_.getAssetLibrary().initialize();
    ofLogNotice("ofApp") << "AssetLibrary initialized for project: " << engine_.getProjectManager().getProjectName();
}

//--------------------------------------------------------------
void ofApp::onProjectClosed() {
    // Reset FileBrowser to user home directory
    fileBrowser.setProjectDirectory(ofFilePath::getUserHomeDir());
    ofLogNotice("ofApp") << "FileBrowser reset to user home directory";
    
    // AssetLibrary doesn't need explicit cleanup - it will reinitialize when project opens
    // But we could clear its state if needed in the future
}

//--------------------------------------------------------------
// Shell Management
//--------------------------------------------------------------
void ofApp::setupShells(const std::string& cliCommandOrFile) {
    // Use provided CLI command/file (passed from main.cpp via static variable)
    std::string cliArg = cliCommandOrFile.empty() ? g_cliCommandOrFile : cliCommandOrFile;
    bool cliMode = !cliArg.empty();
    
    if (cliMode) {
        // Create CLI shell
        auto cliShell = std::make_unique<vt::shell::CLIShell>(&engine_, cliArg);
        cliShell->setup();
        activeShell_ = cliShell.get();
        shells_.push_back(std::move(cliShell));
        ofLogNotice("ofApp") << "CLI shell activated with: " << (cliArg.empty() ? "stdin" : cliArg);
        
        // For CLI mode, we might want to exit after execution
        // This is handled in update() by checking shouldExit()
        return;
    }
    
    // Create Command shell (default)
    auto commandShell = std::make_unique<vt::shell::CommandShell>(&engine_);
    commandShell->setup();
    commandShell_ = commandShell.get();
    commandShell_->setActive(true);  // Start active (default)
    activeShell_ = commandShell.get();
    shells_.push_back(std::move(commandShell));
    
    // Create Editor shell (available via F3)
    auto editorShell = std::make_unique<vt::shell::EditorShell>(&engine_);
    
    // Set callbacks to existing GUI components
    editorShell->setDrawGUICallback([this]() { 
        if (showGUI) {
            drawGUI(); 
        }
    });
    editorShell->setHandleKeyPressCallback([this](int key) {
        ofKeyEventArgs keyEvent(ofKeyEventArgs::Pressed, key);
        inputRouter.handleKeyPress(keyEvent);
        return false;  // Let other handlers process if needed
    });
    
    editorShell->setup();
    editorShell_ = editorShell.get();
    editorShell_->setActive(false);  // Start inactive
    shells_.push_back(std::move(editorShell));
    
    ofLogNotice("ofApp") << "Command shell activated (default)";
    ofLogNotice("ofApp") << "Editor shell ready (press F3 to activate)";
}

void ofApp::switchShell(vt::shell::Shell* shell) {
    if (!shell) return;
    
    // Deactivate current shell
    if (activeShell_) {
        activeShell_->setActive(false);
    }
    
    // Activate new shell
    activeShell_ = shell;
    activeShell_->setActive(true);
    
    ofLogNotice("ofApp") << "Switched to shell: " << shell->getName();
}

void ofApp::switchToEditor() {
    if (editorShell_) {
        // If layout was never loaded (e.g., Command Shell was active), try to load it now
        // This ensures Editor Shell gets the proper layout with docking
        if (!layoutLoaded_ && !engine_.getSessionManager().wasSessionLayoutLoaded()) {
            ofLogNotice("ofApp") << "Editor Shell activated - attempting to load layout from session";
            
            // Ensure windows are synced before loading layout
            guiManager.syncWithRegistry();
            
            // Try to load layout (this will use preserved pendingImGuiState_ if available)
            engine_.getSessionManager().loadPendingImGuiState();
            
            // Check if layout was successfully loaded
            if (engine_.getSessionManager().wasSessionLayoutLoaded()) {
                ofLogNotice("ofApp") << "Layout loaded successfully when Editor Shell activated";
                layoutLoaded_ = true;
            } else {
                ofLogNotice("ofApp") << "No layout available to load for Editor Shell";
            }
        }
        
        switchShell(editorShell_);
    }
}

void ofApp::switchToCommand() {
    if (commandShell_) {
        switchShell(commandShell_);
    } else {
        // Create Command shell on first use
        auto commandShell = std::make_unique<vt::shell::CommandShell>(&engine_);
        commandShell->setup();
        commandShell_ = commandShell.get();
        shells_.push_back(std::move(commandShell));
        switchShell(commandShell_);
    }
}

bool ofApp::handleShellKeyPress(int key) {
    // F1: Command Shell (toggle)
    if (key == OF_KEY_F1) {
        if (activeShell_ && activeShell_->getName() == "Command") {
            // Already in Command shell, switch to Editor
            switchToEditor();
        } else {
            // Switch to Command shell
            switchToCommand();
        }
        return true;
    }
    
    // F2: Patcher Shell (not implemented yet)
    if (key == OF_KEY_F2) {
        ofLogNotice("ofApp") << "F2 pressed - Patcher shell (not yet implemented)";
        return true;
    }
    
    // F3: Editor Shell
    if (key == OF_KEY_F3) {
        switchToEditor();
        return true;
    }
    

    return false;
}

vt::shell::Shell* ofApp::findShellByName(const std::string& name) {
    for (auto& shell : shells_) {
        if (shell && shell->getName() == name) {
            return shell.get();
        }
    }
    return nullptr;
}
