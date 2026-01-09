#pragma once

#include "ofMain.h"
#include "ofEvents.h"
#include "Module.h"
#include "data/Pattern.h"
#include "data/PatternChain.h"
#include "ofJson.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>

// Forward declarations
class Clock;
struct TimeEvent;
class PatternRuntime;

// TrackerSequencer: Pattern-based step sequencer for triggering media playback
//
// STEP INDEXING CONVENTION:
// - All internal step indices are 0-based (0, 1, 2, ...)
// - Display conversions to 1-based (1, 2, 3, ...) happen only at GUI/log boundaries
// - Methods accepting step parameters expect 0-based indices
// - Log messages and user-facing displays show 1-based step numbers
class TrackerSequencer : public Module {
    friend class TrackerSequencerGUI;  // Allow GUI to access private members for rendering
    
public:

    // Event for step triggers - broadcasts TriggerEvent to all subscribers
    // This makes TrackerSequencer truly modular - receivers subscribe via ofAddListener
    ofEvent<TriggerEvent> triggerEvent;
    
    
    TrackerSequencer();
    ~TrackerSequencer();

    
    void setup(Clock* clockRef);
    
    // Unified initialization method
    void initialize(Clock* clock, ModuleRegistry* registry, ConnectionManager* connectionManager, 
                    ParameterRouter* parameterRouter, PatternRuntime* patternRuntime = nullptr, 
                    bool isRestored = false) override;
    
    // React to runtime connection changes (e.g., from Console)
    void onConnectionEstablished(const std::string& targetModuleName,
                                 Module::ConnectionType connectionType,
                                 ConnectionManager* connectionManager) override;
    
    void onConnectionBroken(const std::string& targetModuleName,
                                 Module::ConnectionType connectionType,
                                 ConnectionManager* connectionManager) override;
    
    // Initialize default pattern based on connected MultiSampler (Phase 8.1)
    // Called after all modules are set up and connections are established
    void initializeDefaultPattern(class ModuleRegistry* registry, class ConnectionManager* connectionManager);
    
    // PatternRuntime integration (Phase 3: Runtime-only architecture)
    bool bindToPattern(const std::string& patternName);  // Bind to a pattern in PatternRuntime, returns true on success
    void onPatternRuntimeTrigger(TriggerEvent& event);    // Event handler for PatternRuntime triggers
    void onPatternDeleted(std::string& deletedPatternName);  // Event handler for pattern deletion cleanup
    void onSequencerBindingChanged(std::string& sequencerName);  // Event handler for immediate binding sync
    
    // Get index range from connected module (replaces callback pattern)
    // Queries connected modules directly via ConnectionManager
    int getIndexRange() const;
    void processAudioBuffer(ofSoundBuffer& buffer);
    void onTimeEvent(TimeEvent& data); // Sample-accurate time event from Clock (filters for STEP type)
    
    // Event listener system
    void addStepEventListener(std::function<void(int, float, const Step&)> listener);
    
    // Module interface implementation
    std::string getName() const override;
    ModuleType getType() const override;
    std::vector<ParameterDescriptor> getParametersImpl() const override;
    void onTrigger(TriggerEvent& event) override; // Sequencers don't receive triggers, but method exists for interface
    void setParameterImpl(const std::string& paramName, float value, bool notify = true) override;
    float getParameterImpl(const std::string& paramName) const override;
    
    // Indexed parameter support (for ParameterRouter step[4].position access)
    bool supportsIndexedParameters() const override { return true; }
    float getIndexedParameter(const std::string& paramName, int index) const override;
    void setIndexedParameter(const std::string& paramName, int index, float value, bool notify = true) override;
    void update() override; // Update step active state (clears manually triggered steps when duration expires)
    
    // Capability interface implementation
    bool hasCapability(ModuleCapability capability) const override;
    std::vector<ModuleCapability> getCapabilities() const override;
    
    // Metadata interface implementation
    ModuleMetadata getMetadata() const override;
    
    // Event access - expose triggerEvent for generic subscription
    ofEvent<TriggerEvent>* getEvent(const std::string& eventName) override;
    
    // Port-based routing interface (Phase 1)
    std::vector<Port> getInputPorts() const override;
    std::vector<Port> getOutputPorts() const override;
    
