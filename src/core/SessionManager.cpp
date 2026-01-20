#include "SessionManager.h"
#include <fstream>
#include <chrono>
#include "utils/Clock.h"  // Needed for Clock* member access (must be before SessionManager.h if SessionManager.h uses Clock)
#include "ProjectManager.h"
#include "ModuleRegistry.h"
#include "ModuleFactory.h"
#include "ParameterRouter.h"
#include "ConnectionManager.h"
#include "modules/Module.h"
#include "modules/TrackerSequencer.h"
#include "core/PatternRuntime.h"
#include "core/Engine.h"  // For Engine::getStateSnapshot() and getStateVersion()
#include "ofMain.h"  // For ofGetElapsedTimef()
#include "ofLog.h"
#include "ofJson.h"
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
    ConnectionManager* connectionManager
) : projectManager_(projectManager), clock(clock), registry(registry), factory(factory), router(router), connectionManager_(connectionManager), patternRuntime_(nullptr), currentSessionName_(""), postLoadCallback_(nullptr), projectOpenedCallback_(nullptr), pendingImGuiState_(""), originalImGuiState_(""), pendingVisibilityState_(ofJson::object()), engine_(nullptr) {
    if (!clock || !registry || !factory || !router) {
        ofLogError("SessionManager") << "SessionManager initialized with null pointers";
    }
    
    // Start serialization thread
    serializationThread_ = std::thread(&SessionManager::serializationThreadFunction, this);
}

//--------------------------------------------------------------
SessionManager::~SessionManager() {
    // Signal serialization thread to stop
    shouldStopSerializationThread_.store(true);
    
    // CRITICAL: Notify queue to wake waiting thread (BlockingConcurrentQueue handles this internally)
    // The wait_dequeue_timed will wake up when shouldStopSerializationThread_ is checked
    
    // Wait for thread to finish (only if thread was started)
    if (serializationThread_.joinable()) {
        serializationQueue_.enqueue(SerializationRequest{});  // Wake up thread with empty request
        serializationThread_.join();
    }
}

//--------------------------------------------------------------
SessionManager& SessionManager::operator=(SessionManager&& other) noexcept {
    if (this != &other) {
        // Stop current thread if running (default-constructed SessionManager won't have a running thread)
        if (serializationThread_.joinable()) {
            shouldStopSerializationThread_.store(true);
            serializationQueue_.enqueue(SerializationRequest{});  // Wake up thread
            serializationThread_.join();
        }
        
        // Use default move assignment for all members (std::thread is moveable)
        // This will move the thread from other to this
        projectManager_ = other.projectManager_;
        clock = other.clock;
        registry = other.registry;
        factory = other.factory;
        router = other.router;
        connectionManager_ = other.connectionManager_;
        patternRuntime_ = other.patternRuntime_;
        currentSessionName_ = std::move(other.currentSessionName_);
        postLoadCallback_ = std::move(other.postLoadCallback_);
        projectOpenedCallback_ = std::move(other.projectOpenedCallback_);
        pendingImGuiState_ = std::move(other.pendingImGuiState_);
        originalImGuiState_ = std::move(other.originalImGuiState_);
        pendingVisibilityState_ = std::move(other.pendingVisibilityState_);
        engine_ = other.engine_;
        autoSaveEnabled_ = other.autoSaveEnabled_;
        autoSaveInterval_ = other.autoSaveInterval_;
        lastAutoSave_ = other.lastAutoSave_;
        saveInProgress_ = other.saveInProgress_;
        onUpdateWindowTitle_ = std::move(other.onUpdateWindowTitle_);
        shouldStopSerializationThread_ = other.shouldStopSerializationThread_.load();
        serializationQueue_ = std::move(other.serializationQueue_);
        serializationThread_ = std::move(other.serializationThread_);  // Move thread (or default-constructed if other was default-constructed)
    }
    return *this;
}

//--------------------------------------------------------------
ofJson SessionManager::serializeAll() const {
    // Get core state (no UI dependencies)
    ofJson json = serializeCore();
    
    // Add empty GUI state object (for backward compatibility)
    // Note: This method is kept for backward compatibility but will be deprecated
    // GUI state serialization is now handled by Shells (EditorShell)
    json["gui"] = ofJson::object();
    
    // Preserve existing GUI state from pendingImGuiState_ if available (for backward compatibility)
    // This allows old code that calls serializeAll() to still get some GUI state
    if (!pendingImGuiState_.empty()) {
        json["gui"]["imguiState"] = pendingImGuiState_;
    }
    
    // Note: All other GUI state (viewState, visibleInstances, moduleLayouts) is now handled by Shells
    // This method is deprecated - use serializeCore() for core state and Shells for UI state
    
    return json;
}

//--------------------------------------------------------------
ofJson SessionManager::serializeCore() const {
    ofJson json;
    
    // Version and metadata
    json["version"] = SESSION_VERSION;
    
    // Timestamp
    auto now = std::time(nullptr);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%SZ");
    json["metadata"] = ofJson::object();
    json["metadata"]["modified"] = ss.str();
    
    // Get engine snapshot (lock-free read - no registryMutex_ or moduleMutex_ needed)
    if (engine_) {
        auto engineSnapshot = engine_->getStateSnapshot();
        if (engineSnapshot) {
            // Extract core state from engine snapshot (already JSON)
            // Engine snapshot includes: transport, modules, connections, script
            if (engineSnapshot->contains("transport")) {
                json["clock"] = (*engineSnapshot)["transport"];  // Use transport as clock state
            }
            
            // Modules: Use registry->toJson() which is now lock-free (after Task 1)
            // Engine snapshot has modules as object, but session format needs array
            // registry->toJson() uses Module::getSnapshot() (lock-free) and returns correct format
            if (registry) {
                json["modules"] = ofJson::object();
                json["modules"]["instances"] = registry->toJson();  // Lock-free (uses snapshots)
            }
            
            // Connections: Use ConnectionManager::toJson() directly (not engine snapshot format)
            // CRITICAL FIX: Engine snapshot uses ConnectionInfo array format (for observation),
            // but ConnectionManager expects its native format (object with separate arrays per type).
            // Using ConnectionManager::toJson() ensures format matches what fromJson() expects.
            if (connectionManager_) {
                json["modules"]["connections"] = connectionManager_->toJson();
                // Log connection counts for verification
                auto connectionsJson = connectionManager_->toJson();
                int audioCount = 0, videoCount = 0, paramCount = 0, eventCount = 0;
                if (connectionsJson.contains("audioConnections") && connectionsJson["audioConnections"].is_array()) {
                    audioCount = connectionsJson["audioConnections"].size();
                }
                if (connectionsJson.contains("videoConnections") && connectionsJson["videoConnections"].is_array()) {
                    videoCount = connectionsJson["videoConnections"].size();
                }
                if (connectionsJson.contains("parameterConnections") && connectionsJson["parameterConnections"].is_array()) {
                    paramCount = connectionsJson["parameterConnections"].size();
                }
                if (connectionsJson.contains("eventSubscriptions") && connectionsJson["eventSubscriptions"].is_array()) {
                    eventCount = connectionsJson["eventSubscriptions"].size();
                }
                ofLogVerbose("SessionManager") << "Saving connections: " << audioCount << " audio, " 
                                                << videoCount << " video, " << paramCount << " parameter, " 
                                                << eventCount << " event";
            } else {
                ofLogWarning("SessionManager") << "ConnectionManager not available, connections not saved";
            }
            
            if (engineSnapshot->contains("script")) {
                // Script state is in engine snapshot, but we may not need it in session
                // (Script state is runtime, not session state)
            }
        } else {
            ofLogWarning("SessionManager") << "No engine snapshot available, falling back to legacy serialization";
            // Fallback to legacy serialization if snapshot not available
            if (clock) {
                json["clock"] = clock->toJson();
            }
            if (registry) {
                json["modules"] = ofJson::object();
                json["modules"]["instances"] = registry->toJson();  // Lock-free (uses snapshots)
            }
        }
    } else {
        // No engine reference - use legacy serialization
        if (clock) {
            json["clock"] = clock->toJson();
        }
        if (registry) {
            json["modules"] = ofJson::object();
            json["modules"]["instances"] = registry->toJson();  // Lock-free (uses snapshots)
        }
    }
    
    // Parameter routing (still needs router->toJson() - minimal lock time, separate system)
    if (router) {
        json["modules"]["routing"] = router->toJson();
    }
    
    // PatternRuntime state (still needs patternRuntime_->toJson() - minimal lock time, separate system)
    if (patternRuntime_) {
        json["patternRuntime"] = patternRuntime_->toJson();
        ofLogNotice("SessionManager") << "Saving PatternRuntime patterns";
    } else {
        ofLogWarning("SessionManager") << "PatternRuntime is null, cannot save patterns to session";
    }
    
    // Note: GUI state (viewState, visibleInstances, imguiState, moduleLayouts) is NOT included
    // UI state serialization is handled by Shells (EditorShell)
    
    return json;
}

