#include "CellWidget.h"
#include "ExpressionParser.h"
#include "gui/GUIConstants.h"
#include <imgui.h>
#include "ofLog.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <limits>

CellWidget::CellWidget() 
    : selected_(false), editing_(false), editBufferInitialized_(false), bufferModifiedByUser_(false),
      dragging_(false), dragStartY_(0.0f), dragStartX_(0.0f), lastDragValue_(0.0f) {
}

// Helper function implementations
// Check if string represents empty/NaN value placeholder ("--")
// The "--" string is used to represent NaN (empty cell, no value)
bool CellWidget::isEmpty(const std::string& str) {
    if (str.empty()) return false;
    for (char c : str) {
        if (c != '-') return false;
    }
    return true;
}

std::string CellWidget::trimWhitespace(const std::string& str) {
    size_t first = str.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
}

void CellWidget::disableImGuiKeyboardNav() {
    // DEPRECATED: Navigation is no longer disabled to support gamepad navigation
    // This function is kept for backward compatibility but does nothing
    // Navigation remains enabled at all times for gamepad/keyboard support
}

void CellWidget::enableImGuiKeyboardNav() {
    // DEPRECATED: Navigation is no longer disabled, so no need to re-enable
    // This function is kept for backward compatibility but does nothing
    // Navigation remains enabled at all times for gamepad/keyboard support
}

void CellWidget::removeParameter() {
    if (onValueRemoved) {
        onValueRemoved(parameterName);
    }
}

void CellWidget::setValueRange(float min, float max, float defaultValue) {
    if (min > max) {
        ofLogWarning("CellWidget") << "Invalid range: min > max, swapping values";
        std::swap(min, max);
    }
    minVal = min;
    maxVal = max;
    this->defaultValue = std::max(min, std::min(max, defaultValue));
}

void CellWidget::calculateStepIncrement() {
    // Calculate optimal step increment based on parameter type and range
    if (isInteger) {
        // Integer parameters: always use 1.0
        stepIncrement = 1.0f;
    } else {
        // Float parameter: unified 0.001 precision for all float parameters
        // This provides consistent fine-grained control across all parameters
        // (position, speed, volume, etc. all use the same precision)
        stepIncrement = 0.001f;
    }
}

void CellWidget::setEditing(bool e) {
    if (e && !editing_) {
        enterEditMode();
    } else if (!e && editing_) {
        exitEditMode();
    }
}

void CellWidget::setEditBuffer(const std::string& buffer) {
    editBuffer_ = buffer;
    if (!editBuffer_.empty()) {
        // If setting a non-empty buffer, ensure we're in edit mode
        if (!editing_) {
            editing_ = true;
            // Don't call enterEditMode() here as it would re-initialize the buffer
            // Just set editing flag - navigation remains enabled for gamepad support
        }
        // If buffer is non-empty and being restored from cache, it means user has modified it
        // This ensures subsequent characters append rather than replace
        bufferModifiedByUser_ = true;
    }
}

void CellWidget::setEditBuffer(const std::string& buffer, bool initialized) {
    editBuffer_ = buffer;
    editBufferInitialized_ = initialized;
    if (!editBuffer_.empty()) {
        // If setting a non-empty buffer, ensure we're in edit mode
        if (!editing_) {
            editing_ = true;
            // Don't call enterEditMode() here as it would re-initialize the buffer
            // Just set editing flag - navigation remains enabled for gamepad support
        }
        // CRITICAL: When restoring buffer from cache, we need to determine if user has modified it
        // The `initialized` flag tells us if the buffer was initialized from current value
        // But if we're restoring from cache, the buffer has already been modified by user input
        // So: if buffer is non-empty and we're restoring (not just initializing), user has modified it
        // We can't distinguish "just initialized" from "restored from cache" with just the initialized flag
        // Solution: If buffer is non-empty when restoring, assume user has modified it
        // This ensures subsequent characters append rather than replace
        // The only time bufferModifiedByUser_ should be false is when we just entered edit mode
        // and the buffer matches the formatted current value (handled by enterEditMode())
        bufferModifiedByUser_ = true;  // Assume modified when restoring non-empty buffer
    } else {
        // Empty buffer: reset flags
        bufferModifiedByUser_ = false;
    }
}

void CellWidget::enterEditMode() {
    bool wasEditing = editing_;
    editing_ = true;
    initializeEditBuffer();
    editBufferInitialized_ = true;
    bufferModifiedByUser_ = false;  // Buffer was initialized, not modified by user yet
    
    // Notify GUI layer of edit mode change
    if (!wasEditing && onEditModeChanged) {
        onEditModeChanged(true);
    }
}

void CellWidget::exitEditMode() {
    bool wasEditing = editing_;
    editing_ = false;
    editBuffer_.clear();
    editBufferInitialized_ = false;
    bufferModifiedByUser_ = false;  // Reset flag when exiting edit mode
    
    // Notify GUI layer of edit mode change
    if (wasEditing && onEditModeChanged) {
        onEditModeChanged(false);
    }
}

