#pragma once

#include "ParameterPath.h"
#include "modules/Module.h"
#include "ModuleRegistry.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <atomic>
#include <memory>
#include "ofJson.h"

// Forward declaration
namespace vt {
    class Engine;
    class Command;
}

class ParameterRouter {
public:
    ParameterRouter(ModuleRegistry* registry = nullptr);
    ~ParameterRouter();
    
    /**
     * Set Engine reference (for state synchronization notifications)
     * Must be called before notifyParameterChange() can notify Engine
     */
    void setEngine(vt::Engine* engine) { engine_ = engine; }
    
    /**
     * Connect two parameters with bidirectional binding
     * @param sourcePath Path to source parameter (e.g., "tracker1.currentStepPosition")
     * @param targetPath Path to target parameter (e.g., "multisampler2.position")
     * @param condition Optional function that returns true when sync should be active
     * @return true if connection succeeded, false otherwise
     */
    bool connect(const std::string& sourcePath, const std::string& targetPath, 
                 std::function<bool()> condition = nullptr);
    
    /**
     * Connect using ParameterPath objects
     */
    bool connect(const ParameterPath& sourcePath, const ParameterPath& targetPath,
                 std::function<bool()> condition = nullptr);
    
    /**
     * Connect parameters directly without path parsing (simpler API for common cases)
     * @param sourceModule Source module name (e.g., "tracker1")
     * @param sourceParam Source parameter name (e.g., "currentStepPosition")
     * @param targetModule Target module name (e.g., "pool1")
     * @param targetParam Target parameter name (e.g., "position")
     * @param condition Optional function that returns true when sync should be active
     * @return true if connection succeeded, false otherwise
     * 
     * This is a convenience method that constructs paths internally.
     * Use this for simple direct connections. Use connect(path, path) for advanced
     * path-based routing with indices (e.g., "tracker1.step[4].position").
     */
    bool connectDirect(const std::string& sourceModule, const std::string& sourceParam,
                       const std::string& targetModule, const std::string& targetParam,
                       std::function<bool()> condition = nullptr);
    
    /**
     * Disconnect a binding
     * @param sourcePath Path to source parameter
     * @return true if disconnection succeeded
     */
    bool disconnect(const std::string& sourcePath);
    
    /**
     * Disconnect using ParameterPath
     */
    bool disconnect(const ParameterPath& sourcePath);
    
    /**
     * Update module name in all parameter connections
     * @param oldName Old module name
     * @param newName New module name
     */
    void renameModule(const std::string& oldName, const std::string& newName);
    
    /**
     * Disconnect all connections
     */
    void clear();
    
    /**
     * Get all connections
     * @return Vector of connection info (source path, target path)
     */
    std::vector<std::pair<std::string, std::string>> getConnections() const;
    
    /**
     * Get connections from a specific source
     */
    std::vector<std::pair<std::string, std::string>> getConnectionsFrom(const std::string& sourcePath) const;
    
    /**
     * Get connections to a specific target
     */
    std::vector<std::pair<std::string, std::string>> getConnectionsTo(const std::string& targetPath) const;
    
    /**
     * Get all modules connected to a given instance (by instance name)
     * Finds connections where the instance appears as either source or target
     * @param instanceName Module instance name (e.g., "tracker1")
     * @return Vector of shared_ptr to connected modules
     */
    std::vector<std::shared_ptr<Module>> getConnectedModules(const std::string& instanceName) const;
    
    /**
     * Notify that a parameter has changed (called by modules)
     * @param module Module that changed
     * @param paramName Parameter name that changed
     * @param value New parameter value
     */
    void notifyParameterChange(Module* module, const std::string& paramName, float value);
    
    /**
     * Update method - call from ofApp::update() to process routing
     * (Currently not needed as we use event-based routing, but kept for future use)
     */
    void update();
    
    /**
     * Process parameter commands from GUI thread (call from audio thread)
     * @deprecated This method is no longer used - commands are processed via Engine's unified queue
     * @return Number of commands processed (always 0 now)
     */
    int processCommands();
    
