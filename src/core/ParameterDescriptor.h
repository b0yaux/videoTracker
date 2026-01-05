#pragma once

#include <string>
#include <vector>

// Parameter type enumeration
enum class ParameterType {
    FLOAT,
    INT,
    BOOL,
    ENUM,      // Enum selection (e.g., playStyle, polyphonyMode)
    STRING     // Text input/output (future)
};

// Describes a parameter that can be controlled by TrackerSequencer or other modules
struct ParameterDescriptor {
    std::string name;           // e.g., "position", "speed", "volume"
    ParameterType type;         // FLOAT, INT, BOOL, ENUM, STRING
    float minValue;             // For FLOAT/INT parameters
    float maxValue;             // For FLOAT/INT parameters
    float defaultValue;         // Default value
    std::string displayName;    // User-friendly name, e.g., "Position"
    
    // Enum parameters (ENUM)
    std::vector<std::string> enumOptions;  // e.g., {"ONCE", "LOOP", "NEXT"}
    int defaultEnumIndex;                  // Index into enumOptions
    
    // String parameters (STRING) - future
    std::string defaultStringValue;
    size_t maxStringLength;                // Optional: max input length
    
    ParameterDescriptor()
        : name(""), type(ParameterType::FLOAT), minValue(0.0f), maxValue(1.0f), defaultValue(0.0f), displayName(""), defaultEnumIndex(0), maxStringLength(256) {}
    
    ParameterDescriptor(const std::string& n, ParameterType t, float min, float max, float def, const std::string& display)
        : name(n), type(t), minValue(min), maxValue(max), defaultValue(def), displayName(display), defaultEnumIndex(0), maxStringLength(256) {}
    
    // Constructor for enum parameters
    ParameterDescriptor(const std::string& n, ParameterType t, const std::vector<std::string>& options, int defaultIdx, const std::string& display)
        : name(n), type(t), minValue(0.0f), maxValue(0.0f), defaultValue(0.0f), displayName(display), enumOptions(options), defaultEnumIndex(defaultIdx), maxStringLength(256) {}
};


