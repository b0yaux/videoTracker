#pragma once

#include "core/ModuleRegistry.h"
#include "gui/ModuleGUI.h"
#include "modules/Module.h"
#include <memory>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <functional>
struct ImVec2;  // Forward declaration for ImGui types

// Forward declarations
class ParameterRouter;

/**
 * GUIManager - Manages GUI object lifecycle, one per module instance
 * 
 * RESPONSIBILITY: GUI object lifecycle and instance visibility management
 * 
 * Responsibilities:
 * - Create GUI objects (MediaPoolGUI, TrackerSequencerGUI) when modules are registered
 * - Destroy GUI objects when modules are removed from registry
 * - Maintain mapping: instance name → GUI object (one GUI per module instance)
 * - Manage instance visibility state (which module instances should be displayed)
 * - Provide GUI objects to ViewManager for rendering
 * 
 * Separation of Concerns:
 * - ModuleFactory: Creates modules and manages identity (UUID/name)
 * - ModuleRegistry: Stores and retrieves modules
 * - GUIManager: Creates/destroys GUI objects, manages instance visibility
 * - ViewManager: Renders panels, manages panel navigation and focus
 * 
 * Pattern: Similar to TouchDesigner/Max/MSP where each node/object has its own panel
 * 
 * Usage Flow:
 *   1. ModuleRegistry registers a new module
 *   2. GUIManager.syncWithRegistry() detects new module
 *   3. GUIManager creates appropriate GUI object (MediaPoolGUI or TrackerSequencerGUI)
 *   4. ViewManager calls GUIManager to get GUI objects for rendering
 *   5. When module is removed, GUIManager.syncWithRegistry() destroys GUI object
 * 
 * Note: Instance visibility (which instances to show) is managed here.
 *       Panel visibility (FileBrowser, Console) is managed by ViewManager.
 */
/**
 * GUIManager - Manages GUI object lifecycle, one per module instance
 * 
 * Uses registration-based factory pattern (like ModuleFactory) for true modularity.
 * GUI classes register themselves via static registration, eliminating hardcoded dependencies.
 */
class GUIManager {
public:
    /**
     * GUI creator function type
     * Simply creates a GUI instance - no parameters needed
     * @return unique_ptr to created GUI
     */
    using GUICreator = std::function<std::unique_ptr<ModuleGUI>()>;
    
    /**
     * Register a GUI type with the factory
     * Called automatically by GUI classes during static initialization
     * @param typeName Module type name (e.g., "TrackerSequencer", "MultiSampler")
     * @param creator Factory function that creates instances of this GUI type
     */
    static void registerGUIType(const std::string& typeName, GUICreator creator);
    
    /**
     * Check if a GUI type is registered
     * @param typeName Module type name
     * @return true if type is registered
     */
    static bool isGUITypeRegistered(const std::string& typeName);
    
    GUIManager();
    ~GUIManager();
    
    /**
     * Set the module registry (must be called before syncWithRegistry)
     */
    void setRegistry(ModuleRegistry* registry);
    
    /**
     * Set the parameter router (for connection-based parameter discovery)
     */
    void setParameterRouter(ParameterRouter* router);
    
    /**
     * Get the parameter router
     */
    ParameterRouter* getParameterRouter() const { return parameterRouter; }
    
    /**
     * Set the connection manager (for connection-based parameter discovery)
     */
    void setConnectionManager(class ConnectionManager* manager);
    
    /**
     * Get the connection manager
     */
    class ConnectionManager* getConnectionManager() const { return connectionManager; }
    
    /**
     * Sync GUI objects with registry (create/destroy as needed)
     * Call this when modules are added/removed from registry
     */
    void syncWithRegistry();
    
    // ========================================================================
    // GENERIC GUI ACCESS (Phase 12.5) - Use getGUI() and getAllGUIs() instead of module-specific getters
    // ========================================================================
    
    /**
     * Get GUI for any module instance by name (generic)
     * @param instanceName Instance name (e.g., "pool1", "tracker1")
     * @return Pointer to ModuleGUI, or nullptr if not found
     */
    ModuleGUI* getGUI(const std::string& instanceName);
    
    /**
     * Get all GUI objects (generic - Phase 12.5)
     * @return Vector of pointers to all ModuleGUIs
     * @deprecated Use getAllInstanceNames() and getGUI() for safer access
     */
    std::vector<ModuleGUI*> getAllGUIs();
    
    /**
     * Get all instance names that have GUIs (safer than getAllGUIs)
     * @return Vector of instance names
     */
    std::vector<std::string> getAllInstanceNames() const;
    
    /**
     * Check if a GUI exists for an instance (for safe access validation)
     * @param instanceName Instance name
     * @return True if GUI exists and is valid
     */
    bool hasGUI(const std::string& instanceName) const;
    
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
    
    /**
     * Validate window state after session restoration
     * Checks if all visible module instances have corresponding ImGui windows
     * @return true if all visible instances have windows, false otherwise
     */
    bool validateWindowStates() const;
    
    /**
     * Remove GUI for a specific instance (for safe deletion)
     * This directly removes the GUI without iterating through all modules
     * @param instanceName Instance name to remove
     */
    void removeGUI(const std::string& instanceName);
    
    /**
     * Rename a module instance (updates GUI mapping)
     * Called when ModuleRegistry::renameModule() succeeds
     * @param oldName Old instance name
     * @param newName New instance name
     * @return true if rename succeeded, false if GUI not found
     */
    bool renameInstance(const std::string& oldName, const std::string& newName);

private:
    ModuleRegistry* registry = nullptr;
    ParameterRouter* parameterRouter = nullptr;
    class ConnectionManager* connectionManager = nullptr;
    
    // Static registration map: typeName -> GUI creator function
    // Note: Implementation uses function-local static for initialization order safety
    // No public static member needed - accessed via getGUICreators() internally
    
    // ========================================================================
    // UNIFIED GUI STORAGE (Phase 12.8) - Replaces separate maps per type
    // ========================================================================
    // One GUI object per instance, stored generically
    std::map<std::string, std::unique_ptr<ModuleGUI>> allGUIs;
    
    // Visibility state (which instances should be shown) - unified by instance name
    std::set<std::string> visibleInstances;
    
    /**
     * Create a GUI object for a module based on its type (factory pattern)
     * @param module Module to create GUI for
     * @param instanceName Instance name for the GUI
     * @return Unique pointer to created GUI, or nullptr if module type not supported
     */
    std::unique_ptr<ModuleGUI> createGUIForModule(std::shared_ptr<Module> module, const std::string& instanceName);
    
    /**
     * Get instance name for a module (helper)
     * @param module Module instance
     * @return Instance name, or empty string if not found
     */
    std::string getInstanceNameForModule(std::shared_ptr<Module> module) const;
    
    // Legacy visibility helpers (for backward compatibility during migration)
    // These map to the unified visibleInstances set
    std::set<std::string> getVisibleInstancesForType(ModuleType type) const;
    void setVisibleInstancesForType(ModuleType type, const std::set<std::string>& instances);
};

