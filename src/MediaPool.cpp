#include "MediaPool.h"
#include "MediaPlayer.h"
#include "core/ModuleFactory.h"
#include "ofFileUtils.h"
#include "ofSystemUtils.h"
#include "ofUtils.h"
#include "ofJson.h"
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

MediaPool::MediaPool(const std::string& dataDir) 
    : currentIndex(0), dataDirectory(dataDir), isSetup(false), currentMode(PlaybackMode::IDLE), 
      currentPlayStyle(PlayStyle::ONCE), clock(nullptr), activePlayer(nullptr), 
      lastTriggeredStep(-1), activeStepContext(),
      polyphonyMode_(PolyphonyMode::MONOPHONIC) {  // Default to monophonic for backward compatibility
    // setup() will be called later with clock reference
}

MediaPool::~MediaPool() noexcept {
    isDestroying_ = true;  // Prevent update() from running after destruction starts
    clear();
}

void MediaPool::setup(Clock* clockRef) {
    if (isSetup) return;
    
    clock = clockRef; // Store clock reference
    ofLogNotice("MediaPool") << "Setting up media library with directory: " << dataDirectory;
    isSetup = true;
}

//--------------------------------------------------------------
//--------------------------------------------------------------
void MediaPool::initialize(Clock* clock, ModuleRegistry* registry, ConnectionManager* connectionManager, 
                          ParameterRouter* parameterRouter, bool isRestored) {
    // Phase 2.3: Unified initialization - combines postCreateSetup and completeRestore
    
    // 1. Basic setup (from postCreateSetup)
    if (clock) {
        setup(clock);
    }
    
    // 2. Complete restoration (from completeRestore) - only if restoring from session
    if (isRestored) {
        // Check if we have files to load (either from deferred loading or from fromJson)
        bool hasFilesToLoad = !audioFiles.empty() || !videoFiles.empty();
        bool shouldLoad = deferMediaLoading_ || (hasFilesToLoad && players.empty());
        
        if (shouldLoad) {
            ofLogNotice("MediaPool") << "Completing deferred media loading...";
            
            // Only call mediaPair if we have files but no players yet
            // This handles both the deferred loading case and cases where fromJson
            // populated file lists but mediaPair wasn't called
            if (hasFilesToLoad && players.empty()) {
                ofLogNotice("MediaPool") << "Creating players from " << audioFiles.size() 
                                         << " audio files and " << videoFiles.size() << " video files";
                deferMediaLoading_ = false;  // Clear flag before calling mediaPair
                mediaPair();  // This creates the players
                
                // Verify players were created
                if (players.empty()) {
                    ofLogWarning("MediaPool") << "mediaPair() completed but no players were created. "
                                               << "Check that media files exist at saved paths.";
                    // Log some example paths for debugging
                    if (!audioFiles.empty()) {
                        ofLogWarning("MediaPool") << "Example audio file path: " << audioFiles[0];
                        ofLogWarning("MediaPool") << "File exists: " << (ofFile::doesFileExist(audioFiles[0]) ? "yes" : "no");
                    }
                    if (!videoFiles.empty()) {
                        ofLogWarning("MediaPool") << "Example video file path: " << videoFiles[0];
                        ofLogWarning("MediaPool") << "File exists: " << (ofFile::doesFileExist(videoFiles[0]) ? "yes" : "no");
                    }
                } else {
                    ofLogNotice("MediaPool") << "Successfully created " << players.size() << " players from saved file paths";
                }
            } else {
                deferMediaLoading_ = false;  // Clear flag even if we skip mediaPair
                if (!hasFilesToLoad) {
                    ofLogNotice("MediaPool") << "No files to load (audioFiles: " << audioFiles.size() 
                                             << ", videoFiles: " << videoFiles.size() << ")";
                }
                if (!players.empty()) {
                    ofLogNotice("MediaPool") << "Players already exist (" << players.size() << "), skipping mediaPair()";
                }
            }
            
            // NOW load parameters for the newly created players
            // Match players by file paths since indices might have changed
            size_t paramsRestored = 0;
            for (const auto& deferredParams : deferredPlayerParams_) {
                // Find the player that matches these file paths
                bool found = false;
                for (size_t i = 0; i < players.size(); i++) {
                    auto player = players[i].get();
                    if (!player) continue;
                    
                    bool audioMatch = deferredParams.audioFile.empty() ? 
                        player->getAudioFilePath().empty() : 
                        player->getAudioFilePath() == deferredParams.audioFile;
                    bool videoMatch = deferredParams.videoFile.empty() ? 
                        player->getVideoFilePath().empty() : 
                        player->getVideoFilePath() == deferredParams.videoFile;
                    
                    if (audioMatch && videoMatch) {
                        // Found matching player - load parameters
                        auto paramsJson = deferredParams.paramsJson;
                        
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
                        
                        ofLogNotice("MediaPool") << "Restored parameters for player " << i 
                                                 << " (audio: " << deferredParams.audioFile 
                                                 << ", video: " << deferredParams.videoFile << ")";
                        paramsRestored++;
                        found = true;
                        break;
                    }
                }
                
                if (!found) {
                    ofLogWarning("MediaPool") << "Could not find player matching audio: " 
                                               << deferredParams.audioFile 
                                               << ", video: " << deferredParams.videoFile;
                }
            }
            
            ofLogNotice("MediaPool") << "Restored parameters for " << paramsRestored 
                                     << " of " << deferredPlayerParams_.size() << " players";
            
            // Restore active player index after players are created
            if (deferredActivePlayerIndex_ >= 0 && deferredActivePlayerIndex_ < (int)players.size()) {
                setCurrentIndex(deferredActivePlayerIndex_);
                ofLogNotice("MediaPool") << "Restored active player index: " << deferredActivePlayerIndex_;
            }
            
            // Clear deferred params after loading
            deferredPlayerParams_.clear();
            
            // Initialize active player if we have players
            if (!players.empty()) {
                initializeFirstActivePlayer();
                
                // CRITICAL: Initialize first video frame for ALL loaded video players after session restore
                // This prevents GUI lag and low FPS when session loads.
                // 
                // Problem: When session loads, players are stopped at position 0.0, so:
                // 1. MediaPool::update() only calls player->update() for playing players
                // 2. MediaPlayer::update() only processes video when shouldBeEnabled=true
                // 3. shouldBeEnabled requires isPlaying() || position > 0.001f
                // 4. ofxVideoPlayerObject::process() gates out stopped videos at position 0
                // Result: Video textures never get initialized, causing lag until scrubbing
                //
                // Solution: Initialize first frame for ALL loaded videos (not just enabled ones)
                // by setting position to a small non-zero value and calling update/process once
                size_t videosInitialized = 0;
                for (size_t i = 0; i < players.size(); i++) {
                    MediaPlayer* player = players[i].get();
                    if (player && player->isVideoLoaded()) {
                        // Check if video file is actually loaded at lower level
                        if (player->getVideoPlayer().getVideoFile().isLoaded()) {
                            // Use startPosition if available (from session restore), otherwise use 0.01
                            float initPos = player->startPosition.get() > MIN_REGION_SIZE ? 
                                           player->startPosition.get() : INIT_POSITION;
                            
                            // Set position using helper function (handles both audio and video)
                            seekPlayerToPosition(player, initPos);
                            
                            // CRITICAL: Call process() once to initialize the output buffer
                            // This ensures the video texture is loaded and ready for rendering
                            ofFbo emptyInput;
                            player->getVideoPlayer().process(emptyInput, player->getVideoPlayer().getOutputBuffer());
                            
                            videosInitialized++;
                            ofLogNotice("MediaPool") << "Initialized first frame for video player " << i 
                                                      << " at position " << initPos;
                        }
                    }
                }
                
                if (videosInitialized > 0) {
                    ofLogNotice("MediaPool") << "Initialized " << videosInitialized << " video frame(s) after session restore";
                }
                
                ofLogNotice("MediaPool") << "Session restore complete: " << players.size() 
                                         << " players loaded with parameters restored";
            } else {
                ofLogWarning("MediaPool") << "Session restore complete but no players were created. "
                                          << "Check that media files exist at the saved paths.";
            }
            
            deferMediaLoading_ = false;  // Clear flag
        } else {
            ofLogNotice("MediaPool") << "No deferred loading needed (deferMediaLoading_=" 
                                     << deferMediaLoading_ << ", hasFiles=" << hasFilesToLoad 
                                     << ", players=" << players.size() << ")";
        }
    }
}

//--------------------------------------------------------------
// DEPRECATED: Use initialize() instead
void MediaPool::postCreateSetup(Clock* clock) {
    // Legacy implementation - delegates to initialize()
    initialize(clock, nullptr, nullptr, nullptr, false);
}

void MediaPool::setCustomPath(const std::string& absolutePath) {
    ofLogNotice("MediaPool") << "Setting custom absolute path: " << absolutePath;
    
    ofDirectory dir(absolutePath);
    if (!dir.exists()) {
        ofLogError("MediaPool") << "Custom path does not exist: " << absolutePath;
        return;
    }
    
    dataDirectory = absolutePath;
    clear();
    
    ofLogNotice("MediaPool") << "✅ Using custom path: " << absolutePath;
    
    // Scan the custom directory
    scanMediaFiles(absolutePath, dir);
    
    // Auto-pair files
    mediaPair();
}

void MediaPool::scanDirectory(const std::string& path) {
    dataDirectory = path;
    clear();
    
    ofLogNotice("MediaPool") << "🔍 scanDirectory called with path: " << path;
    
    // Simple approach: just use the provided path
    ofDirectory dir(path);
    if (!dir.exists()) {
        ofLogError("MediaPool") << "Directory does not exist: " << path;
        return;
    }
    
    ofLogNotice("MediaPool") << "✅ Directory exists, scanning for media files...";
    
    // Scan for media files
    scanMediaFiles(path, dir);
}


void MediaPool::mediaPair() {
    // CRITICAL: Reset activePlayer BEFORE clearing players to avoid dangling pointer
    if (activePlayer) {
        disconnectActivePlayer();
        activePlayer = nullptr;
    }
    
    // Disconnect all players before clearing
    for (auto& player : players) {
        if (player) {
            ensurePlayerAudioDisconnected(player.get());
            ensurePlayerVideoDisconnected(player.get());
        }
    }
    playerAudioConnected.clear();
    playerVideoConnected.clear();
    
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
                MediaPlayer* playerPtr = player.get();
                players.push_back(std::move(player));
                // Don't connect immediately - connections are managed dynamically when players start/stop
                // This ensures only active players are connected, fixing video blending issues
                playerVideoConnected[playerPtr] = false;
                playerAudioConnected[playerPtr] = false;
                pairedVideos.insert(audioBase);
            } else {
                ofLogWarning("MediaPool") << "Failed to load paired media: " << audioFile << " + " << it->second;
            }
        } else {
            // Create audio-only player
            auto player = std::make_unique<MediaPlayer>();
            bool loaded = player->loadAudio(audioFile);
            // Only add player if audio loaded successfully
            if (loaded) {
                MediaPlayer* playerPtr = player.get();
                players.push_back(std::move(player));
                // Don't connect immediately - connections are managed dynamically when players start/stop
                playerVideoConnected[playerPtr] = false;
            } else {
                ofLogWarning("MediaPool") << "Failed to load audio: " << audioFile;
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
                MediaPlayer* playerPtr = player.get();
                players.push_back(std::move(player));
                // Don't connect immediately - connections are managed dynamically when players start/stop
                playerVideoConnected[playerPtr] = false;
            } else {
                ofLogWarning("MediaPool") << "Failed to load video: " << videoFile;
            }
        }
    }
    
    ofLogNotice("MediaPool") << "Created " << players.size() << " media players";
}

