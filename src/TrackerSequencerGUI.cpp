#include "TrackerSequencerGUI.h"
// Note: TrackerSequencer.h is already included by TrackerSequencerGUI.h
#include "CellWidget.h"
#include "core/ModuleRegistry.h"
#include "gui/HeaderPopup.h"
#include "gui/GUIManager.h"
// ParameterCell.h no longer needed - ParameterCell is now an implementation detail of ModuleGUI
#include <imgui.h>
#include "ofLog.h"
#include "gui/GUIConstants.h"
#include <cmath>  // For std::round
#include <limits>  // For std::numeric_limits
#include <set>
#include <map>

// Helper to sync playback position to edit position when paused
// Uses public methods since static functions don't have friend class access
static void syncPlaybackToEditIfPaused(TrackerSequencer& sequencer, int newStep, bool stepChanged, bool forceTrigger, int& lastTriggeredStep) {
    // Reset tracking when playing (not needed when playing)
    if (sequencer.isPlaying()) {
        lastTriggeredStep = -1;
        return;
    }
    
    // Trigger if:
    // 1. Not playing AND step changed AND it's a different step, OR
    // 2. Force trigger is true (e.g., navigating from header row back to a step)
    // AND we haven't already triggered this step
    if (forceTrigger || (stepChanged && newStep != sequencer.getPlaybackStep())) {
        // Only trigger if this is a different step than the last one we triggered
        if (newStep != lastTriggeredStep) {
            sequencer.setCurrentStep(newStep);
            sequencer.triggerStep(newStep);
            lastTriggeredStep = newStep;
        }
    }
}

TrackerSequencerGUI::TrackerSequencerGUI() 
    : lastPatternIndex(-1), lastTriggeredStepWhenPaused(-1) {
    // cellFocusState and callbacksState are initialized by default constructors
    pendingRowOutline.shouldDraw = false;
    pendingRowOutline.step = -1;
}

void TrackerSequencerGUI::restoreImGuiKeyboardNavigation() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

void TrackerSequencerGUI::clearCellFocus() {
    // If we were in edit mode, restore ImGui keyboard navigation
    // Note: This should only happen when explicitly clearing focus (e.g., clicking away)
    // The onEditModeChanged callback handles normal edit mode exit
    if (cellFocusState.isEditing) {
                ofLogNotice("TrackerSequencerGUI") << "[CLEAR_FOCUS] Clearing pattern grid focus while in edit mode - restoring navigation";
        restoreImGuiKeyboardNavigation();
    }
    if (patternParamsFocusState.isEditing) {
                ofLogNotice("TrackerSequencerGUI") << "[CLEAR_FOCUS] Clearing pattern params focus while in edit mode - restoring navigation";
        restoreImGuiKeyboardNavigation();
    }
    
    ModuleGUI::clearCellFocus(cellFocusState);
    ModuleGUI::clearCellFocus(patternParamsFocusState);
}

void TrackerSequencerGUI::draw(TrackerSequencer& sequencer) {
    // Legacy method: draw with direct reference (for backward compatibility)
    drawPatternChain(sequencer);
    drawPatternControls(sequencer);
    drawPatternGrid(sequencer);
}

TrackerSequencer* TrackerSequencerGUI::getTrackerSequencer() const {
    // If instance-aware (has registry and instanceName), use that
    if (getRegistry() && !getInstanceName().empty()) {
        auto module = getRegistry()->getModule(getInstanceName());
        if (!module) return nullptr;
        return dynamic_cast<TrackerSequencer*>(module.get());
    }
    
    // Fallback: return nullptr (no legacy direct reference for TrackerSequencer)
    return nullptr;
}

void TrackerSequencerGUI::draw() {
    // Call base class draw (handles visibility, title bar, enabled state)
    ModuleGUI::draw();
}

