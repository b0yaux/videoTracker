
#include "MediaPool.h"
#include "MediaPlayer.h"
#include "TrackerSequencer.h"
#include "ofFileUtils.h"
#include "ofSystemUtils.h"
#include "ofUtils.h"
#include "ofJson.h"
#include <unordered_map>
#include <unordered_set>

MediaPool::MediaPool(const std::string& dataDir) 
    : currentIndex(0), dataDirectory(dataDir), isSetup(false), currentMode(PlaybackMode::IDLE), 
      currentPlayStyle(PlayStyle::ONCE), clock(nullptr), activePlayer(nullptr), 
      lastTransportState(false), playerConnected(false), gateTimerActive(false), gateEndTime(0.0f),
      positionScan(ScanMode::PER_MEDIA), lastTriggeredStep(-1), activeStepContext() {
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
    // CRITICAL: Reset activePlayer BEFORE clearing players to avoid dangling pointer
    if (activePlayer) {
        disconnectActivePlayer();
        activePlayer = nullptr;
        playerConnected = false;
    }
    
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
            bool loaded = player->load(audioFile, it->second);
            // Only add player if at least one media file loaded successfully
            if (loaded) {
                players.push_back(std::move(player));
                pairedVideos.insert(audioBase);
            } else {
                ofLogWarning("ofxMediaPool") << "Failed to load paired media: " << audioFile << " + " << it->second;
            }
        } else {
            // Create audio-only player
            auto player = std::make_unique<MediaPlayer>();
            bool loaded = player->loadAudio(audioFile);
            // Only add player if audio loaded successfully
            if (loaded) {
                players.push_back(std::move(player));
            } else {
                ofLogWarning("ofxMediaPool") << "Failed to load audio: " << audioFile;
            }
        }
    }
    
    // Create video-only players for unmatched video files
    for (const auto& videoFile : videoFiles) {
        std::string videoBase = getBaseName(videoFile);
        if (pairedVideos.find(videoBase) == pairedVideos.end()) {
            // Create video-only player
            auto player = std::make_unique<MediaPlayer>();
            bool loaded = player->loadVideo(videoFile);
            // Only add player if video loaded successfully
            if (loaded) {
                players.push_back(std::move(player));
            } else {
                ofLogWarning("ofxMediaPool") << "Failed to load video: " << videoFile;
            }
        }
    }
    
    ofLogNotice("ofxMediaPool") << "Created " << players.size() << " media players";
}


void MediaPool::pairByIndex() {
    // pairByIndex() calls clear() which already handles activePlayer reset
    clear();
    
    ofLogNotice("ofxMediaPool") << "Pairing files by index";
    
    size_t maxPairs = std::max(audioFiles.size(), videoFiles.size());
    
    for (size_t i = 0; i < maxPairs; i++) {
        auto player = std::make_unique<MediaPlayer>();
        
        std::string audioFile = (i < audioFiles.size()) ? audioFiles[i] : "";
        std::string videoFile = (i < videoFiles.size()) ? videoFiles[i] : "";
        
        bool loaded = player->load(audioFile, videoFile);
        // Only add player if at least one media file loaded successfully
        if (loaded) {
            players.push_back(std::move(player));
            ofLogNotice("ofxMediaPool") << "Index pair " << i << ": " 
                                           << ofFilePath::getFileName(audioFile) 
                                           << " + " << ofFilePath::getFileName(videoFile);
        } else {
            ofLogWarning("ofxMediaPool") << "Failed to load index pair " << i << ": " 
                                            << ofFilePath::getFileName(audioFile) 
                                            << " + " << ofFilePath::getFileName(videoFile);
        }
    }
    
    ofLogNotice("ofxMediaPool") << "Created " << players.size() << " media players by index";
}

MediaPlayer* MediaPool::getMediaPlayer(size_t index) {
    // CRITICAL: Lock mutex to prevent accessing players while they're being modified
    std::lock_guard<std::mutex> lock(stateMutex);
    
    if (index >= players.size()) return nullptr;
    MediaPlayer* player = players[index].get();
    
    // Validate player has media loaded before returning
    if (player && (player->isAudioLoaded() || player->isVideoLoaded())) {
        return player;
    }
    
    return nullptr;
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
    if (currentIndex >= players.size()) return nullptr;
    MediaPlayer* player = players[currentIndex].get();
    // Validate player has media loaded
    if (player && (player->isAudioLoaded() || player->isVideoLoaded())) {
        return player;
    }
    return nullptr;
}

MediaPlayer* MediaPool::getNextPlayer() {
    if (players.empty()) return nullptr;
    
    // Find next valid player (skip invalid ones)
    size_t startIndex = currentIndex;
    size_t attempts = 0;
    do {
        currentIndex = (currentIndex + 1) % players.size();
        if (currentIndex >= players.size()) return nullptr;
        MediaPlayer* player = players[currentIndex].get();
        // Check if player is valid and has media loaded
        if (player && (player->isAudioLoaded() || player->isVideoLoaded())) {
            return player;
        }
        attempts++;
    } while (currentIndex != startIndex && attempts < players.size());
    
    return nullptr; // No valid players found
}

