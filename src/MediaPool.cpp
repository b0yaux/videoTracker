
#include "MediaPool.h"
#include "MediaPlayer.h"
#include "TrackerSequencer.h"
#include "ofFileUtils.h"
#include "ofSystemUtils.h"
#include <unordered_map>
#include <unordered_set>

MediaPool::MediaPool(const std::string& dataDir) 
    : currentIndex(0), dataDirectory(dataDir), isSetup(false), currentMode(PlaybackMode::IDLE), 
      currentPlayStyle(PlayStyle::ONCE), clock(nullptr), activePlayer(nullptr), 
      lastTransportState(false), playerConnected(false), gateTimerActive(false), gateEndTime(0.0f),
      positionMemory(PositionMemoryMode::PER_INDEX), lastTriggeredStep(-1) {
    // setup() will be called later with clock reference
}

MediaPool::~MediaPool() {
    clear();
}

void MediaPool::setup(Clock* clockRef) {
    if (isSetup) return;
    
    clock = clockRef; // Store clock reference
    ofLogNotice("ofxMediaPool") << "Setting up media library with directory: " << dataDirectory;
    isSetup = true;
}

void MediaPool::setCustomPath(const std::string& absolutePath) {
    ofLogNotice("ofxMediaPool") << "Setting custom absolute path: " << absolutePath;
    
    ofDirectory dir(absolutePath);
    if (!dir.exists()) {
        ofLogError("ofxMediaPool") << "Custom path does not exist: " << absolutePath;
        return;
    }
    
    dataDirectory = absolutePath;
    clear();
    
    ofLogNotice("ofxMediaPool") << "✅ Using custom path: " << absolutePath;
    
    // Scan the custom directory
    scanMediaFiles(absolutePath, dir);
    
    // Auto-pair files
    mediaPair();
}

void MediaPool::scanDirectory(const std::string& path) {
    dataDirectory = path;
    clear();
    
    ofLogNotice("ofxMediaPool") << "🔍 scanDirectory called with path: " << path;
    
    // Simple approach: just use the provided path
    ofDirectory dir(path);
    if (!dir.exists()) {
        ofLogError("ofxMediaPool") << "Directory does not exist: " << path;
        return;
    }
    
    ofLogNotice("ofxMediaPool") << "✅ Directory exists, scanning for media files...";
    
    // Scan for media files
    scanMediaFiles(path, dir);
}


void MediaPool::mediaPair() {
    // Clear existing players before creating new ones
    players.clear();
    
    // Build hash map of video files by base name for O(1) lookup
    std::unordered_map<std::string, std::string> videoMap;
    for (const auto& videoFile : videoFiles) {
        std::string videoBase = getBaseName(videoFile);
        videoMap[videoBase] = videoFile;
    }
    
    // Track which video files have been paired
    std::unordered_set<std::string> pairedVideos;
    
    // Create paired players for matching audio/video files
    for (const auto& audioFile : audioFiles) {
        std::string audioBase = getBaseName(audioFile);
        auto it = videoMap.find(audioBase);
        
        if (it != videoMap.end()) {
            // Create paired player
            auto player = std::make_unique<MediaPlayer>();
            player->load(audioFile, it->second);
            players.push_back(std::move(player));
            pairedVideos.insert(audioBase);
        } else {
            // Create audio-only player
            auto player = std::make_unique<MediaPlayer>();
            player->loadAudio(audioFile);
            players.push_back(std::move(player));
        }
    }
    
    // Create video-only players for unmatched video files
    for (const auto& videoFile : videoFiles) {
        std::string videoBase = getBaseName(videoFile);
        if (pairedVideos.find(videoBase) == pairedVideos.end()) {
            // Create video-only player
            auto player = std::make_unique<MediaPlayer>();
            player->loadVideo(videoFile);
            players.push_back(std::move(player));
        }
    }
    
    ofLogNotice("ofxMediaPool") << "Created " << players.size() << " media players";
}


void MediaPool::pairByIndex() {
    clear();
    
    ofLogNotice("ofxMediaPool") << "Pairing files by index";
    
    size_t maxPairs = std::max(audioFiles.size(), videoFiles.size());
    
    for (size_t i = 0; i < maxPairs; i++) {
        auto player = std::make_unique<MediaPlayer>();
        
        std::string audioFile = (i < audioFiles.size()) ? audioFiles[i] : "";
        std::string videoFile = (i < videoFiles.size()) ? videoFiles[i] : "";
        
        player->load(audioFile, videoFile);
        players.push_back(std::move(player));
        
        ofLogNotice("ofxMediaPool") << "Index pair " << i << ": " 
                                       << ofFilePath::getFileName(audioFile) 
                                       << " + " << ofFilePath::getFileName(videoFile);
    }
    
    ofLogNotice("ofxMediaPool") << "Created " << players.size() << " media players by index";
}

MediaPlayer* MediaPool::getMediaPlayer(size_t index) {
    if (index >= players.size()) return nullptr;
    return players[index].get();
}

MediaPlayer* MediaPool::getMediaPlayerByName(const std::string& name) {
    for (auto& player : players) {
        // This would need to be implemented based on how we want to name players
        // For now, return first player
        return player.get();
    }
    return nullptr;
}

MediaPlayer* MediaPool::getCurrentPlayer() {
    if (players.empty()) return nullptr;
    return players[currentIndex].get();
}

MediaPlayer* MediaPool::getNextPlayer() {
    if (players.empty()) return nullptr;
    currentIndex = (currentIndex + 1) % players.size();
    return getCurrentPlayer();
}

