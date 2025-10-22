//
//  ofApp.cpp
//
//  Audiovisual Sequencer Example - Time + Sound + Video coordination
//

#include "ofApp.h"

//--------------------------------------------------------------
ofApp::~ofApp() noexcept {
}

//--------------------------------------------------------------
void ofApp::setup() {
    ofSetFrameRate(60);
    ofSetVerticalSync(true);
    ofSetLogLevel(OF_LOG_NOTICE);
    
    ofLogNotice("ofApp") << "Setting up Audiovisual Sequencer";
    
    // Setup media library with correct path for app bundle
    std::string absolutePath;
    
    // Try multiple possible paths
    // Try to load saved media directory first
    std::string savedMediaDir = loadMediaDirectory();
    bool foundDataDir = false;
    
    if (!savedMediaDir.empty() && ofDirectory(savedMediaDir).exists()) {
        ofLogNotice("ofApp") << "✅ Using saved media directory: " << savedMediaDir;
        mediaPool.setDataDirectory(savedMediaDir);
        foundDataDir = true;
    } else {
        // Fallback to default paths
        std::vector<std::string> possiblePaths;
        possiblePaths.push_back(ofFilePath::getCurrentWorkingDirectory() + "/bin/data");
        possiblePaths.push_back(ofFilePath::getCurrentWorkingDirectory() + "/data");
        possiblePaths.push_back("/Users/jaufre/works/of_v0.12.1_osx_release/addons/ofxMediaObjects/example-audiovisualSequencer/bin/data");
        
        ofLogNotice("ofApp") << "Trying to find data directory...";
        
        for (const auto& path : possiblePaths) {
            ofLogNotice("ofApp") << "Trying path: " << path;
            if (ofDirectory(path).exists()) {
                ofLogNotice("ofApp") << "✅ Found data directory: " << path;
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
    trackerSequencer.setup(&mediaPool, &clock, numSteps);
    
    // Auto-load saved state if it exists
    if (trackerSequencer.loadState("tracker_sequencer_state.json")) {
        ofLogNotice("ofApp") << "TrackerSequencer state loaded from file";
    }
    
    // Setup MediaPool directory change callback
    mediaPool.setDirectoryChangeCallback([this](const std::string& path) {
        saveMediaDirectory(path);
    });
    
    // Register step event listener
    trackerSequencer.addStepEventListener([this](int step, float bpm, const TrackerSequencer::PatternCell& cell) {
        onTrackerStepEvent(step, bpm, cell);
    });
    
    // Setup time objects using Clock wrapper
    clock.setup();
    
    // Setup MediaPool with clock reference
    mediaPool.setup(&clock);
    
    // Setup clock → TrackerSequencer connection (DIRECT step event system)
    clock.addTickListener([this](const ofxTimeBuffer& tick) {
        // Process clock tick directly through TrackerSequencer
        trackerSequencer.update(tick);
        
        // Update last triggered step for visual feedback
        lastTriggeredStep = trackerSequencer.getCurrentStep();
        
        // Debug: Log when we get clock ticks (only when playing)
        if (isPlaying) {
            ofLogVerbose("ofApp") << "Clock tick - Step: " << trackerSequencer.getCurrentStep();
        }
    });
    
    ofLogNotice("ofApp") << "Time objects setup complete";
    
    // Setup sound objects
    setupSoundObjects();
    
    // Setup visual objects
    setupVisualObjects();
    
    // Set output references in MediaPool (connection will happen when player becomes active)
    mediaPool.setOutputs(soundOutput, visualOutput);
    
    // Debug: Log audio routing
    ofLogNotice("ofApp") << "Audio routing: MediaPool -> soundOutput -> audioOut callback";
    ofLogNotice("ofApp") << "Sound output name: " << soundOutput.getName();
    ofLogNotice("ofApp") << "Sound stream setup complete";
    
    // Setup GUI
    setupGUI();
    
    // Try to load saved state, otherwise use default pattern
    if (!trackerSequencer.loadState("tracker_sequencer_state.json")) {
        // Initialize pattern with some default steps if no saved state
        if (mediaPool.getNumPlayers() > 0) {
            ofLogNotice("ofApp") << "Initializing default pattern with " << mediaPool.getNumPlayers() << " media items";
            
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
            ofLogNotice("ofApp") << "Default pattern saved";
        } else {
            ofLogWarning("ofApp") << "No media items available for pattern initialization";
        }
    }
    
    // Clock listener is set up in setupTimeObjects()
    
    ofLogNotice("ofApp") << "Audiovisual Sequencer setup complete";
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
    ofBackground(20, 20, 30);
    
    // Draw video if available and currently playing
    auto currentPlayer = mediaPool.getActivePlayer();
    if (currentPlayer && currentPlayer->isVideoLoaded() && 
        currentPlayer->videoEnabled.get() && currentPlayer->isPlaying()) {
        ofSetColor(255, 255, 255);
        currentPlayer->getVideoPlayer().getVideoFile().getTexture().draw(0, 0, ofGetWidth(), ofGetHeight());
    }
    
    // Draw GUI
    if (showGUI) {
        drawGUI();
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
    // Debug: Log audio callback frequency
    static int debugCounter = 0;
    if (++debugCounter % 100 == 0) {
        ofLogNotice("ofApp") << "audioOut callback called - buffer size: " << buffer.getNumFrames() << " frames, " << buffer.getNumChannels() << " channels";
    }
    
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
    
    // Debug: Log audio levels (only occasionally to avoid spam)
    if (debugCounter % 1000 == 0) {
        ofLogNotice("ofApp") << "Audio processing - Global volume: " << globalVolume;
        
        // Additional debugging for audio routing
        auto currentPlayer = mediaPool.getActivePlayer();
        if (currentPlayer) {
            ofLogNotice("ofApp") << "Active player audio enabled: " << currentPlayer->audioEnabled.get() 
                                 << ", volume: " << currentPlayer->volume.get()
                                 << ", isPlaying: " << currentPlayer->isPlaying()
                                 << ", audio loaded: " << currentPlayer->isAudioLoaded();
        }
        
        // Check if soundOutput is connected
        ofLogNotice("ofApp") << "SoundOutput connected: " << (soundOutput.getInputObject() != nullptr);
    }
    
    // Debug: Log when global volume changes
    static float lastGlobalVolume = globalVolume;
    if (globalVolume != lastGlobalVolume) {
        ofLogNotice("ofApp") << "Global volume changed from " << lastGlobalVolume << " to " << globalVolume;
        lastGlobalVolume = globalVolume;
    }
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
                trackerSequencer.pause();
                isPlaying = false;
                ofLogNotice("ofApp") << "Paused playback at step " << currentStep;
            } else {
                // Stop all media before starting pattern playback
                mediaPool.stopAllMedia();
                
                // Reset TrackerSequencer to beginning when starting playback
                trackerSequencer.reset();
                clock.start();
                trackerSequencer.play();
                isPlaying = true;
                
                // Initialize tracking variables
                lastTriggeredStep = 0;  // First step (0-based)
                currentStep = 0;         // Visual feedback
                
                // Sync TrackerSequencer with global playback state
                trackerSequencer.setCurrentStep(0);
                
                // Trigger first step immediately (step 1)
                trackerSequencer.triggerStep(0);
                
                ofLogNotice("ofApp") << "Started playback from beginning (step 1)";
            }
            break;
            
        case 'r':
            clock.reset();
            trackerSequencer.reset();
            trackerSequencer.stop();
            currentStep = 0;
            lastTriggeredStep = 0;
            
            // Sync TrackerSequencer with global reset state
            trackerSequencer.setCurrentStep(0);
            
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
void ofApp::onTrackerStepEvent(int step, float bpm, const TrackerSequencer::PatternCell& cell) {
    ofLogNotice("ofApp") << "TrackerSequencer step event: step=" << step << ", bpm=" << bpm << ", stepLength=" << cell.stepLength;
    
    // Synchronize ofApp::currentStep with TrackerSequencer::currentStep
    currentStep = step;
    
    // Extract parameters from PatternCell and pass to MediaPool
    // stepLength is already in beats (converted by TrackerSequencer)
    mediaPool.onStepTrigger(step, cell.mediaIndex, cell.position, 
                           cell.speed, cell.volume, cell.stepLength, 
                           cell.audioEnabled, cell.videoEnabled);
}

//--------------------------------------------------------------
void ofApp::setupSoundObjects() {
    // Setup sound output
    soundOutput.setName("Sound Output");
    
    
    // Global volume will be applied in audioOut callback
    
    // Get available audio devices
    audioDevices = soundStream.getDeviceList();
    ofLogNotice("ofApp") << "Found " << audioDevices.size() << " audio devices";
    
    // Find default output device
    for (size_t i = 0; i < audioDevices.size(); i++) {
        if (audioDevices[i].isDefaultOutput) {
            selectedAudioDevice = i;
            ofLogNotice("ofApp") << "Using default audio device: " << audioDevices[i].name;
            break;
        }
    }
    
    // Setup audio stream with selected device
    setupAudioStream();
    
    ofLogNotice("ofApp") << "Sound objects setup complete";
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
        ofLogNotice("ofApp") << "Using audio device: " << audioDevices[selectedAudioDevice].name;
    }
    
    soundStream.setup(settings);
    
    ofLogNotice("ofApp") << "Audio stream setup complete";
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
    ofLogNotice("ofApp") << "Video setup complete";
    
    ofLogNotice("ofApp") << "Visual objects setup complete";
}

// No bridge setup needed - direct connections only

//--------------------------------------------------------------
void ofApp::setupGUI() {
    // Setup ImGui
    gui.setup();
    
    ofLogNotice("ofApp") << "GUI setup complete";
}

//--------------------------------------------------------------
void ofApp::drawGUI() {
    gui.begin();
    
    // Clock panel - position at bottom left
    ImGui::SetNextWindowPos(ImVec2(10, ofGetHeight() - 100), ImGuiCond_FirstUseEver);
    ImGui::Begin("Clock", &showGUI);
    
    // Clock controls
    drawClockControls();
    
    ImGui::End();
    
    // Audio Output panel - position next to Clock
    ImGui::SetNextWindowPos(ImVec2(250, ofGetHeight() - 100), ImGuiCond_FirstUseEver);
    ImGui::Begin("Audio Output", &showGUI);
    
    // Audio controls
    drawAudioOutput();
    
    ImGui::End();
    
    // Save/Load panel - position next to Audio Output
    ImGui::SetNextWindowPos(ImVec2(490, ofGetHeight() - 200), ImGuiCond_FirstUseEver);
    ImGui::Begin("Save/Load", &showGUI);
    
    if (ImGui::Button("Save Pattern")) {
        if (trackerSequencer.saveState("tracker_sequencer_state.json")) {
            ofLogNotice("ofApp") << "Pattern saved successfully";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Pattern")) {
        if (trackerSequencer.loadState("tracker_sequencer_state.json")) {
            ofLogNotice("ofApp") << "Pattern loaded successfully";
        }
    }
    
    ImGui::Text("Auto-save on exit enabled");
    ImGui::Text("Auto-load on start enabled");
    
    ImGui::End();
    
    // Draw tracker interface using TrackerSequencer
    trackerSequencer.drawTrackerInterface();
    
    // Draw media library interface using MediaPool
    mediaPool.drawMediaPoolGUI();
    
    // Draw controls overlay
    drawControlsOverlay();
    
    gui.end();
}

//--------------------------------------------------------------
void ofApp::drawClockControls() {
    // BPM control with proper clock synchronization and debouncing
    static float bpmSlider = clock.getBPM();
    static float lastBpmUpdate = 0.0f;
    static float bpmChangeThreshold = 3.0f; // Increased threshold to prevent rapid changes
    static bool isDragging = false;
    
    if (ImGui::SliderFloat("BPM", &bpmSlider, 60.0f, 240.0f)) {
        isDragging = true;
        float currentTime = ofGetElapsedTimef();
        
        // More aggressive debouncing during playback to prevent timing disruption
        float debounceTime = isPlaying ? 0.3f : 0.1f;
        float currentBpm = clock.getBPM();
        
        if (currentTime - lastBpmUpdate > debounceTime && abs(bpmSlider - currentBpm) > bpmChangeThreshold) {
            lastBpmUpdate = currentTime;
            
            // Update the clock's BPM directly
            clock.setBPM(bpmSlider);
            
            // If playing, log the BPM change for debugging
            if (isPlaying) {
                ofLogNotice("ofApp") << "BPM changed during playback to: " << bpmSlider;
            } else {
                ofLogNotice("ofApp") << "BPM slider changed to: " << bpmSlider;
            }
        }
    } else if (isDragging && !ImGui::IsItemActive()) {
        // User finished dragging, apply final value
        isDragging = false;
        clock.setBPM(bpmSlider);
        ofLogNotice("ofApp") << "BPM drag finished at: " << bpmSlider;
    }
    
    // REMOVED: Clock sync to prevent feedback loop
    // The slider should be the source of truth, not the clock
    
    ImGui::Separator();
    
    // Play/Stop button with improved thread safety
    if (ImGui::Button(isPlaying ? "Stop" : "Play")) {
        if (isPlaying) {
            // Stop in proper order to prevent thread conflicts
            trackerSequencer.stop();
            clock.stop();
            isPlaying = false;
            ofLogNotice("ofApp") << "Stopped playback";
        } else {
            // Start in proper order to prevent thread conflicts
            trackerSequencer.reset();
            currentStep = 0;
            clock.start();
            trackerSequencer.play();
            isPlaying = true;
            ofLogNotice("ofApp") << "Started playback at BPM: " << clock.getBPM();
        }
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        clock.reset();
        trackerSequencer.reset();
        currentStep = 0;
        // Don't trigger step 0 - let TrackerSequencer handle first step naturally
    }
    
    // Status
    ImGui::Text("Status: %s", isPlaying ? "Playing" : "Stopped");
    ImGui::Text("Current Step: %d", currentStep + 1); // Display 1-16 instead of 0-15
    ImGui::Text("Direct connections: Active");
}

//--------------------------------------------------------------
void ofApp::drawAudioOutput() {
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
        ofLogNotice("ofApp") << "Audio device changed to: " << audioDevices[selectedAudioDevice].name;
    }
    
    // Global volume control with better feedback
    static float lastGlobalVolume = globalVolume;
    if (ImGui::SliderFloat("Global Volume", &globalVolume, 0.0f, 1.0f, "%.2f")) {
        // Global volume will be applied in audioOut callback
        ofLogNotice("ofApp") << "Global volume changed to: " << globalVolume;
        lastGlobalVolume = globalVolume;
    }
    
    // Simple audio level visualization with green color
    ImGui::Text("Audio Level:");
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.0f, 1.0f, 0.0f, 1.0f)); // Green color
    ImGui::ProgressBar(currentAudioLevel, ImVec2(-1, 0), "");
    ImGui::PopStyleColor();
    ImGui::Text("Level: %.3f | Volume: %.2f", currentAudioLevel, globalVolume);
    
    // Debug: Show if volume is actually changing
    if (globalVolume != lastGlobalVolume) {
        ofLogNotice("ofApp") << "Global volume slider value changed from " << lastGlobalVolume << " to " << globalVolume;
        lastGlobalVolume = globalVolume;
    }
}




//--------------------------------------------------------------
void ofApp::drawControlsOverlay() {
    // Draw controls overlay with transparent black background
    // Position at bottom-left of the window
    float windowHeight = ofGetHeight();
    ImGui::SetNextWindowPos(ImVec2(10, windowHeight - 10), ImGuiCond_Appearing);
    ImGui::SetNextWindowBgAlpha(0.8f); // Semi-transparent background
    
    if (ImGui::Begin("Controls Overlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Controls:");
        ImGui::Text("SPACE: Play/Stop");
        ImGui::Text("R: Reset");
        ImGui::Text("G: Toggle GUI");
        ImGui::Text("N/M: Next/Prev Player");
        ImGui::Text("S: Save State");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Tracker Navigation:");
        ImGui::Text("Arrow Keys: Navigate steps");
        ImGui::Text("1-9: Set media index");
        ImGui::Text("0: Clear (rest)");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), "Tracker Editing:");
        ImGui::Text("C: Clear current step");
        ImGui::Text("X: Copy from previous");
        ImGui::Text("V: Paste to current");
        ImGui::Text("P: Cycle position");
        ImGui::Text("S: Cycle speed");
        ImGui::Text("W: Cycle volume");
        ImGui::Text("L: Cycle step length");
        ImGui::Text("A: Toggle audio");
        ImGui::Text("V: Toggle video");
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