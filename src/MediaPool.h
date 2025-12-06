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
class ModuleRegistry;
class ConnectionManager;
class ParameterRouter;

// Playback state machine enum
// Simplified to 2 modes: IDLE (no playback) and PLAYING (any playback active)
// MediaPool doesn't need to distinguish between trigger sources - it just needs to know if it's playing
enum class PlaybackMode {
    IDLE,     // No playback active
    PLAYING   // Playback active (from user trigger, sequencer, or any source)
};

enum class PlayStyle {
    ONCE,      // Stop when playback reaches region end
    LOOP,      // Loop within region
    NEXT       // Play next media in pool
};

// Position scan mode: how scan position is stored and restored
// Polyphony mode: how multiple triggers are handled
enum class PolyphonyMode {
    MONOPHONIC = 0,  // Only one player active at a time (stops previous player on new trigger)
    POLYPHONIC = 1   // Multiple players can play simultaneously (layering allowed)
};

// Note: StepTriggerParams and TriggerEventData removed - using TriggerEvent directly from Module.h

class MediaPool : public Module {
public:
    // Constructor with optional directory
    MediaPool(const std::string& dataDir = "data");
    virtual ~MediaPool() noexcept;
    
    // Scan directory for media files
    void scanDirectory(const std::string& path);
    
    // Set custom absolute path (bypasses automatic path resolution)
    void setCustomPath(const std::string& absolutePath);
    
    // File pairing strategy
    void mediaPair();      // Match by base filename
    void pairByIndex();    // Pair in order (1st audio + 1st video, etc.)
    
    // New unified initialization method (Phase 2.3)
    void initialize(Clock* clock, ModuleRegistry* registry, ConnectionManager* connectionManager, 
                    ParameterRouter* parameterRouter, bool isRestored) override;
    
    // DEPRECATED: Use initialize() instead
    [[deprecated("Use initialize() instead")]]
    void completeRestore();
    
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
    
    // Individual file addition (for drag-and-drop support)
    bool addMediaFile(const std::string& filePath);
    void addMediaFiles(const std::vector<std::string>& filePaths);
    
    // Module interface - accept file drops
    bool acceptFileDrop(const std::vector<std::string>& filePaths) override;
    
    // Remove a specific player by index
    bool removePlayer(size_t index);
    
    // Setup method
    void setup(Clock* clockRef);
    [[deprecated("Use initialize() instead")]]
    void postCreateSetup(Clock* clock); // Module interface - handles own initialization
    
    // Process lock-free event queue (called from update in GUI thread)
    // Queue contains TriggerEvent instances from sequencer
    void processEventQueue();
    
    // Module interface implementation
    std::string getName() const override;
    ModuleType getType() const override;
    std::vector<ParameterDescriptor> getParameters() const override;
    void onTrigger(TriggerEvent& event) override;
    void setParameter(const std::string& paramName, float value, bool notify = true) override;
    float getParameter(const std::string& paramName) const override;
    
    // Capability interface implementation
    bool hasCapability(ModuleCapability capability) const override;
    std::vector<ModuleCapability> getCapabilities() const override;
    
    // Metadata interface implementation
    ModuleMetadata getMetadata() const override;
    
    // Override setEnabled to stop all media when disabled
    void setEnabled(bool enabled) override;
    
    // Module serialization interface
    ofJson toJson() const override;
    void fromJson(const ofJson& json) override;
    // getTypeName() uses default implementation from Module base class
    
    // Module routing interface - expose stable audio/video outputs via internal mixers
    ofxSoundObject* getAudioOutput() const override;
    ofxVisualObject* getVideoOutput() const override;
    
    // Port-based routing interface (Phase 1)
    std::vector<Port> getInputPorts() const override;
    std::vector<Port> getOutputPorts() const override;
    
    // Manual media playback (for GUI preview)
    // Automatically determines start position based on speed (end for backward, start for forward)
    bool playMediaManual(size_t index);
    // stopManualPreview() removed - update() automatically transitions PLAYING → IDLE when player stops
    
    // Query methods for state checking
    PlaybackMode getCurrentMode() const;
    bool isPlaying() const;  // Returns true if any player is playing (PLAYING mode)
    
