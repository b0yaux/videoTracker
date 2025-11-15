//
//  ofApp.cpp
//
//  Audiovisual Sequencer Example - Time + Sound + Video coordination
//

#include "ofApp.h"
#include <filesystem>
#include <future>

//--------------------------------------------------------------
ofApp::~ofApp() noexcept {
    // No auto-save - use menu buttons instead
}

//--------------------------------------------------------------
void ofApp::setup() {
    ofSetFrameRate(60);
    ofSetVerticalSync(true);
    ofSetLogLevel(OF_LOG_NOTICE);
    
    // Setup media library with correct path for app bundle
    std::string absolutePath;
    
    // Try multiple possible paths
    // Try to load saved media directory first
    std::string savedMediaDir = loadMediaDirectory();
    bool foundDataDir = false;
    
    if (!savedMediaDir.empty() && ofDirectory(savedMediaDir).exists()) {
        mediaPool.setDataDirectory(savedMediaDir);
        foundDataDir = true;
    } else {
        // Fallback to default paths
        std::vector<std::string> possiblePaths;
        try {
            std::string cwd = ofFilePath::getCurrentWorkingDirectory();
            possiblePaths.push_back(cwd + "/bin/data");
            possiblePaths.push_back(cwd + "/data");
        } catch (const std::filesystem::filesystem_error& e) {
            ofLogWarning("ofApp") << "Filesystem error getting current directory: " << e.what();
        } catch (const std::exception& e) {
            ofLogWarning("ofApp") << "Exception getting current directory: " << e.what();
        } catch (...) {
            ofLogWarning("ofApp") << "Unknown exception getting current directory";
        }
        possiblePaths.push_back("/Users/jaufre/works/of_v0.12.1_osx_release/addons/ofxMediaObjects/example-audiovisualSequencer/bin/data");
        
        for (const auto& path : possiblePaths) {
            if (ofDirectory(path).exists()) {
                mediaPool.setDataDirectory(path);
                saveMediaDirectory(path); // Save for next launch
                foundDataDir = true;
                break;
            }
        }
        
        if (!foundDataDir) {
            ofLogError("ofApp") << "❌ No data directory found in any of the tried paths";
        }
    }
    
    
    // Setup TrackerSequencer with clock reference
    trackerSequencer.setup(&clock, numSteps);
    
    // Auto-load saved state if it exists
    trackerSequencer.loadState("tracker_sequencer_state.json");
    
    // Setup MediaPool directory change callback
    mediaPool.setDirectoryChangeCallback([this](const std::string& path) {
        saveMediaDirectory(path);
    });
    
    
    // Register step event listener
    // Modular connection: MediaPool subscribes to TrackerSequencer trigger events
    // This replaces the old routing logic in ofApp::onTrackerStepEvent()
    mediaPool.subscribeToTrackerSequencer(&trackerSequencer);
    
    // Legacy: Keep old event listener for backward compatibility (can be removed later)
    trackerSequencer.addStepEventListener([this](int step, float duration, const PatternCell& cell) {
        onTrackerStepEvent(step, duration, cell);
    });
    
    // Setup time objects using Clock wrapper
    clock.setup();
    
    // Setup MediaPool with clock reference
    mediaPool.setup(&clock);
    
    // Initialize MediaPoolGUI with reference to mediaPool
    mediaPoolGUI.setMediaPool(mediaPool);
    
    // Setup TrackerSequencer callbacks for UI queries
    trackerSequencer.setIndexRangeCallback([this]() {
        return mediaPool.getNumPlayers();
    });
    
    // Setup parameter synchronization between TrackerSequencer and MediaPool
    // Connect TrackerSequencer currentStepPosition to MediaPool position (bidirectional)
    // Sync only when clock is paused (for editing)
    parameterSync.connect(
        &trackerSequencer,
        "currentStepPosition",
        &mediaPool,
        "position",
        [this]() {
            // Only sync when paused (for editing)
            return !clock.isPlaying();
        }
    );
    
    // Reverse connection (MediaPool -> TrackerSequencer)
    parameterSync.connect(
        &mediaPool,
        "position",
        &trackerSequencer,
        "currentStepPosition",
        [this]() {
            // Only sync when paused AND when MediaPool is not in manual preview
            // This allows manual preview to work independently
            return !clock.isPlaying() && !mediaPool.isManualPreview();
        }
    );
    
    // Set up parameter change callbacks for both modules
    trackerSequencer.setParameterChangeCallback([this](const std::string& paramName, float value) {
        if (paramName == "currentStepPosition") {
            parameterSync.notifyParameterChange(&trackerSequencer, paramName, value);
        }
    });
    
    mediaPool.setParameterChangeCallback([this](const std::string& paramName, float value) {
        if (paramName == "position") {
            parameterSync.notifyParameterChange(&mediaPool, paramName, value);
        }
    });
    
    // TrackerSequencer now uses Clock's beat events for sample-accurate timing
    // Add step event listener for visual feedback (legacy - can be removed later)
    trackerSequencer.addStepEventListener([this](int step, float duration, const PatternCell& cell) {
        lastTriggeredStep = step;
    });
    
    
    // Setup visual objects
    setupVisualObjects();
    
    // Note: Audio setup is now handled by ViewManager
    
    // Connect Clock transport events to MediaPool for proper state management
    clock.addTransportListener([this](bool isPlaying) {
        // Forward transport events to MediaPool
        mediaPool.onTransportChanged(isPlaying);
    });
    
    
    // Setup GUI
    setupGUI();
    
    // Setup MenuBar with callbacks
    menuBar.setup(
        [this]() { trackerSequencer.saveState("tracker_sequencer_state.json"); },
        [this]() { trackerSequencer.loadState("tracker_sequencer_state.json"); },
        [this]() { saveLayout(); },
        [this]() { loadLayout(); }
    );
    
    // Setup ViewManager with panel objects and sound stream
    viewManager.setup(
        &clock,
        &clockGUI,
        &soundOutput,
        &trackerSequencer,
        &trackerSequencerGUI,
        &mediaPool,
        &mediaPoolGUI,
        &soundStream
    );
    
    // Set audio listener and setup initial audio stream
    viewManager.setAudioListener(this);
    viewManager.setupAudioStream(this);
    
    // Setup InputRouter with system references
    inputRouter.setup(
        &clock,
        &trackerSequencer,
        &trackerSequencerGUI,
        &viewManager,
        &mediaPool,
        &mediaPoolGUI
    );
    
    // Setup InputRouter state callbacks
    // Note: Play state now comes directly from Clock (single source of truth)
    // InputRouter has Clock reference from setup() call above
    inputRouter.setCurrentStep(&currentStep);
    inputRouter.setLastTriggeredStep(&lastTriggeredStep);
    inputRouter.setShowGUI(&showGUI);
    
    // Try to load saved state, otherwise use default pattern
    if (!trackerSequencer.loadState("tracker_sequencer_state.json")) {
        // Initialize pattern with some default steps if no saved state
        if (mediaPool.getNumPlayers() > 0) {
            PatternCell cell0(0, 0.0f, 1.0f, 1.0f, 1.0f);
            trackerSequencer.setCell(0, cell0);
            
            if (mediaPool.getNumPlayers() > 1) {
                PatternCell cell4(1, 0.0f, 1.2f, 1.0f, 1.0f);
                trackerSequencer.setCell(4, cell4);
                
                PatternCell cell8(0, 0.5f, 1.0f, 1.0f, 1.0f);
                trackerSequencer.setCell(8, cell8);
            }
            
            // Save the default pattern
            trackerSequencer.saveState("tracker_sequencer_state.json");
        } else {
            ofLogWarning("ofApp") << "No media items available for pattern initialization";
        }
    }
    
    // Clock listener is set up in setupTimeObjects()
    
    // Initialize first active player after everything is set up
    mediaPool.initializeFirstActivePlayer();
    
    // Load default layout on startup
    loadLayout();
}

