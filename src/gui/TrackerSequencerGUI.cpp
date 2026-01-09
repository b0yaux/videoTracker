#include "TrackerSequencerGUI.h"
// Note: TrackerSequencer.h is already included by TrackerSequencerGUI.h
#include "ParameterCell.h"  // For ParameterCell in createParameterCellForColumn
#include "core/ModuleRegistry.h"  // Needed for registry->getModule() calls
#include "core/ConnectionManager.h"  // Needed for ConnectionManager::ConnectionType and getConnectionsFrom()
#include "gui/HeaderPopup.h"
#include "gui/GUIManager.h"
#include <imgui.h>
#include "ofLog.h"
#include "gui/GUIConstants.h"
#include <cmath>  // For std::round
#include <limits>  // For std::numeric_limits
#include <algorithm>  // For std::max, std::min
#include <set>
#include <map>
#include <chrono>
#include <fstream>

// Helper to sync playback position to edit position when paused
// Uses public methods since static functions don't have friend class access
static void syncPlaybackToEditIfPaused(TrackerSequencer& sequencer, int newStep, bool stepChanged, bool forceTrigger, int& lastTriggeredStep) {
    // Don't sync when playing - playback drives the position
    if (sequencer.isPlaying()) {
        lastTriggeredStep = -1;
        return;
    }
    
    int currentPlaybackStep = sequencer.getPlaybackStep();
    
    // Determine if we should trigger this step:
    // 1. Force trigger (e.g., navigating from header row), OR
    // 2. Moving to a different step than current playback position, OR
    // 3. Clicking same step again (retrigger)
    bool movingToNewStep = (newStep != currentPlaybackStep);
    bool isRetrigger = (!movingToNewStep && newStep == lastTriggeredStep);
    bool shouldTrigger = forceTrigger || movingToNewStep || isRetrigger;
    
    if (shouldTrigger) {
        const Step& stepData = sequencer.getStep(newStep);
        bool isEmpty = stepData.isEmpty();
        
        // Always update playback position
        sequencer.setCurrentStep(newStep);
        
        // Only trigger sound for non-empty steps
        // Empty steps (index < 0) just move position without triggering
        if (!isEmpty) {
            sequencer.triggerStep(newStep);
        }
        
        // Track this step as triggered to handle retrigger detection
        lastTriggeredStep = newStep;
    }
}

