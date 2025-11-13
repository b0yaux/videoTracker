#include "ParameterCell.h"
#include "ofxImGui.h"
#include "ofLog.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <stack>
#include <cctype>
#include <limits>

ParameterCell::ParameterCell() 
    : isEditing(false), isSelected(false), editBufferInitialized(false), shouldRefocus(false),
      isDragging(false), dragStartY(0.0f), dragStartX(0.0f), lastDragValue(0.0f) {
}

// Helper function to evaluate simple mathematical expressions
// Supports: +, -, *, / with proper precedence
// Handles negative numbers and decimal numbers
static float evaluateExpression(const std::string& expr) {
    if (expr.empty()) {
        throw std::invalid_argument("Empty expression");
    }
    
    // Handle starting with '.' (treat as "0.")
    std::string processed = expr;
    if (processed[0] == '.') {
        processed = "0" + processed;
    }
    
    // Simple expression evaluator using two stacks (shunting yard algorithm simplified)
    std::stack<float> values;
    std::stack<char> ops;
    
    auto applyOp = [&](char op) {
        if (values.size() < 2) return;
        float b = values.top(); values.pop();
        float a = values.top(); values.pop();
        switch (op) {
            case '+': values.push(a + b); break;
            case '-': values.push(a - b); break;
            case '*': values.push(a * b); break;
            case '/': 
                if (std::abs(b) < 1e-9f) throw std::runtime_error("Division by zero");
                values.push(a / b); 
                break;
        }
    };
    
    auto precedence = [](char op) -> int {
        if (op == '+' || op == '-') return 1;
        if (op == '*' || op == '/') return 2;
        return 0;
    };
    
    size_t i = 0;
    bool expectNumber = true;
    
    while (i < processed.length()) {
        // Skip whitespace
        if (std::isspace(processed[i])) {
            i++;
            continue;
        }
        
        // Handle '-' - could be negative number or subtraction
        if (processed[i] == '-') {
            if (expectNumber) {
                // Check if this is a negative number (followed by digit or '.')
                // or subtraction (not followed by digit/'.' and we have values)
                bool isNegative = false;
                if (i + 1 < processed.length() && (std::isdigit(processed[i + 1]) || processed[i + 1] == '.')) {
                    isNegative = true;
                } else if (values.empty()) {
                    // No values yet, must be negative (even if incomplete, user is typing)
                    isNegative = true;
                }
                // Otherwise, it's subtraction (handled below)
                
                if (isNegative) {
                    i++; // Consume the '-'
                    if (i >= processed.length()) {
                        // Incomplete negative - user might be typing, allow it
                        // Don't throw, just return 0 or let it be handled by caller
                        throw std::invalid_argument("Incomplete negative number");
                    }
                    
                    size_t start = i;
                    bool hasDecimal = false;
                    while (i < processed.length() && (std::isdigit(processed[i]) || processed[i] == '.')) {
                        if (processed[i] == '.') {
                            if (hasDecimal) throw std::invalid_argument("Multiple decimal points");
                            hasDecimal = true;
                        }
                        i++;
                    }
                    
                    if (i == start) throw std::invalid_argument("Invalid negative number");
                    float val = std::stof(processed.substr(start, i - start));
                    values.push(-val);
                    expectNumber = false;
                    continue;
                }
            }
            // Fall through to operator handling for subtraction
        }
        
        // Parse number (positive, starting with digit or '.')
        if (std::isdigit(processed[i]) || processed[i] == '.') {
            size_t start = i;
            bool hasDecimal = false;
            while (i < processed.length() && (std::isdigit(processed[i]) || processed[i] == '.')) {
                if (processed[i] == '.') {
                    if (hasDecimal) throw std::invalid_argument("Multiple decimal points");
                    hasDecimal = true;
                }
                i++;
            }
            float val = std::stof(processed.substr(start, i - start));
            values.push(val);
            expectNumber = false;
            continue;
        }
        
        // Handle operators (binary operations: +, -, *, /)
        if (processed[i] == '+' || processed[i] == '-' || processed[i] == '*' || processed[i] == '/') {
            if (expectNumber) {
                throw std::invalid_argument("Unexpected operator");
            }
            
            // This is a binary operator
            while (!ops.empty() && precedence(ops.top()) >= precedence(processed[i])) {
                applyOp(ops.top());
                ops.pop();
            }
            ops.push(processed[i]);
            expectNumber = true;
            i++;
            continue;
        }
        
        throw std::invalid_argument("Invalid character in expression");
    }
    
    // Apply remaining operators
    while (!ops.empty()) {
        applyOp(ops.top());
        ops.pop();
    }
    
    if (values.size() != 1) {
        throw std::invalid_argument("Invalid expression");
    }
    
    return values.top();
}