//--------------------------------------------------------------
void ofApp::update() {
    // PERFORMANCE: Only update the active player, not all 117 players
    auto currentPlayer = mediaPool.getActivePlayer();
    if (currentPlayer) {
        // CRITICAL FIX: Always call update() to check for gate ending
        // The gate ending check in MediaPlayer::update() must run every frame
        // to detect when scheduledStopActive expires, even if player appears stopped
        currentPlayer->update();
        
        // Process visual pipeline - only when actually playing (performance optimization)
        if (currentPlayer->isPlaying() && currentPlayer->videoEnabled.get() && currentPlayer->isVideoLoaded()) {
            auto& videoPlayer = currentPlayer->getVideoPlayer();
            videoPlayer.update();  // Just update, no FBO processing needed
        }
    }
    
    // Update MediaPool for end-of-media detection
    mediaPool.update();
    
    // Update step active state (clears manually triggered steps when duration expires)
    trackerSequencer.updateStepActiveState();
    
    // Connect active player (internal flag check prevents redundant connections)
    // PERFORMANCE: Only call connectActivePlayer when player is actually playing or just started
    // The playerConnected flag prevents redundant connections, but we avoid calling it every frame
    // when nothing is playing to reduce overhead
    if (currentPlayer && (currentPlayer->isPlaying() || !mediaPool.isPlayerConnected())) {
        mediaPool.connectActivePlayer(soundOutput, visualOutput);
    }


    // PERFORMANCE: Rate-limit parameter synchronization to 15Hz instead of 60Hz
    // Most parameters don't change every frame, so we can reduce CPU overhead by checking less frequently
    static int syncFrameCounter = 0;
    if (++syncFrameCounter >= 4) {  // Sync every 4 frames (15Hz instead of 60Hz)
        parameterSync.update();
        syncFrameCounter = 0;
    }
    
    // PERFORMANCE: Periodic auto-save every 30 seconds (async to avoid frame drops)
    // File I/O in update loop can cause 10-50ms stuttering every 30 seconds
    static float lastAutoSave = 0.0f;
    static bool saveInProgress = false;
    float elapsed = ofGetElapsedTimef();
    
    if (elapsed - lastAutoSave > 30.0f && !saveInProgress) {
        saveInProgress = true;
        lastAutoSave = elapsed;  // Update immediately to prevent multiple triggers
        // Async save in background thread to avoid blocking main thread
        auto future = std::async(std::launch::async, [this]() {
            trackerSequencer.saveState("tracker_sequencer_state.json");
            saveInProgress = false;  // Reset flag after save completes
            ofLogVerbose("ofApp") << "Periodic auto-save completed";
        });
        (void)future;  // Explicitly ignore return value to silence warning
    }
}

