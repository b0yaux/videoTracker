#include "CellGrid.h"
#include "gui/GUIConstants.h"
#include "gui/HeaderPopup.h"
#include "ofLog.h"
#include <algorithm>
#include <set>

CellGrid::CellGrid()
    : tableId("CellGrid")
    , tableFlags(ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                 ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit)
    , scrollingEnabled(false)
    , tableHeight(0.0f)
    , scrollbarSize(8.0f)
    , cellPadding(ImVec2(2, 2))
    , itemSpacing(ImVec2(1, 1))
    , reorderingEnabled(false)
    , autoScrollEnabled(false)
    , lastFocusedRowForScroll(-1)
    , cachedFocusedRow(-1)
    , cachedFocusedRowFrame(-1)
    , tableStarted(false)
    , currentRow(-1)
    , numRows(0)
    , numFixedColumns(0)
{
}

CellGrid::~CellGrid() {
}

void CellGrid::setColumnConfiguration(const std::vector<CellGridColumnConfig>& config) {
    columnConfig = config;
    updateColumnIndices();
    // Clear widget cache when column configuration changes
    clearCellCache();
}

void CellGrid::addColumn(const std::string& parameterName, const std::string& displayName, int position) {
    // Don't allow duplicate parameter names
    for (const auto& col : columnConfig) {
        if (col.parameterName == parameterName) {
            ofLogWarning("CellGrid") << "Column for parameter '" << parameterName << "' already exists";
            return;
        }
    }
    
    int insertPos = (position < 0 || position >= (int)columnConfig.size()) 
                    ? (int)columnConfig.size() 
                    : position;
    
    // Insert at specified position
    columnConfig.insert(columnConfig.begin() + insertPos, 
                       CellGridColumnConfig(parameterName, displayName, true, insertPos));
    
    updateColumnIndices();
    // Clear widget cache when columns change
    clearCellCache();
}

void CellGrid::removeColumn(int columnIndex) {
    if (columnIndex < 0 || columnIndex >= (int)columnConfig.size()) {
        ofLogWarning("CellGrid") << "Invalid column index: " << columnIndex;
        return;
    }
    
    // Don't allow removing non-removable columns (e.g., index, length)
    if (!columnConfig[columnIndex].isRemovable) {
        ofLogWarning("CellGrid") << "Cannot remove required column: " << columnConfig[columnIndex].parameterName;
        return;
    }
    
    columnConfig.erase(columnConfig.begin() + columnIndex);
    updateColumnIndices();
    // Clear widget cache when columns change
    clearCellCache();
}

void CellGrid::reorderColumn(int fromIndex, int toIndex) {
    if (fromIndex < 0 || fromIndex >= (int)columnConfig.size() ||
        toIndex < 0 || toIndex >= (int)columnConfig.size()) {
        ofLogWarning("CellGrid") << "Invalid column indices for reorder: " << fromIndex << " -> " << toIndex;
        return;
    }
    
    // Don't allow moving non-draggable columns
    if (!columnConfig[fromIndex].isDraggable) {
        ofLogWarning("CellGrid") << "Cannot reorder non-draggable column: " << columnConfig[fromIndex].parameterName;
        return;
    }
    
    // Move the column
    CellGridColumnConfig col = columnConfig[fromIndex];
    columnConfig.erase(columnConfig.begin() + fromIndex);
    columnConfig.insert(columnConfig.begin() + toIndex, col);
    
    updateColumnIndices();
    // Clear widget cache when columns are reordered
    clearCellCache();
}

bool CellGrid::isColumnFixed(int columnIndex) const {
    // DEPRECATED: Use isColumnRemovable() instead. Kept for backward compatibility.
    if (columnIndex < 0 || columnIndex >= (int)columnConfig.size()) {
        return false;
    }
    return !columnConfig[columnIndex].isRemovable; // isFixed = !isRemovable
}

