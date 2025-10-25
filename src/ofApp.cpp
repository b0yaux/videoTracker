//
//  ofApp.cpp
//
//  Audiovisual Sequencer Example - Time + Sound + Video coordination
//

#include "ofApp.h"

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
        possiblePaths.push_back(ofFilePath::getCurrentWorkingDirectory() + "/bin/data");
        possiblePaths.push_back(ofFilePath::getCurrentWorkingDirectory() + "/data");
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
    trackerSequencer.addStepEventListener([this](int step, float duration, const TrackerSequencer::PatternCell& cell) {
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
    
    // TrackerSequencer now uses Clock's beat events for sample-accurate timing
    // Add step event listener for visual feedback
    trackerSequencer.addStepEventListener([this](int step, float duration, const TrackerSequencer::PatternCell& cell) {
        lastTriggeredStep = step;
    });
    
    
    // Setup sound objects
    setupSoundObjects();
    
    // Setup visual objects
    setupVisualObjects();
    
    // Connect Clock transport events to MediaPool for proper state management
    clock.addTransportListener([this](bool isPlaying) {
        // Forward transport events to MediaPool
        mediaPool.onTransportChanged(isPlaying);
    });
    
    
    // Setup GUI
    setupGUI();
    
    // Try to load saved state, otherwise use default pattern
    if (!trackerSequencer.loadState("tracker_sequencer_state.json")) {
        // Initialize pattern with some default steps if no saved state
        if (mediaPool.getNumPlayers() > 0) {
            TrackerSequencer::PatternCell cell0(0, 0.0f, 1.0f, 1.0f, 1.0f);
            cell0.audioEnabled = true;
            cell0.videoEnabled = true;
            trackerSequencer.setCell(0, cell0);
            
            if (mediaPool.getNumPlayers() > 1) {
                TrackerSequencer::PatternCell cell4(1, 0.0f, 1.2f, 1.0f, 1.0f);
                cell4.audioEnabled = true;
                cell4.videoEnabled = true;
                trackerSequencer.setCell(4, cell4);
                
                TrackerSequencer::PatternCell cell8(0, 0.5f, 1.0f, 1.0f, 1.0f);
                cell8.audioEnabled = true;
                cell8.videoEnabled = true;
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
    // Update all media players (this is crucial for video processing)
    for (size_t i = 0; i < mediaPool.getNumPlayers(); i++) {
        auto player = mediaPool.getMediaPlayer(i);
        if (player) {
            player->update();
        }
    }
    
    // Update MediaPool for end-of-media detection
    mediaPool.update();
    
    // Ensure active player is connected to outputs (modular connection management)
    // Only connect if there's an active player to avoid warning spam
    if (mediaPool.getActivePlayer()) {
        mediaPool.connectActivePlayer(soundOutput, visualOutput);
    }
    
    // Process visual pipeline - simplified for direct texture drawing
    auto currentPlayer = mediaPool.getActivePlayer();
    if (currentPlayer && currentPlayer->videoEnabled.get()) {
        auto& videoPlayer = currentPlayer->getVideoPlayer();
        videoPlayer.update();  // Just update, no FBO processing needed
    }
    
    // REMOVED: BPM update logic moved to GUI slider only
    // The clock BPM should only be updated by user interaction, not automatically
    
    // Update pattern display
    // Pattern display is now handled by TrackerSequencer
    
    // Periodic auto-save every 30 seconds
    static float lastAutoSave = 0.0f;
    if (ofGetElapsedTimef() - lastAutoSave > 30.0f) {
        trackerSequencer.saveState("tracker_sequencer_state.json");
        lastAutoSave = ofGetElapsedTimef();
        ofLogVerbose("ofApp") << "Periodic auto-save completed";
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
    
    // Apply global volume AFTER sound processing
    buffer *= globalVolume;
    
    // Simple audio level calculation for visualization
    float maxLevel = 0.0f;
    for (size_t i = 0; i < buffer.getNumFrames() * buffer.getNumChannels(); i++) {
        maxLevel = std::max(maxLevel, std::abs(buffer[i]));
    }
    currentAudioLevel = maxLevel;
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
    // Handle Alt+Spacebar for triggering current step only (check this first)
    if (key == ' ' && ofGetKeyPressed(OF_KEY_ALT)) {
        // Trigger current step manually without starting sequencer (force retrigger)
        ofLogNotice("ofApp") << "Manual trigger of step " << (currentStep + 1);
        // Step triggering is now handled by TrackerSequencer event system
        return; // Exit early to avoid regular spacebar handling
    }
    
    switch (key) {
        case ' ':
            if (isPlaying) {
                clock.stop();
                isPlaying = false;
                ofLogNotice("ofApp") << "Paused playback";
            } else {
                // Clock transport listeners will handle TrackerSequencer and MediaPool automatically
                clock.start();
                isPlaying = true;
                
                ofLogNotice("ofApp") << "Started playback from beginning (step 1)";
            }
            break;
            
        case 'r':
            clock.reset();
            trackerSequencer.reset();
            currentStep = 0;
            lastTriggeredStep = 0;
            
            ofLogNotice("ofApp") << "Reset sequencer";
            break;
            
        case 'g':
            showGUI = !showGUI;
            break;
            
        case 'n':
            // Next media player
            mediaPool.nextPlayer();
            ofLogNotice("ofApp") << "Switched to next player";
            break;
            
        case 'm':
            // Previous media player
            mediaPool.previousPlayer();
            ofLogNotice("ofApp") << "Switched to previous player";
            break;
            
        // Global save state (capital S to distinguish from speed)
        case 'S':
            trackerSequencer.saveState("pattern.json");
            break;
            
        // All pattern editing is delegated to TrackerSequencer
        default:
            // Let TrackerSequencer handle all pattern editing and navigation
            if (trackerSequencer.handleKeyPress(key)) {
                currentStep = trackerSequencer.getCurrentStep();
            }
            break;
    }
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
void ofApp::onTrackerStepEvent(int step, float duration, const TrackerSequencer::PatternCell& cell) {
    ofLogNotice("ofApp") << "TrackerSequencer step event: step=" << step << ", duration=" << duration << "s, stepLength=" << cell.stepLength;
    
    // Synchronize ofApp::currentStep with TrackerSequencer::currentStep
    currentStep = step;
    
    // Only trigger MediaPool for non-empty steps
    // Empty steps should be silent and let previous step's duration complete naturally
    if (cell.mediaIndex >= 0) {
        // Extract parameters from PatternCell and pass to MediaPool
        // Pass duration in seconds instead of stepLength in beats
        mediaPool.onStepTrigger(step, cell.mediaIndex, cell.position, 
                               cell.speed, cell.volume, duration, 
                               cell.audioEnabled, cell.videoEnabled);
    } else {
        ofLogNotice("ofApp") << "Step " << step << " is empty (rest) - no media trigger";
    }
}

//--------------------------------------------------------------
void ofApp::setupSoundObjects() {
    // Setup sound output
    soundOutput.setName("Sound Output");
    
    
    // Global volume will be applied in audioOut callback
    
    // Get available audio devices
    audioDevices = soundStream.getDeviceList();
    
    // Find default output device
    for (size_t i = 0; i < audioDevices.size(); i++) {
        if (audioDevices[i].isDefaultOutput) {
            selectedAudioDevice = i;
            break;
        }
    }
    
    // Setup audio stream with selected device
    setupAudioStream();
}

//--------------------------------------------------------------
void ofApp::setupAudioStream() {
    if (audioDevices.empty()) {
        ofLogError("ofApp") << "No audio devices available";
        return;
    }
    
    // Close existing stream if open
    soundStream.close();
    
    // Setup audio stream with selected device
    ofSoundStreamSettings settings;
    settings.setOutListener(this);
    settings.sampleRate = 44100;
    settings.numOutputChannels = 2;
    settings.numInputChannels = 0;
    settings.bufferSize = 512;
    
    if (selectedAudioDevice < audioDevices.size()) {
        settings.setOutDevice(audioDevices[selectedAudioDevice]);
    }
    
    soundStream.setup(settings);
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
    
    // Set up ImGui with keyboard navigation
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGuiStyle& style = ImGui::GetStyle();
 
    style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.7f);        // Dark neutral grey panels
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.15f, 0.15f, 0.6f);  
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.95f);
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f); // Modal dimming

    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);     

    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.01f, 0.01f, 0.01f, 0.75f);          // Window title background
    // style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.15f, 0.15f, 0.9f); // Active title background
    // style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.05f, 0.05f, 0.7f); // Collapsed title background
    
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.8f);      // Scrollbar background
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.3f, 0.3f, 0.3f, 0.8f);    // Scrollbar grab
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.4f, 0.4f, 0.4f, 0.9f); // Scrollbar hover
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Scrollbar active
    
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);       // Resize grip
    // style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.3f, 0.3f, 0.3f, 0.9f); // Resize grip hover
    //style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f); // Resize grip active
    
    style.Colors[ImGuiCol_Tab] = ImVec4(0.1f, 0.1f, 0.1f, 0.8f);              // Tab background
    // style.Colors[ImGuiCol_TabHovered] = ImVec4(0.2f, 0.2f, 0.2f, 0.9f);      // Tab hover
    // style.Colors[ImGuiCol_TabActive] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);        // Tab active
    style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.05f, 0.05f, 0.05f, 0.7f);  // Tab unfocused
    style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.15f, 0.15f, 0.8f); // Tab unfocused active
    
    style.Colors[ImGuiCol_Separator] = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);         // Dark separator
    style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.3f, 0.3f, 0.3f, 0.9f);   // Dark hover
    style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);    // Dark active
    
    // Table / Grid colors
    style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.01f, 0.01f, 0.01f, 0.8f);   // Dark table headers
    style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.1f, 0.1f, 0.1f, 0.8f); // Dark borders
    style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.4f, 0.4f, 0.4f, 0.6f);  // Lighter borders
    style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);       // Transparent row backgrounds
    style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.05f, 0.05f, 0.05f, 0.5f); // Subtle alternating rows

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
        
        // Layout loading will be handled manually via menu buttons
        
        // Menu bar at top of main window
        drawMenuBar();

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

            // Draw 3 main panels - ImGui handles positioning
            drawClockPanel();
            drawAudioOutputPanel();
            drawTrackerPanel();
            drawMediaPoolPanel();
        }
        ImGui::End();

        gui.end();
    } catch (const std::exception& e) {
        ofLogError("ofApp") << "Exception in drawGUI(): " << e.what();
    } catch (...) {
        ofLogError("ofApp") << "Unknown exception in drawGUI()";
    }
}