void TrackerSequencerGUI::drawContent() {
    // Instance-aware draw method
    TrackerSequencer* sequencer = getTrackerSequencer();
    if (!sequencer) {
        ImGui::Text("Instance '%s' not found", getInstanceName().empty() ? "unknown" : getInstanceName().c_str());
        return;
    }
    
    drawPatternChain(*sequencer);
    drawPatternControls(*sequencer);
    drawPatternGrid(*sequencer);
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
    const float buttonsStartY = ImGui::GetCursorPosY();
    
    // Draw pattern chain entries (top row)
    for (size_t i = 0; i < chain.size(); i++) {
        int patternIdx = chain[i];
        bool isCurrentChainEntry = ((int)i == currentChainIndex);
        bool isCurrentPattern = (patternIdx == currentPatternIndex);
        bool isDisabled = sequencer.isPatternChainEntryDisabled((int)i);
        
        ImGui::PushID((int)i);
        
        // Pattern cell - clickable to select pattern (Renoise-style)
        ImVec2 cellSize(PATTERN_CELL_WIDTH, PATTERN_CELL_HEIGHT);
        
        // Color coding: blue for current pattern, gray for current chain position, dark for others, red tint if disabled
        ImU32 bgColor;
        if (isDisabled) {
            bgColor = GUIConstants::toU32(GUIConstants::Outline::DisabledBg);
        } else if (isCurrentPattern && isPlaying) {
            bgColor = GUIConstants::toU32(GUIConstants::Active::PatternPlaying);
        } else if (isCurrentPattern) {
            bgColor = GUIConstants::toU32(GUIConstants::Active::Pattern);
        } else if (isCurrentChainEntry) {
            bgColor = GUIConstants::toU32(GUIConstants::Active::ChainEntry);
        } else {
            bgColor = GUIConstants::toU32(GUIConstants::Active::ChainEntryInactive);
        }
        
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        
        // Draw background
        drawList->AddRectFilled(cursorPos, ImVec2(cursorPos.x + cellSize.x, cursorPos.y + cellSize.y), bgColor);
        
        // Draw border for current chain entry
        if (isCurrentChainEntry) {
            ImU32 borderColor = GUIConstants::toU32(GUIConstants::Active::ChainEntryBorder);
            drawList->AddRect(cursorPos, ImVec2(cursorPos.x + cellSize.x, cursorPos.y + cellSize.y), borderColor, 0.0f, 0, 1.5f);
        }
        
        // Draw diagonal line if disabled
        if (isDisabled) {
            ImU32 lineColor = GUIConstants::toU32(GUIConstants::Outline::Disabled);
            drawList->AddLine(cursorPos, ImVec2(cursorPos.x + cellSize.x, cursorPos.y + cellSize.y), lineColor, OUTLINE_THICKNESS);
        }
        
        // Pattern number text (2-digit format: 01, 02, 03, etc.)
        // Display chain position (1-based) instead of actual pattern index for sequential numbering
        char patternLabel[BUFFER_SIZE];
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
        const float verticalOffset = (PATTERN_CELL_HEIGHT - BUTTON_HEIGHT) * 0.5f;
        const float buttonsY = buttonsStartY + verticalOffset;
    
    // 'D' button for duplicate current pattern
    ImGui::SetCursorPosY(buttonsY);
    if (ImGui::Button("D", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
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
    if (ImGui::Button("+", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
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
    if (ImGui::Button("-", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT)) && canRemove) {
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
        ImGui::PushID((int)(i + REPEAT_COUNT_ID_OFFSET));  // Different ID range to avoid conflicts
        
        int repeatCount = sequencer.getPatternChainRepeatCount((int)i);
        bool isCurrentChainEntry = ((int)i == currentChainIndex);
        
        ImVec2 repeatCellSize(PATTERN_CELL_WIDTH, REPEAT_CELL_HEIGHT);
        ImGui::PushItemWidth(repeatCellSize.x);
        
        // Editable repeat count (small input field, similar to pattern grid cells)
        char repeatBuf[BUFFER_SIZE];
        snprintf(repeatBuf, sizeof(repeatBuf), "%d", repeatCount);
        
        // Style the repeat count cell to match pattern cell
        if (isCurrentChainEntry) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, GUIConstants::Frame::ChainEntry);
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

void TrackerSequencerGUI::drawPatternControls(TrackerSequencer& sequencer) {
    // Action buttons row
    if (ImGui::Button("Clear Pattern")) {
        sequencer.clearPattern();
    }
    ImGui::SameLine();
    
    // 'D' button to double steps (duplicate all steps to double pattern length)
    if (ImGui::Button("D", ImVec2(20, 20))) {
        sequencer.getCurrentPattern().doubleSteps();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Double pattern length (duplicate all steps)");
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Pattern parameters table using CellGrid (similar to MediaPool's drawParameters)
    // Reset focus tracking at start of frame
    patternParamsCallbacksState.resetFrame();
    
    // Configure CellGrid using unified helper
    CellGridConfig gridConfig;
    gridConfig.tableId = "PatternParametersTable";
    gridConfig.tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                            ImGuiTableFlags_SizingStretchProp;
    configureCellGrid(patternParametersGrid, gridConfig);
    
    // Column configuration: Steps and Steps Per Beat
    std::vector<CellGridColumnConfig> paramsColumnConfig;
    paramsColumnConfig.push_back(CellGridColumnConfig("steps", "Steps", false, 0, false));
    paramsColumnConfig.push_back(CellGridColumnConfig("stepsPerBeat", "Steps Per Beat", false, 1, false));
    
    // Update column configuration using unified helper (only updates if changed)
    updateColumnConfigIfChanged(patternParametersGrid, paramsColumnConfig, lastPatternParamsColumnConfig);
    
    // Setup callbacks for CellGrid
    CellGridCallbacks callbacks;
    
    // Setup standard callbacks (focus tracking, edit mode, state sync)
    setupStandardCellGridCallbacks(callbacks, patternParamsFocusState, 
                                   patternParamsCallbacksState, 
                                   patternParametersGrid, true); // true = single row
    
    // Create cell widgets for Steps and SPB
    callbacks.createCellWidget = [this, &sequencer](int row, int col, const CellGridColumnConfig& colConfig) -> CellWidget {
        const std::string& paramName = colConfig.parameterName;
        
        if (paramName == "steps") {
            ParameterDescriptor stepsParam("steps", ParameterType::INT, 4, 64, 16, "Steps");
            CellWidget widget = createCellWidget(
                stepsParam,
                [&sequencer]() -> float {
                    return (float)sequencer.getCurrentPattern().getStepCount();
                },
                [&sequencer](float value) {
                    sequencer.getCurrentPattern().setStepCount((int)value);
                }
            );
            widget.isRemovable = false;
            return widget;
        } else if (paramName == "stepsPerBeat") {
            ParameterDescriptor spbParam("stepsPerBeat", ParameterType::INT, 1, 96, 4, "Steps Per Beat");
            CellWidget widget = createCellWidget(
                spbParam,
                [&sequencer]() -> float {
                    return (float)sequencer.getStepsPerBeat();
                },
                [&sequencer](float value) {
                    sequencer.setStepsPerBeat((int)value);
                }
            );
            widget.isRemovable = false;
            return widget;
        }
        
        return CellWidget(); // Return empty cell if not found
    };
    
    // Get cell values
    callbacks.getCellValue = [&sequencer](int row, int col, const CellGridColumnConfig& colConfig) -> float {
        const std::string& paramName = colConfig.parameterName;
        
        if (paramName == "steps") {
            return (float)sequencer.getCurrentPattern().getStepCount();
        } else if (paramName == "stepsPerBeat") {
            return (float)sequencer.getStepsPerBeat();
        }
        
        return 0.0f;
    };
    
    // Set cell values
    callbacks.setCellValue = [&sequencer](int row, int col, float value, const CellGridColumnConfig& colConfig) {
        const std::string& paramName = colConfig.parameterName;
        
        if (paramName == "steps") {
            sequencer.getCurrentPattern().setStepCount((int)value);
        } else if (paramName == "stepsPerBeat") {
            sequencer.setStepsPerBeat((int)value);
        }
    };
    
    // Row background
    callbacks.onRowStart = [](int row, bool isPlaybackRow, bool isEditRow) {
        ImU32 rowBgColor = GUIConstants::toU32(GUIConstants::Background::TableRowFilled);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowBgColor);
    };
    
    // Header click detection
    callbacks.onHeaderClicked = [this](int col) {
        patternParamsCallbacksState.headerClickedThisFrame = true;
    };
    
    patternParametersGrid.setCallbacks(callbacks);
    
    // Begin table (single row, no fixed columns)
    patternParametersGrid.beginTable(1, 0); // 1 row, 0 fixed columns
    
    // Draw headers (handled by CellGrid automatically)
    patternParametersGrid.drawHeaders(0, nullptr);
    
    // Draw single row (handled by CellGrid automatically)
    patternParametersGrid.drawRow(0, 0, false, false, nullptr);
    
    // Clear focus using unified helper if conditions are met
    ModuleGUI::handleFocusClearing(patternParamsFocusState, patternParamsCallbacksState);
    
    // End table
    patternParametersGrid.endTable();
    
    // Check for clicks outside the grid (after table ends)
    if (patternParamsFocusState.hasFocus() && ImGui::IsWindowHovered() && 
        ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
        ModuleGUI::clearCellFocus(patternParamsFocusState);
    }
    
    ImGui::Spacing();
    ImGui::Separator();
}

void TrackerSequencerGUI::drawPatternGrid(TrackerSequencer& sequencer) {
    // Track pattern index changes to refresh column configuration
    int currentPatternIndex = sequencer.getCurrentPatternIndex();
    bool patternChanged = (currentPatternIndex != lastPatternIndex);
    if (patternChanged) {
        lastPatternIndex = currentPatternIndex;
        // Clear cell cache when pattern changes to force refresh
        cellGrid.clearCellCache();
    }
    
    
    // No parent widget - cells are directly navigable like other widgets
    // ImGui handles navigation naturally when pressing UP on first row
    
    // Note: Compact styling is handled by CellGrid (CellPadding, ItemSpacing, ScrollbarSize)
    
    // Ensure column configuration is initialized (per-pattern)
    if (sequencer.getColumnConfiguration().empty()) {
        sequencer.initializeDefaultColumns();
    }
    
    // Force refresh column configuration when pattern changes
    // This ensures each pattern displays its own column configuration
    if (patternChanged) {
        // Column configuration will be updated below via setColumnConfiguration
    }
    
    // PERFORMANCE: Cache all expensive calls ONCE per frame
    // Cache active step info ONCE per frame (not per row!)
    bool isPlaying = sequencer.isPlaying();
    int currentPlayingStep = sequencer.getCurrentPlayingStep();
    int playbackStep = sequencer.getPlaybackStepIndex();
    
    // Calculate exact table height to fit all rows (no extra space)
    // Height = header row + (numRows * row height) + borders
    int numRows = sequencer.getCurrentPattern().getStepCount();
    float headerHeight = ImGui::GetFrameHeightWithSpacing(); // Header row height
    float rowHeight = ImGui::GetFrameHeightWithSpacing(); // Each data row height
    float borderHeight = 2.0f; // Top and bottom borders (1px each)
    float exactHeight = headerHeight + (numRows * rowHeight) + borderHeight;
    
    // Configure CellGrid using unified helper
    // Use SizingFixedFit for mixed column sizing (fixed + stretch columns)
    // This allows first two columns to be fixed and parameter columns to stretch proportionally
    ModuleGUI::CellGridConfig gridConfig;
    gridConfig.tableId = "TrackerGrid";
    gridConfig.tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY;
    gridConfig.enableScrolling = true;
    gridConfig.scrollHeight = exactHeight;
    gridConfig.scrollbarSize = SCROLLBAR_SIZE;
    configureCellGrid(cellGrid, gridConfig);
    
    // Convert column configuration to CellGrid format
    std::vector<CellGridColumnConfig> tableColumnConfig;
    for (const auto& col : sequencer.getColumnConfiguration()) {
        // Index and Length columns should not be draggable and are not removable
        bool isDraggable = (col.parameterName != "index" && col.parameterName != "length");
        bool isRemovable = (col.parameterName != "index" && col.parameterName != "length");
        CellGridColumnConfig tableCol(
            col.parameterName, col.displayName, isRemovable, col.columnIndex, isDraggable);
        tableColumnConfig.push_back(tableCol);
    }
    
    // Update column configuration using unified helper (only updates if changed)
    updateColumnConfigIfChanged(cellGrid, tableColumnConfig, lastColumnConfig);
    // Query external parameters from connected modules (GUI layer handles ParameterRouter dependency)
    std::vector<ParameterDescriptor> externalParams = queryExternalParameters(sequencer);
    cellGrid.setAvailableParameters(sequencer.getAvailableParameters(externalParams));
    
    // Store header buttons per column for custom header rendering
    std::map<int, std::vector<HeaderButton>> columnHeaderButtons;
    
    // Register header buttons
    // Column indexing system:
    // - Absolute indices: 0 = step number, 1 = index column, 2 = length column, 3+ = parameter columns
    // - All callbacks and CellGrid use absolute indices for consistency
    // - getColumnConfiguration() returns parameter columns only (0-based), so convert when accessing: paramColIdx = absoluteColIdx - 1
    cellGrid.clearHeaderButtons();
    for (size_t i = 0; i < sequencer.getColumnConfiguration().size(); i++) {
        const auto& colConfig = sequencer.getColumnConfiguration()[i];
        int absoluteColIdx = (int)i + 1;  // Convert parameter-relative to absolute (1+)
        bool isRemovable = (colConfig.parameterName != "index" && colConfig.parameterName != "length");
        
        // Randomize button for all columns
        HeaderButton randomizeBtn("R", "Randomize", [&sequencer, absoluteColIdx]() {
            sequencer.randomizeColumn(absoluteColIdx);
        });
        cellGrid.registerHeaderButton(absoluteColIdx, randomizeBtn);
        columnHeaderButtons[absoluteColIdx].push_back(randomizeBtn);
        
        // Legato button for length column
        if (colConfig.parameterName == "length") {
            HeaderButton legatoBtn("L", "Legato", [&sequencer]() {
                sequencer.applyLegato();
            });
            cellGrid.registerHeaderButton(absoluteColIdx, legatoBtn);
            columnHeaderButtons[absoluteColIdx].push_back(legatoBtn);
        }
    }
    
    // Setup callbacks for CellGrid (extracted into helper methods for clarity)
    CellGridCallbacks callbacks;
    setupHeaderCallbacks(callbacks, callbacksState.headerClickedThisFrame, sequencer, columnHeaderButtons);
    setupCellValueCallbacks(callbacks, sequencer);
    setupStateSyncCallbacks(callbacks, sequencer);
    setupRowCallbacks(callbacks, sequencer, currentPlayingStep);
    cellGrid.setCallbacks(callbacks);
    cellGrid.enableAutoScroll(true);
    
    // Begin table (CellGrid handles ImGui::BeginTable internally)
    // numRows already calculated above for height calculation
    cellGrid.beginTable(numRows, 1); // 1 fixed column (step number)
    cellGrid.setupFixedColumn(0, "##", STEP_NUMBER_COLUMN_WIDTH, false, 1.0f);
    
    // Draw headers using CellGrid (now supports fixed columns and custom header rendering)
    cellGrid.drawHeaders(1, [](int fixedColIndex) {
        // Draw fixed column header (step number column)
        if (fixedColIndex == 0) {
            ImGui::TableHeader("##");
        }
    });
    
    // Draw pattern rows using CellGrid
    pendingRowOutline.shouldDraw = false; // Reset row outline state
    callbacksState.resetFrame(); // Reset focus tracking
    
    // Note: Auto-scroll is handled by CellGrid (via getFocusedRow callback)
    
    for (int step = 0; step < sequencer.getCurrentPattern().getStepCount(); step++) {
        // Draw row using CellGrid (handles TableNextRow, row background, auto-scroll, and parameter columns)
        // Fixed column (step number) is drawn via callback
        cellGrid.drawRow(step, 1, step == playbackStep, step == cellFocusState.row,
                              [this, &sequencer, isPlaying, currentPlayingStep, playbackStep](int row, int fixedColIndex) {
            // Draw fixed column (step number)
            if (fixedColIndex == 0) {
                drawStepNumber(sequencer, row, row == playbackStep, isPlaying, currentPlayingStep);
            }
        });
        
        // After all cells in row are drawn, update row outline XMax if needed
        if (pendingRowOutline.shouldDraw && pendingRowOutline.step == step) {
            ImVec2 lastCellMin = ImGui::GetCursorScreenPos();
            float lastCellWidth = ImGui::GetColumnWidth();
            pendingRowOutline.rowXMax = lastCellMin.x + lastCellWidth + 1;
        }
    }
    
    // After drawing all rows, if cellFocusState.row is set but no cell was focused,
        // clear the sequencer's cell focus (focus moved to header row or elsewhere)
        // Note: Refocus is now handled automatically by CellWidget when exiting edit mode
        // This prevents focus loss when ImGui temporarily loses focus after exiting edit mode
        // NOTE: This check happens AFTER all cells are drawn, so it won't interfere with
        // the ViewManager's empty space click handling which clears focus BEFORE drawing
    // Clear focus using unified helper if conditions are met
    // Additional condition: don't clear focus while dragging (sequencer.draggingStep < 0)
    ModuleGUI::handleFocusClearing(cellFocusState, callbacksState, 
                                    [&sequencer]() { return sequencer.draggingStep < 0; });
        
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
                    drawList->AddRect(rowMin, rowMax, pendingRowOutline.color, 0.0f, 0, OUTLINE_THICKNESS);
                }
            }
        }
        
    // End table (CellGrid handles ImGui::EndTable internally)
    cellGrid.endTable();
    
    // NOTE: Input processing is now handled by CellWidget internally
    // No need for processCellInput() - CellWidget processes input during draw()
    
    // NOTE: Auto-scrolling is handled by CellGrid when drawing the focused row
    // This ensures smooth scrolling that follows keyboard navigation
        
    // NOTE: Empty space click handling is now done in ViewManager::drawTrackerPanel
    // before calling trackerGUI->draw(). This prevents the focus loop issue where
    // ImGui auto-focuses a cell and then we try to clear it, causing a loop.
    // We keep this check as a fallback for clicks outside the window.
    if (cellFocusState.row >= 0 && ImGui::IsMouseClicked(0)) {
        // Only clear if click is outside the window entirely
        // (Empty space clicks within the window are handled by ViewManager)
        if (!ImGui::IsWindowHovered()) {
            clearCellFocus();
        }
    }
    
    // Note: CellGrid's endTable() handles popping all style vars (CellPadding, ItemSpacing, ScrollbarSize)
    
    // No parent widget state to update - cells are directly navigable
}

void TrackerSequencerGUI::drawStepNumber(TrackerSequencer& sequencer, int step, bool isPlaybackStep,
                                         bool isPlaying, int currentPlayingStep) {
    // Note: Don't call TableNextColumn() here - CellGrid::drawRow already sets the column index
    
    // Set step number cell background to match table header color
    // static ImU32 stepNumberBgColor = GUIConstants::toU32(GUIConstants::Background::TableHeader);
    // ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, stepNumberBgColor);
    
    // Get cell rect for red outline (before drawing button)
    ImVec2 cellMin = ImGui::GetCursorScreenPos();
    float cellHeight = ImGui::GetFrameHeight();
    float cellWidth = ImGui::GetColumnWidth();
    ImVec2 cellMax = ImVec2(cellMin.x + cellWidth, cellMin.y + cellHeight);
    
    char stepBuf[BUFFER_SIZE];
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
        ImGui::PushStyleColor(ImGuiCol_Button, GUIConstants::Active::StepButton);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GUIConstants::Active::StepButtonHover);
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
            callbacksState.anyCellFocusedThisFrame = true;
            bool cellChanged = (cellFocusState.row != step || cellFocusState.column != 0);
            
            // When in edit mode, prevent focus from changing to a different cell
            if (cellFocusState.isEditing && cellChanged) {
                return; // Exit early - keep focus locked to editing cell
            }
            
            // Sync state to GUI
            int previousStep = cellFocusState.row;
            setEditCell(step, 0);
            
            // When paused, sync playback position and trigger step (walk through)
            bool stepChanged = (step != sequencer.getPlaybackStep());
            bool fromHeaderRow = (previousStep == -1);
            if (fromHeaderRow || stepChanged) {
                syncPlaybackToEditIfPaused(sequencer, step, stepChanged, fromHeaderRow, lastTriggeredStepWhenPaused);
            }
            
            // Note: Edit mode is managed by CellWidget - it will exit edit mode when focus changes
            // No need to manually clear edit mode here
        }
    }
    
    // Draw outline for selected cells only (not hover)
    // When step number cell is selected, draw outline around entire row
    // Don't draw outline if we're on header row (cellFocusState.row == -1)
    bool isSelected = (cellFocusState.row == step && cellFocusState.column == 0 && cellFocusState.row >= 0);
    bool isFocused = ImGui::IsItemFocused();
    bool shouldShowOutline = isSelected || (isFocused && !cellFocusState.isEditing && cellFocusState.row >= 0);
    
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
            static ImU32 rowOrangeOutlineColor = GUIConstants::toU32(GUIConstants::Outline::Orange);
            static ImU32 rowRedOutlineColor = GUIConstants::toU32(GUIConstants::Outline::Red);
            
            // Orange outline when in edit mode, red outline when just selected (use cached colors)
            pendingRowOutline.color = (isSelected && cellFocusState.isEditing)
                ? rowOrangeOutlineColor  // Orange outline in edit mode
                : rowRedOutlineColor; // Red outline when not editing
        } else if (isFocused) {
            // Just draw outline around the step number cell itself (when focused but not selected)
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (drawList) {
                // Static cached color for step number outline
                static ImU32 stepNumberOutlineColor = GUIConstants::toU32(GUIConstants::Outline::Red);
                ImVec2 outlineMin = ImVec2(cellMin.x - 1, cellMin.y - 1);
                ImVec2 outlineMax = ImVec2(cellMax.x + 1, cellMax.y + 1);
                drawList->AddRect(outlineMin, outlineMax, stepNumberOutlineColor, 0.0f, 0, OUTLINE_THICKNESS);
            }
        }
    }
}

