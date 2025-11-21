#include "TrackerSequencerGUI.h"
// Note: TrackerSequencer.h is already included by TrackerSequencerGUI.h
#include "CellWidget.h"
#include "core/ModuleRegistry.h"
#include "gui/HeaderPopup.h"
#include <imgui.h>
#include "ofLog.h"
#include "gui/GUIConstants.h"
#include <cmath>  // For std::round
#include <limits>  // For std::numeric_limits
#include <set>
#include <map>

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

TrackerSequencerGUI::TrackerSequencerGUI() 
    : editStep(-1), editColumn(-1), isEditingCell(false), editBufferInitializedCache(false),
      patternDirty(true), lastNumSteps(0), lastPlaybackStep(-1), lastPatternIndex(-1), anyCellFocusedThisFrame(false) {
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
    shouldRefocusCurrentCell = false;
}

void TrackerSequencerGUI::draw(TrackerSequencer& sequencer) {
    // Legacy method: draw with direct reference (for backward compatibility)
    drawPatternChain(sequencer);
    drawTrackerStatus(sequencer);
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
    drawTrackerStatus(*sequencer);
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
    
    // Track pattern index changes to refresh column configuration
    int currentPatternIndex = sequencer.getCurrentPatternIndex();
    bool patternChanged = (currentPatternIndex != lastPatternIndex);
    if (patternChanged) {
        lastPatternIndex = currentPatternIndex;
        // Clear cell cache when pattern changes to force refresh
        cellGrid.clearCellCache();
    }
    
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
    
    // Cache edit state to avoid repeated member access
    // Use GUI's own state instead of sequencer's (GUI state moved to TrackerSequencerGUI)
    int cachedEditStep = editStep;
    int cachedEditColumn = editColumn;
    bool cachedIsEditingCell = isEditingCell;
    
    // Configure CellGrid
    cellGrid.setTableId("TrackerGrid");
    // Use SizingFixedFit for mixed column sizing (fixed + stretch columns)
    // This allows first two columns to be fixed and parameter columns to stretch proportionally
    cellGrid.setTableFlags(ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                                 ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                                 ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY);
    cellGrid.enableScrolling(true, 0.0f); // Auto-calculate height
    cellGrid.setScrollbarSize(8.0f);
    cellGrid.setCellPadding(ImVec2(2, 2));
    cellGrid.setItemSpacing(ImVec2(1, 1));
    cellGrid.enableReordering(true);
    
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
    cellGrid.setColumnConfiguration(tableColumnConfig);
    // Query external parameters from connected modules (GUI layer handles ParameterRouter dependency)
    std::vector<ParameterDescriptor> externalParams;
    ParameterRouter* router = getParameterRouter();
    ModuleRegistry* registry = getRegistry();
    if (router && registry) {
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
    }
    cellGrid.setAvailableParameters(sequencer.getAvailableParameters(externalParams));
    
    // Store header buttons per column for custom header rendering
    std::map<int, std::vector<HeaderButton>> columnHeaderButtons;
    
    // Register header buttons
    cellGrid.clearHeaderButtons();
    for (size_t i = 0; i < sequencer.getColumnConfiguration().size(); i++) {
        const auto& colConfig = sequencer.getColumnConfiguration()[i];
        int colIdx = (int)i;
        bool isRemovable = (colConfig.parameterName != "index" && colConfig.parameterName != "length");
        
        // Randomize button for all columns
        HeaderButton randomizeBtn("R", "Randomize", [&sequencer, colIdx]() {
            sequencer.randomizeColumn(colIdx);
        });
        cellGrid.registerHeaderButton(colIdx, randomizeBtn);
        columnHeaderButtons[colIdx].push_back(randomizeBtn);
        
        // Legato button for length column
        if (colConfig.parameterName == "length") {
            HeaderButton legatoBtn("L", "Legato", [&sequencer]() {
                sequencer.applyLegato();
            });
            cellGrid.registerHeaderButton(colIdx, legatoBtn);
            columnHeaderButtons[colIdx].push_back(legatoBtn);
        }
    }
    
    // Setup callbacks for CellGrid
    CellGridCallbacks callbacks;
    // Capture this to access getRegistry() in lambda
    callbacks.drawCustomHeader = [this, &sequencer, &columnHeaderButtons](int col, const CellGridColumnConfig& colConfig, ImVec2 cellStartPos, float columnWidth, float cellMinY) -> bool {
        // Only handle swap popup for removable columns
        if (colConfig.parameterName == "index" || colConfig.parameterName == "length") {
            return false; // Use default header rendering for fixed columns
        }
        
        // Draw column name (left-aligned)
        ImGui::TableHeader(colConfig.displayName.c_str());
        
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
                std::vector<ParameterDescriptor> externalParams;
                ParameterRouter* router = getParameterRouter();
                if (router && getRegistry()) {
                    // Query all INSTRUMENT modules for their parameters
                    auto instruments = getRegistry()->getModulesByType(ModuleType::INSTRUMENT);
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
                }
                
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
        std::vector<ParameterDescriptor> externalParams;
        ParameterRouter* router = getParameterRouter();
        if (router && getRegistry()) {
            // Get internal parameter names to filter them out
            auto internalParams = sequencer.getInternalParameters();
            std::set<std::string> internalParamNames;
            for (const auto& param : internalParams) {
                internalParamNames.insert(param.name);
            }
            
            // Query all INSTRUMENT modules for their parameters
            auto instruments = getRegistry()->getModulesByType(ModuleType::INSTRUMENT);
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
        }
        
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
                    totalButtonWidth += 2.0f; // Spacing between buttons
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
                    currentX += 2.0f; // Spacing between buttons
                }
            }
            
            ImGui::PopStyleVar();
        }
        
        return true; // Header was drawn by custom renderer
    };
    
    // Custom column sizing: index and length columns use fixed width, others stretch
    // This uses the setupParameterColumn callback to expose ImGui's TableSetupColumn API directly
    // See CellGrid.h for full documentation on this callback
    callbacks.setupParameterColumn = [](int colIndex, const CellGridColumnConfig& colConfig, int absoluteColIndex) -> bool {
        ImGuiTableColumnFlags flags = 0;
        float widthOrWeight = 0.0f;
        
        // Index and length columns: fixed width with reasonable defaults
        if (colConfig.parameterName == "index" || colConfig.parameterName == "length") {
            // Fixed width for 2-digit display (01-99 for index, 01-16 for length)
            flags = ImGuiTableColumnFlags_WidthFixed;
            widthOrWeight = 45.0f;
        } else {
            // Other parameter columns: stretch to fill available space
            flags = ImGuiTableColumnFlags_WidthStretch;
            widthOrWeight = 1.0f;
        }
        
        // Disable reordering for non-draggable columns
        if (!colConfig.isDraggable) {
            flags |= ImGuiTableColumnFlags_NoReorder;
        }
        
        // Call ImGui API directly for full control
        ImGui::TableSetupColumn(colConfig.displayName.c_str(), flags, widthOrWeight);
        return true; // Column was set up
    };
    callbacks.getFocusedRow = [this]() -> int {
        return editStep; // Return focused row (-1 if none)
    };
    callbacks.isCellFocused = [this](int row, int col) -> bool {
        // col is now absolute column index (0=step number, 1+=parameter columns)
        return (editStep == row && editColumn == col);
    };
    callbacks.onCellFocusChanged = [this, &sequencer](int row, int col) {
        // col is now absolute column index (0=step number, 1+=parameter columns)
        int previousStep = editStep;
        setEditCell(row, col);  // Use absolute column index directly
        anyCellFocusedThisFrame = true;
        
        // When paused, sync playback position and trigger step (walk through)
        bool stepChanged = (previousStep != row);
        bool fromHeaderRow = (previousStep == -1);
        if (fromHeaderRow || stepChanged) {
            syncPlaybackToEditIfPaused(sequencer, row, stepChanged, fromHeaderRow);
        }
    };
    callbacks.onCellClicked = [this, &sequencer](int row, int col) {
        // col is now absolute column index (0=step number, 1+=parameter columns)
        int previousStep = editStep;
        setEditCell(row, col);  // Use absolute column index directly
        setInEditMode(false);
        anyCellFocusedThisFrame = true;
        
        // When paused, sync playback position and trigger step (walk through)
        bool stepChanged = (previousStep != row);
        bool fromHeaderRow = (previousStep == -1);
        if (fromHeaderRow || stepChanged) {
            syncPlaybackToEditIfPaused(sequencer, row, stepChanged, fromHeaderRow);
        }
    };
    callbacks.createCellWidget = [&sequencer](int row, int col, const CellGridColumnConfig& colConfig) -> CellWidget {
        // Use parameter name directly from colConfig - no index conversion needed
        // createParameterCellForColumn still uses column index internally, but we pass absolute index
        // (col=0 is step number, col=1+ are parameter columns, which matches the function's 1-based expectation)
        return sequencer.createParameterCellForColumn(row, col);
    };
    callbacks.getCellValue = [&sequencer](int row, int col, const CellGridColumnConfig& colConfig) -> float {
        // Use parameter name directly from colConfig - no index conversion needed
        const std::string& paramName = colConfig.parameterName;
        const auto& cell = sequencer.getCurrentPattern()[row];
        
        // SPECIAL CASE: "index" parameter
        // - Storage: 0-based (0 = first media, -1 = rest)
        // - Display: 1-based (1 = first media, 0 = rest)
        // - Returns NaN when empty (index < 0, which represents a rest step)
        if (paramName == "index") {
            int idx = cell.index;
            return (idx < 0) ? std::numeric_limits<float>::quiet_NaN() : (float)(idx + 1);
        }
        
        // SPECIAL CASE: "length" parameter
        // - Range: 1-16
        // - Only valid when index >= 0 (not a rest step)
        // - Returns NaN when index < 0 (rest step, length is meaningless)
        if (paramName == "length") {
            if (cell.index < 0) {
                return std::numeric_limits<float>::quiet_NaN();
            }
            return (float)cell.length;
        }
        
        // Dynamic parameter: returns NaN if not set (displays as "--")
        // This allows parameters with negative ranges (like speed -10 to 10) to distinguish
        // between "not set" (NaN/--) and explicit values like 1.0 or -1.0
        if (!cell.hasParameter(paramName)) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return cell.getParameterValue(paramName, 0.0f);
    };
    callbacks.setCellValue = [&sequencer](int row, int col, float value, const CellGridColumnConfig& colConfig) {
        // Use parameter name directly from colConfig - no index conversion needed
        const std::string& paramName = colConfig.parameterName;
        auto& cell = sequencer.getCurrentPattern()[row];
        
        // SPECIAL CASE: "index" parameter
        // - Input: 1-based display value (1 = first media, 0 = rest)
        // - Storage: 0-based (0 = first media, -1 = rest)
        if (paramName == "index") {
            int indexValue = (int)std::round(value);
            cell.index = (indexValue == 0) ? -1 : (indexValue - 1);
        }
        // SPECIAL CASE: "length" parameter
        // - Range: 1-16, clamped
        // - Only valid when index >= 0 (not a rest step)
        else if (paramName == "length") {
            cell.length = std::max(1, std::min(16, (int)std::round(value)));
        }
        // Dynamic parameter: set value directly
        else {
            cell.setParameterValue(paramName, value);
        }
    };
    callbacks.onRowStart = [&sequencer, currentPlayingStep](int row, bool isPlaybackRow, bool isEditRow) {
        // Set row background colors (same logic as original drawPatternRow)
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
    // Don't set drawSpecialColumn - we want CellGrid to use default CellWidget rendering for all parameter columns
    callbacks.syncStateToCell = [this, &sequencer, cachedEditStep, cachedEditColumn, cachedIsEditingCell](int row, int col, CellWidget& cell) {
        // col is now absolute column index (0=step number, 1+=parameter columns)
        bool isSelected = (cachedEditStep == row && cachedEditColumn == col);
        cell.setSelected(isSelected);
        cell.setEditing(cachedIsEditingCell && isSelected);
        
        // Restore edit buffer cache if editing
        if (cachedIsEditingCell && isSelected) {
            cell.setEditBuffer(editBufferCache, editBufferInitializedCache);
        }
        
        // Restore drag state if this cell is being dragged
        bool isDraggingThis = (sequencer.draggingStep == row && sequencer.draggingColumn == col);
        if (isDraggingThis) {
            cell.setDragState(true, sequencer.dragStartY, sequencer.dragStartX, sequencer.lastDragValue);
        }
    };
    callbacks.syncStateFromCell = [this, &sequencer](int row, int col, const CellWidget& cell, const CellWidgetInteraction& interaction) {
        // col is now absolute column index (0=step number, 1+=parameter columns)
        bool isSelected = (editStep == row && editColumn == col);
        
        // CRITICAL: Check if cell is actually focused (including after refocus)
        // This ensures focus is maintained after Enter validation even if focusChanged is false
        bool actuallyFocused = ImGui::IsItemFocused();
        if (actuallyFocused && isSelected) {
            anyCellFocusedThisFrame = true;
        }
            
        // Sync edit mode state and edit buffer cache if editing
        if (cell.isEditingMode()) {
            setInEditMode(true);
            editBufferCache = cell.getEditBuffer();
            setEditBufferInitializedCache(cell.isEditBufferInitialized());
            anyCellFocusedThisFrame = true;
        } else if (isSelected && !cell.isEditingMode() && isEditingCell) {
            // Exited edit mode
            setInEditMode(false);
            editBufferCache.clear();
            editBufferInitializedCache = false;
            
            if (cell.shouldRefocus() && isSelected) {
                shouldRefocusCurrentCell = true;
                anyCellFocusedThisFrame = true;  // ADD THIS: Prevent focus from being cleared
            }
        }
        
        // Sync drag state to TrackerSequencer
        if (cell.getIsDragging()) {
            sequencer.draggingStep = row;
            sequencer.draggingColumn = col;  // Use absolute column index directly
            sequencer.dragStartY = cell.getDragStartY();
            sequencer.dragStartX = cell.getDragStartX();
            sequencer.lastDragValue = cell.getLastDragValue();
        } else if (sequencer.draggingStep == row && sequencer.draggingColumn == col && !cell.getIsDragging()) {
            // Drag ended
            sequencer.draggingStep = -1;
            sequencer.draggingColumn = -1;
        }
        
        // Clear refocus flag only when cell is actually focused after refocus
        // Don't clear it just because focus changed - wait until refocus succeeds
        if (shouldRefocusCurrentCell && isSelected && actuallyFocused) {
            shouldRefocusCurrentCell = false;
        }
    };
    cellGrid.setCallbacks(callbacks);
    cellGrid.enableAutoScroll(true);
    
    // Begin table (CellGrid handles ImGui::BeginTable internally)
    int numRows = sequencer.getCurrentPattern().getStepCount();
    cellGrid.beginTable(numRows, 1); // 1 fixed column (step number)
    cellGrid.setupFixedColumn(0, "##", 30.0f, false, 1.0f);
    
    // Draw headers using CellGrid (now supports fixed columns and custom header rendering)
    cellGrid.drawHeaders(1, [](int fixedColIndex) {
        // Draw fixed column header (step number column)
        if (fixedColIndex == 0) {
            ImGui::TableHeader("##");
        }
    });
    
    // Store header row Y position for row drawing
    float headerY = ImGui::GetCursorPosY() - ImGui::GetTextLineHeightWithSpacing();
        
    // Draw pattern rows using CellGrid
        pendingRowOutline.shouldDraw = false; // Reset row outline state
        anyCellFocusedThisFrame = false; // Reset focus tracking
    
    // Note: Auto-scroll is handled by CellGrid (via getFocusedRow callback)
    // Reset scroll tracking if focus is cleared (CellGrid also handles this, but we keep it for compatibility)
    if (cachedEditStep < 0 && lastEditStepForScroll >= 0) {
        lastEditStepForScroll = -1;
    }
    
        for (int step = 0; step < sequencer.getCurrentPattern().getStepCount(); step++) {
        // Draw row using CellGrid (handles TableNextRow, row background, auto-scroll, and parameter columns)
        // Fixed column (step number) is drawn via callback
        cellGrid.drawRow(step, 1, step == playbackStep, step == cachedEditStep,
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
        
        // After drawing all rows, if editStep is set but no cell was focused,
        // clear the sequencer's cell focus (focus moved to header row or elsewhere)
        // BUT: Don't clear if we just exited edit mode (shouldRefocusCurrentCell flag is set)
        // This prevents focus loss when ImGui temporarily loses focus after exiting edit mode
        // NOTE: This check happens AFTER all cells are drawn, so it won't interfere with
        // the ViewManager's empty space click handling which clears focus BEFORE drawing
        if (editStep >= 0 && !anyCellFocusedThisFrame && !isEditingCell && !shouldRefocusCurrentCell) {  // ADD: && !shouldRefocusCurrentCell
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
        
    // End table (CellGrid handles ImGui::EndTable internally)
    cellGrid.endTable();
    
    // NOTE: Auto-scrolling is handled by CellGrid when drawing the focused row
    // This ensures smooth scrolling that follows keyboard navigation
        
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
    
    // Note: CellGrid's endTable() handles popping all style vars (CellPadding, ItemSpacing, ScrollbarSize)
    
    // No parent widget state to update - cells are directly navigable
    
    ImGui::Separator();
}

void TrackerSequencerGUI::drawStepNumber(TrackerSequencer& sequencer, int step, bool isPlaybackStep,
                                         bool isPlaying, int currentPlayingStep) {
    // Note: Don't call TableNextColumn() here - CellGrid::drawRow already sets the column index
    
    // Set step number cell background to black (like column headers)
    static ImU32 stepNumberBgColor = GUIConstants::toU32(GUIConstants::Background::StepNumber);
    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, stepNumberBgColor);
    
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
                // Note: CellWidget manages its own edit buffer state
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
            static ImU32 rowOrangeOutlineColor = GUIConstants::toU32(GUIConstants::Outline::Orange);
            static ImU32 rowRedOutlineColor = GUIConstants::toU32(GUIConstants::Outline::Red);
            
            // Orange outline when in edit mode, red outline when just selected (use cached colors)
            pendingRowOutline.color = (isSelected && isEditingCell)
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
                drawList->AddRect(outlineMin, outlineMax, stepNumberOutlineColor, 0.0f, 0, 2.0f); // 2px border
            }
        }
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

