#pragma once

#include "ofMain.h"
#include "ofEvents.h"
#include "Module.h"
#include "Pattern.h"
#include "CellWidget.h"
#include "ofJson.h"
#include <string>
#include <vector>
#include <map>
#include <functional>

// Forward declarations
class Clock;
struct TimeEvent;

class TrackerSequencer : public Module {
    friend class TrackerSequencerGUI;  // Allow GUI to access private members for rendering
    
public:

    // Event for step triggers - broadcasts TriggerEvent to all subscribers
    // This makes TrackerSequencer truly modular - receivers subscribe via ofAddListener
    ofEvent<TriggerEvent> triggerEvent;
    
    
    TrackerSequencer();
    ~TrackerSequencer();

    // Callback types for querying external state
    using IndexRangeCallback = std::function<int()>;
    
    void setup(Clock* clockRef, int steps = 16);
    void setIndexRangeCallback(IndexRangeCallback callback);
    void processAudioBuffer(ofSoundBuffer& buffer);
    void onTimeEvent(TimeEvent& data); // Sample-accurate time event from Clock (filters for STEP type)
    
    // Event listener system
    void addStepEventListener(std::function<void(int, float, const PatternCell&)> listener);
    
    // Module interface implementation
    std::string getName() const override;
    ModuleType getType() const override;
    std::vector<ParameterDescriptor> getParameters() override;
    void onTrigger(TriggerEvent& event) override; // Sequencers don't receive triggers, but method exists for interface
    void setParameter(const std::string& paramName, float value, bool notify = true) override;
    
    // Expose TrackerSequencer parameters (for discovery by modules)
    // Internal parameters: sequencer-specific (note, chance) - not sent to external modules
    std::vector<ParameterDescriptor> getInternalParameters() const;
    // Combined: internal + external parameters
    // externalParams: optional list of external parameters from connected modules (provided by GUI layer)
    // If not provided, returns internal parameters + hardcoded defaults for backward compatibility
    std::vector<ParameterDescriptor> getAvailableParameters(const std::vector<ParameterDescriptor>& externalParams = {}) const;
    
    // Transport listener for Clock play/stop events
    void onClockTransportChanged(bool isPlaying);
    
    // Pattern management
    void setCell(int step, const PatternCell& cell);
    PatternCell getCell(int step) const;
    void setStepCount(int steps);  // Set step count for current pattern only
    void clearCell(int step);
    void clearPattern();
    void randomizePattern();
    void randomizeColumn(int columnIndex);  // Randomize a specific column (0 = index, 1 = length, 2+ = parameter columns)
    void applyLegato();  // Apply legato to length column (extend lengths to connect steps)
    
    // Multi-step duplication: copy a range of steps to a destination
    // fromStep: inclusive start of source range
    // toStep: inclusive end of source range
    // destinationStep: where to copy the range (overwrites existing cells)
    // Returns true if successful, false if range is invalid
    bool duplicateRange(int fromStep, int toStep, int destinationStep);
    
    // Multi-pattern support
    int getNumPatterns() const { return (int)patterns.size(); }
    int getCurrentPatternIndex() const { return currentPatternIndex; }
    void setCurrentPatternIndex(int index);
    int addPattern();  // Add a new empty pattern, returns its index
    void removePattern(int index);  // Remove a pattern (cannot remove if it's the only one)
    void copyPattern(int sourceIndex, int destIndex);  // Copy one pattern to another
    void duplicatePattern(int index);  // Duplicate a pattern (adds new pattern)
    
    // Pattern chain (pattern chaining) support
    int getPatternChainSize() const { return (int)patternChain.size(); }
    int getCurrentChainIndex() const { return currentChainIndex; }
    void setCurrentChainIndex(int index);
    void addToPatternChain(int patternIndex);  // Add pattern to chain
    void removeFromPatternChain(int chainIndex);  // Remove entry from chain
    void clearPatternChain();  // Clear pattern chain
    int getPatternChainEntry(int chainIndex) const;  // Get pattern index at chain position
    void setPatternChainEntry(int chainIndex, int patternIndex);  // Set pattern at chain position
    const std::vector<int>& getPatternChain() const { return patternChain; }
    
    // Pattern chain repeat counts
    int getPatternChainRepeatCount(int chainIndex) const;  // Get repeat count for chain entry (1-99)
    void setPatternChainRepeatCount(int chainIndex, int repeatCount);  // Set repeat count (1-99)
    
    // Pattern chain toggle
    bool getUsePatternChain() const { return usePatternChain; }
    void setUsePatternChain(bool use) { usePatternChain = use; }
    
    // Pattern chain disable (temporary disable during playback for performance)
    bool isPatternChainEntryDisabled(int chainIndex) const;
    void setPatternChainEntryDisabled(int chainIndex, bool disabled);
    
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
    bool loadState(const std::string& filename);
    bool saveState(const std::string& filename) const;
    
    // Module serialization interface
    ofJson toJson() const override;
    void fromJson(const ofJson& json) override;
    // getTypeName() uses default implementation from Module base class
    
    // UI interaction
    // Note: GUI state (editStep, editColumn, isEditingCell, editBufferCache) is now managed by TrackerSequencerGUI
    // These methods accept GUI state as parameters instead of using member variables
    struct GUIState {
        int editStep = -1;
        int editColumn = -1;
        bool isEditingCell = false;
        std::string editBufferCache;
        bool editBufferInitializedCache = false;
        bool shouldRefocusCurrentCell = false;  // For maintaining focus after exiting edit mode via Enter
    };
    bool handleKeyPress(int key, bool ctrlPressed, bool shiftPressed, GUIState& guiState);
    bool handleKeyPress(ofKeyEventArgs& keyEvent, GUIState& guiState); // Overload for ofKeyEventArgs
    void handleMouseClick(int x, int y, int button);
    