    // Helper to transition to IDLE mode immediately (for button handlers)
    void setModeIdle();
    
    // Temporary playback for scrubbing (doesn't change MediaPool mode)
    void startTemporaryPlayback(size_t index, float position);
    void stopTemporaryPlayback();
    
    // Start playback for scrubbing (doesn't change mode or startPosition - only updates playheadPosition)
    void startScrubbingPlayback(size_t index, float position);
    
    // Play style control (applies to both manual preview and sequencer playback)
    void setPlayStyle(PlayStyle style);
    PlayStyle getPlayStyle() const;
    void onPlaybackEnd();
    
    // Update method for end-of-media detection
    void update() override;
    
    // Connection management (internal)
    void setActivePlayer(size_t index);
    MediaPlayer* getActivePlayer();
    // DEPRECATED: Old direct connection methods - kept for backward compatibility but no longer used
    // Modules should connect to MediaPool via getAudioOutput()/getVideoOutput() instead
    void connectActivePlayer(ofxSoundOutput& soundOut, ofxVisualOutput& visualOut);
    void disconnectActivePlayer();
    void stopAllMedia();
    
    // Internal connection management - connect/disconnect players to internal mixers
    void connectPlayerToInternalMixers(MediaPlayer* player);
    void disconnectPlayerFromInternalMixers(MediaPlayer* player);
    
    // Initialize first active player after setup is complete
    void initializeFirstActivePlayer();
    
    // Helper method for polyphony mode switching: stop all players except activePlayer
    // Internal version assumes lock is already held
    void stopAllNonActivePlayersLocked();
    // Public version that locks mutex
    void stopAllNonActivePlayers();
    
    
    // Directory management
    void setDataDirectory(const std::string& path);
    void browseForDirectory();
    std::string getDataDirectory() const { return dataDirectory; }
    
    // Directory change callback
    std::function<void(const std::string&)> onDirectoryChanged;
    void setDirectoryChangeCallback(std::function<void(const std::string&)> callback) { onDirectoryChanged = callback; }
    
    // Polyphony mode control
    PolyphonyMode getPolyphonyMode() const;
    
private:
private:
    Clock* clock;
    std::vector<std::unique_ptr<MediaPlayer>> players;
    std::vector<std::string> audioFiles;
    std::vector<std::string> videoFiles;
    size_t currentIndex;
    std::string dataDirectory;
    bool isSetup;
    
    // Internal mixers for stable audio/video output
    // All players connect to these mixers, providing a stable output interface
    ofxSoundMixer internalAudioMixer_;
    ofxVideoMixer internalVideoMixer_;
    
    // Thread safety
    mutable std::mutex stateMutex;
    std::atomic<bool> isDestroying_{false};  // Prevent update() after destruction starts
    
    // Lock-free event queue for audio thread -> GUI thread communication
    // Uses moodycamel::ReaderWriterQueue (SPSC - Single Producer Single Consumer)
    // Producer: Audio thread (onTrigger)
    // Consumer: GUI thread (processEventQueue)
    // Capacity: 1024 events (increased to handle rapid triggering with many media files)
    moodycamel::ReaderWriterQueue<TriggerEvent> eventQueue{1024};
    
    // Scheduled stop tracking for sequencer-triggered playback (replaces playWithGate)
    // Tracks when players should be stopped after their gate duration expires
    struct ScheduledStop {
        MediaPlayer* player;
        float stopTime;  // Absolute time when player should stop (ofGetElapsedTimef() + duration)
    };
    std::vector<ScheduledStop> scheduledStops_;
    
    // Diagnostic: Track if onTrigger is being called (for debugging event subscription issues)
    std::atomic<int> onTriggerCallCount_{0};
    
    // Playback state machine (atomic for lock-free reads)
    std::atomic<PlaybackMode> currentMode;
    PlayStyle currentPlayStyle;
    
    // Connection state
    MediaPlayer* activePlayer;
    
    // Dynamic connection management: track which players are connected to mixers
    // Only players that are playing and have audio/video enabled should be connected
    std::map<MediaPlayer*, bool> playerVideoConnected;  // Track video connection state per player
    std::map<MediaPlayer*, bool> playerAudioConnected;  // Track audio connection state per player
    