    // Unified parameter registry for tracker-specific parameters
    // Returns all tracker-specific parameters (index, note, length, chance)
    // Note: index and length ranges are dynamic and need instance context
    std::vector<ParameterDescriptor> getTrackerParameters() const;
    
    // Static helper to get tracker parameter descriptor (for static contexts)
    // Returns descriptor with default ranges (caller should update ranges if needed)
    static ParameterDescriptor getTrackerParameterDescriptor(const std::string& paramName);
    
    // Expose TrackerSequencer parameters (for discovery by modules)
    // Combined: internal + external parameters
    // externalParams: optional list of external parameters from connected modules (provided by GUI layer)
    // If not provided, returns internal parameters + hardcoded defaults for backward compatibility
    std::vector<ParameterDescriptor> getAvailableParameters(const std::vector<ParameterDescriptor>& externalParams = {}) const;
    
    // Static helper to get default parameters (for backward compatibility when no external params)
    static std::vector<ParameterDescriptor> getDefaultParameters();
    
    // DEPRECATED: Use getTrackerParameters() instead
    // This method is kept for backward compatibility but will be removed in a future version
    // @deprecated Use getTrackerParameters() which includes all tracker-specific parameters
    static std::vector<ParameterDescriptor> getInternalParameters();
    
    // Transport listener for Clock play/stop events
    void onTransportChanged(bool isPlaying) override; // Module interface
    void onClockTransportChanged(bool isPlaying); // Internal implementation (called by Clock)
    
    // Pattern management
    void setStep(int stepIndex, const Step& step);
    Step getStep(int stepIndex) const;
    void setStepCount(int stepCount);  // Set step count for current pattern only
    void clearStep(int stepIndex);
    void clearPattern();
    void randomizePattern();
    void randomizeColumn(int columnIndex);  // Randomize a specific column (absolute index: 1 = index, 2 = length, 3+ = parameter columns)
    void applyLegato();  // Apply legato to length column (extend lengths to connect steps)
    
    // Multi-step duplication: copy a range of steps to a destination
    // fromStep: inclusive start of source range
    // toStep: inclusive end of source range
    // destinationStep: where to copy the range (overwrites existing cells)
    // Returns true if successful, false if range is invalid
    bool duplicateRange(int fromStep, int toStep, int destinationStep);
    
    // Clipboard operations for step copy/paste/cut
    // StepClipboard: stores copied/cut steps for paste operations
    struct StepClipboard {
        std::vector<Step> steps;  // Copied steps
        int startStep = -1;       // Original start position (for reference)
        int endStep = -1;         // Original end position (for reference)
        
        bool isEmpty() const { return steps.empty(); }
        void clear() { 
            steps.clear(); 
            startStep = -1; 
            endStep = -1; 
        }
    };
    
    // Clipboard operations
    void copySteps(int fromStep, int toStep);  // Copy range to clipboard
    void cutSteps(int fromStep, int toStep);    // Cut range to clipboard (copy + clear)
    bool pasteSteps(int destinationStep);       // Paste from clipboard, returns true if successful
    void duplicateSteps(int fromStep, int toStep, int destinationStep);  // Duplicate range (uses duplicateRange)
    void clearStepRange(int fromStep, int toStep);  // Clear multiple steps
    
    // Pattern management (Phase 3: All patterns are in PatternRuntime)
    int getNumPatterns() const;  // Returns number of patterns in PatternRuntime
    std::string getCurrentPatternName() const { return boundPatternName_; }  // Get bound pattern name
    void setCurrentPatternName(const std::string& patternName);  // Bind to a different pattern
    std::vector<std::string> getAllPatternNames() const;  // Get all pattern names from PatternRuntime
    
    // Pattern management methods (name-based - primary API)
    std::string addPatternByName();  // Add pattern, returns pattern name
    void removePatternByName(const std::string& patternName);  // Remove pattern by name
    void copyPatternByName(const std::string& sourceName, const std::string& destName);  // Copy pattern by name
    std::string duplicatePatternByName(const std::string& patternName);  // Duplicate pattern by name, returns new pattern name
    
    // Helper: Get pattern name by index (for PatternChain compatibility - indices are unstable, prefer names)
    std::string getPatternNameByIndex(int index) const;  // Get pattern name at index, returns empty if invalid
    