//--------------------------------------------------------------
// DEPRECATED: Use initialize() instead
void MediaPool::completeRestore() {
    // Legacy implementation - delegates to initialize() with isRestored=true
    initialize(nullptr, nullptr, nullptr, nullptr, true);
}


void MediaPool::pairByIndex() {
    // pairByIndex() calls clear() which already handles activePlayer reset
    clear();
    
    ofLogNotice("MediaPool") << "Pairing files by index";
    
    size_t maxPairs = std::max(audioFiles.size(), videoFiles.size());
    
    for (size_t i = 0; i < maxPairs; i++) {
        auto player = std::make_unique<MediaPlayer>();
        
        std::string audioFile = (i < audioFiles.size()) ? audioFiles[i] : "";
        std::string videoFile = (i < videoFiles.size()) ? videoFiles[i] : "";
        
        bool loaded = player->load(audioFile, videoFile);
        // Only add player if at least one media file loaded successfully
        if (loaded) {
            MediaPlayer* playerPtr = player.get();
            players.push_back(std::move(player));
            // Don't connect immediately - connections are managed dynamically when players start/stop
            playerVideoConnected[playerPtr] = false;
            ofLogNotice("MediaPool") << "Index pair " << i << ": " 
                                           << ofFilePath::getFileName(audioFile) 
                                           << " + " << ofFilePath::getFileName(videoFile);
        } else {
            ofLogWarning("MediaPool") << "Failed to load index pair " << i << ": " 
                                            << ofFilePath::getFileName(audioFile) 
                                            << " + " << ofFilePath::getFileName(videoFile);
        }
    }
    
    ofLogNotice("MediaPool") << "Created " << players.size() << " media players by index";
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
    // Disconnect active player first to avoid dangling pointer
    if (activePlayer) {
        disconnectActivePlayer();
        activePlayer = nullptr;
    }
    
    // Disconnect all players from mixers (MediaPool's responsibility)
    for (auto& player : players) {
        if (player) {
            ensurePlayerAudioDisconnected(player.get());
            ensurePlayerVideoDisconnected(player.get());
        }
    }
    
    // Clear all state
    players.clear();
    playerAudioConnected.clear();
    playerVideoConnected.clear();
    audioFiles.clear();
    videoFiles.clear();
    currentIndex = 0;
    scheduledStops_.clear();  // Clear scheduled stops when clearing pool
    
    // Note: Mixers are member objects - their destructors will safely clear
    // internal connections with proper locking. Routers already disconnected
    // external connections (AudioOutput/VideoOutput side).
}

void MediaPool::refresh() {
    scanDirectory(dataDirectory);
    mediaPair();
}

//--------------------------------------------------------------
bool MediaPool::removePlayer(size_t index) {
    std::lock_guard<std::mutex> lock(stateMutex);
    
    if (index >= players.size()) {
        ofLogWarning("MediaPool") << "Cannot remove player: index " << index << " out of range";
        return false;
    }
    
    // Get player pointer before any operations
    MediaPlayer* playerToRemove = players[index].get();
    if (!playerToRemove) {
        ofLogWarning("MediaPool") << "Cannot remove player: player at index " << index << " is null";
        return false;
    }
    
    // CRITICAL: Stop the player if it's playing BEFORE removing it
    // This prevents crashes from update() or audio thread accessing deleted player
    if (playerToRemove->isPlaying()) {
        playerToRemove->stop();
        ofLogNotice("MediaPool") << "Stopped playing player before removal";
    }
    
    // If this is the active player, disconnect and reset it, and transition to IDLE
    bool wasActivePlayer = (activePlayer == playerToRemove);
    if (wasActivePlayer) {
        disconnectActivePlayer();
        activePlayer = nullptr;
        // Transition to IDLE mode if this was the active player
        currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
        ofLogNotice("MediaPool") << "Removed active player - transitioning to IDLE mode";
    }
    
    // Disconnect player from mixers before removing
    ensurePlayerAudioDisconnected(playerToRemove);
    ensurePlayerVideoDisconnected(playerToRemove);
    playerAudioConnected.erase(playerToRemove);
    playerVideoConnected.erase(playerToRemove);
    
    // Remove any scheduled stops for this player
    scheduledStops_.erase(
        std::remove_if(scheduledStops_.begin(), scheduledStops_.end(),
            [playerToRemove](const ScheduledStop& stop) {
                return stop.player == playerToRemove;
            }),
        scheduledStops_.end()
    );
    
    // CRITICAL: Clear event queue of any pending events for this player
    // This prevents processEventQueue() from trying to access the deleted player
    // Note: We can't safely filter and re-enqueue with lock-free queue, so we drain all events
    // If this was the active player, we want to clear all events anyway
    // If it wasn't active, clearing is still safe - sequencer will retrigger if needed
    TriggerEvent dummy;
    size_t eventsDrained = 0;
    while (eventQueue.try_dequeue(dummy)) {
        eventsDrained++;
    }
    if (eventsDrained > 0) {
        ofLogNotice("MediaPool") << "Drained " << eventsDrained << " events from queue when removing player";
    }
    
    // Remove the player
    players.erase(players.begin() + index);
    
    // Adjust currentIndex if needed
    if (currentIndex >= players.size() && !players.empty()) {
        // If currentIndex was at or beyond the removed player, adjust it
        currentIndex = players.size() - 1;
    } else if (currentIndex > index) {
        // If currentIndex was after the removed player, decrement it
        currentIndex--;
    }
    
    // If we removed the last player, reset currentIndex
    if (players.empty()) {
        currentIndex = 0;
    }
    
    // Also remove corresponding entries from audioFiles and videoFiles if they exist
    // Note: This is approximate since we don't track which files correspond to which player
    // A more accurate implementation would require tracking file-to-player mapping
    if (index < audioFiles.size()) {
        audioFiles.erase(audioFiles.begin() + index);
    }
    if (index < videoFiles.size()) {
        videoFiles.erase(videoFiles.begin() + index);
    }
    
    ofLogNotice("MediaPool") << "Removed player at index " << index << " (remaining: " << players.size() << ")";
    return true;
}

//--------------------------------------------------------------
// Individual file addition (for drag-and-drop support)
bool MediaPool::addMediaFile(const std::string& filePath) {
    // Validate file exists
    ofFile file(filePath);
    if (!file.exists()) {
        ofLogWarning("MediaPool") << "File does not exist: " << filePath;
        return false;
    }
    
    // Check if file is a valid media file
    std::string filename = ofFilePath::getFileName(filePath);
    bool isAudio = isAudioFile(filename);
    bool isVideo = isVideoFile(filename);
    
    if (!isAudio && !isVideo) {
        ofLogWarning("MediaPool") << "File is not a valid media file: " << filePath;
        return false;
    }
    
    // CRITICAL: Lock mutex to prevent accessing players while they're being modified
    std::lock_guard<std::mutex> lock(stateMutex);
    
    // Check if file is already in the list (avoid duplicates)
    if (isAudio) {
        for (const auto& existingFile : audioFiles) {
            if (existingFile == filePath) {
                ofLogNotice("MediaPool") << "File already in pool: " << filePath;
                return false; // Already exists, but not an error
            }
        }
    } else if (isVideo) {
        for (const auto& existingFile : videoFiles) {
            if (existingFile == filePath) {
                ofLogNotice("MediaPool") << "File already in pool: " << filePath;
                return false; // Already exists, but not an error
            }
        }
    }
    
    // Add file to appropriate list
    if (isAudio) {
        audioFiles.push_back(filePath);
        ofLogNotice("MediaPool") << "Added audio file: " << filePath;
    } else if (isVideo) {
        videoFiles.push_back(filePath);
        ofLogNotice("MediaPool") << "Added video file: " << filePath;
    }
    
    // If deferring media loading (during session restore), just add to lists and return
    // Media will be loaded later via mediaPair()
    if (deferMediaLoading_) {
        return true;
    }
    
    // Try to pair with existing files
    std::string baseName = getBaseName(filePath);
    
    if (isAudio) {
        // Look for matching video file
        for (const auto& videoFile : videoFiles) {
            if (getBaseName(videoFile) == baseName && videoFile != filePath) {
                // Found matching video - create paired player
                auto player = std::make_unique<MediaPlayer>();
                bool loaded = player->load(filePath, videoFile);
                if (loaded) {
                    MediaPlayer* playerPtr = player.get();
                    players.push_back(std::move(player));
                    // Don't connect immediately - connections are managed dynamically when players start/stop
                    playerVideoConnected[playerPtr] = false;
                    ofLogNotice("MediaPool") << "Created paired player: " << filename << " + " << ofFilePath::getFileName(videoFile);
                    return true;
                } else {
                    ofLogWarning("MediaPool") << "Failed to load paired media: " << filePath << " + " << videoFile;
                }
            }
        }
        // No matching video found - create audio-only player
        auto player = std::make_unique<MediaPlayer>();
        bool loaded = player->loadAudio(filePath);
        if (loaded) {
            MediaPlayer* playerPtr = player.get();
            players.push_back(std::move(player));
            // Don't connect immediately - connections are managed dynamically when players start/stop
            playerVideoConnected[playerPtr] = false;
            ofLogNotice("MediaPool") << "Created audio-only player: " << filename;
            return true;
        } else {
            ofLogWarning("MediaPool") << "Failed to load audio: " << filePath;
            // Remove from list if loading failed
            audioFiles.pop_back();
            return false;
        }
    } else if (isVideo) {
        // Look for matching audio file
        for (const auto& audioFile : audioFiles) {
            if (getBaseName(audioFile) == baseName && audioFile != filePath) {
                // Found matching audio - check if there's already a player using this audio
                // If so, remove it before creating the paired player
                for (auto it = players.begin(); it != players.end(); ++it) {
                    MediaPlayer* existingPlayer = it->get();
                    if (existingPlayer && existingPlayer->getAudioFilePath() == audioFile && 
                        existingPlayer->getVideoFilePath().empty()) {
                        // Found audio-only player using this audio file - remove it
                        if (activePlayer == existingPlayer) {
                            disconnectActivePlayer();
                            activePlayer = nullptr;
                        }
                        players.erase(it);
                        ofLogNotice("MediaPool") << "Removed audio-only player to create paired player";
                        break;
                    }
                }
                
                // Create paired player
                auto player = std::make_unique<MediaPlayer>();
                bool loaded = player->load(audioFile, filePath);
                if (loaded) {
                    MediaPlayer* playerPtr = player.get();
                    players.push_back(std::move(player));
                    // Don't connect immediately - connections are managed dynamically when players start/stop
                    playerVideoConnected[playerPtr] = false;
                    ofLogNotice("MediaPool") << "Created paired player: " << ofFilePath::getFileName(audioFile) << " + " << filename;
                    return true;
                } else {
                    ofLogWarning("MediaPool") << "Failed to load paired media: " << audioFile << " + " << filePath;
                }
            }
        }
        // No matching audio found - create video-only player
        auto player = std::make_unique<MediaPlayer>();
        bool loaded = player->loadVideo(filePath);
        if (loaded) {
            MediaPlayer* playerPtr = player.get();
            players.push_back(std::move(player));
            // Don't connect immediately - connections are managed dynamically when players start/stop
            playerVideoConnected[playerPtr] = false;
            ofLogNotice("MediaPool") << "Created video-only player: " << filename;
            return true;
        } else {
            ofLogWarning("MediaPool") << "Failed to load video: " << filePath;
            // Remove from list if loading failed
            videoFiles.pop_back();
            return false;
        }
    }
    
    return false;
}

