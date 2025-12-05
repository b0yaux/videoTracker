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
    : editing_(false), bufferState_(EditBufferState::None),
      originalValue_(NAN), shouldRefocus_(false), dragging_(false), dragStartY_(0.0f), dragStartX_(0.0f), lastDragValue_(0.0f) {
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

void CellWidget::clearCell() {
    // Exit edit mode if active
    if (editing_) {
        exitEditMode();
    }
    
    // Set refocus flag to maintain focus after clearing
    shouldRefocus_ = true;
    
    // Remove parameter to restore natural empty state
    // In tracker context, empty state (not in parameterValues map) IS the default
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
        bufferState_ = EditBufferState::Restored;
    } else {
        bufferState_ = EditBufferState::None;
    }
}

void CellWidget::setEditBuffer(const std::string& buffer, bool initialized) {
    editBuffer_ = buffer;
    if (!editBuffer_.empty()) {
        // If setting a non-empty buffer, ensure we're in edit mode
        if (!editing_) {
            editing_ = true;
            // Don't call enterEditMode() here as it would re-initialize the buffer
            // Just set editing flag - navigation remains enabled for gamepad support
        }
        // Set buffer state based on initialization flag
        // If initialized=true, buffer was just set from current value
        // If initialized=false, buffer is being restored from cache (user had typed something)
        bufferState_ = initialized ? EditBufferState::Initialized : EditBufferState::Restored;
    } else {
        bufferState_ = EditBufferState::None;
    }
}

void CellWidget::enterEditMode() {
    bool wasEditing = editing_;
    editing_ = true;
    
    // Store original value for fallback
    if (!wasEditing && getCurrentValue) {
        originalValue_ = getCurrentValue();
        // Keep NaN as-is to preserve empty cell state
    }
    
    initializeEditBuffer();
    bufferState_ = EditBufferState::Initialized;  // Buffer was initialized from current value
    
    // Disable ImGui navigation when entering edit mode
    if (!wasEditing) {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        ofLogNotice("CellWidget") << "[ENTER_EDIT] Disabled navigation for edit mode";
    }
    
    // Notify GUI layer of edit mode change
    if (!wasEditing && onEditModeChanged) {
        onEditModeChanged(true);
    }
}

