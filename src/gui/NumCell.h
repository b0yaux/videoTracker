#pragma once

#include "BaseCell.h"
#include "ofMain.h"
#include <string>
#include <functional>

// Forward declarations for ImGui types
typedef unsigned int ImU32;
struct ImVec2;

// Input context for NumCell (simplified - no frame tracking needed)
// ImGui handles input state management internally
struct NumCellInputContext {
    // Empty struct - kept for API compatibility but no longer needed
    // ImGui's input system (IsKeyPressed, InputQueueCharacters) already handles
    // preventing duplicate processing within a frame
    NumCellInputContext() = default;
};

// NumCell - Reusable editing widget for numeric parameter values (FLOAT and INT)
// Inherits from BaseCell for unified cell system
// Core responsibilities:
//   1. Display value (formatted text, fill bar visualization)
//   2. Handle keyboard input (typing, Enter, Escape, arrow keys, etc.)
//   3. Handle mouse drag for value adjustment
//   4. Call callbacks (onValueApplied, onEditModeChanged, etc.) to notify GUI layer
// 
// NOTE: NumCell is a SELF-CONTAINED, REUSABLE widget that handles all input processing internally.
// GUI layers (TrackerSequencerGUI, MediaPoolGUI, etc.) only need to:
//   - Set up callbacks (onValueApplied, onEditModeChanged) to sync state
//   - Sync state TO cell before drawing (selection, edit mode, buffer cache)
//   - Sync state FROM cell after drawing (buffer cache for persistence)
// 
// This architecture makes NumCell reusable across all modules without duplicating input logic.
// 
// Note: Focus management is handled by NumCell itself. When exiting edit mode,
// NumCell immediately refocuses the cell if it's still focused, eliminating delays.
// 
// Supports numeric parameter editing with:
//   - Keyboard input (direct typing, Enter to confirm, Escape to cancel)
//   - Drag editing (mouse drag to adjust values)
//   - Expression evaluation (e.g., "1.5 + 0.3", "+0.3" to add to current value)
//   - Gamepad navigation (via ImGui's built-in navigation system)
class NumCell : public BaseCell {
public:
    NumCell();
    
    // BaseCell interface implementation
    CellInteraction draw(int uniqueId, bool isFocused, bool shouldFocusFirst = false) override;
    void enterEditMode() override;
    void exitEditMode() override;
    bool isEditingMode() const override { return editing_; }
    bool isFocused() const override { return focused_; }
    bool isDragging() const override { return dragging_; }
    void configure(const ParameterDescriptor& desc,
                   std::function<float()> getter,
                   std::function<void(float)> setter,
                   std::function<void()> remover = nullptr,
                   std::function<std::string(float)> formatter = nullptr,
                   std::function<float(const std::string&)> parser = nullptr) override;
    
    // Configuration
    void setValueRange(float min, float max, float defaultValue);
    
    // Helper: Calculate optimal step increment based on parameter range and type
    // This can be called after setValueRange() to auto-configure stepIncrement
    void calculateStepIncrement();
    
    // Edit buffer management
    void setEditBuffer(const std::string& buffer);
    void setEditBuffer(const std::string& buffer, bool initialized); // Overload to set both buffer and initialized flag
    const std::string& getEditBuffer() const { return editBuffer_; }
    bool isEditBufferInitialized() const { return bufferState_ != EditBufferState::None; }
    
    // Keyboard input handling
    bool handleKeyPress(int key, bool ctrlPressed = false, bool shiftPressed = false);
    
    // Unified character input handling (for direct typing)
    // FIXED: When operator is typed as first character, prepends current value for relative operations
    bool handleCharacterInput(char c);
    
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
    
    // Numeric-specific callbacks (for direct float access - more efficient than string conversion)
    std::function<float()> getCurrentValue;              // Get current value for display
    std::function<void(const std::string&, float)> onValueAppliedFloat;  // Called when value is applied (float version)
    std::function<std::string(float)> formatValue;       // Optional: custom formatter
    std::function<float(const std::string&)> parseValue; // Optional: custom parser
    std::function<int()> getMaxIndex;                    // For index columns: max index value
    std::function<void(int, float)> customAdjustValue;  // Optional: custom adjustValue callback (overrides default behavior)
    
