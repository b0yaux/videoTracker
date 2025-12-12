#pragma once

#include "ofMain.h"
#include "ofJson.h"
#include <string>
#include <vector>
#include <map>

// Column category for organizing columns by purpose
enum class ColumnCategory {
    TRIGGER,      // Required: what to play (index/note, length)
    CONDITION,    // Optional: when to trigger (chance)
    PARAMETER     // Optional: how to play (position, speed, volume, external params)
};

// Column configuration for pattern grid
struct ColumnConfig {
    std::string parameterName;      // e.g., "index", "length", "position", "speed", "volume", "chance"
    ColumnCategory category;        // Column category (TRIGGER, CONDITION, PARAMETER)
    bool isRequired;                // true for required columns (index, length), false for optional
    int columnIndex;                // Position in grid (0 = first column)
    
    ColumnConfig() 
        : parameterName(""), category(ColumnCategory::PARAMETER), 
          isRequired(false), columnIndex(0) {}
    
    ColumnConfig(const std::string& param, ColumnCategory cat, bool required, int idx)
        : parameterName(param), category(cat), isRequired(required), columnIndex(idx) {}
    
    // Legacy constructor for backward compatibility (maps isRemovable to isRequired)
    ColumnConfig(const std::string& param, const std::string& display, bool removable, int idx)
        : parameterName(param), 
          category(ColumnCategory::PARAMETER),  // Default category
          isRequired(!removable),  // Inverted: removable=false means required=true
          columnIndex(idx) {}
    
    // Helper methods
    bool isRemovable() const { return !isRequired; }
    bool isTriggerColumn() const { return category == ColumnCategory::TRIGGER; }
    bool isConditionColumn() const { return category == ColumnCategory::CONDITION; }
    bool isParameterColumn() const { return category == ColumnCategory::PARAMETER; }
    // Get display name (capitalize first letter)
    std::string getDisplayName() const {
        if (parameterName.empty()) return "";
        std::string result = parameterName;
        result[0] = std::toupper(result[0]);
        return result;
    }
};

// Step represents a single row in a tracker pattern (the step data)
// NOTE: "Cell" refers to UI elements (table cells), "Step" refers to pattern row data
struct Step {
    // Fixed fields (always present) - tracker-specific parameters
    int index = -1;              // Media index (-1 = empty/rest, 0+ = media index)
    int length = 1;              // Step length in sequencer steps (1-16, integer count)
    int note = -1;               // MIDI note (-1 = not set, 0-127 = MIDI note number)
    int chance = 100;            // Trigger probability (0-100, default 100 = always trigger)
    int ratioA = 1;              // Ratio trigger: which cycle to trigger (1-16, default 1)
    int ratioB = 1;              // Ratio trigger: total cycles in loop (1-16, default 1)
    
    // Dynamic parameter values (keyed by parameter name)
    // These use float for precision (position: 0-1, speed: -10 to 10, volume: 0-2)
    // Note: note and chance are now direct fields, not stored in this map
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
    
    // Steps per beat (per-pattern timing)
    float getStepsPerBeat() const { return stepsPerBeat; }
    void setStepsPerBeat(float steps);
    
    // Serialization
    ofJson toJson() const;
    void fromJson(const ofJson& json);
    
private:
    std::vector<Step> steps;  // Step data for each row in the pattern
    std::vector<ColumnConfig> columnConfig;  // Per-pattern column configuration
    std::vector<Step> overflowSteps;  // Store steps that were cut off when reducing step count
    float stepsPerBeat = 4.0f;  // Steps per beat for this pattern (supports fractional values and negative for backward reading)
    
    bool isValidStep(int stepIndex) const {
        return stepIndex >= 0 && stepIndex < (int)steps.size();
    }
};
