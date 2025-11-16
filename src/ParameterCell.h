#pragma once

#include "ofMain.h"
#include <string>
#include <functional>

// Forward declarations for ImGui types
typedef unsigned int ImU32;

// Interaction result from ParameterCell::draw()
struct ParameterCellInteraction {
    bool clicked = false;
    bool focusChanged = false;
    bool dragStarted = false;
    bool dragEnded = false;
    bool shouldExitEarly = false;
    
    ParameterCellInteraction() = default;
};

// ParameterCell - Reusable editing widget for parameter values
// Supports keyboard input, drag editing, and visual feedback
class ParameterCell {
public:
    ParameterCell();
    
    // Configuration
    void setValueRange(float min, float max, float defaultValue);
    
    // Helper: Calculate optimal step increment based on parameter range and type
    // This can be called after setValueRange() to auto-configure stepIncrement
    void calculateStepIncrement();
    
    // Edit mode management
    void setEditing(bool e);
    void enterEditMode();
    void exitEditMode();
    bool isEditingMode() const { return isEditing; }
    bool isSelectedState() const { return isSelected; }
    
    // Edit buffer management
    void setEditBuffer(const std::string& buffer);
    void setEditBuffer(const std::string& buffer, bool initialized); // Overload to set both buffer and initialized flag
    const std::string& getEditBuffer() const { return editBuffer; }
    bool isEditBufferInitialized() const { return editBufferInitialized; }
    
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
    ParameterCellInteraction draw(int uniqueId,
                                  bool isFocused,
                                  bool shouldFocusFirst = false,
                                  bool shouldRefocusCurrentCell = false);
    
    // Drag editing
    void startDrag();
    void updateDrag();
    void endDrag();
    
    // Drag state management (for persistence across frames)
    bool getIsDragging() const { return isDragging; }
    void setDragState(bool dragging, float startY, float startX, float lastValue) {
        isDragging = dragging;
        dragStartY = startY;
        dragStartX = startX;
        lastDragValue = lastValue;
    }
    float getDragStartY() const { return dragStartY; }
    float getDragStartX() const { return dragStartX; }
    float getLastDragValue() const { return lastDragValue; }
    
    // Refocus state management (for maintaining focus after exiting edit mode)
    bool getShouldRefocus() const { return shouldRefocus; }
    
    // Callbacks - set these to connect to your data model
    std::function<float()> getCurrentValue;              // Get current value for display
    std::function<void(const std::string&, float)> onValueApplied;  // Called when value is applied
    std::function<void(const std::string&)> onValueRemoved;         // Called when parameter is removed
    std::function<std::string(float)> formatValue;       // Optional: custom formatter
    std::function<float(const std::string&)> parseValue; // Optional: custom parser
    std::function<int()> getMaxIndex;                    // For index columns: max index value
    
    // Configuration properties
    std::string parameterName;  // Parameter name (e.g., "position", "speed", "volume")
    bool isFixed = false;       // True for fixed columns (index, length)
    std::string fixedType;      // "index" or "length" for fixed columns
    bool isBool = false;        // True for boolean parameters
    bool isInteger = false;     // True for integer parameters (affects arrow key increments)
    float stepIncrement = 0.01f; // Step size for arrow key adjustments (0.001, 0.01, 0.1, or 1.0)
    
    // Value range
    float minVal = 0.0f;
    float maxVal = 1.0f;
    float defaultValue = 0.0f;
    
    // Selection state (can be set externally)
    bool isSelected = false;
    bool shouldRefocus = false;
    
private:
    // Constants
    static constexpr size_t MAX_EDIT_BUFFER_LENGTH = 50;
    static constexpr float DRAG_SENSITIVITY_PIXELS = 200.0f;
    static constexpr float EPSILON_DIVISION = 1e-9f;
    static constexpr int INDEX_MAX_DEFAULT = 127;
    static constexpr int LENGTH_MIN = 1;
    static constexpr int LENGTH_MAX = 16;
    static constexpr const char* FIXED_TYPE_INDEX = "index";
    static constexpr const char* FIXED_TYPE_LENGTH = "length";
    
    // Internal state
    bool isEditing = false;
    bool editBufferInitialized = false;
    bool bufferModifiedByUser = false;  // Track if buffer was modified by user input (vs initialized from current value)
    std::string editBuffer;
    
    // Frame tracking to prevent double-processing of input
    int lastProcessedFrame = -1;  // Track which frame we last processed input on
    
    // Drag state
    bool isDragging = false;
    float dragStartY = 0.0f;
    float dragStartX = 0.0f;
    float lastDragValue = 0.0f;
    
    // Internal methods
    void initializeEditBuffer();
    void applyEditValueFloat(float floatValue);
    void applyEditValueInt(int intValue);
    bool parseAndApplyEditBuffer();
    
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
    
    // Value removal helper
    void removeParameter();
    
    // Color helpers
    ImU32 getFillBarColor() const;
    ImU32 getRedOutlineColor() const;
    ImU32 getOrangeOutlineColor() const;
};


