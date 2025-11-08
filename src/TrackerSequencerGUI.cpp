#include "TrackerSequencerGUI.h"
// Note: TrackerSequencer.h is already included by TrackerSequencerGUI.h
#include "ofxImGui.h"
#include "ofLog.h"
#include <cmath>  // For std::round
#include <map>    // For paramRanges map

TrackerSequencerGUI::TrackerSequencerGUI() 
    : patternDirty(true), lastNumSteps(0), lastPlaybackStep(-1), anyCellFocusedThisFrame(false), parentWidgetId(0) {
    pendingRowOutline.shouldDraw = false;
    pendingRowOutline.step = -1;
}

void TrackerSequencerGUI::draw(TrackerSequencer& sequencer) {
    drawTrackerStatus(sequencer);
    drawPatternGrid(sequencer);
}

void TrackerSequencerGUI::drawTrackerStatus(TrackerSequencer& sequencer) {
    // Status section
    ImGui::Text("Status:");
    ImGui::Text("Playback Step: %d", sequencer.playbackStep + 1);
    ImGui::Text("Edit Step: %d", sequencer.editStep + 1);
    ImGui::Text("Pattern Steps: %d", sequencer.numSteps);
    
    ImGui::Separator();
    
    // Pattern controls
    int newNumSteps = sequencer.numSteps;
    if (ImGui::SliderInt("Steps", &newNumSteps, 4, 64, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        if (newNumSteps != sequencer.numSteps) {
            sequencer.setNumSteps(newNumSteps);
        }
    }
    
    int newStepsPerBeat = sequencer.getStepsPerBeat();
    if (ImGui::SliderInt("Steps Per Beat", &newStepsPerBeat, 1, 96, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        sequencer.setStepsPerBeat(newStepsPerBeat);
    }
    
    if (ImGui::Button("Clear Pattern")) {
        sequencer.clearPattern();
    }
    ImGui::SameLine();
    if (ImGui::Button("Randomize Pattern")) {
        sequencer.randomizePattern();
    }
    
    ImGui::Separator();
}

void TrackerSequencerGUI::drawPatternGrid(TrackerSequencer& sequencer) {
    // Track changes for optimization
    patternDirty = false;
    lastNumSteps = sequencer.numSteps;
    lastPlaybackStep = sequencer.playbackStep;
    
    ImGui::Separator();
    
    // Create a focusable parent widget BEFORE the table for navigation
    // This widget can receive focus when exiting the table via UP key on header row
    ImGui::PushID("TrackerPatternGridParent");
    
    // Create an invisible, focusable button that acts as the parent widget
    // This allows us to move focus here when exiting the table
    // Arrow keys will navigate to other widgets in the panel when this is focused
    // Following ImGui pattern: SetKeyboardFocusHere(0) BEFORE creating widget to request focus
    if (sequencer.requestFocusMoveToParent) {
        ImGui::SetKeyboardFocusHere(0); // Request focus for the upcoming widget
        // Set flag immediately so InputRouter can see it in the same frame
        // (SetKeyboardFocusHere takes effect next frame, but we want InputRouter to know now)
        sequencer.isParentWidgetFocused = true;
        sequencer.editStep = -1;
        sequencer.editColumn = -1;
    }
    
    // Make invisible button span full width to cover empty space between separator and table
    // This prevents clicks in empty space from focusing cells
    float availableWidth = ImGui::GetContentRegionAvail().x;
    ImGui::InvisibleButton("##TrackerGridParent", ImVec2(availableWidth, 20));
    parentWidgetId = ImGui::GetItemID(); // Store ID for potential future use
    
    // Handle clicks on parent widget - clear cell focus when clicked
    // This prevents cell focus from being set when clicking in the area before the first row
    if (ImGui::IsItemClicked(0)) {
        sequencer.clearCellFocus();
        sequencer.isParentWidgetFocused = true;
    }
    
    // Following ImGui pattern: SetItemDefaultFocus() AFTER creating widget to mark as default
    if (sequencer.requestFocusMoveToParent) {
        ImGui::SetItemDefaultFocus(); // Mark this widget as the default focus
        sequencer.requestFocusMoveToParent = false; // Clear flag after using it
    }
    
    // Check if parent widget is focused right after creating it (ImGui pattern: IsItemFocused() works for last item)
    // This updates the state if focus has already moved (e.g., from previous frame's request)
    // If we just requested focus move above, the flag is already set, but we verify here
    if (!sequencer.isParentWidgetFocused) {
        // Only check if we didn't just set it above (to avoid overwriting)
        sequencer.isParentWidgetFocused = ImGui::IsItemFocused();
        if (sequencer.isParentWidgetFocused) {
            sequencer.editStep = -1;
            sequencer.editColumn = -1;
        }
    }
    
    ImGui::PopID();
    
    // Compact styling
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1, 1));
    
    // Ensure column configuration is initialized
    if (sequencer.columnConfig.empty()) {
        sequencer.initializeDefaultColumns();
    }
    
    // PERFORMANCE: Cache all expensive calls ONCE per frame
    // Cache active step info ONCE per frame (not per row!)
    bool isPlaying = sequencer.isPlaying();
    int currentPlayingStep = sequencer.getCurrentPlayingStep();
    int remainingSteps = sequencer.getRemainingSteps();
    int playbackStep = sequencer.playbackStep;
    
    // Cache edit state to avoid repeated member access
    int cachedEditStep = sequencer.editStep;
    int cachedEditColumn = sequencer.editColumn;
    bool cachedIsEditingCell = sequencer.isEditingCell;
    
    // Cache indexRangeCallback result (expensive callback - called once instead of 16+ times)
    int maxIndex = sequencer.indexRangeCallback ? sequencer.indexRangeCallback() : 127;
    
    // Cache parameter ranges and defaults for all parameter columns (expensive lookups - called once instead of 48+ times)
    std::map<std::string, std::pair<float, float>> paramRanges;
    std::map<std::string, float> paramDefaults;
    for (const auto& col : sequencer.columnConfig) {
        // Only cache for dynamic parameters (not fixed columns like "index" or "length")
        if (col.parameterName != "index" && col.parameterName != "length") {
            if (paramRanges.find(col.parameterName) == paramRanges.end()) {
                paramRanges[col.parameterName] = TrackerSequencer::getParameterRange(col.parameterName);
                paramDefaults[col.parameterName] = TrackerSequencer::getParameterDefault(col.parameterName);
            }
        }
    }
    
    // Table setup
    static ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                                   ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                   ImGuiTableFlags_SizingFixedFit;
    
    int totalColumns = 1 + (int)sequencer.columnConfig.size();
    
    if (ImGui::BeginTable("TrackerGrid", totalColumns, flags)) {
        ImGui::TableSetupColumn("##", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        
        // MODULAR: Use isFixed flag to determine column width instead of hardcoded names
        for (const auto& col : sequencer.columnConfig) {
            float colWidth = col.isFixed ? 45.0f : 60.0f; // Fixed columns (index/length) are narrower
            ImGui::TableSetupColumn(col.displayName.c_str(), ImGuiTableColumnFlags_WidthFixed, colWidth);
        }
        
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        
        // Draw pattern rows
        pendingRowOutline.shouldDraw = false; // Reset row outline state
        anyCellFocusedThisFrame = false; // Reset focus tracking
        for (int step = 0; step < sequencer.numSteps; step++) {
            drawPatternRow(sequencer, step, step == playbackStep, step == cachedEditStep, 
                          isPlaying, currentPlayingStep, remainingSteps,
                          maxIndex, paramRanges, paramDefaults, cachedEditStep, cachedEditColumn, cachedIsEditingCell);
        }
        
        // After drawing all rows, if editStep is set but no cell was focused,
        // clear the sequencer's cell focus (focus moved to header row or elsewhere)
        // BUT: Don't clear if we just exited edit mode (shouldRefocusCurrentCell flag is set)
        // This prevents focus loss when ImGui temporarily loses focus after exiting edit mode
        // NOTE: This check happens AFTER all cells are drawn, so it won't interfere with
        // the ViewManager's empty space click handling which clears focus BEFORE drawing
        if (sequencer.getEditStep() >= 0 && !anyCellFocusedThisFrame && !sequencer.getIsEditingCell() && !sequencer.shouldRefocusCurrentCell) {
            sequencer.clearCellFocus();
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
        if (sequencer.getEditStep() >= 0 && ImGui::IsMouseClicked(0)) {
            // Only clear if click is outside the window entirely
            // (Empty space clicks within the window are handled by ViewManager)
            if (!ImGui::IsWindowHovered()) {
                sequencer.clearCellFocus();
            }
        }
    }
    
    // CRITICAL: Update parent widget focus state AFTER the table ends
    // Following ImGui pattern: We can't check IsItemFocused() for a widget created earlier,
    // so we infer the state based on what we know:
    // - If any cell was focused, parent widget is definitely not focused
    // - If no cell is focused and editStep == -1, we might be on header row or parent widget
    // - The state checked right after creating the button is still valid if no cells were focused
    if (anyCellFocusedThisFrame || sequencer.editStep >= 0) {
        // A cell is focused, so parent widget is definitely not focused
        sequencer.isParentWidgetFocused = false;
    }
    // Otherwise, keep the state we checked right after creating the button
    // This follows ImGui's pattern: IsItemFocused() is only valid for the last item,
    // so we rely on the state we captured when the widget was created
    
    ImGui::PopStyleVar(2);
    ImGui::Separator();
}

void TrackerSequencerGUI::drawPatternRow(TrackerSequencer& sequencer, int step, bool isPlaybackStep, bool isEditStep,
                                         bool isPlaying, int currentPlayingStep, int remainingSteps,
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
    drawStepNumber(sequencer, step, isPlaybackStep, isPlaying, currentPlayingStep, remainingSteps);
    
    // Draw dynamic columns (pass cached values for performance)
    for (size_t i = 0; i < sequencer.columnConfig.size(); i++) {
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
                                         bool isPlaying, int currentPlayingStep, int remainingSteps) {
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
    
    // Don't force focus - let ImGui handle navigation naturally
    // Only sync our state FROM ImGui's focus, not the other way around
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
        sequencer.editStep = step;
        sequencer.editColumn = 0;
    }
    
    // ONE-WAY SYNC: ImGui focus → Sequencer state
    // BUT: Only sync when item was actually clicked, keyboard-navigated, or refocusing after edit
    // This prevents auto-focus from empty space clicks from selecting cells
    bool shouldExitEarly = false;
    if (ImGui::IsItemFocused()) {
        // Only sync focus if:
        // 1. Item was clicked (explicit user action)
        // 2. Keyboard navigation is active (user is navigating with arrow keys)
        // 3. We're refocusing after exiting edit mode (shouldRefocusCurrentCell flag)
        bool itemWasClicked = ImGui::IsItemClicked(0);
        bool keyboardNavActive = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
        bool shouldRefocus = (step == sequencer.getEditStep() && sequencer.shouldRefocusCurrentCell);
        
        // Only sync if this is an intentional focus (click, keyboard nav, or refocus)
        if (itemWasClicked || keyboardNavActive || shouldRefocus) {
            anyCellFocusedThisFrame = true; // Track that a cell is focused
            bool cellChanged = (sequencer.editStep != step || sequencer.editColumn != 0);
            
            // CRITICAL: When in edit mode, prevent focus from changing to a different cell
            // This prevents arrow keys (used for value adjustment) from accidentally changing focus
            if (sequencer.isEditingCell && cellChanged) {
                // Don't sync focus changes when in edit mode - keep focus locked to editing cell
                // NOTE: Don't return early here - we need to pop styles below
                shouldExitEarly = true;
            } else {
                int previousStep = sequencer.editStep;
                sequencer.editStep = step;
                sequencer.editColumn = 0;
                
                // When paused, sync playback position and trigger step (walk through)
                // Trigger if step changed OR if we're navigating from header row (previousStep == -1) to a step
                bool stepChanged = (step != sequencer.getPlaybackStep());
                bool fromHeaderRow = (previousStep == -1);
                if (fromHeaderRow || stepChanged) {
                    // Force trigger when coming from header row (even if same step)
                    syncPlaybackToEditIfPaused(sequencer, sequencer.editStep, stepChanged, fromHeaderRow);
                }
                
                // If navigating to a different cell while in edit mode, exit edit mode
                // (This shouldn't happen now due to the check above, but keep as safety)
                if (cellChanged && sequencer.isEditingCell) {
                    sequencer.isEditingCell = false;
                    sequencer.editBuffer.clear();
                    sequencer.editBufferInitialized = false;
                }
            }
        }
        // If focus is set but not from click/keyboard/refocus, don't sync (prevents auto-selection)
    }
    
    // Early exit after syncing (but before drawing outline)
    if (shouldExitEarly) {
        return;
    }
    
    // Draw outline for selected cells only (not hover)
    // When step number cell is selected, draw outline around entire row
    // Don't draw outline if we're on header row (editStep == -1)
    bool isSelected = (sequencer.editStep == step && sequencer.editColumn == 0 && sequencer.editStep >= 0);
    bool isFocused = ImGui::IsItemFocused();
    bool shouldShowOutline = isSelected || (isFocused && !sequencer.isEditingCell && sequencer.editStep >= 0);
    
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
            pendingRowOutline.color = (isSelected && sequencer.isEditingCell)
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
    if (colConfigIndex < 0 || colConfigIndex >= (int)sequencer.columnConfig.size()) {
        return;
    }
    
    const auto& colConfig = sequencer.columnConfig[colConfigIndex];
    ImGui::TableNextColumn();
    ImGui::PushID(step * 1000 + colConfig.columnIndex);
    
    int editColumnValue = colConfigIndex + 1;
    auto& cell = sequencer.pattern[step];
    // Use cached edit state instead of accessing sequencer members repeatedly
    bool isSelected = (cachedEditStep == step && cachedEditColumn == editColumnValue);
    
    // Static cached colors for performance
    static ImU32 fillBarColor = ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 0.25f));
    static ImU32 redOutlineColor = ImGui::GetColorU32(ImVec4(0.9f, 0.05f, 0.1f, 1.0f));
    static ImU32 orangeOutlineColor = ImGui::GetColorU32(ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
    
    // Get display text and value based on column type
    // Use colConfig.isFixed to determine if it's a fixed column (index/length) vs dynamic parameter
    std::string displayText;
    float fillPercent = 0.0f;
    
    if (colConfig.isFixed && colConfig.parameterName == "index") {
        int currentMediaIdx = cell.index;
        // Show edit buffer when editing (even if empty, to show edit mode is active)
        if (cachedIsEditingCell && isSelected) {
            displayText = sequencer.editBuffer.empty() ? "00" : sequencer.editBuffer;
        } else if (currentMediaIdx >= 0) {
            // Use cached maxIndex instead of calling callback
            if (currentMediaIdx < maxIndex) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%02d", currentMediaIdx + 1);
                displayText = buf;
                if (maxIndex > 0) {
                    fillPercent = (float)(currentMediaIdx + 1) / (float)maxIndex;
                }
            }
        } else {
            displayText = "--";
        }
    } else if (colConfig.isFixed && colConfig.parameterName == "length") {
        // If index is -1 (empty/rest), show "--" for length as well
        if (cell.index < 0) {
            displayText = "--";
            fillPercent = 0.0f;
        } else {
        int stepCount = std::max(1, std::min(16, cell.length));
        char buf[8];
        if (cachedIsEditingCell && isSelected && !sequencer.editBuffer.empty()) {
            snprintf(buf, sizeof(buf), "%s", sequencer.editBuffer.c_str());
        } else {
            snprintf(buf, sizeof(buf), "%02d", stepCount);
        }
        displayText = buf;
        fillPercent = (float)stepCount / 16.0f;
        }
    } else {
        // Dynamic parameter column - check if parameter is set
        if (!cell.hasParameter(colConfig.parameterName)) {
            // Parameter not set - display "--" (similar to empty index)
            displayText = "--";
            fillPercent = 0.0f;
        } else {
            // Parameter is set - use cached parameter range
            auto rangeIt = paramRanges.find(colConfig.parameterName);
            if (rangeIt == paramRanges.end()) {
                // Fallback if range not cached (shouldn't happen, but safe)
                displayText = "0.00";
            } else {
                // Get the actual value (parameter is set, so this returns the stored value)
                auto defaultIt = paramDefaults.find(colConfig.parameterName);
                float defaultValue = (defaultIt != paramDefaults.end()) ? defaultIt->second : 0.0f;
                float value = cell.getParameterValue(colConfig.parameterName, defaultValue);
                
                // Display actual float value with appropriate precision
                char buf[16];
                if (cachedIsEditingCell && isSelected) {
                    // Show edit buffer when editing (use current value if buffer empty)
                    if (!sequencer.editBuffer.empty()) {
                        snprintf(buf, sizeof(buf), "%s", sequencer.editBuffer.c_str());
                    } else {
                        // Buffer empty - show current value as fallback
                        snprintf(buf, sizeof(buf), "%.2f", value);
                    }
                } else {
                    // Display actual float value with appropriate decimal precision (all use 2 decimal places)
                    snprintf(buf, sizeof(buf), "%.2f", value);
                }
                displayText = buf;
                
                // Calculate fill percent for visualization (normalize to 0-1 range)
                const auto& range = rangeIt->second;
                float minVal = range.first;
                float maxVal = range.second;
                float rangeSize = maxVal - minVal;
                if (rangeSize > 0.0f) {
                    fillPercent = (value - minVal) / rangeSize;
                    fillPercent = std::max(0.0f, std::min(1.0f, fillPercent)); // Clamp to 0-1
                } else {
                    fillPercent = 0.0f;
                }
            }
        }
    }
    
    // Draw value bar visualization first (as true background layer)
    // We need to predict the button position, so get the cell rect
    ImVec2 cellMin = ImGui::GetCursorScreenPos();
    float cellHeight = ImGui::GetFrameHeight();
    float cellWidth = ImGui::GetColumnWidth();
    ImVec2 cellMax = ImVec2(cellMin.x + cellWidth, cellMin.y + cellHeight);
    
    if (fillPercent > 0.01f) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (drawList) {
            ImVec2 fillEnd = ImVec2(cellMin.x + (cellMax.x - cellMin.x) * fillPercent, cellMax.y);
            // Use cached color instead of calculating every frame
            drawList->AddRectFilled(cellMin, fillEnd, fillBarColor);
        }
    }
    
    // Apply edit mode styling: dark grey/black background (Blender-style)
    // This provides clear visual feedback when a cell is being edited
    if (cachedIsEditingCell && isSelected) {
        // Dark grey/black background for edit mode
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.8f)); // Dark grey/black background
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.05f, 0.05f, 0.05f, 0.8f)); // Slightly lighter on hover
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.1f, 0.1f, 1.0f)); // Lighter when active
        // Keep text white for better visibility
    } else {
        // Make button backgrounds completely transparent when not editing
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); // Fully transparent
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0)); // No hover background
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0)); // No active background
    }
    
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(1.0f, 0.5f));
    
    // CRITICAL: Prevent ImGui from auto-focusing cells when clicking empty space
    // This prevents the first cell from being auto-focused when clicking empty space in the panel
    // Cells should only be focused via explicit clicks or keyboard navigation
    ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
    
    // Set focus on first cell if requested (when Enter is pressed on focused table)
    // This should be the first data column (column 1), first row (step 0)
    // Note: SetItemDefaultFocus() won't work with NoNavDefaultFocus, so we use SetKeyboardFocusHere() instead
    if (step == 0 && editColumnValue == 1 && sequencer.shouldFocusFirstCell) {
        ImGui::SetKeyboardFocusHere(0); // Request focus for the upcoming button
        sequencer.shouldFocusFirstCell = false; // Clear the flag after setting focus
    }
    
    // Check if button was clicked
    bool buttonClicked = ImGui::Button(displayText.c_str(), ImVec2(-1, 0));
    
    // Pop the flag after creating the button
    ImGui::PopItemFlag();
    
    // Refocus current cell after exiting edit mode (to prevent focus loss)
    // This ensures the cell remains selected after validating edits with Enter
    // MUST be called AFTER button creation to work properly
    // Use SetKeyboardFocusHere(-1) to focus the current item immediately
    if (sequencer.shouldRefocusCurrentCell && step == sequencer.getEditStep() && editColumnValue == sequencer.getEditColumn()) {
        ImGui::SetKeyboardFocusHere(-1); // Focus the button we just created
        sequencer.shouldRefocusCurrentCell = false; // Clear the flag after setting focus
        // Re-enable keyboard navigation now that we've refocused
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }
    
    // Prevent spacebar from triggering button clicks (spacebar should only pause/play)
    // ImGui uses spacebar for button activation, but we want spacebar to be global play/pause
    bool spacebarPressed = ImGui::IsKeyPressed(ImGuiKey_Space, false);
    
    // Sync ImGui focus to sequencer state
    // BUT: Only sync when item was actually clicked, keyboard-navigated, or refocusing after edit
    // This prevents auto-focus from empty space clicks from selecting cells
    bool shouldExitEarly = false;
    if (ImGui::IsItemFocused()) {
        // Only sync focus if:
        // 1. Item was clicked (explicit user action)
        // 2. Keyboard navigation is active (user is navigating with arrow keys)
        // 3. We're refocusing after exiting edit mode (shouldRefocusCurrentCell flag)
        bool itemWasClicked = ImGui::IsItemClicked(0);
        bool keyboardNavActive = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
        bool shouldRefocus = (step == sequencer.getEditStep() && editColumnValue == sequencer.getEditColumn() && sequencer.shouldRefocusCurrentCell);
        
        // Only sync if this is an intentional focus (click, keyboard nav, or refocus)
        if (itemWasClicked || keyboardNavActive || shouldRefocus) {
            anyCellFocusedThisFrame = true; // Track that a cell is focused
            bool cellChanged = (cachedEditStep != step || cachedEditColumn != editColumnValue);
            bool stepChanged = (cachedEditStep != step);
            
            // Lock focus to editing cell - arrow keys adjust values, not navigate
            if (cachedIsEditingCell && cellChanged) {
                // Don't sync focus change during edit
                // NOTE: Don't return early here - we need to pop styles below
                shouldExitEarly = true;
            } else {
                // Sync focus state
                int previousStep = sequencer.editStep;
                sequencer.editStep = step;
                sequencer.editColumn = editColumnValue;
                
                // When paused, sync playback position and trigger step (walk through)
                // Trigger if step changed OR if we're navigating from header row (previousStep == -1) to a step
                bool fromHeaderRow = (previousStep == -1);
                if (fromHeaderRow || stepChanged) {
                    // Force trigger when coming from header row (even if same step)
                    syncPlaybackToEditIfPaused(sequencer, sequencer.editStep, stepChanged, fromHeaderRow);
                }
                
                // Don't consider selected if we're on header row (editStep == -1)
                isSelected = (sequencer.editStep == step && sequencer.editColumn == editColumnValue && sequencer.editStep >= 0);
            }
        }
        // If focus is set but not from click/keyboard/refocus, don't sync (prevents auto-selection)
    }
    
    // Early exit after syncing (but before drawing outline)
    if (shouldExitEarly) {
        // Pop style var and colors before returning
        ImGui::PopStyleVar(1); // Pop ButtonTextAlign
        ImGui::PopStyleColor(3); // Pop 3 button colors
        ImGui::PopID();
        return;
    }
    
    // Single click (no drag): Focus cell but DON'T enter edit mode
    // User can either type numbers directly OR hit Enter to enter edit mode
    // IMPORTANT: Prevent spacebar from triggering focus (spacebar should only pause/play)
    // Verify the click is actually on this button to prevent false positives from clicks before first row
    bool isItemClicked = ImGui::IsItemClicked(0);
    
    if (buttonClicked && !ImGui::IsMouseDragging(0) && !spacebarPressed && isItemClicked) {
        sequencer.editStep = step;
        sequencer.editColumn = editColumnValue;
        // Update isSelected after syncing
        isSelected = true;
        
        // DON'T enter edit mode on click - just focus the cell
        // User can type numbers directly (auto-enters edit mode) or hit Enter to enter edit mode
        sequencer.isEditingCell = false;
        sequencer.editBuffer.clear();
        sequencer.editBufferInitialized = false;
    }
    
    // Simplified drag handling (unified for all column types)
    handleDragEditing(sequencer, step, editColumnValue, colConfig, cell, paramRanges, paramDefaults);
    
    // Pop style var and colors (must match pushes)
    ImGui::PopStyleVar(1); // Pop ButtonTextAlign
    ImGui::PopStyleColor(3); // Pop 3 button colors
    
        // Draw outline for selected/editing cells
        // Don't show outline if we're on header row (cachedEditStep == -1)
        isSelected = (cachedEditStep == step && cachedEditColumn == editColumnValue && cachedEditStep >= 0);
        bool shouldShowOutline = isSelected || (ImGui::IsItemFocused() && !cachedIsEditingCell && cachedEditStep >= 0);
        if (shouldShowOutline) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (drawList) {
                ImVec2 outlineMin = ImVec2(cellMin.x - 1, cellMin.y - 1);
                ImVec2 outlineMax = ImVec2(cellMax.x + 1, cellMax.y + 1);
                // Orange outline when in edit mode, red outline when just focused (use cached colors)
                ImU32 outlineColor = (isSelected && cachedIsEditingCell)
                    ? orangeOutlineColor  // Orange outline in edit mode
                    : redOutlineColor; // Red outline when not editing
                drawList->AddRect(outlineMin, outlineMax, outlineColor, 0.0f, 0, 2.0f); // 2px border
            }
        }
    
    ImGui::PopID();
}

