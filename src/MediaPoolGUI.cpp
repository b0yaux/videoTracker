#include "MediaPoolGUI.h"
#include "MediaPool.h"  // Includes PlayStyle enum
#include "MediaPlayer.h"
#include "CellWidget.h"
#include "Module.h"
#include "core/ModuleRegistry.h"
#include "gui/GUIConstants.h"
#include "gui/MediaPreview.h"
#include "gui/GUIManager.h"
#include <limits>

MediaPoolGUI::MediaPoolGUI() 
    : mediaPool(nullptr), waveformHeight(100.0f), parentWidgetId(0), 
      isParentWidgetFocused(false), requestFocusMoveToParentWidget(false),
      editingColumnIndex(-1), shouldFocusFirstCell(false), shouldRefocusCurrentCell(false),
      anyCellFocusedThisFrame(false) {
}

void MediaPoolGUI::setMediaPool(MediaPool& pool) {
    // Legacy method: set direct pointer (for backward compatibility)
    mediaPool = &pool;
}

MediaPool* MediaPoolGUI::getMediaPool() const {
    // If instance-aware (has registry and instanceName), use that
    ModuleRegistry* reg = ModuleGUI::getRegistry();
    std::string instanceName = ModuleGUI::getInstanceName();
    if (reg && !instanceName.empty()) {
        auto module = reg->getModule(instanceName);
        if (!module) return nullptr;
        return dynamic_cast<MediaPool*>(module.get());
    }
    
    // Fallback to legacy direct pointer (for backward compatibility)
    return mediaPool;
}

std::string MediaPoolGUI::truncateTextToWidth(const std::string& text, float maxWidth, bool showEnd, const std::string& ellipsis) {
    if (maxWidth <= 0.0f) return text;
    
    ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    if (textSize.x <= maxWidth) return text;
    
    float ellipsisWidth = ImGui::CalcTextSize(ellipsis.c_str()).x;
    float maxTextWidth = maxWidth - ellipsisWidth;
    
    if (showEnd) {
        // Truncate from start: show end of text with ellipsis prefix
        std::string result = text;
        while (result.length() > 0) {
            ImVec2 testSize = ImGui::CalcTextSize(result.c_str());
            if (testSize.x <= maxTextWidth) break;
            result = result.substr(1); // Remove first character
        }
        return ellipsis + result;
    } else {
        // Truncate from end: show start of text with ellipsis suffix
        // Quick estimate to reduce iterations for very long strings
        float avgCharWidth = textSize.x / text.length();
        int estimatedChars = (int)(maxTextWidth / avgCharWidth);
        std::string result = text.substr(0, std::max(0, estimatedChars - 1));
        
        // Refine by checking actual width (usually only 1-2 iterations needed)
        while (result.length() > 0) {
            ImVec2 testSize = ImGui::CalcTextSize(result.c_str());
            if (testSize.x <= maxTextWidth) break;
            result.pop_back();
        }
        
        return result + ellipsis;
    }
}

void MediaPoolGUI::draw() {
    // Call base class draw (handles visibility, title bar, enabled state)
    ModuleGUI::draw();
}

// Helper function to draw waveform preview in tooltip
// Now uses shared MediaPreview
void MediaPoolGUI::drawWaveformPreview(MediaPlayer* player, float width, float height) {
    MediaPreview::drawWaveformPreview(player, width, height);
}