void ParameterCell::setValueRange(float min, float max, float defaultValue) {
    minVal = min;
    maxVal = max;
    this->defaultValue = defaultValue;
}

void ParameterCell::calculateStepIncrement() {
    // Calculate optimal step increment based on parameter type and range
    if (isInteger || isFixed) {
        // Integer parameters: always use 1.0
        stepIncrement = 1.0f;
    } else {
        // Float parameter: determine precision based on range size
        // Use finer precision for smaller ranges, coarser for larger ranges
        float rangeSize = maxVal - minVal;
        if (rangeSize <= 1.0f) {
            // Small range (e.g., position 0-1): use 0.001 precision
            stepIncrement = 0.001f;
        } else if (rangeSize <= 2.0f) {
            // Small-medium range (e.g., volume 0-2): use 0.01 precision
            stepIncrement = 0.01f;
        } else if (rangeSize <= 20.0f) {
            // Medium range (e.g., speed -10 to 10): use 0.01 precision for finer control
            stepIncrement = 0.01f;
        } else {
            // Large range: use 0.1 precision
            stepIncrement = 0.1f;
        }
    }
}

void ParameterCell::setEditing(bool e) {
    if (e && !isEditing) {
        enterEditMode();
    } else if (!e && isEditing) {
        exitEditMode();
    }
}

void ParameterCell::setEditBuffer(const std::string& buffer) {
    editBuffer = buffer;
    if (!editBuffer.empty()) {
        // If setting a non-empty buffer, ensure we're in edit mode
        if (!isEditing) {
            isEditing = true;
            // Don't call enterEditMode() here as it would re-initialize the buffer
            // Just disable ImGui keyboard navigation
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        }
    }
}

void ParameterCell::enterEditMode() {
    isEditing = true;
    initializeEditBuffer();
    editBufferInitialized = true;
    
    // Disable ImGui keyboard navigation when entering edit mode
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
}

