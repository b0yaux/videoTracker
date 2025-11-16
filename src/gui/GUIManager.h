#pragma once

#include "core/ModuleRegistry.h"
#include "MediaPoolGUI.h"
#include "TrackerSequencerGUI.h"
#include "Module.h"
#include <memory>
#include <map>
#include <set>
#include <string>
#include <vector>

/**
 * GUIManager - Manages GUI object lifecycle, one per module instance
 * 
 * Responsibilities:
 * - Create GUI objects when modules are registered
 * - Destroy GUI objects when modules are removed
 * - Maintain mapping: instance name → GUI object
 * - Provide GUI objects to ViewManager
 * 
 * Pattern: Similar to TouchDesigner/Max/MSP where each node/object has its own panel
 */
class GUIManager {
public:
    GUIManager();
    ~GUIManager();
    
    /**
     * Set the module registry (must be called before syncWithRegistry)
     */
    void setRegistry(ModuleRegistry* registry);
    
    /**
     * Sync GUI objects with registry (create/destroy as needed)
     * Call this when modules are added/removed from registry
     */
    void syncWithRegistry();
    
    /**
     * Get GUI for specific MediaPool instance
     * @param instanceName Instance name (e.g., "pool1")
     * @return Pointer to GUI object, or nullptr if not found
     */
    MediaPoolGUI* getMediaPoolGUI(const std::string& instanceName);
    
    /**
     * Get GUI for specific TrackerSequencer instance
     * @param instanceName Instance name (e.g., "tracker1")
     * @return Pointer to GUI object, or nullptr if not found
     */
    TrackerSequencerGUI* getTrackerGUI(const std::string& instanceName);
    
    /**
     * Get all MediaPool GUI objects
     * @return Vector of pointers to all MediaPool GUIs
     */
    std::vector<MediaPoolGUI*> getAllMediaPoolGUIs();
    
    /**
     * Get all TrackerSequencer GUI objects
     * @return Vector of pointers to all TrackerSequencer GUIs
     */
    std::vector<TrackerSequencerGUI*> getAllTrackerGUIs();
    
    /**
     * Set visibility for a specific instance
     * @param instanceName Instance name
     * @param visible True to show, false to hide
     */
    void setInstanceVisible(const std::string& instanceName, bool visible);
    
    /**
     * Check if an instance is visible
     * @param instanceName Instance name
     * @return True if visible
     */
    bool isInstanceVisible(const std::string& instanceName) const;
    
    /**
     * Get all visible instance names for a module type
     * @param type Module type
     * @return Set of visible instance names
     */
    std::set<std::string> getVisibleInstances(ModuleType type) const;

private:
    ModuleRegistry* registry = nullptr;
    
    // One GUI object per instance
    std::map<std::string, std::unique_ptr<MediaPoolGUI>> mediaPoolGUIs;
    std::map<std::string, std::unique_ptr<TrackerSequencerGUI>> trackerGUIs;
    
    // Visibility state (which instances should be shown)
    std::set<std::string> visibleMediaPoolInstances;
    std::set<std::string> visibleTrackerInstances;
    
    /**
     * Sync MediaPool GUIs with registry
     */
    void syncMediaPoolGUIs();
    
    /**
     * Sync TrackerSequencer GUIs with registry
     */
    void syncTrackerGUIs();
    
    /**
     * Get instance name for a module (helper)
     * @param module Module instance
     * @return Instance name, or empty string if not found
     */
    std::string getInstanceNameForModule(std::shared_ptr<Module> module) const;
};

