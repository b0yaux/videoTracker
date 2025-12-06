#include "ModuleFactory.h"
#include "ModuleRegistry.h"
#include "Poco/UUID.h"
#include "Poco/UUIDGenerator.h"
#include "ofLog.h"
#include <regex>
#include <mutex>
#include <algorithm>

// Static registration map - modules register themselves here
// Use function-local static (Meyer's singleton) to ensure initialization order safety
// This guarantees the map is initialized before first use, avoiding static initialization order issues
static std::map<std::string, ModuleFactory::ModuleCreator>& getModuleCreators() {
    static std::map<std::string, ModuleFactory::ModuleCreator> moduleCreators;
    return moduleCreators;
}

//--------------------------------------------------------------
// Static Registration Methods
//--------------------------------------------------------------

void ModuleFactory::registerModuleType(const std::string& typeName, ModuleCreator creator) {
    auto& moduleCreators = getModuleCreators();
    if (moduleCreators.find(typeName) != moduleCreators.end()) {
        ofLogWarning("ModuleFactory") << "Module type '" << typeName << "' already registered, overwriting";
    }
    moduleCreators[typeName] = creator;
    ofLogNotice("ModuleFactory") << "Registered module type: " << typeName;
}

bool ModuleFactory::isModuleTypeRegistered(const std::string& typeName) {
    auto& moduleCreators = getModuleCreators();
    return moduleCreators.find(typeName) != moduleCreators.end();
}

//--------------------------------------------------------------
// Instance Methods
//--------------------------------------------------------------

ModuleFactory::ModuleFactory() {
}

ModuleFactory::~ModuleFactory() {
    clear();
}

std::string ModuleFactory::getUUID(const std::string& humanName) const {
    auto it = nameToUUID.find(humanName);
    if (it != nameToUUID.end()) {
        return it->second;
    }
    return "";
}

std::string ModuleFactory::getHumanName(const std::string& uuid) const {
    auto it = uuidToName.find(uuid);
    if (it != uuidToName.end()) {
        return it->second;
    }
    return "";
}

bool ModuleFactory::isHumanNameUsed(const std::string& humanName) const {
    return nameToUUID.find(humanName) != nameToUUID.end();
}

bool ModuleFactory::isUUIDUsed(const std::string& uuid) const {
    return uuidToName.find(uuid) != uuidToName.end();
}

void ModuleFactory::clear() {
    uuidToName.clear();
    nameToUUID.clear();
    typeCounters.clear();
}

std::string ModuleFactory::generateUUID() {
    try {
        Poco::UUID uuid = Poco::UUIDGenerator::defaultGenerator().createRandom();
        return uuid.toString();
    } catch (const std::exception& e) {
        ofLogError("ModuleFactory") << "Failed to generate UUID: " << e.what();
        // Fallback: generate simple ID (not a real UUID, but unique enough for testing)
        static int fallbackCounter = 0;
        return "fallback-uuid-" + std::to_string(++fallbackCounter);
    }
}

// Helper function to convert PascalCase to camelCase
// "TrackerSequencer" -> "trackerSequencer", "AudioMixer" -> "audioMixer"
static std::string pascalToCamelCase(const std::string& pascalCase) {
    if (pascalCase.empty()) return pascalCase;
    
    std::string camelCase = pascalCase;
    // Convert first character to lowercase
    if (camelCase[0] >= 'A' && camelCase[0] <= 'Z') {
        camelCase[0] = camelCase[0] - 'A' + 'a';
    }
    return camelCase;
}

std::string ModuleFactory::generateName(const std::string& typeName) {
    // Get current count for this type (defaults to 0 if not found)
    int count = typeCounters[typeName];
    
    // Convert type name to camelCase: "TrackerSequencer" -> "trackerSequencer"
    std::string baseName = pascalToCamelCase(typeName);
    
    // Generate name: "trackerSequencer1", "trackerSequencer2", etc.
    // If that's taken, try with higher number
    std::string name = baseName + std::to_string(count + 1);
    int suffix = count + 1;
    
    while (isHumanNameUsed(name)) {
        suffix++;
        name = baseName + std::to_string(suffix);
    }
    
    return name;
}