void ParameterCell::exitEditMode() {
    isEditing = false;
    editBuffer.clear();
    editBufferInitialized = false;
    
    // Re-enable ImGui keyboard navigation when exiting edit mode
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

bool ParameterCell::handleKeyPress(int key, bool ctrlPressed, bool shiftPressed) {
    // Enter key behavior
    if (key == OF_KEY_RETURN) {
        if (ctrlPressed || shiftPressed) {
            // Ctrl+Enter or Shift+Enter: Exit edit mode
            exitEditMode();
            return true;
        }
        
        if (isEditing) {
            // In edit mode: Confirm and exit edit mode
            applyValue();
            shouldRefocus = true;
            exitEditMode();
            return true;
        } else if (isSelected) {
            // Selected but not editing: Enter edit mode
            enterEditMode();
            return true;
        }
        return false;
    }
    
    // Escape: Exit edit mode
    if (key == OF_KEY_ESC) {
        if (isEditing) {
            cancelEdit();
            return true;
        }
        return false;
    }
    
    // Backspace: Delete last character in edit buffer
    if (key == OF_KEY_BACKSPACE) {
        if (isEditing && !editBuffer.empty()) {
            editBuffer.pop_back();
            editBufferInitialized = false;
            
            // Re-apply value after backspace (Blender-style reactive editing)
            // This allows the value to update as the user corrects their input
            if (editBuffer.empty()) {
                // Buffer is now empty - remove parameter (set to "none")
                if (onValueRemoved) {
                    onValueRemoved(parameterName);
                }
            } else {
                // Check if buffer contains only dashes (indicates "none" state, e.g., "-", "--")
                bool onlyDashes = true;
                for (char c : editBuffer) {
                    if (c != '-') {
                        onlyDashes = false;
                        break;
                    }
                }
                
                if (onlyDashes) {
                    // Buffer is only dashes - remove parameter (set to "none")
                    if (onValueRemoved) {
                        onValueRemoved(parameterName);
                    }
                } else {
                    try {
                        // Try to evaluate as expression (supports operations)
                        float floatValue = evaluateExpression(editBuffer);
                        applyEditValueFloat(floatValue);
                    } catch (...) {
                        // Expression invalid - remove parameter (set to "none")
                        // This handles invalid expressions
                        if (onValueRemoved) {
                            onValueRemoved(parameterName);
                        }
                    }
                }
            }
            return true;
        }
        return false;
    }
    
    // Delete key: Clear edit buffer
    if (key == OF_KEY_DEL) {
        if (isEditing) {
            editBuffer.clear();
            editBufferInitialized = false;
            return true;
        }
        return false;
    }
    
    // Numeric input (0-9) - Blender-style: direct typing enters edit mode and replaces value
    if (key >= '0' && key <= '9') {
        bool justEnteredEditMode = false;
        if (!isEditing) {
            // Auto-enter edit mode if cell is selected
            if (isSelected) {
                enterEditMode();
                justEnteredEditMode = true;
            } else {
                return false;  // Not selected, don't handle
            }
        }
        
        // Only clear buffer if we just entered edit mode via numeric key
        // This allows typing decimals after numbers (e.g., "1.5") and using backspace to correct
        if (justEnteredEditMode) {
            editBuffer.clear();
            editBufferInitialized = false;
        }
        // NOTE: We do NOT clear the buffer if already in edit mode - this allows:
        // - Typing multi-digit numbers (e.g., "123")
        // - Typing decimals after numbers (e.g., "1.5")
        // - Using backspace to correct input
        
        // Append digit to buffer
        editBuffer += (char)key;
        if (editBuffer.length() > 50) {  // Increased limit for expressions
            editBuffer = editBuffer.substr(editBuffer.length() - 50);
        }
        
        // Apply value immediately (Blender-style reactive editing)
        // Try to evaluate as expression (supports operations like "2*3", "10/2", etc.)
        if (!editBuffer.empty()) {
            // Check if buffer is only dashes (including "--")
            bool onlyDashes = true;
            for (char c : editBuffer) {
                if (c != '-') {
                    onlyDashes = false;
                    break;
                }
            }
            
            if (onlyDashes) {
                // Only dashes (e.g., "-", "--") - remove parameter (set to "none")
                if (onValueRemoved) {
                    onValueRemoved(parameterName);
                }
            } else {
                try {
                    float floatValue = evaluateExpression(editBuffer);
                    applyEditValueFloat(floatValue);
                } catch (...) {
                    // Expression invalid or incomplete
                    // If it's clearly invalid (not just incomplete), remove parameter
                    // For incomplete expressions (like "2*"), let user continue typing
                    // We can't easily distinguish, so for now, let user continue
                    // The final parseAndApplyEditBuffer will handle invalid expressions
                }
            }
        }
        return true;
    }
    
    // Mathematical operators: +, *, /
    if (key == '+' || key == '*' || key == '/') {
        if (!isEditing) {
            // Auto-enter edit mode if cell is selected
            if (isSelected) {
                enterEditMode();
                // Don't clear buffer - allow appending operator to existing value
                // This allows operations like "5*2" or "10/2"
            } else {
                return false;  // Not selected, don't handle
            }
        }
        
        // Append operator to buffer
        editBuffer += (char)key;
        if (editBuffer.length() > 50) {  // Increased limit for expressions
            editBuffer = editBuffer.substr(editBuffer.length() - 50);
        }
        
        // Try to evaluate expression if it's complete
        // For operators, we wait for the next number before evaluating
        // But we can try to evaluate if the expression is already valid
        if (!editBuffer.empty()) {
            // Check if buffer is only dashes (including "--")
            bool onlyDashes = true;
            for (char c : editBuffer) {
                if (c != '-' && c != '+' && c != '*' && c != '/') {
                    onlyDashes = false;
                    break;
                }
            }
            
            if (onlyDashes) {
                // Only operators/dashes - remove parameter (set to "none")
                if (onValueRemoved) {
                    onValueRemoved(parameterName);
                }
            } else {
                try {
                    float floatValue = evaluateExpression(editBuffer);
                    applyEditValueFloat(floatValue);
                } catch (...) {
                    // Expression incomplete or invalid - let user continue typing
                    // Final validation happens in parseAndApplyEditBuffer
                }
            }
        }
        return true;
    }
    
    // Decimal point and minus sign (can be negative number or subtraction)
    if (key == '.' || key == '-') {
        bool justEnteredEditMode = false;
        if (!isEditing) {
            // Auto-enter edit mode if cell is selected
            if (isSelected) {
                enterEditMode();
                // Clear buffer when entering edit mode via decimal/minus (replaces current value)
                editBuffer.clear();
                editBufferInitialized = false;
                justEnteredEditMode = true;
            } else {
                return false;  // Not selected, don't handle
            }
        }
        
        // Only clear buffer if we just entered edit mode
        // If already in edit mode, don't clear - this allows typing decimals after numbers (e.g., "1.5")
        // and using backspace to correct input
        // NOTE: We do NOT clear the buffer if already in edit mode
        
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
            size_t lastOp = editBuffer.find_last_of("+-*/");
            std::string lastNumber = (lastOp == std::string::npos) ? editBuffer : editBuffer.substr(lastOp + 1);
            if (lastNumber.find('.') != std::string::npos) {
                return true;  // This number already has a decimal point
            }
        }
        
        editBuffer += (char)key;
        if (editBuffer.length() > 50) {  // Increased limit for expressions
            editBuffer = editBuffer.substr(editBuffer.length() - 50);
        }
        
        // Apply value immediately (Blender-style)
        // Check if buffer is empty, single '.', or contains only dashes
        bool shouldRemoveParameter = false;
        if (editBuffer.empty() || editBuffer == ".") {
            shouldRemoveParameter = true;
        } else {
            // Check if buffer contains only dashes
            bool onlyDashes = true;
            for (char c : editBuffer) {
                if (c != '-') {
                    onlyDashes = false;
                    break;
                }
            }
            if (onlyDashes) {
                shouldRemoveParameter = true;
            }
        }
        
        if (shouldRemoveParameter) {
            // Buffer is only dashes, empty, or single '.' - remove parameter (set to "none")
            if (onValueRemoved) {
                onValueRemoved(parameterName);
            }
        } else {
            // Check if buffer is only dashes (including "--")
            bool onlyDashes = true;
            for (char c : editBuffer) {
                if (c != '-') {
                    onlyDashes = false;
                    break;
                }
            }
            
            if (onlyDashes) {
                // Only dashes (e.g., "-", "--") - remove parameter (set to "none")
                if (onValueRemoved) {
                    onValueRemoved(parameterName);
                }
            } else {
                try {
                    // Try to evaluate as expression (supports operations)
                    float floatValue = evaluateExpression(editBuffer);
                    applyEditValueFloat(floatValue);
                } catch (...) {
                    // Expression invalid - remove parameter (set to "none")
                    // This handles invalid expressions like "abc", "2**3", etc.
                    if (onValueRemoved) {
                        onValueRemoved(parameterName);
                    }
                }
            }
        }
        return true;
    }
    
    // Arrow keys in edit mode: Adjust values ONLY (no navigation)
    // CRITICAL: When editing, arrow keys must ONLY adjust values, never navigate
    // This ensures focus stays locked to the editing cell
    if (isEditing) {
        if (key == OF_KEY_UP || key == OF_KEY_DOWN || key == OF_KEY_LEFT || key == OF_KEY_RIGHT) {
            // Adjust value based on arrow direction
            int delta = 0;
            if (key == OF_KEY_UP || key == OF_KEY_RIGHT) {
                delta = 1;  // Up/Right = increase
            } else {
                delta = -1; // Down/Left = decrease
            }
            adjustValue(delta);
            // Always return true to consume the event and prevent navigation
            return true;
        }
    }
    
    return false;
}

