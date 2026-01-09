#include "ModuleRegistry.h"
#include "ModuleFactory.h"
#include "ConnectionManager.h"
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
    
    // THREAD-SAFE: Exclusive lock for write operation
    std::unique_lock<std::shared_mutex> lock(registryMutex_);
    
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
    
    // CRITICAL: Set instance name on module immediately during registration
    // This ensures getInstanceName() returns the correct instance name, not the type name
    // This is especially important during session restoration
    module->setInstanceName(humanName);
    
    ofLogNotice("ModuleRegistry") << "Registered module: UUID=" << uuid << ", name=" << humanName 
                                   << ", type=" << static_cast<int>(module->getType());
    
    return true;
}

std::shared_ptr<Module> ModuleRegistry::getModule(const std::string& identifier) const {
    // THREAD-SAFE: Shared lock for read operation
    std::shared_lock<std::shared_mutex> lock(registryMutex_);
    
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
    // THREAD-SAFE: Exclusive lock for write operation
    std::unique_lock<std::shared_mutex> lock(registryMutex_);
    
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
    // THREAD-SAFE: Shared lock for read operation
    std::shared_lock<std::shared_mutex> lock(registryMutex_);
    
    auto it = nameToUUID.find(humanName);
    if (it != nameToUUID.end()) {
        return it->second;
    }
    return "";
}

std::string ModuleRegistry::getName(const std::string& uuid) const {
    // THREAD-SAFE: Shared lock for read operation
    std::shared_lock<std::shared_mutex> lock(registryMutex_);
    
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
    
    // THREAD-SAFE: Shared lock for read operation
    std::shared_lock<std::shared_mutex> lock(registryMutex_);
    
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
    // THREAD-SAFE: Shared lock for read operation
    std::shared_lock<std::shared_mutex> lock(registryMutex_);
    
    std::vector<std::string> uuids;
    uuids.reserve(modules.size());
    for (const auto& pair : modules) {
        uuids.push_back(pair.first);
    }
    return uuids;
}

std::vector<std::string> ModuleRegistry::getAllHumanNames() const {
    // THREAD-SAFE: Shared lock for read operation
    std::shared_lock<std::shared_mutex> lock(registryMutex_);
    
    std::vector<std::string> names;
    names.reserve(nameToUUID.size());
    for (const auto& pair : nameToUUID) {
        names.push_back(pair.first);
    }
    return names;
}

std::vector<std::shared_ptr<Module>> ModuleRegistry::getModulesByType(ModuleType type) const {
    // THREAD-SAFE: Shared lock for read operation
    std::shared_lock<std::shared_mutex> lock(registryMutex_);
    
    std::vector<std::shared_ptr<Module>> result;
    for (const auto& pair : modules) {
        if (pair.second && pair.second->getType() == type) {
            result.push_back(pair.second);
        }
    }
    return result;
}

void ModuleRegistry::forEachModule(std::function<void(const std::string& uuid, const std::string& humanName, std::shared_ptr<Module>)> callback) const {
    // THREAD-SAFE: Shared lock for read operation (iteration)
    // This prevents modules from being deleted during iteration
    std::shared_lock<std::shared_mutex> lock(registryMutex_);
    
    for (const auto& pair : modules) {
        const std::string& uuid = pair.first;
        auto nameIt = uuidToName.find(uuid);
        if (nameIt != uuidToName.end()) {
            callback(uuid, nameIt->second, pair.second);
        }
    }
}

void ModuleRegistry::clear() {
    // THREAD-SAFE: Exclusive lock for write operation
    std::unique_lock<std::shared_mutex> lock(registryMutex_);
    
    size_t count = modules.size();
    modules.clear();
    uuidToName.clear();
    nameToUUID.clear();
    ofLogNotice("ModuleRegistry") << "Cleared " << count << " modules from registry";
}

//--------------------------------------------------------------
ofJson ModuleRegistry::toJson() const {
    ofJson json = ofJson::array();
    
    // Use forEachModule for iteration safety (minimal lock time)
    // But use Module::getSnapshot() instead of module->toJson() (lock-free read)
    forEachModule([&json](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
        if (module) {
            // Get module snapshot (lock-free read - no moduleMutex_ needed)
            auto moduleSnapshot = module->getSnapshot();
            if (!moduleSnapshot) {
                ofLogWarning("ModuleRegistry") << "Module " << name << " has no snapshot, skipping";
                return;  // Skip modules without snapshots
            }
            
            // Build module JSON from snapshot
            ofJson moduleJson;
            moduleJson["uuid"] = uuid;
            moduleJson["name"] = name;
            moduleJson["type"] = module->getTypeName();  // Still need type name (const method, safe)
            
            // Module snapshot is already JSON (from Phase 7.1)
            // Use snapshot directly as "data" field
            moduleJson["data"] = *moduleSnapshot;
            
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
                // Initialize rendering snapshot after module restoration
                module->updateRenderingSnapshot();
            } catch (const std::exception& e) {
                ofLogError("ModuleRegistry") << "Failed to deserialize module " << uuid << ": " << e.what();
                return false;
            }
        }
    }
    
    return true;
}

