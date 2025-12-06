#include "Pattern.h"
#include "ofLog.h"
#include "ofUtils.h"
#include <algorithm>  // For std::min

// Step implementation
//--------------------------------------------------------------
void Step::clear() {
    index = -1;
    length = 1;  // Changed to int
    parameterValues.clear();
    
    // Don't set default parameters here - defaults come from MediaPool/MediaPlayer
    // Empty parameterValues means "use defaults/position memory" when triggering
}

float Step::getParameterValue(const std::string& paramName, float defaultValue) const {
    auto it = parameterValues.find(paramName);
    if (it != parameterValues.end()) {
        return it->second;
    }
    return defaultValue;
}

void Step::setParameterValue(const std::string& paramName, float value) {
    parameterValues[paramName] = value;
}

bool Step::hasParameter(const std::string& paramName) const {
    return parameterValues.find(paramName) != parameterValues.end();
}

void Step::removeParameter(const std::string& paramName) {
    parameterValues.erase(paramName);
}

bool Step::operator==(const Step& other) const {
    if (index != other.index || length != other.length) {
        return false;
    }
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
    
    // Save stepCount explicitly to preserve pattern length
    json["stepCount"] = (int)steps.size();
    
    // Save steps (JSON key "cells" kept for backward compatibility)
    ofJson patternArray = ofJson::array();
    for (size_t i = 0; i < steps.size(); i++) {
        ofJson stepJson;
        const auto& step = steps[i];
        stepJson["index"] = step.index;
        stepJson["length"] = step.length;
        
        // Save parameter values
        ofJson paramJson = ofJson::object();
        for (const auto& pair : step.parameterValues) {
            paramJson[pair.first] = pair.second;
        }
        stepJson["parameters"] = paramJson;
        patternArray.push_back(stepJson);
    }
    json["cells"] = patternArray;  // Keep "cells" key for backward compatibility
    
    // Save column configuration
    ofJson columnArray = ofJson::array();
    for (const auto& col : columnConfig) {
        ofJson colJson;
        colJson["parameterName"] = col.parameterName;
        colJson["displayName"] = col.displayName;
        colJson["isRemovable"] = col.isRemovable;
        colJson["columnIndex"] = col.columnIndex;
        columnArray.push_back(colJson);
    }
    json["columnConfig"] = columnArray;
    
    return json;
}

void Pattern::fromJson(const ofJson& json) {
    // Handle both old format (array of steps) and new format (object with steps and columnConfig)
    // JSON key "cells" kept for backward compatibility
    ofJson stepsJson;
    int stepCount = 16;  // Default step count (fallback only)
    
    if (json.is_array()) {
        // Old format: just array of steps
        stepsJson = json;
        stepCount = (int)stepsJson.size();  // Infer from array size
        // Initialize default columns for old format
        initializeDefaultColumns();
    } else if (json.is_object()) {
        // New format: object with steps and columnConfig
        if (json.contains("cells") && json["cells"].is_array()) {
            stepsJson = json["cells"];  // JSON key "cells" for backward compatibility
        } else {
            ofLogError("Pattern") << "Invalid JSON format: expected 'cells' array";
            return;
        }
        
        // Load stepCount if present (new format) - this is the authoritative value
        // Only fall back to array size if stepCount is not explicitly saved
        if (json.contains("stepCount") && json["stepCount"].is_number()) {
            stepCount = json["stepCount"];  // Direct conversion (ofJson supports this)
        } else {
            // No stepCount in JSON - infer from array size (backward compatibility)
            stepCount = (int)stepsJson.size();
        }
        
        // Load column configuration if present
        if (json.contains("columnConfig") && json["columnConfig"].is_array()) {
            columnConfig.clear();
            for (const auto& colJson : json["columnConfig"]) {
                ColumnConfig col;
                if (colJson.contains("parameterName")) col.parameterName = colJson["parameterName"];
                if (colJson.contains("displayName")) col.displayName = colJson["displayName"];
                if (colJson.contains("isRemovable")) col.isRemovable = colJson["isRemovable"];
                if (colJson.contains("columnIndex")) col.columnIndex = colJson["columnIndex"];
                columnConfig.push_back(col);
            }
        } else {
            // No column config in JSON - initialize defaults
            initializeDefaultColumns();
        }
    } else {
        ofLogError("Pattern") << "Invalid JSON format: expected array or object";
        return;
    }
    
    // Resize to stepCount (preserves user-set pattern length)
    // This overwrites any previous size set by Pattern constructor
    steps.clear();
    steps.resize(stepCount);
    
    ofLogNotice("Pattern") << "Loading pattern with stepCount=" << stepCount 
                           << " (cells in JSON: " << stepsJson.size() << ")";
    
    // Load step data (may be fewer items than stepCount if pattern was extended)
    for (size_t i = 0; i < stepsJson.size() && i < (size_t)stepCount; i++) {
        auto stepJson = stepsJson[i];
        Step step;
        
        // Load fixed fields - handle null values gracefully
        if (stepJson.contains("index") && !stepJson["index"].is_null()) {
            step.index = stepJson["index"];
        } else if (stepJson.contains("mediaIndex") && !stepJson["mediaIndex"].is_null()) {
            step.index = stepJson["mediaIndex"]; // Legacy support
        }
        // else: use default value (-1) from Step constructor
        
        if (stepJson.contains("length") && !stepJson["length"].is_null()) {
            step.length = stepJson["length"];
        } else if (stepJson.contains("stepLength") && !stepJson["stepLength"].is_null()) {
            step.length = stepJson["stepLength"]; // Legacy support
        }
        // else: use default value (1) from Step constructor
        
        // Load parameter values (new format)
        if (stepJson.contains("parameters") && stepJson["parameters"].is_object()) {
            auto paramJson = stepJson["parameters"];
            for (auto it = paramJson.begin(); it != paramJson.end(); ++it) {
                // Skip null parameter values
                if (!it.value().is_null() && it.value().is_number()) {
                    step.setParameterValue(it.key(), it.value());
                }
            }
        } else {
            // Legacy: migrate old format to new parameter map
            if (stepJson.contains("position") && !stepJson["position"].is_null()) {
                step.setParameterValue("position", stepJson["position"]);
            }
            if (stepJson.contains("speed") && !stepJson["speed"].is_null()) {
                step.setParameterValue("speed", stepJson["speed"]);
            }
            if (stepJson.contains("volume") && !stepJson["volume"].is_null()) {
                step.setParameterValue("volume", stepJson["volume"]);
            }
        }
        // Legacy: audioEnabled/videoEnabled fields are ignored (backward compatibility)
        
        steps[i] = step;
    }
}