MediaPlayer* MediaPool::getPreviousPlayer() {
    if (players.empty()) return nullptr;
    currentIndex = (currentIndex == 0) ? players.size() - 1 : currentIndex - 1;
    return getCurrentPlayer();
}

void MediaPool::setCurrentIndex(size_t index) {
    if (index < players.size()) {
        currentIndex = index;
    }
}

void MediaPool::nextPlayer() {
    getNextPlayer();
}

void MediaPool::previousPlayer() {
    getPreviousPlayer();
}

size_t MediaPool::getNumPlayers() const {
    return players.size();
}

size_t MediaPool::getCurrentIndex() const {
    return currentIndex;
}

std::vector<std::string> MediaPool::getPlayerNames() const {
    std::vector<std::string> names;
    for (size_t i = 0; i < players.size(); i++) {
        auto player = players[i].get();
        if (player) {
            std::string name = "[" + std::to_string(i) + "] ";
            
            // Try to get meaningful names from loaded files
            bool hasAudio = player->isAudioLoaded();
            bool hasVideo = player->isVideoLoaded();
            
            if (hasAudio && hasVideo) {
                // For paired files, try to get a base name
                name += "A+V";
            } else if (hasAudio) {
                name += "Audio";
            } else if (hasVideo) {
                name += "Video";
            } else {
                name += "Empty";
            }
            
            names.push_back(name);
        } else {
            names.push_back("[" + std::to_string(i) + "] Invalid");
        }
    }
    return names;
}

std::vector<std::string> MediaPool::getPlayerFileNames() const {
    std::vector<std::string> fileNames;
    for (size_t i = 0; i < players.size(); i++) {
        auto player = players[i].get();
        if (player) {
            std::string fileName = "";
            
            // Get actual file names from the player
            std::string audioFile = player->getAudioFilePath();
            std::string videoFile = player->getVideoFilePath();
            
            if (!audioFile.empty() && !videoFile.empty()) {
                // Paired files - show both
                fileName = ofFilePath::getFileName(audioFile) + " | " + ofFilePath::getFileName(videoFile);
            } else if (!audioFile.empty()) {
                // Audio only
                fileName = ofFilePath::getFileName(audioFile);
            } else if (!videoFile.empty()) {
                // Video only
                fileName = ofFilePath::getFileName(videoFile);
            } else {
                fileName = "empty_" + std::to_string(i);
            }
            
            fileNames.push_back(fileName);
        } else {
            fileNames.push_back("invalid_" + std::to_string(i));
        }
    }
    return fileNames;
}

std::vector<std::string> MediaPool::getAudioFiles() const {
    return audioFiles;
}

std::vector<std::string> MediaPool::getVideoFiles() const {
    return videoFiles;
}

void MediaPool::clear() {
    players.clear();
    audioFiles.clear();
    videoFiles.clear();
    currentIndex = 0;
    positionMemory.clear();  // Clear position memory when clearing media pool
}

void MediaPool::refresh() {
    scanDirectory(dataDirectory);
    mediaPair();
}

// Helper methods
std::string MediaPool::getBaseName(const std::string& filename) {
    std::string baseName = ofFilePath::getBaseName(filename);
    return baseName;
}


bool MediaPool::isAudioFile(const std::string& filename) {
    std::string ext = ofFilePath::getFileExt(filename);
    return (ext == "wav" || ext == "mp3" || ext == "aiff");
}

bool MediaPool::isVideoFile(const std::string& filename) {
    std::string ext = ofFilePath::getFileExt(filename);
    return (ext == "mov" || ext == "mp4" || ext == "avi");
}


std::string MediaPool::getMediaDirectory() const {
    return dataDirectory;
}

// Connection management methods
void MediaPool::setActivePlayer(size_t index) {
    if (index >= players.size()) {
        ofLogWarning("ofxMediaPool") << "Invalid player index: " << index;
        return;
    }
    
    MediaPlayer* newPlayer = players[index].get();
    
    // PERFORMANCE CRITICAL FIX: Only disconnect/reset if player actually changed
    // This prevents unnecessary reconnection attempts that cause mutex locks and warnings
    if (activePlayer != newPlayer) {
        // Player changed - disconnect old player (removed debug logging for performance)
        if (activePlayer) {
            disconnectActivePlayer();
        }
        
        activePlayer = newPlayer;
        currentIndex = index;  // Keep currentIndex in sync with activePlayer
        
        // Reset connection flag - new player needs to be connected
        playerConnected = false;
    } else {
        // Same player - keep currentIndex in sync but don't reset connection flag
        // This prevents unnecessary reconnection attempts when setActivePlayer is called
        // multiple times with the same index (e.g., from processEventQueue)
        currentIndex = index;
        // playerConnected flag remains unchanged - connections are still valid
    }
    
    // Note: Output connections are now managed externally by ofApp
}

MediaPlayer* MediaPool::getActivePlayer() {
    return activePlayer;
}


