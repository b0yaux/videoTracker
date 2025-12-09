#pragma once

#include "modules/Module.h"
#include "ModuleRegistry.h"
#include "ParameterRouter.h"
#include "AudioRouter.h"
#include "VideoRouter.h"
#include "EventRouter.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <memory>
#include "ofJson.h"

/**
 * ConnectionManager - Unified connection management system
 * 
 * Consolidates audio/video routing (replacing RoutingManager), ParameterRouter, and event subscriptions into
 * a single unified API for managing all module connections.
 * 
 * Features:
 * - Audio/Video routing (module→module and module→mixer)
 * - Parameter routing (integrates ParameterRouter)
 * - Event subscription management
 * - Automatic routing of orphaned outputs
 * - Chain detection and auto-routing
 * 
 * Usage:
 *   ConnectionManager manager(&registry);
 *   manager.setParameterRouter(&parameterRouter);
 *   
 *   // Audio routing
 *   manager.connectAudio("pool1", "masterAudioMixer");
 *   manager.connectAudio("pool1", "effect1");  // module→module
 *   
 *   // Parameter routing
 *   manager.connectParameter("tracker1.currentStepPosition", "pool1.position",
 *                            [this]() { return !clock.isPlaying(); });
 *   
 *   // Auto-route orphaned outputs
 *   manager.autoRouteOrphanedOutputs("masterAudioMixer", "masterVideoMixer");
 */
class ConnectionManager {
public:
    /**
     * Connection type enumeration
     */
    enum class ConnectionType {
        AUDIO,      // Audio signal routing (module→module or module→mixer)
        VIDEO,      // Video signal routing (module→module or module→mixer)
        PARAMETER,  // Parameter routing (integrates ParameterRouter)
        EVENT       // Event subscriptions
    };
    
    /**
     * Unified connection information structure
     */
    struct Connection {
        std::string sourceModule;      // Source module name (e.g., "pool1")
        std::string targetModule;      // Target module/mixer name (e.g., "masterAudioMixer")
        ConnectionType type;           // Connection type
        std::string sourcePath;        // For parameter routing: "currentStepPosition"
        std::string targetPath;        // For parameter routing: "position"
        std::string eventName;         // For event subscriptions: "triggerEvent"
        std::string handlerName;       // For event subscriptions: "onTrigger"
        bool active;                   // Whether connection is active
        
        Connection()
            : type(ConnectionType::AUDIO)
            , active(true) {}
        
        Connection(const std::string& source, const std::string& target, ConnectionType t)
            : sourceModule(source)
            , targetModule(target)
            , type(t)
            , active(true) {}
        
        // Comparison for set/map operations
        bool operator<(const Connection& other) const {
            if (sourceModule != other.sourceModule) return sourceModule < other.sourceModule;
            if (targetModule != other.targetModule) return targetModule < other.targetModule;
            if (type != other.type) return static_cast<int>(type) < static_cast<int>(other.type);
            if (sourcePath != other.sourcePath) return sourcePath < other.sourcePath;
            return targetPath < other.targetPath;
        }
    };
    
    /**
     * Auto-routing configuration
     */
    enum class AutoRouteMode {
        DISABLED,           // No automatic routing
        ORPHANED_ONLY,      // Only route modules with no outgoing connections
        CHAIN_ENDS,         // Route modules at end of chains
        BOTH                // Route both orphaned and chain ends
    };
    
    ConnectionManager(ModuleRegistry* registry = nullptr);
    ~ConnectionManager();
    
    /**
     * Set module registry (can be called after construction)
     */
    void setRegistry(ModuleRegistry* registry) {
        registry_ = registry;
        audioRouter_.setRegistry(registry);
        videoRouter_.setRegistry(registry);
        eventRouter_.setRegistry(registry);
    }
    
    /**
     * Get module registry (for modules that need to query connections)
     */
    ModuleRegistry* getRegistry() const {
        return registry_;
    }
    
    /**
     * Set ParameterRouter (required for parameter routing)
     * ConnectionManager wraps ParameterRouter rather than replacing it
     */
    void setParameterRouter(ParameterRouter* router) { parameterRouter_ = router; }
    
    // ========================================================================
    // AUDIO/VIDEO ROUTING (Module→Module and Module→Mixer)
    // ========================================================================
    
    /**
     * Connect audio from one module to another (module→module or module→mixer)
     * @param fromModule Source module name (e.g., "pool1")
     * @param toModule Target module or mixer name (e.g., "masterAudioMixer" or "effect1")
     * @return true if connection succeeded, false otherwise
     */
    bool connectAudio(const std::string& fromModule, const std::string& toModule);
    
    /**
     * Connect video from one module to another (module→module or module→mixer)
     * @param fromModule Source module name (e.g., "pool1")
     * @param toModule Target module or mixer name (e.g., "masterVideoMixer" or "effect1")
     * @return true if connection succeeded, false otherwise
     */
    bool connectVideo(const std::string& fromModule, const std::string& toModule);
    
