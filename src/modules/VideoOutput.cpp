#include "VideoOutput.h"
#include <fstream>
#include <chrono>
#include "core/ModuleRegistry.h"
#include "core/ModuleFactory.h"
#include "ofLog.h"
#include "ofMain.h"
#include <algorithm>

VideoOutput::VideoOutput() 
    : masterOpacity_(1.0f) {
    // Initialize video mixer (mixes all connected sources)
    videoMixer_.setName("Video Mixer");
    videoMixer_.setMasterOpacity(1.0f);
    videoMixer_.setBlendMode(OF_BLENDMODE_ADD);
    videoMixer_.setAutoNormalize(true);
    
    // Initialize visual output (connects mixer to screen)
    visualOutput_.setName("Video Output");
    
    // Connect mixer to output internally
    videoMixer_.connectTo(visualOutput_);
    
    // Initialize viewport to window size if available, otherwise use defaults
    int windowWidth = ofGetWidth();
    int windowHeight = ofGetHeight();
    if (windowWidth > 0 && windowHeight > 0) {
        viewportWidth_ = windowWidth;
        viewportHeight_ = windowHeight;
    } else {
        // Default fallback - will be updated on first draw or window resize
        viewportWidth_ = 1920;
        viewportHeight_ = 1080;
    }
    
    // Initialize output FBO
    ensureOutputFbo();
}

VideoOutput::~VideoOutput() {
    // Disconnect all modules
    std::lock_guard<std::mutex> lock(connectionMutex_);
    connectedModules_.clear();
    sourceOpacities_.clear();
    sourceBlendModes_.clear();
}

std::string VideoOutput::getName() const {
    return "VideoOutput";
}

ModuleType VideoOutput::getType() const {
    return ModuleType::UTILITY;
}

std::vector<ParameterDescriptor> VideoOutput::getParametersImpl() const {
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
    // CRITICAL: Use actual connection index, not loop index, to match getParameter() behavior
    std::lock_guard<std::mutex> lock(connectionMutex_);
    for (size_t i = 0; i < connectedModules_.size(); i++) {
        // Only generate parameter if connection is still valid (not expired)
        // Use the actual index 'i' which matches sourceOpacities_[i]
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

void VideoOutput::onTrigger(TriggerEvent& event) {
    // Outputs don't receive triggers
}

void VideoOutput::setParameterImpl(const std::string& paramName, float value, bool notify) {
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
        // "connectionOpacity_" is 19 chars, so we need at least 20 chars for "connectionOpacity_0"
        const size_t prefixLength = 19; // "connectionOpacity_".length()
        if (paramName.length() <= prefixLength) {
            // Parameter name is too short - it's just "connectionOpacity_" without index
            ofLogWarning("VideoOutput") << "Invalid connection opacity parameter name (missing index): " << paramName << " (length: " << paramName.length() << ", need > " << prefixLength << ")";
            return;
        }
        
        std::string indexStr = paramName.substr(prefixLength);
        if (indexStr.empty()) {
            ofLogWarning("VideoOutput") << "Invalid connection opacity parameter name (missing index): " << paramName;
            return;
        }
        try {
            size_t index = std::stoul(indexStr);
            
            // #region agent log
            {
                std::lock_guard<std::mutex> lock(connectionMutex_);
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    size_t currentSize = sourceOpacities_.size();
                    size_t connectedSize = connectedModules_.size();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"VideoOutput.cpp:136\",\"message\":\"setParameter connectionOpacity\",\"data\":{\"paramName\":\"" << paramName << "\",\"index\":" << index << ",\"value\":" << value << ",\"sourceOpacitiesSize\":" << currentSize << ",\"connectedModulesSize\":" << connectedSize << "},\"timestamp\":" << now << "}\n";
                    logFile.close();
                    
                    // Validate index before calling setSourceOpacity (check while we have the lock)
                    if (index >= currentSize) {
                        ofLogWarning("VideoOutput") << "Invalid connection opacity index: " << index << " (max: " << (currentSize > 0 ? currentSize - 1 : 0) << ")";
                        
                        // #region agent log
                        {
                            std::ofstream logFile2("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                            if (logFile2.is_open()) {
                                auto now2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                                logFile2 << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"VideoOutput.cpp:151\",\"message\":\"setParameter - index out of bounds\",\"data\":{\"index\":" << index << ",\"sourceOpacitiesSize\":" << currentSize << "},\"timestamp\":" << now2 << "}\n";
                                logFile2.close();
                            }
                        }
                        // #endregion
                        
                        return;
                    }
                }
            }
            // #endregion
            
            // setSourceOpacity() already triggers parameterChangeCallback, so we don't need to call it again here
            setSourceOpacity(index, value);
            // Note: setSourceOpacity() always triggers parameterChangeCallback, so we don't need to call it again
            // even if notify is true, because setSourceOpacity() handles the notification
        } catch (const std::exception& e) {
            ofLogWarning("VideoOutput") << "Invalid connection opacity parameter name: " << paramName << " (" << e.what() << ")";
            
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"VideoOutput.cpp:168\",\"message\":\"setParameter - exception\",\"data\":{\"paramName\":\"" << paramName << "\",\"error\":\"" << e.what() << "\"},\"timestamp\":" << now << "}\n";
                    logFile.close();
                }
            }
            // #endregion
        }
    }
}

