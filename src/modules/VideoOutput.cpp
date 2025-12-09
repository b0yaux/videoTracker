#include "VideoOutput.h"
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

std::vector<ParameterDescriptor> VideoOutput::getParameters() const {
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

void VideoOutput::onTrigger(TriggerEvent& event) {
    // Outputs don't receive triggers
}

void VideoOutput::setParameter(const std::string& paramName, float value, bool notify) {
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
        setSourceOpacity(index, value);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback(paramName, value);
        }
    }
}

float VideoOutput::getParameter(const std::string& paramName) const {
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
        size_t index = std::stoul(paramName.substr(19)); // "connectionOpacity_".length() == 19
        return getSourceOpacity(index);
    }
    // Unknown parameter - return default
    return Module::getParameter(paramName);
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

ofJson VideoOutput::toJson() const {
    ofJson json;
    json["type"] = "VideoOutput";
    json["name"] = getName();
    json["masterOpacity"] = getMasterOpacity();
    
    // Serialize blend mode
    ofBlendMode mode = getBlendMode();
    int modeIndex = 0;
    if (mode == OF_BLENDMODE_ADD) modeIndex = 0;
    else if (mode == OF_BLENDMODE_MULTIPLY) modeIndex = 1;
    else if (mode == OF_BLENDMODE_ALPHA) modeIndex = 2;
    json["blendMode"] = modeIndex;
    
    json["autoNormalize"] = getAutoNormalize();
    
    // Serialize connections
    std::lock_guard<std::mutex> lock(connectionMutex_);
    ofJson connectionsJson = ofJson::array();
    for (size_t i = 0; i < connectedModules_.size(); i++) {
        if (auto module = connectedModules_[i].lock()) {
            ofJson connJson;
            connJson["moduleName"] = module->getName();
            connJson["opacity"] = (i < sourceOpacities_.size()) ? sourceOpacities_[i] : 1.0f;
            
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
    
    // CRITICAL: Match connections by finding the actual connected module
    // We saved type names ("MediaPool") but need to match to instance names ("mediaPool1", "mediaPool2")
    // ConnectionManager connects modules in the same order as saved, so we can match by index
    // BUT we need to verify the match is correct by checking the module type
    
    size_t sourceIndex = 0;
    for (const auto& connJson : connectionsJson) {
        if (!connJson.is_object() || !connJson.contains("moduleName")) {
            ofLogWarning("VideoOutput") << "[RESTORE] Skipping invalid connection JSON";
            continue;
        }
        
        std::string savedModuleName = connJson["moduleName"].get<std::string>(); // Type name like "MediaPool"
        float opacity = connJson.contains("opacity") ? connJson["opacity"].get<float>() : 1.0f;
        int blendModeIndex = connJson.contains("blendMode") ? connJson["blendMode"].get<int>() : 0;
        
        ofBlendMode blendMode = OF_BLENDMODE_ADD;
        if (blendModeIndex == 0) blendMode = OF_BLENDMODE_ADD;
        else if (blendModeIndex == 1) blendMode = OF_BLENDMODE_MULTIPLY;
        else if (blendModeIndex == 2) blendMode = OF_BLENDMODE_ALPHA;
        
        // Match by index - ConnectionManager connects in the same order as saved
        // Verify the match by checking if the connected module's type matches the saved type name
        // Use getSourceModule() which handles locking internally
        if (auto module = getSourceModule(sourceIndex)) {
            std::string connectedModuleType = module->getName(); // This returns type name
            
            // Verify type matches
            bool typeMatches = (savedModuleName == connectedModuleType);
            
            if (typeMatches) {
                setSourceOpacity(sourceIndex, opacity); // This method handles its own locking
                setSourceBlendMode(sourceIndex, blendMode); // This method handles its own locking
                
                // Verify values were set correctly
                float restoredOpacity = getSourceOpacity(sourceIndex); // This method handles its own locking
                ofBlendMode restoredBlendMode = getSourceBlendMode(sourceIndex); // This method handles its own locking
                
                // Get instance name from registry for logging
                std::string instanceName = registry ? registry->getName(module) : "";
                ofLogNotice("VideoOutput") << "[RESTORE] ✓ Restored opacity " << opacity 
                                        << " (verified: " << restoredOpacity << ")"
                                        << " and blend mode " << blendModeIndex 
                                        << " (verified: " << (int)restoredBlendMode << ")"
                                        << " for connection " << sourceIndex 
                                        << " (" << instanceName << ", type: " << savedModuleName << ")";
            } else {
                ofLogWarning("VideoOutput") << "[RESTORE] Type mismatch at index " << sourceIndex 
                                           << ": saved '" << savedModuleName 
                                           << "' but found '" << connectedModuleType << "' - skipping";
            }
        } else {
            ofLogWarning("VideoOutput") << "[RESTORE] Connection " << sourceIndex 
                                       << " not found or expired";
        }
        
        sourceIndex++;
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
        // No connections - still track frame time
        float frameTime = (ofGetElapsedTimef() - frameStartTime) * 1000.0f;
        lastFrameTime_ = frameTime;
    }
    // No input connected - screen already cleared by ofApp::draw()
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
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (sourceIndex >= sourceOpacities_.size()) {
        ofLogWarning("VideoOutput") << "Invalid source index: " << sourceIndex;
        return;
    }
    
    sourceOpacities_[sourceIndex] = opacity;
    videoMixer_.setSourceOpacity(sourceIndex, opacity);
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