    /**
     * Process routing immediately (called from audio thread or commands)
     * This routes parameter changes to connected modules
     * @param module Source module
     * @param paramName Parameter name
     * @param value Parameter value
     */
    void processRoutingImmediate(Module* module, const std::string& paramName, float value);
    
    /**
     * Set the module registry (can be changed after construction)
     */
    void setRegistry(ModuleRegistry* registry) { this->registry = registry; }
    
    /**
     * Get the module registry
     */
    ModuleRegistry* getRegistry() const { return registry; }
    
    /**
     * Serialize all connections to JSON
     * Note: Conditions are not serialized (they're runtime-only)
     * @return JSON array of connection objects
     */
    ofJson toJson() const;
    
    /**
     * Deserialize connections from JSON
     * Note: Conditions will be set to always-true (default)
     * @param json JSON array of connection objects
     * @return true if successful, false otherwise
     */
    bool fromJson(const ofJson& json);

private:
    struct Connection {
        ParameterPath sourcePath;
        ParameterPath targetPath;
        std::function<bool()> condition;
        std::atomic<bool> syncing; // Guard to prevent feedback loops
        
        Connection() : syncing(false) {}
        
        // Delete copy constructor and assignment (atomic is not copyable)
        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;
        
        // Allow move constructor and assignment
        Connection(Connection&& other) noexcept
            : sourcePath(std::move(other.sourcePath))
            , targetPath(std::move(other.targetPath))
            , condition(std::move(other.condition))
            , syncing(other.syncing.load(std::memory_order_acquire))
        {}
        
        Connection& operator=(Connection&& other) noexcept {
            if (this != &other) {
                sourcePath = std::move(other.sourcePath);
                targetPath = std::move(other.targetPath);
                condition = std::move(other.condition);
                syncing.store(other.syncing.load(std::memory_order_acquire), std::memory_order_release);
            }
            return *this;
        }
    };
    
    std::vector<Connection> connections;
    ModuleRegistry* registry;
    vt::Engine* engine_ = nullptr;  // Reference to Engine for unified command queue
    
    // NOTE: ParameterRouter no longer has its own queue - all commands go through Engine's unified queue
    
    /**
     * Resolve a ParameterPath to a module instance
     * @param path Parameter path
     * @return shared_ptr to module, or nullptr if not found
     */
    std::shared_ptr<Module> resolvePath(const ParameterPath& path) const;
    
    /**
     * Get parameter value from a module
     * @param module Module instance
     * @param path Parameter path (for indexed parameters)
     * @return Parameter value, or 0.0f if not found
     */
    float getParameterValue(std::shared_ptr<Module> module, const ParameterPath& path) const;
    
    /**
     * Set parameter value in a module
     * @param module Module instance
     * @param path Parameter path (for indexed parameters)
     * @param value Value to set
     */
    void setParameterValue(std::shared_ptr<Module> module, const ParameterPath& path, float value);
    
    /**
     * Find connections matching a source path
     */
    std::vector<size_t> findConnectionsForSource(const ParameterPath& sourcePath) const;
    
    /**
     * Find connections matching a target path
     */
    std::vector<size_t> findConnectionsForTarget(const ParameterPath& targetPath) const;
    
    /**
     * Handle indexed parameters (e.g., "tracker1.position[4]")
     * 
     * Currently implemented for TrackerSequencer:
     * - Format: "tracker1.position[4]" where index is step index (0-based)
     * - Supports all step parameters: position, speed, volume, etc.
     * - Uses getStep()/setStep() for pattern cell access
     * 
     * Future extensions:
     * - Support for other module types with indexed parameters
     * - Nested indexing for multi-dimensional data
     * - Dynamic index resolution at runtime
     */
    float getIndexedParameterValue(std::shared_ptr<Module> module, const ParameterPath& path) const;
    void setIndexedParameterValue(std::shared_ptr<Module> module, const ParameterPath& path, float value);
    
};