//--------------------------------------------------------------
void ofApp::drawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Pattern")) {
                trackerSequencer.saveState("tracker_sequencer_state.json");
            }
            if (ImGui::MenuItem("Load Pattern")) {
                trackerSequencer.loadState("tracker_sequencer_state.json");
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Layout")) {
            if (ImGui::MenuItem("Save Layout as Default")) {
                saveLayout();
            }
            if (ImGui::MenuItem("Load Default Layout")) {
                loadLayout();
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Controls")) {
                // Show controls help in a popup or window
                ImGui::OpenPopup("Controls Help");
            }
            ImGui::EndMenu();
        }
        
        // Controls help popup - this needs to be called every frame
        if (ImGui::BeginPopupModal("Controls Help", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Controls:");
            ImGui::Text("SPACE: Play/Stop");
            ImGui::Text("R: Reset");
            ImGui::Text("G: Toggle GUI");
            ImGui::Text("N: Next media");
            ImGui::Text("M: Previous media");
            ImGui::Text("S: Save pattern");
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Pattern Editing:");
            ImGui::Text("Click cells to edit");
            ImGui::Text("Drag to set values");
            ImGui::Text("Right-click for options");
            ImGui::Separator();
            if (ImGui::Button("Close")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        ImGui::EndMainMenuBar();
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
        }
    } catch (const std::exception& e) {
        ofLogError("ofApp") << "Exception in loadLayout(): " << e.what();
    } catch (...) {
        ofLogError("ofApp") << "Unknown exception in loadLayout()";
    }
}


