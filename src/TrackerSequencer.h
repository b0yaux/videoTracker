#pragma once

#include "ofMain.h"
#include <functional>
#include "Clock.h"
#include "Module.h"

// Forward declarations
class TrackerSequencerGUI;

class TrackerSequencer {
    friend class TrackerSequencerGUI;  // Allow GUI to access private members for rendering
public:
    struct PatternCell {
        // Fixed fields (always present)
        int index = -1;              // Media index (-1 = empty/rest, 0+ = media index)
        int length = 1;              // Step length in sequencer steps (1-16, integer count)
        
        // Dynamic parameter values (keyed by parameter name)
        // These use float for precision (position: 0-1, speed: -10 to 10, volume: 0-2)
        std::map<std::string, float> parameterValues;

        PatternCell() = default;
        // Legacy constructor for backward compatibility during migration
        PatternCell(int mediaIdx, float pos, float spd, float vol, float len)
            : index(mediaIdx), length((int)len) {
            // Store old parameters in map for migration
            parameterValues["position"] = pos;
            parameterValues["speed"] = spd;
            parameterValues["volume"] = vol;
        }

        bool isEmpty() const { return index < 0; }
        
        // Parameter access methods
        float getParameterValue(const std::string& paramName, float defaultValue = 0.0f) const;
        void setParameterValue(const std::string& paramName, float value);
        bool hasParameter(const std::string& paramName) const;
        void removeParameter(const std::string& paramName);
        
        // Additional methods
        void clear();
        bool operator==(const PatternCell& other) const;
        bool operator!=(const PatternCell& other) const;
        std::string toString() const;
    };

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
    
    // Transport listener for Clock play/stop events
    void onClockTransportChanged(bool isPlaying);
    
    // Pattern management
    void setCell(int step, const PatternCell& cell);
    PatternCell getCell(int step) const;
    void setNumSteps(int steps);
    void clearCell(int step);
    void clearPattern();
    void randomizePattern();
    
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
    void handleMouseClick(int x, int y, int button);
    
    // Getters
    int getNumSteps() const { return numSteps; }
    int getCurrentStep() const { return playbackStep; }  // Backward compatibility: returns playback step
    int getPlaybackStep() const { return playbackStep; }
    int getEditStep() const { return editStep; }
    int getEditColumn() const { return editColumn; }
    void setEditCell(int step, int column) { 
        editStep = step; 
        editColumn = column; 
    }
    bool isPlaying() const { return playing; }
    bool getIsEditingCell() const { return isEditingCell; }
    int getCurrentPlayingStep() const { return currentPlayingStep; }
    int getRemainingSteps() const { return remainingSteps; }
    void clearCellFocus() {
        // Guard: Don't clear if already cleared (prevents spam and unnecessary work)
        if (editStep == -1) {
            return;
        }
        ofLogNotice("TrackerSequencer") << "[DEBUG] [SET editStep] clearCellFocus() - clearing editStep to -1 (was: " << editStep << ")";
        editStep = -1;
        editColumn = -1;
        isEditingCell = false;
        editBuffer.clear();
        editBufferInitialized = false;
        shouldFocusFirstCell = false;
        shouldRefocusCurrentCell = false;
    }
    void requestFocusMoveToParentWidget() { requestFocusMoveToParent = true; }  // Request GUI to move focus to parent widget
    bool getIsParentWidgetFocused() const { return isParentWidgetFocused; }  // Check if parent widget is focused
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
    
    // Column configuration (forward declaration for method signatures)
    struct ColumnConfig;
    
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
    
    // Pattern interaction methods
    bool handlePatternGridClick(int x, int y);
    bool handlePatternRowClick(int step, int column); // Unused - kept for API compatibility
    
    // Edit mode helpers
    void adjustParameterValue(int delta);
    void applyEditValue(int displayValue);
    void applyEditValueFloat(float value, const std::string& parameterName);
    void initializeEditBuffer(); // Initialize edit buffer with current cell value
    
    // Parameter range conversion helpers (use actual parameter ranges, not 0-127)
    static float parameterToDisplayValue(const std::string& paramName, float value);
    static float displayValueToParameter(const std::string& paramName, float displayValue);
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
    std::vector<float> stepLengths;  // Per-step gate lengths
    
    std::vector<PatternCell> pattern;
    int numSteps;
    int playbackStep;  // Currently playing step (for visual indicator)
    int editStep;      // Currently selected row for editing
    int editColumn;    // Currently selected column for editing (-1 = none, 0 = step number, 1+ = column index)
    bool isEditingCell; // True when in edit mode (typing numeric value)
    std::string editBuffer; // Buffer for numeric input during edit mode
    bool editBufferInitialized; // True if buffer was initialized from cell value (not user typing)
    
    // Drag state for parameter cell editing (moved from static variables to avoid loop issues)
    int draggingStep;      // Step being dragged (-1 if not dragging)
    int draggingColumn;    // Column being dragged (-1 if not dragging)
    int lastDragValue;     // Last drag value for throttling
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
    
    // Media playback timing (separate from sequencer timing)
    float currentStepStartTime;
    float currentStepDuration;
    bool stepActive;
    
    // Step event listeners
    std::vector<std::function<void(int, float, const PatternCell&)>> stepEventListeners;
    
    // Callback for querying external state
    IndexRangeCallback indexRangeCallback;
    
    // UI state
    bool showGUI;
    
    // Step length tracking for multi-step cells
    int remainingSteps;  // Remaining steps for current playing cell
    int currentPlayingStep;  // Current step that's playing (for multi-step cells)
    
    // Cell focus management
    bool shouldFocusFirstCell;  // Flag to request focus on first cell when entering grid
    bool shouldRefocusCurrentCell;  // Flag to request focus on current cell after exiting edit mode
    bool requestFocusMoveToParent;  // Flag to request GUI to move focus to parent widget (when UP pressed on header row)
    bool isParentWidgetFocused;  // True when parent widget (outside table) is focused, false when on header row (inside table)
    
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
    
    // Helper to parse edit buffer and apply edit (used for both immediate and queued edits)
    bool parseAndApplyEditBuffer(int step, int column, bool queueForPlayback);
    
    // Helper to determine if edit should be queued (during playback on current step)
    bool shouldQueueEdit() const;
};