// Unified drag handling for all column types
void TrackerSequencerGUI::handleDragEditing(TrackerSequencer& sequencer, int step, int editColumnValue,
                                             const TrackerSequencer::ColumnConfig& colConfig, 
                                             TrackerSequencer::PatternCell& cell,
                                             const std::map<std::string, std::pair<float, float>>& paramRanges,
                                             const std::map<std::string, float>& paramDefaults) {
    bool isSelected = (sequencer.editStep == step && sequencer.editColumn == editColumnValue);
    bool isDraggingThis = (sequencer.draggingStep == step && sequencer.draggingColumn == editColumnValue);
    
    // If we're already dragging this cell, process the drag globally (works even if mouse is not over cell)
    if (isDraggingThis) {
        if (ImGui::IsMouseDown(0) && ImGui::IsMouseDragging(0)) {
            // Continue processing drag - mouse can be anywhere on screen
            // Calculate total delta from original drag start position
            ImVec2 currentPos = ImGui::GetMousePos();
            float totalDragDeltaY = sequencer.dragStartY - currentPos.y; // Up = positive (increase)
            float totalDragDeltaX = currentPos.x - sequencer.dragStartX; // Right = positive (increase)
            // Use the direction with the most movement (vertical or horizontal)
            // Both directions work: vertical (up/down) and horizontal (left/right)
            float totalDragDelta = std::abs(totalDragDeltaX) > std::abs(totalDragDeltaY) ? totalDragDeltaX : totalDragDeltaY;
            
            // Improved precision: use smaller sensitivity for finer control
            // 1 pixel = 0.1 units, so 10 pixels = 1 unit
            float dragSensitivity = 10.0f;
            float valueDelta = totalDragDelta / dragSensitivity;
            
            // Get initial value to calculate new value from
            int initialValue = sequencer.lastDragValue;
            float newValueFloat = initialValue + valueDelta;
            int newValue = (int)std::round(newValueFloat);
            
            // Apply value based on column type
            if (colConfig.isFixed && colConfig.parameterName == "index") {
                int maxIndex = sequencer.indexRangeCallback ? sequencer.indexRangeCallback() : 127;
                newValue = std::max(0, std::min(maxIndex, newValue));
                if (newValue == 0) {
                    cell.index = -1;
                } else {
                    cell.index = newValue - 1;
                }
                sequencer.setCell(step, cell);
            } else if (colConfig.isFixed && colConfig.parameterName == "length") {
                newValue = std::max(1, std::min(16, newValue));
                cell.length = newValue;
                sequencer.setCell(step, cell);
            } else {
                // Parameter value - use helper function for proper range conversion
                newValue = std::max(0, std::min(127, newValue));
                float actualValue = TrackerSequencer::displayValueToParameter(colConfig.parameterName, (float)newValue);
                cell.setParameterValue(colConfig.parameterName, actualValue);
                sequencer.setCell(step, cell);
            }
        } else {
            // Mouse released - clean up
            sequencer.draggingStep = -1;
            sequencer.draggingColumn = -1;
            sequencer.lastDragValue = -1;
        }
        return; // Exit early - we're already dragging this cell
    }
    
    // Not dragging this cell - check if we should start a drag
    // Start drag if:
    // 1. This cell is selected AND
    // 2. Mouse is down on this item (clicked) AND
    // 3. Mouse is dragging (moved after click)
    if (isSelected && ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        // Exit edit mode when dragging starts
        if (sequencer.isEditingCell) {
            sequencer.isEditingCell = false;
            sequencer.editBuffer.clear();
            sequencer.editBufferInitialized = false;
        }
        
        // Start drag - initialize drag state
        sequencer.draggingStep = step;
        sequencer.draggingColumn = editColumnValue;
        sequencer.dragStartY = ImGui::GetMousePos().y;
        sequencer.dragStartX = ImGui::GetMousePos().x;
        
        // Get initial value based on column type
        if (colConfig.isFixed && colConfig.parameterName == "index") {
            sequencer.lastDragValue = cell.index >= 0 ? cell.index + 1 : 0;
        } else if (colConfig.isFixed && colConfig.parameterName == "length") {
            sequencer.lastDragValue = cell.length;
        } else {
            // Dynamic parameter value
            auto defaultIt = paramDefaults.find(colConfig.parameterName);
            float defaultValue = (defaultIt != paramDefaults.end()) ? defaultIt->second : 0.0f;
            float value = cell.getParameterValue(colConfig.parameterName, defaultValue);
            float displayValueFloat = TrackerSequencer::parameterToDisplayValue(colConfig.parameterName, value);
            sequencer.lastDragValue = (int)std::round(displayValueFloat);
        }
    }
}