void ModuleRegistry::setParameterChangeNotificationCallback(std::function<void()> callback) {
    parameterChangeNotificationCallback_ = callback;
    
    // CRITICAL: Update all existing modules' callbacks when this is set
    // This ensures master outputs and other early-initialized modules get callbacks
    for (const auto& [uuid, module] : modules) {
        if (module && parameterChangeNotificationCallback_) {
            auto existingCallback = module->getParameterChangeCallback();
            module->setParameterChangeCallback([existingCallback, this](const std::string& paramName, float value) {
                // Call existing callback (ParameterRouter notification)
                if (existingCallback) {
                    existingCallback(paramName, value);
                }
                // Also notify Engine for script sync
                if (parameterChangeNotificationCallback_) {
                    parameterChangeNotificationCallback_();
                }
            });
        }
    }
}

void ModuleRegistry::setupAllModules(class Clock* clock, class ModuleRegistry* registry, class ConnectionManager* connectionManager, class ParameterRouter* parameterRouter, class PatternRuntime* patternRuntime, bool isRestored) {
    // Use this registry if none provided
    ModuleRegistry* reg = registry ? registry : this;
    
    for (const auto& [uuid, module] : modules) {
        if (module) {
            // CRITICAL FIX: Ensure instance name is set correctly
            // Check if instance name equals type name (means it wasn't set properly)
            // This handles cases where modules were registered before instance names were set
            auto nameIt = uuidToName.find(uuid);
            if (nameIt != uuidToName.end()) {
                std::string expectedName = nameIt->second;
                std::string currentInstanceName = module->getInstanceName();
                
                // If instance name equals type name, it means instanceName_ wasn't set
                // Set it to the expected name from the registry
                if (currentInstanceName == module->getName() || currentInstanceName != expectedName) {
                    module->setInstanceName(expectedName);
                    ofLogVerbose("ModuleRegistry") << "Set instance name for module " << uuid 
                                                    << ": '" << currentInstanceName << "' -> '" << expectedName << "'";
                }
            }
            module->initialize(clock, reg, connectionManager, parameterRouter, patternRuntime, isRestored);
            
            // Set up parameter change notification callback chain
            // This ensures script sync when GUI changes parameters
            if (parameterChangeNotificationCallback_) {
                auto existingCallback = module->getParameterChangeCallback();
                module->setParameterChangeCallback([existingCallback, this](const std::string& paramName, float value) {
                    // Call existing callback (ParameterRouter notification)
                    if (existingCallback) {
                        existingCallback(paramName, value);
                    }
                    // Also notify Engine for script sync
                    if (parameterChangeNotificationCallback_) {
                        parameterChangeNotificationCallback_();
                    }
                });
            }
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
    std::function<void(const std::string&)> onAdded,
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
    
    // CRITICAL: Set instance name in module before initialization
    // This allows modules to use getInstanceName() during initialization
    module->setInstanceName(instanceName);
    
    // Initialize module
    module->initialize(clock, this, connectionManager, parameterRouter, patternRuntime, false);
    
    // Initialize module snapshot (for lock-free serialization)
    module->updateSnapshot();
    
    // Initialize rendering snapshot (for lock-free rendering)
    module->updateRenderingSnapshot();
    
    // Set up parameter change notification callback chain
    // This ensures script sync when GUI changes parameters
    if (parameterChangeNotificationCallback_) {
        auto existingCallback = module->getParameterChangeCallback();
        module->setParameterChangeCallback([existingCallback, this](const std::string& paramName, float value) {
            // Call existing callback (ParameterRouter notification)
            if (existingCallback) {
                existingCallback(paramName, value);
            }
            // Also notify Engine for script sync
            if (parameterChangeNotificationCallback_) {
                parameterChangeNotificationCallback_();
            }
        });
    }
    
    // Auto-connect to master outputs
    if (connectionManager) {
        connectionManager->autoRouteToMasters(masterAudioOutName, masterVideoOutName);
    }
    
    // Notify callback if registered (for UI sync)
    if (onAdded) {
        onAdded(instanceName);
    }
    
    ofLogNotice("ModuleRegistry") << "Added module: " << instanceName << " (type: " << moduleType << ")";
    
    return instanceName;
}

//--------------------------------------------------------------
bool ModuleRegistry::removeModule(
    const std::string& identifier,
    class ConnectionManager* connectionManager,
    std::function<void(const std::string&)> onRemoved,
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
    
    // Notify callback BEFORE removing from registry to prevent race conditions
    // This ensures UI cleanup happens while module still exists
    if (onRemoved) {
        try {
            onRemoved(moduleName);
            ofLogNotice("ModuleRegistry") << "✓ Notified UI callback for module removal: " << moduleName;
        } catch (const std::exception& e) {
            ofLogError("ModuleRegistry") << "Exception in onRemoved callback: " << e.what();
        } catch (...) {
            ofLogError("ModuleRegistry") << "Unknown exception in onRemoved callback";
        }
    }
    
    // Remove from all maps AFTER callback notification
    // This ensures any UI operations can still access the module if needed
    modules.erase(uuid);
    uuidToName.erase(uuid);
    nameToUUID.erase(moduleName);
    
    ofLogNotice("ModuleRegistry") << "Removed module from registry: UUID=" << uuid << ", name=" << moduleName;
    ofLogNotice("ModuleRegistry") << "✓ Successfully removed module: " << moduleName;
    
    return true;
}

