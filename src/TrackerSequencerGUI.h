#pragma once

#include "TrackerSequencer.h"
#include "Pattern.h"
#include "gui/ModuleGUI.h"

// Forward declarations for ImGui types
typedef unsigned int ImGuiID;
typedef unsigned int ImU32;

class ModuleRegistry;  // Forward declaration

class TrackerSequencerGUI : public ModuleGUI {
public:
    TrackerSequencerGUI();
    
    // Legacy method (for backward compatibility during migration)
    void draw(TrackerSequencer& sequencer);
    
    // Override draw to call base class (which calls drawContent)
    void draw();
    
    // Sync edit state from ImGui focus - called from InputRouter when keys are pressed
    // Note: This is now an instance method since GUI state is managed by TrackerSequencerGUI
    bool syncEditStateFromImGuiFocus();
    
    // GUI state accessors (moved from TrackerSequencer)
    int getEditStep() const { return editStep; }
    int getEditColumn() const { return editColumn; }
    bool getIsEditingCell() const { return isEditingCell; }
    const std::string& getEditBufferCache() const { return editBufferCache; }
    std::string& getEditBufferCache() { return editBufferCache; }
    bool getEditBufferInitializedCache() const { return editBufferInitializedCache; }
    bool getShouldRefocusCurrentCell() const { return shouldRefocusCurrentCell; }
    
    // GUI state setters
    void setEditCell(int step, int column) { 
        editStep = step; 
        editColumn = column; 
    }
    void setInEditMode(bool editing) { isEditingCell = editing; }
    void setEditBufferInitializedCache(bool init) { editBufferInitializedCache = init; }
    void setShouldRefocusCurrentCell(bool refocus) { shouldRefocusCurrentCell = refocus; }
    void clearCellFocus();
    
    // Check if keyboard input should be routed to sequencer
    bool isKeyboardFocused() const { return (editStep >= 0 && editColumn >= 0); }
    
protected:
    // Implement ModuleGUI::drawContent() - draws panel-specific content
    void drawContent() override;
    
private:
    // Helper to get current TrackerSequencer instance from registry
    TrackerSequencer* getTrackerSequencer() const;
    
    // GUI state (moved from TrackerSequencer)
    int editStep = -1;      // Currently selected row for editing (-1 = none)
    int editColumn = -1;    // Currently selected column for editing (-1 = none, 0 = step number, 1+ = column index)
    bool isEditingCell = false; // True when in edit mode (typing numeric value)
    std::string editBufferCache; // Cache for edit buffer to persist across frames
    bool editBufferInitializedCache = false; // Cache for edit buffer initialized state
    bool shouldRefocusCurrentCell = false; // For maintaining focus after exiting edit mode via Enter
    
    // Performance optimization: dirty flag to avoid expensive string formatting every frame
    bool patternDirty;
    int lastNumSteps;
    int lastPlaybackStep;
    
    // Track if any cell is focused during drawing (to detect header row focus)
    bool anyCellFocusedThisFrame;
    
    // Store parent widget ID for navigation (widget created before table)
    ImGuiID parentWidgetId;
    
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
    
    // Drawing methods
    void drawPatternChain(TrackerSequencer& sequencer);
    void drawTrackerStatus(TrackerSequencer& sequencer);
    void drawPatternGrid(TrackerSequencer& sequencer);
    void drawPatternRow(TrackerSequencer& sequencer, int step, bool isPlaybackStep, bool isEditStep,
                       bool isPlaying, int currentPlayingStep,
                       int maxIndex, const std::map<std::string, std::pair<float, float>>& paramRanges,
                       const std::map<std::string, float>& paramDefaults,
                       int cachedEditStep, int cachedEditColumn, bool cachedIsEditingCell);
    void drawStepNumber(TrackerSequencer& sequencer, int step, bool isPlaybackStep,
                       bool isPlaying, int currentPlayingStep);
    void drawParameterCell(TrackerSequencer& sequencer, int step, int colConfigIndex,
                          int maxIndex, const std::map<std::string, std::pair<float, float>>& paramRanges,
                          const std::map<std::string, float>& paramDefaults,
                          int cachedEditStep, int cachedEditColumn, bool cachedIsEditingCell);
};

