#include "MediaPool.h"
#include "MediaPlayer.h"
#include "Clock.h"
#include "ofFileUtils.h"
#include "ofSystemUtils.h"

MediaPool::MediaPool(const std::string& dataDir) 
    : currentIndex(0), dataDirectory(dataDir), isSetup(false), currentMode(PlaybackMode::IDLE), currentPreviewMode(PreviewMode::STOP_AT_END), clock(nullptr), activePlayer(nullptr), lastTransportState(false) {
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
    
    ofLogNotice("ofxMediaPool") << "Media pairing files by base name";
    ofLogNotice("ofxMediaPool") << "Audio files count: " << audioFiles.size();
    ofLogNotice("ofxMediaPool") << "Video files count: " << videoFiles.size();
    
    // Create paired players for matching audio/video files
    for (const auto& audioFile : audioFiles) {
        ofLogNotice("ofxMediaPool") << "Processing audio file: " << audioFile;
        std::string matchingVideo = findMatchingVideo(audioFile);
        
        if (!matchingVideo.empty()) {
            // Create paired player
            auto player = std::make_unique<MediaPlayer>();
            player->load(audioFile, matchingVideo);
            players.push_back(std::move(player));
            
            
            ofLogNotice("ofxMediaPool") << "Paired: " << ofFilePath::getFileName(audioFile) 
                                         << " + " << ofFilePath::getFileName(matchingVideo);
        } else {
            // Create audio-only player
            auto player = std::make_unique<MediaPlayer>();
            player->loadAudio(audioFile);
            players.push_back(std::move(player));
            
            
            ofLogNotice("ofxMediaPool") << "Audio-only: " << ofFilePath::getFileName(audioFile);
        }
    }
    
    // Create video-only players for unmatched video files
    for (const auto& videoFile : videoFiles) {
        std::string matchingAudio = findMatchingAudio(videoFile);
        
        if (matchingAudio.empty()) {
            // Create video-only player
            auto player = std::make_unique<MediaPlayer>();
            player->loadVideo(videoFile);
            players.push_back(std::move(player));
            
            
            ofLogNotice("ofxMediaPool") << "Video-only: " << ofFilePath::getFileName(videoFile);
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
    ofLogNotice("ofxMediaPool") << "Looking for video matching audio base: " << audioBase;
    
    for (const auto& videoFile : videoFiles) {
        std::string videoBase = getBaseName(videoFile);
        ofLogNotice("ofxMediaPool") << "Checking video base: " << videoBase;
        if (audioBase == videoBase) {
            ofLogNotice("ofxMediaPool") << "✅ Found matching video: " << videoFile;
            return videoFile;
        }
    }
    
    ofLogNotice("ofxMediaPool") << "❌ No matching video found for: " << audioBase;
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
    
    // Disconnect previous active player
    if (activePlayer) {
        disconnectActivePlayer();
    }
    
    activePlayer = players[index].get();
    currentIndex = index;  // Keep currentIndex in sync with activePlayer
    ofLogNotice("ofxMediaPool") << "Set active player to index " << index << ", currentIndex now=" << currentIndex;
    
    
    // Note: Output connections are now managed externally by ofApp
}

MediaPlayer* MediaPool::getActivePlayer() {
    return activePlayer;
}


void MediaPool::connectActivePlayer(ofxSoundOutput& soundOut, ofxVisualOutput& visualOut) {
    if (!activePlayer) {
        ofLogWarning("ofxMediaPool") << "No active player to connect";
        return;
    }
    
    
    // Debug: Log connection details
    ofLogNotice("ofxMediaPool") << "Connecting active player to sound output: " << soundOut.getName();
    ofLogNotice("ofxMediaPool") << "Active player: " << activePlayer;
    
    // Connect audio and video outputs
    try {
        // Connect the MediaPlayer's audio output to the soundOutput
        activePlayer->getAudioPlayer().connectTo(soundOut);
        ofLogNotice("ofxMediaPool") << "Audio connection successful";
    } catch (const std::exception& e) {
        ofLogError("ofxMediaPool") << "Failed to connect audio: " << e.what();
        return;
    }
    
    // Only connect video output if the player has video loaded
    if (activePlayer->isVideoLoaded()) {
        visualOut.connectTo(activePlayer->getVideoPlayer());
    }
    
    ofLogNotice("ofxMediaPool") << "Connected active player to outputs - audio routing: MediaPlayer -> soundOutput";
}

void MediaPool::disconnectActivePlayer() {
    if (!activePlayer) {
        return;
    }
    
    // Disconnect audio and video outputs
    activePlayer->getAudioPlayer().disconnect();
    // Note: visualOutput.disconnect() is called from the sequencer
    
    ofLogNotice("ofxMediaPool") << "Disconnected active player from outputs";
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
    
    // Set active player and connect to outputs
    setActivePlayer(index);
    
    // Transition to MANUAL_PREVIEW state
    currentMode = PlaybackMode::MANUAL_PREVIEW;
    
    // Stop and reset the player for fresh playback
    player->stop();  // Stop any current playback
    player->position.set(position);
    
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
void MediaPool::onStepTrigger(int step, int mediaIndex, float position, 
                              float speed, float volume, float stepLength, 
                              bool audioEnabled, bool videoEnabled) {
    std::lock_guard<std::mutex> lock(stateMutex);
    
    if (!clock) {
        ofLogError("ofxMediaPool") << "Clock reference not set!";
        return;
    }
    
    float bpm = clock->getBPM(); // Get BPM from clock
    ofLogNotice("ofxMediaPool") << "Step event received: step=" << step << ", bpm=" << bpm << ", duration=" << stepLength << "s";
    
    // Handle empty cells (rests) - stop immediately
    if (mediaIndex < 0) {
        if (activePlayer) {
            activePlayer->stop();
            ofLogNotice("ofxMediaPool") << "Step " << step << " is empty (rest) - stopping current media immediately";
        }
        return;
    }
    
    // Validate media index
    if (mediaIndex >= (int)players.size()) {
        ofLogWarning("ofxMediaPool") << "Invalid media index: " << mediaIndex 
                                     << " (available: " << players.size() << ")";
        return;
    }
    
    // Get the media player for this step
    MediaPlayer* player = players[mediaIndex].get();
    if (!player) {
        ofLogWarning("ofxMediaPool") << "Media player not found for index: " << mediaIndex;
        return;
    }
    
    // Set active player and connect to outputs
    setActivePlayer(mediaIndex);
    
    // Transition to SEQUENCER_ACTIVE state
    currentMode = PlaybackMode::SEQUENCER_ACTIVE;
    
    // Apply media-specific parameters from the step event
    if (audioEnabled) {
        player->audioEnabled.set(true);
        player->volume.set(volume);
    } else {
        player->audioEnabled.set(false);
    }
    
    if (videoEnabled) {
        player->videoEnabled.set(true);
    } else {
        player->videoEnabled.set(false);
    }
    
    // Set playback parameters
    player->position.set(position);
    player->speed.set(speed);
    
    // Use duration directly (already calculated in seconds by TrackerSequencer)
    float stepDurationSeconds = stepLength;  // stepLength is now duration in seconds
    
    ofLogNotice("ofxMediaPool") << "Using duration: " << stepDurationSeconds << "s (passed from TrackerSequencer)";
    
    // Trigger media playback with gating (default behavior for step-based playback)
    player->playWithGate(stepDurationSeconds);
    
    ofLogNotice("ofxMediaPool") << "Triggered media " << mediaIndex 
                                << " with duration " << stepDurationSeconds << "s";
}

// Overloaded version using struct for cleaner interface
void MediaPool::onStepTrigger(const StepTriggerParams& params) {
    onStepTrigger(params.step, params.mediaIndex, params.position, 
                  params.speed, params.volume, params.duration, 
                  params.audioEnabled, params.videoEnabled);
}

//--------------------------------------------------------------
void MediaPool::stopAllMedia() {
    std::lock_guard<std::mutex> lock(stateMutex);
    
    for (auto& player : players) {
        if (player) {
            player->stop();
        }
    }
    
    if (activePlayer) {
        disconnectActivePlayer();
    }
    
    // Transition to IDLE state
    currentMode = PlaybackMode::IDLE;
    
    ofLogNotice("ofxMediaPool") << "All media stopped (state: IDLE)";
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
            ofLogNotice("ofxMediaPool") << "Found audio file: " << filename;
        } else if (isVideoFile(filename)) {
            videoFiles.push_back(fullPath);
            ofLogNotice("ofxMediaPool") << "Found video file: " << filename;
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
    std::lock_guard<std::mutex> lock(stateMutex);
    return currentMode;
}

bool MediaPool::isSequencerActive() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return currentMode == PlaybackMode::SEQUENCER_ACTIVE;
}

bool MediaPool::isManualPreview() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return currentMode == PlaybackMode::MANUAL_PREVIEW;
}

bool MediaPool::isIdle() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return currentMode == PlaybackMode::IDLE;
}

