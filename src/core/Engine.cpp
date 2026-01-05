#include "Engine.h"
#include "gui/GUIManager.h"
#include "gui/ViewManager.h"
#include "ofLog.h"
#include "ofFileUtils.h"
#include <algorithm>

namespace vt {

Engine::Engine() 
    : assetLibrary_(&projectManager_, &mediaConverter_, &moduleRegistry_) {
}

Engine::~Engine() {
    // Cleanup handled by member destructors
}

void Engine::setup(const EngineConfig& config) {
    if (isSetup_) {
        ofLogWarning("Engine") << "Engine already setup, skipping";
        return;
    }
    
    config_ = config;
    
    // Setup Clock first (foundation for timing)
    clock_.setup();
    
    // Setup PatternRuntime (foundational system for patterns)
    patternRuntime_.setup(&clock_);
    
    // Setup core systems
    setupCoreSystems();
    
    // Setup master outputs
    setupMasterOutputs();
    
    // Setup command executor
    setupCommandExecutor();
    
    // Initialize project and session
    initializeProjectAndSession();
    
    isSetup_ = true;
    ofLogNotice("Engine") << "Engine setup complete";
}

void Engine::setupCoreSystems() {
    // Setup core systems - CRITICAL: ParameterRouter needs registry BEFORE SessionManager uses it
    parameterRouter_.setRegistry(&moduleRegistry_);
    connectionManager_.setRegistry(&moduleRegistry_);
    connectionManager_.setParameterRouter(&parameterRouter_);
    connectionManager_.setPatternRuntime(&patternRuntime_);  // Enable PatternRuntime access for module initialization
    
    // Initialize SessionManager with dependencies
    sessionManager_ = SessionManager(
        &projectManager_,
        &clock_,
        &moduleRegistry_,
        &moduleFactory_,
        &parameterRouter_,
        &connectionManager_,
        nullptr  // ViewManager set later via setupGUIManagers
    );
    sessionManager_.setConnectionManager(&connectionManager_);
    sessionManager_.setPatternRuntime(&patternRuntime_);  // Phase 2: Enable pattern migration
}

void Engine::setupMasterOutputs() {
    // Create master outputs using ModuleFactory (system modules)
    moduleFactory_.ensureSystemModules(&moduleRegistry_, config_.masterAudioOutName, config_.masterVideoOutName);
    
    // Get master outputs from registry
    masterAudioOut_ = std::dynamic_pointer_cast<AudioOutput>(moduleRegistry_.getModule(config_.masterAudioOutName));
    masterVideoOut_ = std::dynamic_pointer_cast<VideoOutput>(moduleRegistry_.getModule(config_.masterVideoOutName));
    
    if (!masterAudioOut_ || !masterVideoOut_) {
        ofLogError("Engine") << "Failed to create master outputs";
        return;
    }
    
    // Initialize master outputs
    masterAudioOut_->initialize(&clock_, &moduleRegistry_, &connectionManager_, &parameterRouter_, &patternRuntime_, false);
    masterVideoOut_->initialize(&clock_, &moduleRegistry_, &connectionManager_, &parameterRouter_, &patternRuntime_, false);
    
    // Initialize master oscilloscope and spectrogram if they exist
    auto masterOscilloscope = moduleRegistry_.getModule("masterOscilloscope");
    auto masterSpectrogram = moduleRegistry_.getModule("masterSpectrogram");
    
    if (masterOscilloscope) {
        masterOscilloscope->initialize(&clock_, &moduleRegistry_, &connectionManager_, &parameterRouter_, &patternRuntime_, false);
    }
    if (masterSpectrogram) {
        masterSpectrogram->initialize(&clock_, &moduleRegistry_, &connectionManager_, &parameterRouter_, &patternRuntime_, false);
    }
    
    // Setup default connections for master outputs
    connectionManager_.setupDefaultConnections(&clock_, config_.masterAudioOutName, config_.masterVideoOutName);
}

void Engine::setupCommandExecutor() {
    // Setup CommandExecutor with dependencies
    // GUIManager will be set later via setupGUIManagers
    commandExecutor_.setup(&moduleRegistry_, nullptr, &connectionManager_, &assetLibrary_, &clock_, &patternRuntime_);
    
    // Set callbacks for module operations
    // These will be updated when GUIManager is available
    commandExecutor_.setOnAddModule([this](const std::string& moduleType) {
        moduleRegistry_.addModule(
            moduleFactory_,
            moduleType,
            &clock_,
            &connectionManager_,
            &parameterRouter_,
            &patternRuntime_,
            nullptr,  // GUIManager set later via setupGUIManagers
            config_.masterAudioOutName,
            config_.masterVideoOutName
        );
        notifyStateChange();
    });
    
    commandExecutor_.setOnRemoveModule([this](const std::string& instanceName) {
        moduleRegistry_.removeModule(
            instanceName,
            &connectionManager_,
            nullptr,  // GUIManager set later via setupGUIManagers
            config_.masterAudioOutName,
            config_.masterVideoOutName
        );
        notifyStateChange();
    });
}

void Engine::initializeProjectAndSession() {
    // Initialize project and session (opens/creates project, loads default session)
    std::string dataPath = ofToDataPath("", true);
    bool sessionLoaded = sessionManager_.initializeProjectAndSession(dataPath);
    
    if (onProjectOpened_ && projectManager_.isProjectOpen()) {
        onProjectOpened_();
    }
    
    if (!sessionLoaded) {
        // If no session was loaded, ensure default modules exist
        sessionManager_.ensureDefaultModules({"TrackerSequencer", "MultiSampler"});
    }
    
    // CRITICAL: Ensure system modules exist after session load
    moduleFactory_.ensureSystemModules(&moduleRegistry_, config_.masterAudioOutName, config_.masterVideoOutName);
    
    // CRITICAL: Refresh master outputs after session load
    masterAudioOut_ = std::dynamic_pointer_cast<AudioOutput>(moduleRegistry_.getModule(config_.masterAudioOutName));
    masterVideoOut_ = std::dynamic_pointer_cast<VideoOutput>(moduleRegistry_.getModule(config_.masterVideoOutName));
    
    if (!masterAudioOut_ || !masterVideoOut_) {
        ofLogError("Engine") << "Failed to refresh master outputs after session load";
        return;
    }
    
    // Re-initialize master outputs after session load
    masterAudioOut_->initialize(&clock_, &moduleRegistry_, &connectionManager_, &parameterRouter_, &patternRuntime_, true);
    masterVideoOut_->initialize(&clock_, &moduleRegistry_, &connectionManager_, &parameterRouter_, &patternRuntime_, true);
    
    // Initialize master oscilloscope and spectrogram if they exist
    auto masterOscilloscope = moduleRegistry_.getModule("masterOscilloscope");
    auto masterSpectrogram = moduleRegistry_.getModule("masterSpectrogram");
    
    if (masterOscilloscope) {
        masterOscilloscope->initialize(&clock_, &moduleRegistry_, &connectionManager_, &parameterRouter_, &patternRuntime_, true);
    }
    if (masterSpectrogram) {
        masterSpectrogram->initialize(&clock_, &moduleRegistry_, &connectionManager_, &parameterRouter_, &patternRuntime_, true);
    }
    
    // Setup automatic routing for master oscilloscope and spectrogram
    if (masterOscilloscope) {
        connectionManager_.connectAudio(config_.masterAudioOutName, "masterOscilloscope");
        connectionManager_.connectVideo("masterOscilloscope", config_.masterVideoOutName);
    }
    
    if (masterSpectrogram) {
        connectionManager_.connectAudio(config_.masterAudioOutName, "masterSpectrogram");
        connectionManager_.connectVideo("masterSpectrogram", config_.masterVideoOutName);
    }
    
    // Enable auto-save if configured
    if (config_.enableAutoSave) {
        sessionManager_.enableAutoSave(config_.autoSaveInterval, onUpdateWindowTitle_);
    }
    
    notifyStateChange();
}

void Engine::setupAudio(int sampleRate, int bufferSize) {
    if (masterAudioOut_) {
        // AudioOutput manages its own soundStream internally
        // This is a placeholder for future audio setup if needed
    }
}

void Engine::setupGUIManagers(GUIManager* guiManager, ViewManager* viewManager) {
    guiManager_ = guiManager;
    
    if (guiManager) {
        guiManager->setRegistry(&moduleRegistry_);
        guiManager->setParameterRouter(&parameterRouter_);
        guiManager->setConnectionManager(&connectionManager_);
        sessionManager_.setGUIManager(guiManager);
        
        // Update CommandExecutor with GUIManager
        commandExecutor_.setup(&moduleRegistry_, guiManager, &connectionManager_, &assetLibrary_, &clock_, &patternRuntime_);
        
        // Update callbacks to include GUIManager
        commandExecutor_.setOnAddModule([this, guiManager](const std::string& moduleType) {
            moduleRegistry_.addModule(
                moduleFactory_,
                moduleType,
                &clock_,
                &connectionManager_,
                &parameterRouter_,
                &patternRuntime_,
                guiManager,
                config_.masterAudioOutName,
                config_.masterVideoOutName
            );
            notifyStateChange();
        });
        
        commandExecutor_.setOnRemoveModule([this, guiManager](const std::string& instanceName) {
            moduleRegistry_.removeModule(
                instanceName,
                &connectionManager_,
                guiManager,
                config_.masterAudioOutName,
                config_.masterVideoOutName
            );
            notifyStateChange();
        });
    }
    
    if (viewManager) {
        sessionManager_.setViewManager(viewManager);
    }
    
    // Setup GUI coordination (initializes all modules)
    if (guiManager) {
        sessionManager_.setupGUI(guiManager);
    }
}

Engine::Result Engine::executeCommand(const std::string& command) {
    try {
        // Capture output from CommandExecutor
        std::string capturedOutput;
        
        // Set up output callback to capture messages
        auto oldCallback = commandExecutor_.getOutputCallback();
        commandExecutor_.setOutputCallback([&capturedOutput](const std::string& msg) {
            if (!capturedOutput.empty()) {
                capturedOutput += "\n";
            }
            capturedOutput += msg;
        });
        
        // Execute command
        commandExecutor_.executeCommand(command);
        
        // Restore old callback
        commandExecutor_.setOutputCallback(oldCallback);
        
        // Notify state change after command execution
        notifyStateChange();
        
        // Return captured output (or success message if no output)
        if (!capturedOutput.empty()) {
            // Remove the command echo ("> command") from output
            std::string output = capturedOutput;
            size_t firstNewline = output.find('\n');
            if (firstNewline != std::string::npos && output.find("> ") == 0) {
                output = output.substr(firstNewline + 1);
            }
            return Result(true, output);
        }
        
        return Result(true, "Command executed successfully");
    } catch (const std::exception& e) {
        return Result(false, "Command execution failed", e.what());
    } catch (...) {
        return Result(false, "Command execution failed", "Unknown error");
    }
}

Engine::Result Engine::eval(const std::string& script) {
    // For now, treat script as command
    // Future: Parse as Lua or mini-notation
    return executeCommand(script);
}

Engine::Result Engine::evalFile(const std::string& path) {
    ofFile file(path);
    if (!file.exists()) {
        return Result(false, "File not found", path);
    }
    
    std::string content = file.readToBuffer().getText();
    return eval(content);
}

EngineState Engine::getState() const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    return buildStateSnapshot();
}

