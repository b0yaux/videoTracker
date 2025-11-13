#pragma once

#include "ofMain.h"
#include "ofEvents.h"
#include "Module.h"
#include "Pattern.h"
#include "ParameterCell.h"
#include <string>
#include <vector>
#include <map>
#include <functional>

// Forward declarations
class Clock;
struct StepEventData;

class TrackerSequencer {
    friend class TrackerSequencerGUI;  // Allow GUI to access private members for rendering
    
public:

    // Event for step triggers - broadcasts TriggerEvent to all subscribers
    // This makes TrackerSequencer truly modular - receivers subscribe via ofAddListener
    ofEvent<TriggerEvent> triggerEvent;
    
    // Legacy event (kept for backward compatibility during migration)
    ofEvent<void> stepEvent;
    
    TrackerSequencer();
    ~TrackerSequencer();

    // Callback types for querying external state
    using IndexRangeCallback = std::function<int()>;
    
    void setup(Clock* clockRef, int steps = 16);
    void setIndexRangeCallback(IndexRangeCallback callback);
    void processAudioBuffer(ofSoundBuffer& buffer);
    void onStepEvent(StepEventData& data); // Sample-accurate step event from Clock
    
    // Event listener system
    void addStepEventListener(std::function<void(int, float, const PatternCell&)> listener);
    
    // Expose TrackerSequencer parameters (for discovery by modules)
    // TrackerSequencer exposes its own parameters (note, position, speed, volume)
    // Modules map these to their own parameters (e.g., note → mediaIndex)
    // NOTE: TrackerSequencer does NOT inherit from Module (SunVox-style)
    // This prepares for future BespokeSynth-style migration where TrackerSequencer becomes a Module
    std::vector<ParameterDescriptor> getAvailableParameters() const;
    
    // Module interface compatibility methods (for future Module inheritance)
    std::vector<ParameterDescriptor> getParameters(); // Alias for getAvailableParameters
    void onTrigger(TriggerEvent& event); // Sequencers don't receive triggers, but method exists for interface
    void setParameter(const std::string& paramName, float value, bool notify = true); // Interface compliance
    
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
    
    // UI interaction
    bool handleKeyPress(int key, bool ctrlPressed = false, bool shiftPressed = false);
    bool handleKeyPress(ofKeyEventArgs& keyEvent); // Overload for ofKeyEventArgs
    void handleMouseClick(int x, int y, int button);
    bool isKeyboardFocused() const; // Check if keyboard input should be routed here
    
    // Getters
    int getStepCount() const;  // Returns current pattern's step count
    int getCurrentStep() const { return playbackStep; }  // Backward compatibility: returns playback step
    int getPlaybackStep() const { return playbackStep; }
    int getPlaybackStepIndex() const { return playbackStep; }  // GUI compatibility alias
    int getEditStep() const { return editStep; }
    int getEditColumn() const { return editColumn; }
    int getEditingStepIndex() const { return editStep; }  // GUI compatibility alias
    int getEditingColumnIndex() const { return editColumn; }  // GUI compatibility alias
    void setEditCell(int step, int column) { 
        editStep = step; 
        editColumn = column; 
    }
    void setEditingStepIndex(int step) { editStep = step; }  // GUI compatibility
    void setEditingColumnIndex(int column) { editColumn = column; }  // GUI compatibility
    bool isPlaying() const { return playing; }
    bool getIsEditingCell() const { return isEditingCell; }
    bool isInEditMode() const { return isEditingCell; }  // GUI compatibility alias
    int getCurrentPlayingStep() const { return currentPlayingStep; }
    void clearCellFocus();
    
    // Edit mode accessors for GUI
    void setInEditMode(bool editing) { isEditingCell = editing; }
    // Edit buffer cache accessors (for persistence across frames - ParameterCell owns the logic)
    std::string& getEditBufferCache() { return editBufferCache; }
    const std::string& getEditBufferCache() const { return editBufferCache; }
    void setEditBufferInitializedCache(bool init) { editBufferInitializedCache = init; }
    bool getEditBufferInitializedCache() const { return editBufferInitializedCache; }
    
    // Pattern cell accessor for GUI
    PatternCell& getPatternCell(int step) { return getCurrentPattern()[step]; }
    const PatternCell& getPatternCell(int step) const { return getCurrentPattern()[step]; }
    
    // Drag state accessors for GUI
    float getDragStartY() const { return dragStartY; }
    float getDragStartX() const { return dragStartX; }
    float getLastDragValue() const { return lastDragValue; }
    void setLastDragValue(float value) { lastDragValue = value; }
    