void MediaPool::connectActivePlayer(ofxSoundOutput& soundOut, ofxVisualOutput& visualOut) {
    if (!activePlayer) {
        return;  // No active player, silent return
    }
    
    // PERFORMANCE CRITICAL: Use simple flag check to avoid expensive connection state queries
    // The flag is managed by setActivePlayer() which only resets it when player actually changes
    // This avoids mutex locks and expensive connection state checks on every frame
    if (playerConnected) {
        return; // Already connected, skip expensive checks
    }
    
    // PERFORMANCE CRITICAL: Check if we're already connected to this player before doing anything
    // This avoids expensive disconnect() and connect() calls when not needed
    bool videoAlreadyConnected = false;
    if (activePlayer->isVideoLoaded()) {
        auto& videoPlayer = activePlayer->getVideoPlayer();
        // Quick check: see if we're already connected to this specific player
        if (videoPlayer.getInputObject() == &visualOut) {
            videoAlreadyConnected = true;
        } else {
            // Not connected to this player - disconnect old connections
            visualOut.disconnect();
        }
    }
    
    // Connect audio and video outputs
    try {
        // Connect the MediaPlayer's audio output to the soundOutput
        activePlayer->getAudioPlayer().connectTo(soundOut);
        
        // Only connect video output if not already connected to this player
        if (activePlayer->isVideoLoaded() && !videoAlreadyConnected) {
            visualOut.connectTo(activePlayer->getVideoPlayer());
        }
        
        // Mark as connected AFTER successful connection attempts
        // This prevents future unnecessary connection attempts
        playerConnected = true;
    } catch (const std::exception& e) {
        ofLogError("ofxMediaPool") << "Failed to connect player: " << e.what();
        // Don't set playerConnected = true on error - will retry next frame
        return;
    }
}

void MediaPool::disconnectActivePlayer() {
    if (!activePlayer) {
        return;
    }
    
    // Disconnect audio output
    activePlayer->getAudioPlayer().disconnect();
    
    // NOTE: Video disconnection is handled by checking connection state in connectActivePlayer()
    // We don't disconnect video here because visualOut is a shared resource that persists
    // across player changes. The connection check in connectActivePlayer() will prevent
    // reconnection attempts when video is already connected.
    
    playerConnected = false;
}

void MediaPool::initializeFirstActivePlayer() {
    if (!players.empty() && !activePlayer) {
        setActivePlayer(0);
        ofLogNotice("ofxMediaPool") << "Initialized first player as active (index 0)";
    }
}

//--------------------------------------------------------------
bool MediaPool::playMediaManual(size_t index, float position) {
    std::lock_guard<std::mutex> lock(stateMutex);
    
    if (index >= players.size()) {
        ofLogWarning("ofxMediaPool") << "Invalid media index for manual playback: " << index;
        return false;
    }
    
    MediaPlayer* player = players[index].get();
    if (!player) {
        ofLogWarning("ofxMediaPool") << "Media player not found for index: " << index;
        return false;
    }
    
    // Check if player has any media loaded
    if (!player->isAudioLoaded() && !player->isVideoLoaded()) {
        ofLogWarning("ofxMediaPool") << "No media loaded for player at index: " << index;
        return false;
    }
    
    // Stop current playback before starting new one
    if (activePlayer && activePlayer != player) {
        activePlayer->stop();
    }
    
    // Only set active player if it's different (optimization: avoid resetting connection flag)
    if (currentIndex != index || activePlayer != player) {
        setActivePlayer(index);
    }
    
    // Transition to MANUAL_PREVIEW state (atomic write)
    currentMode.store(PlaybackMode::MANUAL_PREVIEW, std::memory_order_relaxed);
    
    // Stop and reset the player for fresh playback
    player->stop();  // Stop any current playback
    
    // Convert absolute position to relative within region
    float regionStartVal = player->regionStart.get();
    float regionEndVal = player->regionEnd.get();
    float regionSize = regionEndVal - regionStartVal;
    
    float relativePos = 0.0f;
    if (regionSize > 0.001f) {
        // Clamp to region bounds, then map to relative
        float clampedAbsolute = std::max(regionStartVal, std::min(regionEndVal, position));
        relativePos = (clampedAbsolute - regionStartVal) / regionSize;
        relativePos = std::max(0.0f, std::min(1.0f, relativePos));
    } else {
        // If region is invalid, use position directly (clamped)
        relativePos = std::max(0.0f, std::min(1.0f, position));
    }
    
    // PERFORMANCE CRITICAL: Only set startPosition before play() - don't set playheadPosition
    // because that triggers onPlayheadPositionChanged() which calls expensive setPosition() (~200ms).
    // The play() method will handle setting the actual video position efficiently.
    player->startPosition.set(relativePos);
    // NOTE: Don't set player->playheadPosition here - it triggers expensive setPosition() via listener
    // The play() method will set the position correctly based on startPosition
    
    // Re-enable audio/video if they were loaded (stop() disables them)
    if (player->isAudioLoaded()) {
        player->audioEnabled.set(true);
    }
    if (player->isVideoLoaded()) {
        player->videoEnabled.set(true);
    }
    
    // Set loop based on play style
    bool shouldLoop = (currentPlayStyle == PlayStyle::LOOP);
    player->loop.set(shouldLoop);
    
    // Now play
    player->play();
    
    ofLogNotice("ofxMediaPool") << "Manual playback started for media " << index << " at position " << position << " (state: MANUAL_PREVIEW)";
    ofLogNotice("ofxMediaPool") << "Player state - audio enabled: " << player->audioEnabled.get() 
                                << ", video enabled: " << player->videoEnabled.get()
                                << ", audio loaded: " << player->isAudioLoaded()
                                << ", video loaded: " << player->isVideoLoaded();
    return true;
}

// Note: Old onStepTrigger methods removed - now using onTrigger() which receives TriggerEvent directly
// stopManualPreview() removed - update() now automatically transitions MANUAL_PREVIEW → IDLE when player stops

//--------------------------------------------------------------
void MediaPool::stopAllMedia() {
    std::lock_guard<std::mutex> lock(stateMutex);
    
    // Clear event queue
    while (!eventQueue.empty()) {
        eventQueue.pop();
    }
    
    for (auto& player : players) {
        if (player) {
            player->stop();
        }
    }
    
    if (activePlayer) {
        disconnectActivePlayer();
    }
    
    // Reset gate timer and transition to IDLE
    gateTimerActive = false;
    currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
}