float VideoOutput::getParameterImpl(const std::string& paramName) const {
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
        // Extract connection index from parameter name
        // "connectionOpacity_" is 19 chars, so we need at least 20 chars for "connectionOpacity_0"
        const size_t prefixLength = 19; // "connectionOpacity_".length()
        if (paramName.length() <= prefixLength) {
            // Parameter name is too short - it's just "connectionOpacity_" without index
            ofLogWarning("VideoOutput") << "Invalid connection opacity parameter name (missing index): " << paramName << " (length: " << paramName.length() << ", need > " << prefixLength << ")";
            return 0.0f;
        }
        
        std::string indexStr = paramName.substr(prefixLength);
        if (indexStr.empty()) {
            ofLogWarning("VideoOutput") << "Invalid connection opacity parameter name (missing index): " << paramName;
            return 0.0f;
        }
        try {
            size_t index = std::stoul(indexStr);
            float opacity = getSourceOpacity(index);
            ofLogVerbose("VideoOutput") << "[OPACITY_READ] getParameter(" << paramName << ") = " << opacity << " (index: " << index << ")";
            return opacity;
        } catch (const std::exception& e) {
            ofLogWarning("VideoOutput") << "Invalid connection opacity parameter name: " << paramName << " (" << e.what() << ")";
            return 0.0f;
        }
    }
    // Unknown parameter - return default (base class default is 0.0f)
    // NOTE: Cannot call Module::getParameter() here as it would deadlock (lock already held)
    return 0.0f;
}

// Indexed parameter support for connection-based parameters
std::vector<std::pair<std::string, int>> VideoOutput::getIndexedParameterRanges() const {
    std::vector<std::pair<std::string, int>> ranges;
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    size_t numConnections = connectedModules_.size();
    
    // Count valid (non-expired) connections
    size_t validConnections = 0;
    for (size_t i = 0; i < numConnections; i++) {
        if (!connectedModules_[i].expired()) {
            validConnections = i + 1; // Update max index
        }
    }
    
    if (validConnections > 0) {
        // Return max index (validConnections - 1) since indices are 0-based
        ranges.push_back({"connectionOpacity", static_cast<int>(validConnections - 1)});
    }
    
    return ranges;
}

float VideoOutput::getIndexedParameter(const std::string& baseName, int index) const {
    if (baseName == "connectionOpacity") {
        if (index < 0) {
            return 0.0f;
        }
        return getSourceOpacity(static_cast<size_t>(index));
    }
    return 0.0f;
}

void VideoOutput::setIndexedParameter(const std::string& baseName, int index, float value, bool notify) {
    if (baseName == "connectionOpacity") {
        if (index < 0) {
            return;
        }
        setSourceOpacity(static_cast<size_t>(index), value);
        // Note: setSourceOpacity() already triggers parameterChangeCallback
    }
}

Module::ModuleMetadata VideoOutput::getMetadata() const {
    Module::ModuleMetadata metadata;
    metadata.typeName = "VideoOutput";
    metadata.eventNames = {};  // VideoOutput doesn't emit events
    metadata.parameterNames = {"masterOpacity", "blendMode", "autoNormalize"};
    metadata.parameterDisplayNames["masterOpacity"] = "Master Opacity";
    metadata.parameterDisplayNames["blendMode"] = "Blend Mode";
    metadata.parameterDisplayNames["autoNormalize"] = "Auto Normalize";
    return metadata;
}

