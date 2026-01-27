#include "VideoMixer.h"
#include "core/ModuleRegistry.h"
#include "core/ModuleFactory.h"
#include "ofLog.h"
#include <algorithm>

VideoMixer::VideoMixer() 
    : masterOpacity_(1.0f) {
    // Setup parameters
    params.setName("VideoMixer");
    params.add(masterOpacityParam.set("Master Opacity", 1.0f, 0.0f, 1.0f));
    params.add(blendModeParam.set("Blend Mode", 0, 0, 2));
    params.add(autoNormalizeParam.set("Auto Normalize", true));

    // Add listeners
    masterOpacityParam.addListener(this, &VideoMixer::onMasterOpacityParamChanged);
    blendModeParam.addListener(this, &VideoMixer::onBlendModeParamChanged);
    autoNormalizeParam.addListener(this, &VideoMixer::onAutoNormalizeParamChanged);

    // Initialize video mixer
    videoMixer_.setName("Video Mixer");
    videoMixer_.setMasterOpacity(1.0f);
    videoMixer_.setBlendMode(OF_BLENDMODE_ADD);
    videoMixer_.setAutoNormalize(true);
    
    // Initialize output FBO
    ensureOutputFbo();
}

VideoMixer::~VideoMixer() {
    // Disconnect all modules
    std::lock_guard<std::mutex> lock(connectionMutex_);
    connectedModules_.clear();
    sourceOpacities_.clear();
}

void VideoMixer::setMasterOpacity(float opacity) {
    masterOpacityParam.set(opacity);
}

void VideoMixer::onMasterOpacityParamChanged(float& val) {
    masterOpacity_ = ofClamp(val, 0.0f, 1.0f);
    videoMixer_.setMasterOpacity(masterOpacity_);
}

float VideoMixer::getMasterOpacity() const {
    return masterOpacity_;
}

void VideoMixer::setBlendMode(ofBlendMode mode) {
    int modeIndex = 0;
    if (mode == OF_BLENDMODE_ADD) modeIndex = 0;
    else if (mode == OF_BLENDMODE_MULTIPLY) modeIndex = 1;
    else if (mode == OF_BLENDMODE_ALPHA) modeIndex = 2;
    blendModeParam.set(modeIndex);
}

void VideoMixer::onBlendModeParamChanged(int& val) {
    ofBlendMode mode = OF_BLENDMODE_ADD;
    if (val == 0) mode = OF_BLENDMODE_ADD;
    else if (val == 1) mode = OF_BLENDMODE_MULTIPLY;
    else if (val == 2) mode = OF_BLENDMODE_ALPHA;
    videoMixer_.setBlendMode(mode);
}

ofBlendMode VideoMixer::getBlendMode() const {
    return videoMixer_.getBlendMode();
}

void VideoMixer::setAutoNormalize(bool enabled) {
    autoNormalizeParam.set(enabled);
}

void VideoMixer::onAutoNormalizeParamChanged(bool& val) {
    videoMixer_.setAutoNormalize(val);
}

bool VideoMixer::getAutoNormalize() const {
    return videoMixer_.getAutoNormalize();
}

std::string VideoMixer::getName() const {
    return "VideoMixer";
}

ModuleType VideoMixer::getType() const {
    return ModuleType::UTILITY;
}

std::vector<ParameterDescriptor> VideoMixer::getParameters() const {
    std::vector<ParameterDescriptor> params;
    
    // Master opacity parameter
    params.push_back(ParameterDescriptor(
        "masterOpacity",
        ParameterType::FLOAT,
        0.0f,
        1.0f,
        1.0f,
        "Master Opacity"
    ));
    
    // Blend mode parameter (0=ADD, 1=MULTIPLY, 2=ALPHA)
    params.push_back(ParameterDescriptor(
        "blendMode",
        ParameterType::INT,
        0.0f,
        2.0f,
        0.0f,
        "Blend Mode"
    ));
    
    // Auto-normalize parameter
    params.push_back(ParameterDescriptor(
        "autoNormalize",
        ParameterType::BOOL,
        0.0f,
        1.0f,
        1.0f,
        "Auto Normalize"
    ));
    
    // Per-connection opacities (dynamic based on number of connections)
    std::lock_guard<std::mutex> lock(connectionMutex_);
    for (size_t i = 0; i < connectedModules_.size(); i++) {
        if (!connectedModules_[i].expired()) {
            std::string paramName = "connectionOpacity_" + std::to_string(i);
            params.push_back(ParameterDescriptor(
                paramName,
                ParameterType::FLOAT,
                0.0f,
                1.0f,
                1.0f,
                "Connection " + std::to_string(i) + " Opacity"
            ));
        }
    }
    
    return params;
}