    /**
     * Generic connect method (auto-detects audio/video)
     * @param fromModule Source module name
     * @param toModule Target module or mixer name
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
    bool connectAudioPort(const std::string& fromModule, const std::string& fromPort,
                          const std::string& toModule, const std::string& toPort);
    
    /**
     * Connect video using explicit ports (Phase 2: port-based routing)
     * @param fromModule Source module name
     * @param fromPort Source port name (e.g., "video_out")
     * @param toModule Target module name
     * @param toPort Target port name (e.g., "video_in_0")
     * @return true if connection succeeded, false otherwise
     */
    bool connectVideoPort(const std::string& fromModule, const std::string& fromPort,
                          const std::string& toModule, const std::string& toPort);
    
    /**
     * Disconnect audio connection
     * @param fromModule Source module name
     * @param toModule Target module name (empty = disconnect from all)
     * @return true if disconnection succeeded
     */
    bool disconnectAudio(const std::string& fromModule, const std::string& toModule = "");
    
    /**
     * Disconnect video connection
     * @param fromModule Source module name
     * @param toModule Target module name (empty = disconnect from all)
     * @return true if disconnection succeeded
     */
    bool disconnectVideo(const std::string& fromModule, const std::string& toModule = "");
    
    /**
     * Generic disconnect method
     * @param fromModule Source module name
     * @param toModule Target module name (empty = disconnect from all)
     * @return true if disconnection succeeded
     */
    bool disconnect(const std::string& fromModule, const std::string& toModule = "");
    
    /**
     * Disconnect all connections from/to a module
     * @param moduleName Module name
     * @return true if all disconnections succeeded
     */
    bool disconnectAll(const std::string& moduleName);
    
    /**
     * Clear all connections (disconnect everything)
     */
    void clear();
    
    // ========================================================================
    // PARAMETER ROUTING (Integrates ParameterRouter)
    // ========================================================================
    
