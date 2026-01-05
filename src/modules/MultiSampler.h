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
#include <array>
#include <thread>
#include <queue>
#include <condition_variable>
#include "ofJson.h"

// Forward declarations to avoid circular dependency
// MediaPlayer is now used directly in Voice struct, so we need full include
#include "MediaPlayer.h"
class Clock;
class ModuleRegistry;
class ConnectionManager;
class ParameterRouter;
class PatternRuntime;

// Include VoiceProcessor (needed for Voice struct member)
#include "VoiceProcessor.h"

// Include VoiceManager for unified voice pool management
#include "core/VoiceManager.h"

// Playback state machine enum
// Simplified to 2 modes: IDLE (no playback) and PLAYING (any playback active)
// MultiSampler doesn't need to distinguish between trigger sources - it just needs to know if it's playing
enum class PlaybackMode {
    IDLE,     // No playback active
    PLAYING   // Playback active (from user trigger, sequencer, or any source)
};

enum class PlayStyle {
    ONCE,      // Play from startPosition to regionEnd, then stop
    LOOP,      // Loop full region (regionStart → regionEnd → regionStart)
    GRAIN,     // Loop from startPosition for grainSize seconds (granular looping)
    NEXT       // Like LOOP, but remembers position on retrigger (position memory)
};

// Polyphony mode: how multiple triggers are handled
enum class PolyphonyMode {
    MONOPHONIC = 0,  // Only one player active at a time (stops previous player on new trigger)
    POLYPHONIC = 1   // Multiple players can play simultaneously (layering allowed)
};

// ============================================================================
// SAMPLER-INSPIRED ARCHITECTURE
// ============================================================================

// SampleRef: Contains shared audio data and video path for efficient memory usage
// Audio is loaded once into a shared buffer, enabling multiple voices to play the same sample
// without duplicating audio data in memory
struct SampleRef {
    std::string audioPath;          // Path to audio file (empty if video-only)
    std::string videoPath;          // Path to video file (empty if audio-only)
    std::string displayName;        // Cached for GUI (basename without extension)
    float duration = 0.0f;          // Cached duration in seconds
    bool metadataLoaded = false;    // True once duration has been cached

    // SHARED AUDIO: Single audio file loaded once, shared by all voices
    // Multiple ofxSingleSoundPlayer instances can reference this same buffer
    std::shared_ptr<ofxSoundFile> sharedAudioFile;
    
    // DEFAULT PARAMETERS (per-sample configuration)
    // These are the initial values used when voice is triggered
    float defaultRegionStart = 0.0f;      // Default crop start (0.0-1.0 of full media)
    float defaultRegionEnd = 1.0f;        // Default crop end (0.0-1.0 of full media)
    float defaultStartPosition = 0.0f;     // Default start within region (0.0-1.0 relative)
    float defaultSpeed = 1.0f;            // Default playback speed
    float defaultVolume = 1.0f;           // Default volume
    float defaultGrainSize = 0.0f;        // Default grain size in seconds (0 = use full region)
    
    // GUI STATE: Current values for waveform display and parameter editing
    // Synced from active voice during playback, editable when idle
    float guiPlayheadPosition = 0.0f;     // Current playhead position (absolute, 0.0-1.0)
    float guiStartPosition = 0.0f;        // Current start position (relative to region)
    float guiSpeed = 1.0f;                // Current playback speed
    float guiVolume = 1.0f;               // Current volume level
    float guiRegionStart = 0.0f;          // Current region start (for display)
    float guiRegionEnd = 1.0f;            // Current region end (for display)
    float guiGrainSize = 0.0f;            // Current grain size (for display)
    
    // PREVIEW PLAYER: Created on-demand for scrubbing/preview (not always present)
    // Only exists when user is actively scrubbing or previewing
    std::shared_ptr<MediaPlayer> previewPlayer;
    bool isScrubbing = false;           // True when previewPlayer is active for scrubbing
    
    // VIDEO: Each voice loads its own video player (decoders can't be shared)
    // Video path is stored here, loaded into voice on trigger

