#include "Pattern.h"
#include "ofLog.h"
#include "ofUtils.h"
#include <algorithm>  // For std::min, std::max

// Step implementation
//--------------------------------------------------------------
void Step::clear() {
    index = -1;
    length = 1;
    note = -1;      // Reset to not set
    chance = 100;   // Reset to default (always trigger)
    ratioA = 1;     // Reset to default (always trigger)
    ratioB = 1;     // Reset to default (always trigger)
    parameterValues.clear();
    
    // Don't set default parameters here - defaults come from MediaPool/MediaPlayer
    // Empty parameterValues means "use defaults/position memory" when triggering
}

float Step::getParameterValue(const std::string& paramName, float defaultValue) const {
    // Handle tracker-specific parameters stored as direct fields
    if (paramName == "note") {
        return (note >= 0) ? (float)note : defaultValue;
    }
    if (paramName == "chance") {
        return (float)chance;
    }
    if (paramName == "ratio") {
        // Encode ratio as A * 1000 + B (e.g., 2:4 = 2004)
        return (float)(ratioA * 1000 + ratioB);
    }
    
    // Handle external parameters stored in map
    auto it = parameterValues.find(paramName);
    if (it != parameterValues.end()) {
        return it->second;
    }
    return defaultValue;
}

void Step::setParameterValue(const std::string& paramName, float value) {
    // Handle tracker-specific parameters stored as direct fields
    if (paramName == "note") {
        note = (int)std::round(value);
        // Also remove from map if it exists (for migration)
        parameterValues.erase("note");
        return;
    }
    if (paramName == "chance") {
        chance = (int)std::round(std::max(0.0f, std::min(100.0f, value)));
        // Also remove from map if it exists (for migration)
        parameterValues.erase("chance");
        return;
    }
    if (paramName == "ratio") {
        // Decode ratio from encoded value (A * 1000 + B)
        int encoded = (int)std::round(value);
        ratioA = std::max(1, std::min(16, encoded / 1000));
        ratioB = std::max(1, std::min(16, encoded % 1000));
        // Also remove from map if it exists (for migration)
        parameterValues.erase("ratio");
        return;
    }
    
    // Handle external parameters stored in map
    parameterValues[paramName] = value;
}

bool Step::hasParameter(const std::string& paramName) const {
    // Handle tracker-specific parameters stored as direct fields
    if (paramName == "note") {
        return note >= 0;  // Note is set if >= 0
    }
    if (paramName == "chance") {
        return true;  // Chance is always present (defaults to 100)
    }
    if (paramName == "ratio") {
        return true;  // Ratio is always present (defaults to 1:1)
    }
    
    // Handle external parameters stored in map
    return parameterValues.find(paramName) != parameterValues.end();
}

void Step::removeParameter(const std::string& paramName) {
    // Handle tracker-specific parameters stored as direct fields
    if (paramName == "note") {
        note = -1;  // Reset to not set
        // Also remove from map if it exists (for migration)
        parameterValues.erase("note");
        return;
    }
    if (paramName == "chance") {
        chance = 100;  // Reset to default
        // Also remove from map if it exists (for migration)
        parameterValues.erase("chance");
        return;
    }
    if (paramName == "ratio") {
        ratioA = 1;  // Reset to default
        ratioB = 1;  // Reset to default
        // Also remove from map if it exists (for migration)
        parameterValues.erase("ratio");
        return;
    }
    
    // Handle external parameters stored in map
    parameterValues.erase(paramName);
}

bool Step::operator==(const Step& other) const {
    // Compare direct fields first
    if (index != other.index || length != other.length || 
        note != other.note || chance != other.chance ||
        ratioA != other.ratioA || ratioB != other.ratioB) {
        return false;
    }
    // Compare parameter values map
    if (parameterValues.size() != other.parameterValues.size()) {
        return false;
    }
    for (const auto& pair : parameterValues) {
        auto it = other.parameterValues.find(pair.first);
        if (it == other.parameterValues.end() || it->second != pair.second) {
            return false;
        }
    }
    return true;
}