void MediaPoolGUI::drawContent() {
    // Skip drawing when window is collapsed to avoid accessing invalid window properties
    // This is a safety check in case drawContent() is called despite the ViewManager check
    if (ImGui::IsWindowCollapsed()) {
        return;
    }
    
    // Get current MediaPool instance (handles null case)
    MediaPool* pool = getMediaPool();
    if (!pool) {
        std::string instanceName = ModuleGUI::getInstanceName();
        ImGui::Text("Instance '%s' not found", instanceName.empty() ? "unknown" : instanceName.c_str());
        // Still set up drag drop target even if pool is null
        setupDragDropTarget();
        return;
    }
    
    // Wrap content in a child window to enable drag & drop target
    // The child window acts as the drop target area
    ImGui::BeginChild("MediaPoolContent", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
    
    // Draw parameter editing section
    drawParameters();

    // Draw waveform on top
    drawWaveform(); 
    
    // Calculate space needed for bottom controls (directory controls + separators)
    // Estimate: directory controls (~button height + padding), separators
    float bottomControlsHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y + 
                                 ImGui::GetStyle().ItemSpacing.y * 2; // Separators and spacing
    
    // Get remaining space after waveform (already accounts for waveform height)
    float availableHeight = ImGui::GetContentRegionAvail().y;
    
    // Draw media list in scrollable area, reserving space for bottom controls
    // Use availableHeight minus bottom controls height, but ensure minimum size
    float mediaListHeight = availableHeight - bottomControlsHeight;
    float minMediaListHeight = ImGui::GetFrameHeight(); // Minimum height is one media line
    if (mediaListHeight < minMediaListHeight) {
        mediaListHeight = minMediaListHeight;
    }
    
    ImGui::BeginChild("MediaList", ImVec2(0, mediaListHeight), true);
    drawMediaList();
    ImGui::EndChild();
    
    drawDirectoryControls();
    
    ImGui::EndChild(); // End MediaPoolContent
    
    // Set up drag & drop target on the main window (covers entire panel)
    // Must be called after all content is drawn, like AssetLibraryGUI does
    // This ensures the yellow highlight appears and drops work properly
    setupDragDropTarget();
}

void MediaPoolGUI::drawDirectoryControls() {
    MediaPool* pool = getMediaPool();
    if (!pool) return;
    
    // Browse Directory button - opens native directory browser
    if (ImGui::Button("Browse Directory")) {
        pool->browseForDirectory();
    }
    
    ImGui::SameLine();
    std::string displayPath = pool->getDataDirectory();
    
    // Calculate available width after button (account for spacing)
    float availableWidth = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x;
    if (availableWidth > 0.0f) {
        // For directory paths, show the end (directory name) rather than the beginning
        displayPath = truncateTextToWidth(displayPath, availableWidth, true);
    }
    
    ImGui::Text("%s", displayPath.c_str());
    ImGui::Separator();
}



/// MARK: - PARAMETERS
/// @brief create a CellWidget for a given ParameterDescriptor
/// @param paramDesc 
/// @return CellWidget
CellWidget MediaPoolGUI::createCellWidgetForParameter(const ParameterDescriptor& paramDesc) {
    CellWidget cell;
    cell.parameterName = paramDesc.name;
    cell.isInteger = (paramDesc.type == ParameterType::INT);
    cell.setValueRange(paramDesc.minValue, paramDesc.maxValue, paramDesc.defaultValue);
    cell.calculateStepIncrement();
    
    // Set up getCurrentValue callback - capture mediaPool to get active player dynamically
    // This ensures we always get the current active player, not a stale reference
    cell.getCurrentValue = [this, paramDesc]() -> float {
        MediaPool* pool = getMediaPool();
        if (!pool) return std::numeric_limits<float>::quiet_NaN();
        
        auto activePlayer = pool->getActivePlayer();
        if (!activePlayer) return std::numeric_limits<float>::quiet_NaN();
        
        // Special handling for "position" parameter: show startPosition instead of playheadPosition
        // (playheadPosition is already visually displayed as the green playhead in the waveform)
        if (paramDesc.name == "position") {
            return activePlayer->startPosition.get();
        }
        
        // Use MediaPlayer's helper method for cleaner parameter access
        const auto* param = activePlayer->getFloatParameter(paramDesc.name);
        if (param) {
            return param->get();
        }
        return std::numeric_limits<float>::quiet_NaN();
    };
    
    // Set up onValueApplied callback
    cell.onValueApplied = [this, paramDesc](const std::string&, float value) {
        MediaPool* pool = getMediaPool();
        if (pool && pool->getActivePlayer()) {
            pool->setParameter(paramDesc.name, value, true);
        }
    };
    
    // Set up onValueRemoved callback: reset to default value (double-click to reset)
    cell.onValueRemoved = [this, paramDesc](const std::string&) {
        MediaPool* pool = getMediaPool();
        if (pool && pool->getActivePlayer()) {
            pool->setParameter(paramDesc.name, paramDesc.defaultValue, true);
        }
    };
    
    // Set up formatValue callback (use openFrameworks ofToString for modern C++ string formatting)
    // Unified precision: 0.001 (3 decimal places) for all float parameters
    // Special handling for loopSize: logarithmic mapping for better precision at low values (1-100ms granular range)
    if (paramDesc.name == "loopSize") {
        // Logarithmic mapping: slider value (0.0-1.0) maps to loopSize (0.001s to 10s)
        // This provides better precision at low values (1-100ms = 0.001-0.1s)
        const float MIN_LOOP_SIZE = 0.001f;  // 1ms minimum
        const float MAX_LOOP_SIZE = 10.0f;   // 10s maximum
        
        // Set cell value range to slider range (0.0-1.0) for logarithmic mapping
        // Calculate default slider value from default seconds value (1.0s)
        float defaultSeconds = 1.0f;
        float defaultSliderValue = 0.0f;
        if (defaultSeconds > MIN_LOOP_SIZE && defaultSeconds < MAX_LOOP_SIZE) {
            defaultSliderValue = std::log(defaultSeconds / MIN_LOOP_SIZE) / std::log(MAX_LOOP_SIZE / MIN_LOOP_SIZE);
        } else if (defaultSeconds >= MAX_LOOP_SIZE) {
            defaultSliderValue = 1.0f;
        }
        cell.setValueRange(0.0f, 1.0f, defaultSliderValue);
        cell.calculateStepIncrement();
        
        // Map slider value to logarithmic range
        cell.getCurrentValue = [this, paramDesc, MIN_LOOP_SIZE, MAX_LOOP_SIZE]() -> float {
            MediaPool* pool = getMediaPool();
            if (!pool) return paramDesc.defaultValue;
            
            auto activePlayer = pool->getActivePlayer();
            if (!activePlayer) return paramDesc.defaultValue;
            
            // Get actual loopSize value in seconds
            float actualValue = activePlayer->loopSize.get();
            
            // Map from linear seconds to logarithmic slider value (0.0-1.0)
            // Inverse of: value = MIN * pow(MAX/MIN, sliderValue)
            if (actualValue <= MIN_LOOP_SIZE) return 0.0f;
            if (actualValue >= MAX_LOOP_SIZE) return 1.0f;
            float sliderValue = std::log(actualValue / MIN_LOOP_SIZE) / std::log(MAX_LOOP_SIZE / MIN_LOOP_SIZE);
            return sliderValue;
        };
        
        // Map slider value back to actual seconds
        cell.onValueApplied = [this, paramDesc, MIN_LOOP_SIZE, MAX_LOOP_SIZE](const std::string&, float sliderValue) {
            MediaPool* pool = getMediaPool();
            if (!pool) {
                ofLogWarning("MediaPoolGUI") << "[CRASH PREVENTION] MediaPool is null in setValue callback for parameter: " << paramDesc.name;
                return;
            }
            
            // Clamp slider value to [0.0, 1.0]
            sliderValue = std::max(0.0f, std::min(1.0f, sliderValue));
            
            // Map from logarithmic slider value to linear seconds
            // value = MIN * pow(MAX/MIN, sliderValue)
            float actualValue = MIN_LOOP_SIZE * std::pow(MAX_LOOP_SIZE / MIN_LOOP_SIZE, sliderValue);
            
            // Clamp to actual duration if available
            auto activePlayer = pool->getActivePlayer();
            if (activePlayer) {
                float duration = activePlayer->getDuration();
                if (duration > 0.001f) {
                    actualValue = std::min(actualValue, duration);
                }
            }
            
            pool->setParameter(paramDesc.name, actualValue, true);
        };
        
        // Format display value: show actual seconds with appropriate precision
        // NOTE: No "s" suffix - keeps parsing simple and standard (no custom parseValue needed)
        cell.formatValue = [MIN_LOOP_SIZE, MAX_LOOP_SIZE](float sliderValue) -> std::string {
            // Map slider value to actual seconds for display
            sliderValue = std::max(0.0f, std::min(1.0f, sliderValue));
            float actualValue = MIN_LOOP_SIZE * std::pow(MAX_LOOP_SIZE / MIN_LOOP_SIZE, sliderValue);
            
            // Use appropriate precision based on value magnitude:
            // - 5 decimals for values < 0.01s (10ms) - granular synthesis range
            // - 4 decimals for values < 0.1s (100ms)
            // - 3 decimals for values >= 0.1s
            if (actualValue < 0.01f) {
                return ofToString(actualValue, 5);
            } else if (actualValue < 0.1f) {
                return ofToString(actualValue, 4);
            } else {
                return ofToString(actualValue, 3);
            }
        };
    } else {
        // Standard linear mapping for other parameters
        cell.formatValue = [paramDesc](float value) -> std::string {
            if (paramDesc.type == ParameterType::INT) {
                return ofToString((int)std::round(value));
            } else {
                return ofToString(value, 3); // 3 decimal places (0.001 precision) for all float parameters
            }
        };
    }
    
    return cell;
}



/// MARK: - P Descritpor
// ParameterDescriptor : Returns a vector of ParameterDescriptor objects representing editable parameters.
// Parameters named "note" are excluded from the returned vector.
std::vector<ParameterDescriptor> MediaPoolGUI::getEditableParameters() const {
    MediaPool* pool = getMediaPool();
    if (!pool) {
        ofLogWarning("MediaPoolGUI") << "[CRASH PREVENTION] MediaPool is null in getEditableParameters()";
        return std::vector<ParameterDescriptor>(); // Return empty vector
    }
    
    auto params = pool->getParameters();
    std::vector<ParameterDescriptor> editableParams;
    for (const auto& param : params) {
        if (param.name != "note") {
            editableParams.push_back(param);
        }
    }
    return editableParams;
}


void MediaPoolGUI::drawParameters() {
    MediaPool* pool = getMediaPool();
    if (!pool) return;
    
    ImGui::Separator();
    // Get available parameters from MediaPool
    auto editableParams = getEditableParameters();
    
    if (editableParams.empty()) {
        ImGui::Text("No editable parameters available");
        return;
    }
    
    // Create a focusable parent widget BEFORE the table for navigation (similar to TrackerSequencer)
    ImGui::PushID("MediaPoolParametersParent");
    
    // Handle parent widget focus requests
    if (requestFocusMoveToParentWidget) {
        ImGui::SetKeyboardFocusHere(0); // Request focus for the upcoming widget
        isParentWidgetFocused = true;
        clearCellFocus();
        requestFocusMoveToParentWidget = false;
    }
    
    // Create an invisible button as the parent widget (similar to TrackerSequencer)
    // CRITICAL: InvisibleButton requires non-zero size (ImGui assertion)
    // Use minimum size of 1x1 pixels to satisfy the requirement
    ImGui::InvisibleButton("##MediaPoolParamsParent", ImVec2(1, 1));
    
    // Handle clicks on parent widget - clear cell focus when clicked
    if (ImGui::IsItemClicked(0)) {
        clearCellFocus();
        isParentWidgetFocused = true;
    }
    
    // Check if parent widget is focused
    if (ImGui::IsItemFocused()) {
        isParentWidgetFocused = true;
    } else if (isParentWidgetFocused && !ImGui::IsAnyItemFocused()) {
        // Parent widget lost focus - update state
        isParentWidgetFocused = false;
    }
    
    parentWidgetId = ImGui::GetItemID();
    ImGui::PopID();
    
    // Note: Table styling is handled by CellGrid (CellPadding, ItemSpacing)
    
    // Reset focus tracking at start of frame
    anyCellFocusedThisFrame = false;
    
    // Use versioned table ID to reset column order if needed (change version number to force reset)
    static int tableVersion = 3; // Increment this to reset all saved column settings (v2: added STYLE column, v3: reordered polyphonyMode after playStyle)
    std::string tableId = "MediaPoolParameters_v" + std::to_string(tableVersion);
    
    // Configure CellGrid
    cellGrid.setTableId(tableId);
    cellGrid.setTableFlags(ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                                 ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                                 ImGuiTableFlags_SizingStretchProp);
    cellGrid.setCellPadding(ImVec2(2, 2));
    cellGrid.setItemSpacing(ImVec2(1, 1));
    cellGrid.enableReordering(true);
        
    // Convert editableParams to CellGrid column configuration
    // Add Index, Play style, and Polyphony mode as regular columns at the beginning
    std::vector<CellGridColumnConfig> tableColumnConfig;
    // Add Index column (not removable, not draggable)
    tableColumnConfig.push_back(CellGridColumnConfig("mediaIndex", "Index", false, 0, false));
    // Add Play style column (not removable, not draggable)
    tableColumnConfig.push_back(CellGridColumnConfig("playStyle", "Play style", false, 1, false));
    // Add Polyphony mode column (not removable, not draggable)
    tableColumnConfig.push_back(CellGridColumnConfig("polyphonyMode", "Polyphony", false, 2, false));
    // Add all editable parameters (all removable), skipping polyphonyMode since it's already added
    for (const auto& paramDesc : editableParams) {
        // Skip polyphonyMode since it's already added as a fixed column
        if (paramDesc.name == "polyphonyMode") {
            continue;
        }
        tableColumnConfig.push_back(CellGridColumnConfig(
            paramDesc.name, paramDesc.displayName, true, 0));  // isRemovable = true for all parameters
        }
    cellGrid.setColumnConfiguration(tableColumnConfig);
    cellGrid.setAvailableParameters(editableParams);
    
    // Clear special column widget cache when column configuration changes
    specialColumnWidgetCache.clear();
    
    // Setup callbacks for CellGrid
    CellGridCallbacks callbacks;
    callbacks.getFocusedRow = [this]() -> int {
        return (editingColumnIndex >= 0) ? 0 : -1; // Single row table, row 0 if focused, -1 if not
    };
    callbacks.isCellFocused = [this](int row, int col) -> bool {
        // col is now absolute column index
        return (editingColumnIndex == col);
    };
    callbacks.onCellFocusChanged = [](int row, int col) {
        // This will be handled by syncStateFromCell
    };
    callbacks.onCellClicked = [](int row, int col) {
        // This will be handled by syncStateFromCell
    };
    callbacks.createCellWidget = [this](int row, int col, const CellGridColumnConfig& colConfig) -> CellWidget {
        // Use parameter name directly from colConfig - no need to search through all parameters
        const std::string& paramName = colConfig.parameterName;
        
        // Skip button columns - they use drawSpecialColumn for direct ImGui::Button() calls (better performance)
        if (paramName == "mediaIndex" || paramName == "playStyle" || paramName == "polyphonyMode") {
            return CellWidget(); // Empty widget - won't be used
        }
        
        // Skip "note" parameter (not editable in GUI, only used internally)
        if (paramName == "note") {
            return CellWidget();
        }
        
        // Look up parameter descriptor by name
        // TODO: Could optimize with a parameter name -> ParameterDescriptor map for O(1) lookup
        auto editableParams = getEditableParameters();
        for (const auto& paramDesc : editableParams) {
            if (paramDesc.name == paramName) {
                return createCellWidgetForParameter(paramDesc);
            }
        }
        return CellWidget(); // Return empty cell if not found
    };
    callbacks.drawSpecialColumn = nullptr; // Will be set after all other callbacks are defined
    callbacks.getCellValue = [this, pool](int row, int col, const CellGridColumnConfig& colConfig) -> float {
        // Use parameter name directly from colConfig - no index conversion needed
        const std::string& paramName = colConfig.parameterName;
        
        // Handle special button columns (return 0.0f as they don't have numeric values)
        if (paramName == "mediaIndex" || paramName == "playStyle" || paramName == "polyphonyMode") {
            return 0.0f;
        }
        
        auto activePlayer = pool->getActivePlayer();
        if (!activePlayer) {
            // Fallback: look up default value from available parameters
            auto editableParams = getEditableParameters();
            for (const auto& paramDesc : editableParams) {
                if (paramDesc.name == paramName) {
                    return paramDesc.defaultValue;
                }
            }
            return 0.0f;
        }
        
        // SPECIAL CASE: "position" parameter shows startPosition instead of playheadPosition
        // (playheadPosition is visually displayed as green playhead in waveform)
        // This allows editing the start position independently from the playhead
        if (paramName == "position") {
            return activePlayer->startPosition.get();
        }
        
        // Use MediaPlayer's helper method for parameter access
        const auto* param = activePlayer->getFloatParameter(paramName);
        if (param) {
            return param->get();
        }
        
        // Fallback: look up default value from available parameters
        auto editableParams = getEditableParameters();
        for (const auto& paramDesc : editableParams) {
            if (paramDesc.name == paramName) {
                return paramDesc.defaultValue;
            }
        }
        return 0.0f;
    };
    callbacks.setCellValue = [pool](int row, int col, float value, const CellGridColumnConfig& colConfig) {
        // Use parameter name directly from colConfig - no index conversion needed
        const std::string& paramName = colConfig.parameterName;
        
        // Skip button columns (they handle their own state changes)
        if (paramName == "mediaIndex" || paramName == "playStyle" || paramName == "polyphonyMode") {
            return;
        }
        
        auto activePlayer = pool->getActivePlayer();
        if (!activePlayer) return;
        
        // Use MediaPool's unified parameter setting API
        pool->setParameter(paramName, value, true);
    };
    callbacks.onRowStart = [](int row, bool isPlaybackRow, bool isEditRow) {
        // Set row background color
        ImU32 rowBgColor = GUIConstants::toU32(GUIConstants::Background::TableRowFilled);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowBgColor);
    };
    callbacks.syncStateToCell = [this](int row, int col, CellWidget& cell) {
        // col is now absolute column index
        bool isSelected = (editingColumnIndex == col);
        cell.setSelected(isSelected);
        cell.setEditing(isEditingParameter_ && isSelected);
        
        if (isEditingParameter_ && isSelected) {
            cell.setEditBuffer(editBufferCache, editBufferInitializedCache);
        }
            
        // Restore drag state - use parameter name directly from cell (no index conversion needed)
        if (!draggingParameter.empty() && cell.parameterName == draggingParameter) {
            cell.setDragState(true, dragStartY, dragStartX, lastDragValue);
        }
    };
    callbacks.syncStateFromCell = [this](int row, int col, const CellWidget& cell, const CellWidgetInteraction& interaction) {
        // col is now absolute column index
        bool isSelected = (editingColumnIndex == col);
        
        // Use parameter name directly from cell - no index conversion needed
        const std::string& paramName = cell.parameterName;
        if (paramName.empty()) return;  // Skip if cell has no parameter name
        
        // Sync focus state
            // CRITICAL: Check if cell is actually focused (including after refocus)
            // This ensures focus is maintained after Enter validation even if focusChanged is false
            bool actuallyFocused = ImGui::IsItemFocused();
            if (interaction.focusChanged || (actuallyFocused && isSelected)) {
                int previousColumn = editingColumnIndex;
                editingParameter = paramName;
                editingColumnIndex = col;  // Use absolute column index directly
                anyCellFocusedThisFrame = true;
                isParentWidgetFocused = false;
                
                if (previousColumn != col && isEditingParameter_) {
                    isEditingParameter_ = false;
                    editBufferCache.clear();
                    editBufferInitializedCache = false;
                }
            }
            
            if (interaction.clicked) {
                editingParameter = paramName;
                editingColumnIndex = col;  // Use absolute column index directly
                isEditingParameter_ = false;
                anyCellFocusedThisFrame = true;
                isParentWidgetFocused = false;
            }
            
        // Sync drag state
        if (cell.getIsDragging()) {
                    draggingParameter = paramName;
            dragStartY = cell.getDragStartY();
            dragStartX = cell.getDragStartX();
            lastDragValue = cell.getLastDragValue();
                    // Maintain focus during drag
                    editingColumnIndex = col;  // Use absolute column index directly
                    editingParameter = paramName;
                    anyCellFocusedThisFrame = true;
        } else if (draggingParameter == paramName && !cell.getIsDragging()) {
                    draggingParameter.clear();
            }
            
            // Sync edit mode state
        if (cell.isEditingMode()) {
                isEditingParameter_ = true;
            editBufferCache = cell.getEditBuffer();
            editBufferInitializedCache = cell.isEditBufferInitialized();
            anyCellFocusedThisFrame = true;
        } else if (isEditingParameter_ && isSelected && !cell.isEditingMode()) {
                isEditingParameter_ = false;
                editBufferCache.clear();
                editBufferInitializedCache = false;
            
            // Check if cell needs refocus (signaled via interaction.needsRefocus)
            if (interaction.needsRefocus && isSelected) {
                shouldRefocusCurrentCell = true;
                anyCellFocusedThisFrame = true;  // ADD THIS: Prevent focus from being cleared
            }
            }
            
        // Clear refocus flag only when cell is actually focused after refocus
        // Don't clear it just because focus changed - wait until refocus succeeds
        if (shouldRefocusCurrentCell && isSelected && ImGui::IsItemFocused()) {
                shouldRefocusCurrentCell = false;
            }
    };
    // Track if header was clicked to clear button cell focus
    bool headerClickedThisFrame = false;
    
    // Setup header click callback to detect when headers are clicked
    callbacks.onHeaderClicked = [&headerClickedThisFrame](int col) {
        headerClickedThisFrame = true;
    };
    
    // Setup custom header rendering callback for Position parameter (scan mode button only, no popup)
    callbacks.drawCustomHeader = [this, pool, &headerClickedThisFrame](int col, const CellGridColumnConfig& colConfig, ImVec2 cellStartPos, float columnWidth, float cellMinY) -> bool {
        if (colConfig.parameterName == "position") {
            // Draw column name first (standard header)
            ImGui::TableHeader(colConfig.displayName.c_str());
            
            // Check if header was clicked
            if (ImGui::IsItemClicked(0)) {
                headerClickedThisFrame = true;
            }
            
            // Draw scan mode selector button (right-aligned in header)
            if (pool) {
                drawPositionScanModeButton(cellStartPos, columnWidth, cellMinY);
            }
            return true; // Header was drawn by custom callback
        } else {
            // Default header for other parameters (use CellGrid's default rendering)
            // Header click detection happens in CellGrid via onHeaderClicked callback
            return false; // Let CellGrid handle default rendering
        }
        };
    
    // Now set drawSpecialColumn after all other callbacks are defined
    // Capture callbacks by value to avoid dangling references
    auto getCellValueCallback = callbacks.getCellValue;
    auto setCellValueCallback = callbacks.setCellValue;
    auto createCellWidgetCallback = callbacks.createCellWidget;
    auto isCellFocusedCallback = callbacks.isCellFocused;
    auto syncStateToCellCallback = callbacks.syncStateToCell;
    auto syncStateFromCellCallback = callbacks.syncStateFromCell;
    auto onCellFocusChangedCallback = callbacks.onCellFocusChanged;
    auto onCellClickedCallback = callbacks.onCellClicked;
    
    callbacks.drawSpecialColumn = [this, pool, getCellValueCallback, setCellValueCallback, 
                                    createCellWidgetCallback, isCellFocusedCallback,
                                    syncStateToCellCallback, syncStateFromCellCallback,
                                    onCellFocusChangedCallback, onCellClickedCallback]
                                    (int row, int col, const CellGridColumnConfig& colConfig) {
        const std::string& paramName = colConfig.parameterName;
        
        // Only handle button columns here - for other columns, we need to manually render CellWidget
        // because when drawSpecialColumn is set, CellGrid uses it exclusively and doesn't fall back
        if (paramName != "mediaIndex" && paramName != "playStyle" && paramName != "polyphonyMode") {
            // Not a button column - manually render CellWidget (replicating CellGrid's default behavior)
            // Get focus state
            bool isFocused = (editingColumnIndex == col);
            if (!isFocused && isCellFocusedCallback) {
                isFocused = isCellFocusedCallback(row, col);
            }
            
            // Get or create cell widget from cache
            auto key = std::make_pair(row, col);
            auto it = specialColumnWidgetCache.find(key);
            if (it == specialColumnWidgetCache.end()) {
                // Create new widget using callback
                if (createCellWidgetCallback) {
                    CellWidget newCell = createCellWidgetCallback(row, col, colConfig);
                    specialColumnWidgetCache[key] = std::move(newCell);
                    it = specialColumnWidgetCache.find(key);
                } else {
                    // No callback - create empty widget
                    specialColumnWidgetCache[key] = CellWidget();
                    it = specialColumnWidgetCache.find(key);
                }
            }
            CellWidget& cell = it->second;
            
            // Set up callbacks on first creation
            if (!cell.getCurrentValue && getCellValueCallback) {
                int rowCapture = row;
                int colCapture = col;
                const CellGridColumnConfig colConfigCapture = colConfig;
                cell.getCurrentValue = [rowCapture, colCapture, colConfigCapture, getCellValueCallback]() -> float {
                    return getCellValueCallback(rowCapture, colCapture, colConfigCapture);
                };
            }
            
            if (!cell.onValueApplied && setCellValueCallback) {
                int rowCapture = row;
                int colCapture = col;
                const CellGridColumnConfig colConfigCapture = colConfig;
                cell.onValueApplied = [rowCapture, colCapture, colConfigCapture, setCellValueCallback](const std::string&, float value) {
                    setCellValueCallback(rowCapture, colCapture, value, colConfigCapture);
                };
            }
            
            // Sync state TO cell
            cell.setSelected(isFocused);
            cell.setEditing(isEditingParameter_ && isFocused);
            
            if (isEditingParameter_ && isFocused) {
                cell.setEditBuffer(editBufferCache, editBufferInitializedCache);
            }
            
            // Allow callback to override state
            if (syncStateToCellCallback) {
                syncStateToCellCallback(row, col, cell);
            }
            
            // Draw cell
            int uniqueId = row * 1000 + col;
            bool shouldRefocus = shouldRefocusCurrentCell && isFocused;
            int currentFrame = ofGetFrameNum();
            CellWidgetInputContext inputContext;
            
            CellWidgetInteraction interaction = cell.draw(uniqueId, isFocused, false, shouldRefocus, inputContext);
            
            // Handle interactions
            bool actuallyFocused = ImGui::IsItemFocused();
            
            if (interaction.focusChanged) {
                if (actuallyFocused) {
                    editingColumnIndex = col;
                    editingParameter = paramName;
                    anyCellFocusedThisFrame = true;
                } else if (editingColumnIndex == col) {
                    clearCellFocus();
                }
                
                if (onCellFocusChangedCallback) {
                    onCellFocusChangedCallback(row, col);
                }
            }
            
            if (interaction.clicked) {
                editingColumnIndex = col;
                editingParameter = paramName;
                if (onCellClickedCallback) {
                    onCellClickedCallback(row, col);
                }
            }
            
            isFocused = actuallyFocused;
            
            // Sync state FROM cell
            if (cell.isEditingMode()) {
                isEditingParameter_ = true;
                editBufferCache = cell.getEditBuffer();
                editBufferInitializedCache = cell.isEditBufferInitialized();
                anyCellFocusedThisFrame = true;
            } else if (isEditingParameter_ && isFocused && !cell.isEditingMode()) {
                isEditingParameter_ = false;
                editBufferCache.clear();
                editBufferInitializedCache = false;
                
                // Check if cell needs refocus (signaled via interaction.needsRefocus)
                if (interaction.needsRefocus) {
                    shouldRefocusCurrentCell = true;
                }
            }
            
            // Allow callback to handle additional state sync
            if (syncStateFromCellCallback) {
                syncStateFromCellCallback(row, col, cell, interaction);
            }
            
            return; // Done rendering CellWidget for this column
        }
        
        // Button columns: use direct ImGui::Button() calls
        // Set cell background to match step number button style
        static ImU32 buttonCellBgColor = GUIConstants::toU32(GUIConstants::Background::StepNumber);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, buttonCellBgColor);
        
        if (paramName == "mediaIndex") {
            // Draw media index button (like step number button)
            size_t currentIndex = pool->getCurrentIndex();
            size_t numPlayers = pool->getNumPlayers();
            
            char indexBuf[8];
            if (numPlayers > 0) {
                snprintf(indexBuf, sizeof(indexBuf), "%02d", (int)(currentIndex + 1));
            } else {
                snprintf(indexBuf, sizeof(indexBuf), "--");
            }
            
            // Check if active and playing
            bool isActive = false;
            auto activePlayer = pool->getActivePlayer();
            if (activePlayer != nullptr && currentIndex < numPlayers) {
                auto currentPlayer = pool->getMediaPlayer(currentIndex);
                if (currentPlayer == activePlayer) {
                    isActive = (pool->isManualPreview() || pool->isSequencerActive()) 
                              && currentPlayer->isPlaying();
                }
            }
            
            // Apply active state styling
            if (isActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, GUIConstants::Active::StepButton);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GUIConstants::Active::StepButtonHover);
            }
            
            ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
            
            if (ImGui::Button(indexBuf, ImVec2(-1, 0))) {
                // Toggle playback (same logic as CellWidget version)
                if (numPlayers == 0) return;
                
                auto currentPlayer = pool->getMediaPlayer(currentIndex);
                if (!currentPlayer) return;
                
                // Only toggle manual preview - don't interfere with sequencer playback
                if (pool->isManualPreview()) {
                    currentPlayer->stop();
                    pool->setModeIdle();
                } else if (pool->isIdle()) {
                    // Start manual preview - position determined automatically based on speed
                    pool->playMediaManual(currentIndex);
                }
            }
            
            ImGui::PopItemFlag();
            
            if (isActive) {
                ImGui::PopStyleColor(2);
            }
            
        } else if (paramName == "playStyle") {
            // Draw play style button (cycles ONCE/LOOP/NEXT)
            PlayStyle currentStyle = pool->getPlayStyle();
            const char* styleLabel;
            switch (currentStyle) {
                case PlayStyle::ONCE: styleLabel = "ONCE"; break;
                case PlayStyle::LOOP: styleLabel = "LOOP"; break;
                case PlayStyle::NEXT: styleLabel = "NEXT"; break;
                default: styleLabel = "ONCE"; break;
            }
            
            ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
            
            if (ImGui::Button(styleLabel, ImVec2(-1, 0))) {
                // Cycle play style
                PlayStyle nextStyle;
                switch (currentStyle) {
                    case PlayStyle::ONCE: nextStyle = PlayStyle::LOOP; break;
                    case PlayStyle::LOOP: nextStyle = PlayStyle::NEXT; break;
                    case PlayStyle::NEXT: nextStyle = PlayStyle::ONCE; break;
                }
                pool->setPlayStyle(nextStyle);
            }
            
            ImGui::PopItemFlag();
            
            if (ImGui::IsItemHovered()) {
                const char* tooltip;
                switch (currentStyle) {
                    case PlayStyle::ONCE:
                        tooltip = "Play Style: ONCE\nClick to cycle: ONCE → LOOP → NEXT";
                        break;
                    case PlayStyle::LOOP:
                        tooltip = "Play Style: LOOP\nClick to cycle: LOOP → NEXT → ONCE";
                        break;
                    case PlayStyle::NEXT:
                        tooltip = "Play Style: NEXT\nClick to cycle: NEXT → ONCE → LOOP";
                        break;
                    default:
                        tooltip = "Play Style: ONCE\nClick to cycle: ONCE → LOOP → NEXT";
                        break;
                }
                ImGui::SetTooltip("%s", tooltip);
            }
            
        } else if (paramName == "polyphonyMode") {
            // Draw polyphony mode button (toggles MONO/POLY)
            PolyphonyMode currentMode = pool->getPolyphonyMode();
            const char* modeLabel = (currentMode == PolyphonyMode::POLYPHONIC) ? "POLY" : "MONO";
            const char* tooltipText = (currentMode == PolyphonyMode::POLYPHONIC) 
                ? "POLYPHONIC\nswitch to MONOPHONIC ?"
                : "MONOPHONIC\nswitch to POLYPHONIC ?";
            
            ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
            
            if (ImGui::Button(modeLabel, ImVec2(-1, 0))) {
                // Toggle polyphony mode
                float newValue = (currentMode == PolyphonyMode::MONOPHONIC) ? 1.0f : 0.0f;
                pool->setParameter("polyphonyMode", newValue, true);
            }
            
            ImGui::PopItemFlag();
            
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tooltipText);
            }
        }
    };
    
    cellGrid.setCallbacks(callbacks);
    
    // Begin table (single row, no fixed columns)
    cellGrid.beginTable(1, 0); // 1 row, 0 fixed columns
    
    // Draw headers (handled by CellGrid automatically)
    // Header click detection happens in the custom header callback above
    cellGrid.drawHeaders(0, nullptr);
    
    // Draw single row (handled by CellGrid automatically)
    cellGrid.drawRow(0, 0, false, false, nullptr);
        
        // Clear shouldFocusFirstCell flag after drawing
        if (shouldFocusFirstCell) {
            shouldFocusFirstCell = false;
        }
        
        // Clear focus if:
        // 1. Header was clicked (navigating to header row)
        // 2. No cell was focused this frame AND we had a column focused (clicked outside grid)
        // 3. CRITICAL: Don't clear focus if we're dragging - this prevents focus loss during smooth dragging
        if (headerClickedThisFrame || 
            (editingColumnIndex >= 0 && !anyCellFocusedThisFrame && !isEditingParameter_ && !shouldRefocusCurrentCell && draggingParameter.empty())) {
            clearCellFocus();
        }
        
    // End table
    cellGrid.endTable();
    
    // Check for clicks outside the grid (after table ends)
    // This handles clicks on empty space within the window
    if (editingColumnIndex >= 0 && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
        clearCellFocus();
    }
}