    // Getters
    int getStepCount() const;  // Returns current pattern's step count
    int getCurrentStep() const { return playbackStep; }  // Backward compatibility: returns playback step
    int getPlaybackStep() const { return playbackStep; }
    int getPlaybackStepIndex() const { return playbackStep; }  // GUI compatibility alias
    // Sequencer playback state - derived from Clock transport state
    // This represents whether the sequencer is actively advancing steps.
    // It's synchronized with Clock via onClockTransportChanged() listener.
    // NOTE: This is sequencer-specific state, not global transport state.
    // For global transport state, query clock->isPlaying() instead.
    bool isPlaying() const { return playing; }
    int getCurrentPlayingStep() const { return currentPlayingStep; }
    
    // Note: GUI state accessors (editStep, editColumn, isEditingCell, editBufferCache) removed
    // Use TrackerSequencerGUI::getEditStep(), etc. instead
    
    // Pattern cell accessor for GUI
    PatternCell& getPatternCell(int step) { return getCurrentPattern()[step]; }
    const PatternCell& getPatternCell(int step) const { return getCurrentPattern()[step]; }
    
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
        return !col.isRemovable;
    }
    const ColumnConfig& getColumnConfig(int columnIndex) const { return getCurrentPattern().getColumnConfig(columnIndex); }
    int getColumnCount() const { return getCurrentPattern().getColumnCount(); }
    const std::vector<ColumnConfig>& getColumnConfiguration() const { return getCurrentPattern().getColumnConfiguration(); }
    
    // Pattern interaction methods
    bool handlePatternGridClick(int x, int y);
    bool handlePatternRowClick(int step, int column); // Unused - kept for API compatibility
    
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
    std::vector<int> patternChain;  // Pattern chain for pattern chaining (sequence of pattern indices)
    std::map<int, int> patternChainRepeatCounts;  // Repeat counts for each chain entry (default: 1)
    std::map<int, bool> patternChainDisabled;  // Disabled state for each chain entry (temporary disable during playback)
    int currentChainIndex = 0;  // Current position in pattern chain
    int currentChainRepeat = 0;  // Current repeat count for current chain entry
    bool usePatternChain = true;  // If true, use pattern chain for playback; if false, use currentPatternIndex
    
    // Note: numSteps removed - step count is now per-pattern (use getCurrentPattern().getStepCount())
    int playbackStep;  // Currently playing step (for visual indicator)
    // Note: GUI state (editStep, editColumn, isEditingCell, editBufferCache) moved to TrackerSequencerGUI
    
    // Drag state for parameter cell editing (moved from static variables to avoid loop issues)
    int draggingStep;      // Step being dragged (-1 if not dragging)
    int draggingColumn;    // Column being dragged (-1 if not dragging)
    float lastDragValue;   // Last drag value (float for precision with float parameters)
    float dragStartY;      // Y position when drag started
    float dragStartX;      // X position when drag started (for horizontal dragging)
    int lastTriggeredStep;
    bool playing;  // Renamed from isPlaying to avoid conflict
    
    // Track current media playback
    int currentMediaStartStep;
    float currentMediaStepLength;
    
    // Audio-rate timing system
    double sampleAccumulator; // Sample accumulator for step timing
    float lastBpm; // Last known BPM for timing calculations
    
    // Unified timing system for step duration (works for both manual and playback triggers)
    float stepStartTime;      // When current step started (unified for manual and playback)
    float stepEndTime;        // When current step should end (calculated from duration)
    
    // Step event listeners
    std::vector<std::function<void(int, float, const PatternCell&)>> stepEventListeners;
    
    // Callback for querying external state
    IndexRangeCallback indexRangeCallback;
    
    // UI state
    bool showGUI;
    
    // Step playback tracking
    int currentPlayingStep;  // Current step that's playing (for GUI visualization)
    
    // Note: Cell focus management flags removed - these are GUI concerns managed by TrackerSequencerGUI
    
    // Parameter change callback (for ParameterRouter system)
    std::function<void(const std::string&, float)> parameterChangeCallback;
    
    // Pending edit system for playback editing
    struct PendingEdit {
        int step;
        int column;
        std::string parameterName;
        float value;
        bool isIndex;
        int indexValue;
        bool isLength;
        int lengthValue;
        bool shouldRemove;
        
        PendingEdit() : step(-1), column(-1), value(0.0f), isIndex(false), indexValue(-1), 
                       isLength(false), lengthValue(1), shouldRemove(false) {}
    };
    PendingEdit pendingEdit;  // Stores edit queued for next trigger
    
    // Helper to apply pending edit
    void applyPendingEdit();
    
    // Helper to determine if edit should be queued (during playback on current step)
    // Note: GUI state parameters added - editStep and editColumn are now passed in
    bool shouldQueueEdit(int editStep, int editColumn) const;
    
    // CellWidget adapter methods - bridge PatternCell to CellWidget
    // Creates and configures a CellWidget for a specific step/column
    CellWidget createParameterCellForColumn(int step, int column);
    
    // Configures callbacks for a CellWidget to connect to PatternCell operations
    void configureParameterCellCallbacks(CellWidget& cell, int step, int column);
};


