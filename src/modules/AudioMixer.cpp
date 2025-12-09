#include "AudioMixer.h"
#include "core/ModuleRegistry.h"
#include "core/ModuleFactory.h"
#include "ofLog.h"
#include <algorithm>

AudioMixer::AudioMixer() {
    // Initialize sound mixer
    soundMixer_.setName("Audio Mixer");
    soundMixer_.setMasterVolume(1.0f);
}

AudioMixer::~AudioMixer() {
    // Disconnect all modules
    std::lock_guard<std::mutex> lock(connectionMutex_);
    connectedModules_.clear();
    connectionVolumes_.clear();
}

std::string AudioMixer::getName() const {
    return "AudioMixer";
}

ModuleType AudioMixer::getType() const {
    return ModuleType::UTILITY;
}

std::vector<ParameterDescriptor> AudioMixer::getParameters() const {
    std::vector<ParameterDescriptor> params;
    
    // Master volume parameter
    params.push_back(ParameterDescriptor(
        "masterVolume",
        ParameterType::FLOAT,
        0.0f,
        1.0f,
        1.0f,
        "Master Volume"
    ));
    
    // Per-connection volumes (dynamic based on number of connections)
    std::lock_guard<std::mutex> lock(connectionMutex_);
    for (size_t i = 0; i < connectedModules_.size(); i++) {
        if (!connectedModules_[i].expired()) {
            std::string paramName = "connectionVolume_" + std::to_string(i);
            params.push_back(ParameterDescriptor(
                paramName,
                ParameterType::FLOAT,
                0.0f,
                1.0f,
                1.0f,
                "Connection " + std::to_string(i) + " Volume"
            ));
        }
    }
    
    return params;
}

void AudioMixer::onTrigger(TriggerEvent& event) {
    // Mixers don't receive triggers - they just mix audio
    // This method exists to satisfy Module interface
}

void AudioMixer::setParameter(const std::string& paramName, float value, bool notify) {
    if (paramName == "masterVolume") {
        setMasterVolume(value);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback("masterVolume", value);
        }
    } else if (paramName.find("connectionVolume_") == 0) {
        // Extract connection index from parameter name
        size_t index = std::stoul(paramName.substr(17)); // "connectionVolume_".length() == 17
        setConnectionVolume(index, value);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback(paramName, value);
        }
    }
}

float AudioMixer::getParameter(const std::string& paramName) const {
    if (paramName == "masterVolume") {
        return getMasterVolume();
    } else if (paramName.find("connectionVolume_") == 0) {
        // Extract connection index from parameter name
        size_t index = std::stoul(paramName.substr(17)); // "connectionVolume_".length() == 17
        return getConnectionVolume(index);
    }
    // Unknown parameter - return default
    return Module::getParameter(paramName);
}

Module::ModuleMetadata AudioMixer::getMetadata() const {
    Module::ModuleMetadata metadata;
    metadata.typeName = "AudioMixer";
    metadata.eventNames = {};  // AudioMixer doesn't emit events
    metadata.parameterNames = {"masterVolume"};
    metadata.parameterDisplayNames["masterVolume"] = "Master Volume";
    return metadata;
}

ofJson AudioMixer::toJson() const {
    ofJson json;
    json["type"] = "AudioMixer";
    json["name"] = getName();
    json["masterVolume"] = getMasterVolume();
    
    // Serialize connections
    std::lock_guard<std::mutex> lock(connectionMutex_);
    ofJson connectionsJson = ofJson::array();
    for (size_t i = 0; i < connectedModules_.size(); i++) {
        if (auto module = connectedModules_[i].lock()) {
            ofJson connJson;
            connJson["moduleName"] = module->getName();
            connJson["volume"] = (i < connectionVolumes_.size()) ? connectionVolumes_[i] : 1.0f;
            connectionsJson.push_back(connJson);
        }
    }
    json["connections"] = connectionsJson;
    
    return json;
}

void AudioMixer::fromJson(const ofJson& json) {
    // Load master volume
    if (json.contains("masterVolume")) {
        setMasterVolume(json["masterVolume"].get<float>());
    }
    
    // Note: Connections are restored by SessionManager via restoreConnections()
    // after all modules are loaded
}

void AudioMixer::restoreConnections(const ofJson& connectionsJson, ModuleRegistry* registry) {
    if (!registry || !connectionsJson.is_array()) {
        return;
    }
    
    for (const auto& connJson : connectionsJson) {
        if (!connJson.is_object() || !connJson.contains("moduleName")) {
            continue;
        }
        
        std::string moduleName = connJson["moduleName"].get<std::string>();
        float volume = connJson.contains("volume") ? connJson["volume"].get<float>() : 1.0f;
        
        // Look up module by name
        auto module = registry->getModule(moduleName);
        if (module) {
            int connectionIndex = connectModule(module);
            if (connectionIndex >= 0) {
                setConnectionVolume(static_cast<size_t>(connectionIndex), volume);
                ofLogNotice("AudioMixer") << "Restored connection to " << moduleName 
                                          << " with volume " << volume;
            }
        } else {
            ofLogWarning("AudioMixer") << "Cannot restore connection: module not found: " << moduleName;
        }
    }
}

