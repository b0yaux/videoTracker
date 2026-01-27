#include "SessionManager.h"
#include "utils/Clock.h"  // Needed for Clock* member access (must be before SessionManager.h if SessionManager.h uses Clock)
#include "ProjectManager.h"
#include "ModuleRegistry.h"
#include "ModuleFactory.h"
#include "ParameterRouter.h"
#include "ConnectionManager.h"
#include "gui/ViewManager.h"
#include "gui/GUIManager.h"
#include "gui/ModuleGUI.h"
#include "modules/Module.h"
#include "ofMain.h"  // For ofGetElapsedTimef()
#include "ofLog.h"
#include "ofJson.h"
#include <imgui.h>
#include "ofFileUtils.h"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <set>
#include <vector>
#include <future>

//--------------------------------------------------------------
SessionManager::SessionManager(
    ProjectManager* projectManager,
    Clock* clock,
    ModuleRegistry* registry,
    ModuleFactory* factory,
    ParameterRouter* router,
    ConnectionManager* connectionManager,
    ViewManager* viewManager
) : projectManager_(projectManager), clock(clock), registry(registry), factory(factory), router(router), connectionManager_(connectionManager), viewManager_(viewManager), guiManager_(nullptr), currentSessionName_(""), pendingImGuiState_(""), pendingVisibilityState_(ofJson::object()), postLoadCallback_(nullptr), projectOpenedCallback_(nullptr) {
    if (!clock || !registry || !factory || !router) {
        ofLogError("SessionManager") << "SessionManager initialized with null pointers";
    }
}

