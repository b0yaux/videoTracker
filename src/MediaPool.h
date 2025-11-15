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

enum class PlayStyle {
    ONCE,      // Stop when playback reaches region end
    LOOP,      // Loop within region
    NEXT       // Play next media in pool
};

// Position memory mode: how position is stored and restored
enum class PositionMemoryMode {
    PER_STEP,    // Each step has its own position memory (key: step + mediaIndex)
    PER_INDEX,   // All steps share position per media index (key: mediaIndex)
    GLOBAL       // Single position shared across all indexes and steps
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
    // stopManualPreview() removed - update() automatically transitions MANUAL_PREVIEW → IDLE when player stops
    
    // Query methods for state checking
    PlaybackMode getCurrentMode() const;
    bool isSequencerActive() const;
    bool isManualPreview() const;
    bool isIdle() const;
    
    // Helper to transition to IDLE mode immediately (for button handlers)
    void setModeIdle();
    
    // Play style control (applies to both manual preview and sequencer playback)
    void setPlayStyle(PlayStyle style);
    PlayStyle getPlayStyle() const;
    void onPlaybackEnd();
    
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
    
    // Position memory mode control
    void setPositionMemoryMode(PositionMemoryMode mode);
    PositionMemoryMode getPositionMemoryMode() const;
    
private:
    // Unified position memory system
    class PositionMemory {
    private:
        PositionMemoryMode mode;
        
        // For PER_STEP: key = (step << 16) | mediaIndex
        // For PER_INDEX: key = mediaIndex
        // For GLOBAL: single value, key ignored
        std::map<int, float> positions;
        float globalPosition = 0.0f;
        
        static constexpr float POSITION_THRESHOLD = 0.01f;
        static constexpr float POSITION_EPSILON = 0.001f;
        static constexpr float POSITION_END_THRESHOLD = 0.99f;  // Positions >= this are treated as "end of media" (reset to start)
        
        int makeKey(int step, int mediaIndex) const {
            switch (mode) {
                case PositionMemoryMode::PER_STEP:
                    return (step << 16) | (mediaIndex & 0xFFFF);
                case PositionMemoryMode::PER_INDEX:
                    return mediaIndex;
                case PositionMemoryMode::GLOBAL:
                    return 0; // Ignored
                default:
                    return 0; // Should never happen
            }
        }
        
    public:
        PositionMemory(PositionMemoryMode m = PositionMemoryMode::PER_INDEX) : mode(m) {}
        
        void setMode(PositionMemoryMode m) { mode = m; }
        PositionMemoryMode getMode() const { return mode; }
        
        // Capture position (called from update() when media is playing)
        void capture(int step, int mediaIndex, float position) {
            // Don't store near-zero positions
            if (position < POSITION_THRESHOLD) return;
            
            // CRITICAL FIX: Don't capture positions at end of media (>= 0.99)
            // When media reaches the end, we should reset position memory (treat as "media ended, start fresh")
            // This prevents the issue where media gets stuck at 0.999 and immediately stops on retrigger
            if (position >= POSITION_END_THRESHOLD) {
                // Media reached the end - clear position memory for this key (will start from beginning next time)
                clear(step, mediaIndex);
                return;
            }
            
            // Normal position capture (media is still playing, not at end)
            if (mode == PositionMemoryMode::GLOBAL) {
                globalPosition = position;
            } else {
                int key = makeKey(step, mediaIndex);
                auto it = positions.find(key);
                if (it == positions.end() || position > it->second + POSITION_EPSILON) {
                    positions[key] = position;
                }
            }
        }
        
        // Restore position (called from onTrigger() when position parameter missing)
        float restore(int step, int mediaIndex, float defaultValue = 0.0f) const {
            if (mode == PositionMemoryMode::GLOBAL) {
                // Check if global position is at end of media
                if (globalPosition >= POSITION_END_THRESHOLD) {
                    return defaultValue; // Media ended, start from beginning
                }
                return globalPosition;
            }
            
            int key = makeKey(step, mediaIndex);
            auto it = positions.find(key);
            if (it != positions.end()) {
                // Check if stored position is at end of media
                if (it->second >= POSITION_END_THRESHOLD) {
                    return defaultValue; // Media ended, start from beginning
                }
                return it->second;
            }
            return defaultValue;
        }
        
        // Clear all stored positions
        void clear() {
            positions.clear();
            globalPosition = 0.0f;
        }
        
        // Clear position for specific step/index (useful for reset)
        void clear(int step, int mediaIndex) {
            if (mode == PositionMemoryMode::GLOBAL) {
                globalPosition = 0.0f;
            } else {
                int key = makeKey(step, mediaIndex);
                positions.erase(key);
            }
        }
        
        // Get number of stored positions (for checking if memory is empty)
        size_t size() const {
            if (mode == PositionMemoryMode::GLOBAL) {
                return (globalPosition > POSITION_THRESHOLD) ? 1 : 0;
            }
            return positions.size();
        }
    };
    
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
    PlayStyle currentPlayStyle;
    
    // Connection state
    MediaPlayer* activePlayer;
    bool playerConnected;  // Track if player is already connected to avoid reconnecting every frame
    
    // Transport listener system
    TransportCallback transportListener;
    bool lastTransportState;
    bool transportJustStarted = false;  // Add this flag
    
    // Unified position memory system (replaces lastPositionByMediaIndex and lastPlayingStateByMediaIndex)
    PositionMemory positionMemory;
    
    // Track last triggered step for position capture (used in update())
    int lastTriggeredStep = -1;
    
    // Gate timer tracking for sequencer-triggered playback
    bool gateTimerActive;
    float gateEndTime;
    
    // Parameter change callback (inherited from Module base class)
    // Note: MediaPool inherits parameterChangeCallback from Module
    
    // Helper methods
    std::string getBaseName(const std::string& filename);
    void scanMediaFiles(const std::string& path, ofDirectory& dir);  // Extract duplicate logic
    bool isAudioFile(const std::string& filename);
    bool isVideoFile(const std::string& filename);
    
    // Position and parameter comparison thresholds
    static constexpr float POSITION_EPSILON = 0.001f;      // Small difference threshold for position comparisons
    static constexpr float POSITION_THRESHOLD = 0.01f;     // Significant position threshold (for video seeking, position memory)
    static constexpr float PARAMETER_EPSILON = 0.001f;     // Small difference threshold for parameter comparisons
};