bool CellWidget::handleKeyPress(int key, bool ctrlPressed, bool shiftPressed) {
    // Enter key behavior
    if (key == OF_KEY_RETURN) {
        if (ctrlPressed || shiftPressed) {
            // Ctrl+Enter or Shift+Enter: Exit edit mode
            exitEditMode();
            return true;
        }
        
        if (editing_) {
            // In edit mode: Confirm and exit edit mode
            applyValue();
            exitEditMode();
            // Signal refocus needed - GUI layer will handle refocus on next frame
            // This maintains cell focus after exiting edit mode (normal cell focus, not edit mode)
            return true;  // Return true to indicate handled, refocus will be signaled via needsRefocus
        } else if (isSelected()) {
            // Enter edit mode
            enterEditMode();
            return true;
        }
        return false;
    }
    
    // Escape: Exit edit mode
    // IMPORTANT: Only handle ESC when in edit mode. When NOT in edit mode, let ESC pass through
    // to ImGui so it can use ESC to escape contained navigation contexts (like scrollable tables)
    if (key == OF_KEY_ESC) {
        if (editing_) {
            cancelEdit();
            return true;
        }
        // NOT in edit mode: Let ESC pass through to ImGui for navigation escape
        return false;
    }
    
    // Backspace: Delete last character in edit buffer
    if (key == OF_KEY_BACKSPACE) {
        if (editing_ && !editBuffer_.empty()) {
            editBuffer_.pop_back();
            editBufferInitialized_ = false;
            bufferModifiedByUser_ = true;  // User modified the buffer
            
            // Re-apply value after backspace (Blender-style reactive editing)
            // This allows the value to update as the user corrects their input
            if (editBuffer_.empty() || isEmpty(editBuffer_)) {
                // Buffer is empty or only dashes - remove parameter (set to "none")
                removeParameter();
            } else {
                try {
                    // Try to evaluate as expression (supports operations)
                    float floatValue = ExpressionParser::evaluate(editBuffer_);
                    applyEditValueFloat(floatValue);
                } catch (...) {
                    // Expression invalid - remove parameter (set to "none")
                    // This handles invalid expressions
                    removeParameter();
                }
            }
            return true;
        }
        return false;
    }
    
    // Delete key: Clear edit buffer
    if (key == OF_KEY_DEL) {
        if (editing_) {
            editBuffer_.clear();
            editBufferInitialized_ = false;
            bufferModifiedByUser_ = true;  // User modified the buffer (cleared it)
            return true;
        }
        return false;
    }
    
    // Numeric input (0-9) - Blender-style: direct typing enters edit mode and replaces value
    if (key >= '0' && key <= '9') {
        bool justEnteredEditMode = false;
        if (!editing_) {
            // Auto-enter edit mode if cell is selected
            if (isSelected()) {
                // CRITICAL: If buffer is already set (restored from cache), don't call enterEditMode()
                // as it would overwrite the restored buffer. Instead, just set editing_ and preserve the buffer.
                if (editBuffer_.empty() || !bufferModifiedByUser_) {
                    // Buffer is empty or not modified yet - safe to call enterEditMode()
                    enterEditMode();
                    justEnteredEditMode = true;
                } else {
                    // Buffer is already set (restored from cache) - just enable edit mode without reinitializing
                    editing_ = true;
                    // Navigation remains enabled for gamepad support
                    // Don't set justEnteredEditMode - we want to preserve the buffer
                }
            } else {
                return false;  // Not selected, don't handle
            }
        }
        
        // Clear buffer if we just entered edit mode or buffer is empty/placeholder
        // This ensures typing REPLACES the initialized value when starting to type
        // CRITICAL: Don't clear if buffer was already modified by user (restored from cache)
        bool shouldClear = false;
        if (justEnteredEditMode) {
            // Only clear if buffer hasn't been modified by user yet
            // If bufferModifiedByUser_ is true, it means we're restoring from cache, so don't clear
            if (!bufferModifiedByUser_) {
                shouldClear = true;
            }
        } else if (isEmpty(editBuffer_)) {
            shouldClear = true;
        } else if (editBufferInitialized_ && !bufferModifiedByUser_) {
            shouldClear = true;
        }
        
        if (shouldClear) {
            editBuffer_.clear();
            editBufferInitialized_ = false;
        }
        
        // Append digit to buffer
        editBuffer_ += (char)key;
        bufferModifiedByUser_ = true;  // Mark that user has modified the buffer
        if (editBuffer_.length() > MAX_EDIT_BUFFER_LENGTH) {
            editBuffer_ = editBuffer_.substr(editBuffer_.length() - MAX_EDIT_BUFFER_LENGTH);
        }
        
        // Apply value immediately (Blender-style reactive editing)
        // Try to evaluate as expression (supports operations like "2*3", "10/2", etc.)
        if (!editBuffer_.empty()) {
            if (isEmpty(editBuffer_)) {
                // Only dashes (e.g., "-", "--") - remove parameter (set to "none")
                removeParameter();
            } else {
                try {
                    float floatValue = ExpressionParser::evaluate(editBuffer_);
                    applyEditValueFloat(floatValue);
                } catch (const std::exception& e) {
                    // Invalid input - interpret as NaN/'--' (clear parameter)
                    removeParameter();
                } catch (...) {
                    // Invalid input - interpret as NaN/'--' (clear parameter)
                    removeParameter();
                }
            }
        }
        return true;
    }
    
    // Mathematical operators: +, *, /
    if (key == '+' || key == '*' || key == '/') {
        if (!editing_) {
            // Auto-enter edit mode if cell is selected
            if (isSelected()) {
                enterEditMode();
                // Clear buffer if it's "--" (placeholder) - typing should replace it
                if (isEmpty(editBuffer_)) {
                    editBuffer_.clear();
                    editBufferInitialized_ = false;
                }
                // Otherwise, don't clear buffer - allow appending operator to existing value
                // This allows operations like "5*2" or "10/2"
            } else {
                return false;  // Not selected, don't handle
            }
        } else {
            // Already in edit mode - clear buffer if it's "--" (placeholder)
            if (isEmpty(editBuffer_)) {
                editBuffer_.clear();
                editBufferInitialized_ = false;
            }
        }
        
        // Append operator to buffer
        editBuffer_ += (char)key;
        bufferModifiedByUser_ = true;  // User modified the buffer
        if (editBuffer_.length() > MAX_EDIT_BUFFER_LENGTH) {
            editBuffer_ = editBuffer_.substr(editBuffer_.length() - MAX_EDIT_BUFFER_LENGTH);
        }
        
        // Try to evaluate expression if it's complete
        // For operators, we wait for the next number before evaluating
        // But we can try to evaluate if the expression is already valid
        if (!editBuffer_.empty()) {
            // Check if buffer contains only operators/dashes
            bool onlyOpsOrDashes = true;
            for (char c : editBuffer_) {
                if (c != '-' && c != '+' && c != '*' && c != '/') {
                    onlyOpsOrDashes = false;
                    break;
                }
            }
            
            if (onlyOpsOrDashes) {
                // Only operators/dashes - remove parameter (set to "none")
                removeParameter();
            } else {
                try {
                    float floatValue = ExpressionParser::evaluate(editBuffer_);
                    applyEditValueFloat(floatValue);
                } catch (const std::exception& e) {
                    // Invalid input - interpret as NaN/'--' (clear parameter)
                    removeParameter();
                } catch (...) {
                    // Invalid input - interpret as NaN/'--' (clear parameter)
                    removeParameter();
                }
            }
        }
        return true;
    }
    
    // Decimal point and minus sign (can be negative number or subtraction)
    if (key == '.' || key == '-') {
        // For integer columns, don't allow decimal point input
        if (key == '.' && isInteger) {
            // Ignore decimal point for integer columns - they should only accept whole numbers
            return true;  // Consume the event but don't add decimal point
        }
        
        if (!editing_) {
            // Auto-enter edit mode if cell is selected
            if (isSelected()) {
                enterEditMode();
                // Clear buffer when entering edit mode via decimal/minus (replaces current value)
                editBuffer_.clear();
                editBufferInitialized_ = false;
            } else {
                return false;  // Not selected, don't handle
            }
        }
        
        // Clear buffer if it's "--" (placeholder) - typing should replace it
        // This ensures typing replaces "--" even if we entered edit mode via Enter key
        if (isEmpty(editBuffer_)) {
            editBuffer_.clear();
            editBufferInitialized_ = false;
        }
        // NOTE: We do NOT clear the buffer if already in edit mode with actual content - this allows:
        // - Typing decimals after numbers (e.g., "1.5")
        // - Using backspace to correct input
        
        // Allow decimal point and minus sign in edit buffer
        // For minus: allow at start (negative number) or as subtraction operator
        // The expression evaluator will handle distinguishing between negative and subtraction
        if (key == '-') {
            // Always allow minus - expression evaluator will handle it correctly
            // (negative at start/after operator, subtraction otherwise)
        }
        
        // Only allow one decimal point per number (but allow multiple in expression like "1.5*2.3")
        if (key == '.') {
            // Find the last number in the buffer (after last operator)
            size_t lastOp = editBuffer_.find_last_of("+-*/");
            std::string lastNumber = (lastOp == std::string::npos) ? editBuffer_ : editBuffer_.substr(lastOp + 1);
            if (lastNumber.find('.') != std::string::npos) {
                return true;  // This number already has a decimal point
            }
        }
        
        editBuffer_ += (char)key;
        bufferModifiedByUser_ = true;  // User modified the buffer
        if (editBuffer_.length() > MAX_EDIT_BUFFER_LENGTH) {
            editBuffer_ = editBuffer_.substr(editBuffer_.length() - MAX_EDIT_BUFFER_LENGTH);
        }
        
        // Apply value immediately (Blender-style)
        // Check if buffer is empty, single '.', or contains only dashes
        // CRITICAL: Handle '--' explicitly as clear/reset command
        if (editBuffer_ == "--") {
            // User typed '--' explicitly - remove parameter (set to "none"/rest)
            removeParameter();
        } else if (editBuffer_.empty() || editBuffer_ == "." || isEmpty(editBuffer_)) {
            // Buffer is only dashes, empty, or single '.' - remove parameter (set to "none")
            removeParameter();
        } else {
            try {
                // Try to evaluate as expression (supports operations)
                float floatValue = ExpressionParser::evaluate(editBuffer_);
                applyEditValueFloat(floatValue);
            } catch (...) {
                // Expression invalid - remove parameter (set to "none")
                // This handles invalid expressions like "abc", "2**3", etc.
                removeParameter();
            }
        }
        return true;
    }
    
    // Arrow keys in edit mode: Adjust values ONLY (no navigation)
    // CRITICAL: When editing, arrow keys must ONLY adjust values, never navigate
    // This ensures focus stays locked to the editing cell
    // Multi-precision: Shift = fine precision (0.001), standard = range-based increment
    if (editing_) {
        if (key == OF_KEY_UP || key == OF_KEY_DOWN || key == OF_KEY_LEFT || key == OF_KEY_RIGHT) {
            // Adjust value based on arrow direction
            int delta = 0;
            if (key == OF_KEY_UP || key == OF_KEY_RIGHT) {
                delta = 1;  // Up/Right = increase
            } else {
                delta = -1; // Down/Left = decrease
            }
            
            // Multi-precision arrow key adjustment: check modifier keys
            // Shift: Unified fine precision (0.001 per arrow key press)
            // Standard: Range-based increment for practical traversal (rangeSize/100 per press)
            // This matches the drag system's multi-precision approach
            float stepSize;
            if (isInteger) {
                // Integer parameters: Always 1 step per arrow key (modifiers don't affect integers)
                stepSize = 1.0f;
            } else {
                // Float parameters: Multi-precision based on modifier keys
                ImGuiIO& io = ImGui::GetIO();
                bool shiftPressed = (io.KeyMods & ImGuiMod_Shift) != 0;
                
                if (shiftPressed) {
                    // Shift: Unified fine precision (0.001 per arrow key) for precise adjustments
                    stepSize = 0.001f;
                } else {
                    // Standard: Practical increment for full-range traversal (rangeSize/100 per arrow key)
                    // This allows traversing full range in ~100 arrow key presses while maintaining reasonable precision
                    float rangeSize = maxVal - minVal;
                    stepSize = rangeSize / 100.0f;
                }
            }
            
            adjustValue(delta, stepSize);
            // Always return true to consume the event and prevent navigation
            return true;
        }
    }
    
    return false;
}