    /**
     * Connect two parameters (wraps ParameterRouter)
     * @param sourcePath Path to source parameter (e.g., "tracker1.currentStepPosition")
     * @param targetPath Path to target parameter (e.g., "pool1.position")
     * @param condition Optional function that returns true when sync should be active
     * @return true if connection succeeded, false otherwise
     * 
     * Use this for path-based routing with indices (e.g., "tracker1.step[4].position").
     * For simple direct connections, use connectParameterDirect() instead.
     */
    bool connectParameter(const std::string& sourcePath, const std::string& targetPath,
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
     * This is a convenience wrapper that constructs paths internally.
     * Use this for simple direct connections. Use connectParameter(path, path) for
     * advanced path-based routing with indices.
     */
    bool connectParameterDirect(const std::string& sourceModule, const std::string& sourceParam,
                                const std::string& targetModule, const std::string& targetParam,
                                std::function<bool()> condition = nullptr);
    
    /**
     * Disconnect a parameter connection
     * @param sourcePath Path to source parameter
     * @return true if disconnection succeeded
     */
    bool disconnectParameter(const std::string& sourcePath);
    
    // ========================================================================
    // EVENT SUBSCRIPTIONS
    // ========================================================================
    
    /**
     * Subscribe a module to another module's event
     * @param sourceModule Source module name (e.g., "tracker1")
     * @param eventName Event name (e.g., "triggerEvent")
     * @param targetModule Target module name (e.g., "pool1")
     * @param handlerName Handler method name (e.g., "onTrigger")
     * @return true if subscription succeeded, false otherwise
     */
    bool subscribeEvent(const std::string& sourceModule, const std::string& eventName,
                       const std::string& targetModule, const std::string& handlerName);
    
    /**
     * Unsubscribe from an event
     * @param sourceModule Source module name
     * @param eventName Event name
     * @param targetModule Target module name
     * @param handlerName Handler method name (optional, will find matching subscription if not provided)
     * @return true if unsubscription succeeded
     */
    bool unsubscribeEvent(const std::string& sourceModule, const std::string& eventName,
                         const std::string& targetModule, const std::string& handlerName = "");
    
    // ========================================================================
    // AUTO-ROUTING FEATURES
    // ========================================================================
    
    /**
     * Auto-route modules with no outgoing connections to master mixers
     * @param masterAudioMixer Master audio mixer name (e.g., "masterAudioMixer")
     * @param masterVideoMixer Master video mixer name (e.g., "masterVideoMixer")
     * @return Number of connections created
     */
    int autoRouteOrphanedOutputs(const std::string& masterAudioMixer,
                                  const std::string& masterVideoMixer);
    
    /**
     * Auto-route modules at end of chains to master mixers
     * @param masterAudioMixer Master audio mixer name
     * @param masterVideoMixer Master video mixer name
     * @return Number of connections created
     */
    int autoRouteChainEnds(const std::string& masterAudioMixer,
                           const std::string& masterVideoMixer);
    
    /**
     * Auto-route unconnected audio/video outputs to master outputs
     * Routes any module with audio/video outputs that aren't already connected
     * @param masterAudioOutName Master audio output name (e.g., "masterAudioOut")
     * @param masterVideoOutName Master video output name (e.g., "masterVideoOut")
     * @return Number of connections created
     */
    int autoRouteToMasters(const std::string& masterAudioOutName,
                          const std::string& masterVideoOutName);
    
    /**
     * Set auto-routing mode
     * @param mode Auto-routing mode
     */
    void setAutoRouteMode(AutoRouteMode mode) { autoRouteMode_ = mode; }
    
    /**
     * Get current auto-routing mode
     */
    AutoRouteMode getAutoRouteMode() const { return autoRouteMode_; }
    
    // ========================================================================
    // CONNECTION DISCOVERY (Phase 9.2)
    // ========================================================================
    
    /**
     * Auto-discover and connect compatible modules for a newly created module
     * @param moduleName Name of the newly created module
     * @return Number of connections created
     */
    int discoverConnectionsForModule(const std::string& moduleName);
    
    /**
     * Discover and connect modules based on capability matching
     * @param connectionType Type of connection to discover (AUDIO, VIDEO, PARAMETER, EVENT)
     * @return Number of connections created
     */
    int discoverConnections(ConnectionType connectionType);
    
    /**
     * Setup default connections for all modules
     * Sets up Clock subscriptions, connects modules to master outputs, and discovers connections
     * @param clock Clock instance for module setup
     * @param masterAudioOutName Name of master audio output (default: "masterAudioOut")
     * @param masterVideoOutName Name of master video output (default: "masterVideoOut")
     */
    void setupDefaultConnections(class Clock* clock,
                                 const std::string& masterAudioOutName = "masterAudioOut",
                                 const std::string& masterVideoOutName = "masterVideoOut");
    
    /**
     * Find compatible modules for a given module and connection type
     * @param moduleName Source module name
     * @param connectionType Type of connection
     * @return Vector of compatible module names
     */
    std::vector<std::string> findCompatibleModules(const std::string& moduleName, ConnectionType connectionType) const;
    
    // ========================================================================
    // QUERY METHODS
    // ========================================================================
    
    /**
     * Get all connections
     * @return Vector of all connections
     */
    std::vector<Connection> getConnections() const;
    
    /**
     * Get connections from a specific module
     * @param moduleName Source module name
     * @return Vector of connections
     */
    std::vector<Connection> getConnectionsFrom(const std::string& moduleName) const;
    
    /**
     * Get connections to a specific module
     * @param moduleName Target module name
     * @return Vector of connections
     */
    std::vector<Connection> getConnectionsTo(const std::string& moduleName) const;
    
    /**
     * Get connections by type
     * @param type Connection type
     * @return Vector of connections
     */
    std::vector<Connection> getConnectionsByType(ConnectionType type) const;
    
    /**
     * Check if a connection exists
     * @param fromModule Source module name
     * @param toModule Target module name
     * @param type Connection type (optional, checks all if not specified)
     * @return true if connection exists
     */
    bool hasConnection(const std::string& fromModule, const std::string& toModule,
                      ConnectionType type = ConnectionType::AUDIO) const;
    
    /**
     * Get total number of connections
     */
    int getTotalConnectionCount() const;
    
    /**
     * Get all modules connected to a specific module (Phase 9.5)
     * @param moduleName Module name
     * @param connectionType Optional connection type filter
     * @return Vector of connected module names
     */
    std::vector<std::string> getConnectedModules(const std::string& moduleName, 
                                                 ConnectionType connectionType = ConnectionType::AUDIO) const;
    
    /**
     * Find a connected module by capability (Phase 9.5)
     * @param moduleName Source module name
     * @param capability Required capability of connected module
     * @param connectionType Connection type to search
     * @return Name of first matching connected module, or empty string if none found
     */
    std::string findConnectedModuleByCapability(const std::string& moduleName,
                                                ModuleCapability capability,
                                                ConnectionType connectionType = ConnectionType::EVENT) const;
    
    // ========================================================================
    // SERIALIZATION
    // ========================================================================
    
    /**
     * Serialize all connections to JSON
     * @return JSON object containing all connections
     */
    ofJson toJson() const;
    
    /**
     * Deserialize connections from JSON
     * @param json JSON object containing connections
     * @return true if deserialization succeeded
     */
    bool fromJson(const ofJson& json);
    
private:
    ModuleRegistry* registry_;
    ParameterRouter* parameterRouter_;
    
    // Routers for different connection types
    AudioRouter audioRouter_;
    VideoRouter videoRouter_;
    EventRouter eventRouter_;
    
    // Auto-routing configuration
    AutoRouteMode autoRouteMode_;
    
    // Helper methods
    std::shared_ptr<Module> getModule(const std::string& moduleName) const;
    
    // Chain detection (uses routers)
    std::vector<std::string> findOrphanedModules() const;
    std::vector<std::string> findChainEnds() const;
    
    // Internal helper methods for simplification
    bool isMixer(const Module* module) const;
    std::string extractModuleName(const std::string& path) const;
    std::pair<bool, bool> checkOutputPorts(const Module* module) const; // Returns {hasAudio, hasVideo}
    int routeModulesToMixers(const std::vector<std::string>& modules,
                             const std::string& masterAudioMixer,
                             const std::string& masterVideoMixer,
                             bool checkExistingConnections = false);
};

