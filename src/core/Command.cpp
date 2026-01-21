#include "Command.h"
#include "Engine.h"
#include "ModuleRegistry.h"
#include "ModuleFactory.h"
#include "ParameterRouter.h"
#include "ConnectionManager.h"
#include "Clock.h"
#include "ofLog.h"

namespace vt {

void SetParameterCommand::execute(Engine& engine) {
    // Get module from registry
    auto& moduleRegistry = engine.getModuleRegistry();
    auto module = moduleRegistry.getModule(moduleName_);
    
    if (!module) {
        ofLogError("SetParameterCommand") << "Module not found: " << moduleName_;
        return;
    }
    
    // Store old value for undo (only once)
    if (!oldValueStored_) {
        try {
            oldValue_ = module->getParameter(paramName_);
            oldValueStored_ = true;
        } catch (const std::exception& e) {
            ofLogWarning("SetParameterCommand") << "Could not get old value for " 
                                                << moduleName_ << "." << paramName_ 
                                                << ": " << e.what();
            oldValue_ = 0.0f;
            oldValueStored_ = true;
        }
    }
    
    // Set parameter (don't notify - we handle state change at Engine level)
    try {
        module->setParameter(paramName_, value_, false);
    } catch (const std::exception& e) {
        ofLogError("SetParameterCommand") << "Failed to set parameter " 
                                          << moduleName_ << "." << paramName_ 
                                          << ": " << e.what();
        return;
    }
    
    // Process parameter routing (if ParameterRouter is available)
    // ParameterRouter is always available, so we can call it directly
    auto& parameterRouter = engine.getParameterRouter();
    // Process routing immediately (this is called from audio thread)
    parameterRouter.processRoutingImmediate(module.get(), paramName_, value_);
    
    // NOTE: State synchronization is handled by Engine::processCommands()
    // which calls notifyStateChange() after processing commands
}

void SetParameterCommand::undo(Engine& engine) {
    if (!oldValueStored_) {
        ofLogWarning("SetParameterCommand") << "Cannot undo: old value not stored";
        return;
    }
    
    // Get module from registry
    auto& moduleRegistry = engine.getModuleRegistry();
    auto module = moduleRegistry.getModule(moduleName_);
    
    if (!module) {
        ofLogError("SetParameterCommand") << "Module not found for undo: " << moduleName_;
        return;
    }
    
    // Restore old value
    try {
        module->setParameter(paramName_, oldValue_, false);
    } catch (const std::exception& e) {
        ofLogError("SetParameterCommand") << "Failed to undo parameter " 
                                          << moduleName_ << "." << paramName_ 
                                          << ": " << e.what();
        return;
    }
    
    // Process parameter routing for undo
    auto& parameterRouter = engine.getParameterRouter();
    parameterRouter.processRoutingImmediate(module.get(), paramName_, oldValue_);
}

void SetBPMCommand::execute(Engine& engine) {    // Get Clock from engine
    auto& clock = engine.getClock();
    
    // Store old BPM value for undo (only once)
    if (!oldBPMStored_) {
        try {
            oldBPM_ = clock.getBPM();
            oldBPMStored_ = true;
        } catch (const std::exception& e) {
            ofLogWarning("SetBPMCommand") << "Could not get old BPM value: " << e.what();
            oldBPM_ = 120.0f;  // Default fallback
            oldBPMStored_ = true;
        }
    }
    
    // Set new BPM
    try {
        clock.setBPM(newBPM_);    } catch (const std::exception& e) {
        ofLogError("SetBPMCommand") << "Failed to set BPM to " << newBPM_ << ": " << e.what();
        return;
    }
    
    // NOTE: State synchronization is handled by Engine::processCommands()
    // which calls notifyStateChange() after processing commands
}

void SetBPMCommand::undo(Engine& engine) {
    if (!oldBPMStored_) {
        ofLogWarning("SetBPMCommand") << "Cannot undo: old BPM value not stored";
        return;
    }
    
    // Get Clock from engine
    auto& clock = engine.getClock();
    
    // Restore old BPM
    try {
        clock.setBPM(oldBPM_);
    } catch (const std::exception& e) {
        ofLogError("SetBPMCommand") << "Failed to undo BPM to " << oldBPM_ << ": " << e.what();
        return;
    }
}

void AddModuleCommand::execute(Engine& engine) {
    // Get ModuleRegistry and ModuleFactory from engine
    auto& moduleRegistry = engine.getModuleRegistry();
    auto& moduleFactory = engine.getModuleFactory();
    auto& clock = engine.getClock();
    auto& connectionManager = engine.getConnectionManager();
    auto& parameterRouter = engine.getParameterRouter();
    auto& patternRuntime = engine.getPatternRuntime();
    
    // Add module using ModuleRegistry::addModule()
    // This handles creation, registration, initialization, and auto-connection
    std::string result = moduleRegistry.addModule(
        moduleFactory,
        moduleType_,
        &clock,
        &connectionManager,
        &parameterRouter,
        &patternRuntime,
        nullptr,  // onAdded callback (not needed for command)
        "masterAudioOut",
        "masterVideoOut"
    );
    
    if (result.empty()) {
        ofLogError("AddModuleCommand") << "Failed to add module " << moduleType_;
        return;
    }
    
    // Store created module name for undo
    createdModuleName_ = result;
    
    // NOTE: State synchronization is handled by Engine::processCommands()
    // which calls notifyStateChange() after processing commands
}

void AddModuleCommand::undo(Engine& engine) {
    if (createdModuleName_.empty()) {
        ofLogWarning("AddModuleCommand") << "Cannot undo: module was not created";
        return;
    }
    
    // Remove the created module
    auto& moduleRegistry = engine.getModuleRegistry();
    auto& connectionManager = engine.getConnectionManager();
    
    bool success = moduleRegistry.removeModule(
        createdModuleName_,
        &connectionManager,
        nullptr,  // onRemoved callback (not needed for command)
        "masterAudioOut",
        "masterVideoOut"
    );
    
    if (!success) {
        ofLogError("AddModuleCommand") << "Failed to undo: could not remove module " << createdModuleName_;
        return;
    }
}

void RemoveModuleCommand::execute(Engine& engine) {
    // Get ModuleRegistry and ConnectionManager from engine
    auto& moduleRegistry = engine.getModuleRegistry();
    auto& connectionManager = engine.getConnectionManager();
    
    // Remove module using ModuleRegistry::removeModule()
    // This handles disconnection, cleanup, and unregistration
    bool success = moduleRegistry.removeModule(
        moduleName_,
        &connectionManager,
        nullptr,  // onRemoved callback (not needed for command)
        "masterAudioOut",
        "masterVideoOut"
    );
    
    if (!success) {
        ofLogError("RemoveModuleCommand") << "Failed to remove module " << moduleName_;
        return;
    }
    
    // NOTE: State synchronization is handled by Engine::processCommands()
    // which calls notifyStateChange() after processing commands
}

void RemoveModuleCommand::undo(Engine& engine) {
    // TODO: Module removal undo is complex - requires storing full module state
    // For now, mark as not implemented (acceptable per plan requirements)
    ofLogWarning("RemoveModuleCommand") << "Undo not implemented for module removal (requires storing module state)";
    // Future work: Store module state during execute() and recreate module during undo()
}

void ConnectCommand::execute(Engine& engine) {
    // Get ConnectionManager from engine
    auto& connectionManager = engine.getConnectionManager();
    
    // Connect based on connection type
    bool success = false;
    switch (connectionType_) {
        case ConnectionManager::ConnectionType::AUDIO:
            success = connectionManager.connectAudio(sourceModule_, targetModule_);
            break;
        case ConnectionManager::ConnectionType::VIDEO:
            success = connectionManager.connectVideo(sourceModule_, targetModule_);
            break;
        case ConnectionManager::ConnectionType::PARAMETER:
            // Parameter connections require paths - use connect() for now
            // TODO: Add parameter-specific connection method if needed
            ofLogWarning("ConnectCommand") << "Parameter connections require source/target paths - not fully implemented";
            success = false;
            break;
        case ConnectionManager::ConnectionType::EVENT:
            // Event connections require event name and handler - use subscribeEvent()
            // TODO: Add event-specific connection method if needed
            ofLogWarning("ConnectCommand") << "Event connections require event/handler names - not fully implemented";
            success = false;
            break;
    }
    
    if (!success) {
        ofLogError("ConnectCommand") << "Failed to connect " << sourceModule_ << " to " << targetModule_;
        return;
    }
    
    // NOTE: State synchronization is handled by Engine::processCommands()
    // which calls notifyStateChange() after processing commands
}

void ConnectCommand::undo(Engine& engine) {
    // Disconnect the connection we just created
    auto& connectionManager = engine.getConnectionManager();
    
    bool success = false;
    switch (connectionType_) {
        case ConnectionManager::ConnectionType::AUDIO:
            success = connectionManager.disconnectAudio(sourceModule_, targetModule_);
            break;
        case ConnectionManager::ConnectionType::VIDEO:
            success = connectionManager.disconnectVideo(sourceModule_, targetModule_);
            break;
        case ConnectionManager::ConnectionType::PARAMETER:
            // TODO: Implement parameter disconnection
            ofLogWarning("ConnectCommand") << "Parameter disconnection undo not fully implemented";
            success = false;
            break;
        case ConnectionManager::ConnectionType::EVENT:
            // TODO: Implement event unsubscription
            ofLogWarning("ConnectCommand") << "Event unsubscription undo not fully implemented";
            success = false;
            break;
    }
    
    if (!success) {
        ofLogError("ConnectCommand") << "Failed to undo connection from " << sourceModule_ << " to " << targetModule_;
        return;
    }
}

void DisconnectCommand::execute(Engine& engine) {
    // Get ConnectionManager from engine
    auto& connectionManager = engine.getConnectionManager();
    
    bool success = false;
    
    if (targetModule_.empty()) {
        // Disconnect all connections from source module
        success = connectionManager.disconnectAll(sourceModule_);
    } else if (connectionType_.has_value()) {
        // Disconnect specific connection type
        switch (connectionType_.value()) {
            case ConnectionManager::ConnectionType::AUDIO:
                success = connectionManager.disconnectAudio(sourceModule_, targetModule_);
                break;
            case ConnectionManager::ConnectionType::VIDEO:
                success = connectionManager.disconnectVideo(sourceModule_, targetModule_);
                break;
            case ConnectionManager::ConnectionType::PARAMETER:
                // TODO: Implement parameter disconnection
                ofLogWarning("DisconnectCommand") << "Parameter disconnection not fully implemented";
                success = false;
                break;
            case ConnectionManager::ConnectionType::EVENT:
                // TODO: Implement event unsubscription
                ofLogWarning("DisconnectCommand") << "Event unsubscription not fully implemented";
                success = false;
                break;
        }
    } else {
        // Disconnect all types (generic disconnect)
        success = connectionManager.disconnect(sourceModule_, targetModule_);
    }
    
    if (!success) {
        ofLogError("DisconnectCommand") << "Failed to disconnect " << sourceModule_ 
                                        << (targetModule_.empty() ? " from all" : " from " + targetModule_);
        return;
    }
    
    // NOTE: State synchronization is handled by Engine::processCommands()
    // which calls notifyStateChange() after processing commands
}

void DisconnectCommand::undo(Engine& engine) {
    // TODO: Connection undo is complex - requires storing connection details
    // For now, mark as not implemented (acceptable per plan requirements)
    ofLogWarning("DisconnectCommand") << "Undo not implemented for disconnection (requires storing connection details)";
    // Future work: Store connection details during execute() and reconnect during undo()
}

void StartTransportCommand::execute(Engine& engine) {
    // Get Clock from engine
    auto& clock = engine.getClock();
    
    // Store old playing state for undo (only once)
    if (!wasPlayingStored_) {
        try {
            wasPlaying_ = clock.isPlaying();
            wasPlayingStored_ = true;
        } catch (const std::exception& e) {
            ofLogWarning("StartTransportCommand") << "Could not get old playing state: " << e.what();
            wasPlaying_ = false;  // Default fallback
            wasPlayingStored_ = true;
        }
    }
    
    // Start transport
    try {
        clock.start();
    } catch (const std::exception& e) {
        ofLogError("StartTransportCommand") << "Failed to start transport: " << e.what();
        return;
    }
    
    // NOTE: State synchronization is handled by Engine::processCommands()
    // which calls notifyStateChange() after processing commands
}

void StartTransportCommand::undo(Engine& engine) {
    if (!wasPlayingStored_) {
        ofLogWarning("StartTransportCommand") << "Cannot undo: old playing state not stored";
        return;
    }
    
    // Get Clock from engine
    auto& clock = engine.getClock();
    
    // Restore old playing state
    try {
        if (wasPlaying_) {
            clock.start();
        } else {
            clock.stop();
        }
    } catch (const std::exception& e) {
        ofLogError("StartTransportCommand") << "Failed to undo transport: " << e.what();
        return;
    }
}

void StopTransportCommand::execute(Engine& engine) {
    // Get Clock from engine
    auto& clock = engine.getClock();
    
    // Store old playing state for undo (only once)
    if (!wasPlayingStored_) {
        try {
            wasPlaying_ = clock.isPlaying();
            wasPlayingStored_ = true;
        } catch (const std::exception& e) {
            ofLogWarning("StopTransportCommand") << "Could not get old playing state: " << e.what();
            wasPlaying_ = false;  // Default fallback
            wasPlayingStored_ = true;
        }
    }
    
    // Stop transport
    try {
        clock.stop();
    } catch (const std::exception& e) {
        ofLogError("StopTransportCommand") << "Failed to stop transport: " << e.what();
        return;
    }
    
    // NOTE: State synchronization is handled by Engine::processCommands()
    // which calls notifyStateChange() after processing commands
}

void StopTransportCommand::undo(Engine& engine) {
    if (!wasPlayingStored_) {
        ofLogWarning("StopTransportCommand") << "Cannot undo: old playing state not stored";
        return;
    }
    
    // Get Clock from engine
    auto& clock = engine.getClock();
    
    // Restore old playing state
    try {
        if (wasPlaying_) {
            clock.start();
        } else {
            clock.stop();
        }
    } catch (const std::exception& e) {
        ofLogError("StopTransportCommand") << "Failed to undo transport: " << e.what();
        return;
    }
}

void PauseTransportCommand::execute(Engine& engine) {
    // Get Clock from engine
    auto& clock = engine.getClock();
    
    // Store old playing state for undo (only once)
    if (!wasPlayingStored_) {
        try {
            wasPlaying_ = clock.isPlaying();
            wasPlayingStored_ = true;
        } catch (const std::exception& e) {
            ofLogWarning("PauseTransportCommand") << "Could not get old playing state: " << e.what();
            wasPlaying_ = false;  // Default fallback
            wasPlayingStored_ = true;
        }
    }
    
    // Pause transport
    try {
        clock.pause();
    } catch (const std::exception& e) {
        ofLogError("PauseTransportCommand") << "Failed to pause transport: " << e.what();
        return;
    }
    
    // NOTE: State synchronization is handled by Engine::processCommands()
    // which calls notifyStateChange() after processing commands
}

void PauseTransportCommand::undo(Engine& engine) {
    if (!wasPlayingStored_) {
        ofLogWarning("PauseTransportCommand") << "Cannot undo: old playing state not stored";
        return;
    }
    
    // Get Clock from engine
    auto& clock = engine.getClock();
    
    // Restore old playing state
    try {
        if (wasPlaying_) {
            clock.start();
        } else {
            clock.stop();
        }
    } catch (const std::exception& e) {
        ofLogError("PauseTransportCommand") << "Failed to undo transport: " << e.what();
        return;
    }
}

void ResetTransportCommand::execute(Engine& engine) {
    // Get Clock from engine
    auto& clock = engine.getClock();
    
    // Store old state for undo (only once)
    if (!stateStored_) {
        try {
            wasPlaying_ = clock.isPlaying();
            previousPosition_ = clock.getCurrentBeat();
            stateStored_ = true;
        } catch (const std::exception& e) {
            ofLogWarning("ResetTransportCommand") << "Could not get old state: " << e.what();
            wasPlaying_ = false;
            previousPosition_ = 0.0;
            stateStored_ = true;
        }
    }
    
    // Reset transport
    try {
        clock.reset();
    } catch (const std::exception& e) {
        ofLogError("ResetTransportCommand") << "Failed to reset transport: " << e.what();
        return;
    }
    
    // NOTE: State synchronization is handled by Engine::processCommands()
    // which calls notifyStateChange() after processing commands
}

void ResetTransportCommand::undo(Engine& engine) {
    if (!stateStored_) {
        ofLogWarning("ResetTransportCommand") << "Cannot undo: old state not stored";
        return;
    }
    
    // Get Clock from engine
    auto& clock = engine.getClock();
    
    // Restore old playing state
    // Note: Full position undo requires setPosition() which may not exist
    try {
        if (wasPlaying_) {
            clock.start();
        }
        // Position is not restored (clock.reset() is not easily reversible)
        // This is a limitation noted in the plan
    } catch (const std::exception& e) {
        ofLogError("ResetTransportCommand") << "Failed to undo transport: " << e.what();
        return;
    }
}

} // namespace vt

