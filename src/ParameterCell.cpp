#include "ParameterCell.h"
#include "gui/GUIConstants.h"
#include "ofxImGui.h"
#include "ofLog.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <stack>
#include <cctype>
#include <limits>

// Constants for expression evaluation (used by static function)
namespace {
    constexpr float EPSILON_DIVISION = 1e-9f;
}

ParameterCell::ParameterCell() 
    : isSelected(false), shouldRefocus(false), isEditing(false), editBufferInitialized(false), bufferModifiedByUser(false),
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
                if (std::abs(b) < EPSILON_DIVISION) throw std::runtime_error("Division by zero");
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

// Helper function implementations
// Check if string represents empty/NaN value placeholder ("--")
// The "--" string is used to represent NaN (empty cell, no value)
bool ParameterCell::isEmpty(const std::string& str) {
    if (str.empty()) return false;
    for (char c : str) {
        if (c != '-') return false;
    }
    return true;
}

std::string ParameterCell::trimWhitespace(const std::string& str) {
    size_t first = str.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
}

void ParameterCell::disableImGuiKeyboardNav() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
}

void ParameterCell::enableImGuiKeyboardNav() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

void ParameterCell::removeParameter() {
    if (onValueRemoved) {
        onValueRemoved(parameterName);
    }
}

void ParameterCell::setValueRange(float min, float max, float defaultValue) {
    if (min > max) {
        ofLogWarning("ParameterCell") << "Invalid range: min > max, swapping values";
        std::swap(min, max);
    }
    minVal = min;
    maxVal = max;
    this->defaultValue = std::max(min, std::min(max, defaultValue));
}

void ParameterCell::calculateStepIncrement() {
    // Calculate optimal step increment based on parameter type and range
    if (isInteger || isFixed) {
        // Integer parameters: always use 1.0
        stepIncrement = 1.0f;
    } else {
        // Float parameter: unified 0.001 precision for all float parameters
        // This provides consistent fine-grained control across all parameters
        // (position, speed, volume, etc. all use the same precision)
        stepIncrement = 0.001f;
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
            disableImGuiKeyboardNav();
        }
    }
}

void ParameterCell::setEditBuffer(const std::string& buffer, bool initialized) {
    editBuffer = buffer;
    editBufferInitialized = initialized;
    if (!editBuffer.empty()) {
        // If setting a non-empty buffer, ensure we're in edit mode
        if (!isEditing) {
            isEditing = true;
            // Don't call enterEditMode() here as it would re-initialize the buffer
            // Just disable ImGui keyboard navigation
            disableImGuiKeyboardNav();
        }
    }
}

void ParameterCell::enterEditMode() {
    isEditing = true;
    initializeEditBuffer();
    editBufferInitialized = true;
    bufferModifiedByUser = false;  // Buffer was initialized, not modified by user yet
    
    // Disable ImGui keyboard navigation when entering edit mode
    disableImGuiKeyboardNav();
}

void ParameterCell::exitEditMode() {
    isEditing = false;
    editBuffer.clear();
    editBufferInitialized = false;
    bufferModifiedByUser = false;  // Reset flag when exiting edit mode
    
    // Re-enable ImGui keyboard navigation when exiting edit mode
    enableImGuiKeyboardNav();
}