    // Helper to check if sample has any media
    bool hasMedia() const { return !audioPath.empty() || !videoPath.empty(); }

    // Helper to check if sample is ready for playback
    bool isReadyForPlayback() const { 
        // Ready if we have shared audio loaded, OR if we only have video (no audio to load)
        return (sharedAudioFile && sharedAudioFile->isLoaded()) || 
               (audioPath.empty() && !videoPath.empty());
    }
    
    // Load audio into shared buffer (call during preload)
    bool loadSharedAudio();
    
    // Unload shared audio (for memory management)
    void unloadSharedAudio();
    
    // Get the audio buffer for waveform display
    const ofSoundBuffer& getAudioBuffer() const;
    
    // Reset GUI state to defaults
    void resetGuiState();
};

// Voice: A playback slot with its own audio and video players
// Fixed number of voices provides memory-bounded polyphony
// Audio players load from shared buffers (instant), video players load on demand
struct Voice {
    enum State { FREE, PLAYING, RELEASING };  // RELEASING = fade-out in progress

    // UNIFIED PLAYBACK ENGINE: MediaPlayer handles all playback (audio + video)
    // MediaPlayer provides unified state management, position tracking, and HAP audio handling
    MediaPlayer player;
    
    // AUDIO PROCESSING: VoiceProcessor wraps player.audioPlayer and applies envelope
    // This provides click-free playback with ADSR envelope control
    VoiceProcessor voiceProcessor;
    bool audioConnected = false;              // Whether audio is connected to mixer

    // VIDEO: Handled by MediaPlayer.player.videoPlayer
    bool videoConnected = false;              // Whether video is connected to mixer
    std::string loadedVideoPath;              // Currently loaded video path (for cache check)
    
    // CURRENT PARAMETER VALUES (all modulatable in real-time)
    // Initialized from SampleRef defaults, can be overridden/modulated
    ofParameter<float> speed;           // Current playback speed
    ofParameter<float> volume;          // Current volume
    ofParameter<float> startPosition;   // Current start position (relative to region, 0.0-1.0)
    ofParameter<float> regionStart;    // Current region start (can be modulated!)
    ofParameter<float> regionEnd;       // Current region end (can be modulated!)
    ofParameter<float> grainSize;      // Current grain size in seconds (0 = use full region)
    
    // ENVELOPE PARAMETERS (ADSR - modulatable in real-time)
    ofParameter<float> attackMs;        // Attack time in milliseconds
    ofParameter<float> decayMs;         // Decay time in milliseconds
    ofParameter<float> sustain;        // Sustain level (0.0-1.0)
    ofParameter<float> releaseMs;      // Release time in milliseconds
    
    // State tracking
    int sampleIndex = -1;                     // Which sample this voice is playing
    float startTime = 0.0f;                   // When playback started (for voice stealing LRU)
    State state = FREE;
    uint32_t generation = 0;                  // Incremented each trigger - used to invalidate stale scheduled stops

    // Helper to check if voice is available for allocation
    bool isFree() const { return state == FREE; }
    bool isActive() const { return state == PLAYING || state == RELEASING; }
    bool isPlaying() const { 
        // Check if MediaPlayer is playing OR if envelope is still active (for smooth fade-out)
        return player.isPlaying() || voiceProcessor.isActive();
    }
    
    // Get duration from loaded media
    float getDuration() const;
    
    // Load sample into this voice (audio from shared buffer, video from path)
    bool loadSample(const SampleRef& sample);
    
    // Apply parameters from trigger event
    void applyParameters(float spd, float vol, float pos, float regStart, float regEnd, float loopSz);
    
    // Reset parameters to defaults before each trigger
    void resetToDefaults();
    
    // Playback control
    void play();
    void release();  // Start release phase (smooth fade-out)
    void stop();
    void setPosition(float pos);
};

// Note: StepTriggerParams and TriggerEventData removed - using TriggerEvent directly from Module.h