EngineState::ModuleState Engine::getModuleState(const std::string& name) const {
    auto state = getState();
    auto it = state.modules.find(name);
    if (it != state.modules.end()) {
        return it->second;
    }
    return EngineState::ModuleState();
}

size_t Engine::subscribe(StateObserver callback) {
    std::unique_lock<std::shared_mutex> lock(stateMutex_);
    size_t id = nextObserverId_.fetch_add(1);
    observers_.emplace_back(id, callback);
    return id;
}

void Engine::unsubscribe(size_t id) {
    std::unique_lock<std::shared_mutex> lock(stateMutex_);
    observers_.erase(
        std::remove_if(observers_.begin(), observers_.end(),
            [id](const auto& pair) { return pair.first == id; }),
        observers_.end()
    );
}

void Engine::notifyStateChange() {
    EngineState state = buildStateSnapshot();
    
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    for (const auto& [id, observer] : observers_) {
        try {
            observer(state);
        } catch (const std::exception& e) {
            ofLogError("Engine") << "Error in state observer: " << e.what();
        }
    }
}

EngineState Engine::buildStateSnapshot() const {
    EngineState state;
    
    buildTransportState(state);
    buildModuleStates(state);
    buildConnectionStates(state);
    
    return state;
}

void Engine::buildTransportState(EngineState& state) const {
    state.transport.isPlaying = clock_.isPlaying();
    state.transport.bpm = clock_.getBPM();
    state.transport.currentBeat = 0;  // TODO: Get from Clock if available
}

