#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <optional>
#include "core/ConnectionManager.h"  // For ConnectionType enum

namespace vt {

// Forward declaration
class Engine;

/**
 * Command - Base interface for all state mutation commands
 * 
 * Implements the Command Pattern for:
 * - Unified command queue processing
 * - Undo/redo support
 * - State versioning
 * - Thread-safe execution
 * 
 * All state mutations (parameter changes, module creation, connections, etc.)
 * should go through Command objects for proper synchronization.
 */
class Command {
public:
    virtual ~Command() = default;
    
    /**
     * Execute the command
     * @param engine Engine instance to operate on
     */
    virtual void execute(Engine& engine) = 0;
    
    /**
     * Undo the command (for undo/redo support)
     * @param engine Engine instance to operate on
     */
    virtual void undo(Engine& engine) = 0;
    
    /**
     * Get human-readable description of the command
     * @return Command description string
     */
    virtual std::string describe() const = 0;
    
    /**
     * Get command timestamp (for ordering)
     * @return Timestamp in milliseconds since epoch
     */
    uint64_t getTimestamp() const { return timestamp_; }
    
    /**
     * Set command timestamp (called by Engine when enqueued)
     * @param timestamp Timestamp in milliseconds since epoch
     */
    void setTimestamp(uint64_t timestamp) { timestamp_ = timestamp; }
    
protected:
    uint64_t timestamp_ = 0;  // Set by Engine when enqueued
};

/**
 * SetParameterCommand - Command to set a module parameter
 * 
 * This command handles:
 * - Setting parameter value
 * - Storing old value for undo
 * - Triggering parameter routing
 * - Notifying state changes
 */
class SetParameterCommand : public Command {
public:
    SetParameterCommand(const std::string& moduleName, 
                       const std::string& paramName, 
                       float value)
        : moduleName_(moduleName)
        , paramName_(paramName)
        , value_(value)
        , oldValue_(0.0f)
        , oldValueStored_(false)
    {}
    
    void execute(Engine& engine) override;
    void undo(Engine& engine) override;
    
    std::string describe() const override {
        return "set " + moduleName_ + " " + paramName_ + " " + std::to_string(value_);
    }
    
    const std::string& getModuleName() const { return moduleName_; }
    const std::string& getParamName() const { return paramName_; }
    float getValue() const { return value_; }
    float getOldValue() const { return oldValue_; }
    
private:
    std::string moduleName_;
    std::string paramName_;
    float value_;
    float oldValue_;  // Stored during execute() for undo
    bool oldValueStored_;
};

/**
 * SetBPMCommand - Command to set the global BPM
 * 
 * This command handles:
 * - Setting BPM value
 * - Storing old BPM for undo
 * - Thread-safe execution via command queue
 */
class SetBPMCommand : public Command {
public:
    SetBPMCommand(float newBPM) : newBPM_(newBPM), oldBPM_(0.0f), oldBPMStored_(false) {}
    
    void execute(Engine& engine) override;
    void undo(Engine& engine) override;
    
    std::string describe() const override {
        return "set BPM to " + std::to_string(newBPM_);
    }
    
    float getNewBPM() const { return newBPM_; }
    float getOldBPM() const { return oldBPM_; }
    
private:
    float newBPM_;
    float oldBPM_;
    bool oldBPMStored_;
};

/**
 * AddModuleCommand - Command to add a module
 * 
 * This command handles:
 * - Creating and registering a new module
 * - Storing created module name for undo
 */
class AddModuleCommand : public Command {
public:
    AddModuleCommand(const std::string& moduleType, const std::string& moduleName = "")
        : moduleType_(moduleType), moduleName_(moduleName), createdModuleName_("") {}
    
    void execute(Engine& engine) override;
    void undo(Engine& engine) override;
    
    std::string describe() const override {
        return "add module " + moduleType_ + (moduleName_.empty() ? "" : " as " + moduleName_);
    }
    
    const std::string& getModuleType() const { return moduleType_; }
    const std::string& getCreatedModuleName() const { return createdModuleName_; }
    
private:
    std::string moduleType_;
    std::string moduleName_;  // Optional name hint
    std::string createdModuleName_;  // Actual name after creation (for undo)
};

/**
 * RemoveModuleCommand - Command to remove a module
 * 
 * This command handles:
 * - Removing a module from the registry
 * - Undo requires storing module state (TODO for future work)
 */
class RemoveModuleCommand : public Command {
public:
    RemoveModuleCommand(const std::string& moduleName) : moduleName_(moduleName) {}
    
    void execute(Engine& engine) override;
    void undo(Engine& engine) override;
    
    std::string describe() const override {
        return "remove module " + moduleName_;
    }
    
    const std::string& getModuleName() const { return moduleName_; }
    
private:
    std::string moduleName_;
    // TODO: Store module state for undo (Phase 6 future work - undo module removal)
};

/**
 * ConnectCommand - Command to connect modules
 * 
 * This command handles:
 * - Audio/Video/Parameter/Event connections
 * - Connection type specified via enum
 */
class ConnectCommand : public Command {
public:
    ConnectCommand(const std::string& sourceModule, const std::string& targetModule, 
                   ConnectionManager::ConnectionType connectionType)
        : sourceModule_(sourceModule), targetModule_(targetModule), connectionType_(connectionType) {}
    