// Handle keyboard input - simplified to only process global shortcuts
bool TrackerSequencerGUI::handleKeyPress(int key, bool ctrlPressed, bool shiftPressed) {
    auto sequencer = getTrackerSequencer();
    if (!sequencer) return false;
    
    // PHASE 1: SINGLE INPUT PATH - CellWidget is sole input processor for cells
    // If any cell has focus (pattern params or pattern grid), let CellWidget handle ALL input
    if (patternParamsFocusState.hasFocus() || cellFocusState.hasFocus()) {
        return false; // CellWidget will handle in processInputInDraw()
    }
    
    // Only handle global shortcuts when no cell is focused
    switch (key) {
        case ' ': // Spacebar: Play/pause (global shortcut)
            // Handle play/pause globally
            if (sequencer->isPlaying()) {
                sequencer->stop();
            } else {
                sequencer->play();
            }
            return true;
            
        // All other keys are either handled by CellWidget (when cells are focused)
        // or should pass through to ImGui for standard navigation
        default:
            break;
        
    }
    return false;
}

// NOTE: processCellInput() has been removed - input processing is now handled by CellWidget internally
// CellWidget processes input during draw() and uses callbacks (onEditModeChanged, onValueApplied) to notify GUI layer

//--------------------------------------------------------------
// Helper methods
//--------------------------------------------------------------
std::vector<ParameterDescriptor> TrackerSequencerGUI::queryExternalParameters(TrackerSequencer& sequencer) const {
    std::vector<ParameterDescriptor> externalParams;
    ParameterRouter* router = getParameterRouter();
    ModuleRegistry* registry = getRegistry();
    
    if (!router || !registry) {
        return externalParams; // Return empty if dependencies not available
    }
    
    // Get internal parameter names to filter them out
    auto internalParams = sequencer.getInternalParameters();
    std::set<std::string> internalParamNames;
    for (const auto& param : internalParams) {
        internalParamNames.insert(param.name);
    }
    
    // Query all INSTRUMENT modules for their parameters
    auto instruments = registry->getModulesByType(ModuleType::INSTRUMENT);
    std::map<std::string, ParameterDescriptor> uniqueParams;
    
    for (const auto& instrument : instruments) {
        auto instrumentParams = instrument->getParameters();
        for (const auto& param : instrumentParams) {
            // Skip internal parameters
            if (internalParamNames.find(param.name) != internalParamNames.end()) {
                continue;
            }
            // Add parameter if not already seen (first occurrence wins)
            if (uniqueParams.find(param.name) == uniqueParams.end()) {
                uniqueParams[param.name] = param;
            }
        }
    }
    
    // Convert map to vector
    for (const auto& pair : uniqueParams) {
        externalParams.push_back(pair.second);
    }
    
    return externalParams;
}