//--------------------------------------------------------------
    // Default constructor implementation (already defined inline in header)
    // Note: viewManager_ is nullptr in default constructor

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
        ofLogNotice("SessionManager") << "Saving Clock BPM: " << clock->getBPM();
    } else {
        ofLogWarning("SessionManager") << "Clock is null, cannot save BPM to session";
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
    
    // Unified connections (audio, video, parameter, event)
    if (connectionManager_) {
        json["modules"]["connections"] = connectionManager_->toJson();
    }
    
    // GUI state
    json["gui"] = ofJson::object();
    
    // Module layouts (from ModuleGUI)
    json["gui"]["moduleLayouts"] = ofJson::object();
    std::map<std::string, ImVec2> layouts = ModuleGUI::getAllDefaultLayouts();
    for (const auto& [typeName, size] : layouts) {
        json["gui"]["moduleLayouts"][typeName] = ofJson::object();
        json["gui"]["moduleLayouts"][typeName]["width"] = size.x;
        json["gui"]["moduleLayouts"][typeName]["height"] = size.y;
    }
    
    // View state (panel visibility, current panel, etc.)
    if (viewManager_) {
        json["gui"]["viewState"] = ofJson::object();
        json["gui"]["viewState"]["fileBrowserVisible"] = viewManager_->isFileBrowserVisible();
        json["gui"]["viewState"]["consoleVisible"] = viewManager_->isConsoleVisible();
        json["gui"]["viewState"]["assetLibraryVisible"] = viewManager_->isAssetLibraryVisible();
        json["gui"]["viewState"]["currentFocusedWindow"] = viewManager_->getCurrentFocusedWindow();
        json["gui"]["viewState"]["masterModulesVisible"] = viewManager_->isMasterModulesVisible();
    }
    
    // Module instance visibility state
    if (guiManager_) {
        json["gui"]["visibleInstances"] = ofJson::object();
        
        // Save visible instances for all module types generically
        std::vector<ModuleType> types = { 
            ModuleType::SEQUENCER, 
            ModuleType::INSTRUMENT, 
            ModuleType::EFFECT, 
            ModuleType::UTILITY 
        };
        
        for (auto type : types) {
            std::string typeKey;
            switch (type) {
                case ModuleType::SEQUENCER: typeKey = "sequencer"; break;
                case ModuleType::INSTRUMENT: typeKey = "instrument"; break;
                case ModuleType::EFFECT: typeKey = "effect"; break;
                case ModuleType::UTILITY: typeKey = "utility"; break;
            }
            
            json["gui"]["visibleInstances"][typeKey] = ofJson::array();
            auto visible = guiManager_->getVisibleInstances(type);
            for (const auto& name : visible) {
                json["gui"]["visibleInstances"][typeKey].push_back(name);
            }
        }
    }
    
    // ImGui window state (docking, positions, sizes)
    // Save ImGui ini settings to memory and store in session JSON
    // This allows each session to have its own window layout
    ofLogNotice("SessionManager") << "DEBUG: Attempting to save ImGui window state...";
    
    // Check if ImGui is initialized
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) {
        ofLogError("SessionManager") << "DEBUG: ImGui context is null when trying to save state!";
    } else {
        ofLogNotice("SessionManager") << "DEBUG: ImGui context is valid";
    }
    
    size_t iniSize = 0;
    const char* iniData = ImGui::SaveIniSettingsToMemory(&iniSize);
    ofLogNotice("SessionManager") << "DEBUG: ImGui::SaveIniSettingsToMemory returned " << iniSize << " bytes";
    
    if (iniData && iniSize > 0) {
        // Show first 100 chars of the data for debugging
        std::string preview(iniData, std::min<size_t>(100, iniSize));
        ofLogNotice("SessionManager") << "DEBUG: ImGui data preview: " << preview;
        json["gui"]["imguiState"] = std::string(iniData, iniSize);
        ofLogNotice("SessionManager") << "✓ Saved ImGui window state (" << iniSize << " bytes) to session";
    } else {
        ofLogWarning("SessionManager") << "DEBUG: ImGui state is EMPTY! iniData=" << (void*)iniData << ", iniSize=" << iniSize;
    }
    
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
    if (version != SESSION_VERSION) {
        ofLogError("SessionManager") << "Session version mismatch: " << version 
                                     << " (expected " << SESSION_VERSION << ")";
        return false;
    }
    
    // Load clock
    if (clock && json.contains("clock")) {
        try {
            float bpmBefore = clock->getBPM();
            ofLogNotice("SessionManager") << "Loading Clock BPM from session (current: " << bpmBefore << ")";
            clock->fromJson(json["clock"]);
            float bpmAfter = clock->getBPM();
            ofLogNotice("SessionManager") << "Clock BPM loaded: " << bpmAfter << " (was: " << bpmBefore << ")";
        } catch (const std::exception& e) {
            ofLogError("SessionManager") << "Failed to load clock: " << e.what();
            return false;
        }
    } else {
        if (!clock) {
            ofLogWarning("SessionManager") << "Clock is null, cannot load BPM from session";
        } else if (!json.contains("clock")) {
            ofLogWarning("SessionManager") << "Session JSON does not contain 'clock' key, using default BPM";
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
        
        // Load parameter routing (after modules are loaded)
        if (router && modulesJson.contains("routing")) {
            try {
                if (!router->fromJson(modulesJson["routing"])) {
                    ofLogError("SessionManager") << "Failed to load parameter routing";
                    return false;
                }
            } catch (const std::exception& e) {
                ofLogError("SessionManager") << "Failed to load parameter routing: " << e.what();
                return false;
            }
        }
        
        // Load unified connections (after modules are loaded)
        if (connectionManager_ && modulesJson.contains("connections")) {
            try {
                // ConnectionManager::fromJson() will restore connections immediately
                // This establishes the physical connections but sets default volumes/opacities
                if (!connectionManager_->fromJson(modulesJson["connections"])) {
                    ofLogError("SessionManager") << "Failed to load module connections";
                    return false;
                }
            } catch (const std::exception& e) {
                ofLogError("SessionManager") << "Failed to load module connections: " << e.what();
                return false;
            }
            
            // CRITICAL: After ConnectionManager restores connections, restore connection-specific
            // parameters (volumes, opacities, blend modes) from module JSON data
            // ConnectionManager only establishes the connections, but doesn't restore the parameters
            if (modulesJson.contains("instances")) {
                ofLogNotice("SessionManager") << "Restoring connection parameters (volumes, opacities, blend modes)...";
                restoreMixerConnections(modulesJson["instances"]);
            }
        } else {
            // Fallback: Restore mixer connections from module data (backward compatibility)
            restoreMixerConnections(modulesJson["instances"]);
        }
        
        // Complete module restoration (for deferred operations like media loading)
        // This is called after all modules are loaded and connections are restored
        // but before GUI state is restored, so modules can prepare their state
        if (registry) {
            ofLogNotice("SessionManager") << "Completing module restoration...";
            size_t restoredCount = 0;
            registry->forEachModule([&](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
                if (module) {
                    try {
                        // Initialize module with isRestored=true to trigger restoration logic
                        module->initialize(clock, registry, connectionManager_, router, true);
                        restoredCount++;
                    } catch (const std::exception& e) {
                        ofLogError("SessionManager") << "Failed to initialize restored module " 
                                                     << name << " (" << uuid << "): " << e.what();
                    }
                }
            });
            ofLogNotice("SessionManager") << "Module restoration complete: " << restoredCount << " module(s) restored";
        }
    }
    
    // Load GUI state
    if (json.contains("gui") && json["gui"].is_object()) {
        auto guiJson = json["gui"];
        
        // Load module layouts
        if (guiJson.contains("moduleLayouts") && guiJson["moduleLayouts"].is_object()) {
            try {
                std::map<std::string, ImVec2> layouts;
                for (auto& [key, value] : guiJson["moduleLayouts"].items()) {
                    if (value.is_object() && value.contains("width") && value.contains("height")) {
                        float width = value["width"].get<float>();
                        float height = value["height"].get<float>();
                        layouts[key] = ImVec2(width, height);
                    }
                }
                ModuleGUI::setAllDefaultLayouts(layouts);
                ofLogNotice("SessionManager") << "Loaded " << layouts.size() << " module layout(s)";
            } catch (const std::exception& e) {
                ofLogError("SessionManager") << "Failed to load module layouts: " << e.what();
                // Non-fatal, continue
            }
        }
        
        // Load view state
        if (viewManager_ && guiJson.contains("viewState") && guiJson["viewState"].is_object()) {
            try {
                auto viewState = guiJson["viewState"];
                if (viewState.contains("fileBrowserVisible")) {
                    viewManager_->setFileBrowserVisible(viewState["fileBrowserVisible"].get<bool>());
                }
                if (viewState.contains("consoleVisible")) {
                    viewManager_->setConsoleVisible(viewState["consoleVisible"].get<bool>());
                }
                if (viewState.contains("assetLibraryVisible")) {
                    viewManager_->setAssetLibraryVisible(viewState["assetLibraryVisible"].get<bool>());
                }
                if (viewState.contains("masterModulesVisible")) {
                    viewManager_->setMasterModulesVisible(viewState["masterModulesVisible"].get<bool>());
                }
                if (viewState.contains("currentPanel")) {
                    int panelIndex = viewState["currentPanel"].get<int>();
                    // Legacy panel index - convert to window name (deprecated, will be removed)
                    std::vector<std::string> legacyPanelNames = {
                        "Clock ", "Audio Output", "Tracker Sequencer", "Media Pool", 
                        "File Browser", "Console", "Asset Library"
                    };
                    if (panelIndex >= 0 && panelIndex < static_cast<int>(legacyPanelNames.size())) {
                        viewManager_->navigateToWindow(legacyPanelNames[panelIndex]);
                    }
                }
                
                // New window name format (preferred)
                if (viewState.contains("currentFocusedWindow")) {
                    std::string windowName = viewState["currentFocusedWindow"].get<std::string>();
                    if (!windowName.empty()) {
                        viewManager_->navigateToWindow(windowName);
                    }
                }
                
                ofLogNotice("SessionManager") << "Loaded view state";
            } catch (const std::exception& e) {
                ofLogError("SessionManager") << "Failed to load view state: " << e.what();
                // Non-fatal, continue
            }
        }
        
        // Store ImGui window state for later loading (after ImGui is initialized)
        // We can't load it here because ImGui might not be initialized yet
        ofLogNotice("SessionManager") << "DEBUG: Checking for ImGui state in loaded session...";
        ofLogNotice("SessionManager") << "DEBUG: guiJson.contains('imguiState'): " << (guiJson.contains("imguiState") ? "YES" : "NO");
        
        if (guiJson.contains("imguiState")) {
            ofLogNotice("SessionManager") << "DEBUG: imguiState is_string: " << (guiJson["imguiState"].is_string() ? "YES" : "NO");
            if (!guiJson["imguiState"].is_string()) {
                ofLogWarning("SessionManager") << "DEBUG: imguiState exists but is not a string!";
            }
        }
        
        if (guiJson.contains("imguiState") && guiJson["imguiState"].is_string()) {
            pendingImGuiState_ = guiJson["imguiState"].get<std::string>();
            ofLogNotice("SessionManager") << "✓ Stored ImGui window state for later loading (" 
                                         << pendingImGuiState_.size() << " bytes)";
            // Show first 100 chars for debugging
            std::string preview = pendingImGuiState_.substr(0, std::min<size_t>(100, pendingImGuiState_.size()));
            ofLogNotice("SessionManager") << "DEBUG: Stored ImGui data preview: " << preview;
        } else {
            ofLogWarning("SessionManager") << "DEBUG: No ImGui state found in session - pendingImGuiState_ cleared";
            pendingImGuiState_.clear();
        }
        
        // Store visibility state for later restoration (after GUIs are created)
        // Visibility state will be restored in restoreVisibilityState() after syncWithRegistry()
        if (guiJson.contains("visibleInstances") && guiJson["visibleInstances"].is_object()) {
            pendingVisibilityState_ = guiJson["visibleInstances"];
            ofLogNotice("SessionManager") << "Stored module instance visibility state for later restoration";
        } else {
            pendingVisibilityState_ = ofJson::object();
        }
    }
    
    ofLogNotice("SessionManager") << "Session loaded successfully";
    
    // Sync GUIManager after modules are loaded to ensure GUIs are created
    // This must happen after modules are loaded but before visibility state is restored
    if (guiManager_) {
        // Ensure ConnectionManager is set on GUIManager before syncing
        // (it should already be set in ofApp::setup(), but this is a safety check)
        if (connectionManager_) {
            guiManager_->setConnectionManager(connectionManager_);
        }
        guiManager_->syncWithRegistry();
        ofLogNotice("SessionManager") << "Synced GUIManager after loading session modules";
        
        // Make all session-loaded modules visible by default
        // restoreVisibilityState() will then override with saved visibility if it exists
        if (registry) {
            registry->forEachModule([this](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
                if (module) {
                    // Skip system modules (they're always visible)
                    if (name != "masterAudioOut" && name != "masterVideoOut") {
                        guiManager_->setInstanceVisible(name, true);
                    }
                }
            });
            ofLogNotice("SessionManager") << "Made all session-loaded modules visible by default";
        }
        
        // Restore visibility state from saved session (will override defaults)
        restoreVisibilityState();
    }
    
    // Call post-load callback if set (for re-initializing audio streams, etc.)
    if (postLoadCallback_) {
        postLoadCallback_();
    }
    
    // Note: ImGui state loading is deferred until after setupGUI() is called
    // This is handled by calling loadPendingImGuiState() from ofApp::setup()
    
    return true;
}

