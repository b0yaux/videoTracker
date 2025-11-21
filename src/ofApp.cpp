//
//  ofApp.cpp
//
//  Audiovisual Sequencer Example - Time + Sound + Video coordination
//

#include "ofApp.h"
#include "gui/GUIConstants.h"
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
    ofSetEscapeQuitsApp(false);
    
    // Setup media library with correct path for app bundle
    std::string absolutePath;
    
    // Note: MediaPool state is now loaded via session serialization
    // No need to manually load directory here
    
    // Initialize ParameterRouter with ModuleRegistry
    // Note: ParameterRouter can be initialized with nullptr and setRegistry() called later
    // But we initialize it here for clarity
    parameterRouter.setRegistry(&moduleRegistry);
    
    // Initialize SessionManager
    sessionManager = SessionManager(&clock, &moduleRegistry, &moduleFactory, &parameterRouter);
    
    // Try to load session first (if it exists)
    bool sessionLoaded = false;
    if (ofFile("session.json").exists()) {
        ofLogNotice("ofApp") << "Found session.json, loading session...";
        if (sessionManager.loadSession("session.json")) {
            sessionLoaded = true;
            ofLogNotice("ofApp") << "✓ Session loaded successfully";
            
            // Get modules from registry after loading (they were recreated with session UUIDs)
            trackerSequencer = std::dynamic_pointer_cast<TrackerSequencer>(
                moduleRegistry.getModule(TRACKER_INSTANCE_NAME));
            mediaPool = std::dynamic_pointer_cast<MediaPool>(
                moduleRegistry.getModule(MEDIAPOOL_INSTANCE_NAME));
            
            if (!trackerSequencer || !mediaPool) {
                ofLogWarning("ofApp") << "Session loaded but default modules not found, creating defaults...";
                sessionLoaded = false; // Fall through to create defaults
            }
        } else {
            ofLogWarning("ofApp") << "Failed to load session, creating default modules...";
        }
    }
    
    // Create default modules if session wasn't loaded
    if (!sessionLoaded) {
        trackerSequencer = std::dynamic_pointer_cast<TrackerSequencer>(
            moduleFactory.createTrackerSequencer(TRACKER_INSTANCE_NAME));
        mediaPool = std::dynamic_pointer_cast<MediaPool>(
            moduleFactory.createMediaPool(MEDIAPOOL_INSTANCE_NAME));
        
        if (!trackerSequencer || !mediaPool) {
            ofLogError("ofApp") << "Failed to create modules via factory";
            return;
        }
        
        // Register modules in registry
        std::string trackerUUID = moduleFactory.getUUID(TRACKER_INSTANCE_NAME);
        std::string poolUUID = moduleFactory.getUUID(MEDIAPOOL_INSTANCE_NAME);
        
        if (!moduleRegistry.registerModule(trackerUUID, trackerSequencer, TRACKER_INSTANCE_NAME)) {
            ofLogError("ofApp") << "Failed to register TrackerSequencer in registry";
            return;
        }
        
        if (!moduleRegistry.registerModule(poolUUID, mediaPool, MEDIAPOOL_INSTANCE_NAME)) {
            ofLogError("ofApp") << "Failed to register MediaPool in registry";
            return;
        }
    }
    
    // Note: MediaPool state (including file paths) is now loaded via session serialization
    // No need to manually load directory - it's handled by MediaPool::fromJson()
    // If session was loaded, module data is already loaded via fromJson()
    // If not, MediaPool will be empty and user can add files via drag-and-drop or directory browse
    
    // Setup TrackerSequencer with clock reference
    trackerSequencer->setup(&clock, numSteps);
    
    
    // Register step event listener
    // Modular connection: MediaPool subscribes to TrackerSequencer trigger events
    // This replaces the old routing logic in ofApp::onTrackerStepEvent()
    mediaPool->subscribeToTrackerSequencer(trackerSequencer.get());
    
    // Legacy: Keep old event listener for backward compatibility (can be removed later)
    trackerSequencer->addStepEventListener([this](int step, float duration, const PatternCell& cell) {
        onTrackerStepEvent(step, duration, cell);
    });
    
    // Setup time objects using Clock wrapper
    clock.setup();
    
    // Setup MediaPool with clock reference
    mediaPool->setup(&clock);
    
    // Initialize GUIManager with registry and parameter router
    guiManager.setRegistry(&moduleRegistry);
    guiManager.setParameterRouter(&parameterRouter);
    
    // Sync GUIManager with registry (creates GUI objects for registered modules)
    guiManager.syncWithRegistry();
    
    // Legacy: Initialize MediaPoolGUI with reference to mediaPool (for backward compatibility)
    mediaPoolGUI.setMediaPool(*mediaPool);
    
    // Setup TrackerSequencer callbacks for UI queries
    trackerSequencer->setIndexRangeCallback([this]() {
        return mediaPool->getNumPlayers();
    });
    
    // Setup parameter routing using ParameterRouter (Phase 1: replaces ParameterSync)
    // Connect TrackerSequencer currentStepPosition to MediaPool position (bidirectional)
    // Sync only when clock is paused (for editing)
    parameterRouter.connect(
        std::string(TRACKER_INSTANCE_NAME) + ".currentStepPosition",
        std::string(MEDIAPOOL_INSTANCE_NAME) + ".position",
        [this]() {
            // Only sync when paused (for editing)
            return !clock.isPlaying();
        }
    );
    
    // Reverse connection (MediaPool -> TrackerSequencer)
    parameterRouter.connect(
        std::string(MEDIAPOOL_INSTANCE_NAME) + ".position",
        std::string(TRACKER_INSTANCE_NAME) + ".currentStepPosition",
        [this]() {
            // Only sync when paused AND when MediaPool is not in manual preview
            // This allows manual preview to work independently
            return !clock.isPlaying() && !mediaPool->isManualPreview();
        }
    );
    
    // Set up parameter change callbacks for both modules
    trackerSequencer->setParameterChangeCallback([this](const std::string& paramName, float value) {
        if (paramName == "currentStepPosition") {
            parameterRouter.notifyParameterChange(trackerSequencer.get(), paramName, value);
        }
    });
    
    mediaPool->setParameterChangeCallback([this](const std::string& paramName, float value) {
        if (paramName == "position") {
            parameterRouter.notifyParameterChange(mediaPool.get(), paramName, value);
        }
    });
    
    // TrackerSequencer now uses Clock's beat events for sample-accurate timing
    // Add step event listener for visual feedback (legacy - can be removed later)
    trackerSequencer->addStepEventListener([this](int step, float duration, const PatternCell& cell) {
        lastTriggeredStep = step;
    });
    
    
    // Setup visual objects
    setupVisualObjects();
    
    // Note: Audio setup is now handled by ViewManager
    
    // Connect Clock transport events to MediaPool for proper state management
    clock.addTransportListener([this](bool isPlaying) {
        // Forward transport events to MediaPool
        mediaPool->onTransportChanged(isPlaying);
    });
    
    
    // Setup GUI
    setupGUI();
    
    // Setup FileBrowser callbacks (utility panel, not a module)
    // Set up import callback - import files to selected MediaPool instance
    fileBrowser.setImportCallback([this](const std::vector<std::string>& files, const std::string& instanceName) {
        auto module = moduleRegistry.getModule(instanceName);
        if (!module) {
            ofLogError("ofApp") << "Module not found: " << instanceName;
            return;
        }
        
        auto mediaPool = std::dynamic_pointer_cast<MediaPool>(module);
        if (!mediaPool) {
            ofLogError("ofApp") << "Module is not a MediaPool: " << instanceName;
            return;
        }
        
        // TODO: Use FileImporter for proper validation (Phase 2)
        // For now, just log the files that would be imported
        ofLogNotice("ofApp") << "Importing " << files.size() << " file(s) to " << instanceName;
        for (const auto& file : files) {
            ofLogNotice("ofApp") << "  - " << file;
        }
    });
    
    // Set up get instances callback - return available MediaPool instances
    fileBrowser.setGetInstancesCallback([this]() {
        std::vector<std::string> instances;
        auto mediaPools = moduleRegistry.getModulesByType(ModuleType::INSTRUMENT);
        for (const auto& module : mediaPools) {
            if (std::dynamic_pointer_cast<MediaPool>(module)) {
                // Get human name for this module
                moduleRegistry.forEachModule([&instances, &module](const std::string& uuid, const std::string& name, std::shared_ptr<Module> m) {
                    if (m == module) {
                        instances.push_back(name);
                    }
                });
            }
        }
        return instances;
    });
    
    // Setup MenuBar with callbacks
    menuBar.setup(
        [this]() { sessionManager.saveSession("session.json"); },  // Save full session
        [this]() { sessionManager.loadSession("session.json"); },  // Load full session
        [this]() { saveLayout(); },
        [this]() { loadLayout(); },
        [this](const std::string& moduleType) { addModule(moduleType); },
        [this]() { 
            bool visible = viewManager.isFileBrowserVisible();
            viewManager.setFileBrowserVisible(!visible);
        },
        [this]() { 
            bool visible = viewManager.isConsoleVisible();
            viewManager.setConsoleVisible(!visible);
        },
        [this]() { 
            showDemoWindow = !showDemoWindow;
            ofLogNotice("ofApp") << "[IMGUI] Toggled Demo Window: " << (showDemoWindow ? "Visible" : "Hidden");
        }
    );
    
    // Setup Console with callbacks
    console.setup(&moduleRegistry, &guiManager);
    console.setOnAddModule([this](const std::string& type) { addModule(type); });
    console.setOnRemoveModule([this](const std::string& name) { removeModule(name); });
    
    // Setup ViewManager with panel objects and sound stream
    // Use new instance-aware setup with GUIManager
    viewManager.setup(
        &clock,
        &clockGUI,
        &soundOutput,
        &guiManager,
        &fileBrowser,
        &console,
        &soundStream
    );
    
    // Set audio listener and setup initial audio stream
    viewManager.setAudioListener(this);
    viewManager.setupAudioStream(this);
    
    // Setup InputRouter with system references
    inputRouter.setup(
        &clock,
        trackerSequencer.get(),
        &trackerSequencerGUI,
        &viewManager,
        mediaPool.get(),
        &mediaPoolGUI,
        &console
    );
    
    // Set session save/load callbacks for keyboard shortcut (S key)
    inputRouter.setSessionCallbacks(
        [this]() { sessionManager.saveSession("session.json"); },
        [this]() { sessionManager.loadSession("session.json"); }
    );
    
    // Setup InputRouter state callbacks
    // Note: Play state now comes directly from Clock (single source of truth)
    // InputRouter has Clock reference from setup() call above
    inputRouter.setCurrentStep(&currentStep);
    inputRouter.setLastTriggeredStep(&lastTriggeredStep);
    inputRouter.setShowGUI(&showGUI);
    
    // Initialize default pattern if session wasn't loaded and no media available
    // (If session was loaded, pattern data is already loaded via fromJson())
    if (!sessionLoaded) {
        if (mediaPool->getNumPlayers() > 0) {
            PatternCell cell0(0, 0.0f, 1.0f, 1.0f, 1.0f);
            trackerSequencer->setCell(0, cell0);
            
            if (mediaPool->getNumPlayers() > 1) {
                PatternCell cell4(1, 0.0f, 1.2f, 1.0f, 1.0f);
                trackerSequencer->setCell(4, cell4);
                
                PatternCell cell8(0, 0.5f, 1.0f, 1.0f, 1.0f);
                trackerSequencer->setCell(8, cell8);
            }
        } else {
            ofLogWarning("ofApp") << "No media items available for pattern initialization";
        }
    }
    
    // Clock listener is set up in setupTimeObjects()
    
    // If session was loaded, we need to load media files now (they were deferred during session restore)
    // This prevents blocking during session load
    if (sessionLoaded) {
        ofLogNotice("ofApp") << "Completing deferred media loading from session...";
        mediaPool->completeDeferredLoading();
    }
    
    // Initialize first active player after everything is set up
    // NOTE: This is now also called in setDataDirectory(), but we call it here
    // as a safety measure in case no directory was loaded
    // Only initialize if we have players (setDataDirectory already initialized if directory was loaded)
    if (mediaPool->getNumPlayers() > 0) {
        mediaPool->initializeFirstActivePlayer();
    } else {
        ofLogNotice("ofApp") << "No media players available - skipping active player initialization";
    }
    
    // Load default layout on startup
    loadLayout();
}

