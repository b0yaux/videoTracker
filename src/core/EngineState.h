#pragma once

#include "ofJson.h"
#include <string>
#include <map>
#include <vector>
#include <cstdint>

namespace vt {

// Connection information
struct ConnectionInfo {
    std::string sourceModule;
    std::string targetModule;
    std::string connectionType;  // "AUDIO", "VIDEO", "PARAMETER", "EVENT"
    std::string sourcePath;
    std::string targetPath;
    std::string eventName;
    bool active = true;
    
    ofJson toJson() const;
    void fromJson(const ofJson& json);
};

// Module-specific state structures
struct StepState {
    int index = -1;
    float position = 0.0f;
    float speed = 1.0f;
    float volume = 1.0f;
    float length = 1.0f;
    float chance = 1.0f;
    ofJson parameters;
};

struct PatternState {
    int stepCount = 16;
    std::vector<StepState> steps;
};

struct ChainState {
    bool enabled = false;
    std::vector<int> chain;
    std::vector<int> repeatCounts;
    int currentIndex = 0;
};

struct TrackerSequencerState {
    int currentPatternIndex = 0;
    int playbackStep = 0;
    int stepCount = 16;
    float stepsPerBeat = 4.0f;
    bool isPlaying = false;
    std::vector<PatternState> patterns;
    ChainState chain;
    
    ofJson toJson() const;
    void fromJson(const ofJson& json);
};

struct SampleState {
    std::string audioPath;
    std::string videoPath;
    std::string displayName;
    float duration = 0.0f;
    float defaultRegionStart = 0.0f;
    float defaultRegionEnd = 1.0f;
    float defaultStartPosition = 0.0f;
    float defaultSpeed = 1.0f;
    float defaultVolume = 1.0f;
};

struct VoiceState {
    int sampleIndex = -1;
    bool isActive = false;
    float position = 0.0f;
    float speed = 1.0f;
    float volume = 1.0f;
};

struct MultiSamplerState {
    int sampleCount = 0;
    int displayIndex = 0;
    bool isPlaying = false;
    std::vector<SampleState> samples;
    std::vector<VoiceState> activeVoices;
    
    ofJson toJson() const;
    void fromJson(const ofJson& json);
};

struct AudioMixerState {
    int inputCount = 0;
    float masterVolume = 1.0f;
    std::map<int, float> inputVolumes;  // input index -> volume
    
    ofJson toJson() const;
    void fromJson(const ofJson& json);
};

struct VideoMixerState {
    int inputCount = 0;
    float masterOpacity = 1.0f;
    std::map<int, float> inputOpacities;  // input index -> opacity
    
    ofJson toJson() const;
    void fromJson(const ofJson& json);
};

// SIMPLIFIED: Type-specific state is now just JSON
// Modules serialize their own state, Engine just stores it
// This eliminates variant complexity and makes serialization straightforward

// State delta for incremental updates (only changed data)
struct StateDelta {
    // Transport changes
    struct TransportDelta {
        bool isPlayingChanged = false;
        bool isPlaying = false;
        bool bpmChanged = false;
        float bpm = 120.0f;
        bool currentBeatChanged = false;
        int currentBeat = 0;
        
        bool hasChanges() const {
            return isPlayingChanged || bpmChanged || currentBeatChanged;
        }
    };
    
    // Module parameter changes
    struct ParameterChange {
        std::string moduleName;
        std::string parameterName;
        float value;
    };
    
    // Module changes
    struct ModuleDelta {
        bool enabledChanged = false;
        bool enabled = true;
        std::vector<ParameterChange> parameterChanges;
        
        bool hasChanges() const {
            return enabledChanged || !parameterChanges.empty();
        }
    };
    
    TransportDelta transport;
    std::map<std::string, ModuleDelta> moduleChanges;  // module name -> changes
    bool connectionsChanged = false;  // If true, full connection list changed
    
    bool hasChanges() const {
        return transport.hasChanges() || !moduleChanges.empty() || connectionsChanged;
    }
};

// Main EngineState structure
struct EngineState {
    // Transport state
    struct Transport {
        bool isPlaying = false;
        float bpm = 120.0f;
        int currentBeat = 0;
    };
    
    // Module state
    struct ModuleState {
        std::string name;
        std::string type;
        bool enabled = true;
        std::map<std::string, float> parameters;  // parameter name -> value
        
        // SIMPLIFIED: Type-specific state as JSON (no variant complexity)
        // Modules control their own serialization format, Engine just stores it
        ofJson typeSpecificData;  // Replaces std::variant<...>
        
        ofJson toJson() const;
        void fromJson(const ofJson& json);
    };
    
    Transport transport;
    std::map<std::string, ModuleState> modules;  // module name -> state
    std::vector<ConnectionInfo> connections;
    
    // Script state (from ScriptManager)
    struct ScriptState {
        std::string currentScript;
        bool autoUpdateEnabled = true;
    };
    ScriptState script;
    
    // State version (for consistency tracking)
    uint64_t version = 0;  // Monotonically increasing version number
    
    // Serialization
    std::string toJson() const;
    std::string toYaml() const;
    static EngineState fromJson(const std::string& jsonStr);
    static EngineState fromJson(const ofJson& json);
    
    // Apply delta to state
    void applyDelta(const StateDelta& delta);
};

} // namespace vt

