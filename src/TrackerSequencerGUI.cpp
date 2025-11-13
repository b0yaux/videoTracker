#include "TrackerSequencerGUI.h"
// Note: TrackerSequencer.h is already included by TrackerSequencerGUI.h
#include "ParameterCell.h"
#include "ofxImGui.h"
#include "ofLog.h"
#include <cmath>  // For std::round
#include <map>    // For paramRanges map

TrackerSequencerGUI::TrackerSequencerGUI() 
    : editStep(-1), editColumn(-1), isEditingCell(false), editBufferInitializedCache(false),
      patternDirty(true), lastNumSteps(0), lastPlaybackStep(-1), anyCellFocusedThisFrame(false), parentWidgetId(0) {
    pendingRowOutline.shouldDraw = false;
    pendingRowOutline.step = -1;
}

void TrackerSequencerGUI::clearCellFocus() {
    // Guard: Don't clear if already cleared (prevents spam and unnecessary work)
    if (editStep == -1) {
        return;
    }
    ofLogNotice("TrackerSequencerGUI") << "[FOCUS_DEBUG] clearCellFocus() - clearing editStep to -1 (was: " << editStep 
                                     << ", column: " << editColumn 
                                     << ", isEditingCell: " << isEditingCell << ")";
    
    // If we were in edit mode, restore ImGui keyboard navigation
    if (isEditingCell) {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ofLogNotice("TrackerSequencerGUI") << "[FOCUS_DEBUG] Restored ImGui keyboard navigation (was in edit mode)";
    }
    
    editStep = -1;
    editColumn = -1;
    isEditingCell = false;
    editBufferCache.clear();
    editBufferInitializedCache = false;
}

void TrackerSequencerGUI::draw(TrackerSequencer& sequencer) {
    drawPatternChain(sequencer);
    drawTrackerStatus(sequencer);
    drawPatternGrid(sequencer);
}

