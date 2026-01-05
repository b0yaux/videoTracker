#include "SessionManager.h"
#include <fstream>
#include <chrono>
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
#include "modules/TrackerSequencer.h"
#include "core/PatternRuntime.h"
#include "ofMain.h"  // For ofGetElapsedTimef()
#include "ofLog.h"
#include "ofJson.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <fstream>
#include <chrono>
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
) : projectManager_(projectManager), clock(clock), registry(registry), factory(factory), router(router), connectionManager_(connectionManager), viewManager_(viewManager), guiManager_(nullptr), currentSessionName_(""), postLoadCallback_(nullptr), projectOpenedCallback_(nullptr), pendingImGuiState_(""), pendingVisibilityState_(ofJson::object()) {
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
    
    // PatternRuntime state (Phase 2: Save patterns from Runtime, not TrackerSequencer)
    if (patternRuntime_) {
        json["patternRuntime"] = patternRuntime_->toJson();
        ofLogNotice("SessionManager") << "Saving PatternRuntime patterns";
    } else {
        ofLogWarning("SessionManager") << "PatternRuntime is null, cannot save patterns to session";
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
        json["gui"]["visibleInstances"]["mediaPool"] = ofJson::array();
        json["gui"]["visibleInstances"]["tracker"] = ofJson::array();
        json["gui"]["visibleInstances"]["audioOutput"] = ofJson::array();
        json["gui"]["visibleInstances"]["videoOutput"] = ofJson::array();
        json["gui"]["visibleInstances"]["audioMixer"] = ofJson::array();
        json["gui"]["visibleInstances"]["videoMixer"] = ofJson::array();
        
        auto visibleInstruments = guiManager_->getVisibleInstances(ModuleType::INSTRUMENT);
        for (const auto& name : visibleInstruments) {
            json["gui"]["visibleInstances"]["mediaPool"].push_back(name);
        }
        
        auto visibleTracker = guiManager_->getVisibleInstances(ModuleType::SEQUENCER);
        for (const auto& name : visibleTracker) {
            json["gui"]["visibleInstances"]["tracker"].push_back(name);
        }
        
        auto visibleUtility = guiManager_->getVisibleInstances(ModuleType::UTILITY);
        // Separate utility types by checking module metadata (generic approach)
        for (const auto& name : visibleUtility) {
            auto module = registry->getModule(name);
            if (module) {
                auto metadata = module->getMetadata();
                std::string typeName = metadata.typeName;
                if (typeName == "AudioOutput") {
                    json["gui"]["visibleInstances"]["audioOutput"].push_back(name);
                } else if (typeName == "VideoOutput") {
                    json["gui"]["visibleInstances"]["videoOutput"].push_back(name);
                } else if (typeName == "AudioMixer") {
                    json["gui"]["visibleInstances"]["audioMixer"].push_back(name);
                } else if (typeName == "VideoMixer") {
                    json["gui"]["visibleInstances"]["videoMixer"].push_back(name);
                }
            }
        }
    }
    
    // ImGui window state (docking, positions, sizes)
    // Only save if layout was actually loaded from session, or preserve existing session layout
    if (!sessionLayoutWasLoaded_) {
        // Layout was never loaded - check if we should preserve existing session layout
        // Use originalImGuiState_ which contains the original layout from session JSON
        // Check if it's meaningful before preserving it
        bool shouldPreserve = false;
        std::string layoutToPreserve;
        
        if (!originalImGuiState_.empty()) {
            // Check if original layout is meaningful (not empty)
            bool originalIsMeaningful = originalImGuiState_.size() > 50 || 
                                       originalImGuiState_.find("[Window") != std::string::npos;
            if (originalIsMeaningful) {
                shouldPreserve = true;
                layoutToPreserve = originalImGuiState_;
                ofLogNotice("SessionManager") << "Preserving original meaningful session layout (was never loaded): " 
                                             << layoutToPreserve.size() << " bytes";
            } else {
                ofLogNotice("SessionManager") << "Original session layout is empty (" << originalImGuiState_.size() 
                                             << " bytes), not preserving";
            }
        } else if (!pendingImGuiState_.empty()) {
            // Fallback to pendingImGuiState_ if originalImGuiState_ is not available
            // But still check if it's meaningful
            bool pendingIsMeaningful = pendingImGuiState_.size() > 50 || 
                                      pendingImGuiState_.find("[Window") != std::string::npos;
            if (pendingIsMeaningful) {
                shouldPreserve = true;
                layoutToPreserve = pendingImGuiState_;
                ofLogNotice("SessionManager") << "Preserving pending meaningful session layout (was never loaded): " 
                                             << layoutToPreserve.size() << " bytes";
            } else {
                ofLogNotice("SessionManager") << "Pending session layout is empty (" << pendingImGuiState_.size() 
                                             << " bytes), not preserving";
            }
        }
        
        if (shouldPreserve) {
            json["gui"]["imguiState"] = layoutToPreserve;
        } else {
            // Don't add imguiState to JSON - this preserves whatever was in the session file before
            // (i.e., if there was a good layout, it stays; if it was empty, it stays empty)
            ofLogNotice("SessionManager") << "Skipping ImGui state save: Layout was never loaded from session (Command Shell only)";
        }
        return json;
    }
    
    // Layout was loaded - proceed with normal save
    // Check if ImGui is initialized
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) {
        ofLogError("SessionManager") << "Cannot save ImGui state: ImGui context is null";
        return json;
    }
    
    size_t iniSize = 0;
    const char* iniData = ImGui::SaveIniSettingsToMemory(&iniSize);
    
    if (iniData && iniSize > 0) {
        // Check if current state is meaningful (contains window configurations, not just empty docking data)
        std::string currentState(iniData, iniSize);
        bool currentStateIsMeaningful = iniSize > 50 || currentState.find("[Window") != std::string::npos;
        
        if (!currentStateIsMeaningful) {
            // Current state is empty - don't save it, preserve existing layout instead
            ofLogNotice("SessionManager") << "Current ImGui state is empty (" << iniSize 
                                         << " bytes), preserving existing session layout instead of saving empty state";
            if (!pendingImGuiState_.empty()) {
                // Preserve existing layout if we have one
                json["gui"]["imguiState"] = pendingImGuiState_;
                ofLogNotice("SessionManager") << "Preserved existing session layout (" << pendingImGuiState_.size() << " bytes)";
            }
            // If no existing layout, don't add imguiState to JSON (preserves whatever was there before)
            return json;
        }
        
        json["gui"]["imguiState"] = std::string(iniData, iniSize);
        ofLogNotice("SessionManager") << "✓ Saved ImGui window state (" << iniSize << " bytes) to session";
    } else {
        ofLogWarning("SessionManager") << "ImGui state is empty, cannot save to session";
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
        
        // Load PatternRuntime (Phase 2: Load patterns from Runtime, migrate TrackerSequencer patterns)
        if (patternRuntime_ && json.contains("patternRuntime") && json["patternRuntime"].is_object()) {
            try {
                ofLogNotice("SessionManager") << "Loading PatternRuntime patterns from session...";
                patternRuntime_->fromJson(json["patternRuntime"]);
                ofLogNotice("SessionManager") << "PatternRuntime patterns loaded";
            } catch (const std::exception& e) {
                ofLogError("SessionManager") << "Failed to load PatternRuntime: " << e.what();
                // Non-fatal, continue (patterns may be in TrackerSequencer for migration)
            }
        }
        
        // Complete module restoration (for deferred operations like media loading)
        // This is called after all modules are loaded and connections are restored
        // but before GUI state is restored, so modules can prepare their state
        if (registry) {
            ofLogNotice("SessionManager") << "Completing module restoration...";
            if (!patternRuntime_) {
                ofLogError("SessionManager") << "CRITICAL: PatternRuntime is null during module restoration! "
                                              << "Modules will not have access to patterns.";
            }
            size_t restoredCount = 0;
            registry->forEachModule([&](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
                if (module) {
                    try {
                        // Initialize module with PatternRuntime and isRestored=true to trigger restoration logic
                        if (!patternRuntime_) {
                            ofLogWarning("SessionManager") << "Initializing module '" << name 
                                                            << "' without PatternRuntime (PatternRuntime is null)";
                        }
                        module->initialize(clock, registry, connectionManager_, router, patternRuntime_, true);
                        restoredCount++;
                    } catch (const std::exception& e) {
                        ofLogError("SessionManager") << "Failed to initialize restored module " 
                                                     << name << " (" << uuid << "): " << e.what();
                    }
                }
            });
            ofLogNotice("SessionManager") << "Module restoration complete: " << restoredCount << " module(s) restored";
            
            // Phase 2: Restore sequencer bindings from PatternRuntime AFTER modules are initialized
            if (patternRuntime_) {
                auto sequencerNames = patternRuntime_->getSequencerNames();
                // #region agent log
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"D\",\"location\":\"SessionManager.cpp:395\",\"message\":\"Restoring sequencer bindings\",\"data\":{\"sequencerCount\":" << sequencerNames.size() << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n";
                }
                // #endregion
                for (const auto& seqName : sequencerNames) {
                    auto binding = patternRuntime_->getSequencerBinding(seqName);
                    // #region agent log
                    {
                        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"D\",\"location\":\"SessionManager.cpp:399\",\"message\":\"Processing sequencer binding\",\"data\":{\"sequencerName\":\"" << seqName << "\",\"patternName\":\"" << binding.patternName << "\",\"chainName\":\"" << binding.chainName << "\",\"chainEnabled\":" << (binding.chainEnabled ? "true" : "false") << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n";
                    }
                    // #endregion
                    
                    // CRITICAL: Only restore bindings for sequencers that match the sequencer name
                    // This prevents applying bindings from one sequencer to another
                    auto module = registry->getModule(seqName);
                    auto tracker = std::dynamic_pointer_cast<class TrackerSequencer>(module);
                    if (tracker) {
                        // CRITICAL: Verify sequencer name matches (prevent cross-binding)
                        if (tracker->getName() != seqName) {
                            ofLogWarning("SessionManager") << "Sequencer name mismatch: binding for '" << seqName 
                                                           << "' but tracker name is '" << tracker->getName() << "', skipping";
                            continue;
                        }
                        
                        // #region agent log
                        {
                            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"D\",\"location\":\"SessionManager.cpp:404\",\"message\":\"TrackerSequencer found\",\"data\":{\"sequencerName\":\"" << seqName << "\",\"trackerName\":\"" << tracker->getName() << "\"},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n";
                        }
                        // #endregion
                        // Restore pattern binding
                        if (!binding.patternName.empty() && patternRuntime_->patternExists(binding.patternName)) {
                            tracker->bindToPattern(binding.patternName);
                        }
                        
                        // CRITICAL: Only restore chain binding if this sequencer actually has one
                        // This prevents applying a chain from one sequencer to all sequencers
                        if (!binding.chainName.empty() && patternRuntime_->chainExists(binding.chainName)) {
                            // #region agent log
                            {
                                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"D\",\"location\":\"SessionManager.cpp:417\",\"message\":\"Before bindToChain\",\"data\":{\"sequencerName\":\"" << seqName << "\",\"chainName\":\"" << binding.chainName << "\"},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n";
                                logFile.close();
                            }
                            // #endregion
                            
                            // CRITICAL: Set chain enabled state BEFORE binding to ensure it's correct
                            // This must happen before bindToChain() so the binding is set up correctly
                            PatternChain* chain = patternRuntime_->getChain(binding.chainName);
                            if (chain) {
                                // Restore chain enabled state from binding (or use chain's current state)
                                if (binding.chainEnabled) {
                                    patternRuntime_->setSequencerChainEnabled(seqName, true);
                                } else if (chain->isEnabled()) {
                                    // Chain is enabled but binding says disabled - honor chain state
                                    patternRuntime_->setSequencerChainEnabled(seqName, true);
                                }
                            }
                            
                            // Now bind to chain (this will sync chain index with bound pattern)
                            // Note: bindToChain() sets boundChainName_ internally
                            tracker->bindToChain(binding.chainName);
                            
                            // CRITICAL: Verify boundChainName_ was set correctly
                            // If fromJson() loaded boundChainName_, it should match
                            // If not, bindToChain() should have set it
                            if (tracker->getBoundChainName() != binding.chainName) {
                                ofLogWarning("SessionManager") << "Chain binding mismatch for sequencer '" << seqName 
                                                              << "': expected '" << binding.chainName 
                                                              << "', got '" << tracker->getBoundChainName() << "'";
                            }
                            
                            // #region agent log
                            {
                                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"D\",\"location\":\"SessionManager.cpp:440\",\"message\":\"After bindToChain\",\"data\":{\"sequencerName\":\"" << seqName << "\",\"chainName\":\"" << binding.chainName << "\",\"boundChainName\":\"" << tracker->getBoundChainName() << "\",\"chainMatches\":" << (tracker->getBoundChainName() == binding.chainName ? "true" : "false") << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n";
                                logFile.close();
                            }
                            // #endregion
                        }
                        
                        ofLogVerbose("SessionManager") << "Restored bindings for sequencer '" << seqName 
                                                       << "': pattern='" << binding.patternName 
                                                       << "', chain='" << binding.chainName << "'";
                    } else {
                        // #region agent log
                        {
                            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"D\",\"location\":\"SessionManager.cpp:420\",\"message\":\"TrackerSequencer NOT found\",\"data\":{\"sequencerName\":\"" << seqName << "\",\"moduleFound\":" << (module ? "true" : "false") << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n";
                        }
                        // #endregion
                        ofLogWarning("SessionManager") << "Sequencer binding found for '" << seqName 
                                                       << "' but module not found in registry";
                    }
                }
            }
            
            // Phase 3: Migrate TrackerSequencer patterns to PatternRuntime (for legacy sessions)
            // Only migrate if PatternRuntime is empty (new format sessions already have patterns in Runtime)
            if (patternRuntime_ && modulesJson.contains("instances")) {
                // Check if PatternRuntime already has patterns (new format session)
                bool runtimeHasPatterns = !patternRuntime_->getPatternNames().empty();
                
                if (!runtimeHasPatterns) {
                    ofLogNotice("SessionManager") << "PatternRuntime is empty, checking for legacy patterns to migrate...";
                    size_t migratedCount = 0;
                    
                    // Check each module's JSON for patterns
                    // Note: Module JSON structure is: {"uuid": "...", "name": "...", "type": "...", "data": {...}}
                    // Patterns are stored in moduleJson["data"]["patterns"] for legacy sessions
                    for (auto& [uuid, moduleJson] : modulesJson["instances"].items()) {
                        if (moduleJson.contains("type") && moduleJson["type"].get<std::string>() == "TrackerSequencer") {
                            // Check if this TrackerSequencer has patterns in its data JSON (legacy format)
                            // FIX: Patterns are in moduleJson["data"]["patterns"], not moduleJson["patterns"]
                            if (moduleJson.contains("data") && 
                                moduleJson["data"].is_object() &&
                                moduleJson["data"].contains("patterns") && 
                                moduleJson["data"]["patterns"].is_array()) {
                                
                                auto tracker = std::dynamic_pointer_cast<class TrackerSequencer>(registry->getModule(uuid));
                                if (tracker) {
                                    std::string trackerName = tracker->getName();
                                    auto patternsArray = moduleJson["data"]["patterns"];  // FIX: Access from "data" object
                                    
                                    if (patternsArray.empty()) {
                                        ofLogVerbose("SessionManager") << "TrackerSequencer '" << trackerName 
                                                                       << "' has empty patterns array, skipping";
                                        continue;
                                    }
                                    
                                    ofLogNotice("SessionManager") << "Found " << patternsArray.size() 
                                                                  << " legacy patterns in TrackerSequencer '" << trackerName << "'";
                                    
                                    // Migrate each pattern to PatternRuntime
                                    std::vector<std::string> migratedNames;
                                    for (size_t i = 0; i < patternsArray.size(); ++i) {
                                        try {
                                            Pattern p(16);  // Default step count, will be updated from JSON
                                            p.fromJson(patternsArray[i]);
                                            // Use simple pattern naming (P0, P1, P2, etc.) - let PatternRuntime generate the name
                                            std::string actualName = patternRuntime_->addPattern(p, "");
                                            
                                            // PatternRuntime will generate a unique name
                                            if (!actualName.empty()) {
                                                migratedNames.push_back(actualName);
                                                ofLogVerbose("SessionManager") << "Migrated pattern '" << actualName 
                                                                               << "' from TrackerSequencer '" << trackerName << "'";
                                            } else {
                                                ofLogWarning("SessionManager") << "Failed to add pattern to PatternRuntime during migration";
                                            }
                                        } catch (const std::exception& e) {
                                            ofLogError("SessionManager") << "Failed to migrate pattern " << i 
                                                                          << " from TrackerSequencer '" << trackerName << "': " << e.what();
                                        }
                                    }
                                    
                                    // Bind to first migrated pattern (or existing pattern if migration skipped)
                                    if (!migratedNames.empty()) {
                                        tracker->bindToPattern(migratedNames[0]);
                                        migratedCount++;
                                        ofLogNotice("SessionManager") << "Migrated " << patternsArray.size() 
                                                                      << " patterns from TrackerSequencer '" << trackerName 
                                                                      << "' to PatternRuntime, bound to '" << migratedNames[0] << "'";
                                    } else {
                                        ofLogWarning("SessionManager") << "No patterns migrated from TrackerSequencer '" 
                                                                      << trackerName << "' (all patterns failed to migrate)";
                                    }
                                } else {
                                    ofLogWarning("SessionManager") << "TrackerSequencer module not found for UUID: " << uuid;
                                }
                            } else {
                                // No patterns found in this TrackerSequencer (might be new format or empty)
                                ofLogVerbose("SessionManager") << "TrackerSequencer module has no legacy patterns to migrate";
                            }
                        }
                    }
                    
                    if (migratedCount > 0) {
                        ofLogNotice("SessionManager") << "Migration complete: " << migratedCount << " TrackerSequencer(s) migrated to PatternRuntime";
                    } else {
                        ofLogNotice("SessionManager") << "No legacy patterns found to migrate (session may already be in new format)";
                    }
                } else {
                    ofLogNotice("SessionManager") << "PatternRuntime already has patterns (new format session), skipping migration";
                }
                
                // CRITICAL: Reload pattern chains for all TrackerSequencers after migration/load
                // Pattern chains were loaded in fromJson() before patterns existed in PatternRuntime,
                // so we need to reload them now that patterns are available
                if (patternRuntime_) {
                    auto patternNames = patternRuntime_->getPatternNames();
                    if (!patternNames.empty()) {
                        ofLogNotice("SessionManager") << "Reloading pattern chains with " << patternNames.size() << " available patterns...";
                        size_t reloadedCount = 0;
                        
                        for (auto& [uuid, moduleJson] : modulesJson["instances"].items()) {
                            if (moduleJson.contains("type") && moduleJson["type"].get<std::string>() == "TrackerSequencer") {
                                auto tracker = std::dynamic_pointer_cast<class TrackerSequencer>(registry->getModule(uuid));
                                if (tracker && moduleJson.contains("data") && moduleJson["data"].is_object()) {
                                    // Reload pattern chain from JSON with now-available pattern names
                                    if (moduleJson["data"].contains("patternChain")) {
                                        try {
                                            // Get the pattern chain JSON from the module data
                                            auto chainJson = moduleJson["data"]["patternChain"];
                                            // Reload pattern chain with available pattern names
                                            tracker->reloadPatternChain(chainJson, patternNames);
                                            
                                            // CRITICAL: After reloading pattern chain, sync current chain index with bound pattern
                                            // This ensures the GUI displays the correct active pattern
                                            std::string boundName = tracker->getCurrentPatternName();
                                            if (!boundName.empty() && tracker->getUsePatternChain()) {
                                                const auto& chain = tracker->getPatternChain();
                                                for (size_t i = 0; i < chain.size(); ++i) {
                                                    if (chain[i] == boundName) {
                                                        tracker->setCurrentChainIndex((int)i);
                                                        ofLogVerbose("SessionManager") << "Synced chain index to " << i 
                                                                                       << " for TrackerSequencer '" << tracker->getName() << "' after chain reload";
                                                        break;
                                                    }
                                                }
                                            }
                                            
                                            reloadedCount++;
                                            ofLogVerbose("SessionManager") << "Reloaded pattern chain for TrackerSequencer '" 
                                                                           << tracker->getName() << "'";
                                        } catch (const std::exception& e) {
                                            ofLogWarning("SessionManager") << "Failed to reload pattern chain for TrackerSequencer '" 
                                                                           << tracker->getName() << "': " << e.what();
                                        }
                                    }
                                }
                            }
                        }
                        
                        if (reloadedCount > 0) {
                            ofLogNotice("SessionManager") << "Reloaded pattern chains for " << reloadedCount << " TrackerSequencer(s)";
                        }
                    }
                }
            }
            
            // CRITICAL: Always validate TrackerSequencer bindings AFTER migration/pattern loading
            // This ensures all TrackerSequencers have valid pattern bindings regardless of session format
            // This runs OUTSIDE the migration block so it always executes
            if (patternRuntime_ && modulesJson.contains("instances")) {
                ofLogNotice("SessionManager") << "Validating TrackerSequencer pattern bindings...";
                auto patternNames = patternRuntime_->getPatternNames();
                
                for (auto& [uuid, moduleJson] : modulesJson["instances"].items()) {
                    if (moduleJson.contains("type") && moduleJson["type"].get<std::string>() == "TrackerSequencer") {
                        auto tracker = std::dynamic_pointer_cast<class TrackerSequencer>(registry->getModule(uuid));
                        if (tracker) {
                            std::string boundName = tracker->getCurrentPatternName();
                            
                            if (boundName.empty()) {
                                // No pattern bound - bind to first available pattern or create default
                                if (!patternNames.empty()) {
                                    ofLogNotice("SessionManager") << "TrackerSequencer '" << tracker->getName() 
                                                                    << "' has no bound pattern, binding to first available pattern: " << patternNames[0];
                                    tracker->bindToPattern(patternNames[0]);
                                } else {
                                    // No patterns exist - create default pattern
                                    Pattern defaultPattern(16);
                                    // Use simple pattern naming (P0, P1, P2, etc.) - let PatternRuntime generate the name
                                    std::string actualName = patternRuntime_->addPattern(defaultPattern, "");
                                    if (!actualName.empty()) {
                                        tracker->bindToPattern(actualName);
                                        ofLogNotice("SessionManager") << "Created default pattern '" << actualName 
                                                                      << "' for TrackerSequencer '" << tracker->getName() << "'";
                                    }
                                }
                            } else if (!patternRuntime_->patternExists(boundName)) {
                                // Bound pattern doesn't exist - bind to first available pattern or create default
                                if (!patternNames.empty()) {
                                    ofLogWarning("SessionManager") << "TrackerSequencer '" << tracker->getName() 
                                                                    << "' bound to non-existent pattern '" << boundName 
                                                                    << "', binding to first available pattern: " << patternNames[0];
                                    tracker->bindToPattern(patternNames[0]);
                                } else {
                                    // No patterns exist - create default pattern
                                    Pattern defaultPattern(16);
                                    // Use simple pattern naming (P0, P1, P2, etc.) - let PatternRuntime generate the name
                                    std::string actualName = patternRuntime_->addPattern(defaultPattern, "");
                                    if (!actualName.empty()) {
                                        tracker->bindToPattern(actualName);
                                        ofLogNotice("SessionManager") << "Created default pattern '" << actualName 
                                                                      << "' for TrackerSequencer '" << tracker->getName() << "'";
                                    }
                                }
                            } else {
                                // Pattern exists and is valid - verify binding worked
                                ofLogVerbose("SessionManager") << "TrackerSequencer '" << tracker->getName() 
                                                                << "' correctly bound to pattern '" << boundName << "'";
                                
                                // CRITICAL: Sync pattern chain current index with bound pattern after restoration
                                // This ensures the GUI displays the correct active pattern in the chain
                                const auto& chain = tracker->getPatternChain();
                                if (tracker->getUsePatternChain() && !chain.empty()) {
                                    for (size_t i = 0; i < chain.size(); ++i) {
                                        if (chain[i] == boundName) {
                                            tracker->setCurrentChainIndex((int)i);
                                            ofLogVerbose("SessionManager") << "Synced chain index to " << i 
                                                                           << " for TrackerSequencer '" << tracker->getName() << "'";
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                ofLogNotice("SessionManager") << "Pattern binding validation complete";
            }
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
        // Reset flag - will be set to true when layout is actually loaded
        sessionLayoutWasLoaded_ = false;
        
        if (guiJson.contains("imguiState") && guiJson["imguiState"].is_string()) {
            pendingImGuiState_ = guiJson["imguiState"].get<std::string>();
            originalImGuiState_ = pendingImGuiState_;  // Preserve original for later reload if needed
            ofLogNotice("SessionManager") << "✓ Stored ImGui window state for later loading (" 
                                         << pendingImGuiState_.size() << " bytes)";
        } else {
            pendingImGuiState_.clear();
            originalImGuiState_.clear();
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
    
    // Update ImGui state in session before serializing
    updateImGuiStateInSession();
    
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
    
    // First, try to migrate any legacy files into the session
    migrateLegacyFiles(filePath);
    
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
    
    // Handle legacy data in session
    if (json.contains("legacy") && json["legacy"].is_object()) {
        auto legacy = json["legacy"];
        
        // Migrate legacy TrackerSequencer state
        if (legacy.contains("trackerSequencerState") && registry) {
            ofLogNotice("SessionManager") << "Migrating legacy TrackerSequencer state from session";
            auto trackers = registry->getModulesByType(ModuleType::SEQUENCER);
            if (!trackers.empty()) {
                try {
                    trackers[0]->fromJson(legacy["trackerSequencerState"]);
                    ofLogNotice("SessionManager") << "Migrated TrackerSequencer state";
                } catch (const std::exception& e) {
                    ofLogError("SessionManager") << "Failed to migrate TrackerSequencer state: " << e.what();
                }
            }
        }
        
        // Migrate legacy sequencer state (similar to trackerSequencerState)
        if (legacy.contains("sequencerState") && registry) {
            ofLogNotice("SessionManager") << "Migrating legacy sequencer state from session";
            auto trackers = registry->getModulesByType(ModuleType::SEQUENCER);
            if (!trackers.empty()) {
                try {
                    trackers[0]->fromJson(legacy["sequencerState"]);
                    ofLogNotice("SessionManager") << "Migrated sequencer state";
                } catch (const std::exception& e) {
                    ofLogError("SessionManager") << "Failed to migrate sequencer state: " << e.what();
                }
            }
        }
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

//--------------------------------------------------------------
bool SessionManager::migrateLegacyFiles(const std::string& sessionPath) {
    ofLogNotice("SessionManager") << "Checking for legacy state files to migrate...";
    
    std::string dataPath = ofToDataPath("", true);
    bool migrated = false;
    ofJson consolidatedJson;
    
    // Check if we already have a session file
    bool hasSessionFile = ofFile::doesFileExist(sessionPath);
    if (hasSessionFile) {
        // Load existing session
        ofFile file(sessionPath, ofFile::ReadOnly);
        if (file.is_open()) {
            std::string jsonString = file.readToBuffer().getText();
            file.close();
            try {
                consolidatedJson = ofJson::parse(jsonString);
                ofLogNotice("SessionManager") << "Loaded existing session file for migration";
            } catch (const std::exception& e) {
                ofLogWarning("SessionManager") << "Failed to parse existing session: " << e.what();
                consolidatedJson = ofJson::object();
            }
        }
    } else {
        // Start with empty session structure
        consolidatedJson["version"] = SESSION_VERSION;
        consolidatedJson["metadata"] = ofJson::object();
        consolidatedJson["modules"] = ofJson::object();
        consolidatedJson["gui"] = ofJson::object();
    }
    
    // Migrate tracker_sequencer_state.json
    std::string trackerStatePath = ofToDataPath("tracker_sequencer_state.json", true);
    if (ofFile::doesFileExist(trackerStatePath)) {
        ofLogNotice("SessionManager") << "Found tracker_sequencer_state.json, migrating...";
        ofFile file(trackerStatePath, ofFile::ReadOnly);
        if (file.is_open()) {
            std::string jsonString = file.readToBuffer().getText();
            file.close();
            try {
                ofJson trackerJson = ofJson::parse(jsonString);
                // Store in a legacy section for migration during load
                if (!consolidatedJson.contains("legacy")) {
                    consolidatedJson["legacy"] = ofJson::object();
                }
                consolidatedJson["legacy"]["trackerSequencerState"] = trackerJson;
                migrated = true;
                ofLogNotice("SessionManager") << "Migrated tracker_sequencer_state.json";
            } catch (const std::exception& e) {
                ofLogError("SessionManager") << "Failed to parse tracker_sequencer_state.json: " << e.what();
            }
        }
    }
    
    // Migrate sequencer_state.json
    std::string sequencerStatePath = ofToDataPath("sequencer_state.json", true);
    if (ofFile::doesFileExist(sequencerStatePath)) {
        ofLogNotice("SessionManager") << "Found sequencer_state.json, migrating...";
        ofFile file(sequencerStatePath, ofFile::ReadOnly);
        if (file.is_open()) {
            std::string jsonString = file.readToBuffer().getText();
            file.close();
            try {
                ofJson sequencerJson = ofJson::parse(jsonString);
                if (!consolidatedJson.contains("legacy")) {
                    consolidatedJson["legacy"] = ofJson::object();
                }
                consolidatedJson["legacy"]["sequencerState"] = sequencerJson;
                migrated = true;
                ofLogNotice("SessionManager") << "Migrated sequencer_state.json";
            } catch (const std::exception& e) {
                ofLogError("SessionManager") << "Failed to parse sequencer_state.json: " << e.what();
            }
        }
    }
    
    // Migrate module_layouts.json
    std::string layoutsPath = ofToDataPath("module_layouts.json", true);
    if (ofFile::doesFileExist(layoutsPath)) {
        ofLogNotice("SessionManager") << "Found module_layouts.json, migrating...";
        ofFile file(layoutsPath, ofFile::ReadOnly);
        if (file.is_open()) {
            std::string jsonString = file.readToBuffer().getText();
            file.close();
            try {
                ofJson layoutsJson = ofJson::parse(jsonString);
                if (layoutsJson.contains("layouts") && layoutsJson["layouts"].is_object()) {
                    // Merge into GUI section
                    if (!consolidatedJson.contains("gui")) {
                        consolidatedJson["gui"] = ofJson::object();
                    }
                    consolidatedJson["gui"]["moduleLayouts"] = layoutsJson["layouts"];
                    migrated = true;
                    ofLogNotice("SessionManager") << "Migrated module_layouts.json";
                }
            } catch (const std::exception& e) {
                ofLogError("SessionManager") << "Failed to parse module_layouts.json: " << e.what();
            }
        }
    }
    
    // Migrate media_settings.json (store in settings section)
    std::string mediaSettingsPath = ofToDataPath("media_settings.json", true);
    if (ofFile::doesFileExist(mediaSettingsPath)) {
        ofLogNotice("SessionManager") << "Found media_settings.json, migrating...";
        ofFile file(mediaSettingsPath, ofFile::ReadOnly);
        if (file.is_open()) {
            std::string jsonString = file.readToBuffer().getText();
            file.close();
            try {
                ofJson mediaJson = ofJson::parse(jsonString);
                if (!consolidatedJson.contains("settings")) {
                    consolidatedJson["settings"] = ofJson::object();
                }
                if (mediaJson.contains("mediaDirectory")) {
                    consolidatedJson["settings"]["mediaDirectory"] = mediaJson["mediaDirectory"];
                }
                migrated = true;
                ofLogNotice("SessionManager") << "Migrated media_settings.json";
            } catch (const std::exception& e) {
                ofLogError("SessionManager") << "Failed to parse media_settings.json: " << e.what();
            }
        }
    }
    
    // Save consolidated session if we migrated anything
    if (migrated) {
        // Create backup of old session if it exists
        if (hasSessionFile) {
            std::string backupPath = sessionPath + ".backup";
            if (!ofFile::doesFileExist(backupPath)) {
                ofFile::copyFromTo(sessionPath, backupPath, false, true);
                ofLogNotice("SessionManager") << "Created backup: " << backupPath;
            }
        }
        
        // Save consolidated session
        ofFile file(sessionPath, ofFile::WriteOnly);
        if (file.is_open()) {
            file << consolidatedJson.dump(4);
            file.close();
            ofLogNotice("SessionManager") << "Saved consolidated session to " << sessionPath;
            
            // Optionally rename legacy files (add .migrated extension)
            // This preserves them for reference but marks them as migrated
            if (ofFile::doesFileExist(trackerStatePath)) {
                std::string migratedPath = trackerStatePath + ".migrated";
                ofFile::moveFromTo(trackerStatePath, migratedPath, false, true);
            }
            if (ofFile::doesFileExist(sequencerStatePath)) {
                std::string migratedPath = sequencerStatePath + ".migrated";
                ofFile::moveFromTo(sequencerStatePath, migratedPath, false, true);
            }
            if (ofFile::doesFileExist(layoutsPath)) {
                std::string migratedPath = layoutsPath + ".migrated";
                ofFile::moveFromTo(layoutsPath, migratedPath, false, true);
            }
            if (ofFile::doesFileExist(mediaSettingsPath)) {
                std::string migratedPath = mediaSettingsPath + ".migrated";
                ofFile::moveFromTo(mediaSettingsPath, migratedPath, false, true);
            }
            
            return true;
        } else {
            ofLogError("SessionManager") << "Failed to save consolidated session";
            return false;
        }
    }
    
    ofLogNotice("SessionManager") << "No legacy files found to migrate";
    return true;  // Success (nothing to migrate)
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
    // Only load if we have pending state and ImGui is initialized
    if (pendingImGuiState_.empty()) {
        // No pending state - layout was not loaded from session
        sessionLayoutWasLoaded_ = false;
        // No pending state, try to load from imgui.ini as fallback
        std::string iniPath = ofToDataPath("imgui.ini", true);
        if (ofFile::doesFileExist(iniPath)) {
            try {
                ImGui::LoadIniSettingsFromDisk(iniPath.c_str());
                ofLogNotice("SessionManager") << "Loaded window layout from imgui.ini (fallback)";
            } catch (const std::exception& e) {
                ofLogError("SessionManager") << "Failed to load imgui.ini: " << e.what();
            }
        }
        return;
    }
    
    // CRITICAL FIX: If pendingImGuiState_ is empty/meaningless but originalImGuiState_ is meaningful,
    // use originalImGuiState_ instead (this handles the case where updateImGuiStateInSession overwrote it)
    bool pendingIsMeaningful = pendingImGuiState_.size() > 50 || pendingImGuiState_.find("[Window") != std::string::npos;
    bool originalIsMeaningful = !originalImGuiState_.empty() && (originalImGuiState_.size() > 50 || originalImGuiState_.find("[Window") != std::string::npos);
    
    if (!pendingIsMeaningful && originalIsMeaningful) {
        // pendingImGuiState_ was overwritten with empty state, but we have original meaningful layout
        pendingImGuiState_ = originalImGuiState_;
        ofLogNotice("SessionManager") << "Restored pendingImGuiState_ from originalImGuiState_ (" << pendingImGuiState_.size() << " bytes)";
    }
    
    ofLogNotice("SessionManager") << "Loading ImGui state from session (" << pendingImGuiState_.size() << " bytes)";
    
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
        
        // CRITICAL: Don't sync here - windows should already exist before layout is loaded
        // ImGui needs windows to exist when layout is loaded for docking to work properly
        // syncWithRegistry() should be called BEFORE loadPendingImGuiState(), not after
        // This ensures windows exist when the layout is applied
        
        // Check if layout is meaningful (contains window configurations, not just empty docking data)
        // Empty layouts are typically < 50 bytes and only contain [Docking][Data] sections
        bool layoutIsMeaningful = pendingImGuiState_.size() > 50 || 
                                  pendingImGuiState_.find("[Window") != std::string::npos;
        
        if (layoutIsMeaningful) {
            sessionLayoutWasLoaded_ = true;  // Mark that layout was successfully loaded from session
            ofLogNotice("SessionManager") << "Layout is meaningful, marking as loaded";
        } else {
            sessionLayoutWasLoaded_ = false;  // Empty layout - treat as not loaded
            ofLogNotice("SessionManager") << "Layout is empty/meaningless (" << pendingImGuiState_.size() 
                                         << " bytes), treating as not loaded to prevent overwriting";
        }
        
        // Only clear pendingImGuiState_ if layout was successfully loaded
        // Keep it if layout wasn't loaded (e.g., Command Shell active, Editor windows not drawn)
        // This allows us to reload it when Editor Shell is activated
        if (sessionLayoutWasLoaded_) {
            pendingImGuiState_.clear();  // Clear after successful load
            originalImGuiState_.clear();  // Also clear original since it's been applied
        } else {
            // Keep pendingImGuiState_ for potential reload when Editor Shell activates
            // Restore from original if we cleared it
            if (pendingImGuiState_.empty() && !originalImGuiState_.empty()) {
                pendingImGuiState_ = originalImGuiState_;
                ofLogNotice("SessionManager") << "Restored pendingImGuiState_ from original for Editor Shell activation";
            }
        }
    } catch (const std::exception& e) {
        ofLogError("SessionManager") << "Failed to load ImGui state: " << e.what();
        sessionLayoutWasLoaded_ = false;  // Mark as not loaded on failure
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
        // Don't clear on failure - keep for potential reload when Editor Shell activates
        // pendingImGuiState_ will be preserved for retry
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
        // CRITICAL: Don't overwrite pendingImGuiState_ if layout was never loaded and current state is empty
        // This preserves the original layout for later use
        bool currentIsMeaningful = iniSize > 50 || std::string(iniData, std::min<size_t>(100, iniSize)).find("[Window") != std::string::npos;
        
        if (!sessionLayoutWasLoaded_ && !currentIsMeaningful) {
            // Layout was never loaded and current state is empty - preserve original layout
            ofLogNotice("SessionManager") << "Skipping ImGui state update: Layout was never loaded and current state is empty, preserving original layout";
            return;
        }
        
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
                // Note: ImGui layout will be loaded on first frame after dockspace is created
                // This is handled in ofApp::drawGUI() to ensure proper timing
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
                // Note: ImGui layout will be loaded on first frame after dockspace is created
                // This is handled in ofApp::drawGUI() to ensure proper timing
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
        
        // Initialize the module with PatternRuntime (if available)
        // This ensures modules are ready for use even if no session was loaded
        if (clock && registry && connectionManager_ && router) {
            module->initialize(clock, registry, connectionManager_, router, patternRuntime_, false);
            ofLogVerbose("SessionManager") << "Initialized default module: " << instanceName;
        } else {
            ofLogWarning("SessionManager") << "Cannot initialize default module " << instanceName 
                                            << ": missing dependencies (will be initialized later)";
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
    // CRITICAL: Pass PatternRuntime to all modules (especially TrackerSequencer)
    if (!patternRuntime_) {
        ofLogWarning("SessionManager") << "PatternRuntime is null during setupGUI - modules will not have pattern access";
    }
    registry->forEachModule([this](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
        if (module && connectionManager_) {
            module->initialize(clock, registry, connectionManager_, router, patternRuntime_, false);
        }
    });
    
    // Note: ImGui layout loading is deferred until first frame after dockspace is created
    // This is handled in ofApp::drawGUI() to ensure proper timing (dockspace must exist first)
    
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

