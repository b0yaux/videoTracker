#pragma once

#include "ofxSoundObjects.h"
#include "ofxVisualObjects.h"
#include "Module.h"
#include "readerwriterqueue.h"
#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <atomic>
#include <map>
#include "ofJson.h"

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

// Position scan mode: how scan position is stored and restored
// Scan position tracks where playback left off, so next trigger (without explicit position) continues from there
enum class ScanMode {
    NONE,        // No scanning - always start from set position (or 0.0)
    PER_STEP,    // Each step remembers its own scan position (key: step + mediaIndex)
    PER_MEDIA,   // Each media remembers its scan position across all steps (key: mediaIndex)
    GLOBAL       // All media share one scan position (key: ignored)
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
    
    // Module serialization interface
    ofJson toJson() const override;
    void fromJson(const ofJson& json) override;
    // getTypeName() uses default implementation from Module base class
    
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
    
    // Position scan mode control
    void setScanMode(ScanMode mode);
    ScanMode getScanMode() const;
    
private:
    // Position scan system: tracks where playback left off for scanning through media
    // SIMPLIFIED: PER_MEDIA mode now uses MediaPlayer::playheadPosition directly (no storage needed)
    // Only PER_STEP and GLOBAL modes use this storage
    class PositionScan {
    private:
        ScanMode mode;
        
        // For PER_STEP: key = (step, mediaIndex) pair (simplified from bit-shifting)
        // For GLOBAL: single value, key ignored
        // NOTE: PER_MEDIA mode no longer uses storage - reads directly from MediaPlayer::playheadPosition
        std::map<std::pair<int, int>, float> stepPositions;  // (step, mediaIndex) -> position
        float globalScanPosition = 0.0f;
        
        static constexpr float SCAN_THRESHOLD = 0.01f;      // Don't store near-zero positions
        static constexpr float SCAN_END_THRESHOLD = 0.99f;  // Positions >= this mean "media ended" (reset to start)
        
    public:
        PositionScan(ScanMode m = ScanMode::PER_MEDIA) : mode(m) {}
        
        void setMode(ScanMode m) { mode = m; }
        ScanMode getMode() const { return mode; }
        
        // Capture scan position during active playback
        // NOTE: Only called for PER_STEP and GLOBAL modes
        // PER_MEDIA mode uses MediaPlayer::playheadPosition directly (no capture needed)
        void capture(int step, int mediaIndex, float position) {
            if (mode == ScanMode::NONE || mode == ScanMode::PER_MEDIA) return;
            if (position < SCAN_THRESHOLD) return;  // Don't store near-zero positions
            
            // CRITICAL: If media reached the end, reset scan position (start fresh next time)
            if (position >= SCAN_END_THRESHOLD) {
                clear(step, mediaIndex);
                return;
            }
            
            // Store scan position
            if (mode == ScanMode::GLOBAL) {
                globalScanPosition = position;
            } else if (mode == ScanMode::PER_STEP) {
                // For PER_STEP mode, step must be valid (>= 0)
                if (step >= 0) {
                    stepPositions[{step, mediaIndex}] = position;
                }
            }
        }
        
        // Restore scan position (returns 0.0 if not found or at end)
        // NOTE: For PER_MEDIA mode, caller should read directly from MediaPlayer::playheadPosition
        float restore(int step, int mediaIndex) const {
            if (mode == ScanMode::NONE || mode == ScanMode::PER_MEDIA) return 0.0f;
            
            if (mode == ScanMode::GLOBAL) {
                return (globalScanPosition >= SCAN_END_THRESHOLD) ? 0.0f : globalScanPosition;
            } else if (mode == ScanMode::PER_STEP) {
                if (step < 0) return 0.0f;
                auto it = stepPositions.find({step, mediaIndex});
                if (it != stepPositions.end()) {
                    // If stored position is at end, return 0.0 (start fresh)
                    return (it->second >= SCAN_END_THRESHOLD) ? 0.0f : it->second;
                }
            }
            return 0.0f;
        }
        
        // Clear all scan positions (called when transport starts)
        void clear() {
            stepPositions.clear();
            globalScanPosition = 0.0f;
        }
        
        // Clear scan position for specific step/index
        void clear(int step, int mediaIndex) {
            if (mode == ScanMode::NONE || mode == ScanMode::PER_MEDIA) return;
            
            if (mode == ScanMode::GLOBAL) {
                globalScanPosition = 0.0f;
            } else if (mode == ScanMode::PER_STEP) {
                if (step >= 0) {
                    stepPositions.erase({step, mediaIndex});
                }
            }
        }
        
        // Get number of stored scan positions
        size_t size() const {
            if (mode == ScanMode::NONE || mode == ScanMode::PER_MEDIA) return 0;
            if (mode == ScanMode::GLOBAL) {
                return (globalScanPosition > SCAN_THRESHOLD) ? 1 : 0;
            }
            return stepPositions.size();
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
    // Uses moodycamel::ReaderWriterQueue (SPSC - Single Producer Single Consumer)
    // Producer: Audio thread (onTrigger)
    // Consumer: GUI thread (processEventQueue)
    // Capacity: 1024 events (increased to handle rapid triggering with many media files)
    moodycamel::ReaderWriterQueue<TriggerEvent> eventQueue{1024};
    
    // Playback state machine (atomic for lock-free reads)
    std::atomic<PlaybackMode> currentMode;
    PlayStyle currentPlayStyle;
    
    // Connection state
    MediaPlayer* activePlayer;
    bool playerConnected;  // Track if player is already connected to avoid reconnecting every frame
    
    // Transport listener system
    TransportCallback transportListener;
    bool lastTransportState;
    
    // Position scan system (tracks where playback left off for scanning through media)
    PositionScan positionScan;
    
    // Track last triggered step for position capture (used in update())
    int lastTriggeredStep = -1;
    
    // Active step context for accurate position capture timing
    // Updated in processEventQueue() before position capture happens in update()
    struct ActiveStepContext {
        int step = -1;
        int mediaIndex = -1;
        float triggerTime = 0.0f;
    };
    ActiveStepContext activeStepContext;
    
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