// Column configuration methods
//--------------------------------------------------------------
void Pattern::initializeDefaultColumns() {
    columnConfig.clear();
    // Required columns (not removable)
    columnConfig.push_back(ColumnConfig("index", "Index", false, 0));      // isRemovable = false
    columnConfig.push_back(ColumnConfig("length", "Length", false, 1));    // isRemovable = false
    // Default parameter columns (removable)
    columnConfig.push_back(ColumnConfig("position", "Position", true, 2));  // isRemovable = true
    columnConfig.push_back(ColumnConfig("speed", "Speed", true, 3));  // isRemovable = true
    columnConfig.push_back(ColumnConfig("volume", "Volume", true, 4));  // isRemovable = true
}

void Pattern::addColumn(const std::string& parameterName, const std::string& displayName, int position) {
    // Don't allow duplicate parameter names
    for (const auto& col : columnConfig) {
        if (col.parameterName == parameterName) {
            ofLogWarning("Pattern") << "Column for parameter '" << parameterName << "' already exists";
            return;
        }
    }
    
    int insertPos = (position < 0 || position >= (int)columnConfig.size()) ? (int)columnConfig.size() : position;
    
    // Insert at specified position (new columns are removable by default)
    columnConfig.insert(columnConfig.begin() + insertPos, ColumnConfig(parameterName, displayName, true, insertPos));
    
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
    
    // Don't allow removing non-removable columns
    if (!columnConfig[columnIndex].isRemovable) {
        ofLogWarning("Pattern") << "Cannot remove required column: " << columnConfig[columnIndex].parameterName;
        return;
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
    
    // Don't allow swapping non-removable columns
    if (!columnConfig[columnIndex].isRemovable) {
        ofLogWarning("Pattern") << "Cannot swap parameter for required column: " << columnConfig[columnIndex].parameterName;
        return;
    }
    
    // NOTE: We do NOT migrate or remove old parameter values when swapping
    // This preserves all parameter values so they can be restored if the user swaps back
    // The column configuration only controls what's displayed in the grid, not what's stored
    // Old parameter values remain in steps and are saved/loaded with the pattern
    
    // Update parameter name (this only changes what the column displays)
    columnConfig[columnIndex].parameterName = newParameterName;
    
    // Update display name
    if (!newDisplayName.empty()) {
        columnConfig[columnIndex].displayName = newDisplayName;
    } else {
        // Use parameter name as fallback
        columnConfig[columnIndex].displayName = newParameterName;
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

