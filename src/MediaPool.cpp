
#include "MediaPool.h"
#include "MediaPlayer.h"
#include "TrackerSequencer.h"
#include "ofFileUtils.h"
#include "ofSystemUtils.h"
#include <unordered_map>
#include <unordered_set>

MediaPool::MediaPool(const std::string& dataDir) 
    : currentIndex(0), dataDirectory(dataDir), isSetup(false), currentMode(PlaybackMode::IDLE), currentPreviewMode(PreviewMode::STOP_AT_END), clock(nullptr), activePlayer(nullptr), lastTransportState(false), playerConnected(false) {
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

std::string MediaPool::findMatchingVideo(const std::string& audioFile) {
    std::string audioBase = getBaseName(audioFile);
    
    // Use hash map for O(1) lookup instead of O(n) linear search
    for (const auto& videoFile : videoFiles) {
        std::string videoBase = getBaseName(videoFile);
        if (audioBase == videoBase) {
            return videoFile;
        }
    }
    
    return "";
}

std::string MediaPool::findMatchingAudio(const std::string& videoFile) {
    std::string videoBase = getBaseName(videoFile);
    
    for (const auto& audioFile : audioFiles) {
        std::string audioBase = getBaseName(audioFile);
        if (audioBase == videoBase) {
            return audioFile;
        }
    }
    
    return "";
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
    // PERFORMANCE CRITICAL: Only set startPosition before play() - don't set position
    // because that triggers onPositionChanged() which calls expensive setPosition() (~200ms).
    // The play() method will handle setting the actual video position efficiently.
    player->startPosition.set(position);
    // NOTE: Don't set player->position here - it triggers expensive setPosition() via listener
    // The play() method will set the position correctly based on startPosition
    
    // Re-enable audio/video if they were loaded (stop() disables them)
    if (player->isAudioLoaded()) {
        player->audioEnabled.set(true);
    }
    if (player->isVideoLoaded()) {
        player->videoEnabled.set(true);
    }
    
    // Set loop based on preview mode
    bool shouldLoop = (currentPreviewMode == PreviewMode::LOOP);
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

//--------------------------------------------------------------
void MediaPool::stopManualPreview() {
    std::lock_guard<std::mutex> lock(stateMutex);
    
    // Only stop if we're in manual preview mode
    if (currentMode.load(std::memory_order_relaxed) != PlaybackMode::MANUAL_PREVIEW) {
        return;
    }
    
    // Stop the active player
    if (activePlayer) {
        activePlayer->stop();
    }
    
    // Transition to IDLE state
    currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
    
    ofLogNotice("ofxMediaPool") << "Manual preview stopped (state: IDLE)";
}

// Note: Old onStepTrigger methods removed - now using onTrigger() which receives TriggerEvent directly

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
    
    // Transition to IDLE state
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

//--------------------------------------------------------------
// Preview mode control
void MediaPool::setPreviewMode(PreviewMode mode) {
    std::lock_guard<std::mutex> lock(stateMutex);
    currentPreviewMode = mode;
    ofLogNotice("ofxMediaPool") << "Preview mode set to: " << (int)mode;
    
    // Apply the new mode to the currently active player if it's playing
    if (activePlayer && currentMode.load(std::memory_order_relaxed) == PlaybackMode::MANUAL_PREVIEW) {
        bool shouldLoop = (mode == PreviewMode::LOOP);
        activePlayer->loop.set(shouldLoop);
        ofLogNotice("ofxMediaPool") << "Applied preview mode to active player - loop: " << shouldLoop;
    }
}

PreviewMode MediaPool::getPreviewMode() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return currentPreviewMode;
}

//--------------------------------------------------------------
void MediaPool::update() {
    // Update position memory: Store current playback position for active player
    // Also detect when player stops to capture final position before it's reset
    if (activePlayer && currentIndex < players.size()) {
        bool isCurrentlyPlaying = activePlayer->isPlaying();
        bool wasPlaying = lastPlayingStateByMediaIndex[currentIndex];
        
        if (isCurrentlyPlaying) {
            // Player is playing - store position only when it's actually advancing
            float currentPosition = activePlayer->position.get();
            auto it = lastPositionByMediaIndex.find(currentIndex);
            
            // SIMPLIFIED LOGIC: Only store position when it's advancing forward
            // This prevents storing startPosition (near-zero) when playback just started
            float currentStartPosition = activePlayer->startPosition.get();
            if (it == lastPositionByMediaIndex.end()) {
                // First time storing - only store if position is meaningful (> 1% into media)
                // This prevents storing near-zero startPosition values
                if (currentPosition > 0.01f) {
                    lastPositionByMediaIndex[currentIndex] = currentPosition;
                    ofLogNotice("MediaPool") << "[POSITION_MEMORY] Stored initial position for media " << currentIndex 
                                             << ": " << currentPosition 
                                             << " | startPosition: " << currentStartPosition;
                }
            } else {
                // Already have stored position - only update if advancing forward
                // This prevents overwriting good positions with earlier/near-zero values
                float storedPosition = it->second;
                if (currentPosition > storedPosition + 0.001f) {
                    // Position advanced - update stored value
                    lastPositionByMediaIndex[currentIndex] = currentPosition;
                    if (std::abs(currentPosition - storedPosition) > 0.01f) {
                        ofLogNotice("MediaPool") << "[POSITION_MEMORY] Stored advancing position for media " << currentIndex 
                                                 << ": " << currentPosition << " (was: " << storedPosition << ")"
                                                 << " | startPosition: " << currentStartPosition;
                    }
                }
                // If position didn't advance or went backward, keep existing stored value
            }
        } else if (wasPlaying && !isCurrentlyPlaying) {
            // Player just stopped - capture final position for position memory
            // Note: stop() preserves the actual playback position (fixed in MediaPlayer::stop())
            float stoppedPosition = activePlayer->position.get();
            float stoppedStartPosition = activePlayer->startPosition.get();
            
            ofLogNotice("MediaPool") << "[POSITION_MEMORY] Player stopped for media " << currentIndex
                                      << " | position: " << stoppedPosition 
                                      << " | startPosition: " << stoppedStartPosition;
            
            // CRITICAL FIX: If position was reset to 0 (or near-zero) after stop(),
            // it means the underlying players reset their position before stop() could preserve it.
            // In this case, check if we already have a stored position that's more recent.
            // If the current position is invalid (< 0.01f) but we have a stored position,
            // use the stored position instead (it was captured before the reset).
            if (stoppedPosition < 0.01f) {
                // Position was reset - check if we have a stored position
                auto it = lastPositionByMediaIndex.find(currentIndex);
                if (it != lastPositionByMediaIndex.end() && it->second > 0.01f) {
                    // Use the stored position (it was captured before the reset)
                    stoppedPosition = it->second;
                    ofLogNotice("MediaPool") << "[POSITION_MEMORY] Position was reset to " 
                                             << activePlayer->position.get() 
                                             << ", using stored position: " << stoppedPosition;
                }
            }
            
            // Only store if position is meaningful (> 1% into media)
            // This prevents storing near-zero positions when playback stops immediately
            if (stoppedPosition > 0.01f) {
                float oldStored = (lastPositionByMediaIndex.find(currentIndex) != lastPositionByMediaIndex.end()) 
                                  ? lastPositionByMediaIndex[currentIndex] : 0.0f;
                lastPositionByMediaIndex[currentIndex] = stoppedPosition;
                
                if (std::abs(stoppedPosition - oldStored) > 0.001f) {
                    ofLogNotice("MediaPool") << "[POSITION_MEMORY] Captured position on stop for media " 
                                             << currentIndex << ": " << stoppedPosition 
                                             << " (was: " << oldStored << ")";
                }
            } else {
                ofLogNotice("MediaPool") << "[POSITION_MEMORY] Position too small to store on stop: " 
                                         << stoppedPosition << " (threshold: 0.01)";
            }
        }
        
        // Update playing state tracking
        lastPlayingStateByMediaIndex[currentIndex] = isCurrentlyPlaying;
    }
    
    // Process lock-free event queue from audio thread (moved to GUI thread)
    processEventQueue();
    
    // Check for end-of-media in manual preview mode
    // Use the underlying players' built-in end detection instead of hacky position checking
    PlaybackMode mode = currentMode.load(std::memory_order_relaxed);
    if (mode == PlaybackMode::MANUAL_PREVIEW && activePlayer) {
        // Check if the player has stopped playing (which happens automatically at end)
        if (!activePlayer->isPlaying() && !activePlayer->loop.get()) {
            // Media has reached the end and stopped
            onManualPreviewEnd();
        }
    }
    
    // Check for gate end in sequencer active mode (user-triggered steps with length-based triggers)
    // When playWithGate() is called, MediaPlayer stops automatically when the gate duration expires
    // We need to detect this and transition back to IDLE mode so the button state updates correctly
    if (mode == PlaybackMode::SEQUENCER_ACTIVE && activePlayer && currentIndex < players.size()) {
        // Check if the player was playing and then stopped (gate ended)
        // This handles length-based triggers where the step ends after a duration
        bool isCurrentlyPlaying = activePlayer->isPlaying();
        bool wasPlaying = lastPlayingStateByMediaIndex[currentIndex];
        
        if (wasPlaying && !isCurrentlyPlaying) {
            // Player was playing and then stopped - gate ended
            // Transition to IDLE mode so the button state updates correctly (goes back to grey)
            currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
            ofLogNotice("MediaPool") << "Gate ended for sequencer-triggered step - transitioning to IDLE mode";
        }
    }
}

//--------------------------------------------------------------
void MediaPool::processEventQueue() {
    // Process all queued events from audio thread (called from update in GUI thread)
    std::queue<TriggerEvent> localQueue;
    
    // Swap queue under lock (fast operation)
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
    while (!localQueue.empty()) {
        const TriggerEvent& event = localQueue.front();
        
        // Extract mediaIndex from "note" parameter
        int mediaIndex = -1;
        auto noteIt = event.parameters.find("note");
        if (noteIt != event.parameters.end()) {
            mediaIndex = (int)noteIt->second;
        }
        
        // Handle empty cells (rests) - stop immediately
        if (mediaIndex < 0) {
            if (activePlayer) {
                // Capture position before stopping (for position memory)
                // Only store if meaningful - update() will handle the actual storage
                if (activePlayer->isPlaying() && currentIndex < players.size()) {
                    float currentPosition = activePlayer->position.get();
                    float currentStartPosition = activePlayer->startPosition.get();
                    ofLogNotice("MediaPool") << "[POSITION_MEMORY] Rest stop for media " << currentIndex
                                              << " | position: " << currentPosition 
                                              << " | startPosition: " << currentStartPosition;
                    // Only store if meaningful (> 1%) - prevents storing near-zero values
                    if (currentPosition > 0.01f) {
                        lastPositionByMediaIndex[currentIndex] = currentPosition;
                        ofLogNotice("MediaPool") << "[POSITION_MEMORY] Captured position before rest stop: media " 
                                                 << currentIndex << " = " << currentPosition;
                    }
                }
                activePlayer->stop();
                // After stop, log both positions again to see if they changed
                if (currentIndex < players.size()) {
                    float afterStopPosition = activePlayer->position.get();
                    float afterStopStartPosition = activePlayer->startPosition.get();
                    ofLogNotice("MediaPool") << "[POSITION_MEMORY] After rest stop for media " << currentIndex
                                              << " | position: " << afterStopPosition 
                                              << " | startPosition: " << afterStopStartPosition;
                }
            }
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
        
        // Only set active player if it's different (optimization: avoid resetting connection flag)
        bool playerChanged = (currentIndex != mediaIndex || activePlayer != player);
        if (playerChanged) {
            // Capture position of previous player before switching (for position memory)
            // Only store if meaningful - update() will handle the actual storage during playback
            if (activePlayer && activePlayer->isPlaying() && currentIndex < players.size()) {
                float currentPosition = activePlayer->position.get();
                float currentStartPosition = activePlayer->startPosition.get();
                ofLogNotice("MediaPool") << "[POSITION_MEMORY] Player switch from media " << currentIndex
                                          << " | position: " << currentPosition 
                                          << " | startPosition: " << currentStartPosition
                                          << " (switching to media " << mediaIndex << ")";
                // Only store if meaningful (> 1%) - prevents storing near-zero values
                if (currentPosition > 0.01f) {
                    lastPositionByMediaIndex[currentIndex] = currentPosition;
                    ofLogNotice("MediaPool") << "[POSITION_MEMORY] Captured position before switch: media " 
                                             << currentIndex << " = " << currentPosition;
                }
            }
            setActivePlayer(mediaIndex);
        }
        
        // Extract parameters from TriggerEvent map with defaults and proper clamping
        // Helper lambda to get parameter value with validation
        auto getParamValue = [&](const std::string& paramName, float defaultValue) -> float {
            // Find parameter descriptor for range validation
            auto descIt = std::find_if(paramDescriptors.begin(), paramDescriptors.end(),
                [&](const ParameterDescriptor& desc) { return desc.name == paramName; });
            
            float minVal = 0.0f;
            float maxVal = 1.0f;
            if (descIt != paramDescriptors.end()) {
                minVal = descIt->minValue;
                maxVal = descIt->maxValue;
            }
            
            // Get value from event or use default
            auto eventIt = event.parameters.find(paramName);
            if (eventIt != event.parameters.end()) {
                // Clamp to parameter range
                return std::max(minVal, std::min(maxVal, eventIt->second));
            }
            return defaultValue;
        };
        
        float position = getParamValue("position", defaults.count("position") > 0 ? defaults["position"] : 0.0f);
        float speed = getParamValue("speed", defaults.count("speed") > 0 ? defaults["speed"] : 1.0f);
        float volume = getParamValue("volume", defaults.count("volume") > 0 ? defaults["volume"] : 1.0f);
        
        // Audio/video always enabled for sequencer triggers
        if (!player->audioEnabled.get()) {
            player->audioEnabled.set(true);
        }
        if (!player->videoEnabled.get()) {
            player->videoEnabled.set(true);
        }
        
        // Set volume (audio parameter)
        if (std::abs(player->volume.get() - volume) > 0.001f) {
            player->volume.set(volume);
        }
        
        // Set playback parameters (only if changed)
        // PERFORMANCE CRITICAL: Only set startPosition before play() - don't set position
        // because that triggers onPositionChanged() which calls expensive setPosition() (~200ms).
        // The play() method will handle setting the actual video position efficiently.
        // We'll update position parameter after play() for UI display, but without triggering listener.
        float currentStartPos = player->startPosition.get();
        float currentPos = player->position.get();
        ofLogNotice("MediaPool") << "[POSITION_MEMORY] Processing trigger for media " << mediaIndex 
                                  << " with position: " << position 
                                  << " (currentIndex: " << currentIndex << ")"
                                  << " | startPosition: " << currentStartPos 
                                  << " | position: " << currentPos;
        if (std::abs(currentStartPos - position) > 0.001f) {
            player->startPosition.set(position);
            ofLogNotice("MediaPool") << "[POSITION_MEMORY] Set startPosition to: " << position;
        }
        // NOTE: Don't set player->position here - it triggers expensive setPosition() via listener
        // The play() method will set the position correctly based on startPosition
        if (std::abs(player->speed.get() - speed) > 0.001f) {
            player->speed.set(speed);
        }
        
        // Use duration directly from TriggerEvent (already calculated in seconds by TrackerSequencer)
        float stepDurationSeconds = event.duration;
        
        // Trigger media playback with gating (default behavior for step-based playback)
        // NOTE: We do NOT store event.position here - that's the START position, not the current playback position.
        // Position memory should only store the actual playback position during playback (in update()),
        // or when capturing before stop. Storing event.position here would overwrite the real playback position.
        player->playWithGate(stepDurationSeconds);
        
        // CRITICAL FIX: Only transition to SEQUENCER_ACTIVE if playback actually started
        // This ensures the button state is correct - if playWithGate() fails or doesn't start playback,
        // we stay in IDLE mode so the button doesn't incorrectly show as green
        if (player->isPlaying()) {
            // Transition to SEQUENCER_ACTIVE state (atomic write)
            currentMode.store(PlaybackMode::SEQUENCER_ACTIVE, std::memory_order_relaxed);
        } else {
            // Playback didn't start - stay in IDLE mode
            // This can happen if the player is in a bad state or media isn't loaded
            ofLogWarning("MediaPool") << "playWithGate() called but player is not playing - staying in IDLE mode";
        }
        
        // Log positions after playWithGate to track any changes
        float afterPlayPosition = player->position.get();
        float afterPlayStartPosition = player->startPosition.get();
        ofLogNotice("MediaPool") << "[POSITION_MEMORY] After playWithGate for media " << mediaIndex
                                  << " | position: " << afterPlayPosition 
                                  << " | startPosition: " << afterPlayStartPosition;
        
        localQueue.pop();
    }
}

//--------------------------------------------------------------
// Handle end-of-media in manual preview mode
void MediaPool::onManualPreviewEnd() {
    // No lock needed - caller (update()) already holds stateMutex
    
    if (currentMode.load(std::memory_order_relaxed) != PlaybackMode::MANUAL_PREVIEW) return;
    
    switch (currentPreviewMode) {
        case PreviewMode::STOP_AT_END:
            // Stop the current player
            if (activePlayer) {
                activePlayer->stop();
            }
            currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
            break;
        case PreviewMode::LOOP:
            // Already handled by loop=true
            break;
        case PreviewMode::PLAY_NEXT:
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
                    nextPlayer->position.set(0.0f);
                    
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
                    
                    ofLogNotice("ofxMediaPool") << "Started next media " << nextIndex << " (state: MANUAL_PREVIEW)";
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
        
        if (!isPlaying) {
            // Transport stopped - transition to IDLE if in SEQUENCER_ACTIVE mode
            if (currentMode.load(std::memory_order_relaxed) == PlaybackMode::SEQUENCER_ACTIVE) {
                currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
                ofLogNotice("MediaPool") << "Transport stopped - transitioning to IDLE mode";
                
                // Stop any active player that was playing under sequencer control
                if (activePlayer && activePlayer->isPlaying()) {
                    activePlayer->stop();
                    // REMOVED: Don't notify position change on transport stop
                    // The pattern already has the correct position value from when the step was triggered
                    // Position sync should only happen when user explicitly edits position, not on transport state changes
                    // This prevents unwanted pattern cell edits when pausing playback
                    ofLogNotice("MediaPool") << "Stopped active player due to transport stop";
                }
            }
        }
        
        // Notify any registered transport listener
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
    params.push_back(ParameterDescriptor("loopStart", ParameterType::FLOAT, 0.0f, 1.0f, 0.0f, "Loop Start"));
    params.push_back(ParameterDescriptor("loopEnd", ParameterType::FLOAT, 0.0f, 1.0f, 1.0f, "Loop End"));
    
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
            return std::abs(oldValue - val) > 0.0001f;
        } else if (name == "speed") {
            oldValue = activePlayer->speed.get();
            activePlayer->speed.set(val);
            return std::abs(oldValue - val) > 0.0001f;
        } else if (name == "pitch") {
            oldValue = activePlayer->pitch.get();
            activePlayer->pitch.set(val);
            return std::abs(oldValue - val) > 0.0001f;
        } else if (name == "loopStart") {
            oldValue = activePlayer->loopStart.get();
            activePlayer->loopStart.set(val);
            return std::abs(oldValue - val) > 0.0001f;
        } else if (name == "loopEnd") {
            oldValue = activePlayer->loopEnd.get();
            activePlayer->loopEnd.set(val);
            return std::abs(oldValue - val) > 0.0001f;
        } else if (name == "position") {
            // Position has special handling: set both startPosition and position
            // When setting position via ParameterSync, we're setting the startPosition (for sync with tracker)
            // PERFORMANCE CRITICAL: Update position parameter for UI display, but rely on onPositionChanged() checks
            // to prevent expensive setPosition() calls when position is already correct
            oldValue = activePlayer->startPosition.get();
            if (std::abs(oldValue - val) > 0.0001f) {
                activePlayer->startPosition.set(val);
                // Update position parameter for UI display (shows the target start position when paused)
                // The onPositionChanged() listener will check if actual video position needs to change
                // and only call expensive setPosition() if there's a significant difference (>0.01f threshold)
                float currentPos = activePlayer->position.get();
                if (std::abs(currentPos - val) > 0.001f) {
                    activePlayer->position.set(val);
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
            // Check if we have a stored position for this media index
            auto posIt = lastPositionByMediaIndex.find(mediaIndex);
            if (posIt != lastPositionByMediaIndex.end()) {
                // Use stored position (position memory)
                event.parameters["position"] = posIt->second;
                ofLogNotice("MediaPool") << "[POSITION_MEMORY] Using stored position for media " 
                                         << mediaIndex << ": " << posIt->second;
            }
        }
    }
    
    // Lock-free queue push (std::queue is thread-safe for single producer, single consumer)
    // We use a mutex only for queue access, but this is minimal overhead
    {
        std::lock_guard<std::mutex> queueLock(stateMutex);
        eventQueue.push(event);
    }
}


