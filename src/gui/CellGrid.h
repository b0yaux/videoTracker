#pragma once

#include "ofMain.h"
#include "Module.h"  // For ParameterDescriptor
#include "CellWidget.h"
#include "gui/HeaderPopup.h"
#include <imgui.h>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <utility>  // For std::pair

// Forward declarations
typedef unsigned int ImU32;
typedef unsigned int ImGuiID;

// Column configuration structure (compatible with TrackerSequencer::ColumnConfig)
struct CellGridColumnConfig {
    std::string parameterName;      // e.g., "position", "speed", "volume" (or "index", "length" for required)
    std::string displayName;        // e.g., "Position", "Speed", "Volume"
    bool isRemovable;               // true if column can be deleted (default: true). false for required columns like index/length
    bool isDraggable;               // true if column can be reordered (default: true)
    int columnIndex;                // Position in grid (0 = first column)
    
    CellGridColumnConfig() 
        : parameterName(""), displayName(""), isRemovable(true), isDraggable(true), columnIndex(0) {}
    
    CellGridColumnConfig(const std::string& param, const std::string& display, bool removable, int idx)
        : parameterName(param), displayName(display), isRemovable(removable), isDraggable(true), columnIndex(idx) {}
    
    CellGridColumnConfig(const std::string& param, const std::string& display, bool removable, int idx, bool draggable)
        : parameterName(param), displayName(display), isRemovable(removable), isDraggable(draggable), columnIndex(idx) {}
    
    // Equality operator for vector comparison
    bool operator==(const CellGridColumnConfig& other) const {
        return parameterName == other.parameterName &&
               displayName == other.displayName &&
               isRemovable == other.isRemovable &&
               isDraggable == other.isDraggable &&
               columnIndex == other.columnIndex;
    }
};

// Header button definition - modular button system for column headers
struct HeaderButton {
    std::string label;           // Button text (e.g., "R", "L", "N")
    std::string tooltip;         // Tooltip text
    std::function<void()> onClick;  // Callback when button is clicked
    std::function<bool(const CellGridColumnConfig&)> shouldShow;  // Condition to show button (nullptr = always show)
    std::function<std::string()> getDynamicLabel;  // Optional: get label dynamically (e.g., for cycling buttons)
    std::function<std::string()> getDynamicTooltip;  // Optional: get tooltip dynamically
    
    HeaderButton() {}
    HeaderButton(const std::string& lbl, const std::string& tip, std::function<void()> callback)
        : label(lbl), tooltip(tip), onClick(callback) {}
};

// Callback types for CellGrid
struct CellGridCallbacks {
    // Cell value access
    // col parameter is absolute column index (0-based, includes all columns in ImGui table)
    // colConfig provides parameter name for direct lookup (eliminates need for index conversion)
    std::function<float(int row, int col, const CellGridColumnConfig& colConfig)> getCellValue;  // Get current value for cell at (row, col)
    
    // Cell value modification
    // col parameter is absolute column index (0-based, includes all columns in ImGui table)
    // colConfig provides parameter name for direct lookup (eliminates need for index conversion)
    std::function<void(int row, int col, float value, const CellGridColumnConfig& colConfig)> setCellValue;  // Set value for cell at (row, col)
    
    // CellWidget creation (optional - CellGrid can create basic cells if not provided)
    // col parameter is absolute column index (0-based, includes all columns in ImGui table)
    std::function<CellWidget(int row, int col, const CellGridColumnConfig& colConfig)> createCellWidget;
    
    // Row rendering callbacks
    std::function<void(int row, bool isPlaybackRow, bool isEditRow)> onRowStart;  // Called before row is drawn
    std::function<void(int row)> onRowEnd;  // Called after row is drawn
    
    // Special column rendering (for buttons, step numbers, etc.)
    // col parameter is absolute column index (0-based, includes all columns in ImGui table)
    std::function<void(int row, int col, const CellGridColumnConfig& colConfig)> drawSpecialColumn;
    
    // Header rendering (legacy - use registerHeaderButton instead)
    // col parameter is parameter column index (0-based within parameter columns only)
    // NOTE: This is deprecated - use absolute column indices instead
    std::function<void(int col, const CellGridColumnConfig& colConfig, ImVec2 cellPos, float cellWidth)> drawHeaderButton;
    
