#include "CellWidget.h"
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

CellWidget::CellWidget() 
    : selected_(false), shouldRefocus_(false), editing_(false), editBufferInitialized_(false), bufferModifiedByUser_(false),
      dragging_(false), dragStartY_(0.0f), dragStartX_(0.0f), lastDragValue_(0.0f) {
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
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
}

void CellWidget::enableImGuiKeyboardNav() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
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
            // Just disable ImGui keyboard navigation
            disableImGuiKeyboardNav();
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
            // Just disable ImGui keyboard navigation
            disableImGuiKeyboardNav();
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
    editing_ = true;
    initializeEditBuffer();
    editBufferInitialized_ = true;
    bufferModifiedByUser_ = false;  // Buffer was initialized, not modified by user yet
    
    // Disable ImGui keyboard navigation when entering edit mode
    disableImGuiKeyboardNav();
}

void CellWidget::exitEditMode() {
    editing_ = false;
    editBuffer_.clear();
    editBufferInitialized_ = false;
    bufferModifiedByUser_ = false;  // Reset flag when exiting edit mode
    
    // Re-enable ImGui keyboard navigation when exiting edit mode
    enableImGuiKeyboardNav();
}

bool CellWidget::handleKeyPress(int key, bool ctrlPressed, bool shiftPressed) {
    // Enter key behavior
    if (key == OF_KEY_RETURN) {
        ofLogNotice("CellWidget") << "[DEBUG] handleKeyPress: Enter key pressed"
            << ", editing_=" << (editing_ ? "YES" : "NO") << ", isSelected=" << (isSelected() ? "YES" : "NO")
            << ", ctrlPressed=" << (ctrlPressed ? "YES" : "NO") << ", shiftPressed=" << (shiftPressed ? "YES" : "NO");
        
        if (ctrlPressed || shiftPressed) {
            // Ctrl+Enter or Shift+Enter: Exit edit mode
            ofLogNotice("CellWidget") << "[DEBUG] Enter with modifier: exiting edit mode";
            exitEditMode();
            return true;
        }
        
        if (editing_) {
            // In edit mode: Confirm and exit edit mode
            ofLogNotice("CellWidget") << "[DEBUG] Enter in edit mode: confirming and exiting, editBuffer_='" << editBuffer_ << "'";
            applyValue();
            // CRITICAL: Set refocus flag BEFORE exiting edit mode
            // This ensures the cell maintains focus after validation
            setShouldRefocus(true);
            exitEditMode();
            // Note: Refocus will happen in draw() after state is synced back to GUI
            return true;
        } else if (isSelected()) {
            // For BUTTON mode: Enter should trigger button click, not enter edit mode
            if (cellType == CellWidgetType::BUTTON) {
                ofLogNotice("CellWidget") << "[DEBUG] Enter on button: triggering click";
                // Trigger button click
                if (enableStateCycling && onButtonCycleState) {
                    onButtonCycleState();
                } else if (onButtonClicked) {
                    onButtonClicked();
                }
                // Maintain focus after state change
                setShouldRefocus(true);
                return true;
            }
            // For SLIDER mode: Enter edit mode
            ofLogNotice("CellWidget") << "[DEBUG] Enter on selected cell: entering edit mode";
            enterEditMode();
            return true;
        }
        ofLogNotice("CellWidget") << "[DEBUG] Enter key not handled (not editing, not selected)";
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
                    float floatValue = evaluateExpression(editBuffer_);
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
        ofLogNotice("CellWidget") << "[DEBUG] handleKeyPress: Numeric key '" << (char)key << "' pressed"
            << ", editing_=" << (editing_ ? "YES" : "NO") << ", isSelected=" << (isSelected() ? "YES" : "NO")
            << ", editBuffer_='" << editBuffer_ << "', bufferModifiedByUser_=" << (bufferModifiedByUser_ ? "YES" : "NO")
            << ", editBufferInitialized_=" << (editBufferInitialized_ ? "YES" : "NO");
        
        bool justEnteredEditMode = false;
        if (!editing_) {
            // Auto-enter edit mode if cell is selected
            if (isSelected()) {
                ofLogNotice("CellWidget") << "[DEBUG] Entering edit mode via numeric key";
                // CRITICAL: If buffer is already set (restored from cache), don't call enterEditMode()
                // as it would overwrite the restored buffer. Instead, just set editing_ and preserve the buffer.
                if (editBuffer_.empty() || !bufferModifiedByUser_) {
                    // Buffer is empty or not modified yet - safe to call enterEditMode()
                    enterEditMode();
                    justEnteredEditMode = true;
                } else {
                    // Buffer is already set (restored from cache) - just enable edit mode without reinitializing
                    ofLogNotice("CellWidget") << "[DEBUG] Buffer already set (restored from cache), preserving it";
                    editing_ = true;
                    disableImGuiKeyboardNav();
                    // Don't set justEnteredEditMode - we want to preserve the buffer
                }
            } else {
                ofLogNotice("CellWidget") << "[DEBUG] Not selected, not handling numeric key";
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
                ofLogNotice("CellWidget") << "[DEBUG] Clearing buffer: just entered edit mode (buffer not modified yet)";
                shouldClear = true;
            } else {
                ofLogNotice("CellWidget") << "[DEBUG] NOT clearing buffer: just entered edit mode but buffer was already modified (restored from cache)";
            }
        } else if (isEmpty(editBuffer_)) {
            ofLogNotice("CellWidget") << "[DEBUG] Clearing buffer: buffer is empty/placeholder";
            shouldClear = true;
        } else if (editBufferInitialized_ && !bufferModifiedByUser_) {
            ofLogNotice("CellWidget") << "[DEBUG] Clearing buffer: initialized but not modified by user";
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
        
        ofLogNotice("CellWidget") << "[DEBUG] After appending digit, editBuffer_='" << editBuffer_ << "'";
        
        // Apply value immediately (Blender-style reactive editing)
        // Try to evaluate as expression (supports operations like "2*3", "10/2", etc.)
        if (!editBuffer_.empty()) {
            if (isEmpty(editBuffer_)) {
                // Only dashes (e.g., "-", "--") - remove parameter (set to "none")
                ofLogNotice("CellWidget") << "[DEBUG] Buffer is empty/placeholder, removing parameter";
                removeParameter();
            } else {
                try {
                    float floatValue = evaluateExpression(editBuffer_);
                    ofLogNotice("CellWidget") << "[DEBUG] Evaluated expression '" << editBuffer_ << "' = " << floatValue;
                    applyEditValueFloat(floatValue);
                } catch (const std::exception& e) {
                    ofLogWarning("CellWidget") << "[DEBUG] Expression evaluation failed: " << e.what() << " - treating as invalid input (NaN/--)";
                    // Invalid input - interpret as NaN/'--' (clear parameter)
                    removeParameter();
                } catch (...) {
                    ofLogWarning("CellWidget") << "[DEBUG] Expression evaluation failed with unknown exception - treating as invalid input (NaN/--)";
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
                    float floatValue = evaluateExpression(editBuffer_);
                    applyEditValueFloat(floatValue);
                } catch (const std::exception& e) {
                    ofLogWarning("CellWidget") << "[DEBUG] Expression evaluation failed: " << e.what() << " - treating as invalid input (NaN/--)";
                    // Invalid input - interpret as NaN/'--' (clear parameter)
                    removeParameter();
                } catch (...) {
                    ofLogWarning("CellWidget") << "[DEBUG] Expression evaluation failed - treating as invalid input (NaN/--)";
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
                float floatValue = evaluateExpression(editBuffer_);
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
                floatValue = evaluateExpression(editBuffer_);
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
    
    // Get cell rect (before drawing button)
    ImVec2 cellMin = ImGui::GetCursorScreenPos();
    float cellHeight = ImGui::GetFrameHeight();
    float cellWidth = ImGui::GetColumnWidth();
    ImVec2 cellMax = ImVec2(cellMin.x + cellWidth, cellMin.y + cellHeight);
    
    // Handle BUTTON mode vs SLIDER mode
    CellWidgetInteraction result;
    if (cellType == CellWidgetType::BUTTON) {
        result = drawButtonMode(uniqueId, isFocused, shouldFocusFirst, shouldRefocusCurrentCell, cellMin, cellMax);
        ImGui::PopID();
        return result;
    } else {
        result = drawSliderMode(uniqueId, isFocused, shouldFocusFirst, shouldRefocusCurrentCell, inputContext, cellMin, cellMax);
        ImGui::PopID();
        return result;
    }
}

CellWidgetInteraction CellWidget::drawButtonMode(int uniqueId, bool isFocused, bool shouldFocusFirst, bool shouldRefocusCurrentCell, const ImVec2& cellMin, const ImVec2& cellMax) {
    CellWidgetInteraction result;
    
    // BUTTON mode: use button callbacks for display and interaction
    std::string displayText = getButtonLabel ? getButtonLabel() : "";
    bool isActive = isButtonActive ? isButtonActive() : false;
    
    // Set cell background to match step number button style (like drawStepNumber does)
    static ImU32 buttonCellBgColor = GUIConstants::toU32(GUIConstants::Background::StepNumber);
    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, buttonCellBgColor);
    
    // Apply active state styling (green when active) - only push colors when active
    // When not active, let ImGui use default button styling (like drawStepNumber does)
    if (isActive) {
        ImGui::PushStyleColor(ImGuiCol_Button, GUIConstants::Active::StepButton);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GUIConstants::Active::StepButtonHover);
    }
    
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f)); // Center-aligned for buttons
    
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
    
    // Refocus current cell if requested
    bool needsRefocus = (shouldRefocusCurrentCell || shouldRefocus()) && isSelected();
    if (needsRefocus) {
        ImGui::SetKeyboardFocusHere(-1);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        setShouldRefocus(false);
    }
    
    // Check actual focus state after drawing
    bool actuallyFocused = ImGui::IsItemFocused();
    result.focusChanged = (actuallyFocused != isFocused);
    
    // Sync ImGui focus to selection state (like slider mode)
    // Only sync when item was actually clicked, keyboard-navigated, or refocusing
    if (actuallyFocused) {
        bool itemWasClicked = ImGui::IsItemClicked(0);
        bool keyboardNavActive = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
        bool needsRefocusForSync = (shouldRefocusCurrentCell || shouldRefocus()) && isSelected();
        
        // Only sync if this is an intentional focus (click, keyboard nav, or refocus)
        if (itemWasClicked || keyboardNavActive || needsRefocusForSync) {
            result.focusChanged = true;
            setSelected(true);
        }
    } else {
        // Not focused - clear selection if it was previously selected
        // This ensures button cells don't remain focused when navigating away (to header, outside grid, etc.)
        // CRITICAL: Check actual focus state, not just isFocused parameter (which might be stale)
        if (isSelected() && !actuallyFocused) {
            setSelected(false);
            result.focusChanged = true; // Ensure focus change is reported
        }
    }
    
    // Prevent spacebar from triggering button clicks (spacebar should be global play/pause)
    // This matches the behavior in TrackerSequencerGUI::drawStepNumber
    bool spacebarPressed = ImGui::IsKeyPressed(ImGuiKey_Space, false);
    
    // Handle button click - simplified: just check buttonClicked and spacebar
    // ImGui::Button() already handles click detection properly
    if (buttonClicked && !spacebarPressed) {
        result.clicked = true;
        setSelected(true); // Ensure selection on click
        
        if (enableStateCycling && onButtonCycleState) {
            // State cycling mode: call cycle callback
            onButtonCycleState();
        } else if (onButtonClicked) {
            // Action trigger mode: call click callback
            onButtonClicked();
        }
    }
    
    // Show tooltip if available
    if (ImGui::IsItemHovered() && getButtonTooltip) {
        std::string tooltip = getButtonTooltip();
        if (!tooltip.empty()) {
            ImGui::SetTooltip("%s", tooltip.c_str());
        }
    }
    
    // Pop styling (only pop colors if we pushed them)
    ImGui::PopStyleVar();
    if (isActive) {
        ImGui::PopStyleColor(2);
    }
    
    // Draw outline for selected/focused buttons (like slider mode)
    bool shouldShowOutline = isSelected() || (actuallyFocused && !editing_);
    if (shouldShowOutline) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (drawList) {
            ImVec2 outlineMin = ImVec2(cellMin.x - 1, cellMin.y - 1);
            ImVec2 outlineMax = ImVec2(cellMax.x + 1, cellMax.y + 1);
            // Red outline for selected/focused buttons (buttons don't have edit mode, so always red)
            ImU32 outlineColor = getRedOutlineColor();
            drawList->AddRect(outlineMin, outlineMax, outlineColor, 0.0f, 0, 2.0f);
        }
    }
    
    return result;
}