void CellWidget::appendDigit(char digit) {
    if (!editing_) {
        enterEditMode();
    }
    editBuffer_ += digit;
    bufferModifiedByUser_ = true;  // User modified the buffer
    if (editBuffer_.length() > MAX_EDIT_BUFFER_LENGTH) {
        editBuffer_ = editBuffer_.substr(editBuffer_.length() - MAX_EDIT_BUFFER_LENGTH);
    }
}

void CellWidget::appendChar(char c) {
    if (!editing_) {
        enterEditMode();
    }
    editBuffer_ += c;
    bufferModifiedByUser_ = true;  // User modified the buffer
    if (editBuffer_.length() > MAX_EDIT_BUFFER_LENGTH) {
        editBuffer_ = editBuffer_.substr(editBuffer_.length() - MAX_EDIT_BUFFER_LENGTH);
    }
}

void CellWidget::backspace() {
    if (editing_ && !editBuffer_.empty()) {
        editBuffer_.pop_back();
        editBufferInitialized_ = false;
        bufferModifiedByUser_ = true;  // User modified the buffer
    }
}

void CellWidget::deleteChar() {
    if (editing_) {
        editBuffer_.clear();
        bufferModifiedByUser_ = true;  // User modified the buffer
        editBufferInitialized_ = false;
    }
}