//--------------------------------------------------------------
void ofApp::draw() {
    try {
        ofBackground(0, 0, 0);
        
        // Draw video if available and currently playing
        auto currentPlayer = mediaPool.getActivePlayer();
        if (currentPlayer && currentPlayer->isVideoLoaded() && 
            currentPlayer->videoEnabled.get() && currentPlayer->isPlaying()) {
            try {
                auto& videoPlayer = currentPlayer->getVideoPlayer();
                auto& videoFile = videoPlayer.getVideoFile();
                if (videoFile.isLoaded() && videoFile.getTexture().isAllocated()) {
                    ofSetColor(255, 255, 255);
                    videoFile.getTexture().draw(0, 0, ofGetWidth(), ofGetHeight());
                }
            } catch (const std::exception& e) {
                ofLogError("ofApp") << "Exception drawing video: " << e.what();
            } catch (...) {
                ofLogError("ofApp") << "Unknown exception drawing video";
            }
        }
        
        // Draw GUI
        if (showGUI) {
            drawGUI();
        }
    } catch (const std::exception& e) {
        ofLogError("ofApp") << "Exception in draw(): " << e.what();
    } catch (...) {
        ofLogError("ofApp") << "Unknown exception in draw()";
    }
}

//--------------------------------------------------------------
void ofApp::exit() {
    // Auto-save TrackerSequencer state before exiting
    if (trackerSequencer.saveState("tracker_sequencer_state.json")) {
        ofLogNotice("ofApp") << "TrackerSequencer state saved to file";
    }
    
    clock.stop();
    soundStream.close();
}

//--------------------------------------------------------------
void ofApp::audioOut(ofSoundBuffer& buffer) {
    // Process audio-rate clock first (sample-accurate timing)
    clock.audioOut(buffer);
    
    // Audio processing happens in sound objects
    soundOutput.audioOut(buffer);
    
    // Apply global volume AFTER sound processing (from ViewManager)
    buffer *= viewManager.getGlobalVolume();
    
    // Simple audio level calculation for visualization
    float maxLevel = 0.0f;
    for (size_t i = 0; i < buffer.getNumFrames() * buffer.getNumChannels(); i++) {
        maxLevel = std::max(maxLevel, std::abs(buffer[i]));
    }
    viewManager.setCurrentAudioLevel(maxLevel);
}