void TrackerSequencerGUI::drawPatternChain(TrackerSequencer& sequencer) {
    ImGui::PushID("PatternChain");
        
    // Pattern chain toggle checkbox
    bool useChain = sequencer.getUsePatternChain();
    if (ImGui::Checkbox("##chainToggle", &useChain)) {
        sequencer.setUsePatternChain(useChain);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable pattern chain");
    }
    ImGui::SameLine();
    // Pattern chain header with toggle
    ImGui::Text("Pattern Chain");
    ImGui::SameLine();

    
    ImGui::Spacing();
    
    // Pattern chain visual list (horizontal layout, Renoise-style)
    const auto& chain = sequencer.getPatternChain();
    int currentChainIndex = sequencer.getCurrentChainIndex();
    int currentPatternIndex = sequencer.getCurrentPatternIndex();
    bool isPlaying = sequencer.isPlaying();
    
    // Compact styling for pattern chain
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    
    // Store starting Y position to align buttons with pattern cells
    const float patternCellHeight = 22.0f;
    const float buttonHeight = 16.0f;
    const float buttonsStartY = ImGui::GetCursorPosY();
    
    // Draw pattern chain entries (top row)
    for (size_t i = 0; i < chain.size(); i++) {
        int patternIdx = chain[i];
        bool isCurrentChainEntry = ((int)i == currentChainIndex);
        bool isCurrentPattern = (patternIdx == currentPatternIndex);
        bool isDisabled = sequencer.isPatternChainEntryDisabled((int)i);
        
        ImGui::PushID((int)i);
        
        // Pattern cell - clickable to select pattern (Renoise-style)
        ImVec2 cellSize(32, 22);
        
        // Color coding: blue for current pattern, gray for current chain position, dark for others, red tint if disabled
        ImU32 bgColor;
        if (isDisabled) {
            bgColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.2f, 0.2f, 1.0f));  // Red tint for disabled
        } else if (isCurrentPattern && isPlaying) {
            bgColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.9f, 0.0f, 0.6f));  // Bright green when playing
        } else if (isCurrentPattern) {
            bgColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.9f, 0.5f, 0.1f, 0.6f));  // Blue for current pattern
        } else if (isCurrentChainEntry) {
            bgColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.4f, 0.4f, 1.0f));  // Gray for current chain entry
        } else {
            bgColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.25f, 0.25f, 0.25f, 1.0f));  // Dark for others
        }
        
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        
        // Draw background
        drawList->AddRectFilled(cursorPos, ImVec2(cursorPos.x + cellSize.x, cursorPos.y + cellSize.y), bgColor);
        
        // Draw border for current chain entry
        if (isCurrentChainEntry) {
            ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
            drawList->AddRect(cursorPos, ImVec2(cursorPos.x + cellSize.x, cursorPos.y + cellSize.y), borderColor, 0.0f, 0, 1.5f);
        }
        
        // Draw diagonal line if disabled
        if (isDisabled) {
            ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            drawList->AddLine(cursorPos, ImVec2(cursorPos.x + cellSize.x, cursorPos.y + cellSize.y), lineColor, 2.0f);
        }
        
        // Pattern number text (2-digit format: 01, 02, 03, etc.)
        // Display chain position (1-based) instead of actual pattern index for sequential numbering
        char patternLabel[8];
        snprintf(patternLabel, sizeof(patternLabel), "%02d", (int)i + 1);
        ImVec2 textSize = ImGui::CalcTextSize(patternLabel);
        ImVec2 textPos(cursorPos.x + (cellSize.x - textSize.x) * 0.5f, cursorPos.y + (cellSize.y - textSize.y) * 0.5f);
        drawList->AddText(textPos, IM_COL32_WHITE, patternLabel);
        
        // Make it clickable and navigable with keyboard
        ImGui::InvisibleButton("pattern", cellSize, ImGuiButtonFlags_EnableNav);
        if (ImGui::IsItemClicked(0)) {
            if (isPlaying && useChain) {
                // During playback with chain enabled: toggle disable state
                sequencer.setPatternChainEntryDisabled((int)i, !isDisabled);
            } else {
                // Normal behavior: select pattern
                sequencer.setCurrentPatternIndex(patternIdx);
                sequencer.setCurrentChainIndex((int)i);
            }
        }
        
        if (ImGui::IsItemHovered()) {
            if (isPlaying && useChain) {
                ImGui::SetTooltip("Chain position %02d (Pattern %02d)\nLeft-click: Toggle disable\nRight-click: Remove from chain", (int)i + 1, patternIdx);
            } else {
                ImGui::SetTooltip("Chain position %02d (Pattern %02d)\nLeft-click: Select", (int)i + 1, patternIdx);
            }
        }
        
        ImGui::SameLine();
        ImGui::PopID();
    }
    
    // Small buttons for duplicate, add, and remove (same size, compact, distinct from pattern cells)
    // Note: We keep the same style vars for buttons (they're already pushed above)
    // Center buttons vertically relative to pattern cells
    const float verticalOffset = (patternCellHeight - buttonHeight) * 0.5f;
    const float buttonsY = buttonsStartY + verticalOffset;
    
    // 'D' button for duplicate current pattern
    ImGui::SetCursorPosY(buttonsY);
    if (ImGui::Button("D", ImVec2(buttonHeight, buttonHeight))) {
        int currentPattern = sequencer.getCurrentPatternIndex();
        sequencer.duplicatePattern(currentPattern);
        int newPatternIndex = sequencer.getNumPatterns() - 1;
        // Add new pattern to chain
        sequencer.addToPatternChain(newPatternIndex);
        // Switch to new pattern if not playing with pattern chaining enabled
        if (!(isPlaying && useChain)) {
            sequencer.setCurrentPatternIndex(newPatternIndex);
            sequencer.setCurrentChainIndex(sequencer.getPatternChainSize() - 1);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Duplicate current pattern");
    }
    
    ImGui::SameLine();
    ImGui::SetCursorPosY(buttonsY);  // Set Y after SameLine() to ensure alignment
    
    // '+' button to add new pattern
    if (ImGui::Button("+", ImVec2(buttonHeight, buttonHeight))) {
        int newPatternIndex = sequencer.addPattern();
        sequencer.addToPatternChain(newPatternIndex);
        // Switch to new pattern if not playing with pattern chaining enabled
        if (!(isPlaying && useChain)) {
            sequencer.setCurrentPatternIndex(newPatternIndex);
            sequencer.setCurrentChainIndex(sequencer.getPatternChainSize() - 1);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Add new pattern");
    }
    
    ImGui::SameLine();
    ImGui::SetCursorPosY(buttonsY);  // Set Y after SameLine() to ensure alignment
    
    // '-' button to remove currently selected pattern from chain (if chain has more than one entry)
    bool canRemove = sequencer.getPatternChainSize() > 1;
    if (!canRemove) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }
    if (ImGui::Button("-", ImVec2(buttonHeight, buttonHeight)) && canRemove) {
        int chainSize = sequencer.getPatternChainSize();
        int currentIndex = sequencer.getCurrentChainIndex();
        if (chainSize > 1 && currentIndex >= 0 && currentIndex < chainSize) {
            // Remove the currently selected pattern
            // removeFromPatternChain will handle adjusting currentChainIndex appropriately
            sequencer.removeFromPatternChain(currentIndex);
        }
    }
    if (ImGui::IsItemHovered() && canRemove) {
        ImGui::SetTooltip("Remove currently selected pattern from chain");
    }
    if (!canRemove) {
        ImGui::PopStyleVar();  // Pop the alpha style var
    }
    
    // Pop pattern chain style vars (used for both pattern cells and buttons)
    ImGui::PopStyleVar(2);
    

    // Draw repeat count cells below pattern cells
    // Push style vars for repeat count cells
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    
    for (size_t i = 0; i < chain.size(); i++) {
        ImGui::PushID((int)(i + 1000));  // Different ID range to avoid conflicts
        
        int repeatCount = sequencer.getPatternChainRepeatCount((int)i);
        bool isCurrentChainEntry = ((int)i == currentChainIndex);
        
        ImVec2 repeatCellSize(32, 18);
        ImGui::PushItemWidth(repeatCellSize.x);
        
        // Editable repeat count (small input field, similar to pattern grid cells)
        char repeatBuf[8];
        snprintf(repeatBuf, sizeof(repeatBuf), "%d", repeatCount);
        
        // Style the repeat count cell to match pattern cell
        if (isCurrentChainEntry) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }
        
        if (ImGui::InputText("##repeat", repeatBuf, sizeof(repeatBuf), 
                             ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue | 
                             ImGuiInputTextFlags_AutoSelectAll)) {
            try {
                int newRepeat = std::stoi(repeatBuf);
                newRepeat = std::max(1, std::min(99, newRepeat));
                sequencer.setPatternChainRepeatCount((int)i, newRepeat);
            } catch (...) {
                // Invalid input, ignore
            }
        }
        
        if (isCurrentChainEntry) {
            ImGui::PopStyleColor();
        }
        
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Repeat count: %d (1-99)", repeatCount);
        }
        
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::PopID();
    }
    
    // Pop repeat count style vars
    ImGui::PopStyleVar(2);
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    ImGui::PopID();
}