//--------------------------------------------------------------
void MediaPool::setDataDirectory(const std::string& path) {
    ofLogNotice("ofxMediaPool") << "Setting data directory to: " << path;
    
    ofDirectory dir(path);
    if (!dir.exists()) {
        ofLogError("ofxMediaPool") << "Directory does not exist: " << path;
        return;
    }
    
    ofLogNotice("ofxMediaPool") << "✅ Using data directory: " << path;
    
    // Use the existing scanDirectory method to populate audioFiles and videoFiles
    scanDirectory(path);
    
    // Create media players from the scanned files
    mediaPair();
    
    // Notify ofApp about directory change
    if (onDirectoryChanged) {
        onDirectoryChanged(path);
    }
}

//--------------------------------------------------------------
void MediaPool::scanMediaFiles(const std::string& path, ofDirectory& dir) {
    // Configure directory to allow media file extensions
    dir.allowExt("wav");
    dir.allowExt("mp3");
    dir.allowExt("aiff");
    dir.allowExt("mov");
    dir.allowExt("mp4");
    dir.allowExt("avi");
    
    dir.listDir();
    
    ofLogNotice("ofxMediaPool") << "Found " << dir.size() << " files in directory";
    
    // Separate audio and video files
    for (int i = 0; i < dir.size(); i++) {
        std::string filename = dir.getName(i);
        std::string fullPath = dir.getPath(i);
        
        if (isAudioFile(filename)) {
            audioFiles.push_back(fullPath);
        } else if (isVideoFile(filename)) {
            videoFiles.push_back(fullPath);
        }
    }
    
    ofLogNotice("ofxMediaPool") << "Found " << audioFiles.size() << " audio files, " << videoFiles.size() << " video files";
}

//--------------------------------------------------------------

//--------------------------------------------------------------
void MediaPool::browseForDirectory() {
    ofLogNotice("ofxMediaPool") << "Opening directory browser...";
    
    // Use OpenFrameworks file dialog to select directory
    ofFileDialogResult result = ofSystemLoadDialog("Select Media Directory", true);
    
    if (result.bSuccess) {
        std::string selectedPath = result.getPath();
        ofLogNotice("ofxMediaPool") << "Selected directory: " << selectedPath;
        setDataDirectory(selectedPath);
    } else {
        ofLogNotice("ofxMediaPool") << "Directory selection cancelled";
    }
}

//--------------------------------------------------------------
// Query methods for state checking
PlaybackMode MediaPool::getCurrentMode() const {
    // Lock-free read (atomic)
    return currentMode.load(std::memory_order_relaxed);
}

bool MediaPool::isSequencerActive() const {
    // Lock-free read (atomic)
    return currentMode.load(std::memory_order_relaxed) == PlaybackMode::SEQUENCER_ACTIVE;
}

bool MediaPool::isManualPreview() const {
    // Lock-free read (atomic)
    return currentMode.load(std::memory_order_relaxed) == PlaybackMode::MANUAL_PREVIEW;
}

bool MediaPool::isIdle() const {
    // Lock-free read (atomic)
    return currentMode.load(std::memory_order_relaxed) == PlaybackMode::IDLE;
}

void MediaPool::setModeIdle() {
    // Thread-safe transition to IDLE mode
    // Used by button handlers to immediately transition when stopping
    std::lock_guard<std::mutex> lock(stateMutex);
    currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
}

//--------------------------------------------------------------
// Play style control (applies to both manual preview and sequencer playback)
void MediaPool::setPlayStyle(PlayStyle style) {
    std::lock_guard<std::mutex> lock(stateMutex);
    currentPlayStyle = style;
    ofLogNotice("ofxMediaPool") << "Play style set to: " << (int)style;
    
    // Apply the new style to the currently active player if it's playing
    if (activePlayer) {
        PlaybackMode mode = currentMode.load(std::memory_order_relaxed);
        if (mode == PlaybackMode::MANUAL_PREVIEW || mode == PlaybackMode::SEQUENCER_ACTIVE) {
            bool shouldLoop = (style == PlayStyle::LOOP);
            activePlayer->loop.set(shouldLoop);
            ofLogNotice("ofxMediaPool") << "Applied play style to active player - loop: " << shouldLoop;
        }
    }
}

PlayStyle MediaPool::getPlayStyle() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return currentPlayStyle;
}