// MultiSampler: AV sample playback instrument with polyphonic voice allocation
// Formerly known as MediaPool
class MultiSampler : public Module {
public:
    // Voice pool size constant - total number of simultaneous playback slots
    // With shared audio architecture, memory usage is now O(voices) not O(samples × voices)
    static constexpr size_t MAX_VOICES = 16;
    
    // Anti-click fade: minimum fade time to prevent audio discontinuities
    // Applied during voice stealing, emergency stops, and as minimum release time
    static constexpr float ANTI_CLICK_FADE_MS = 5.0f;  // 5ms = ~220 samples @ 44.1kHz
    
    // ADSR envelope defaults (now instance variables for routability)
    // These can be modulated via ParameterRouter when exposed as module parameters
    // FUTURE: Per-voice ADSR modulation (route LFO to individual voice ADSR parameters)
    float defaultAttackMs_ = 0.0f;   // Instant attack for samplers
    float defaultDecayMs_ = 0.0f;    // No decay (sustain at full level)
    float defaultSustain_ = 1.0f;     // Full sustain level
    float defaultReleaseMs_ = 10.0f; // Smooth release, >= ANTI_CLICK_FADE_MS
    int defaultGrainEnvelope_ = 0;    // Grain envelope shape: 0=LINEAR, 1=EXPONENTIAL, 2=GAUSSIAN, 3=HANN, 4=HAMMING
    
    // Constructor with optional directory
    MultiSampler(const std::string& dataDir = "data");
    virtual ~MultiSampler() noexcept;
    
    // Scan directory for media files
    void scanDirectory(const std::string& path);
    
    // Set custom absolute path (bypasses automatic path resolution)
    void setCustomPath(const std::string& absolutePath);
    
    // File pairing strategy (populates sample bank from scanned files)
    void mediaPair();      // Match by base filename
    void pairByIndex();    // Pair in order (1st audio + 1st video, etc.)
    
    // Unified initialization method
    void initialize(Clock* clock, ModuleRegistry* registry, ConnectionManager* connectionManager, 
                    ParameterRouter* parameterRouter, PatternRuntime* patternRuntime = nullptr, 
                    bool isRestored = false) override;
    
    // ========================================================================
    // SAMPLE BANK API - Primary Interface
    // ========================================================================
    // MultiSampler uses a sampler-inspired architecture:
    // - Sample bank: Lightweight metadata (paths, duration, default parameters)
    // - Voice pool: Fixed-size pool of playback slots (MAX_VOICES = 16)
    // - Shared audio: Audio loaded once per sample, shared by all voices
    // - Per-voice video: Each voice has its own video player (decoders can't be shared)
    
    // Sample bank access
    size_t getSampleCount() const { return sampleBank_.size(); }
    const SampleRef& getSample(size_t index) const;
    SampleRef& getSampleMutable(size_t index);
    
    // Display management: Which sample is shown in GUI
    // Display index is "sticky" - it updates automatically when a sample is triggered,
    // but can also be set manually for navigation
    size_t getDisplayIndex() const { return displayIndex_; }
    void setDisplayIndex(size_t index);  // Set display index (does NOT trigger playback)
    
    // Get the currently displayed sample (for GUI state access)
    // Use this to read gui* fields (guiPlayheadPosition, guiSpeed, etc.) for display
    // Returns nullptr if no sample is displayed
    SampleRef* getDisplaySample();
    const SampleRef* getDisplaySample() const;
    
    // Get preview player (only available when scrubbing)
    // NOTE: This returns nullptr unless the sample is actively being scrubbed.
    // For normal playback, use getVoicesForSample() to access voice players.
    // For GUI state, use getDisplaySample()->gui* fields instead.
    MediaPlayer* getDisplayPlayer();
    const MediaPlayer* getDisplayPlayer() const;
    
    // ========================================================================
    // VOICE MANAGEMENT API - For Playback Control
    // ========================================================================
    
    // Trigger a sample for playback (allocates voice, loads sample, starts playback)
    // This is the primary method for starting playback from code
    // Returns the allocated voice, or nullptr if allocation failed
    Voice* triggerSample(int sampleIndex, const TriggerEvent* event = nullptr);
    
