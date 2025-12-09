#pragma once

#include "Module.h"
#include <memory>
#include <string>
#include <map>
#include <functional>

/**
 * ModuleFactory - Creates module instances with UUID and human-readable names
 * 
 * RESPONSIBILITY: Module instance creation and identity management
 * 
 * Features:
 * - Auto-generates UUID for each instance using Poco::UUID
 * - Auto-generates human-readable names (e.g., "TrackerSequencer_1")
 * - Supports custom human-readable names
 * - Returns shared_ptr for automatic memory management
 * - Tracks UUID ↔ human name mappings
 * 
 * Separation of Concerns:
 * - ModuleFactory: Creates modules and manages their identity (UUID/name)
 * - ModuleRegistry: Stores and retrieves modules by UUID/name
 * - GUIManager: Creates/destroys GUI objects for modules (one per instance)
 * - ViewManager: Renders panels and manages view state (navigation, focus)
 * 
 * Usage Flow:
 *   1. ModuleFactory creates module → returns shared_ptr<Module>
 *   2. ModuleRegistry registers module → stores by UUID/name
 *   3. GUIManager syncs with registry → creates GUI objects
 *   4. ViewManager draws panels → uses GUIManager to get GUI objects
 */
/**
 * ModuleFactory - Creates module instances with UUID and human-readable names
 * 
 * Uses registration-based factory pattern (like VCV Rack) for true modularity.
 * Modules register themselves via static registration, eliminating hardcoded dependencies.
 */
class ModuleFactory {
public:
    /**
     * Module creator function type
     * Simply creates a module instance - factory handles UUID/name mapping
     * @return shared_ptr to created module
     */
    using ModuleCreator = std::function<std::shared_ptr<Module>()>;
    
    /**
     * Register a module type with the factory
     * Called automatically by modules during static initialization
     * @param typeName Module type name (e.g., "TrackerSequencer", "MediaPool")
     * @param creator Factory function that creates instances of this module type
     */
    static void registerModuleType(const std::string& typeName, ModuleCreator creator);
    
    /**
     * Check if a module type is registered
     * @param typeName Module type name
     * @return true if type is registered
     */
    static bool isModuleTypeRegistered(const std::string& typeName);
    
    ModuleFactory();
    ~ModuleFactory();
    
    /**
     * Generic module creation - uses registration system
     * @param typeName Module type name (e.g., "TrackerSequencer", "MediaPool", "AudioMixer")
     * @param humanName Optional human-readable name. If empty, auto-generates based on type
     * @return shared_ptr to the created module, or nullptr if type is unknown
     */
    std::shared_ptr<Module> createModule(const std::string& typeName, const std::string& humanName = "");
    
    /**
     * Generic module creation with explicit UUID (for loading saved patches)
     * @param typeName Module type name
     * @param uuid UUID string (must be valid UUID format)
     * @param humanName Human-readable name
     * @return shared_ptr to the created module, or nullptr if type is unknown or UUID is invalid
     */
    std::shared_ptr<Module> createModule(const std::string& typeName, const std::string& uuid, const std::string& humanName);
    
    /**
     * Generate a unique instance name for a module type
     * @param typeName Module type name
     * @param existingNames Set of existing names to avoid collisions
     * @return Generated unique name (e.g., "trackerSequencer1", "mediaPool2")
     */
    std::string generateInstanceName(const std::string& typeName, const std::set<std::string>& existingNames = {}) const;
    
    /**
     * Get the UUID for a module instance (by human name)
     * @param humanName Human-readable name
     * @return UUID string, or empty string if not found
     */
    std::string getUUID(const std::string& humanName) const;
    
    /**
     * Get the human-readable name for a module instance (by UUID)
     * @param uuid UUID string
     * @return Human-readable name, or empty string if not found
     */
    std::string getHumanName(const std::string& uuid) const;
    
    /**
     * Check if a human name is already in use
     */
    bool isHumanNameUsed(const std::string& humanName) const;
    
    /**
     * Check if a UUID is already in use
     */
    bool isUUIDUsed(const std::string& uuid) const;
    
    /**
     * Clear all factory state (for testing)
     */
    void clear();

    /**
     * Ensure system modules exist in the registry
     * Creates master audio and video outputs if they don't exist
     * @param registry ModuleRegistry to check and register modules in
     * @param audioOutName Name for master audio output (default: "masterAudioOut")
     * @param videoOutName Name for master video output (default: "masterVideoOut")
     * @return true if system modules exist or were created successfully, false on error
     */
    bool ensureSystemModules(class ModuleRegistry* registry, 
                            const std::string& audioOutName = "masterAudioOut",
                            const std::string& videoOutName = "masterVideoOut");

private:
    /**
     * Generate a new UUID using Poco
     */
    std::string generateUUID();
    
    /**
     * Generate a name for a module type
     * @param typeName Base type name (e.g., "TrackerSequencer")
     * @return Generated name (e.g., "trackerSequencer1")
     */
    std::string generateName(const std::string& typeName);
    
    /**
     * Validate UUID format
     */
    bool isValidUUID(const std::string& uuid) const;
    
    
    // Static registration map: typeName -> creator function
    // Note: Implementation uses function-local static for initialization order safety
    // No public static member needed - accessed via getModuleCreators() internally
    
    // Track created instances: UUID -> human name
    std::map<std::string, std::string> uuidToName;
    std::map<std::string, std::string> nameToUUID;
    
    // Generic counters for auto-generated names: typeName -> count
    std::map<std::string, int> typeCounters;
};

