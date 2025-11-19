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
    
    // Add parameter values (3 decimal places for unified precision)
    for (const auto& pair : parameterValues) {
        result += " " + pair.first + ":" + ofToString(pair.second, 3);
    }
    
    return result;
}

// Pattern implementation
//--------------------------------------------------------------
Pattern::Pattern(int numSteps) {
    setStepCount(numSteps);
    initializeDefaultColumns();
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
    ofJson json;
    
    // Save cells
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
    json["cells"] = patternArray;
    
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
    // Handle both old format (array of cells) and new format (object with cells and columnConfig)
    ofJson cellsJson;
    if (json.is_array()) {
        // Old format: just array of cells
        cellsJson = json;
        // Initialize default columns for old format
        initializeDefaultColumns();
    } else if (json.is_object()) {
        // New format: object with cells and columnConfig
        if (json.contains("cells") && json["cells"].is_array()) {
            cellsJson = json["cells"];
        } else {
            ofLogError("Pattern") << "Invalid JSON format: expected 'cells' array";
            return;
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
    
    cells.clear();
    cells.resize(cellsJson.size());
    
    for (size_t i = 0; i < cellsJson.size(); i++) {
        auto cellJson = cellsJson[i];
        PatternCell cell;
        
        // Load fixed fields - handle null values gracefully
        if (cellJson.contains("index") && !cellJson["index"].is_null()) {
            cell.index = cellJson["index"];
        } else if (cellJson.contains("mediaIndex") && !cellJson["mediaIndex"].is_null()) {
            cell.index = cellJson["mediaIndex"]; // Legacy support
        }
        // else: use default value (-1) from PatternCell constructor
        
        if (cellJson.contains("length") && !cellJson["length"].is_null()) {
            cell.length = cellJson["length"];
        } else if (cellJson.contains("stepLength") && !cellJson["stepLength"].is_null()) {
            cell.length = cellJson["stepLength"]; // Legacy support
        }
        // else: use default value (1) from PatternCell constructor
        
        // Load parameter values (new format)
        if (cellJson.contains("parameters") && cellJson["parameters"].is_object()) {
            auto paramJson = cellJson["parameters"];
            for (auto it = paramJson.begin(); it != paramJson.end(); ++it) {
                // Skip null parameter values
                if (!it.value().is_null() && it.value().is_number()) {
                    cell.setParameterValue(it.key(), it.value());
                }
            }
        } else {
            // Legacy: migrate old format to new parameter map
            if (cellJson.contains("position") && !cellJson["position"].is_null()) {
                cell.setParameterValue("position", cellJson["position"]);
            }
            if (cellJson.contains("speed") && !cellJson["speed"].is_null()) {
                cell.setParameterValue("speed", cellJson["speed"]);
            }
            if (cellJson.contains("volume") && !cellJson["volume"].is_null()) {
                cell.setParameterValue("volume", cellJson["volume"]);
            }
        }
        // Legacy: audioEnabled/videoEnabled fields are ignored (backward compatibility)
        
        cells[i] = cell;
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
    
    // NOTE: We do NOT remove parameter values from cells when removing a column
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
    // Old parameter values remain in cells and are saved/loaded with the pattern
    
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