ofJson VideoOutput::toJson(class ModuleRegistry* registry) const {
    ofJson json;
    json["type"] = "VideoOutput";
    json["name"] = getName();
    json["enabled"] = isEnabled();
    json["masterOpacity"] = getMasterOpacity();
    
    // Serialize blend mode
    ofBlendMode mode = getBlendMode();
    int modeIndex = 0;
    if (mode == OF_BLENDMODE_ADD) modeIndex = 0;
    else if (mode == OF_BLENDMODE_MULTIPLY) modeIndex = 1;
    else if (mode == OF_BLENDMODE_ALPHA) modeIndex = 2;
    json["blendMode"] = modeIndex;
    
    json["autoNormalize"] = getAutoNormalize();
    
    // Serialize connections - use UUIDs for reliability (consistent with router system)
    std::lock_guard<std::mutex> lock(connectionMutex_);
    ofJson connectionsJson = ofJson::array();
    for (size_t i = 0; i < connectedModules_.size(); i++) {
        if (auto module = connectedModules_[i].lock()) {
            ofJson connJson;
            
            // Registry may be null when called from getStateSnapshot() (via Engine::buildModuleStates)
            // In that case, we skip UUID/name serialization but still serialize opacity/blendMode
            if (registry) {
                std::string instanceName = registry->getName(module);
                std::string uuid = registry->getUUID(instanceName);
                if (!uuid.empty()) {
                    connJson["moduleUUID"] = uuid;
                }
                if (!instanceName.empty()) {
                    connJson["moduleName"] = instanceName;  // For readability
                }
            }
            
            float opacity = (i < sourceOpacities_.size()) ? sourceOpacities_[i] : 1.0f;
            connJson["opacity"] = opacity;
            
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"F\",\"location\":\"VideoOutput.cpp:292\",\"message\":\"serializing connection opacity\",\"data\":{\"index\":" << i << ",\"opacity\":" << opacity << ",\"sourceOpacitiesSize\":" << sourceOpacities_.size() << "},\"timestamp\":" << now << "}\n";
                    logFile.close();
                }
            }
            // #endregion
            
            ofBlendMode mode = (i < sourceBlendModes_.size()) ? sourceBlendModes_[i] : getBlendMode();
            int blendModeIndex = 0;
            if (mode == OF_BLENDMODE_ADD) blendModeIndex = 0;
            else if (mode == OF_BLENDMODE_MULTIPLY) blendModeIndex = 1;
            else if (mode == OF_BLENDMODE_ALPHA) blendModeIndex = 2;
            connJson["blendMode"] = blendModeIndex;
            connectionsJson.push_back(connJson);
        }
    }
    json["connections"] = connectionsJson;
    
    return json;
}

void VideoOutput::fromJson(const ofJson& json) {
    // Load enabled state
    if (json.contains("enabled")) {
        setEnabled(json["enabled"].get<bool>());
    }
    
    // Load master opacity
    if (json.contains("masterOpacity")) {
        setMasterOpacity(json["masterOpacity"].get<float>());
    }
    
    // Load blend mode
    if (json.contains("blendMode")) {
        int modeIndex = json["blendMode"].get<int>();
        ofBlendMode mode = OF_BLENDMODE_ADD;
        if (modeIndex == 0) mode = OF_BLENDMODE_ADD;
        else if (modeIndex == 1) mode = OF_BLENDMODE_MULTIPLY;
        else if (modeIndex == 2) mode = OF_BLENDMODE_ALPHA;
        setBlendMode(mode);
    }
    
    // Load auto-normalize
    if (json.contains("autoNormalize")) {
        setAutoNormalize(json["autoNormalize"].get<bool>());
    }
    
    // Note: Connections are restored by SessionManager via restoreConnections()
    // after all modules are loaded
}