//--------------------------------------------------------------
std::string SessionManager::resolveSessionPath(const std::string& sessionName) const {
    if (projectManager_ && projectManager_->isProjectOpen()) {
        // Use ProjectManager to resolve session path
        std::string path = projectManager_->getSessionPath(sessionName);
        if (!path.empty()) {
            return path;
        }
        // If session doesn't exist, create it
        if (projectManager_->createSessionFile(sessionName)) {
            return projectManager_->getSessionPath(sessionName);
        }
    }
    
    // Fallback: assume it's a full path
    return sessionName;
}

//--------------------------------------------------------------
bool SessionManager::saveSession(const std::string& sessionName) {
    ofLogNotice("SessionManager") << "DEBUG: saveSession called with sessionName: " << sessionName;
    
    if (!clock || !registry || !factory || !router) {
        ofLogError("SessionManager") << "Cannot save session: null pointers";
        return false;
    }
    
    std::string filePath = resolveSessionPath(sessionName);
    if (filePath.empty()) {
        ofLogError("SessionManager") << "Cannot resolve session path for: " << sessionName;
        return false;
    }
    
    ofLogNotice("SessionManager") << "DEBUG: Resolved session path: " << filePath;
    return saveSessionToPath(filePath);
}

//--------------------------------------------------------------
bool SessionManager::saveSessionToPath(const std::string& filePath) {
    ofLogNotice("SessionManager") << "DEBUG: saveSessionToPath called with filePath: " << filePath;
    
    if (!clock || !registry || !factory || !router) {
        ofLogError("SessionManager") << "Cannot save session: null pointers";
        return false;
    }
    
    try {
        // Create backup of existing session file before overwriting
        // This ensures we can recover if something goes wrong during save
        if (ofFile::doesFileExist(filePath)) {
            std::string backupPath = filePath + ".backup";
            // Overwrite existing backup (keep only one backup per session)
            if (ofFile::copyFromTo(filePath, backupPath, true, true)) {
                ofLogVerbose("SessionManager") << "Created backup: " << backupPath;
            } else {
                ofLogWarning("SessionManager") << "Failed to create backup, continuing with save anyway";
            }
        }
        
        ofJson json = serializeAll();
        
        // Add project root to session if ProjectManager is available
        if (projectManager_ && projectManager_->isProjectOpen()) {
            json["projectRoot"] = projectManager_->getProjectRoot();
        }
        
        ofFile file(filePath, ofFile::WriteOnly);
        if (!file.is_open()) {
            ofLogError("SessionManager") << "Failed to open file for writing: " << filePath;
            return false;
        }
        
        file << json.dump(4); // Pretty print with 4 spaces
        file.close();
        
        // Update current session name
        currentSessionName_ = ofFilePath::getFileName(filePath);
        
        ofLogNotice("SessionManager") << "Session saved to " << filePath;
        return true;
    } catch (const std::exception& e) {
        ofLogError("SessionManager") << "Exception while saving session: " << e.what();
        return false;
    }
}