void MediaPoolGUI::clearCellFocus() {
    editingColumnIndex = -1;
    editingParameter.clear();
    isEditingParameter_ = false;
    editBufferCache.clear();
    editBufferInitializedCache = false;
    draggingParameter.clear();
}

// Sync edit state from ImGui focus - called from InputRouter when keys are pressed
void MediaPoolGUI::syncEditStateFromImGuiFocus(MediaPoolGUI& gui) {
    // Check if editingColumnIndex is already valid (GUI sync already happened)
    if (gui.editingColumnIndex >= 0) {
        // If editingParameter is empty but editingColumnIndex is set, look it up from column config
        if (gui.editingParameter.empty() && gui.mediaPool) {
            // Get column configuration from CellGrid
            auto columnConfig = gui.cellGrid.getColumnConfiguration();
            if (gui.editingColumnIndex >= 0 && (size_t)gui.editingColumnIndex < columnConfig.size()) {
                gui.editingParameter = columnConfig[gui.editingColumnIndex].parameterName;
            }
        }
        return; // Already synced
    }
    
    // GUI draw sync should handle this every frame
    // If not set, handleKeyPress will default gracefully
}



// Button widget creation functions removed - buttons are drawn directly via drawSpecialColumn
// using ImGui::Button() for better performance (see drawSpecialColumn callback)