void CellWidget::applyValue() {
    parseAndApplyEditBuffer();
}

void CellWidget::cancelEdit() {
    exitEditMode();
}

void CellWidget::adjustValue(int delta, float customStepSize) {
    if (!getCurrentValue) return;
    
    float currentVal = getCurrentValue();
    
    // If current value is NaN (not set), start from default value or middle of range
    if (std::isnan(currentVal)) {
        // Use default value if available, otherwise use middle of range
        if (defaultValue >= minVal && defaultValue <= maxVal) {
            currentVal = defaultValue;
        } else {
            currentVal = (minVal + maxVal) / 2.0f;
        }
    }
    
    float stepSize;
    
    // Use custom step size if provided (for multi-precision arrow keys), otherwise use configured step increment
    // Custom step size is used when arrow keys are pressed with modifier keys (Shift for fine precision)
    // Default step increment is set in createParameterCellForColumn based on:
    // - Integer parameters: 1.0
    // - Float parameters: 0.001 (unified precision for all float parameters)
    if (customStepSize > 0.0f) {
        stepSize = customStepSize;
    } else {
        stepSize = stepIncrement;
    }
    
    float newValue = currentVal + (delta * stepSize);
    
    // For integer parameters, round to nearest integer
    if (isInteger) {
        newValue = std::round(newValue);
    }
    
    newValue = std::max(minVal, std::min(maxVal, newValue));
    
    // Update edit buffer with new value
    if (formatValue) {
        editBuffer_ = formatValue(newValue);
    } else {
        editBuffer_ = getDefaultFormatValue(newValue);
    }
    editBufferInitialized_ = false;
    
    // Apply immediately
    applyEditValueFloat(newValue);
}

void CellWidget::initializeEditBuffer() {
    if (!getCurrentValue) {
        editBuffer_.clear();
        return;
    }
    
    float currentVal = getCurrentValue();
    
    // Use formatValue callback if available (moved tracker-specific formatting to callbacks)
    if (formatValue) {
        editBuffer_ = formatValue(currentVal);
    } else {
        editBuffer_ = getDefaultFormatValue(currentVal);
    }
}

std::string CellWidget::formatDisplayText(float value) const {
    // Check for NaN (not a number) - indicates empty/not set (show "--")
    // This represents "none" state - let MediaPool handle the parameter
    // Using NaN allows parameters with negative ranges (like speed -10 to 10) to distinguish
    // between "not set" (NaN/--) and explicit values like 1.0 or -1.0
    if (std::isnan(value)) {
        return "--";
    }
    
    // Use custom formatValue callback if available (allows for tracker-specific formatting, logarithmic mapping, etc.)
    if (formatValue) {
        return formatValue(value);
    }
    
    // Default formatting (fallback if no callback provided)
    return getDefaultFormatValue(value);
}

float CellWidget::calculateFillPercent(float value) const {
    // Check for NaN (not a number) - indicates empty/not set (no fill)
    // Unified system: all empty values (Index, Length, dynamic parameters) use NaN
    if (std::isnan(value)) {
        return 0.0f;
    }
    
    float rangeSize = maxVal - minVal;
    if (rangeSize > 0.0f) {
        float fillPercent = (value - minVal) / rangeSize;
        return std::max(0.0f, std::min(1.0f, fillPercent));
    }
    return 0.0f;
}

void CellWidget::applyEditValueFloat(float floatValue) {
    // For integer parameters, round and clamp to integer range
    if (isInteger) {
        int intValue = (int)std::round(floatValue);
        // Clamp to valid range
        int clampedValue = std::max((int)minVal, std::min((int)maxVal, intValue));
        applyEditValueInt(clampedValue);
        return;
    }
    
    // For float parameters, check range
    // If value is outside range, remove parameter (set to "none" state) for removable parameters
    // This allows users to clear invalid values by typing out-of-range numbers
    if (floatValue < minVal || floatValue > maxVal) {
        if (isRemovable) {
            // Value is outside valid range - remove parameter
            removeParameter();
        }
        // For non-removable parameters, clamp to range instead
        else {
            float clampedValue = std::max(minVal, std::min(maxVal, floatValue));
            if (onValueApplied) {
                onValueApplied(parameterName, clampedValue);
            }
        }
    } else {
        // Value is within range - apply it
        if (onValueApplied) {
            onValueApplied(parameterName, floatValue);
        }
    }
}