//--------------------------------------------------------------
bool SessionManager::loadSession(const std::string& sessionName) {
    ofLogNotice("SessionManager") << "DEBUG: loadSession called with sessionName: " << sessionName;
    
    if (!clock || !registry || !factory || !router) {
        ofLogError("SessionManager") << "Cannot load session: null pointers";
        return false;
    }
    
    std::string filePath = resolveSessionPath(sessionName);
    if (filePath.empty()) {
        ofLogError("SessionManager") << "Cannot resolve session path for: " << sessionName;
        return false;
    }
    
    ofLogNotice("SessionManager") << "DEBUG: Resolved session path: " << filePath;
    return loadSessionFromPath(filePath);
}

//--------------------------------------------------------------
bool SessionManager::loadSessionFromPath(const std::string& filePath) {
    ofLogNotice("SessionManager") << "DEBUG: loadSessionFromPath called with filePath: " << filePath;
    
    if (!clock || !registry || !factory || !router) {
        ofLogError("SessionManager") << "Cannot load session: null pointers";
        return false;
    }
    
    ofFile file(filePath, ofFile::ReadOnly);
    if (!file.is_open()) {
        ofLogError("SessionManager") << "Failed to open file for reading: " << filePath;
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
    
    // Update current session name
    currentSessionName_ = ofFilePath::getFileName(filePath);
    
    // If session contains projectRoot, try to open project
    if (json.contains("projectRoot") && projectManager_) {
        std::string projectRoot = json["projectRoot"].get<std::string>();
        if (!projectManager_->isProjectOpen() || projectManager_->getProjectRoot() != projectRoot) {
            ofLogNotice("SessionManager") << "Opening project from session: " << projectRoot;
            if (projectManager_->openProject(projectRoot)) {
                // Notify that project was opened (for FileBrowser and AssetLibrary)
                if (projectOpenedCallback_) {
                    projectOpenedCallback_();
                }
            }
        }
    }
    
    return deserializeAll(json);
}

//--------------------------------------------------------------
void SessionManager::restoreMixerConnections(const ofJson& modulesJson) {
    if (!registry || !modulesJson.is_array()) {
        return;
    }
    
    // Find all mixer modules and restore their connections
    for (const auto& moduleJson : modulesJson) {
        if (!moduleJson.is_object() || !moduleJson.contains("type") || !moduleJson.contains("data")) {
            continue;
        }
        
        std::string type = moduleJson["type"].get<std::string>();
        std::string uuid = moduleJson.contains("uuid") ? moduleJson["uuid"].get<std::string>() : "";
        std::string name = moduleJson.contains("name") ? moduleJson["name"].get<std::string>() : "";
        
        // Restore connections for modules that support it (mixers, outputs)
        // Use generic Module interface - no need for type-specific casts
        auto module = registry->getModule(uuid.empty() ? name : uuid);
        if (module && moduleJson["data"].contains("connections")) {
            module->restoreConnections(moduleJson["data"]["connections"], registry);
        }
    }
    
    // Note: AudioOutput and VideoOutput now have internal mixers, so mixer→output
    // connections are no longer needed. Sources connect directly to outputs via
    // their connectModule() methods, which use the internal mixers.
    // Legacy mixer→output connections in saved sessions are handled automatically
    // by the output's internal mixer connection.
}

void SessionManager::restoreVisibilityState() {
    if (!guiManager_) {
        ofLogWarning("SessionManager") << "Cannot restore visibility: GUIManager is null";
        return;
    }
    
    if (pendingVisibilityState_.empty()) {
        ofLogNotice("SessionManager") << "No saved visibility state to restore - using defaults from syncWithRegistry()";
        return;
    }
    
    ofLogNotice("SessionManager") << "Restoring visibility state from saved session";
    
    try {
        // Generic restoration: iterate through all saved visibility categories
        // This handles all module types (mediaPool, tracker, audioOutput, videoOutput, audioMixer, videoMixer, etc.)
        for (auto& [category, instances] : pendingVisibilityState_.items()) {
            if (instances.is_array()) {
                for (const auto& name : instances) {
                    if (name.is_string()) {
                        std::string instanceName = name.get<std::string>();
                        // Verify module exists before making it visible
                        if (registry && registry->hasModule(instanceName)) {
                            guiManager_->setInstanceVisible(instanceName, true);
                            ofLogNotice("SessionManager") << "Restored " << category << " visibility: " << instanceName;
                        } else {
                            ofLogWarning("SessionManager") << "Cannot restore visibility for non-existent module: " << instanceName;
                        }
                    }
                }
            }
        }
        
        ofLogNotice("SessionManager") << "Restored module instance visibility state";
    } catch (const std::exception& e) {
        ofLogError("SessionManager") << "Failed to restore visibility state: " << e.what();
    }
}

void SessionManager::loadPendingImGuiState() {
    ofLogNotice("SessionManager") << "DEBUG: loadPendingImGuiState called, pendingImGuiState_.size() = " << pendingImGuiState_.size();
    
    // Only load if we have pending state and ImGui is initialized
    if (pendingImGuiState_.empty()) {
        ofLogWarning("SessionManager") << "DEBUG: No pending ImGui state, falling back to imgui.ini";
        // No pending state, try to load from imgui.ini as fallback
        std::string iniPath = ofToDataPath("imgui.ini", true);
        if (ofFile::doesFileExist(iniPath)) {
            try {
                ImGui::LoadIniSettingsFromDisk(iniPath.c_str());
                ofLogNotice("SessionManager") << "Loaded window layout from imgui.ini (fallback)";
            } catch (const std::exception& e) {
                ofLogError("SessionManager") << "Failed to load imgui.ini: " << e.what();
            }
        } else {
            ofLogWarning("SessionManager") << "DEBUG: No imgui.ini file found either";
        }
        return;
    }
    
    ofLogNotice("SessionManager") << "DEBUG: Loading ImGui state from session (" << pendingImGuiState_.size() << " bytes)";
    // Show first 100 chars for debugging
    std::string preview = pendingImGuiState_.substr(0, std::min<size_t>(100, pendingImGuiState_.size()));
    ofLogNotice("SessionManager") << "DEBUG: ImGui data preview: " << preview;
    
    // Check if ImGui is initialized (by checking if context exists)
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) {
        ofLogWarning("SessionManager") << "ImGui not initialized yet, deferring state load";
        return;
    }
    
    try {
        ImGui::LoadIniSettingsFromMemory(pendingImGuiState_.c_str(), pendingImGuiState_.size());
        ofLogNotice("SessionManager") << "Loaded ImGui window state from session (" 
                                      << pendingImGuiState_.size() << " bytes)";
        
        // After loading saved state, ensure all current modules have windows
        // This handles the case where modules were added after the session was saved
        // The GUIManager should have already synced, but we ensure windows are created
        if (guiManager_) {
            // Ensure ConnectionManager is set before syncing (safety check)
            if (connectionManager_) {
                guiManager_->setConnectionManager(connectionManager_);
            }
            guiManager_->syncWithRegistry();
            ofLogNotice("SessionManager") << "Synced GUIManager after loading ImGui state to ensure all modules have windows";
        }
        
        pendingImGuiState_.clear();  // Clear after successful load
    } catch (const std::exception& e) {
        ofLogError("SessionManager") << "Failed to load ImGui state: " << e.what();
        // Fall back to imgui.ini if it exists
        std::string iniPath = ofToDataPath("imgui.ini", true);
        if (ofFile::doesFileExist(iniPath)) {
            try {
                ImGui::LoadIniSettingsFromDisk(iniPath.c_str());
                ofLogNotice("SessionManager") << "Fell back to imgui.ini for window layout";
            } catch (const std::exception& e2) {
                ofLogError("SessionManager") << "Failed to load imgui.ini: " << e2.what();
            }
        }
        pendingImGuiState_.clear();  // Clear even on failure to avoid retrying
    }
}

