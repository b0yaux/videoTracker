#include "GUIManager.h"
#include "core/ModuleRegistry.h"
#include "core/ParameterRouter.h"
#include "core/ConnectionManager.h"
#include "ofLog.h"
#include <imgui.h>
#include <algorithm>

// Static registration map - GUI classes register themselves here
// Use function-local static (Meyer's singleton) to ensure initialization order safety
// This guarantees the map is initialized before first use, avoiding static initialization order issues
static std::map<std::string, GUIManager::GUICreator>& getGUICreators() {
    static std::map<std::string, GUIManager::GUICreator> guiCreators;
    return guiCreators;
}

//--------------------------------------------------------------
// Static Registration Methods
//--------------------------------------------------------------

void GUIManager::registerGUIType(const std::string& typeName, GUICreator creator) {
    auto& guiCreators = getGUICreators();
    if (guiCreators.find(typeName) != guiCreators.end()) {
        ofLogWarning("GUIManager") << "GUI type '" << typeName << "' already registered, overwriting";
    }
    guiCreators[typeName] = creator;
    ofLogNotice("GUIManager") << "Registered GUI type: " << typeName;
}

bool GUIManager::isGUITypeRegistered(const std::string& typeName) {
    auto& guiCreators = getGUICreators();
    return guiCreators.find(typeName) != guiCreators.end();
}

//--------------------------------------------------------------
// Instance Methods
//--------------------------------------------------------------

GUIManager::GUIManager() {
}

GUIManager::~GUIManager() {
    // unique_ptr will automatically clean up
}

void GUIManager::setRegistry(ModuleRegistry* registry_) {
    registry = registry_;
}

void GUIManager::setParameterRouter(ParameterRouter* router) {
    parameterRouter = router;
}

void GUIManager::setConnectionManager(ConnectionManager* manager) {
    if (!manager) {
        ofLogWarning("GUIManager") << "setConnectionManager called with null pointer!";
        connectionManager = nullptr;
        return;
    }
    
    connectionManager = manager;
    ofLogNotice("GUIManager") << "setConnectionManager called with valid pointer: " << (void*)manager;
    
    // Update all existing GUIs with the ConnectionManager and GUIManager
    int updatedCount = 0;
    for (auto& pair : allGUIs) {
        if (pair.second) {
            pair.second->setConnectionManager(connectionManager);
            pair.second->setGUIManager(this);  // Also set GUIManager reference
            updatedCount++;
        }
    }
    if (updatedCount > 0) {
        ofLogNotice("GUIManager") << "Updated " << updatedCount << " existing GUIs with ConnectionManager and GUIManager";
    } else {
        ofLogNotice("GUIManager") << "setConnectionManager: No existing GUIs to update (will be set on new GUIs)";
    }
}

/**
 * Sync GUI objects with registry (create/destroy as needed)
 * 
 * This is the main lifecycle management method. It:
 * - Detects new modules in registry → creates GUI objects
 * - Detects removed modules → destroys GUI objects
 * 
 * Call this whenever modules are added/removed from registry.
 */
void GUIManager::syncWithRegistry() {
    if (!registry) {
        ofLogWarning("GUIManager") << "Cannot sync: registry is null";
        return;
    }
    
    // Phase 12.8: Unified sync - iterate all modules and create/destroy GUIs as needed
    std::set<std::string> currentInstanceNames;
    
    // Build set of current instance names from registry
    registry->forEachModule([&currentInstanceNames, this](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
        if (module) {
            currentInstanceNames.insert(name);
            
            // Create GUI if it doesn't exist
            if (allGUIs.find(name) == allGUIs.end()) {
                // Double-check module still exists (race condition protection)
                auto verifyModule = registry->getModule(name);
                if (!verifyModule) {
                    ofLogVerbose("GUIManager") << "Skipping GUI creation for " << name << " - module no longer exists";
                    return;  // Skip this module, it was deleted (return from lambda)
                }
                
                auto gui = createGUIForModule(module, name);
                if (gui) {
                    ofLogNotice("GUIManager") << "Creating GUI for instance: " << name;
                    gui->setRegistry(registry);
                    gui->setParameterRouter(parameterRouter);
                    gui->setConnectionManager(connectionManager);
                    gui->setGUIManager(this);  // Set GUIManager reference for rename operations
                    if (!connectionManager) {
                        ofLogWarning("GUIManager") << "WARNING: Creating GUI for " << name << " but ConnectionManager is null!";
                    }
                    gui->setInstanceName(name);
                    allGUIs[name] = std::move(gui);
                    
                    // Make newly created modules visible by default
                    // This ensures users see modules they just created
                    if (visibleInstances.find(name) == visibleInstances.end()) {
                        visibleInstances.insert(name);
                        ofLogNotice("GUIManager") << "Made new module visible by default: " << name;
                    }
                }
            }
        }
    });
    
    // Remove GUIs for deleted instances
    auto it = allGUIs.begin();
    while (it != allGUIs.end()) {
        if (currentInstanceNames.find(it->first) == currentInstanceNames.end()) {
            std::string instanceName = it->first;
            ofLogNotice("GUIManager") << "Removing GUI for deleted instance: " << instanceName;
            visibleInstances.erase(instanceName);
            it = allGUIs.erase(it);
        } else {
            ++it;
        }
    }
}

// Old sync methods removed (Phase 12.8) - replaced by unified syncWithRegistry()