    void requestFocusMoveToParentWidget() { requestFocusMoveToParent = true; }  // Request GUI to move focus to parent widget
    bool shouldMoveFocusToParent() const { return requestFocusMoveToParent; }  // GUI compatibility alias
    void setShouldMoveFocusToParent(bool value) { requestFocusMoveToParent = value; }  // GUI compatibility
    bool getIsParentWidgetFocused() const { return parentWidgetFocused; }  // Check if parent widget is focused
    bool isParentWidgetFocused() const { return parentWidgetFocused; }  // GUI compatibility alias
    void setParentWidgetFocused(bool value) { parentWidgetFocused = value; }  // GUI compatibility
    // Update step active state (clears manually triggered steps when duration expires)
    void updateStepActiveState();
    float getCurrentBpm() const;
    int getStepsPerBeat() const { return stepsPerBeat; }
    
    // Setters
    void setStepsPerBeat(int steps);
    
    // Parameter synchronization methods (for ParameterSync system)
    // Get position parameter from current edit step (for sync)
    float getCurrentStepPosition() const;
    
    // Set position parameter for current edit step (for sync)
    void setCurrentStepPosition(float position);
    
    // Parameter change callback (for ParameterSync)
    void setParameterChangeCallback(std::function<void(const std::string&, float)> callback) {
        parameterChangeCallback = callback;
    }

private:
    bool isValidStep(int step) const;
    bool isPatternEmpty() const;
    void notifyStepEvent(int step, float stepLength);
    void updateStepInterval();
    
    // Column configuration
    struct ColumnConfig {
        std::string parameterName;      // e.g., "position", "speed", "volume" (or "index", "length" for fixed)
        std::string displayName;        // e.g., "Position", "Speed", "Volume"
        bool isFixed;                   // true for "index" and "length" columns (cannot be deleted)
        int columnIndex;                // Position in grid (0 = first column)
        
        ColumnConfig() : parameterName(""), displayName(""), isFixed(false), columnIndex(0) {}
        ColumnConfig(const std::string& param, const std::string& display, bool fixed, int idx)
            : parameterName(param), displayName(display), isFixed(fixed), columnIndex(idx) {}
    };
    
    // Column configuration management
    void initializeDefaultColumns();
    void addColumn(const std::string& parameterName, const std::string& displayName, int position = -1);
    void removeColumn(int columnIndex);
    void reorderColumn(int fromIndex, int toIndex);
    bool isColumnFixed(int columnIndex) const;
    const ColumnConfig& getColumnConfig(int columnIndex) const;
    int getColumnCount() const;
    const std::vector<ColumnConfig>& getColumnConfiguration() const { return columnConfig; }  // GUI compatibility
    
    // Pattern interaction methods
    bool handlePatternGridClick(int x, int y);
    bool handlePatternRowClick(int step, int column); // Unused - kept for API compatibility
    
    // Parameter range conversion helpers (use actual parameter ranges, not 0-127)
    static std::pair<float, float> getParameterRange(const std::string& paramName);
    static float getParameterDefault(const std::string& paramName); // Note: Uses getAvailableParameters() which is non-static
    static ParameterType getParameterType(const std::string& paramName); // Get parameter type dynamically
    static std::string formatParameterValue(const std::string& paramName, float value); // Format based on parameter type
    
    Clock* clock;
    
    // Column configuration
    std::vector<ColumnConfig> columnConfig;
    
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
    int editStep;      // Currently selected row for editing
    int editColumn;    // Currently selected column for editing (-1 = none, 0 = step number, 1+ = column index)
    bool isEditingCell; // True when in edit mode (typing numeric value) - derived from ParameterCell state
    std::string editBufferCache; // Cache for edit buffer to persist across frames (ParameterCell owns the logic, this is just persistence)
    bool editBufferInitializedCache; // Cache for edit buffer initialized state
    
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
    
    // Cell focus management
    bool shouldFocusFirstCell;  // Flag to request focus on first cell when entering grid
    bool shouldRefocusCurrentCell;  // Flag to request focus on current cell after exiting edit mode
    bool requestFocusMoveToParent;  // Flag to request GUI to move focus to parent widget (when UP pressed on header row)
    bool parentWidgetFocused;  // True when parent widget (outside table) is focused, false when on header row (inside table)
    
    // Parameter change callback (for ParameterSync system)
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
    bool shouldQueueEdit() const;
    
    // ParameterCell adapter methods - bridge PatternCell to ParameterCell
    // Creates and configures a ParameterCell for a specific step/column
    ParameterCell createParameterCellForColumn(int step, int column);
    
    // Configures callbacks for a ParameterCell to connect to PatternCell operations
    void configureParameterCellCallbacks(ParameterCell& cell, int step, int column);
};