void CellWidget::applyEditValueInt(int intValue) {
    // Apply integer value (callbacks handle formatting)
    if (onValueApplied) {
        onValueApplied(parameterName, (float)intValue);
    }
    // Update edit buffer using formatValue callback if available
    if (formatValue) {
        editBuffer_ = formatValue((float)intValue);
    } else {
        // Fallback: format as integer
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", intValue);
        editBuffer_ = buf;
    }
}

bool CellWidget::parseAndApplyEditBuffer() {
    // Trim whitespace for comparison
    std::string trimmed = trimWhitespace(editBuffer_);
    
    // Handle empty buffer or invalid input
    // For removable parameters, empty buffer removes the parameter
    // For non-removable parameters, empty buffer is invalid
    if (trimmed.empty() || isEmpty(trimmed)) {
        if (isRemovable) {
            // Empty buffer or only dashes - remove parameter
            removeParameter();
            return true;
        } else {
            // Invalid value for non-removable column
            return false;
        }
    }
    
    // Try to parse the buffer
    try {
        float floatValue;
        if (parseValue) {
            // Use custom parseValue callback if available (tracker-specific parsing)
            try {
                floatValue = parseValue(editBuffer_);
            } catch (...) {
                // Parse failed - for removable parameters, remove it (set to "none")
                if (isRemovable) {
                    removeParameter();
                    return true;
                }
                // Invalid value for non-removable column
                return false;
            }
        } else {
            // Try to evaluate as expression first, fall back to simple float parse
            try {
                floatValue = ExpressionParser::evaluate(editBuffer_);
            } catch (...) {
                // Expression invalid - try simple float parse
                try {
                    floatValue = std::stof(editBuffer_);
                } catch (...) {
                    // All parsing failed - for removable parameters, remove it (set to "none")
                    if (isRemovable) {
                        removeParameter();
                        return true;
                    }
                    // Invalid value for non-removable column
                    return false;
                }
            }
        }
        // Apply value (will check range and remove if out of range)
        applyEditValueFloat(floatValue);
        return true;
    } catch (...) {
        // Parse failed - for removable parameters, remove it (set to "none")
        if (isRemovable) {
            removeParameter();
            return true;
        }
        // Invalid value for non-removable column
        return false;
    }
}

std::string CellWidget::getDefaultFormatValue(float value) const {
    if (isBool) {
        return value > 0.5f ? "ON" : "OFF";
    }
    // Float value: 3 decimal places (0.001 precision) - unified for all float parameters
    char buf[16];
    snprintf(buf, sizeof(buf), "%.3f", value);
    return buf;
}

float CellWidget::getDefaultParseValue(const std::string& str) const {
    try {
        // Try to evaluate as expression first (supports operations)
        return ExpressionParser::evaluate(str);
    } catch (...) {
        // Fall back to simple float parse
        try {
            return std::stof(str);
        } catch (...) {
            return defaultValue;
        }
    }
}

ImU32 CellWidget::getFillBarColor() const {
    static ImU32 color = GUIConstants::toU32(GUIConstants::CellWidget::FillBar);
    return color;
}

ImU32 CellWidget::getRedOutlineColor() const {
    static ImU32 color = GUIConstants::toU32(GUIConstants::Outline::RedDim);
    return color;
}

ImU32 CellWidget::getOrangeOutlineColor() const {
    static ImU32 color = GUIConstants::toU32(GUIConstants::Outline::Orange);
    return color;
}

CellWidgetInteraction CellWidget::draw(int uniqueId,
                                            bool isFocused,
                                            bool shouldFocusFirst,
                                            bool shouldRefocusCurrentCell,
                                            const CellWidgetInputContext& inputContext) {
    ImGui::PushID(uniqueId);
    
    // Get cell rect (before drawing)
    ImVec2 cellMin = ImGui::GetCursorScreenPos();
    float cellHeight = ImGui::GetFrameHeight();
    float cellWidth = ImGui::GetColumnWidth();
    ImVec2 cellMax = ImVec2(cellMin.x + cellWidth, cellMin.y + cellHeight);
    
    // Draw slider mode (only mode supported)
    CellWidgetInteraction result = drawSliderMode(uniqueId, isFocused, shouldFocusFirst, shouldRefocusCurrentCell, inputContext, cellMin, cellMax);
    ImGui::PopID();
    return result;
}