/// MARK: - MEDIA LIST
/// @brief draw the media list
/// @return void
void MediaPoolGUI::drawMediaList() {
    // Create a focusable parent widget BEFORE the list for navigation
    // This widget can receive focus when exiting the list via Ctrl+Enter or UP key on first item
    ImGui::PushID("MediaListParent");
    
    // Following ImGui pattern: SetKeyboardFocusHere(0) BEFORE creating widget to request focus
    if (requestFocusMoveToParentWidget) {
        ImGui::SetKeyboardFocusHere(0); // Request focus for the upcoming widget
        // Set flag immediately so InputRouter can see it in the same frame
        // (SetKeyboardFocusHere takes effect next frame, but we want InputRouter to know now)
        isParentWidgetFocused = true;
    }
    
    // Create an invisible, focusable button that acts as the parent widget
    // This allows us to move focus here when exiting the list
    // Arrow keys will navigate to other widgets in the panel when this is focused
    ImGui::InvisibleButton("##MediaListParent", ImVec2(100, 5));
    parentWidgetId = ImGui::GetItemID(); // Store ID for potential future use
    
    // Following ImGui pattern: SetItemDefaultFocus() AFTER creating widget to mark as default
    if (requestFocusMoveToParentWidget) {
        ImGui::SetItemDefaultFocus(); // Mark this widget as the default focus
        requestFocusMoveToParentWidget = false; // Clear flag after using it
    }
    
    // Check if parent widget is focused right after creating it (ImGui pattern: IsItemFocused() works for last item)
    // This updates the state if focus has already moved (e.g., from previous frame's request)
    if (!isParentWidgetFocused) {
        // Only check if we didn't just set it above (to avoid overwriting)
        isParentWidgetFocused = ImGui::IsItemFocused();
    }
    

    ImGui::PopID();
    
    // Track if any list item is focused (to update parent widget focus state)
    bool anyListItemFocused = false;
    
    MediaPool* pool = getMediaPool();
    if (!pool) return;
    
    // Get current index for auto-scrolling
    size_t currentIndex = pool->getCurrentIndex();
    
    // Track if index changed to determine if we should sync scroll
    bool shouldSyncScroll = (currentIndex != previousMediaIndex);
    
    // Show indexed media list with actual file names
    size_t numPlayers = pool->getNumPlayers();
    if (numPlayers > 0) {
        auto playerNames = pool->getPlayerNames();
        auto playerFileNames = pool->getPlayerFileNames();
        
        // Log list iteration for troubleshooting
        ofLogVerbose("MediaPoolGUI") << "[drawMediaList] Iterating " << numPlayers << " players "
                                     << "(playerNames.size()=" << playerNames.size() 
                                     << ", playerFileNames.size()=" << playerFileNames.size() << ")";
        
        for (size_t i = 0; i < playerNames.size(); i++) {
            // Validate index before accessing player
            if (i >= numPlayers) {
                ofLogWarning("MediaPoolGUI") << "[drawMediaList] Index " << i << " >= numPlayers " << numPlayers 
                                            << " - skipping invalid index";
                continue;
            }
            
            auto player = pool->getMediaPlayer(i);
            if (player) {
                // Check if this is the currently active player
                bool isActive = (pool->getActivePlayer() == player);
                bool isPlaying = (player->isPlaying());
                
                // Create clean display format: [01] [AV] Title
                std::string indexStr = "[" + std::to_string(i + 1); // Start at 01, not 00
                if (indexStr.length() < 3) {
                    indexStr = "[" + std::string(2 - (indexStr.length() - 1), '0') + std::to_string(i + 1);
                }
                indexStr += "]";
                
                // Get media type badge
                std::string mediaType = "";
                if (player->isAudioLoaded() && player->isVideoLoaded()) {
                    mediaType = "[AV]";
                } else if (player->isAudioLoaded()) {
                    mediaType = "[A]";
                } else if (player->isVideoLoaded()) {
                    mediaType = "[V]";
                } else {
                    mediaType = "--";
                }
                
                // Get clean title from file names
                std::string title = "";
                if (i < playerFileNames.size() && !playerFileNames[i].empty()) {
                    // Use the file name as title, remove extension
                    title = ofFilePath::getBaseName(playerFileNames[i]);
                } else {
                    // Fallback to player name if no file name
                    title = playerNames[i];
                }
                
                // Truncate title if too long for available width
                float availableWidth = ImGui::GetContentRegionAvail().x;
                if (availableWidth > 0.0f) {
                    // Calculate width of prefix: "[01] [AV] "
                    std::string prefix = indexStr + " " + mediaType + " ";
                    float prefixWidth = ImGui::CalcTextSize(prefix.c_str()).x;
                    float maxTitleWidth = availableWidth - prefixWidth - 20.0f; // Reserve padding
                    
                    if (maxTitleWidth > 0.0f) {
                        title = truncateTextToWidth(title, maxTitleWidth);
                    }
                }
                
                // Clean display name: [01] [AV] Title
                std::string displayName = indexStr + " " + mediaType + " " + title;
                
                // Visual styling for active and playing media
                if (isActive) {
                    ImGui::PushStyleColor(ImGuiCol_Header, GUIConstants::Active::MediaItem);
                }
                if (isPlaying) {
                    ImGui::PushStyleColor(ImGuiCol_Text, GUIConstants::Text::Playing);
                }
                
                // Make items selectable and clickable
                if (ImGui::Selectable(displayName.c_str(), isActive)) {
                    // CRITICAL: Re-check pool and validate index before calling playMediaManual
                    // The pool could become null or players list could change between iteration and click
                    MediaPool* clickedPool = getMediaPool();
                    if (!clickedPool) {
                        ofLogError("MediaPoolGUI") << "[CRASH PREVENTION] MediaPool became null when clicking asset at index " << i;
                        // Cannot proceed - skip playback attempt
                    } else {
                        // Validate index is still within bounds
                        size_t numPlayers = clickedPool->getNumPlayers();
                        if (i >= numPlayers) {
                            ofLogError("MediaPoolGUI") << "[CRASH PREVENTION] Index " << i << " out of bounds (numPlayers: " << numPlayers << ") when clicking asset";
                            // Index invalid - skip playback attempt
                        } else {
                            // Validate player still exists at this index
                            auto clickedPlayer = clickedPool->getMediaPlayer(i);
                            if (!clickedPlayer) {
                                ofLogError("MediaPoolGUI") << "[CRASH PREVENTION] Player at index " << i << " is null when clicking asset";
                                // Player invalid - skip playback attempt
                            } else {
                                // Log click attempt for troubleshooting
                                ofLogNotice("MediaPoolGUI") << "[ASSET_CLICK] Clicked asset at index " << i 
                                                            << " (displayName: " << displayName 
                                                            << ", numPlayers: " << numPlayers 
                                                            << ", player valid: " << (clickedPlayer != nullptr) << ")";
                                
                                // Use playMediaManual - automatically determines start position based on speed
                                bool success = clickedPool->playMediaManual(i);
                                if (!success) {
                                    ofLogWarning("MediaPoolGUI") << "[ASSET_CLICK] Failed to play media at index " << i;
                                } else {
                                    ofLogNotice("MediaPoolGUI") << "[ASSET_CLICK] Successfully started playback for index " << i;
                                }
                            }
                        }
                    }
                }
                
                // Auto-scroll to current playing media at top of list
                // Only sync scroll when media index changes (allows user scrolling otherwise)
                if (i == currentIndex && shouldSyncScroll) {
                    ImGui::SetScrollHereY(0.0f); // 0.0 = top of visible area
                }
                
                // Track if any list item is focused
                if (ImGui::IsItemFocused()) {
                    anyListItemFocused = true;
                }
                
                // Add hover tooltip with video frame preview and/or audio waveform
                if (ImGui::IsItemHovered()) {
                    // Use shared preview utility
                    MediaPreview::drawMediaTooltip(player, static_cast<int>(i));
                }
                
                // Add right-click context menu
                if (ImGui::BeginPopupContextItem(("MediaContext" + std::to_string(i)).c_str())) {
                    ImGui::Text("Media %zu", i);
                    ImGui::Separator();
                    
                    


                    
                    ImGui::Separator();
                    
                    if (ImGui::MenuItem("Unload Media")) {
                        MediaPool* pool = getMediaPool();
                        if (pool) {
                            pool->removePlayer(i);
                        }
                    }
                    
                    ImGui::EndPopup();
                }
                
                // Pop style colors if we pushed them
                if (isPlaying) {
                    ImGui::PopStyleColor();
                }
                if (isActive) {
                    ImGui::PopStyleColor();
                }
                
                // Status indicators are now included in the main display name
            }
        }
    } else {
        // Show message when no media files are loaded
        ImGui::TextDisabled("No media files loaded");
        ImGui::TextDisabled("Drag files here or use 'Browse Directory' to add media");
    }
    ImGui::Separator();
    
    // Update previous index after processing list (for scroll sync on next frame)
    previousMediaIndex = currentIndex;
    
    // CRITICAL: Update parent widget focus state AFTER the list ends
    // Following ImGui pattern: We can't check IsItemFocused() for a widget created earlier,
    // so we infer the state based on what we know:
    // - If any list item was focused, parent widget is definitely not focused
    // - If no list item is focused, we might be on parent widget
    // - The state checked right after creating the button is still valid if no items were focused
    if (anyListItemFocused) {
        // A list item is focused, so parent widget is definitely not focused
        isParentWidgetFocused = false;
    }
    // Otherwise, keep the state we checked right after creating the button
    // This follows ImGui's pattern: IsItemFocused() is only valid for the last item,
    // so we rely on the state we captured when the widget was created

}

