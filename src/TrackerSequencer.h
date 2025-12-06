#pragma once

#include "ofMain.h"
#include "ofEvents.h"
#include "Module.h"
#include "Pattern.h"
#include "CellWidget.h"
#include "PatternChain.h"
#include "ofJson.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>

// Forward declarations
class Clock;
struct TimeEvent;

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
    
    // New unified initialization method (Phase 2.2)
    void initialize(Clock* clock, ModuleRegistry* registry, ConnectionManager* connectionManager, 
                    ParameterRouter* parameterRouter, bool isRestored) override;
    
    // React to runtime connection changes (e.g., from Console)
    void onConnectionEstablished(const std::string& targetModuleName,
                                 Module::ConnectionType connectionType,
                                 ConnectionManager* connectionManager) override;
    
    void onConnectionBroken(const std::string& targetModuleName,
                           Module::ConnectionType connectionType,
                           ConnectionManager* connectionManager) override;
    
    // Initialize default pattern based on connected MediaPool (Phase 8.1)
    // Called after all modules are set up and connections are established
    void initializeDefaultPattern(class ModuleRegistry* registry, class ConnectionManager* connectionManager);
    
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
    std::vector<ParameterDescriptor> getParameters() const override;
    void onTrigger(TriggerEvent& event) override; // Sequencers don't receive triggers, but method exists for interface
    void setParameter(const std::string& paramName, float value, bool notify = true) override;
    float getParameter(const std::string& paramName) const override;
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
    
    // Multi-pattern support
    int getNumPatterns() const { return (int)patterns.size(); }
    int getCurrentPatternIndex() const { return currentPatternIndex; }
    void setCurrentPatternIndex(int index);
    int addPattern();  // Add a new empty pattern, returns its index
    void removePattern(int index);  // Remove a pattern (cannot remove if it's the only one)
    void copyPattern(int sourceIndex, int destIndex);  // Copy one pattern to another
    void duplicatePattern(int index);  // Duplicate a pattern (adds new pattern)
    
    // Pattern chain (pattern chaining) support
    int getPatternChainSize() const { return patternChain.getSize(); }
    int getCurrentChainIndex() const { return patternChain.getCurrentIndex(); }
    void setCurrentChainIndex(int index);
    void addToPatternChain(int patternIndex);
    void removeFromPatternChain(int chainIndex);
    void clearPatternChain() { patternChain.clear(); }
    int getPatternChainEntry(int chainIndex) const { return patternChain.getEntry(chainIndex); }
    void setPatternChainEntry(int chainIndex, int patternIndex);
    const std::vector<int>& getPatternChain() const { return patternChain.getChain(); }
    
    // Pattern chain repeat counts
    int getPatternChainRepeatCount(int chainIndex) const { return patternChain.getRepeatCount(chainIndex); }
    void setPatternChainRepeatCount(int chainIndex, int repeatCount) { patternChain.setRepeatCount(chainIndex, repeatCount); }
    
    // Pattern chain toggle
    bool getUsePatternChain() const { return patternChain.isEnabled(); }
    void setUsePatternChain(bool use) { patternChain.setEnabled(use); }
    
    // Pattern chain disable (temporary disable during playback for performance)
    bool isPatternChainEntryDisabled(int chainIndex) const { return patternChain.isEntryDisabled(chainIndex); }
    void setPatternChainEntryDisabled(int chainIndex, bool disabled) { patternChain.setEntryDisabled(chainIndex, disabled); }
    
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
    ofJson toJson() const override;
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
    
    // Drag state accessors for GUI (still needed for CellWidget interaction)
    float getDragStartY() const { return dragStartY; }
    float getDragStartX() const { return dragStartX; }
    float getLastDragValue() const { return lastDragValue; }
    void setLastDragValue(float value) { lastDragValue = value; }
    
    // Note: Focus management flags removed - these are GUI concerns managed by TrackerSequencerGUI
    // Update step active state (clears manually triggered steps when duration expires)
    void updateStepActiveState();
    float getCurrentBpm() const;
    int getStepsPerBeat() const { return stepsPerBeat; }
    
    // Setters
    void setStepsPerBeat(int steps);
    
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
    
    // Pattern sequencer state (app-specific)
    int stepsPerBeat = 4;
    bool gatingEnabled = true;
    
    // Multi-pattern support
    std::vector<Pattern> patterns;  // Pattern bank
    int currentPatternIndex = 0;  // Currently active pattern
    PatternChain patternChain;     // Pattern chain for pattern chaining (sequence of pattern indices)
    
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
        
        void reset() {
            playbackStep = 0;
            currentPlayingStep = -1;
            isPlaying = false;
            stepStartTime = 0.0f;
            stepEndTime = 0.0f;
            sampleAccumulator = 0.0;
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
    
    // Note: GUI state (showGUI, cell focus, etc.) is managed by TrackerSequencerGUI
    
    // Parameter change callback (for ParameterRouter system)
    std::function<void(const std::string&, float)> parameterChangeCallback;
    
    // Pending edit system for playback editing
    // Pending edit system - queues edits during playback to apply on next trigger
    struct PendingEdit {
        enum class EditType {
            NONE,
            PARAMETER,  // Set parameter value
            INDEX,      // Set index value
            LENGTH,     // Set length value
            REMOVE      // Remove parameter
        };
        
        int step = -1;
        int column = -1;
        EditType type = EditType::NONE;
        std::string parameterName;
        float value = 0.0f;
        int intValue = -1;  // Used for index and length
        
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