// Legacy methods - kept for backward compatibility but not used
void TrackerSequencerGUI::drawMediaIndex(TrackerSequencer& sequencer, int step) {
    // Deprecated - use drawParameterCell instead
}

void TrackerSequencerGUI::drawPosition(TrackerSequencer& sequencer, int step) {
    // Deprecated - use drawParameterCell instead
}

void TrackerSequencerGUI::drawSpeed(TrackerSequencer& sequencer, int step) {
    // Deprecated - use drawParameterCell instead
}

void TrackerSequencerGUI::drawVolume(TrackerSequencer& sequencer, int step) {
    // Deprecated - use drawParameterCell instead
}

void TrackerSequencerGUI::drawStepLength(TrackerSequencer& sequencer, int step) {
    // Deprecated - use drawParameterCell instead
}

void TrackerSequencerGUI::drawValueBar(float fillPercent) {
    // Value bar is now drawn before the button in drawParameterCell
    // This function is kept for compatibility but not used
}

// Sync edit state from ImGui focus - called from InputRouter when keys are pressed
bool TrackerSequencerGUI::syncEditStateFromImGuiFocus(TrackerSequencer& sequencer) {
    // Check if editStep/editColumn are already valid (GUI sync already happened)
    if (sequencer.editStep >= 0 && sequencer.editStep < sequencer.numSteps && 
        sequencer.editColumn >= 0) {
        return true; // Already synced
    }
    
    // GUI draw sync should handle this every frame
    // If not set, handleKeyPress will default gracefully
    return false;
}