//--------------------------------------------------------------
void ofApp::keyPressed(ofKeyEventArgs& keyEvent) {
    // Route all keyboard input through InputRouter
    // If InputRouter handled the key, return early to prevent ImGui from processing it
    if (inputRouter.handleKeyPress(keyEvent)) {
        return;
    }
    
    // If InputRouter didn't handle it, let ImGui process it
    // This allows normal text input and other ImGui interactions
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button) {
    // Handle mouse clicks through TrackerSequencer
    if (showGUI) {
        trackerSequencer.handleMouseClick(x, y, button);
    }
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h) {
    // Window resize handled by ImGui docking automatically
    
    // Update visual output dimensions
    visualOutput.width.set(w);
    visualOutput.height.set(h);
    
    ofLogNotice("ofApp") << "Window resized to " << w << "x" << h;
}


//--------------------------------------------------------------
void ofApp::onTrackerStepEvent(int step, float duration, const PatternCell& cell) {
    // LEGACY: This function is kept for backward compatibility but is no longer needed
    // MediaPool now subscribes directly to TrackerSequencer trigger events via subscribeToTrackerSequencer()
    // This removes the tight coupling between ofApp, TrackerSequencer, and MediaPool
    
    // Changed to verbose logging to avoid performance issues during playback
    ofLogVerbose("ofApp") << "TrackerSequencer step event: step=" << step << ", duration=" << duration << "s, length=" << cell.length;
    
    // Synchronize ofApp::currentStep with TrackerSequencer::currentStep (for UI display)
    currentStep = step;
    
    // NOTE: MediaPool is now triggered via the modular event system
    // The old routing logic below has been moved to MediaPool::onTrigger()
    // This function can be removed once we're confident the new system works
}

//--------------------------------------------------------------
void ofApp::setupSoundObjects() {
    // Setup sound output
    soundOutput.setName("Sound Output");
    
    // Note: Audio device management is now handled by ViewManager
    // Global volume is applied in audioOut callback using viewManager.getGlobalVolume()
}

//--------------------------------------------------------------
void ofApp::setupVisualObjects() {
    // Setup visual output
    visualOutput.setName("Visual Output");
    
    // Initialize visual output with proper dimensions
    visualOutput.width.set(ofGetWidth());
    visualOutput.height.set(ofGetHeight());
    visualOutput.enabled.set(true);
    
    // Note: Visual output will allocate its own buffer when needed
    
    // Video connection handled by mediaSequencer
}

// No bridge setup needed - direct connections only

//--------------------------------------------------------------
void ofApp::setupGUI() {
    // Setup ImGui with docking enabled and proper ini file handling
    gui.setup(nullptr, true, ImGuiConfigFlags_DockingEnable);
    
    // Initialize ImPlot
    ImPlot::CreateContext();
    
    // Set ini filename for auto-loading on startup
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "imgui.ini";
    
    // Set up ImGui with keyboard navigation
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGuiStyle& style = ImGui::GetStyle();
 
    style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.15f, 0.15f, 0.4f);           // Window background
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.01f, 0.01f, 0.01f, 0.6f);         // Child background
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.95f);           // Popup background
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);   // Modal dimming

    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);         // Menu bar background

    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.01f, 0.01f, 0.01f, 0.65f);       // Window title background
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.2f, 0.0f, 0.4f, 0.65f);    // Active title background
    // style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.05f, 0.05f, 0.7f); // Collapsed title background
    
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.8f);      // Scrollbar background
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.3f, 0.3f, 0.3f, 0.8f);    // Scrollbar grab
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.4f, 0.4f, 0.4f, 0.9f); // Scrollbar hover
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Scrollbar active
    
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);       // Resize grip
    // style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.3f, 0.3f, 0.3f, 0.9f); // Resize grip hover
    //style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f); // Resize grip active
    
    style.Colors[ImGuiCol_Tab] = ImVec4(0.1f, 0.1f, 0.1f, 0.8f);              // Tab background
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.2f, 0.2f, 0.2f, 0.6f);      // Tab hover
    style.Colors[ImGuiCol_TabActive] = ImVec4(0.01f, 0.01f, 0.01f, 0.8f);        // Tab active
    style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.05f, 0.05f, 0.05f, 0.7f);  // Tab unfocused
    style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.15f, 0.15f, 0.8f); // Tab unfocused active
    
    style.Colors[ImGuiCol_Separator] = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);          // Separator color
    style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.3f, 0.3f, 0.3f, 0.9f);   // Separator hover
    style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);    // Separator active
    
    // Table / Grid colors
    style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.01f, 0.01f, 0.01f, 0.8f);   // Table headers
    style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.1f, 0.1f, 0.1f, 0.8f); // Dark borders
    style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.4f, 0.4f, 0.4f, 0.6f);  // Lighter borders
    // Row backgrounds: standard grey (individual rows can override for empty/playback states)
    style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.5f);       // Row backgrounds
    style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.05f, 0.05f, 0.05f, 0.5f); // Alternative transparent rows

    style.Colors[ImGuiCol_Header] = ImVec4(0.1f, 0.1f, 0.1f, 0.8f);          // Dark headers
    // style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);   // Dark hover
    // style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.3f, 0.3f, 0.3f, 0.9f);    // Dark active
    
    style.Colors[ImGuiCol_Button] = ImVec4(0.3f, 0.3f, 0.3f, 0.8f);          // Dark buttons
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.1f, 0.1f, 0.9f, 0.9f);   // Dark hover
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.04f, 0.04f, 0.04f, 1.0f);    // Dark active
   
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.5f, 0.5f, 0.5f, 0.8f);      // Gray sliders
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.6f, 0.6f, 0.6f, 1.0f); // Gray active
    
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.03f, 0.03f, 0.03f, 0.75f);     // Dark frames
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2f, 0.2f, 0.8f, 0.8f);  // Dark hover
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.15f, 0.15f, 0.15f, 0.9f); // Dark active
    
    style.Colors[ImGuiCol_Text] = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);            // Light text
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);    // Disabled text
    
    style.Colors[ImGuiCol_Border] = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);         // Dark borders
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);    // No shadow
    
}