bool Step::operator!=(const Step& other) const {
    return !(*this == other);
}

std::string Step::toString() const {
    if (isEmpty()) {
        return "---";
    }
    
    std::string result = "[" + ofToString(index) + "]";
    result += " len:" + ofToString(length);
    
    // Add parameter values (3 decimal places for unified precision)
    for (const auto& pair : parameterValues) {
        result += " " + pair.first + ":" + ofToString(pair.second, 3);
    }
    
    return result;
}

// Pattern implementation
//--------------------------------------------------------------
Pattern::Pattern(int stepCount) {
    setStepCount(stepCount);
    initializeDefaultColumns();
    stepsPerBeat = 4.0f;  // Default steps per beat
}

Step& Pattern::getStep(int stepIndex) {
    if (!isValidStep(stepIndex)) {
        static Step emptyStep;
        ofLogWarning("Pattern") << "Invalid step index: " << stepIndex;
        return emptyStep;
    }
    return steps[stepIndex];
}

const Step& Pattern::getStep(int stepIndex) const {
    if (!isValidStep(stepIndex)) {
        static Step emptyStep;
        ofLogWarning("Pattern") << "Invalid step index: " << stepIndex;
        return emptyStep;
    }
    return steps[stepIndex];
}

void Pattern::setStep(int stepIndex, const Step& step) {
    if (!isValidStep(stepIndex)) {
        ofLogWarning("Pattern") << "Invalid step index: " << stepIndex;
        return;
    }
    steps[stepIndex] = step;
}

void Pattern::clearStep(int stepIndex) {
    if (!isValidStep(stepIndex)) {
        return;
    }
    steps[stepIndex].clear();
}

void Pattern::clear() {
    for (auto& step : steps) {
        step.clear();
    }
}

bool Pattern::isEmpty() const {
    for (const auto& step : steps) {
        if (!step.isEmpty()) {
            return false;
        }
    }
    return true;
}

bool Pattern::duplicateRange(int fromStep, int toStep, int destinationStep) {
    // Validate input range
    if (fromStep < 0 || toStep < 0 || destinationStep < 0) {
        ofLogError("Pattern") << "Invalid step index (negative): from=" << fromStep 
                              << ", to=" << toStep << ", dest=" << destinationStep;
        return false;
    }
    
    if (fromStep > toStep) {
        ofLogError("Pattern") << "Invalid range: fromStep (" << fromStep 
                              << ") > toStep (" << toStep << ")";
        return false;
    }
    
    int rangeSize = toStep - fromStep + 1;
    
    // Validate that source range is within bounds
    if (toStep >= (int)steps.size()) {
        ofLogError("Pattern") << "Source range exceeds pattern size: toStep=" << toStep 
                              << ", pattern size=" << steps.size();
        return false;
    }
    
    // Validate that destination range is within bounds
    if (destinationStep + rangeSize - 1 >= (int)steps.size()) {
        ofLogError("Pattern") << "Destination range exceeds pattern size: dest=" << destinationStep 
                              << ", range size=" << rangeSize << ", pattern size=" << steps.size();
        return false;
    }
    
    // Check for overlap (if source and destination overlap, we need to copy to temp first)
    bool hasOverlap = (destinationStep >= fromStep && destinationStep <= toStep) ||
                      (destinationStep + rangeSize - 1 >= fromStep && destinationStep + rangeSize - 1 <= toStep);
    
    if (hasOverlap) {
        // Copy to temporary buffer first to handle overlapping ranges
        std::vector<Step> tempBuffer;
        tempBuffer.reserve(rangeSize);
        for (int i = fromStep; i <= toStep; i++) {
            tempBuffer.push_back(steps[i]);
        }
        
        // Now copy from temp buffer to destination
        for (size_t i = 0; i < tempBuffer.size(); i++) {
            steps[destinationStep + i] = tempBuffer[i];
        }
    } else {
        // No overlap, direct copy
        for (int i = 0; i < rangeSize; i++) {
            steps[destinationStep + i] = steps[fromStep + i];
        }
    }
    
    ofLogNotice("Pattern") << "Duplicated steps " << fromStep << "-" << toStep 
                           << " to position " << destinationStep;
    return true;
}