//--------------------------------------------------------------
// Callback setup helpers (extracted from drawPatternGrid)
//--------------------------------------------------------------
void TrackerSequencerGUI::setupHeaderCallbacks(CellGridCallbacks& callbacks, bool& headerClickedThisFrame,
                                                TrackerSequencer& sequencer, std::map<int, std::vector<HeaderButton>>& columnHeaderButtons) {
    // Setup header click callback to detect when headers are clicked
    // (CellGrid calls this for default headers; we manually check in drawCustomHeader for custom headers)
    callbacks.onHeaderClicked = [&headerClickedThisFrame](int col) {
        headerClickedThisFrame = true;
    };
    
    // Custom header rendering with context menu and swap popup
    callbacks.drawCustomHeader = [this, &sequencer, &columnHeaderButtons, &headerClickedThisFrame]
                                  (int col, const CellGridColumnConfig& colConfig, ImVec2 cellStartPos, float columnWidth, float cellMinY) -> bool {
        // Only handle swap popup for removable columns
        if (colConfig.parameterName == "index" || colConfig.parameterName == "length") {
            return false; // Use default header rendering for fixed columns
        }
        
        // Draw column name (left-aligned)
        ImGui::TableHeader(colConfig.displayName.c_str());
        
        // Check if header was clicked (for focus clearing)
        if (ImGui::IsItemClicked(0)) {
            headerClickedThisFrame = true;
        }
        
        // Right-click context menu for column actions (must be called after TableHeader)
        std::string contextMenuId = "##ColumnContextMenu_" + std::to_string(col);
        if (ImGui::BeginPopupContextItem(contextMenuId.c_str())) {
            int columnConfigIndex = colConfig.columnIndex;
            
            // Remove column option (only for removable columns)
            if (colConfig.isRemovable) {
                if (ImGui::MenuItem("Remove Column")) {
                    sequencer.removeColumn(columnConfigIndex);
                }
            }
            
            // Add column option (always available)
            if (ImGui::BeginMenu("Add Column")) {
                // Get available parameters (external only - internal params excluded)
                auto internalParams = sequencer.getInternalParameters();
                std::set<std::string> internalParamNames;
                for (const auto& param : internalParams) {
                    internalParamNames.insert(param.name);
                }
                
                // Build set of used parameter names
                std::set<std::string> usedParamNames;
                for (const auto& col : sequencer.getColumnConfiguration()) {
                    usedParamNames.insert(col.parameterName);
                }
                
                // Query external parameters
                std::vector<ParameterDescriptor> externalParams = queryExternalParameters(sequencer);
                
                // Get all available parameters
                auto allParams = sequencer.getAvailableParameters(externalParams);
                
                // Show available parameters that aren't already used
                bool hasAvailableParams = false;
                for (const auto& param : allParams) {
                    // Skip internal parameters
                    if (internalParamNames.find(param.name) != internalParamNames.end()) {
                        continue;
                    }
                    // Skip already used parameters
                    if (usedParamNames.find(param.name) != usedParamNames.end()) {
                        continue;
                    }
                    
                    hasAvailableParams = true;
                    if (ImGui::MenuItem(param.displayName.c_str())) {
                        sequencer.addColumn(param.name, param.displayName);
                    }
                }
                
                if (!hasAvailableParams) {
                    ImGui::TextDisabled("No available parameters");
                }
                
                ImGui::EndMenu();
            }
            
            ImGui::EndPopup();
        }
        
        // Handle swap popup (draw every frame when open)
        std::string popupId = "SwapPopup_" + std::to_string(col);
        
        // Check if popup should be opened (click or Enter on header)
        bool headerClicked = ImGui::IsItemClicked(0);
        bool enterPressed = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
        if (headerClicked || enterPressed) {
            ImGui::OpenPopup(popupId.c_str());
        }
        
        // Query external parameters from connected modules (GUI layer handles ParameterRouter dependency)
        std::vector<ParameterDescriptor> externalParams = queryExternalParameters(sequencer);
        
        // Get available parameters (external only - internal params like "note" and "chance" are excluded from popup)
        // Internal parameters are sequencer-specific and can't be swapped in external parameter columns
        auto allParams = sequencer.getAvailableParameters(externalParams);
        
        // Get internal parameter names to filter them out from popup
        auto internalParams = sequencer.getInternalParameters();
        std::set<std::string> internalParamNames;
        for (const auto& param : internalParams) {
            internalParamNames.insert(param.name);
        }
        
        // Build set of used parameter names (exclude current column)
        std::set<std::string> usedParamNames;
        for (const auto& col : sequencer.getColumnConfiguration()) {
            usedParamNames.insert(col.parameterName);
        }
        
        // Convert to HeaderPopup items, filtering out:
        // 1. Internal parameters (note, chance) - these are sequencer-specific
        // 2. Already used parameters
        // Store parameter descriptors in a map for lookup by name
        std::map<std::string, ParameterDescriptor> paramMap;
        std::vector<HeaderPopup::PopupItem> items;
        for (const auto& param : allParams) {
            // Skip internal parameters - they shouldn't appear in external parameter column popup
            if (internalParamNames.find(param.name) != internalParamNames.end()) {
                continue;
            }
            // Skip already used parameters
            if (usedParamNames.find(param.name) == usedParamNames.end()) {
                items.push_back(HeaderPopup::PopupItem(param.name, param.displayName));
                paramMap[param.name] = param; // Store for lookup
            }
        }
        
        // Draw popup (will only show if open)
        // Use colConfig.columnIndex (index into sequencer's columnConfig) not absolute col index
        int columnConfigIndex = colConfig.columnIndex;
        HeaderPopup::draw(popupId, items, columnWidth, cellStartPos,
                         [&sequencer, columnConfigIndex, paramMap](const std::string& paramName) {
                             // Look up display name from the parameter map
                             std::string displayName;
                             auto it = paramMap.find(paramName);
                             if (it != paramMap.end()) {
                                 displayName = it->second.displayName;
                             }
                             sequencer.swapColumnParameter(columnConfigIndex, paramName, displayName);
                         });
        
        // Draw header buttons manually (since we're using custom header renderer)
        auto it = columnHeaderButtons.find(col);
        if (it != columnHeaderButtons.end() && !it->second.empty()) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
            float totalButtonWidth = 0.0f;
            for (const auto& btn : it->second) {
                std::string btnLabel = btn.getDynamicLabel ? btn.getDynamicLabel() : btn.label;
                float btnWidth = ImGui::CalcTextSize(btnLabel.c_str()).x + 
                                ImGui::GetStyle().FramePadding.x * 2.0f;
                totalButtonWidth += btnWidth;
                if (&btn != &it->second.back()) {
                    totalButtonWidth += BUTTON_SPACING; // Spacing between buttons
                }
            }
            
            float padding = ImGui::GetStyle().CellPadding.x;
            float cellMaxX = cellStartPos.x + columnWidth;
            float buttonStartX = cellMaxX - totalButtonWidth - padding;
            
            // Draw buttons from right to left
            float currentX = buttonStartX;
            for (size_t btnIdx = 0; btnIdx < it->second.size(); btnIdx++) {
                const auto& btn = it->second[btnIdx];
                std::string btnLabel = btn.getDynamicLabel ? btn.getDynamicLabel() : btn.label;
                std::string btnTooltip = btn.getDynamicTooltip ? btn.getDynamicTooltip() : btn.tooltip;
                
                float btnWidth = ImGui::CalcTextSize(btnLabel.c_str()).x + 
                                ImGui::GetStyle().FramePadding.x * 2.0f;
                
                ImGui::SetCursorScreenPos(ImVec2(currentX, cellMinY));
                
                if (ImGui::SmallButton(btnLabel.c_str())) {
                    if (btn.onClick) {
                        btn.onClick();
                    }
                }
                
                if (ImGui::IsItemHovered() && !btnTooltip.empty()) {
                    ImGui::SetTooltip("%s", btnTooltip.c_str());
                }
                
                currentX += btnWidth;
                if (btnIdx < it->second.size() - 1) {
                    currentX += BUTTON_SPACING; // Spacing between buttons
                }
            }
            
            ImGui::PopStyleVar();
        }
        
        return true; // Header was drawn by custom renderer
    };
    
    // Custom column sizing: index and length columns use fixed width, others stretch
    callbacks.setupParameterColumn = [](int colIndex, const CellGridColumnConfig& colConfig, int absoluteColIndex) -> bool {
        ImGuiTableColumnFlags flags = 0;
        float widthOrWeight = 0.0f;
        
        // Index and length columns: fixed width with reasonable defaults
        if (colConfig.parameterName == "index" || colConfig.parameterName == "length") {
            flags = ImGuiTableColumnFlags_WidthFixed;
            widthOrWeight = INDEX_LENGTH_COLUMN_WIDTH;
        } else {
            // Other parameter columns: stretch to fill available space
            flags = ImGuiTableColumnFlags_WidthStretch;
            widthOrWeight = 1.0f;
        }
        
        // Disable reordering for non-draggable columns
        if (!colConfig.isDraggable) {
            flags |= ImGuiTableColumnFlags_NoReorder;
        }
        
        ImGui::TableSetupColumn(colConfig.displayName.c_str(), flags, widthOrWeight);
        return true; // Column was set up
    };
}