//--------------------------------------------------------------
void ofApp::drawClockPanel() {
    if (ImGui::Begin("Clock ")) {
        // Clock controls
        clockGUI.draw(clock);
    }
    ImGui::End();
}
//--------------------------------------------------------------
void ofApp::drawAudioOutputPanel() {
    if (ImGui::Begin("Audio Output")) {
        // Audio device selection
        if (ImGui::Combo("Device", &selectedAudioDevice, [](void* data, int idx, const char** out_text) {
            auto* devices = static_cast<std::vector<ofSoundDevice>*>(data);
            if (idx >= 0 && idx < devices->size()) {
                *out_text = (*devices)[idx].name.c_str();
                return true;
            }
            return false;
        }, &audioDevices, audioDevices.size())) {
            audioDeviceChanged = true;
        }
        
        if (audioDeviceChanged) {
            setupAudioStream();
            audioDeviceChanged = false;
        }
        
        // Volume control
        ImGui::SliderFloat("Volume", &globalVolume, 0.0f, 1.0f, "%.2f");
        
        // Audio level visualization
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
        ImGui::ProgressBar(currentAudioLevel, ImVec2(-1, 0), "");
        ImGui::PopStyleColor();
        ImGui::Text("Level: %.3f", currentAudioLevel);
    }
    ImGui::End();
}

//--------------------------------------------------------------
void ofApp::drawTrackerPanel() {
    if (ImGui::Begin("Tracker Sequencer")) {
        trackerSequencer.drawTrackerInterface();
    }
    ImGui::End();
}

//--------------------------------------------------------------
void ofApp::drawMediaPoolPanel() {
    if (ImGui::Begin("Media Pool")) {
        mediaPoolGUI.draw();  // Delegate to separate GUI
    }
    ImGui::End();
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





// Old methods removed - now handled by ofxMediaSequencer