void MediaPool::addMediaFiles(const std::vector<std::string>& filePaths) {
    int successCount = 0;
    int failCount = 0;
    
    for (const auto& filePath : filePaths) {
        if (addMediaFile(filePath)) {
            successCount++;
        } else {
            failCount++;
        }
    }
    
    ofLogNotice("MediaPool") << "Added " << successCount << " files, " << failCount << " failed";
    
    // If we added files successfully, try to initialize first active player if pool was empty
    if (successCount > 0 && players.empty() == false && activePlayer == nullptr) {
        initializeFirstActivePlayer();
    }
}

//--------------------------------------------------------------
bool MediaPool::acceptFileDrop(const std::vector<std::string>& filePaths) {
    // Module interface implementation - delegate to addMediaFiles
    if (filePaths.empty()) {
        return false;
    }
    addMediaFiles(filePaths);
    return true;
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
    ofLogVerbose("MediaPool") << "[setActivePlayer] Called with index=" << index << " (players.size()=" << players.size() << ")";
    
    // CRITICAL: Check if mutex is already held (would cause deadlock)
    // If called from playMediaManual which already holds the lock, we need to avoid double-locking
    // For now, we'll try to acquire the lock - if it's already held, this will deadlock
    // TODO: Consider making this function have an internal version that doesn't lock
    
    try {
        std::lock_guard<std::mutex> lock(stateMutex);
        ofLogVerbose("MediaPool") << "[setActivePlayer] Mutex acquired";
        
        if (index >= players.size()) {
            ofLogWarning("MediaPool") << "[setActivePlayer] Invalid player index: " << index;
            return;
        }
        
        MediaPlayer* newPlayer = players[index].get();
        if (!newPlayer) {
            ofLogWarning("MediaPool") << "[setActivePlayer] Player is null at index: " << index;
            return;
        }
        
        // Validate that player has at least one media file loaded
        bool hasAudio = false, hasVideo = false;
        try {
            hasAudio = newPlayer->isAudioLoaded();
            hasVideo = newPlayer->isVideoLoaded();
        } catch (const std::exception& e) {
            ofLogError("MediaPool") << "[setActivePlayer] CRASH: Exception checking media loaded: " << e.what();
            return;
        }
        
        if (!hasAudio && !hasVideo) {
            ofLogWarning("MediaPool") << "[setActivePlayer] Cannot set active player at index " << index << " - no media loaded";
            return;
        }
        
        // NOTE: With new routing architecture, all players are connected to internal mixers
        // when they're created. The active player is only used for playback control, not routing.
        // No need to connect/disconnect when active player changes.
        if (activePlayer != newPlayer) {
            // Player changed - update active player reference
            activePlayer = newPlayer;
            currentIndex = index;  // Keep currentIndex in sync with activePlayer
            ofLogVerbose("MediaPool") << "[setActivePlayer] Active player changed to index " << index;
        } else {
            // Same player - keep currentIndex in sync
            currentIndex = index;
            ofLogVerbose("MediaPool") << "[setActivePlayer] Same player, updated index to " << index;
        }
        
        // Note: Output connections are now managed externally by ofApp
    } catch (const std::exception& e) {
        ofLogError("MediaPool") << "[setActivePlayer] CRASH: Exception: " << e.what();
    }
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
                return nullptr;
            }
        }
    }
    
    // activePlayer is not in the players vector - it's a dangling pointer!
    // Reset it to prevent crashes
    ofLogWarning("MediaPool") << "getActivePlayer(): activePlayer is a dangling pointer - resetting";
    activePlayer = nullptr;
    return nullptr;
}


void MediaPool::connectActivePlayer(ofxSoundOutput& soundOut, ofxVisualOutput& visualOut) {
    // DEPRECATED: This method is kept for backward compatibility but is no longer used.
    // With the new routing architecture, all players are connected to internal mixers
    // when they're created. External modules should connect to MediaPool via
    // getAudioOutput()/getVideoOutput() instead of calling this method.
    // This method is now a no-op.
    (void)soundOut;  // Suppress unused parameter warning
    (void)visualOut; // Suppress unused parameter warning
}

void MediaPool::disconnectActivePlayer() {
    // DEPRECATED: This method is kept for backward compatibility but is no longer used.
    // With the new routing architecture, players remain connected to internal mixers
    // throughout their lifetime. No need to disconnect when active player changes.
    // This method is now a no-op.
}

void MediaPool::initializeFirstActivePlayer() {
    if (!players.empty() && !activePlayer) {
        // Find first player with valid media loaded
        for (size_t i = 0; i < players.size(); i++) {
            MediaPlayer* player = players[i].get();
            if (player && (player->isAudioLoaded() || player->isVideoLoaded())) {
                setActivePlayer(i);
                ofLogNotice("MediaPool") << "Initialized first player as active (index " << i << ")";
                return;
            }
        }
        ofLogWarning("MediaPool") << "No valid media players found to initialize";
    }
}

//--------------------------------------------------------------
bool MediaPool::playMediaManual(size_t index) {
    std::lock_guard<std::mutex> lock(stateMutex);
    
    if (index >= players.size()) return false;
    MediaPlayer* player = players[index].get();
    if (!player || (!player->isAudioLoaded() && !player->isVideoLoaded())) return false;
    
    // Stop previous player in MONOPHONIC mode
    if (polyphonyMode_ == PolyphonyMode::MONOPHONIC && activePlayer && activePlayer != player) {
        activePlayer->stop();
        ensurePlayerAudioDisconnected(activePlayer);
        ensurePlayerVideoDisconnected(activePlayer);
    }
    
    // Set active player and mode
    activePlayer = player;
    currentIndex = index;
    currentMode.store(PlaybackMode::PLAYING, std::memory_order_relaxed);
    
    // Stop current player if playing (to reset state)
    if (player->isPlaying()) {
        player->stop();
        ensurePlayerAudioDisconnected(player);
        ensurePlayerVideoDisconnected(player);
    }
    
    // Re-enable audio/video (stop() disables them)
    // CRITICAL: Don't enable underlying player loop for LOOP mode
    // The underlying players loop at full media level (0.0-1.0), but we need region-level looping
    // (loopStart to loopEnd based on loopSize). We handle looping manually in update().
    if (player->isAudioLoaded()) player->audioEnabled.set(true);
    if (player->isVideoLoaded()) player->videoEnabled.set(true);
    player->loop.set(false);  // Always disable - looping handled manually at region level
    
    // Play first (this enables the players)
    player->play();
    
    // Then connect to mixers (they check isPlaying(), which is now true)
    ensurePlayerAudioConnected(player);
    ensurePlayerVideoConnected(player);
    
    return true;
}
// Note: Old onStepTrigger methods removed - now using onTrigger() which receives TriggerEvent directly
// stopManualPreview() removed - update() now automatically transitions PLAYING → IDLE when player stops

//--------------------------------------------------------------
// Temporary playback for scrubbing (doesn't change MediaPool mode)
void MediaPool::startTemporaryPlayback(size_t index, float position) {
    std::lock_guard<std::mutex> lock(stateMutex);
    
    if (index >= players.size()) return;
    
    MediaPlayer* player = players[index].get();
    if (!player || (!player->isAudioLoaded() && !player->isVideoLoaded())) return;
    
    // Set active player if needed (but don't change mode)
    if (currentIndex != index || activePlayer != player) {
        setActivePlayer(index);
    }
    
    // Stop current playback
    player->stop();
    ensurePlayerAudioDisconnected(player);
    ensurePlayerVideoDisconnected(player);
    
    // Convert absolute position to relative within region
    float regionStartVal = player->regionStart.get();
    float regionEndVal = player->regionEnd.get();
    
    // Map absolute position to relative position within region using helper function
    float relativePos = mapAbsoluteToRelative(position, regionStartVal, regionEndVal);
    
    // Set position and enable audio/video
    player->startPosition.set(relativePos);
    if (player->isAudioLoaded()) {
        player->audioEnabled.set(true);
    }
    if (player->isVideoLoaded()) {
        player->videoEnabled.set(true);
    }
    
    // Don't set loop for temporary playback - allow scrubbing past loop end
    player->loop.set(false);
    
    // Start playback
    player->play();
    
    // Connect to mixers
    ensurePlayerAudioConnected(player);
    ensurePlayerVideoConnected(player);
}

void MediaPool::stopTemporaryPlayback() {
    std::lock_guard<std::mutex> lock(stateMutex);
    
    if (activePlayer && activePlayer->isPlaying()) {
        activePlayer->stop();
        // For LOOP mode, reset position after stopping (no position memory)
        if (currentPlayStyle == PlayStyle::LOOP) {
            activePlayer->playheadPosition.set(0.0f);
        }
        ensurePlayerAudioDisconnected(activePlayer);
        ensurePlayerVideoDisconnected(activePlayer);
        // Don't change mode - keep it as it was
    }
}

void MediaPool::startScrubbingPlayback(size_t index, float position) {
    std::lock_guard<std::mutex> lock(stateMutex);
    
    if (index >= players.size()) return;
    
    MediaPlayer* player = players[index].get();
    if (!player || (!player->isAudioLoaded() && !player->isVideoLoaded())) return;
    
    // Set active player if needed (but don't change mode)
    if (currentIndex != index || activePlayer != player) {
        setActivePlayer(index);
    }
    
    // Stop current playback
    player->stop();
    ensurePlayerAudioDisconnected(player);
    ensurePlayerVideoDisconnected(player);
    
    // Enable audio/video
    if (player->isAudioLoaded()) {
        player->audioEnabled.set(true);
    }
    if (player->isVideoLoaded()) {
        player->videoEnabled.set(true);
    }
    
    // Don't set loop for scrubbing - allow scrubbing past loop end
    player->loop.set(false);
    
    // Set position using MediaPlayer::setPosition() which handles both audio/video and playheadPosition
    // Do NOT update startPosition (for scrubbing)
    player->setPosition(position);
    
    // Start playback
    player->play();
    
    // Connect to mixers
    ensurePlayerAudioConnected(player);
    ensurePlayerVideoConnected(player);
}