void VideoOutput::restoreConnections(const ofJson& connectionsJson, ModuleRegistry* registry) {
    if (!registry || !connectionsJson.is_array()) {
        return;
    }
    
    // CRITICAL: Don't hold the lock here - setSourceOpacity() and getSourceOpacity() 
    // already acquire their own locks. Holding the lock here would cause a deadlock.
    size_t sourcesBefore = getNumConnections();
    ofLogNotice("VideoOutput") << "[RESTORE] restoreConnections() called with " 
                               << (connectionsJson.is_array() ? connectionsJson.size() : 0) << " connections"
                               << " (current sources: " << sourcesBefore << ")";
    
    // Restore parameters and track desired order in one pass
    // JSON array index = desired layer position (0 = bottom, last = top)
    std::vector<std::pair<std::shared_ptr<Module>, size_t>> orderMap; // (module, desiredIndex)
    
    for (size_t desiredIndex = 0; desiredIndex < connectionsJson.size(); desiredIndex++) {
        const auto& connJson = connectionsJson[desiredIndex];
        if (!connJson.is_object()) {
            ofLogWarning("VideoOutput") << "[RESTORE] Skipping invalid connection JSON";
            continue;
        }
        
        // Try UUID first, then instance name (both are UUID-based identifiers)
        std::string moduleIdentifier;
        if (connJson.contains("moduleUUID")) {
            moduleIdentifier = connJson["moduleUUID"].get<std::string>();
        } else if (connJson.contains("moduleName")) {
            moduleIdentifier = connJson["moduleName"].get<std::string>();
        } else {
            ofLogWarning("VideoOutput") << "[RESTORE] Connection JSON missing module identifier";
            continue;
        }
        
        // Find the connected module by UUID or name
        std::shared_ptr<Module> targetModule = registry->getModule(moduleIdentifier);
        if (!targetModule) {
            ofLogWarning("VideoOutput") << "[RESTORE] Module not found: " << moduleIdentifier;
            continue;
        }
        
        // Find the connection index for this module
        int sourceIndex = getConnectionIndex(targetModule);
        if (sourceIndex < 0) {
            ofLogWarning("VideoOutput") << "[RESTORE] Module " << moduleIdentifier 
                                       << " is not connected to this output";
            continue;
        }
        
        // Restore opacity and blend mode
        float opacity = connJson.contains("opacity") ? connJson["opacity"].get<float>() : 1.0f;
        int blendModeIndex = connJson.contains("blendMode") ? connJson["blendMode"].get<int>() : 0;
        
        ofBlendMode blendMode = OF_BLENDMODE_ADD;
        if (blendModeIndex == 0) blendMode = OF_BLENDMODE_ADD;
        else if (blendModeIndex == 1) blendMode = OF_BLENDMODE_MULTIPLY;
        else if (blendModeIndex == 2) blendMode = OF_BLENDMODE_ALPHA;
        
        setSourceOpacity(sourceIndex, opacity);
        setSourceBlendMode(sourceIndex, blendMode);
        
        // Track desired order for reordering pass
        orderMap.push_back({targetModule, desiredIndex});
    }
    
    // Restore connection order: move each connection to its desired position
    for (const auto& [targetModule, desiredIndex] : orderMap) {
        int currentIndex = getConnectionIndex(targetModule);
        if (currentIndex < 0 || currentIndex == static_cast<int>(desiredIndex)) continue;
        
        if (reorderSource(currentIndex, desiredIndex)) {
            std::string instanceName = registry->getName(targetModule);
            ofLogNotice("VideoOutput") << "[RESTORE] ✓ Reordered " << instanceName 
                                    << " from " << currentIndex << " to " << desiredIndex;
        }
    }
    
    size_t sourcesAfter = getNumConnections();
    ofLogNotice("VideoOutput") << "[RESTORE] After restore - sources: " << sourcesAfter;
}

void VideoOutput::process(ofFbo& input, ofFbo& output) {
    // Delegate to underlying video mixer
    // Note: ofxVideoMixer ignores input and pulls from all connected inputs
    videoMixer_.process(input, output);
    
    // Copy result to our output FBO
    if (output.isAllocated()) {
        ensureOutputFbo(output.getWidth(), output.getHeight());
        outputFbo_ = output;
    }
}

