#include "Pattern.h"
#include "ofLog.h"
#include "ofUtils.h"

// PatternCell implementation
//--------------------------------------------------------------
void PatternCell::clear() {
    index = -1;
    length = 1;  // Changed to int
    parameterValues.clear();
    
    // Don't set default parameters here - defaults come from MediaPool/MediaPlayer
    // Empty parameterValues means "use defaults/position memory" when triggering
}

float PatternCell::getParameterValue(const std::string& paramName, float defaultValue) const {
    auto it = parameterValues.find(paramName);
    if (it != parameterValues.end()) {
        return it->second;
    }
    return defaultValue;
}

void PatternCell::setParameterValue(const std::string& paramName, float value) {
    parameterValues[paramName] = value;
}

bool PatternCell::hasParameter(const std::string& paramName) const {
    return parameterValues.find(paramName) != parameterValues.end();
}

void PatternCell::removeParameter(const std::string& paramName) {
    parameterValues.erase(paramName);
}

bool PatternCell::operator==(const PatternCell& other) const {
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

bool PatternCell::operator!=(const PatternCell& other) const {
    return !(*this == other);
}

std::string PatternCell::toString() const {
    if (isEmpty()) {
        return "---";
    }
    
    std::string result = "[" + ofToString(index) + "]";
    result += " len:" + ofToString(length);
    
    // Add parameter values
    for (const auto& pair : parameterValues) {
        result += " " + pair.first + ":" + ofToString(pair.second, 2);
    }
    
    return result;
}

// Pattern implementation
//--------------------------------------------------------------
Pattern::Pattern(int numSteps) {
    setStepCount(numSteps);
}

PatternCell& Pattern::getCell(int step) {
    if (!isValidStep(step)) {
        static PatternCell emptyCell;
        ofLogWarning("Pattern") << "Invalid step index: " << step;
        return emptyCell;
    }
    return cells[step];
}

const PatternCell& Pattern::getCell(int step) const {
    if (!isValidStep(step)) {
        static PatternCell emptyCell;
        ofLogWarning("Pattern") << "Invalid step index: " << step;
        return emptyCell;
    }
    return cells[step];
}

void Pattern::setCell(int step, const PatternCell& cell) {
    if (!isValidStep(step)) {
        ofLogWarning("Pattern") << "Invalid step index: " << step;
        return;
    }
    cells[step] = cell;
}

void Pattern::clearCell(int step) {
    if (!isValidStep(step)) {
        return;
    }
    cells[step].clear();
}

void Pattern::clear() {
    for (auto& cell : cells) {
        cell.clear();
    }
}

bool Pattern::isEmpty() const {
    for (const auto& cell : cells) {
        if (!cell.isEmpty()) {
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
    if (toStep >= (int)cells.size()) {
        ofLogError("Pattern") << "Source range exceeds pattern size: toStep=" << toStep 
                              << ", pattern size=" << cells.size();
        return false;
    }
    
    // Validate that destination range is within bounds
    if (destinationStep + rangeSize - 1 >= (int)cells.size()) {
        ofLogError("Pattern") << "Destination range exceeds pattern size: dest=" << destinationStep 
                              << ", range size=" << rangeSize << ", pattern size=" << cells.size();
        return false;
    }
    
    // Check for overlap (if source and destination overlap, we need to copy to temp first)
    bool hasOverlap = (destinationStep >= fromStep && destinationStep <= toStep) ||
                      (destinationStep + rangeSize - 1 >= fromStep && destinationStep + rangeSize - 1 <= toStep);
    
    if (hasOverlap) {
        // Copy to temporary buffer first to handle overlapping ranges
        std::vector<PatternCell> tempBuffer;
        tempBuffer.reserve(rangeSize);
        for (int i = fromStep; i <= toStep; i++) {
            tempBuffer.push_back(cells[i]);
        }
        
        // Now copy from temp buffer to destination
        for (size_t i = 0; i < tempBuffer.size(); i++) {
            cells[destinationStep + i] = tempBuffer[i];
        }
    } else {
        // No overlap, direct copy
        for (int i = 0; i < rangeSize; i++) {
            cells[destinationStep + i] = cells[fromStep + i];
        }
    }
    
    ofLogNotice("Pattern") << "Duplicated steps " << fromStep << "-" << toStep 
                           << " to position " << destinationStep;
    return true;
}

void Pattern::setStepCount(int steps) {
    if (steps <= 0) {
        ofLogWarning("Pattern") << "Invalid number of steps: " << steps;
        return;
    }
    
    size_t oldSize = cells.size();
    cells.resize(steps);
    
    // Initialize new cells
    for (size_t i = oldSize; i < cells.size(); i++) {
        cells[i] = PatternCell();
    }
}

void Pattern::doubleSteps() {
    int currentSize = (int)cells.size();
    if (currentSize <= 0) {
        ofLogWarning("Pattern") << "Cannot double steps: pattern is empty";
        return;
    }
    
    // Resize to double the current size
    cells.resize(currentSize * 2);
    
    // Duplicate existing cells
    for (int i = 0; i < currentSize; i++) {
        cells[currentSize + i] = cells[i];
    }
    
    ofLogNotice("Pattern") << "Doubled pattern steps from " << currentSize << " to " << (currentSize * 2);
}

ofJson Pattern::toJson() const {
    ofJson patternArray = ofJson::array();
    for (size_t i = 0; i < cells.size(); i++) {
        ofJson cellJson;
        const auto& cell = cells[i];
        cellJson["index"] = cell.index;
        cellJson["length"] = cell.length;
        
        // Save parameter values
        ofJson paramJson = ofJson::object();
        for (const auto& pair : cell.parameterValues) {
            paramJson[pair.first] = pair.second;
        }
        cellJson["parameters"] = paramJson;
        patternArray.push_back(cellJson);
    }
    return patternArray;
}

void Pattern::fromJson(const ofJson& json) {
    if (!json.is_array()) {
        ofLogError("Pattern") << "Invalid JSON format: expected array";
        return;
    }
    
    cells.clear();
    cells.resize(json.size());
    
    for (size_t i = 0; i < json.size(); i++) {
        auto cellJson = json[i];
        PatternCell cell;
        
        // Load fixed fields
        if (cellJson.contains("index")) {
            cell.index = cellJson["index"];
        } else if (cellJson.contains("mediaIndex")) {
            cell.index = cellJson["mediaIndex"]; // Legacy support
        }
        
        if (cellJson.contains("length")) {
            cell.length = cellJson["length"];
        } else if (cellJson.contains("stepLength")) {
            cell.length = cellJson["stepLength"]; // Legacy support
        }
        
        // Load parameter values (new format)
        if (cellJson.contains("parameters") && cellJson["parameters"].is_object()) {
            auto paramJson = cellJson["parameters"];
            for (auto it = paramJson.begin(); it != paramJson.end(); ++it) {
                cell.setParameterValue(it.key(), it.value());
            }
        } else {
            // Legacy: migrate old format to new parameter map
            if (cellJson.contains("position")) cell.setParameterValue("position", cellJson["position"]);
            if (cellJson.contains("speed")) cell.setParameterValue("speed", cellJson["speed"]);
            if (cellJson.contains("volume")) cell.setParameterValue("volume", cellJson["volume"]);
        }
        // Legacy: audioEnabled/videoEnabled fields are ignored (backward compatibility)
        
        cells[i] = cell;
    }
}

