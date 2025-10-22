#include "MediaPool.h"
#include "MediaPlayer.h"
#include "Clock.h"
#include "ofFileUtils.h"
#include "ofSystemUtils.h"

MediaPool::MediaPool(const std::string& dataDir) 
    : currentIndex(0), dataDirectory(dataDir), isSetup(false), activePlayer(nullptr), isConnected(false), soundOutput(nullptr), visualOutput(nullptr), clock(nullptr) {
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
    dir.allowExt("wav");
    dir.allowExt("mp3");
    dir.allowExt("aiff");
    dir.allowExt("mov");
    dir.allowExt("mp4");
    dir.allowExt("avi");
    
    dir.listDir();
    
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
            ofLogNotice("ofxMediaPool") << "Audio file: " << filename;
        } else if (isVideoFile(filename)) {
            videoFiles.push_back(fullPath);
            ofLogNotice("ofxMediaPool") << "Video file: " << filename;
        }
    }
    
    ofLogNotice("ofxMediaPool") << "Found " << audioFiles.size() << " audio files and " << videoFiles.size() << " video files";
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

void MediaPool::createPairedPlayers() {
    // This is handled in mediaPair()
}

void MediaPool::createStandalonePlayers() {
    // This is handled in mediaPair()
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
    if (activePlayer && isConnected) {
        disconnectActivePlayer();
    }
    
    activePlayer = players[index].get();
    ofLogNotice("ofxMediaPool") << "Set active player to index " << index;
    
    // Connect to outputs when active player changes
    if (soundOutput && visualOutput) {
        ofLogNotice("ofxMediaPool") << "Connecting new active player to outputs";
        connectActivePlayer(*soundOutput, *visualOutput);
    }
}

MediaPlayer* MediaPool::getActivePlayer() {
    return activePlayer;
}

void MediaPool::setOutputs(ofxSoundOutput& soundOut, ofxVisualOutput& visualOut) {
    soundOutput = &soundOut;
    visualOutput = &visualOut;
    isConnected = false; // Not connected yet, will connect when player becomes active
    ofLogNotice("ofxMediaPool") << "Output references set - ready to connect when player becomes active";
}