void VideoOutput::draw() {
    // Performance monitoring: Start frame timing
    float frameStartTime = ofGetElapsedTimef();
    
    // Update viewport to match window size (auto-adjust)
    int currentWidth = ofGetWidth();
    int currentHeight = ofGetHeight();
    
    // Ensure we have valid dimensions (use fallback if window not initialized)
    if (currentWidth <= 0) currentWidth = 1280;  // Safe fallback
    if (currentHeight <= 0) currentHeight = 720;  // Safe fallback
    
    // Always update viewport if dimensions changed or if not yet initialized
    // Use window size directly to ensure viewport matches window
    if (viewportWidth_ != currentWidth || viewportHeight_ != currentHeight || 
        viewportWidth_ <= 0 || viewportHeight_ <= 0) {
        viewportWidth_ = currentWidth;
        viewportHeight_ = currentHeight;
        ensureOutputFbo(viewportWidth_, viewportHeight_);
        ofLogNotice("VideoOutput") << "Viewport updated to: " << viewportWidth_ << "x" << viewportHeight_;
    }
    
    // Process video chain: pull from connected sources via mixer
    size_t numConnections = videoMixer_.getNumConnections();
    if (numConnections > 0) {
        // Ensure output FBO matches viewport dimensions BEFORE processing
        ensureOutputFbo(viewportWidth_, viewportHeight_);
        
        // Performance monitoring: Time mixer processing
        float mixerStartTime = ofGetElapsedTimef();
        
        // OPTIMIZATION: Process mixer directly into outputFbo_ (eliminates temporary FBO allocation
        // and unnecessary visualOutput_ pass-through copy)
        videoMixer_.process(inputFbo_, outputFbo_);
        
        float mixerTime = (ofGetElapsedTimef() - mixerStartTime) * 1000.0f; // Convert to ms
        
        // Performance monitoring: Time drawing
        float drawStartTime = ofGetElapsedTimef();
        
        // Draw to screen
        if (outputFbo_.isAllocated()) {
            ofSetColor(255, 255, 255, 255);
            outputFbo_.draw(0, 0, currentWidth, currentHeight);
        }
        
        float drawTime = (ofGetElapsedTimef() - drawStartTime) * 1000.0f; // Convert to ms
        
        // Calculate total frame time
        float frameTime = (ofGetElapsedTimef() - frameStartTime) * 1000.0f; // Convert to ms
        lastFrameTime_ = frameTime;
        
        // Accumulate for FPS calculation
        frameTimeAccumulator_ += frameTime;
        frameCount_++;
        
        // Log performance stats periodically
        float currentTime = ofGetElapsedTimef();
        if (currentTime - lastFpsLogTime_ >= FPS_LOG_INTERVAL) {
            if (frameCount_ > 0) {
                float avgFrameTime = frameTimeAccumulator_ / frameCount_;
                float avgFps = 1000.0f / avgFrameTime;
                float currentFps = 1000.0f / frameTime;
                
                ofLogNotice("VideoOutput") << "[PERF] FPS: " << std::fixed << std::setprecision(1) << currentFps
                                          << " (avg: " << avgFps << ")"
                                          << " | Frame: " << std::setprecision(2) << frameTime << "ms"
                                          << " (mixer: " << mixerTime << "ms, draw: " << drawTime << "ms)"
                                          << " | Connections: " << numConnections;
                
                // Reset accumulator
                frameTimeAccumulator_ = 0.0f;
                frameCount_ = 0;
                lastFpsLogTime_ = currentTime;
            }
        }
        
        // Log slow frames (warn if frame takes > 20ms, which is < 50fps)
        if (frameTime > 20.0f) {
            ofLogWarning("VideoOutput") << "[PERF] Slow frame detected: " << std::fixed << std::setprecision(2) 
                                       << frameTime << "ms (mixer: " << mixerTime << "ms, draw: " << drawTime << "ms)";
        }
    } else {
        // No connections - clear FBO to black to prevent grey buffer artifacts
        ensureOutputFbo(viewportWidth_, viewportHeight_);
        outputFbo_.begin();
        ofClear(0, 0, 0, 255);  // Clear to black
        outputFbo_.end();
        
        // Draw black screen
        if (outputFbo_.isAllocated()) {
            ofSetColor(255, 255, 255, 255);
            outputFbo_.draw(0, 0, currentWidth, currentHeight);
        }
        
        // Track frame time
        float frameTime = (ofGetElapsedTimef() - frameStartTime) * 1000.0f;
        lastFrameTime_ = frameTime;
    }
}