CellWidgetInteraction CellWidget::drawSliderMode(int uniqueId, bool isFocused, bool shouldFocusFirst, bool shouldRefocusCurrentCell, const CellWidgetInputContext& inputContext, const ImVec2& cellMin, const ImVec2& cellMax) {
    CellWidgetInteraction result;
    bool wasEditingBeforeInput = editing_;  // Track if we were editing before this draw call
    
    // SLIDER mode (original implementation)
    // Get current value for display
    // Note: We keep NaN as-is for formatDisplayText (which will show "--")
    // but use a default value for fill bar calculations
    float currentVal = getCurrentValue ? getCurrentValue() : defaultValue;
    float displayVal = currentVal; // Keep NaN for display formatting
    
    // Get display text (formatDisplayText handles NaN and shows "--")
    std::string displayText;
    if (editing_ && isSelected()) {
        // Show edit buffer when editing (even if empty, to show edit mode is active)
        if (editBuffer_.empty()) {
            displayText = formatDisplayText(displayVal);
        } else {
            displayText = editBuffer_;
        }
    } else {
        displayText = formatDisplayText(displayVal);
    }
    
    // Calculate fill percent for visualization (calculateFillPercent handles NaN)
    float fillPercent = calculateFillPercent(currentVal);
    
    // Draw visual feedback (fill bar)
    drawVisualFeedback(cellMin, cellMax, fillPercent);
    
    // Apply edit mode styling: dark grey/black background (Blender-style)
    if (editing_ && isSelected()) {
        ImGui::PushStyleColor(ImGuiCol_Button, GUIConstants::Button::EditMode);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GUIConstants::Button::EditModeHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, GUIConstants::Button::EditModeActive);
    } else {
        // Make button backgrounds completely transparent when not editing
        ImGui::PushStyleColor(ImGuiCol_Button, GUIConstants::Button::Transparent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GUIConstants::Button::Transparent);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, GUIConstants::Button::Transparent);
    }
    
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(1.0f, 0.5f));
    
    // CRITICAL: Prevent ImGui from auto-focusing cells when clicking empty space
    ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
    
    // Set focus on first cell if requested
    if (shouldFocusFirst) {
        ImGui::SetKeyboardFocusHere(0);
    }
    
    // Draw button
    bool buttonClicked = ImGui::Button(displayText.c_str(), ImVec2(-1, 0));
    
    // Pop the flag after creating the button
    ImGui::PopItemFlag();
    
    // Check for activation (mouse click OR gamepad/keyboard activation)
    // IsItemActivated() works for both mouse clicks and gamepad "A" button / keyboard Enter
    bool isActivated = ImGui::IsItemActivated();
    
    // Prevent spacebar and Enter from triggering button clicks
    bool spacebarPressed = ImGui::IsKeyPressed(ImGuiKey_Space, false);
    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    
    // Check actual focus state after drawing (ImGui::IsItemFocused() works for last item)
    bool actuallyFocused = ImGui::IsItemFocused();
    
    // Handle activation (mouse click OR gamepad activation)
    // NOTE: Mouse clicks should only focus the cell, not enter edit mode
    // Enter key and typing will enter edit mode via processInputInDraw()
    // CRITICAL: Ignore button activation if Enter is pressed - Enter should only enter/edit mode, not trigger button click
    if (isActivated && !editing_ && !spacebarPressed && !enterPressed) {
        // Mouse click or gamepad "A" button - just signal click, don't enter edit mode
        // GUI layer will handle focus, Enter key or typing will enter edit mode
        result.clicked = true;
    }
    
    // Process keyboard input for this cell
    // CRITICAL: Process input if cell is selected, focused, or in edit mode
    // This handles Enter key, typing, and all other keyboard input
    // Allow processing if selected OR focused (for direct typing on focused cell)
    if (isSelected() || actuallyFocused || editing_) {
        processInputInDraw(actuallyFocused);
    }
    
    // Check if we just exited edit mode via Enter (was editing, now not editing)
    // Signal refocus needed for next frame via interaction result
    if (wasEditingBeforeInput && !editing_ && isSelected()) {
        result.needsRefocus = true;
    }
    
    // Refocus current cell after exiting edit mode
    // This happens AFTER input processing so it works when Enter is handled during draw
    // GUI layer passes shouldRefocusCurrentCell when refocus is needed (from previous frame's interaction.needsRefocus)
    if (shouldRefocusCurrentCell && isSelected()) {
        ImGui::SetKeyboardFocusHere(-1);
        // NOTE: Navigation flags are already enabled (we don't disable them anymore)
    }
    
    // Handle drag state (Blender-style: works across entire window)
    // CRITICAL: Check drag state FIRST to handle restored drag states from previous frames
    // When drag state is restored, dragging_ is true but IsItemActive() might be false
    if (dragging_) {
        // Continue drag - update value based on mouse movement (works even if mouse left cell)
        // This handles both active drags and restored drag states
        updateDrag();
        // Ensure we mark drag as started if it was restored (for proper state sync back to GUI)
        if (!result.dragStarted) {
            result.dragStarted = true;
        }
    } else if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        // Start drag when cell is active and mouse is actually being dragged
        // Use IsMouseDragging(0) to require actual mouse movement before starting drag
        // This prevents drag from starting on simple clicks - clicks should just focus the cell
        // IsItemActive() returns true when mouse was clicked on this item and is still held
        // IsMouseDragging(0) only returns true if mouse has moved while button is held
        // This works even if mouse has moved outside the cell (Blender-style)
        // NOTE: Don't require isSelected - IsItemActive() is sufficient to indicate the cell was clicked
        // Set isSelected when drag starts to maintain proper state
        if (!isSelected()) {
            setSelected(true);
            result.focusChanged = true;
        }
        startDrag();
        result.dragStarted = true;
    }
    
    // Check if drag ended (mouse released anywhere in window)
    // This check happens AFTER updateDrag() so we can properly detect drag end
    // updateDrag() also checks for mouse release internally, but we need to sync the dragEnded flag
    if (dragging_ && !ImGui::IsMouseDown(0)) {
        endDrag();
        result.dragEnded = true;
    }
    
    // Sync ImGui focus to selection state
    // Only sync when item was actually clicked, keyboard-navigated, or refocusing after edit
    if (actuallyFocused) {
        bool itemWasClicked = ImGui::IsItemClicked(0);
        bool keyboardNavActive = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
        bool needsRefocus = shouldRefocusCurrentCell && isSelected();
        
        // Only sync if this is an intentional focus (click, keyboard nav, or refocus)
        if (itemWasClicked || keyboardNavActive || needsRefocus) {
            result.focusChanged = true;
            
            // Lock focus to editing cell - arrow keys adjust values, not navigate
            if (editing_ && !isSelected()) {
                // Don't sync focus change during edit
                result.shouldExitEarly = true;
            } else {
                setSelected(true);
            }
        }
    }
    
    // Early exit after syncing (but before drawing outline)
    if (result.shouldExitEarly) {
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor(3);
        ImGui::PopID();
        return result;
    }
    
    // Handle click
    // CRITICAL: Ignore button clicks when Enter is pressed - Enter should only enter/edit mode, not trigger button click
    bool isItemClicked = ImGui::IsItemClicked(0);
    if (buttonClicked && !ImGui::IsMouseDragging(0) && !spacebarPressed && !enterPressed && isItemClicked) {
        result.clicked = true;
        setSelected(true);
        // DON'T enter edit mode on click - just focus the cell
        // User can type numbers directly (auto-enters edit mode) or hit Enter to enter edit mode
        if (editing_) {
            exitEditMode();
        }
    }
    
    // Handle double-click: clear the cell (remove parameter)
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        // Exit edit mode if active
        if (editing_) {
            exitEditMode();
        }
        // Clear the cell by removing the parameter
        removeParameter();
    }
    
    // Maintain focus during drag (even when mouse leaves cell)
    if (dragging_ && !actuallyFocused) {
        // Keep cell focused during drag for visual feedback
        // Don't require IsItemActive() - drag works across entire window
        ImGui::SetKeyboardFocusHere(-1);
    }
    
    // Pop style var and colors
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(3);
    
    // Draw outline for selected/editing cells
    bool shouldShowOutline = isSelected() || dragging_ || (actuallyFocused && !editing_);
    if (shouldShowOutline) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (drawList) {
            ImVec2 outlineMin = ImVec2(cellMin.x - 1, cellMin.y - 1);
            ImVec2 outlineMax = ImVec2(cellMax.x + 1, cellMax.y + 1);
            // Orange outline when in edit mode, red outline when just selected or dragging
            ImU32 outlineColor = (isSelected() && editing_)
                ? getOrangeOutlineColor()
                : getRedOutlineColor();
            drawList->AddRect(outlineMin, outlineMax, outlineColor, 0.0f, 0, 2.0f);
        }
    }
    
    return result;
}