/// MARK: - WAVEFORM
/// @brief draw waveform for the active player
void MediaPoolGUI::drawWaveform() {
    auto currentPlayer = getMediaPool()->getActivePlayer();
    
    // Get current media index for per-index zoom state
    size_t currentIndex = getMediaPool()->getCurrentIndex();
    auto zoomState = getWaveformZoomState(currentIndex);
    float waveformZoom = zoomState.first;
    float waveformOffset = zoomState.second;
    
    // Create invisible button for interaction area
    // CRITICAL: Ensure non-zero size for InvisibleButton (ImGui assertion requirement)
    // This can fail during initial window setup before layout is complete
    float safeHeight = std::max(waveformHeight, 1.0f);
    float availableWidth = ImGui::GetContentRegionAvail().x;
    
    // Ensure both dimensions are positive (required by ImGui)
    // Use fallback values if window isn't ready yet
    if (availableWidth <= 0.0f) {
        availableWidth = 100.0f; // Fallback minimum width
    }
    // safeHeight is already guaranteed to be >= 1.0f
    
    ImVec2 canvasSize = ImVec2(availableWidth, safeHeight);
    ImGui::InvisibleButton("waveform_canvas", canvasSize);
    
    // Get draw list for custom rendering
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetItemRectMin();
    ImVec2 canvasMax = ImGui::GetItemRectMax();
    float canvasWidth = canvasMax.x - canvasPos.x;
    float canvasHeight = canvasMax.y - canvasPos.y;
    float centerY = canvasPos.y + canvasHeight * 0.5f;
    
    // Always draw the background rectangle
    ImU32 bgColor = GUIConstants::toIM_COL32(GUIConstants::Background::Waveform);
    drawList->AddRectFilled(canvasPos, canvasMax, bgColor);
    
    // If no player, show message and return early
    if (!currentPlayer) {
        // Draw message centered in the waveform rectangle
        const char* message = "No active player to display waveform.";
        ImVec2 textSize = ImGui::CalcTextSize(message);
        ImVec2 textPos = ImVec2(
            canvasPos.x + (canvasWidth - textSize.x) * 0.5f,
            canvasPos.y + (canvasHeight - textSize.y) * 0.5f
        );
        drawList->AddText(textPos, GUIConstants::toIM_COL32(GUIConstants::Text::Disabled), message);
        return;
    }
    
    // CRITICAL: Check if dragging a CellWidget - prevents interference with waveform interactions
    bool isDraggingParameter = !draggingParameter.empty();
    
    // Handle zoom and pan interactions
    if (ImGui::IsItemHovered() && !isDraggingParameter) {
        // Mouse wheel for zoom (centered on mouse position)
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            // Get mouse position relative to canvas
            ImVec2 mousePos = ImGui::GetMousePos();
            float mouseX = mousePos.x - canvasPos.x;
            float mouseTime = mouseX / canvasWidth; // Time position under mouse (0-1)
            
            // Calculate visible time range
            float visibleRange = 1.0f / waveformZoom;
            float visibleStart = waveformOffset;
            float mouseTimeAbsolute = visibleStart + mouseTime * visibleRange;
            
            // Zoom factor (1.2x per scroll step)
            float zoomFactor = (wheel > 0.0f) ? 1.2f : 1.0f / 1.2f;
            float newZoom = waveformZoom * zoomFactor;
            newZoom = std::max(1.0f, std::min(100.0f, newZoom)); // Clamp zoom
            
            // Calculate new offset to keep mouse position fixed
            float newVisibleRange = 1.0f / newZoom;
            float newOffset = mouseTimeAbsolute - mouseTime * newVisibleRange;
            newOffset = std::max(0.0f, std::min(1.0f - newVisibleRange, newOffset));
            
            // Store updated zoom state for current index
            setWaveformZoomState(currentIndex, newZoom, newOffset);
            waveformZoom = newZoom;
            waveformOffset = newOffset;
        }
        
        // Handle panning with middle mouse or Shift+drag (only if not dragging a marker or CellWidget)
        bool isPanning = false;
        if (draggingMarker == WaveformMarker::NONE) {
            isPanning = ImGui::IsMouseDown(2) || (ImGui::IsMouseDragging(0) && ImGui::GetIO().KeyShift);
        }
        if (isPanning) {
            ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGui::IsMouseDown(2) ? 2 : 0);
            if (dragDelta.x != 0.0f) {
                float visibleRange = 1.0f / waveformZoom;
                
                // Pan by drag distance (normalized to time range)
                float panDelta = -dragDelta.x / canvasWidth * visibleRange;
                float newOffset = waveformOffset + panDelta;
                newOffset = std::max(0.0f, std::min(1.0f - visibleRange, newOffset));
                
                // Store updated offset for current index
                setWaveformZoomState(currentIndex, waveformZoom, newOffset);
                waveformOffset = newOffset;
                
                ImGui::ResetMouseDragDelta(ImGui::IsMouseDown(2) ? 2 : 0);
            }
        }
        
        // Double-click to reset zoom
        if (ImGui::IsMouseDoubleClicked(0)) {
            setWaveformZoomState(currentIndex, 1.0f, 0.0f);
            waveformZoom = 1.0f;
            waveformOffset = 0.0f;
        }
    }
    
    // Calculate visible time range
    float visibleRange = 1.0f / waveformZoom;
    float visibleStart = waveformOffset;
    
    // Check if we have audio data to draw waveform
    bool hasAudioData = false;
    int numChannels = 0;
    int actualPoints = 0;
    static std::vector<float> timeData;
    static std::vector<std::vector<float>> channelData;
    
    if (currentPlayer->isAudioLoaded()) {
        // Get audio buffer data
        ofSoundBuffer buffer = currentPlayer->getAudioPlayer().getBuffer();
        int numFrames = buffer.getNumFrames();
        numChannels = buffer.getNumChannels();
        
        if (numFrames > 0 && numChannels > 0) {
            hasAudioData = true;
            
            // Calculate how many points we need for visible range
            // When zoomed out (visibleRange = 1.0): use base precision (MAX_WAVEFORM_POINTS)
            // When zoomed in (visibleRange < 1.0): increase precision proportionally to zoom level
            // This ensures better detail when zoomed in while maintaining performance when zoomed out
            // Formula: base points for visible range * (1 + zoom bonus)
            // The zoom bonus increases precision when zoomed in, allowing more points per visible range
            float zoomLevel = 1.0f / visibleRange; // Convert visibleRange back to zoom level (1.0 = no zoom, 10.0 = 10x zoom)
            float zoomPrecisionBonus = (zoomLevel - 1.0f) * ZOOM_PRECISION_MULTIPLIER; // Bonus precision when zoomed in
            float precisionMultiplier = 1.0f + zoomPrecisionBonus;
            // Base points for visible range, multiplied by precision to get more detail when zoomed
            int maxPoints = (int)(MAX_WAVEFORM_POINTS * visibleRange * precisionMultiplier);
            maxPoints = std::max(MIN_WAVEFORM_POINTS, std::min(MAX_WAVEFORM_POINTS, maxPoints));
            
            int stepSize = std::max(1, numFrames / maxPoints);
            actualPoints = std::min(maxPoints, numFrames / stepSize);
            
            timeData.resize(actualPoints);
            channelData.resize(numChannels);
            for (int ch = 0; ch < numChannels; ch++) {
                channelData[ch].resize(actualPoints);
            }
            
            // Downsample audio data (only for visible range if zoomed in)
            for (int i = 0; i < actualPoints; i++) {
                // Map point index to time position within visible range
                float timePos = (float)i / (float)actualPoints;
                float absoluteTime = visibleStart + timePos * visibleRange;
                
                // Clamp to valid range
                absoluteTime = std::max(0.0f, std::min(1.0f, absoluteTime));
                
                // Map to sample index
                int sampleIndex = (int)(absoluteTime * numFrames);
                sampleIndex = std::max(0, std::min(numFrames - 1, sampleIndex));
                
                timeData[i] = timePos; // Normalized time within visible range (0-1)
                
                for (int ch = 0; ch < numChannels; ch++) {
                    channelData[ch][i] = buffer.getSample(sampleIndex, ch);
                }
            }
        }
    }
    
    // Background rectangle already drawn earlier (before early return for no player case)
    // Only draw waveform if we have audio data
    if (hasAudioData) {
        // Draw actual waveform
        float amplitudeScale = canvasHeight * WAVEFORM_AMPLITUDE_SCALE;
        
        // Get volume to scale waveform amplitude proportionally
        float volume = currentPlayer->volume.get();
        
        // Draw each channel with white color
        for (int ch = 0; ch < numChannels; ch++) {
            ImU32 lineColor = GUIConstants::toU32(GUIConstants::Waveform::Line);
            
            for (int i = 0; i < actualPoints - 1; i++) {
                float x1 = canvasPos.x + timeData[i] * canvasWidth;
                float y1 = centerY - channelData[ch][i] * volume * amplitudeScale;
                float x2 = canvasPos.x + timeData[i + 1] * canvasWidth;
                float y2 = centerY - channelData[ch][i + 1] * volume * amplitudeScale;
                
                drawList->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), lineColor, 1.0f);
            }
        }
    }
    
    // Draw controls (markers) on top of waveform
    drawWaveformControls(canvasPos, canvasMax, canvasWidth, canvasHeight);
}