    // Configuration properties
    bool isInteger = false;     // True for integer parameters (affects arrow key increments)
    float stepIncrement = 0.01f; // Step size for arrow key adjustments (0.001, 0.01, 0.1, or 1.0)
    
    // Value range
    float minVal = 0.0f;
    float maxVal = 1.0f;
    float defaultValue = 0.0f;
    
private:
    // Constants
    static constexpr size_t MAX_EDIT_BUFFER_LENGTH = 50;
    static constexpr float DRAG_SENSITIVITY_PIXELS = 200.0f;
    static constexpr float EPSILON_DIVISION = 1e-9f;
    static constexpr int INDEX_MAX_DEFAULT = 127;
    static constexpr int LENGTH_MIN = 1;
    static constexpr int LENGTH_MAX = 16;
    
    // Buffer state management - simplified with single enum
    enum class EditBufferState {
        None,           // No buffer (empty)
        Initialized,    // Buffer initialized from current value
        Restored,       // Buffer restored from cache (user had typed something)
        UserModified    // Buffer modified by user input this frame
    };
    EditBufferState bufferState_ = EditBufferState::None;
    std::string editBuffer_;
    
    // Original value storage for buffer fallback
    float originalValue_ = NAN;  // Value before edit mode started
    
    // Focus management
    bool shouldRefocus_ = false;  // Flag to maintain focus after edit mode exit
    
    // Drag state
    bool dragging_ = false;
    float dragStartY_ = 0.0f;
    float dragStartX_ = 0.0f;
    float lastDragValue_ = 0.0f;
    
    // Arrow key repeat state (for smooth continuous movement when held)
    float arrowKeyRepeatTimer_ = 0.0f;  // Time since arrow key was first pressed
    float arrowKeyLastRepeatTime_ = 0.0f;  // Time of last repeat increment (relative to repeat start)
    static constexpr float ARROW_KEY_REPEAT_DELAY = 0.25f;  // Initial delay before repeat starts (seconds)
    static constexpr float ARROW_KEY_REPEAT_RATE = 0.05f;  // Repeat rate once started (seconds per increment)
    static constexpr float ARROW_KEY_REPEAT_RATE_SHIFT = 0.02f;  // Faster repeat rate when Shift is pressed (seconds per increment)
    
    // Internal methods
    void initializeEditBuffer();
    void applyEditValueFloat(float floatValue, bool updateBuffer = false);
    void applyEditValueInt(int intValue, bool updateBuffer = true);
    bool parseAndApplyEditBuffer();
    void applyBufferWithFallback();  // Apply buffer if valid, fallback to original if invalid
    
    // Drawing helpers (extracted from draw() for better organization)
    CellInteraction drawSliderMode(int uniqueId, bool isFocused, bool shouldFocusFirst, const NumCellInputContext& inputContext, const ImVec2& cellMin, const ImVec2& cellMax);
    void processInputInDraw(bool actuallyFocused);  // Process keyboard input during draw
    void drawVisualFeedback(const ImVec2& cellMin, const ImVec2& cellMax, float fillPercent);
    
    // Helper methods
    std::string getDefaultFormatValue(float value) const;
    float getDefaultParseValue(const std::string& str) const;
    void applyDragValue(float newValue);
    
    // Clear cell to empty state (unified for keyboard shortcuts and double-click)
    // In tracker context, empty state IS the default state (not in parameterValues map)
    void clearCell();
    
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
    
    // Cell-level clipboard operations (for individual cell copy/paste)
    // Static clipboard shared across all NumCell instances
    static std::string cellClipboard;
    
    // Clipboard operations for individual cell values
    void copyCellValue();  // Copy current cell value to clipboard (as formatted text)
    bool pasteCellValue(); // Paste from clipboard and apply to cell, returns true if successful
    void cutCellValue();   // Copy then clear cell
    
    // Color helpers
    ImU32 getFillBarColor() const;
    ImU32 getRedOutlineColor() const;
    ImU32 getOrangeOutlineColor() const;
    
    // Helper to convert float value to string for BaseCell::onValueApplied callback
    std::string floatToString(float value) const;
};

