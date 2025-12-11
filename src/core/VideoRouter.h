#pragma once

#include "modules/Module.h"
#include "ModuleRegistry.h"
#include <string>
#include <map>
#include <set>
#include <vector>
#include "ofJson.h"

// Forward declarations
class VideoOutput;
class VideoMixer;

/**
 * VideoRouter - Handles video signal routing between modules
 * 
 * Extracted from ConnectionManager to provide focused video routing functionality.
 * Supports:
 * - Module to VideoOutput connections
 * - Module to VideoMixer connections
 * - Direct module-to-module video chaining
 * 
 * Design Philosophy:
 * - Public APIs accept module names (user-friendly, backward compatible)
 * - Internal storage uses UUIDs (stable across renames, no renameModule needed)
 * - Serialization saves both UUIDs (primary) and names (readability)
 * - This separation ensures connections persist when modules are renamed
 * 
 * Usage:
 *   VideoRouter router(&registry);
 *   router.connect("pool1", "masterVideoMixer");
 *   router.disconnect("pool1", "masterVideoMixer");
 */
class VideoRouter {
public:
    VideoRouter(ModuleRegistry* registry = nullptr);
    ~VideoRouter();
    
    /**
     * Set module registry (can be called after construction)
     */
    void setRegistry(ModuleRegistry* registry) { registry_ = registry; }
    
    /**
     * Connect video from one module to another (convenience method - auto-selects compatible ports)
     * @param fromModule Source module name (e.g., "pool1")
     * @param toModule Target module/mixer/output name (e.g., "masterVideoMixer")
     * @return true if connection succeeded, false otherwise
     */
    bool connect(const std::string& fromModule, const std::string& toModule);
    
    /**
     * Connect video using explicit ports (Phase 2: port-based routing)
     * @param fromModule Source module name
     * @param fromPort Source port name (e.g., "video_out")
     * @param toModule Target module name
     * @param toPort Target port name (e.g., "video_in_0")
     * @return true if connection succeeded, false otherwise
     */
    bool connectPort(const std::string& fromModule, const std::string& fromPort,
                     const std::string& toModule, const std::string& toPort);
    
    /**
     * Disconnect video connection
     * @param fromModule Source module name
     * @param toModule Target module name (empty = disconnect from all)
     * @return true if disconnection succeeded
     */
    bool disconnect(const std::string& fromModule, const std::string& toModule = "");
    
    /**
     * Disconnect all video connections from/to a module
     * @param moduleName Module name
     * @return true if all disconnections succeeded
     */
    bool disconnectAll(const std::string& moduleName);
    
    /**
     * Clear all video connections
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
     * Get total number of video connections
     */
    int getConnectionCount() const;
    
    /**
     * Serialize video connections to JSON
     */
    ofJson toJson() const;
    
    /**
     * Deserialize video connections from JSON
     */
    bool fromJson(const ofJson& json);
    
private:
    ModuleRegistry* registry_;
    
    // Port-based connection tracking: "uuid.port" -> {"targetUuid.targetPort", ...}
    // Uses UUIDs internally to avoid needing renameModule when modules are renamed
    std::map<std::string, std::set<std::string>> portConnections_;
    
    // Helper methods
    std::shared_ptr<Module> getModule(const std::string& identifier) const;
    
    // Convert module name to UUID (returns UUID if identifier is already UUID)
    std::string getNameToUUID(const std::string& identifier) const;
    
    // Internal connection methods
    bool connectInternal(const std::string& from, const std::string& to);
    bool disconnectInternal(const std::string& from, const std::string& to);
    
    // Validation
    bool validateConnection(const std::string& from, const std::string& to) const;
};