//--------------------------------------------------------------
void ofApp::drawGUI() {
    try {
        gui.begin();
        
        // Ensure ImGui keyboard navigation is enabled (for arrow key navigation)
        ImGuiIO& io = ImGui::GetIO();
        
        // CRITICAL: Track window focus state using ImGui's AppFocusLost flag
        // This is set by the GLFW backend when the window loses/regains focus
        // This is more reliable than checking window focus states manually
        static bool lastAppFocusLost = false;
        bool appFocusLost = io.AppFocusLost;
        
        // Detect focus regain (transition from lost to regained)
        if (lastAppFocusLost && !appFocusLost) {
            // Window regained focus - clear stale focus states and reset navigation
            ofLogNotice("ofApp") << "[FOCUS_DEBUG] Window regained focus - resetting ImGui state";
            
            // Clear any active ImGui items that might be stuck
            // This prevents input fields or other widgets from staying in an active state
            if (ImGui::IsAnyItemActive()) {
                ImGui::ClearActiveID();
                ofLogNotice("ofApp") << "[FOCUS_DEBUG] Cleared active ImGui item";
            }
            
            // Reset ImGui navigation state to prevent stale navigation
            // Access internal context to reset navigation ID (prevents stuck navigation)
            ImGuiContext* g = ImGui::GetCurrentContext();
            if (g) {
                // Reset navigation ID to clear any stale navigation state
                // This ensures keyboard navigation works properly after regaining focus
                if (g->NavId != 0) {
                    g->NavId = 0;
                    g->NavIdIsAlive = false;
                    ofLogNotice("ofApp") << "[FOCUS_DEBUG] Reset ImGui navigation ID";
                }
                
                // Clear any focused item that might be stale
                if (g->ActiveId != 0) {
                    g->ActiveId = 0;
                    g->ActiveIdWindow = nullptr;
                    ofLogNotice("ofApp") << "[FOCUS_DEBUG] Cleared stale ActiveId";
                }
            }
            
            // Clear TrackerSequencer cell focus if it's stale
            if (trackerSequencerGUI.getEditStep() >= 0) {
                ofLogNotice("ofApp") << "[FOCUS_DEBUG] Clearing TrackerSequencer cell focus (step: " 
                                     << trackerSequencerGUI.getEditStep() 
                                     << ", column: " << trackerSequencerGUI.getEditColumn() << ")";
                trackerSequencerGUI.clearCellFocus();
            }
            
            // Clear MediaPoolGUI cell focus if it's stale
            if (mediaPoolGUI.isKeyboardFocused()) {
                ofLogNotice("ofApp") << "[FOCUS_DEBUG] Clearing MediaPoolGUI cell focus";
                mediaPoolGUI.clearCellFocus();
            }
            
            // Force reset ImGui navigation state - ensure keyboard navigation is enabled
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            
            // Reset key and mouse states to prevent stuck keys from before focus was lost
            // This ensures keys/buttons pressed before losing focus don't remain "down" after regaining focus
            // ClearInputKeys() clears keyboard/gamepad state + text input buffer (equivalent to releasing all keys)
            io.ClearInputKeys();
            // ClearInputMouse() clears mouse button states
            io.ClearInputMouse();
            
            // Note: Don't manually set WantCaptureKeyboard/WantCaptureMouse - these are computed by ImGui
            // based on active widgets. They will be recalculated automatically in the next frame.
            
            ofLogNotice("ofApp") << "[FOCUS_DEBUG] Reset ImGui keyboard navigation and input states";
        } else if (!lastAppFocusLost && appFocusLost) {
            // Window lost focus
            ofLogNotice("ofApp") << "[FOCUS_DEBUG] Window lost focus";
        }
        
        lastAppFocusLost = appFocusLost;
        
        // Disable ImGui's Tab key handling - we handle Tab ourselves for panel navigation
        // This prevents ImGui from capturing Tab before our keyPressed handler
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        
        // PERFORMANCE: Cache navigation state - only update ConfigFlags when state actually changes
        // Changing ConfigFlags every frame forces ImGui to rebuild navigation tables (2-5ms overhead)
        // Only update when the state transitions, not every frame
        static bool lastNavState = true;
        bool shouldEnableNav = !(viewManager.getCurrentPanelIndex() == 2 && trackerSequencerGUI.getIsEditingCell());
        
        if (shouldEnableNav != lastNavState) {
            // State changed - update ConfigFlags only now
            if (shouldEnableNav) {
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                ofLogNotice("ofApp") << "[NAV_DEBUG] Enabled keyboard navigation";
            } else {
                io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
                ofLogNotice("ofApp") << "[NAV_DEBUG] Disabled keyboard navigation (cell editing)";
            }
            lastNavState = shouldEnableNav;
        }
        
        // Layout loading will be handled manually via menu buttons
        
        // Menu bar at top of main window
        menuBar.draw();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0.0f);
        
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | 
                                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        
                if (ImGui::Begin("DockSpace", nullptr, window_flags)) {
                    ImGui::DockSpace(ImGui::GetID("MyDockSpace"), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

                    // Draw all panels using ViewManager
                    viewManager.draw();
                }
                ImGui::End();

        // Navigation is already enabled (or will be re-enabled next frame if in edit mode)
        gui.end();
    } catch (const std::exception& e) {
        ofLogError("ofApp") << "Exception in drawGUI(): " << e.what();
    } catch (...) {
        ofLogError("ofApp") << "Unknown exception in drawGUI()";
    }
}