CellWidgetInteraction CellWidget::drawSliderMode(int uniqueId, bool isFocused, bool shouldFocusFirst, bool shouldRefocusCurrentCell, const CellWidgetInputContext& inputContext, const ImVec2& cellMin, const ImVec2& cellMax) {
    CellWidgetInteraction result;
    
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
    
    // Refocus current cell after exiting edit mode
    // Use either the passed parameter OR the internal flag (set when Enter exits edit mode)
    // This unifies refocus logic - GUI classes can pass shouldRefocusCurrentCell, or ParameterCell
    // will automatically refocus if it set the internal shouldRefocus flag
        bool needsRefocus = (shouldRefocusCurrentCell || shouldRefocus()) && isSelected();
    if (needsRefocus) {
        ImGui::SetKeyboardFocusHere(-1);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        // Clear internal flag after using it (only clear if we actually refocused)
        setShouldRefocus(false);
    }
    
    // Prevent spacebar from triggering button clicks
    bool spacebarPressed = ImGui::IsKeyPressed(ImGuiKey_Space, false);
    
    // Check actual focus state after drawing (ImGui::IsItemFocused() works for last item)
    bool actuallyFocused = ImGui::IsItemFocused();
    
    // Handle keyboard input directly in draw() when item is focused
    // CRITICAL: When in edit mode, always process input (even if not focused) to allow multi-character input
    // When not in edit mode, process input if cell is selected (selection indicates cell should accept input)
    // The handleInputInDraw function has its own logic to handle focus state correctly
    // This ensures input works immediately after Enter validates, even if ImGui focus hasn't been restored yet
    if (isSelected()) {
        handleInputInDraw(actuallyFocused, inputContext);
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
        // Use either the passed parameter OR the internal flag (unified refocus logic)
        bool needsRefocus = (shouldRefocusCurrentCell || shouldRefocus()) && isSelected();
        
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
    bool isItemClicked = ImGui::IsItemClicked(0);
    if (buttonClicked && !ImGui::IsMouseDragging(0) && !spacebarPressed && isItemClicked) {
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

void CellWidget::handleInputInDraw(bool actuallyFocused, const CellWidgetInputContext& inputContext) {
    // Handle keyboard input directly in draw() when item is focused
    // This eliminates the need for state synchronization and makes ParameterCell self-contained
    // CRITICAL: Process input if:
    // - Cell is selected (selection indicates cell should accept input, even if not focused yet)
    // - Cell is in edit mode (always process input in edit mode)
    // - Cell is actually focused (ImGui focus)
    // This ensures input works immediately after Enter validates, even if ImGui focus hasn't been restored yet
    if (!isSelected() && !editing_ && !actuallyFocused) {
        return;  // Not selected, not editing, and not focused - don't process input
    }
    
        ImGuiIO& io = ImGui::GetIO();
        
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
        
    // Use frame counter from input context to prevent processing keys multiple times
    // Frame tracking is per-grid instance (managed by CellGrid), allowing multiple grids to work independently
    // If no context provided (pointers are null), process keys normally (backward compatibility)
    int currentFrame = inputContext.currentFrame >= 0 ? inputContext.currentFrame : ofGetFrameNum();
    bool shouldProcessInputQueue = (!inputContext.lastProcessedInputQueueFrame || currentFrame != *inputContext.lastProcessedInputQueueFrame);
    bool inputQueueProcessed = false;  // Track if we processed InputQueueCharacters this frame
        
        // CRITICAL: Process InputQueueCharacters FIRST (before keypad keys) to avoid double-processing
        // Numpad keys can appear in both InputQueueCharacters AND as keypad key codes
        // We process InputQueueCharacters first, then skip keypad key processing if we already processed input this frame
        if (io.InputQueueCharacters.Size > 0 && shouldProcessInputQueue) {
        ofLogNotice("CellWidget") << "[DEBUG] InputQueueCharacters.Size=" << io.InputQueueCharacters.Size << ", currentFrame=" << currentFrame;
        if (inputContext.lastProcessedInputQueueFrame) {
            *inputContext.lastProcessedInputQueueFrame = currentFrame;  // Mark this frame as processed for InputQueueCharacters
        }
        if (inputContext.lastProcessedFrame) {
            *inputContext.lastProcessedFrame = currentFrame;  // Also mark general key processing to skip keypad keys
        }
        inputQueueProcessed = true;  // Mark that we processed InputQueueCharacters
            
            // Process each character only once
            for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
                unsigned int c = io.InputQueueCharacters[i];
                ofLogNotice("CellWidget") << "[DEBUG] Processing character '" << (char)c << "' (" << (int)c << ") from InputQueueCharacters[" << i << "]";
                bool handled = false;
                
                // Check for numeric keys (0-9)
                if (c >= '0' && c <= '9') {
                    ofLogNotice("CellWidget") << "[DEBUG] Numeric character '" << (char)c << "' detected, calling handleKeyPress";
                    handled = handleKeyPress((int)c, false, false);
                    ofLogNotice("CellWidget") << "[DEBUG] handleKeyPress returned " << (handled ? "true" : "false");
                }
                // Check for decimal point
                else if (c == '.' || c == ',') {  // Some keyboards use comma as decimal
                    ofLogNotice("CellWidget") << "[DEBUG] Decimal point detected, calling handleKeyPress";
                    handled = handleKeyPress('.', false, false);
                }
                // Check for minus sign
                else if (c == '-') {
                    ofLogNotice("CellWidget") << "[DEBUG] Minus sign detected, calling handleKeyPress";
                    handled = handleKeyPress('-', false, false);
                }
                // Check for operators (only in edit mode)
                else if (editing_) {
                    if (c == '+') {
                        handled = handleKeyPress('+', false, false);
                    } else if (c == '*') {
                        handled = handleKeyPress('*', false, false);
                    } else if (c == '/') {
                        handled = handleKeyPress('/', false, false);
                } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                        // Invalid character (letter) - treat as invalid input, clear parameter
                        ofLogNotice("CellWidget") << "[DEBUG] Invalid character (letter) '" << (char)c << "' detected - treating as invalid input (NaN/--)";
                        if (editing_) {
                            // Clear buffer and remove parameter
                            editBuffer_.clear();
                            removeParameter();
                            handled = true;
                        }
                    }
            } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                    // Invalid character (letter) when not in edit mode - ignore
                    ofLogNotice("CellWidget") << "[DEBUG] Invalid character (letter) '" << (char)c << "' detected but not in edit mode - ignoring";
                    handled = true;  // Consume the event to prevent other handlers
                }
            }
            
            // CRITICAL: Clear InputQueueCharacters after processing to prevent double-processing
            // This ensures each character is only processed once, even if draw() is called multiple times
            io.InputQueueCharacters.clear();
        } else if (io.InputQueueCharacters.Size > 0) {
        ofLogNotice("CellWidget") << "[DEBUG] Skipping InputQueueCharacters processing - already processed this frame (currentFrame=" << currentFrame << ")";
            // Still clear it to prevent other widgets from processing it
            io.InputQueueCharacters.clear();
        }
        
        // Recalculate shouldProcessKeys after potentially processing InputQueueCharacters
        // This ensures keypad keys are not processed if InputQueueCharacters was already processed
        bool shouldProcessKeys = (!inputContext.lastProcessedFrame || currentFrame != *inputContext.lastProcessedFrame) && !inputQueueProcessed;
        
        // Check for Enter key (Enter or KeypadEnter) - only if we haven't processed InputQueueCharacters this frame
        if (shouldProcessKeys && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
            ofLogNotice("CellWidget") << "[DEBUG] Enter key pressed in draw()";
        if (inputContext.lastProcessedFrame) {
            *inputContext.lastProcessedFrame = currentFrame;  // Mark this frame as processed
        }
            bool ctrlPressed = io.KeyCtrl;
            bool shiftPressed = io.KeyShift;
            int key = convertImGuiKeyToOF(ImGuiKey_Enter);
            if (handleKeyPress(key, ctrlPressed, shiftPressed)) {
                ofLogNotice("CellWidget") << "[DEBUG] Enter key handled";
            } else {
                ofLogWarning("CellWidget") << "[DEBUG] Enter key NOT handled";
            }
        }
        
        // Check for Escape key - only when in edit mode
        // IMPORTANT: Only process ESC when actually in edit mode. When NOT in edit mode, let ESC pass through
        // to ImGui so it can use ESC to escape contained navigation contexts (like scrollable tables)
        if (shouldProcessKeys && editing_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ofLogNotice("CellWidget") << "[DEBUG] Escape key pressed in draw() (in edit mode)";
        if (inputContext.lastProcessedFrame) {
            *inputContext.lastProcessedFrame = currentFrame;  // Mark this frame as processed
        }
            if (handleKeyPress(OF_KEY_ESC, false, false)) {
                ofLogNotice("CellWidget") << "[DEBUG] Escape key handled";
            }
        }
        // NOT in edit mode: ESC will pass through to ImGui for navigation escape
        
        // Check for Backspace key
        if (shouldProcessKeys && ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
            ofLogNotice("CellWidget") << "[DEBUG] Backspace key pressed in draw()";
        if (inputContext.lastProcessedFrame) {
            *inputContext.lastProcessedFrame = currentFrame;  // Mark this frame as processed
        }
            if (handleKeyPress(OF_KEY_BACKSPACE, false, false)) {
                ofLogNotice("CellWidget") << "[DEBUG] Backspace key handled";
            }
        }
        
        // Check for Delete key
        if (shouldProcessKeys && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            ofLogNotice("CellWidget") << "[DEBUG] Delete key pressed in draw()";
        if (inputContext.lastProcessedFrame) {
            *inputContext.lastProcessedFrame = currentFrame;  // Mark this frame as processed
        }
            if (handleKeyPress(OF_KEY_DEL, false, false)) {
                ofLogNotice("CellWidget") << "[DEBUG] Delete key handled";
            }
        }
        
        // Also check for keypad keys using key codes (for numpad support)
        // CRITICAL: Only process keypad keys if we haven't already processed InputQueueCharacters this frame
        // This prevents double-processing when numpad keys appear in both InputQueueCharacters AND as keypad key codes
        if (shouldProcessKeys && (ImGui::IsKeyPressed(ImGuiKey_Keypad0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad1, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Keypad2, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad3, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Keypad4, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad5, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Keypad6, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad7, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Keypad8, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad9, false))) {
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
                ofLogNotice("CellWidget") << "[DEBUG] Keypad key '" << (char)keypadChar << "' detected (not in InputQueueCharacters), calling handleKeyPress";
                handleKeyPress(keypadChar, false, false);
            if (inputContext.lastProcessedFrame) {
                *inputContext.lastProcessedFrame = currentFrame;  // Mark this frame as processed
            }
            }
        }
        
        // Check for keypad decimal and operators - only if we haven't processed input this frame
        if (shouldProcessKeys && ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false)) {
            ofLogNotice("CellWidget") << "[DEBUG] Keypad decimal detected (not in InputQueueCharacters), calling handleKeyPress";
            handleKeyPress('.', false, false);
        if (inputContext.lastProcessedFrame) {
            *inputContext.lastProcessedFrame = currentFrame;  // Mark this frame as processed
        }
        }
        if (shouldProcessKeys && editing_) {
            if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)) {
                ofLogNotice("CellWidget") << "[DEBUG] Keypad add detected (not in InputQueueCharacters), calling handleKeyPress";
                handleKeyPress('+', false, false);
            if (inputContext.lastProcessedFrame) {
                *inputContext.lastProcessedFrame = currentFrame;  // Mark this frame as processed
            }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) {
                ofLogNotice("CellWidget") << "[DEBUG] Keypad subtract detected (not in InputQueueCharacters), calling handleKeyPress";
                handleKeyPress('-', false, false);
            if (inputContext.lastProcessedFrame) {
                *inputContext.lastProcessedFrame = currentFrame;  // Mark this frame as processed
            }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_KeypadMultiply, false)) {
                ofLogNotice("CellWidget") << "[DEBUG] Keypad multiply detected (not in InputQueueCharacters), calling handleKeyPress";
                handleKeyPress('*', false, false);
            if (inputContext.lastProcessedFrame) {
                *inputContext.lastProcessedFrame = currentFrame;  // Mark this frame as processed
            }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_KeypadDivide, false)) {
                ofLogNotice("CellWidget") << "[DEBUG] Keypad divide detected (not in InputQueueCharacters), calling handleKeyPress";
                handleKeyPress('/', false, false);
            if (inputContext.lastProcessedFrame) {
                *inputContext.lastProcessedFrame = currentFrame;  // Mark this frame as processed
            }
            }
        }
        
        // Check for arrow keys in edit mode (adjust values)
        // Use IsKeyDown with repeat support for held keys (quick edits)
        if (editing_) {
            bool shiftPressed = io.KeyShift;
            // Use IsKeyDown for repeat support when key is held
            // This allows quick value adjustments by holding arrow keys
            if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
                handleKeyPress(OF_KEY_UP, false, shiftPressed);
            }
            if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
                handleKeyPress(OF_KEY_DOWN, false, shiftPressed);
            }
            if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
                handleKeyPress(OF_KEY_LEFT, false, shiftPressed);
            }
            if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
                handleKeyPress(OF_KEY_RIGHT, false, shiftPressed);
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
    
    // Disable keyboard navigation during drag
    disableImGuiKeyboardNav();
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
    
    // Re-enable keyboard navigation when drag ends
    enableImGuiKeyboardNav();
}

void CellWidget::applyDragValue(float newValue) {
    if (!onValueApplied) return;
    
    // Clamp value to range
    float clampedValue = std::max(minVal, std::min(maxVal, newValue));
    
    // Apply via callback
    onValueApplied(parameterName, clampedValue);
}



