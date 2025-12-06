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
};

class TrackerSequencerGUI : public ModuleGUI {
public:
    TrackerSequencerGUI();
    
    // Legacy method (for backward compatibility during migration)
    void draw(TrackerSequencer& sequencer);
    
    // Override draw to call base class (which calls drawContent)
    void draw();
    
    // GUI state accessors (moved from TrackerSequencer)
    int getEditStep() const { return cellFocusState.row; }
    int getEditColumn() const { return cellFocusState.column; }
    bool getIsEditingCell() const { return cellFocusState.isEditing; }
    // Note: editBuffer and editBufferInitialized are managed internally by CellWidget
    // These accessors are kept for backward compatibility but may not be used
    const std::string& getEditBufferCache() const { 
        static std::string empty; 
        return empty; // CellWidget manages edit buffer internally
    }
    std::string& getEditBufferCache() { 
        static std::string empty; 
        return empty; // CellWidget manages edit buffer internally
    }
    bool getEditBufferInitializedCache() const { 
        return false; // CellWidget manages this internally
    }
    // GUI state setters
    void setEditCell(int step, int column) { 
        setCellFocus(cellFocusState, step, column);
    }
    void setInEditMode(bool editing) { cellFocusState.isEditing = editing; }
    void setEditBufferInitializedCache(bool init) { 
        // CellWidget manages this internally - no-op for backward compatibility
        (void)init;
    }
    
    // Override ModuleGUI generic interface (Phase 7.3/7.4)
    bool isEditingCell() const override { 
        return getIsEditingCell() || patternParamsFocusState.isEditing; 
    }
    bool isKeyboardFocused() const override { 
        return (cellFocusState.row >= 0 && cellFocusState.column >= 0) ||
               (patternParamsFocusState.row >= 0 && patternParamsFocusState.column >= 0);
    }
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
    // Unified cell focus state (replaces FocusState struct)
    // Note: editBuffer and editBufferInitialized are now managed by CellWidget internally
    CellFocusState cellFocusState;
    
    // Unified callback state tracking
    CellGridCallbacksState callbacksState;
    
    // Track pattern index to detect pattern switches
    int lastPatternIndex;
    
    // Track last step triggered when paused (to prevent repeated triggers)
    int lastTriggeredStepWhenPaused;
    
    // Multi-step selection state for copy/paste/cut operations
    struct SelectionState {
        int anchorStep = -1;      // Selection anchor (where shift was first pressed)
        int currentStep = -1;     // Current selection end
        bool isSelecting = false; // Whether shift is held
        
        int getStartStep() const { 
            if (anchorStep < 0 || currentStep < 0) return -1;
            return std::min(anchorStep, currentStep); 
        }
        int getEndStep() const { 
            if (anchorStep < 0 || currentStep < 0) return -1;
            return std::max(anchorStep, currentStep); 
        }
        bool hasSelection() const { 
            return anchorStep >= 0 && currentStep >= 0 && anchorStep != currentStep; 
        }
        bool hasSingleStep() const {
            return anchorStep >= 0 && currentStep >= 0 && anchorStep == currentStep;
        }
        void clear() { 
            anchorStep = -1; 
            currentStep = -1; 
            isSelecting = false; 
        }
        void setAnchor(int step) { 
            anchorStep = step; 
            currentStep = step; 
            isSelecting = true;
        }
        void extendTo(int step) { 
            currentStep = step; 
        }
    };
    SelectionState selectionState;
    
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
    
    // CellGrid instance for reusable table rendering (pattern grid)
    CellGrid cellGrid;
    
    // CellGrid instance for pattern parameters (Steps, SPB)
    CellGrid patternParametersGrid;
    
    // Focus state for pattern parameters grid
    CellFocusState patternParamsFocusState;
    
    // Callback state for pattern parameters grid
    CellGridCallbacksState patternParamsCallbacksState;
    
    // Track last column configuration for pattern parameters grid
    std::vector<CellGridColumnConfig> lastPatternParamsColumnConfig;
    
    // Drawing methods
    void drawPatternChain(TrackerSequencer& sequencer);
    void drawPatternControls(TrackerSequencer& sequencer);
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
    
    // Track last column configuration to avoid clearing cache unnecessarily
    std::vector<CellGridColumnConfig> lastColumnConfig;
    
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