void CellWidget::exitEditMode() {
    bool wasEditing = editing_;
    if (!wasEditing) {
        // Not in edit mode - nothing to do
        return;
    }
    
    editing_ = false;
    editBuffer_.clear();
    bufferState_ = EditBufferState::None;  // Reset buffer state when exiting edit mode
    originalValue_ = NAN;  // Clear original value
    
    // Re-enable ImGui navigation when exiting edit mode
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ofLogNotice("CellWidget") << "[EXIT_EDIT] Re-enabled navigation after edit mode";
    
    // Notify GUI layer of edit mode change
    if (onEditModeChanged) {
        ofLogNotice("CellWidget") << "[EXIT_EDIT] Calling onEditModeChanged(false)";
        onEditModeChanged(false);
    }
    
    // Note: Immediate refocus is handled in drawSliderMode() when cell is still focused
    // This ensures refocus happens in the same frame without delay
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
            shouldRefocus_ = true;  // Flag to maintain focus after exit
            applyValue();
            exitEditMode();
            // Refocus will be handled in draw() based on shouldRefocus_ flag
            return true;
        } else {
            // Enter edit mode if cell is focused (checked by caller via processInputInDraw)
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
            bufferState_ = EditBufferState::UserModified;  // User modified the buffer
            
            // Apply buffer with fallback after backspace
            applyBufferWithFallback();
            return true;
        }
        return false;
    }
    
    // Delete key: Clear edit buffer
    if (key == OF_KEY_DEL) {
        if (editing_) {
            editBuffer_.clear();
            bufferState_ = EditBufferState::UserModified;  // User modified the buffer (cleared it)
            return true;
        }
        return false;
    }
    
    // Character input (numeric, operators, decimal, minus) - use unified handler
    if ((key >= '0' && key <= '9') || key == '+' || key == '*' || key == '/' || key == '.' || key == '-') {
        return handleCharacterInput((char)key);
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

bool CellWidget::handleCharacterInput(char c) {
    // Unified character input handler for direct typing
    // Handles: numeric (0-9), operators (+, *, /), decimal (.), minus (-)
    
    // Special validation for integer columns
    if (c == '.' && isInteger) {
        // Ignore decimal point for integer columns
        return true;  // Consume the event but don't add decimal point
    }
    
    // Enter edit mode if not already editing
    bool justEnteredEditMode = false;
    if (!editing_) {
        // Auto-enter edit mode
        // CRITICAL: If buffer is already set (restored from cache), don't call enterEditMode()
        // as it would overwrite the restored buffer. Instead, just set editing_ and preserve the buffer.
        if (editBuffer_.empty() || bufferState_ == EditBufferState::None || bufferState_ == EditBufferState::Initialized) {
            // Buffer is empty or just initialized - safe to call enterEditMode()
            enterEditMode();
            justEnteredEditMode = true;
        } else {
            // Buffer is already set (restored from cache) - just enable edit mode without reinitializing
            editing_ = true;
            // Still need to disable navigation and notify callbacks for proper edit mode
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
            if (onEditModeChanged) {
                onEditModeChanged(true);
            }
        }
    }
    
    // Determine if we should clear the buffer before appending
    // Clear if:
    // 1. We just entered edit mode and buffer was initialized (not restored) - typing should REPLACE the value
    // 2. Buffer is empty or placeholder ("--")
    // 3. Buffer was just initialized (user hasn't typed yet) - typing should REPLACE the value
    // Don't clear if buffer was restored from cache (user had typed something) - typing should APPEND
    bool shouldClear = false;
    if (justEnteredEditMode) {
        // We just entered edit mode - clear buffer so typing REPLACES the initialized value
        // This gives Blender-style behavior: typing immediately replaces the displayed value
        shouldClear = true;
    } else if (isEmpty(editBuffer_)) {
        // Buffer is placeholder ("--") - clear it
        shouldClear = true;
    } else if (bufferState_ == EditBufferState::Initialized) {
        // Buffer was initialized but user hasn't typed yet - clear so typing replaces it
        shouldClear = true;
    }
    // If bufferState_ == Restored, don't clear - user had typed something, so append
    
    // Special handling for operators: don't clear if buffer has content (allow "5*2")
    if ((c == '+' || c == '*' || c == '/') && !editBuffer_.empty() && !isEmpty(editBuffer_) && bufferState_ != EditBufferState::Initialized) {
        shouldClear = false;
    }
    
    if (shouldClear) {
        editBuffer_.clear();
    }
    
    // Special validation for decimal point: only allow one per number
    if (c == '.') {
        // Find the last number in the buffer (after last operator)
        size_t lastOp = editBuffer_.find_last_of("+-*/");
        std::string lastNumber = (lastOp == std::string::npos) ? editBuffer_ : editBuffer_.substr(lastOp + 1);
        if (lastNumber.find('.') != std::string::npos) {
            return true;  // This number already has a decimal point
        }
    }
    
    // Append character to buffer
    editBuffer_ += c;
    bufferState_ = EditBufferState::UserModified;  // Mark that user has modified the buffer
    if (editBuffer_.length() > MAX_EDIT_BUFFER_LENGTH) {
        editBuffer_ = editBuffer_.substr(editBuffer_.length() - MAX_EDIT_BUFFER_LENGTH);
    }
    
    // Apply value immediately (Blender-style reactive editing)
    if (!editBuffer_.empty()) {
        // Check for special cases
        if (editBuffer_ == "--") {
            // User typed '--' explicitly - remove parameter (set to "none")
            removeParameter();
        } else if (isEmpty(editBuffer_)) {
            // Only dashes (e.g., "-") - remove parameter (set to "none")
            removeParameter();
        } else if (editBuffer_ == ".") {
            // Single decimal point - remove parameter (set to "none")
            removeParameter();
        } else {
            // Check if buffer contains only operators/dashes (for operators)
            if (c == '+' || c == '*' || c == '/') {
                bool onlyOpsOrDashes = true;
                for (char ch : editBuffer_) {
                    if (ch != '-' && ch != '+' && ch != '*' && ch != '/') {
                        onlyOpsOrDashes = false;
                        break;
                    }
                }
                if (onlyOpsOrDashes) {
                    // Only operators/dashes - remove parameter (set to "none")
                    removeParameter();
                    return true;
                }
            }
            
            // Apply buffer with fallback to original value
            // This provides immediate visual feedback while maintaining stability
            applyBufferWithFallback();
        }
    }
    
    return true;
}

void CellWidget::appendDigit(char digit) {
    if (!editing_) {
        enterEditMode();
    }
    editBuffer_ += digit;
    bufferState_ = EditBufferState::UserModified;  // User modified the buffer
    if (editBuffer_.length() > MAX_EDIT_BUFFER_LENGTH) {
        editBuffer_ = editBuffer_.substr(editBuffer_.length() - MAX_EDIT_BUFFER_LENGTH);
    }
}

void CellWidget::appendChar(char c) {
    if (!editing_) {
        enterEditMode();
    }
    editBuffer_ += c;
    bufferState_ = EditBufferState::UserModified;  // User modified the buffer
    if (editBuffer_.length() > MAX_EDIT_BUFFER_LENGTH) {
        editBuffer_ = editBuffer_.substr(editBuffer_.length() - MAX_EDIT_BUFFER_LENGTH);
    }
}

void CellWidget::backspace() {
    if (editing_ && !editBuffer_.empty()) {
        editBuffer_.pop_back();
        bufferState_ = EditBufferState::UserModified;  // User modified the buffer
    }
}

void CellWidget::deleteChar() {
    if (editing_) {
        editBuffer_.clear();
        bufferState_ = EditBufferState::UserModified;  // User modified the buffer
    }
}

void CellWidget::applyValue() {
    ofLogNotice("CellWidget") << "[ENTER_KEY] applyValue() called - calling parseAndApplyEditBuffer()";
    bool result = parseAndApplyEditBuffer();
    ofLogNotice("CellWidget") << "[ENTER_KEY] parseAndApplyEditBuffer() returned: " << (result ? "true" : "false");
}

void CellWidget::cancelEdit() {
    // Restore original value before exiting
    if (onValueApplied) {
        if (std::isnan(originalValue_)) {
            // Original was empty (NaN) - remove parameter to restore empty state
            removeParameter();
        } else {
            // Original had a value - restore it
            onValueApplied(parameterName, originalValue_);
        }
    }
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
    bufferState_ = EditBufferState::UserModified;  // Value was adjusted by user
    
    // Apply with buffer system for consistency
    applyBufferWithFallback();
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

void CellWidget::applyEditValueFloat(float floatValue, bool updateBuffer) {
    // For integer parameters, round and clamp to integer range
    if (isInteger) {
        int intValue = (int)std::round(floatValue);
        // Clamp to valid range
        int clampedValue = std::max((int)minVal, std::min((int)maxVal, intValue));
        
        // Only update buffer if:
        // 1. Explicitly requested (updateBuffer=true, e.g., on Enter key confirmation)
        // 2. Value wasn't clamped (user typed valid value that doesn't need clamping)
        bool shouldUpdateBuffer = updateBuffer || (intValue == clampedValue);
        applyEditValueInt(clampedValue, shouldUpdateBuffer);
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
            // Update buffer only if explicitly requested (final confirmation)
            if (updateBuffer) {
                if (formatValue) {
                    editBuffer_ = formatValue(clampedValue);
                } else {
                    editBuffer_ = getDefaultFormatValue(clampedValue);
                }
            }
        }
    } else {
        // Value is within range - apply it
        if (onValueApplied) {
            onValueApplied(parameterName, floatValue);
        }
        // Update buffer only if explicitly requested (final confirmation)
        if (updateBuffer) {
            if (formatValue) {
                editBuffer_ = formatValue(floatValue);
            } else {
                editBuffer_ = getDefaultFormatValue(floatValue);
            }
        }
    }
}

void CellWidget::applyEditValueInt(int intValue, bool updateBuffer) {
    // Apply integer value (callbacks handle formatting)
    if (onValueApplied) {
        onValueApplied(parameterName, (float)intValue);
    }
    // Only update edit buffer if explicitly requested
    // This prevents overwriting the buffer during reactive editing when values are clamped
    // (e.g., typing '18' with min=4: '1' gets clamped to 4, but buffer should stay '1' so '8' can be appended)
    if (updateBuffer) {
        if (formatValue) {
            editBuffer_ = formatValue((float)intValue);
        } else {
            // Fallback: format as integer
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", intValue);
            editBuffer_ = buf;
        }
    }
}

bool CellWidget::parseAndApplyEditBuffer() {
    // Debug logging
    ofLogNotice("CellWidget") << "[ENTER_KEY] parseAndApplyEditBuffer called with buffer: '" << editBuffer_ << "'";
    
    // Trim whitespace for comparison
    std::string trimmed = trimWhitespace(editBuffer_);
    
    // Handle empty buffer or invalid input
    // For removable parameters, empty buffer removes the parameter
    // For non-removable parameters, empty buffer is invalid
    if (trimmed.empty() || isEmpty(trimmed)) {
        if (isRemovable) {
            // Empty buffer or only dashes - remove parameter
            ofLogNotice("CellWidget") << "[ENTER_KEY] Empty buffer - removing parameter";
            removeParameter();
            return true;
        } else {
            // Invalid value for non-removable column
            ofLogNotice("CellWidget") << "[ENTER_KEY] Empty buffer invalid for non-removable column";
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
        // updateBuffer=true: This is final confirmation (Enter key), so update buffer with final value
        ofLogNotice("CellWidget") << "[ENTER_KEY] Parsed value: " << floatValue << " - applying with updateBuffer=true";
        applyEditValueFloat(floatValue, true);
        ofLogNotice("CellWidget") << "[ENTER_KEY] Successfully applied and returning true";
        return true;
    } catch (...) {
        // Parse failed - for removable parameters, remove it (set to "none")
        ofLogNotice("CellWidget") << "[ENTER_KEY] Parse failed with exception";
        if (isRemovable) {
            removeParameter();
            return true;
        }
        // Invalid value for non-removable column
        return false;
    }
}

void CellWidget::applyBufferWithFallback() {
    // Apply buffer value immediately if valid, fallback to original if invalid
    // This provides real-time feedback while maintaining a safety net
    
    if (editBuffer_.empty() || isEmpty(editBuffer_)) {
        // Empty buffer - fallback to original value
        if (!std::isnan(originalValue_) && onValueApplied) {
            onValueApplied(parameterName, originalValue_);
        }
        return;
    }
    
    try {
        // Try to parse current buffer
        float bufferValue;
        if (parseValue) {
            bufferValue = parseValue(editBuffer_);
        } else {
            bufferValue = ExpressionParser::evaluate(editBuffer_);
        }
        
        // Check if value is in valid range
        if (bufferValue >= minVal && bufferValue <= maxVal) {
            // Valid buffer value - apply it immediately
            if (onValueApplied) {
                onValueApplied(parameterName, bufferValue);
            }
        } else {
            // Out of range - fallback to original
            if (!std::isnan(originalValue_) && onValueApplied) {
                onValueApplied(parameterName, originalValue_);
            }
        }
    } catch (...) {
        // Invalid buffer - fallback to original
        if (!std::isnan(originalValue_) && onValueApplied) {
            onValueApplied(parameterName, originalValue_);
        }
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
                                            const CellWidgetInputContext& inputContext) {
    ImGui::PushID(uniqueId);
    
    // Get cell rect (before drawing)
    ImVec2 cellMin = ImGui::GetCursorScreenPos();
    float cellHeight = ImGui::GetFrameHeight();
    float cellWidth = ImGui::GetColumnWidth();
    ImVec2 cellMax = ImVec2(cellMin.x + cellWidth, cellMin.y + cellHeight);
    
    // Draw slider mode (only mode supported)
    CellWidgetInteraction result = drawSliderMode(uniqueId, isFocused, shouldFocusFirst, inputContext, cellMin, cellMax);
    ImGui::PopID();
    return result;
}

CellWidgetInteraction CellWidget::drawSliderMode(int uniqueId, bool isFocused, bool shouldFocusFirst, const CellWidgetInputContext& inputContext, const ImVec2& cellMin, const ImVec2& cellMax) {
    CellWidgetInteraction result;
    
    // SLIDER mode (original implementation)
    // Get current value for display
    // Note: We keep NaN as-is for formatDisplayText (which will show "--")
    // but use a default value for fill bar calculations
    float currentVal = getCurrentValue ? getCurrentValue() : defaultValue;
    float displayVal = currentVal; // Keep NaN for display formatting
    
    // Get display text (formatDisplayText handles NaN and shows "--")
    std::string displayText;
    if (editing_) {
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
    
    // Navigation is managed by ModuleGUI::onEditModeChanged callback
    // No need to disable it here - the callback handles it when entering edit mode
    
    // Apply edit mode styling: dark grey/black background (Blender-style)
    if (editing_) {
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
    
    // Set focus on first cell if requested OR if refocus is needed after edit exit
    if (shouldFocusFirst || shouldRefocus_) {
        ImGui::SetKeyboardFocusHere(0);
        shouldRefocus_ = false;  // Clear the flag after using it
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
    
    // PHASE 2: TRUST IMGUI FOCUS - Use ImGui's native focus system directly
    // Remove complex parallel focus tracking that caused race conditions
    bool actuallyFocused = ImGui::IsItemFocused();
    bool isItemActive = ImGui::IsItemActive();
    
    // PHASE 2: TRUST IMGUI FOCUS - Remove defensive focus checking that causes race conditions
    // Let ImGui handle focus changes naturally - edit mode will exit naturally when focus is lost
    // The original defensive check was causing immediate exit due to focus detection timing issues
    
    // Handle activation (mouse click OR gamepad activation)
    // NOTE: Mouse clicks should only focus the cell, not enter edit mode
    // Enter key and typing will enter edit mode via processInputInDraw()
    // CRITICAL: Ignore button activation if Enter is pressed - Enter should only enter/edit mode, not trigger button click
    if (isActivated && !editing_ && !spacebarPressed && !enterPressed) {
        // Mouse click or gamepad "A" button - just signal click, don't enter edit mode
        // GUI layer will handle focus, Enter key or typing will enter edit mode
        result.clicked = true;
        result.focusChanged = true;
    }
    
    // Process keyboard input for focused cells
    // Simplified: only process input when cell is actually focused by ImGui
    if (actuallyFocused || editing_) {
        processInputInDraw(actuallyFocused);
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
        // IsItemActive() is sufficient to indicate the cell was clicked
        result.focusChanged = true;
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
    
    // Simplified: no special focus locking needed - trust ImGui
    
    // Early exit after syncing (but before drawing outline)
    if (result.shouldExitEarly) {
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor(3);
        ImGui::PopID();
        return result;
    }
    
    // Handle click
    // CRITICAL: Ignore button clicks when Enter is pressed - Enter should only enter/edit mode, not trigger button click
    if (buttonClicked && !ImGui::IsMouseDragging(0) && !spacebarPressed && !enterPressed && ImGui::IsItemClicked(0)) {
        result.clicked = true;
        result.focusChanged = true;
        // DON'T enter edit mode on click - just focus the cell
        // User can type numbers directly (auto-enters edit mode) or hit Enter to enter edit mode
        if (editing_) {
            exitEditMode();
        }
    }
    
    // Handle double-click: clear the cell to empty state
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        clearCell();
    }
    
    // Simplified drag focus: let ImGui handle focus naturally during drag
    
    // Pop style var and colors
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(3);
    
    // Draw outline for selected/editing cells
    // Show outline if focused, dragging, or active (mouse held)
    // Use ImGui state directly - no parallel selection state needed
    bool shouldShowOutline = actuallyFocused || dragging_ || isItemActive || editing_;
    if (shouldShowOutline) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (drawList) {
            ImVec2 outlineMin = ImVec2(cellMin.x - 1, cellMin.y - 1);
            ImVec2 outlineMax = ImVec2(cellMax.x + 1, cellMax.y + 1);
            // Orange outline when in edit mode, red outline when just selected or dragging
            ImU32 outlineColor = editing_
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
    // Process keyboard input directly in draw() when cell is focused
    // This makes CellWidget self-contained and reusable across all modules
    
    // CRITICAL: Allow input processing if:
    // 1. Cell is actually focused, OR
    // 2. Cell is already in edit mode (handles focus detection edge cases)
    // This ensures typing works even when focus detection has timing issues
    if (!actuallyFocused && !editing_) {
        // Not focused and not editing - don't process input
        return;
    }
    
    ImGuiIO& io = ImGui::GetIO();
    
    // DEFENSIVE: If we're in edit mode but navigation is enabled, disable it immediately
    // This prevents navigation from interfering with edit mode (e.g., arrow keys navigating instead of editing)
    if (editing_ && (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard)) {
        ofLogNotice("CellWidget") << "[NAV_DEFENSE] Navigation enabled while in edit mode - disabling (this should rarely happen now)";
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    }
    
    // Check if ImGui navigation is active (gamepad/keyboard nav)
    bool navActive = (io.NavActive && (io.ConfigFlags & (ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad)));
    
    // CRITICAL: Check for Enter key BEFORE navigation check
    // Enter should enter/edit mode even when navigation is active
    bool enterPressed = (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    if (enterPressed && (actuallyFocused || editing_)) {
        // Allow Enter key when focused OR when already in edit mode (fixes buffer validation issue)
        bool ctrlPressed = io.KeyCtrl;
        bool shiftPressed = io.KeyShift;
        if (this->handleKeyPress(OF_KEY_RETURN, ctrlPressed, shiftPressed)) {
            return;  // Handled
        }
    }
    
    // CRITICAL: Process InputQueueCharacters FIRST (typed characters)
    // This MUST work even when navigation is active - typing should always work
    // Direct typing should auto-enter edit mode, so we process it before any navigation checks
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
    
    // CRITICAL: Don't block input processing when navigation is active
    // Typing should work regardless of navigation state
    // Only skip navigation-related keys (arrow keys) when not editing
    // Navigation can coexist with typing - they don't conflict
    
    // Process special keys (only if not already processed via InputQueueCharacters)
        if (!inputQueueProcessed) {
            // Escape key - only when in edit mode
            if (editing_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                this->handleKeyPress(OF_KEY_ESC, false, false);
            }
            
            // Backspace key - clear to empty when not editing, backspace in buffer when editing
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
                if (!editing_) {
                    // Not in edit mode - clear cell to empty state
                    clearCell();
                } else {
                    // In edit mode - handle backspace in buffer
                    backspace();
                    applyBufferWithFallback();
                }
            }
            
            // Delete key - clear to empty when not editing, clear buffer when editing  
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                if (!editing_) {
                    // Not in edit mode - clear cell to empty state
                    clearCell();
                } else {
                    // In edit mode - clear entire buffer
                    deleteChar();
                    applyBufferWithFallback();
                }
            }
            
            // Keypad keys (for numpad support) - use unified character input handler
            if (ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) this->handleCharacterInput('0');
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad1, false)) this->handleCharacterInput('1');
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad2, false)) this->handleCharacterInput('2');
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad3, false)) this->handleCharacterInput('3');
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad4, false)) this->handleCharacterInput('4');
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad5, false)) this->handleCharacterInput('5');
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad6, false)) this->handleCharacterInput('6');
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad7, false)) this->handleCharacterInput('7');
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad8, false)) this->handleCharacterInput('8');
            else if (ImGui::IsKeyPressed(ImGuiKey_Keypad9, false)) this->handleCharacterInput('9');
            else if (ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false)) this->handleCharacterInput('.');
            
            if (editing_) {
                if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)) this->handleCharacterInput('+');
                if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) this->handleCharacterInput('-');
                if (ImGui::IsKeyPressed(ImGuiKey_KeypadMultiply, false)) this->handleCharacterInput('*');
                if (ImGui::IsKeyPressed(ImGuiKey_KeypadDivide, false)) this->handleCharacterInput('/');
            }
            
            // Arrow keys in edit mode (adjust values)
            // CRITICAL: Arrow keys should work when editing, regardless of navigation state
            // They adjust values, not navigate between cells
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
    
    // CRITICAL: After processing all input, if we processed typed characters and entered edit mode,
    // ensure navigation doesn't interfere with editing
    // Navigation is automatically disabled when typing starts (handled in handleCharacterInput)
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