//--------------------------------------------------------------
void ofApp::update() {
    // PERFORMANCE: Only update the active player, not all 117 players
    // CRITICAL: getActivePlayer() now validates the pointer is still valid
    auto currentPlayer = mediaPool->getActivePlayer();
    if (currentPlayer) {
        // Double-check player is still valid before accessing (defensive programming)
        try {
            // CRITICAL FIX: Always call update() to check for gate ending
            // The gate ending check in MediaPlayer::update() must run every frame
            // to detect when scheduledStopActive expires, even if player appears stopped
            currentPlayer->update();
            
            // Process visual pipeline - only when actually playing (performance optimization)
            if (currentPlayer->isPlaying() && currentPlayer->videoEnabled.get() && currentPlayer->isVideoLoaded()) {
                auto& videoPlayer = currentPlayer->getVideoPlayer();
                videoPlayer.update();  // Just update, no FBO processing needed
            }
        } catch (const std::exception& e) {
            ofLogError("ofApp") << "Exception updating player: " << e.what();
            // Player might be invalid - getActivePlayer will reset it next time
        } catch (...) {
            ofLogError("ofApp") << "Unknown exception updating player";
            // Player might be invalid - getActivePlayer will reset it next time
        }
    }
    
    // Update MediaPool for end-of-media detection
    mediaPool->update();
    
    // Update step active state (clears manually triggered steps when duration expires)
    trackerSequencer->updateStepActiveState();
    
    // Connect active player (internal flag check prevents redundant connections)
    // PERFORMANCE: Only call connectActivePlayer when player is actually playing or just started
    // The playerConnected flag prevents redundant connections, but we avoid calling it every frame
    // when nothing is playing to reduce overhead
    // CRITICAL: Re-get currentPlayer in case it was reset during update()
    currentPlayer = mediaPool->getActivePlayer();
    if (currentPlayer && (currentPlayer->isPlaying() || !mediaPool->isPlayerConnected())) {
        mediaPool->connectActivePlayer(soundOutput, visualOutput);
    }

    // Note: ParameterRouter uses event-based routing (notifyParameterChange)
    // No periodic update needed - routing happens immediately when parameters change
    
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
            sessionManager.saveSession("session.json");
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
        auto currentPlayer = mediaPool->getActivePlayer();
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
    // Auto-save full session before exiting
    if (sessionManager.saveSession("session.json")) {
        ofLogNotice("ofApp") << "Session saved to file";
    }
    
    // Cleanup ImGui (wrapper handles ImPlot and ImGui cleanup)
    gui.shutdown();
    
    clock.stop();
    soundStream.close();
}

