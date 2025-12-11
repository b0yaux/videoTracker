#include "MediaPoolGUI.h"
#include "modules/MediaPool.h"  // Includes PlayStyle enum
#include "modules/MediaPlayer.h"
#include "CellWidget.h"
#include "modules/Module.h"
#include "core/ModuleRegistry.h"
#include "gui/GUIConstants.h"
#include "gui/MediaPreview.h"
#include "gui/GUIManager.h"
#include "ofMain.h"
#include "ofLog.h"
#include <limits>
#include <iomanip>
#include <cmath>

MediaPoolGUI::MediaPoolGUI() 
    : mediaPool(nullptr), waveformHeight(100.0f), parentWidgetId(0), 
      isParentWidgetFocused(false), requestFocusMoveToParentWidget(false),
      shouldFocusFirstCell(false) {
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
    
    // Child 1: Parameter table (auto-size to fit content, no extra space)
    // Use a calculated height based on table structure: header + row + minimal padding
    // Account for: header height, row height, cell padding (2px top + 2px bottom)
    float tableHeaderHeight = ImGui::GetFrameHeight();
    float tableRowHeight = ImGui::GetFrameHeight();
    float cellVerticalPadding = 4.0f; // 2px top + 2px bottom (from CellGrid cellPadding ImVec2(2, 2))
    // Use a tighter calculation - borders are included in frame height
    float parameterTableHeight = tableHeaderHeight + tableRowHeight + cellVerticalPadding;
    
    ImGui::BeginChild("MediaPoolParameters", ImVec2(0, parameterTableHeight), false, ImGuiWindowFlags_NoScrollbar);
    float paramsStartTime = ofGetElapsedTimef();
    drawParameters();
    float paramsTime = (ofGetElapsedTimef() - paramsStartTime) * 1000.0f;
    if (paramsTime > 1.0f) {
        std::string instanceName = ModuleGUI::getInstanceName();
        ofLogNotice("MediaPoolGUI") << "[PERF] '" << instanceName << "' drawParameters: " 
                                    << std::fixed << std::setprecision(2) << paramsTime << "ms";
    }
    ImGui::EndChild();
    
    // Child 2: Waveform (fixed height)
    ImGui::BeginChild("MediaPoolWaveform", ImVec2(0, waveformHeight), false, ImGuiWindowFlags_NoScrollbar);
    float waveformStartTime = ofGetElapsedTimef();
    drawWaveform();
    float waveformTime = (ofGetElapsedTimef() - waveformStartTime) * 1000.0f;
    if (waveformTime > 1.0f) {
        std::string instanceName = ModuleGUI::getInstanceName();
        ofLogNotice("MediaPoolGUI") << "[PERF] '" << instanceName << "' drawWaveform: " 
                                    << std::fixed << std::setprecision(2) << waveformTime << "ms";
    }
    ImGui::EndChild();
    
    // Child 3: Media list (takes all remaining space)
    ImGui::BeginChild("MediaList", ImVec2(0, 0), true);
    float listStartTime = ofGetElapsedTimef();
    drawMediaList();
    float listTime = (ofGetElapsedTimef() - listStartTime) * 1000.0f;
    if (listTime > 1.0f) {
        std::string instanceName = ModuleGUI::getInstanceName();
        ofLogNotice("MediaPoolGUI") << "[PERF] '" << instanceName << "' drawMediaList: " 
                                    << std::fixed << std::setprecision(2) << listTime << "ms";
    }
    ImGui::EndChild();
    
    // Set up drag & drop target on the main window (covers entire panel)
    // Must be called after all content is drawn, like AssetLibraryGUI does
    // This ensures the yellow highlight appears and drops work properly
    setupDragDropTarget();
}




