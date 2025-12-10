#include "AudioOutput.h"
#include "core/ModuleRegistry.h"
#include "core/ModuleFactory.h"
#include "ofLog.h"
#include "ofxSoundUtils.h"
#include <algorithm>
#include <typeinfo>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <map>

// Debug helper to get timestamp
static std::string getDebugTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

AudioOutput::AudioOutput() {
    // Initialize sound mixer (mixes all connected sources)
    soundMixer_.setName("Audio Mixer");
    soundMixer_.setMasterVolume(1.0f);
    
    // Initialize sound output (kept for compatibility, but not used when calling mixer.audioOut() directly)
    soundOutput_.setName("Audio Output");
    
    // Note: We call soundMixer_.audioOut() directly from audioOut() callback
    // soundOutput_ would be used with setOutputStream() for automatic stream management
    // but since we manage soundStream ourselves, we call the mixer directly
    
    // Enumerate audio devices early so device selection works during fromJson()
    refreshAudioDevices();
}

AudioOutput::~AudioOutput() noexcept {
    // Disconnect all modules
    std::lock_guard<std::mutex> lock(connectionMutex_);
    connectedModules_.clear();
    connectionVolumes_.clear();
    connectionAudioLevels_.clear();
    
    // Cleanup audio stream
    // Check if stream is set up and not already closed
    // getNumOutputChannels() returns 0 if stream is closed or not set up
    if (soundStream_.getNumOutputChannels() > 0) {
        try {
            soundStream_.close();
        } catch (...) {
            // Stream might already be closed, ignore
        }
    }
}

std::string AudioOutput::getName() const {
    return "AudioOutput";
}

ModuleType AudioOutput::getType() const {
    return ModuleType::UTILITY;
}