    // Custom header rendering per column (returns true if header was drawn, false to use default)
    // col parameter is parameter column index (0-based within parameter columns only)
    // NOTE: This is deprecated - use absolute column indices instead
    std::function<bool(int col, const CellGridColumnConfig& colConfig, ImVec2 cellStartPos, float columnWidth, float cellMinY)> drawCustomHeader;
    
    // Focus management
    // col parameter is absolute column index (0-based, includes all columns in ImGui table)
    std::function<bool(int row, int col)> isCellFocused;  // Check if cell is focused (optional - CellGrid uses actual ImGui focus if not provided)
    std::function<void(int row, int col)> onCellFocusChanged;  // Called when focus changes
    std::function<void(int row, int col)> onCellClicked;  // Called when cell is clicked
    std::function<void(int row, int col, bool editing)> onEditModeChanged;  // Called when cell enters/exits edit mode (CellWidget manages editing state internally)
    
    // Header click callback (for focus clearing)
    // col parameter is parameter column index (0-based within parameter columns only)
    std::function<void(int col)> onHeaderClicked;  // Called when header is clicked
    
    // Custom column setup callback - allows full control over ImGui::TableSetupColumn
    // 
    // USAGE: Set this callback to customize column sizing policy on a per-column basis.
    // This exposes ImGui's full TableSetupColumn API for maximum flexibility.
    // 
    // When to use:
    //   - Need mixed fixed/stretch columns (e.g., first columns fixed, rest stretch)
    //   - Need custom column widths or weights
    //   - Need to apply specific ImGuiTableColumnFlags per column
    // 
    // Parameters:
    //   - colIndex: parameter column index (0-based within parameter columns only)
    //   - colConfig: column configuration (contains parameterName, displayName, etc.)
    //   - absoluteColIndex: absolute column index in ImGui table (includes fixed columns)
    // 
    // Return: 
    //   - true: column was set up by callback (CellGrid will skip default setup)
    //   - false: use default behavior (all columns stretch with equal weight)
    // 
    // Example - Fixed width for specific columns, stretch for others:
    //   callbacks.setupParameterColumn = [](int colIndex, const CellGridColumnConfig& colConfig, int absoluteColIndex) -> bool {
    //       ImGuiTableColumnFlags flags = 0;
    //       float widthOrWeight = 0.0f;
    //       
    //       if (colConfig.parameterName == "index" || colConfig.parameterName == "length") {
    //           flags = ImGuiTableColumnFlags_WidthFixed;
    //           widthOrWeight = 45.0f;  // Fixed width in pixels
    //       } else {
    //           flags = ImGuiTableColumnFlags_WidthStretch;
    //           widthOrWeight = 1.0f;   // Stretch weight
    //       }
    //       
    //       if (!colConfig.isDraggable) {
    //           flags |= ImGuiTableColumnFlags_NoReorder;
    //       }
    //       
    //       ImGui::TableSetupColumn(colConfig.displayName.c_str(), flags, widthOrWeight);
    //       return true;
    //   };
    std::function<bool(int colIndex, const CellGridColumnConfig& colConfig, int absoluteColIndex)> setupParameterColumn;
    
    // Auto-scroll management
    std::function<int()> getFocusedRow;  // Get currently focused row (-1 if none)
};

// CellGrid - Reusable table component for parameter grids
// Supports both TrackerSequencer-style (multi-row) and MediaPool-style (single-row) tables
class CellGrid {
public:
    CellGrid();
    ~CellGrid();
    
    // Configuration
    void setTableId(const std::string& id) { tableId = id; }
    void setAvailableParameters(const std::vector<ParameterDescriptor>& params) { availableParameters = params; }
    void setColumnConfiguration(const std::vector<CellGridColumnConfig>& config);
    void setCallbacks(const CellGridCallbacks& callbacks) { this->callbacks = callbacks; }
    