void Engine::buildModuleStates(EngineState& state) const {
    moduleRegistry_.forEachModule([&](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
        if (!module) return;
        
        EngineState::ModuleState moduleState;
        moduleState.name = name;
        moduleState.type = module->getTypeName();
        moduleState.enabled = module->isEnabled();
        
        // Get parameters
        auto params = module->getParameters();
        for (const auto& param : params) {
            try {
                moduleState.parameters[param.name] = module->getParameter(param.name);
            } catch (const std::exception& e) {
                ofLogWarning("Engine") << "Error getting parameter '" << param.name << "' from module '" << name << "': " << e.what();
                // Use default value (0.0f) if parameter access fails
                moduleState.parameters[param.name] = 0.0f;
            } catch (...) {
                ofLogWarning("Engine") << "Unknown error getting parameter '" << param.name << "' from module '" << name << "'";
                moduleState.parameters[param.name] = 0.0f;
            }
        }
        
        // Get type-specific state from module as JSON (via virtual method)
        // Module returns JSON, which EngineState can parse into typed variants
        // This keeps Engine decoupled from specific module types
        ofJson moduleSnapshot = module->getStateSnapshot();
        
        // Parse JSON into appropriate state variant based on type
        if (moduleState.type == "TrackerSequencer") {
            TrackerSequencerState trackerState;
            trackerState.fromJson(moduleSnapshot);
            moduleState.typeSpecific = trackerState;
        } else if (moduleState.type == "MultiSampler") {
            MultiSamplerState samplerState;
            samplerState.fromJson(moduleSnapshot);
            moduleState.typeSpecific = samplerState;
        } else if (moduleState.type == "AudioMixer") {
            // AudioMixer can use default toJson() - extract volumes from parameters
            AudioMixerState mixerState;
            mixerState.inputCount = 0;  // TODO: Get from module if available
            mixerState.masterVolume = moduleState.parameters.count("masterVolume") 
                ? moduleState.parameters.at("masterVolume") : 1.0f;
            // Try to parse from JSON if available (gracefully handles missing/invalid JSON)
            try {
                mixerState.fromJson(moduleSnapshot);
            } catch (const std::exception& e) {
                // If parsing fails, use defaults (already set above)
                ofLogVerbose("Engine") << "Could not parse AudioMixer state from JSON: " << e.what();
            }
            moduleState.typeSpecific = mixerState;
        } else if (moduleState.type == "VideoMixer") {
            // VideoMixer can use default toJson() - extract opacities from parameters
            VideoMixerState mixerState;
            mixerState.inputCount = 0;  // TODO: Get from module if available
            mixerState.masterOpacity = moduleState.parameters.count("masterOpacity")
                ? moduleState.parameters.at("masterOpacity") : 1.0f;
            // Try to parse from JSON if available (gracefully handles missing/invalid JSON)
            try {
                mixerState.fromJson(moduleSnapshot);
            } catch (const std::exception& e) {
                // If parsing fails, use defaults (already set above)
                ofLogVerbose("Engine") << "Could not parse VideoMixer state from JSON: " << e.what();
            }
            moduleState.typeSpecific = mixerState;
        }
        
        state.modules[name] = moduleState;
    });
}