void TrackerSequencerGUI::drawTrackerStatus(TrackerSequencer& sequencer) {


    if (ImGui::Button("Clear Pattern")) {
        sequencer.clearPattern();
    }
    // Pattern controls
    int newNumSteps = sequencer.getCurrentPattern().getStepCount();
    if (ImGui::SliderInt("Steps", &newNumSteps, 4, 64, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        if (newNumSteps != sequencer.getCurrentPattern().getStepCount()) {
            sequencer.getCurrentPattern().setStepCount(newNumSteps);
        }
    }
    
    ImGui::SameLine();
    
    // 'D' button to double steps (duplicate all steps to double pattern length)
    if (ImGui::Button("D", ImVec2(20, 20))) {
        sequencer.getCurrentPattern().doubleSteps();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Double pattern length (duplicate all steps)");
    }
    
    int newStepsPerBeat = sequencer.getStepsPerBeat();
    if (ImGui::SliderInt("Steps Per Beat", &newStepsPerBeat, 1, 96, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        sequencer.setStepsPerBeat(newStepsPerBeat);
    }

    ImGui::Separator();
}

void TrackerSequencerGUI::drawPatternGrid(TrackerSequencer& sequencer) {
    // Track changes for optimization
    patternDirty = false;
    lastNumSteps = sequencer.getCurrentPattern().getStepCount();
    lastPlaybackStep = sequencer.getPlaybackStepIndex();
    
    // DEBUG: Log focus state changes
    static int lastEditStep = -1;
    static int lastEditColumn = -1;
    static bool lastIsEditingCell = false;
    
    // Use GUI's own state instead of sequencer's (GUI state moved to TrackerSequencerGUI)
    int currentEditStep = editStep;
    int currentEditColumn = editColumn;
    bool currentIsEditingCell = isEditingCell;
    
    if (currentEditStep != lastEditStep || 
        currentEditColumn != lastEditColumn ||
        currentIsEditingCell != lastIsEditingCell) {
        ofLogNotice("TrackerSequencerGUI") << "[FOCUS_DEBUG] Pattern grid focus state changed - "
                                            << "editStep: " << currentEditStep
                                            << ", editColumn: " << currentEditColumn
                                            << ", isEditingCell: " << currentIsEditingCell;
        lastEditStep = currentEditStep;
        lastEditColumn = currentEditColumn;
        lastIsEditingCell = currentIsEditingCell;
    }
    
    // No parent widget - cells are directly navigable like other widgets
    // ImGui handles navigation naturally when pressing UP on first row
    
    // Compact styling
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1, 1));
    
    // Ensure column configuration is initialized
    if (sequencer.getColumnConfiguration().empty()) {
        sequencer.initializeDefaultColumns();
    }
    
    // PERFORMANCE: Cache all expensive calls ONCE per frame
    // Cache active step info ONCE per frame (not per row!)
    bool isPlaying = sequencer.isPlaying();
    int currentPlayingStep = sequencer.getCurrentPlayingStep();
    int playbackStep = sequencer.getPlaybackStepIndex();
    
    // Cache edit state to avoid repeated member access
    // Use GUI's own state instead of sequencer's (GUI state moved to TrackerSequencerGUI)
    int cachedEditStep = editStep;
    int cachedEditColumn = editColumn;
    bool cachedIsEditingCell = isEditingCell;
    
    // Cache indexRangeCallback result (expensive callback - called once instead of 16+ times)
    int maxIndex = sequencer.indexRangeCallback ? sequencer.indexRangeCallback() : 127;
    
    // Cache parameter ranges and defaults for all parameter columns (expensive lookups - called once instead of 48+ times)
    std::map<std::string, std::pair<float, float>> paramRanges;
    std::map<std::string, float> paramDefaults;
    for (const auto& col : sequencer.getColumnConfiguration()) {
        // Only cache for dynamic parameters (not fixed columns like "index" or "length")
        if (col.parameterName != "index" && col.parameterName != "length") {
            if (paramRanges.find(col.parameterName) == paramRanges.end()) {
                paramRanges[col.parameterName] = TrackerSequencer::getParameterRange(col.parameterName);
                paramDefaults[col.parameterName] = TrackerSequencer::getParameterDefault(col.parameterName);
            }
        }
    }
    
    // Table setup - match MediaPoolGUI: no ScrollY flag (creates container)
    // Cells are directly navigable like other widgets
    static ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                                   ImGuiTableFlags_Resizable |
                                   ImGuiTableFlags_SizingFixedFit;
    
    int totalColumns = 1 + (int)sequencer.getColumnConfiguration().size();
    
    if (ImGui::BeginTable("TrackerGrid", totalColumns, flags)) {
        ImGui::TableSetupColumn("##", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        
        // Parameter columns: stretch to fill available space (responsive to panel size)
        // Weight determines proportional sizing:
        // - Index: widest (2.0x weight)
        // - Length: second widest (1.5x weight)
        // - Parameters: roughly half of Index (1.0x weight)
        for (const auto& col : sequencer.getColumnConfiguration()) {
            float weight;
            if (col.parameterName == "index") {
                weight = 2.0f;  // Index is the widest
            } else if (col.parameterName == "length") {
                weight = 1.5f;  // Length is second widest
            } else {
                weight = 1.0f;  // Parameters are roughly half of Index
            }
            // For WidthStretch, the 3rd parameter (init_width_or_weight) is interpreted as the weight
            ImGui::TableSetupColumn(col.displayName.c_str(), ImGuiTableColumnFlags_WidthStretch, weight);
        }
        
        ImGui::TableSetupScrollFreeze(0, 1);
        
        // Custom header row with randomization buttons
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        
        // Step number column header (no button)
        ImGui::TableSetColumnIndex(0);
        ImGui::TableHeader("##");
        
        // Column headers with randomization buttons
        for (size_t i = 0; i < sequencer.getColumnConfiguration().size(); i++) {
            ImGui::TableSetColumnIndex((int)i + 1);
            const auto& colConfig = sequencer.getColumnConfiguration()[i];
            
            ImGui::PushID((int)(i + 1000)); // Unique ID for header buttons
            
            // Get cell position and width before drawing header
            ImVec2 cellStartPos = ImGui::GetCursorScreenPos();
            float columnWidth = ImGui::GetColumnWidth();
            float cellMinY = cellStartPos.y;
            
            // Draw column name first (left-aligned)
            ImGui::TableHeader(colConfig.displayName.c_str());
            
            // Calculate button sizes
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
            float buttonWidth = ImGui::CalcTextSize("R").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            float legatoButtonWidth = 0.0f;
            if (colConfig.parameterName == "length") {
                legatoButtonWidth = ImGui::CalcTextSize("L").x + ImGui::GetStyle().FramePadding.x * 2.0f + 2.0f; // +2 for spacing
            }
            float totalButtonWidth = buttonWidth + legatoButtonWidth;
            float padding = ImGui::GetStyle().CellPadding.x;
            
            // Position buttons to the right edge of the cell
            float cellMaxX = cellStartPos.x + columnWidth;
            float buttonStartX = cellMaxX - totalButtonWidth - padding;
            ImGui::SetCursorScreenPos(ImVec2(buttonStartX, cellMinY));
            

            // Add legato button for length column
            if (colConfig.parameterName == "length") {
                if (ImGui::SmallButton("L")) {
                    sequencer.applyLegato();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Legato");
                }
                ImGui::SameLine(0.0f, 2.0f);
            }

            // Small randomization button ("R")
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));
            if (ImGui::SmallButton("R")) {
                sequencer.randomizeColumn((int)i);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Randomize");
            }
            
            
            ImGui::PopStyleVar(2);
            ImGui::PopID();
        }
        
        // Draw pattern rows
        pendingRowOutline.shouldDraw = false; // Reset row outline state
        anyCellFocusedThisFrame = false; // Reset focus tracking
        for (int step = 0; step < sequencer.getCurrentPattern().getStepCount(); step++) {
            drawPatternRow(sequencer, step, step == playbackStep, step == cachedEditStep, 
                          isPlaying, currentPlayingStep,
                          maxIndex, paramRanges, paramDefaults, cachedEditStep, cachedEditColumn, cachedIsEditingCell);
        }
        
        // After drawing all rows, if editStep is set but no cell was focused,
        // clear the sequencer's cell focus (focus moved to header row or elsewhere)
        // BUT: Don't clear if we just exited edit mode (shouldRefocusCurrentCell flag is set)
        // This prevents focus loss when ImGui temporarily loses focus after exiting edit mode
        // NOTE: This check happens AFTER all cells are drawn, so it won't interfere with
        // the ViewManager's empty space click handling which clears focus BEFORE drawing
        // Note: shouldRefocusCurrentCell flag removed - GUI manages its own focus state
        if (editStep >= 0 && !anyCellFocusedThisFrame && !isEditingCell) {
            ofLogNotice("TrackerSequencerGUI") << "[FOCUS_DEBUG] Clearing cell focus - no cell was focused this frame (step: " 
                                                << editStep << ", column: " << editColumn << ")";
            clearCellFocus();
        }
        
        // Draw row outline if needed (after all cells are drawn)
        // Use stored X/Y positions which were updated after all cells in the row were drawn
        // Clamp to visible table area to prevent outline from extending beyond panel bounds
        if (pendingRowOutline.shouldDraw) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (drawList) {
                // Get visible window/content region bounds to clamp the outline
                ImVec2 windowPos = ImGui::GetWindowPos();
                ImVec2 contentRegionMin = ImGui::GetWindowContentRegionMin();
                ImVec2 contentRegionMax = ImGui::GetWindowContentRegionMax();
                
                // Calculate visible bounds (accounting for scrollbars, padding, etc.)
                float visibleXMin = windowPos.x + contentRegionMin.x;
                float visibleXMax = windowPos.x + contentRegionMax.x;
                
                // Clamp row outline to visible bounds
                float clampedXMin = std::max(pendingRowOutline.rowXMin, visibleXMin);
                float clampedXMax = std::min(pendingRowOutline.rowXMax, visibleXMax);
                
                // Only draw if there's a visible portion
                if (clampedXMin < clampedXMax) {
                    ImVec2 rowMin = ImVec2(clampedXMin, pendingRowOutline.rowYMin);
                    ImVec2 rowMax = ImVec2(clampedXMax, pendingRowOutline.rowYMax);
                    drawList->AddRect(rowMin, rowMax, pendingRowOutline.color, 0.0f, 0, 2.0f);
                }
            }
        }
        
        // Check hover state before EndTable (while table is still the active item)
        bool tableHovered = ImGui::IsItemHovered();
        
        ImGui::EndTable();
        
        // NOTE: Empty space click handling is now done in ViewManager::drawTrackerPanel
        // before calling trackerGUI->draw(). This prevents the focus loop issue where
        // ImGui auto-focuses a cell and then we try to clear it, causing a loop.
        // We keep this check as a fallback for clicks outside the window.
        if (editStep >= 0 && ImGui::IsMouseClicked(0)) {
            // Only clear if click is outside the window entirely
            // (Empty space clicks within the window are handled by ViewManager)
            if (!ImGui::IsWindowHovered()) {
                clearCellFocus();
            }
        }
    }
    
    // No parent widget state to update - cells are directly navigable
    
    ImGui::PopStyleVar(2);
    ImGui::Separator();
}