    // Track last triggered step (used by ofApp/InputRouter for state tracking)
    int lastTriggeredStep = -1;
    
    // Active step context (kept for potential future use, currently not used in simplified logic)
    struct ActiveStepContext {
        int step = -1;
        int mediaIndex = -1;
        float triggerTime = 0.0f;
    };
    ActiveStepContext activeStepContext;
    
    // Track last trigger time (kept for potential future use, not needed for simplified queue-based logic)
    float lastTriggerTime = 0.0f;
    
    // Polyphony mode: controls whether multiple players can play simultaneously
    PolyphonyMode polyphonyMode_;
    
    // Parameter change callback (inherited from Module base class)
    // Note: MediaPool inherits parameterChangeCallback from Module
    
    // Helper methods
    std::string getBaseName(const std::string& filename);
    void scanMediaFiles(const std::string& path, ofDirectory& dir);  // Extract duplicate logic
    bool isAudioFile(const std::string& filename);
    bool isVideoFile(const std::string& filename);
    
    // Dynamic connection management: connect/disconnect players based on playing state
    void ensurePlayerVideoConnected(MediaPlayer* player);
    void ensurePlayerVideoDisconnected(MediaPlayer* player);
    void ensurePlayerAudioConnected(MediaPlayer* player);
    void ensurePlayerAudioDisconnected(MediaPlayer* player);
    void updatePlayerConnections();  // Update all player connections based on current state
    
    // Position and parameter comparison thresholds
    static constexpr float POSITION_EPSILON = 0.001f;      // Small difference threshold for position comparisons
    static constexpr float POSITION_THRESHOLD = 0.01f;     // Significant position threshold (for video seeking, position memory)
    static constexpr float PARAMETER_EPSILON = 0.001f;     // Small difference threshold for parameter comparisons
    
    // Named constants for magic numbers
    static constexpr float MIN_REGION_SIZE = 0.001f;       // Minimum valid region size
    static constexpr float MIN_DURATION = 0.001f;         // Minimum valid duration
    static constexpr float MIN_LOOP_SIZE = 0.001f;         // Minimum valid loop size
    static constexpr float REGION_BOUNDARY_THRESHOLD = 0.001f;  // Threshold for region boundary checks
    static constexpr float END_POSITION_THRESHOLD = 0.99f;  // Position >= this means media reached end
    static constexpr float INIT_POSITION = 0.01f;          // Initial position for video frame initialization
    
    // Loop bounds structure for cleaner return values
    struct LoopBounds {
        float start;  // Absolute position (0.0-1.0)
        float end;    // Absolute position (0.0-1.0)
    };
    
    // Helper functions for position mapping and loop calculations
    float mapRelativeToAbsolute(float relativePos, float regionStart, float regionEnd) const;
    float mapAbsoluteToRelative(float absolutePos, float regionStart, float regionEnd) const;
    LoopBounds calculateLoopBounds(MediaPlayer* player, PlayStyle playStyle) const;
    void seekPlayerToPosition(MediaPlayer* player, float position) const;
    
    // Region end handling - simplifies complex conditional logic
    void handleRegionEnd(MediaPlayer* player, float currentPosition, 
                         float effectiveRegionEnd, float loopStartPos, 
                         PlayStyle playStyle);
    
    // Scan position helpers - unified capture and restore logic
    void captureScanPosition(int step, int mediaIndex, float position, PlaybackMode mode);
    float restoreScanPosition(int step, int mediaIndex, MediaPlayer* player);
    
    // Parameter processing helper
    void applyEventParameters(MediaPlayer* player, const TriggerEvent& event, 
                             const std::vector<ParameterDescriptor>& descriptors);
    
    // Position validation helper - clamps position for playback with play style awareness
    float clampPositionForPlayback(float position, PlayStyle playStyle) const;
    
    // Flag to defer media loading during session restore (prevents blocking)
    bool deferMediaLoading_ = false;
    
    // Store player parameters for deferred loading (after mediaPair creates players)
    struct DeferredPlayerParams {
        std::string audioFile;
        std::string videoFile;
        ofJson paramsJson;
    };
    std::vector<DeferredPlayerParams> deferredPlayerParams_;
    size_t deferredActivePlayerIndex_ = 0;
};

