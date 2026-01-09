#pragma once

#include "modules/Module.h"
#include <memory>
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <shared_mutex>
#include "ofJson.h"

// Forward declarations
class ModuleFactory;

/**
 * ModuleRegistry - Centralized storage and lookup for module instances
 * 
 * Features:
 * - Stores modules as shared_ptr with UUID keys
 * - Supports lookup by UUID or human-readable name
 * - Provides weak_ptr access to avoid circular dependencies
 * - Thread-safe for GUI thread access (module creation/destruction)
 */
class ModuleRegistry {
public:
    ModuleRegistry();
    ~ModuleRegistry();
    
    /**
     * Register a module in the registry
     * @param uuid UUID string (must be unique)
     * @param module shared_ptr to the module (registry takes ownership)
     * @param humanName Human-readable name (must be unique)
     * @return true if registration succeeded, false if UUID or name already exists
     */
    bool registerModule(const std::string& uuid, std::shared_ptr<Module> module, const std::string& humanName);
    
    /**
     * Get a module by UUID or human name
     * @param identifier UUID string or human-readable name
     * @return shared_ptr to module, or nullptr if not found
     */
    std::shared_ptr<Module> getModule(const std::string& identifier) const;
    
    /**
     * Get a module as weak_ptr (for cross-references to avoid cycles)
     * @param identifier UUID string or human-readable name
     * @return weak_ptr to module
     */
    std::weak_ptr<Module> getModuleWeak(const std::string& identifier) const;
    
    /**
     * Check if a module exists
     * @param identifier UUID string or human-readable name
     * @return true if module exists
     */
    bool hasModule(const std::string& identifier) const;
    
    /**
     * Remove a module from the registry
     * @param identifier UUID string or human-readable name
     * @return true if module was removed, false if not found
     */
    bool removeModule(const std::string& identifier);
    
    /**
     * Rename a module instance
     * @param oldName Current human-readable name of the module
     * @param newName New human-readable name for the module
     * @return true if rename succeeded, false if old name not found, new name already exists, or validation failed
     * 
     * Validation rules:
     * - Old name must exist in registry
     * - New name must be unique (not already registered)
     * - New name must be non-empty
     * - New name must contain only alphanumeric characters, underscores, and hyphens
     * - New name cannot be the same as old name
     * 
     * Note: This only updates the registry's internal mappings. Callers are responsible for:
     * - Updating ConnectionManager connections that reference the old name
     * - Updating GUIManager instance names
     * - Updating any other systems that reference module names
     */
    bool renameModule(const std::string& oldName, const std::string& newName);
    
    /**
     * Get UUID for a human-readable name
     * @param humanName Human-readable name
     * @return UUID string, or empty string if not found
     */
    std::string getUUID(const std::string& humanName) const;
    
    /**
     * Get name for a UUID
     * @param uuid UUID string
     * @return Module name, or empty string if not found
     */
    std::string getName(const std::string& uuid) const;
    
    /**
     * Get human-readable name for a module pointer
     * @param module shared_ptr to module
     * @return Human-readable name, or empty string if not found
     */
    std::string getName(std::shared_ptr<Module> module) const;
    
    /**
     * Get all registered UUIDs
     * @return Vector of UUID strings
     */
    std::vector<std::string> getAllUUIDs() const;
    
    /**
     * Get all registered human names
     * @return Vector of human-readable names
     */
    std::vector<std::string> getAllHumanNames() const;
    
    /**
     * Get all modules of a specific type
     * @param type Module type to filter by
     * @return Vector of shared_ptr to matching modules
     */
    std::vector<std::shared_ptr<Module>> getModulesByType(ModuleType type) const;
    
    /**
     * Iterate over all modules
     * @param callback Function called for each module: (uuid, humanName, module)
     */
    void forEachModule(std::function<void(const std::string& uuid, const std::string& humanName, std::shared_ptr<Module>)> callback) const;
    
    /**
     * Get number of registered modules
     */
    size_t getModuleCount() const { return modules.size(); }
    
    /**
     * Clear all modules from registry
     */
    void clear();
    
