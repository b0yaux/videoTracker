#pragma once

#include "ofxSoundObjects.h"
#include "ofxVisualObjects.h"
#include "Module.h"
#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <atomic>
#include <queue>
#include <map>

// Forward declarations to avoid circular dependency
class MediaPlayer;
class Clock;

// Playback state machine enum
// NOTE: This represents MediaPool's LOCAL playback mode, NOT global transport state.
// MediaPool can be in different modes regardless of Clock transport state:
// - IDLE: No media playing (can occur while Clock is playing or stopped)
// - MANUAL_PREVIEW: User-triggered preview (can occur while Clock is stopped)
// - SEQUENCER_ACTIVE: Sequencer-triggered playback (requires Clock to be playing)
// This is a different concern than transport state - it answers "What is MediaPool doing?"
// rather than "Is the global transport playing?" (which comes from Clock).
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

// Note: StepTriggerParams and TriggerEventData removed - using TriggerEvent directly from Module.h

class MediaPool : public Module {
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
    
    // Subscribe to TrackerSequencer trigger events (modular connection)
    void subscribeToTrackerSequencer(class TrackerSequencer* sequencer);
    
    // Process lock-free event queue (called from update in GUI thread)
    // Queue contains TriggerEvent instances from sequencer
    void processEventQueue();
    
    // Module interface implementation
    std::string getName() const override;
    ModuleType getType() const override;
    std::vector<ParameterDescriptor> getParameters() override;
    void onTrigger(TriggerEvent& event) override;
    void setParameter(const std::string& paramName, float value, bool notify = true) override;
    
    // Manual media playback (for GUI preview)
    bool playMediaManual(size_t index, float position = 0.0f);
    void stopManualPreview();  // Stop manual preview and transition to IDLE
    
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
    void update() override;
    
    // Transport listener system for Clock play/stop events
    typedef std::function<void(bool isPlaying)> TransportCallback;
    void addTransportListener(TransportCallback listener);
    void removeTransportListener();
    void onTransportChanged(bool isPlaying);
    
    // Connection management (internal)
    void setActivePlayer(size_t index);
    MediaPlayer* getActivePlayer();
    void connectActivePlayer(ofxSoundOutput& soundOut, ofxVisualOutput& visualOut);
    void disconnectActivePlayer();
    bool isPlayerConnected() const { return playerConnected; }
    void stopAllMedia();
    
    // Initialize first active player after setup is complete
    void initializeFirstActivePlayer();
    
    
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
    
    // Lock-free event queue for audio thread -> GUI thread communication
    // Uses TriggerEvent directly (from Module.h) - no redundant data structures
    std::queue<TriggerEvent> eventQueue;
    
    // Playback state machine (atomic for lock-free reads)
    std::atomic<PlaybackMode> currentMode;
    PreviewMode currentPreviewMode;
    
    // Connection state
    MediaPlayer* activePlayer;
    bool playerConnected;  // Track if player is already connected to avoid reconnecting every frame
    
    // Transport listener system
    TransportCallback transportListener;
    bool lastTransportState;
    
    // Position memory: Store last playback position per media index
    // This allows position consistency when retriggering same media without position instructions
    std::map<int, float> lastPositionByMediaIndex;
    
    // Track last playing state to detect when player stops (for position capture)
    std::map<int, bool> lastPlayingStateByMediaIndex;
    
    // Parameter change callback (inherited from Module base class)
    // Note: MediaPool inherits parameterChangeCallback from Module
    
    // Helper methods
    std::string getBaseName(const std::string& filename);
    void scanMediaFiles(const std::string& path, ofDirectory& dir);  // Extract duplicate logic
    bool isAudioFile(const std::string& filename);
    bool isVideoFile(const std::string& filename);
    std::string findMatchingVideo(const std::string& audioFile);
    std::string findMatchingAudio(const std::string& videoFile);
    
};