//--------------------------------------------------------------
void MediaPool::stopAllMedia() {
    std::lock_guard<std::mutex> lock(stateMutex);
    
    // Clear lock-free event queue (drain all pending events)
    TriggerEvent dummy;
    while (eventQueue.try_dequeue(dummy)) {
        // Drain queue - events are discarded
    }
    
    // Clear all scheduled stops (gate durations)
    scheduledStops_.clear();
    
    for (auto& player : players) {
        if (player) {
            player->stop();
            // Disconnect stopped player from mixers
            ensurePlayerAudioDisconnected(player.get());
            ensurePlayerVideoDisconnected(player.get());
        }
    }
    
    if (activePlayer) {
        disconnectActivePlayer();
    }
    
    // Transition to IDLE
    currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
}

//--------------------------------------------------------------
void MediaPool::setDataDirectory(const std::string& path) {
    ofLogNotice("MediaPool") << "Setting data directory to: " << path;
    
    // CRITICAL: Lock mutex to prevent GUI/update loop from accessing players during directory change
    // This prevents race conditions where GUI tries to access players while they're being cleared/recreated
    std::lock_guard<std::mutex> lock(stateMutex);
    
    try {
        ofDirectory dir(path);
        if (!dir.exists()) {
            ofLogError("MediaPool") << "Directory does not exist: " << path;
            return;
        }
        
        ofLogNotice("MediaPool") << "✅ Using data directory: " << path;
        
        // CRITICAL: Reset activePlayer BEFORE scanning to prevent dangling pointer access
        // This ensures no code tries to access activePlayer while we're clearing/recreating players
        if (activePlayer) {
            disconnectActivePlayer();
            activePlayer = nullptr;
        }
        
        // Use the existing scanDirectory method to populate audioFiles and videoFiles
        scanDirectory(path);
        
        // Create media players from the scanned files
        mediaPair();
        
        // Only initialize active player if we have valid players
        if (!players.empty()) {
            initializeFirstActivePlayer();
        } else {
            ofLogWarning("MediaPool") << "No valid media players created from directory: " << path;
        }
    } catch (const std::exception& e) {
        ofLogError("MediaPool") << "Exception in setDataDirectory: " << e.what();
        // Ensure activePlayer is reset on error
        if (activePlayer) {
            disconnectActivePlayer();
            activePlayer = nullptr;
        }
    } catch (...) {
        ofLogError("MediaPool") << "Unknown exception in setDataDirectory";
        // Ensure activePlayer is reset on error
        if (activePlayer) {
            disconnectActivePlayer();
            activePlayer = nullptr;
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
    
    ofLogNotice("MediaPool") << "Found " << dir.size() << " files in directory";
    
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
    
    ofLogNotice("MediaPool") << "Found " << audioFiles.size() << " audio files, " << videoFiles.size() << " video files";
}

//--------------------------------------------------------------

//--------------------------------------------------------------
void MediaPool::browseForDirectory() {
    ofLogNotice("MediaPool") << "Opening directory browser...";
    
    // Use OpenFrameworks file dialog to select directory
    ofFileDialogResult result = ofSystemLoadDialog("Select Media Directory", true);
    
    if (result.bSuccess) {
        std::string selectedPath = result.getPath();
        ofLogNotice("MediaPool") << "Selected directory: " << selectedPath;
        setDataDirectory(selectedPath);
    } else {
        ofLogNotice("MediaPool") << "Directory selection cancelled";
    }
}

//--------------------------------------------------------------
// Query methods for state checking
PlaybackMode MediaPool::getCurrentMode() const {
    // Lock-free read (atomic)
    return currentMode.load(std::memory_order_relaxed);
}

bool MediaPool::isPlaying() const {
    // Lock-free read (atomic)
    // Returns true if mode is PLAYING (any playback active)
    return currentMode.load(std::memory_order_relaxed) == PlaybackMode::PLAYING;
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
    ofLogNotice("MediaPool") << "Play style set to: " << (int)style;
    
    // Apply the new style to the currently active player if it's playing
    // CRITICAL: Don't enable underlying player loop for LOOP mode
    // The underlying players loop at full media level (0.0-1.0), but we need region-level looping
    // (loopStart to loopEnd based on loopSize). We handle looping manually in update().
    if (activePlayer) {
        PlaybackMode mode = currentMode.load(std::memory_order_relaxed);
        if (mode == PlaybackMode::PLAYING) {
            // Always disable underlying loop - looping is handled manually at region level
            activePlayer->loop.set(false);
            ofLogNotice("MediaPool") << "Applied play style to active player - looping handled manually at region level";
        }
    }
}

PlayStyle MediaPool::getPlayStyle() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return currentPlayStyle;
}

//--------------------------------------------------------------
void MediaPool::update() {
    // Early exit if we're being destroyed
    if (isDestroying_.load(std::memory_order_acquire)) {
        return;
    }
    
    // Early validation - check if we're in a valid state
    if (players.empty() && activePlayer == nullptr) {
        return;
    }
    
    PlaybackMode mode = currentMode.load(std::memory_order_relaxed);
    
    // Update all playing players (this syncs playheadPosition for scheduled stop checks)
    // Scheduled stops are checked AFTER player updates so position is synced
    for (auto& player : players) {
        if (player && player->isPlaying()) {
            try {
                player->update();  // Updates playhead position and handles playback state
            } catch (const std::exception& e) {
                ofLogError("MediaPool") << "Exception updating player: " << e.what();
            } catch (...) {
                ofLogError("MediaPool") << "Unknown exception updating player";
            }
        }
        // CRITICAL: Also update non-playing videos with initialized frames (position > MIN_REGION_SIZE)
        // This ensures video textures stay fresh and prevents lag when session loads.
        // MediaPlayer::update() will call process() for these videos (line 497) which
        // updates the output buffer for the pull-based video chain.
        // Only update if video is enabled and has a valid position (initialized frame).
        else if (player && player->isVideoLoaded() && player->videoEnabled.get()) {
            float pos = player->getVideoPlayer().getVideoFile().getPosition();
            if (pos > MIN_REGION_SIZE) {
                try {
                    player->update();  // This will call process() for scrubbing preview (MediaPlayer line 497)
                } catch (const std::exception& e) {
                    ofLogError("MediaPool") << "Exception updating non-playing player: " << e.what();
                } catch (...) {
                    ofLogError("MediaPool") << "Unknown exception updating non-playing player";
                }
            }
        }
    }
    
    // Check scheduled stops (gate duration expired) - AFTER player updates so position is synced
    float currentTime = ofGetElapsedTimef();
    for (auto it = scheduledStops_.begin(); it != scheduledStops_.end();) {
        if (currentTime >= it->stopTime) {
            // Gate duration expired - stop the player
            if (it->player && it->player->isPlaying()) {
                it->player->stop();
                // For LOOP mode, reset position after stopping (no position memory)
                if (currentPlayStyle == PlayStyle::LOOP) {
                    it->player->playheadPosition.set(0.0f);
                }
                ensurePlayerAudioDisconnected(it->player);
                ensurePlayerVideoDisconnected(it->player);
                ofLogVerbose("MediaPool") << "[GATE_STOP] Stopped player after gate duration expired";
            }
            it = scheduledStops_.erase(it);
        } else {
            ++it;
        }
    }
    
    // Process event queue FIRST to update activeStepContext before position capture
    try {
        processEventQueue();
    } catch (const std::exception& e) {
        ofLogError("MediaPool") << "Exception processing event queue: " << e.what();
    } catch (...) {
        ofLogError("MediaPool") << "Unknown exception processing event queue";
    }
    
    // CRITICAL: Validate activePlayer is still in players vector before accessing it
    // This check is used throughout update() for all modes
    bool activePlayerIsValid = false;
    bool isPlayerPlaying = false;
    if (activePlayer && currentIndex < players.size()) {
        // Verify activePlayer is actually in the players vector
        for (const auto& player : players) {
            if (player.get() == activePlayer) {
                // Also verify it has media loaded
                if (activePlayer->isAudioLoaded() || activePlayer->isVideoLoaded()) {
                    activePlayerIsValid = true;
                    isPlayerPlaying = activePlayer->isPlaying();
                }
                break;
            }
        }
    }
    
    // Check if we should transition from PLAYING to IDLE
    // Simple logic: if player stopped AND no events in queue, transition to IDLE
    // If events arrive later, processEventQueue() will set mode back to PLAYING
    mode = currentMode.load(std::memory_order_relaxed);
    if (mode == PlaybackMode::PLAYING) {
        if (!activePlayerIsValid) {
            // No active player - module was disabled or disconnected
            currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
            mode = PlaybackMode::IDLE;
            ofLogNotice("MediaPool") << "[STOP] No active player - transitioning to IDLE";
        } else if (!isPlayerPlaying) {
            // Player stopped - check if more triggers are coming
            size_t queuedEvents = eventQueue.size_approx();
            
            if (queuedEvents == 0) {
                // No events in queue - transition to IDLE
                // If triggers arrive later, processEventQueue() will set mode back to PLAYING
                currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
                mode = PlaybackMode::IDLE;
                ofLogVerbose("MediaPool") << "[STOP] Player stopped and no events queued - transitioning to IDLE";
            } else {
                // Events in queue - more triggers coming, stay in PLAYING
                // Only log occasionally to avoid spam
                static int stopFrameCount = 0;
                if (++stopFrameCount % 60 == 0) { // Log every second at 60fps
                    ofLogVerbose("MediaPool") << "[PLAYING] Player stopped between triggers (" 
                                              << queuedEvents << " events queued) - staying in PLAYING";
                }
            }
        }
    }
    
    // Simplified position memory capture - always track position every frame while playing
    
    if (activePlayerIsValid) {
        bool isCurrentlyPlaying = activePlayer->isPlaying();
        
        // ALWAYS check boundaries when player is playing (loop handling, region end, etc.)
        // This is independent of mode or transport state - if it's playing, we need to handle boundaries
        // Boundary checking ensures proper loop behavior in all playback modes
        if (isCurrentlyPlaying) {
            // Get current playhead position (absolute: 0.0-1.0 of entire media)
            float currentPosition = activePlayer->playheadPosition.get();
            float regionStartVal = activePlayer->regionStart.get();
            float regionEndVal = activePlayer->regionEnd.get();
            
            // Calculate loop bounds using helper function
            LoopBounds loopBounds = calculateLoopBounds(activePlayer, currentPlayStyle);
            float effectiveRegionEnd = loopBounds.end;
            
            // Ensure region bounds are valid (for boundary checking)
            if (regionStartVal > regionEndVal) {
                std::swap(regionStartVal, regionEndVal);
            }
            
            // CRITICAL FIX: Convert threshold to absolute time first to avoid scaling issues with long samples
            // REGION_BOUNDARY_THRESHOLD was a fixed normalized value (0.001 = 0.1%), which becomes huge
            // in absolute time for long samples (e.g., 0.001 * 240s = 240ms for a 4-minute sample)
            // Fix: Use a fixed absolute time threshold (1ms) and convert to normalized based on duration
            float duration = activePlayer->getDuration();
            const float THRESHOLD_ABSOLUTE_SECONDS = 0.001f; // 1ms absolute threshold
            float thresholdNormalized = (duration > MIN_DURATION) ? (THRESHOLD_ABSOLUTE_SECONDS / duration) : REGION_BOUNDARY_THRESHOLD;
            
            // Check if playhead has gone below region start (shouldn't happen, but clamp if it does)
            if (currentPosition < regionStartVal - thresholdNormalized) {
                // Playhead went below region start - clamp to region start using helper
                seekPlayerToPosition(activePlayer, regionStartVal);
            }
            
            // Get loop start and end positions from calculated bounds
            float loopStartPos = loopBounds.start;
            float loopEndPos = loopBounds.end;
            
            // CRITICAL: In LOOP play style, handle looping manually
            // We don't enable underlying player loop because it loops at full media level (0.0-1.0),
            // but we need region-level looping (loopStart to loopEnd based on loopSize).
            if (currentPlayStyle == PlayStyle::LOOP) {
                // Check if playhead went below loop start - clamp to loop start
                if (currentPosition < loopStartPos - thresholdNormalized) {
                    seekPlayerToPosition(activePlayer, loopStartPos);
                    currentPosition = loopStartPos;  // Update for subsequent checks
                }
                
                // Check if playhead reached loop end - loop back to loop start
                if (currentPosition >= loopEndPos - thresholdNormalized) {
                    seekPlayerToPosition(activePlayer, loopStartPos);
                    // Don't call handleRegionEnd() - we're looping, not stopping
                    // Skip region end handling for LOOP mode
                }
            } else {
                // For ONCE and NEXT modes: Check if playhead has reached region end
                bool reachedRegionEnd = (currentPosition >= effectiveRegionEnd - thresholdNormalized);
                if (reachedRegionEnd) {
                    handleRegionEnd(activePlayer, currentPosition, effectiveRegionEnd, 
                                   loopStartPos, currentPlayStyle);
                }
            }
            
            // Position memory: Only for NEXT mode, captured before stop() in processEventQueue()
            // LOOP mode: Position is reset to 0 when stopping (no position memory)
            // ONCE mode: Position is NOT preserved (starts from startPosition each time)
        } else if (!isCurrentlyPlaying) {
            // Player stopped - transitions are handled by the early check above
            // No additional action needed here
        }
    } else if (activePlayer && !activePlayerIsValid) {
        // activePlayer is no longer valid - reset it
        activePlayer = nullptr;
        currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
    }
    
    // Check for end-of-playback
    mode = currentMode.load(std::memory_order_relaxed);
    if (mode == PlaybackMode::PLAYING && activePlayerIsValid && activePlayer) {
        if (!activePlayer->isPlaying() && !activePlayer->loop.get()) {
            onPlaybackEnd();
        }
    }
    
    // Update player connections based on current playing state
    // This ensures connections stay in sync if players stop/start outside of our control
    try {
        updatePlayerConnections();
    } catch (const std::exception& e) {
        ofLogError("MediaPool") << "Exception in updatePlayerConnections(): " << e.what();
    } catch (...) {
        ofLogError("MediaPool") << "Unknown exception in updatePlayerConnections()";
    }
}

//--------------------------------------------------------------
void MediaPool::processEventQueue() {
    // Don't process events if module is disabled
    if (!isEnabled()) {
        return;
    }
    
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
        int noteValue = (noteIt != event.parameters.end() ? (int)noteIt->second : -1);
        ofLogVerbose("MediaPool") << "[TRIGGER] Step " << event.step << ", Note: " << noteValue
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
                // For LOOP mode, reset position after stopping (no position memory)
                if (currentPlayStyle == PlayStyle::LOOP) {
                    activePlayer->playheadPosition.set(0.0f);
                }
                // Disconnect stopped player from mixers
                ensurePlayerAudioDisconnected(activePlayer);
                ensurePlayerVideoDisconnected(activePlayer);
            }
            continue;  // Process next event
        }
        
        // Validate media index
        if (mediaIndex >= (int)players.size()) {
            ofLogWarning("MediaPool") << "[TRIGGER] INVALID mediaIndex: " << mediaIndex 
                                      << " >= players.size(): " << players.size() 
                                      << " - skipping event for step " << event.step;
            continue;  // Process next event
        }
        
        // Get the media player for this step
        MediaPlayer* player = players[mediaIndex].get();
        if (!player) {
            ofLogWarning("MediaPool") << "[TRIGGER] Player at index " << mediaIndex 
                                      << " is NULL - skipping event for step " << event.step;
            continue;  // Process next event
        }
        
        // Update activeStepContext and apply position memory (GUI thread)
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (event.step >= 0) {
                lastTriggeredStep = event.step;
                activeStepContext.step = event.step;
                activeStepContext.mediaIndex = mediaIndex;
                activeStepContext.triggerTime = ofGetElapsedTimef();
                // Track last trigger time (kept for potential future use, but not needed for simplified logic)
                lastTriggerTime = ofGetElapsedTimef();
            }
            
            // Position memory: For NEXT mode only, capture position before stopping
            // This allows NEXT mode to continue from where it left off
            if (event.parameters.find("position") == event.parameters.end()) {
                // For NEXT mode, capture position from player if playing, otherwise use playheadPosition
                // For ONCE/LOOP modes, use startPosition (GUI-set value) - no position memory
                if (currentPlayStyle == PlayStyle::NEXT) {
                    // Capture position BEFORE any stop() calls happen
                    float rememberedPosition = 0.0f;
                    if (player->isPlaying()) {
                        rememberedPosition = player->captureCurrentPosition();
                    } else {
                        // Player already stopped - use playheadPosition parameter
                        rememberedPosition = player->playheadPosition.get();
                    }
                    
                    if (rememberedPosition > POSITION_THRESHOLD) {
                        // Convert absolute position to relative position for startPosition
                        float regionStart = player->regionStart.get();
                        float regionEnd = player->regionEnd.get();
                        float relativePos = mapAbsoluteToRelative(rememberedPosition, regionStart, regionEnd);
                        
                        // Reset to 0.0 if position is at the end or too small
                        if (relativePos >= END_POSITION_THRESHOLD || relativePos < POSITION_THRESHOLD) {
                            relativePos = 0.0f;
                            player->playheadPosition.set(0.0f);
                        } else {
                            // Preserve the position in playheadPosition for next time
                            player->playheadPosition.set(rememberedPosition);
                        }
                        
                        event.parameters["position"] = relativePos;
                        ofLogNotice("MediaPool") << "[TRIGGER] NEXT mode: Using position memory " 
                                                 << rememberedPosition << " -> relative " << relativePos;
                    }
                }
            }
        }
        
        // Set active player if changed - protect with mutex
        bool playerChanged = false;
        MediaPlayer* previousActivePlayer = nullptr;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            playerChanged = (currentIndex != mediaIndex || activePlayer != player);
            previousActivePlayer = activePlayer;
        }
        
        if (playerChanged) {
            // MONOPHONIC BEHAVIOR: Stop previous active player before switching to new one
            // POLYPHONIC BEHAVIOR: Allow previous player to continue playing
            if (polyphonyMode_ == PolyphonyMode::MONOPHONIC) {
                if (previousActivePlayer && previousActivePlayer != player && previousActivePlayer->isPlaying()) {
                    previousActivePlayer->stop();
                    // For LOOP mode, reset position after stopping (no position memory)
                    if (currentPlayStyle == PlayStyle::LOOP) {
                        previousActivePlayer->playheadPosition.set(0.0f);
                    }
                    // Disconnect stopped player from mixer
                    ensurePlayerVideoDisconnected(previousActivePlayer);
                    ofLogVerbose("MediaPool") << "[MONO] Stopped previous active player before starting new player (index " << mediaIndex << ")";
                }
            } else {
                // POLYPHONIC: Previous player continues playing - no stop needed
                ofLogVerbose("MediaPool") << "[POLY] Allowing previous player to continue while starting new player (index " << mediaIndex << ")";
            }
            setActivePlayer(mediaIndex);  // setActivePlayer now holds its own lock
        } else {
            // Same player being retriggered
            if (polyphonyMode_ == PolyphonyMode::MONOPHONIC) {
                // MONO: Stop it first to restart from new position (prevents layering)
                if (player && player->isPlaying()) {
                    player->stop();
                    // For LOOP mode, reset position after stopping (no position memory)
                    if (currentPlayStyle == PlayStyle::LOOP) {
                        player->playheadPosition.set(0.0f);
                    }
                    // Disconnect stopped player from mixers (will reconnect when it starts again)
                    ensurePlayerAudioDisconnected(player);
                    ensurePlayerVideoDisconnected(player);
                    ofLogVerbose("MediaPool") << "[MONO] Stopped same player (index " << mediaIndex 
                                              << ") before retriggering from new position";
                }
            } else {
                // POLY: Allow layering - same player can be triggered multiple times simultaneously
                // (Each trigger creates a new playback instance if the player supports it)
                // For now, we'll restart from new position but allow multiple instances if player supports it
                ofLogVerbose("MediaPool") << "[POLY] Retriggering same player (index " << mediaIndex 
                                          << ") - may layer if player supports it";
            }
        }
        
        // Apply all parameters from event using helper method
        applyEventParameters(player, event, paramDescriptors);
        
        // CRITICAL FIX: Cancel any existing scheduled stop for this player
        // This prevents scheduled stops from interfering with retriggers when
        // step length equals pattern stepCount (e.g., both = 16).
        // When a pattern loops back to the same step, the new trigger should
        // take precedence over the old scheduled stop.
        scheduledStops_.erase(
            std::remove_if(scheduledStops_.begin(), scheduledStops_.end(),
                [player](const ScheduledStop& stop) {
                    return stop.player == player;
                }),
            scheduledStops_.end()
        );
        
        // Use duration directly from TriggerEvent
        float stepDurationSeconds = event.duration;
        
        // Start playback (respects loop state set by applyEventParameters)
        player->play();
        
        // Schedule stop after gate duration expires
        // This allows LOOP mode to work correctly - player loops within bounds until gate expires
        if (stepDurationSeconds > 0.0f) {
            scheduledStops_.push_back({player, ofGetElapsedTimef() + stepDurationSeconds});
        }
        
        // Connect player to mixers now that it's playing
        ensurePlayerAudioConnected(player);
        ensurePlayerVideoConnected(player);
        
        // Transition to PLAYING if playback started
        if (player->isPlaying()) {
            currentMode.store(PlaybackMode::PLAYING, std::memory_order_relaxed);
            // (in case step was -1 and we didn't update it above)
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                lastTriggerTime = ofGetElapsedTimef();
            }
        } else {
            ofLogWarning("MediaPool") << "play() called but player is not playing - staying in IDLE mode";
        }
        
        // Event processed, continue to next event in queue
    }
    
    // Log warning if queue is backing up (but only occasionally to avoid spam)
    static int warningFrameCount = 0;
    if (++warningFrameCount % 300 == 0) { // Every 5 seconds at 60fps
        size_t remainingQueueSize = eventQueue.size_approx();
        if (remainingQueueSize > 100) {
            ofLogWarning("MediaPool") << "Event queue backing up - " << remainingQueueSize 
                                      << " events remaining (processed " << eventsProcessed 
                                      << " this frame, maxEventsPerFrame: " << maxEventsPerFrame << ")";
        }
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
    if (mode != PlaybackMode::PLAYING) return;
    
    // Transitions to IDLE are handled by update() when player stops and queue is empty
    // This function only handles play style behavior (ONCE, LOOP, NEXT)
    
    switch (currentPlayStyle) {
        case PlayStyle::ONCE:
            // Stop the current player - update() will transition to IDLE when it detects player stopped
            if (activePlayer) {
                // ONCE mode: Position is NOT preserved (starts from startPosition each time)
                activePlayer->stop();
            }
            break;
        case PlayStyle::LOOP:
            // Already handled by loop=true
            break;
        case PlayStyle::NEXT:
            if (players.size() > 1) {
                size_t nextIndex = (currentIndex + 1) % players.size();
                ofLogNotice("MediaPool") << "Playing next media: " << nextIndex;
                
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
                    
                    ofLogNotice("MediaPool") << "Started next media " << nextIndex << " (PLAYING mode)";
                } else {
                    // No next player available - stop current player
                    // update() will transition to IDLE when it detects player stopped
                    if (activePlayer) {
                        // NEXT mode: Position is preserved before stop() in processEventQueue()
                        activePlayer->stop();
                    }
                }
            } else {
                // Only one player (no next player) - stop current player
                // update() will transition to IDLE when it detects player stopped
                if (activePlayer) {
                    // NEXT mode: Position is preserved before stop() in processEventQueue()
                    activePlayer->stop();
                }
            }
            break;
    }
}