void ParameterCell::appendDigit(char digit) {
    if (!isEditing) {
        enterEditMode();
    }
    editBuffer += digit;
    if (editBuffer.length() > 15) {
        editBuffer = editBuffer.substr(editBuffer.length() - 15);
    }
}

void ParameterCell::appendChar(char c) {
    if (!isEditing) {
        enterEditMode();
    }
    editBuffer += c;
    if (editBuffer.length() > 15) {
        editBuffer = editBuffer.substr(editBuffer.length() - 15);
    }
}

void ParameterCell::backspace() {
    if (isEditing && !editBuffer.empty()) {
        editBuffer.pop_back();
        editBufferInitialized = false;
    }
}

void ParameterCell::deleteChar() {
    if (isEditing) {
        editBuffer.clear();
        editBufferInitialized = false;
    }
}

void ParameterCell::applyValue() {
    parseAndApplyEditBuffer();
}

void ParameterCell::cancelEdit() {
    exitEditMode();
}

void ParameterCell::adjustValue(int delta) {
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
    
    // Use configured step increment (set based on parameter type and range)
    // This is set in createParameterCellForColumn based on:
    // - Integer parameters: 1.0
    // - Float parameters: 0.001, 0.01, or 0.1 based on range size
    stepSize = stepIncrement;
    
    float newValue = currentVal + (delta * stepSize);
    
    // For integer parameters, round to nearest integer
    if (isInteger || isFixed) {
        newValue = std::round(newValue);
    }
    
    newValue = std::max(minVal, std::min(maxVal, newValue));
    
    // Update edit buffer with new value
    if (formatValue) {
        editBuffer = formatValue(newValue);
    } else {
        editBuffer = getDefaultFormatValue(newValue);
    }
    editBufferInitialized = false;
    
    // Apply immediately
    applyEditValueFloat(newValue);
}