void SessionManager::updateImGuiStateInSession() {
    // Check if ImGui is initialized (by checking if context exists)
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) {
        ofLogWarning("SessionManager") << "ImGui not initialized yet, cannot update state";
        return;
    }
    
    // Update the pending ImGui state with current state
    // This will be saved the next time the session is saved
    size_t iniSize = 0;
    const char* iniData = ImGui::SaveIniSettingsToMemory(&iniSize);
    if (iniData && iniSize > 0) {
        pendingImGuiState_ = std::string(iniData, iniSize);
        ofLogNotice("SessionManager") << "Updated session ImGui state (" << iniSize << " bytes)";
    } else {
        ofLogWarning("SessionManager") << "Failed to save ImGui state to memory";
    }
}

//--------------------------------------------------------------
bool SessionManager::initializeProjectAndSession(const std::string& dataPath) {
    if (!projectManager_) {
        ofLogError("SessionManager") << "Cannot initialize project: ProjectManager is null";
        return false;
    }
    
    // Try to open project or create new one
    // Check for project in data directory (bin/data/)
    bool projectOpened = false;
    std::string projectConfigPath = ofFilePath::join(dataPath, ".project.json");
    
    if (ofFile(projectConfigPath).exists()) {
        ofLogNotice("SessionManager") << "Found .project.json in data directory, opening project...";
        if (projectManager_->openProject(dataPath)) {
            projectOpened = true;
            ofLogNotice("SessionManager") << "✓ Project opened successfully: " << projectManager_->getProjectName();
            // Notify that project was opened (for FileBrowser and AssetLibrary)
            if (projectOpenedCallback_) {
                projectOpenedCallback_();
            }
        }
    }
    
    // If no project, create a default one in data directory
    if (!projectOpened) {
        std::string projectName = "Default Project";
        std::string projectPath = dataPath;
        if (projectManager_->createProject(projectPath, projectName)) {
            ofLogNotice("SessionManager") << "✓ Default project created in data directory";
            projectOpened = true;
            // Notify that project was opened (for FileBrowser and AssetLibrary)
            if (projectOpenedCallback_) {
                projectOpenedCallback_();
            }
        } else {
            ofLogWarning("SessionManager") << "Failed to create project, continuing without project";
        }
    }
    
    // Try to load default session if project is open
    bool sessionLoaded = false;
    if (projectManager_->isProjectOpen()) {
        // Try to load default session from project metadata, or first available session
        auto sessions = projectManager_->listSessions();
        std::string sessionToLoad;
        
        // Check if project has a default session set
        ofJson metadata = projectManager_->getProjectMetadata();
        if (metadata.contains("defaultSession") && metadata["defaultSession"].is_string()) {
            std::string defaultSessionName = metadata["defaultSession"].get<std::string>();
            if (!defaultSessionName.empty()) {
                // Check if default session exists in the sessions list
                auto it = std::find(sessions.begin(), sessions.end(), defaultSessionName);
                if (it != sessions.end()) {
                    sessionToLoad = defaultSessionName;
                }
            }
        }
        
        // Fallback to first available session if no default or default not found
        if (sessionToLoad.empty() && !sessions.empty()) {
            sessionToLoad = sessions[0];
        }
        
        if (!sessionToLoad.empty()) {
            ofLogNotice("SessionManager") << "Loading session: " << sessionToLoad;
            if (loadSession(sessionToLoad)) {
                sessionLoaded = true;
                ofLogNotice("SessionManager") << "✓ Session loaded successfully";
                // Apply ImGui layout from the loaded session immediately
                loadPendingImGuiState();
            }
        } else {
            // Create default session
            std::string defaultSessionName = projectManager_->generateDefaultSessionName();
            if (projectManager_->createSessionFile(defaultSessionName)) {
                ofLogNotice("SessionManager") << "Created default session: " << defaultSessionName;
            }
        }
    } else {
        // Fallback: try to load legacy session.json
        if (ofFile("session.json").exists()) {
            ofLogNotice("SessionManager") << "Found legacy session.json, loading...";
            if (loadSessionFromPath("session.json")) {
                sessionLoaded = true;
                ofLogNotice("SessionManager") << "✓ Legacy session loaded successfully";
                // Apply ImGui layout from the loaded session immediately
                loadPendingImGuiState();
            }
        }
    }
    
    return sessionLoaded;
}