//--------------------------------------------------------------
// Module interface implementation
//--------------------------------------------------------------
std::string MediaPool::getName() const {
    return "MediaPool";
}

// getTypeName() uses default implementation from Module base class (returns getName())

void MediaPool::setEnabled(bool enabled) {
    Module::setEnabled(enabled);
    
    // Stop all media when disabled
    if (!enabled) {
        stopAllMedia();
    }
}

ofJson MediaPool::toJson() const {
    ofJson json;
    
    // Save active player index
    json["activePlayerIndex"] = currentIndex;
    
    // Save play style
    json["playStyle"] = static_cast<int>(currentPlayStyle);
    
    // Save polyphony mode
    json["polyphonyMode"] = (polyphonyMode_ == PolyphonyMode::POLYPHONIC) ? 1.0f : 0.0f;
    
    // Save all players with their file paths and parameters
    // File paths are saved as-is (should already be absolute from file system)
    ofJson playersArray = ofJson::array();
    std::lock_guard<std::mutex> lock(stateMutex);
    
    // Log how many players we're serializing
    size_t validPlayers = 0;
    for (size_t i = 0; i < players.size(); i++) {
        if (players[i].get()) {
            validPlayers++;
        }
    }
    ofLogNotice("MediaPool") << "Serializing " << validPlayers << " players to session (total players: " << players.size() << ")";
    
    for (size_t i = 0; i < players.size(); i++) {
        auto player = players[i].get();
        if (player) {
            ofJson playerJson;
            std::string audioPath = player->getAudioFilePath();
            std::string videoPath = player->getVideoFilePath();
            
            // Save file paths (empty string if not set)
            playerJson["audioFile"] = audioPath;
            playerJson["videoFile"] = videoPath;
            
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
            
            playerJson["parameters"] = paramsJson;
            playersArray.push_back(playerJson);
        }
    }
    json["players"] = playersArray;
    
    // Log final count
    if (playersArray.size() != validPlayers) {
        ofLogWarning("MediaPool") << "Player count mismatch: " << validPlayers << " valid players, " 
                                   << playersArray.size() << " serialized";
    } else {
        ofLogNotice("MediaPool") << "Successfully serialized " << playersArray.size() << " players to session";
    }
    
    return json;
}