void TrackerSequencerGUI::drawPatternRow(TrackerSequencer& sequencer, int step, bool isPlaybackStep, bool isEditStep,
                                         bool isPlaying, int currentPlayingStep,
                                         int maxIndex, const std::map<std::string, std::pair<float, float>>& paramRanges,
                                         const std::map<std::string, float>& paramDefaults,
                                         int cachedEditStep, int cachedEditColumn, bool cachedIsEditingCell) {
    ImGui::TableNextRow();
    
    // Static cached colors for performance (calculated once at initialization)
    static ImU32 activeStepColor = ImGui::GetColorU32(ImVec4(0.0f, 0.85f, 0.0f, 0.4f));
    static ImU32 inactiveStepColor = ImGui::GetColorU32(ImVec4(0.4f, 0.8f, 0.4f, 0.3f));
    
    // Determine if this step is currently active (playing)
    // Use cached values passed as parameters instead of calling getters
    bool isCurrentPlayingStep = (currentPlayingStep == step);
    
    // Step is active ONLY if it's the current playing step
    // For length=1 steps, currentPlayingStep is cleared when they finish, so they won't show as active
    bool isStepActive = isCurrentPlayingStep;
    
    // Highlight playback step: green when active, slightly green grey when inactive
    if (isPlaybackStep) {
        if (isStepActive) {
            // Active step: bright green (using cached color)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, activeStepColor);
        } else {
            // Inactive step: slightly green grey (using cached color)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, inactiveStepColor);
        }
    }
    
    // Draw step number column (pass cached values)
    drawStepNumber(sequencer, step, isPlaybackStep, isPlaying, currentPlayingStep);
    
    // Draw dynamic columns (pass cached values for performance)
    for (size_t i = 0; i < sequencer.getColumnConfiguration().size(); i++) {
        drawParameterCell(sequencer, step, (int)i, maxIndex, paramRanges, paramDefaults,
                         cachedEditStep, cachedEditColumn, cachedIsEditingCell);
    }
    
    // After all cells in row are drawn, update row outline XMax if needed
    // This ensures the outline extends to the last cell's right edge
    if (pendingRowOutline.shouldDraw && pendingRowOutline.step == step) {
        // Get the current cell position (should be the last column after the loop)
        ImVec2 lastCellMin = ImGui::GetCursorScreenPos();
        float lastCellWidth = ImGui::GetColumnWidth();
        pendingRowOutline.rowXMax = lastCellMin.x + lastCellWidth + 1;
    }
}