    // DEPRECATED: Index-based methods (kept for backward compatibility, will be removed)
    // Use name-based methods instead for proper integration
    int getCurrentPatternIndex() const;  // Returns index of bound pattern in PatternRuntime pattern list
    void setCurrentPatternIndex(int index);  // Bind to pattern at given index
    int addPattern();  // Add a new empty pattern to Runtime, returns index
    void removePattern(int index);  // Remove a pattern at given index
    void copyPattern(int sourceIndex, int destIndex);  // Copy one pattern to another by index
    void duplicatePattern(int index);  // Duplicate a pattern by index
    
    // Pattern chain (pattern chaining) support (uses pattern names)
    // Phase 2: Chains are now in PatternRuntime, accessed via sequencer bindings
    // These methods delegate to PatternRuntime chains when bound, fallback to internal chain for backward compatibility
    int getPatternChainSize() const;
    int getCurrentChainIndex() const;
    void setCurrentChainIndex(int index);
    void addToPatternChain(const std::string& patternName);
    void removeFromPatternChain(int chainIndex);
    void clearPatternChain();
    std::string getPatternChainEntry(int chainIndex) const;
    void setPatternChainEntry(int chainIndex, const std::string& patternName);
    const std::vector<std::string>& getPatternChain() const;
    
    // Pattern chain repeat counts
    int getPatternChainRepeatCount(int chainIndex) const;
    void setPatternChainRepeatCount(int chainIndex, int repeatCount);
    
    // Pattern chain toggle
    bool getUsePatternChain() const;
    void setUsePatternChain(bool use);
    
    // Pattern chain disable (temporary disable during playback for performance)
    bool isPatternChainEntryDisabled(int chainIndex) const;
    void setPatternChainEntryDisabled(int chainIndex, bool disabled);
    
    // Reload pattern chain from JSON (used after migration when patterns are available)
    void reloadPatternChain(const ofJson& json, const std::vector<std::string>& availablePatternNames);
    
    // Chain binding (Phase 2: Use PatternRuntime chains)
    void bindToChain(const std::string& chainName);
    void unbindChain();
    std::string getBoundChainName() const { return boundChainName_; }
    
    // Helper to get current chain (from PatternRuntime or fallback to internal)
    PatternChain* getCurrentChain();
    const PatternChain* getCurrentChain() const;
    
    // Helper to get current pattern (for internal use)
    Pattern& getCurrentPattern();
    const Pattern& getCurrentPattern() const;
    
    // Playback control
    void play();
    void pause();
    void stop();
    void reset();
    void setCurrentStep(int step);
    void advanceStep();
    void triggerStep(int step);
    
    // State management
    // DEPRECATED: Use SessionManager instead for unified session saving/loading
    // These methods are kept for backward compatibility but should not be used in new code
    [[deprecated("Use SessionManager::saveSession() instead")]]
    bool loadState(const std::string& filename);
    [[deprecated("Use SessionManager::saveSession() instead")]]
    bool saveState(const std::string& filename) const;
    
    // Module serialization interface
    ofJson toJson(class ModuleRegistry* registry = nullptr) const override;
    void fromJson(const ofJson& json) override;
    // getTypeName() uses default implementation from Module base class
    
    // UI interaction
    // Note: GUI state (editStep, editColumn, isEditingCell, editBufferCache) is managed by TrackerSequencerGUI
    // Keyboard input handling has been moved to TrackerSequencerGUI::handleKeyPress()
    // Mouse click handling is managed by TrackerSequencerGUI (Module::handleMouseClick uses default empty implementation)
    
    // Getters
    int getStepCount() const;  // Returns current pattern's step count
    int getCurrentStep() const { return playbackState.playbackStep; }  // Backward compatibility: returns playback step
    int getPlaybackStep() const { return playbackState.playbackStep; }
    int getPlaybackStepIndex() const { return playbackState.playbackStep; }  // GUI compatibility alias
    // Sequencer playback state - derived from Clock transport state
    // This represents whether the sequencer is actively advancing steps.
    // It's synchronized with Clock via onClockTransportChanged() listener.
    // NOTE: This is sequencer-specific state, not global transport state.
    // For global transport state, query clock->isPlaying() instead.
    bool isPlaying() const { return playbackState.isPlaying; }
    int getCurrentPlayingStep() const { return playbackState.currentPlayingStep; }
    