/// MARK: - WF ctrls
/// @brief draw controls for the waveform
/// @param canvasPos 
/// @param canvasMax 
/// @param canvasWidth 
/// @param canvasHeight 
/// @return void
void MediaPoolGUI::drawWaveformControls(const ImVec2& canvasPos, const ImVec2& canvasMax, float canvasWidth, float canvasHeight) {
    MediaPool* pool = getMediaPool();
    if (!pool) return;
    
    auto currentPlayer = pool->getActivePlayer();
    if (!currentPlayer) return;
    
    // CRITICAL: Check if dragging a CellWidget - prevents interference with waveform interactions
    bool isDraggingParameter = !draggingParameter.empty();
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Get current media index for per-index zoom state
    size_t currentIndex = getMediaPool()->getCurrentIndex();
    auto zoomState = getWaveformZoomState(currentIndex);
    float waveformZoom = zoomState.first;
    float waveformOffset = zoomState.second;
    
    // Get current parameter values
    float playheadPos = currentPlayer->playheadPosition.get(); // Absolute position
    float startPosRelative = currentPlayer->startPosition.get(); // Relative position (0.0-1.0 within region)
    float regionStart = currentPlayer->regionStart.get();
    float regionEnd = currentPlayer->regionEnd.get();
    
    // Ensure region bounds are valid (start <= end)
    if (regionStart > regionEnd) {
        std::swap(regionStart, regionEnd);
    }
    
    // Map relative startPosition to absolute position for display
    float regionSize = regionEnd - regionStart;
    float startPosAbsolute = 0.0f;
    if (regionSize > 0.001f) {
        startPosAbsolute = regionStart + startPosRelative * regionSize;
    } else {
        startPosAbsolute = std::max(0.0f, std::min(1.0f, startPosRelative));
    }
    
    // Calculate visible time range (accounting for zoom)
    float visibleRange = 1.0f / waveformZoom;
    float visibleStart = waveformOffset;
    
    // Helper lambda to map absolute time position to screen X coordinate
    auto mapToScreenX = [&](float absolutePos) -> float {
        if (absolutePos < visibleStart || absolutePos > visibleStart + visibleRange) {
            return -1000.0f; // Off-screen (negative value to indicate off-screen)
        }
        float relativePos = (absolutePos - visibleStart) / visibleRange;
        return canvasPos.x + relativePos * canvasWidth;
    };
    
    // Calculate marker positions in screen space (accounting for zoom)
    float playheadX = mapToScreenX(playheadPos);
    float positionX = mapToScreenX(startPosAbsolute);
    float regionStartX = mapToScreenX(regionStart);
    float regionEndX = mapToScreenX(regionEnd);
    
    // Marker hit detection threshold (pixels)
    const float MARKER_HIT_THRESHOLD = 8.0f;
    
    // Check if waveform canvas is hovered/active for interaction
    bool isCanvasHovered = ImGui::IsItemHovered();
    bool isCanvasActive = ImGui::IsItemActive();
    ImVec2 mousePos = ImGui::GetMousePos();
    float mouseX = mousePos.x;
    
    // Map screen X to absolute time (accounting for zoom/pan)
    float relativeX = (mouseX - canvasPos.x) / canvasWidth;
    relativeX = visibleStart + relativeX * visibleRange; // Convert to absolute time
    relativeX = std::max(0.0f, std::min(1.0f, relativeX));
    
    // Detect which marker is closest to mouse (for dragging)
    // Only check markers that are on-screen
    WaveformMarker hoveredMarker = WaveformMarker::NONE;
    if (isCanvasHovered || isCanvasActive) {
        float minDist = MARKER_HIT_THRESHOLD;
        
        // Check region start (only if on-screen)
        if (regionStartX >= 0.0f) {
            float dist = std::abs(mouseX - regionStartX);
            if (dist < minDist) {
                minDist = dist;
                hoveredMarker = WaveformMarker::REGION_START;
            }
        }
        
        // Check region end (only if on-screen)
        if (regionEndX >= 0.0f) {
            float dist = std::abs(mouseX - regionEndX);
            if (dist < minDist) {
                minDist = dist;
                hoveredMarker = WaveformMarker::REGION_END;
            }
        }
        
        // Check position marker (only if on-screen)
        if (positionX >= 0.0f) {
            float dist = std::abs(mouseX - positionX);
            if (dist < minDist) {
                minDist = dist;
                hoveredMarker = WaveformMarker::POSITION;
            }
        }
        
        // Playhead is not draggable, but we can still seek by clicking
    }
    
    // Handle mouse interaction
    // CRITICAL: Don't process waveform mouse interactions when dragging a CellWidget
    // This prevents interference between parameter dragging and waveform interactions
    // (isDraggingParameter is already defined above)
    if ((isCanvasHovered || isCanvasActive) && !isDraggingParameter) {
        // Update cursor based on hovered marker
        if (hoveredMarker != WaveformMarker::NONE) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        } else {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        
        // Start dragging
        if (ImGui::IsMouseClicked(0)) {
            if (hoveredMarker != WaveformMarker::NONE) {
                draggingMarker = hoveredMarker;
                waveformDragStartX = mouseX;
            } else {
                // Click on empty area: behavior depends on MediaPool mode
                auto player = getMediaPool()->getActivePlayer();
                MediaPool* pool = getMediaPool();
                if (player && pool) {
                    // CRITICAL: Check transport FIRST, before checking isIdle()
                    // This ensures transport playing always updates startPosition, even if MediaPool is IDLE between triggers
                    if (pool->isTransportPlaying()) {
                        // Transport is playing: Update startPosition (not playheadPosition)
                        // Do NOT seek playhead even if player is playing - sequencer/transport controls playback
                        float regionStartVal = player->regionStart.get();
                        float regionEndVal = player->regionEnd.get();
                        float regionSize = regionEndVal - regionStartVal;
                        
                        float relativePos = 0.0f;
                        if (regionSize > 0.001f) {
                            float clampedAbsolute = std::max(regionStartVal, std::min(regionEndVal, relativeX));
                            relativePos = (clampedAbsolute - regionStartVal) / regionSize;
                            relativePos = std::max(0.0f, std::min(1.0f, relativePos));
                        } else {
                            relativePos = std::max(0.0f, std::min(1.0f, relativeX));
                        }
                        
                        player->startPosition.set(relativePos);
                        pool->setParameter("position", relativePos, true);
                    } else if (pool->isIdle()) {
                        // IDLE mode: Just set playhead position (no playback)
                        // Scrubbing playback will start when dragging begins (handled in drag section)
                        if (player->isAudioLoaded()) {
                            player->getAudioPlayer().setPosition(relativeX);
                        }
                        if (player->isVideoLoaded()) {
                            player->getVideoPlayer().getVideoFile().setPosition(relativeX);
                            player->getVideoPlayer().getVideoFile().update();
                        }
                        player->playheadPosition.set(relativeX);
                    } else if (player->isPlaying()) {
                        // MANUAL_PREVIEW mode during playback: seek playhead only (scrubbing)
                        if (player->isAudioLoaded()) {
                            player->getAudioPlayer().setPosition(relativeX);
                        }
                        if (player->isVideoLoaded()) {
                            player->getVideoPlayer().getVideoFile().setPosition(relativeX);
                            player->getVideoPlayer().getVideoFile().update();
                        }
                        player->playheadPosition.set(relativeX);
                    } else {
                        // Not playing: only update playheadPosition for visual feedback
                        player->playheadPosition.set(relativeX);
                    }
                }
            }
        }
        
        // Continue dragging
        if (draggingMarker != WaveformMarker::NONE && ImGui::IsMouseDragging(0)) {
            auto player = getMediaPool()->getActivePlayer();
            if (player) {
                switch (draggingMarker) {
                    case WaveformMarker::REGION_START: {
                        float newStart = relativeX;
                        // Clamp to [0, regionEnd]
                        newStart = std::max(0.0f, std::min(regionEnd, newStart));
                        player->regionStart.set(newStart);
                        getMediaPool()->setParameter("regionStart", newStart, true);
                        break;
                    }
                    case WaveformMarker::REGION_END: {
                        float newEnd = relativeX;
                        // Clamp to [regionStart, 1]
                        newEnd = std::max(regionStart, std::min(1.0f, newEnd));
                        player->regionEnd.set(newEnd);
                        getMediaPool()->setParameter("regionEnd", newEnd, true);
                        break;
                    }
                    case WaveformMarker::POSITION: {
                        // Update startPosition (position marker) - map absolute to relative within region
                        float regionStartVal = player->regionStart.get();
                        float regionEndVal = player->regionEnd.get();
                        float regionSize = regionEndVal - regionStartVal;
                        
                        float relativePos = 0.0f;
                        if (regionSize > 0.001f) {
                            // Clamp to region bounds, then map to relative
                            float clampedAbsolute = std::max(regionStartVal, std::min(regionEndVal, relativeX));
                            relativePos = (clampedAbsolute - regionStartVal) / regionSize;
                            relativePos = std::max(0.0f, std::min(1.0f, relativePos));
                        } else {
                            relativePos = std::max(0.0f, std::min(1.0f, relativeX));
                        }
                        
                        player->startPosition.set(relativePos);
                        if (!player->isPlaying()) {
                            // Update playheadPosition to show absolute position
                            float absolutePos = (regionSize > 0.001f) ? 
                                (regionStartVal + relativePos * regionSize) : relativePos;
                            player->playheadPosition.set(absolutePos);
                        }
                        getMediaPool()->setParameter("position", relativePos, true);
                        break;
                    }
                    default:
                        break;
                }
            }
        }
        
        // Stop dragging
        if (ImGui::IsMouseReleased(0)) {
            draggingMarker = WaveformMarker::NONE;
            
            // When scrubbing ends in IDLE mode, stop temporary playback but keep playhead position
            if (isScrubbing) {
                isScrubbing = false;
                MediaPool* pool = getMediaPool();
                if (pool && pool->isIdle()) {
                    // Stop temporary playback (doesn't change mode)
                    pool->stopTemporaryPlayback();
                    // playheadPosition is already set to the scrub position, so it stays
                }
            }
        }
        
        // Handle scrubbing (dragging without marker)
        // CRITICAL: Scrubbing behavior depends on MediaPool mode
        if (draggingMarker == WaveformMarker::NONE && ImGui::IsMouseDragging(0) && !isDraggingParameter) {
            auto player = getMediaPool()->getActivePlayer();
            MediaPool* pool = getMediaPool();
            if (!player || !pool) return;
            
            bool wasScrubbing = isScrubbing;
            isScrubbing = true;
            
            // CRITICAL: Check transport FIRST, before checking isIdle()
            // This handles the case where MediaPool is IDLE between triggers but transport is still playing
            if (pool->isTransportPlaying()) {
                // Transport is playing (sequencer active or between triggers): Update startPosition only
                // CRITICAL: Check transport FIRST, before checking isSequencerActive()
                // This handles the case where MediaPool is IDLE between triggers but transport is still playing
                float regionStartVal = player->regionStart.get();
                float regionEndVal = player->regionEnd.get();
                float regionSize = regionEndVal - regionStartVal;
                
                float relativePos = 0.0f;
                if (regionSize > 0.001f) {
                    float clampedAbsolute = std::max(regionStartVal, std::min(regionEndVal, relativeX));
                    relativePos = (clampedAbsolute - regionStartVal) / regionSize;
                    relativePos = std::max(0.0f, std::min(1.0f, relativePos));
                } else {
                    relativePos = std::max(0.0f, std::min(1.0f, relativeX));
                }
                
                player->startPosition.set(relativePos);
                pool->setParameter("position", relativePos, true);
            } else if (pool->isIdle()) {
                // IDLE mode: Start scrubbing playback for AV feedback (doesn't change mode or startPosition)
                if (!wasScrubbing) {
                    // First frame of scrubbing: start scrubbing playback
                    size_t currentIndex = pool->getCurrentIndex();
                    pool->startScrubbingPlayback(currentIndex, relativeX);
                } else {
                    // Continue scrubbing: seek to new position
                    if (player->isPlaying()) {
                        if (player->isAudioLoaded()) {
                            player->getAudioPlayer().setPosition(relativeX);
                        }
                        if (player->isVideoLoaded()) {
                            player->getVideoPlayer().getVideoFile().setPosition(relativeX);
                            player->getVideoPlayer().getVideoFile().update();
                        }
                        player->playheadPosition.set(relativeX);
                    }
                }
            } else {
                // MANUAL_PREVIEW mode: Normal scrubbing (seek playhead, allow past loop end)
                if (player->isPlaying()) {
                    // Temporarily disable loop to allow scrubbing past loop end
                    bool wasLooping = player->loop.get();
                    if (wasLooping) {
                        player->loop.set(false);
                    }
                    
                    if (player->isAudioLoaded()) {
                        player->getAudioPlayer().setPosition(relativeX);
                    }
                    if (player->isVideoLoaded()) {
                        player->getVideoPlayer().getVideoFile().setPosition(relativeX);
                        player->getVideoPlayer().getVideoFile().update();
                    }
                    player->playheadPosition.set(relativeX);
                    
                    // Restore loop state after seeking
                    if (wasLooping) {
                        player->loop.set(true);
                    }
                }
            }
        }
    }
    
    // Draw grey background on trimmed parts (outside the range)
    // The range itself keeps the black waveform background
    // Only draw if markers are on-screen
    ImU32 trimmedColor = GUIConstants::toIM_COL32(GUIConstants::Background::WaveformTrimmed);
    if (regionStart > 0.0f && regionStartX >= 0.0f) {
        // Draw grey background for left trimmed part (before region start)
        float trimStartX = canvasPos.x;
        float trimEndX = std::min(regionStartX, canvasMax.x);
        if (trimEndX > trimStartX) {
            drawList->AddRectFilled(
                ImVec2(trimStartX, canvasPos.y),
                ImVec2(trimEndX, canvasMax.y),
                trimmedColor
            );
        }
    }
    if (regionEnd < 1.0f && regionEndX >= 0.0f) {
        // Draw grey background for right trimmed part (after region end)
        float trimStartX = std::max(regionEndX, canvasPos.x);
        float trimEndX = canvasMax.x;
        if (trimEndX > trimStartX) {
            drawList->AddRectFilled(
                ImVec2(trimStartX, canvasPos.y),
                ImVec2(trimEndX, canvasMax.y),
                trimmedColor
            );
        }
    }
    
    // Marker dimensions
    const float markerLineWidth = 1.5f;
    const float markerHandleWidth = 8.0f;
    const float markerHandleHeight = 6.0f;
    const float markerLineTopOffset = markerHandleHeight;
    
    // Draw region start marker (grey) - only if on-screen
    if (regionStartX >= 0.0f) {
        ImU32 regionStartColor = GUIConstants::toU32(GUIConstants::Waveform::RegionStart);
        drawList->AddLine(
            ImVec2(regionStartX, canvasPos.y + markerLineTopOffset),
            ImVec2(regionStartX, canvasMax.y),
            regionStartColor, markerLineWidth
        );
        // Draw marker handle (small horizontal bar at top)
        drawList->AddRectFilled(
            ImVec2(regionStartX - markerHandleWidth * 0.5f, canvasPos.y),
            ImVec2(regionStartX + markerHandleWidth * 0.5f, canvasPos.y + markerHandleHeight),
            regionStartColor
        );
    }
    
    // Draw region end marker (grey) - only if on-screen
    if (regionEndX >= 0.0f) {
        ImU32 regionEndColor = GUIConstants::toU32(GUIConstants::Waveform::RegionEnd);
        drawList->AddLine(
            ImVec2(regionEndX, canvasPos.y + markerLineTopOffset),
            ImVec2(regionEndX, canvasMax.y),
            regionEndColor, markerLineWidth
        );
        // Draw marker handle (small horizontal bar at top)
        drawList->AddRectFilled(
            ImVec2(regionEndX - markerHandleWidth * 0.5f, canvasPos.y),
            ImVec2(regionEndX + markerHandleWidth * 0.5f, canvasPos.y + markerHandleHeight),
            regionEndColor
        );
    }
    
    // Draw position marker (darker grey) - shows where playback will start - only if on-screen
    if (positionX >= 0.0f) {
        ImU32 positionColor = GUIConstants::toU32(GUIConstants::Waveform::Position);
        drawList->AddLine(
            ImVec2(positionX, canvasPos.y + markerLineTopOffset),
            ImVec2(positionX, canvasMax.y),
            positionColor, markerLineWidth
        );
        // Draw marker handle (small horizontal bar at top, slightly wider)
        const float positionHandleWidth = 10.0f;
        drawList->AddRectFilled(
            ImVec2(positionX - positionHandleWidth * 0.5f, canvasPos.y),
            ImVec2(positionX + positionHandleWidth * 0.5f, canvasPos.y + markerHandleHeight),
            positionColor
        );
    }
    
    // Draw playhead (green) - shows current playback position (can move freely, even outside region) - only if on-screen
    bool showPlayhead = (playheadPos > 0.0f || currentPlayer->isPlaying());
    if (showPlayhead && playheadX >= 0.0f) {
        ImU32 playheadColor = GUIConstants::toU32(GUIConstants::Waveform::Playhead);
        drawList->AddLine(
            ImVec2(playheadX, canvasPos.y),
            ImVec2(playheadX, canvasMax.y),
            playheadColor, 2.0f
        );
    }
    
    // Draw loop range visualization (when in LOOP play style with loopSize > 0)
    PlayStyle currentPlayStyle = getMediaPool()->getPlayStyle();
    if (currentPlayStyle == PlayStyle::LOOP) {
        float loopSizeSeconds = currentPlayer->loopSize.get();
        if (loopSizeSeconds > 0.001f) {
            float duration = currentPlayer->getDuration();
            if (duration > 0.001f) {
                // Calculate loop start position (absolute) - same logic as in MediaPool::update()
                float relativeStartPos = currentPlayer->startPosition.get();
                float regionSize = regionEnd - regionStart;
                float loopStartAbsolute = 0.0f;
                
                if (regionSize > 0.001f) {
                    loopStartAbsolute = regionStart + relativeStartPos * regionSize;
                } else {
                    loopStartAbsolute = std::max(0.0f, std::min(1.0f, relativeStartPos));
                }
                
                // CRITICAL FIX: Work in absolute time (seconds) first to preserve precision
                // Converting small time values to normalized positions loses precision for long samples
                // Convert normalized positions to absolute time
                float loopStartSeconds = loopStartAbsolute * duration;
                float regionEndSeconds = regionEnd * duration;
                
                // Calculate loop end in absolute time
                float calculatedLoopEndSeconds = loopStartSeconds + loopSizeSeconds;
                
                // Clamp to region end and media duration
                float clampedLoopEndSeconds = std::min(regionEndSeconds, std::min(duration, calculatedLoopEndSeconds));
                
                // Convert back to normalized position (0-1)
                float loopEndAbsolute = clampedLoopEndSeconds / duration;
                
                // Map to screen coordinates
                float loopStartX = mapToScreenX(loopStartAbsolute);
                float loopEndX = mapToScreenX(loopEndAbsolute);
                
                // Draw loop range overlay (semi-transparent blue/purple)
                if (loopStartX >= 0.0f || loopEndX >= 0.0f) {
                    // Clamp to visible area
                    float drawStartX = std::max(canvasPos.x, loopStartX >= 0.0f ? loopStartX : canvasPos.x);
                    float drawEndX = std::min(canvasMax.x, loopEndX >= 0.0f ? loopEndX : canvasMax.x);
                    
                    if (drawEndX > drawStartX) {
                        // Semi-transparent overlay for loop range
                        ImU32 loopRangeColor = GUIConstants::toIM_COL32(GUIConstants::Waveform::LoopRange);
                        drawList->AddRectFilled(
                            ImVec2(drawStartX, canvasPos.y),
                            ImVec2(drawEndX, canvasMax.y),
                            loopRangeColor
                        );
                        
                        // Draw border lines for clarity
                        ImU32 loopBorderColor = GUIConstants::toIM_COL32(GUIConstants::Waveform::LoopRangeBorder);
                        if (loopStartX >= 0.0f) {
                            drawList->AddLine(
                                ImVec2(loopStartX, canvasPos.y),
                                ImVec2(loopStartX, canvasMax.y),
                                loopBorderColor, 1.0f
                            );
                        }
                        if (loopEndX >= 0.0f) {
                            drawList->AddLine(
                                ImVec2(loopEndX, canvasPos.y),
                                ImVec2(loopEndX, canvasMax.y),
                                loopBorderColor, 1.0f
                            );
                        }
                    }
                }
            }
        }
    }
}