void MediaPool::connectActivePlayer(ofxSoundOutput& soundOut, ofxVisualOutput& visualOut) {
    if (!activePlayer) {
        ofLogWarning("ofxMediaPool") << "No active player to connect";
        return;
    }
    
    if (isConnected) {
        ofLogNotice("ofxMediaPool") << "Player already connected, skipping";
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
    
    visualOut.connectTo(activePlayer->getVideoPlayer());
    isConnected = true;
    
    // Store references for reconnection
    soundOutput = &soundOut;
    visualOutput = &visualOut;
    
    ofLogNotice("ofxMediaPool") << "Connected active player to outputs - audio routing: MediaPlayer -> soundOutput";
}

void MediaPool::disconnectActivePlayer() {
    if (!activePlayer || !isConnected) {
        return;
    }
    
    // Disconnect audio and video outputs
    activePlayer->getAudioPlayer().disconnect();
    // Note: visualOutput.disconnect() is called from the sequencer
    isConnected = false;
    
    ofLogNotice("ofxMediaPool") << "Disconnected active player from outputs";
}

//--------------------------------------------------------------
void MediaPool::onStepTrigger(int step, int mediaIndex, float position, 
                              float speed, float volume, float stepLength, 
                              bool audioEnabled, bool videoEnabled) {
    if (!clock) {
        ofLogError("ofxMediaPool") << "Clock reference not set!";
        return;
    }
    
    float bpm = clock->getBPM(); // Get BPM from clock
    ofLogNotice("ofxMediaPool") << "Step event received: step=" << step << ", bpm=" << bpm << ", stepLength=" << stepLength;
    
    // Handle empty cells (rests) - only stop if no scheduled stop is in progress
    if (mediaIndex < 0) {
        if (activePlayer) {
            // Only stop immediately if there's no scheduled stop in progress
            // This prevents empty steps from overriding the duration of previous steps
            if (!activePlayer->hasScheduledStop()) {
                activePlayer->stop();
                ofLogNotice("ofxMediaPool") << "Step " << step << " is empty (rest) - stopping current media immediately";
            } else {
                ofLogNotice("ofxMediaPool") << "Step " << step << " is empty (rest) - but previous step has scheduled stop, letting it complete";
            }
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
    
    // Calculate step duration based on BPM and step length
    // stepLength is already in beats (converted by ofApp)
    float stepDurationSeconds = (60.0f / bpm) * stepLength;
    
    ofLogNotice("ofxMediaPool") << "Calculated duration: " << stepDurationSeconds << "s (BPM=" << bpm << ", stepLength=" << stepLength << " steps)";
    
    // Trigger media playback with gating
    player->playWithGate(stepDurationSeconds);
    
    ofLogNotice("ofxMediaPool") << "Triggered media " << mediaIndex 
                                << " with duration " << stepDurationSeconds << "s";
}

//--------------------------------------------------------------
void MediaPool::stopAllMedia() {
    for (auto& player : players) {
        if (player) {
            player->stop();
            player->cancelScheduledStop();
        }
    }
    
    // Disconnect active player
    if (activePlayer) {
        disconnectActivePlayer();
    }
    
    ofLogNotice("ofxMediaPool") << "Stopped all media players";
}

//--------------------------------------------------------------
void MediaPool::drawMediaPoolGUI() {
    // Create a dedicated media library window
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    bool showWindow = true;
    ImGui::Begin("Media Pool", &showWindow);
    
    // Media library info
    ImGui::Text("Media Pool:");
    
    // Directory path bar with browse button
    ImGui::Text("Directory:");
    ImGui::SameLine();
    
    // Display current directory path (truncated if too long)
    std::string displayPath = dataDirectory;
    if (displayPath.length() > 50) {
        displayPath = "..." + displayPath.substr(displayPath.length() - 47);
    }
    ImGui::Text("%s", displayPath.c_str());
    
    // Browse button
    if (ImGui::Button("Browse Directory")) {
        browseForDirectory();
    }
    
    ImGui::Text("Total Players: %zu", getNumPlayers());
    ImGui::Text("Current Index: %zu", currentIndex);
    
    ImGui::Separator();
    
    // Show indexed media list with actual file names
    if (getNumPlayers() > 0) {
        ImGui::Text("Available Media:");
        auto playerNames = getPlayerNames();
        auto playerFileNames = getPlayerFileNames();
        
        for (size_t i = 0; i < playerNames.size(); i++) {
            auto player = getMediaPlayer(i);
            if (player) {
                std::string status = "";
                if (player->isAudioLoaded()) status += "A";
                if (player->isVideoLoaded()) status += "V";
                if (status.empty()) status = "---";
                
                // Show actual file names
                std::string fileInfo = "";
                if (i < playerFileNames.size() && !playerFileNames[i].empty()) {
                    fileInfo = " | " + playerFileNames[i];
                }
                
                ImGui::Text("[%zu] %s [%s]%s", i, playerNames[i].c_str(), status.c_str(), fileInfo.c_str());
            }
        }
    }
    
    ImGui::Separator();
    
    // Current player status
    auto currentPlayer = getActivePlayer();
    if (currentPlayer) {
        ImGui::Text("Current Player Status:");
        ImGui::Text("Audio: %s", currentPlayer->isAudioLoaded() ? "Loaded" : "Not loaded");
        ImGui::Text("Video: %s", currentPlayer->isVideoLoaded() ? "Loaded" : "Not loaded");
        ImGui::Text("Playing: %s", currentPlayer->isPlaying() ? "Yes" : "No");
        
        // Navigation buttons
        if (ImGui::Button("Previous Player")) {
            previousPlayer();
        }
        ImGui::SameLine();
        if (ImGui::Button("Next Player")) {
            nextPlayer();
        }
    } else {
        ImGui::Text("No current player");
    }
    
    ImGui::End();
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