void ParameterCell::initializeEditBuffer() {
    if (!getCurrentValue) {
        editBuffer.clear();
        return;
    }
    
    float currentVal = getCurrentValue();
    
    if (isFixed && fixedType == "index") {
        // Index column: 1-based display (01-99), 0 = rest
        // currentVal is already in 1-based display format (0 = rest, 1+ = media index)
        int indexVal = (int)std::round(currentVal);
        if (indexVal <= 0) {
            editBuffer = "00"; // Rest
        } else {
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d", indexVal);
            editBuffer = buf;
        }
    } else if (isFixed && fixedType == "length") {
        // Length column: 1-16 range
        int lengthVal = (int)std::round(currentVal);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", lengthVal);
        editBuffer = buf;
    } else {
        // Dynamic parameter or MediaPool parameter
        if (formatValue) {
            editBuffer = formatValue(currentVal);
        } else {
            editBuffer = getDefaultFormatValue(currentVal);
        }
    }
}

std::string ParameterCell::formatDisplayText(float value) const {
    // Check for NaN (not a number) - indicates empty/not set (show "--")
    // This represents "none" state - let MediaPool handle the parameter
    // Using NaN allows parameters with negative ranges (like speed -10 to 10) to distinguish
    // between "not set" (NaN/--) and explicit values like 1.0 or -1.0
    if (std::isnan(value)) {
        return "--";
    }
    
    if (isBool) {
        return value > 0.5f ? "ON" : "OFF";
    }
    
    if (isFixed && fixedType == "index") {
        // Index: 1-based display (01-99)
        // For fixed columns, we still use -1.0f to indicate rest (compatibility)
        if (value < 0.0f) {
            return "--";
        }
        int idx = (int)std::round(value);
        if (idx <= 0) {
            return "--";
        }
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d", idx);
        return buf;
    }
    
    if (isFixed && fixedType == "length") {
        // Length: 1-16 range, formatted as "02"
        // For fixed columns, we still use -1.0f to indicate rest (compatibility)
        if (value < 0.0f) {
            return "--";
        }
        int len = std::max(1, std::min(16, (int)std::round(value)));
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d", len);
        return buf;
    }
    
    // Float value: 2 decimal places
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", value);
    return buf;
}

float ParameterCell::calculateFillPercent(float value) const {
    // Check for NaN (not a number) - indicates empty/not set (no fill)
    if (std::isnan(value)) {
        return 0.0f;
    }
    
    // For fixed columns, we still use -1.0f to indicate rest (compatibility)
    if (isFixed && value < 0.0f) {
        return 0.0f;
    }
    
    float rangeSize = maxVal - minVal;
    if (rangeSize > 0.0f) {
        float fillPercent = (value - minVal) / rangeSize;
        return std::max(0.0f, std::min(1.0f, fillPercent));
    }
    return 0.0f;
}

void ParameterCell::applyEditValueFloat(float floatValue) {
    if (isFixed && fixedType == "length") {
        // Length must be integer between 1-16
        // For fixed columns, clamp to valid range
        int newValue = std::max(1, std::min(16, (int)std::round(floatValue)));
        applyEditValueInt(newValue);
    } else if (isFixed && fixedType == "index") {
        // Index: 0 = rest, 1+ = media index (1-based display)
        // For fixed columns, clamp to valid range
        int maxIdx = getMaxIndex ? getMaxIndex() : 127;
        int newValue = std::max(0, std::min(maxIdx, (int)std::round(floatValue)));
        applyEditValueInt(newValue);
    } else {
        // Dynamic parameter or MediaPool parameter
        // If value is outside range, remove parameter (set to "none" state)
        // This allows users to clear invalid values by typing out-of-range numbers
        if (floatValue < minVal || floatValue > maxVal) {
            // Value is outside valid range - remove parameter
            if (onValueRemoved) {
                onValueRemoved(parameterName);
            }
        } else {
            // Value is within range - apply it
            if (onValueApplied) {
                onValueApplied(parameterName, floatValue);
            }
        }
    }
}