//--------------------------------------------------------------
void ofApp::audioOut(ofSoundBuffer& buffer) {
    // Process queued parameter commands from GUI thread (lock-free)
    // This ensures parameter changes are applied in the audio thread context
    parameterRouter.processCommands();
    
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
    // All keyboard input is now handled in InputRouter:
    // - Cmd+':' toggles Console
    // - Console arrow keys for history navigation
    // - Panel navigation (Ctrl+Tab)
    // - Tracker input
    // - MediaPool input
    // - Global shortcuts
    
    // Check for MenuBar shortcuts (MAJ+a for Add Module)
    bool shiftPressed = (keyEvent.modifiers & OF_KEY_SHIFT) != 0;
    if (menuBar.handleKeyPress(keyEvent.key, shiftPressed)) {
        return;
    }
    
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
    if (showGUI && trackerSequencer) {
        trackerSequencer->handleMouseClick(x, y, button);
    }
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h) {
    // ImGuiIntegration wrapper handles window resize automatically via event listeners
    gui.onWindowResized(w, h);
    
    // Update visual output dimensions
    visualOutput.width.set(w);
    visualOutput.height.set(h);
    
    ofLogNotice("ofApp") << "Window resized to " << w << "x" << h;
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo) {
    if (dragInfo.files.empty()) {
        return;
    }
    
    // Get mouse position from drag info
    ofPoint mousePos = dragInfo.position;
    
    // Convert to ImGui coordinates (ImGui uses screen coordinates)
    ImVec2 imguiMousePos(mousePos.x, mousePos.y);
    
    // Filter valid media files
    std::vector<std::string> validFiles;
    for (const auto& filePath : dragInfo.files) {
        ofFile file(filePath);
        if (!file.exists()) {
            continue;
        }
        
        std::string ext = ofToLower(ofFilePath::getFileExt(filePath));
        bool isAudio = (ext == "wav" || ext == "mp3" || ext == "aiff" || ext == "aif" || ext == "m4a");
        bool isVideo = (ext == "mov" || ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "webm" || ext == "hap");
        
        if (isAudio || isVideo) {
            validFiles.push_back(filePath);
        }
    }
    
    if (validFiles.empty()) {
        ofLogNotice("ofApp") << "No valid media files in drag-and-drop";
        return;
    }
    
    // Find which MediaPool window received the drop by checking all ImGui windows
    // We need to check windows that are currently open
    std::string targetInstanceName;
    
    // Get all MediaPool GUI instances
    auto allMediaPoolGUIs = guiManager.getAllMediaPoolGUIs();
    
    // Check each MediaPool window to see if the drop position is within it
    for (auto* mediaPoolGUI : allMediaPoolGUIs) {
        if (!mediaPoolGUI) continue;
        
        std::string instanceName = mediaPoolGUI->getInstanceName();
        std::string windowTitle = instanceName;  // Window title matches instance name
        
        // Check if window exists and is visible
        ImGuiWindow* window = ImGui::FindWindowByName(windowTitle.c_str());
        if (window && window->Active) {
            // Check if mouse position is within window bounds
            ImVec2 windowPos = window->Pos;
            ImVec2 windowSize = window->Size;
            ImVec2 windowMax = ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y);
            
            if (imguiMousePos.x >= windowPos.x && imguiMousePos.x <= windowMax.x &&
                imguiMousePos.y >= windowPos.y && imguiMousePos.y <= windowMax.y) {
                targetInstanceName = instanceName;
                break;
            }
        }
    }
    
    // If no specific window was found, use the first visible MediaPool instance as default
    if (targetInstanceName.empty()) {
        auto visibleInstances = guiManager.getVisibleInstances(ModuleType::INSTRUMENT);
        if (!visibleInstances.empty()) {
            targetInstanceName = *visibleInstances.begin();
        } else if (!allMediaPoolGUIs.empty()) {
            // Fallback to first available instance
            targetInstanceName = allMediaPoolGUIs[0]->getInstanceName();
        }
    }
    
    // Add files to the target MediaPool instance
    if (!targetInstanceName.empty()) {
        auto mediaPoolModule = std::dynamic_pointer_cast<MediaPool>(
            moduleRegistry.getModule(targetInstanceName));
        
        if (mediaPoolModule) {
            ofLogNotice("ofApp") << "Adding " << validFiles.size() << " file(s) to MediaPool: " << targetInstanceName;
            mediaPoolModule->addMediaFiles(validFiles);
        } else {
            ofLogWarning("ofApp") << "MediaPool instance not found: " << targetInstanceName;
        }
    } else {
        ofLogWarning("ofApp") << "No MediaPool instance available for drag-and-drop";
    }
}