bool ModuleFactory::isValidUUID(const std::string& uuid) const {
    try {
        // Try to parse as Poco UUID
        Poco::UUID test(uuid);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

//--------------------------------------------------------------
std::shared_ptr<Module> ModuleFactory::createModule(const std::string& typeName, const std::string& humanName) {
    // Use registration map to find creator
    auto& moduleCreators = getModuleCreators();
    auto it = moduleCreators.find(typeName);
    if (it == moduleCreators.end()) {
        ofLogError("ModuleFactory") << "Unknown module type: " << typeName;
        return nullptr;
    }
    
    // Generate UUID and name if needed
    std::string uuid = generateUUID();
    std::string name = humanName.empty() ? generateName(typeName) : humanName;
    
    // Check if name is already used
    if (isHumanNameUsed(name)) {
        ofLogWarning("ModuleFactory") << "Human name '" << name << "' already in use, auto-generating";
        name = generateName(typeName);
    }
    
    // Create module using registered creator
    auto module = it->second();
    
    if (!module) {
        ofLogError("ModuleFactory") << "Failed to create module: " << typeName;
        return nullptr;
    }
    
    // Register UUID/name mapping
    uuidToName[uuid] = name;
    nameToUUID[name] = uuid;
    
    // Update counter for this type
    typeCounters[typeName]++;
    
    ofLogNotice("ModuleFactory") << "Created " << typeName << ": UUID=" << uuid << ", name=" << name;
    
    return module;
}

//--------------------------------------------------------------
std::shared_ptr<Module> ModuleFactory::createModule(const std::string& typeName, const std::string& uuid, const std::string& humanName) {
    // Validate UUID format
    if (!isValidUUID(uuid)) {
        ofLogError("ModuleFactory") << "Invalid UUID format: " << uuid;
        return nullptr;
    }
    
    // Check if UUID is already used
    if (isUUIDUsed(uuid)) {
        ofLogError("ModuleFactory") << "UUID already in use: " << uuid;
        return nullptr;
    }
    
    // Validate human name
    if (humanName.empty()) {
        ofLogError("ModuleFactory") << "Human name required when specifying UUID";
        return nullptr;
    }
    
    if (isHumanNameUsed(humanName)) {
        ofLogError("ModuleFactory") << "Human name already in use: " << humanName;
        return nullptr;
    }
    
    // Use registration map to find creator
    auto& moduleCreators = getModuleCreators();
    auto it = moduleCreators.find(typeName);
    if (it == moduleCreators.end()) {
        ofLogError("ModuleFactory") << "Unknown module type: " << typeName;
        return nullptr;
    }
    
    // Create module using registered creator
    auto module = it->second();
    
    if (!module) {
        ofLogError("ModuleFactory") << "Failed to create module: " << typeName;
        return nullptr;
    }
    
    // Register UUID/name mapping
    uuidToName[uuid] = humanName;
    nameToUUID[humanName] = uuid;
    
    // Update counter for this type
    typeCounters[typeName]++;
    
    ofLogNotice("ModuleFactory") << "Created " << typeName << " with explicit UUID: " << uuid << ", name=" << humanName;
    
    return module;
}

//--------------------------------------------------------------
std::string ModuleFactory::generateInstanceName(const std::string& typeName, const std::set<std::string>& existingNames) const {
    // Convert type name to camelCase for base name: "TrackerSequencer" -> "trackerSequencer"
    std::string baseName = pascalToCamelCase(typeName);
    
    // Generate unique name by appending number
    // Only check existingNames (from registry) - this is the source of truth for currently used names
    // Don't check isHumanNameUsed() as factory's maps may contain stale data from removed modules
    int instanceNum = 1;
    std::string instanceName;
    do {
        instanceName = baseName + std::to_string(instanceNum);
        instanceNum++;
    } while (existingNames.find(instanceName) != existingNames.end());
    
    return instanceName;
}

//--------------------------------------------------------------
bool ModuleFactory::ensureSystemModules(ModuleRegistry* registry, 
                                        const std::string& audioOutName,
                                        const std::string& videoOutName) {
    if (!registry) {
        ofLogError("ModuleFactory") << "Cannot ensure system modules: Registry is null";
        return false;
    }
    
    // Check if master audio output exists
    auto audioOut = registry->getModule(audioOutName);
    if (!audioOut) {
        // Create master audio output using generic factory
        auto newAudioOut = createModule("AudioOutput", audioOutName);
        if (!newAudioOut) {
            ofLogError("ModuleFactory") << "Failed to create master audio output";
            return false;
        }
        
        std::string audioOutUUID = getUUID(audioOutName);
        if (audioOutUUID.empty()) {
            ofLogError("ModuleFactory") << "Failed to get UUID for master audio output";
            return false;
        }
        
        if (!registry->registerModule(audioOutUUID, newAudioOut, audioOutName)) {
            ofLogError("ModuleFactory") << "Failed to register master audio output in registry";
            return false;
        }
        
        ofLogNotice("ModuleFactory") << "Created master audio output: " << audioOutName;
    }
    
    // Check if master video output exists
    auto videoOut = registry->getModule(videoOutName);
    if (!videoOut) {
        // Create master video output using generic factory
        auto newVideoOut = createModule("VideoOutput", videoOutName);
        if (!newVideoOut) {
            ofLogError("ModuleFactory") << "Failed to create master video output";
            return false;
        }
        
        std::string videoOutUUID = getUUID(videoOutName);
        if (videoOutUUID.empty()) {
            ofLogError("ModuleFactory") << "Failed to get UUID for master video output";
            return false;
        }
        
        if (!registry->registerModule(videoOutUUID, newVideoOut, videoOutName)) {
            ofLogError("ModuleFactory") << "Failed to register master video output in registry";
            return false;
        }
        
        ofLogNotice("ModuleFactory") << "Created master video output: " << videoOutName;
    }
    
    return true;
}