// Helper to sync playback position to edit position when paused
// Uses public methods since static functions don't have friend class access
static void syncPlaybackToEditIfPaused(TrackerSequencer& sequencer, int newStep, bool stepChanged, bool forceTrigger = false) {
    // Trigger if:
    // 1. Not playing AND step changed AND it's a different step, OR
    // 2. Force trigger is true (e.g., navigating from header row back to a step)
    if (!sequencer.isPlaying() && (forceTrigger || (stepChanged && newStep != sequencer.getPlaybackStep()))) {
        sequencer.setCurrentStep(newStep);
        sequencer.triggerStep(newStep);
    }
}

void TrackerSequencerGUI::drawStepNumber(TrackerSequencer& sequencer, int step, bool isPlaybackStep,
                                         bool isPlaying, int currentPlayingStep) {
    ImGui::TableNextColumn();
    
    // Get cell rect for red outline (before drawing button)
    ImVec2 cellMin = ImGui::GetCursorScreenPos();
    float cellHeight = ImGui::GetFrameHeight();
    float cellWidth = ImGui::GetColumnWidth();
    ImVec2 cellMax = ImVec2(cellMin.x + cellWidth, cellMin.y + cellHeight);
    
    char stepBuf[8];
    snprintf(stepBuf, sizeof(stepBuf), "%02d", step + 1);
    
    // Determine if this step is currently active (playing)
    // Use cached values passed as parameters instead of calling getters
    bool isCurrentPlayingStep = (currentPlayingStep == step);
    
    // Step is active ONLY if it's the current playing step
    // For length=1 steps, currentPlayingStep is cleared when they finish, so they won't show as active
    bool isStepActive = isCurrentPlayingStep;
    
    // Apply button styling for active steps (pushed appearance with green tint)
    if (isStepActive) {
        // Use green-tinted active state for pushed appearance
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImVec4(0.2f, 0.7f, 0.2f, 0.8f))); // Green active state
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImVec4(0.25f, 0.75f, 0.25f, 0.9f))); // Brighter green on hover
    }
    
    // CRITICAL: Prevent ImGui from auto-focusing step number cells when clicking empty space
    // This prevents the first step cell from being auto-focused when clicking empty space in the panel
    // Cells should only be focused via explicit clicks or keyboard navigation
    ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
    
    // Note: shouldRefocusCurrentCell flag removed - GUI manages its own focus state
    // Draw button
    bool buttonClicked = ImGui::Button(stepBuf, ImVec2(-1, 0));
    
    // Pop the flag after creating the button
    ImGui::PopItemFlag();
    
    // Pop button styling if we pushed it
    if (isStepActive) {
        ImGui::PopStyleColor(2);
    }
    
    // Prevent spacebar from triggering button clicks (spacebar should only pause/play)
    // ImGui uses spacebar for button activation, but we want spacebar to be global play/pause
    // Check if spacebar is pressed - if so, ignore this button click
    bool spacebarPressed = ImGui::IsKeyPressed(ImGuiKey_Space, false);
    
    // Only set cell focus if button was actually clicked (not just hovered)
    // Verify the click is within the button bounds to prevent false positives
    bool isItemClicked = ImGui::IsItemClicked(0);
    
    if (buttonClicked && !spacebarPressed && isItemClicked) {
        sequencer.triggerStep(step);
        // Note: editStep/editColumn will be synced below when ImGui::IsItemFocused() is true
        // If focus doesn't happen immediately, we still need to set it here for immediate keyboard input
        setEditCell(step, 0);
    }
    
    // ONE-WAY SYNC: ImGui focus → GUI state
    // Only sync when item was actually clicked or keyboard-navigated
    bool actuallyFocused = ImGui::IsItemFocused();
    if (actuallyFocused) {
        bool itemWasClicked = ImGui::IsItemClicked(0);
        bool keyboardNavActive = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
        
        // Only sync if this is an intentional focus (click or keyboard nav)
        if (itemWasClicked || keyboardNavActive) {
            anyCellFocusedThisFrame = true;
            bool cellChanged = (editStep != step || editColumn != 0);
            
            // When in edit mode, prevent focus from changing to a different cell
            if (isEditingCell && cellChanged) {
                return; // Exit early - keep focus locked to editing cell
            }
            
            // Sync state to GUI
            int previousStep = editStep;
            setEditCell(step, 0);
            
            // When paused, sync playback position and trigger step (walk through)
            bool stepChanged = (step != sequencer.getPlaybackStep());
            bool fromHeaderRow = (previousStep == -1);
            if (fromHeaderRow || stepChanged) {
                syncPlaybackToEditIfPaused(sequencer, step, stepChanged, fromHeaderRow);
            }
            
            // Exit edit mode if navigating to a different cell
            if (cellChanged && isEditingCell) {
                setInEditMode(false);
                editBufferCache.clear();
                editBufferInitializedCache = false;
                // Note: ParameterCell manages its own edit buffer state
            }
        }
    }
    
    // Draw outline for selected cells only (not hover)
    // When step number cell is selected, draw outline around entire row
    // Don't draw outline if we're on header row (editStep == -1)
    bool isSelected = (editStep == step && editColumn == 0 && editStep >= 0);
    bool isFocused = ImGui::IsItemFocused();
    bool shouldShowOutline = isSelected || (isFocused && !isEditingCell && editStep >= 0);
    
    if (shouldShowOutline) {
        if (isSelected) {
            // Store row outline info to draw after all cells are drawn
            // Store Y position and first cell X position - XMax will be updated after all cells are drawn
            pendingRowOutline.shouldDraw = true;
            pendingRowOutline.step = step;
            pendingRowOutline.rowYMin = cellMin.y - 1;
            pendingRowOutline.rowYMax = cellMax.y + 1;
            pendingRowOutline.rowXMin = cellMin.x - 1; // Store first cell X position
            pendingRowOutline.rowXMax = cellMax.x + 1; // Will be updated after all cells drawn
            
            // Static cached colors for row outline (calculated once at initialization)
            static ImU32 rowOrangeOutlineColor = ImGui::GetColorU32(ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
            static ImU32 rowRedOutlineColor = ImGui::GetColorU32(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
            
            // Orange outline when in edit mode, red outline when just selected (use cached colors)
            pendingRowOutline.color = (isSelected && isEditingCell)
                ? rowOrangeOutlineColor  // Orange outline in edit mode
                : rowRedOutlineColor; // Red outline when not editing
        } else if (isFocused) {
            // Just draw outline around the step number cell itself (when focused but not selected)
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (drawList) {
                // Static cached color for step number outline
                static ImU32 stepNumberOutlineColor = ImGui::GetColorU32(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                ImVec2 outlineMin = ImVec2(cellMin.x - 1, cellMin.y - 1);
                ImVec2 outlineMax = ImVec2(cellMax.x + 1, cellMax.y + 1);
                drawList->AddRect(outlineMin, outlineMax, stepNumberOutlineColor, 0.0f, 0, 2.0f); // 2px border
            }
        }
    }
}

void TrackerSequencerGUI::drawParameterCell(TrackerSequencer& sequencer, int step, int colConfigIndex,
                                            int maxIndex, const std::map<std::string, std::pair<float, float>>& paramRanges,
                                            const std::map<std::string, float>& paramDefaults,
                                            int cachedEditStep, int cachedEditColumn, bool cachedIsEditingCell) {
    if (colConfigIndex < 0 || colConfigIndex >= (int)sequencer.getColumnConfiguration().size()) {
        return;
    }
    
    const auto& colConfig = sequencer.getColumnConfiguration()[colConfigIndex];
    ImGui::TableNextColumn();
    
    int editColumnValue = colConfigIndex + 1;
    int uniqueId = step * 1000 + colConfig.columnIndex;
    
    // Create ParameterCell instance using adapter method
    ParameterCell paramCell = sequencer.createParameterCellForColumn(step, editColumnValue);
    
    // Sync state from TrackerSequencer to ParameterCell
    bool isSelected = (cachedEditStep == step && cachedEditColumn == editColumnValue);
    paramCell.isSelected = isSelected;
    paramCell.setEditing(cachedIsEditingCell && isSelected);
    // Restore edit buffer cache if editing (for persistence across frames)
    if (cachedIsEditingCell && isSelected) {
        paramCell.setEditBuffer(editBufferCache);
    }
    
    // Restore drag state if this cell is being dragged (for persistence across frames)
    bool isDraggingThis = (sequencer.draggingStep == step && sequencer.draggingColumn == editColumnValue);
    if (isDraggingThis) {
        // Restore drag state to ParameterCell using the setter method
        paramCell.setDragState(true, sequencer.dragStartY, sequencer.dragStartX, sequencer.lastDragValue);
    }
    
    // Determine focus state
    bool isFocused = (cachedEditStep == step && cachedEditColumn == editColumnValue);
    // Note: shouldFocusFirstCell and shouldRefocusCurrentCell flags removed - GUI manages its own focus state
    bool shouldFocusFirst = false;  // Can be re-implemented if needed
    bool shouldRefocusCurrentCell = false;  // Can be re-implemented if needed
    
    // Draw using ParameterCell
    ParameterCellInteraction interaction = paramCell.draw(uniqueId, isFocused, shouldFocusFirst, shouldRefocusCurrentCell);
    
    // Sync state back from ParameterCell to GUI state
    if (interaction.focusChanged) {
        // Update edit step/column if focus changed
        int previousStep = editStep;
        setEditCell(step, editColumnValue);
        anyCellFocusedThisFrame = true;
        
        // When paused, sync playback position and trigger step (walk through)
        bool stepChanged = (previousStep != step);
        bool fromHeaderRow = (previousStep == -1);
        if (fromHeaderRow || stepChanged) {
            syncPlaybackToEditIfPaused(sequencer, step, stepChanged, fromHeaderRow);
        }
    }
    
    if (interaction.clicked) {
        // Cell was clicked - focus it but don't enter edit mode yet
        setEditCell(step, editColumnValue);
        setInEditMode(false);
        // Note: ParameterCell manages its own edit buffer state
    }
    
    // Sync drag state to TrackerSequencer (for persistence across frames)
    // CRITICAL: Always check drag state if we're dragging this cell, even if mouse is outside cell
    // This ensures drag continues across entire window (Blender-style)
    // Helper lambda to sync drag state
    auto syncDragState = [&](bool isDragging) {
        if (isDragging) {
            sequencer.draggingStep = step;
            sequencer.draggingColumn = editColumnValue;
            sequencer.dragStartY = paramCell.getDragStartY();
            sequencer.dragStartX = paramCell.getDragStartX();
            sequencer.lastDragValue = paramCell.getLastDragValue(); // Keep as float for precision
        } else {
            sequencer.draggingStep = -1;
            sequencer.draggingColumn = -1;
        }
    };
    
    if (interaction.dragStarted || interaction.dragEnded || paramCell.getIsDragging() || isDraggingThis) {
        if (paramCell.getIsDragging()) {
            syncDragState(true);
        } else if (isDraggingThis && !paramCell.getIsDragging()) {
            // Drag ended - clear drag state
            syncDragState(false);
        }
    }
    
    // Sync edit mode state and edit buffer cache if editing
    if (paramCell.isEditingMode()) {
        setInEditMode(true);
        // Cache edit buffer for persistence across frames (ParameterCell owns the logic)
        editBufferCache = paramCell.getEditBuffer();
        setEditBufferInitializedCache(paramCell.isEditBufferInitialized());
    } else if (cachedIsEditingCell && isSelected && !paramCell.isEditingMode()) {
        // Exited edit mode
        setInEditMode(false);
        editBufferCache.clear();
        editBufferInitializedCache = false;
    }
    
    // Early exit if requested (prevents further processing for this cell)
    if (interaction.shouldExitEarly) {
        return;
    }
}


// Sync edit state from ImGui focus - called from InputRouter when keys are pressed
bool TrackerSequencerGUI::syncEditStateFromImGuiFocus() {
    // Check if editStep/editColumn are already valid (GUI sync already happened)
    // This method is called from InputRouter before processing keys to ensure state is synced
    // The actual syncing from ImGui focus happens in the draw methods
    // This method just checks if state is already set
    if (editStep >= 0 && editColumn >= 0) {
        return true; // Already synced
    }
    
    // GUI draw sync should handle this every frame
    // If not set, handleKeyPress will default gracefully
    return false;
}