const CellGridColumnConfig& CellGrid::getColumnConfig(int columnIndex) const {
    static CellGridColumnConfig emptyConfig;
    if (columnIndex < 0 || columnIndex >= (int)columnConfig.size()) {
        return emptyConfig;
    }
    return columnConfig[columnIndex];
}

void CellGrid::enableScrolling(bool enable, float height) {
    scrollingEnabled = enable;
    tableHeight = height;
    
    if (enable) {
        tableFlags |= ImGuiTableFlags_ScrollY;
    } else {
        tableFlags &= ~ImGuiTableFlags_ScrollY;
    }
}

void CellGrid::beginTable(int numRows, int numFixedColumns) {
    this->numRows = numRows;
    this->numFixedColumns = numFixedColumns;
    tableStarted = true;
    currentRow = -1;
    
    // Focus tracking is handled by GUI layer via callbacks
    
    // Clear fixed column setups if they don't match
    if ((int)fixedColumnSetups.size() != numFixedColumns) {
        fixedColumnSetups.clear();
        fixedColumnSetups.resize(numFixedColumns);
    }
    
    // Apply styling
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cellPadding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, itemSpacing);
    
    // Calculate table size if scrolling is enabled
    ImVec2 outerSize(0.0f, 0.0f);
    if (scrollingEnabled) {
        if (tableHeight <= 0.0f) {
            // Auto-calculate height from available content region
            ImVec2 contentRegion = ImGui::GetContentRegionAvail();
            tableHeight = std::max(200.0f, contentRegion.y);
        }
        // Use outerSize to constrain table height and enable scrolling
        // Width = 0 means use available width, Height = tableHeight constrains vertical size
        outerSize = ImVec2(0.0f, tableHeight);
        
        // Make scrollbar thinner
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, scrollbarSize);
        
        // Ensure ScrollY flag is set for proper scrolling
        tableFlags |= ImGuiTableFlags_ScrollY;
    } else {
        // Disable scrolling - remove ScrollY flag if present
        tableFlags &= ~ImGuiTableFlags_ScrollY;
    }
    
    int totalColumns = numFixedColumns + (int)columnConfig.size();
    
    if (ImGui::BeginTable(tableId.c_str(), totalColumns, tableFlags, outerSize)) {
        // Setup scroll freeze (freeze header row)
        ImGui::TableSetupScrollFreeze(0, 1);
        
        // Setup fixed columns
        for (int i = 0; i < numFixedColumns; i++) {
            if (i < (int)fixedColumnSetups.size() && !fixedColumnSetups[i].label.empty()) {
                const auto& setup = fixedColumnSetups[i];
                if (setup.isStretch) {
                    ImGui::TableSetupColumn(setup.label.c_str(), ImGuiTableColumnFlags_WidthStretch, setup.weight);
                } else {
                    ImGui::TableSetupColumn(setup.label.c_str(), ImGuiTableColumnFlags_WidthFixed, setup.width);
                }
            } else {
                // Default: fixed width column
                ImGui::TableSetupColumn("##", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            }
        }
        
        // Setup parameter columns
        setupParameterColumns();
    } else {
        // BeginTable failed - need to pop style vars that were pushed
        tableStarted = false;
        if (scrollingEnabled) {
            ImGui::PopStyleVar(); // Pop ScrollbarSize
        }
        ImGui::PopStyleVar(2); // Pop CellPadding and ItemSpacing
    }
}

void CellGrid::setupFixedColumn(int index, const std::string& label, float width, bool isStretch, float weight) {
    if (index < 0) return;
    
    if (index >= (int)fixedColumnSetups.size()) {
        fixedColumnSetups.resize(index + 1);
    }
    
    fixedColumnSetups[index].label = label;
    fixedColumnSetups[index].width = width;
    fixedColumnSetups[index].isStretch = isStretch;
    fixedColumnSetups[index].weight = weight;
}

