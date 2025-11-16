#pragma once

#include "ParameterPath.h"
#include "Module.h"
#include "ModuleRegistry.h"
#include "readerwriterqueue.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <atomic>
#include <memory>
#include "ofJson.h"

// Forward declarations
class TrackerSequencer;
class MediaPool;

/**
 * ParameterRouter - Path-based parameter routing system (replaces ParameterSync)
 * 
 * Features:
 * - TouchDesigner-style hierarchical paths: "tracker1.step[4].position"
 * - Resolves paths to module instances via ModuleRegistry
 * - Bidirectional parameter synchronization
 * - Prevents feedback loops
 * - Supports conditional routing
 * 
 * Usage:
 *   ParameterRouter router(&registry);
 *   router.connect("tracker1.currentStepPosition", "multisampler2.position",
 *                  [this]() { return !clock.isPlaying(); });
 */
class ParameterRouter {
public:
    ParameterRouter(ModuleRegistry* registry = nullptr);
    ~ParameterRouter();
    
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
     * This processes queued parameter changes in a lock-free manner
     * @return Number of commands processed
     */
    int processCommands();
    
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
    /**
     * Parameter command for lock-free queue (GUI → Audio)
     */
    struct ParameterCommand {
        std::string moduleIdentifier;  // UUID or human name
        std::string paramName;
        float value;
        
        ParameterCommand() : value(0.0f) {}
        ParameterCommand(const std::string& id, const std::string& param, float val)
            : moduleIdentifier(id), paramName(param), value(val) {}
    };
    
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
    
    // Lock-free command queue for parameter changes (GUI → Audio)
    // Producer: GUI thread (notifyParameterChange)
    // Consumer: Audio thread (processCommands)
    // Capacity: 128 commands (should be more than enough)
    moodycamel::ReaderWriterQueue<ParameterCommand> commandQueue{128};
    
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
     * Handle indexed parameters (e.g., step[4].position)
     * Currently, indexing is parsed but not fully implemented.
     * For now, we ignore the index and access the parameter directly.
     * Future: Implement indexed parameter access for TrackerSequencer steps.
     */
    float getIndexedParameterValue(std::shared_ptr<Module> module, const ParameterPath& path) const;
    void setIndexedParameterValue(std::shared_ptr<Module> module, const ParameterPath& path, float value);
    
    /**
     * Process routing immediately (used by both notifyParameterChange and processCommands)
     * This is the actual routing logic that applies parameter changes
     */
    void processRoutingImmediate(Module* module, const std::string& paramName, float value);
};

