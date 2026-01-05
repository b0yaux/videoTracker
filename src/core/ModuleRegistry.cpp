#include "ModuleRegistry.h"
#include "ModuleFactory.h"
#include "ConnectionManager.h"
#include "GUIManager.h"
#include "ofLog.h"
#include "ofJson.h"
#include <set>
#include <cctype>

ModuleRegistry::ModuleRegistry() {
}

ModuleRegistry::~ModuleRegistry() {
    clear();
}

bool ModuleRegistry::registerModule(const std::string& uuid, std::shared_ptr<Module> module, const std::string& humanName) {
    if (!module) {
        ofLogError("ModuleRegistry") << "Cannot register null module";
        return false;
    }
    
    if (uuid.empty()) {
        ofLogError("ModuleRegistry") << "Cannot register module with empty UUID";
        return false;
    }
    
    if (humanName.empty()) {
        ofLogError("ModuleRegistry") << "Cannot register module with empty human name";
        return false;
    }
    
    // Check if UUID already exists
    if (modules.find(uuid) != modules.end()) {
        ofLogError("ModuleRegistry") << "UUID already registered: " << uuid;
        return false;
    }
    
    // Check if human name already exists
    if (nameToUUID.find(humanName) != nameToUUID.end()) {
        ofLogError("ModuleRegistry") << "Human name already registered: " << humanName;
        return false;
    }
    
    // Register module
    modules[uuid] = module;
    uuidToName[uuid] = humanName;
    nameToUUID[humanName] = uuid;
    
    ofLogNotice("ModuleRegistry") << "Registered module: UUID=" << uuid << ", name=" << humanName 
                                   << ", type=" << static_cast<int>(module->getType());
    
    return true;
}

std::shared_ptr<Module> ModuleRegistry::getModule(const std::string& identifier) const {
    // Try as UUID first
    auto it = modules.find(identifier);
    if (it != modules.end()) {
        return it->second;
    }
    
    // Try as human name
    auto nameIt = nameToUUID.find(identifier);
    if (nameIt != nameToUUID.end()) {
        auto uuidIt = modules.find(nameIt->second);
        if (uuidIt != modules.end()) {
            return uuidIt->second;
        }
    }
    
    return nullptr;
}

std::weak_ptr<Module> ModuleRegistry::getModuleWeak(const std::string& identifier) const {
    auto module = getModule(identifier);
    return std::weak_ptr<Module>(module);
}

bool ModuleRegistry::hasModule(const std::string& identifier) const {
    // Check as UUID
    if (modules.find(identifier) != modules.end()) {
        return true;
    }
    
    // Check as human name
    return nameToUUID.find(identifier) != nameToUUID.end();
}

bool ModuleRegistry::removeModule(const std::string& identifier) {
    std::string uuid;
    std::string humanName;
    
    // Determine if identifier is UUID or name
    auto uuidIt = modules.find(identifier);
    if (uuidIt != modules.end()) {
        uuid = identifier;
        auto nameIt = uuidToName.find(uuid);
        if (nameIt != uuidToName.end()) {
            humanName = nameIt->second;
        }
    } else {
        auto nameIt = nameToUUID.find(identifier);
        if (nameIt != nameToUUID.end()) {
            humanName = identifier;
            uuid = nameIt->second;
        } else {
            // Not found
            return false;
        }
    }
    
    // Remove from all maps
    modules.erase(uuid);
    uuidToName.erase(uuid);
    nameToUUID.erase(humanName);
    
    ofLogNotice("ModuleRegistry") << "Removed module: UUID=" << uuid << ", name=" << humanName;
    
    return true;
}