void CellGrid::setupParameterColumns() {
    // Setup parameter columns with optional custom callback for full ImGui API control
    // 
    // If setupParameterColumn callback is provided, it's called for each column, allowing
    // full control over ImGui::TableSetupColumn (e.g., mixed fixed/stretch columns).
    // Otherwise, uses default behavior (all columns stretch with equal weight).
    // 
    // See CellGridCallbacks::setupParameterColumn documentation for usage examples.
    for (size_t i = 0; i < columnConfig.size(); i++) {
        const auto& col = columnConfig[i];
        int absoluteColIndex = (int)i + numFixedColumns;
        
        // If custom setup callback is provided, use it
        if (callbacks.setupParameterColumn) {
            bool handled = callbacks.setupParameterColumn((int)i, col, absoluteColIndex);
            if (handled) {
                // Column was set up by callback - continue to next column
                continue;
            }
            // Fall through to default behavior if callback returns false
        }
        
        // Default behavior: all columns stretch (backward compatible)
        ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_WidthStretch;
        
        // Disable reordering for non-draggable columns
        if (!col.isDraggable) {
            flags |= ImGuiTableColumnFlags_NoReorder;
        }
        
        ImGui::TableSetupColumn(col.displayName.c_str(), flags, 1.0f);
    }
}

void CellGrid::drawHeaders(int numFixedColumns, std::function<void(int fixedColIndex)> drawFixedColumnHeader) {
    if (!tableStarted) return;
    
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    
    // Draw fixed column headers (if any)
    if (drawFixedColumnHeader) {
        for (int i = 0; i < numFixedColumns; i++) {
            ImGui::TableSetColumnIndex(i);
            drawFixedColumnHeader(i);
        }
    } else {
        // Default: draw empty headers for fixed columns
        for (int i = 0; i < numFixedColumns; i++) {
            ImGui::TableSetColumnIndex(i);
            if (i < (int)fixedColumnSetups.size() && !fixedColumnSetups[i].label.empty()) {
                ImGui::TableHeader(fixedColumnSetups[i].label.c_str());
            } else {
                ImGui::TableHeader("##");
            }
        }
    }
    
    // Draw parameter column headers
    for (size_t i = 0; i < columnConfig.size(); i++) {
        ImGui::TableSetColumnIndex((int)i + numFixedColumns);
        const auto& colConfig = columnConfig[i];
        
        ImGui::PushID((int)(i + 1000)); // Unique ID for header buttons
        
        // Get cell position and width before drawing header
        ImVec2 cellStartPos = ImGui::GetCursorScreenPos();
        float columnWidth = ImGui::GetColumnWidth();
        float cellMinY = cellStartPos.y;
        
        // Check if there's a custom header renderer for this column
        bool useCustomHeader = false;
        if (callbacks.drawCustomHeader) {
            useCustomHeader = callbacks.drawCustomHeader((int)i, colConfig, cellStartPos, columnWidth, cellMinY);
        }
        
        // Default header rendering (if not using custom header)
        if (!useCustomHeader) {
            // Draw column name (left-aligned)
            ImGui::TableHeader(colConfig.displayName.c_str());
            
            // Check if header was clicked (for focus clearing)
            if (ImGui::IsItemClicked(0)) {
                // Notify callback if header was clicked
                if (callbacks.onHeaderClicked) {
                    callbacks.onHeaderClicked((int)i);
                }
            }
            
            // Collect all buttons for this column (specific + global with conditions)
            std::vector<HeaderButton> buttonsToDraw;
            
            // Add column-specific buttons
            auto it = headerButtons.find((int)i);
            if (it != headerButtons.end()) {
                buttonsToDraw.insert(buttonsToDraw.end(), it->second.begin(), it->second.end());
            }
            
            // Add global buttons that match the condition
            for (const auto& globalBtn : globalHeaderButtons) {
                if (!globalBtn.shouldShow || globalBtn.shouldShow(colConfig)) {
                    buttonsToDraw.push_back(globalBtn);
                }
            }
            
            // Draw header buttons (right-aligned)
            if (!buttonsToDraw.empty()) {
                // Calculate total button width
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
                float totalButtonWidth = 0.0f;
                for (const auto& btn : buttonsToDraw) {
                    std::string btnLabel = btn.getDynamicLabel ? btn.getDynamicLabel() : btn.label;
                    float btnWidth = ImGui::CalcTextSize(btnLabel.c_str()).x + 
                                    ImGui::GetStyle().FramePadding.x * 2.0f;
                    totalButtonWidth += btnWidth;
                    if (&btn != &buttonsToDraw.back()) {
                        totalButtonWidth += 2.0f; // Spacing between buttons
                    }
                }
                
                float padding = ImGui::GetStyle().CellPadding.x;
                float cellMaxX = cellStartPos.x + columnWidth;
                float buttonStartX = cellMaxX - totalButtonWidth - padding;
                
                // Draw buttons from right to left (reverse order so leftmost button is drawn last)
                // This ensures proper right-alignment
                float currentX = buttonStartX;
                for (size_t btnIdx = 0; btnIdx < buttonsToDraw.size(); btnIdx++) {
                    const auto& btn = buttonsToDraw[btnIdx];
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
                    
                    // Move to next button position
                    currentX += btnWidth;
                    if (btnIdx < buttonsToDraw.size() - 1) {
                        currentX += 2.0f; // Spacing between buttons
                    }
                }
                
                ImGui::PopStyleVar();
            }
            
            // Legacy: Draw header buttons if callback is provided (for backward compatibility)
            if (callbacks.drawHeaderButton) {
                callbacks.drawHeaderButton((int)i, colConfig, cellStartPos, columnWidth);
            }
        }
        
        ImGui::PopID();
    }
}