//--------------------------------------------------------------
void MediaPool::update() {
    PlaybackMode mode = currentMode.load(std::memory_order_relaxed);
    float currentTime = ofGetElapsedTimef();
    
    // Check gate timer expiration for sequencer-triggered playback
    if (mode == PlaybackMode::SEQUENCER_ACTIVE && gateTimerActive) {
        if (currentTime >= gateEndTime) {
            // Gate expired - stop playback and transition to IDLE
            if (activePlayer) {
                activePlayer->stop();
            }
            currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
            gateTimerActive = false;
            ofLogNotice("MediaPool") << "[GATE_END] Gate timer expired - transitioning to IDLE mode";
        }
    }
    
    // Process event queue FIRST to update lastTriggeredStep before position capture
    processEventQueue();
    
    // Unified position memory capture and region end detection
    if (activePlayer && currentIndex < players.size()) {
        bool isCurrentlyPlaying = activePlayer->isPlaying();
        
        if (isCurrentlyPlaying) {
            // Get current playhead position (absolute: 0.0-1.0 of entire media)
            float currentPosition = activePlayer->playheadPosition.get();
            float regionStartVal = activePlayer->regionStart.get();
            float regionEndVal = activePlayer->regionEnd.get();
            
            // Ensure region bounds are valid
            if (regionStartVal > regionEndVal) {
                std::swap(regionStartVal, regionEndVal);
            }
            
            // Check if playhead has reached or exceeded region end
            const float REGION_END_THRESHOLD = 0.001f;
            bool reachedRegionEnd = (currentPosition >= regionEndVal - REGION_END_THRESHOLD);
            
            // Handle region end based on play style (only if not looping)
            if (reachedRegionEnd && !activePlayer->loop.get()) {
                switch (currentPlayStyle) {
                    case PlayStyle::ONCE:
                        activePlayer->stop();
                        break;
                    case PlayStyle::LOOP:
                        if (activePlayer->isAudioLoaded()) {
                            activePlayer->getAudioPlayer().setPosition(regionStartVal);
                        }
                        if (activePlayer->isVideoLoaded()) {
                            activePlayer->getVideoPlayer().getVideoFile().setPosition(regionStartVal);
                            activePlayer->getVideoPlayer().getVideoFile().update();
                        }
                        activePlayer->playheadPosition.set(regionStartVal);
                        break;
                    case PlayStyle::NEXT:
                        activePlayer->stop();
                        break;
                }
            } else if (reachedRegionEnd && activePlayer->loop.get()) {
                if (currentPosition > regionEndVal + REGION_END_THRESHOLD) {
                    if (activePlayer->isAudioLoaded()) {
                        activePlayer->getAudioPlayer().setPosition(regionStartVal);
                    }
                    if (activePlayer->isVideoLoaded()) {
                        activePlayer->getVideoPlayer().getVideoFile().setPosition(regionStartVal);
                        activePlayer->getVideoPlayer().getVideoFile().update();
                    }
                    activePlayer->playheadPosition.set(regionStartVal);
                }
            }
            
            // Capture position while playing - use lastTriggeredStep (now updated by processEventQueue)
            // For per-index mode, step is ignored in makeKey(), so this is fine
            // For per-step mode, we need the correct step which is now in lastTriggeredStep
            // CRITICAL: Only capture position if transport is playing (not stopped)
            // This prevents position from being saved after transport stops, which would
            // repopulate position memory after it was cleared on transport stop
            bool transportIsPlaying = false;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                transportIsPlaying = lastTransportState;
            }
            if (transportIsPlaying && (!reachedRegionEnd || currentPlayStyle != PlayStyle::LOOP)) {
                int step = (mode == PlaybackMode::SEQUENCER_ACTIVE) ? lastTriggeredStep : -1;
                if (step >= 0 || mode != PlaybackMode::SEQUENCER_ACTIVE) {
                    positionMemory.capture(step, currentIndex, currentPosition);
                }
            }
        } else {
            // Player stopped - capture final position
            // CRITICAL: Only capture position if transport is still playing
            // When transport stops, position memory is cleared, and we don't want to
            // repopulate it with the preserved position from stop()
            bool transportIsPlaying = false;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                transportIsPlaying = lastTransportState;
            }
            if (transportIsPlaying) {
                float stoppedPosition = activePlayer->playheadPosition.get();
                int step = (mode == PlaybackMode::SEQUENCER_ACTIVE) ? lastTriggeredStep : -1;
                if (step >= 0 || mode != PlaybackMode::SEQUENCER_ACTIVE) {
                    positionMemory.capture(step, currentIndex, stoppedPosition);
                }
            }
            
            // Transition to IDLE when playback stops
            mode = currentMode.load(std::memory_order_relaxed);
            if (mode == PlaybackMode::SEQUENCER_ACTIVE) {
                currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
                gateTimerActive = false;
                ofLogNotice("MediaPool") << "[GATE_END] Player stopped - transitioning to IDLE mode";
            } else if (mode == PlaybackMode::MANUAL_PREVIEW) {
                currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
                ofLogNotice("MediaPool") << "[MANUAL_STOP] Manual preview stopped - transitioning to IDLE mode";
            }
        }
    }
    
    // Check for end-of-playback (applies to both manual preview and sequencer)
    mode = currentMode.load(std::memory_order_relaxed);
    if ((mode == PlaybackMode::MANUAL_PREVIEW || mode == PlaybackMode::SEQUENCER_ACTIVE) && activePlayer) {
        if (!activePlayer->isPlaying() && !activePlayer->loop.get()) {
            onPlaybackEnd();
        }
    }
}