    // Note: GUI state accessors (editStep, editColumn, isEditingCell, editBufferCache) removed
    // Use TrackerSequencerGUI::getEditStep(), etc. instead
    
    // Pattern cell accessor for GUI
    Step& getPatternStep(int stepIndex) { return getCurrentPattern()[stepIndex]; }
    const Step& getPatternStep(int stepIndex) const { return getCurrentPattern()[stepIndex]; }
    
    // Drag state accessors for GUI (still needed for BaseCell/NumCell interaction)
    float getDragStartY() const { return dragStartY; }
    float getDragStartX() const { return dragStartX; }
    float getLastDragValue() const { return lastDragValue; }
    void setLastDragValue(float value) { lastDragValue = value; }
    
    // Note: Focus management flags removed - these are GUI concerns managed by TrackerSequencerGUI
    // Update step active state (clears manually triggered steps when duration expires)
    void updateStepActiveState();
    float getCurrentBpm() const;
    float getStepsPerBeat() const { return getCurrentPattern().getStepsPerBeat(); }
    
    // Setters
    void setStepsPerBeat(float steps);  // Supports fractional values (1/2, 1/4, 1/8) and negative for backward reading
    
    // Parameter synchronization methods (for ParameterRouter system)
    // Get position parameter from current edit step (for sync)
    float getCurrentStepPosition() const;
    
    // Set position parameter for current edit step (for sync)
    void setCurrentStepPosition(float position);
    
    // Parameter change callback (for ParameterRouter)
    void setParameterChangeCallback(std::function<void(const std::string&, float)> callback) {
        parameterChangeCallback = callback;
    }

private:
    bool isValidStep(int step) const;
    bool isPatternEmpty() const;
    void notifyStepEvent(int step, float stepLength);
    void updateStepInterval();
    
    
    // Column configuration management (delegates to current pattern)
    // Column configuration is now stored per-pattern in Pattern class
    // Note: ColumnConfig is defined at namespace scope in Pattern.h
    using ColumnConfig = ::ColumnConfig;  // Alias for backward compatibility (ColumnConfig is in global namespace from Pattern.h)
    void initializeDefaultColumns() { getCurrentPattern().initializeDefaultColumns(); }
    void addColumn(const std::string& parameterName, const std::string& displayName, int position = -1) {
        getCurrentPattern().addColumn(parameterName, displayName, position);
    }
    void removeColumn(int columnIndex) { getCurrentPattern().removeColumn(columnIndex); }
    void reorderColumn(int fromIndex, int toIndex) { getCurrentPattern().reorderColumn(fromIndex, toIndex); }
    void swapColumnParameter(int columnIndex, const std::string& newParameterName, const std::string& newDisplayName = "") {
        getCurrentPattern().swapColumnParameter(columnIndex, newParameterName, newDisplayName);
    }
    bool isColumnFixed(int columnIndex) const {
        const auto& col = getCurrentPattern().getColumnConfig(columnIndex);
        return col.isRequired; // Fixed = required (not removable)
    }
    const ColumnConfig& getColumnConfig(int columnIndex) const { return getCurrentPattern().getColumnConfig(columnIndex); }
    int getColumnCount() const { return getCurrentPattern().getColumnCount(); }
    const std::vector<ColumnConfig>& getColumnConfiguration() const { return getCurrentPattern().getColumnConfiguration(); }
    
    // Pattern interaction methods
    // Mouse click handling has been moved to TrackerSequencerGUI
    
    // Parameter range conversion helpers (use actual parameter ranges, not 0-127)
    static std::pair<float, float> getParameterRange(const std::string& paramName);
    static float getParameterDefault(const std::string& paramName); // Note: Uses getAvailableParameters() which is non-static
    static ParameterType getParameterType(const std::string& paramName); // Get parameter type dynamically
    static std::string formatParameterValue(const std::string& paramName, float value); // Format based on parameter type
    
    Clock* clock;
    bool listenersRegistered_ = false;  // Flag to prevent double listener registration
    
    // Pattern sequencer state (app-specific)
    float stepsPerBeat = 4.0f;  // Supports fractional values (1/2, 1/4, 1/8) and negative for backward reading
    bool gatingEnabled = true;
    
