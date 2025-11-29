#pragma once

#include "TrackerSequencer.h"
#include "Pattern.h"
#include "gui/ModuleGUI.h"
#include "gui/CellGrid.h"
#include "CellWidget.h"

// Forward declarations for ImGui types
typedef unsigned int ImGuiID;
typedef unsigned int ImU32;

class ModuleRegistry;  // Forward declaration

// GUIState struct for passing GUI state to TrackerSequencer::handleKeyPress()
// This is a temporary parameter struct - NOT a source of truth
// The actual GUI state is managed by TrackerSequencerGUI
struct GUIState {
    int editStep = -1;
    int editColumn = -1;
    bool isEditingCell = false;  // Temporary: passed from GUI, modified by handleKeyPress, synced back to GUI
    std::string editBufferCache;  // Temporary: passed from GUI, modified by handleKeyPress, synced back to GUI
    bool editBufferInitializedCache = false;  // Temporary: passed from GUI, modified by handleKeyPress, synced back to GUI
    bool shouldRefocusCurrentCell = false;  // Temporary: passed from GUI, modified by handleKeyPress, synced back to GUI
};

class TrackerSequencerGUI : public ModuleGUI {
public:
    TrackerSequencerGUI();
    
    // Legacy method (for backward compatibility during migration)
    void draw(TrackerSequencer& sequencer);
    
    // Override draw to call base class (which calls drawContent)
    void draw();
    
    // GUI state accessors (moved from TrackerSequencer)
    int getEditStep() const { return focusState.step; }
    int getEditColumn() const { return focusState.column; }
    bool getIsEditingCell() const { return focusState.isEditing; }
    const std::string& getEditBufferCache() const { return focusState.editBuffer; }
    std::string& getEditBufferCache() { return focusState.editBuffer; }
    bool getEditBufferInitializedCache() const { return focusState.editBufferInitialized; }
    bool getShouldRefocusCurrentCell() const { return focusState.shouldRefocus; }
    
    // GUI state setters
    void setEditCell(int step, int column) { 
        focusState.step = step; 
        focusState.column = column; 
    }
    void setInEditMode(bool editing) { focusState.isEditing = editing; }
    void setEditBufferInitializedCache(bool init) { focusState.editBufferInitialized = init; }
    void setShouldRefocusCurrentCell(bool refocus) { focusState.shouldRefocus = refocus; }
    
    // Override ModuleGUI generic interface (Phase 7.3/7.4)
    bool isEditingCell() const override { return getIsEditingCell(); }
    bool isKeyboardFocused() const override { return (focusState.step >= 0 && focusState.column >= 0); }
    void clearCellFocus() override;
    
    // Override ModuleGUI input handling (InputRouter refactoring)
    bool handleKeyPress(int key, bool ctrlPressed = false, bool shiftPressed = false) override;
    
protected:
    // Implement ModuleGUI::drawContent() - draws panel-specific content
    void drawContent() override;
    
private:
    // Helper to get current TrackerSequencer instance from registry
    TrackerSequencer* getTrackerSequencer() const;
    
    // GUI state (moved from TrackerSequencer)
    // Consolidated focus state for better organization
    struct FocusState {
        int step = -1;      // Currently selected row for editing (-1 = none)
        int column = -1;    // Currently selected column for editing (-1 = none, 0 = step number, 1+ = column index)
        bool isEditing = false; // True when in edit mode (typing numeric value)
        std::string editBuffer; // Cache for edit buffer to persist across frames
        bool editBufferInitialized = false; // Cache for edit buffer initialized state
        bool shouldRefocus = false; // For maintaining focus after exiting edit mode via Enter
        
        void clear() {
            step = -1;
            column = -1;
            isEditing = false;
            editBuffer.clear();
            editBufferInitialized = false;
            shouldRefocus = false;
        }
    };
    FocusState focusState;
    
    // Track pattern index to detect pattern switches
    int lastPatternIndex;
    
    // Track if any cell is focused during drawing (to detect header row focus)
    bool anyCellFocusedThisFrame;
    
    // Row outline tracking for step number hover
    struct RowOutlineState {
        int step;
        float rowYMin;
        float rowYMax;
        float rowXMin;  // Store X positions from first cell
        float rowXMax;  // Store X positions from last cell (calculated after all cells drawn)
        bool shouldDraw;
        ImU32 color;
    };
    RowOutlineState pendingRowOutline;
    
    // CellGrid instance for reusable table rendering
    CellGrid cellGrid;
    
    // Drawing methods
    void drawPatternChain(TrackerSequencer& sequencer);
    void drawTrackerStatus(TrackerSequencer& sequencer);
    void drawPatternGrid(TrackerSequencer& sequencer);
    void drawStepNumber(TrackerSequencer& sequencer, int step, bool isPlaybackStep,
                       bool isPlaying, int currentPlayingStep);
    
    // CellWidget adapter methods (moved from TrackerSequencer)
    // Creates and configures a CellWidget for a specific step/column
    CellWidget createParameterCellForColumn(TrackerSequencer& sequencer, int step, int column);
    
    // Configures callbacks for a CellWidget to connect to Step operations
    void configureParameterCellCallbacks(TrackerSequencer& sequencer, CellWidget& cell, int step, int column);
    
    // Helper method to query external parameters from connected INSTRUMENT modules
    // Filters out internal parameters and returns unique parameter descriptors
    std::vector<ParameterDescriptor> queryExternalParameters(TrackerSequencer& sequencer) const;
    
    // Helper method to restore ImGui keyboard navigation (called when exiting edit mode)
    void restoreImGuiKeyboardNavigation();
    
    // Helper methods for setting up CellGrid callbacks (split from drawPatternGrid)
    void setupHeaderCallbacks(CellGridCallbacks& callbacks, bool& headerClickedThisFrame, 
                              TrackerSequencer& sequencer, std::map<int, std::vector<HeaderButton>>& columnHeaderButtons);
    void setupCellValueCallbacks(CellGridCallbacks& callbacks, TrackerSequencer& sequencer);
    void setupStateSyncCallbacks(CellGridCallbacks& callbacks, TrackerSequencer& sequencer);
    void setupRowCallbacks(CellGridCallbacks& callbacks, TrackerSequencer& sequencer, int currentPlayingStep);
    
    // Constants for UI dimensions and limits
    static constexpr float PATTERN_CELL_HEIGHT = 22.0f;
    static constexpr float BUTTON_HEIGHT = 16.0f;
    static constexpr float PATTERN_CELL_WIDTH = 32.0f;
    static constexpr float REPEAT_CELL_HEIGHT = 18.0f;
    static constexpr float INDEX_LENGTH_COLUMN_WIDTH = 45.0f;
    static constexpr float STEP_NUMBER_COLUMN_WIDTH = 30.0f;
    static constexpr float SCROLLBAR_SIZE = 8.0f;
    static constexpr float OUTLINE_THICKNESS = 2.0f;
    static constexpr float BUTTON_SPACING = 2.0f;
    static constexpr int REPEAT_COUNT_ID_OFFSET = 1000;
    static constexpr int MAX_LENGTH_VALUE = 16;
    static constexpr int MIN_LENGTH_VALUE = 1;
    static constexpr int BUFFER_SIZE = 8;
    
};