//--------------------------------------------------------------
void MediaPool::processEventQueue() {
    std::queue<TriggerEvent> localQueue;
    
    {
        std::lock_guard<std::mutex> queueLock(stateMutex);
        localQueue.swap(eventQueue);
    }
    
    // Get parameter descriptors for defaults and validation
    auto paramDescriptors = getParameters();
    std::map<std::string, float> defaults;
    for (const auto& param : paramDescriptors) {
        defaults[param.name] = param.defaultValue;
    }
    
    // Process events without lock (we're in GUI thread now)
    bool isFirstEvent = true;
    while (!localQueue.empty()) {
        const TriggerEvent& event = localQueue.front();
        
        // Clear transportJustStarted flag after processing first event
        if (isFirstEvent) {
            std::lock_guard<std::mutex> flagLock(stateMutex);
            if (transportJustStarted) {
                transportJustStarted = false;
                ofLogVerbose("MediaPool") << "[TRANSPORT_START] First event processed, position restoration enabled";
            }
            isFirstEvent = false;
        }
        
        // Extract mediaIndex from "note" parameter
        int mediaIndex = -1;
        auto noteIt = event.parameters.find("note");
        if (noteIt != event.parameters.end()) {
            mediaIndex = (int)noteIt->second;
        }
        
        // Handle empty cells (rests) - stop immediately
        if (mediaIndex < 0) {
            if (activePlayer) {
                // CRITICAL: Capture position BEFORE stopping, using current step context
                // Don't capture here - let update() handle it with correct step tracking
                activePlayer->stop();
            }
            gateTimerActive = false;
            localQueue.pop();
            continue;
        }
        
        // Validate media index
        if (mediaIndex >= (int)players.size()) {
            localQueue.pop();
            continue;
        }
        
        // Get the media player for this step
        MediaPlayer* player = players[mediaIndex].get();
        if (!player) {
            localQueue.pop();
            continue;
        }
        
        // CRITICAL: Update lastTriggeredStep BEFORE any position captures
        // This ensures update() uses the correct step when capturing positions
        if (event.step >= 0) {
            lastTriggeredStep = event.step;
        }
        
        // Set active player if changed
        bool playerChanged = (currentIndex != mediaIndex || activePlayer != player);
        if (playerChanged) {
            // CRITICAL: Don't capture position here - let update() handle it
            // Position capture should happen in update() with correct step tracking
            setActivePlayer(mediaIndex);
        }
        
        // Extract parameters from TriggerEvent map with defaults and proper clamping
        auto getParamValue = [&](const std::string& paramName, float defaultValue) -> float {
            auto descIt = std::find_if(paramDescriptors.begin(), paramDescriptors.end(),
                [&](const ParameterDescriptor& desc) { return desc.name == paramName; });
            
            float minVal = 0.0f;
            float maxVal = 1.0f;
            if (descIt != paramDescriptors.end()) {
                minVal = descIt->minValue;
                maxVal = descIt->maxValue;
            }
            
            auto eventIt = event.parameters.find(paramName);
            if (eventIt != event.parameters.end()) {
                return std::max(minVal, std::min(maxVal, eventIt->second));
            }
            return defaultValue;
        };
        
        float position = getParamValue("position", defaults.count("position") > 0 ? defaults["position"] : 0.0f);
        float speed = getParamValue("speed", defaults.count("speed") > 0 ? defaults["speed"] : 1.0f);
        float volume = getParamValue("volume", defaults.count("volume") > 0 ? defaults["volume"] : 1.0f);
        
        // Clamp position to region bounds
        float regionStartVal = player->regionStart.get();
        float regionEndVal = player->regionEnd.get();
        float regionSize = regionEndVal - regionStartVal;
        
        float clampedPosition = position;
        if (regionSize > 0.001f) {
            clampedPosition = std::max(0.0f, std::min(1.0f, position));
        } else {
            clampedPosition = std::max(0.0f, std::min(1.0f, position));
        }
        
        // Audio/video always enabled for sequencer triggers
        if (!player->audioEnabled.get()) {
            player->audioEnabled.set(true);
        }
        if (!player->videoEnabled.get()) {
            player->videoEnabled.set(true);
        }
        
        // Set volume
        if (std::abs(player->volume.get() - volume) > PARAMETER_EPSILON) {
            player->volume.set(volume);
        }
        
        // Set playback parameters
        if (std::abs(player->startPosition.get() - clampedPosition) > PARAMETER_EPSILON) {
            player->startPosition.set(clampedPosition);
        }
        if (std::abs(player->speed.get() - speed) > PARAMETER_EPSILON) {
            player->speed.set(speed);
        }
        
        // Set loop based on play style
        bool shouldLoop = (currentPlayStyle == PlayStyle::LOOP);
        if (std::abs(player->loop.get() ? 1.0f : 0.0f - (shouldLoop ? 1.0f : 0.0f)) > PARAMETER_EPSILON) {
            player->loop.set(shouldLoop);
        }
        
        // Use duration directly from TriggerEvent
        float stepDurationSeconds = event.duration;
        
        // Trigger media playback with gating
        player->playWithGate(stepDurationSeconds);
        
        // Track gate timer for reliable gate-end detection
        gateTimerActive = true;
        gateEndTime = ofGetElapsedTimef() + stepDurationSeconds;
        
        // Transition to SEQUENCER_ACTIVE if playback started
        if (player->isPlaying()) {
            currentMode.store(PlaybackMode::SEQUENCER_ACTIVE, std::memory_order_relaxed);
        } else {
            gateTimerActive = false;
            ofLogWarning("MediaPool") << "playWithGate() called but player is not playing - staying in IDLE mode";
        }
        
        localQueue.pop();
    }
}

//--------------------------------------------------------------
// Handle end-of-playback (applies to both manual preview and sequencer)
void MediaPool::onPlaybackEnd() {
    // No lock needed - caller (update()) already holds stateMutex
    
    PlaybackMode mode = currentMode.load(std::memory_order_relaxed);
    if (mode != PlaybackMode::MANUAL_PREVIEW && mode != PlaybackMode::SEQUENCER_ACTIVE) return;
    
    switch (currentPlayStyle) {
        case PlayStyle::ONCE:
            // Stop the current player
            if (activePlayer) {
                activePlayer->stop();
            }
            currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
            break;
        case PlayStyle::LOOP:
            // Already handled by loop=true
            break;
        case PlayStyle::NEXT:
            if (players.size() > 1) {
                size_t nextIndex = (currentIndex + 1) % players.size();
                ofLogNotice("ofxMediaPool") << "Playing next media: " << nextIndex;
                
                // Get next player and validate it's available
                MediaPlayer* nextPlayer = players[nextIndex].get();
                if (nextPlayer && (nextPlayer->isAudioLoaded() || nextPlayer->isVideoLoaded())) {
                    // Set as active player and connect to outputs
                    setActivePlayer(nextIndex);
                    
                    // Reset and prepare for playback
                    nextPlayer->stop();
                    nextPlayer->playheadPosition.set(0.0f);
                    
                    // Re-enable audio/video if they were loaded
                    if (nextPlayer->isAudioLoaded()) {
                        nextPlayer->audioEnabled.set(true);
                    }
                    if (nextPlayer->isVideoLoaded()) {
                        nextPlayer->videoEnabled.set(true);
                    }
                    
                    // Set loop to false for PLAY_NEXT mode
                    nextPlayer->loop.set(false);
                    
                    // Start playback
                    nextPlayer->play();
                    
                    ofLogNotice("ofxMediaPool") << "Started next media " << nextIndex << " (state: " << (mode == PlaybackMode::MANUAL_PREVIEW ? "MANUAL_PREVIEW" : "SEQUENCER_ACTIVE") << ")";
                } else {
                    currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
                }
            } else {
                if (activePlayer) {
                    activePlayer->stop();
                }
                currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
            }
            break;
    }
}