void CellGrid::drawRow(int row, int numFixedColumns, bool isPlaybackRow, bool isEditRow, std::function<void(int row, int fixedColIndex)> drawFixedColumn) {
    if (!tableStarted) return;
    
    currentRow = row;
    ImGui::TableNextRow();
    
    // Auto-scroll to focused row when it changes
    // Conditions for auto-scroll:
    // 1. Auto-scroll is enabled
    // 2. We have a valid focused row
    // 3. The focused row matches the current row being drawn
    // 4. The focused row has changed since last scroll
    // 5. User is not actively editing (checked in getFocusedRow callback)
    
    // Performance optimization: Cache focused row per frame to avoid calling expensive callbacks for every row
    int currentFrame = ImGui::GetFrameCount();
    if (cachedFocusedRowFrame != currentFrame) {
        cachedFocusedRow = callbacks.getFocusedRow ? callbacks.getFocusedRow() : -1;
        cachedFocusedRowFrame = currentFrame;
    }
    
    int focusedRow = cachedFocusedRow;
    if (autoScrollEnabled && focusedRow >= 0 && focusedRow == row && focusedRow != lastFocusedRowForScroll) {
        // Scroll position: 0.4f positions the row at 40% from top (smoother and less aggressive than center)
        // This only scrolls when the focused row changes, allowing manual scrolling to work
        ImGui::SetScrollHereY(0.6f);
        lastFocusedRowForScroll = focusedRow;
    } else if (focusedRow < 0 && lastFocusedRowForScroll >= 0) {
        // Reset scroll tracking when focus is cleared or editing starts
        lastFocusedRowForScroll = -1;
    }
    
    // Call row start callback
    if (callbacks.onRowStart) {
        callbacks.onRowStart(row, isPlaybackRow, isEditRow);
    }
    
    // Draw fixed columns (if any)
    if (drawFixedColumn) {
        for (int i = 0; i < numFixedColumns; i++) {
            ImGui::TableSetColumnIndex(i);
            drawFixedColumn(row, i);
        }
    }
    
    // Draw parameter columns
    // CRITICAL: Pass absolute column indices to callbacks (col + numFixedColumns)
    // This eliminates the need for offset calculations in GUI classes
    for (size_t i = 0; i < columnConfig.size(); i++) {
        int absoluteCol = (int)i + numFixedColumns;  // Absolute column index in ImGui table
        ImGui::TableSetColumnIndex(absoluteCol);
        const auto& colConfig = columnConfig[i];
        
        // Check if there's a special column renderer
        // CRITICAL: drawSpecialColumn should only handle special cases (buttons, etc.)
        // For regular parameter columns, we should use the default CellWidget rendering
        // This ensures proper callback setup and input handling
        bool handledBySpecialRenderer = false;
        if (callbacks.drawSpecialColumn) {
            // Check if it's a special column that needs custom rendering:
            // - Button columns (mediaIndex, playStyle, polyphonyMode)
            // - Pattern chain columns (parameterName starts with "pattern_")
            // - Buttons column (for pattern chain D/+/− buttons)
            bool isButtonColumn = (colConfig.parameterName == "mediaIndex" || 
                                   colConfig.parameterName == "playStyle" || 
                                   colConfig.parameterName == "polyphonyMode");
            bool isPatternChainColumn = (colConfig.parameterName.find("pattern_") == 0);
            bool isButtonsColumn = (colConfig.parameterName == "buttons");
            
            if (isButtonColumn || isPatternChainColumn || isButtonsColumn) {
                // Special columns: use special renderer
                callbacks.drawSpecialColumn(row, absoluteCol, colConfig);
                handledBySpecialRenderer = true;
            } else {
                // Regular parameter columns: use default CellWidget rendering
                // This ensures proper callback setup and input handling
                handledBySpecialRenderer = false;
            }
        }
        
        if (!handledBySpecialRenderer) {
            // Default: draw CellWidget using retained widget cache
            // Get or create cell widget (retained across frames for performance)
            // Use absolute column index for widget cache key
            CellWidget& cell = getOrCreateCell(row, absoluteCol, colConfig);
            
            // Get initial focus hint from callback (optional - CellWidget will use actual ImGui focus)
            // This is just a hint for initial state; actual focus is determined by ImGui after drawing
            bool focusHint = callbacks.isCellFocused ? callbacks.isCellFocused(row, absoluteCol) : false;
                
            // Set up callbacks (always set, even if widget was recreated - defensive programming)
            // This ensures callbacks are always properly wired even after cache clearing
            if (callbacks.getCellValue) {
                int rowCapture = row;
                int colCapture = absoluteCol;  // Use absolute column index
                const CellGridColumnConfig colConfigCapture = colConfig;  // Capture colConfig for parameter name access
                cell.getCurrentValue = [this, rowCapture, colCapture, colConfigCapture]() -> float {
                    return callbacks.getCellValue(rowCapture, colCapture, colConfigCapture);
                };
            }
                
            if (callbacks.setCellValue) {
                int rowCapture = row;
                int colCapture = absoluteCol;  // Use absolute column index
                const CellGridColumnConfig colConfigCapture = colConfig;  // Capture colConfig for parameter name access
                cell.onValueApplied = [this, rowCapture, colCapture, colConfigCapture](const std::string&, float value) {
                    callbacks.setCellValue(rowCapture, colCapture, value, colConfigCapture);
                };
            }
            
            // Set up onEditModeChanged callback (CellWidget manages editing state internally)
            if (callbacks.onEditModeChanged) {
                int rowCapture = row;
                int colCapture = absoluteCol;  // Use absolute column index
                cell.onEditModeChanged = [this, rowCapture, colCapture](bool editing) {
                    callbacks.onEditModeChanged(rowCapture, colCapture, editing);
                };
            }
            
            // Draw cell - CellWidget manages its own state (editing, selection, buffer, drag)
            // Pass focus hint (CellWidget will use actual ImGui focus internally)
            int uniqueId = row * 1000 + absoluteCol;  // Use absolute column index for unique ID
            // Note: Refocus is now handled automatically by CellWidget when exiting edit mode
            
            // Create input context (simplified - no frame tracking needed)
            CellWidgetInputContext inputContext;
            
            // Draw cell - CellWidget will determine actual focus from ImGui after drawing
            CellWidgetInteraction interaction = cell.draw(uniqueId, focusHint, false, inputContext);
                
            // Handle interactions - check actual focus state after drawing
            // ImGui::IsItemFocused() works for the last item drawn
            bool actuallyFocusedAfterDraw = ImGui::IsItemFocused();
            
            // Notify GUI layer of focus changes (CellWidget signals this via interaction)
            // CRITICAL: Only call onCellFocusChanged when focus is actually GAINED, not when lost
            // This prevents the callback from incorrectly setting focus state when navigating away
            // The callback itself checks frame-based guard to prevent infinite loops
            // We rely solely on CellWidget's focusChanged signal as the authoritative source
            if (interaction.focusChanged && actuallyFocusedAfterDraw && callbacks.onCellFocusChanged) {
                ofLogNotice("CellGrid") << "[FOCUS_GAINED] Calling onCellFocusChanged (row=" << row 
                                       << ", col=" << absoluteCol << ") - interaction.focusChanged=true, actuallyFocused=true";
                callbacks.onCellFocusChanged(row, absoluteCol);
            }
            
            if (interaction.clicked && callbacks.onCellClicked) {
                callbacks.onCellClicked(row, absoluteCol);
            }
            
            // CellWidget manages its own state internally (editing, buffer, drag, selection)
            // GUI layer only needs to track which cell is focused for UI coordination
            // No state syncing needed - CellWidget persists its own state across frames
        }
    }
    
    // Call row end callback
    if (callbacks.onRowEnd) {
        callbacks.onRowEnd(row);
    }
}