void MediaPool::fromJson(const ofJson& json) {
    // Load play style first (before loading players)
    if (json.contains("playStyle")) {
        int styleInt = json["playStyle"];
        if (styleInt >= 0 && styleInt <= 2) {
            setPlayStyle(static_cast<PlayStyle>(styleInt));
        }
    }
    
    // Load polyphony mode (default to MONOPHONIC for backward compatibility)
    if (json.contains("polyphonyMode")) {
        float modeValue = json["polyphonyMode"];
        polyphonyMode_ = (modeValue >= 0.5f) ? PolyphonyMode::POLYPHONIC : PolyphonyMode::MONOPHONIC;
        ofLogNotice("MediaPool") << "Loaded polyphony mode: " 
                                  << (polyphonyMode_ == PolyphonyMode::POLYPHONIC ? "POLYPHONIC" : "MONOPHONIC");
    } else {
        polyphonyMode_ = PolyphonyMode::MONOPHONIC;  // Default for old sessions
    }
    
    // Load players from saved file paths
    if (json.contains("players") && json["players"].is_array()) {
        std::lock_guard<std::mutex> lock(stateMutex);
        
        auto playersArray = json["players"];
        ofLogNotice("MediaPool") << "Loading " << playersArray.size() << " players from session...";
        
        // Clear existing players
        if (activePlayer) {
            disconnectActivePlayer();
            activePlayer = nullptr;
        }
        clear();
        
        // Set flag to defer media loading during session restore to prevent blocking
        deferMediaLoading_ = true;
        
        deferredPlayerParams_.clear();
        deferredPlayerParams_.reserve(playersArray.size());
        
        size_t filesAdded = 0;
        size_t paramsStored = 0;
        
        // First pass: Add file paths directly to lists (skip addMediaFile to avoid file existence checks)
        for (int i = 0; i < playersArray.size(); i++) {
            auto playerJson = playersArray[i];
            std::string audioFile = playerJson.value("audioFile", "");
            std::string videoFile = playerJson.value("videoFile", "");
            
            // Skip if both files are missing
            if (audioFile.empty() && videoFile.empty()) {
                ofLogWarning("MediaPool") << "Skipping player " << i << ": no audio or video file specified";
                continue;
            }
            
            // During session restore, just add paths directly to lists without checking existence
            // File existence will be checked later when mediaPair() is called
            if (!audioFile.empty()) {
                // Check if already in list (avoid duplicates)
                bool alreadyExists = false;
                for (const auto& existingFile : audioFiles) {
                    if (existingFile == audioFile) {
                        alreadyExists = true;
                        break;
                    }
                }
                if (!alreadyExists) {
                    audioFiles.push_back(audioFile);
                    filesAdded++;
                    ofLogNotice("MediaPool") << "Added audio file path: " << audioFile;
                }
            }
            
            if (!videoFile.empty()) {
                // Check if already in list (avoid duplicates)
                bool alreadyExists = false;
                for (const auto& existingFile : videoFiles) {
                    if (existingFile == videoFile) {
                        alreadyExists = true;
                        break;
                    }
                }
                if (!alreadyExists) {
                    videoFiles.push_back(videoFile);
                    filesAdded++;
                    ofLogNotice("MediaPool") << "Added video file path: " << videoFile;
                }
            }
            
            // Store player parameters for loading after mediaPair() creates players
            // We'll match by file paths since indices might change
            if (playerJson.contains("parameters")) {
                deferredPlayerParams_.push_back({
                    audioFile,
                    videoFile,
                    playerJson["parameters"]
                });
                paramsStored++;
            } else {
                // Still store entry even without parameters (for file matching)
                deferredPlayerParams_.push_back({
                    audioFile,
                    videoFile,
                    ofJson::object()  // Empty params object
                });
            }
        }
        
        ofLogNotice("MediaPool") << "Session restore: Added " << filesAdded << " file paths, "
                                 << "stored " << paramsStored << " parameter sets. "
                                 << "Deferred loading flag set. Players will be created in completeRestore().";
        
        // Store active player index for restoration after players are created
        if (json.contains("activePlayerIndex")) {
            deferredActivePlayerIndex_ = json["activePlayerIndex"];
            ofLogNotice("MediaPool") << "Active player index to restore: " << deferredActivePlayerIndex_;
        } else {
            deferredActivePlayerIndex_ = 0;
        }
        
        // DON'T try to load parameters here - players don't exist yet!
        // Parameters will be loaded in completeRestore() after mediaPair() creates players
        
        // Keep defer flag true - media loading will happen later in completeRestore()
        // Don't call mediaPair() here - it will be called by SessionManager after session load completes
        // deferMediaLoading_ remains true until completeRestore() is called
        // This prevents blocking during session restore
    }
    // Legacy support: If no players array but directory exists, use directory-based loading
    // This handles old session files that only saved the directory path
    else if (json.contains("directory")) {
        ofLogNotice("MediaPool") << "Loading from legacy directory-based format";
        std::string dir = json["directory"];
        if (!dir.empty() && ofDirectory(dir).exists()) {
            setDataDirectory(dir);
            
            // Restore active player index
            if (json.contains("activePlayerIndex")) {
                int index = json["activePlayerIndex"];
                if (index >= 0 && index < (int)players.size()) {
                    setCurrentIndex(index);
                }
            }
        } else {
            ofLogWarning("MediaPool") << "Legacy directory not found: " << dir;
        }
    }
}

ModuleType MediaPool::getType() const {
    return ModuleType::INSTRUMENT;
}

//--------------------------------------------------------------
bool MediaPool::hasCapability(ModuleCapability capability) const {
    switch (capability) {
        case ModuleCapability::ACCEPTS_FILE_DROP:
        case ModuleCapability::ACCEPTS_TRIGGER_EVENTS:
            return true;
        default:
            return false;
    }
}

//--------------------------------------------------------------
std::vector<ModuleCapability> MediaPool::getCapabilities() const {
    return {
        ModuleCapability::ACCEPTS_FILE_DROP,
        ModuleCapability::ACCEPTS_TRIGGER_EVENTS
    };
}

//--------------------------------------------------------------
// REMOVED: getIndexRange() - use parameter system instead
// Query getParameters() for "index" parameter's max value

//--------------------------------------------------------------
Module::ModuleMetadata MediaPool::getMetadata() const {
    Module::ModuleMetadata metadata;
    metadata.typeName = "MediaPool";
    metadata.eventNames = {"onTrigger"};  // Event it accepts (not emits)
    metadata.parameterNames = {"position"};
    metadata.parameterDisplayNames["position"] = "Position";
    return metadata;
}