    // Trigger a sample with auto-gate (for GUI click-to-preview)
    // Automatically stops playback after gateDuration seconds
    Voice* triggerSamplePreview(int sampleIndex, float gateDuration = 0.5f);
    
    // Get all active voices (PLAYING or RELEASING state)
    std::vector<Voice*> getActiveVoices();
    
    // Get voices playing a specific sample (for polyphonic GUI support)
    // Returns all voices currently playing the specified sample
    std::vector<Voice*> getVoicesForSample(int sampleIndex);
    std::vector<const Voice*> getVoicesForSample(int sampleIndex) const;
    
    // Get first voice playing a sample (convenience method)
    // Returns nullptr if sample is not playing
    Voice* getVoiceForSample(int sampleIndex);
    const Voice* getVoiceForSample(int sampleIndex) const;
    
    // Lightweight state checks (for GUI visualization)
    int getVoiceCountForSample(int sampleIndex) const;  // How many voices are playing this sample
    bool isSamplePlaying(int sampleIndex) const;         // Is this sample currently playing?
    
    // ========================================================================
    // PLAYBACK CONTROL API
    // ========================================================================
    
    // Manual playback control (for GUI buttons)
    // Triggers playback of a sample using current default parameters
    // Returns true if playback started successfully
    bool playMediaManual(size_t index);
    
    // Stop all playback immediately
    void stopAllMedia();
    
    // Playback state queries
    PlaybackMode getCurrentMode() const;  // IDLE or PLAYING
    bool isPlaying() const;               // Returns true if any voice is playing
    void setModeIdle();                   // Force transition to IDLE (for button handlers)
    
    // Play style control (applies to all playback)
    void setPlayStyle(PlayStyle style);   // ONCE, LOOP, GRAIN, or NEXT
    PlayStyle getPlayStyle() const;
    
    // Scrubbing playback (for waveform seeking)
    // Creates a temporary preview player for audio feedback during scrubbing
    void startScrubbingPlayback(size_t index, float position);
    void stopScrubbingPlayback();
    
    // ========================================================================
    // FILE MANAGEMENT API
    // ========================================================================
    
    // Directory management
    void setDataDirectory(const std::string& path);
    void browseForDirectory();  // Opens file dialog
    std::string getDataDirectory() const { return dataDirectory; }
    std::string getMediaDirectory() const { return getDataDirectory(); }  // Alias
    
    // File scanning and pairing (declared above at lines 210-217)
    void refresh();        // Re-scan directory and re-pair files
    
    // Individual file addition (for drag-and-drop)
    bool addMediaFile(const std::string& filePath);
    void addMediaFiles(const std::vector<std::string>& filePaths);
    bool acceptFileDrop(const std::vector<std::string>& filePaths) override;  // Module interface
    
    // File removal
    bool removeSample(size_t index);
    bool removePlayer(size_t index) { return removeSample(index); }  // Legacy alias
    
    // Clear all samples
    void clear();
    
    // File list access (derived from sample bank)
    std::vector<std::string> getAudioFiles() const;
    std::vector<std::string> getVideoFiles() const;
    std::vector<std::string> getPlayerNames() const;        // Display names for GUI
    std::vector<std::string> getPlayerFileNames() const;    // Actual file names
    
    // ========================================================================
    // LEGACY API (Backward Compatibility)
    // ========================================================================
    // These methods are kept for backward compatibility but are deprecated.
    // New code should use the Sample Bank API and Voice Management API above.
    
    // DEPRECATED: Use getDisplaySample() and getVoicesForSample() instead
    // These methods only return preview players when scrubbing, which is confusing.
    // For normal playback, voices are managed internally and accessed via Voice API.
    MediaPlayer* getMediaPlayer(size_t index);  // Returns preview player only when scrubbing
    MediaPlayer* getMediaPlayerByName(const std::string& name);
    MediaPlayer* getCurrentPlayer();  // DEPRECATED: Use getDisplayPlayer() instead
    
