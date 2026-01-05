#pragma once

#include "ofJson.h"
#include <string>
#include <map>
#include <vector>
#include <variant>

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

// Type alias for module-specific state variant
using ModuleTypeSpecificState = std::variant<
    std::monostate,  // No type-specific state
    TrackerSequencerState,
    MultiSamplerState,
    AudioMixerState,
    VideoMixerState
>;

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
        
        // Type-specific state (variant for different module types)
        ModuleTypeSpecificState typeSpecific;
        
        ofJson toJson() const;
        void fromJson(const ofJson& json);
    };
    
    Transport transport;
    std::map<std::string, ModuleState> modules;  // module name -> state
    std::vector<ConnectionInfo> connections;
    
    // Serialization
    std::string toJson() const;
    std::string toYaml() const;
    static EngineState fromJson(const std::string& jsonStr);
    static EngineState fromJson(const ofJson& json);
};

} // namespace vt

