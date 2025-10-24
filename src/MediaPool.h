#pragma once

#include "ofxSoundObjects.h"
#include "ofxVisualObjects.h"
#include <vector>
#include <memory>
#include <string>
#include <mutex>

// Forward declarations to avoid circular dependency
class MediaPlayer;
class Clock;

// Playback state machine enum
enum class PlaybackMode {
    IDLE,              // No playback active
    MANUAL_PREVIEW,    // GUI-triggered preview playback
    SEQUENCER_ACTIVE   // Sequencer-triggered playback
};

enum class PreviewMode {
    STOP_AT_END,    // Stop when media finishes
    LOOP,           // Loop the current media
    PLAY_NEXT       // Play next media in pool
};

// Struct for step trigger parameters to reduce coupling
struct StepTriggerParams {
    int step;
    int mediaIndex;
    float position;
    float speed;
    float volume;
    float duration;
    bool audioEnabled;
    bool videoEnabled;
};

class MediaPool {
public:
    // Constructor with optional directory
    MediaPool(const std::string& dataDir = "data");
    ~MediaPool();
    
    // Scan directory for media files
    void scanDirectory(const std::string& path);
    
    // Set custom absolute path (bypasses automatic path resolution)
    void setCustomPath(const std::string& absolutePath);
    
    // File pairing strategy
    void mediaPair();      // Match by base filename
    void pairByIndex();    // Pair in order (1st audio + 1st video, etc.)
    
    // Access media players
    MediaPlayer* getMediaPlayer(size_t index);
    MediaPlayer* getMediaPlayerByName(const std::string& name);
    MediaPlayer* getCurrentPlayer();
    MediaPlayer* getNextPlayer();
    MediaPlayer* getPreviousPlayer();
    
    // Navigation
    void setCurrentIndex(size_t index);
    void nextPlayer();
    void previousPlayer();
    
    // Library info
    size_t getNumPlayers() const;
    size_t getCurrentIndex() const;
    std::string getMediaDirectory() const;
    std::vector<std::string> getPlayerNames() const;
    std::vector<std::string> getPlayerFileNames() const;  // Get actual file names
    
    // Audio/video files
    std::vector<std::string> getAudioFiles() const;
    std::vector<std::string> getVideoFiles() const;
    
    // File management
    void clear();
    void refresh();
    
    // Setup method
    void setup(Clock* clockRef);
    
    // Step event handling - receives media parameters directly
    void onStepTrigger(int step, int mediaIndex, float position, 
                      float speed, float volume, float stepLength, 
                      bool audioEnabled, bool videoEnabled);
    
    // Overloaded version using struct for cleaner interface
    void onStepTrigger(const StepTriggerParams& params);
    
    // Manual media playback (for GUI preview)
    bool playMediaManual(size_t index, float position = 0.0f);
    
    // Query methods for state checking
    PlaybackMode getCurrentMode() const;
    bool isSequencerActive() const;
    bool isManualPreview() const;
    bool isIdle() const;
    
    // Preview mode control
    void setPreviewMode(PreviewMode mode);
    PreviewMode getPreviewMode() const;
    void onManualPreviewEnd();
    
    // Update method for end-of-media detection
    void update();
    
    // Connection management (internal)
    void setActivePlayer(size_t index);
    MediaPlayer* getActivePlayer();
    void setOutputs(ofxSoundOutput& soundOut, ofxVisualOutput& visualOut);
    void connectActivePlayer(ofxSoundOutput& soundOut, ofxVisualOutput& visualOut);
    void disconnectActivePlayer();
    void stopAllMedia();
    
    
    // Directory management
    void setDataDirectory(const std::string& path);
    void browseForDirectory();
    std::string getDataDirectory() const { return dataDirectory; }
    
    // Directory change callback
    std::function<void(const std::string&)> onDirectoryChanged;
    void setDirectoryChangeCallback(std::function<void(const std::string&)> callback) { onDirectoryChanged = callback; }
    
    
private:
    Clock* clock;
    std::vector<std::unique_ptr<MediaPlayer>> players;
    std::vector<std::string> audioFiles;
    std::vector<std::string> videoFiles;
    size_t currentIndex;
    std::string dataDirectory;
    bool isSetup;
    
    // Thread safety
    mutable std::mutex stateMutex;
    
    // Playback state machine
    PlaybackMode currentMode;
    PreviewMode currentPreviewMode;
    
    // Connection state
    MediaPlayer* activePlayer;
    bool isConnected;
    ofxSoundOutput* soundOutput;
    ofxVisualOutput* visualOutput;
    
    // Helper methods
    std::string getBaseName(const std::string& filename);
    void scanMediaFiles(const std::string& path, ofDirectory& dir);  // Extract duplicate logic
    bool isAudioFile(const std::string& filename);
    bool isVideoFile(const std::string& filename);
    std::string findMatchingVideo(const std::string& audioFile);
    std::string findMatchingAudio(const std::string& videoFile);
    
};