void CellGrid::endTable() {
    if (!tableStarted) return;
    
    ImGui::EndTable();
    
    // Pop scrollbar size style var if scrolling was enabled
    if (scrollingEnabled) {
        ImGui::PopStyleVar();
    }
    
    // Pop styling
    ImGui::PopStyleVar(2);
    
    tableStarted = false;
    currentRow = -1;
}

void CellGrid::updateColumnIndices() {
    for (size_t i = 0; i < columnConfig.size(); i++) {
        columnConfig[i].columnIndex = (int)i;
    }
}

void CellGrid::clearCellCache() {
    cellWidgets.clear();
}

CellWidget& CellGrid::getOrCreateCell(int row, int col, const CellGridColumnConfig& colConfig) {
    auto key = std::make_pair(row, col);
    auto it = cellWidgets.find(key);
    
    if (it != cellWidgets.end()) {
        // Widget exists - return reference to existing widget
        return it->second;
    }
    
    // Widget doesn't exist - create new one using callback
    if (callbacks.createCellWidget) {
        CellWidget newCell = callbacks.createCellWidget(row, col, colConfig);
        // Insert and return reference to newly created widget
        auto result = cellWidgets.insert(std::make_pair(key, std::move(newCell)));
        return result.first->second;
    }
    
    // No callback provided - create empty widget
    auto result = cellWidgets.insert(std::make_pair(key, CellWidget()));
    return result.first->second;
}


void CellGrid::registerHeaderButton(int columnIndex, const HeaderButton& button) {
    if (columnIndex < 0) {
        // Register as global button
        globalHeaderButtons.push_back(button);
    } else {
        // Register for specific column
        headerButtons[columnIndex].push_back(button);
    }
}

void CellGrid::registerGlobalHeaderButton(const HeaderButton& button) {
    globalHeaderButtons.push_back(button);
}

void CellGrid::clearHeaderButtons(int columnIndex) {
    if (columnIndex < 0) {
        // Clear all buttons
        headerButtons.clear();
        globalHeaderButtons.clear();
    } else {
        // Clear buttons for specific column
        headerButtons.erase(columnIndex);
    }
}

// State management - REMOVED: All state (focus, drag, edit) is now managed by GUI layer via callbacks
// CellGrid is now a pure rendering component focused on table rendering