/// MARK: - WF zoom
/// @brief get the zoom state for a given index
/// @param index 
/// @return std::pair<float, float>
std::pair<float, float> MediaPoolGUI::getWaveformZoomState(size_t index) const {
    auto it = waveformZoomState.find(index);
    if (it != waveformZoomState.end()) {
        return it->second;  // Return stored {zoom, offset}
    }
    // Default values: no zoom (1.0), no offset (0.0)
    return std::make_pair(1.0f, 0.0f);
}

/// @brief set the zoom state for a given index
void MediaPoolGUI::setWaveformZoomState(size_t index, float zoom, float offset) {
    waveformZoomState[index] = std::make_pair(zoom, offset);
}


/// MARK: - KEY PRESS
bool MediaPoolGUI::handleKeyPress(int key, bool ctrlPressed, bool shiftPressed) {
    // CRITICAL FIX: If editingColumnIndex is set but editingParameter is empty,
    // look up the parameter name from the column index
    // This handles cases where focus was synced from ImGui but editingParameter wasn't set yet
    if (editingColumnIndex >= 0 && editingParameter.empty()) {
        auto columnConfig = cellGrid.getColumnConfiguration();
        if (editingColumnIndex >= 0 && (size_t)editingColumnIndex < columnConfig.size()) {
            const std::string& paramName = columnConfig[editingColumnIndex].parameterName;
            // Only set editingParameter for non-button columns
            if (paramName != "mediaIndex" && paramName != "playStyle") {
                editingParameter = paramName;
            }
        }
    }
    
    // Helper function to check if current column is editable (not a button)
    auto isEditableColumn = [this]() -> bool {
        if (editingColumnIndex < 0) return false;
        auto columnConfig = cellGrid.getColumnConfiguration();
        if ((size_t)editingColumnIndex >= columnConfig.size()) return false;
        const std::string& paramName = columnConfig[editingColumnIndex].parameterName;
        return paramName != "mediaIndex" && paramName != "playStyle";
    };
    
    // Handle direct typing (numeric keys, decimal point, operators) - auto-enter edit mode
    // This matches TrackerSequencer behavior: typing directly enters edit mode
    // NOTE: Once edit mode is entered, CellGrid's internal input handling (via CellWidget::handleInputInDraw)
    // will process the actual key input during draw(). We just need to set the edit mode state here.
    if ((key >= '0' && key <= '9') || key == '.' || key == '-' || key == '+' || key == '*' || key == '/') {
        // Check if we have a valid parameter column focused (not Index or Play style button)
        if (!isEditingParameter_ && isEditableColumn()) {
            // Ensure editingParameter is set
            if (editingParameter.empty()) {
                auto columnConfig = cellGrid.getColumnConfiguration();
                if (editingColumnIndex >= 0 && (size_t)editingColumnIndex < columnConfig.size()) {
                    editingParameter = columnConfig[editingColumnIndex].parameterName;
                }
            }
            
            if (!editingParameter.empty()) {
                // Enter edit mode - CellGrid will handle the actual key input during draw()
                isEditingParameter_ = true;
                
                // Disable ImGui keyboard navigation when entering edit mode
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
                
                // Don't consume the key - let it pass through to ImGui so CellGrid can handle it
                // CellGrid's CellWidget::handleInputInDraw() will process it during draw()
                return false;
            }
        }
    }
    
    // CRITICAL: When NOT in edit mode, let ImGui handle arrow keys for native navigation
    // We'll sync our state from ImGui focus after it processes the keys
    if (!isEditingParameter_ && editingColumnIndex >= 0) {
        // Not in edit mode: Let ImGui handle arrow keys for native navigation
        // This allows smooth navigation between parameter cells
        // Our state will be synced from ImGui focus in the next frame during draw()
        if (key == OF_KEY_LEFT || key == OF_KEY_RIGHT || key == OF_KEY_UP || key == OF_KEY_DOWN) {
            // Ensure ImGui navigation is enabled so it can handle the keys
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            
            // Don't consume the key - let ImGui handle navigation
            // Our state will be synced from ImGui focus in drawParameters()
            return false;
        }
    }
    
    // In edit mode: Let CellGrid handle arrow keys and other input internally
    // CellGrid's CellWidget::handleInputInDraw() will process them during draw()
    if (isEditingParameter_ && editingColumnIndex >= 0) {
        // Let ImGui pass the key through so CellGrid can handle it
        // This includes arrow keys (for value adjustment) and all other keys
        return false;
    }
    
    // Handle keyboard shortcuts for parameter navigation (special modifier combinations)
    switch (key) {
        case OF_KEY_RETURN:
            if (ctrlPressed || shiftPressed) {
                // Ctrl+Enter or Shift+Enter: Exit parameter editing
                if (isEditingParameter_) {
                    isEditingParameter_ = false;
                    editBufferCache.clear();
                    editBufferInitializedCache = false;
                    ImGuiIO& io = ImGui::GetIO();
                    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                    shouldRefocusCurrentCell = true; // Refocus cell after exiting edit mode
                    return true;
                }
            }
            // For Enter key: Let CellWidget handle it completely via processInputInDraw()
            // CellWidget will:
            // - Enter edit mode if not editing
            // - Apply value and exit edit mode if already editing
            // We just need to ensure navigation is disabled when entering edit mode
            if (isEditableColumn() && !isEditingParameter_) {
                // Pre-emptively disable navigation so CellWidget can enter edit mode cleanly
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
            }
            // Don't consume Enter - let CellWidget handle it via processInputInDraw()
            return false;
            
        case OF_KEY_ESC:
            // IMPORTANT: Only handle ESC when in edit mode. When NOT in edit mode, let ESC pass through
            // to ImGui so it can use ESC to escape contained navigation contexts (like scrollable tables)
            if (isEditingParameter_) {
                // Exit edit mode - CellGrid will handle the actual ESC key processing
                // We just set the state here so it's ready when CellGrid processes it
                isEditingParameter_ = false;
                editBufferCache.clear();
                editBufferInitializedCache = false;
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                shouldRefocusCurrentCell = true; // Refocus cell after exiting edit mode
                // Let CellGrid handle ESC so it can properly exit edit mode and sync state
                return false;
            }
            // NOT in edit mode: Let ESC pass through to ImGui for navigation escape
            break;
    }
    
    // All other keys: Let CellGrid handle them internally via CellWidget::handleInputInDraw()
    return false;
}