//--------------------------------------------------------------
void ofApp::onTrackerStepEvent(int step, float duration, const PatternCell& cell) {
    // LEGACY: This function is kept for backward compatibility but is no longer needed
    // MediaPool now subscribes directly to TrackerSequencer trigger events via subscribeToTrackerSequencer()
    // This removes the tight coupling between ofApp, TrackerSequencer, and MediaPool
    
    // Changed to verbose logging to avoid performance issues during playback
    ofLogVerbose("ofApp") << "TrackerSequencer step event: step=" << step << ", duration=" << duration << "s, length=" << cell.length;
    
    // Synchronize ofApp::currentStep with TrackerSequencer::currentStep (for UI display)
    if (trackerSequencer) {
        currentStep = trackerSequencer->getCurrentStep();
    } else {
        currentStep = step;
    }
    
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
    // Setup ImGui with docking enabled and ini file handling
    // ImGuiIntegration wrapper handles window context automatically
    // Note: wrapper's setup() takes 3 params: (window, autoDraw, flags)
    // The 4th param (restoreGuiState) is handled automatically via ini file
    gui.setup(nullptr, true, ImGuiConfigFlags_DockingEnable);
    
    // Set ini filename for auto-loading on startup (wrapper sets default, but we can override)
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "imgui.ini";
    
    // Enable keyboard navigation
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // Note: ImPlot context is created automatically by ImGuiIntegration::setup()
    // No need to call ImPlot::CreateContext() here

    // Apply centralized color theme
    GUIConstants::applyImGuiStyle();
    
    // Log ImGui version for verification
    ofLogNotice("ofApp") << "ImGui Version: " << ImGui::GetVersion();
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
        
        // Layout loading will be handled manually via menu buttons
        
        // Menu bar at top of main window
        menuBar.draw();
        
        // PERFORMANCE: Cache navigation state - only update ConfigFlags when state actually changes
        // Changing ConfigFlags every frame forces ImGui to rebuild navigation tables (2-5ms overhead)
        // Only update when the state transitions, not every frame
        static bool lastNavState = true;
        bool shouldEnableNav = !(viewManager.getCurrentPanelIndex() == 2 && trackerSequencerGUI.getIsEditingCell()) &&
                               !(viewManager.isConsoleVisible() && console.isInputTextFocused());
        
        if (shouldEnableNav != lastNavState) {
            // State changed - update ConfigFlags only now
            if (shouldEnableNav) {
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                ofLogNotice("ofApp") << "[NAV_DEBUG] Enabled keyboard navigation";
            } else {
                io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
                ofLogNotice("ofApp") << "[NAV_DEBUG] Disabled keyboard navigation (cell editing or console input)";
            }
            lastNavState = shouldEnableNav;
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        
        // Use DockSpaceOverViewport for more reliable docking (newer ImGui API)
        // This ensures the dockspace is always properly created and cleaned up
        // It handles Begin/End internally, so we don't need to worry about matching calls
        // Signature: DockSpaceOverViewport(ImGuiID dockspace_id = 0, const ImGuiViewport* viewport = NULL, ImGuiDockNodeFlags flags = 0, ...)
        ImGui::DockSpaceOverViewport(0, viewport, ImGuiDockNodeFlags_PassthruCentralNode);
        
        // ImGui Demo Window with opaque background and toggleable via View menu or keyboard shortcut (Ctrl+D)
        // Keyboard shortcut: Ctrl+D to toggle demo window
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            showDemoWindow = !showDemoWindow;
            ofLogNotice("ofApp") << "[IMGUI] Toggled Demo Window: " << (showDemoWindow ? "Visible" : "Hidden");
        }

        if (showDemoWindow) {
            ImGui::SetNextWindowBgAlpha(1.0f); // fully opaque
            ImGui::ShowDemoWindow(&showDemoWindow);
        }

        // Draw all panels using ViewManager
        // Wrap in try-catch to ensure exceptions don't break the frame
        try {
            viewManager.draw();
        } catch (const std::exception& e) {
            ofLogError("ofApp") << "Exception in viewManager.draw(): " << e.what();
        } catch (...) {
            ofLogError("ofApp") << "Unknown exception in viewManager.draw()";
        }

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


// Note: Media directory persistence removed - now handled by session serialization
// Individual file paths are saved in MediaPool::toJson() and restored in MediaPool::fromJson()

//--------------------------------------------------------------
void ofApp::addModule(const std::string& moduleType) {
    ofLogNotice("ofApp") << "Adding module: " << moduleType;
    
    std::shared_ptr<Module> newModule;
    std::string instanceName;
    
    if (moduleType == "MediaPool") {
        // Generate unique name (e.g., "pool2", "pool3", etc.)
        int poolNum = 2;
        do {
            instanceName = "pool" + std::to_string(poolNum);
            poolNum++;
        } while (moduleRegistry.hasModule(instanceName));
        
        newModule = std::dynamic_pointer_cast<MediaPool>(
            moduleFactory.createMediaPool(instanceName));
        
        if (newModule) {
            auto mediaPool = std::dynamic_pointer_cast<MediaPool>(newModule);
            if (mediaPool) {
                // Setup MediaPool with clock reference
                mediaPool->setup(&clock);
                
                // Note: MediaPool state (file paths) is loaded via session serialization
                // User can add files via drag-and-drop or directory browse
            }
        }
    } else if (moduleType == "TrackerSequencer") {
        // Generate unique name (e.g., "tracker2", "tracker3", etc.)
        int trackerNum = 2;
        do {
            instanceName = "tracker" + std::to_string(trackerNum);
            trackerNum++;
        } while (moduleRegistry.hasModule(instanceName));
        
        newModule = std::dynamic_pointer_cast<TrackerSequencer>(
            moduleFactory.createTrackerSequencer(instanceName));
        
        if (newModule) {
            auto tracker = std::dynamic_pointer_cast<TrackerSequencer>(newModule);
            if (tracker) {
                // Setup TrackerSequencer with clock reference
                tracker->setup(&clock, numSteps);
                
                // Setup index range callback
                tracker->setIndexRangeCallback([this]() {
                    return mediaPool->getNumPlayers();
                });
            }
        }
    } else {
        ofLogError("ofApp") << "Unknown module type: " << moduleType;
        return;
    }
    
    if (!newModule) {
        ofLogError("ofApp") << "Failed to create module: " << moduleType;
        return;
    }
    
    // Get UUID from factory
    std::string uuid = moduleFactory.getUUID(instanceName);
    if (uuid.empty()) {
        ofLogError("ofApp") << "Failed to get UUID for module: " << instanceName;
        return;
    }
    
    // Register module in registry
    if (!moduleRegistry.registerModule(uuid, newModule, instanceName)) {
        ofLogError("ofApp") << "Failed to register module in registry: " << instanceName;
        return;
    }
    
    ofLogNotice("ofApp") << "✓ Successfully created and registered: " << instanceName 
                         << " (UUID: " << uuid << ")";
    
    // Sync GUIManager to create GUI objects for new instance
    guiManager.syncWithRegistry();
    
    // Make the new instance visible by default
    guiManager.setInstanceVisible(instanceName, true);
    
    ofLogNotice("ofApp") << "✓ Synced GUIManager (GUI panel created for " << instanceName << ")";
    ofLogNotice("ofApp") << "✓ Made " << instanceName << " visible";
}

//--------------------------------------------------------------
void ofApp::removeModule(const std::string& instanceName) {
    ofLogNotice("ofApp") << "Removing module: " << instanceName;
    
    // Check if module exists
    if (!moduleRegistry.hasModule(instanceName)) {
        ofLogWarning("ofApp") << "Module not found: " << instanceName;
        return;
    }
    
    // Prevent removing default instances
    if (instanceName == MEDIAPOOL_INSTANCE_NAME || instanceName == TRACKER_INSTANCE_NAME) {
        ofLogWarning("ofApp") << "Cannot remove default instance: " << instanceName;
        return;
    }
    
    // Remove from registry
    if (!moduleRegistry.removeModule(instanceName)) {
        ofLogError("ofApp") << "Failed to remove module from registry: " << instanceName;
        return;
    }
    
    // Sync GUIManager (removes GUI objects for deleted modules)
    guiManager.syncWithRegistry();
    
    ofLogNotice("ofApp") << "✓ Successfully removed module: " << instanceName;
}


