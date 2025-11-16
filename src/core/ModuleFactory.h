#pragma once

#include "Module.h"
#include <memory>
#include <string>
#include <map>

// Forward declarations
class TrackerSequencer;
class MediaPool;

/**
 * ModuleFactory - Creates module instances with UUID and human-readable names
 * 
 * Features:
 * - Auto-generates UUID for each instance using Poco::UUID
 * - Auto-generates human-readable names (e.g., "TrackerSequencer_1")
 * - Supports custom human-readable names
 * - Returns shared_ptr for automatic memory management
 */
class ModuleFactory {
public:
    ModuleFactory();
    ~ModuleFactory();
    
    /**
     * Create a TrackerSequencer instance
     * @param humanName Optional human-readable name. If empty, auto-generates (e.g., "TrackerSequencer_1")
     * @return shared_ptr to the created module
     */
    std::shared_ptr<Module> createTrackerSequencer(const std::string& humanName = "");
    
    /**
     * Create a MediaPool instance
     * @param humanName Optional human-readable name. If empty, auto-generates (e.g., "MediaPool_1")
     * @return shared_ptr to the created module
     */
    std::shared_ptr<Module> createMediaPool(const std::string& humanName = "");
    
    /**
     * Create a TrackerSequencer with explicit UUID (for loading saved patches)
     * @param uuid UUID string (must be valid UUID format)
     * @param humanName Human-readable name
     * @return shared_ptr to the created module, or nullptr if UUID is invalid
     */
    std::shared_ptr<Module> createTrackerSequencer(const std::string& uuid, const std::string& humanName);
    
    /**
     * Create a MediaPool with explicit UUID (for loading saved patches)
     * @param uuid UUID string (must be valid UUID format)
     * @param humanName Human-readable name
     * @return shared_ptr to the created module, or nullptr if UUID is invalid
     */
    std::shared_ptr<Module> createMediaPool(const std::string& uuid, const std::string& humanName);
    
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

private:
    /**
     * Generate a new UUID using Poco
     */
    std::string generateUUID();
    
    /**
     * Generate a human-readable name for a module type
     * @param typeName Base type name (e.g., "TrackerSequencer")
     * @return Generated name (e.g., "TrackerSequencer_1")
     */
    std::string generateHumanName(const std::string& typeName);
    
    /**
     * Validate UUID format
     */
    bool isValidUUID(const std::string& uuid) const;
    
    // Track created instances: UUID -> human name
    std::map<std::string, std::string> uuidToName;
    std::map<std::string, std::string> nameToUUID;
    
    // Counters for auto-generated names
    int trackerSequencerCount;
    int mediaPoolCount;
};

