#pragma once

#include "ofMain.h"
#include <string>
#include <functional>

// Forward declarations for ImGui types
typedef unsigned int ImU32;
struct ImVec2;

// CellWidget only supports SLIDER mode (numeric parameter editing)
// Button functionality is handled directly by GUI classes using ImGui::Button()

// Input context for CellWidget (simplified - no frame tracking needed)
// ImGui handles input state management internally
struct CellWidgetInputContext {
    // Empty struct - kept for API compatibility but no longer needed
    // ImGui's input system (IsKeyPressed, InputQueueCharacters) already handles
    // preventing duplicate processing within a frame
    CellWidgetInputContext() = default;
};

// Interaction result from CellWidget::draw()
struct CellWidgetInteraction {
    bool clicked = false;
    bool focusChanged = false;
    bool dragStarted = false;
    bool dragEnded = false;
    bool shouldExitEarly = false;
    bool needsRefocus = false;  // Signals that cell needs refocus after edit operations (e.g., Enter exits edit mode)
    
    CellWidgetInteraction() = default;
};

// CellWidget - Reusable editing widget for numeric parameter values
// Core responsibilities:
//   1. Display value (formatted text, fill bar visualization)
//   2. Handle keyboard input (typing, Enter, Escape, arrow keys, etc.)
//   3. Handle mouse drag for value adjustment
//   4. Call callbacks (onValueApplied, onEditModeChanged, etc.) to notify GUI layer
// 
// NOTE: CellWidget is a SELF-CONTAINED, REUSABLE widget that handles all input processing internally.
// GUI layers (TrackerSequencerGUI, MediaPoolGUI, etc.) only need to:
//   - Set up callbacks (onValueApplied, onEditModeChanged) to sync state
//   - Sync state TO cell before drawing (selection, edit mode, buffer cache)
//   - Sync state FROM cell after drawing (buffer cache for persistence)
// 
// This architecture makes CellWidget reusable across all modules without duplicating input logic.
// 
// Note: Focus management is handled by the GUI layer. CellWidget signals refocus needs
// via interaction.needsRefocus flag, but the GUI layer executes the actual refocus.
// 
// Supports numeric parameter editing with:
//   - Keyboard input (direct typing, Enter to confirm, Escape to cancel)
//   - Drag editing (mouse drag to adjust values)
//   - Expression evaluation (e.g., "1.5 + 0.3")
//   - Gamepad navigation (via ImGui's built-in navigation system)
class CellWidget {
public:
    CellWidget();
    
    // Configuration
    void setValueRange(float min, float max, float defaultValue);
    
    // Helper: Calculate optimal step increment based on parameter range and type
    // This can be called after setValueRange() to auto-configure stepIncrement
    void calculateStepIncrement();
    
    // Edit mode management
    void setEditing(bool e);
    void enterEditMode();
    void exitEditMode();
    bool isEditingMode() const { return editing_; }
    
    // Edit buffer management
    void setEditBuffer(const std::string& buffer);
    void setEditBuffer(const std::string& buffer, bool initialized); // Overload to set both buffer and initialized flag
    const std::string& getEditBuffer() const { return editBuffer_; }
    bool isEditBufferInitialized() const { return editBufferInitialized_; }
    
    // Keyboard input handling
    bool handleKeyPress(int key, bool ctrlPressed = false, bool shiftPressed = false);
    
    // Manual buffer manipulation
    void appendDigit(char digit);
    void appendChar(char c);
    void backspace();
    void deleteChar();
    
    // Edit operations
    void applyValue();
    void cancelEdit();
    void adjustValue(int delta, float customStepSize = 0.0f);  // customStepSize: 0.0f = use default stepIncrement
    
    // Display and formatting
    std::string formatDisplayText(float value) const;
    float calculateFillPercent(float value) const;
    
    // Drawing
    CellWidgetInteraction draw(int uniqueId,
                                  bool isFocused,
                                  bool shouldFocusFirst = false,
                                  bool shouldRefocusCurrentCell = false,
                                  const CellWidgetInputContext& inputContext = CellWidgetInputContext());
    
    // Drag editing
    void startDrag();
    void updateDrag();
    void endDrag();
    