MediaPlayer* MediaPool::getPreviousPlayer() {
    if (players.empty()) return nullptr;
    
    // Find previous valid player (skip invalid ones)
    size_t startIndex = currentIndex;
    size_t attempts = 0;
    do {
        currentIndex = (currentIndex == 0) ? players.size() - 1 : currentIndex - 1;
        if (currentIndex >= players.size()) return nullptr;
        MediaPlayer* player = players[currentIndex].get();
        // Check if player is valid and has media loaded
        if (player && (player->isAudioLoaded() || player->isVideoLoaded())) {
            return player;
        }
        attempts++;
    } while (currentIndex != startIndex && attempts < players.size());
    
    return nullptr; // No valid players found
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
    // CRITICAL: Lock mutex to prevent reading players.size() while players are being modified
    std::lock_guard<std::mutex> lock(stateMutex);
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
    // CRITICAL: Reset activePlayer BEFORE clearing players to avoid dangling pointer
    // When players vector is cleared, all unique_ptr objects are destroyed
    // If activePlayer still points to one of them, it becomes a dangling pointer
    // This causes crashes when update() tries to access activePlayer->playheadPosition.get()
    if (activePlayer) {
        disconnectActivePlayer();
        activePlayer = nullptr;
    }
    
    players.clear();
    audioFiles.clear();
    videoFiles.clear();
    currentIndex = 0;
    positionScan.clear();  // Clear scan positions when clearing media pool
    playerConnected = false;  // Reset connection flag since players are cleared
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
    std::string ext = ofToLower(ofFilePath::getFileExt(filename));
    return (ext == "wav" || ext == "mp3" || ext == "aiff" || ext == "aif" || ext == "m4a");
}

bool MediaPool::isVideoFile(const std::string& filename) {
    std::string ext = ofToLower(ofFilePath::getFileExt(filename));
    return (ext == "mov" || ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "webm" || ext == "hap");
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
    
    // Validate that player has at least one media file loaded
    if (!newPlayer || (!newPlayer->isAudioLoaded() && !newPlayer->isVideoLoaded())) {
        ofLogWarning("ofxMediaPool") << "Cannot set active player at index " << index << " - no media loaded";
        return;
    }
    
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
    // CRITICAL: Lock mutex to prevent accessing activePlayer while it's being modified
    std::lock_guard<std::mutex> lock(stateMutex);
    
    // CRITICAL: Validate that activePlayer is still valid before returning it
    // activePlayer could become a dangling pointer if players vector was cleared/recreated
    if (!activePlayer) {
        return nullptr;
    }
    
    // Verify activePlayer is actually in the players vector
    for (const auto& player : players) {
        if (player.get() == activePlayer) {
            // Found it - also verify it has media loaded
            if (activePlayer->isAudioLoaded() || activePlayer->isVideoLoaded()) {
                return activePlayer;
            } else {
                // Player exists but has no media - reset it
                activePlayer = nullptr;
                playerConnected = false;
                return nullptr;
            }
        }
    }
    
    // activePlayer is not in the players vector - it's a dangling pointer!
    // Reset it to prevent crashes
    ofLogWarning("ofxMediaPool") << "getActivePlayer(): activePlayer is a dangling pointer - resetting";
    activePlayer = nullptr;
    playerConnected = false;
    return nullptr;
}