void VideoMixer::onTrigger(TriggerEvent& event) {
    // Mixers don't receive triggers - they just mix video
    // This method exists to satisfy Module interface
}

void VideoMixer::setParameter(const std::string& paramName, float value, bool notify) {
    if (paramName == "masterOpacity") {
        setMasterOpacity(value);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback("masterOpacity", value);
        }
    } else if (paramName == "blendMode") {
        int modeIndex = static_cast<int>(value);
        ofBlendMode mode = OF_BLENDMODE_ADD;
        if (modeIndex == 0) mode = OF_BLENDMODE_ADD;
        else if (modeIndex == 1) mode = OF_BLENDMODE_MULTIPLY;
        else if (modeIndex == 2) mode = OF_BLENDMODE_ALPHA;
        setBlendMode(mode);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback("blendMode", value);
        }
    } else if (paramName == "autoNormalize") {
        setAutoNormalize(value > 0.5f);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback("autoNormalize", value);
        }
    } else if (paramName.find("connectionOpacity_") == 0) {
        // Extract source index from parameter name
        size_t index = std::stoul(paramName.substr(19)); // "connectionOpacity_".length() == 19
        setConnectionOpacity(index, value);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback(paramName, value);
        }
    }
}

float VideoMixer::getParameter(const std::string& paramName) const {
    if (paramName == "masterOpacity") {
        return getMasterOpacity();
    } else if (paramName == "blendMode") {
        ofBlendMode mode = getBlendMode();
        if (mode == OF_BLENDMODE_ADD) return 0.0f;
        else if (mode == OF_BLENDMODE_MULTIPLY) return 1.0f;
        else if (mode == OF_BLENDMODE_ALPHA) return 2.0f;
        return 0.0f;
    } else if (paramName == "autoNormalize") {
        return getAutoNormalize() ? 1.0f : 0.0f;
    } else if (paramName.find("connectionOpacity_") == 0) {
        // Extract source index from parameter name
        size_t index = std::stoul(paramName.substr(19)); // "connectionOpacity_".length() == 19
        return getConnectionOpacity(index);
    }
    // Unknown parameter - return default
    return Module::getParameter(paramName);
}

Module::ModuleMetadata VideoMixer::getMetadata() const {
    Module::ModuleMetadata metadata;
    metadata.typeName = "VideoMixer";
    metadata.eventNames = {};  // VideoMixer doesn't emit events
    metadata.parameterNames = {"masterOpacity", "blendMode", "autoNormalize"};
    metadata.parameterDisplayNames["masterOpacity"] = "Master Opacity";
    metadata.parameterDisplayNames["blendMode"] = "Blend Mode";
    metadata.parameterDisplayNames["autoNormalize"] = "Auto Normalize";
    return metadata;
}

ofJson VideoMixer::toJson(class ModuleRegistry* registry) const {
    ofJson json;
    ofSerialize(json, params);
    
    // Serialize connections
    std::lock_guard<std::mutex> lock(connectionMutex_);
    ofJson connectionsJson = ofJson::array();
    for (size_t i = 0; i < connectedModules_.size(); i++) {
        if (auto module = connectedModules_[i].lock()) {
            ofJson connJson;
            connJson["moduleName"] = module->getName();
            connJson["opacity"] = (i < sourceOpacities_.size()) ? sourceOpacities_[i] : 1.0f;
            connectionsJson.push_back(connJson);
        }
    }
    json["connections"] = connectionsJson;
    
    return json;
}

void VideoMixer::fromJson(const ofJson& json) {
    ofDeserialize(json, params);
    
    // Listeners triggered by ofDeserialize will sync state
    
    // Note: Connections are restored by SessionManager via restoreConnections()
    // after all modules are loaded
}

