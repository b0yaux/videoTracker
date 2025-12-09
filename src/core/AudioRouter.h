#pragma once

#include "Module.h"
#include "ModuleRegistry.h"
#include <string>
#include <map>
#include <set>
#include <vector>
#include "ofJson.h"

// Forward declarations
class AudioOutput;
class AudioMixer;

/**
 * AudioRouter - Handles audio signal routing between modules
 * 
 * Extracted from ConnectionManager to provide focused audio routing functionality.
 * Supports:
 * - Module to AudioOutput connections
 * - Module to AudioMixer connections
 * - Direct module-to-module audio chaining
 * 
 * Usage:
 *   AudioRouter router(&registry);
 *   router.connect("pool1", "masterAudioMixer");
 *   router.disconnect("pool1", "masterAudioMixer");
 */
class AudioRouter {
public:
    AudioRouter(ModuleRegistry* registry = nullptr);
    ~AudioRouter();
    
    /**
     * Set module registry (can be called after construction)
     */
    void setRegistry(ModuleRegistry* registry) { registry_ = registry; }
    
    /**
     * Connect audio from one module to another (convenience method - auto-selects compatible ports)
     * @param fromModule Source module name (e.g., "pool1")
     * @param toModule Target module/mixer/output name (e.g., "masterAudioMixer")
     * @return true if connection succeeded, false otherwise
     */
    bool connect(const std::string& fromModule, const std::string& toModule);
    
    /**
     * Connect audio using explicit ports (Phase 2: port-based routing)
     * @param fromModule Source module name
     * @param fromPort Source port name (e.g., "audio_out")
     * @param toModule Target module name
     * @param toPort Target port name (e.g., "audio_in_0")
     * @return true if connection succeeded, false otherwise
     */
    bool connectPort(const std::string& fromModule, const std::string& fromPort,
                     const std::string& toModule, const std::string& toPort);
    
    /**
     * Disconnect audio connection
     * @param fromModule Source module name
     * @param toModule Target module name (empty = disconnect from all)
     * @return true if disconnection succeeded
     */
    bool disconnect(const std::string& fromModule, const std::string& toModule = "");
    
    /**
     * Disconnect all audio connections from/to a module
     * @param moduleName Module name
     * @return true if all disconnections succeeded
     */
    bool disconnectAll(const std::string& moduleName);
    
    /**
     * Clear all audio connections
     */
    void clear();
    
    /**
     * Check if a connection exists
     * @param fromModule Source module name
     * @param toModule Target module name
     * @return true if connection exists
     */
    bool hasConnection(const std::string& fromModule, const std::string& toModule) const;
    
    /**
     * Get all target modules connected from a source module
     * @param fromModule Source module name
     * @return Set of target module names
     */
    std::set<std::string> getTargets(const std::string& fromModule) const;
    
    /**
     * Get all source modules connected to a target module
     * @param toModule Target module name
     * @return Set of source module names
     */
    std::set<std::string> getSources(const std::string& toModule) const;
    
    /**
     * Get total number of audio connections
     */
    int getConnectionCount() const;
    
    /**
     * Serialize audio connections to JSON
     */
    ofJson toJson() const;
    
    /**
     * Deserialize audio connections from JSON
     */
    bool fromJson(const ofJson& json);
    
private:
    ModuleRegistry* registry_;
    
    // Port-based connection tracking: "module.port" -> {"targetModule.targetPort", ...}
    std::map<std::string, std::set<std::string>> portConnections_;
    
    // Helper methods
    std::shared_ptr<Module> getModule(const std::string& moduleName) const;
    
    // Internal connection methods
    bool connectInternal(const std::string& from, const std::string& to);
    bool disconnectInternal(const std::string& from, const std::string& to);
    
    // Validation
    bool validateConnection(const std::string& from, const std::string& to) const;
};