//--------------------------------------------------------------
bool SessionManager::loadCore(const ofJson& json) {
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
            ofLogNotice("SessionManager") << "Starting connection restoration (Phase 1: Connection establishment)...";
            
            // Count expected connections from JSON for verification
            int expectedAudio = 0, expectedVideo = 0, expectedParam = 0, expectedEvent = 0;
            auto connectionsJson = modulesJson["connections"];
            if (connectionsJson.is_object()) {
                if (connectionsJson.contains("audioConnections") && connectionsJson["audioConnections"].is_array()) {
                    expectedAudio = connectionsJson["audioConnections"].size();
                }
                if (connectionsJson.contains("videoConnections") && connectionsJson["videoConnections"].is_array()) {
                    expectedVideo = connectionsJson["videoConnections"].size();
                }
                if (connectionsJson.contains("parameterConnections") && connectionsJson["parameterConnections"].is_array()) {
                    expectedParam = connectionsJson["parameterConnections"].size();
                }
                if (connectionsJson.contains("eventSubscriptions") && connectionsJson["eventSubscriptions"].is_array()) {
                    expectedEvent = connectionsJson["eventSubscriptions"].size();
                }
                ofLogNotice("SessionManager") << "Expected connections: " << expectedAudio << " audio, " 
                                              << expectedVideo << " video, " << expectedParam << " parameter, " 
                                              << expectedEvent << " event";
            } else {
                ofLogWarning("SessionManager") << "Connections JSON is not an object (expected ConnectionManager format)";
            }
            
            try {
                // ConnectionManager::fromJson() will restore connections immediately
                // This establishes the physical connections but sets default volumes/opacities
                if (!connectionManager_->fromJson(modulesJson["connections"])) {
                    ofLogError("SessionManager") << "Failed to load module connections";
                    return false;
                }
                
                // Verify restoration success by counting actual connections
                int actualAudio = connectionManager_->getConnectionsByType(ConnectionManager::ConnectionType::AUDIO).size();
                int actualVideo = connectionManager_->getConnectionsByType(ConnectionManager::ConnectionType::VIDEO).size();
                int actualParam = connectionManager_->getConnectionsByType(ConnectionManager::ConnectionType::PARAMETER).size();
                int actualEvent = connectionManager_->getConnectionsByType(ConnectionManager::ConnectionType::EVENT).size();
                
                // Log restoration results
                ofLogNotice("SessionManager") << "Connection restoration complete (Phase 1): " 
                                              << actualAudio << "/" << expectedAudio << " audio, "
                                              << actualVideo << "/" << expectedVideo << " video, "
                                              << actualParam << "/" << expectedParam << " parameter, "
                                              << actualEvent << "/" << expectedEvent << " event";
                
                // Warn if restoration incomplete
                if (actualAudio != expectedAudio) {
                    ofLogWarning("SessionManager") << "Audio connection restoration incomplete: " 
                                                    << actualAudio << "/" << expectedAudio << " restored";
                }
                if (actualVideo != expectedVideo) {
                    ofLogWarning("SessionManager") << "Video connection restoration incomplete: " 
                                                    << actualVideo << "/" << expectedVideo << " restored";
                }
                if (actualParam != expectedParam) {
                    ofLogWarning("SessionManager") << "Parameter connection restoration incomplete: " 
                                                    << actualParam << "/" << expectedParam << " restored";
                }
                if (actualEvent != expectedEvent) {
                    ofLogWarning("SessionManager") << "Event subscription restoration incomplete: " 
                                                    << actualEvent << "/" << expectedEvent << " restored";
                }
            } catch (const std::exception& e) {
                ofLogError("SessionManager") << "Failed to load module connections: " << e.what();
                return false;
            }
            
            // CRITICAL: After ConnectionManager restores connections, restore connection-specific
            // parameters (volumes, opacities, blend modes) from module JSON data
            // ConnectionManager only establishes the connections, but doesn't restore the parameters
            if (modulesJson.contains("instances")) {
                ofLogNotice("SessionManager") << "Starting connection parameter restoration (Phase 2: Volumes, opacities, blend modes)...";
                restoreMixerConnections(modulesJson["instances"]);
                ofLogNotice("SessionManager") << "Connection parameter restoration complete (Phase 2)";
            }
        } else {
            // Fallback: Restore mixer connections from module data (backward compatibility)
            ofLogNotice("SessionManager") << "Using fallback connection restoration (backward compatibility mode)...";
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
            
            // CRITICAL: Migrate old "TrackerSequencer_chain" to instance-specific chains BEFORE restoring bindings
            // This ensures bindings are restored with the correct instance-specific chain names
            if (patternRuntime_ && registry) {
                bool hasOldChain = patternRuntime_->chainExists("TrackerSequencer_chain");
                PatternChain* oldChain = hasOldChain ? patternRuntime_->getChain("TrackerSequencer_chain") : nullptr;
                
                // Check if any sequencer has boundChainName_ set to old chain (from fromJson())
                auto sequencerModules = registry->getModulesByType(ModuleType::SEQUENCER);
                bool needsMigration = false;
                for (const auto& module : sequencerModules) {
                    if (!module) continue;
                    auto tracker = std::dynamic_pointer_cast<class TrackerSequencer>(module);
                    if (!tracker) continue;
                    if (tracker->getBoundChainName() == "TrackerSequencer_chain") {
                        needsMigration = true;
                        break;
                    }
                }
                
                // Also check PatternRuntime bindings for old chain
                // Note: getSequencerNames() only returns sequencers with existing bindings,
                // so we also need to check all sequencers from ModuleRegistry
                auto sequencerNames = patternRuntime_->getSequencerNames();
                for (const auto& seqName : sequencerNames) {
                    auto binding = patternRuntime_->getSequencerBinding(seqName);
                    if (binding.chainName == "TrackerSequencer_chain") {
                        needsMigration = true;
                        break;
                    }
                }
                
                // CRITICAL: Also check ALL sequencers from ModuleRegistry (not just those with bindings)
                // This catches sequencers that have boundChainName_ set but no PatternRuntime binding yet
                // OR sequencers that are using the old chain via getCurrentChain() even if boundChainName_ is empty
                for (const auto& module : sequencerModules) {
                    if (!module) continue;
                    auto tracker = std::dynamic_pointer_cast<class TrackerSequencer>(module);
                    if (!tracker) continue;
                    std::string boundChain = tracker->getBoundChainName();
                    if (boundChain == "TrackerSequencer_chain") {
                        needsMigration = true;
                        break;
                    }
                    // Also check if getCurrentChain() returns the old chain
                    PatternChain* currentChain = tracker->getCurrentChain();
                    if (currentChain && oldChain && currentChain == oldChain) {
                        needsMigration = true;
                        break;
                    }
                }
                
                if (hasOldChain || needsMigration) {
                    ofLogNotice("SessionManager") << "Found old 'TrackerSequencer_chain' references, migrating to instance-specific chains...";
                    int migratedCount = 0;
                    
                    for (const auto& module : sequencerModules) {
                        if (!module) continue;
                        
                        std::string instanceName = registry->getName(module);
                        if (instanceName.empty()) continue;
                        
                        auto tracker = std::dynamic_pointer_cast<class TrackerSequencer>(module);
                        if (!tracker) continue;
                        
                        // Check if this sequencer is bound to the old chain
                        // Check both PatternRuntime binding AND tracker's boundChainName_
                        auto binding = patternRuntime_->getSequencerBinding(instanceName);
                        std::string trackerBoundChain = tracker->getBoundChainName();
                        
                        // Also check if getCurrentChain() returns the old chain (even if boundChainName_ isn't set)
                        // This handles cases where the sequencer is using the old chain but boundChainName_ is empty
                        PatternChain* currentChain = tracker->getCurrentChain();
                        bool usingOldChain = false;
                        if (currentChain && oldChain && currentChain == oldChain) {
                            usingOldChain = true;
                        }
                        
                        // Migrate if:
                        // 1. PatternRuntime binding points to old chain, OR
                        // 2. tracker's boundChainName_ points to old chain, OR
                        // 3. sequencer is currently using the old chain (via getCurrentChain())
                        if (binding.chainName == "TrackerSequencer_chain" || 
                            trackerBoundChain == "TrackerSequencer_chain" || 
                            usingOldChain) {
                            // Create instance-specific chain
                            std::string newChainName = instanceName + "_chain";
                            
                            if (!patternRuntime_->chainExists(newChainName)) {
                                patternRuntime_->addChain(newChainName);
                                
                                // Copy chain entries from old chain if it exists
                                if (oldChain) {
                                    const auto& chainPatterns = oldChain->getChain();
                                    for (size_t i = 0; i < chainPatterns.size(); i++) {
                                        patternRuntime_->chainAddPattern(newChainName, chainPatterns[i]);
                                        int repeatCount = oldChain->getRepeatCount((int)i);
                                        if (repeatCount > 1) {
                                            patternRuntime_->chainSetRepeat(newChainName, (int)i, repeatCount);
                                        }
                                        if (oldChain->isEntryDisabled((int)i)) {
                                            patternRuntime_->chainSetEntryDisabled(newChainName, (int)i, true);
                                        }
                                    }
                                    
                                    // Copy chain state
                                    patternRuntime_->chainSetEnabled(newChainName, oldChain->isEnabled());
                                    PatternChain* newChain = patternRuntime_->getChain(newChainName);
                                    if (newChain) {
                                        newChain->setCurrentIndex(oldChain->getCurrentIndex());
                                    }
                                } else {
                                    // Old chain doesn't exist - create empty chain with default state
                                    patternRuntime_->chainSetEnabled(newChainName, binding.chainEnabled);
                                    ofLogNotice("SessionManager") << "Created new empty chain '" << newChainName 
                                                                  << "' for sequencer '" << instanceName 
                                                                  << "' (old chain 'TrackerSequencer_chain' not found)";
                                }
                                
                                // Update binding to new chain in PatternRuntime
                                patternRuntime_->bindSequencerChain(instanceName, newChainName);
                                if (binding.chainEnabled) {
                                    patternRuntime_->setSequencerChainEnabled(instanceName, true);
                                }
                                
                                // Update tracker's boundChainName_
                                tracker->bindToChain(newChainName);
                                
                                migratedCount++;
                                ofLogNotice("SessionManager") << "Migrated chain for sequencer '" << instanceName 
                                                              << "' from 'TrackerSequencer_chain' to '" << newChainName << "'";
                            }
                        }
                    }
                    
                    if (migratedCount > 0) {
                        ofLogNotice("SessionManager") << "Migrated " << migratedCount << " sequencer(s) from old chain name";
                    }
                }
            }
            
            // Phase 2: Restore sequencer bindings from PatternRuntime AFTER modules are initialized and migration is complete
            if (patternRuntime_) {
                auto sequencerNames = patternRuntime_->getSequencerNames();
                
                for (const auto& seqName : sequencerNames) {
                    auto binding = patternRuntime_->getSequencerBinding(seqName);
                    
                    
                    // CRITICAL: Only restore bindings for sequencers that match the sequencer name
                    // This prevents applying bindings from one sequencer to another
                    auto module = registry->getModule(seqName);
                    auto tracker = std::dynamic_pointer_cast<class TrackerSequencer>(module);
                    if (tracker) {
                        // CRITICAL: Verify sequencer instance name matches (prevent cross-binding)
                        // Use getInstanceName() not getName() - getName() returns type name "TrackerSequencer"
                        if (tracker->getInstanceName() != seqName) {
                            ofLogWarning("SessionManager") << "Sequencer name mismatch: binding for '" << seqName 
                                                           << "' but tracker instance name is '" << tracker->getInstanceName() << "', skipping";
                            continue;
                        }
                        
                        
                        // Restore pattern binding
                        if (!binding.patternName.empty() && patternRuntime_->patternExists(binding.patternName)) {
                            tracker->bindToPattern(binding.patternName);
                        }
                        
                        // CRITICAL: Only restore chain binding if this sequencer actually has one
                        // This prevents applying a chain from one sequencer to all sequencers
                        if (!binding.chainName.empty() && patternRuntime_->chainExists(binding.chainName)) {
                            
                            
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
                            
                            
                        }
                        
                        ofLogVerbose("SessionManager") << "Restored bindings for sequencer '" << seqName 
                                                       << "': pattern='" << binding.patternName 
                                                       << "', chain='" << binding.chainName << "'";
                    } else {
                        
                        ofLogWarning("SessionManager") << "Sequencer binding found for '" << seqName 
                                                       << "' but module not found in registry";
                    }
                }
                
                // CRITICAL: Ensure ALL sequencer modules have bindings, even if they weren't in saved bindings
                // This fixes the issue where only some sequencers appear in the list
                if (registry && patternRuntime_) {
                    // Get old chain reference for late migration (if it exists)
                    bool hasOldChainForLateMigration = patternRuntime_->chainExists("TrackerSequencer_chain");
                    PatternChain* oldChainForLateMigration = hasOldChainForLateMigration ? patternRuntime_->getChain("TrackerSequencer_chain") : nullptr;
                    
                    auto sequencerModules = registry->getModulesByType(ModuleType::SEQUENCER);
                    for (const auto& module : sequencerModules) {
                        if (!module) continue;
                        
                        std::string name = registry->getName(module);
                        if (name.empty()) continue;
                        
                        auto tracker = std::dynamic_pointer_cast<class TrackerSequencer>(module);
                        if (!tracker) continue;
                        
                        // Check if this sequencer already has a binding
                        auto binding = patternRuntime_->getSequencerBinding(name);
                        
                        // CRITICAL: If sequencer has no chain binding but is using "TrackerSequencer_chain",
                        // migrate it now (this handles sequencers that were missed in the earlier migration)
                        std::string trackerBoundChain = tracker->getBoundChainName();
                        if (binding.chainName.empty() && trackerBoundChain == "TrackerSequencer_chain" && hasOldChainForLateMigration) {
                            std::string newChainName = name + "_chain";
                            if (!patternRuntime_->chainExists(newChainName)) {
                                patternRuntime_->addChain(newChainName);
                                
                                // Copy chain entries from old chain
                                if (oldChainForLateMigration) {
                                    const auto& chainPatterns = oldChainForLateMigration->getChain();
                                    for (size_t i = 0; i < chainPatterns.size(); i++) {
                                        patternRuntime_->chainAddPattern(newChainName, chainPatterns[i]);
                                        int repeatCount = oldChainForLateMigration->getRepeatCount((int)i);
                                        if (repeatCount > 1) {
                                            patternRuntime_->chainSetRepeat(newChainName, (int)i, repeatCount);
                                        }
                                        if (oldChainForLateMigration->isEntryDisabled((int)i)) {
                                            patternRuntime_->chainSetEntryDisabled(newChainName, (int)i, true);
                                        }
                                    }
                                    
                                    // Copy chain state
                                    patternRuntime_->chainSetEnabled(newChainName, oldChainForLateMigration->isEnabled());
                                    PatternChain* newChain = patternRuntime_->getChain(newChainName);
                                    if (newChain) {
                                        newChain->setCurrentIndex(oldChainForLateMigration->getCurrentIndex());
                                    }
                                }
                                
                                // Update binding to new chain in PatternRuntime
                                patternRuntime_->bindSequencerChain(name, newChainName);
                                patternRuntime_->setSequencerChainEnabled(name, oldChainForLateMigration ? oldChainForLateMigration->isEnabled() : true);
                                
                                // Update tracker's boundChainName_
                                tracker->bindToChain(newChainName);
                                
                                ofLogNotice("SessionManager") << "Late migration: Migrated chain for sequencer '" << name 
                                                              << "' from 'TrackerSequencer_chain' to '" << newChainName << "'";
                            }
                        }
                        
                        // CRITICAL: If sequencer has no chain binding, create instance-specific chain
                        // This ensures all sequencers have their own chains, even if they weren't in saved bindings
                        if (binding.chainName.empty()) {
                            std::string newChainName = name + "_chain";
                            if (!patternRuntime_->chainExists(newChainName)) {
                                patternRuntime_->addChain(newChainName);
                                ofLogNotice("SessionManager") << "Created instance-specific chain '" << newChainName 
                                                              << "' for sequencer '" << name << "' (no binding found)";
                            }
                            // Bind sequencer to the new chain
                            patternRuntime_->bindSequencerChain(name, newChainName);
                            patternRuntime_->setSequencerChainEnabled(name, true);
                            tracker->bindToChain(newChainName);
                            ofLogNotice("SessionManager") << "Bound sequencer '" << name << "' to chain '" << newChainName << "'";
                        }
                        
                        // If no pattern binding, ensure sequencer gets one
                        if (binding.patternName.empty()) {
                            // Get available patterns
                            auto patternNames = patternRuntime_->getPatternNames();
                            
                            if (!patternNames.empty()) {
                                // Bind to first available pattern
                                ofLogNotice("SessionManager") << "Auto-binding sequencer '" << name 
                                                              << "' to pattern '" << patternNames[0] << "'";
                                tracker->bindToPattern(patternNames[0]);
                            } else {
                                // No patterns exist - create default pattern
                                Pattern defaultPattern(16);
                                std::string patternName = patternRuntime_->addPattern(defaultPattern, "");
                                if (!patternName.empty()) {
                                    ofLogNotice("SessionManager") << "Created default pattern '" << patternName 
                                                                  << "' for sequencer '" << name << "'";
                                    tracker->bindToPattern(patternName);
                                }
                            }
                        } else {
                            // Pattern binding exists - verify it's still valid
                            if (!patternRuntime_->patternExists(binding.patternName)) {
                                // Pattern doesn't exist - bind to first available or create default
                                auto patternNames = patternRuntime_->getPatternNames();
                                if (!patternNames.empty()) {
                                    ofLogWarning("SessionManager") << "Pattern '" << binding.patternName 
                                                                   << "' not found for sequencer '" << name 
                                                                   << "', binding to '" << patternNames[0] << "'";
                                    tracker->bindToPattern(patternNames[0]);
                                } else {
                                    Pattern defaultPattern(16);
                                    std::string patternName = patternRuntime_->addPattern(defaultPattern, "");
                                    if (!patternName.empty()) {
                                        tracker->bindToPattern(patternName);
                                    }
                                }
                            }
                        }
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
                                                                                       << " for TrackerSequencer '" << tracker->getInstanceName() << "' after chain reload";
                                                        break;
                                                    }
                                                }
                                            }
                                            
                                            reloadedCount++;
                                            ofLogVerbose("SessionManager") << "Reloaded pattern chain for TrackerSequencer '" 
                                                                           << tracker->getInstanceName() << "'";
                                        } catch (const std::exception& e) {
                                            ofLogWarning("SessionManager") << "Failed to reload pattern chain for TrackerSequencer '" 
                                                                           << tracker->getInstanceName() << "': " << e.what();
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
                                    ofLogNotice("SessionManager") << "TrackerSequencer '" << tracker->getInstanceName() 
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
                                                                      << "' for TrackerSequencer '" << tracker->getInstanceName() << "'";
                                    }
                                }
                            } else if (!patternRuntime_->patternExists(boundName)) {
                                // Bound pattern doesn't exist - bind to first available pattern or create default
                                if (!patternNames.empty()) {
                                    ofLogWarning("SessionManager") << "TrackerSequencer '" << tracker->getInstanceName() 
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
                                                                      << "' for TrackerSequencer '" << tracker->getInstanceName() << "'";
                                    }
                                }
                            } else {
                                // Pattern exists and is valid - verify binding worked
                                ofLogVerbose("SessionManager") << "TrackerSequencer '" << tracker->getInstanceName() 
                                                                << "' correctly bound to pattern '" << boundName << "'";
                                
                                // CRITICAL: Sync pattern chain current index with bound pattern after restoration
                                // This ensures the GUI displays the correct active pattern in the chain
                                const auto& chain = tracker->getPatternChain();
                                if (tracker->getUsePatternChain() && !chain.empty()) {
                                    for (size_t i = 0; i < chain.size(); ++i) {
                                        if (chain[i] == boundName) {
                                            tracker->setCurrentChainIndex((int)i);
                                            ofLogVerbose("SessionManager") << "Synced chain index to " << i 
                                                                           << " for TrackerSequencer '" << tracker->getInstanceName() << "'";
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
    
    // Note: GUI state loading (viewState, visibleInstances, imguiState, moduleLayouts) is NOT included
    // UI state deserialization is handled by Shells (EditorShell)
    
    // Call post-load callback if set (for re-initializing audio streams, etc.)
    if (postLoadCallback_) {
        postLoadCallback_();
    }
    
    return true;
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
            ofLogNotice("SessionManager") << "Starting connection restoration (Phase 1: Connection establishment)...";
            
            // Count expected connections from JSON for verification
            int expectedAudio = 0, expectedVideo = 0, expectedParam = 0, expectedEvent = 0;
            auto connectionsJson = modulesJson["connections"];
            if (connectionsJson.is_object()) {
                if (connectionsJson.contains("audioConnections") && connectionsJson["audioConnections"].is_array()) {
                    expectedAudio = connectionsJson["audioConnections"].size();
                }
                if (connectionsJson.contains("videoConnections") && connectionsJson["videoConnections"].is_array()) {
                    expectedVideo = connectionsJson["videoConnections"].size();
                }
                if (connectionsJson.contains("parameterConnections") && connectionsJson["parameterConnections"].is_array()) {
                    expectedParam = connectionsJson["parameterConnections"].size();
                }
                if (connectionsJson.contains("eventSubscriptions") && connectionsJson["eventSubscriptions"].is_array()) {
                    expectedEvent = connectionsJson["eventSubscriptions"].size();
                }
                ofLogNotice("SessionManager") << "Expected connections: " << expectedAudio << " audio, " 
                                              << expectedVideo << " video, " << expectedParam << " parameter, " 
                                              << expectedEvent << " event";
            } else {
                ofLogWarning("SessionManager") << "Connections JSON is not an object (expected ConnectionManager format)";
            }
            
            try {
                // ConnectionManager::fromJson() will restore connections immediately
                // This establishes the physical connections but sets default volumes/opacities
                if (!connectionManager_->fromJson(modulesJson["connections"])) {
                    ofLogError("SessionManager") << "Failed to load module connections";
                    return false;
                }
                
                // Verify restoration success by counting actual connections
                int actualAudio = connectionManager_->getConnectionsByType(ConnectionManager::ConnectionType::AUDIO).size();
                int actualVideo = connectionManager_->getConnectionsByType(ConnectionManager::ConnectionType::VIDEO).size();
                int actualParam = connectionManager_->getConnectionsByType(ConnectionManager::ConnectionType::PARAMETER).size();
                int actualEvent = connectionManager_->getConnectionsByType(ConnectionManager::ConnectionType::EVENT).size();
                
                // Log restoration results
                ofLogNotice("SessionManager") << "Connection restoration complete (Phase 1): " 
                                              << actualAudio << "/" << expectedAudio << " audio, "
                                              << actualVideo << "/" << expectedVideo << " video, "
                                              << actualParam << "/" << expectedParam << " parameter, "
                                              << actualEvent << "/" << expectedEvent << " event";
                
                // Warn if restoration incomplete
                if (actualAudio != expectedAudio) {
                    ofLogWarning("SessionManager") << "Audio connection restoration incomplete: " 
                                                    << actualAudio << "/" << expectedAudio << " restored";
                }
                if (actualVideo != expectedVideo) {
                    ofLogWarning("SessionManager") << "Video connection restoration incomplete: " 
                                                    << actualVideo << "/" << expectedVideo << " restored";
                }
                if (actualParam != expectedParam) {
                    ofLogWarning("SessionManager") << "Parameter connection restoration incomplete: " 
                                                    << actualParam << "/" << expectedParam << " restored";
                }
                if (actualEvent != expectedEvent) {
                    ofLogWarning("SessionManager") << "Event subscription restoration incomplete: " 
                                                    << actualEvent << "/" << expectedEvent << " restored";
                }
            } catch (const std::exception& e) {
                ofLogError("SessionManager") << "Failed to load module connections: " << e.what();
                return false;
            }
            
            // CRITICAL: After ConnectionManager restores connections, restore connection-specific
            // parameters (volumes, opacities, blend modes) from module JSON data
            // ConnectionManager only establishes the connections, but doesn't restore the parameters
            if (modulesJson.contains("instances")) {
                ofLogNotice("SessionManager") << "Starting connection parameter restoration (Phase 2: Volumes, opacities, blend modes)...";
                restoreMixerConnections(modulesJson["instances"]);
                ofLogNotice("SessionManager") << "Connection parameter restoration complete (Phase 2)";
            }
        } else {
            // Fallback: Restore mixer connections from module data (backward compatibility)
            ofLogNotice("SessionManager") << "Using fallback connection restoration (backward compatibility mode)...";
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
            
            // CRITICAL: Migrate old "TrackerSequencer_chain" to instance-specific chains BEFORE restoring bindings
            // This ensures bindings are restored with the correct instance-specific chain names
            if (patternRuntime_ && registry) {
                bool hasOldChain = patternRuntime_->chainExists("TrackerSequencer_chain");
                PatternChain* oldChain = hasOldChain ? patternRuntime_->getChain("TrackerSequencer_chain") : nullptr;
                
                // Check if any sequencer has boundChainName_ set to old chain (from fromJson())
                auto sequencerModules = registry->getModulesByType(ModuleType::SEQUENCER);
                bool needsMigration = false;
                for (const auto& module : sequencerModules) {
                    if (!module) continue;
                    auto tracker = std::dynamic_pointer_cast<class TrackerSequencer>(module);
                    if (!tracker) continue;
                    if (tracker->getBoundChainName() == "TrackerSequencer_chain") {
                        needsMigration = true;
                        break;
                    }
                }
                
                // Also check PatternRuntime bindings for old chain
                // Note: getSequencerNames() only returns sequencers with existing bindings,
                // so we also need to check all sequencers from ModuleRegistry
                auto sequencerNames = patternRuntime_->getSequencerNames();
                for (const auto& seqName : sequencerNames) {
                    auto binding = patternRuntime_->getSequencerBinding(seqName);
                    if (binding.chainName == "TrackerSequencer_chain") {
                        needsMigration = true;
                        break;
                    }
                }
                
                // CRITICAL: Also check ALL sequencers from ModuleRegistry (not just those with bindings)
                // This catches sequencers that have boundChainName_ set but no PatternRuntime binding yet
                // OR sequencers that are using the old chain via getCurrentChain() even if boundChainName_ is empty
                for (const auto& module : sequencerModules) {
                    if (!module) continue;
                    auto tracker = std::dynamic_pointer_cast<class TrackerSequencer>(module);
                    if (!tracker) continue;
                    std::string boundChain = tracker->getBoundChainName();
                    if (boundChain == "TrackerSequencer_chain") {
                        needsMigration = true;
                        break;
                    }
                    // Also check if getCurrentChain() returns the old chain
                    PatternChain* currentChain = tracker->getCurrentChain();
                    if (currentChain && oldChain && currentChain == oldChain) {
                        needsMigration = true;
                        break;
                    }
                }
                
                if (hasOldChain || needsMigration) {
                    ofLogNotice("SessionManager") << "Found old 'TrackerSequencer_chain' references, migrating to instance-specific chains...";
                    int migratedCount = 0;
                    
                    for (const auto& module : sequencerModules) {
                        if (!module) continue;
                        
                        std::string instanceName = registry->getName(module);
                        if (instanceName.empty()) continue;
                        
                        auto tracker = std::dynamic_pointer_cast<class TrackerSequencer>(module);
                        if (!tracker) continue;
                        
                        // Check if this sequencer is bound to the old chain
                        // Check both PatternRuntime binding AND tracker's boundChainName_
                        auto binding = patternRuntime_->getSequencerBinding(instanceName);
                        std::string trackerBoundChain = tracker->getBoundChainName();
                        
                        // Also check if getCurrentChain() returns the old chain (even if boundChainName_ isn't set)
                        // This handles cases where the sequencer is using the old chain but boundChainName_ is empty
                        PatternChain* currentChain = tracker->getCurrentChain();
                        bool usingOldChain = false;
                        if (currentChain && oldChain && currentChain == oldChain) {
                            usingOldChain = true;
                        }
                        
                        // Migrate if:
                        // 1. PatternRuntime binding points to old chain, OR
                        // 2. tracker's boundChainName_ points to old chain, OR
                        // 3. sequencer is currently using the old chain (via getCurrentChain())
                        if (binding.chainName == "TrackerSequencer_chain" || 
                            trackerBoundChain == "TrackerSequencer_chain" || 
                            usingOldChain) {
                            // Create instance-specific chain
                            std::string newChainName = instanceName + "_chain";
                            
                            if (!patternRuntime_->chainExists(newChainName)) {
                                patternRuntime_->addChain(newChainName);
                                
                                // Copy chain entries from old chain if it exists
                                if (oldChain) {
                                    const auto& chainPatterns = oldChain->getChain();
                                    for (size_t i = 0; i < chainPatterns.size(); i++) {
                                        patternRuntime_->chainAddPattern(newChainName, chainPatterns[i]);
                                        int repeatCount = oldChain->getRepeatCount((int)i);
                                        if (repeatCount > 1) {
                                            patternRuntime_->chainSetRepeat(newChainName, (int)i, repeatCount);
                                        }
                                        if (oldChain->isEntryDisabled((int)i)) {
                                            patternRuntime_->chainSetEntryDisabled(newChainName, (int)i, true);
                                        }
                                    }
                                    
                                    // Copy chain state
                                    patternRuntime_->chainSetEnabled(newChainName, oldChain->isEnabled());
                                    PatternChain* newChain = patternRuntime_->getChain(newChainName);
                                    if (newChain) {
                                        newChain->setCurrentIndex(oldChain->getCurrentIndex());
                                    }
                                } else {
                                    // Old chain doesn't exist - create empty chain with default state
                                    patternRuntime_->chainSetEnabled(newChainName, binding.chainEnabled);
                                    ofLogNotice("SessionManager") << "Created new empty chain '" << newChainName 
                                                                  << "' for sequencer '" << instanceName 
                                                                  << "' (old chain 'TrackerSequencer_chain' not found)";
                                }
                                
                                // Update binding to new chain in PatternRuntime
                                patternRuntime_->bindSequencerChain(instanceName, newChainName);
                                if (binding.chainEnabled) {
                                    patternRuntime_->setSequencerChainEnabled(instanceName, true);
                                }
                                
                                // Update tracker's boundChainName_
                                tracker->bindToChain(newChainName);
                                
                                migratedCount++;
                                ofLogNotice("SessionManager") << "Migrated chain for sequencer '" << instanceName 
                                                              << "' from 'TrackerSequencer_chain' to '" << newChainName << "'";
                            }
                        }
                    }
                    
                    if (migratedCount > 0) {
                        ofLogNotice("SessionManager") << "Migrated " << migratedCount << " sequencer(s) from old chain name";
                    }
                }
            }
            
            // Phase 2: Restore sequencer bindings from PatternRuntime AFTER modules are initialized and migration is complete
            if (patternRuntime_) {
                auto sequencerNames = patternRuntime_->getSequencerNames();
                
                for (const auto& seqName : sequencerNames) {
                    auto binding = patternRuntime_->getSequencerBinding(seqName);
                    
                    
                    // CRITICAL: Only restore bindings for sequencers that match the sequencer name
                    // This prevents applying bindings from one sequencer to another
                    auto module = registry->getModule(seqName);
                    auto tracker = std::dynamic_pointer_cast<class TrackerSequencer>(module);
                    if (tracker) {
                        // CRITICAL: Verify sequencer instance name matches (prevent cross-binding)
                        // Use getInstanceName() not getName() - getName() returns type name "TrackerSequencer"
                        if (tracker->getInstanceName() != seqName) {
                            ofLogWarning("SessionManager") << "Sequencer name mismatch: binding for '" << seqName 
                                                           << "' but tracker instance name is '" << tracker->getInstanceName() << "', skipping";
                            continue;
                        }
                        
                        
                        // Restore pattern binding
                        if (!binding.patternName.empty() && patternRuntime_->patternExists(binding.patternName)) {
                            tracker->bindToPattern(binding.patternName);
                        }
                        
                        // CRITICAL: Only restore chain binding if this sequencer actually has one
                        // This prevents applying a chain from one sequencer to all sequencers
                        if (!binding.chainName.empty() && patternRuntime_->chainExists(binding.chainName)) {
                            
                            
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
                            
                            
                        }
                        
                        ofLogVerbose("SessionManager") << "Restored bindings for sequencer '" << seqName 
                                                       << "': pattern='" << binding.patternName 
                                                       << "', chain='" << binding.chainName << "'";
                    } else {
                        
                        ofLogWarning("SessionManager") << "Sequencer binding found for '" << seqName 
                                                       << "' but module not found in registry";
                    }
                }
                
                // CRITICAL: Ensure ALL sequencer modules have bindings, even if they weren't in saved bindings
                // This fixes the issue where only some sequencers appear in the list
                if (registry && patternRuntime_) {
                    // Get old chain reference for late migration (if it exists)
                    bool hasOldChainForLateMigration = patternRuntime_->chainExists("TrackerSequencer_chain");
                    PatternChain* oldChainForLateMigration = hasOldChainForLateMigration ? patternRuntime_->getChain("TrackerSequencer_chain") : nullptr;
                    
                    auto sequencerModules = registry->getModulesByType(ModuleType::SEQUENCER);
                    for (const auto& module : sequencerModules) {
                        if (!module) continue;
                        
                        std::string name = registry->getName(module);
                        if (name.empty()) continue;
                        
                        auto tracker = std::dynamic_pointer_cast<class TrackerSequencer>(module);
                        if (!tracker) continue;
                        
                        // Check if this sequencer already has a binding
                        auto binding = patternRuntime_->getSequencerBinding(name);
                        
                        // CRITICAL: If sequencer has no chain binding but is using "TrackerSequencer_chain",
                        // migrate it now (this handles sequencers that were missed in the earlier migration)
                        std::string trackerBoundChain = tracker->getBoundChainName();
                        if (binding.chainName.empty() && trackerBoundChain == "TrackerSequencer_chain" && hasOldChainForLateMigration) {
                            std::string newChainName = name + "_chain";
                            if (!patternRuntime_->chainExists(newChainName)) {
                                patternRuntime_->addChain(newChainName);
                                
                                // Copy chain entries from old chain
                                if (oldChainForLateMigration) {
                                    const auto& chainPatterns = oldChainForLateMigration->getChain();
                                    for (size_t i = 0; i < chainPatterns.size(); i++) {
                                        patternRuntime_->chainAddPattern(newChainName, chainPatterns[i]);
                                        int repeatCount = oldChainForLateMigration->getRepeatCount((int)i);
                                        if (repeatCount > 1) {
                                            patternRuntime_->chainSetRepeat(newChainName, (int)i, repeatCount);
                                        }
                                        if (oldChainForLateMigration->isEntryDisabled((int)i)) {
                                            patternRuntime_->chainSetEntryDisabled(newChainName, (int)i, true);
                                        }
                                    }
                                    
                                    // Copy chain state
                                    patternRuntime_->chainSetEnabled(newChainName, oldChainForLateMigration->isEnabled());
                                    PatternChain* newChain = patternRuntime_->getChain(newChainName);
                                    if (newChain) {
                                        newChain->setCurrentIndex(oldChainForLateMigration->getCurrentIndex());
                                    }
                                }
                                
                                // Update binding to new chain in PatternRuntime
                                patternRuntime_->bindSequencerChain(name, newChainName);
                                patternRuntime_->setSequencerChainEnabled(name, oldChainForLateMigration ? oldChainForLateMigration->isEnabled() : true);
                                
                                // Update tracker's boundChainName_
                                tracker->bindToChain(newChainName);
                                
                                ofLogNotice("SessionManager") << "Late migration: Migrated chain for sequencer '" << name 
                                                              << "' from 'TrackerSequencer_chain' to '" << newChainName << "'";
                            }
                        }
                        
                        // CRITICAL: If sequencer has no chain binding, create instance-specific chain
                        // This ensures all sequencers have their own chains, even if they weren't in saved bindings
                        if (binding.chainName.empty()) {
                            std::string newChainName = name + "_chain";
                            if (!patternRuntime_->chainExists(newChainName)) {
                                patternRuntime_->addChain(newChainName);
                                ofLogNotice("SessionManager") << "Created instance-specific chain '" << newChainName 
                                                              << "' for sequencer '" << name << "' (no binding found)";
                            }
                            // Bind sequencer to the new chain
                            patternRuntime_->bindSequencerChain(name, newChainName);
                            patternRuntime_->setSequencerChainEnabled(name, true);
                            tracker->bindToChain(newChainName);
                            ofLogNotice("SessionManager") << "Bound sequencer '" << name << "' to chain '" << newChainName << "'";
                        }
                        
                        // If no pattern binding, ensure sequencer gets one
                        if (binding.patternName.empty()) {
                            // Get available patterns
                            auto patternNames = patternRuntime_->getPatternNames();
                            
                            if (!patternNames.empty()) {
                                // Bind to first available pattern
                                ofLogNotice("SessionManager") << "Auto-binding sequencer '" << name 
                                                              << "' to pattern '" << patternNames[0] << "'";
                                tracker->bindToPattern(patternNames[0]);
                            } else {
                                // No patterns exist - create default pattern
                                Pattern defaultPattern(16);
                                std::string patternName = patternRuntime_->addPattern(defaultPattern, "");
                                if (!patternName.empty()) {
                                    ofLogNotice("SessionManager") << "Created default pattern '" << patternName 
                                                                  << "' for sequencer '" << name << "'";
                                    tracker->bindToPattern(patternName);
                                }
                            }
                        } else {
                            // Pattern binding exists - verify it's still valid
                            if (!patternRuntime_->patternExists(binding.patternName)) {
                                // Pattern doesn't exist - bind to first available or create default
                                auto patternNames = patternRuntime_->getPatternNames();
                                if (!patternNames.empty()) {
                                    ofLogWarning("SessionManager") << "Pattern '" << binding.patternName 
                                                                   << "' not found for sequencer '" << name 
                                                                   << "', binding to '" << patternNames[0] << "'";
                                    tracker->bindToPattern(patternNames[0]);
                                } else {
                                    Pattern defaultPattern(16);
                                    std::string patternName = patternRuntime_->addPattern(defaultPattern, "");
                                    if (!patternName.empty()) {
                                        tracker->bindToPattern(patternName);
                                    }
                                }
                            }
                        }
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
                                                                                       << " for TrackerSequencer '" << tracker->getInstanceName() << "' after chain reload";
                                                        break;
                                                    }
                                                }
                                            }
                                            
                                            reloadedCount++;
                                            ofLogVerbose("SessionManager") << "Reloaded pattern chain for TrackerSequencer '" 
                                                                           << tracker->getInstanceName() << "'";
                                        } catch (const std::exception& e) {
                                            ofLogWarning("SessionManager") << "Failed to reload pattern chain for TrackerSequencer '" 
                                                                           << tracker->getInstanceName() << "': " << e.what();
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
                                    ofLogNotice("SessionManager") << "TrackerSequencer '" << tracker->getInstanceName() 
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
                                                                      << "' for TrackerSequencer '" << tracker->getInstanceName() << "'";
                                    }
                                }
                            } else if (!patternRuntime_->patternExists(boundName)) {
                                // Bound pattern doesn't exist - bind to first available pattern or create default
                                if (!patternNames.empty()) {
                                    ofLogWarning("SessionManager") << "TrackerSequencer '" << tracker->getInstanceName() 
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
                                                                      << "' for TrackerSequencer '" << tracker->getInstanceName() << "'";
                                    }
                                }
                            } else {
                                // Pattern exists and is valid - verify binding worked
                                ofLogVerbose("SessionManager") << "TrackerSequencer '" << tracker->getInstanceName() 
                                                                << "' correctly bound to pattern '" << boundName << "'";
                                
                                // CRITICAL: Sync pattern chain current index with bound pattern after restoration
                                // This ensures the GUI displays the correct active pattern in the chain
                                const auto& chain = tracker->getPatternChain();
                                if (tracker->getUsePatternChain() && !chain.empty()) {
                                    for (size_t i = 0; i < chain.size(); ++i) {
                                        if (chain[i] == boundName) {
                                            tracker->setCurrentChainIndex((int)i);
                                            ofLogVerbose("SessionManager") << "Synced chain index to " << i 
                                                                           << " for TrackerSequencer '" << tracker->getInstanceName() << "'";
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
    
    // Load GUI state (for backward compatibility - UI state should be handled by Shells)
    // Note: This method is kept for backward compatibility but will be deprecated
    // GUI state loading is now handled by Shells (EditorShell)
    if (json.contains("gui") && json["gui"].is_object()) {
        auto guiJson = json["gui"];
        
        // Store ImGui window state for later loading (for backward compatibility)
        // Note: This is deprecated - UI state should be handled by Shells
        if (guiJson.contains("imguiState") && guiJson["imguiState"].is_string()) {
            pendingImGuiState_ = guiJson["imguiState"].get<std::string>();
            originalImGuiState_ = pendingImGuiState_;  // Preserve original for later reload if needed
            ofLogNotice("SessionManager") << "✓ Stored ImGui window state for later loading (" 
                                         << pendingImGuiState_.size() << " bytes)";
        } else {
            pendingImGuiState_.clear();
            originalImGuiState_.clear();
        }
        
        // Store visibility state for later restoration (for backward compatibility)
        // Note: This is deprecated - UI state should be handled by Shells
        if (guiJson.contains("visibleInstances") && guiJson["visibleInstances"].is_object()) {
            pendingVisibilityState_ = guiJson["visibleInstances"];
            ofLogNotice("SessionManager") << "Stored module instance visibility state for later restoration";
        } else {
            pendingVisibilityState_ = ofJson::object();
        }
        
        // Note: Module layouts, view state, and other GUI state are now handled by Shells
    }
    
    ofLogNotice("SessionManager") << "Session loaded successfully";
    
    // Call post-load callback if set (for re-initializing audio streams, etc.)
    if (postLoadCallback_) {
        postLoadCallback_();
    }
    
    // Note: GUI state restoration (visibility, ImGui layout) is now handled by Shells (EditorShell)
    
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
    
    // Note: ImGui state update is now handled by Shells (EditorShell)
    
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

// Note: restoreVisibilityState(), loadPendingImGuiState(), updateImGuiStateInSession(), and setupGUI() 
// have been removed - UI state management is now handled by Shells (EditorShell)

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

// Note: setupGUI() has been removed - GUI setup is now handled by Shells (EditorShell)

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

//--------------------------------------------------------------
void SessionManager::serializationThreadFunction() {
    SerializationRequest request;
    
    while (!shouldStopSerializationThread_.load()) {
        // Blocking wait with timeout (wakes up every 100ms to check shouldStopSerializationThread_)
        // Lock-free: no mutex needed, BlockingConcurrentQueue handles synchronization internally
        if (serializationQueue_.wait_dequeue_timed(request, std::chrono::milliseconds(100))) {
            // Process serialization request
            processSerializationRequest(request);
        }
        // If timeout, loop continues and checks shouldStopSerializationThread_ again
    }
}

//--------------------------------------------------------------
bool SessionManager::processSerializationRequest(const SerializationRequest& request) {
    if (!engine_) {
        ofLogError("SessionManager") << "Cannot serialize: Engine not set";
        return false;
    }
    
    // Check if snapshot is stale (version changed since request was queued)
    uint64_t currentVersion = engine_->getStateVersion();
    if (request.snapshotVersion < currentVersion) {
        ofLogNotice("SessionManager") << "Snapshot is stale (version " << request.snapshotVersion 
                                      << " < current " << currentVersion 
                                      << "), getting fresh snapshot";
        
        // Get fresh snapshot (lock-free read)
        auto freshSnapshot = engine_->getStateSnapshot();
        if (!freshSnapshot) {
            ofLogError("SessionManager") << "Cannot get fresh snapshot, using stale snapshot";
            // Continue with stale snapshot (better than failing)
        } else {
            // Use fresh snapshot instead
            SerializationRequest freshRequest = request;
            freshRequest.snapshot = freshSnapshot;
            if (freshSnapshot->contains("version")) {
                freshRequest.snapshotVersion = freshSnapshot->at("version").get<uint64_t>();
            } else {
                freshRequest.snapshotVersion = currentVersion;
            }
            
            // Process with fresh snapshot
            return processSerializationRequestWithSnapshot(freshRequest);
        }
    }
    
    // Process with original snapshot (not stale or fallback)
    return processSerializationRequestWithSnapshot(request);
}

//--------------------------------------------------------------
bool SessionManager::processSerializationRequestWithSnapshot(const SerializationRequest& request) {
    // Get snapshot (lock-free read)
    auto snapshot = request.snapshot;
    if (!snapshot) {
        ofLogError("SessionManager") << "Cannot serialize: Snapshot is null";
        return false;
    }
    
    try {
        // Serialize snapshot to JSON (snapshot is already JSON, but we may need to merge with UI state)
        ofJson json = *snapshot;
        
        // Add project root to session if ProjectManager is available
        if (projectManager_ && projectManager_->isProjectOpen()) {
            json["projectRoot"] = projectManager_->getProjectRoot();
        }
        
        // Create backup of existing session file before overwriting
        if (ofFile::doesFileExist(request.filePath)) {
            std::string backupPath = request.filePath + ".backup";
            if (ofFile::copyFromTo(request.filePath, backupPath, true, true)) {
                ofLogVerbose("SessionManager") << "Created backup: " << backupPath;
            } else {
                ofLogWarning("SessionManager") << "Failed to create backup, continuing with save anyway";
            }
        }
        
        // Write to file (I/O in background thread)
        ofFile file(request.filePath, ofFile::WriteOnly);
        if (!file.is_open()) {
            ofLogError("SessionManager") << "Failed to open file for writing: " << request.filePath;
            return false;
        }
        
        file << json.dump(4); // Pretty print with 4 spaces
        file.close();
        
        ofLogNotice("SessionManager") << "Session saved asynchronously to " << request.filePath 
                                      << " (version " << request.snapshotVersion << ")";
        return true;
    } catch (const std::exception& e) {
        ofLogError("SessionManager") << "Exception while saving session: " << e.what();
        return false;
    } catch (...) {
        ofLogError("SessionManager") << "Unknown exception while saving session";
        return false;
    }
}

//--------------------------------------------------------------
bool SessionManager::saveSessionAsync(const std::string& filePath) {
    if (!engine_) {
        ofLogError("SessionManager") << "Cannot save async: Engine not set";
        return false;
    }
    
    // Get current snapshot from Engine (lock-free read)
    auto snapshot = engine_->getStateSnapshot();
    if (!snapshot) {
        ofLogWarning("SessionManager") << "No snapshot available, creating one synchronously";
        // Fallback: use synchronous serialization if snapshot not available
        return saveSessionToPath(filePath);
    }
    
    // Get snapshot version from Engine (atomic read)
    uint64_t snapshotVersion = engine_->getStateVersion();
    
    // Also check version in JSON snapshot (should match, but use Engine version as source of truth)
    if (snapshot->contains("version")) {
        uint64_t jsonVersion = snapshot->at("version").get<uint64_t>();
        if (jsonVersion != snapshotVersion) {
            ofLogWarning("SessionManager") << "Snapshot version mismatch: JSON=" << jsonVersion 
                                            << " Engine=" << snapshotVersion 
                                            << ", using Engine version";
        }
    }
    
    // Create serialization request
    SerializationRequest request;
    request.filePath = filePath;
    request.snapshot = snapshot;  // Shared pointer to immutable JSON
    request.snapshotVersion = snapshotVersion;
    request.timestamp = std::chrono::steady_clock::now();
    
    // Queue request (non-blocking, lock-free)
    // try_enqueue returns false if queue is full (shouldn't happen in practice)
    if (!serializationQueue_.try_enqueue(request)) {
        ofLogError("SessionManager") << "Failed to queue async save request (queue full)";
        return false;
    }
    
    ofLogVerbose("SessionManager") << "Queued async save request for " << filePath 
                                    << " (version " << snapshotVersion << ")";
    return true;
}