    /**
     * Set callback for parameter change notifications (for script sync)
     * @param callback Function to call when parameters change (can be nullptr)
     */
    void setParameterChangeNotificationCallback(std::function<void()> callback);
    
    /**
     * Initialize all registered modules
     * This should be called after modules are registered to ensure proper initialization
     * @param clock Pointer to Clock instance (can be nullptr)
     * @param registry Pointer to ModuleRegistry (can be nullptr, defaults to this)
     * @param connectionManager Pointer to ConnectionManager (can be nullptr)
     * @param parameterRouter Pointer to ParameterRouter (can be nullptr)
     * @param patternRuntime PatternRuntime for modules that need pattern access (can be nullptr)
     * @param isRestored Whether modules are being restored from a session (defaults to false)
     */
    void setupAllModules(class Clock* clock, class ModuleRegistry* registry = nullptr, class ConnectionManager* connectionManager = nullptr, class ParameterRouter* parameterRouter = nullptr, class PatternRuntime* patternRuntime = nullptr, bool isRestored = false);
    
    /**
     * Serialize all modules to JSON
     * @return JSON array of module data
     */
    ofJson toJson() const;
    
    /**
     * Deserialize modules from JSON and recreate them using ModuleFactory
     * @param json JSON array of module data
     * @param factory ModuleFactory to create module instances
     * @return true if successful, false otherwise
     */
    bool fromJson(const ofJson& json, ModuleFactory& factory);
    
    /**
     * Add a module with full lifecycle management
     * Creates, registers, initializes, and auto-connects the module
     * @param factory ModuleFactory to create the module
     * @param moduleType Module type name (e.g., "MultiSampler", "TrackerSequencer")
     * @param clock Clock instance for module initialization
     * @param connectionManager ConnectionManager for connections
     * @param parameterRouter ParameterRouter for parameter routing
     * @param guiManager GUIManager for GUI sync (can be nullptr)
     * @param masterAudioOutName Master audio output name for auto-routing
     * @param masterVideoOutName Master video output name for auto-routing
     * @return Module name if successful, empty string on failure
     */
    std::string addModule(
        ModuleFactory& factory,
        const std::string& moduleType,
        class Clock* clock,
        class ConnectionManager* connectionManager,
        class ParameterRouter* parameterRouter,
        class PatternRuntime* patternRuntime = nullptr,  // PatternRuntime for modules that need pattern access
        std::function<void(const std::string&)> onAdded = nullptr,  // Callback when module is added (for UI sync)
        const std::string& masterAudioOutName = "masterAudioOut",
        const std::string& masterVideoOutName = "masterVideoOut"
    );
    
    /**
     * Remove a module with full lifecycle management
     * Disconnects all connections, cleans up GUI, and unregisters the module
     * @param identifier Module UUID or human-readable name
     * @param connectionManager ConnectionManager for disconnection
     * @param guiManager GUIManager for GUI cleanup (can be nullptr)
     * @param masterAudioOutName Master audio output name (for validation)
     * @param masterVideoOutName Master video output name (for validation)
     * @return true if successful, false otherwise
     */
    bool removeModule(
        const std::string& identifier,
        class ConnectionManager* connectionManager,
        std::function<void(const std::string&)> onRemoved = nullptr,  // Callback when module is removed (for UI cleanup)
        const std::string& masterAudioOutName = "masterAudioOut",
        const std::string& masterVideoOutName = "masterVideoOut"
    );

private:
    // Primary storage: UUID -> module
    std::map<std::string, std::shared_ptr<Module>> modules;
    
    // UUID -> human name mapping
    std::map<std::string, std::string> uuidToName;
    
    // Human name -> UUID mapping (for reverse lookup)
    std::map<std::string, std::string> nameToUUID;
    
    // Callback for parameter change notifications (for script sync)
    std::function<void()> parameterChangeNotificationCallback_;
    
    // Thread-safe registry access mutex (read-write lock)
    // Protects module storage and iteration from concurrent access
    // Read operations (getModule, forEachModule) use shared_lock
    // Write operations (registerModule, removeModule) use unique_lock
    mutable std::shared_mutex registryMutex_;
};

