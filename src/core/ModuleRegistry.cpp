#include "ModuleRegistry.h"
#include "ModuleFactory.h"
#include "ofLog.h"
#include "ofJson.h"

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
    
    forEachModule([&json](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
        if (module) {
            ofJson moduleJson;
            moduleJson["uuid"] = uuid;
            moduleJson["name"] = name;
            moduleJson["type"] = module->getTypeName();
            moduleJson["data"] = module->toJson();
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
        
        std::shared_ptr<Module> module;
        if (type == "TrackerSequencer") {
            module = factory.createTrackerSequencer(uuid, name);
        } else if (type == "MediaPool") {
            module = factory.createMediaPool(uuid, name);
        } else {
            ofLogWarning("ModuleRegistry") << "Unknown module type: " << type;
            continue;
        }
        
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