std::vector<ParameterDescriptor> MediaPool::getParameters() const {
    std::vector<ParameterDescriptor> params;
    
    // MediaPool parameters that can be controlled by TrackerSequencer
    // These are the parameters that TrackerSequencer sends in trigger events
    // MediaPool maps these to MediaPlayer parameters
    
    // Core playback parameters
    // "index" parameter with dynamic range based on number of players
    int maxIndex = static_cast<int>(getNumPlayers()) - 1;
    if (maxIndex < 0) maxIndex = 0;  // Safety: at least 0
    params.push_back(ParameterDescriptor("index", ParameterType::INT, 0.0f, static_cast<float>(maxIndex), 0.0f, "Media Index"));
    // Note: "note" is still handled in trigger events for backward compatibility, but not exposed as a parameter
    // to avoid conflicts with TrackerSequencer's internal "note" parameter (musical notes)
    params.push_back(ParameterDescriptor("position", ParameterType::FLOAT, 0.0f, 1.0f, 0.0f, "Position"));
    params.push_back(ParameterDescriptor("speed", ParameterType::FLOAT, -10.0f, 10.0f, 1.0f, "Speed"));
    params.push_back(ParameterDescriptor("volume", ParameterType::FLOAT, 0.0f, 2.0f, 1.0f, "Volume"));
    
    // Region and loop control
    params.push_back(ParameterDescriptor("loopSize", ParameterType::FLOAT, 0.0f, 10.0f, 1.0f, "Loop Size (seconds)"));
    params.push_back(ParameterDescriptor("regionStart", ParameterType::FLOAT, 0.0f, 1.0f, 0.0f, "Region Start"));
    params.push_back(ParameterDescriptor("regionEnd", ParameterType::FLOAT, 0.0f, 1.0f, 1.0f, "Region End"));
    
    // Polyphony control
    params.push_back(ParameterDescriptor("polyphonyMode", ParameterType::INT, 0.0f, 1.0f, 0.0f, "Polyphony Mode"));
    
    return params;
}

void MediaPool::setParameter(const std::string& paramName, float value, bool notify) {
    // Handle polyphonyMode parameter (module-level, not player-level)
    if (paramName == "polyphonyMode") {
        std::lock_guard<std::mutex> lock(stateMutex);
        PolyphonyMode oldMode = polyphonyMode_;
        polyphonyMode_ = (value >= 0.5f) ? PolyphonyMode::POLYPHONIC : PolyphonyMode::MONOPHONIC;
        
        if (polyphonyMode_ != oldMode) {
            // Mode changed - handle transition
            // NOTE: We don't stop all non-active players here because:
            // 1. It breaks video mixing (stopped players output transparent but remain connected)
            // 2. The original mono behavior only stopped the previous active player when switching
            // 3. Players will be properly stopped when switching in processEventQueue() and playMediaManual()
            if (polyphonyMode_ == PolyphonyMode::MONOPHONIC) {
                ofLogNotice("MediaPool") << "[POLYPHONY] Switched to MONOPHONIC mode - will stop previous player on next switch";
            } else {
                // Switching to POLY: no action needed - allow all to play
                ofLogNotice("MediaPool") << "[POLYPHONY] Switched to POLYPHONIC mode - multiple players can play simultaneously";
            }
            
            if (notify && parameterChangeCallback) {
                parameterChangeCallback(paramName, value);
            }
        }
        return;
    }
    
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
            float maxAllowed = (duration > MIN_DURATION) ? duration : 10.0f;
            float clampedVal = std::max(MIN_LOOP_SIZE, std::min(maxAllowed, val));
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
                // Clamp to valid relative range using helper (handles ONCE mode clamping)
                float relativePos = clampPositionForPlayback(val, currentPlayStyle);
                activePlayer->startPosition.set(relativePos);
                
                // Update playheadPosition for UI display (map to absolute using helper function)
                float regionStartVal = activePlayer->regionStart.get();
                float regionEndVal = activePlayer->regionEnd.get();
                float absolutePos = mapRelativeToAbsolute(relativePos, regionStartVal, regionEndVal);
                
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

float MediaPool::getParameter(const std::string& paramName) const {
    // Handle "position" parameter - read from active player
    if (paramName == "position") {
        // getActivePlayer() is not const, but we're only reading, so use const_cast
        auto* player = const_cast<MediaPool*>(this)->getActivePlayer();
        if (player) {
            return player->startPosition.get();
        }
        return 0.0f;
    }
    
    // Handle "polyphonyMode" - module-level parameter
    if (paramName == "polyphonyMode") {
        std::lock_guard<std::mutex> lock(stateMutex);
        return (polyphonyMode_ == PolyphonyMode::POLYPHONIC) ? 1.0f : 0.0f;
    }
    
    // For other parameters, read from active player
    auto* player = const_cast<MediaPool*>(this)->getActivePlayer();
    if (!player) {
        // No active player - return default value from parameter descriptor
        auto paramDescriptors = getParameters();
        for (const auto& param : paramDescriptors) {
            if (param.name == paramName) {
                return param.defaultValue;
            }
        }
        return 0.0f;
    }
    
    // Map parameter names to MediaPlayer parameters
    if (paramName == "speed") {
        return player->speed.get();
    } else if (paramName == "volume") {
        return player->volume.get();
    } else if (paramName == "loopSize") {
        return player->loopSize.get();
    } else if (paramName == "regionStart" || paramName == "loopStart") {
        return player->regionStart.get();
    } else if (paramName == "regionEnd" || paramName == "loopEnd") {
        return player->regionEnd.get();
    } else if (paramName == "note") {
        // Note parameter represents the media index
        // This is tricky - we'd need to track which player corresponds to which index
        // For now, return 0.0f as it's not a continuous parameter
        return 0.0f;
    }
    
    // Unknown parameter - return default
    return Module::getParameter(paramName);
}

void MediaPool::onTrigger(TriggerEvent& event) {
    // Don't process triggers if module is disabled
    if (!isEnabled()) return;
    
    // Increment diagnostic counter (atomic, safe from audio thread)
    onTriggerCallCount_.fetch_add(1, std::memory_order_relaxed);
    
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
        // Use atomic counter to track dropped events (increment a separate counter)
    }
}

//--------------------------------------------------------------
// Position scan mode control
PolyphonyMode MediaPool::getPolyphonyMode() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return polyphonyMode_;
}

//--------------------------------------------------------------
// Module routing interface - expose stable audio/video outputs via internal mixers
ofxSoundObject* MediaPool::getAudioOutput() const {
    return const_cast<ofxSoundMixer*>(&internalAudioMixer_);
}

ofxVisualObject* MediaPool::getVideoOutput() const {
    return const_cast<ofxVideoMixer*>(&internalVideoMixer_);
}

//--------------------------------------------------------------
// Port-based routing interface (Phase 1)
std::vector<Port> MediaPool::getInputPorts() const {
    return {
        Port("trigger_in", PortType::EVENT_IN, false, "Trigger Input")
    };
}

std::vector<Port> MediaPool::getOutputPorts() const {
    return {
        Port("audio_out", PortType::AUDIO_OUT, false, "Audio Output", 
             const_cast<void*>(static_cast<const void*>(&internalAudioMixer_))),
        Port("video_out", PortType::VIDEO_OUT, false, "Video Output",
             const_cast<void*>(static_cast<const void*>(&internalVideoMixer_)))
    };
}

//--------------------------------------------------------------
// Internal connection management - connect/disconnect players to internal mixers
void MediaPool::connectPlayerToInternalMixers(MediaPlayer* player) {
    if (!player) return;
    
    try {
        // Connect audio if available
        if (player->isAudioLoaded()) {
            player->getAudioPlayer().connectTo(internalAudioMixer_);
        }
        
        // Connect video if available
        if (player->isVideoLoaded()) {
            player->getVideoPlayer().connectTo(internalVideoMixer_);
        }
        
        ofLogNotice("MediaPool") << "Connected player to internal mixers";
    } catch (const std::exception& e) {
        ofLogError("MediaPool") << "Failed to connect player to internal mixers: " << e.what();
    }
}

void MediaPool::disconnectPlayerFromInternalMixers(MediaPlayer* player) {
    if (!player) return;
    
    try {
        // Disconnect audio if connected
        if (player->isAudioLoaded()) {
            player->getAudioPlayer().disconnect();
        }
        
        // Disconnect video if connected
        if (player->isVideoLoaded()) {
            player->getVideoPlayer().disconnect();
        }
        
        ofLogNotice("MediaPool") << "Disconnected player from internal mixers";
    } catch (const std::exception& e) {
        ofLogError("MediaPool") << "Failed to disconnect player from internal mixers: " << e.what();
    }
}

//--------------------------------------------------------------
// Dynamic connection management: connect/disconnect players based on playing state
void MediaPool::ensurePlayerAudioConnected(MediaPlayer* player) {
    if (!player || !player->isAudioLoaded()) return;
    
    // Check if already connected (use find to avoid creating map entry)
    auto it = playerAudioConnected.find(player);
    if (it != playerAudioConnected.end() && it->second) return;
    
    // Connect if audio is enabled and playing
    if (player->audioEnabled.get() && player->isPlaying()) {
        try {
            player->getAudioPlayer().connectTo(internalAudioMixer_);
            playerAudioConnected[player] = true;
            ofLogVerbose("MediaPool") << "[CONNECTION] Connected player audio to mixer (playing: " 
                                      << player->isPlaying() << ", audioEnabled: " << player->audioEnabled.get() << ")";
        } catch (const std::exception& e) {
            ofLogError("MediaPool") << "Failed to connect player audio: " << e.what();
        }
    }
}

void MediaPool::ensurePlayerAudioDisconnected(MediaPlayer* player) {
    if (!player) return;
    
    // Check if connected (use find to avoid creating map entry)
    auto it = playerAudioConnected.find(player);
    if (it == playerAudioConnected.end() || !it->second) return;
    
    // Disconnect if audio is disabled or stopped
    if (!player->audioEnabled.get() || !player->isPlaying()) {
        try {
            if (player->isAudioLoaded()) {
                player->getAudioPlayer().disconnect();
            }
            playerAudioConnected[player] = false;
            ofLogVerbose("MediaPool") << "[CONNECTION] Disconnected player audio from mixer (playing: " 
                                      << player->isPlaying() << ", audioEnabled: " << player->audioEnabled.get() << ")";
        } catch (const std::exception& e) {
            ofLogError("MediaPool") << "Failed to disconnect player audio: " << e.what();
        }
    }
}

void MediaPool::ensurePlayerVideoConnected(MediaPlayer* player) {
    if (!player || !player->isVideoLoaded()) return;
    
    // Check if already connected (use find to avoid creating map entry)
    auto it = playerVideoConnected.find(player);
    if (it != playerVideoConnected.end() && it->second) return;
    
    // Connect if video is enabled and playing
    if (player->videoEnabled.get() && player->isPlaying()) {
        try {
            player->getVideoPlayer().connectTo(internalVideoMixer_);
            playerVideoConnected[player] = true;
            ofLogVerbose("MediaPool") << "[CONNECTION] Connected player video to mixer (playing: " 
                                      << player->isPlaying() << ", videoEnabled: " << player->videoEnabled.get() << ")";
        } catch (const std::exception& e) {
            ofLogError("MediaPool") << "Failed to connect player video: " << e.what();
        }
    }
}

void MediaPool::ensurePlayerVideoDisconnected(MediaPlayer* player) {
    if (!player) return;
    
    // Check if connected (use find to avoid creating map entry)
    auto it = playerVideoConnected.find(player);
    if (it == playerVideoConnected.end() || !it->second) return;
    
    // Disconnect if video is disabled or stopped
    if (!player->videoEnabled.get() || !player->isPlaying()) {
        try {
            if (player->isVideoLoaded()) {
                player->getVideoPlayer().disconnect();
            }
            playerVideoConnected[player] = false;
            ofLogVerbose("MediaPool") << "[CONNECTION] Disconnected player video from mixer (playing: " 
                                      << player->isPlaying() << ", videoEnabled: " << player->videoEnabled.get() << ")";
        } catch (const std::exception& e) {
            ofLogError("MediaPool") << "Failed to disconnect player video: " << e.what();
        }
    }
}

