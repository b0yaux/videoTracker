#pragma once

#include "ofMain.h"
#include "ofJson.h"
#include <string>
#include <vector>
#include <map>

// Column configuration for pattern grid
struct ColumnConfig {
    std::string parameterName;      // e.g., "position", "speed", "volume" (or "index", "length" for required)
    std::string displayName;        // e.g., "Position", "Speed", "Volume"
    bool isRemovable;               // true if column can be deleted (default: true). false for required columns like index/length
    int columnIndex;                // Position in grid (0 = first column)
    
    ColumnConfig() : parameterName(""), displayName(""), isRemovable(true), columnIndex(0) {}
    ColumnConfig(const std::string& param, const std::string& display, bool removable, int idx)
        : parameterName(param), displayName(display), isRemovable(removable), columnIndex(idx) {}
};

// Step represents a single row in a tracker pattern (the step data)
// NOTE: "Cell" refers to UI elements (table cells), "Step" refers to pattern row data
struct Step {
    // Fixed fields (always present)
    int index = -1;              // Media index (-1 = empty/rest, 0+ = media index)
    int length = 1;              // Step length in sequencer steps (1-16, integer count)
    
    // Dynamic parameter values (keyed by parameter name)
    // These use float for precision (position: 0-1, speed: -10 to 10, volume: 0-2)
    std::map<std::string, float> parameterValues;

    Step() = default;
    // Legacy constructor for backward compatibility during migration
    Step(int mediaIdx, float pos, float spd, float vol, float len)
        : index(mediaIdx), length((int)len) {
        // Store old parameters in map for migration
        parameterValues["position"] = pos;
        parameterValues["speed"] = spd;
        parameterValues["volume"] = vol;
    }

    bool isEmpty() const { return index < 0; }
    
    // Parameter access methods
    float getParameterValue(const std::string& paramName, float defaultValue = 0.0f) const;
    void setParameterValue(const std::string& paramName, float value);
    bool hasParameter(const std::string& paramName) const;
    void removeParameter(const std::string& paramName);
    
    // Additional methods
    void clear();
    bool operator==(const Step& other) const;
    bool operator!=(const Step& other) const;
    std::string toString() const;
};

// Pattern represents a complete tracker pattern (sequence of steps)
class Pattern {
public:
    Pattern(int stepCount = 16);
    
    // Step access (step = row index in pattern, 0-based)
    Step& getStep(int stepIndex);
    const Step& getStep(int stepIndex) const;
    void setStep(int stepIndex, const Step& step);
    void clearStep(int stepIndex);
    
    // Pattern operations
    void clear();
    bool isEmpty() const;
    
    // Multi-step duplication: copy a range of steps to a destination
    // fromStep: inclusive start of source range
    // toStep: inclusive end of source range
    // destinationStep: where to copy the range (overwrites existing steps)
    // Returns true if successful, false if range is invalid
    bool duplicateRange(int fromStep, int toStep, int destinationStep);
    
    // Pattern info
    int getStepCount() const { return (int)steps.size(); }
    void setStepCount(int stepCount);
    
    // Double the pattern length by duplicating all steps
    void doubleSteps();
    
    // Direct access for performance-critical code (used by GUI)
    Step& operator[](int stepIndex) { return steps[stepIndex]; }
    const Step& operator[](int stepIndex) const { return steps[stepIndex]; }
    
    // Column configuration management (per-pattern)
    void initializeDefaultColumns();
    void addColumn(const std::string& parameterName, const std::string& displayName, int position = -1);
    void removeColumn(int columnIndex);
    void reorderColumn(int fromIndex, int toIndex);
    void swapColumnParameter(int columnIndex, const std::string& newParameterName, const std::string& newDisplayName = "");
    const ColumnConfig& getColumnConfig(int columnIndex) const;
    int getColumnCount() const;
    const std::vector<ColumnConfig>& getColumnConfiguration() const { return columnConfig; }
    
    // Serialization
    ofJson toJson() const;
    void fromJson(const ofJson& json);
    
private:
    std::vector<Step> steps;  // Step data for each row in the pattern
    std::vector<ColumnConfig> columnConfig;  // Per-pattern column configuration
    
    bool isValidStep(int stepIndex) const {
        return stepIndex >= 0 && stepIndex < (int)steps.size();
    }
};