//--------------------------------------------------------------
bool SessionManager::ensureDefaultModules(const std::vector<std::string>& defaultModuleTypes) {
    if (!factory || !registry) {
        ofLogError("SessionManager") << "Cannot ensure default modules: Factory or Registry is null";
        return false;
    }
    
    // Check if registry already has modules
    if (!registry->getAllUUIDs().empty()) {
        ofLogVerbose("SessionManager") << "Registry already has modules, skipping default creation";
        return true;
    }
    
    // Get existing names to avoid collisions
    std::set<std::string> existingNames;
    registry->forEachModule([&existingNames](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
        existingNames.insert(name);
    });
    
    // Create each default module type generically
    std::vector<std::string> createdNames;
    for (const auto& typeName : defaultModuleTypes) {
        // Generate instance name using factory
        std::string instanceName = factory->generateInstanceName(typeName, existingNames);
        existingNames.insert(instanceName); // Add to set for next generation
        
        // Create module using generic factory method
        auto module = factory->createModule(typeName, instanceName);
        if (!module) {
            ofLogError("SessionManager") << "Failed to create default module: " << typeName;
            return false;
        }
        
        // Get UUID from factory and register module
        std::string moduleUUID = factory->getUUID(instanceName);
        if (moduleUUID.empty()) {
            ofLogError("SessionManager") << "Failed to get UUID for module: " << instanceName;
            return false;
        }
        
        if (!registry->registerModule(moduleUUID, module, instanceName)) {
            ofLogError("SessionManager") << "Failed to register module in registry: " << instanceName;
            return false;
        }
        
        createdNames.push_back(instanceName);
    }
    
    ofLogNotice("SessionManager") << "Created default modules: ";
    for (size_t i = 0; i < createdNames.size(); ++i) {
        ofLogNotice("SessionManager") << "  - " << createdNames[i];
    }
    
    return true;
}