    // Pattern chaining (Phase 3: Patterns are now in PatternRuntime, chain references pattern names)
    PatternChain patternChain;     // Pattern chain for pattern chaining (sequence of pattern names)
    
    // Note: stepCount is now per-pattern (use getCurrentPattern().getStepCount())
    
    // Consolidated playback state
    struct PlaybackState {
        int playbackStep = 0;           // Sequencer position in pattern (advances every step, wraps around)
        int currentPlayingStep = -1;    // Step currently playing audio/media (-1 if none, set when media triggers)
        bool isPlaying = false;         // Whether sequencer is actively playing
        
        // Timing state for current playing step
        float stepStartTime = 0.0f;    // When current step started (unified for manual and playback)
        float stepEndTime = 0.0f;       // When current step should end (calculated from duration)
        
        // Audio-rate timing system
        double sampleAccumulator = 0.0; // Sample accumulator for step timing
        float lastBpm = 120.0f;         // Last known BPM for timing calculations
        
        // Pattern cycle counting for ratio conditional triggering
        int patternCycleCount = 0;      // Global cycle counter (increments when pattern wraps, resets on transport stop)
        
        void reset() {
            playbackStep = 0;
            currentPlayingStep = -1;
            isPlaying = false;
            stepStartTime = 0.0f;
            stepEndTime = 0.0f;
            sampleAccumulator = 0.0;
            patternCycleCount = 0;
        }
        
        void clearPlayingStep() {
            currentPlayingStep = -1;
            stepStartTime = 0.0f;
            stepEndTime = 0.0f;
        }
    };
    PlaybackState playbackState;
    
    // Note: GUI state (editStep, editColumn, isEditingCell, editBufferCache) moved to TrackerSequencerGUI
    
    // Drag state for parameter cell editing (moved from static variables to avoid loop issues)
    int draggingStep;      // Step being dragged (-1 if not dragging)
    int draggingColumn;    // Column being dragged (-1 if not dragging)
    float lastDragValue;   // Last drag value (float for precision with float parameters)
    float dragStartY;      // Y position when drag started
    float dragStartX;      // X position when drag started (for horizontal dragging)
    
    // Step event listeners
    std::vector<std::function<void(int, float, const Step&)>> stepEventListeners;
    
    // Connection management for index range discovery
    ConnectionManager* connectionManager_;  // Stored reference for querying connections
    
    // Connection tracking: store names of modules connected via EVENT connections
    // Used to invalidate parameter cache when connections change
    std::set<std::string> connectedModuleNames_;  // Names of modules connected via EVENT
    
    // PatternRuntime integration (Phase 3: Runtime-only architecture)
    PatternRuntime* patternRuntime_ = nullptr;  // Reference to PatternRuntime
    std::string boundPatternName_;              // Name of bound pattern in PatternRuntime
    std::string boundChainName_;                // Name of bound chain in PatternRuntime (optional)
    
    // Note: GUI state (showGUI, cell focus, etc.) is managed by TrackerSequencerGUI
    
    // Parameter change callback (for ParameterRouter system)
    std::function<void(const std::string&, float)> parameterChangeCallback;
    
    // Pending edit system for playback editing
    // Pending edit system - queues edits during playback to apply on next trigger
    struct PendingEdit {
        enum class EditType {
            NONE,
            PARAMETER,  // Set any parameter value (index, length, note, chance, or external params)
            REMOVE      // Remove parameter
        };
        
        int step = -1;
        int column = -1;
        EditType type = EditType::NONE;
        std::string parameterName;
        float value = 0.0f;  // Used for all parameters (setParameterValue handles conversion)
        
        bool isValid() const { return step >= 0 && type != EditType::NONE; }
        void clear() { *this = PendingEdit(); }
    };
    PendingEdit pendingEdit;  // Stores edit queued for next trigger
    
    // Helper to apply pending edit
    void applyPendingEdit();
    
    // Helper to determine if edit should be queued (during playback on current step)
    bool shouldQueueEdit(int editStep, int editColumn) const;
    
    // Static clipboard (shared across all TrackerSequencer instances)
    static StepClipboard clipboard;
    
    // CellWidget adapter methods removed - moved to TrackerSequencerGUI
    // Use TrackerSequencerGUI::createParameterCellForColumn() instead
};