void AudioMixer::audioOut(ofSoundBuffer& output) {
    // Delegate to underlying sound mixer
    size_t numConnections = soundMixer_.getNumConnections();
    
    // Debug: Log occasionally to verify audioOut is being called
    static int callCount = 0;
    if (++callCount % 1000 == 0) {
        ofLogNotice("AudioMixer") << "audioOut() called #" << callCount 
                                   << ", connections: " << numConnections
                                   << ", buffer size: " << output.getNumFrames();
    }
    
    soundMixer_.audioOut(output);
    
    // Calculate audio level for visualization (only if we have connections)
    if (numConnections > 0) {
        calculateAudioLevel(output);
    } else {
        // No connections, reset level
        currentAudioLevel_ = 0.0f;
    }
}

int AudioMixer::connectModule(std::shared_ptr<Module> module) {
    if (!module) {
        ofLogError("AudioMixer") << "Cannot connect null module";
        return -1;
    }
    
    // Port-based: Check if module has audio output port
    auto outputPorts = module->getOutputPorts();
    const Port* audioOutPort = nullptr;
    for (const auto& port : outputPorts) {
        if (port.type == PortType::AUDIO_OUT) {
            audioOutPort = &port;
            break;
        }
    }
    
    if (!audioOutPort || !audioOutPort->dataPtr) {
        ofLogWarning("AudioMixer") << "Module " << module->getName() << " does not have audio output port";
        return -1;
    }
    
    // Check if already connected
    std::lock_guard<std::mutex> lock(connectionMutex_);
    for (size_t i = 0; i < connectedModules_.size(); i++) {
        if (auto existing = connectedModules_[i].lock()) {
            if (existing == module) {
                ofLogNotice("AudioMixer") << "Module " << module->getName() << " already connected";
                return static_cast<int>(i);
            }
        }
    }
    
    // Get audio output from port dataPtr
    ofxSoundObject* audioOutput = static_cast<ofxSoundObject*>(audioOutPort->dataPtr);
    if (!audioOutput) {
        ofLogError("AudioMixer") << "Module " << module->getName() << " audio output port has invalid dataPtr";
        return -1;
    }
    
    // Connect to sound mixer using connectTo (public interface)
    audioOutput->connectTo(soundMixer_);
    
    // Verify connection was established
    size_t numConnections = soundMixer_.getNumConnections();
    ofLogNotice("AudioMixer") << "After connectTo(), soundMixer_ has " << numConnections << " connections";
    
    // Store module reference and default volume
    connectedModules_.push_back(std::weak_ptr<Module>(module));
    connectionVolumes_.push_back(1.0f);
    
    // Set default volume in sound mixer
    size_t connectionIndex = connectedModules_.size() - 1;
    soundMixer_.setConnectionVolume(connectionIndex, 1.0f);
    
    // Verify final connection count
    numConnections = soundMixer_.getNumConnections();
    ofLogNotice("AudioMixer") << "Connected module " << module->getName() 
                              << " at index " << connectionIndex
                              << " (total connections: " << numConnections << ")";
    
    return static_cast<int>(connectionIndex);
}

void AudioMixer::disconnectModule(std::shared_ptr<Module> module) {
    if (!module) return;
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    for (size_t i = 0; i < connectedModules_.size(); i++) {
        if (auto existing = connectedModules_[i].lock()) {
            if (existing == module) {
                // Get audio output from port and disconnect from sound mixer
                auto outputPorts = module->getOutputPorts();
                for (const auto& port : outputPorts) {
                    if (port.type == PortType::AUDIO_OUT && port.dataPtr) {
                        auto* audioOutput = static_cast<ofxSoundObject*>(port.dataPtr);
                        if (audioOutput) {
                            // Note: disconnectInput is protected, so we use disconnect() on the source
                            // This disconnects all connections from the source, which is acceptable
                            audioOutput->disconnect();
                            break;
                        }
                    }
                }
                
                // Remove from vectors
                connectedModules_.erase(connectedModules_.begin() + i);
                connectionVolumes_.erase(connectionVolumes_.begin() + i);
                
                ofLogNotice("AudioMixer") << "Disconnected module " << module->getName();
                return;
            }
        }
    }
}