void MediaPool::connectActivePlayer(ofxSoundOutput& soundOut, ofxVisualOutput& visualOut) {
    if (!activePlayer) {
        return;  // No active player, silent return
    }
    
    // CRITICAL: Validate that activePlayer is still valid and has media loaded
    // activePlayer could become invalid if clear() was called or players were recreated
    bool playerIsValid = false;
    for (const auto& player : players) {
        if (player.get() == activePlayer) {
            // Found the player in the vector - validate it has media
            if (activePlayer->isAudioLoaded() || activePlayer->isVideoLoaded()) {
                playerIsValid = true;
            }
            break;
        }
    }
    
    if (!playerIsValid) {
        // activePlayer is no longer valid - reset it
        activePlayer = nullptr;
        playerConnected = false;
        return;
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
        // Find first player with valid media loaded
        for (size_t i = 0; i < players.size(); i++) {
            MediaPlayer* player = players[i].get();
            if (player && (player->isAudioLoaded() || player->isVideoLoaded())) {
                setActivePlayer(i);
                ofLogNotice("ofxMediaPool") << "Initialized first player as active (index " << i << ")";
                return;
            }
        }
        ofLogWarning("ofxMediaPool") << "No valid media players found to initialize";
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
    
    // Clear lock-free event queue (drain all pending events)
    TriggerEvent dummy;
    while (eventQueue.try_dequeue(dummy)) {
        // Drain queue - events are discarded
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
    
    // CRITICAL: Lock mutex to prevent GUI/update loop from accessing players during directory change
    // This prevents race conditions where GUI tries to access players while they're being cleared/recreated
    std::lock_guard<std::mutex> lock(stateMutex);
    
    try {
        ofDirectory dir(path);
        if (!dir.exists()) {
            ofLogError("ofxMediaPool") << "Directory does not exist: " << path;
            return;
        }
        
        ofLogNotice("ofxMediaPool") << "✅ Using data directory: " << path;
        
        // CRITICAL: Reset activePlayer BEFORE scanning to prevent dangling pointer access
        // This ensures no code tries to access activePlayer while we're clearing/recreating players
        if (activePlayer) {
            disconnectActivePlayer();
            activePlayer = nullptr;
            playerConnected = false;
        }
        
        // Use the existing scanDirectory method to populate audioFiles and videoFiles
        scanDirectory(path);
        
        // Create media players from the scanned files
        mediaPair();
        
        // Only initialize active player if we have valid players
        if (!players.empty()) {
            initializeFirstActivePlayer();
        } else {
            ofLogWarning("ofxMediaPool") << "No valid media players created from directory: " << path;
        }
    } catch (const std::exception& e) {
        ofLogError("ofxMediaPool") << "Exception in setDataDirectory: " << e.what();
        // Ensure activePlayer is reset on error
        if (activePlayer) {
            disconnectActivePlayer();
            activePlayer = nullptr;
            playerConnected = false;
        }
    } catch (...) {
        ofLogError("ofxMediaPool") << "Unknown exception in setDataDirectory";
        // Ensure activePlayer is reset on error
        if (activePlayer) {
            disconnectActivePlayer();
            activePlayer = nullptr;
            playerConnected = false;
        }
    }
    
    // Notify ofApp about directory change (call outside mutex to avoid deadlock)
    // Note: This callback might access MediaPool, so we need to be careful
    // But since we're done modifying players and mutex is released, it should be safe
    if (onDirectoryChanged) {
        onDirectoryChanged(path);
    }
}

//--------------------------------------------------------------
void MediaPool::scanMediaFiles(const std::string& path, ofDirectory& dir) {
    // Configure directory to allow media file extensions (case-insensitive via allowExt)
    dir.allowExt("wav");
    dir.allowExt("mp3");
    dir.allowExt("aiff");
    dir.allowExt("aif");
    dir.allowExt("m4a");
    dir.allowExt("mov");
    dir.allowExt("mp4");
    dir.allowExt("avi");
    dir.allowExt("mkv");
    dir.allowExt("webm");
    dir.allowExt("hap");
    
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
                // DEBUG: Log position before stop
                float posBeforeStop = activePlayer->playheadPosition.get();
                ofLogNotice("MediaPool") << "[GATE_END] Gate timer expired - stopping player (position before stop: " << posBeforeStop << ")";
                activePlayer->stop();
                // DEBUG: Log position after stop
                float posAfterStop = activePlayer->playheadPosition.get();
                ofLogNotice("MediaPool") << "[GATE_END] Position after stop: " << posAfterStop;
                if (std::abs(posAfterStop - posBeforeStop) > 0.001f && posBeforeStop > 0.001f) {
                    ofLogWarning("MediaPool") << "[GATE_END] WARNING: Position changed during stop! "
                                               << "Before: " << posBeforeStop << ", After: " << posAfterStop;
                }
            }
            currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
            gateTimerActive = false;
            ofLogNotice("MediaPool") << "[GATE_END] Gate timer expired - transitioning to IDLE mode";
        }
    }
    
    // Process event queue FIRST to update activeStepContext before position capture
    processEventQueue();
    
    // Simplified position memory capture - always track position every frame while playing
    // CRITICAL: Validate activePlayer is still in players vector before accessing it
    bool activePlayerIsValid = false;
    if (activePlayer && currentIndex < players.size()) {
        // Verify activePlayer is actually in the players vector
        for (const auto& player : players) {
            if (player.get() == activePlayer) {
                // Also verify it has media loaded
                if (activePlayer->isAudioLoaded() || activePlayer->isVideoLoaded()) {
                    activePlayerIsValid = true;
                }
                break;
            }
        }
    }
    
    if (activePlayerIsValid) {
        // Get transport state and step context
        bool transportIsPlaying = false;
        int captureStep = -1;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            transportIsPlaying = lastTransportState;
            if (mode == PlaybackMode::SEQUENCER_ACTIVE) {
                captureStep = activeStepContext.step;
            }
        }
        
        bool isCurrentlyPlaying = activePlayer->isPlaying();
        
        // Check region boundaries for both sequencer playback (when transport is playing) and manual preview
        bool shouldCheckRegionBoundaries = isCurrentlyPlaying && 
            (transportIsPlaying || mode == PlaybackMode::MANUAL_PREVIEW);
        
        if (shouldCheckRegionBoundaries) {
            // Get current playhead position (absolute: 0.0-1.0 of entire media)
            float currentPosition = activePlayer->playheadPosition.get();
            float regionStartVal = activePlayer->regionStart.get();
            float regionEndVal = activePlayer->regionEnd.get();
            
            // Ensure region bounds are valid
            if (regionStartVal > regionEndVal) {
                std::swap(regionStartVal, regionEndVal);
            }
            
            // Calculate effective loop end based on loopSize when in LOOP play style
            float effectiveRegionEnd = regionEndVal;
            if (currentPlayStyle == PlayStyle::LOOP) {
                float loopSizeSeconds = activePlayer->loopSize.get();
                if (loopSizeSeconds > 0.001f) {
                    float duration = activePlayer->getDuration();
                    if (duration > 0.001f) {
                        // Convert loopSize (seconds) to normalized position (0-1)
                        float loopSizeNormalized = loopSizeSeconds / duration;
                        
                        // Calculate loop start position (absolute) from startPosition (relative within region)
                        float relativeStartPos = activePlayer->startPosition.get();
                        float regionSize = regionEndVal - regionStartVal;
                        float loopStartAbsolute = 0.0f;
                        
                        if (regionSize > 0.001f) {
                            loopStartAbsolute = regionStartVal + relativeStartPos * regionSize;
                        } else {
                            loopStartAbsolute = std::max(0.0f, std::min(1.0f, relativeStartPos));
                        }
                        
                        // Loop end is loop start + loopSize
                        // CRITICAL: Must clamp to BOTH region end AND media duration (1.0)
                        // The loop should NEVER exceed the region boundaries (region acts as outer loop)
                        float calculatedLoopEnd = loopStartAbsolute + loopSizeNormalized;
                        effectiveRegionEnd = std::min(regionEndVal, std::min(1.0f, calculatedLoopEnd));
                    }
                }
            }
            
            // Check if playhead has gone below region start (shouldn't happen, but clamp if it does)
            const float REGION_BOUNDARY_THRESHOLD = 0.001f;
            if (currentPosition < regionStartVal - REGION_BOUNDARY_THRESHOLD) {
                // Playhead went below region start - clamp to region start
                if (activePlayer->isAudioLoaded()) {
                    activePlayer->getAudioPlayer().setPosition(regionStartVal);
                }
                if (activePlayer->isVideoLoaded()) {
                    activePlayer->getVideoPlayer().getVideoFile().setPosition(regionStartVal);
                    activePlayer->getVideoPlayer().getVideoFile().update();
                }
                activePlayer->playheadPosition.set(regionStartVal);
            }
            
            // PHASE 2 SIMPLIFICATION: Only capture for PER_STEP and GLOBAL modes
            // PER_MEDIA mode uses MediaPlayer::playheadPosition directly (no capture needed)
            // MediaPlayer::stop() already preserves position, so we only need to capture
            // for modes that require step-based or global tracking
            ScanMode scanMode = positionScan.getMode();
            if (scanMode == ScanMode::PER_STEP || scanMode == ScanMode::GLOBAL) {
                // Only capture for modes that need storage (PER_STEP, GLOBAL)
                // For PER_STEP, only capture if step is valid
                if (scanMode == ScanMode::PER_STEP) {
                    if (mode == PlaybackMode::SEQUENCER_ACTIVE && captureStep >= 0) {
                        size_t sizeBefore = positionScan.size();
                        positionScan.capture(captureStep, currentIndex, currentPosition);
                        size_t sizeAfter = positionScan.size();
                        // Only log when scan size changes (new position stored) to avoid spam
                        if (sizeAfter != sizeBefore) {
                            ofLogVerbose("MediaPool") << "[SCAN_CAPTURE] PER_STEP: Step " << captureStep 
                                                      << ", Media " << currentIndex << ", Position " << currentPosition
                                                      << " (scan size: " << sizeBefore << " -> " << sizeAfter << ")";
                        }
                    }
                } else if (scanMode == ScanMode::GLOBAL) {
                    // GLOBAL mode: capture every frame (single shared position)
                    size_t sizeBefore = positionScan.size();
                    positionScan.capture(captureStep, currentIndex, currentPosition);
                    size_t sizeAfter = positionScan.size();
                    if (sizeAfter != sizeBefore) {
                        ofLogVerbose("MediaPool") << "[SCAN_CAPTURE] GLOBAL: Media " << currentIndex 
                                                  << ", Position " << currentPosition
                                                  << " (scan size: " << sizeBefore << " -> " << sizeAfter << ")";
                    }
                }
            }
            // PER_MEDIA mode: No capture needed - MediaPlayer::playheadPosition is the source of truth
            
            // Check if playhead has reached or exceeded effective region end
            bool reachedRegionEnd = (currentPosition >= effectiveRegionEnd - REGION_BOUNDARY_THRESHOLD);
            
            // Calculate loop start position for granular-style looping (used when in LOOP play style)
            float loopStartPos = regionStartVal;
            if (currentPlayStyle == PlayStyle::LOOP) {
                float relativeStartPos = activePlayer->startPosition.get();
                float regionSize = regionEndVal - regionStartVal;
                if (regionSize > 0.001f) {
                    loopStartPos = regionStartVal + relativeStartPos * regionSize;
                } else {
                    loopStartPos = std::max(0.0f, std::min(1.0f, relativeStartPos));
                }
            }
            
            // Handle region end based on play style
            if (reachedRegionEnd && !activePlayer->loop.get()) {
                switch (currentPlayStyle) {
                    case PlayStyle::ONCE:
                        // ONCE mode: Stop playback but preserve position for scanning
                        // Scanning should work even in ONCE mode - each trigger resumes from where it left off
                        // Only reset position when media actually finishes (reaches end)
                        // For now, just stop - position is preserved by MediaPlayer::stop()
                        // Position will be reset by MediaPool when appropriate (e.g., transport start)
                        activePlayer->stop();
                        break;
                    case PlayStyle::LOOP:
                        // Loop back to calculated loop start position
                        if (activePlayer->isAudioLoaded()) {
                            activePlayer->getAudioPlayer().setPosition(loopStartPos);
                        }
                        if (activePlayer->isVideoLoaded()) {
                            activePlayer->getVideoPlayer().getVideoFile().setPosition(loopStartPos);
                            activePlayer->getVideoPlayer().getVideoFile().update();
                        }
                        activePlayer->playheadPosition.set(loopStartPos);
                        break;
                    case PlayStyle::NEXT:
                        activePlayer->stop();
                        break;
                }
            } else if (reachedRegionEnd && activePlayer->loop.get()) {
                // Loop back to calculated loop start position when using player's internal loop
                if (currentPosition > effectiveRegionEnd + REGION_BOUNDARY_THRESHOLD) {
                    if (activePlayer->isAudioLoaded()) {
                        activePlayer->getAudioPlayer().setPosition(loopStartPos);
                    }
                    if (activePlayer->isVideoLoaded()) {
                        activePlayer->getVideoPlayer().getVideoFile().setPosition(loopStartPos);
                        activePlayer->getVideoPlayer().getVideoFile().update();
                    }
                    activePlayer->playheadPosition.set(loopStartPos);
                }
            }
            
            // PHASE 2: Position capture is now handled above (only for PER_STEP/GLOBAL modes)
            // MediaPlayer::stop() already preserves position in playheadPosition parameter
            // No need for redundant capture here
        } else if (!isCurrentlyPlaying && transportIsPlaying) {
            // Player stopped but transport still playing
            // NOTE: This only happens during normal playback (gate ends, region ends, etc.)
            // When transport stops, transportIsPlaying will be false, so this won't execute
            // PHASE 2: MediaPlayer::stop() already preserved position in playheadPosition
            // For PER_STEP/GLOBAL modes, we capture during playback (above), not on stop
            // No additional capture needed here
            
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
        // NOTE: When transportIsPlaying is false, we don't capture position at all
        // This prevents position from being saved after transport stops and memory is cleared
    } else if (activePlayer && !activePlayerIsValid) {
        // activePlayer is no longer valid - reset it
        activePlayer = nullptr;
        playerConnected = false;
        currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
    }
    
    // Check for end-of-playback (applies to both manual preview and sequencer)
    mode = currentMode.load(std::memory_order_relaxed);
    if ((mode == PlaybackMode::MANUAL_PREVIEW || mode == PlaybackMode::SEQUENCER_ACTIVE) && activePlayerIsValid && activePlayer) {
        if (!activePlayer->isPlaying() && !activePlayer->loop.get()) {
            onPlaybackEnd();
        }
    }
}