void VideoOutput::handleWindowResize(int width, int height) {
    if (width > 0 && height > 0) {
        viewportWidth_ = width;
        viewportHeight_ = height;
        ensureOutputFbo(viewportWidth_, viewportHeight_);
        ofLogVerbose("VideoOutput") << "Viewport adjusted to: " << width << "x" << height;
    }
}

// Connection management methods (from VideoMixer)

int VideoOutput::connectModule(std::shared_ptr<Module> module) {
    if (!module) {
        ofLogError("VideoOutput") << "Cannot connect null module";
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
        ofLogWarning("VideoOutput") << "Module " << module->getName() << " does not have video output port";
        return -1;
    }
    
    // Check if already connected
    std::lock_guard<std::mutex> lock(connectionMutex_);
    for (size_t i = 0; i < connectedModules_.size(); i++) {
        if (auto existing = connectedModules_[i].lock()) {
            if (existing == module) {
                ofLogNotice("VideoOutput") << "Module " << module->getName() << " already connected";
                return static_cast<int>(i);
            }
        }
    }
    
    // Get video output from port dataPtr
    ofxVisualObject* videoOutput = static_cast<ofxVisualObject*>(videoOutPort->dataPtr);
    if (!videoOutput) {
        ofLogError("VideoOutput") << "Module " << module->getName() << " video output port has invalid dataPtr";
        return -1;
    }
    
    // Connect to video mixer
    videoMixer_.setInput(videoOutput);
    
    // Store module reference and default opacity
    connectedModules_.push_back(std::weak_ptr<Module>(module));
    sourceOpacities_.push_back(1.0f);
    sourceBlendModes_.push_back(OF_BLENDMODE_ADD); // Default blend mode
    
    // Set default opacity and blend mode in video mixer
    size_t sourceIndex = connectedModules_.size() - 1;
    videoMixer_.setSourceOpacity(sourceIndex, 1.0f);
    videoMixer_.setSourceBlendMode(sourceIndex, OF_BLENDMODE_ADD);
    
    ofLogNotice("VideoOutput") << "Connected module " << module->getName() 
                               << " at index " << sourceIndex;
    
    return static_cast<int>(sourceIndex);
}