    // DEPRECATED: Use setDisplayIndex() and getDisplayIndex() instead
    void setCurrentIndex(size_t index) { setDisplayIndex(index); }
    size_t getCurrentIndex() const { return getDisplayIndex(); }
    size_t getNumPlayers() const { return getSampleCount(); }  // Alias
    
    // DEPRECATED: Navigation methods that modify state and return preview player
    // Use setDisplayIndex() for navigation instead
    MediaPlayer* getNextPlayer();      // Modifies displayIndex_, returns preview player
    MediaPlayer* getPreviousPlayer(); // Modifies displayIndex_, returns preview player
    void nextPlayer() { setDisplayIndex((getDisplayIndex() + 1) % getSampleCount()); }
    void previousPlayer() { 
        size_t idx = getDisplayIndex();
        setDisplayIndex(idx == 0 ? getSampleCount() - 1 : idx - 1);
    }
    
    
    // ========================================================================
    // MODULE INTERFACE IMPLEMENTATION
    // ========================================================================
    
    // Module lifecycle
    void setup(Clock* clockRef);
    void update() override;  // Called every frame - handles voice state updates, loop logic, etc.
    
    // Module metadata
    std::string getName() const override;
    ModuleType getType() const override;
    ModuleMetadata getMetadata() const override;
    
    // Module capabilities
    bool hasCapability(ModuleCapability capability) const override;
    std::vector<ModuleCapability> getCapabilities() const override;
    
    // Module parameters (for sequencer/modulator control)
    std::vector<ParameterDescriptor> getParameters() const override;
    void setParameter(const std::string& paramName, float value, bool notify = true) override;
    float getParameter(const std::string& paramName) const override;
    
    // Module event handling
    void onTrigger(TriggerEvent& event) override;  // Called from audio thread - queues events
    void processEventQueue();  // Called from update() - processes queued events in GUI thread
    
    // Module routing (exposes stable audio/video outputs via internal mixers)
    ofxSoundObject* getAudioOutput() const override;
    ofxVisualObject* getVideoOutput() const override;
    std::vector<Port> getInputPorts() const override;
    std::vector<Port> getOutputPorts() const override;
    
    // Module state
    void setEnabled(bool enabled) override;  // Stops all playback when disabled
    
    // Module serialization
    ofJson toJson(class ModuleRegistry* registry = nullptr) const override;
    void fromJson(const ofJson& json) override;
    
    // ========================================================================
    // ADVANCED CONFIGURATION
    // ========================================================================
    
    // Polyphony mode: Controls how multiple triggers are handled
    PolyphonyMode getPolyphonyMode() const;
    // Note: setPolyphonyMode() is via setParameter("polyphonyMode", 0.0 or 1.0)
    
    // Directory change callback (for GUI updates)
    std::function<void(const std::string&)> onDirectoryChanged;
    void setDirectoryChangeCallback(std::function<void(const std::string&)> callback) { 
        onDirectoryChanged = callback; 
    }
    
    // ========================================================================
    // INTERNAL API (For Preview Players Only)
    // ========================================================================
    // These methods are for internal use and preview player management.
    // Normal playback uses the voice pool which handles connections automatically.
    
    // DEPRECATED: Use setDisplayIndex() instead
    void setActivePlayer(size_t index) { setDisplayIndex(index); }
    
    // DEPRECATED: Use getDisplayPlayer() instead (same behavior)
    MediaPlayer* getActivePlayer() { return getDisplayPlayer(); }
    
    // Internal connection management (for preview players only)
    // Voice connections are handled automatically during allocation/release
    void connectPlayerToInternalMixers(MediaPlayer* player);
    void disconnectPlayerFromInternalMixers(MediaPlayer* player);
    
private:
    Clock* clock;
    
    // ========================================================================
    // SAMPLER-INSPIRED DATA STRUCTURES
    // ========================================================================
    
    // Sample bank: lightweight metadata-only storage (paths, duration, display name)
    // This is what gets serialized to sessions - NO loaded media data
    std::vector<SampleRef> sampleBank_;
    