TrackerSequencerGUI::TrackerSequencerGUI() 
    : lastPatternIndex(-1), lastTriggeredStepWhenPaused(-1),
      lastTriggeredStepThisFrame(-1), lastTriggeredStepFrame(-1),
      cachedTableWindowFocused(false), cachedTableWindowFocusedFrame(-1) {
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
        
    // Pattern chain header (no toggle - always show active pattern)
    ImGui::Text("Pattern Chain");
    
    ImGui::Spacing();
    
    // Get chain data
    const auto& chain = sequencer.getPatternChain();
    int currentChainIndex = sequencer.getCurrentChainIndex();
    std::string currentPatternName = sequencer.getCurrentPatternName();  // Use pattern name instead of index
    bool isPlaying = sequencer.isPlaying();
    int numPatterns = sequencer.getNumPatterns();
    bool useChain = sequencer.getUsePatternChain();
    
    // CRITICAL: Always show at least the active pattern, even if chain is empty
    // Build column configuration (one column per chain entry + buttons column at end)
    std::vector<CellGridColumnConfig> chainColumnConfig;
    
    if (chain.empty()) {
        // Chain is empty - show active pattern as single column
        std::string patternName = currentPatternName.empty() ? "Pattern" : currentPatternName;
        chainColumnConfig.push_back(CellGridColumnConfig(
            "pattern_0",                      // parameterName: unique ID for this chain position
            patternName,                      // displayName: shows pattern name
            false,                            // isRemovable: columns are not removable (use - button instead)
            0,                                // columnIndex
            true                              // isDraggable: allow reordering
        ));
    } else {
        // Chain has entries - show all chain entries
        for (size_t i = 0; i < chain.size(); i++) {
            std::string patternName = chain[i];  // PatternChain now uses pattern names directly
            if (patternName.empty()) {
                patternName = "Pattern " + std::to_string(i);  // Fallback if name not found
            }
            chainColumnConfig.push_back(CellGridColumnConfig(
                "pattern_" + std::to_string(i),  // parameterName: unique ID for this chain position
                patternName,                      // displayName: shows pattern name
                false,                            // isRemovable: columns are not removable (use - button instead)
                (int)i,                           // columnIndex
                true                              // isDraggable: allow reordering
            ));
        }
    }
    // Add buttons column at the end (not draggable, fixed width)
    chainColumnConfig.push_back(CellGridColumnConfig(
        "buttons",                           // parameterName: special identifier for buttons column
        "##buttons",                         // displayName: empty header
        false,                                // isRemovable: buttons column is not removable
        (int)chain.size(),                   // columnIndex
        false                                // isDraggable: buttons column should not be reorderable
    ));
    
    // Update column configuration if changed
    if (chainColumnConfig != lastPatternChainColumnConfig) {
        patternChainGrid.setColumnConfiguration(chainColumnConfig);
        lastPatternChainColumnConfig = chainColumnConfig;
    }
    
    // Configure CellGrid
    patternChainGrid.setTableId("PatternChainTable");
    patternChainGrid.setTableFlags(ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                                   ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp |
                                   ImGuiTableFlags_Reorderable);
    patternChainGrid.enableReordering(true);
    patternChainGrid.setCellPadding(ImVec2(4, 2));
    patternChainGrid.setItemSpacing(ImVec2(2, 2));
    
    // Setup callbacks
    CellGridCallbacks callbacks;
    
    // Setup column widths (stretched width for pattern chain columns, fixed width for buttons)
    callbacks.setupParameterColumn = [](int colIndex, const CellGridColumnConfig& colConfig, int absoluteColIndex) -> bool {
        if (colConfig.parameterName == "buttons") {
            // Buttons column: fixed width
            float buttonColumnWidth = (BUTTON_HEIGHT * 3) + (ImGui::GetStyle().ItemSpacing.x * 2); // 3 buttons + spacing
            ImGui::TableSetupColumn(colConfig.displayName.c_str(), 
                                    ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoReorder, 
                                    buttonColumnWidth);
        } else {
            // Pattern columns: stretched width
            ImGui::TableSetupColumn(colConfig.displayName.c_str(), 
                                    ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 
                                    1.0f); // Stretch weight
        }
        return true;
    };
    
    // Custom header rendering with HeaderPopup for pattern selection
    // Note: col is parameter column index (0-based within parameter columns)
    callbacks.drawCustomHeader = [this, &sequencer, numPatterns, chain, currentPatternName]
                                 (int col, const CellGridColumnConfig& colConfig, ImVec2 cellStartPos, float columnWidth, float cellMinY) -> bool {
        // Buttons column: empty header
        if (colConfig.parameterName == "buttons") {
            return true; // Header drawn (empty)
        }
        
        // Get chain index for this column (col is parameter column index)
        // Handle empty chain case - use active pattern
        int chainIndex = col;
        std::string patternNameStr;
        bool isDisabled = false;
        
        if (chain.empty()) {
            // Chain is empty - show active pattern
            if (col != 0) return false;  // Only show one column when chain is empty
            patternNameStr = currentPatternName.empty() ? "Pattern" : currentPatternName;
            chainIndex = 0;  // Single column for active pattern
        } else {
            // Chain has entries
            if (col < 0 || col >= (int)chain.size()) return false;
            patternNameStr = chain[chainIndex];  // PatternChain now uses pattern names directly
            if (patternNameStr.empty()) {
                patternNameStr = "Pattern " + std::to_string(chainIndex);  // Fallback if name not found
            }
            isDisabled = sequencer.isPatternChainEntryDisabled(chainIndex);
        }
        
        int currentChainIndex = sequencer.getCurrentChainIndex();
        bool isPlaying = sequencer.isPlaying();
        
        // Style header background with color coding using ImGui's header color system
        ImU32 bgColor;
        if (isDisabled) {
            bgColor = GUIConstants::toU32(GUIConstants::Outline::DisabledBg);
        } else if (patternNameStr == currentPatternName && isPlaying) {
            bgColor = GUIConstants::toU32(GUIConstants::Active::PatternPlaying);
        } else if (patternNameStr == currentPatternName) {
            bgColor = GUIConstants::toU32(GUIConstants::Active::Pattern);
        } else if (chainIndex == currentChainIndex) {
            bgColor = GUIConstants::toU32(GUIConstants::Active::ChainEntry);
        } else {
            bgColor = GUIConstants::toU32(GUIConstants::Active::ChainEntryInactive);
        }
        
        // Style TableHeader to use our custom background color and look like a plain header
        ImVec4 bgColorVec = ImGui::ColorConvertU32ToFloat4(bgColor);
        // Make button colors match background to avoid button-like appearance
        ImGui::PushStyleColor(ImGuiCol_Header, bgColorVec);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, bgColorVec);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, bgColorVec);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); // Transparent
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bgColorVec);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, bgColorVec);
        
        // Use TableHeader for proper table integration (required for column reordering)
        ImGui::TableHeader(patternNameStr.c_str());
        
        // Draw disabled line after TableHeader (overlay on top)
        // Note: Removed border for current chain entry per user request
        if (isDisabled) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            float headerHeight = ImGui::GetFrameHeight();
            ImVec2 headerEndPos(cellStartPos.x + columnWidth, cellStartPos.y + headerHeight);
            ImU32 lineColor = GUIConstants::toU32(GUIConstants::Outline::Disabled);
            drawList->AddLine(cellStartPos, headerEndPos, lineColor, OUTLINE_THICKNESS);
        }
        
        // Pop style colors
        ImGui::PopStyleColor(6);
        
        // Handle header click - open HeaderPopup for pattern selection
        if (ImGui::IsItemClicked(0)) {
            std::string popupId = "PatternChainPopup_" + std::to_string(col);
            ImGui::OpenPopup(popupId.c_str());
        }
        
        // Draw HeaderPopup with all available patterns (using pattern names)
        std::string popupId = "PatternChainPopup_" + std::to_string(col);
        std::vector<HeaderPopup::PopupItem> items;
        // Get all pattern names from PatternRuntime
        // Note: PatternChain still uses indices internally, so we need to map names to indices
        auto patternNames = sequencer.getAllPatternNames();
        for (size_t i = 0; i < patternNames.size(); i++) {
            items.push_back(HeaderPopup::PopupItem(patternNames[i], patternNames[i]));
        }
        
        HeaderPopup::draw(popupId, items, columnWidth, cellStartPos,
                         [&sequencer, chainIndex, chain](const std::string& patternName) {
                             // PatternChain now uses pattern names directly
                             if (chain.empty()) {
                                 // Chain is empty - add pattern to chain and bind to it
                                 sequencer.addToPatternChain(patternName);
                                 sequencer.setCurrentPatternName(patternName);
                                 sequencer.setCurrentChainIndex(0);
                             } else {
                                 // Chain has entries - update existing entry
                                 sequencer.setPatternChainEntry(chainIndex, patternName);
                                 // If this is the current pattern, also update binding
                                 if (chainIndex == sequencer.getCurrentChainIndex()) {
                                     sequencer.setCurrentPatternName(patternName);
                                 }
                             }
                         },
                         nullptr, // filter (not used)
                         [&sequencer](const std::string& patternName) {
                             // Delete pattern callback
                             sequencer.removePatternByName(patternName);
                         });
        
        return true; // Header was drawn
    };
    
    // Row 0: Fixed position numbers (01, 02, etc.) - these never move when columns are reordered
    // Row 1: Editable repeat counts
    // Note: col is absolute column index (0+ = pattern columns, buttons column at end)
    callbacks.drawSpecialColumn = [this, &sequencer, chain, currentChainIndex, currentPatternName, isPlaying, useChain]
                                  (int row, int col, const CellGridColumnConfig& colConfig) {
        // Check if this is the buttons column (last column)
        if (colConfig.parameterName == "buttons") {
            if (row == 0) {
                // Row 0: Draw D/+/− buttons (aligned with chain position buttons)
                // 'D' button for duplicate current pattern (using pattern name)
                if (ImGui::Button("D", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
                    std::string currentPattern = sequencer.getCurrentPatternName();
                    if (!currentPattern.empty()) {
                        std::string newPatternName = sequencer.duplicatePatternByName(currentPattern);
                        if (!newPatternName.empty()) {
                            // Add the duplicated pattern to the chain
                            sequencer.addToPatternChain(newPatternName);
                            if (!(isPlaying && useChain)) {
                                sequencer.setCurrentPatternName(newPatternName);
                                sequencer.setCurrentChainIndex(sequencer.getPatternChainSize() - 1);
                            }
                        }
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Duplicate current pattern");
                }
                
                ImGui::SameLine();
                
                // '+' button to add current pattern to chain (duplicate entry)
                if (ImGui::Button("+", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
                    std::string currentPattern = sequencer.getCurrentPatternName();
                    if (!currentPattern.empty()) {
                        sequencer.addToPatternChain(currentPattern);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Add current pattern to chain");
                }
                
                ImGui::SameLine();
                
                // '-' button to remove currently selected pattern from chain
                bool canRemove = sequencer.getPatternChainSize() > 1;
                if (!canRemove) {
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
                }
                if (ImGui::Button("-", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT)) && canRemove) {
                    int chainSize = sequencer.getPatternChainSize();
                    int currentIndex = sequencer.getCurrentChainIndex();
                    if (chainSize > 1 && currentIndex >= 0 && currentIndex < chainSize) {
                        sequencer.removeFromPatternChain(currentIndex);
                    }
                }
                if (ImGui::IsItemHovered() && canRemove) {
                    ImGui::SetTooltip("Remove currently selected pattern from chain");
                }
                if (!canRemove) {
                    ImGui::PopStyleVar();
                }
            }
            // Row 1: empty for buttons column
            return;
        }
        
        // Pattern columns only
        // Handle empty chain case - show active pattern
        int chainIndex = col;
        std::string patternNameStr;
        bool isDisabled = false;
        bool isCurrentChainEntry = false;
        bool isCurrentPattern = false;
        
        if (chain.empty()) {
            // Chain is empty - show active pattern
            if (col != 0) return;  // Only show one column when chain is empty
            patternNameStr = currentPatternName.empty() ? "Pattern" : currentPatternName;
            chainIndex = 0;
            isCurrentPattern = true;  // Always current when it's the only one
            isCurrentChainEntry = true;
        } else {
            // Chain has entries
            if (col < 0 || col >= (int)chain.size()) return;
            chainIndex = col;
            patternNameStr = chain[chainIndex];  // PatternChain now uses pattern names directly
            if (patternNameStr.empty()) {
                patternNameStr = "Pattern " + std::to_string(chainIndex);  // Fallback if name not found
            }
            isCurrentChainEntry = (chainIndex == currentChainIndex);
            isCurrentPattern = (patternNameStr == currentPatternName);  // Compare by name
            isDisabled = sequencer.isPatternChainEntryDisabled(chainIndex);
        }
        
        if (row == 0) {
            // Row 0: Position numbers (01, 02, etc.) - fixed display, never moves
            ImVec2 cellSize = ImGui::GetContentRegionAvail();
            cellSize.y = PATTERN_CELL_HEIGHT;
            
            // Color coding: same as header
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
            ImVec2 cellEndPos(cursorPos.x + cellSize.x, cursorPos.y + cellSize.y);
            
            // Draw background
            drawList->AddRectFilled(cursorPos, cellEndPos, bgColor);
            
            // Draw border for current chain entry
            if (isCurrentChainEntry) {
                ImU32 borderColor = GUIConstants::toU32(GUIConstants::Active::ChainEntryBorder);
                drawList->AddRect(cursorPos, cellEndPos, borderColor, 0.0f, 0, 1.5f);
            }
            
            // Draw diagonal line if disabled
            if (isDisabled) {
                ImU32 lineColor = GUIConstants::toU32(GUIConstants::Outline::Disabled);
                drawList->AddLine(cursorPos, cellEndPos, lineColor, OUTLINE_THICKNESS);
            }
            
            // Draw position number (01, 02, etc.) - chain position, not pattern index
            char positionLabel[BUFFER_SIZE];
            snprintf(positionLabel, sizeof(positionLabel), "%02d", chainIndex + 1);
            ImVec2 textSize = ImGui::CalcTextSize(positionLabel);
            ImVec2 textPos(cursorPos.x + (cellSize.x - textSize.x) * 0.5f, 
                          cursorPos.y + (cellSize.y - textSize.y) * 0.5f);
            drawList->AddText(textPos, IM_COL32_WHITE, positionLabel);
            
            // Make it clickable
            ImGui::SetCursorScreenPos(cursorPos);
            ImGui::InvisibleButton(("##pos_" + std::to_string(chainIndex)).c_str(), cellSize, ImGuiButtonFlags_EnableNav);
            
            if (ImGui::IsItemClicked(0)) {
                if (isPlaying && useChain && !chain.empty()) {
                    // During playback with chain enabled: toggle disable state
                    sequencer.setPatternChainEntryDisabled(chainIndex, !isDisabled);
                } else {
                    // Normal behavior: select pattern (using pattern name)
                    if (chain.empty()) {
                        // Chain is empty - add pattern to chain first
                        sequencer.addToPatternChain(patternNameStr);
                        sequencer.setCurrentChainIndex(0);
                    }
                    sequencer.setCurrentPatternName(patternNameStr);
                    sequencer.setCurrentChainIndex(chainIndex);
                }
            }
            
            if (ImGui::IsItemHovered()) {
                if (isPlaying && useChain) {
                    ImGui::SetTooltip("Chain position %02d (%s)\nLeft-click: Toggle disable", chainIndex + 1, patternNameStr.c_str());
                } else {
                    ImGui::SetTooltip("Chain position %02d (%s)\nLeft-click: Select", chainIndex + 1, patternNameStr.c_str());
                }
            }
        } else if (row == 1) {
            // Row 1: Editable repeat count (only if chain is not empty)
            if (chain.empty()) {
                // Empty chain - show empty cell or placeholder
                return;
            }
            int repeatCount = sequencer.getPatternChainRepeatCount(chainIndex);
            
            ImVec2 cellSize = ImGui::GetContentRegionAvail();
            cellSize.y = REPEAT_CELL_HEIGHT;
            ImGui::PushItemWidth(cellSize.x);
            
            // Style the repeat count cell
            if (isCurrentChainEntry) {
                ImGui::PushStyleColor(ImGuiCol_FrameBg, GUIConstants::Frame::ChainEntry);
            }
            
            char repeatBuf[BUFFER_SIZE];
            snprintf(repeatBuf, sizeof(repeatBuf), "%d", repeatCount);
            
            if (ImGui::InputText(("##repeat_" + std::to_string(chainIndex)).c_str(), repeatBuf, sizeof(repeatBuf), 
                                 ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue | 
                                 ImGuiInputTextFlags_AutoSelectAll)) {
                try {
                    int newRepeat = std::stoi(repeatBuf);
                    newRepeat = std::max(1, std::min(99, newRepeat));
                    sequencer.setPatternChainRepeatCount(chainIndex, newRepeat);
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
        }
    };
    
    // Set callbacks
    patternChainGrid.setCallbacks(callbacks);
    
    // TODO: Detect column reordering and update pattern chain
    // ImGui handles visual reordering automatically when ImGuiTableFlags_Reorderable is set,
    // but we need to detect when columns are reordered and update the pattern chain accordingly.
    // This can be done by checking ImGui::TableGetColumnOrder() after rendering and comparing
    // to the previous order, then calling sequencer methods to reorder chain entries.
    
    // Begin table (no fixed columns - buttons are a regular parameter column at the end)
    patternChainGrid.beginTable(2, 0); // 2 rows, 0 fixed columns
    
    // Draw headers
    patternChainGrid.drawHeaders(0);
    
    // Draw rows
    for (int row = 0; row < 2; row++) {
        bool isPlaybackRow = false; // Pattern chain doesn't have playback rows
        bool isEditRow = false;
        patternChainGrid.drawRow(row, 0, isPlaybackRow, isEditRow);
    }
    
    patternChainGrid.endTable();
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    ImGui::PopID();
}

void TrackerSequencerGUI::drawPatternControls(TrackerSequencer& sequencer) {
    // Action buttons row removed - 'Clear Pattern' and 'D' buttons removed per requirements
    
    // Pattern parameters table using CellGrid (similar to MultiSampler's drawParameters)
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
    callbacks.createCell = [this, &sequencer](int row, int col, const CellGridColumnConfig& colConfig) -> std::unique_ptr<BaseCell> {
        const std::string& paramName = colConfig.parameterName;
        
        if (paramName == "steps") {
            ParameterDescriptor stepsParam("steps", ParameterType::INT, 4, 64, 16, "Steps");
            auto widget = createCellWidget(
                stepsParam,
                [&sequencer]() -> float {
                    return (float)sequencer.getCurrentPattern().getStepCount();
                },
                [&sequencer](float value) {
                    sequencer.getCurrentPattern().setStepCount((int)value);
                }
            );
            if (widget) {
                widget->isRemovable = false;  // Pattern params are not removable (different from pattern grid columns)
            }
            return widget;
        } else if (paramName == "stepsPerBeat") {
            ParameterDescriptor spbParam("stepsPerBeat", ParameterType::FLOAT, -96.0f, 96.0f, 4.0f, "Steps Per Beat");
            
            // Custom parsing for fractional values (1/2, 1/4, 1/8) and negative values
            auto customParser = [](const std::string& str) -> float {
                if (str.empty() || str == "--") {
                    return std::numeric_limits<float>::quiet_NaN();
                }
                
                // Handle negative sign
                bool isNegative = (str[0] == '-');
                std::string parseStr = isNegative ? str.substr(1) : str;
                
                // Try parsing as fraction (e.g., "1/2", "1/4", "1/8")
                size_t slashPos = parseStr.find('/');
                if (slashPos != std::string::npos && slashPos > 0 && slashPos < parseStr.length() - 1) {
                    try {
                        float numerator = std::stof(parseStr.substr(0, slashPos));
                        float denominator = std::stof(parseStr.substr(slashPos + 1));
                        if (denominator == 0.0f) {
                            return std::numeric_limits<float>::quiet_NaN();
                        }
                        float result = numerator / denominator;
                        return isNegative ? -result : result;
                    } catch (...) {
                        return std::numeric_limits<float>::quiet_NaN();
                    }
                }
                
                // Try parsing as regular float
                try {
                    float result = std::stof(parseStr);
                    return isNegative ? -result : result;
                } catch (...) {
                    return std::numeric_limits<float>::quiet_NaN();
                }
            };
            
            // Custom formatting to display fractions nicely
            auto customFormatter = [](float value) -> std::string {
                if (std::isnan(value)) {
                    return "--";
                }
                
                bool isNegative = (value < 0.0f);
                float absValue = std::abs(value);
                
                // Check for common fractions
                const float EPSILON = 0.001f;
                if (std::abs(absValue - 0.5f) < EPSILON) {
                    return isNegative ? "-1/2" : "1/2";
                } else if (std::abs(absValue - 0.25f) < EPSILON) {
                    return isNegative ? "-1/4" : "1/4";
                } else if (std::abs(absValue - 0.125f) < EPSILON) {
                    return isNegative ? "-1/8" : "1/8";
                } else if (std::abs(absValue - std::round(absValue)) < EPSILON) {
                    // Integer value
                    return std::to_string((int)(isNegative ? -std::round(absValue) : std::round(absValue)));
                } else {
                    // Regular float - show with limited decimals
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.3f", isNegative ? -absValue : absValue);
                    // Remove trailing zeros
                    std::string result = buf;
                    while (result.back() == '0' && result.find('.') != std::string::npos) {
                        result.pop_back();
                    }
                    if (result.back() == '.') {
                        result.pop_back();
                    }
                    return result;
                }
            };
            
            // Create widget with custom parser and formatter
            auto spbWidget = createCellWidget(
                spbParam,
                [&sequencer]() -> float {
                    return sequencer.getStepsPerBeat();
                },
                [&sequencer](float value) {
                    sequencer.setStepsPerBeat(value);
                },
                nullptr,  // remover
                customFormatter,
                customParser
            );
            if (spbWidget) {
                spbWidget->isRemovable = false;  // Pattern params are not removable (different from pattern grid columns)
            }
            return spbWidget;
        }
        
        return nullptr; // Return nullptr if not found
    };
    
    // Get cell values
    callbacks.getCellValue = [&sequencer](int row, int col, const CellGridColumnConfig& colConfig) -> float {
        const std::string& paramName = colConfig.parameterName;
        
        if (paramName == "steps") {
            return (float)sequencer.getCurrentPattern().getStepCount();
        } else if (paramName == "stepsPerBeat") {
            return sequencer.getStepsPerBeat();
        }
        
        return 0.0f;
    };
    
    // Set cell values
    callbacks.setCellValue = [&sequencer](int row, int col, float value, const CellGridColumnConfig& colConfig) {
        const std::string& paramName = colConfig.parameterName;
        
        if (paramName == "steps") {
            sequencer.getCurrentPattern().setStepCount((int)value);
        } else if (paramName == "stepsPerBeat") {
            sequencer.setStepsPerBeat(value);
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
    // Track pattern name changes to refresh column configuration
    std::string currentPatternName = sequencer.getCurrentPatternName();
    static std::string lastPatternName;
    bool patternChanged = (currentPatternName != lastPatternName);
    if (patternChanged) {
        lastPatternName = currentPatternName;
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
    
    // Get number of rows for the table
    int numRows = sequencer.getCurrentPattern().getStepCount();
    
    // Configure CellGrid using unified helper
    // Use SizingFixedFit for mixed column sizing (fixed + stretch columns)
    // This allows first two columns to be fixed and parameter columns to stretch proportionally
    ModuleGUI::CellGridConfig gridConfig;
    gridConfig.tableId = "TrackerGrid";
    gridConfig.tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY;
    gridConfig.enableScrolling = true;
    gridConfig.scrollHeight = 0.0f;  // 0.0f means auto-calculate from available content region (adapts to window size)
    gridConfig.scrollbarSize = SCROLLBAR_SIZE;
    configureCellGrid(cellGrid, gridConfig);
    
    // Convert column configuration to CellGrid format
    std::vector<CellGridColumnConfig> tableColumnConfig;
    for (const auto& col : sequencer.getColumnConfiguration()) {
        // Index and Length columns should not be draggable and are not removable
        bool isDraggable = !col.isRequired;  // Required columns (index, length) are not draggable
        bool isRemovable = !col.isRequired;  // Use isRequired (inverted)
        // Derive displayName from parameterName (capitalize first letter)
        std::string displayName = col.getDisplayName();
        CellGridColumnConfig tableCol(
            col.parameterName, displayName, isRemovable, col.columnIndex, isDraggable);
        tableColumnConfig.push_back(tableCol);
    }
    
    // Update column configuration using unified helper (only updates if changed)
    updateColumnConfigIfChanged(cellGrid, tableColumnConfig, lastColumnConfig);
    // Query external parameters from connected modules (GUI layer handles ParameterRouter dependency)
    std::vector<ParameterDescriptor> externalParams = queryExternalParameters(sequencer);
    cellGrid.setAvailableParameters(sequencer.getAvailableParameters(externalParams));
    
    // Store header buttons per column for custom header rendering
    std::map<int, std::vector<HeaderButton>> columnHeaderButtons;
    
    // Register header buttons based on column category/type
    // Column indexing system:
    // - Absolute indices: 0 = step number, 1 = index column, 2 = length column, 3+ = parameter columns
    // - All callbacks and CellGrid use absolute indices for consistency
    // - getColumnConfiguration() returns parameter columns only (0-based), so convert when accessing: paramColIdx = absoluteColIdx - 1
    cellGrid.clearHeaderButtons();
    for (size_t i = 0; i < sequencer.getColumnConfiguration().size(); i++) {
        const auto& colConfig = sequencer.getColumnConfiguration()[i];
        int absoluteColIdx = (int)i + 1;  // Convert parameter-relative to absolute (1+)
        
        // ALL columns get "R" (randomize) button - including index column (was missing before)
        HeaderButton randomizeBtn("R", "Randomize", [&sequencer, absoluteColIdx]() {
            sequencer.randomizeColumn(absoluteColIdx);
        });
        cellGrid.registerHeaderButton(absoluteColIdx, randomizeBtn);
        columnHeaderButtons[absoluteColIdx].push_back(randomizeBtn);
        
        // Length column additionally gets "L" (legato) button
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
    // Only enable auto-scroll if user is not editing
    // Auto-scroll will be disabled during editing via getFocusedRow callback returning -1
    cellGrid.enableAutoScroll(true);
    
    // Begin table (CellGrid handles ImGui::BeginTable internally)
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
        // For multi-row selection, update XMax for any row in the selection (last row will have final value)
        if (pendingRowOutline.shouldDraw) {
            bool isInSelection = false;
            if (selectionState.hasSelection()) {
                int selectionStart = selectionState.getStartStep();
                int selectionEnd = selectionState.getEndStep();
                isInSelection = (step >= selectionStart && step <= selectionEnd);
            }
            // Update XMax for single row selection or any row in multi-row selection
            if (pendingRowOutline.step == step || isInSelection) {
                ImVec2 lastCellMin = ImGui::GetCursorScreenPos();
                float lastCellWidth = ImGui::GetColumnWidth();
                pendingRowOutline.rowXMax = lastCellMin.x + lastCellWidth + 1;
            }
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
    
    // Track if button was clicked for use in focus handler below
    bool stepButtonWasClicked = (buttonClicked && !spacebarPressed && isItemClicked);
    
    // Handle step button click - trigger step via syncPlaybackToEditIfPaused (respects empty check)
    if (stepButtonWasClicked) {
        callbacksState.anyCellFocusedThisFrame = true;
        
        // Save previous step BEFORE updating focus state
        int previousStep = cellFocusState.row;
        
        // When in edit mode, prevent focus from changing to a different cell
        if (cellFocusState.isEditing && (previousStep != step || cellFocusState.column != 0)) {
            return;
        }
        
        // Update focus state to column 0 (step number column)
        setEditCell(step, 0);
        
        // Trigger step via syncPlaybackToEditIfPaused (properly handles empty step check)
        bool stepChanged = (step != sequencer.getPlaybackStep());
        bool fromHeaderRow = (previousStep == -1);
        syncPlaybackToEditIfPaused(sequencer, step, stepChanged, fromHeaderRow, lastTriggeredStepWhenPaused);
    }
    
    // Handle keyboard navigation focus changes for step button (column 0)
    // Note: onCellFocusChanged only fires for parameter columns, so we need to handle column 0 here
    bool actuallyFocused = ImGui::IsItemFocused();
    if (actuallyFocused && !stepButtonWasClicked) {
        bool keyboardNavActive = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
        
        // Only handle keyboard navigation (not clicks - those are handled above)
        if (keyboardNavActive) {
            // Prevent duplicate triggers from multiple cells in the same frame
            int currentFrame = ImGui::GetFrameCount();
            bool alreadyTriggeredThisFrame = (lastTriggeredStepFrame == currentFrame && 
                                              lastTriggeredStepThisFrame == step);
            
            if (!alreadyTriggeredThisFrame) {
                callbacksState.anyCellFocusedThisFrame = true;
                bool cellChanged = (cellFocusState.row != step || cellFocusState.column != 0);
                
                // When in edit mode, prevent focus from changing to a different cell
                if (cellFocusState.isEditing && cellChanged) {
                    return;
                }
                
                // Save previous step BEFORE updating focus state
                int previousStep = cellFocusState.row;
                
                // Update focus state to column 0 (step number column)
                setEditCell(step, 0);
                
                // Trigger step via syncPlaybackToEditIfPaused (properly handles empty step check)
                // Only trigger if step actually changed (prevents re-triggering same step)
                bool stepChanged = (step != sequencer.getPlaybackStep());
                bool fromHeaderRow = (previousStep == -1);
                if (fromHeaderRow || stepChanged) {
                    syncPlaybackToEditIfPaused(sequencer, step, stepChanged, fromHeaderRow, lastTriggeredStepWhenPaused);
                    lastTriggeredStepThisFrame = step;
                    lastTriggeredStepFrame = currentFrame;
                }
            }
        }
    }
    
    // Draw outline for selected cells only (not hover)
    // When step number cell is selected, draw outline around entire row
    // Don't draw outline if we're on header row (cellFocusState.row == -1)
    bool isSelected = (cellFocusState.row == step && cellFocusState.column == 0 && cellFocusState.row >= 0);
    bool isFocused = ImGui::IsItemFocused();
    bool shouldShowOutline = isSelected || (isFocused && !cellFocusState.isEditing && cellFocusState.row >= 0);
    
    // Check if this step is part of a multi-row selection
    bool isInSelection = false;
    int selectionStart = -1;
    int selectionEnd = -1;
    if (selectionState.hasSelection()) {
        selectionStart = selectionState.getStartStep();
        selectionEnd = selectionState.getEndStep();
        isInSelection = (step >= selectionStart && step <= selectionEnd);
    }
    
    if (shouldShowOutline || isInSelection) {
        if (isSelected || isInSelection) {
            // Store row outline info to draw after all cells are drawn
            // For multi-row selection, expand Y bounds to include all selected rows
            if (isInSelection && selectionState.hasSelection()) {
                // Multi-row selection: expand Y bounds
                if (!pendingRowOutline.shouldDraw) {
                    // First row in selection: initialize bounds
                    pendingRowOutline.shouldDraw = true;
                    pendingRowOutline.step = step;
                    pendingRowOutline.rowYMin = cellMin.y - 1;
                    pendingRowOutline.rowYMax = cellMax.y + 1;
                    pendingRowOutline.rowXMin = cellMin.x - 1; // Store first cell X position
                    pendingRowOutline.rowXMax = cellMax.x + 1; // Will be updated after all cells drawn
                } else {
                    // Subsequent row in selection: expand Y bounds
                    pendingRowOutline.rowYMin = std::min(pendingRowOutline.rowYMin, cellMin.y - 1);
                    pendingRowOutline.rowYMax = std::max(pendingRowOutline.rowYMax, cellMax.y + 1);
                    // X bounds should be consistent (use first/last cell positions)
                    // rowXMin stays at first cell, rowXMax will be updated after all cells drawn
                }
            } else {
                // Single row selection: use existing logic
                pendingRowOutline.shouldDraw = true;
                pendingRowOutline.step = step;
                pendingRowOutline.rowYMin = cellMin.y - 1;
                pendingRowOutline.rowYMax = cellMax.y + 1;
                pendingRowOutline.rowXMin = cellMin.x - 1; // Store first cell X position
                pendingRowOutline.rowXMax = cellMax.x + 1; // Will be updated after all cells drawn
            }
            
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
    
    // Use ImGui key detection for reliable clipboard operations (works across platforms)
    ImGuiIO& io = ImGui::GetIO();
    bool cmdOrCtrlPressed = io.KeyCtrl || io.KeySuper; // Support both Ctrl and Cmd (Super)
    // Also check the passed ctrlPressed parameter as fallback (for OF key events)
    if (!cmdOrCtrlPressed) {
        cmdOrCtrlPressed = ctrlPressed;
    }
    
    // CRITICAL: Step-level operations (copy/paste/cut/duplicate) only work when step number column is focused
    // When parameter columns are focused (column != 0), CellWidget handles individual cell copy/paste
    bool isStepColumnFocused = (cellFocusState.hasFocus() && cellFocusState.column == 0);
    
    // Handle step-level clipboard and selection operations (only for step number column)
    // Use both ImGui key detection and OF key codes for maximum compatibility
    if (isStepColumnFocused) {
        // cmd+C / ctrl+C: Copy selected steps (step-level operation)
        bool cKeyPressed = ImGui::IsKeyPressed(ImGuiKey_C, false) || (key == 'c' || key == 'C');
        if (cmdOrCtrlPressed && cKeyPressed) {
            if (selectionState.hasSelection()) {
                sequencer->copySteps(selectionState.getStartStep(), selectionState.getEndStep());
                return true;
            } else if (selectionState.hasSingleStep()) {
                sequencer->copySteps(selectionState.anchorStep, selectionState.anchorStep);
                return true;
            } else if (cellFocusState.row >= 0) {
                // Copy single step at current focus
                sequencer->copySteps(cellFocusState.row, cellFocusState.row);
                return true;
            }
            return false; // Nothing to copy
        }
        
        // cmd+V / ctrl+V: Paste steps (step-level operation)
        bool vKeyPressed = ImGui::IsKeyPressed(ImGuiKey_V, false) || (key == 'v' || key == 'V');
        if (cmdOrCtrlPressed && vKeyPressed) {
            if (cellFocusState.row >= 0) {
                if (sequencer->pasteSteps(cellFocusState.row)) {
                    // Clear selection after paste
                    selectionState.clear();
                    return true;
                }
            }
            return false; // No valid paste destination
        }
        
        // cmd+X / ctrl+X: Cut selected steps (step-level operation)
        bool xKeyPressed = ImGui::IsKeyPressed(ImGuiKey_X, false) || (key == 'x' || key == 'X');
        if (cmdOrCtrlPressed && xKeyPressed) {
            if (selectionState.hasSelection()) {
                sequencer->cutSteps(selectionState.getStartStep(), selectionState.getEndStep());
                selectionState.clear();
                return true;
            } else if (selectionState.hasSingleStep()) {
                sequencer->cutSteps(selectionState.anchorStep, selectionState.anchorStep);
                selectionState.clear();
                return true;
            } else if (cellFocusState.row >= 0) {
                // Cut single step at current focus
                sequencer->cutSteps(cellFocusState.row, cellFocusState.row);
                return true;
            }
            return false; // Nothing to cut
        }
        
        // cmd+A / ctrl+A: Select all steps in current pattern
        bool aKeyPressed = ImGui::IsKeyPressed(ImGuiKey_A, false) || (key == 'a' || key == 'A');
        if (cmdOrCtrlPressed && aKeyPressed) {
            int stepCount = sequencer->getStepCount();
            if (stepCount > 0) {
                selectionState.setAnchor(0);
                selectionState.extendTo(stepCount - 1);
                return true;
            }
            return false; // No steps to select
        }
        
        // cmd+D / ctrl+D: Duplicate selected steps (step-level operation)
        // Auto-expands pattern if duplication would exceed current step count
        bool dKeyPressed = ImGui::IsKeyPressed(ImGuiKey_D, false) || (key == 'd' || key == 'D');
        if (cmdOrCtrlPressed && dKeyPressed) {
            if (selectionState.hasSelection()) {
                int start = selectionState.getStartStep();
                int end = selectionState.getEndStep();
                int numSteps = end - start + 1;
                int dest = end + 1;  // Paste after selection
                int currentStepCount = sequencer->getStepCount();
                
                // Auto-expand pattern if duplication would exceed bounds
                if (dest + numSteps > currentStepCount) {
                    int newStepCount = dest + numSteps;
                    sequencer->setStepCount(newStepCount);
                }
                
                sequencer->duplicateSteps(start, end, dest);
                // Extend selection to include duplicated steps
                selectionState.extendTo(dest + numSteps - 1);
                return true;
            } else if (selectionState.hasSingleStep()) {
                int dest = selectionState.anchorStep + 1;
                int currentStepCount = sequencer->getStepCount();
                
                // Auto-expand pattern if duplication would exceed bounds
                if (dest >= currentStepCount) {
                    int newStepCount = dest + 1;
                    sequencer->setStepCount(newStepCount);
                }
                
                sequencer->duplicateSteps(selectionState.anchorStep, selectionState.anchorStep, dest);
                selectionState.extendTo(dest);
                return true;
            } else if (cellFocusState.row >= 0) {
                // Duplicate single step at current focus
                int dest = cellFocusState.row + 1;
                int currentStepCount = sequencer->getStepCount();
                
                // Auto-expand pattern if duplication would exceed bounds
                if (dest >= currentStepCount) {
                    int newStepCount = dest + 1;
                    sequencer->setStepCount(newStepCount);
                }
                
                sequencer->duplicateSteps(cellFocusState.row, cellFocusState.row, dest);
                return true;
            }
            return false; // Nothing to duplicate
        }
    }
    
    // Backspace: Clear step (when not editing cell and on step column)
    // Note: When on parameter columns, CellWidget handles Backspace for individual cells
    if (key == OF_KEY_BACKSPACE && !cellFocusState.isEditing && isStepColumnFocused) {
        if (selectionState.hasSelection()) {
            sequencer->clearStepRange(selectionState.getStartStep(), selectionState.getEndStep());
            selectionState.clear();
            return true;
        } else if (selectionState.hasSingleStep()) {
            sequencer->clearStep(selectionState.anchorStep);
            selectionState.clear();
            return true;
        } else if (cellFocusState.row >= 0) {
            sequencer->clearStep(cellFocusState.row);
            return true;
        }
        return false; // Nothing to clear
    }
    
    // Shift + Arrow keys: Extend selection (only when pattern grid has focus)
    if (shiftPressed && cellFocusState.hasFocus()) {
        if (key == OF_KEY_UP || key == OF_KEY_DOWN) {
            int currentStep = cellFocusState.row;
            int newStep = currentStep;
            
            if (key == OF_KEY_UP) {
                newStep = std::max(0, currentStep - 1);
            } else if (key == OF_KEY_DOWN) {
                newStep = std::min(sequencer->getStepCount() - 1, currentStep + 1);
            }
            
            // Initialize selection if not already selecting
            if (!selectionState.isSelecting) {
                selectionState.setAnchor(currentStep);
            }
            
            // Extend selection to new step
            selectionState.extendTo(newStep);
            
            // Move focus to new step
            setEditCell(newStep, cellFocusState.column);
            
            return true;
        }
    }
    
    // Clear selection when clicking/navigating without shift (handled in callbacks)
    // But also clear if user presses a non-shift navigation key
    if (!shiftPressed && (key == OF_KEY_UP || key == OF_KEY_DOWN || key == OF_KEY_LEFT || key == OF_KEY_RIGHT)) {
        if (selectionState.isSelecting) {
            selectionState.clear();
        }
    }
    
    // Handle UP/DOWN arrow key navigation between steps when not editing
    // Works for ALL columns - preserves the current column while navigating rows
    // When editing, CellWidget handles arrow keys for value adjustment
    if (!shiftPressed && !cellFocusState.isEditing && cellFocusState.hasFocus()) {
        if (key == OF_KEY_UP || key == OF_KEY_DOWN) {
            int currentStep = cellFocusState.row;
            int newStep = currentStep;
            
            if (key == OF_KEY_UP) {
                newStep = std::max(0, currentStep - 1);
            } else if (key == OF_KEY_DOWN) {
                newStep = std::min(sequencer->getStepCount() - 1, currentStep + 1);
            }
            
            // Only navigate if step actually changed
            if (newStep != currentStep) {
                // Update focus state - preserves current column for non-column-0 navigation
                // Don't trigger here - let ImGui move focus, then drawStepNumber (col 0) or 
                // onCellFocusChanged (col > 0) will trigger the step
                setEditCell(newStep, cellFocusState.column);
                
                // Return false to let ImGui process the arrow key and move focus
                // This ensures the new cell appears focused and triggers properly
                return false;
            }
        }
    }
    
    // Parameter column focused - let CellWidget handle individual cell copy/paste
    // But only return early for clipboard operations, not for arrow keys (handled above)
    if (cellFocusState.hasFocus() && cellFocusState.column != 0) {
        // Check if this is a clipboard operation that CellWidget should handle
        bool isClipboardOp = (cmdOrCtrlPressed && (key == 'c' || key == 'C' || key == 'v' || key == 'V' || key == 'x' || key == 'X'));
        if (isClipboardOp) {
            // Return false to allow CellWidget to process cmd+C/V/X
            return false;
        }
    }
    
    // PHASE 1: SINGLE INPUT PATH - CellWidget is sole input processor for cells
    // If any cell has focus, let CellWidget handle remaining input
    if (patternParamsFocusState.hasFocus() || cellFocusState.hasFocus()) {
        return false;
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
    ConnectionManager* connectionManager = getConnectionManager();
    ModuleRegistry* registry = getRegistry();
    
    if (!connectionManager || !registry) {
        // Only log once per frame to avoid spam - use static to track last log time
        static float lastLogTime = 0.0f;
        float currentTime = ofGetElapsedTimef();
        if (currentTime - lastLogTime > 1.0f) {  // Log at most once per second
            ofLogWarning("TrackerSequencerGUI") << "queryExternalParameters: Missing dependencies - "
                                                << "ConnectionManager: " << (connectionManager ? "OK" : "NULL") 
                                                << ", Registry: " << (registry ? "OK" : "NULL")
                                                << " (instance: " << getInstanceName() << ")";
            lastLogTime = currentTime;
        }
        return externalParams; // Return empty if dependencies not available
    }
    
    // Query parameters only from modules connected via EVENT connections
    // This ensures we only show parameters from modules that this sequencer is actually connected to
    // Use instance name from GUI instead of sequencer.getName() which returns type name
    std::string sequencerName = getInstanceName();
    if (sequencerName.empty()) {
        // Fallback to sequencer.getName() if instance name not set (shouldn't happen)
        sequencerName = sequencer.getName();
        ofLogWarning("TrackerSequencerGUI") << "queryExternalParameters: Instance name empty, using type name: " << sequencerName;
    }
    auto connections = connectionManager->getConnectionsFrom(sequencerName);
    
    std::map<std::string, ParameterDescriptor> uniqueParams;
    
    for (const auto& conn : connections) {
        // Only process EVENT connections (tracker -> instrument connections)
        if (conn.type != ConnectionManager::ConnectionType::EVENT) {
            continue;
        }
        
        // Get the connected module
        auto connectedModule = registry->getModule(conn.targetModule);
        if (!connectedModule) {
            continue;
        }
        
        // Get parameters from this connected module
        auto moduleParams = connectedModule->getParameters();
        
        for (const auto& param : moduleParams) {
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
    // UNIFIED: Now handles ALL columns (required and optional)
    callbacks.drawCustomHeader = [this, &sequencer, &columnHeaderButtons, &headerClickedThisFrame]
                                  (int col, const CellGridColumnConfig& colConfig, ImVec2 cellStartPos, float columnWidth, float cellMinY) -> bool {
        // Draw column name (left-aligned) for all columns
        ImGui::TableHeader(colConfig.displayName.c_str());
        
        // Check if header was clicked (for focus clearing)
        if (ImGui::IsItemClicked(0)) {
            headerClickedThisFrame = true;
        }
        
        // UNIFIED: Right-click context menu for ALL columns (required and optional)
        std::string contextMenuId = "##ColumnContextMenu_" + std::to_string(col);
        if (ImGui::BeginPopupContextItem(contextMenuId.c_str())) {
            int columnConfigIndex = colConfig.columnIndex;
            
            // Get actual ColumnConfig from sequencer to check category
            const auto& sequencerCols = sequencer.getColumnConfiguration();
            bool isRequiredCol = false;
            ColumnCategory colCategory = ColumnCategory::PARAMETER;
            if (columnConfigIndex >= 0 && columnConfigIndex < (int)sequencerCols.size()) {
                const auto& actualCol = sequencerCols[columnConfigIndex];
                isRequiredCol = actualCol.isRequired;
                colCategory = actualCol.category;
            }
            
            // Simple Add/Remove items (above column list)
            // Add Column - try to add same column type, fallback to first available if can't duplicate
            if (ImGui::MenuItem("Add Column")) {
                std::string currentParamName = "";
                ColumnCategory currentCategory = ColumnCategory::PARAMETER;
                if (columnConfigIndex >= 0 && columnConfigIndex < (int)sequencerCols.size()) {
                    currentParamName = sequencerCols[columnConfigIndex].parameterName;
                    currentCategory = sequencerCols[columnConfigIndex].category;
                }
                
                // Try to add same column type (only TRIGGER category allows duplicates)
                bool added = false;
                if (!currentParamName.empty() && currentCategory == ColumnCategory::TRIGGER) {
                    std::string displayName = (currentParamName == "index") ? "Index" : 
                                             (currentParamName == "note") ? "Note" : 
                                             (currentParamName == "length") ? "Length" : currentParamName;
                    sequencer.addColumn(currentParamName, displayName);
                    added = true; // TRIGGER columns always allow duplicates
                }
                
                // Fallback: find first available parameter
                if (!added) {
                std::set<std::string> usedParamNames;
                for (const auto& col : sequencer.getColumnConfiguration()) {
                    usedParamNames.insert(col.parameterName);
                }
                
                std::vector<ParameterDescriptor> externalParams = queryExternalParameters(sequencer);
                auto allParams = sequencer.getAvailableParameters(externalParams);
                
                for (const auto& param : allParams) {
                    if (param.name == "index" || param.name == "length") continue; // Skip required
                    if (usedParamNames.find(param.name) == usedParamNames.end()) {
                        sequencer.addColumn(param.name, param.displayName);
                        break;
                        }
                    }
                }
            }
            
            // Remove Column - only if this column is removable
            if (!isRequiredCol) {
                if (ImGui::MenuItem("Remove Column")) {
                    sequencer.removeColumn(columnConfigIndex);
                }
            }
            
            ImGui::Separator();
            
            // Column visibility toggles (checkboxes for optional columns only, like View menu)
            // Build map of used parameter names to their column indices
            std::map<std::string, int> usedParamToColumnIndex;
            for (const auto& col : sequencer.getColumnConfiguration()) {
                usedParamToColumnIndex[col.parameterName] = col.columnIndex;
            }
            
            // Helper lambda to show a parameter menu item
            auto showParamItem = [&](const ParameterDescriptor& param) -> bool {
                if (param.name == "index" || param.name == "length") return false; // Skip required
                
                bool isColumnPresent = usedParamToColumnIndex.find(param.name) != usedParamToColumnIndex.end();
                if (ImGui::MenuItem(param.displayName.c_str(), nullptr, isColumnPresent)) {
                    if (isColumnPresent) {
                        auto it = usedParamToColumnIndex.find(param.name);
                        if (it != usedParamToColumnIndex.end()) {
                            sequencer.removeColumn(it->second);
                        }
                    } else {
                        sequencer.addColumn(param.name, param.displayName);
                    }
                }
                return true;
            };
            
            bool hasItems = false;
            
            // Show internal parameters (sequencer-specific: index, note, chance, ratio)
            // Count index/note columns for display
            std::map<std::string, int> paramCounts;
            for (const auto& col : sequencer.getColumnConfiguration()) {
                if (col.parameterName == "index" || col.parameterName == "note") {
                    paramCounts[col.parameterName]++;
                }
            }
            
            // Get tracker-specific parameters (index, note, length, chance, ratio)
            auto trackerParams = sequencer.getTrackerParameters();
            std::vector<ParameterDescriptor> internalOnlyParams;
            std::set<std::string> internalParamNames; // Track to exclude from external
            // Filter to only show index, note, chance, ratio (length is handled separately as required column)
            for (const auto& param : trackerParams) {
                if (param.name == "index" || param.name == "note" || param.name == "chance" || param.name == "ratio") {
                    internalOnlyParams.push_back(param);
                    internalParamNames.insert(param.name);
                }
            }
            if (!internalOnlyParams.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("Internal");
                for (const auto& param : internalOnlyParams) {
                    // Show count for index/note columns
                    std::string displayText = param.displayName;
                    if ((param.name == "index" || param.name == "note") && paramCounts[param.name] > 0) {
                        displayText += " (" + std::to_string(paramCounts[param.name]) + ")";
                    }
                    bool isColumnPresent = usedParamToColumnIndex.find(param.name) != usedParamToColumnIndex.end();
                    if (ImGui::MenuItem(displayText.c_str(), nullptr, isColumnPresent)) {
                        if (isColumnPresent) {
                            auto it = usedParamToColumnIndex.find(param.name);
                            if (it != usedParamToColumnIndex.end()) {
                                sequencer.removeColumn(it->second);
                            }
                        } else {
                            sequencer.addColumn(param.name, param.displayName);
                }
                hasItems = true;
                    }
                }
            }
            
            // Show external parameters grouped by module
            ConnectionManager* connectionManager = getConnectionManager();
            ModuleRegistry* registry = getRegistry();
            if (connectionManager && registry) {
                std::string sequencerName = getInstanceName();
                if (sequencerName.empty()) sequencerName = sequencer.getName();
                
                auto connections = connectionManager->getConnectionsFrom(sequencerName);
                for (const auto& conn : connections) {
                    if (conn.type != ConnectionManager::ConnectionType::EVENT) continue;
                    
                    auto connectedModule = registry->getModule(conn.targetModule);
                    if (!connectedModule) continue;
                    
                    ImGui::Separator();
                    ImGui::TextDisabled(conn.targetModule.c_str());
                    
                    for (const auto& param : connectedModule->getParameters()) {
                        // Skip parameters that conflict with internal parameters
                        if (internalParamNames.find(param.name) != internalParamNames.end()) {
                            continue;
                        }
                        if (showParamItem(param)) hasItems = true;
                    }
                }
            }
            
            if (!hasItems) {
                ImGui::TextDisabled("No optional columns available");
            }
            
            ImGui::EndPopup();
        }
        
        // Handle swap popup (for optional columns and index/note columns)
        const auto& sequencerCols = sequencer.getColumnConfiguration();
        bool canSwap = false;
        int columnConfigIndex = colConfig.columnIndex;
        if (columnConfigIndex >= 0 && columnConfigIndex < (int)sequencerCols.size()) {
            const auto& actualCol = sequencerCols[columnConfigIndex];
            // Allow swapping: optional parameter columns, or index/note columns (to swap between them)
            canSwap = (!actualCol.isRequired && actualCol.isParameterColumn()) || 
                      (actualCol.parameterName == "index" || actualCol.parameterName == "note");
        }
        
        if (canSwap) {
            std::string popupId = "SwapPopup_" + std::to_string(col);
            
            // Check if popup should be opened (click or Enter on header)
            bool headerClicked = ImGui::IsItemClicked(0);
            bool enterPressed = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
            if (headerClicked || enterPressed) {
                ImGui::OpenPopup(popupId.c_str());
            }
            
            // Check if this is an index/note column (can swap between them)
            bool isIndexNoteColumn = false;
            if (columnConfigIndex >= 0 && columnConfigIndex < (int)sequencerCols.size()) {
                const auto& actualCol = sequencerCols[columnConfigIndex];
                isIndexNoteColumn = (actualCol.parameterName == "index" || actualCol.parameterName == "note");
            }
            
            std::map<std::string, ParameterDescriptor> paramMap;
            std::vector<HeaderPopup::PopupItem> items;
            
            if (isIndexNoteColumn) {
                // For index/note columns, show only the opposite option
                const auto& actualCol = sequencerCols[columnConfigIndex];
            
                // Use unified registry for parameter descriptors
                auto trackerParams = sequencer.getTrackerParameters();
                if (actualCol.parameterName == "index") {
                    // Currently index, show note option
                    for (const auto& param : trackerParams) {
                        if (param.name == "note") {
                            items.push_back(HeaderPopup::PopupItem("note", "Note"));
                            paramMap["note"] = param;
                            break;
                        }
                    }
                } else {
                    // Currently note, show index option
                    for (const auto& param : trackerParams) {
                        if (param.name == "index") {
                            items.push_back(HeaderPopup::PopupItem("index", "Index"));
                            paramMap["index"] = param;
                            break;
                        }
                    }
                }
            } else {
                // For other columns, show external parameters
                std::vector<ParameterDescriptor> externalParams = queryExternalParameters(sequencer);
            auto allParams = sequencer.getAvailableParameters(externalParams);
            
            std::set<std::string> usedParamNames;
            for (const auto& col : sequencer.getColumnConfiguration()) {
                usedParamNames.insert(col.parameterName);
            }
            
            for (const auto& param : allParams) {
                    // Skip internal parameters (chance, ratio, note, index) - they're only for index/note columns or condition columns
                    if (param.name == "chance" || param.name == "ratio" || param.name == "note" || param.name == "index") {
                    continue;
                }
                // Skip already used parameters
                if (usedParamNames.find(param.name) == usedParamNames.end()) {
                    items.push_back(HeaderPopup::PopupItem(param.name, param.displayName));
                        paramMap[param.name] = param;
                    }
                }
            }
            
            // Draw popup (will only show if open)
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
        }
        
        // Draw header buttons manually (since we're using custom header renderer)
        // NOTE: 'col' is parameter column index (0-based), but buttons are registered with absolute indices
        // Convert to absolute index: absolute = col + 1 (col 0 is step number, parameter columns start at 1)
        int absoluteColIdx = col + 1;
        auto it = columnHeaderButtons.find(absoluteColIdx);
        if (it != columnHeaderButtons.end() && !it->second.empty()) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
            
            // Calculate total button width including spacing
            float totalButtonWidth = 0.0f;
            for (size_t btnIdx = 0; btnIdx < it->second.size(); btnIdx++) {
                const auto& btn = it->second[btnIdx];
                std::string btnLabel = btn.getDynamicLabel ? btn.getDynamicLabel() : btn.label;
                float btnWidth = ImGui::CalcTextSize(btnLabel.c_str()).x + 
                                ImGui::GetStyle().FramePadding.x * 2.0f;
                totalButtonWidth += btnWidth;
                if (btnIdx < it->second.size() - 1) {
                    totalButtonWidth += BUTTON_SPACING; // Spacing between buttons
                }
            }
            
            // Calculate button start position: align to right edge of cell
            // Use consistent padding that matches ImGui's table cell padding
            float cellPadding = ImGui::GetStyle().CellPadding.x;
            float cellMaxX = cellStartPos.x + columnWidth;
            float buttonStartX = cellMaxX - totalButtonWidth - cellPadding;
            
            // Ensure buttons don't go outside cell bounds
            buttonStartX = std::max(cellStartPos.x + cellPadding, buttonStartX);
            
            // Draw buttons from right to left (R button first, then L button if present)
            float currentX = buttonStartX;
            for (size_t btnIdx = 0; btnIdx < it->second.size(); btnIdx++) {
                const auto& btn = it->second[btnIdx];
                std::string btnLabel = btn.getDynamicLabel ? btn.getDynamicLabel() : btn.label;
                std::string btnTooltip = btn.getDynamicTooltip ? btn.getDynamicTooltip() : btn.tooltip;
                
                // Recalculate button width to ensure consistency
                float btnWidth = ImGui::CalcTextSize(btnLabel.c_str()).x + 
                                ImGui::GetStyle().FramePadding.x * 2.0f;
                
                // Set cursor position for button (accounting for frame padding)
                ImGui::SetCursorScreenPos(ImVec2(currentX, cellMinY));
                
                if (ImGui::SmallButton(btnLabel.c_str())) {
                    if (btn.onClick) {
                        btn.onClick();
                    }
                }
                
                if (ImGui::IsItemHovered() && !btnTooltip.empty()) {
                    ImGui::SetTooltip("%s", btnTooltip.c_str());
                }
                
                // Move to next button position
                currentX += btnWidth;
                if (btnIdx < it->second.size() - 1) {
                    currentX += BUTTON_SPACING; // Spacing between buttons
                }
            }
            
            ImGui::PopStyleVar();
        }
        
        return true; // Header was drawn by custom renderer
    };
    
    // UNIFIED column sizing: All columns use WidthStretch with equal weight
    // User can resize columns manually, and ImGui will save/restore widths in imgui.ini
    callbacks.setupParameterColumn = [](int colIndex, const CellGridColumnConfig& colConfig, int absoluteColIndex) -> bool {
        ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_WidthStretch;
        float widthOrWeight = 1.0f;  // Equal weight for all columns
        
        // Disable reordering for non-draggable columns (required columns like index, length)
        if (!colConfig.isDraggable) {
            flags |= ImGuiTableColumnFlags_NoReorder;
        }
        
        ImGui::TableSetupColumn(colConfig.displayName.c_str(), flags, widthOrWeight);
        return true; // Column was set up
    };
}

void TrackerSequencerGUI::setupCellValueCallbacks(CellGridCallbacks& callbacks, TrackerSequencer& sequencer) {
    callbacks.createCell = [this, &sequencer](int row, int col, const CellGridColumnConfig& colConfig) -> std::unique_ptr<BaseCell> {
        return createParameterCellForColumn(sequencer, row, col);
    };
    
    callbacks.getCellValue = [&sequencer](int row, int col, const CellGridColumnConfig& colConfig) -> float {
        const std::string& paramName = colConfig.parameterName;
        const auto& step = sequencer.getCurrentPattern()[row];
        
        // Trigger columns (index, note) - these ARE the triggers themselves
        if (paramName == "index") {
            int idx = step.index;
            return (idx < 0) ? std::numeric_limits<float>::quiet_NaN() : (float)(idx + 1);
        }
        if (paramName == "note") {
            int noteValue = step.note;
            return (noteValue < 0) ? std::numeric_limits<float>::quiet_NaN() : (float)noteValue;
        }
        
        // All other parameters are trigger-dependent - show '--' if step has no trigger
        if (step.isEmpty()) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        
        // Handle parameter-specific formatting/encoding
        if (paramName == "length") {
            return (float)step.length;
        }
        if (paramName == "chance") {
            return (float)step.chance;
        }
        if (paramName == "ratio") {
            return (float)(step.ratioA * 1000 + step.ratioB);
        }
        
        // External parameters: returns NaN if not set (displays as "--")
        if (!step.hasParameter(paramName)) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return step.getParameterValue(paramName, 0.0f);
    };
    
    callbacks.setCellValue = [&sequencer](int row, int col, float value, const CellGridColumnConfig& colConfig) {
        const std::string& paramName = colConfig.parameterName;
        Step step = sequencer.getStep(row);
        
        // Trigger columns (index, note) - handle directly
        if (paramName == "index") {
            int indexValue = (int)std::round(value);
            step.index = (indexValue == 0) ? -1 : (indexValue - 1);
        }
        // Length parameter - range is 1 to 64 (fixed maximum, can exceed pattern length)
        else if (paramName == "length") {
            const int MAX_STEP_LENGTH = 64;
            step.length = std::max(MIN_LENGTH_VALUE, std::min(MAX_STEP_LENGTH, (int)std::round(value)));
        }
        // Ratio parameter - decode from encoded value (A * 1000 + B)
        else if (paramName == "ratio") {
            if (std::isnan(value)) {
                step.ratioA = 1;
                step.ratioB = 1;
            } else {
                int encoded = (int)std::round(value);
                step.ratioA = std::max(1, std::min(16, encoded / 1000));
                step.ratioB = std::max(1, std::min(16, encoded % 1000));
                // Validate A <= B (A can't exceed B)
                if (step.ratioA > step.ratioB) {
                    step.ratioA = step.ratioB;
                }
            }
        }
        // All other parameters: use setParameterValue (handles note, chance, and external params)
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
    
    // Auto-scroll callback: determines which row should be scrolled into view
    // Priority order:
    // 1. Never auto-scroll when user is editing (typing in a cell)
    // 2. Follow user focus if user has focus in a data row
    // 3. Disable auto-scroll when user is navigating header row
    // 4. Follow playback step if sequencer is playing and user has no focus
    callbacks.getFocusedRow = [this, &sequencer]() -> int {
        // Never auto-scroll when user is editing - this interferes with typing
        if (cellFocusState.isEditing) {
            return -1;
        }
        
        // User has focus in a data row - follow user focus
        if (cellFocusState.row >= 0) {
            return cellFocusState.row;
        }
        
        // When row < 0, distinguish between header navigation and no focus
        if (cellFocusState.row < 0) {
            // Performance optimization: Cache window focus check per frame to avoid expensive ImGui calls
            int currentFrame = ImGui::GetFrameCount();
            if (cachedTableWindowFocusedFrame != currentFrame) {
                cachedTableWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
                cachedTableWindowFocusedFrame = currentFrame;
            }
            
            // Disable auto-scroll when navigating header (window focused but no cell focused)
            if (cachedTableWindowFocused || callbacksState.headerClickedThisFrame || cellFocusState.column >= 0) {
                return -1;
            }
            
            // User has no focus - allow playback auto-scroll if sequencer is playing
            if (sequencer.isPlaying()) {
                return sequencer.getPlaybackStepIndex();
            }
            return -1;
        }
        
        return -1;
    };
    
    // Focus callback for playback syncing (parameter columns only - col > 0)
    // Note: CellGrid only calls this for parameter columns, not for fixed columns (step button)
    // Step button focus is handled separately in drawStepNumber
    callbacks.onCellFocusChanged = [this, &sequencer](int row, int col) {
        int previousStep = cellFocusState.row;
        
        // Update focus state
        cellFocusState.row = row;
        cellFocusState.column = col;
        
        bool stepChanged = (previousStep != row);
        bool fromHeaderRow = (previousStep == -1);
        
        // Skip if this step was just triggered by arrow key handler in handleKeyPress
        // This prevents duplicate triggers when arrow key handler already triggered the step
        bool wasJustTriggeredByArrowKeys = (!sequencer.isPlaying() && 
                                            row == lastTriggeredStepWhenPaused && 
                                            row == sequencer.getPlaybackStep());
        
        // Prevent duplicate triggers from multiple cells in the same frame
        int currentFrame = ImGui::GetFrameCount();
        bool alreadyTriggeredThisFrame = (lastTriggeredStepFrame == currentFrame && 
                                          lastTriggeredStepThisFrame == row);
        
        // Only trigger when:
        // 1. Arriving at a step (not staying on same step)
        // 2. Not already triggered by arrow key handler
        // 3. Not already triggered this frame
        bool isArrivingAtStep = fromHeaderRow || stepChanged;
        bool shouldTrigger = isArrivingAtStep && !wasJustTriggeredByArrowKeys && !alreadyTriggeredThisFrame;
        
        if (shouldTrigger) {
            syncPlaybackToEditIfPaused(sequencer, row, stepChanged, fromHeaderRow, lastTriggeredStepWhenPaused);
            lastTriggeredStepThisFrame = row;
            lastTriggeredStepFrame = currentFrame;
        }
    };
    
    // Simple click callback for playback syncing and selection management
    callbacks.onCellClicked = [this, &sequencer](int row, int col) {
        // TrackerSequencer-specific: When paused, sync playback position and trigger step (walk through)
        int previousStep = cellFocusState.row;
        
        // Update minimal state needed for playback syncing
        cellFocusState.row = row;
        cellFocusState.column = col;
        
        // Handle selection state on click
        ImGuiIO& io = ImGui::GetIO();
        bool shiftPressed = io.KeyShift;
        
        if (shiftPressed) {
            // Shift+click: extend selection
            if (!selectionState.isSelecting) {
                selectionState.setAnchor(previousStep >= 0 ? previousStep : row);
            }
            selectionState.extendTo(row);
        } else {
            // Normal click: clear selection or set single-step selection
            if (selectionState.isSelecting) {
                selectionState.clear();
            }
            // Set anchor for potential future shift+click
            selectionState.setAnchor(row);
        }
        
        bool stepChanged = (previousStep != row);
        bool fromHeaderRow = (previousStep == -1);
        if (fromHeaderRow || stepChanged) {
            syncPlaybackToEditIfPaused(sequencer, row, stepChanged, fromHeaderRow, lastTriggeredStepWhenPaused);
        }
    };
    
    // CRITICAL: Setup onEditModeChanged callback for pattern grid cells
    // This manages ImGui keyboard navigation state when entering/exiting edit mode
    // Uses the same logic as setupStandardCellGridCallbacks to ensure consistent behavior
    callbacks.onEditModeChanged = [this](int row, int col, bool editing) {
        // #region agent log
        {
            std::ofstream log("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (log.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                log << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"G\",\"location\":\"TrackerSequencerGUI.cpp:2080\",\"message\":\"Pattern grid onEditModeChanged called\",\"data\":{\"row\":" << row << ",\"col\":" << col << ",\"editing\":" << (editing ? "true" : "false") << ",\"focusedRow\":" << cellFocusState.row << ",\"focusedCol\":" << cellFocusState.column << "},\"timestamp\":" << now << "}\n";
            }
        }
        // #endregion
        ImGuiIO& io = ImGui::GetIO();
        bool navWasEnabled = (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
        
        // CRITICAL FIX: If cellFocusState is stale (no cell focused) but we're being called for edit mode,
        // it means the cell IS focused but cellFocusState wasn't updated (e.g., when Enter is pressed
        // on an already-focused cell). Update cellFocusState to match the actual focused cell.
        // This fixes the issue where Enter disables navigation but cellFocusState shows no cell is focused.
        if (!cellFocusState.hasFocus() && row >= 0 && col >= 0) {
            // #region agent log
            {
                std::ofstream log("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (log.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    log << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"G\",\"location\":\"TrackerSequencerGUI.cpp:2090\",\"message\":\"Fixing stale cellFocusState in pattern grid\",\"data\":{\"oldRow\":" << cellFocusState.row << ",\"oldCol\":" << cellFocusState.column << ",\"newRow\":" << row << ",\"newCol\":" << col << "},\"timestamp\":" << now << "}\n";
                }
            }
            // #endregion
            cellFocusState.row = row;
            cellFocusState.column = col;
        }
        
        // Update editing state
        bool isFocusedCell = (cellFocusState.row == row && cellFocusState.column == col);
        if (isFocusedCell) {
            cellFocusState.isEditing = editing;
        }
        
        // CRITICAL FIX: Check if navigation is currently disabled (indicates we were in edit mode)
        // This fixes the issue where navigation is disabled but editing_ is reset, leaving navigation stuck
        bool navCurrentlyDisabled = !(io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard);
        
        if (editing) {
            // Disable ImGui keyboard navigation when entering edit mode
            // Only manage navigation for the focused cell to prevent incorrect state changes
            if (isFocusedCell) {
                io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
                bool navNowEnabled = (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
                // #region agent log
                {
                    std::ofstream log("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (log.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        log << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"G\",\"location\":\"TrackerSequencerGUI.cpp:2121\",\"message\":\"Pattern grid disabling navigation\",\"data\":{\"navWasEnabled\":" << (navWasEnabled ? "true" : "false") << ",\"navNowEnabled\":" << (navNowEnabled ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
                    }
                }
                // #endregion
                ofLogNotice("TrackerSequencerGUI") << "[EDIT_MODE] Pattern grid entering edit mode (row=" << row << ", col=" << col 
                                                   << ") - Navigation " << (navWasEnabled ? "was ENABLED, disabled" : "already disabled");
            }
        } else {
            // CRITICAL: Restore navigation when exiting edit mode
            // Restore navigation if:
            // 1. This is the focused cell, OR
            // 2. Navigation is currently disabled (indicating we were in edit mode, even if cellFocusState is stale)
            // This fixes the issue where navigation is disabled but editing_ is reset, leaving navigation stuck
            if (isFocusedCell || navCurrentlyDisabled) {
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                bool navNowEnabled = (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
                // #region agent log
                {
                    std::ofstream log("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (log.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        log << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"G\",\"location\":\"TrackerSequencerGUI.cpp:2140\",\"message\":\"Pattern grid re-enabling navigation\",\"data\":{\"navWasEnabled\":" << (navWasEnabled ? "true" : "false") << ",\"navNowEnabled\":" << (navNowEnabled ? "true" : "false") << ",\"isFocusedCell\":" << (isFocusedCell ? "true" : "false") << ",\"navCurrentlyDisabled\":" << (navCurrentlyDisabled ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
                    }
                }
                // #endregion
                ofLogNotice("TrackerSequencerGUI") << "[EDIT_MODE] Pattern grid exiting edit mode (row=" << row << ", col=" << col 
                                                   << ", isFocused=" << isFocusedCell << ", navWasDisabled=" << navCurrentlyDisabled
                                                   << ") - Navigation " << (navWasEnabled ? "was already enabled" : "restored")
                                                   << ", now " << (navNowEnabled ? "ENABLED" : "DISABLED");
            } else {
                // #region agent log
                {
                    std::ofstream log("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (log.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        log << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"G\",\"location\":\"TrackerSequencerGUI.cpp:2150\",\"message\":\"Pattern grid edit mode changed for non-focused cell - skipping navigation management\",\"data\":{\"row\":" << row << ",\"col\":" << col << ",\"editing\":" << (editing ? "true" : "false") << ",\"focusedRow\":" << cellFocusState.row << ",\"focusedCol\":" << cellFocusState.column << "},\"timestamp\":" << now << "}\n";
                    }
                }
                // #endregion
                ofLogVerbose("TrackerSequencerGUI") << "[EDIT_MODE] Pattern grid edit mode changed for non-focused cell (row=" << row 
                                                    << ", col=" << col << ", editing=" << editing 
                                                    << ") - Navigation state unchanged";
            }
        }
        
        // #region agent log
        {
            std::ofstream log("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (log.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                ImGuiIO& ioFinal = ImGui::GetIO();
                bool navFinal = (ioFinal.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
                log << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"G\",\"location\":\"TrackerSequencerGUI.cpp:2160\",\"message\":\"Pattern grid callback completed\",\"data\":{\"navFinal\":" << (navFinal ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
            }
        }
        // #endregion
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
// BaseCell adapter methods (moved from TrackerSequencer)
//--------------------------------------------------------------
std::unique_ptr<BaseCell> TrackerSequencerGUI::createParameterCellForColumn(TrackerSequencer& sequencer, int step, int column) {
    // column is absolute column index (0=step number, 1+=parameter columns)
    // For parameter columns, convert to parameter-relative index: paramColIdx = column - 1
    if (step < 0 || step >= sequencer.getStepCount() || column <= 0) {
        return nullptr; // Return nullptr for invalid step or step number column (column=0)
    }
    
    const auto& columnConfig = sequencer.getCurrentPattern().getColumnConfiguration();
    int paramColIdx = column - 1;  // Convert absolute column index to parameter-relative index
    if (paramColIdx < 0 || paramColIdx >= (int)columnConfig.size()) {
        return nullptr;
    }
    
    const auto& col = columnConfig[paramColIdx];
    
    // Create ParameterDescriptor based on column type
    ParameterDescriptor paramDesc;
    paramDesc.name = col.parameterName;
    paramDesc.displayName = col.parameterName;  // Use parameterName as displayName (ColumnConfig doesn't have displayName)
    
    // Set value range based on column type - use unified parameter registry
    auto trackerParams = sequencer.getTrackerParameters();
    bool isTrackerParam = false;
    for (const auto& trackerParam : trackerParams) {
        if (trackerParam.name == col.parameterName) {
            // Tracker parameter: use registry with dynamic ranges
            paramDesc.type = trackerParam.type;
            if (col.parameterName == "index") {
                // Index: allow -1 for empty state (displayed as "--")
                paramDesc.minValue = -1.0f;
                paramDesc.maxValue = trackerParam.maxValue;
                paramDesc.defaultValue = -1.0f;
            } else if (col.parameterName == "note") {
                // Note: allow -1 for empty state (displayed as "--")
                paramDesc.minValue = -1.0f;
                paramDesc.maxValue = trackerParam.maxValue;
                paramDesc.defaultValue = -1.0f;
            } else {
                paramDesc.minValue = trackerParam.minValue;
                paramDesc.maxValue = trackerParam.maxValue;
                paramDesc.defaultValue = trackerParam.defaultValue;
            }
            isTrackerParam = true;
            break;
        }
    }
    
    if (!isTrackerParam) {
        // External parameter column - use parameter ranges
        auto range = TrackerSequencer::getParameterRange(col.parameterName);
        float defaultValue = TrackerSequencer::getParameterDefault(col.parameterName);
        paramDesc.type = TrackerSequencer::getParameterType(col.parameterName);
        paramDesc.minValue = range.first;
        paramDesc.maxValue = range.second;
        paramDesc.defaultValue = defaultValue;
    }
    
    // Create ParameterCell and configure it with custom callbacks
    // Note: We don't have a Module for TrackerSequencer, so we'll use ParameterCell with custom callbacks
    ParameterCell paramCell(nullptr, paramDesc, nullptr);  // No module, no router
    
    // Set up custom getter/setter/remover callbacks
    configureParameterCellCallbacks(sequencer, &paramCell, step, column);
    
    // Create the cell
    auto cell = paramCell.createCell();
    if (cell) {
        cell->parameterName = col.parameterName;
        cell->isRemovable = !col.isRequired; // Use isRequired (inverted)
    }
    
    return cell;
}

void TrackerSequencerGUI::configureParameterCellCallbacks(TrackerSequencer& sequencer, ParameterCell* paramCell, int step, int column) {
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
    bool isRequiredCol = col.isRequired; // Capture by value
    std::string requiredTypeCol = col.isRequired ? col.parameterName : ""; // Capture by value
    
    if (!paramCell) {
        return;  // Invalid ParameterCell
    }
    
    // getCurrentValue callback - returns current value from Step
    // Returns NaN to indicate empty/not set (will display as "--")
    // Unified system: all empty values (Index, Length, dynamic parameters) use NaN
    auto getter = [&sequencer, step, paramName, isRequiredCol, requiredTypeCol]() -> float {
        if (step < 0 || step >= sequencer.getStepCount()) {
            // Return NaN for invalid step (will display as "--")
            return std::numeric_limits<float>::quiet_NaN();
        }
        
        const Step& stepData = sequencer.getCurrentPattern()[step];
        
        // Trigger columns (index, note) - these ARE the triggers themselves
        if (isRequiredCol && requiredTypeCol == "index") {
            // Index: return NaN when empty, otherwise return 1-based display value
            int idx = stepData.index;
            return (idx < 0) ? std::numeric_limits<float>::quiet_NaN() : (float)(idx + 1);
        }
        if (paramName == "note") {
            // Note column: return NaN when empty, otherwise return note value
            int noteValue = stepData.note;
            return (noteValue < 0) ? std::numeric_limits<float>::quiet_NaN() : (float)noteValue;
        }
        
        // All other parameters are trigger-dependent - show '--' if step has no trigger
        if (stepData.isEmpty()) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        
        // Handle parameter-specific formatting/encoding
        if (isRequiredCol && requiredTypeCol == "length") {
            return (float)stepData.length;
        }
        if (paramName == "chance") {
            return (float)stepData.chance;
        }
        if (paramName == "ratio") {
            return (float)(stepData.ratioA * 1000 + stepData.ratioB);
        }
        
        // External parameters: return NaN if parameter doesn't exist (will display as "--")
        // This allows parameters with negative ranges (like speed -10 to 10) to distinguish
        // between "not set" (NaN/--) and explicit values like 1.0 or -1.0
        if (!stepData.hasParameter(paramName)) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return stepData.getParameterValue(paramName, 0.0f);
    };
    paramCell->setCustomGetter(getter);
    
    // setter callback - applies value to Step
    auto setter = [&sequencer, step, paramName, isRequiredCol, requiredTypeCol](float value) {
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
            // Length: clamp to 1 to 64 (fixed maximum, can exceed pattern length)
            const int MAX_STEP_LENGTH = 64;
            stepData.length = std::max(1, std::min(MAX_STEP_LENGTH, (int)std::round(value)));
        } else if (paramName == "note") {
            // Note: store -1 for empty (NaN), otherwise store the note value
            // Use direct field access for performance (note is now a direct field)
            stepData.note = std::isnan(value) ? -1 : (int)std::round(value);
        } else if (paramName == "ratio") {
            // Ratio: decode from encoded value (A * 1000 + B)
            if (std::isnan(value)) {
                stepData.ratioA = 1;
                stepData.ratioB = 1;
            } else {
                int encoded = (int)std::round(value);
                stepData.ratioA = std::max(1, std::min(16, encoded / 1000));
                stepData.ratioB = std::max(1, std::min(16, encoded % 1000));
                // Validate A <= B (A can't exceed B)
                if (stepData.ratioA > stepData.ratioB) {
                    stepData.ratioA = stepData.ratioB;
                }
            }
        } else {
            // Dynamic parameter
            stepData.setParameterValue(paramName, value);
        }
        sequencer.setStep(step, stepData);
    };
    paramCell->setCustomSetter(setter);
    
    // remover callback - removes parameter from Step
    auto remover = [&sequencer, step, paramName, isRequiredCol, requiredTypeCol]() {
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
            // Removable parameter - remove it (or set to -1 for note, or reset ratio to 1:1)
            Step stepData = sequencer.getStep(step);
            if (paramName == "note") {
                // Use direct field access for performance (note is now a direct field)
                stepData.note = -1;
            } else if (paramName == "ratio") {
                stepData.ratioA = 1;
                stepData.ratioB = 1;
            } else {
            stepData.removeParameter(paramName);
            }
            sequencer.setStep(step, stepData);
        }
    };
    paramCell->setCustomRemover(remover);
    
    // Set custom formatters for tracker-specific formatting
    if (isRequiredCol && requiredTypeCol == "index") {
        // Index column: 1-based display (01-99), NaN = rest
        auto formatter = [](float value) -> std::string {
            if (std::isnan(value)) {
                return "--"; // Show "--" for NaN (empty/rest)
            }
            int indexVal = (int)std::round(value);
            if (indexVal <= 0) {
                return "--"; // Also handle edge case
            }
            char buf[32];
            snprintf(buf, sizeof(buf), "%02d", indexVal);
            return std::string(buf);
        };
        paramCell->setCustomFormatter(formatter);
    } else if (isRequiredCol && requiredTypeCol == "length") {
        // Length column: 1 to 64 range (fixed maximum, can exceed pattern length), formatted as "02", NaN = not set
        const int MAX_STEP_LENGTH = 64;
        auto formatter = [MAX_STEP_LENGTH](float value) -> std::string {
            if (std::isnan(value)) {
                return "--"; // Show "--" for NaN (empty/not set)
            }
            int lengthVal = (int)std::round(value);
            lengthVal = std::max(MIN_LENGTH_VALUE, std::min(MAX_STEP_LENGTH, lengthVal)); // Clamp to valid range
            char buf[32];
            snprintf(buf, sizeof(buf), "%02d", lengthVal); // Zero-padded to 2 digits
            return std::string(buf);
        };
        paramCell->setCustomFormatter(formatter);
    } else if (paramName == "note") {
        // Note column: format as note name (C4, C#5, etc.), NaN = "--"
        auto formatter = [](float value) -> std::string {
            if (std::isnan(value) || value < 0) {
                return "--";
            }
            int noteNum = (int)std::round(value);
            if (noteNum < 0 || noteNum > 127) {
                return "--";
            }
            
            // MIDI note names
            static const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            int octave = noteNum / 12;
            int note = noteNum % 12;
            
            return std::string(noteNames[note]) + std::to_string(octave);
        };
        paramCell->setCustomFormatter(formatter);
    } else if (paramName == "ratio") {
        // Ratio column: format as A:B (e.g., 2:4), NaN = "--"
        auto formatter = [](float value) -> std::string {
            if (std::isnan(value)) {
                return "--";
            }
            int encoded = (int)std::round(value);
            int ratioA = encoded / 1000;
            int ratioB = encoded % 1000;
            // Clamp to valid range (1-16)
            ratioA = std::max(1, std::min(16, ratioA));
            ratioB = std::max(1, std::min(16, ratioB));
            return std::to_string(ratioA) + ":" + std::to_string(ratioB);
        };
        paramCell->setCustomFormatter(formatter);
    } else {
        // Dynamic parameter: use TrackerSequencer's formatting
        auto formatter = [paramName](float value) -> std::string {
            return TrackerSequencer::formatParameterValue(paramName, value);
        };
        paramCell->setCustomFormatter(formatter);
    }
    
    // Set custom parsers for tracker-specific parsing
    if (isRequiredCol && requiredTypeCol == "index") {
        // Index: parse as integer, handle "--" as NaN
        auto parser = [](const std::string& str) -> float {
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
        paramCell->setCustomParser(parser);
    } else if (isRequiredCol && requiredTypeCol == "length") {
        // Length: parse as integer (1 to 64, fixed maximum, can exceed pattern length), handle "--" as NaN
        const int MAX_STEP_LENGTH = 64;
        auto parser = [MAX_STEP_LENGTH](const std::string& str) -> float {
            if (str == "--" || str.empty()) {
                return std::numeric_limits<float>::quiet_NaN();
            }
            try {
                int val = std::stoi(str);
                val = std::max(MIN_LENGTH_VALUE, std::min(MAX_STEP_LENGTH, val)); // Clamp to valid range
                return (float)val;
            } catch (...) {
                return std::numeric_limits<float>::quiet_NaN();
            }
        };
        paramCell->setCustomParser(parser);
    } else if (paramName == "note") {
        // Note: parse note name (C4, C#5, etc.) or number, handle "--" as NaN
        auto parser = [](const std::string& str) -> float {
            if (str == "--" || str.empty()) {
                return std::numeric_limits<float>::quiet_NaN();
            }
            
            // Try parsing as note name (C4, C#5, etc.)
            static const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            for (int i = 0; i < 12; i++) {
                std::string noteName = noteNames[i];
                if (str.length() > noteName.length() && str.substr(0, noteName.length()) == noteName) {
                    try {
                        int octave = std::stoi(str.substr(noteName.length()));
                        int noteNum = octave * 12 + i;
                        if (noteNum >= 0 && noteNum <= 127) {
                            return (float)noteNum;
                        }
                    } catch (...) {
                        break;
                    }
                }
            }
            
            // Fall back to parsing as number
            try {
                int val = std::stoi(str);
                if (val >= 0 && val <= 127) {
                    return (float)val;
                }
            } catch (...) {
            }
            
            return std::numeric_limits<float>::quiet_NaN();
        };
        paramCell->setCustomParser(parser);
    } else if (paramName == "ratio") {
        // Ratio: parse A:B format (e.g., "2:4"), handle "--" as NaN
        auto parser = [](const std::string& str) -> float {
            if (str == "--" || str.empty()) {
                return std::numeric_limits<float>::quiet_NaN();
            }
            
            // Parse A:B format
            size_t colonPos = str.find(':');
            if (colonPos != std::string::npos && colonPos > 0 && colonPos < str.length() - 1) {
                try {
                    int ratioA = std::stoi(str.substr(0, colonPos));
                    int ratioB = std::stoi(str.substr(colonPos + 1));
                    // Validate and clamp to 1-16 range
                    ratioA = std::max(1, std::min(16, ratioA));
                    ratioB = std::max(1, std::min(16, ratioB));
                    // Validate A <= B (A can't exceed B)
                    if (ratioA > ratioB) {
                        ratioA = ratioB; // Clamp A to B
                    }
                    // Encode as A * 1000 + B
                    return (float)(ratioA * 1000 + ratioB);
                } catch (...) {
                    return std::numeric_limits<float>::quiet_NaN();
                }
            }
            
            // Try slash format: "A/B" → "A:B"
            size_t slashPos = str.find('/');
            if (slashPos != std::string::npos && slashPos > 0 && slashPos < str.length() - 1) {
                try {
                    int ratioA = std::stoi(str.substr(0, slashPos));
                    int ratioB = std::stoi(str.substr(slashPos + 1));
                    // Validate and clamp to 1-16 range
                    ratioA = std::max(1, std::min(16, ratioA));
                    ratioB = std::max(1, std::min(16, ratioB));
                    // Validate A <= B (A can't exceed B)
                    if (ratioA > ratioB) {
                        ratioA = ratioB; // Clamp A to B
                    }
                    // Encode as A * 1000 + B
                    return (float)(ratioA * 1000 + ratioB);
                } catch (...) {
                    return std::numeric_limits<float>::quiet_NaN();
                }
            }
            
            // Fall back: try parsing as single number (treat as A:A, e.g., "4" → "4:4")
            try {
                int val = std::stoi(str);
                val = std::max(1, std::min(16, val));
                return (float)(val * 1000 + val); // A:A format
            } catch (...) {
            }
            
            return std::numeric_limits<float>::quiet_NaN();
        };
        paramCell->setCustomParser(parser);
    }
    // Dynamic parameters use ParameterCell's default parsing (expression evaluation)
    
    // Note: customAdjustValue for ratio cycling is not currently supported in BaseCell/ParameterCell
    // This functionality may need to be re-implemented in the future if needed
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