void VideoOutput::disconnectModule(std::shared_ptr<Module> module) {
    if (!module) return;
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    for (size_t i = 0; i < connectedModules_.size(); i++) {
        if (auto existing = connectedModules_[i].lock()) {
            if (existing == module) {
                // Get video output from port and disconnect from video mixer
                // Wrap in try-catch to handle cases where module is partially destroyed
                try {
                    auto outputPorts = module->getOutputPorts();
                    for (const auto& port : outputPorts) {
                        if (port.type == PortType::VIDEO_OUT && port.dataPtr) {
                            auto* videoOutput = static_cast<ofxVisualObject*>(port.dataPtr);
                            if (videoOutput) {
                                // Wrap disconnectInput in try-catch as it might fail if module is being destroyed
                                try {
                                    videoMixer_.disconnectInput(videoOutput);
                                } catch (const std::exception& e) {
                                    ofLogWarning("VideoOutput") << "Error during video disconnect: " << e.what();
                                } catch (...) {
                                    ofLogWarning("VideoOutput") << "Unknown error during video disconnect";
                                }
                                break;
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    ofLogWarning("VideoOutput") << "Error getting video output for disconnection: " << e.what();
                } catch (...) {
                    ofLogWarning("VideoOutput") << "Unknown error getting video output for disconnection";
                }
                
                // Remove from vectors (do this even if disconnect failed)
                connectedModules_.erase(connectedModules_.begin() + i);
                sourceOpacities_.erase(sourceOpacities_.begin() + i);
                sourceBlendModes_.erase(sourceBlendModes_.begin() + i);
                
                // Safely get module name (module might be partially destroyed)
                try {
                    std::string moduleName = module ? module->getName() : "unknown";
                    ofLogNotice("VideoOutput") << "Disconnected module " << moduleName;
                } catch (...) {
                    ofLogNotice("VideoOutput") << "Disconnected module (name unavailable)";
                }
                return;
            }
        }
    }
}

void VideoOutput::disconnectModule(size_t sourceIndex) {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (sourceIndex >= connectedModules_.size()) {
        ofLogWarning("VideoOutput") << "Invalid source index: " << sourceIndex;
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
    sourceBlendModes_.erase(sourceBlendModes_.begin() + sourceIndex);
    
    ofLogNotice("VideoOutput") << "Disconnected module at index " << sourceIndex;
}

size_t VideoOutput::getNumConnections() const {
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

bool VideoOutput::isConnectedTo(std::shared_ptr<Module> module) const {
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

int VideoOutput::getConnectionIndex(std::shared_ptr<Module> module) const {
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

void VideoOutput::setSourceOpacity(size_t sourceIndex, float opacity) {
    opacity = ofClamp(opacity, 0.0f, 1.0f);
    
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"G\",\"location\":\"VideoOutput.cpp:735\",\"message\":\"setSourceOpacity called\",\"data\":{\"sourceIndex\":" << sourceIndex << ",\"opacity\":" << opacity << "},\"timestamp\":" << now << "}\n";
            logFile.close();
        }
    }
    // #endregion
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (sourceIndex >= sourceOpacities_.size()) {
        ofLogWarning("VideoOutput") << "Invalid source index: " << sourceIndex;
        return;
    }
    
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            float oldValue = sourceOpacities_[sourceIndex];
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"G\",\"location\":\"VideoOutput.cpp:744\",\"message\":\"updating sourceOpacities_\",\"data\":{\"sourceIndex\":" << sourceIndex << ",\"oldValue\":" << oldValue << ",\"newValue\":" << opacity << ",\"sourceOpacitiesSize\":" << sourceOpacities_.size() << "},\"timestamp\":" << now << "}\n";
            logFile.close();
        }
    }
    // #endregion
    
    sourceOpacities_[sourceIndex] = opacity;
    videoMixer_.setSourceOpacity(sourceIndex, opacity);
    
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            std::string paramName = "connectionOpacity_" + std::to_string(sourceIndex);
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"D\",\"location\":\"VideoOutput.cpp:767\",\"message\":\"setSourceOpacity - before callback\",\"data\":{\"sourceIndex\":" << sourceIndex << ",\"opacity\":" << opacity << ",\"paramName\":\"" << paramName << "\",\"hasCallback\":" << (parameterChangeCallback ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
            logFile.close();
        }
    }
    // #endregion
    
    // Trigger parameter change callback to notify Engine for script sync
    // This ensures state changes from GUI are captured in script generation
    if (parameterChangeCallback) {
        std::string paramName = "connectionOpacity_" + std::to_string(sourceIndex);
        ofLogNotice("VideoOutput") << "[OPACITY_SYNC] setSourceOpacity(" << sourceIndex << ") = " << opacity 
                                   << ", triggering callback for " << paramName;
        parameterChangeCallback(paramName, opacity);
        
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"D\",\"location\":\"VideoOutput.cpp:776\",\"message\":\"setSourceOpacity - callback triggered\",\"data\":{\"paramName\":\"" << paramName << "\",\"opacity\":" << opacity << "},\"timestamp\":" << now << "}\n";
                logFile.close();
            }
        }
        // #endregion
    } else {
        ofLogWarning("VideoOutput") << "[OPACITY_SYNC] setSourceOpacity(" << sourceIndex << ") = " << opacity 
                                    << ", but parameterChangeCallback is not set!";
    }
}

float VideoOutput::getSourceOpacity(size_t sourceIndex) const {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (sourceIndex >= sourceOpacities_.size()) {
        return 0.0f;
    }
    return sourceOpacities_[sourceIndex];
}

void VideoOutput::setSourceBlendMode(size_t sourceIndex, ofBlendMode mode) {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (sourceIndex >= sourceBlendModes_.size()) {
        ofLogWarning("VideoOutput") << "Invalid source index: " << sourceIndex;
        return;
    }
    
    sourceBlendModes_[sourceIndex] = mode;
    videoMixer_.setSourceBlendMode(sourceIndex, mode);
}

ofBlendMode VideoOutput::getSourceBlendMode(size_t sourceIndex) const {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (sourceIndex >= sourceBlendModes_.size()) {
        return getBlendMode(); // Return global blend mode as fallback
    }
    return sourceBlendModes_[sourceIndex];
}

