#pragma once

#include "TrackerSequencer.h"

// Forward declarations for ImGui types
typedef unsigned int ImGuiID;
typedef unsigned int ImU32;

class TrackerSequencerGUI {
public:
    TrackerSequencerGUI();
    void draw(TrackerSequencer& sequencer);
    
    // Sync edit state from ImGui focus - called from InputRouter when keys are pressed
    static bool syncEditStateFromImGuiFocus(TrackerSequencer& sequencer);
    
private:
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
    void drawTrackerStatus(TrackerSequencer& sequencer);
    void drawPatternGrid(TrackerSequencer& sequencer);
    void drawPatternRow(TrackerSequencer& sequencer, int step, bool isPlaybackStep, bool isEditStep,
                       bool isPlaying, int currentPlayingStep, int remainingSteps,
                       int maxIndex, const std::map<std::string, std::pair<float, float>>& paramRanges,
                       const std::map<std::string, float>& paramDefaults,
                       int cachedEditStep, int cachedEditColumn, bool cachedIsEditingCell);
    void drawStepNumber(TrackerSequencer& sequencer, int step, bool isPlaybackStep,
                       bool isPlaying, int currentPlayingStep, int remainingSteps);
    void drawParameterCell(TrackerSequencer& sequencer, int step, int colConfigIndex,
                          int maxIndex, const std::map<std::string, std::pair<float, float>>& paramRanges,
                          const std::map<std::string, float>& paramDefaults,
                          int cachedEditStep, int cachedEditColumn, bool cachedIsEditingCell);
    
    // Unified drag handling for all column types
    void handleDragEditing(TrackerSequencer& sequencer, int step, int editColumnValue,
                          const TrackerSequencer::ColumnConfig& colConfig, 
                          TrackerSequencer::PatternCell& cell,
                          const std::map<std::string, std::pair<float, float>>& paramRanges,
                          const std::map<std::string, float>& paramDefaults);
    
    // Legacy methods - kept for backward compatibility but not used
    void drawMediaIndex(TrackerSequencer& sequencer, int step);
    void drawPosition(TrackerSequencer& sequencer, int step);
    void drawSpeed(TrackerSequencer& sequencer, int step);
    void drawVolume(TrackerSequencer& sequencer, int step);
    void drawStepLength(TrackerSequencer& sequencer, int step);
    void drawValueBar(float fillPercent);
};