void TrackerSequencerGUI::setupCellValueCallbacks(CellGridCallbacks& callbacks, TrackerSequencer& sequencer) {
    callbacks.createCellWidget = [this, &sequencer](int row, int col, const CellGridColumnConfig& colConfig) -> CellWidget {
        return createParameterCellForColumn(sequencer, row, col);
    };
    
    callbacks.getCellValue = [&sequencer](int row, int col, const CellGridColumnConfig& colConfig) -> float {
        const std::string& paramName = colConfig.parameterName;
        const auto& step = sequencer.getCurrentPattern()[row];
        
        // SPECIAL CASE: "index" parameter
        if (paramName == "index") {
            int idx = step.index;
            return (idx < 0) ? std::numeric_limits<float>::quiet_NaN() : (float)(idx + 1);
        }
        
        // SPECIAL CASE: "length" parameter
        if (paramName == "length") {
            if (step.index < 0) {
                return std::numeric_limits<float>::quiet_NaN();
            }
            return (float)step.length;
        }
        
        // Dynamic parameter: returns NaN if not set (displays as "--")
        if (!step.hasParameter(paramName)) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return step.getParameterValue(paramName, 0.0f);
    };
    
    callbacks.setCellValue = [&sequencer](int row, int col, float value, const CellGridColumnConfig& colConfig) {
        const std::string& paramName = colConfig.parameterName;
        Step step = sequencer.getStep(row);
        
        // SPECIAL CASE: "index" parameter
        if (paramName == "index") {
            int indexValue = (int)std::round(value);
            step.index = (indexValue == 0) ? -1 : (indexValue - 1);
        }
        // SPECIAL CASE: "length" parameter - range is 1 to pattern stepCount
        else if (paramName == "length") {
            int maxLength = sequencer.getStepCount();
            step.length = std::max(MIN_LENGTH_VALUE, std::min(maxLength, (int)std::round(value)));
        }
        // Dynamic parameter: set value directly
        else {
            step.setParameterValue(paramName, value);
        }
        
        // Update the pattern with modified step
        sequencer.setStep(row, step);
    };
}