void MediaPool::updatePlayerConnections() {
    // Update all player connections based on current state
    for (auto& player : players) {
        if (!player) continue;
        
        // Helper lambda to update a single connection type
        auto updateConnection = [this, &player](bool isLoaded, bool isEnabled, 
                                                std::map<MediaPlayer*, bool>& connectionMap,
                                                void (MediaPool::*ensureConnected)(MediaPlayer*),
                                                void (MediaPool::*ensureDisconnected)(MediaPlayer*)) {
            bool shouldBeConnected = isLoaded && isEnabled && player->isPlaying();
            auto it = connectionMap.find(player.get());
            bool isConnected = (it != connectionMap.end() && it->second);
            
            if (shouldBeConnected && !isConnected) {
                (this->*ensureConnected)(player.get());
            } else if (!shouldBeConnected && isConnected) {
                (this->*ensureDisconnected)(player.get());
            }
        };
        
        // Update audio connections
        updateConnection(player->isAudioLoaded(), 
                        player->audioEnabled.get(),
                        playerAudioConnected,
                        &MediaPool::ensurePlayerAudioConnected,
                        &MediaPool::ensurePlayerAudioDisconnected);
        
        // Update video connections
        updateConnection(player->isVideoLoaded(),
                        player->videoEnabled.get(),
                        playerVideoConnected,
                        &MediaPool::ensurePlayerVideoConnected,
                        &MediaPool::ensurePlayerVideoDisconnected);
    }
}

//--------------------------------------------------------------
// Helper functions for position mapping and loop calculations
//--------------------------------------------------------------
float MediaPool::mapRelativeToAbsolute(float relativePos, float regionStart, float regionEnd) const {
    float regionSize = regionEnd - regionStart;
    if (regionSize > MIN_REGION_SIZE) {
        return regionStart + relativePos * regionSize;
    } else {
        // Invalid or collapsed region - clamp relative position to valid range
        return std::max(0.0f, std::min(1.0f, relativePos));
    }
}

float MediaPool::mapAbsoluteToRelative(float absolutePos, float regionStart, float regionEnd) const {
    float regionSize = regionEnd - regionStart;
    if (regionSize > MIN_REGION_SIZE) {
        // Clamp absolute position to region bounds first
        float clampedAbsolute = std::max(regionStart, std::min(regionEnd, absolutePos));
        return (clampedAbsolute - regionStart) / regionSize;
    } else {
        // Invalid or collapsed region - return absolute position clamped to 0-1
        return std::max(0.0f, std::min(1.0f, absolutePos));
    }
}

MediaPool::LoopBounds MediaPool::calculateLoopBounds(MediaPlayer* player, PlayStyle playStyle) const {
    if (!player) {
        return {0.0f, 1.0f};
    }
    
    float regionStart = player->regionStart.get();
    float regionEnd = player->regionEnd.get();
    
    // Ensure region bounds are valid
    if (regionStart > regionEnd) {
        std::swap(regionStart, regionEnd);
    }
    
    // Calculate loop start from relative startPosition
    float relativeStart = player->startPosition.get();
    float loopStart = mapRelativeToAbsolute(relativeStart, regionStart, regionEnd);
    float loopEnd = regionEnd;
    
    // Calculate loop end based on loopSize when in LOOP play style
    if (playStyle == PlayStyle::LOOP) {
        float loopSizeSeconds = player->loopSize.get();
        if (loopSizeSeconds > MIN_LOOP_SIZE) {
            float duration = player->getDuration();
            if (duration > MIN_DURATION) {
                // CRITICAL FIX: Work in absolute time (seconds) first to preserve precision
                // Converting small time values to normalized positions loses precision for long samples
                // Convert normalized positions to absolute time
                float loopStartSeconds = loopStart * duration;
                float regionEndSeconds = regionEnd * duration;
                
                // Calculate loop end in absolute time
                float calculatedLoopEndSeconds = loopStartSeconds + loopSizeSeconds;
                
                // Clamp to region end and media duration
                float clampedLoopEndSeconds = std::min(regionEndSeconds, std::min(duration, calculatedLoopEndSeconds));
                
                // Convert back to normalized position (0-1)
                loopEnd = clampedLoopEndSeconds / duration;
            }
        }
    }
    
    return {loopStart, loopEnd};
}

void MediaPool::seekPlayerToPosition(MediaPlayer* player, float position) const {
    if (!player) return;
    
    // Use MediaPlayer::setPosition() which handles both audio and video internally
    player->setPosition(position);
}

void MediaPool::handleRegionEnd(MediaPlayer* player, float currentPosition, 
                                 float effectiveRegionEnd, float loopStartPos, 
                                 PlayStyle playStyle) {
    if (!player) return;
    
    // CRITICAL: This function only handles STOPPING at region end, not looping
    // LOOP mode handles looping manually in update() by checking loopEnd and seeking to loopStart
    // This function should never be called for LOOP mode - if it is, it's a bug
    switch (playStyle) {
        case PlayStyle::ONCE:
            // ONCE mode: Stop playback
            // Position is NOT preserved - each trigger starts from startPosition
            player->stop();
            break;
        case PlayStyle::LOOP:
            // LOOP mode should never reach here - looping is handled in update()
            // If we do reach here, it's a bug - log warning and loop back as fallback
            ofLogWarning("MediaPool") << "handleRegionEnd() called for LOOP mode - this should not happen. Looping back to loopStart.";
            seekPlayerToPosition(player, loopStartPos);
            break;
        case PlayStyle::NEXT:
            // NEXT mode: Stop current player - onPlaybackEnd() will handle playing next media
            player->stop();
            break;
    }
}

// Position scan functions removed - position memory now handled directly via MediaPlayer::playheadPosition


float MediaPool::clampPositionForPlayback(float position, PlayStyle playStyle) const {
    // First clamp to valid range (0.0-1.0)
    float clampedPosition = std::max(0.0f, std::min(1.0f, position));
    
    // CRITICAL: In ONCE mode with position memory, if position is at the end (>= END_POSITION_THRESHOLD),
    // clamp to just before the end so playback can continue instead of immediately stopping.
    // This preserves position memory behavior while allowing playback to work.
    if (playStyle == PlayStyle::ONCE && clampedPosition >= END_POSITION_THRESHOLD) {
        clampedPosition = END_POSITION_THRESHOLD;
    }
    
    return clampedPosition;
}

void MediaPool::applyEventParameters(MediaPlayer* player, const TriggerEvent& event, 
                                     const std::vector<ParameterDescriptor>& descriptors) {
    if (!player) return;
    
    // Special handling for "position" parameter (maps to startPosition, needs region clamping)
    float position = 0.0f;
    bool positionInEvent = false;
    auto positionIt = event.parameters.find("position");
    if (positionIt != event.parameters.end()) {
        position = positionIt->second;
        positionInEvent = true;
    } else {
        // Position not in event - use current player value (position memory)
        position = player->startPosition.get();
    }
    
    // Clamp position using helper method (handles ONCE mode end position clamping)
    float clampedPosition = clampPositionForPlayback(position, currentPlayStyle);
    
    // Update playheadPosition if we clamped for ONCE mode
    if (currentPlayStyle == PlayStyle::ONCE && position >= END_POSITION_THRESHOLD && 
        clampedPosition == END_POSITION_THRESHOLD) {
        player->playheadPosition.set(END_POSITION_THRESHOLD);
        ofLogVerbose("MediaPool") << "[ONCE_MODE] Clamped end position to allow playback continuation";
    }
    
    // Set position if it was in event or if it changed
    if (positionInEvent && std::abs(player->startPosition.get() - clampedPosition) > PARAMETER_EPSILON) {
        player->startPosition.set(clampedPosition);
    }
    
    // Audio/video always enabled for sequencer triggers
    if (!player->audioEnabled.get()) {
        player->audioEnabled.set(true);
    }
    if (!player->videoEnabled.get()) {
        player->videoEnabled.set(true);
    }
    
    // Process all other parameters from event dynamically
    // Skip "note" (handled separately) and "position" (handled above)
    for (const auto& paramPair : event.parameters) {
        const std::string& paramName = paramPair.first;
        float paramValue = paramPair.second;
        
        // Skip special parameters
        if (paramName == "note" || paramName == "position") {
            continue;
        }
        
        // Get parameter descriptor for validation
        auto descIt = std::find_if(descriptors.begin(), descriptors.end(),
            [&](const ParameterDescriptor& desc) { return desc.name == paramName; });
        
        // Clamp value to parameter range if descriptor found
        float clampedValue = paramValue;
        if (descIt != descriptors.end()) {
            clampedValue = std::max(descIt->minValue, std::min(descIt->maxValue, paramValue));
        }
        
        // Use MediaPlayer's getFloatParameter to check if parameter exists
        auto* param = player->getFloatParameter(paramName);
        if (param) {
            // Parameter exists on player - set it
            float currentValue = param->get();
            if (std::abs(currentValue - clampedValue) > PARAMETER_EPSILON) {
                param->set(clampedValue);
            }
        } else {
            // Parameter doesn't exist on player - log warning but continue
            ofLogVerbose("MediaPool") << "Parameter '" << paramName << "' not found on MediaPlayer, skipping";
        }
    }
    
    // CRITICAL: Don't enable underlying player loop for LOOP mode
    // The underlying players loop at full media level (0.0-1.0), but we need region-level looping
    // (loopStart to loopEnd based on loopSize). We handle looping manually in update().
    // Always disable underlying loop - looping is handled manually at region level
    player->loop.set(false);
}

//--------------------------------------------------------------
void MediaPool::stopAllNonActivePlayersLocked() {
    // Internal version - assumes stateMutex is already locked
    for (size_t i = 0; i < players.size(); i++) {
        auto& player = players[i];
        if (player && player.get() != activePlayer && player->isPlaying()) {
            player->stop();
            // For LOOP mode, reset position after stopping (no position memory)
            if (currentPlayStyle == PlayStyle::LOOP) {
                player->playheadPosition.set(0.0f);
            }
            ofLogVerbose("MediaPool") << "[POLYPHONY] Stopped non-active player at index " << i;
        }
    }
}

void MediaPool::stopAllNonActivePlayers() {
    // Public version - locks mutex
    std::lock_guard<std::mutex> lock(stateMutex);
    stopAllNonActivePlayersLocked();
}

//--------------------------------------------------------------
// Module Factory Registration
//--------------------------------------------------------------
// Auto-register MediaPool with ModuleFactory on static initialization
// This enables true modularity - no hardcoded dependencies in ModuleFactory
namespace {
    struct MediaPoolRegistrar {
        MediaPoolRegistrar() {
            ModuleFactory::registerModuleType("MediaPool", 
                []() -> std::shared_ptr<Module> {
                    return std::make_shared<MediaPool>();
                });
        }
    };
    static MediaPoolRegistrar g_mediaPoolRegistrar;
}