//--------------------------------------------------------------
bool SessionManager::setupGUI(GUIManager* guiManager) {
    if (!guiManager) {
        ofLogError("SessionManager") << "Cannot setup GUI: GUIManager is null";
        return false;
    }
    
    if (!registry || !router) {
        ofLogError("SessionManager") << "Cannot setup GUI: Registry or ParameterRouter is null";
        return false;
    }
    
    // Store reference for later use
    guiManager_ = guiManager;
    
    // Initialize GUIManager with registry and parameter router
    guiManager->setRegistry(registry);
    guiManager->setParameterRouter(router);
    
    // Sync GUIManager with registry (creates GUI objects for registered modules)
    guiManager->syncWithRegistry();
    
    // Initialize modules after GUI is set up (isRestored=false for new modules)
    // This replaces all module-specific setup logic in ofApp
    registry->forEachModule([this](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
        if (module && connectionManager_) {
            module->initialize(clock, registry, connectionManager_, router, false);
        }
    });
    
    // Load pending ImGui state if available, otherwise load default layout
    // This ensures we always have some layout loaded
    loadPendingImGuiState();
    
    return true;
}

//--------------------------------------------------------------
void SessionManager::enableAutoSave(float intervalSeconds, std::function<void()> onUpdateWindowTitle) {
    autoSaveEnabled_ = true;
    autoSaveInterval_ = intervalSeconds;
    lastAutoSave_ = ofGetElapsedTimef();
    onUpdateWindowTitle_ = onUpdateWindowTitle;
    ofLogNotice("SessionManager") << "Auto-save enabled with interval: " << intervalSeconds << " seconds";
}

//--------------------------------------------------------------
void SessionManager::update() {
    if (!autoSaveEnabled_ || saveInProgress_) {
        return;
    }
    
    float currentTime = ofGetElapsedTimef();
    float timeSinceLastSave = currentTime - lastAutoSave_;
    
    if (timeSinceLastSave >= autoSaveInterval_) {
        saveInProgress_ = true;
        
        // Auto-save to current session
        if (!currentSessionName_.empty()) {
            if (saveSession(currentSessionName_)) {
                ofLogVerbose("SessionManager") << "Auto-saved session: " << currentSessionName_;
                
                // Update window title if callback is provided
                if (onUpdateWindowTitle_) {
                    onUpdateWindowTitle_();
                }
            } else {
                ofLogWarning("SessionManager") << "Auto-save failed for session: " << currentSessionName_;
            }
        }
        
        lastAutoSave_ = currentTime;
        saveInProgress_ = false;
    }
}

//--------------------------------------------------------------
bool SessionManager::autoSaveOnExit() {
    if (!autoSaveEnabled_ || currentSessionName_.empty()) {
        return false;
    }
    
    ofLogNotice("SessionManager") << "Auto-saving session before exit: " << currentSessionName_;
    return saveSession(currentSessionName_);
}