void TrackerSequencerGUI::setupStateSyncCallbacks(CellGridCallbacks& callbacks, TrackerSequencer& sequencer) {
    // PHASE 2: TRUST IMGUI FOCUS - Remove parallel focus tracking
    // Let CellWidget manage focus naturally, only handle TrackerSequencer-specific playback syncing
    
    // Simple focus callback for playback syncing only (no parallel focus state tracking)
    callbacks.onCellFocusChanged = [this, &sequencer](int row, int col) {
        // TrackerSequencer-specific: Handle playback syncing only if step actually changed
        int previousStep = cellFocusState.row;
        
        // Update minimal state needed for playback syncing
        cellFocusState.row = row;
        cellFocusState.column = col;
        
        bool stepChanged = (previousStep != row);
        bool fromHeaderRow = (previousStep == -1);
        if (fromHeaderRow || stepChanged) {
            syncPlaybackToEditIfPaused(sequencer, row, stepChanged, fromHeaderRow, lastTriggeredStepWhenPaused);
        }
    };
    
    // Simple click callback for playback syncing only
    callbacks.onCellClicked = [this, &sequencer](int row, int col) {
        // TrackerSequencer-specific: When paused, sync playback position and trigger step (walk through)
        int previousStep = cellFocusState.row;
        
        // Update minimal state needed for playback syncing
        cellFocusState.row = row;
        cellFocusState.column = col;
        
        bool stepChanged = (previousStep != row);
        bool fromHeaderRow = (previousStep == -1);
        if (fromHeaderRow || stepChanged) {
            syncPlaybackToEditIfPaused(sequencer, row, stepChanged, fromHeaderRow, lastTriggeredStepWhenPaused);
        }
    };
    
    // Note: Edit buffer and edit mode are managed by CellWidget internally
    // Note: No more parallel focus tracking - CellWidget handles focus naturally
}

