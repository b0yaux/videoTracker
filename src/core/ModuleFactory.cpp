#include "ModuleFactory.h"
#include "TrackerSequencer.h"
#include "MediaPool.h"
#include "Poco/UUID.h"
#include "Poco/UUIDGenerator.h"
#include "ofLog.h"
#include <regex>

ModuleFactory::ModuleFactory() 
    : trackerSequencerCount(0), mediaPoolCount(0) {
}

ModuleFactory::~ModuleFactory() {
    clear();
}

std::shared_ptr<Module> ModuleFactory::createTrackerSequencer(const std::string& humanName) {
    std::string uuid = generateUUID();
    std::string name = humanName.empty() ? generateHumanName("TrackerSequencer") : humanName;
    
    // Check if name is already used
    if (isHumanNameUsed(name)) {
        ofLogWarning("ModuleFactory") << "Human name '" << name << "' already in use, auto-generating";
        name = generateHumanName("TrackerSequencer");
    }
    
    // Create instance
    auto instance = std::make_shared<TrackerSequencer>();
    
    // Register
    uuidToName[uuid] = name;
    nameToUUID[name] = uuid;
    trackerSequencerCount++;
    
    ofLogNotice("ModuleFactory") << "Created TrackerSequencer: UUID=" << uuid << ", name=" << name;
    
    return instance;
}

std::shared_ptr<Module> ModuleFactory::createMediaPool(const std::string& humanName) {
    std::string uuid = generateUUID();
    std::string name = humanName.empty() ? generateHumanName("MediaPool") : humanName;
    
    // Check if name is already used
    if (isHumanNameUsed(name)) {
        ofLogWarning("ModuleFactory") << "Human name '" << name << "' already in use, auto-generating";
        name = generateHumanName("MediaPool");
    }
    
    // Create instance (MediaPool constructor takes optional dataDir)
    auto instance = std::make_shared<MediaPool>();
    
    // Register
    uuidToName[uuid] = name;
    nameToUUID[name] = uuid;
    mediaPoolCount++;
    
    ofLogNotice("ModuleFactory") << "Created MediaPool: UUID=" << uuid << ", name=" << name;
    
    return instance;
}

std::shared_ptr<Module> ModuleFactory::createTrackerSequencer(const std::string& uuid, const std::string& humanName) {
    if (!isValidUUID(uuid)) {
        ofLogError("ModuleFactory") << "Invalid UUID format: " << uuid;
        return nullptr;
    }
    
    if (isUUIDUsed(uuid)) {
        ofLogError("ModuleFactory") << "UUID already in use: " << uuid;
        return nullptr;
    }
    
    if (humanName.empty()) {
        ofLogError("ModuleFactory") << "Human name required when specifying UUID";
        return nullptr;
    }
    
    if (isHumanNameUsed(humanName)) {
        ofLogError("ModuleFactory") << "Human name already in use: " << humanName;
        return nullptr;
    }
    
    // Create instance
    auto instance = std::make_shared<TrackerSequencer>();
    
    // Register with explicit UUID
    uuidToName[uuid] = humanName;
    nameToUUID[humanName] = uuid;
    trackerSequencerCount++;
    
    ofLogNotice("ModuleFactory") << "Created TrackerSequencer with explicit UUID: " << uuid << ", name=" << humanName;
    
    return instance;
}

std::shared_ptr<Module> ModuleFactory::createMediaPool(const std::string& uuid, const std::string& humanName) {
    if (!isValidUUID(uuid)) {
        ofLogError("ModuleFactory") << "Invalid UUID format: " << uuid;
        return nullptr;
    }
    
    if (isUUIDUsed(uuid)) {
        ofLogError("ModuleFactory") << "UUID already in use: " << uuid;
        return nullptr;
    }
    
    if (humanName.empty()) {
        ofLogError("ModuleFactory") << "Human name required when specifying UUID";
        return nullptr;
    }
    
    if (isHumanNameUsed(humanName)) {
        ofLogError("ModuleFactory") << "Human name already in use: " << humanName;
        return nullptr;
    }
    
    // Create instance
    auto instance = std::make_shared<MediaPool>();
    
    // Register with explicit UUID
    uuidToName[uuid] = humanName;
    nameToUUID[humanName] = uuid;
    mediaPoolCount++;
    
    ofLogNotice("ModuleFactory") << "Created MediaPool with explicit UUID: " << uuid << ", name=" << humanName;
    
    return instance;
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
    trackerSequencerCount = 0;
    mediaPoolCount = 0;
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

std::string ModuleFactory::generateHumanName(const std::string& typeName) {
    int count = 0;
    if (typeName == "TrackerSequencer") {
        count = trackerSequencerCount;
    } else if (typeName == "MediaPool") {
        count = mediaPoolCount;
    }
    
    // Generate name: "TypeName_1", "TypeName_2", etc.
    // If that's taken, try with higher number
    std::string baseName = typeName + "_" + std::to_string(count + 1);
    std::string name = baseName;
    int suffix = count + 1;
    
    while (isHumanNameUsed(name)) {
        suffix++;
        name = typeName + "_" + std::to_string(suffix);
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

