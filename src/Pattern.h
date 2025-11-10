#pragma once

#include "ofMain.h"
#include "ofJson.h"
#include <string>
#include <vector>
#include <map>

// PatternCell represents a single step in a tracker pattern
struct PatternCell {
    // Fixed fields (always present)
    int index = -1;              // Media index (-1 = empty/rest, 0+ = media index)
    int length = 1;              // Step length in sequencer steps (1-16, integer count)
    
    // Dynamic parameter values (keyed by parameter name)
    // These use float for precision (position: 0-1, speed: -10 to 10, volume: 0-2)
    std::map<std::string, float> parameterValues;

    PatternCell() = default;
    // Legacy constructor for backward compatibility during migration
    PatternCell(int mediaIdx, float pos, float spd, float vol, float len)
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
    bool operator==(const PatternCell& other) const;
    bool operator!=(const PatternCell& other) const;
    std::string toString() const;
};

// Pattern represents a complete tracker pattern (sequence of steps)
class Pattern {
public:
    Pattern(int numSteps = 16);
    
    // Cell access
    PatternCell& getCell(int step);
    const PatternCell& getCell(int step) const;
    void setCell(int step, const PatternCell& cell);
    void clearCell(int step);
    
    // Pattern operations
    void clear();
    bool isEmpty() const;
    
    // Multi-step duplication: copy a range of steps to a destination
    // fromStep: inclusive start of source range
    // toStep: inclusive end of source range
    // destinationStep: where to copy the range (overwrites existing cells)
    // Returns true if successful, false if range is invalid
    bool duplicateRange(int fromStep, int toStep, int destinationStep);
    
    // Pattern info
    int getStepCount() const { return (int)cells.size(); }
    void setStepCount(int steps);
    
    // Double the pattern length by duplicating all steps
    void doubleSteps();
    
    // Direct access for performance-critical code (used by GUI)
    PatternCell& operator[](int step) { return cells[step]; }
    const PatternCell& operator[](int step) const { return cells[step]; }
    
    // Serialization
    ofJson toJson() const;
    void fromJson(const ofJson& json);
    
private:
    std::vector<PatternCell> cells;
    
    bool isValidStep(int step) const {
        return step >= 0 && step < (int)cells.size();
    }
};