//--------------------------------------------------------------
void MediaPool::processEventQueue() {
    // Get parameter descriptors for defaults and validation
    auto paramDescriptors = getParameters();
    std::map<std::string, float> defaults;
    for (const auto& param : paramDescriptors) {
        defaults[param.name] = param.defaultValue;
    }
    
    // Process events from lock-free queue (no mutex needed!)
    // Consumer: GUI thread (this function)
    // Producer: Audio thread (onTrigger)
    // 
    // Limit processing per frame to prevent GUI thread blocking
    const int maxEventsPerFrame = 100;
    int eventsProcessed = 0;
    
    TriggerEvent event;
    while (eventQueue.try_dequeue(event) && eventsProcessed < maxEventsPerFrame) {
        eventsProcessed++;
        
        // Log trigger event (safe in GUI thread)
        auto noteIt = event.parameters.find("note");
        bool positionExplicitlySet = event.parameters.find("position") != event.parameters.end();
        ofLogVerbose("MediaPool") << "[TRIGGER] Step " << event.step << ", Note: " 
                                   << (noteIt != event.parameters.end() ? (int)noteIt->second : -1)
                                   << ", Position explicit: " << (positionExplicitlySet ? "YES" : "NO");
        
        // Extract mediaIndex from "note" parameter
        int mediaIndex = -1;
        if (noteIt != event.parameters.end()) {
            mediaIndex = (int)noteIt->second;
        }
        
        // Handle empty cells (rests) - stop immediately
        if (mediaIndex < 0) {
            if (activePlayer) {
                activePlayer->stop();
            }
            gateTimerActive = false;
            continue;  // Process next event
        }
        
        // Validate media index
        if (mediaIndex >= (int)players.size()) {
            continue;  // Process next event
        }
        
        // Get the media player for this step
        MediaPlayer* player = players[mediaIndex].get();
        if (!player) {
            continue;  // Process next event
        }
        
        // Update activeStepContext and apply position scan (GUI thread - safe to access positionScan)
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (event.step >= 0) {
                lastTriggeredStep = event.step;
                activeStepContext.step = event.step;
                activeStepContext.mediaIndex = mediaIndex;
                activeStepContext.triggerTime = ofGetElapsedTimef();
            }
            
            // Apply position scan: restore stored scan position if position not explicitly set
            // PHASE 1 SIMPLIFICATION: PER_MEDIA mode reads directly from MediaPlayer::playheadPosition
            // (no need for PositionScan storage - MediaPlayer already preserves position on stop())
            if (event.parameters.find("position") == event.parameters.end()) {
                static constexpr float POSITION_THRESHOLD = 0.01f;
                float scanPosition = 0.0f;
                ScanMode scanMode = positionScan.getMode();
                
                if (scanMode == ScanMode::PER_MEDIA) {
                    // PHASE 1: Read directly from MediaPlayer (single source of truth)
                    // MediaPlayer::stop() already preserves position in playheadPosition parameter
                    scanPosition = player->playheadPosition.get();
                    
                    // CRITICAL: If position is at the end (>= 0.99), reset to 0.0f for fresh start
                    // This allows scanning to work (resume from where it left off), but when media
                    // reaches the end, the next trigger starts from the beginning
                    static constexpr float END_THRESHOLD = 0.99f;
                    if (scanPosition >= END_THRESHOLD) {
                        float originalPos = scanPosition;  // Save for logging
                        scanPosition = 0.0f;
                        player->playheadPosition.set(0.0f);  // Reset in player too
                        ofLogNotice("MediaPool") << "[SCAN_RESTORE] PER_MEDIA: Position at end (" 
                                                 << originalPos << "), resetting to 0.0f for media " << mediaIndex;
                    }
                    
                    // Always set position parameter (including 0.0f after reset from end)
                    // This ensures playback starts from the correct position
                    event.parameters["position"] = scanPosition;
                    if (scanPosition > POSITION_THRESHOLD) {
                        ofLogNotice("MediaPool") << "[SCAN_RESTORE] PER_MEDIA: Using playheadPosition for media " 
                                                 << mediaIndex << ": " << scanPosition;
                    } else {
                        ofLogVerbose("MediaPool") << "[SCAN_RESTORE] PER_MEDIA: Starting from beginning (position: " 
                                                  << scanPosition << ") for media " << mediaIndex;
                    }
                } else {
                    // PER_STEP or GLOBAL mode: use PositionScan storage
                    size_t scanSizeBefore = positionScan.size();
                    scanPosition = positionScan.restore(event.step, mediaIndex);
                    
                    if (scanPosition > POSITION_THRESHOLD) {
                        event.parameters["position"] = scanPosition;
                        ofLogNotice("MediaPool") << "[SCAN_RESTORE] " << (scanMode == ScanMode::PER_STEP ? "PER_STEP" : "GLOBAL")
                                                 << ": Using scan position for step " << event.step 
                                                 << ", media " << mediaIndex << ": " << scanPosition
                                                 << " (scan size: " << scanSizeBefore << ")";
                    } else {
                        ofLogVerbose("MediaPool") << "[SCAN_SKIP] " << (scanMode == ScanMode::PER_STEP ? "PER_STEP" : "GLOBAL")
                                                  << ": No scan position available for step " << event.step 
                                                  << ", media " << mediaIndex << " (scan size: " << scanSizeBefore << ")";
                    }
                }
            }
        }
        
        // Set active player if changed
        bool playerChanged = (currentIndex != mediaIndex || activePlayer != player);
        if (playerChanged) {
            setActivePlayer(mediaIndex);
        }
        
        // Extract parameters from TriggerEvent map
        // If parameter is not in event, use current player value (not default) to preserve GUI settings
        // This allows MediaPool GUI to control parameters when not triggered from sequencer
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
                // Parameter is in event - use it (clamped to valid range)
                return std::max(minVal, std::min(maxVal, eventIt->second));
            }
            
            // Parameter NOT in event - use current player value instead of default
            // This preserves GUI settings when triggering manually (not from sequencer)
            // Special handling for "position" parameter (uses startPosition, not a regular parameter)
            if (paramName == "position") {
                float currentValue = player->startPosition.get();
                // Clamp current value to valid range
                return std::max(minVal, std::min(maxVal, currentValue));
            }
            
            const auto* param = player->getFloatParameter(paramName);
            if (param) {
                float currentValue = param->get();
                // Clamp current value to valid range
                return std::max(minVal, std::min(maxVal, currentValue));
            }
            
            // Fallback to default if parameter doesn't exist on player
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
        
        // Event processed, continue to next event in queue
    }
    
    // Check if we hit the processing limit (indicates queue might be backing up)
    if (eventsProcessed >= maxEventsPerFrame) {
        // Check if there are still events in queue
        size_t remainingEvents = eventQueue.size_approx();
        if (remainingEvents > 0) {
            ofLogWarning("MediaPool") << "Event queue processing limit reached (" << maxEventsPerFrame 
                                      << " events processed this frame). " << remainingEvents 
                                      << " events still in queue. Consider increasing maxEventsPerFrame or reducing trigger rate.";
        }
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
        // CRITICAL: Update lastTransportState FIRST to prevent update() from capturing position
        // after we clear memory. This ensures update() sees transport as stopped immediately.
        bool wasPlaying = lastTransportState;
        lastTransportState = isPlaying;
        
        if (isPlaying) {
            // Transport started - clear all positions and reset step context (fresh start)
            // PHASE 2: Clear positions based on scan mode
            ScanMode scanMode = positionScan.getMode();
            if (scanMode == ScanMode::PER_STEP || scanMode == ScanMode::GLOBAL) {
                // Clear PositionScan storage for PER_STEP/GLOBAL modes
                positionScan.clear();
            }
            // For PER_MEDIA mode, clear playheadPosition on all players (PositionScan not used)
            // For all modes, reset playheadPosition to ensure fresh start
            for (auto& player : players) {
                if (player) {
                    player->playheadPosition.set(0.0f);
                }
            }
            
            activeStepContext.step = -1;
            activeStepContext.mediaIndex = -1;
            activeStepContext.triggerTime = 0.0f;
            
            ofLogNotice("MediaPool") << "[TRANSPORT_START] ===== TRANSPORT STARTED ===== ScanMode: " 
                                     << (int)scanMode << ", ScanSize: " << positionScan.size() << " =====";
        } else {
            // Transport stopped - CRITICAL: Clear positions FIRST before stopping players
            // This prevents update() from capturing position after we clear
            // (update() checks lastTransportState, which we've already set to false above)
            ScanMode scanMode = positionScan.getMode();
            if (scanMode == ScanMode::PER_STEP || scanMode == ScanMode::GLOBAL) {
                // Clear PositionScan storage for PER_STEP/GLOBAL modes
                positionScan.clear();
            }
            // For PER_MEDIA mode, clear playheadPosition on all players
            // For all modes, reset playheadPosition to prevent position leakage
            for (auto& player : players) {
                if (player) {
                    player->playheadPosition.set(0.0f);
                }
            }
            
            // Reset step context BEFORE stopping players
            activeStepContext.step = -1;
            activeStepContext.mediaIndex = -1;
            activeStepContext.triggerTime = 0.0f;
            
            // Now stop players
            // NOTE: We stop players AFTER clearing positions to prevent any position capture
            // in update() from repopulating scan
            if (currentMode.load(std::memory_order_relaxed) == PlaybackMode::SEQUENCER_ACTIVE) {
                gateTimerActive = false;
                currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
                
                if (activePlayer && activePlayer->isPlaying()) {
                    activePlayer->stop();
                }
            }
            
            ofLogNotice("MediaPool") << "[TRANSPORT_STOP] ===== TRANSPORT STOPPED ===== Positions CLEARED (ScanMode: " 
                                     << (int)scanMode << ", ScanSize: " << positionScan.size() << ") =====";
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

// getTypeName() uses default implementation from Module base class (returns getName())

ofJson MediaPool::toJson() const {
    ofJson json;
    
    // Save directory
    json["directory"] = dataDirectory;
    
    // Save active player index
    json["activePlayerIndex"] = currentIndex;
    
    // Save scan mode
    json["scanMode"] = static_cast<int>(getScanMode());
    
    // Save play style
    json["playStyle"] = static_cast<int>(currentPlayStyle);
    
    // Save all players with their file paths and parameters
    ofJson playersArray = ofJson::array();
    for (size_t i = 0; i < players.size(); i++) {
        auto player = players[i].get();
        if (player) {
            ofJson playerJson;
            playerJson["audioFile"] = player->getAudioFilePath();
            playerJson["videoFile"] = player->getVideoFilePath();
            
            // Save MediaPlayer parameters
            ofJson paramsJson;
            paramsJson["startPosition"] = player->startPosition.get();
            paramsJson["speed"] = player->speed.get();
            paramsJson["volume"] = player->volume.get();
            paramsJson["loop"] = player->loop.get();
            paramsJson["loopSize"] = player->loopSize.get();
            paramsJson["regionStart"] = player->regionStart.get();
            paramsJson["regionEnd"] = player->regionEnd.get();
            paramsJson["audioEnabled"] = player->audioEnabled.get();
            paramsJson["videoEnabled"] = player->videoEnabled.get();
            paramsJson["brightness"] = player->brightness.get();
            paramsJson["hue"] = player->hue.get();
            paramsJson["saturation"] = player->saturation.get();
            
            playerJson["parameters"] = paramsJson;
            playersArray.push_back(playerJson);
        }
    }
    json["players"] = playersArray;
    
    return json;
}

void MediaPool::fromJson(const ofJson& json) {
    // Load directory
    if (json.contains("directory")) {
        std::string dir = json["directory"];
        if (!dir.empty() && ofDirectory(dir).exists()) {
            setDataDirectory(dir);
        }
    }
    
    // Load scan mode
    if (json.contains("scanMode")) {
        int modeInt = json["scanMode"];
        if (modeInt >= 0 && modeInt <= 3) {
            setScanMode(static_cast<ScanMode>(modeInt));
        }
    }
    
    // Load play style
    if (json.contains("playStyle")) {
        int styleInt = json["playStyle"];
        if (styleInt >= 0 && styleInt <= 2) {
            setPlayStyle(static_cast<PlayStyle>(styleInt));
        }
    }
    
    // Load players (if directory wasn't set, we still try to load player parameters)
    if (json.contains("players") && json["players"].is_array()) {
        auto playersArray = json["players"];
        
        // First, ensure we have enough players
        // If directory was loaded, players should already be created
        // Otherwise, we can't restore players without file paths
        
        // Load player parameters for existing players
        for (size_t i = 0; i < playersArray.size() && i < players.size(); i++) {
            auto playerJson = playersArray[i];
            auto player = players[i].get();
            
            if (player && playerJson.contains("parameters")) {
                auto paramsJson = playerJson["parameters"];
                
                if (paramsJson.contains("startPosition")) {
                    player->startPosition.set(paramsJson["startPosition"]);
                }
                if (paramsJson.contains("speed")) {
                    player->speed.set(paramsJson["speed"]);
                }
                if (paramsJson.contains("volume")) {
                    player->volume.set(paramsJson["volume"]);
                }
                if (paramsJson.contains("loop")) {
                    player->loop.set(paramsJson["loop"]);
                }
                if (paramsJson.contains("loopSize")) {
                    player->loopSize.set(paramsJson["loopSize"]);
                }
                if (paramsJson.contains("regionStart")) {
                    player->regionStart.set(paramsJson["regionStart"]);
                }
                if (paramsJson.contains("regionEnd")) {
                    player->regionEnd.set(paramsJson["regionEnd"]);
                }
                if (paramsJson.contains("audioEnabled")) {
                    player->audioEnabled.set(paramsJson["audioEnabled"]);
                }
                if (paramsJson.contains("videoEnabled")) {
                    player->videoEnabled.set(paramsJson["videoEnabled"]);
                }
                if (paramsJson.contains("brightness")) {
                    player->brightness.set(paramsJson["brightness"]);
                }
                if (paramsJson.contains("hue")) {
                    player->hue.set(paramsJson["hue"]);
                }
                if (paramsJson.contains("saturation")) {
                    player->saturation.set(paramsJson["saturation"]);
                }
            }
        }
    }
    
    // Load active player index (after players are loaded)
    if (json.contains("activePlayerIndex")) {
        int index = json["activePlayerIndex"];
        if (index >= 0 && index < (int)players.size()) {
            setCurrentIndex(index);
        }
    }
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
    params.push_back(ParameterDescriptor("loopSize", ParameterType::FLOAT, 0.0f, 10.0f, 1.0f, "Loop Size (seconds)"));
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
        } else if (name == "loopSize") {
            oldValue = activePlayer->loopSize.get();
            // Clamp loopSize to valid range (0.001s minimum, up to duration or 10s max)
            float duration = activePlayer->getDuration();
            float maxAllowed = (duration > 0.001f) ? duration : 10.0f;
            float clampedVal = std::max(0.001f, std::min(maxAllowed, val));
            activePlayer->loopSize.set(clampedVal);
            return std::abs(oldValue - clampedVal) > PARAMETER_EPSILON;
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
    // LOCK-FREE: Push event to queue from audio thread (no mutex needed!)
    // Position memory application happens in processEventQueue() (GUI thread) to avoid race conditions
    // 
    // CRITICAL: NO LOGGING IN AUDIO THREAD - logging can allocate memory and cause crashes
    // All logging is done in processEventQueue() (GUI thread) where it's safe
    
    // Make a copy of the event to ensure safe enqueueing
    // This ensures the std::map inside TriggerEvent is properly copied
    TriggerEvent eventCopy = event;
    
    // Queue event using lock-free queue (no mutex needed!)
    // If queue is full, event is dropped silently (logging happens in GUI thread)
    if (!eventQueue.try_enqueue(eventCopy)) {
        // Queue full - drop event silently (can't log from audio thread)
        // The GUI thread will log a warning if it detects queue issues
    }
}

//--------------------------------------------------------------
// Position scan mode control
void MediaPool::setScanMode(ScanMode mode) {
    std::lock_guard<std::mutex> lock(stateMutex);
    positionScan.setMode(mode);
    ofLogNotice("MediaPool") << "Position scan mode set to: " << (int)mode;
}

ScanMode MediaPool::getScanMode() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return positionScan.getMode();
}