bool VideoOutput::reorderSource(size_t fromIndex, size_t toIndex) {
    if (fromIndex == toIndex) return true; // No-op
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    
    // Validate indices
    if (fromIndex >= connectedModules_.size() || toIndex >= connectedModules_.size()) {
        ofLogWarning("VideoOutput") << "Invalid indices for reorder: " << fromIndex << " -> " << toIndex;
        return false;
    }
    
    // Swap elements in all parallel vectors
    std::swap(connectedModules_[fromIndex], connectedModules_[toIndex]);
    std::swap(sourceOpacities_[fromIndex], sourceOpacities_[toIndex]);
    if (fromIndex < sourceBlendModes_.size() && toIndex < sourceBlendModes_.size()) {
        std::swap(sourceBlendModes_[fromIndex], sourceBlendModes_[toIndex]);
    }
    
    // Also reorder in underlying video mixer (indices should match)
    videoMixer_.reorderConnection(fromIndex, toIndex);
    
    ofLogNotice("VideoOutput") << "Reordered source " << fromIndex << " -> " << toIndex;
    return true;
}

std::shared_ptr<Module> VideoOutput::getSourceModule(size_t sourceIndex) const {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    
    if (sourceIndex >= connectedModules_.size()) {
        return nullptr;
    }
    
    // Try to lock the weak_ptr
    return connectedModules_[sourceIndex].lock();
}

void VideoOutput::setMasterOpacity(float opacity) {
    opacity = ofClamp(opacity, 0.0f, 1.0f);
    masterOpacity_ = opacity;
    videoMixer_.setMasterOpacity(opacity);
}

float VideoOutput::getMasterOpacity() const {
    return masterOpacity_;
}

void VideoOutput::setBlendMode(ofBlendMode mode) {
    videoMixer_.setBlendMode(mode);
}

ofBlendMode VideoOutput::getBlendMode() const {
    return videoMixer_.getBlendMode();
}

void VideoOutput::setAutoNormalize(bool enabled) {
    videoMixer_.setAutoNormalize(enabled);
}

bool VideoOutput::getAutoNormalize() const {
    return videoMixer_.getAutoNormalize();
}

// Helper methods

void VideoOutput::ensureOutputFbo(int width, int height) {
    if (width <= 0 || height <= 0) {
        width = viewportWidth_ > 0 ? viewportWidth_ : 1920;
        height = viewportHeight_ > 0 ? viewportHeight_ : 1080;
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
        
        ofLogVerbose("VideoOutput") << "Allocated output FBO: " << width << "x" << height;
    }
    
    // Also ensure input FBO is allocated
    if (!inputFbo_.isAllocated() || 
        inputFbo_.getWidth() != width || 
        inputFbo_.getHeight() != height) {
        
        ofFbo::Settings s;
        s.width = width;
        s.height = height;
        s.internalformat = GL_RGBA;
        s.useDepth = false;
        s.useStencil = false;
        s.textureTarget = GL_TEXTURE_2D;
        s.numSamples = 0;
        inputFbo_.allocate(s);
    }
}

//--------------------------------------------------------------
// Port-based routing interface (Phase 1)
std::vector<Port> VideoOutput::getInputPorts() const {
    std::vector<Port> ports;
    // Create 8 multi-connect video input ports (VideoOutput is a sink)
    for (size_t i = 0; i < 8; i++) {
        ports.push_back(Port(
            "video_in_" + std::to_string(i),
            PortType::VIDEO_IN,
            true,  // multi-connect enabled
            "Video Input " + std::to_string(i + 1),
            const_cast<void*>(static_cast<const void*>(this))
        ));
    }
    return ports;
}

std::vector<Port> VideoOutput::getOutputPorts() const {
    // VideoOutput is a sink, no outputs
    return {};
}

//--------------------------------------------------------------
// Module Factory Registration
//--------------------------------------------------------------
namespace {
    struct VideoOutputRegistrar {
        VideoOutputRegistrar() {
            ModuleFactory::registerModuleType("VideoOutput", 
                []() -> std::shared_ptr<Module> {
                    return std::make_shared<VideoOutput>();
                });
        }
    };
    static VideoOutputRegistrar g_videoOutputRegistrar;
}

