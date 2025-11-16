#pragma once

#include "Module.h"
#include <memory>
#include <string>
#include <map>
#include <vector>
#include <functional>
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
    
private:
    // Primary storage: UUID -> module
    std::map<std::string, std::shared_ptr<Module>> modules;
    
    // UUID -> human name mapping
    std::map<std::string, std::string> uuidToName;
    
    // Human name -> UUID mapping (for reverse lookup)
    std::map<std::string, std::string> nameToUUID;
};