void ParameterCell::applyEditValueInt(int intValue) {
    if (isFixed && fixedType == "index") {
        // Index: 0 = rest (-1 in storage), 1+ = media index (0-based in storage)
        // But we work with 1-based display values here
        // The callback should handle the conversion
        if (onValueApplied) {
            onValueApplied(parameterName, (float)intValue);
        }
    } else if (isFixed && fixedType == "length") {
        // Length: 1-16 range
        int clampedValue = std::max(1, std::min(16, intValue));
        if (onValueApplied) {
            onValueApplied(parameterName, (float)clampedValue);
        }
    } else {
        // Shouldn't happen for non-fixed, but handle it
        if (onValueApplied) {
            onValueApplied(parameterName, (float)intValue);
        }
    }
}

bool ParameterCell::parseAndApplyEditBuffer() {
    // For fixed columns, empty buffer is invalid
    if (isFixed && editBuffer.empty()) {
        return false;
    }
    
    // Handle empty buffer or invalid input for dynamic parameters (removes parameter)
    if (!isFixed) {
        // Trim whitespace for comparison
        std::string trimmed = editBuffer;
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.back() == ' ')) {
            if (trimmed.front() == ' ') trimmed.erase(0, 1);
            if (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
        }
        
        // Check for clear patterns: empty, or strings containing only dashes
        if (trimmed.empty()) {
            // Empty buffer - remove parameter
            if (onValueRemoved) {
                onValueRemoved(parameterName);
            }
            return true;
        }
        
        // Check if string contains only dashes
        bool onlyDashes = true;
        for (char c : trimmed) {
            if (c != '-') {
                onlyDashes = false;
                break;
            }
        }
        if (onlyDashes) {
            // Only dashes - remove parameter
            if (onValueRemoved) {
                onValueRemoved(parameterName);
            }
            return true;
        }
    }
    
    // Try to parse the buffer
    try {
        if (isFixed && fixedType == "length") {
            int lengthValue = std::max(1, std::min(16, (int)std::round(std::stof(editBuffer))));
            applyEditValueInt(lengthValue);
            return true;
        } else if (isFixed && fixedType == "index") {
            int maxIdx = getMaxIndex ? getMaxIndex() : 127;
            int indexValue = std::max(0, std::min(maxIdx, (int)std::round(std::stof(editBuffer))));
            applyEditValueInt(indexValue);
            return true;
        } else {
            // Dynamic parameter - parse as expression (supports operations)
            float floatValue;
            if (parseValue) {
                try {
                    floatValue = parseValue(editBuffer);
                } catch (...) {
                    // Parse failed - remove parameter (set to "none")
                    if (onValueRemoved) {
                        onValueRemoved(parameterName);
                    }
                    return true;
                }
            } else {
                // Try to evaluate as expression first, fall back to simple float parse
                try {
                    floatValue = evaluateExpression(editBuffer);
                } catch (...) {
                    // Expression invalid - try simple float parse
                    try {
                        floatValue = std::stof(editBuffer);
                    } catch (...) {
                        // All parsing failed - remove parameter (set to "none")
                        if (onValueRemoved) {
                            onValueRemoved(parameterName);
                        }
                        return true;
                    }
                }
            }
            // Apply value (will check range and remove if out of range)
            applyEditValueFloat(floatValue);
            return true;
        }
    } catch (...) {
        // Parse failed - for dynamic parameters, remove it (set to "none")
        if (!isFixed) {
            if (onValueRemoved) {
                onValueRemoved(parameterName);
            }
            return true;
        }
        // Invalid value for fixed column
        return false;
    }
}

std::string ParameterCell::getDefaultFormatValue(float value) const {
    if (isBool) {
        return value > 0.5f ? "ON" : "OFF";
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", value);
    return buf;
}

float ParameterCell::getDefaultParseValue(const std::string& str) const {
    try {
        // Try to evaluate as expression first (supports operations)
        return evaluateExpression(str);
    } catch (...) {
        // Fall back to simple float parse
        try {
            return std::stof(str);
        } catch (...) {
            return defaultValue;
        }
    }
}

ImU32 ParameterCell::getFillBarColor() {
    static ImU32 color = ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 0.25f));
    return color;
}

ImU32 ParameterCell::getRedOutlineColor() {
    static ImU32 color = ImGui::GetColorU32(ImVec4(0.9f, 0.05f, 0.1f, 1.0f));
    return color;
}