void TrackerSequencerGUI::setupRowCallbacks(CellGridCallbacks& callbacks, TrackerSequencer& sequencer, int currentPlayingStep) {
    callbacks.onRowStart = [&sequencer, currentPlayingStep](int row, bool isPlaybackRow, bool isEditRow) {
        // Set row background colors
        static ImU32 activeStepColor = GUIConstants::toU32(GUIConstants::Active::StepBright);
        static ImU32 inactiveStepColor = GUIConstants::toU32(GUIConstants::Active::StepDim);
        static ImU32 rowBgColor = GUIConstants::toU32(GUIConstants::Background::TableRowFilled);
        static ImU32 emptyRowBgColor = GUIConstants::toU32(GUIConstants::Background::TableRowEmpty);
        
        bool isRowEmpty = sequencer.getCurrentPattern()[row].isEmpty();
        bool isCurrentPlayingStep = (currentPlayingStep == row);
        bool isStepActive = isCurrentPlayingStep;
        
        if (isPlaybackRow) {
            if (isStepActive) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, activeStepColor);
            } else {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, inactiveStepColor);
            }
        } else if (!isRowEmpty) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowBgColor);
        } else {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, emptyRowBgColor);
        }
    };
}

//--------------------------------------------------------------
// CellWidget adapter methods (moved from TrackerSequencer)
//--------------------------------------------------------------
CellWidget TrackerSequencerGUI::createParameterCellForColumn(TrackerSequencer& sequencer, int step, int column) {
    // column is absolute column index (0=step number, 1+=parameter columns)
    // For parameter columns, convert to parameter-relative index: paramColIdx = column - 1
    if (step < 0 || step >= sequencer.getStepCount() || column <= 0) {
        return CellWidget(); // Return empty cell for invalid step or step number column (column=0)
    }
    
    const auto& columnConfig = sequencer.getCurrentPattern().getColumnConfiguration();
    int paramColIdx = column - 1;  // Convert absolute column index to parameter-relative index
    if (paramColIdx < 0 || paramColIdx >= (int)columnConfig.size()) {
        return CellWidget();
    }
    
    const auto& col = columnConfig[paramColIdx];
    CellWidget cell;
    
    // Configure basic properties
    cell.parameterName = col.parameterName;
    cell.isRemovable = col.isRemovable; // Columns (index, length) cannot be removed
    if (!col.isRemovable) {
        // Required columns (index, length) are always integers
        cell.isInteger = true;
        cell.stepIncrement = 1.0f;
    }
    
    // Set value range based on column type
    if (!col.isRemovable && col.parameterName == "index") {
        // Index column: 0 = rest, 1+ = media index (1-based display)
        int maxIndex = sequencer.getIndexRange();
        cell.setValueRange(0.0f, (float)maxIndex, 0.0f);
        cell.getMaxIndex = [&sequencer]() { return sequencer.getIndexRange(); };
    } else if (!col.isRemovable && col.parameterName == "length") {
        // Length column: 1 to pattern stepCount range (dynamic)
        int maxLength = sequencer.getStepCount();
        cell.setValueRange((float)MIN_LENGTH_VALUE, (float)maxLength, 1.0f);
    } else {
        // Dynamic parameter column - use parameter ranges
        auto range = TrackerSequencer::getParameterRange(col.parameterName);
        float defaultValue = TrackerSequencer::getParameterDefault(col.parameterName);
        cell.setValueRange(range.first, range.second, defaultValue);
        
        // Determine if parameter is integer or float
        ParameterType paramType = TrackerSequencer::getParameterType(col.parameterName);
        cell.isInteger = (paramType == ParameterType::INT);
        
        // Calculate optimal step increment based on range and type
        cell.calculateStepIncrement();
    }
    
    // Configure callbacks
    configureParameterCellCallbacks(sequencer, cell, step, column);
    
    return cell;
}