//--------------------------------------------------------------
bool ModuleRegistry::renameModule(const std::string& oldName, const std::string& newName) {
    // Validate old name exists
    auto oldNameIt = nameToUUID.find(oldName);
    if (oldNameIt == nameToUUID.end()) {
        ofLogError("ModuleRegistry") << "Cannot rename: old name not found: " << oldName;
        return false;
    }
    
    // Validate new name is not empty
    if (newName.empty()) {
        ofLogError("ModuleRegistry") << "Cannot rename: new name cannot be empty";
        return false;
    }
    
    // Validate new name is different from old name
    if (oldName == newName) {
        ofLogWarning("ModuleRegistry") << "Cannot rename: new name is same as old name: " << oldName;
        return false;
    }
    
    // Validate new name is unique (not already registered)
    if (nameToUUID.find(newName) != nameToUUID.end()) {
        ofLogError("ModuleRegistry") << "Cannot rename: new name already exists: " << newName;
        return false;
    }
    
    // Validate new name contains only valid characters (alphanumeric, underscore, hyphen)
    // This matches typical naming conventions and prevents issues with path parsing
    for (char c : newName) {
        if (!std::isalnum(c) && c != '_' && c != '-') {
            ofLogError("ModuleRegistry") << "Cannot rename: new name contains invalid character '" << c 
                                        << "'. Only alphanumeric characters, underscores, and hyphens are allowed.";
            return false;
        }
    }
    
    // Get UUID for the old name
    std::string uuid = oldNameIt->second;
    
    // Update mappings
    // 1. Update uuidToName mapping
    uuidToName[uuid] = newName;
    
    // 2. Remove old name from nameToUUID
    nameToUUID.erase(oldName);
    
    // 3. Add new name to nameToUUID
    nameToUUID[newName] = uuid;
    
    ofLogNotice("ModuleRegistry") << "Renamed module: " << oldName << " -> " << newName << " (UUID: " << uuid << ")";
    
    return true;
}

std::string ModuleRegistry::getUUID(const std::string& humanName) const {
    auto it = nameToUUID.find(humanName);
    if (it != nameToUUID.end()) {
        return it->second;
    }
    return "";
}

std::string ModuleRegistry::getName(const std::string& uuid) const {
    auto it = uuidToName.find(uuid);
    if (it != uuidToName.end()) {
        return it->second;
    }
    return "";
}

std::string ModuleRegistry::getName(std::shared_ptr<Module> module) const {
    if (!module) {
        return "";
    }
    
    // Find the module in our map by comparing pointers
    for (const auto& pair : modules) {
        if (pair.second == module) {
            auto nameIt = uuidToName.find(pair.first);
            if (nameIt != uuidToName.end()) {
                return nameIt->second;
            }
        }
    }
    return "";
}

std::vector<std::string> ModuleRegistry::getAllUUIDs() const {
    std::vector<std::string> uuids;
    uuids.reserve(modules.size());
    for (const auto& pair : modules) {
        uuids.push_back(pair.first);
    }
    return uuids;
}

std::vector<std::string> ModuleRegistry::getAllHumanNames() const {
    std::vector<std::string> names;
    names.reserve(nameToUUID.size());
    for (const auto& pair : nameToUUID) {
        names.push_back(pair.first);
    }
    return names;
}

std::vector<std::shared_ptr<Module>> ModuleRegistry::getModulesByType(ModuleType type) const {
    std::vector<std::shared_ptr<Module>> result;
    for (const auto& pair : modules) {
        if (pair.second && pair.second->getType() == type) {
            result.push_back(pair.second);
        }
    }
    return result;
}

void ModuleRegistry::forEachModule(std::function<void(const std::string& uuid, const std::string& humanName, std::shared_ptr<Module>)> callback) const {
    for (const auto& pair : modules) {
        const std::string& uuid = pair.first;
        auto nameIt = uuidToName.find(uuid);
        if (nameIt != uuidToName.end()) {
            callback(uuid, nameIt->second, pair.second);
        }
    }
}

void ModuleRegistry::clear() {
    size_t count = modules.size();
    modules.clear();
    uuidToName.clear();
    nameToUUID.clear();
    ofLogNotice("ModuleRegistry") << "Cleared " << count << " modules from registry";
}