//--------------------------------------------------------------
// Preview mode control
void MediaPool::setPreviewMode(PreviewMode mode) {
    std::lock_guard<std::mutex> lock(stateMutex);
    currentPreviewMode = mode;
    ofLogNotice("ofxMediaPool") << "Preview mode set to: " << (int)mode;
    
    // Apply the new mode to the currently active player if it's playing
    if (activePlayer && currentMode == PlaybackMode::MANUAL_PREVIEW) {
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
    std::lock_guard<std::mutex> lock(stateMutex);
    
    // Check for end-of-media in manual preview mode
    // Use the underlying players' built-in end detection instead of hacky position checking
    if (currentMode == PlaybackMode::MANUAL_PREVIEW && activePlayer) {
        // Check if the player has stopped playing (which happens automatically at end)
        if (!activePlayer->isPlaying() && !activePlayer->loop.get()) {
            // Media has reached the end and stopped
            ofLogNotice("ofxMediaPool") << "Media reached end and stopped";
            onManualPreviewEnd();
        }
    }
}

//--------------------------------------------------------------
// Handle end-of-media in manual preview mode
void MediaPool::onManualPreviewEnd() {
    // No lock needed - caller (update()) already holds stateMutex
    
    if (currentMode != PlaybackMode::MANUAL_PREVIEW) return;
    
    switch (currentPreviewMode) {
        case PreviewMode::STOP_AT_END:
            // Stop the current player
            if (activePlayer) {
                activePlayer->stop();
            }
            ofLogNotice("ofxMediaPool") << "Stopped at end of media, currentIndex=" << currentIndex << ", going to IDLE";
            currentMode = PlaybackMode::IDLE;
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
                    ofLogNotice("ofxMediaPool") << "Next media not available, stopping";
                    currentMode = PlaybackMode::IDLE;
                }
            } else {
                ofLogNotice("ofxMediaPool") << "No next media to play, stopping";
                if (activePlayer) {
                    activePlayer->stop();
                }
                currentMode = PlaybackMode::IDLE;
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
            if (currentMode == PlaybackMode::SEQUENCER_ACTIVE) {
                currentMode = PlaybackMode::IDLE;
                ofLogNotice("MediaPool") << "Transport stopped - transitioning to IDLE mode";
                
                // Stop any active player that was playing under sequencer control
                if (activePlayer && activePlayer->isPlaying()) {
                    activePlayer->stop();
                    ofLogNotice("MediaPool") << "Stopped active player due to transport stop";
                }
            }
        } else {
            // Transport started - log the state change
            ofLogNotice("MediaPool") << "Transport started - current mode: " << (int)currentMode;
        }
        
        // Notify any registered transport listener
        if (transportListener) {
            transportListener(isPlaying);
        }
    }
}