void TrackerSequencerGUI::configureParameterCellCallbacks(TrackerSequencer& sequencer, CellWidget& cell, int step, int column) {
    // column is absolute column index (0=step number, 1+=parameter columns)
    // For parameter columns, convert to parameter-relative index: paramColIdx = column - 1
    if (step < 0 || step >= sequencer.getStepCount() || column <= 0) {
        return;  // Invalid step or step number column (column=0)
    }
    
    const auto& columnConfig = sequencer.getCurrentPattern().getColumnConfiguration();
    int paramColIdx = column - 1;  // Convert absolute column index to parameter-relative index
    if (paramColIdx < 0 || paramColIdx >= (int)columnConfig.size()) {
        return;
    }
    
    const auto& col = columnConfig[paramColIdx];
    std::string paramName = col.parameterName; // Capture by value for lambda
    bool isRequiredCol = !col.isRemovable; // Capture by value
    std::string requiredTypeCol = !col.isRemovable ? col.parameterName : ""; // Capture by value
    
    // getCurrentValue callback - returns current value from Step
    // Returns NaN to indicate empty/not set (will display as "--")
    // Unified system: all empty values (Index, Length, dynamic parameters) use NaN
    cell.getCurrentValue = [&sequencer, step, paramName, isRequiredCol, requiredTypeCol]() -> float {
        if (step < 0 || step >= sequencer.getStepCount()) {
            // Return NaN for invalid step (will display as "--")
            return std::numeric_limits<float>::quiet_NaN();
        }
        
        const Step& stepData = sequencer.getCurrentPattern()[step];
        
        if (isRequiredCol && requiredTypeCol == "index") {
            // Index: return NaN when empty (index <= 0), otherwise return 1-based display value
            int idx = stepData.index;
            return (idx < 0) ? std::numeric_limits<float>::quiet_NaN() : (float)(idx + 1);
        } else if (isRequiredCol && requiredTypeCol == "length") {
            // Length: return NaN when index < 0 (rest), otherwise return length
            if (stepData.index < 0) {
                return std::numeric_limits<float>::quiet_NaN(); // Use NaN instead of -1.0f
            }
            return (float)stepData.length;
        } else {
            // Dynamic parameter: return NaN if parameter doesn't exist (will display as "--")
            // This allows parameters with negative ranges (like speed -10 to 10) to distinguish
            // between "not set" (NaN/--) and explicit values like 1.0 or -1.0
            if (!stepData.hasParameter(paramName)) {
                return std::numeric_limits<float>::quiet_NaN();
            }
            return stepData.getParameterValue(paramName, 0.0f);
        }
    };
    
    // onValueApplied callback - applies value to Step
    cell.onValueApplied = [&sequencer, step, column, paramName, isRequiredCol, requiredTypeCol](const std::string&, float value) {
        if (step < 0 || step >= sequencer.getStepCount()) return;
        
        // TODO (Task 6): Implement proper pending edit queuing for playback editing
        // For now, apply edits immediately. The pending edit system will be refactored
        // to properly handle edits during playback.
        
        // Apply immediately
        Step stepData = sequencer.getStep(step);
        if (isRequiredCol && requiredTypeCol == "index") {
            // Index: value is 1-based display, convert to 0-based storage
            int indexValue = (int)std::round(value);
            stepData.index = (indexValue == 0) ? -1 : (indexValue - 1);
        } else if (isRequiredCol && requiredTypeCol == "length") {
            // Length: clamp to 1 to pattern stepCount (dynamic)
            int maxLength = sequencer.getStepCount();
            stepData.length = std::max(1, std::min(maxLength, (int)std::round(value)));
        } else {
            // Dynamic parameter
            stepData.setParameterValue(paramName, value);
        }
        sequencer.setStep(step, stepData);
    };
    
    // onValueRemoved callback - removes parameter from Step
    cell.onValueRemoved = [&sequencer, step, column, paramName, isRequiredCol, requiredTypeCol](const std::string&) {
        if (step < 0 || step >= sequencer.getStepCount()) return;
        
        // TODO (Task 6): Implement proper pending edit queuing for playback editing
        // For now, apply removals immediately. The pending edit system will be refactored.
        
        // Remove immediately (only for removable parameters)
        if (isRequiredCol) {
            // Required columns (index, length) cannot be removed - reset to default
            Step stepData = sequencer.getStep(step);
            if (requiredTypeCol == "index") {
                stepData.index = -1; // Rest
            } else if (requiredTypeCol == "length") {
                stepData.length = MIN_LENGTH_VALUE; // Default length
            }
            sequencer.setStep(step, stepData);
        } else {
            // Removable parameter - remove it
            Step stepData = sequencer.getStep(step);
            stepData.removeParameter(paramName);
            sequencer.setStep(step, stepData);
        }
    };
    
    // onEditModeChanged callback removed - using CellGrid callback instead
    // This prevents duplicate callbacks that could cause conflicts
    // The callback in setupStateSyncCallbacks handles edit mode changes
    
    // formatValue callback - tracker-specific formatting for all columns
    if (isRequiredCol && requiredTypeCol == "index") {
        // Index column: 1-based display (01-99), NaN = rest
        cell.formatValue = [](float value) -> std::string {
            if (std::isnan(value)) {
                return "--"; // Show "--" for NaN (empty/rest)
            }
            int indexVal = (int)std::round(value);
            if (indexVal <= 0) {
                return "--"; // Also handle edge case
            }
            char buf[BUFFER_SIZE];
            snprintf(buf, sizeof(buf), "%02d", indexVal);
            return std::string(buf);
        };
    } else if (isRequiredCol && requiredTypeCol == "length") {
        // Length column: 1 to pattern stepCount range (dynamic), formatted as "02", NaN = not set
        int maxLength = sequencer.getStepCount();
        cell.formatValue = [maxLength](float value) -> std::string {
            if (std::isnan(value)) {
                return "--"; // Show "--" for NaN (empty/not set)
            }
            int lengthVal = (int)std::round(value);
            lengthVal = std::max(MIN_LENGTH_VALUE, std::min(maxLength, lengthVal)); // Clamp to valid range
            char buf[BUFFER_SIZE];
            snprintf(buf, sizeof(buf), "%02d", lengthVal); // Zero-padded to 2 digits
            return std::string(buf);
        };
    } else {
        // Dynamic parameter: use TrackerSequencer's formatting
        cell.formatValue = [paramName](float value) -> std::string {
            return TrackerSequencer::formatParameterValue(paramName, value);
        };
    }
    
    // parseValue callback - tracker-specific parsing for index/length
    if (isRequiredCol && requiredTypeCol == "index") {
        // Index: parse as integer, handle "--" as NaN
        cell.parseValue = [](const std::string& str) -> float {
            if (str == "--" || str.empty()) {
                return std::numeric_limits<float>::quiet_NaN();
            }
            try {
                int val = std::stoi(str);
                return (float)val;
            } catch (...) {
                return std::numeric_limits<float>::quiet_NaN();
            }
        };
    } else if (isRequiredCol && requiredTypeCol == "length") {
        // Length: parse as integer (1 to pattern stepCount, dynamic), handle "--" as NaN
        int maxLength = sequencer.getStepCount();
        cell.parseValue = [maxLength](const std::string& str) -> float {
            if (str == "--" || str.empty()) {
                return std::numeric_limits<float>::quiet_NaN();
            }
            try {
                int val = std::stoi(str);
                val = std::max(MIN_LENGTH_VALUE, std::min(maxLength, val)); // Clamp to valid range
                return (float)val;
            } catch (...) {
                return std::numeric_limits<float>::quiet_NaN();
            }
        };
    }
    // Dynamic parameters use ParameterCell's default parsing (expression evaluation)
}

//--------------------------------------------------------------
// GUI Factory Registration
//--------------------------------------------------------------
namespace {
    struct TrackerSequencerGUIRegistrar {
        TrackerSequencerGUIRegistrar() {
            GUIManager::registerGUIType("TrackerSequencer", 
                []() -> std::unique_ptr<ModuleGUI> {
                    return std::make_unique<TrackerSequencerGUI>();
                });
        }
    };
    static TrackerSequencerGUIRegistrar g_trackerSequencerGUIRegistrar;
}