ImU32 ParameterCell::getOrangeOutlineColor() {
    static ImU32 color = ImGui::GetColorU32(ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
    return color;
}

ParameterCellInteraction ParameterCell::draw(int uniqueId,
                                            bool isFocused,
                                            bool shouldFocusFirst,
                                            bool shouldRefocusCurrentCell) {
    ParameterCellInteraction result;
    
    ImGui::PushID(uniqueId);
    
    // Get current value for display
    // Note: We keep NaN as-is for formatDisplayText (which will show "--")
    // but use a default value for fill bar calculations
    float currentVal = getCurrentValue ? getCurrentValue() : defaultValue;
    float displayVal = currentVal; // Keep NaN for display formatting
    
    // Get display text (formatDisplayText handles NaN and shows "--")
    std::string displayText;
    if (isEditing && isSelected) {
        // Show edit buffer when editing (even if empty, to show edit mode is active)
        if (editBuffer.empty()) {
            displayText = formatDisplayText(displayVal);
        } else {
            displayText = editBuffer;
        }
    } else {
        displayText = formatDisplayText(displayVal);
    }
    
    // Calculate fill percent for visualization (calculateFillPercent handles NaN)
    float fillPercent = calculateFillPercent(currentVal);
    
    // Get cell rect for value bar (before drawing button)
    ImVec2 cellMin = ImGui::GetCursorScreenPos();
    float cellHeight = ImGui::GetFrameHeight();
    float cellWidth = ImGui::GetColumnWidth();
    ImVec2 cellMax = ImVec2(cellMin.x + cellWidth, cellMin.y + cellHeight);
    
    // Draw value bar background first (as true background layer)
    if (fillPercent > 0.01f) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (drawList) {
            ImVec2 fillEnd = ImVec2(cellMin.x + (cellMax.x - cellMin.x) * fillPercent, cellMax.y);
            drawList->AddRectFilled(cellMin, fillEnd, getFillBarColor());
        }
    }
    
    // Apply edit mode styling: dark grey/black background (Blender-style)
    if (isEditing && isSelected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.05f, 0.05f, 0.05f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    } else {
        // Make button backgrounds completely transparent when not editing
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
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
    
    // Refocus current cell after exiting edit mode
    if (shouldRefocusCurrentCell && isSelected) {
        ImGui::SetKeyboardFocusHere(-1);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }
    
    // Prevent spacebar from triggering button clicks
    bool spacebarPressed = ImGui::IsKeyPressed(ImGuiKey_Space, false);
    
    // Check actual focus state after drawing (ImGui::IsItemFocused() works for last item)
    bool actuallyFocused = ImGui::IsItemFocused();
    
    // Handle drag state (Blender-style: works across entire window)
    // CRITICAL: Check drag state FIRST to handle restored drag states from previous frames
    // When drag state is restored, isDragging is true but IsItemActive() might be false
    if (isDragging) {
        // Continue drag - update value based on mouse movement (works even if mouse left cell)
        // This handles both active drags and restored drag states
        updateDrag();
        // Ensure we mark drag as started if it was restored (for proper state sync back to GUI)
        if (!result.dragStarted) {
            result.dragStarted = true;
        }
    } else if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        // Start drag when cell is active and mouse is dragging
        // IsItemActive() returns true when mouse was clicked on this item and is still held
        // This works even if mouse has moved outside the cell (Blender-style)
        // NOTE: Don't require isSelected - IsItemActive() is sufficient to indicate the cell was clicked
        // Set isSelected when drag starts to maintain proper state
        if (!isSelected) {
            isSelected = true;
            result.focusChanged = true;
        }
        startDrag();
        result.dragStarted = true;
    }
    
    // Check if drag ended (mouse released anywhere in window)
    // This check happens AFTER updateDrag() so we can properly detect drag end
    // updateDrag() also checks for mouse release internally, but we need to sync the dragEnded flag
    if (isDragging && !ImGui::IsMouseDown(0)) {
        endDrag();
        result.dragEnded = true;
    }
    
    // Sync ImGui focus to selection state
    // Only sync when item was actually clicked, keyboard-navigated, or refocusing after edit
    if (actuallyFocused) {
        bool itemWasClicked = ImGui::IsItemClicked(0);
        bool keyboardNavActive = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
        bool shouldRefocus = shouldRefocusCurrentCell && isSelected;
        
        // Only sync if this is an intentional focus (click, keyboard nav, or refocus)
        if (itemWasClicked || keyboardNavActive || shouldRefocus) {
            result.focusChanged = true;
            
            // Lock focus to editing cell - arrow keys adjust values, not navigate
            if (isEditing && !isSelected) {
                // Don't sync focus change during edit
                result.shouldExitEarly = true;
            } else {
                isSelected = true;
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
    bool isItemClicked = ImGui::IsItemClicked(0);
    if (buttonClicked && !ImGui::IsMouseDragging(0) && !spacebarPressed && isItemClicked) {
        result.clicked = true;
        isSelected = true;
        // DON'T enter edit mode on click - just focus the cell
        // User can type numbers directly (auto-enters edit mode) or hit Enter to enter edit mode
        if (isEditing) {
            exitEditMode();
        }
    }
    
    // Maintain focus during drag (even when mouse leaves cell)
    if (isDragging && !actuallyFocused) {
        // Keep cell focused during drag for visual feedback
        // Don't require IsItemActive() - drag works across entire window
        ImGui::SetKeyboardFocusHere(-1);
    }
    
    // Pop style var and colors
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(3);
    
    // Draw outline for selected/editing cells
    bool shouldShowOutline = isSelected || isDragging || (actuallyFocused && !isEditing);
    if (shouldShowOutline) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (drawList) {
            ImVec2 outlineMin = ImVec2(cellMin.x - 1, cellMin.y - 1);
            ImVec2 outlineMax = ImVec2(cellMax.x + 1, cellMax.y + 1);
            // Orange outline when in edit mode, red outline when just selected or dragging
            ImU32 outlineColor = (isSelected && isEditing)
                ? getOrangeOutlineColor()
                : getRedOutlineColor();
            drawList->AddRect(outlineMin, outlineMax, outlineColor, 0.0f, 0, 2.0f);
        }
    }
    
    ImGui::PopID();
    return result;
}

void ParameterCell::startDrag() {
    if (isDragging) return; // Already dragging
    
    // Exit edit mode when dragging starts
    if (isEditing) {
        exitEditMode();
    }
    
    // Initialize drag state
    isDragging = true;
    dragStartY = ImGui::GetMousePos().y;
    dragStartX = ImGui::GetMousePos().x;
    
    // Get current value as starting point
    if (getCurrentValue) {
        float val = getCurrentValue();
        // Handle NaN (not set) - use default value or middle of range
        if (std::isnan(val)) {
            if (defaultValue >= minVal && defaultValue <= maxVal) {
                lastDragValue = defaultValue;
            } else {
                lastDragValue = (minVal + maxVal) / 2.0f;
            }
        } else {
            lastDragValue = val;
        }
    } else {
        lastDragValue = defaultValue;
    }
    
    // Disable keyboard navigation during drag
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
}

void ParameterCell::updateDrag() {
    if (!isDragging) return;
    
    // Check if mouse is still down (allows dragging across entire window)
    if (!ImGui::IsMouseDown(0)) {
        // Mouse released - end drag
        endDrag();
        return;
    }
    
    // Calculate drag delta (both vertical AND horizontal)
    ImVec2 currentPos = ImGui::GetMousePos();
    float dragDeltaY = dragStartY - currentPos.y; // Up = positive (increase)
    float dragDeltaX = currentPos.x - dragStartX; // Right = positive (increase)
    
    // Use the larger of horizontal or vertical movement for maximum precision
    // This allows dragging in any direction with equal effectiveness
    float totalDragDelta = std::abs(dragDeltaY) > std::abs(dragDeltaX) ? dragDeltaY : dragDeltaX;
    
    // Direct range mapping: map pixel movement directly to value range
    // Sensitivity: pixels needed to traverse full range (200 pixels = full range)
    float rangeSize = maxVal - minVal;
    float sensitivity = 200.0f; // pixels to traverse full range
    
    // Calculate value change directly from pixel movement (maximum precision)
    float valueDelta = (totalDragDelta / sensitivity) * rangeSize;
    float newValue = lastDragValue + valueDelta;
    
    // Clamp to valid range
    newValue = std::max(minVal, std::min(maxVal, newValue));
    
    // For integer parameters, round to nearest integer
    if (isInteger || isFixed) {
        newValue = std::round(newValue);
    }
    
    // Apply immediately (no threshold - maximum precision and responsiveness)
    applyDragValue(newValue);
}

void ParameterCell::endDrag() {
    if (!isDragging) return;
    
    isDragging = false;
    dragStartY = 0.0f;
    dragStartX = 0.0f;
    lastDragValue = 0.0f;
    
    // Re-enable keyboard navigation when drag ends
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

void ParameterCell::applyDragValue(float newValue) {
    if (!onValueApplied) return;
    
    // Clamp value to range
    float clampedValue = std::max(minVal, std::min(maxVal, newValue));
    
    // Apply via callback
    onValueApplied(parameterName, clampedValue);
}