    void execute(Engine& engine) override;
    void undo(Engine& engine) override;
    
    std::string describe() const override {
        const char* typeStr = (connectionType_ == ConnectionManager::ConnectionType::AUDIO) ? "AUDIO" :
                              (connectionType_ == ConnectionManager::ConnectionType::VIDEO) ? "VIDEO" :
                              (connectionType_ == ConnectionManager::ConnectionType::PARAMETER) ? "PARAMETER" : "EVENT";
        return "connect " + sourceModule_ + " to " + targetModule_ + " (" + typeStr + ")";
    }
    
    const std::string& getSourceModule() const { return sourceModule_; }
    const std::string& getTargetModule() const { return targetModule_; }
    ConnectionManager::ConnectionType getConnectionType() const { return connectionType_; }
    
private:
    std::string sourceModule_;
    std::string targetModule_;
    ConnectionManager::ConnectionType connectionType_;  // Use enum, not string
};

/**
 * DisconnectCommand - Command to disconnect modules
 * 
 * This command handles:
 * - Disconnecting specific connections or all connections from a module
 * - Connection type is optional (nullopt = all types)
 */
class DisconnectCommand : public Command {
public:
    DisconnectCommand(const std::string& sourceModule, const std::string& targetModule = "",
                      std::optional<ConnectionManager::ConnectionType> connectionType = std::nullopt)
        : sourceModule_(sourceModule), targetModule_(targetModule), connectionType_(connectionType) {}
    
    void execute(Engine& engine) override;
    void undo(Engine& engine) override;
    
    std::string describe() const override {
        if (targetModule_.empty()) {
            return "disconnect " + sourceModule_ + " from all";
        }
        std::string typeStr = connectionType_.has_value() ? 
            (connectionType_.value() == ConnectionManager::ConnectionType::AUDIO ? "AUDIO" :
             connectionType_.value() == ConnectionManager::ConnectionType::VIDEO ? "VIDEO" :
             connectionType_.value() == ConnectionManager::ConnectionType::PARAMETER ? "PARAMETER" : "EVENT") : "";
        return "disconnect " + sourceModule_ + " from " + targetModule_ + 
               (typeStr.empty() ? "" : " (" + typeStr + ")");
    }
    
    const std::string& getSourceModule() const { return sourceModule_; }
    const std::string& getTargetModule() const { return targetModule_; }
    std::optional<ConnectionManager::ConnectionType> getConnectionType() const { return connectionType_; }
    
private:
    std::string sourceModule_;
    std::string targetModule_;  // Empty = disconnect from all
    std::optional<ConnectionManager::ConnectionType> connectionType_;  // nullopt = all types
};

/**
 * StartTransportCommand - Command to start transport
 * 
 * This command handles:
 * - Starting the clock transport
 * - Storing previous playing state for undo
 */
class StartTransportCommand : public Command {
public:
    StartTransportCommand() : wasPlaying_(false), wasPlayingStored_(false) {}
    
    void execute(Engine& engine) override;
    void undo(Engine& engine) override;
    
    std::string describe() const override {
        return "start transport";
    }
    
private:
    bool wasPlaying_;
    bool wasPlayingStored_;
};

/**
 * StopTransportCommand - Command to stop transport
 * 
 * This command handles:
 * - Stopping the clock transport
 * - Storing previous playing state for undo
 */
class StopTransportCommand : public Command {
public:
    StopTransportCommand() : wasPlaying_(false), wasPlayingStored_(false) {}
    
    void execute(Engine& engine) override;
    void undo(Engine& engine) override;
    
    std::string describe() const override {
        return "stop transport";
    }
    
private:
    bool wasPlaying_;
    bool wasPlayingStored_;
};

/**
 * PauseTransportCommand - Command to pause transport
 *
 * This command handles:
 * - Pausing the clock transport (maintains position)
 * - Storing previous playing state for undo
 */
class PauseTransportCommand : public Command {
public:
    PauseTransportCommand() : wasPlaying_(false), wasPlayingStored_(false) {}
    
    void execute(Engine& engine) override;
    void undo(Engine& engine) override;
    
    std::string describe() const override {
        return "pause transport";
    }
    
private:
    bool wasPlaying_;
    bool wasPlayingStored_;
};

/**
 * ResetTransportCommand - Command to reset transport
 *
 * This command handles:
 * - Resetting the clock transport to position 0
 * - Storing previous position and playing state for undo
 */
class ResetTransportCommand : public Command {
public:
    ResetTransportCommand() : wasPlaying_(false), previousPosition_(0.0), stateStored_(false) {}
    
    void execute(Engine& engine) override;
    void undo(Engine& engine) override;
    
    std::string describe() const override {
        return "reset transport";
    }
    
private:
    bool wasPlaying_;
    double previousPosition_;
    bool stateStored_;
};

} // namespace vt

