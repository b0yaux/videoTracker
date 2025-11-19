#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include "ofJson.h"

// Parameter type enumeration
enum class ParameterType {
    FLOAT,
    INT,
    BOOL
};

// Describes a parameter that can be controlled by TrackerSequencer or other modules
struct ParameterDescriptor {
    std::string name;           // e.g., "position", "speed", "volume"
    ParameterType type;         // FLOAT, INT, BOOL
    float minValue;             // For FLOAT/INT parameters
    float maxValue;             // For FLOAT/INT parameters
    float defaultValue;         // Default value
    std::string displayName;    // User-friendly name, e.g., "Position"
    
    ParameterDescriptor()
        : name(""), type(ParameterType::FLOAT), minValue(0.0f), maxValue(1.0f), defaultValue(0.0f), displayName("") {}
    
    ParameterDescriptor(const std::string& n, ParameterType t, float min, float max, float def, const std::string& display)
        : name(n), type(t), minValue(min), maxValue(max), defaultValue(def), displayName(display) {}
};

// Module type enumeration (SunVox-style: sequencers separate, modules are instruments/effects)
enum class ModuleType {
    SEQUENCER,    // TrackerSequencer - generates triggers
    INSTRUMENT,   // MediaPool, MIDIOutput - responds to triggers
    EFFECT,       // Future: video effects, audio effects
    UTILITY       // Future: routing, mixing, utilities
};

// Event data for trigger events (discrete step triggers)
struct TriggerEvent {
    // Map of parameter names to values
    // TrackerSequencer sends: {"note": 60, "position": 0.5, "speed": 1.0, "volume": 1.0}
    // Modules map these to their own parameters (e.g., note → mediaIndex)
    std::map<std::string, float> parameters;
    
    // Optional: duration in seconds (for step-based triggers)
    float duration = 0.0f;
    
    // Step number from sequencer (-1 for non-sequencer triggers like manual preview)
    int step = -1;
};

// Unified base class for instruments and effects (SunVox-style)
// TrackerSequencer stays separate - it connects to Modules but doesn't inherit from Module
// This allows for future BespokeSynth-style evolution where TrackerSequencer becomes a Module too
class Module {
public:
    virtual ~Module() = default;
    
    // Identity
    virtual std::string getName() const = 0;
    virtual ModuleType getType() const = 0;
    
    // Get all available parameters that this module can accept
    // TrackerSequencer will query this to discover what parameters can be mapped to columns
    virtual std::vector<ParameterDescriptor> getParameters() = 0;
    
    // Discrete trigger event (called when a step triggers)
    // This is separate from continuous parameter modulation
    // Parameters map contains all values for this trigger (e.g., note, position, speed, volume)
    // Note: non-const reference to match ofEvent signature requirements
    virtual void onTrigger(TriggerEvent& event) = 0;
    
    // Continuous parameter modulation (for modulators, envelopes, etc.)
    // This is for continuous updates, not discrete triggers
    // paramName: The parameter name (e.g., "position", "speed", "volume")
    // value: The value to set (interpreted based on parameter type)
    // notify: If true, notify parameter change callback (default: true)
    virtual void setParameter(const std::string& paramName, float value, bool notify = true) = 0;
    
    // Parameter change callback (modules can notify external systems like ParameterRouter)
    void setParameterChangeCallback(std::function<void(const std::string&, float)> callback) {
        parameterChangeCallback = callback;
    }
    
    // Optional: update loop (for modules that need continuous updates)
    virtual void update() {}
    
    // Optional: draw GUI (for modules that need visual representation)
    virtual void draw() {}
    
    // Serialization interface
    // Each module implements its own serialization logic
    virtual ofJson toJson() const = 0;
    virtual void fromJson(const ofJson& json) = 0;
    
    // Get module type name for serialization (e.g., "TrackerSequencer", "MediaPool")
    // Default implementation returns getName() - override only if different
    virtual std::string getTypeName() const {
        return getName();
    }

protected:
    // Parameter change callback for synchronization systems
    std::function<void(const std::string&, float)> parameterChangeCallback;
};