/// MARK: - SCAN MODE
/// @brief draw button for position scan mode
void MediaPoolGUI::drawPositionScanModeButton(const ImVec2& cellStartPos, float columnWidth, float cellMinY) {
    MediaPool* pool = getMediaPool();
    if (!pool) return;
    
    // Mode cycling button with all 4 modes properly mapped
    static const char* const MODE_LABELS[] = { "N", "S", "M", "G" }; // None, Step, Media, Global
    static const char* const MODE_TOOLTIPS[] = { 
        "None: No scanning - always start from set position (or 0.0)",
        "Step: Each step remembers its scan position separately",
        "Media: Each media remembers its scan position across all steps", 
        "Global: All media share one scan position"
    };
    static constexpr int NUM_MODES = 4;
    
    // Helper functions to map between enum and GUI index
    auto modeToGuiIndex = [](ScanMode mode) -> int {
        switch (mode) {
            case ScanMode::NONE: return 0;
            case ScanMode::PER_STEP: return 1;
            case ScanMode::PER_MEDIA: return 2;
            case ScanMode::GLOBAL: return 3;
            default: return 2; // Default to PER_MEDIA
        }
    };
    
    auto guiIndexToMode = [](int guiIndex) -> ScanMode {
        switch (guiIndex) {
            case 0: return ScanMode::NONE;
            case 1: return ScanMode::PER_STEP;
            case 2: return ScanMode::PER_MEDIA;
            case 3: return ScanMode::GLOBAL;
            default: return ScanMode::PER_MEDIA;
        }
    };
    
    ScanMode currentMode = getMediaPool()->getScanMode();
    int currentModeIndex = modeToGuiIndex(currentMode);
    
    // Validate mode index
    if (currentModeIndex < 0 || currentModeIndex >= NUM_MODES) {
        currentModeIndex = 2; // Default to PER_MEDIA if invalid
    }
    
    ImGui::PushID("PositionScanMode");
    
    // Calculate button size and position (right-aligned in header)
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
    float buttonWidth = ImGui::CalcTextSize(MODE_LABELS[currentModeIndex]).x + 
                        ImGui::GetStyle().FramePadding.x * 2.0f;
    float padding = ImGui::GetStyle().CellPadding.x;
    
    // Position button to the right edge of the cell
    float cellMaxX = cellStartPos.x + columnWidth;
    float buttonStartX = cellMaxX - buttonWidth - padding;
    ImGui::SetCursorScreenPos(ImVec2(buttonStartX, cellMinY));
    
    // Single click cycles to next mode
    if (ImGui::SmallButton(MODE_LABELS[currentModeIndex])) {
        int nextModeIndex = (currentModeIndex + 1) % NUM_MODES;
        getMediaPool()->setScanMode(guiIndexToMode(nextModeIndex));
    }
    
    // Simple tooltip on hover
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", MODE_TOOLTIPS[currentModeIndex]);
    }
    
    ImGui::PopStyleVar();
    
    ImGui::PopID();
}

bool MediaPoolGUI::handleFileDrop(const std::vector<std::string>& filePaths) {
    MediaPool* pool = getMediaPool();
    if (!pool || filePaths.empty()) {
        return false;
    }
    
    // Add files to MediaPool
    pool->addMediaFiles(filePaths);
    return true;
}

// Note: setupDragDropTarget() is inherited from ModuleGUI base class
// It handles FILE_PATHS payload (unified for all sources: FileBrowser, AssetLibrary, OS)
// and calls handleFileDrop() which adds files to MediaPool

//--------------------------------------------------------------
// GUI Factory Registration
//--------------------------------------------------------------
// Auto-register MediaPoolGUI with GUIManager on static initialization
// This enables true modularity - no hardcoded dependencies in GUIManager
namespace {
    struct MediaPoolGUIRegistrar {
        MediaPoolGUIRegistrar() {
            GUIManager::registerGUIType("MediaPool", 
                []() -> std::unique_ptr<ModuleGUI> {
                    return std::make_unique<MediaPoolGUI>();
                });
        }
    };
    static MediaPoolGUIRegistrar g_mediaPoolGUIRegistrar;
}