    // Column management
    void addColumn(const std::string& parameterName, const std::string& displayName, int position = -1);
    void removeColumn(int columnIndex);
    void reorderColumn(int fromIndex, int toIndex);
    bool isColumnFixed(int columnIndex) const;
    const CellGridColumnConfig& getColumnConfig(int columnIndex) const;
    int getColumnCount() const { return (int)columnConfig.size(); }
    const std::vector<CellGridColumnConfig>& getColumnConfiguration() const { return columnConfig; }
    
    // Table flags and styling
    void setTableFlags(ImGuiTableFlags flags) { tableFlags = flags; }
    void enableScrolling(bool enable, float height = 0.0f);  // height = 0 means auto-calculate
    void setScrollbarSize(float size) { scrollbarSize = size; }
    void setCellPadding(ImVec2 padding) { cellPadding = padding; }
    void setItemSpacing(ImVec2 spacing) { itemSpacing = spacing; }
    
    // Drawing
    void beginTable(int numRows, int numFixedColumns = 0);  // numFixedColumns = columns before parameter columns (e.g., step number)
    void setupFixedColumn(int index, const std::string& label, float width = 0.0f, bool isStretch = false, float weight = 1.0f);
    void setupParameterColumns();  // Setup parameter columns based on columnConfig
    void drawHeaders(int numFixedColumns = 0, 
                     std::function<void(int fixedColIndex)> drawFixedColumnHeader = nullptr);  // Optional callback to draw fixed column headers
    void drawRow(int row, int numFixedColumns = 0, 
                 bool isPlaybackRow = false, bool isEditRow = false,
                 std::function<void(int row, int fixedColIndex)> drawFixedColumn = nullptr);  // Optional callback to draw fixed columns
    void endTable();
    
    // Features
    void enableReordering(bool enable) { reorderingEnabled = enable; }
    void enableAutoScroll(bool enable) { autoScrollEnabled = enable; }
    
    // Header button management (modular system)
    void registerHeaderButton(int columnIndex, const HeaderButton& button);  // Register button for specific column
    void registerGlobalHeaderButton(const HeaderButton& button);  // Register button for all columns (with condition)
    void clearHeaderButtons(int columnIndex = -1);  // Clear buttons for column (-1 = all columns)
    
    // Widget cache management (for retained widgets across frames)
    void clearCellCache();  // Clear all cached cell widgets (call when grid structure changes)
    
    // State management - REMOVED: All state (focus, drag, edit buffer) is managed by CellWidget itself
    // CellGrid is now a pure rendering component focused on table layout and rendering
    // GUI classes read state from CellWidgetInteraction results, no state syncing needed
    
private:
    // Configuration
    std::string tableId;
    std::vector<CellGridColumnConfig> columnConfig;
    std::vector<ParameterDescriptor> availableParameters;
    CellGridCallbacks callbacks;
    
    // Table settings
    ImGuiTableFlags tableFlags;
    bool scrollingEnabled;
    float tableHeight;
    float scrollbarSize;
    ImVec2 cellPadding;
    ImVec2 itemSpacing;
    
    // Features
    bool reorderingEnabled;
    bool autoScrollEnabled;
    
    // Header buttons (modular system)
    // Map: columnIndex -> vector of buttons
    std::map<int, std::vector<HeaderButton>> headerButtons;
    std::vector<HeaderButton> globalHeaderButtons;  // Buttons that apply to all columns (with conditions)
    
    // Auto-scroll state
    int lastFocusedRowForScroll;
    
    // Internal state
    bool tableStarted;
    int currentRow;
    int numRows;
    int numFixedColumns;
    
    // Fixed column configuration
    struct FixedColumnSetup {
        std::string label;
        float width;
        bool isStretch;
        float weight;
    };
    std::vector<FixedColumnSetup> fixedColumnSetups;
    
    // Widget cache (retained across frames for performance and state preservation)
    // Key: (row, col) pair, Value: CellWidget instance
    std::map<std::pair<int, int>, CellWidget> cellWidgets;
    
    // Internal state management - REMOVED: All state managed by CellWidget itself
    
    // Helper methods
    void updateColumnIndices();
    CellWidget& getOrCreateCell(int row, int col, const CellGridColumnConfig& colConfig);
};