void VideoMixer::restoreConnections(const ofJson& connectionsJson, ModuleRegistry* registry) {
    if (!registry || !connectionsJson.is_array()) {
        return;
    }
    
    for (const auto& connJson : connectionsJson) {
        if (!connJson.is_object() || !connJson.contains("moduleName")) {
            continue;
        }
        
        std::string moduleName = connJson["moduleName"].get<std::string>();
        float opacity = connJson.contains("opacity") ? connJson["opacity"].get<float>() : 1.0f;
        
        // Look up module by name
        auto module = registry->getModule(moduleName);
        if (module) {
            int sourceIndex = connectModule(module);
            if (sourceIndex >= 0) {
                setConnectionOpacity(static_cast<size_t>(sourceIndex), opacity);
                ofLogNotice("VideoMixer") << "Restored connection to " << moduleName 
                                          << " with opacity " << opacity;
            }
        } else {
            ofLogWarning("VideoMixer") << "Cannot restore connection: module not found: " << moduleName;
        }
    }
}

void VideoMixer::process(ofFbo& input, ofFbo& output) {
    if (!isEnabled()) {
        if (output.isAllocated()) {
            output.begin();
            ofClear(0, 0, 0, 0);
            output.end();
        }
        return;
    }

    // Delegate to underlying video mixer
    // Note: ofxVideoMixer ignores input and pulls from all connected inputs
    videoMixer_.process(input, output);
    
    // Copy result to our output FBO
    if (output.isAllocated()) {
        ensureOutputFbo(output.getWidth(), output.getHeight());
        outputFbo_ = output;
    }
}

int VideoMixer::connectModule(std::shared_ptr<Module> module) {
    if (!module) {
        ofLogError("VideoMixer") << "Cannot connect null module";
        return -1;
    }
    
    // Port-based: Check if module has video output port
    auto outputPorts = module->getOutputPorts();
    const Port* videoOutPort = nullptr;
    for (const auto& port : outputPorts) {
        if (port.type == PortType::VIDEO_OUT) {
            videoOutPort = &port;
            break;
        }
    }
    
    if (!videoOutPort || !videoOutPort->dataPtr) {
        ofLogWarning("VideoMixer") << "Module " << module->getName() << " does not have video output port";
        return -1;
    }
    
    // Check if already connected
    std::lock_guard<std::mutex> lock(connectionMutex_);
    for (size_t i = 0; i < connectedModules_.size(); i++) {
        if (auto existing = connectedModules_[i].lock()) {
            if (existing == module) {
                ofLogNotice("VideoMixer") << "Module " << module->getName() << " already connected";
                return static_cast<int>(i);
            }
        }
    }
    
    // Get video output from port dataPtr
    ofxVisualObject* videoOutput = static_cast<ofxVisualObject*>(videoOutPort->dataPtr);
    if (!videoOutput) {
        ofLogError("VideoMixer") << "Module " << module->getName() << " video output port has invalid dataPtr";
        return -1;
    }
    
    // Connect to video mixer
    videoMixer_.setInput(videoOutput);
    
    // Store module reference and default opacity
    connectedModules_.push_back(std::weak_ptr<Module>(module));
    sourceOpacities_.push_back(1.0f);
    
    // Set default opacity in video mixer
    size_t sourceIndex = connectedModules_.size() - 1;
    videoMixer_.setSourceOpacity(sourceIndex, 1.0f);
    
    ofLogNotice("VideoMixer") << "Connected module " << module->getName() 
                               << " at index " << sourceIndex;
    
    return static_cast<int>(sourceIndex);
}

void VideoMixer::disconnectModule(std::shared_ptr<Module> module) {
    if (!module) return;
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    for (size_t i = 0; i < connectedModules_.size(); i++) {
        if (auto existing = connectedModules_[i].lock()) {
            if (existing == module) {
                // Get video output from port and disconnect from video mixer
                auto outputPorts = module->getOutputPorts();
                for (const auto& port : outputPorts) {
                    if (port.type == PortType::VIDEO_OUT && port.dataPtr) {
                        auto* videoOutput = static_cast<ofxVisualObject*>(port.dataPtr);
                        if (videoOutput) {
                            videoMixer_.disconnectInput(videoOutput);
                            break;
                        }
                    }
                }
                
                // Remove from vectors
                connectedModules_.erase(connectedModules_.begin() + i);
                sourceOpacities_.erase(sourceOpacities_.begin() + i);
                
                ofLogNotice("VideoMixer") << "Disconnected module " << module->getName();
                return;
            }
        }
    }
}

