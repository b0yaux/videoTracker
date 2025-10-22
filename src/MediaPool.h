#pragma once

#include "ofxSoundObjects.h"
#include "ofxVisualObjects.h"
#include "ofxImGui.h"
#include <vector>
#include <memory>
#include <string>

// Forward declarations to avoid circular dependency
class MediaPlayer;
class Clock;

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
    
    // Connection management (internal)
    void setActivePlayer(size_t index);
    MediaPlayer* getActivePlayer();
    void setOutputs(ofxSoundOutput& soundOut, ofxVisualOutput& visualOut);
    void connectActivePlayer(ofxSoundOutput& soundOut, ofxVisualOutput& visualOut);
    void disconnectActivePlayer();
    void stopAllMedia();
    
    // GUI
    void drawMediaPoolGUI();
    
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
    
    // Connection state
    MediaPlayer* activePlayer;
    bool isConnected;
    ofxSoundOutput* soundOutput;
    ofxVisualOutput* visualOutput;
    
    // Helper methods
    std::string getBaseName(const std::string& filename);
    void createPairedPlayers();
    void createStandalonePlayers();
    bool isAudioFile(const std::string& filename);
    bool isVideoFile(const std::string& filename);
    std::string findMatchingVideo(const std::string& audioFile);
    std::string findMatchingAudio(const std::string& videoFile);
};