void CellWidget::drawVisualFeedback(const ImVec2& cellMin, const ImVec2& cellMax, float fillPercent) {
    // Draw value bar background (no cell background - using row background instead)
    if (fillPercent > 0.01f) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (drawList) {
            ImVec2 fillEnd = ImVec2(cellMin.x + (cellMax.x - cellMin.x) * fillPercent, cellMax.y);
            drawList->AddRectFilled(cellMin, fillEnd, getFillBarColor());
        }
    }
}

void CellWidget::processInputInDraw(bool actuallyFocused) {
    // Process keyboard input directly in draw() when cell is selected or editing
    // This makes CellWidget self-contained and reusable across all modules
    
    // Early exit if not selected, not editing, and not focused
    if (!isSelected() && !editing_ && !actuallyFocused) {
        return;
    }
    
    ImGuiIO& io = ImGui::GetIO();
    
    // Check if ImGui navigation is active (gamepad/keyboard nav)
    bool navActive = (io.NavActive && (io.ConfigFlags & (ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad)));
    
    // CRITICAL: Check for Enter key BEFORE navigation check
    // Enter should enter/edit mode even when navigation is active
    bool enterPressed = (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    if (enterPressed && isSelected()) {
        bool ctrlPressed = io.KeyCtrl;
        bool shiftPressed = io.KeyShift;
        if (this->handleKeyPress(OF_KEY_RETURN, ctrlPressed, shiftPressed)) {
            return;  // Handled
        }
    }
    
    // Process InputQueueCharacters (typed characters) - this should work even when navigation is active
    // Direct typing should auto-enter edit mode, so we process it before the navigation check
    bool inputQueueProcessed = false;
    if (io.InputQueueCharacters.Size > 0) {
        inputQueueProcessed = true;
        
        for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
            unsigned int c = io.InputQueueCharacters[i];
            bool handled = false;
            
            // Check for numeric keys (0-9) - these should auto-enter edit mode
            if (c >= '0' && c <= '9') {
                handled = this->handleKeyPress((int)c, false, false);
            }
            // Check for decimal point
            else if (c == '.' || c == ',') {
                handled = this->handleKeyPress('.', false, false);
            }
            // Check for minus sign
            else if (c == '-') {
                handled = this->handleKeyPress('-', false, false);
            }
            // Check for operators (only in edit mode)
            else if (editing_) {
                if (c == '+') {
                    handled = this->handleKeyPress('+', false, false);
                } else if (c == '*') {
                    handled = this->handleKeyPress('*', false, false);
                } else if (c == '/') {
                    handled = this->handleKeyPress('/', false, false);
                } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                    // Invalid character (letter) - clear parameter
                    this->removeParameter();
                    handled = true;
                }
            } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                // Invalid character (letter) when not in edit mode - ignore
                handled = true;  // Consume the event
            }
        }
        
        // Clear InputQueueCharacters after processing
        io.InputQueueCharacters.clear();
    }
    
    // If navigation is active and not editing, let ImGui handle navigation
    // BUT: Only skip if we haven't processed typed characters (typing should work)
    if (!editing_ && navActive && !inputQueueProcessed) {
        return;  // Let ImGui handle navigation (gamepad/keyboard nav)
    }
    
    // Process special keys (only if not already processed via InputQueueCharacters)
        if (!inputQueueProcessed) {
            // Escape key - only when in edit mode
            if (editing_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                this->handleKeyPress(OF_KEY_ESC, false, false);
            }
            
            // Backspace key
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
                this->handleKeyPress(OF_KEY_BACKSPACE, false, false);
            }
            
            // Delete key
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                this->handleKeyPress(OF_KEY_DEL, false, false);
            }
            
            // Keypad keys (for numpad support)
            if (ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) this->handleKeyPress('0', false, false);
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad1, false)) this->handleKeyPress('1', false, false);
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad2, false)) this->handleKeyPress('2', false, false);
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad3, false)) this->handleKeyPress('3', false, false);
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad4, false)) this->handleKeyPress('4', false, false);
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad5, false)) this->handleKeyPress('5', false, false);
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad6, false)) this->handleKeyPress('6', false, false);
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad7, false)) this->handleKeyPress('7', false, false);
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad8, false)) this->handleKeyPress('8', false, false);
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad9, false)) this->handleKeyPress('9', false, false);
            else if (ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false)) this->handleKeyPress('.', false, false);
            
            if (editing_) {
                if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)) this->handleKeyPress('+', false, false);
                if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) this->handleKeyPress('-', false, false);
                if (ImGui::IsKeyPressed(ImGuiKey_KeypadMultiply, false)) this->handleKeyPress('*', false, false);
                if (ImGui::IsKeyPressed(ImGuiKey_KeypadDivide, false)) this->handleKeyPress('/', false, false);
            }
            
            // Arrow keys in edit mode (adjust values)
            if (editing_) {
                bool shiftPressed = io.KeyShift;
                if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
                    this->handleKeyPress(OF_KEY_UP, false, shiftPressed);
                }
                if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
                    this->handleKeyPress(OF_KEY_DOWN, false, shiftPressed);
                }
                if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
                    this->handleKeyPress(OF_KEY_LEFT, false, shiftPressed);
                }
                if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
                    this->handleKeyPress(OF_KEY_RIGHT, false, shiftPressed);
                }
            }
        }
}