    // Voice pool: managed by VoiceManager for unified voice allocation
    // Provides memory-bounded polyphony - voices are allocated on trigger, released on stop
    // FUTURE: Make voice pool size configurable per module instance (requires template specialization)
    VoiceManager<Voice, MAX_VOICES> voiceManager_;
    
    // Temporary file lists used during directory scanning (cleared after mediaPair)
    std::vector<std::string> audioFiles;
    std::vector<std::string> videoFiles;
    
    // Display index: which sample is shown in GUI (sticky after trigger, manually changeable)
    size_t displayIndex_ = 0;
    std::string dataDirectory;
    bool isSetup;
    
    // Internal mixers for stable audio/video output
    // All active voices connect to these mixers, providing a stable output interface
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
    
    // Scheduled stop tracking for sequencer-triggered playback
    // Tracks when voices should be released after their gate duration expires
    // Uses generation ID to avoid stopping voices that were reused for new triggers
    struct ScheduledStop {
        Voice* voice;              // Voice to release (not raw player pointer)
        float stopTime;            // Absolute time when voice should stop
        uint32_t expectedGeneration; // Only execute if voice generation matches (prevents stale stop)
    };
    std::vector<ScheduledStop> scheduledStops_;
    
    
    // Diagnostic: Track if onTrigger is being called (for debugging event subscription issues)
    std::atomic<int> onTriggerCallCount_{0};
    
    // Playback state machine (atomic for lock-free reads)
    std::atomic<PlaybackMode> currentMode;
    PlayStyle currentPlayStyle;
    
    // Polyphony mode: controls whether multiple players can play simultaneously
    PolyphonyMode polyphonyMode_;

    // ========================================================================
    // COMPLETE PRELOADING SYSTEM
    // ========================================================================

    // Preload all samples during initialization for zero-latency playback
    bool preloadAllSamples();

    // ========================================================================
    // VOICE ALLOCATION METHODS
    // ========================================================================
    
    // Allocate a voice for playback (prefers warm voice with sample already loaded)
    Voice* allocateVoice(int requestedSampleIndex = -1);
    
    // Release a voice (stop playback, disconnect, free memory)
    void releaseVoice(Voice& voice);
    
    // Release all voices
    void releaseAllVoices();
    
    // Process a trigger event (allocate voice, load sample, play)
    void processEvent(const TriggerEvent& event);
    
    // ========================================================================
    // HELPER METHODS
    // ========================================================================
    
    std::string getBaseName(const std::string& filename);
    void scanMediaFiles(const std::string& path, ofDirectory& dir);
    bool isAudioFile(const std::string& filename);
    bool isVideoFile(const std::string& filename);
    
    // Compute display name from sample paths
    std::string computeDisplayName(const SampleRef& sample) const;
    
    // Add a sample to the bank (from file paths)
    void addSampleToBank(const std::string& audioPath, const std::string& videoPath);
    
    // Position and parameter comparison thresholds
    static constexpr float POSITION_EPSILON = 0.001f;
    static constexpr float POSITION_THRESHOLD = 0.01f;
    static constexpr float PARAMETER_EPSILON = 0.001f;
    
    // Named constants for magic numbers
    static constexpr float MIN_REGION_SIZE = 0.001f;
    static constexpr float MIN_DURATION = 0.001f;
    static constexpr float MIN_LOOP_SIZE = 0.001f;
    static constexpr float REGION_BOUNDARY_THRESHOLD = 0.001f;
    static constexpr float END_POSITION_THRESHOLD = 0.99f;
    static constexpr float INIT_POSITION = 0.01f;
    
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
    
    // Parameter processing helpers
    void resetPlayerToDefaults(MediaPlayer* player);  // Reset player parameters before each trigger
    void applyEventParameters(MediaPlayer* player, const TriggerEvent& event, 
                             const std::vector<ParameterDescriptor>& descriptors);
    
    // GUI state synchronization
    void syncGuiStateFromVoice(size_t sampleIndex, Voice* voice);  // Sync GUI state from active voice
    
    // Position validation helper - clamps position for playback with play style awareness
    float clampPositionForPlayback(float position, PlayStyle playStyle) const;
};