void Pattern::setStepCount(int stepCount) {
    if (stepCount <= 0) {
        ofLogWarning("Pattern") << "Invalid number of steps: " << stepCount;
        return;
    }
    
    size_t oldSize = steps.size();
    
    if (stepCount < (int)oldSize) {
        // Reducing step count: save overflow steps
        // Get the steps that will be cut off in this reduction
        std::vector<Step> newOverflow(steps.begin() + stepCount, steps.end());
        
        // Merge with existing overflow: new overflow (from lower indices in original pattern) goes at the beginning
        // This preserves the original pattern order: [stepCount, stepCount+1, ..., oldSize-1]
        overflowSteps.insert(overflowSteps.begin(), newOverflow.begin(), newOverflow.end());
        
        steps.resize(stepCount);
        ofLogNotice("Pattern") << "Reduced pattern from " << oldSize << " to " << stepCount 
                               << " steps (saved " << newOverflow.size() << " new overflow steps, total: " << overflowSteps.size() << ")";
    } else if (stepCount > (int)oldSize) {
        // Expanding step count: restore overflow steps if available
        steps.resize(stepCount);
        
        // Restore overflow steps first (if any)
        size_t overflowToRestore = std::min(overflowSteps.size(), 
                                            (size_t)(stepCount - oldSize));
        if (overflowToRestore > 0) {
            for (size_t i = 0; i < overflowToRestore; i++) {
                steps[oldSize + i] = overflowSteps[i];
            }
            // Remove restored steps from overflow buffer
            overflowSteps.erase(overflowSteps.begin(), 
                               overflowSteps.begin() + overflowToRestore);
            ofLogNotice("Pattern") << "Expanded pattern from " << oldSize << " to " << stepCount 
                                   << " steps (restored " << overflowToRestore << " overflow steps)";
        }
        
        // Initialize any remaining new steps as empty
        for (size_t i = oldSize + overflowToRestore; i < steps.size(); i++) {
            steps[i] = Step();
        }
    }
    // If stepCount == oldSize, do nothing
}

void Pattern::doubleSteps() {
    int currentSize = (int)steps.size();
    if (currentSize <= 0) {
        ofLogWarning("Pattern") << "Cannot double steps: pattern is empty";
        return;
    }
    
    // Resize to double the current size
    steps.resize(currentSize * 2);
    
    // Duplicate existing steps
    for (int i = 0; i < currentSize; i++) {
        steps[currentSize + i] = steps[i];
    }
    
    ofLogNotice("Pattern") << "Doubled pattern steps from " << currentSize << " to " << (currentSize * 2);
}

ofJson Pattern::toJson() const {
    ofJson json;
    json["steps"] = steps;
    json["columnConfig"] = columnConfig;
    json["stepsPerBeat"] = stepsPerBeat;
    return json;
}