void CellWidget::startDrag() {
    if (dragging_) return; // Already dragging
    
    // Exit edit mode when dragging starts
    if (editing_) {
        exitEditMode();
    }
    
    // Initialize drag state
    dragging_ = true;
    dragStartY_ = ImGui::GetMousePos().y;
    dragStartX_ = ImGui::GetMousePos().x;
    
    // Get current value as starting point
    if (getCurrentValue) {
        float val = getCurrentValue();
        // Handle NaN (not set) - use default value or middle of range
        if (std::isnan(val)) {
            if (defaultValue >= minVal && defaultValue <= maxVal) {
                lastDragValue_ = defaultValue;
            } else {
                lastDragValue_ = (minVal + maxVal) / 2.0f;
            }
        } else {
            lastDragValue_ = val;
        }
    } else {
        lastDragValue_ = defaultValue;
    }
    
    // NOTE: Navigation remains enabled - drag is mouse-based and doesn't conflict with gamepad
}

void CellWidget::updateDrag() {
    if (!dragging_) return;
    
    // Check if mouse is still down (allows dragging across entire window)
    if (!ImGui::IsMouseDown(0)) {
        // Mouse released - end drag
        endDrag();
        return;
    }
    
    // Calculate drag delta (both vertical AND horizontal)
    ImVec2 currentPos = ImGui::GetMousePos();
    float dragDeltaY = dragStartY_ - currentPos.y; // Up = positive (increase)
    float dragDeltaX = currentPos.x - dragStartX_; // Right = positive (increase)
    
    // Use the larger of horizontal or vertical movement for maximum precision
    // This allows dragging in any direction with equal effectiveness
    float totalDragDelta = std::abs(dragDeltaY) > std::abs(dragDeltaX) ? dragDeltaY : dragDeltaX;
    
    // Multi-precision dragging: standard for full-range traversal, Shift for unified fine precision
    // Check modifier keys for precision control
    ImGuiIO& io = ImGui::GetIO();
    bool shiftPressed = (io.KeyMods & ImGuiMod_Shift) != 0;
    
    float dragStepIncrement;
    float rangeSize = maxVal - minVal;
    if (isInteger) {
        // Integer parameters: Range-based step increment for precise control
        // This allows traversing full range in ~200 pixels, making the slider much more precise
        // For example: Index (0-127) = 0.635 per pixel, Length (1-16) = 0.075 per pixel
        if (shiftPressed) {
            // Shift: Fine precision (rangeSize/400 per pixel) for precise integer adjustments
            dragStepIncrement = rangeSize / 400.0f;
        } else {
            // Standard: Practical sensitivity for full-range traversal (rangeSize/200 per pixel)
            // This allows traversing full range in ~200 pixels while maintaining reasonable precision
            dragStepIncrement = rangeSize / 200.0f;
        }
    } else {
        // Float parameters: Multi-precision based on modifier keys
        if (shiftPressed) {
            // Shift: Unified fine precision (0.001 per pixel) for precise adjustments
            dragStepIncrement = 0.001f;
        } else {
            // Standard: Practical sensitivity for full-range traversal (rangeSize/200 per pixel)
            // This allows traversing full range in ~200 pixels while maintaining reasonable precision
            dragStepIncrement = rangeSize / 200.0f;
        }
    }
    
    // Calculate value change using drag step increment
    float valueDelta = totalDragDelta * dragStepIncrement;
    float newValue = lastDragValue_ + valueDelta;
    
    // Clamp to valid range
    newValue = std::max(minVal, std::min(maxVal, newValue));
    
    // For integer parameters, round to nearest integer
    if (isInteger) {
        newValue = std::round(newValue);
    }
    
    // Apply immediately (no threshold - maximum precision and responsiveness)
    applyDragValue(newValue);
}

void CellWidget::endDrag() {
    if (!dragging_) return;
    
    dragging_ = false;
    dragStartY_ = 0.0f;
    dragStartX_ = 0.0f;
    lastDragValue_ = 0.0f;
    
    // NOTE: Navigation remains enabled - no need to re-enable
}

void CellWidget::applyDragValue(float newValue) {
    if (!onValueApplied) return;
    
    // Clamp value to range
    float clampedValue = std::max(minVal, std::min(maxVal, newValue));
    
    // Apply via callback
    onValueApplied(parameterName, clampedValue);
}