void VideoMixer::disconnectModuleAtIndex(size_t sourceIndex) {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (sourceIndex >= connectedModules_.size()) {
        ofLogWarning("VideoMixer") << "Invalid source index: " << sourceIndex;
        return;
    }
    
    if (auto module = connectedModules_[sourceIndex].lock()) {
        // Get video output from port and disconnect from video mixer
        auto outputPorts = module->getOutputPorts();
        for (const auto& port : outputPorts) {
            if (port.type == PortType::VIDEO_OUT && port.dataPtr) {
                auto* videoOutput = static_cast<ofxVisualObject*>(port.dataPtr);
                if (videoOutput) {
                    videoMixer_.disconnectInput(videoOutput);
                    break;
                }
            }
        }
    }
    
    // Remove from vectors
    connectedModules_.erase(connectedModules_.begin() + sourceIndex);
    sourceOpacities_.erase(sourceOpacities_.begin() + sourceIndex);
    
    ofLogNotice("VideoMixer") << "Disconnected module at index " << sourceIndex;
}

size_t VideoMixer::getNumConnections() const {
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

bool VideoMixer::isConnectedTo(std::shared_ptr<Module> module) const {
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

int VideoMixer::getConnectionIndex(std::shared_ptr<Module> module) const {
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

void VideoMixer::setConnectionOpacity(size_t sourceIndex, float opacity) {
    opacity = ofClamp(opacity, 0.0f, 1.0f);
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (sourceIndex >= sourceOpacities_.size()) {
        ofLogWarning("VideoMixer") << "Invalid source index: " << sourceIndex;
        return;
    }
    
    sourceOpacities_[sourceIndex] = opacity;
    videoMixer_.setSourceOpacity(sourceIndex, opacity);
}

float VideoMixer::getConnectionOpacity(size_t sourceIndex) const {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (sourceIndex >= sourceOpacities_.size()) {
        return 0.0f;
    }
    return sourceOpacities_[sourceIndex];
}

void VideoMixer::ensureOutputFbo(int width, int height) {
    if (width <= 0 || height <= 0) {
        width = 1920;
        height = 1080;
    }
    
    if (!outputFbo_.isAllocated() || 
        outputFbo_.getWidth() != width || 
        outputFbo_.getHeight() != height) {
        
        ofFbo::Settings s;
        s.width = width;
        s.height = height;
        s.internalformat = GL_RGBA;
        s.useDepth = false;
        s.useStencil = false;
        s.textureTarget = GL_TEXTURE_2D;
        s.numSamples = 0;
        outputFbo_.allocate(s);
        
        ofLogVerbose("VideoMixer") << "Allocated output FBO: " << width << "x" << height;
    }
}

// Port-based routing interface (Phase 1)
std::vector<Port> VideoMixer::getInputPorts() const {
    std::vector<Port> ports;
    // Create 8 multi-connect video input ports
    for (size_t i = 0; i < 8; i++) {
        ports.push_back(Port(
            "video_in_" + std::to_string(i),
            PortType::VIDEO_IN,
            true,  // multi-connect enabled
            "Video Input " + std::to_string(i + 1),
            const_cast<void*>(static_cast<const void*>(&videoMixer_))
        ));
    }
    return ports;
}

std::vector<Port> VideoMixer::getOutputPorts() const {
    return {
        Port("video_out", PortType::VIDEO_OUT, false, "Video Output",
             const_cast<void*>(static_cast<const void*>(this)))
    };
}

//--------------------------------------------------------------
// Module Factory Registration
//--------------------------------------------------------------
namespace {
    struct VideoMixerRegistrar {
        VideoMixerRegistrar() {
            ModuleFactory::registerModuleType("VideoMixer", 
                []() -> std::shared_ptr<Module> {
                    return std::make_shared<VideoMixer>();
                });
        }
    };
    static VideoMixerRegistrar g_videoMixerRegistrar;
}