//--------------------------------------------------------------
ofJson ModuleRegistry::toJson() const {
    ofJson json = ofJson::array();
    
    forEachModule([&json, this](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
        if (module) {
            ofJson moduleJson;
            moduleJson["uuid"] = uuid;
            moduleJson["name"] = name;
            moduleJson["type"] = module->getTypeName();
            // Pass registry context so modules can look up UUIDs/names for connected modules
            moduleJson["data"] = module->toJson(const_cast<ModuleRegistry*>(this));
            json.push_back(moduleJson);
        }
    });
    
    return json;
}

//--------------------------------------------------------------
bool ModuleRegistry::fromJson(const ofJson& json, ModuleFactory& factory) {
    if (!json.is_array()) {
        ofLogError("ModuleRegistry") << "Invalid JSON format: expected array";
        return false;
    }
    
    // First pass: create all modules
    for (const auto& moduleJson : json) {
        if (!moduleJson.is_object()) continue;
        
        if (!moduleJson.contains("uuid") || !moduleJson.contains("name") || !moduleJson.contains("type")) {
            ofLogWarning("ModuleRegistry") << "Skipping module with missing required fields";
            continue;
        }
        
        std::string uuid = moduleJson["uuid"];
        std::string name = moduleJson["name"];
        std::string type = moduleJson["type"];
        
        // Support backward compatibility: "VisualOutput" -> "VideoOutput"
        std::string normalizedType = type;
        if (type == "VisualOutput") {
            normalizedType = "VideoOutput";
        }
        
        // Use generic factory method - works for any registered module type
        std::shared_ptr<Module> module = factory.createModule(normalizedType, uuid, name);
        
        if (module) {
            if (!registerModule(uuid, module, name)) {
                ofLogError("ModuleRegistry") << "Failed to register module: " << name;
                return false;
            }
        } else {
            ofLogError("ModuleRegistry") << "Failed to create module: " << type;
            return false;
        }
    }
    
    // Second pass: deserialize module data (now all modules exist)
    for (const auto& moduleJson : json) {
        if (!moduleJson.is_object() || !moduleJson.contains("uuid") || !moduleJson.contains("data")) {
            continue;
        }
        
        std::string uuid = moduleJson["uuid"];
        auto module = getModule(uuid);
        if (module && moduleJson.contains("data")) {
            try {
                module->fromJson(moduleJson["data"]);
            } catch (const std::exception& e) {
                ofLogError("ModuleRegistry") << "Failed to deserialize module " << uuid << ": " << e.what();
                return false;
            }
        }
    }
    
    return true;
}

void ModuleRegistry::setupAllModules(class Clock* clock, class ModuleRegistry* registry, class ConnectionManager* connectionManager, class ParameterRouter* parameterRouter, class PatternRuntime* patternRuntime, bool isRestored) {
    // Use this registry if none provided
    ModuleRegistry* reg = registry ? registry : this;
    
    for (const auto& [uuid, module] : modules) {
        if (module) {
            module->initialize(clock, reg, connectionManager, parameterRouter, patternRuntime, isRestored);
        }
    }
}

//--------------------------------------------------------------
std::string ModuleRegistry::addModule(
    ModuleFactory& factory,
    const std::string& moduleType,
    class Clock* clock,
    class ConnectionManager* connectionManager,
    class ParameterRouter* parameterRouter,
    class PatternRuntime* patternRuntime,
    class GUIManager* guiManager,
    const std::string& masterAudioOutName,
    const std::string& masterVideoOutName
) {
    // Generate instance name first to ensure we know what it will be
    std::set<std::string> existingNames;
    auto allNames = getAllHumanNames();
    existingNames.insert(allNames.begin(), allNames.end());
    
    std::string instanceName = factory.generateInstanceName(moduleType, existingNames);
    if (instanceName.empty()) {
        ofLogError("ModuleRegistry") << "Failed to generate instance name for module type: " << moduleType;
        return "";
    }
    
    // Create module using factory with the generated name
    auto module = factory.createModule(moduleType, instanceName);
    if (!module) {
        ofLogError("ModuleRegistry") << "Failed to create module of type: " << moduleType;
        return "";
    }
    
    // Get UUID from factory using the name we generated
    std::string uuid = factory.getUUID(instanceName);
    if (uuid.empty()) {
        ofLogError("ModuleRegistry") << "Factory did not generate UUID for module: " << instanceName;
        return "";
    }
    
    // Register module
    if (!registerModule(uuid, module, instanceName)) {
        ofLogError("ModuleRegistry") << "Failed to register module: " << instanceName;
        return "";
    }
    
    // Initialize module
    module->initialize(clock, this, connectionManager, parameterRouter, patternRuntime, false);
    
    // Auto-connect to master outputs
    if (connectionManager) {
        connectionManager->autoRouteToMasters(masterAudioOutName, masterVideoOutName);
    }
    
    // Sync GUI
    if (guiManager) {
        guiManager->syncWithRegistry();
    }
    
    ofLogNotice("ModuleRegistry") << "Added module: " << instanceName << " (type: " << moduleType << ")";
    
    return instanceName;
}