std::vector<ParameterDescriptor> AudioOutput::getParameters() const {
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
    
    // Audio device parameter (index into device list)
    params.push_back(ParameterDescriptor(
        "audioDevice",
        ParameterType::INT,
        0.0f,
        100.0f, // Max devices (will be clamped to actual count)
        0.0f,
        "Audio Device"
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

void AudioOutput::onTrigger(TriggerEvent& event) {
    // Outputs don't receive triggers
}

void AudioOutput::setParameter(const std::string& paramName, float value, bool notify) {
    if (paramName == "masterVolume") {
        setMasterVolume(value);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback("masterVolume", value);
        }
    } else if (paramName == "audioDevice") {
        int deviceIndex = static_cast<int>(value);
        setAudioDevice(deviceIndex);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback("audioDevice", value);
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

float AudioOutput::getParameter(const std::string& paramName) const {
    if (paramName == "masterVolume") {
        return getMasterVolume();
    } else if (paramName == "audioDevice") {
        return static_cast<float>(getAudioDevice());
    } else if (paramName.find("connectionVolume_") == 0) {
        // Extract connection index from parameter name
        size_t index = std::stoul(paramName.substr(17)); // "connectionVolume_".length() == 17
        return getConnectionVolume(index);
    }
    // Unknown parameter - return default
    return Module::getParameter(paramName);
}

Module::ModuleMetadata AudioOutput::getMetadata() const {
    Module::ModuleMetadata metadata;
    metadata.typeName = "AudioOutput";
    metadata.eventNames = {};  // AudioOutput doesn't emit events
    metadata.parameterNames = {"masterVolume", "audioDevice"};
    metadata.parameterDisplayNames["masterVolume"] = "Master Volume";
    metadata.parameterDisplayNames["audioDevice"] = "Audio Device";
    return metadata;
}

ofJson AudioOutput::toJson() const {
    ofJson json;
    json["type"] = "AudioOutput";
    json["name"] = getName();
    json["masterVolume"] = getMasterVolume();
    json["audioDevice"] = getAudioDevice();
    
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

void AudioOutput::fromJson(const ofJson& json) {
    // Load master volume
    if (json.contains("masterVolume")) {
        setMasterVolume(json["masterVolume"].get<float>());
    }
    
    // Load audio device - ensure devices are enumerated first
    if (json.contains("audioDevice")) {
        int savedDeviceIndex = json["audioDevice"].get<int>();
        
        // If devices aren't enumerated yet, enumerate them now
        if (audioDevices_.empty()) {
            refreshAudioDevices();
        }
        
        // Only set device if we have valid devices and the index is valid
        if (!audioDevices_.empty() && savedDeviceIndex >= 0 && savedDeviceIndex < static_cast<int>(audioDevices_.size())) {
            selectedAudioDevice_ = savedDeviceIndex;
            // Don't call setupAudioStream() here - it will be called later in ofApp::setupSoundObjects()
            // Just store the device index for now
        } else {
            // Invalid device index - use default device instead
            ofLogWarning("AudioOutput") << "Invalid saved audio device index: " << savedDeviceIndex 
                                       << ", using default device instead";
            // refreshAudioDevices() already set selectedAudioDevice_ to default, so we're good
        }
    } else {
        // No saved device - ensure default is selected
        if (audioDevices_.empty()) {
            refreshAudioDevices();
        }
        // refreshAudioDevices() already selects default if selectedAudioDevice_ is invalid
    }
    
    // Note: Connections are restored by SessionManager via restoreConnections()
    // after all modules are loaded
}

void AudioOutput::restoreConnections(const ofJson& connectionsJson, ModuleRegistry* registry) {
    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [RESTORE] restoreConnections() called with " 
                                 << (connectionsJson.is_array() ? connectionsJson.size() : 0) << " connections";
    
    if (!registry || !connectionsJson.is_array()) {
        ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [RESTORE] Invalid parameters - registry: " 
                                    << (registry ? "valid" : "null") << ", json is_array: " << connectionsJson.is_array();
        return;
    }
    
    // CRITICAL: Don't hold the lock here - setConnectionVolume() and getConnectionVolume() 
    // already acquire their own locks. Holding the lock here would cause a deadlock.
    size_t mixerBefore = soundMixer_.getNumConnections();
    size_t internalBefore = getNumConnections();
    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [RESTORE] Before restore - mixer: " << mixerBefore 
                               << ", internal: " << internalBefore;
    
    // CRITICAL: Match connections by finding the actual connected module
    // We saved type names ("MediaPool") but need to match to instance names ("mediaPool1", "mediaPool2")
    // ConnectionManager connects modules in the same order as saved, so we can match by index
    // BUT we need to verify the match is correct by checking the module type
    
    size_t connectionIndex = 0;
    for (const auto& connJson : connectionsJson) {
        if (!connJson.is_object() || !connJson.contains("moduleName")) {
            ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [RESTORE] Skipping invalid connection JSON";
            continue;
        }
        
        std::string savedModuleName = connJson["moduleName"].get<std::string>(); // Type name like "MediaPool"
        float volume = connJson.contains("volume") ? connJson["volume"].get<float>() : 1.0f;
        
        // Match by index - ConnectionManager connects in the same order as saved
        // Verify the match by checking if the connected module's type matches the saved type name
        // Use getConnectionModule() which handles locking internally
        if (auto module = getConnectionModule(connectionIndex)) {
            std::string connectedModuleType = module->getName(); // This returns type name
            
            // Verify type matches
            bool typeMatches = (savedModuleName == connectedModuleType);
            
            if (typeMatches) {
                setConnectionVolume(connectionIndex, volume); // This method handles its own locking
                float restoredVolume = getConnectionVolume(connectionIndex); // This method handles its own locking
                
                // Get instance name from registry for logging
                std::string instanceName = registry ? registry->getName(module) : "";
                ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [RESTORE] ✓ Restored volume for connection " 
                                          << connectionIndex << " (" << instanceName << ", type: " << savedModuleName 
                                          << ") to " << volume << " (verified: " << restoredVolume << ")";
            } else {
                ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [RESTORE] Type mismatch at index " 
                                           << connectionIndex << ": saved '" << savedModuleName 
                                           << "' but found '" << connectedModuleType << "' - skipping";
            }
        } else {
            ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [RESTORE] Connection " << connectionIndex 
                                       << " not found or expired";
        }
        
        connectionIndex++;
    }
    
    size_t mixerAfter = soundMixer_.getNumConnections();
    size_t connectionsAfter = getNumConnections();
    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [RESTORE] After restore - mixer: " << mixerAfter 
                               << ", getNumConnections(): " << connectionsAfter;
}

void AudioOutput::audioOut(ofSoundBuffer& buffer) {
    // Initialize buffer to silence first (important for proper audio processing)
    buffer.set(0.0f);
    
    // Clean up expired connections periodically (every 1000 calls)
    static int cleanupCounter = 0;
    if (++cleanupCounter % 1000 == 0) {
        cleanupExpiredConnections();
    }
    
    // Process audio through the mixer directly (which pulls from all connected sources)
    // The mixer will pull from all its connections and mix them together
    size_t numConnections = soundMixer_.getNumConnections();
    size_t internalConnections = getNumConnections();
    
    // Debug: Log occasionally to verify audioOut is being called and connections exist
    static int callCount = 0;
    static int lastConnectionCount = -1;
    callCount++;
    
    // Log every 1000 calls or when connection count changes
    bool shouldLog = (callCount % 1000 == 0 || numConnections != lastConnectionCount);
    
    if (shouldLog) {
        // Log only at verbose level to reduce console spam
        ofLogVerbose("AudioOutput") << "[" << getDebugTimestamp() << "] [AUDIO_OUT] Call #" << callCount
                                    << " - mixer connections: " << numConnections
                                    << ", getNumConnections(): " << internalConnections
                                    << ", buffer size: " << buffer.getNumFrames();
        
        if (numConnections != lastConnectionCount) {
            ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [AUDIO_OUT] ⚠ CONNECTION COUNT CHANGED: " 
                                        << lastConnectionCount << " -> " << numConnections;
            lastConnectionCount = numConnections;
        }
        
        // Enhanced debug: Check each connection to verify they're valid (verbose only)
        if (numConnections > 0) {
            for (size_t i = 0; i < numConnections; i++) {
                auto* source = soundMixer_.getConnectionSource(i);
                if (source) {
                    // Try to cast to mixer to check its connections
                    auto* mixer = dynamic_cast<ofxSoundMixer*>(source);
                    if (mixer) {
                        size_t mixerConnections = mixer->getNumConnections();
                        ofLogVerbose("AudioOutput") << "[" << getDebugTimestamp() << "] [AUDIO_OUT]   Connection " << i 
                                                   << " is a mixer with " << mixerConnections << " internal connections";
                    } else {
                        ofLogVerbose("AudioOutput") << "[" << getDebugTimestamp() << "] [AUDIO_OUT]   Connection " << i 
                                                   << " is not a mixer (type: " << typeid(*source).name() << ")";
                    }
                } else {
                    ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [AUDIO_OUT]   ⚠ Connection " << i << " is null!";
                }
            }
        } else {
            ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [AUDIO_OUT]   ⚠ No connections in soundMixer_!";
        }
    }
    
    // Initialize per-connection levels
    {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        connectionAudioLevels_.resize(numConnections, 0.0f);
    }
    
    // Process each connection individually to capture per-source levels
    // This replicates the mixer's logic but allows us to capture levels
    if (numConnections > 0) {
        ofSoundBuffer tempBuffer;
        ofxSoundUtils::checkBuffers(buffer, tempBuffer, true);
        
        // Get connections and volumes safely
        std::vector<ofxSoundObject*> tempConnections;
        std::vector<float> tempVolumes;
        {
            std::lock_guard<std::mutex> lock(connectionMutex_);
            for (size_t i = 0; i < numConnections; i++) {
                auto* source = soundMixer_.getConnectionSource(i);
                if (source) {
                    // Verify the corresponding weak_ptr is still valid (if index exists)
                    // If index is beyond our tracking, it's a new connection - allow it
                    bool isValid = (i >= connectedModules_.size()) || !connectedModules_[i].expired();
                    if (isValid) {
                        tempConnections.push_back(source);
                        float vol = (i < connectionVolumes_.size()) ? connectionVolumes_[i] : 1.0f;
                        tempVolumes.push_back(vol);
                    } else {
                        // Skip expired connection - will be cleaned up on next cleanup cycle
                        ofLogVerbose("AudioOutput") << "[" << getDebugTimestamp() << "] [AUDIO_OUT] Skipping expired connection at index " << i;
                    }
                }
            }
        }
        
        // Process each connection and capture its level
        for (size_t i = 0; i < tempConnections.size(); i++) {
            if (tempConnections[i] && tempVolumes[i] > 0.0f) {
                tempBuffer.set(0);
                tempConnections[i]->audioOut(tempBuffer);
                
                // Calculate level for this source (before volume scaling)
                float sourceLevel = 0.0f;
                size_t numSamples = tempBuffer.getNumFrames() * tempBuffer.getNumChannels();
                for (size_t j = 0; j < numSamples; j++) {
                    sourceLevel = std::max(sourceLevel, std::abs(tempBuffer[j]));
                }
                
                // Store per-connection level
                {
                    std::lock_guard<std::mutex> lock(connectionMutex_);
                    if (i < connectionAudioLevels_.size()) {
                        connectionAudioLevels_[i] = sourceLevel;
                    }
                }
                
                // Mix into output (with volume scaling)
                for (size_t j = 0; j < tempBuffer.size(); j++) {
                    buffer.getBuffer()[j] += tempBuffer.getBuffer()[j] * tempVolumes[i];
                }
            }
        }
        
        // Apply master volume
        buffer *= soundMixer_.getMasterVolume();
        
        // Calculate master level
        calculateAudioLevel(buffer);
        
        // Process monitoring connections (modules that monitor the mixed audio for visualization)
        // These are modules like Oscilloscope/Spectrogram connected to our output port
        // We call process() (not audioOut()) because they're receiving input, not outputting
        {
            std::lock_guard<std::mutex> lock(connectionMutex_);
            for (ofxSoundObject* monitor : monitoringConnections_) {
                if (monitor) {
                    // Create input and output buffers for process() call
                    ofSoundBuffer inputBuffer = buffer;  // Copy mixed audio as input
                    ofSoundBuffer outputBuffer;
                    // Allocate output buffer with same specs as input
                    ofxSoundUtils::checkBuffers(inputBuffer, outputBuffer, true);
                    
                    // Call process() - this is the correct method for receiving audio input
                    // This will call Oscilloscope/Spectrogram's process() method which extracts samples
                    monitor->process(inputBuffer, outputBuffer);
                }
            }
        }
    } else {
        // No connections, reset levels
        currentAudioLevel_ = 0.0f;
        std::lock_guard<std::mutex> lock(connectionMutex_);
        connectionAudioLevels_.clear();
    }
}

void AudioOutput::setupAudioStream(ofBaseApp* listener) {
    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [SETUP_STREAM] setupAudioStream() called";
    
    audioListener_ = listener;
    
    // Ensure devices are enumerated
    if (audioDevices_.empty()) {
        refreshAudioDevices();
    }
    
    if (audioDevices_.empty()) {
        ofLogError("AudioOutput") << "[" << getDebugTimestamp() << "] [SETUP_STREAM] No audio devices available";
        return;
    }
    
    // Ensure valid device index (use default if invalid)
    if (selectedAudioDevice_ < 0 || selectedAudioDevice_ >= static_cast<int>(audioDevices_.size())) {
        // Find default output device
        selectedAudioDevice_ = -1;
        for (size_t i = 0; i < audioDevices_.size(); i++) {
            if (audioDevices_[i].isDefaultOutput) {
                selectedAudioDevice_ = static_cast<int>(i);
                break;
            }
        }
        // If no default found, use first output device
        if (selectedAudioDevice_ < 0 && !audioDevices_.empty()) {
            selectedAudioDevice_ = 0;
        }
        ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [SETUP_STREAM] Using default audio device: " 
                                    << audioDevices_[selectedAudioDevice_].name;
    }
    
    // Store current connection count to verify they're maintained
    size_t mixerBefore = soundMixer_.getNumConnections();
    size_t connectionsBefore = getNumConnections();
    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [SETUP_STREAM] Before stream setup - mixer: " 
                                << mixerBefore << ", getNumConnections(): " << connectionsBefore;
    
    // Close existing stream if open
    // Check if stream is set up by checking if it has output channels
    if (soundStream_.getNumOutputChannels() > 0) {
        ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [SETUP_STREAM] Closing existing stream (channels: " 
                                    << soundStream_.getNumOutputChannels() << ")";
        soundStream_.close();
        size_t mixerAfterClose = soundMixer_.getNumConnections();
        size_t connectionsAfterClose = getNumConnections();
        ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [SETUP_STREAM] After close - mixer: " 
                                    << mixerAfterClose << ", getNumConnections(): " << connectionsAfterClose;
    }
    
    // Setup new stream
    ofSoundStreamSettings settings;
    settings.setOutListener(listener);
    settings.sampleRate = 44100;
    settings.numOutputChannels = 2;
    settings.numInputChannels = 0;
    settings.bufferSize = 512;
    
    if (selectedAudioDevice_ < static_cast<int>(audioDevices_.size())) {
        settings.setOutDevice(audioDevices_[selectedAudioDevice_]);
    }
    
    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [SETUP_STREAM] Calling soundStream_.setup()...";
    bool setupSuccess = soundStream_.setup(settings);
    
    size_t mixerAfterSetup = soundMixer_.getNumConnections();
    size_t connectionsAfterSetup = getNumConnections();
    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [SETUP_STREAM] After setup() - mixer: " 
                                << mixerAfterSetup << ", getNumConnections(): " << connectionsAfterSetup;
    
    // Verify stream is active
    if (setupSuccess && soundStream_.getNumOutputChannels() > 0) {
        ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [SETUP_STREAM] ✓ Audio stream setup successfully with device: " 
                                    << audioDevices_[selectedAudioDevice_].name
                                    << " (SR: " << soundStream_.getSampleRate() 
                                    << ", channels: " << soundStream_.getNumOutputChannels() 
                                    << ", buffer size: " << soundStream_.getBufferSize() << ")";
        
        // Verify stream is actually running (setup() should start it automatically)
        if (soundStream_.getTickCount() == 0) {
            ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [SETUP_STREAM] ⚠ Audio stream setup but tick count is 0 - stream may not be running yet";
        } else {
            ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [SETUP_STREAM] ✓ Audio stream is running (tick count: " 
                                        << soundStream_.getTickCount() << ")";
        }
    } else {
        ofLogError("AudioOutput") << "[" << getDebugTimestamp() << "] [SETUP_STREAM] ✗ Audio stream setup failed - setupSuccess: " 
                                  << setupSuccess << ", output channels: " << soundStream_.getNumOutputChannels();
    }
    
    // Verify connections are still intact after stream setup
    if (mixerBefore != mixerAfterSetup) {
        ofLogError("AudioOutput") << "[" << getDebugTimestamp() << "] [SETUP_STREAM] ✗ CRITICAL: Mixer connection count changed during stream setup: " 
                                  << mixerBefore << " -> " << mixerAfterSetup;
    } else if (connectionsBefore != connectionsAfterSetup) {
        ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [SETUP_STREAM] ⚠ getNumConnections() changed during stream setup: " 
                                    << connectionsBefore << " -> " << connectionsAfterSetup;
    } else if (mixerBefore > 0) {
        ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [SETUP_STREAM] ✓ Audio stream setup complete - " 
                                    << mixerAfterSetup << " connections maintained";
    } else {
        ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [SETUP_STREAM] ⚠ Audio stream setup complete but NO connections!";
    }
}

std::vector<ofSoundDevice> AudioOutput::getAudioDevices() const {
    // If devices list is empty, try to refresh it
    if (audioDevices_.empty()) {
        // Use const_cast to allow refreshing (getAudioDevices should ideally be non-const)
        const_cast<AudioOutput*>(this)->refreshAudioDevices();
    }
    return audioDevices_;
}

void AudioOutput::setAudioDevice(int deviceIndex) {
    // Ensure devices are enumerated
    if (audioDevices_.empty()) {
        refreshAudioDevices();
    }
    
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(audioDevices_.size())) {
        ofLogWarning("AudioOutput") << "Invalid audio device index: " << deviceIndex 
                                    << " (available devices: " << audioDevices_.size() << ")";
        // Use default device instead
        if (!audioDevices_.empty()) {
            // Find default or use first device
            selectedAudioDevice_ = -1;
            for (size_t i = 0; i < audioDevices_.size(); i++) {
                if (audioDevices_[i].isDefaultOutput) {
                    selectedAudioDevice_ = static_cast<int>(i);
                    break;
                }
            }
            if (selectedAudioDevice_ < 0) {
                selectedAudioDevice_ = 0;
            }
            ofLogNotice("AudioOutput") << "Using default device instead: " << audioDevices_[selectedAudioDevice_].name;
        }
        return;
    }
    
    selectedAudioDevice_ = deviceIndex;
    audioDeviceChanged_ = true;
    
    // Re-setup audio stream if listener is set
    if (audioListener_) {
        setupAudioStream(audioListener_);
    }
}

int AudioOutput::getAudioDevice() const {
    return selectedAudioDevice_;
}

float AudioOutput::getCurrentAudioLevel() const {
    return currentAudioLevel_;
}

float AudioOutput::getConnectionAudioLevel(size_t connectionIndex) const {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (connectionIndex >= connectionAudioLevels_.size()) {
        return 0.0f;
    }
    return connectionAudioLevels_[connectionIndex];
}

// Connection management methods (from AudioMixer)

int AudioOutput::connectModule(std::shared_ptr<Module> module) {
    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [CONNECT] connectModule() called for: " 
                                << (module ? module->getName() : "null");
    
    if (!module) {
        ofLogError("AudioOutput") << "[" << getDebugTimestamp() << "] [CONNECT] Cannot connect null module";
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
        ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [CONNECT] Module " << module->getName() 
                                    << " does not have audio output port (port: " 
                                    << (audioOutPort ? audioOutPort->name : "null") 
                                    << ", dataPtr: " << (audioOutPort && audioOutPort->dataPtr ? "valid" : "null") << ")";
        return -1;
    }
    
    // Check if already connected
    std::lock_guard<std::mutex> lock(connectionMutex_);
    size_t mixerConnectionsBefore = soundMixer_.getNumConnections();
    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [CONNECT] Before connection - mixer: " 
                                << mixerConnectionsBefore << ", internal: " << connectedModules_.size();
    
    for (size_t i = 0; i < connectedModules_.size(); i++) {
        if (auto existing = connectedModules_[i].lock()) {
            if (existing == module) {
                size_t currentMixer = soundMixer_.getNumConnections();
                ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [CONNECT] Module " 
                                          << module->getName() << " already connected at index " << i
                                          << " (mixer: " << currentMixer << ")";
                return static_cast<int>(i);
            }
        }
    }
    
    // Get audio output from port dataPtr
    ofxSoundObject* audioOutput = static_cast<ofxSoundObject*>(audioOutPort->dataPtr);
    if (!audioOutput) {
        ofLogError("AudioOutput") << "[" << getDebugTimestamp() << "] [CONNECT] Module " << module->getName() 
                                  << " audio output port has invalid dataPtr";
        return -1;
    }
    
    // Connect to sound mixer using connectTo (public interface)
    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [CONNECT] Connecting " << module->getName() 
                               << " audio output to soundMixer_ (audioOutput ptr: " 
                               << static_cast<void*>(audioOutput) << ", mixer before: " << mixerConnectionsBefore << ")";
    
    audioOutput->connectTo(soundMixer_);
    
    // Verify connection was established immediately after connectTo
    size_t numConnections = soundMixer_.getNumConnections();
    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [CONNECT] After connectTo(), soundMixer_ has " 
                               << numConnections << " connections (was: " << mixerConnectionsBefore << ")";
    
    // Additional verification: Check if the connection actually exists
    if (numConnections > 0) {
        for (size_t i = 0; i < numConnections; i++) {
            auto* source = soundMixer_.getConnectionSource(i);
            if (source == audioOutput) {
                ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [CONNECT]   ✓ Verified: Connection " << i 
                                           << " matches audioOutput pointer";
            } else {
                ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [CONNECT]   ✗ Mismatch: Connection " << i 
                                            << " pointer: " << static_cast<void*>(source)
                                            << " (expected: " << static_cast<void*>(audioOutput) << ")";
            }
        }
    } else {
        ofLogError("AudioOutput") << "[" << getDebugTimestamp() << "] [CONNECT]   ✗ CRITICAL: Connection was NOT added to soundMixer_!";
    }
    
    // Store module reference and default volume
    connectedModules_.push_back(std::weak_ptr<Module>(module));
    connectionVolumes_.push_back(1.0f);
    
    // Set default volume in sound mixer
    size_t connectionIndex = connectedModules_.size() - 1;
    soundMixer_.setConnectionVolume(connectionIndex, 1.0f);
    
    // Verify final connection count
    numConnections = soundMixer_.getNumConnections();
    
    // DEBUG: Verify weak_ptr is valid
    if (connectedModules_[connectionIndex].expired()) {
        ofLogError("AudioOutput") << "[" << getDebugTimestamp() << "] [CONNECT] CRITICAL: weak_ptr expired immediately after adding! Module: " << module->getName();
    }
    
    // Count internal connections manually (we're already inside the lock)
    size_t internalCount = connectedModules_.size();
    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [CONNECT] ✓ Connected module " << module->getName() 
                              << " at index " << connectionIndex
                              << " (mixer connections: " << numConnections 
                              << ", internal connections: " << internalCount << ")";
    
    return static_cast<int>(connectionIndex);
}

void AudioOutput::disconnectModule(std::shared_ptr<Module> module) {
    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [DISCONNECT] disconnectModule() called for: " 
                                << (module ? module->getName() : "null");
    
    if (!module) {
        ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [DISCONNECT] Cannot disconnect null module";
        return;
    }
    
    // Clean up expired connections first
    cleanupExpiredConnections();
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    size_t mixerConnectionsBefore = soundMixer_.getNumConnections();
    size_t internalBefore = connectedModules_.size();
    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [DISCONNECT] Before disconnect - mixer: " 
                                << mixerConnectionsBefore << ", internal: " << internalBefore;
    
    for (size_t i = 0; i < connectedModules_.size(); i++) {
        if (auto existing = connectedModules_[i].lock()) {
            if (existing == module) {
                // Get audio output from port and disconnect from sound mixer
                // Wrap in try-catch to handle cases where module is partially destroyed
                try {
                    auto outputPorts = module->getOutputPorts();
                    for (const auto& port : outputPorts) {
                        if (port.type == PortType::AUDIO_OUT && port.dataPtr) {
                            auto* audioOutput = static_cast<ofxSoundObject*>(port.dataPtr);
                            if (audioOutput) {
                                ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [DISCONNECT] Disconnecting audioOutput ptr: " 
                                                          << static_cast<void*>(audioOutput);
                                // Note: disconnectInput is protected, so we use disconnect() on the source
                                // This disconnects all connections from the source, which is acceptable
                                // Wrap disconnect() in try-catch as it might fail if module is being destroyed
                                try {
                                    audioOutput->disconnect();
                                    size_t mixerAfter = soundMixer_.getNumConnections();
                                    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [DISCONNECT] After disconnect() - mixer: " 
                                                              << mixerAfter << " (was: " << mixerConnectionsBefore << ")";
                                } catch (const std::exception& e) {
                                    ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [DISCONNECT] Error during audio disconnect: " << e.what();
                                } catch (...) {
                                    ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [DISCONNECT] Unknown error during audio disconnect";
                                }
                                break;
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [DISCONNECT] Error getting audio output for disconnection: " << e.what();
                } catch (...) {
                    ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [DISCONNECT] Unknown error getting audio output for disconnection";
                }
                
                // Remove from vectors (do this even if disconnect failed)
                connectedModules_.erase(connectedModules_.begin() + i);
                connectionVolumes_.erase(connectionVolumes_.begin() + i);
                
                size_t mixerAfter = soundMixer_.getNumConnections();
                size_t internalAfter = connectedModules_.size();
                
                // Safely get module name (module might be partially destroyed)
                try {
                    std::string moduleName = module ? module->getName() : "unknown";
                    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [DISCONNECT] ✓ Disconnected module " << moduleName
                                               << " at index " << i
                                               << " (mixer: " << mixerConnectionsBefore << " -> " << mixerAfter
                                               << ", internal: " << internalBefore << " -> " << internalAfter << ")";
                } catch (...) {
                    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [DISCONNECT] ✓ Disconnected module (name unavailable)";
                }
                return;
            }
        }
    }
    
    ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [DISCONNECT] Module not found in connections: " 
                                << (module ? module->getName() : "null");
}

void AudioOutput::disconnectModule(size_t connectionIndex) {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (connectionIndex >= connectedModules_.size()) {
        ofLogWarning("AudioOutput") << "Invalid connection index: " << connectionIndex;
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
    
    ofLogNotice("AudioOutput") << "Disconnected module at index " << connectionIndex;
}

size_t AudioOutput::getNumConnections() const {
    // Clean up expired weak_ptrs first (need non-const access)
    const_cast<AudioOutput*>(this)->cleanupExpiredConnections();
    
    // The mixer is the source of truth for actual audio connections
    // Use const_cast to access non-const method (safe here as we're just reading)
    size_t mixerConnections = const_cast<AudioOutput*>(this)->soundMixer_.getNumConnections();
    
    // Also check internal tracking for consistency (but trust mixer count)
    std::lock_guard<std::mutex> lock(connectionMutex_);
    size_t internalCount = 0;
    size_t expiredCount = 0;
    for (const auto& weak : connectedModules_) {
        if (!weak.expired()) {
            internalCount++;
        } else {
            expiredCount++;
        }
    }
    
    // DEBUG: Track per-instance (use instance address as ID)
    static std::map<const AudioOutput*, int> instanceCallCounts;
    static std::map<const AudioOutput*, size_t> instanceLastMixerCounts;
    static std::map<const AudioOutput*, size_t> instanceLastInternalCounts;
    
    int& callCount = instanceCallCounts[this];
    size_t& lastMixerCount = instanceLastMixerCounts[this];
    size_t& lastInternalCount = instanceLastInternalCounts[this];
    
    callCount++;
    
    // Only log on actual changes or every 100 calls (to reduce spam)
    bool countChanged = (mixerConnections != lastMixerCount || internalCount != lastInternalCount);
    bool shouldLog = countChanged || 
                     expiredCount > 0 ||
                     (mixerConnections == 0 && lastMixerCount > 0) ||
                     callCount % 100 == 0;  // Log every 100th call even if no change
    
    if (shouldLog) {
        // Get instance ID (use last 4 hex digits of address)
        uintptr_t instanceId = reinterpret_cast<uintptr_t>(this) & 0xFFFF;
        
        // Log only at verbose level to reduce console spam
        ofLogVerbose("AudioOutput") << "[" << getDebugTimestamp() << "] [GET_CONNECTIONS] Instance:0x" 
                                    << std::hex << instanceId << std::dec
                                    << " Call #" << callCount
                                    << " - mixer=" << mixerConnections 
                                    << ", internal=" << internalCount 
                                    << ", expired=" << expiredCount
                                    << ", total weak_ptrs=" << connectedModules_.size();
        
        if (countChanged) {
            ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [GET_CONNECTIONS] ⚠ COUNT CHANGED: "
                                        << "mixer " << lastMixerCount << "->" << mixerConnections
                                        << ", internal " << lastInternalCount << "->" << internalCount;
        }
        
        if (mixerConnections != internalCount) {
            ofLogError("AudioOutput") << "[" << getDebugTimestamp() << "] [GET_CONNECTIONS] ✗ CRITICAL MISMATCH: mixer=" 
                                      << mixerConnections << " != internal=" << internalCount;
        }
        if (expiredCount > 0) {
            ofLogWarning("AudioOutput") << "[" << getDebugTimestamp() << "] [GET_CONNECTIONS] ⚠ " << expiredCount << " expired weak_ptrs";
        }
        
        lastMixerCount = mixerConnections;
        lastInternalCount = internalCount;
    }
    
    // Return mixer count as source of truth (this is what actually matters for audio)
    return mixerConnections;
}

bool AudioOutput::isConnectedTo(std::shared_ptr<Module> module) const {
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

int AudioOutput::getConnectionIndex(std::shared_ptr<Module> module) const {
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

std::string AudioOutput::getConnectionModuleName(size_t connectionIndex) const {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (connectionIndex >= connectedModules_.size()) {
        return "";
    }
    
    if (auto module = connectedModules_[connectionIndex].lock()) {
        return module->getName();
    }
    return "";
}

std::shared_ptr<Module> AudioOutput::getConnectionModule(size_t connectionIndex) const {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (connectionIndex >= connectedModules_.size()) {
        return nullptr;
    }
    return connectedModules_[connectionIndex].lock();
}

void AudioOutput::setConnectionVolume(size_t connectionIndex, float volume) {
    volume = ofClamp(volume, 0.0f, 1.0f);
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (connectionIndex >= connectionVolumes_.size()) {
        ofLogWarning("AudioOutput") << "Invalid connection index: " << connectionIndex;
        return;
    }
    
    connectionVolumes_[connectionIndex] = volume;
    soundMixer_.setConnectionVolume(connectionIndex, volume);
}

float AudioOutput::getConnectionVolume(size_t connectionIndex) const {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (connectionIndex >= connectionVolumes_.size()) {
        return 0.0f;
    }
    return connectionVolumes_[connectionIndex];
}

void AudioOutput::setMasterVolume(float volume) {
    volume = ofClamp(volume, 0.0f, 1.0f);
    soundMixer_.setMasterVolume(volume);
}

float AudioOutput::getMasterVolume() const {
    // Note: getMasterVolume() is not const in ofxSoundMixer, but we need const access
    // Use const_cast for now, or store master volume value ourselves
    return const_cast<ofxSoundMixer&>(soundMixer_).getMasterVolume();
}

// Helper methods

void AudioOutput::calculateAudioLevel(const ofSoundBuffer& buffer) {
    // Simple peak level calculation
    float maxLevel = 0.0f;
    size_t numSamples = buffer.getNumFrames() * buffer.getNumChannels();
    
    for (size_t i = 0; i < numSamples; i++) {
        maxLevel = std::max(maxLevel, std::abs(buffer[i]));
    }
    
    currentAudioLevel_ = maxLevel;
}

void AudioOutput::refreshAudioDevices() {
    // Use global function - works before stream setup
    std::vector<ofSoundDevice> allDevices = ofSoundStreamListDevices();
    
    // Filter to output devices only
    audioDevices_.clear();
    for (const auto& device : allDevices) {
        if (device.outputChannels > 0) {
            audioDevices_.push_back(device);
        }
    }
    
    // Find default output device if not already set or if current selection is invalid
    bool needDefault = (selectedAudioDevice_ < 0 || selectedAudioDevice_ >= static_cast<int>(audioDevices_.size()));
    
    if (needDefault) {
        selectedAudioDevice_ = -1;
        for (size_t i = 0; i < audioDevices_.size(); i++) {
            if (audioDevices_[i].isDefaultOutput) {
                selectedAudioDevice_ = static_cast<int>(i);
                ofLogNotice("AudioOutput") << "Found default output device: " << audioDevices_[i].name 
                                          << " (index: " << selectedAudioDevice_ << ")";
                break;
            }
        }
        // If no default found, use first output device
        if (selectedAudioDevice_ < 0 && !audioDevices_.empty()) {
            selectedAudioDevice_ = 0;
            ofLogNotice("AudioOutput") << "No default device marked, using first output device: " 
                                      << audioDevices_[0].name << " (index: 0)";
        }
    }
    
    ofLogNotice("AudioOutput") << "Refreshed audio device list: " << audioDevices_.size() 
                               << " output devices found, selected: " << selectedAudioDevice_;
}

void AudioOutput::cleanupExpiredConnections() {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    
    // Remove expired weak_ptrs and their corresponding volumes/levels
    size_t originalSize = connectedModules_.size();
    for (size_t i = connectedModules_.size(); i > 0; i--) {
        size_t idx = i - 1;  // Iterate backwards to avoid index issues
        if (connectedModules_[idx].expired()) {
            // Get audio source from mixer before removing (if still valid)
            try {
                if (idx < soundMixer_.getNumConnections()) {
                    auto* source = soundMixer_.getConnectionSource(idx);
                    if (source) {
                        source->disconnect();
                    }
                }
            } catch (...) {
                // Ignore errors during cleanup
            }
            
            // Remove expired entry
            connectedModules_.erase(connectedModules_.begin() + idx);
            if (idx < connectionVolumes_.size()) {
                connectionVolumes_.erase(connectionVolumes_.begin() + idx);
            }
            if (idx < connectionAudioLevels_.size()) {
                connectionAudioLevels_.erase(connectionAudioLevels_.begin() + idx);
            }
        }
    }
    
    if (originalSize != connectedModules_.size()) {
        ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [CLEANUP] Removed " 
                                   << (originalSize - connectedModules_.size()) 
                                   << " expired weak_ptrs (was: " << originalSize 
                                   << ", now: " << connectedModules_.size() << ")";
    }
}

void AudioOutput::clearConnections() {
    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [CLEAR] clearConnections() called";
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    
    // Disconnect all modules from the mixer
    for (size_t i = 0; i < connectedModules_.size(); i++) {
        if (auto module = connectedModules_[i].lock()) {
            // Get audio output from port and disconnect from sound mixer
            try {
                auto outputPorts = module->getOutputPorts();
                for (const auto& port : outputPorts) {
                    if (port.type == PortType::AUDIO_OUT && port.dataPtr) {
                        auto* audioOutput = static_cast<ofxSoundObject*>(port.dataPtr);
                        if (audioOutput) {
                            audioOutput->disconnect();
                            break;
                        }
                    }
                }
            } catch (...) {
                // Module may be partially destroyed, ignore
            }
        } else {
            // Also disconnect from mixer if weak_ptr expired but connection still exists
            try {
                if (i < soundMixer_.getNumConnections()) {
                    auto* source = soundMixer_.getConnectionSource(i);
                    if (source) {
                        source->disconnect();
                    }
                }
            } catch (...) {
                // Ignore errors during cleanup
            }
        }
    }
    
    // Clear all tracking vectors
    connectedModules_.clear();
    connectionVolumes_.clear();
    connectionAudioLevels_.clear();
    
    size_t mixerAfter = soundMixer_.getNumConnections();
    ofLogNotice("AudioOutput") << "[" << getDebugTimestamp() << "] [CLEAR] ✓ Cleared all connections (mixer now has: " 
                               << mixerAfter << " connections)";
}

//--------------------------------------------------------------
// Port-based routing interface (Phase 1)
std::vector<Port> AudioOutput::getInputPorts() const {
    std::vector<Port> ports;
    // Create 8 multi-connect audio input ports (AudioOutput is a sink)
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

std::vector<Port> AudioOutput::getOutputPorts() const {
    std::vector<Port> ports;
    // Expose mixer output for monitoring/visualization (oscilloscope, spectrogram, etc.)
    // This allows modules to tap into the mixed audio output for visualization
    ports.push_back(Port(
        "audio_out",
        PortType::AUDIO_OUT,
        true,  // Multi-connect: multiple modules can monitor the output
        "Audio Output (Mixed)",
        const_cast<ofxSoundObject*>(static_cast<const ofxSoundObject*>(&soundMixer_))
    ));
    return ports;
}

//--------------------------------------------------------------
bool AudioOutput::addMonitoringConnection(std::shared_ptr<Module> monitorModule) {
    if (!monitorModule) {
        ofLogError("AudioOutput") << "Cannot add null monitoring module";
        return false;
    }
    
    // Get the module's audio input (ofxSoundObject)
    auto inputPorts = monitorModule->getInputPorts();
    ofxSoundObject* audioInput = nullptr;
    for (const auto& port : inputPorts) {
        if (port.type == PortType::AUDIO_IN && port.dataPtr) {
            audioInput = static_cast<ofxSoundObject*>(port.dataPtr);
            break;
        }
    }
    
    if (!audioInput) {
        ofLogError("AudioOutput") << "Monitoring module " << monitorModule->getName() 
                                  << " does not have audio input port";
        return false;
    }
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    
    // Check if already registered
    for (ofxSoundObject* existing : monitoringConnections_) {
        if (existing == audioInput) {
            ofLogNotice("AudioOutput") << "Monitoring module " << monitorModule->getName() 
                                       << " already registered";
            return true;
        }
    }
    
    // Add to monitoring connections
    monitoringConnections_.push_back(audioInput);
    ofLogNotice("AudioOutput") << "Added monitoring connection: " << monitorModule->getName() 
                               << " (total: " << monitoringConnections_.size() << ")";
    return true;
}

//--------------------------------------------------------------
void AudioOutput::removeMonitoringConnection(std::shared_ptr<Module> monitorModule) {
    if (!monitorModule) {
        return;
    }
    
    // Get the module's audio input (ofxSoundObject)
    auto inputPorts = monitorModule->getInputPorts();
    ofxSoundObject* audioInput = nullptr;
    for (const auto& port : inputPorts) {
        if (port.type == PortType::AUDIO_IN && port.dataPtr) {
            audioInput = static_cast<ofxSoundObject*>(port.dataPtr);
            break;
        }
    }
    
    if (!audioInput) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    
    // Remove from monitoring connections
    monitoringConnections_.erase(
        std::remove(monitoringConnections_.begin(), monitoringConnections_.end(), audioInput),
        monitoringConnections_.end()
    );
    
    ofLogNotice("AudioOutput") << "Removed monitoring connection: " << monitorModule->getName() 
                               << " (remaining: " << monitoringConnections_.size() << ")";
}

//--------------------------------------------------------------
// Module Factory Registration
//--------------------------------------------------------------
namespace {
    struct AudioOutputRegistrar {
        AudioOutputRegistrar() {
            ModuleFactory::registerModuleType("AudioOutput", 
                []() -> std::shared_ptr<Module> {
                    return std::make_shared<AudioOutput>();
                });
        }
    };
    static AudioOutputRegistrar g_audioOutputRegistrar;
}