void Pattern::fromJson(const ofJson& json) {
    if (json.contains("steps") && json["steps"].is_array()) {
        steps = json["steps"].get<std::vector<Step>>();
    } else if (json.contains("cells") && json["cells"].is_array()) {
        // Transitional support for 'cells' key if we just renamed it
        steps = json["cells"].get<std::vector<Step>>();
    }
    
    if (json.contains("columnConfig") && json["columnConfig"].is_array()) {
        columnConfig = json["columnConfig"].get<std::vector<ColumnConfig>>();
    }
    
    stepsPerBeat = json.value("stepsPerBeat", 4.0f);
}

    // Steps per beat methods
    //--------------------------------------------------------------
    void Pattern::setStepsPerBeat(float steps) {
        // Support fractional values (1/2, 1/4, 1/8) and negative for backward reading
        // Clamp to reasonable range: -96 to 96, excluding 0
        if (steps == 0.0f) {
            steps = 4.0f;  // Default fallback if 0
        }
        stepsPerBeat = std::max(-96.0f, std::min(96.0f, steps));
    }
    
    // Column configuration methods
    //--------------------------------------------------------------
    void Pattern::initializeDefaultColumns() {
    columnConfig.clear();
    // TRIGGER COLUMNS (Required - what to play)
    columnConfig.push_back(ColumnConfig("index", ColumnCategory::TRIGGER, true, 0));
    columnConfig.push_back(ColumnConfig("length", ColumnCategory::TRIGGER, true, 1));
    // PARAMETER COLUMNS (Optional - how to play)
    columnConfig.push_back(ColumnConfig("position", ColumnCategory::PARAMETER, false, 2));
    columnConfig.push_back(ColumnConfig("speed", ColumnCategory::PARAMETER, false, 3));
    columnConfig.push_back(ColumnConfig("volume", ColumnCategory::PARAMETER, false, 4));
    // Note: chance and ratio are CONDITION category, but not added by default (user can add them via context menu)
}

void Pattern::addColumn(const std::string& parameterName, const std::string& displayName, int position) {
    // displayName parameter kept for API compatibility but ignored (derived from parameterName)
    
    // Infer category based on parameter name
    ColumnCategory category = ColumnCategory::PARAMETER;
    
    if (parameterName == "index" || parameterName == "length" || parameterName == "note") {
        category = ColumnCategory::TRIGGER;
    } else if (parameterName == "chance") {
        category = ColumnCategory::CONDITION;
    } else {
        // External parameters are PARAMETER category
        category = ColumnCategory::PARAMETER;
    }
    
    // Allow multiple index/note columns, but prevent duplicates for other parameters
    if (category != ColumnCategory::TRIGGER) {
        for (const auto& col : columnConfig) {
            if (col.parameterName == parameterName) {
                ofLogWarning("Pattern") << "Column for parameter '" << parameterName << "' already exists";
                return;
            }
        }
    }
    
    // New columns are optional (not required) by default
    bool isRequired = false;
    
    // Determine insertion position
    int insertPos;
    if (position >= 0 && position < (int)columnConfig.size()) {
        // Explicit position specified
        insertPos = position;
    } else {
        // Auto-position based on category
        if (category == ColumnCategory::TRIGGER) {
            // Find last TRIGGER column position
            insertPos = 0;
            for (size_t i = 0; i < columnConfig.size() && columnConfig[i].category == ColumnCategory::TRIGGER; i++) {
                insertPos = (int)i + 1;
                // If this is length, insert before it (unless we're adding length)
                if (columnConfig[i].parameterName == "length" && parameterName != "length") {
                    insertPos = (int)i;
                    break;
                }
            }
        } else if (category == ColumnCategory::CONDITION) {
            // Insert after TRIGGER, before PARAMETER
            insertPos = 0;
            for (size_t i = 0; i < columnConfig.size(); i++) {
                if (columnConfig[i].category == ColumnCategory::TRIGGER) {
                    insertPos = (int)i + 1;
                } else if (columnConfig[i].category == ColumnCategory::PARAMETER) {
                    break;
                } else if (columnConfig[i].category == ColumnCategory::CONDITION) {
                    insertPos = (int)i + 1;
                }
            }
        } else {
            insertPos = (int)columnConfig.size();
        }
    }
    
    // Insert at calculated position
    columnConfig.insert(columnConfig.begin() + insertPos, 
                       ColumnConfig(parameterName, category, isRequired, insertPos));
    
    // Update column indices
    for (size_t i = 0; i < columnConfig.size(); i++) {
        columnConfig[i].columnIndex = (int)i;
    }
}