void Engine::buildConnectionStates(EngineState& state) const {
    auto connections = connectionManager_.getConnections();
    for (const auto& conn : connections) {
        ConnectionInfo info;
        info.sourceModule = conn.sourceModule;
        info.targetModule = conn.targetModule;
        info.connectionType = (conn.type == ConnectionManager::ConnectionType::AUDIO) ? "AUDIO" :
                             (conn.type == ConnectionManager::ConnectionType::VIDEO) ? "VIDEO" :
                             (conn.type == ConnectionManager::ConnectionType::PARAMETER) ? "PARAMETER" : "EVENT";
        info.sourcePath = conn.sourcePath;
        info.targetPath = conn.targetPath;
        info.eventName = conn.eventName;
        info.active = conn.active;
        state.connections.push_back(info);
    }
}

void Engine::audioOut(ofSoundBuffer& buffer) {
    // CRITICAL: Process Clock first to generate timing events
    clock_.audioOut(buffer);
    
    // Evaluate patterns (sample-accurate timing)
    patternRuntime_.evaluatePatterns(buffer);
    
    // Then process audio through master output
    if (masterAudioOut_) {
        masterAudioOut_->audioOut(buffer);
    }
}

void Engine::update(float deltaTime) {
    // Update session manager (handles auto-save)
    sessionManager_.update();
    
    // Update asset library
    assetLibrary_.update();
    
    // Update command executor
    commandExecutor_.update();
    
    // Update all modules
    moduleRegistry_.forEachModule([&](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
        if (module) {
            try {
                module->update();
            } catch (const std::exception& e) {
                ofLogError("Engine") << "Error updating module '" << name << "': " << e.what();
            }
        }
    });
}

bool Engine::loadSession(const std::string& path) {
    bool result = sessionManager_.loadSession(path);
    if (result) {
        notifyStateChange();
    }
    return result;
}

bool Engine::saveSession(const std::string& path) {
    return sessionManager_.saveSession(path);
}

std::string Engine::serializeState() const {
    return getState().toJson();
}

bool Engine::deserializeState(const std::string& data) {
    try {
        EngineState state = EngineState::fromJson(data);
        // TODO: Apply state to engine (Phase 2)
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace vt