void AudioMixer::disconnectModule(size_t connectionIndex) {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (connectionIndex >= connectedModules_.size()) {
        ofLogWarning("AudioMixer") << "Invalid connection index: " << connectionIndex;
        return;
    }
    
    if (auto module = connectedModules_[connectionIndex].lock()) {
        // Get audio output from port and disconnect from sound mixer
        auto outputPorts = module->getOutputPorts();
        for (const auto& port : outputPorts) {
            if (port.type == PortType::AUDIO_OUT && port.dataPtr) {
                auto* audioOutput = static_cast<ofxSoundObject*>(port.dataPtr);
                if (audioOutput) {
                    // Note: disconnectInput is protected, so we use disconnect() on the source
                    // This disconnects all connections from the source, which is acceptable
                    audioOutput->disconnect();
                    break;
                }
            }
        }
    }
    
    // Remove from vectors
    connectedModules_.erase(connectedModules_.begin() + connectionIndex);
    connectionVolumes_.erase(connectionVolumes_.begin() + connectionIndex);
    
    ofLogNotice("AudioMixer") << "Disconnected module at index " << connectionIndex;
}

size_t AudioMixer::getNumConnections() const {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    // Clean up expired weak_ptrs
    size_t count = 0;
    for (const auto& weak : connectedModules_) {
        if (!weak.expired()) {
            count++;
        }
    }
    return count;
}

bool AudioMixer::isConnectedTo(std::shared_ptr<Module> module) const {
    if (!module) return false;
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    for (const auto& weak : connectedModules_) {
        if (auto existing = weak.lock()) {
            if (existing == module) {
                return true;
            }
        }
    }
    return false;
}

int AudioMixer::getConnectionIndex(std::shared_ptr<Module> module) const {
    if (!module) return -1;
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    for (size_t i = 0; i < connectedModules_.size(); i++) {
        if (auto existing = connectedModules_[i].lock()) {
            if (existing == module) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

void AudioMixer::setConnectionVolume(size_t connectionIndex, float volume) {
    volume = ofClamp(volume, 0.0f, 1.0f);
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (connectionIndex >= connectionVolumes_.size()) {
        ofLogWarning("AudioMixer") << "Invalid connection index: " << connectionIndex;
        return;
    }
    
    connectionVolumes_[connectionIndex] = volume;
    soundMixer_.setConnectionVolume(connectionIndex, volume);
}

float AudioMixer::getConnectionVolume(size_t connectionIndex) const {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (connectionIndex >= connectionVolumes_.size()) {
        return 0.0f;
    }
    return connectionVolumes_[connectionIndex];
}

void AudioMixer::setMasterVolume(float volume) {
    volume = ofClamp(volume, 0.0f, 1.0f);
    soundMixer_.setMasterVolume(volume);
}

float AudioMixer::getMasterVolume() const {
    // Note: getMasterVolume() is not const in ofxSoundMixer, but we need const access
    // Use const_cast for now, or store master volume value ourselves
    return const_cast<ofxSoundMixer&>(soundMixer_).getMasterVolume();
}


float AudioMixer::getCurrentAudioLevel() const {
    return currentAudioLevel_;
}

void AudioMixer::calculateAudioLevel(const ofSoundBuffer& buffer) {
    // Simple peak level calculation
    float maxLevel = 0.0f;
    size_t numSamples = buffer.getNumFrames() * buffer.getNumChannels();
    
    for (size_t i = 0; i < numSamples; i++) {
        maxLevel = std::max(maxLevel, std::abs(buffer[i]));
    }
    
    currentAudioLevel_ = maxLevel;
}

void AudioMixer::updateAudioLevelFromBuffer(const ofSoundBuffer& buffer) {
    // Calculate audio level from buffer (called externally when audioOut() isn't invoked)
    // This is needed because soundMixer_ is in the chain, not AudioMixer
    if (soundMixer_.getNumConnections() > 0) {
        calculateAudioLevel(buffer);
    } else {
        currentAudioLevel_ = 0.0f;
    }
}

// Port-based routing interface (Phase 1)
std::vector<Port> AudioMixer::getInputPorts() const {
    std::vector<Port> ports;
    // Create 8 multi-connect audio input ports
    // Note: Mixers can accept multiple connections per port (isMultiConnect = true)
    // In practice, all sources connect to the same soundMixer_, but we expose multiple ports for GUI clarity
    for (size_t i = 0; i < 8; i++) {
        ports.push_back(Port(
            "audio_in_" + std::to_string(i),
            PortType::AUDIO_IN,
            true,  // multi-connect enabled
            "Audio Input " + std::to_string(i + 1),
            const_cast<void*>(static_cast<const void*>(&soundMixer_))
        ));
    }
    return ports;
}

std::vector<Port> AudioMixer::getOutputPorts() const {
    return {
        Port("audio_out", PortType::AUDIO_OUT, false, "Audio Output",
             const_cast<void*>(static_cast<const void*>(&soundMixer_)))
    };
}

//--------------------------------------------------------------
// Module Factory Registration
//--------------------------------------------------------------
namespace {
    struct AudioMixerRegistrar {
        AudioMixerRegistrar() {
            ModuleFactory::registerModuleType("AudioMixer", 
                []() -> std::shared_ptr<Module> {
                    return std::make_shared<AudioMixer>();
                });
        }
    };
    static AudioMixerRegistrar g_audioMixerRegistrar;
}