bool GUIManager::renameInstance(const std::string& oldName, const std::string& newName) {
    // Find GUI with old name
    auto it = allGUIs.find(oldName);
    if (it == allGUIs.end()) {
        ofLogWarning("GUIManager") << "Cannot rename: GUI not found for instance: " << oldName;
        return false;
    }
    
    // Note: Window position is now preserved automatically via UUID-based window IDs
    // (ViewManager uses "DisplayName###UUID" format, so UUID stays constant across renames)
    
    // Update visibility set
    bool wasVisible = visibleInstances.find(oldName) != visibleInstances.end();
    if (wasVisible) {
        visibleInstances.erase(oldName);
        visibleInstances.insert(newName);
    }
    
    // Move GUI to new name in map
    auto gui = std::move(it->second);
    allGUIs.erase(it);
    allGUIs[newName] = std::move(gui);
    
    // Update GUI's instance name
    if (allGUIs[newName]) {
        allGUIs[newName]->setInstanceName(newName);
    }
    
    ofLogNotice("GUIManager") << "Renamed GUI instance: " << oldName << " -> " << newName;
    return true;
}

void GUIManager::removeGUI(const std::string& instanceName) {
    // Remove from visible instances first
    visibleInstances.erase(instanceName);
    
    // Remove GUI object directly
    // Note: ImGui windows are managed by ImGui - when we stop calling Begin/End,
    // the window will be cleaned up automatically. We just need to remove our reference.
    auto it = allGUIs.find(instanceName);
    if (it != allGUIs.end()) {
        // Explicitly reset the unique_ptr to ensure destruction happens now
        // This ensures any cleanup in the GUI destructor happens immediately
        it->second.reset();
        allGUIs.erase(it);
    }
}

std::string GUIManager::getInstanceNameForModule(std::shared_ptr<Module> module) const {
    if (!registry || !module) return "";
    
    // Iterate through all modules to find the one matching this pointer
    std::vector<std::string> allUUIDs = registry->getAllUUIDs();
    for (const auto& uuid : allUUIDs) {
        auto regModule = registry->getModule(uuid);
        if (regModule == module) {
            return registry->getName(uuid);
        }
    }
    
    return "";
}

void GUIManager::setInstanceVisible(const std::string& instanceName, bool visible) {
    // Phase 12.8: Use unified visibility set
    if (visible) {
        visibleInstances.insert(instanceName);
    } else {
        visibleInstances.erase(instanceName);
    }
}

bool GUIManager::isInstanceVisible(const std::string& instanceName) const {
    // Phase 12.8: Use unified visibility set
    return visibleInstances.find(instanceName) != visibleInstances.end();
}

std::set<std::string> GUIManager::getVisibleInstances(ModuleType type) const {
    // Phase 12.8: Filter unified visibleInstances by module type
    std::set<std::string> result;
    if (!registry) return result;
    
    for (const auto& instanceName : visibleInstances) {
        auto module = registry->getModule(instanceName);
        if (module && module->getType() == type) {
            result.insert(instanceName);
        }
    }
    return result;
}

// ========================================================================
// GENERIC GUI ACCESS (Phase 12.5)
// ========================================================================

ModuleGUI* GUIManager::getGUI(const std::string& instanceName) {
    // Phase 12.8: Use unified storage
    auto it = allGUIs.find(instanceName);
    if (it != allGUIs.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::vector<ModuleGUI*> GUIManager::getAllGUIs() {
    // Phase 12.8: Use unified storage
    std::vector<ModuleGUI*> result;
    result.reserve(allGUIs.size());
    for (auto& pair : allGUIs) {
        if (pair.second) {
            result.push_back(pair.second.get());
        } else {
            ofLogWarning("GUIManager") << "Found null unique_ptr for: " << pair.first;
        }
    }
    return result;
}

std::vector<std::string> GUIManager::getAllInstanceNames() const {
    // Return instance names instead of pointers - much safer!
    std::vector<std::string> result;
    result.reserve(allGUIs.size());
    for (const auto& pair : allGUIs) {
        if (pair.second) {  // Only include if GUI exists
            result.push_back(pair.first);
        }
    }
    return result;
}

bool GUIManager::hasGUI(const std::string& instanceName) const {
    auto it = allGUIs.find(instanceName);
    return it != allGUIs.end() && it->second != nullptr;
}

// ========================================================================
// GUI FACTORY (Phase 12.8)
// ========================================================================

std::unique_ptr<ModuleGUI> GUIManager::createGUIForModule(std::shared_ptr<Module> module, const std::string& instanceName) {
    if (!module) return nullptr;
    
    // Get module type name from metadata
    auto metadata = module->getMetadata();
    std::string typeName = metadata.typeName;
    
    // Use registration map to find GUI creator
    auto& guiCreators = getGUICreators();
    auto it = guiCreators.find(typeName);
    if (it == guiCreators.end()) {
        ofLogWarning("GUIManager") << "No GUI factory for module type: " << typeName << " (" << instanceName << ")";
        return nullptr;
    }
    
    // Create GUI using registered creator
    return it->second();
}

bool GUIManager::validateWindowStates() const {
    // Check if ImGui is initialized
    if (ImGui::GetCurrentContext() == nullptr) {
        ofLogWarning("GUIManager") << "Cannot validate window states: ImGui not initialized";
        return false;
    }
    
    bool allValid = true;
    int missingCount = 0;
    
    // Phase 12.8: Use unified storage and visibility set
    for (const auto& instanceName : visibleInstances) {
        auto it = allGUIs.find(instanceName);
        if (it != allGUIs.end() && it->second) {
            if (!it->second->hasWindowState()) {
                ofLogWarning("GUIManager") << "Instance '" << instanceName << "' is visible but has no window state";
                allValid = false;
                missingCount++;
            }
        }
    }
    
    if (allValid) {
        ofLogNotice("GUIManager") << "Window state validation passed: all visible instances have windows";
    } else {
        ofLogWarning("GUIManager") << "Window state validation failed: " << missingCount << " visible instance(s) missing windows";
    }
    
    return allValid;
}

