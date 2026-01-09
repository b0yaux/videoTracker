#include "EngineState.h"
#include <sstream>

namespace vt {

// TrackerSequencerState serialization
ofJson TrackerSequencerState::toJson() const {
    ofJson json;
    json["currentPatternIndex"] = currentPatternIndex;
    json["playbackStep"] = playbackStep;
    json["stepCount"] = stepCount;
    json["stepsPerBeat"] = stepsPerBeat;
    json["isPlaying"] = isPlaying;
    
    ofJson patternsJson = ofJson::array();
    for (const auto& pattern : patterns) {
        ofJson patternJson;
        patternJson["stepCount"] = pattern.stepCount;
        ofJson stepsJson = ofJson::array();
        for (const auto& step : pattern.steps) {
            ofJson stepJson;
            stepJson["index"] = step.index;
            stepJson["position"] = step.position;
            stepJson["speed"] = step.speed;
            stepJson["volume"] = step.volume;
            stepJson["length"] = step.length;
            stepJson["chance"] = step.chance;
            if (!step.parameters.empty()) {
                stepJson["parameters"] = step.parameters;
            }
            stepsJson.push_back(stepJson);
        }
        patternJson["steps"] = stepsJson;
        patternsJson.push_back(patternJson);
    }
    json["patterns"] = patternsJson;
    
    ofJson chainJson;
    chainJson["enabled"] = chain.enabled;
    chainJson["chain"] = chain.chain;
    chainJson["repeatCounts"] = chain.repeatCounts;
    chainJson["currentIndex"] = chain.currentIndex;
    json["chain"] = chainJson;
    
    return json;
}

void TrackerSequencerState::fromJson(const ofJson& json) {
    currentPatternIndex = json.value("currentPatternIndex", 0);
    playbackStep = json.value("playbackStep", 0);
    stepCount = json.value("stepCount", 16);
    stepsPerBeat = json.value("stepsPerBeat", 4.0f);
    isPlaying = json.value("isPlaying", false);
    
    if (json.contains("patterns") && json["patterns"].is_array()) {
        patterns.clear();
        for (const auto& patternJson : json["patterns"]) {
            PatternState pattern;
            pattern.stepCount = patternJson.value("stepCount", 16);
            if (patternJson.contains("steps") && patternJson["steps"].is_array()) {
                for (const auto& stepJson : patternJson["steps"]) {
                    StepState step;
                    step.index = stepJson.value("index", -1);
                    step.position = stepJson.value("position", 0.0f);
                    step.speed = stepJson.value("speed", 1.0f);
                    step.volume = stepJson.value("volume", 1.0f);
                    step.length = stepJson.value("length", 1.0f);
                    step.chance = stepJson.value("chance", 1.0f);
                    if (stepJson.contains("parameters")) {
                        step.parameters = stepJson["parameters"];
                    }
                    pattern.steps.push_back(step);
                }
            }
            patterns.push_back(pattern);
        }
    }
    
    if (json.contains("chain")) {
        const auto& chainJson = json["chain"];
        chain.enabled = chainJson.value("enabled", false);
        chain.chain = chainJson.value("chain", std::vector<int>());
        chain.repeatCounts = chainJson.value("repeatCounts", std::vector<int>());
        chain.currentIndex = chainJson.value("currentIndex", 0);
    }
}

// MultiSamplerState serialization
ofJson MultiSamplerState::toJson() const {
    ofJson json;
    json["sampleCount"] = sampleCount;
    json["displayIndex"] = displayIndex;
    json["isPlaying"] = isPlaying;
    
    ofJson samplesJson = ofJson::array();
    for (const auto& sample : samples) {
        ofJson sampleJson;
        sampleJson["audioPath"] = sample.audioPath;
        sampleJson["videoPath"] = sample.videoPath;
        sampleJson["displayName"] = sample.displayName;
        sampleJson["duration"] = sample.duration;
        sampleJson["defaultRegionStart"] = sample.defaultRegionStart;
        sampleJson["defaultRegionEnd"] = sample.defaultRegionEnd;
        sampleJson["defaultStartPosition"] = sample.defaultStartPosition;
        sampleJson["defaultSpeed"] = sample.defaultSpeed;
        sampleJson["defaultVolume"] = sample.defaultVolume;
        samplesJson.push_back(sampleJson);
    }
    json["samples"] = samplesJson;
    
    ofJson voicesJson = ofJson::array();
    for (const auto& voice : activeVoices) {
        ofJson voiceJson;
        voiceJson["sampleIndex"] = voice.sampleIndex;
        voiceJson["isActive"] = voice.isActive;
        voiceJson["position"] = voice.position;
        voiceJson["speed"] = voice.speed;
        voiceJson["volume"] = voice.volume;
        voicesJson.push_back(voiceJson);
    }
    json["activeVoices"] = voicesJson;
    
    return json;
}

void MultiSamplerState::fromJson(const ofJson& json) {
    sampleCount = json.value("sampleCount", 0);
    displayIndex = json.value("displayIndex", 0);
    isPlaying = json.value("isPlaying", false);
    
    if (json.contains("samples") && json["samples"].is_array()) {
        samples.clear();
        for (const auto& sampleJson : json["samples"]) {
            SampleState sample;
            sample.audioPath = sampleJson.value("audioPath", "");
            sample.videoPath = sampleJson.value("videoPath", "");
            sample.displayName = sampleJson.value("displayName", "");
            sample.duration = sampleJson.value("duration", 0.0f);
            sample.defaultRegionStart = sampleJson.value("defaultRegionStart", 0.0f);
            sample.defaultRegionEnd = sampleJson.value("defaultRegionEnd", 1.0f);
            sample.defaultStartPosition = sampleJson.value("defaultStartPosition", 0.0f);
            sample.defaultSpeed = sampleJson.value("defaultSpeed", 1.0f);
            sample.defaultVolume = sampleJson.value("defaultVolume", 1.0f);
            samples.push_back(sample);
        }
    }
    
    if (json.contains("activeVoices") && json["activeVoices"].is_array()) {
        activeVoices.clear();
        for (const auto& voiceJson : json["activeVoices"]) {
            VoiceState voice;
            voice.sampleIndex = voiceJson.value("sampleIndex", -1);
            voice.isActive = voiceJson.value("isActive", false);
            voice.position = voiceJson.value("position", 0.0f);
            voice.speed = voiceJson.value("speed", 1.0f);
            voice.volume = voiceJson.value("volume", 1.0f);
            activeVoices.push_back(voice);
        }
    }
}

// AudioMixerState serialization
ofJson AudioMixerState::toJson() const {
    ofJson json;
    json["inputCount"] = inputCount;
    json["masterVolume"] = masterVolume;
    ofJson volumesJson;
    for (const auto& [idx, vol] : inputVolumes) {
        volumesJson[std::to_string(idx)] = vol;
    }
    json["inputVolumes"] = volumesJson;
    return json;
}

void AudioMixerState::fromJson(const ofJson& json) {
    inputCount = json.value("inputCount", 0);
    masterVolume = json.value("masterVolume", 1.0f);
    inputVolumes.clear();
    if (json.contains("inputVolumes") && json["inputVolumes"].is_object()) {
        for (auto it = json["inputVolumes"].begin(); it != json["inputVolumes"].end(); ++it) {
            try {
                int idx = std::stoi(it.key());
                if (it.value().is_number()) {
                    float vol = it.value();
                    inputVolumes[idx] = vol;
                }
            } catch (const std::exception& e) {
                // Skip invalid keys (non-numeric or non-number values)
                continue;
            }
        }
    }
}

// VideoMixerState serialization
ofJson VideoMixerState::toJson() const {
    ofJson json;
    json["inputCount"] = inputCount;
    json["masterOpacity"] = masterOpacity;
    ofJson opacitiesJson;
    for (const auto& [idx, opacity] : inputOpacities) {
        opacitiesJson[std::to_string(idx)] = opacity;
    }
    json["inputOpacities"] = opacitiesJson;
    return json;
}

void VideoMixerState::fromJson(const ofJson& json) {
    inputCount = json.value("inputCount", 0);
    masterOpacity = json.value("masterOpacity", 1.0f);
    inputOpacities.clear();
    if (json.contains("inputOpacities") && json["inputOpacities"].is_object()) {
        for (auto it = json["inputOpacities"].begin(); it != json["inputOpacities"].end(); ++it) {
            try {
                int idx = std::stoi(it.key());
                if (it.value().is_number()) {
                    float opacity = it.value();
                    inputOpacities[idx] = opacity;
                }
            } catch (const std::exception& e) {
                // Skip invalid keys (non-numeric or non-number values)
                continue;
            }
        }
    }
}

// ConnectionInfo serialization
ofJson ConnectionInfo::toJson() const {
    ofJson json;
    json["sourceModule"] = sourceModule;
    json["targetModule"] = targetModule;
    json["connectionType"] = connectionType;
    json["sourcePath"] = sourcePath;
    json["targetPath"] = targetPath;
    json["eventName"] = eventName;
    json["active"] = active;
    return json;
}

void ConnectionInfo::fromJson(const ofJson& json) {
    sourceModule = json.value("sourceModule", "");
    targetModule = json.value("targetModule", "");
    connectionType = json.value("connectionType", "");
    sourcePath = json.value("sourcePath", "");
    targetPath = json.value("targetPath", "");
    eventName = json.value("eventName", "");
    active = json.value("active", true);
}

// ModuleState serialization
ofJson EngineState::ModuleState::toJson() const {
    ofJson json;
    json["name"] = name;
    json["type"] = type;
    json["enabled"] = enabled;
    
    ofJson paramsJson;
    for (const auto& [key, value] : parameters) {
        paramsJson[key] = value;
    }
    json["parameters"] = paramsJson;
    
    // SIMPLIFIED: Direct JSON assignment (no variant type checking)
    // Modules serialize their own state, we just store it
    if (!typeSpecificData.is_null()) {
        json["typeSpecific"] = typeSpecificData;
    }
    
    return json;
}

void EngineState::ModuleState::fromJson(const ofJson& json) {
    name = json.value("name", "");
    type = json.value("type", "");
    enabled = json.value("enabled", true);
    
    parameters.clear();
    if (json.contains("parameters")) {
        for (auto it = json["parameters"].begin(); it != json["parameters"].end(); ++it) {
            parameters[it.key()] = it.value();
        }
    }
    
    // SIMPLIFIED: Direct JSON assignment (no variant type checking)
    // Modules deserialize their own state when needed
    if (json.contains("typeSpecific")) {
        typeSpecificData = json["typeSpecific"];
    } else {
        typeSpecificData = ofJson();  // Empty JSON
    }
}

// EngineState serialization
std::string EngineState::toJson() const {
    ofJson json;
    
    ofJson transportJson;
    transportJson["isPlaying"] = transport.isPlaying;
    transportJson["bpm"] = transport.bpm;
    transportJson["currentBeat"] = transport.currentBeat;
    json["transport"] = transportJson;
    
    ofJson modulesJson;
    for (const auto& [name, moduleState] : modules) {
        modulesJson[name] = moduleState.toJson();
    }
    json["modules"] = modulesJson;
    
    ofJson connectionsJson = ofJson::array();
    for (const auto& conn : connections) {
        connectionsJson.push_back(conn.toJson());
    }
    json["connections"] = connectionsJson;
    
    return json.dump(2);
}

std::string EngineState::toYaml() const {
    // For now, just return JSON formatted nicely
    // Can add proper YAML support later if needed
    return toJson();
}

EngineState EngineState::fromJson(const std::string& jsonStr) {
    try {
        ofJson json = ofJson::parse(jsonStr);
        return fromJson(json);
    } catch (...) {
        return EngineState();  // Return empty state on error
    }
}

EngineState EngineState::fromJson(const ofJson& json) {
    EngineState state;
    
    if (json.contains("transport")) {
        const auto& transportJson = json["transport"];
        state.transport.isPlaying = transportJson.value("isPlaying", false);
        state.transport.bpm = transportJson.value("bpm", 120.0f);
        state.transport.currentBeat = transportJson.value("currentBeat", 0);
    }
    
    if (json.contains("modules")) {
        for (auto it = json["modules"].begin(); it != json["modules"].end(); ++it) {
            ModuleState moduleState;
            moduleState.fromJson(it.value());
            state.modules[it.key()] = moduleState;
        }
    }
    
    if (json.contains("connections") && json["connections"].is_array()) {
        for (const auto& connJson : json["connections"]) {
            ConnectionInfo conn;
            conn.fromJson(connJson);
            state.connections.push_back(conn);
        }
    }
    
    return state;
}

void EngineState::applyDelta(const StateDelta& delta) {
    // Apply transport changes
    if (delta.transport.isPlayingChanged) {
        transport.isPlaying = delta.transport.isPlaying;
    }
    if (delta.transport.bpmChanged) {
        transport.bpm = delta.transport.bpm;
    }
    if (delta.transport.currentBeatChanged) {
        transport.currentBeat = delta.transport.currentBeat;
    }
    
    // Apply module changes
    for (const auto& [moduleName, moduleDelta] : delta.moduleChanges) {
        auto it = modules.find(moduleName);
        if (it != modules.end()) {
            // Apply enabled change
            if (moduleDelta.enabledChanged) {
                it->second.enabled = moduleDelta.enabled;
            }
            
            // Apply parameter changes
            for (const auto& paramChange : moduleDelta.parameterChanges) {
                it->second.parameters[paramChange.parameterName] = paramChange.value;
            }
        }
    }
    
    // Note: connectionsChanged flag indicates full connection list needs to be rebuilt
    // This is handled by the observer, not by applying the delta
}

} // namespace vt

