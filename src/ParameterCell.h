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
    void adjustValue(int delta);
    
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
    // Internal state
    bool isEditing = false;
    bool editBufferInitialized = false;
    std::string editBuffer;
    
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
    
    // Color helpers
    ImU32 getFillBarColor();
    ImU32 getRedOutlineColor();
    ImU32 getOrangeOutlineColor();
};