/// MARK: - PARAMETERS
/// @brief create a CellWidget for a given ParameterDescriptor
/// @param paramDesc 
/// @return CellWidget
CellWidget MediaPoolGUI::createCellWidgetForParameter(const ParameterDescriptor& paramDesc) {
    MediaPool* pool = getMediaPool();
    if (!pool) {
        return CellWidget();  // Return empty cell if no pool
    }
    
    // Use ModuleGUI helper to create CellWidget with routing awareness
    // This centralizes the common pattern of getting module + router
    // Set up custom getter - capture mediaPool to get active player dynamically
    // This ensures we always get the current active player, not a stale reference
    auto customGetter = [this, paramDesc]() -> float {
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
    
    // Set up custom setter
    auto customSetter = [this, paramDesc](float value) {
        MediaPool* pool = getMediaPool();
        if (pool && pool->getActivePlayer()) {
            pool->setParameter(paramDesc.name, value, true);
        }
    };
    
    // Set up custom remover: reset to default value (double-click to reset)
    auto customRemover = [this, paramDesc]() {
        MediaPool* pool = getMediaPool();
        if (pool && pool->getActivePlayer()) {
            pool->setParameter(paramDesc.name, paramDesc.defaultValue, true);
        }
    };
    
    // Special handling for loopSize: logarithmic mapping for better precision at low values (1-100ms granular range)
    if (paramDesc.name == "loopSize") {
        // Logarithmic mapping: slider value (0.0-1.0) maps to loopSize (0.001s to 10s)
        // This provides better precision at low values (1-100ms = 0.001-0.1s)
        const float MIN_LOOP_SIZE = 0.001f;  // 1ms minimum
        const float MAX_LOOP_SIZE = 10.0f;   // 10s maximum
        
        // Calculate default slider value from default seconds value (1.0s)
        float defaultSeconds = 1.0f;
        float defaultSliderValue = 0.0f;
        if (defaultSeconds > MIN_LOOP_SIZE && defaultSeconds < MAX_LOOP_SIZE) {
            defaultSliderValue = std::log(defaultSeconds / MIN_LOOP_SIZE) / std::log(MAX_LOOP_SIZE / MIN_LOOP_SIZE);
        } else if (defaultSeconds >= MAX_LOOP_SIZE) {
            defaultSliderValue = 1.0f;
        }
        
        // Create modified parameter descriptor with slider range (0.0-1.0)
        ParameterDescriptor loopSizeParam(paramDesc.name, paramDesc.type, 0.0f, 1.0f, defaultSliderValue, paramDesc.displayName);
        
        // Override getter: Map from actual seconds to logarithmic slider value (0.0-1.0)
        auto loopSizeGetter = [this, MIN_LOOP_SIZE, MAX_LOOP_SIZE]() -> float {
            MediaPool* pool = getMediaPool();
            if (!pool) return 0.0f;
            
            auto activePlayer = pool->getActivePlayer();
            if (!activePlayer) return 0.0f;
            
            // Get actual loopSize value in seconds
            float actualValue = activePlayer->loopSize.get();
            
            // Map from linear seconds to logarithmic slider value (0.0-1.0)
            // Inverse of: value = MIN * pow(MAX/MIN, sliderValue)
            if (actualValue <= MIN_LOOP_SIZE) return 0.0f;
            if (actualValue >= MAX_LOOP_SIZE) return 1.0f;
            float sliderValue = std::log(actualValue / MIN_LOOP_SIZE) / std::log(MAX_LOOP_SIZE / MIN_LOOP_SIZE);
            return sliderValue;
        };
        
        // Override setter: Map from slider value to actual seconds
        auto loopSizeSetter = [this, paramDesc, MIN_LOOP_SIZE, MAX_LOOP_SIZE](float sliderValue) {
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
        
        // Override formatter: Show actual seconds with appropriate precision
        // NOTE: No "s" suffix - keeps parsing simple and standard (no custom parseValue needed)
        auto loopSizeFormatter = [MIN_LOOP_SIZE, MAX_LOOP_SIZE](float sliderValue) -> std::string {
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
        
        // Create and return CellWidget with custom callbacks
        return createCellWidget(loopSizeParam, loopSizeGetter, loopSizeSetter, nullptr, loopSizeFormatter);
    }
    
    // For all other parameters: use standard createCellWidget with custom callbacks
    return createCellWidget(paramDesc, customGetter, customSetter, customRemover);
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
    
    // Start at the very top of the child window (no padding)
    ImGui::SetCursorPosY(0);
    
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
    // Position at top-left with minimum size (1x1 pixels)
    ImGui::SetCursorPos(ImVec2(0, 0));
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
    
    // Reset cursor to top after InvisibleButton to ensure table starts at the top
    ImGui::SetCursorPosY(0);
    
    // Note: Table styling is handled by CellGrid (CellPadding, ItemSpacing)
    
    // Reset focus tracking at start of frame
    callbacksState.resetFrame();
    
    // Use versioned table ID to reset column order if needed (change version number to force reset)
    static int tableVersion = 3; // Increment this to reset all saved column settings (v2: added STYLE column, v3: reordered polyphonyMode after playStyle)
    std::string tableId = "MediaPoolParameters_v" + std::to_string(tableVersion);
    
    // Configure CellGrid using unified helper
    CellGridConfig gridConfig;
    gridConfig.tableId = tableId;
    gridConfig.tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                            ImGuiTableFlags_SizingStretchProp;
    configureCellGrid(cellGrid, gridConfig);
        
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
    
    // Update column configuration using unified helper (only updates if changed)
    updateColumnConfigIfChanged(cellGrid, tableColumnConfig, lastColumnConfig);
    
    // Clear special column widget cache when column configuration changes
    if (tableColumnConfig != lastColumnConfig) {
        specialColumnWidgetCache.clear();
    }
    
    cellGrid.setAvailableParameters(editableParams);
    
    // Setup callbacks for CellGrid
    CellGridCallbacks callbacks;
    
    // Setup standard callbacks (focus tracking, edit mode, state sync)
    setupStandardCellGridCallbacks(callbacks, cellFocusState, callbacksState, cellGrid, true); // true = single row
    
    // MediaPool-specific: Update isParentWidgetFocused in callbacks
    // Wrap the standard callbacks to also update isParentWidgetFocused
    auto originalOnCellFocusChanged = callbacks.onCellFocusChanged;
    callbacks.onCellFocusChanged = [this, originalOnCellFocusChanged](int row, int col) {
        if (originalOnCellFocusChanged) {
            originalOnCellFocusChanged(row, col);
        }
        isParentWidgetFocused = false;
    };
    
    auto originalOnCellClicked = callbacks.onCellClicked;
    callbacks.onCellClicked = [this, originalOnCellClicked](int row, int col) {
        if (originalOnCellClicked) {
            originalOnCellClicked(row, col);
        }
        isParentWidgetFocused = false;
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
    // State sync callbacks removed - CellWidget manages its own state internally
    // MediaPoolGUI only tracks which column is focused for UI purposes
    // Track if header was clicked to clear button cell focus
    // Setup header click callback to detect when headers are clicked
    callbacks.onHeaderClicked = [this](int col) {
        callbacksState.headerClickedThisFrame = true;
    };
    
    // Setup custom header rendering callback for Position parameter
    callbacks.drawCustomHeader = [this](int col, const CellGridColumnConfig& colConfig, ImVec2 cellStartPos, float columnWidth, float cellMinY) -> bool {
        if (colConfig.parameterName == "position") {
            // Draw column name first (standard header)
            ImGui::TableHeader(colConfig.displayName.c_str());
            
            // Check if header was clicked
            if (ImGui::IsItemClicked(0)) {
                callbacksState.headerClickedThisFrame = true;
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
    auto onCellFocusChangedCallback = callbacks.onCellFocusChanged;
    auto onCellClickedCallback = callbacks.onCellClicked;
    
    callbacks.drawSpecialColumn = [this, pool, getCellValueCallback, setCellValueCallback, 
                                    createCellWidgetCallback, isCellFocusedCallback,
                                    onCellFocusChangedCallback, onCellClickedCallback]
                                    (int row, int col, const CellGridColumnConfig& colConfig) {
        const std::string& paramName = colConfig.parameterName;
        
        // Only handle button columns here - for other columns, we need to manually render CellWidget
        // because when drawSpecialColumn is set, CellGrid uses it exclusively and doesn't fall back
        if (paramName != "mediaIndex" && paramName != "playStyle" && paramName != "polyphonyMode") {
            // Not a button column - manually render CellWidget (replicating CellGrid's default behavior)
            // Get focus state
            bool isFocused = ModuleGUI::isCellFocused(cellFocusState, row, col);
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
            
            // CellWidget manages its own state (editing, buffer, drag, selection)
            // We only track which cell is focused for UI coordination
            // onEditModeChanged callback is set up by CellGrid automatically
            
            // Draw cell - CellWidget handles all state internally
            int uniqueId = row * 1000 + col;
            CellWidgetInputContext inputContext;
            
            CellWidgetInteraction interaction = cell.draw(uniqueId, isFocused, false, inputContext);
            
            // Handle interactions
            bool actuallyFocused = ImGui::IsItemFocused();
            
            if (interaction.focusChanged) {
                if (actuallyFocused) {
                    setCellFocus(cellFocusState, row, col, paramName);
                    callbacksState.anyCellFocusedThisFrame = true;
                } else if (cellFocusState.column == col) {
                    ModuleGUI::clearCellFocus(cellFocusState);
                }
                
                if (onCellFocusChangedCallback) {
                    onCellFocusChangedCallback(row, col);
                }
            }
            
            if (interaction.clicked) {
                setCellFocus(cellFocusState, row, col, paramName);
                if (onCellClickedCallback) {
                    onCellClickedCallback(row, col);
                }
            }
            
            isFocused = actuallyFocused;
            
            // Track editing state for UI purposes (CellWidget manages its own edit buffer)
            if (cell.isEditingMode() && isFocused) {
                cellFocusState.isEditing = true;
                callbacksState.anyCellFocusedThisFrame = true;
            } else if (cellFocusState.isEditing && isFocused && !cell.isEditingMode()) {
                cellFocusState.isEditing = false;
                // Note: Refocus is now handled automatically by CellWidget when exiting edit mode
            }
            
            return; // Done rendering CellWidget for this column
        }
        
        // Button columns: use direct ImGui::Button() calls
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
            // Button should be green only when:
            // 1. This player is the active player
            // 2. MediaPool is in PLAYING mode (pool->isPlaying())
            // MediaPool's transition logic ensures isPlaying() accurately reflects playback state
            bool isActive = false;
            auto activePlayer = pool->getActivePlayer();
            if (activePlayer != nullptr && currentIndex < numPlayers) {
                auto currentPlayer = pool->getMediaPlayer(currentIndex);
                if (currentPlayer == activePlayer) {
                    // Rely on MediaPool's authoritative state - it handles all edge cases
                    // including when media finishes naturally and transitions to IDLE
                    isActive = pool->isPlaying();
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
                
                // Only toggle if currently playing - don't interfere with sequencer playback
                if (pool->isPlaying()) {
                    currentPlayer->stop();
                    pool->setModeIdle();
                } else if (!pool->isPlaying()) {
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
        // Clear focus using unified helper if conditions are met
        // Conditions: header clicked OR (cell focused but no cell focused this frame AND not editing)
        ModuleGUI::handleFocusClearing(cellFocusState, callbacksState);
        
    // End table
    cellGrid.endTable();
    
    // Check for clicks outside the grid (after table ends)
    // This handles clicks on empty space within the window
    if (cellFocusState.hasFocus() && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
        ModuleGUI::clearCellFocus(cellFocusState);
    }
}

void MediaPoolGUI::clearCellFocus() {
    ModuleGUI::clearCellFocus(cellFocusState);
}

// Sync edit state from ImGui focus - called from InputRouter when keys are pressed
void MediaPoolGUI::syncEditStateFromImGuiFocus(MediaPoolGUI& gui) {
    // Check if editingColumnIndex is already valid (GUI sync already happened)
    if (gui.cellFocusState.column >= 0) {
        // If editingParameter is empty but column is set, look it up from column config
        if (gui.cellFocusState.editingParameter.empty() && gui.mediaPool) {
            // Get column configuration from CellGrid
            auto columnConfig = gui.cellGrid.getColumnConfiguration();
            if (gui.cellFocusState.column >= 0 && (size_t)gui.cellFocusState.column < columnConfig.size()) {
                gui.cellFocusState.editingParameter = columnConfig[gui.cellFocusState.column].parameterName;
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
    
    // Create invisible button for interaction area (ensure non-zero size for ImGui)
    float safeHeight = std::max(waveformHeight, 1.0f);
    float availableWidth = std::max(ImGui::GetContentRegionAvail().x, 100.0f); // Fallback if window not ready
    
    ImVec2 canvasSize = ImVec2(availableWidth, safeHeight);
    ImGui::InvisibleButton("waveform_canvas", canvasSize);
    
    // Get draw list and canvas dimensions
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetItemRectMin();
    ImVec2 canvasMax = ImGui::GetItemRectMax();
    float canvasWidth = canvasMax.x - canvasPos.x;
    float canvasHeight = canvasMax.y - canvasPos.y;
    float centerY = canvasPos.y + canvasHeight * 0.5f;
    
    // Draw background
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
    // Check cached widgets to see if any are currently dragging
    bool isDraggingParameter = false;
    for (const auto& [key, cell] : specialColumnWidgetCache) {
        if (cell.getIsDragging()) {
            isDraggingParameter = true;
            break;
        }
    }
    
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
            newZoom = std::max(1.0f, std::min(10000.0f, newZoom)); // Clamp zoom (10000x for extreme precision)
            
            // Calculate new offset to keep mouse position fixed
            float newVisibleRange = 1.0f / newZoom;
            float newOffset = mouseTimeAbsolute - mouseTime * newVisibleRange;
            newOffset = std::max(0.0f, std::min(1.0f - newVisibleRange, newOffset));
            
            // Store updated zoom state for current index
            setWaveformZoomState(currentIndex, newZoom, newOffset);
            waveformZoom = newZoom;
            waveformOffset = newOffset;
            // Invalidate waveform cache when zoom changes
            waveformCacheValid_ = false;
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
                // Invalidate waveform cache when pan changes
                waveformCacheValid_ = false;
                
                ImGui::ResetMouseDragDelta(ImGui::IsMouseDown(2) ? 2 : 0);
            }
        }
        
        // Double-click to reset zoom
        if (ImGui::IsMouseDoubleClicked(0)) {
            setWaveformZoomState(currentIndex, 1.0f, 0.0f);
            waveformZoom = 1.0f;
            waveformOffset = 0.0f;
            // Invalidate waveform cache when zoom resets
            waveformCacheValid_ = false;
        }
    }
    
    // Calculate visible time range
    float visibleRange = 1.0f / waveformZoom;
    float visibleStart = waveformOffset;
    
    // Waveform data for rendering (min/max pairs for industry-standard visualization)
    bool hasAudioData = false;
    int numChannels = 0;
    int actualPoints = 0;
    std::vector<float> waveformTimeData;
    std::vector<std::vector<float>> waveformChannelMinData;
    std::vector<std::vector<float>> waveformChannelMaxData;
    
    if (currentPlayer->isAudioLoaded()) {
        // Cache audio buffer (getBuffer() is expensive ~10ms)
        std::string currentAudioPath = currentPlayer->getAudioFilePath();
        bool bufferNeedsRefresh = !audioBufferCacheValid_ || (cachedAudioFilePath_ != currentAudioPath);
        
        if (bufferNeedsRefresh) {
            cachedAudioBuffer_ = currentPlayer->getAudioPlayer().getBuffer();
            cachedAudioFilePath_ = currentAudioPath;
            audioBufferCacheValid_ = true;
            waveformCacheValid_ = false; // Invalidate waveform cache
        }
        const ofSoundBuffer& buffer = cachedAudioBuffer_;
        
        int numFrames = buffer.getNumFrames();
        numChannels = buffer.getNumChannels();
        
        if (numFrames > 0 && numChannels > 0) {
            hasAudioData = true;
            
            // Check if waveform cache is valid
            bool cacheValid = waveformCacheValid_ &&
                             (cachedMediaIndex_ == currentIndex) &&
                             (cachedNumFrames_ == numFrames) &&
                             (cachedNumChannels_ == numChannels) &&
                             (std::abs(cachedVisibleStart_ - visibleStart) < 0.0001f) &&
                             (std::abs(cachedVisibleRange_ - visibleRange) < 0.0001f) &&
                             (std::abs(cachedCanvasWidth_ - canvasWidth) < 1.0f);
            
            if (cacheValid && !cachedWaveformTimeData_.empty()) {
                // Use cached waveform data
                waveformTimeData = cachedWaveformTimeData_;
                waveformChannelMinData = cachedWaveformMinData_;
                waveformChannelMaxData = cachedWaveformMaxData_;
                actualPoints = static_cast<int>(waveformTimeData.size());
            } else {
                // Recalculate waveform data with adaptive quality
                // Calculate points based on canvas width (pixels) and zoom level
                // Base: 2.0 points per pixel for better unzoomed precision (increased from 1.5)
                float pointsPerPixel = 2.0f;
                
                // Adaptive precision scaling for deep zooming
                // Uses logarithmic scaling to provide more precision at higher zoom levels
                if (visibleRange < 1.0f) {
                    float zoomLevel = 1.0f / visibleRange; // 1.0 = no zoom, 10000.0 = 10000x zoom
                    
                    // Logarithmic scaling: provides smooth precision increase from 1x to 10000x zoom
                    // At 1x zoom: multiplier = 1.0
                    // At 10x zoom: multiplier ≈ 1.5
                    // At 100x zoom: multiplier ≈ 2.0
                    // At 1000x zoom: multiplier ≈ 2.5
                    // At 10000x zoom: multiplier ≈ 3.0
                    // This ensures we get more detail as we zoom deeper without excessive points at low zoom
                    float logZoom = std::log10(std::max(1.0f, zoomLevel));
                    float zoomDetailMultiplier = 1.0f + logZoom * 0.5f; // Logarithmic scaling factor
                    
                    // Cap at 10.0x for extremely deep zoom (10000x+) to prevent excessive point counts
                    // This allows up to 20 points per pixel at maximum zoom
                    pointsPerPixel *= std::min(zoomDetailMultiplier, 10.0f);
                }
                
                // Calculate max points based on canvas width and adaptive precision
                int maxPoints = (int)(canvasWidth * pointsPerPixel);
                // Clamp to reasonable bounds (supports up to 64000 points for extreme zoom)
                maxPoints = std::max(MIN_WAVEFORM_POINTS, std::min(MAX_WAVEFORM_POINTS, maxPoints));
                
                int stepSize = std::max(1, numFrames / maxPoints);
                actualPoints = std::min(maxPoints, numFrames / stepSize);
                
                waveformTimeData.resize(actualPoints);
                waveformChannelMinData.resize(numChannels);
                waveformChannelMaxData.resize(numChannels);
                for (int ch = 0; ch < numChannels; ch++) {
                    waveformChannelMinData[ch].resize(actualPoints);
                    waveformChannelMaxData[ch].resize(actualPoints);
                }
                
                // Downsample audio using min/max peak detection (industry standard)
                for (int i = 0; i < actualPoints; i++) {
                    // Map point index to time position within visible range
                    float timePos = (float)i / (float)actualPoints;
                    float absoluteTime = visibleStart + timePos * visibleRange;
                    
                    // Clamp to valid range
                    absoluteTime = std::max(0.0f, std::min(1.0f, absoluteTime));
                    
                    // Calculate sample range for this display point
                    // Each point represents a range of samples to avoid aliasing
                    float nextTimePos = (float)(i + 1) / (float)actualPoints;
                    float nextAbsoluteTime = visibleStart + nextTimePos * visibleRange;
                    nextAbsoluteTime = std::max(0.0f, std::min(1.0f, nextAbsoluteTime));
                    
                    // Convert time range to sample indices (use float precision)
                    float startSample = absoluteTime * numFrames;
                    float endSample = nextAbsoluteTime * numFrames;
                    
                    // Ensure we have at least one sample
                    int startIdx = std::max(0, std::min(numFrames - 1, (int)std::floor(startSample)));
                    int endIdx = std::max(0, std::min(numFrames - 1, (int)std::floor(endSample)));
                    
                    // Use at least one sample, but prefer range for smoothing
                    if (endIdx <= startIdx) {
                        endIdx = std::min(numFrames - 1, startIdx + 1);
                    }
                    
                    waveformTimeData[i] = timePos; // Normalized time within visible range (0-1)
                    
                    // Find min/max across sample range for each channel
                    for (int ch = 0; ch < numChannels; ch++) {
                        float minVal = buffer.getSample(startIdx, ch);
                        float maxVal = minVal;
                        
                        for (int s = startIdx; s <= endIdx && s < numFrames; s++) {
                            float sample = buffer.getSample(s, ch);
                            minVal = std::min(minVal, sample);
                            maxVal = std::max(maxVal, sample);
                        }
                        
                        // Store both min and max (preserves full dynamic range)
                        waveformChannelMinData[ch][i] = minVal;
                        waveformChannelMaxData[ch][i] = maxVal;
                    }
                }
                
                // Cache the calculated waveform data
                cachedWaveformTimeData_ = waveformTimeData;
                cachedWaveformMinData_ = waveformChannelMinData;
                cachedWaveformMaxData_ = waveformChannelMaxData;
                cachedVisibleStart_ = visibleStart;
                cachedVisibleRange_ = visibleRange;
                cachedCanvasWidth_ = canvasWidth;
                cachedNumFrames_ = numFrames;
                cachedNumChannels_ = numChannels;
                cachedMediaIndex_ = currentIndex;
                waveformCacheValid_ = true;
            }
        }
    } else {
        // No audio - invalidate all caches
        audioBufferCacheValid_ = false;
        waveformCacheValid_ = false;
    }
    
    // Draw waveform using industry-standard min/max vertical lines
    if (hasAudioData) {
        float amplitudeScale = canvasHeight * WAVEFORM_AMPLITUDE_SCALE;
        float volume = currentPlayer->volume.get();
        ImU32 lineColor = GUIConstants::toU32(GUIConstants::Waveform::Line);
        
        // Draw each channel as vertical lines from min to max
        for (int ch = 0; ch < numChannels; ch++) {
            for (int i = 0; i < actualPoints; i++) {
                float x = canvasPos.x + waveformTimeData[i] * canvasWidth;
                float yMin = centerY - waveformChannelMinData[ch][i] * volume * amplitudeScale;
                float yMax = centerY - waveformChannelMaxData[ch][i] * volume * amplitudeScale;
                
                // Vertical line from min to max (filled waveform appearance)
                drawList->AddLine(ImVec2(x, yMin), ImVec2(x, yMax), lineColor, 1.0f);
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
    // Check cached widgets to see if any are currently dragging
    bool isDraggingParameter = false;
    for (const auto& [key, cell] : specialColumnWidgetCache) {
        if (cell.getIsDragging()) {
            isDraggingParameter = true;
            break;
        }
    }
    
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
                    // If player is playing (sequencer active), update startPosition for next trigger
                    // If player is not playing, allow scrubbing (update playheadPosition)
                    if (player->isPlaying()) {
                        // Player is playing: Update startPosition (not playheadPosition)
                        // Do NOT seek playhead - sequencer controls playback
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
                    } else if (!pool->isPlaying()) {
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
                        // PLAYING mode during playback: seek playhead only (scrubbing)
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
                if (pool && !pool->isPlaying()) {
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
            
            // If player is playing (sequencer active), update startPosition for next trigger
            // If player is not playing, allow scrubbing (update playheadPosition)
            if (player->isPlaying()) {
                // Player is playing: Update startPosition (not playheadPosition)
                // Do NOT seek playhead - sequencer controls playback
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
            } else if (!pool->isPlaying()) {
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
                // IDLE mode: Normal scrubbing (seek playhead, allow past loop end)
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
    // PHASE 1: SINGLE INPUT PATH - CellWidget is sole input processor for cells
    // If any cell has focus, let CellWidget handle ALL input
    if (cellFocusState.hasFocus()) {
        return false; // CellWidget will handle in processInputInDraw()
    }
    
    // Only handle global shortcuts when no cell is focused
    // Currently MediaPoolGUI doesn't have global shortcuts like play/pause
    // (those are handled by TrackerSequencerGUI)
    // So for now, just let all keys pass through to ImGui when no cell is focused
    return false;
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