    // Drag state management (for persistence across frames)
    bool getIsDragging() const { return dragging_; }
    void setDragState(bool dragging, float startY, float startX, float lastValue) {
        dragging_ = dragging;
        dragStartY_ = startY;
        dragStartX_ = startX;
        lastDragValue_ = lastValue;
    }
    float getDragStartY() const { return dragStartY_; }
    float getDragStartX() const { return dragStartX_; }
    float getLastDragValue() const { return lastDragValue_; }
    
    // Callbacks - set these to connect to your data model
    std::function<float()> getCurrentValue;              // Get current value for display
    std::function<void(const std::string&, float)> onValueApplied;  // Called when value is applied
    std::function<void(const std::string&)> onValueRemoved;         // Called when parameter is removed
    std::function<void(bool)> onEditModeChanged;        // Called when edit mode is entered (true) or exited (false)
    std::function<std::string(float)> formatValue;       // Optional: custom formatter
    std::function<float(const std::string&)> parseValue; // Optional: custom parser
    std::function<int()> getMaxIndex;                    // For index columns: max index value
    
    // Configuration properties
    std::string parameterName;  // Parameter name (e.g., "position", "speed", "volume")
    bool isRemovable = true;    // true if parameter can be removed/deleted (default: true). false for required columns like index/length
    bool isBool = false;        // True for boolean parameters
    bool isInteger = false;     // True for integer parameters (affects arrow key increments)
    float stepIncrement = 0.01f; // Step size for arrow key adjustments (0.001, 0.01, 0.1, or 1.0)
    
    // Value range
    float minVal = 0.0f;
    float maxVal = 1.0f;
    float defaultValue = 0.0f;
    
    // Selection state accessors
    void setSelected(bool selected) { selected_ = selected; }
    bool isSelected() const { return selected_; }
    
    // Refocus flag removed - use CellWidgetInteraction.needsRefocus instead
    
private:
    // Constants
    static constexpr size_t MAX_EDIT_BUFFER_LENGTH = 50;
    static constexpr float DRAG_SENSITIVITY_PIXELS = 200.0f;
    static constexpr float EPSILON_DIVISION = 1e-9f;
    static constexpr int INDEX_MAX_DEFAULT = 127;
    static constexpr int LENGTH_MIN = 1;
    static constexpr int LENGTH_MAX = 16;
    
    // Selection state (private - use accessors)
    bool selected_ = false;
    bool shouldRefocus_ = false;
    
    // Internal state
    bool editing_ = false;
    bool editBufferInitialized_ = false;
    bool bufferModifiedByUser_ = false;  // Track if buffer was modified by user input (vs initialized from current value)
    std::string editBuffer_;
    
    // Drag state
    bool dragging_ = false;
    float dragStartY_ = 0.0f;
    float dragStartX_ = 0.0f;
    float lastDragValue_ = 0.0f;
    
    // Internal methods
    void initializeEditBuffer();
    void applyEditValueFloat(float floatValue);
    void applyEditValueInt(int intValue);
    bool parseAndApplyEditBuffer();
    
    // Drawing helpers (extracted from draw() for better organization)
    CellWidgetInteraction drawSliderMode(int uniqueId, bool isFocused, bool shouldFocusFirst, bool shouldRefocusCurrentCell, const CellWidgetInputContext& inputContext, const ImVec2& cellMin, const ImVec2& cellMax);
    void processInputInDraw(bool actuallyFocused);  // Process keyboard input during draw
    void drawVisualFeedback(const ImVec2& cellMin, const ImVec2& cellMax, float fillPercent);
    
    // Helper methods
    std::string getDefaultFormatValue(float value) const;
    float getDefaultParseValue(const std::string& str) const;
    void applyDragValue(float newValue);
    
    // String utility helpers
    // Check if string represents empty/NaN value placeholder ("--")
    // The "--" string is used to represent NaN (empty cell, no value)
    static bool isEmpty(const std::string& str);
    static std::string trimWhitespace(const std::string& str);
    
    // ImGui state management helpers
    void disableImGuiKeyboardNav();
    void enableImGuiKeyboardNav();
    
    // Value removal helper (public so GUI can call it via callback if needed)
    void removeParameter();
    
    // Color helpers
    ImU32 getFillBarColor() const;
    ImU32 getRedOutlineColor() const;
    ImU32 getOrangeOutlineColor() const;
};