//--------------------------------------------------------------
bool ModuleRegistry::removeModule(
    const std::string& identifier,
    class ConnectionManager* connectionManager,
    class GUIManager* guiManager,
    const std::string& masterAudioOutName,
    const std::string& masterVideoOutName
) {
    // Check if module exists
    if (!hasModule(identifier)) {
        ofLogWarning("ModuleRegistry") << "Module not found: " << identifier;
        return false;
    }
    
    // Get module before removal
    auto module = getModule(identifier);
    if (!module) {
        ofLogWarning("ModuleRegistry") << "Failed to get module: " << identifier;
        return false;
    }
    
    // Get UUID and name before removal
    std::string uuid;
    std::string moduleName;
    
    // Determine if identifier is UUID or name
    auto uuidIt = modules.find(identifier);
    if (uuidIt != modules.end()) {
        uuid = identifier;
        auto nameIt = uuidToName.find(uuid);
        if (nameIt != uuidToName.end()) {
            moduleName = nameIt->second;
        }
    } else {
        auto nameIt = nameToUUID.find(identifier);
        if (nameIt != nameToUUID.end()) {
            moduleName = identifier;
            uuid = nameIt->second;
        } else {
            ofLogWarning("ModuleRegistry") << "Module not found: " << identifier;
            return false;
        }
    }
    
    if (moduleName.empty()) {
        moduleName = identifier;  // Fallback
    }
    
    if (moduleName == masterAudioOutName || moduleName == masterVideoOutName) {
        ofLogWarning("ModuleRegistry") << "Cannot remove system module: " << moduleName;
        return false;
    }
    
    // Disconnect all connections BEFORE removing from registry
    // This ensures connections are cleaned up properly
    if (connectionManager) {
        try {
            connectionManager->disconnectAll(moduleName);
            ofLogNotice("ModuleRegistry") << "✓ Disconnected all connections for module: " << moduleName;
        } catch (const std::exception& e) {
            ofLogError("ModuleRegistry") << "Exception in disconnectAll(): " << e.what();
        } catch (...) {
            ofLogError("ModuleRegistry") << "Unknown exception in disconnectAll()";
        }
    }
    
    // Remove GUI BEFORE removing from registry to prevent race conditions
    // This ensures GUI cleanup happens while module still exists
    // Use direct removal method to avoid iterating through all modules
    if (guiManager) {
        try {
            guiManager->removeGUI(moduleName);
            ofLogNotice("ModuleRegistry") << "✓ Removed GUI for module: " << moduleName;
        } catch (const std::exception& e) {
            ofLogError("ModuleRegistry") << "Exception in removeGUI(): " << e.what();
        } catch (...) {
            ofLogError("ModuleRegistry") << "Unknown exception in removeGUI()";
        }
    }
    
    // Remove from all maps AFTER GUI cleanup
    // This ensures syncWithRegistry() won't try to access a deleted module
    modules.erase(uuid);
    uuidToName.erase(uuid);
    nameToUUID.erase(moduleName);
    
    ofLogNotice("ModuleRegistry") << "Removed module from registry: UUID=" << uuid << ", name=" << moduleName;
    ofLogNotice("ModuleRegistry") << "✓ Successfully removed module: " << moduleName;
    
    return true;
}