//--------------------------------------------------------------
void ofApp::saveLayout() {
    try {
        // Set the ini filename for saving
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = "imgui.ini";
        
        std::string iniPath = ofToDataPath("imgui.ini", true);
        ImGui::SaveIniSettingsToDisk(iniPath.c_str());
        ofLogNotice("ofApp") << "Layout saved to " << iniPath;
    } catch (const std::exception& e) {
        ofLogError("ofApp") << "Exception in saveLayout(): " << e.what();
    } catch (...) {
        ofLogError("ofApp") << "Unknown exception in saveLayout()";
    }
}

//--------------------------------------------------------------
void ofApp::loadLayout() {
    try {
        // Set the ini filename for loading
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = "imgui.ini";
        
        std::string iniPath = ofToDataPath("imgui.ini", true);
        
        if (ofFile::doesFileExist(iniPath)) {
            ImGui::LoadIniSettingsFromDisk(iniPath.c_str());
            ofLogNotice("ofApp") << "Layout loaded from " << iniPath;
        } else {
            ofLogNotice("ofApp") << "No saved layout found at " << iniPath;
        }
    } catch (const std::exception& e) {
        ofLogError("ofApp") << "Exception in loadLayout(): " << e.what();
    } catch (...) {
        ofLogError("ofApp") << "Unknown exception in loadLayout()";
    }
}


//--------------------------------------------------------------
std::string ofApp::loadMediaDirectory() {
    ofFile settingsFile("media_settings.json");
    if (settingsFile.exists()) {
        ofJson settings = ofJson::parse(settingsFile.readToBuffer().getText());
        if (settings.contains("mediaDirectory")) {
            return settings["mediaDirectory"].get<std::string>();
        }
    }
    return "";
}

//--------------------------------------------------------------
void ofApp::saveMediaDirectory(const std::string& path) {
    ofJson settings;
    settings["mediaDirectory"] = path;
    
    ofFile settingsFile("media_settings.json", ofFile::WriteOnly);
    settingsFile << settings.dump(4);
    ofLogNotice("ofApp") << "Saved media directory: " << path;
}


