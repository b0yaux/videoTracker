#include "SessionManager.h"
#include "Clock.h"
#include "ModuleRegistry.h"
#include "ModuleFactory.h"
#include "ParameterRouter.h"
#include "ofLog.h"
#include "ofJson.h"
#include "ofFileUtils.h"
#include <ctime>
#include <iomanip>
#include <sstream>

//--------------------------------------------------------------
SessionManager::SessionManager(
    Clock* clock,
    ModuleRegistry* registry,
    ModuleFactory* factory,
    ParameterRouter* router
) : clock(clock), registry(registry), factory(factory), router(router) {
    if (!clock || !registry || !factory || !router) {
        ofLogError("SessionManager") << "SessionManager initialized with null pointers";
    }
}

//--------------------------------------------------------------
// Default constructor implementation (already defined inline in header)

//--------------------------------------------------------------
ofJson SessionManager::serializeAll() const {
    ofJson json;
    
    // Version and metadata
    json["version"] = SESSION_VERSION;
    
    // Timestamp
    auto now = std::time(nullptr);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%SZ");
    json["metadata"] = ofJson::object();
    json["metadata"]["modified"] = ss.str();
    
    // Clock state
    if (clock) {
        json["clock"] = clock->toJson();
    }
    
    // Modules
    if (registry) {
        json["modules"] = ofJson::object();
        json["modules"]["instances"] = registry->toJson();
    }
    
    // Parameter routing
    if (router) {
        json["modules"]["routing"] = router->toJson();
    }
    
    // Note: GUI state (layout, view state) can be added here in the future
    // For now, GUI layout is handled separately via ImGui's .ini system
    
    return json;
}

//--------------------------------------------------------------
bool SessionManager::deserializeAll(const ofJson& json) {
    if (!json.is_object()) {
        ofLogError("SessionManager") << "Invalid session format: expected object";
        return false;
    }
    
    // Check version
    std::string version = json.value("version", "");
    if (version.empty()) {
        // Legacy format - try migration
        ofLogNotice("SessionManager") << "Legacy format detected, attempting migration";
        return migrateLegacyFormat(json);
    }
    
    if (version != SESSION_VERSION) {
        ofLogWarning("SessionManager") << "Session version mismatch: " << version 
                                       << " (expected " << SESSION_VERSION << ")";
        // Continue anyway - might still work
    }
    
    // Load clock
    if (clock && json.contains("clock")) {
        try {
            clock->fromJson(json["clock"]);
        } catch (const std::exception& e) {
            ofLogError("SessionManager") << "Failed to load clock: " << e.what();
            return false;
        }
    }
    
    // Load modules
    if (registry && factory && json.contains("modules") && json["modules"].is_object()) {
        auto modulesJson = json["modules"];
        if (modulesJson.contains("instances")) {
            try {
                // Clear existing modules and factory state before loading
                // This prevents UUID conflicts when loading a session
                registry->clear();
                factory->clear();
                
                if (!registry->fromJson(modulesJson["instances"], *factory)) {
                    ofLogError("SessionManager") << "Failed to load modules";
                    return false;
                }
            } catch (const std::exception& e) {
                ofLogError("SessionManager") << "Failed to load modules: " << e.what();
                return false;
            }
        }
        
        // Load routing (after modules are loaded)
        if (router && modulesJson.contains("routing")) {
            try {
                if (!router->fromJson(modulesJson["routing"])) {
                    ofLogError("SessionManager") << "Failed to load routing";
                    return false;
                }
            } catch (const std::exception& e) {
                ofLogError("SessionManager") << "Failed to load routing: " << e.what();
                return false;
            }
        }
    }
    
    ofLogNotice("SessionManager") << "Session loaded successfully";
    return true;
}

//--------------------------------------------------------------
bool SessionManager::saveSession(const std::string& filename) {
    if (!clock || !registry || !factory || !router) {
        ofLogError("SessionManager") << "Cannot save session: null pointers";
        return false;
    }
    
    try {
        ofJson json = serializeAll();
        
        ofFile file(filename, ofFile::WriteOnly);
        if (!file.is_open()) {
            ofLogError("SessionManager") << "Failed to open file for writing: " << filename;
            return false;
        }
        
        file << json.dump(4); // Pretty print with 4 spaces
        file.close();
        
        ofLogNotice("SessionManager") << "Session saved to " << filename;
        return true;
    } catch (const std::exception& e) {
        ofLogError("SessionManager") << "Exception while saving session: " << e.what();
        return false;
    }
}

//--------------------------------------------------------------
bool SessionManager::loadSession(const std::string& filename) {
    if (!clock || !registry || !factory || !router) {
        ofLogError("SessionManager") << "Cannot load session: null pointers";
        return false;
    }
    
    ofFile file(filename, ofFile::ReadOnly);
    if (!file.is_open()) {
        ofLogError("SessionManager") << "Failed to open file for reading: " << filename;
        return false;
    }
    
    std::string jsonString = file.readToBuffer().getText();
    file.close();
    
    ofJson json;
    try {
        json = ofJson::parse(jsonString);
    } catch (const std::exception& e) {
        ofLogError("SessionManager") << "Failed to parse JSON: " << e.what();
        return false;
    }
    
    return deserializeAll(json);
}

//--------------------------------------------------------------
bool SessionManager::migrateLegacyFormat(const ofJson& json) {
    // Legacy format: tracker_sequencer_state.json
    // This is a TrackerSequencer-only save file
    // We can load it into a TrackerSequencer module if one exists
    
    ofLogNotice("SessionManager") << "Attempting to migrate legacy TrackerSequencer format";
    
    // Check if this looks like a TrackerSequencer state file
    if (json.contains("pattern") || json.contains("patterns") || json.contains("columnConfig")) {
        // This is a TrackerSequencer state file
        // Try to find an existing TrackerSequencer in the registry
        if (registry) {
            auto trackers = registry->getModulesByType(ModuleType::SEQUENCER);
            if (!trackers.empty()) {
                auto tracker = trackers[0];
                try {
                    tracker->fromJson(json);
                    ofLogNotice("SessionManager") << "Migrated legacy TrackerSequencer state";
                    return true;
                } catch (const std::exception& e) {
                    ofLogError("SessionManager") << "Failed to migrate TrackerSequencer state: " << e.what();
                    return false;
                }
            }
        }
    }
    
    ofLogError("SessionManager") << "Unknown legacy format or no TrackerSequencer found";
    return false;
}