//--------------------------------------------------------------
// Transport listener system for Clock play/stop events
void MediaPool::addTransportListener(TransportCallback listener) {
    std::lock_guard<std::mutex> lock(stateMutex);
    transportListener = listener;
    ofLogNotice("MediaPool") << "Transport listener added";
}

void MediaPool::removeTransportListener() {
    std::lock_guard<std::mutex> lock(stateMutex);
    transportListener = nullptr;
    ofLogNotice("MediaPool") << "Transport listener removed";
}

void MediaPool::onTransportChanged(bool isPlaying) {
    std::lock_guard<std::mutex> lock(stateMutex);
    
    if (isPlaying != lastTransportState) {
        lastTransportState = isPlaying;
        
        if (isPlaying) {
            // Transport started - flag should already be set from previous stop
            // Just log that we're starting a fresh session
            ofLogNotice("MediaPool") << "[TRANSPORT_START] Starting fresh playback session (mode: " 
                                     << (int)positionMemory.getMode() << ", flag: " << transportJustStarted << ")";
        } else {
            // Transport stopped - prepare for next start by clearing memory and setting flag
            // This ensures memory is cleared BEFORE the next transport start triggers events
            // CRITICAL: Clear memory on STOP, not START, to avoid race condition
            positionMemory.clear();
            transportJustStarted = true;  // Set flag now, so it's ready for next start
            
            if (currentMode.load(std::memory_order_relaxed) == PlaybackMode::SEQUENCER_ACTIVE) {
                gateTimerActive = false;
                currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
                
                if (activePlayer && activePlayer->isPlaying()) {
                    activePlayer->stop();
                }
            }
            
            ofLogNotice("MediaPool") << "[TRANSPORT_STOP] Position memory cleared for next playback session (mode: " 
                                     << (int)positionMemory.getMode() << ")";
        }
        
        if (transportListener) {
            transportListener(isPlaying);
        }
    }
}

//--------------------------------------------------------------
// Module interface implementation
//--------------------------------------------------------------
std::string MediaPool::getName() const {
    return "MediaPool";
}

ModuleType MediaPool::getType() const {
    return ModuleType::INSTRUMENT;
}

std::vector<ParameterDescriptor> MediaPool::getParameters() {
    std::vector<ParameterDescriptor> params;
    
    // MediaPool parameters that can be controlled by TrackerSequencer
    // These are the parameters that TrackerSequencer sends in trigger events
    // MediaPool maps these to MediaPlayer parameters
    params.push_back(ParameterDescriptor("note", ParameterType::INT, 0.0f, 127.0f, 0.0f, "Note/Media Index"));
    params.push_back(ParameterDescriptor("position", ParameterType::FLOAT, 0.0f, 1.0f, 0.0f, "Position"));
    params.push_back(ParameterDescriptor("speed", ParameterType::FLOAT, -10.0f, 10.0f, 1.0f, "Speed"));
    params.push_back(ParameterDescriptor("volume", ParameterType::FLOAT, 0.0f, 2.0f, 1.0f, "Volume"));
    params.push_back(ParameterDescriptor("pitch", ParameterType::FLOAT, 0.5f, 2.0f, 1.0f, "Pitch"));
    params.push_back(ParameterDescriptor("regionStart", ParameterType::FLOAT, 0.0f, 1.0f, 0.0f, "Region Start"));
    params.push_back(ParameterDescriptor("regionEnd", ParameterType::FLOAT, 0.0f, 1.0f, 1.0f, "Region End"));
    
    return params;
}