void Pattern::removeColumn(int columnIndex) {
    if (columnIndex < 0 || columnIndex >= (int)columnConfig.size()) {
        ofLogWarning("Pattern") << "Invalid column index: " << columnIndex;
        return;
    }
    
    // Don't allow removing required columns
    if (columnConfig[columnIndex].isRequired) {
        ofLogWarning("Pattern") << "Cannot remove required column: " << columnConfig[columnIndex].parameterName;
        return;
    }
    
    // Ensure at least one index/note column remains
    if (columnConfig[columnIndex].parameterName == "index" || columnConfig[columnIndex].parameterName == "note") {
        int indexNoteCount = 0;
        for (const auto& col : columnConfig) {
            if (col.parameterName == "index" || col.parameterName == "note") {
                indexNoteCount++;
            }
        }
        if (indexNoteCount <= 1) {
            ofLogWarning("Pattern") << "Cannot remove last index/note column. At least one is required.";
            return;
        }
    }
    
    // NOTE: We do NOT remove parameter values from steps when removing a column
    // This preserves the values so they can be restored if the column is added back
    // Parameter values are saved in Pattern::toJson() and will persist across saves/loads
    // The column configuration only controls what's displayed in the grid, not what's stored
    
    columnConfig.erase(columnConfig.begin() + columnIndex);
    
    // Update column indices
    for (size_t i = 0; i < columnConfig.size(); i++) {
        columnConfig[i].columnIndex = (int)i;
    }
}

void Pattern::reorderColumn(int fromIndex, int toIndex) {
    if (fromIndex < 0 || fromIndex >= (int)columnConfig.size() ||
        toIndex < 0 || toIndex >= (int)columnConfig.size()) {
        ofLogWarning("Pattern") << "Invalid column indices for reorder: " << fromIndex << " -> " << toIndex;
        return;
    }
    
    // Move the column
    ColumnConfig col = columnConfig[fromIndex];
    columnConfig.erase(columnConfig.begin() + fromIndex);
    columnConfig.insert(columnConfig.begin() + toIndex, col);
    
    // Update column indices
    for (size_t i = 0; i < columnConfig.size(); i++) {
        columnConfig[i].columnIndex = (int)i;
    }
}

void Pattern::swapColumnParameter(int columnIndex, const std::string& newParameterName, const std::string& newDisplayName) {
    if (columnIndex < 0 || columnIndex >= (int)columnConfig.size()) {
        ofLogWarning("Pattern") << "Invalid column index for swap: " << columnIndex;
        return;
    }
    
    // Don't allow swapping required columns (except index/note which can swap between each other)
    bool isIndexNoteSwap = (columnConfig[columnIndex].parameterName == "index" || columnConfig[columnIndex].parameterName == "note") &&
                           (newParameterName == "index" || newParameterName == "note");
    if (columnConfig[columnIndex].isRequired && !isIndexNoteSwap) {
        ofLogWarning("Pattern") << "Cannot swap parameter for required column: " << columnConfig[columnIndex].parameterName;
        return;
    }
    
    // NOTE: We do NOT migrate or remove old parameter values when swapping
    // This preserves all parameter values so they can be restored if the user swaps back
    // The column configuration only controls what's displayed in the grid, not what's stored
    // Old parameter values remain in steps and are saved/loaded with the pattern
    
    // Update parameter name and category if needed
    columnConfig[columnIndex].parameterName = newParameterName;
    
    // Update category based on new parameter name
    if (newParameterName == "index" || newParameterName == "length" || newParameterName == "note") {
        columnConfig[columnIndex].category = ColumnCategory::TRIGGER;
    } else if (newParameterName == "chance" || newParameterName == "ratio") {
        columnConfig[columnIndex].category = ColumnCategory::CONDITION;
    } else {
        columnConfig[columnIndex].category = ColumnCategory::PARAMETER;
    }
}

const ColumnConfig& Pattern::getColumnConfig(int columnIndex) const {
    static ColumnConfig emptyConfig;
    if (columnIndex < 0 || columnIndex >= (int)columnConfig.size()) {
        return emptyConfig;
    }
    return columnConfig[columnIndex];
}

int Pattern::getColumnCount() const {
    return (int)columnConfig.size();
}