bool ParameterCell::handleKeyPress(int key, bool ctrlPressed, bool shiftPressed) {
    // Enter key behavior
    if (key == OF_KEY_RETURN) {
        ofLogNotice("ParameterCell") << "[DEBUG] handleKeyPress: Enter key pressed"
            << ", isEditing=" << (isEditing ? "YES" : "NO") << ", isSelected=" << (isSelected ? "YES" : "NO")
            << ", ctrlPressed=" << (ctrlPressed ? "YES" : "NO") << ", shiftPressed=" << (shiftPressed ? "YES" : "NO");
        
        if (ctrlPressed || shiftPressed) {
            // Ctrl+Enter or Shift+Enter: Exit edit mode
            ofLogNotice("ParameterCell") << "[DEBUG] Enter with modifier: exiting edit mode";
            exitEditMode();
            return true;
        }
        
        if (isEditing) {
            // In edit mode: Confirm and exit edit mode
            ofLogNotice("ParameterCell") << "[DEBUG] Enter in edit mode: confirming and exiting, editBuffer='" << editBuffer << "'";
            applyValue();
            shouldRefocus = true;
            exitEditMode();
            return true;
        } else if (isSelected) {
            // Selected but not editing: Enter edit mode
            ofLogNotice("ParameterCell") << "[DEBUG] Enter on selected cell: entering edit mode";
            enterEditMode();
            return true;
        }
        ofLogNotice("ParameterCell") << "[DEBUG] Enter key not handled (not editing, not selected)";
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
            bufferModifiedByUser = true;  // User modified the buffer
            
            // Re-apply value after backspace (Blender-style reactive editing)
            // This allows the value to update as the user corrects their input
            if (editBuffer.empty() || isEmpty(editBuffer)) {
                // Buffer is empty or only dashes - remove parameter (set to "none")
                removeParameter();
            } else {
                try {
                    // Try to evaluate as expression (supports operations)
                    float floatValue = evaluateExpression(editBuffer);
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
        if (isEditing) {
            editBuffer.clear();
            editBufferInitialized = false;
            bufferModifiedByUser = true;  // User modified the buffer (cleared it)
            return true;
        }
        return false;
    }
    
    // Numeric input (0-9) - Blender-style: direct typing enters edit mode and replaces value
    if (key >= '0' && key <= '9') {
        ofLogNotice("ParameterCell") << "[DEBUG] handleKeyPress: Numeric key '" << (char)key << "' pressed"
            << ", isEditing=" << (isEditing ? "YES" : "NO") << ", isSelected=" << (isSelected ? "YES" : "NO")
            << ", editBuffer='" << editBuffer << "', bufferModifiedByUser=" << (bufferModifiedByUser ? "YES" : "NO")
            << ", editBufferInitialized=" << (editBufferInitialized ? "YES" : "NO");
        
        bool justEnteredEditMode = false;
        if (!isEditing) {
            // Auto-enter edit mode if cell is selected
            if (isSelected) {
                ofLogNotice("ParameterCell") << "[DEBUG] Entering edit mode via numeric key";
                enterEditMode();
                justEnteredEditMode = true;
            } else {
                ofLogNotice("ParameterCell") << "[DEBUG] Not selected, not handling numeric key";
                return false;  // Not selected, don't handle
            }
        }
        
        // Clear buffer if we just entered edit mode or buffer is empty/placeholder
        // This ensures typing REPLACES the initialized value when starting to type
        bool shouldClear = false;
        if (justEnteredEditMode) {
            ofLogNotice("ParameterCell") << "[DEBUG] Clearing buffer: just entered edit mode";
            shouldClear = true;
        } else if (isEmpty(editBuffer)) {
            ofLogNotice("ParameterCell") << "[DEBUG] Clearing buffer: buffer is empty/placeholder";
            shouldClear = true;
        } else if (editBufferInitialized && !bufferModifiedByUser) {
            ofLogNotice("ParameterCell") << "[DEBUG] Clearing buffer: initialized but not modified by user";
            shouldClear = true;
        }
        
        if (shouldClear) {
            editBuffer.clear();
            editBufferInitialized = false;
        }
        
        // Append digit to buffer
        editBuffer += (char)key;
        bufferModifiedByUser = true;  // Mark that user has modified the buffer
        if (editBuffer.length() > MAX_EDIT_BUFFER_LENGTH) {
            editBuffer = editBuffer.substr(editBuffer.length() - MAX_EDIT_BUFFER_LENGTH);
        }
        
        ofLogNotice("ParameterCell") << "[DEBUG] After appending digit, editBuffer='" << editBuffer << "'";
        
        // Apply value immediately (Blender-style reactive editing)
        // Try to evaluate as expression (supports operations like "2*3", "10/2", etc.)
        if (!editBuffer.empty()) {
            if (isEmpty(editBuffer)) {
                // Only dashes (e.g., "-", "--") - remove parameter (set to "none")
                ofLogNotice("ParameterCell") << "[DEBUG] Buffer is empty/placeholder, removing parameter";
                removeParameter();
            } else {
                try {
                    float floatValue = evaluateExpression(editBuffer);
                    ofLogNotice("ParameterCell") << "[DEBUG] Evaluated expression '" << editBuffer << "' = " << floatValue;
                    applyEditValueFloat(floatValue);
                } catch (const std::exception& e) {
                    ofLogWarning("ParameterCell") << "[DEBUG] Expression evaluation failed: " << e.what();
                } catch (...) {
                    ofLogWarning("ParameterCell") << "[DEBUG] Expression evaluation failed with unknown exception";
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
                // Clear buffer if it's "--" (placeholder) - typing should replace it
                if (isEmpty(editBuffer)) {
                    editBuffer.clear();
                    editBufferInitialized = false;
                }
                // Otherwise, don't clear buffer - allow appending operator to existing value
                // This allows operations like "5*2" or "10/2"
            } else {
                return false;  // Not selected, don't handle
            }
        } else {
            // Already in edit mode - clear buffer if it's "--" (placeholder)
            if (isEmpty(editBuffer)) {
                editBuffer.clear();
                editBufferInitialized = false;
            }
        }
        
        // Append operator to buffer
        editBuffer += (char)key;
        bufferModifiedByUser = true;  // User modified the buffer
        if (editBuffer.length() > MAX_EDIT_BUFFER_LENGTH) {
            editBuffer = editBuffer.substr(editBuffer.length() - MAX_EDIT_BUFFER_LENGTH);
        }
        
        // Try to evaluate expression if it's complete
        // For operators, we wait for the next number before evaluating
        // But we can try to evaluate if the expression is already valid
        if (!editBuffer.empty()) {
            // Check if buffer contains only operators/dashes
            bool onlyOpsOrDashes = true;
            for (char c : editBuffer) {
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
        // For integer/fixed columns, don't allow decimal point input
        if (key == '.' && (isInteger || isFixed)) {
            // Ignore decimal point for integer columns - they should only accept whole numbers
            return true;  // Consume the event but don't add decimal point
        }
        
        if (!isEditing) {
            // Auto-enter edit mode if cell is selected
            if (isSelected) {
                enterEditMode();
                // Clear buffer when entering edit mode via decimal/minus (replaces current value)
                editBuffer.clear();
                editBufferInitialized = false;
            } else {
                return false;  // Not selected, don't handle
            }
        }
        
        // Clear buffer if it's "--" (placeholder) - typing should replace it
        // This ensures typing replaces "--" even if we entered edit mode via Enter key
        if (isEmpty(editBuffer)) {
            editBuffer.clear();
            editBufferInitialized = false;
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
            size_t lastOp = editBuffer.find_last_of("+-*/");
            std::string lastNumber = (lastOp == std::string::npos) ? editBuffer : editBuffer.substr(lastOp + 1);
            if (lastNumber.find('.') != std::string::npos) {
                return true;  // This number already has a decimal point
            }
        }
        
        editBuffer += (char)key;
        bufferModifiedByUser = true;  // User modified the buffer
        if (editBuffer.length() > MAX_EDIT_BUFFER_LENGTH) {
            editBuffer = editBuffer.substr(editBuffer.length() - MAX_EDIT_BUFFER_LENGTH);
        }
        
        // Apply value immediately (Blender-style)
        // Check if buffer is empty, single '.', or contains only dashes
        if (editBuffer.empty() || editBuffer == "." || isEmpty(editBuffer)) {
            // Buffer is only dashes, empty, or single '.' - remove parameter (set to "none")
            removeParameter();
        } else {
            try {
                // Try to evaluate as expression (supports operations)
                float floatValue = evaluateExpression(editBuffer);
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
    if (isEditing) {
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
            if (isInteger || isFixed) {
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

void ParameterCell::appendDigit(char digit) {
    if (!isEditing) {
        enterEditMode();
    }
    editBuffer += digit;
    bufferModifiedByUser = true;  // User modified the buffer
    if (editBuffer.length() > MAX_EDIT_BUFFER_LENGTH) {
        editBuffer = editBuffer.substr(editBuffer.length() - MAX_EDIT_BUFFER_LENGTH);
    }
}

void ParameterCell::appendChar(char c) {
    if (!isEditing) {
        enterEditMode();
    }
    editBuffer += c;
    bufferModifiedByUser = true;  // User modified the buffer
    if (editBuffer.length() > MAX_EDIT_BUFFER_LENGTH) {
        editBuffer = editBuffer.substr(editBuffer.length() - MAX_EDIT_BUFFER_LENGTH);
    }
}

void ParameterCell::backspace() {
    if (isEditing && !editBuffer.empty()) {
        editBuffer.pop_back();
        editBufferInitialized = false;
        bufferModifiedByUser = true;  // User modified the buffer
    }
}

void ParameterCell::deleteChar() {
    if (isEditing) {
        editBuffer.clear();
        bufferModifiedByUser = true;  // User modified the buffer
        editBufferInitialized = false;
    }
}

void ParameterCell::applyValue() {
    parseAndApplyEditBuffer();
}

void ParameterCell::cancelEdit() {
    exitEditMode();
}

void ParameterCell::adjustValue(int delta, float customStepSize) {
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
    
    if (isFixed && fixedType == FIXED_TYPE_INDEX) {
        // Index column: 1-based display (01-99), NaN = rest
        if (std::isnan(currentVal)) {
            editBuffer = "--"; // Show "--" for NaN (empty/rest)
        } else {
            int indexVal = (int)std::round(currentVal);
            if (indexVal <= 0) {
                editBuffer = "--"; // Also handle edge case
            } else {
                char buf[8];
                snprintf(buf, sizeof(buf), "%02d", indexVal);
                editBuffer = buf;
            }
        }
    } else if (isFixed && fixedType == FIXED_TYPE_LENGTH) {
        // Length column: 1-16 range, NaN = not set
        if (std::isnan(currentVal)) {
            editBuffer = "--"; // Show "--" for NaN (empty/not set)
        } else {
            int lengthVal = (int)std::round(currentVal);
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d", lengthVal); // Zero-padded to 2 digits for consistency
            editBuffer = buf;
        }
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
    
    // Use custom formatValue callback if available (allows for logarithmic mapping, etc.)
    if (formatValue) {
        return formatValue(value);
    }
    
    if (isBool) {
        return value > 0.5f ? "ON" : "OFF";
    }
    
    if (isFixed && fixedType == FIXED_TYPE_INDEX) {
        // Index: 1-based display (01-99)
        // NaN is already handled above, so we can process valid values
        int idx = (int)std::round(value);
        if (idx <= 0) {
            return "--";
        }
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d", idx);
        return buf;
    }
    
    if (isFixed && fixedType == FIXED_TYPE_LENGTH) {
        // Length: 1-16 range, formatted as "02"
        // NaN is already handled above, so we can process valid values
        int len = std::max(LENGTH_MIN, std::min(LENGTH_MAX, (int)std::round(value)));
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d", len);
        return buf;
    }
    
    // Float value: 3 decimal places (0.001 precision) - unified for all float parameters
    char buf[16];
    snprintf(buf, sizeof(buf), "%.3f", value);
    return buf;
}

float ParameterCell::calculateFillPercent(float value) const {
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

void ParameterCell::applyEditValueFloat(float floatValue) {
    if (isFixed && fixedType == FIXED_TYPE_LENGTH) {
        // Length must be integer between 1-16
        // For fixed columns, clamp to valid range
        int newValue = std::max(LENGTH_MIN, std::min(LENGTH_MAX, (int)std::round(floatValue)));
        applyEditValueInt(newValue);
    } else if (isFixed && fixedType == FIXED_TYPE_INDEX) {
        // Index: 0 = rest, 1+ = media index (1-based display)
        // For fixed columns, clamp to valid range
        int maxIdx = getMaxIndex ? getMaxIndex() : INDEX_MAX_DEFAULT;
        int newValue = std::max(0, std::min(maxIdx, (int)std::round(floatValue)));
        applyEditValueInt(newValue);
    } else {
        // Dynamic parameter or MediaPool parameter
        // If value is outside range, remove parameter (set to "none" state)
        // This allows users to clear invalid values by typing out-of-range numbers
        if (floatValue < minVal || floatValue > maxVal) {
            // Value is outside valid range - remove parameter
            removeParameter();
        } else {
            // Value is within range - apply it
            if (onValueApplied) {
                onValueApplied(parameterName, floatValue);
            }
        }
    }
}

void ParameterCell::applyEditValueInt(int intValue) {
    if (isFixed && fixedType == FIXED_TYPE_INDEX) {
        // Index: 0 = rest (-1 in storage), 1+ = media index (0-based in storage)
        // But we work with 1-based display values here
        // The callback should handle the conversion
        if (onValueApplied) {
            onValueApplied(parameterName, (float)intValue);
        }
        // Update edit buffer to show integer format (zero-padded 2 digits)
        if (intValue <= 0) {
            editBuffer = "--";
        } else {
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d", intValue);
            editBuffer = buf;
        }
    } else if (isFixed && fixedType == FIXED_TYPE_LENGTH) {
        // Length: 1-16 range
        int clampedValue = std::max(LENGTH_MIN, std::min(LENGTH_MAX, intValue));
        if (onValueApplied) {
            onValueApplied(parameterName, (float)clampedValue);
        }
        // Update edit buffer to show integer format (zero-padded 2 digits)
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d", clampedValue);
        editBuffer = buf;
    } else {
        // Shouldn't happen for non-fixed, but handle it
        if (onValueApplied) {
            onValueApplied(parameterName, (float)intValue);
        }
    }
}

bool ParameterCell::parseAndApplyEditBuffer() {
    // Trim whitespace for comparison
    std::string trimmed = trimWhitespace(editBuffer);
    
    // Handle '--' input for Index column (disables/removes index)
    if (isFixed && fixedType == FIXED_TYPE_INDEX) {
        if (trimmed.empty() || isEmpty(trimmed)) {
            // Empty buffer or only dashes - remove parameter (set to rest/NaN)
            removeParameter();
            return true;
        }
    }
    
    // For fixed Length column, empty buffer is invalid
    if (isFixed && fixedType == FIXED_TYPE_LENGTH && editBuffer.empty()) {
        return false;
    }
    
    // Handle empty buffer or invalid input for dynamic parameters (removes parameter)
    if (!isFixed) {
        // Check for clear patterns: empty, or strings containing only dashes
        if (trimmed.empty() || isEmpty(trimmed)) {
            // Empty buffer or only dashes - remove parameter
            removeParameter();
            return true;
        }
    }
    
    // Try to parse the buffer
    try {
        if (isFixed && fixedType == FIXED_TYPE_LENGTH) {
            int lengthValue = std::max(LENGTH_MIN, std::min(LENGTH_MAX, (int)std::round(std::stof(editBuffer))));
            applyEditValueInt(lengthValue);
            return true;
        } else if (isFixed && fixedType == FIXED_TYPE_INDEX) {
            int maxIdx = getMaxIndex ? getMaxIndex() : INDEX_MAX_DEFAULT;
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
                    removeParameter();
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
                        removeParameter();
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
            removeParameter();
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
    // Float value: 3 decimal places (0.001 precision) - unified for all float parameters
    char buf[16];
    snprintf(buf, sizeof(buf), "%.3f", value);
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

ImU32 ParameterCell::getFillBarColor() const {
    static ImU32 color = GUIConstants::toU32(GUIConstants::ParameterCell::FillBar);
    return color;
}

ImU32 ParameterCell::getRedOutlineColor() const {
    static ImU32 color = GUIConstants::toU32(GUIConstants::Outline::RedDim);
    return color;
}

ImU32 ParameterCell::getOrangeOutlineColor() const {
    static ImU32 color = GUIConstants::toU32(GUIConstants::Outline::Orange);
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
    
    // Draw value bar background (no cell background - using row background instead)
    if (fillPercent > 0.01f) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (drawList) {
            ImVec2 fillEnd = ImVec2(cellMin.x + (cellMax.x - cellMin.x) * fillPercent, cellMax.y);
            drawList->AddRectFilled(cellMin, fillEnd, getFillBarColor());
        }
    }
    
    // Apply edit mode styling: dark grey/black background (Blender-style)
    if (isEditing && isSelected) {
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
    
    // Refocus current cell after exiting edit mode
    // Use either the passed parameter OR the internal flag (set when Enter exits edit mode)
    // This unifies refocus logic - GUI classes can pass shouldRefocusCurrentCell, or ParameterCell
    // will automatically refocus if it set the internal shouldRefocus flag
    bool needsRefocus = (shouldRefocusCurrentCell || shouldRefocus) && isSelected;
    if (needsRefocus) {
        ImGui::SetKeyboardFocusHere(-1);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        // Clear internal flag after using it (only clear if we actually refocused)
        shouldRefocus = false;
    }
    
    // Prevent spacebar from triggering button clicks
    bool spacebarPressed = ImGui::IsKeyPressed(ImGuiKey_Space, false);
    
    // Check actual focus state after drawing (ImGui::IsItemFocused() works for last item)
    bool actuallyFocused = ImGui::IsItemFocused();
    
    // PHASE 1: Handle keyboard input directly in draw() when item is focused
    // This eliminates the need for state synchronization and makes ParameterCell self-contained
    // CRITICAL: Handle input when focused AND (selected OR in edit mode)
    // This ensures Enter works in edit mode even if isSelected is false
    if (actuallyFocused && (isSelected || isEditing)) {
        ImGuiIO& io = ImGui::GetIO();
        
        // DEBUG: Only log when there's actual input or state changes (reduced verbosity)
        
        // Helper lambda to convert ImGui key to OF_KEY code
        auto convertImGuiKeyToOF = [](ImGuiKey imguiKey) -> int {
            switch (imguiKey) {
                case ImGuiKey_Enter: return OF_KEY_RETURN;
                case ImGuiKey_KeypadEnter: return OF_KEY_RETURN;
                case ImGuiKey_Escape: return OF_KEY_ESC;
                case ImGuiKey_Backspace: return OF_KEY_BACKSPACE;
                case ImGuiKey_Delete: return OF_KEY_DEL;
                case ImGuiKey_UpArrow: return OF_KEY_UP;
                case ImGuiKey_DownArrow: return OF_KEY_DOWN;
                case ImGuiKey_LeftArrow: return OF_KEY_LEFT;
                case ImGuiKey_RightArrow: return OF_KEY_RIGHT;
                default: return 0;
            }
        };
        
        // Use frame counter to prevent processing keys multiple times if draw() is called multiple times per frame
        int currentFrame = ofGetFrameNum();
        bool shouldProcessKeys = (currentFrame != lastProcessedFrame);
        
        // Check for Enter key (Enter or KeypadEnter)
        if (shouldProcessKeys && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
            ofLogNotice("ParameterCell") << "[DEBUG] Enter key pressed in draw()";
            lastProcessedFrame = currentFrame;  // Mark this frame as processed
            bool ctrlPressed = io.KeyCtrl;
            bool shiftPressed = io.KeyShift;
            int key = convertImGuiKeyToOF(ImGuiKey_Enter);
            if (handleKeyPress(key, ctrlPressed, shiftPressed)) {
                ofLogNotice("ParameterCell") << "[DEBUG] Enter key handled";
            } else {
                ofLogWarning("ParameterCell") << "[DEBUG] Enter key NOT handled";
            }
        }
        
        // Check for Escape key
        if (shouldProcessKeys && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ofLogNotice("ParameterCell") << "[DEBUG] Escape key pressed in draw()";
            lastProcessedFrame = currentFrame;  // Mark this frame as processed
            if (handleKeyPress(OF_KEY_ESC, false, false)) {
                ofLogNotice("ParameterCell") << "[DEBUG] Escape key handled";
            }
        }
        
        // Check for Backspace key
        if (shouldProcessKeys && ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
            ofLogNotice("ParameterCell") << "[DEBUG] Backspace key pressed in draw()";
            lastProcessedFrame = currentFrame;  // Mark this frame as processed
            if (handleKeyPress(OF_KEY_BACKSPACE, false, false)) {
                ofLogNotice("ParameterCell") << "[DEBUG] Backspace key handled";
            }
        }
        
        // Check for Delete key
        if (shouldProcessKeys && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            ofLogNotice("ParameterCell") << "[DEBUG] Delete key pressed in draw()";
            lastProcessedFrame = currentFrame;  // Mark this frame as processed
            if (handleKeyPress(OF_KEY_DEL, false, false)) {
                ofLogNotice("ParameterCell") << "[DEBUG] Delete key handled";
            }
        }
        
        // Check for character input (numeric keys, operators, etc.)
        // Use ImGui's character input queue which is more reliable than key codes
        // This handles both main keyboard and numpad input
        // Process ALL characters in the queue to support multi-digit input (e.g., "111", "0.256")
        // Process InputQueueCharacters - these are text input characters
        // CRITICAL: Process all characters in the queue, but only once per frame
        // Use frame counter to prevent processing the same input multiple times if draw() is called multiple times
        if (io.InputQueueCharacters.Size > 0 && shouldProcessKeys) {
            ofLogNotice("ParameterCell") << "[DEBUG] InputQueueCharacters.Size=" << io.InputQueueCharacters.Size << ", currentFrame=" << currentFrame << ", lastProcessedFrame=" << lastProcessedFrame;
            lastProcessedFrame = currentFrame;  // Mark this frame as processed
            
            // Process each character only once
            for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
                unsigned int c = io.InputQueueCharacters[i];
                ofLogNotice("ParameterCell") << "[DEBUG] Processing character '" << (char)c << "' (" << (int)c << ") from InputQueueCharacters[" << i << "]";
                bool handled = false;
                
                // Check for numeric keys (0-9)
                if (c >= '0' && c <= '9') {
                    ofLogNotice("ParameterCell") << "[DEBUG] Numeric character '" << (char)c << "' detected, calling handleKeyPress";
                    handled = handleKeyPress((int)c, false, false);
                    ofLogNotice("ParameterCell") << "[DEBUG] handleKeyPress returned " << (handled ? "true" : "false");
                }
                // Check for decimal point
                else if (c == '.' || c == ',') {  // Some keyboards use comma as decimal
                    ofLogNotice("ParameterCell") << "[DEBUG] Decimal point detected, calling handleKeyPress";
                    handled = handleKeyPress('.', false, false);
                }
                // Check for minus sign
                else if (c == '-') {
                    ofLogNotice("ParameterCell") << "[DEBUG] Minus sign detected, calling handleKeyPress";
                    handled = handleKeyPress('-', false, false);
                }
                // Check for operators (only in edit mode)
                else if (isEditing) {
                    if (c == '+') {
                        handled = handleKeyPress('+', false, false);
                    } else if (c == '*') {
                        handled = handleKeyPress('*', false, false);
                    } else if (c == '/') {
                        handled = handleKeyPress('/', false, false);
                    }
                }
            }
            
            // CRITICAL: Clear InputQueueCharacters after processing to prevent double-processing
            // This ensures each character is only processed once, even if draw() is called multiple times
            io.InputQueueCharacters.clear();
        } else if (io.InputQueueCharacters.Size > 0) {
            ofLogNotice("ParameterCell") << "[DEBUG] Skipping InputQueueCharacters processing - already processed this frame (currentFrame=" << currentFrame << ", lastProcessedFrame=" << lastProcessedFrame << ")";
            // Still clear it to prevent other widgets from processing it
            io.InputQueueCharacters.clear();
        }
        
        // Also check for keypad keys using key codes (for numpad support)
        // These might not appear in InputQueueCharacters
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad1, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Keypad2, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad3, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Keypad4, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad5, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Keypad6, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad7, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Keypad8, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad9, false)) {
            // Map keypad keys to character codes
            int keypadChar = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) keypadChar = '0';
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad1, false)) keypadChar = '1';
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad2, false)) keypadChar = '2';
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad3, false)) keypadChar = '3';
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad4, false)) keypadChar = '4';
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad5, false)) keypadChar = '5';
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad6, false)) keypadChar = '6';
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad7, false)) keypadChar = '7';
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad8, false)) keypadChar = '8';
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad9, false)) keypadChar = '9';
            
            if (keypadChar > 0) {
                handleKeyPress(keypadChar, false, false);
            }
        }
        
        // Check for keypad decimal and operators
        if (ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false)) {
            handleKeyPress('.', false, false);
        }
        if (isEditing) {
            if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)) {
                handleKeyPress('+', false, false);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) {
                handleKeyPress('-', false, false);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_KeypadMultiply, false)) {
                handleKeyPress('*', false, false);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_KeypadDivide, false)) {
                handleKeyPress('/', false, false);
            }
        }
        
        // Check for arrow keys in edit mode (adjust values)
        if (isEditing) {
            bool shiftPressed = io.KeyShift;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
                handleKeyPress(OF_KEY_UP, false, shiftPressed);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
                handleKeyPress(OF_KEY_DOWN, false, shiftPressed);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
                handleKeyPress(OF_KEY_LEFT, false, shiftPressed);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
                handleKeyPress(OF_KEY_RIGHT, false, shiftPressed);
            }
        }
    }
    
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
        // Start drag when cell is active and mouse is actually being dragged
        // Use IsMouseDragging(0) to require actual mouse movement before starting drag
        // This prevents drag from starting on simple clicks - clicks should just focus the cell
        // IsItemActive() returns true when mouse was clicked on this item and is still held
        // IsMouseDragging(0) only returns true if mouse has moved while button is held
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
        // Use either the passed parameter OR the internal flag (unified refocus logic)
        bool needsRefocus = (shouldRefocusCurrentCell || shouldRefocus) && isSelected;
        
        // Only sync if this is an intentional focus (click, keyboard nav, or refocus)
        if (itemWasClicked || keyboardNavActive || needsRefocus) {
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
    
    // Handle double-click: clear the cell (remove parameter)
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        // Exit edit mode if active
        if (isEditing) {
            exitEditMode();
        }
        // Clear the cell by removing the parameter
        removeParameter();
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
    disableImGuiKeyboardNav();
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
    
    // Multi-precision dragging: standard for full-range traversal, Shift for unified fine precision
    // Check modifier keys for precision control
    ImGuiIO& io = ImGui::GetIO();
    bool shiftPressed = (io.KeyMods & ImGuiMod_Shift) != 0;
    
    float dragStepIncrement;
    if (isInteger || isFixed) {
        // Integer parameters: Always 1 step per pixel (modifiers don't affect integers)
        dragStepIncrement = 1.0f;
    } else {
        // Float parameters: Multi-precision based on modifier keys
        float rangeSize = maxVal - minVal;
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
    enableImGuiKeyboardNav();
}

void ParameterCell::applyDragValue(float newValue) {
    if (!onValueApplied) return;
    
    // Clamp value to range
    float clampedValue = std::max(minVal, std::min(maxVal, newValue));
    
    // Apply via callback
    onValueApplied(parameterName, clampedValue);
}