void MediaPool::setParameter(const std::string& paramName, float value, bool notify) {
    // Continuous parameter modulation (for modulators, envelopes, etc.)
    // For MediaPool, we apply this to the active player
    // MODULAR: Use parameter mapping system instead of hardcoded checks
    if (!activePlayer) return;
    
    // Get parameter descriptor to validate range
    auto paramDescriptors = getParameters();
    const ParameterDescriptor* paramDesc = nullptr;
    for (const auto& param : paramDescriptors) {
        if (param.name == paramName) {
            paramDesc = &param;
            break;
        }
    }
    
    // Clamp value to parameter range if descriptor found
    float clampedValue = value;
    if (paramDesc) {
        clampedValue = std::max(paramDesc->minValue, std::min(paramDesc->maxValue, value));
    }
    
    float oldValue = 0.0f;
    bool valueChanged = false;
    
    // MODULAR: Map parameter name to MediaPlayer parameter using a mapping function
    // This is cleaner than hardcoded if/else and easier to extend
    auto applyParameterToPlayer = [&](const std::string& name, float val) -> bool {
        if (name == "volume") {
            oldValue = activePlayer->volume.get();
            activePlayer->volume.set(val);
            return std::abs(oldValue - val) > PARAMETER_EPSILON;
        } else if (name == "speed") {
            oldValue = activePlayer->speed.get();
            activePlayer->speed.set(val);
            return std::abs(oldValue - val) > PARAMETER_EPSILON;
        } else if (name == "pitch") {
            oldValue = activePlayer->pitch.get();
            activePlayer->pitch.set(val);
            return std::abs(oldValue - val) > PARAMETER_EPSILON;
        } else if (name == "regionStart" || name == "loopStart") {
            // Support both new name (regionStart) and old name (loopStart) for backward compatibility
            oldValue = activePlayer->regionStart.get();
            activePlayer->regionStart.set(val);
            return std::abs(oldValue - val) > PARAMETER_EPSILON;
        } else if (name == "regionEnd" || name == "loopEnd") {
            // Support both new name (regionEnd) and old name (loopEnd) for backward compatibility
            oldValue = activePlayer->regionEnd.get();
            activePlayer->regionEnd.set(val);
            return std::abs(oldValue - val) > PARAMETER_EPSILON;
        } else if (name == "position") {
            // Position from TrackerSequencer is relative (0.0-1.0 within region)
            // Store it directly as relative startPosition
            oldValue = activePlayer->startPosition.get();
            if (std::abs(oldValue - val) > PARAMETER_EPSILON) {
                // Clamp to valid relative range
                float relativePos = std::max(0.0f, std::min(1.0f, val));
                activePlayer->startPosition.set(relativePos);
                
                // Update playheadPosition for UI display (map to absolute)
                float regionStartVal = activePlayer->regionStart.get();
                float regionEndVal = activePlayer->regionEnd.get();
                float regionSize = regionEndVal - regionStartVal;
                float absolutePos = 0.0f;
                
                if (regionSize > 0.001f) {
                    absolutePos = regionStartVal + relativePos * regionSize;
                } else {
                    absolutePos = std::max(0.0f, std::min(1.0f, relativePos));
                }
                
                float currentPos = activePlayer->playheadPosition.get();
                if (std::abs(currentPos - absolutePos) > POSITION_EPSILON) {
                    activePlayer->playheadPosition.set(absolutePos);
                }
                return true;
            }
            return false;
        }
        // Unknown parameter - return false (not applied)
        return false;
    };
    
    valueChanged = applyParameterToPlayer(paramName, clampedValue);
    
    // Notify parameter change callback if set and value changed
    if (notify && valueChanged && parameterChangeCallback) {
        parameterChangeCallback(paramName, clampedValue);
    }
    
    // Note: "note" parameter can't be set continuously - it's only for triggers
}

void MediaPool::subscribeToTrackerSequencer(TrackerSequencer* sequencer) {
    if (!sequencer) {
        ofLogError("MediaPool") << "Cannot subscribe to null TrackerSequencer";
        return;
    }
    
    // Subscribe to trigger events - this is the modular connection!
    ofAddListener(sequencer->triggerEvent, this, &MediaPool::onTrigger);
}

void MediaPool::onTrigger(TriggerEvent& event) {
    // LOCK-FREE: Push event to queue from audio thread (minimal logging only)
    // This is called from audio thread via ofNotifyEvent, so we must not block
    
    // Apply position memory: If retriggering same media without specified position,
    // use stored position (continue from where it left off, even after gate ends)
    auto noteIt = event.parameters.find("note");
    if (noteIt != event.parameters.end() && noteIt->second >= 0) {
        int mediaIndex = (int)noteIt->second;
        
        // Check if position parameter is not explicitly set
        if (event.parameters.find("position") == event.parameters.end()) {
            // CRITICAL FIX: Check transportJustStarted flag FIRST
            // This prevents position restoration for the first event after transport start,
            // even if memory was repopulated after clearing (race condition in per-index mode)
            // In per-index mode, position capture after clearing can repopulate memory before
            // the first trigger, causing stale position to be restored. The flag ensures
            // the first event always starts fresh, matching per-step mode behavior.
            bool shouldRestore = true;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                // Don't restore if transport just started (fresh session) OR if memory is empty
                if (transportJustStarted || positionMemory.size() == 0) {
                    shouldRestore = false;
                    if (transportJustStarted) {
                        ofLogVerbose("MediaPool") << "[TRANSPORT_START] Skipping position restore - fresh session (flag set)";
                    } else {
                        ofLogVerbose("MediaPool") << "[TRANSPORT_START] Memory is empty, skipping position restore (fresh session)";
                    }
                }
            }
            
            if (shouldRestore) {
                // Restore position from memory (mode-aware: per-step, per-index, or global)
                float storedPosition = positionMemory.restore(event.step, mediaIndex, 0.0f);
                if (storedPosition > POSITION_THRESHOLD) {
                    event.parameters["position"] = storedPosition;
                    ofLogNotice("MediaPool") << "[POSITION_MEMORY] Using stored position for step " 
                                             << event.step << ", media " << mediaIndex << ": " << storedPosition;
                }
            }
        }
    }
    
    // Lock-free queue push
    {
        std::lock_guard<std::mutex> queueLock(stateMutex);
        eventQueue.push(event);
    }
}

//--------------------------------------------------------------
// Position memory mode control
void MediaPool::setPositionMemoryMode(PositionMemoryMode mode) {
    std::lock_guard<std::mutex> lock(stateMutex);
    positionMemory.setMode(mode);
    ofLogNotice("MediaPool") << "Position memory mode set to: " << (int)mode;
}

PositionMemoryMode MediaPool::getPositionMemoryMode() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return positionMemory.getMode();
}


