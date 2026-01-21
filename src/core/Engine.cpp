#include "Engine.h"
#include "core/lua/LuaGlobals.h"
#include "shell/Shell.h"
#include "ofLog.h"
#include "ofFileUtils.h"
#include "ofxLua.h"
#include "ofEvents.h"
#include <algorithm>
#include <set>
#include <fstream>
#include <chrono>
#include <shared_mutex>
#include <mutex>
#include <thread>

namespace vt {

// Thread-local flag to detect recursive snapshot building
thread_local bool Engine::isBuildingSnapshot_ = false;

// Thread ID tracking (Phase 7.9 Plan 5)
std::thread::id Engine::mainThreadId_;
std::thread::id Engine::audioThreadId_;

Engine::Engine() 
    : assetLibrary_(&projectManager_, &mediaConverter_, &moduleRegistry_)
    , scriptManager_(this) {
    // Initialize snapshot system
    snapshotJson_ = nullptr;  // Will be created on first updateStateSnapshot() call
    stateVersion_ = 0;
    
    // Initialize immutable state snapshot (Phase 7.9-9.2)
    // Create empty state snapshot initially, will be updated during safe periods
    immutableStateSnapshot_ = std::make_shared<const EngineState>();
}

Engine::~Engine() {
    // Cleanup handled by member destructors
}

void Engine::setup(const EngineConfig& config) {
    // Track main thread ID for thread safety assertions (Phase 7.9 Plan 5)
    setMainThreadId(std::this_thread::get_id());
    
    if (isSetup_) {
        ofLogWarning("Engine") << "Engine already setup, skipping";
        return;
    }
    
    config_ = config;
    // Phase 2 Simplification: Removed cachedState_ initialization
    // All state now goes through immutableStateSnapshot_ via updateImmutableStateSnapshot()
    
    // Setup Clock first (foundation for timing)
    clock_.setup();
    
    // Setup PatternRuntime (foundational system for patterns)
    patternRuntime_.setup(&clock_);
    
    // Setup core systems
    setupCoreSystems();
    
    // Setup master outputs
    setupMasterOutputs();
    
    // Setup command executor
    setupCommandExecutor();
    
    // Setup Lua scripting
    setupLua();
    
    // Initialize project and session FIRST (before ScriptManager)
    // This ensures session is loaded before script generation
    initializeProjectAndSession();
    
    // Build initial state snapshot and cache it BEFORE ScriptManager setup
    // This ensures ScriptManager gets a populated state with modules/connections
    // CRITICAL: This must happen before scriptManager_.setup() so the script is generated
    // from the loaded session state, not an empty cached state
    ofLogNotice("Engine") << "Building initial state snapshot...";
    size_t registryCountBefore = moduleRegistry_.getModuleCount();
    ofLogNotice("Engine") << "ModuleRegistry has " << registryCountBefore << " modules registered before snapshot";
    
    try {
        EngineState initialState = buildStateSnapshot();
        
        // Update immutable snapshot with initial state
        // This ensures getState() returns populated snapshot, not empty one
        initialState.version = stateVersion_.load();
        auto newSnapshot = std::make_shared<const EngineState>(std::move(initialState));
        {
            std::lock_guard<std::mutex> lock(immutableStateSnapshotMutex_);
            immutableStateSnapshot_ = newSnapshot;
        }
        ofLogNotice("Engine") << "Immutable state snapshot initialized with " << newSnapshot->modules.size() 
                             << " modules, " << newSnapshot->connections.size() << " connections";
        
        ofLogNotice("Engine") << "Initial state snapshot built - modules: " << newSnapshot->modules.size() 
                             << ", connections: " << newSnapshot->connections.size();
        // Log module names for debugging
        if (newSnapshot->modules.empty()) {
            ofLogError("Engine") << "ERROR: Initial state snapshot has NO modules!";
            ofLogError("Engine") << "ModuleRegistry has " << registryCountBefore << " modules, but snapshot has 0!";
            ofLogError("Engine") << "This will cause script generation to fail - modules won't appear in script!";
            // This is a critical error - the script will be empty
        } else {
            ofLogNotice("Engine") << "Modules in snapshot:";
            for (const auto& [name, moduleState] : newSnapshot->modules) {
                ofLogNotice("Engine") << "  - " << name << " (" << moduleState.type << ")";
            }
        }
    } catch (const std::exception& e) {
        ofLogError("Engine") << "Failed to build initial state snapshot: " << e.what();
        ofLogError("Engine") << "Immutable snapshot may be empty - script generation may fail!";
    }
    
    // Setup ScriptManager AFTER session is loaded AND initial snapshot is built
    // This ensures script is generated from loaded session state with modules/connections
    scriptManager_.setup();
    
    isSetup_ = true;
    ofLogNotice("Engine") << "Engine setup complete";
    
}

void Engine::setupCoreSystems() {
    // Setup core systems - CRITICAL: ParameterRouter needs registry BEFORE SessionManager uses it
    parameterRouter_.setRegistry(&moduleRegistry_);
    parameterRouter_.setEngine(this);  // Enable command queue access
    connectionManager_.setRegistry(&moduleRegistry_);
    connectionManager_.setParameterRouter(&parameterRouter_);
    connectionManager_.setPatternRuntime(&patternRuntime_);  // Enable PatternRuntime access for module initialization
    
    // Initialize SessionManager with dependencies (use move assignment since std::thread is not copyable)
    sessionManager_ = SessionManager(
        &projectManager_,
        &clock_,
        &moduleRegistry_,
        &moduleFactory_,
        &parameterRouter_,
        &connectionManager_
    );
    sessionManager_.setConnectionManager(&connectionManager_);
    sessionManager_.setPatternRuntime(&patternRuntime_);  // Phase 2: Enable pattern migration
    
    // Set Engine reference in SessionManager for async serialization
    sessionManager_.setEngine(this);
}

void Engine::setupMasterOutputs() {
    // Create master outputs using ModuleFactory (system modules)
    moduleFactory_.ensureSystemModules(&moduleRegistry_, config_.masterAudioOutName, config_.masterVideoOutName);
    
    // Get master outputs from registry
    masterAudioOut_ = std::dynamic_pointer_cast<AudioOutput>(moduleRegistry_.getModule(config_.masterAudioOutName));
    masterVideoOut_ = std::dynamic_pointer_cast<VideoOutput>(moduleRegistry_.getModule(config_.masterVideoOutName));
    
    if (!masterAudioOut_ || !masterVideoOut_) {
        ofLogError("Engine") << "Failed to create master outputs";
        return;
    }
    
    // Initialize master outputs
    masterAudioOut_->initialize(&clock_, &moduleRegistry_, &connectionManager_, &parameterRouter_, &patternRuntime_, false);
    masterVideoOut_->initialize(&clock_, &moduleRegistry_, &connectionManager_, &parameterRouter_, &patternRuntime_, false);
    
    // Initialize master oscilloscope and spectrogram if they exist
    auto masterOscilloscope = moduleRegistry_.getModule("masterOscilloscope");
    auto masterSpectrogram = moduleRegistry_.getModule("masterSpectrogram");
    
    if (masterOscilloscope) {
        masterOscilloscope->initialize(&clock_, &moduleRegistry_, &connectionManager_, &parameterRouter_, &patternRuntime_, false);
    }
    if (masterSpectrogram) {
        masterSpectrogram->initialize(&clock_, &moduleRegistry_, &connectionManager_, &parameterRouter_, &patternRuntime_, false);
    }
    
    // Setup default connections for master outputs
    connectionManager_.setupDefaultConnections(&clock_, config_.masterAudioOutName, config_.masterVideoOutName);
    
    // Subscribe to Clock BPM changes (proper event-based architecture)
    ofAddListener(clock_.bpmChangedEvent, this, &Engine::onBPMChanged);
    
    // Subscribe to Clock transport changes (start/stop/pause)
    // This ensures state is updated when clock:start() is called from Lua
    transportListenerId_ = clock_.addTransportListener([this](bool playing) {
        if (playing) {
            ofLogNotice("Engine") << "Transport started via Clock callback";
        } else {
            ofLogNotice("Engine") << "Transport stopped via Clock callback";
        }
        
        // Track transport state changes during script execution
        // This is needed because updateStateSnapshot() skips updates while script is executing
        // The flag will be checked at the end of eval() to trigger a deferred notification
        if (notifyingObservers_.load(std::memory_order_acquire)) {
            ofLogVerbose("Engine") << "Transport changed during script execution - deferring notification";
            transportStateChangedDuringScript_.store(true, std::memory_order_release);
            return;  // Don't enqueue now - will be handled after script completes
        }
        
        // Trigger state notification so observers know playing state changed
        enqueueStateNotification();
    });
    ofLogNotice("Engine") << "Transport listener registered (id: " << transportListenerId_ << ")";
}

void Engine::setupCommandExecutor() {
    // Setup CommandExecutor with dependencies (no UI dependencies)
    commandExecutor_.setup(&moduleRegistry_, &connectionManager_, &assetLibrary_, &clock_, &patternRuntime_, this);
    
    // Set callbacks for module operations
    commandExecutor_.setOnAddModule([this](const std::string& moduleType) {
        std::string moduleName = moduleRegistry_.addModule(
            moduleFactory_,
            moduleType,
            &clock_,
            &connectionManager_,
            &parameterRouter_,
            &patternRuntime_,
            [this](const std::string& name) {
                // Notify UI callback if registered
                if (onModuleAdded_) {
                    onModuleAdded_(name);
                }
            },
            config_.masterAudioOutName,
            config_.masterVideoOutName
        );
        // Use deferred notification pattern to prevent recursive notifications
        // These callbacks can be triggered during state notifications (e.g., when scripts add/remove modules)
        enqueueStateNotification();
    });
    
    commandExecutor_.setOnRemoveModule([this](const std::string& instanceName) {
        bool removed = moduleRegistry_.removeModule(
            instanceName,
            &connectionManager_,
            [this](const std::string& name) {
                // Notify UI callback if registered
                if (onModuleRemoved_) {
                    onModuleRemoved_(name);
                }
            },
            config_.masterAudioOutName,
            config_.masterVideoOutName
        );
        // Use deferred notification pattern to prevent recursive notifications
        enqueueStateNotification();
    });
    
    // Set parameter change notification callback for script sync
    // This will automatically update all existing modules' callbacks (including master outputs)
    moduleRegistry_.setParameterChangeNotificationCallback([this]() {
        // Phase 2 Simplification: Removed parametersBeingModified_ counter
        // All state changes go through command queue, so we just defer the notification
        // The notification will be processed on main thread via notification queue
        enqueueStateNotification();
        
        ofLogVerbose("Engine") << "[PARAM_CHANGE] Parameter changed, deferring state notification";
    });
}

// C function to execute commands from Lua
// This is registered directly with Lua using the Lua C API
static int lua_execCommand(lua_State* L) {
    if (lua_isstring(L, 1)) {
        const char* cmd = lua_tostring(L, 1);
        auto* engine = vt::lua::getGlobalEngine();
        if (engine) {
            auto result = engine->executeCommand(std::string(cmd));
            // Push result table onto stack
            lua_newtable(L);
            lua_pushboolean(L, result.success);
            lua_setfield(L, -2, "success");
            lua_pushstring(L, result.message.c_str());
            lua_setfield(L, -2, "message");
            lua_pushstring(L, result.error.c_str());
            lua_setfield(L, -2, "error");
            return 1; // Return 1 value (the result table)
        }
    }
    // Return error result
    lua_newtable(L);
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "success");
    lua_pushstring(L, "Invalid command or engine not available");
    lua_setfield(L, -2, "error");
    return 1;
}

void Engine::setupLua() {
    if (lua_) {
        ofLogWarning("Engine") << "Lua already initialized";
        return;
    }
    
    lua_ = std::make_unique<ofxLua>();
    
    // Initialize Lua with standard libs but without ofBindings
    bool success = lua_->init(false, true, false);
    
    if (!success) {
        ofLogError("Engine") << "Failed to initialize Lua";
        lua_.reset();
        return;
    }
    
    // Set global engine pointer for helper functions
    vt::lua::setGlobalEngine(this);
    
    if (lua_ && lua_->isValid()) {
        // Get lua_State* from ofxLua (it has operator lua_State*())
        lua_State* L = *lua_;
        
        // Register exec() function directly using Lua C API
        // This is the simplest approach - one C function that executes commands
        lua_register(L, "exec", lua_execCommand);
        
        // CRITICAL: Register 'engine' global for ScriptManager-generated scripts
        // This makes engine:* methods available in all Lua scripts
        vt::lua::registerEngineGlobal(*lua_);
        
        ofLogNotice("Engine") << "Lua initialized with engine global";
    } else {
        ofLogWarning("Engine") << "Lua state not valid, cannot register functions";
    }
}

void Engine::initializeProjectAndSession() {
    // Initialize project and session (opens/creates project, loads default session)
    std::string dataPath = ofToDataPath("", true);
    bool sessionLoaded = sessionManager_.initializeProjectAndSession(dataPath);
    
    if (onProjectOpened_ && projectManager_.isProjectOpen()) {
        onProjectOpened_();
    }
    
    if (!sessionLoaded) {
        // If no session was loaded, ensure default modules exist
        sessionManager_.ensureDefaultModules({"TrackerSequencer", "MultiSampler"});
    }
    
    // CRITICAL: Ensure system modules exist after session load
    moduleFactory_.ensureSystemModules(&moduleRegistry_, config_.masterAudioOutName, config_.masterVideoOutName);
    
    // CRITICAL: Refresh master outputs after session load
    masterAudioOut_ = std::dynamic_pointer_cast<AudioOutput>(moduleRegistry_.getModule(config_.masterAudioOutName));
    masterVideoOut_ = std::dynamic_pointer_cast<VideoOutput>(moduleRegistry_.getModule(config_.masterVideoOutName));
    
    if (!masterAudioOut_ || !masterVideoOut_) {
        ofLogError("Engine") << "Failed to refresh master outputs after session load";
        return;
    }
    
    // Re-initialize master outputs after session load
    masterAudioOut_->initialize(&clock_, &moduleRegistry_, &connectionManager_, &parameterRouter_, &patternRuntime_, true);
    masterVideoOut_->initialize(&clock_, &moduleRegistry_, &connectionManager_, &parameterRouter_, &patternRuntime_, true);
    
    // Initialize master oscilloscope and spectrogram if they exist
    auto masterOscilloscope = moduleRegistry_.getModule("masterOscilloscope");
    auto masterSpectrogram = moduleRegistry_.getModule("masterSpectrogram");
    
    if (masterOscilloscope) {
        masterOscilloscope->initialize(&clock_, &moduleRegistry_, &connectionManager_, &parameterRouter_, &patternRuntime_, true);
    }
    if (masterSpectrogram) {
        masterSpectrogram->initialize(&clock_, &moduleRegistry_, &connectionManager_, &parameterRouter_, &patternRuntime_, true);
    }
    
    // Setup automatic routing for master oscilloscope and spectrogram
    if (masterOscilloscope) {
        connectionManager_.connectAudio(config_.masterAudioOutName, "masterOscilloscope");
        connectionManager_.connectVideo("masterOscilloscope", config_.masterVideoOutName);
    }
    
    if (masterSpectrogram) {
        connectionManager_.connectAudio(config_.masterAudioOutName, "masterSpectrogram");
        connectionManager_.connectVideo("masterSpectrogram", config_.masterVideoOutName);
    }
    
    
    // Enable auto-save if configured
    if (config_.enableAutoSave) {
        sessionManager_.enableAutoSave(config_.autoSaveInterval, onUpdateWindowTitle_);
    }
    
    // Use deferred notification pattern for consistency with all other state changes
    // This ensures notifications happen on main thread event loop, not during setup
    enqueueStateNotification();
}

void Engine::setupAudio(int sampleRate, int bufferSize) {
    // Track audio thread ID for thread safety assertions (Phase 7.9 Plan 5)
    // Note: Audio thread ID is set when audioOut() is first called (from audio thread)
    // This method is called from main thread, so we can't set it here
    // Audio thread ID will be set in audioOut() on first call
    if (masterAudioOut_) {
        // AudioOutput manages its own soundStream internally
        // This is a placeholder for future audio setup if needed
    }
}


Engine::Result Engine::executeCommand(const std::string& command) {
    try {
        // Capture output from CommandExecutor
        std::string capturedOutput;
        
        // Set up output callback to capture messages
        auto oldCallback = commandExecutor_.getOutputCallback();
        commandExecutor_.setOutputCallback([&capturedOutput](const std::string& msg) {
            if (!capturedOutput.empty()) {
                capturedOutput += "\n";
            }
            capturedOutput += msg;
        });
        
        // Execute command
        commandExecutor_.executeCommand(command);
        
        // Restore old callback
        commandExecutor_.setOutputCallback(oldCallback);
        
        // Don't call updateStateSnapshot() directly
        // executeCommand() can be called from any thread (main thread or script execution thread)
        // updateStateSnapshot() must only be called from main thread (has ASSERT_MAIN_THREAD())
        // enqueueStateNotification() will call updateStateSnapshot() on main thread via notification queue
        // This prevents race conditions where both threads try to serialize modules simultaneously
        // This matches the pattern used by processCommands() (line 2436)
        enqueueStateNotification();  // Enqueues updateStateSnapshot() to main thread
        
        // Return captured output (or success message if no output)
        if (!capturedOutput.empty()) {
            // Remove the command echo ("> command") from output
            std::string output = capturedOutput;
            size_t firstNewline = output.find('\n');
            if (firstNewline != std::string::npos && output.find("> ") == 0) {
                output = output.substr(firstNewline + 1);
            }
            return Result(true, output);
        }
        
        return Result(true, "Command executed successfully");
    } catch (const std::exception& e) {
        return Result(false, "Command execution failed", e.what());
    } catch (...) {
        return Result(false, "Command execution failed", "Unknown error");
    }
}

Engine::Result Engine::eval(const std::string& script) {
    if (!lua_) {
        setupLua();
    }
    
    if (!lua_ || !lua_->isValid()) {
        return Result(false, "Lua not initialized", "Failed to initialize Lua state");
    }
    
    // Set script execution flag BEFORE any script execution begins
    // This must be set as early as possible to prevent any code path from calling
    // getState() or buildStateSnapshot() during script execution
    // This prevents crashes when script execution triggers state changes or callbacks
    notifyingObservers_.store(true, std::memory_order_release);
    
    // CRITICAL: Ensure flag is visible to all threads immediately
    // Use memory barrier to ensure flag is set before any script execution
    std::atomic_thread_fence(std::memory_order_seq_cst);
    
    try {
        // Set error callback to capture errors
        std::string luaError;
        lua_->setErrorCallback([&luaError](std::string& msg) {
            luaError = msg;
        });
        
        // Execute Lua script with additional safety
        // Wrap in try-catch to handle any C++ exceptions from SWIG wrappers
        bool success = false;
        try {
            success = lua_->doString(script);
        } catch (const std::bad_alloc& e) {
            // Memory allocation failure
            notifyingObservers_.store(false, std::memory_order_release);
            return Result(false, "Lua execution failed", "Memory allocation error: " + std::string(e.what()));
        } catch (const std::exception& e) {
            // Other C++ exceptions from SWIG wrappers
            notifyingObservers_.store(false, std::memory_order_release);
            return Result(false, "Lua execution failed", "C++ exception: " + std::string(e.what()));
        } catch (...) {
            // Unknown exceptions
            notifyingObservers_.store(false, std::memory_order_release);
            return Result(false, "Lua execution failed", "Unknown C++ exception during script execution");
        }
        
        // Clear script execution flag before returning
        notifyingObservers_.store(false, std::memory_order_release);
        
        // Check if transport state changed during script execution
        // If so, trigger a deferred notification to update observers with the new playing state
        bool transportChanged = transportStateChangedDuringScript_.exchange(false, std::memory_order_acq_rel);
        if (transportChanged) {
            ofLogVerbose("Engine") << "Deferred transport state change detected - triggering notification after script";
            enqueueStateNotification();
        }
        
        // State notifications will happen automatically when commands are processed
        // No need to manually trigger them here
        if (success) {
            return Result(true, "Script executed successfully");
        } else {
            // Extract error line number if possible
            int errorLine = 0;
            std::string errorMsg = luaError.empty() ? lua_->getErrorMessage() : luaError;
            
            // Try to parse line number from error message (format: "filename:line: message")
            size_t colonPos = errorMsg.find(':');
            if (colonPos != std::string::npos) {
                size_t secondColon = errorMsg.find(':', colonPos + 1);
                if (secondColon != std::string::npos) {
                    std::string lineStr = errorMsg.substr(colonPos + 1, secondColon - colonPos - 1);
                    try {
                        errorLine = std::stoi(lineStr);
                    } catch (...) {
                        // Ignore parse errors
                    }
                }
            }
            
            // Improve error message for user clarity
            std::string userFriendlyError = "Script execution error: " + errorMsg;
            if (errorMsg.find("attempt to") != std::string::npos) {
                userFriendlyError += " - Check script syntax and variable names";
            } else if (errorMsg.find("nil value") != std::string::npos) {
                userFriendlyError += " - Variable or function may not be defined";
            }
            
            return Result(false, "Lua execution failed", userFriendlyError);
        }
    } catch (const std::exception& e) {
        // Clear flag on exception
        notifyingObservers_.store(false, std::memory_order_release);
        
        // Check if transport state changed during script execution (even on error)
        bool transportChanged = transportStateChangedDuringScript_.exchange(false, std::memory_order_acq_rel);
        if (transportChanged) {
            ofLogVerbose("Engine") << "Deferred transport state change detected (exception path) - triggering notification";
            enqueueStateNotification();
        }
        
        ofLogError("Engine") << "Script execution exception: " << e.what();
        return Result(false, "Script execution failed", "C++ exception: " + std::string(e.what()));
    } catch (...) {
        // Clear flag on unknown exception
        notifyingObservers_.store(false, std::memory_order_release);
        
        // Check if transport state changed during script execution (even on error)
        bool transportChanged = transportStateChangedDuringScript_.exchange(false, std::memory_order_acq_rel);
        if (transportChanged) {
            ofLogVerbose("Engine") << "Deferred transport state change detected (unknown exception path) - triggering notification";
            enqueueStateNotification();
        }
        
        ofLogError("Engine") << "Script execution unknown exception";
        return Result(false, "Script execution failed", "Unknown error occurred during script execution");
    }
}

void Engine::syncScriptToEngine(const std::string& script, std::function<void(bool success)> callback) {
    // Phase 1 Redesign: Non-blocking script execution
    // 
    // REMOVED: Blocking waitForStateVersion() - caused deadlock because:
    //   1. Main thread blocked waiting for state version
    //   2. Notification queue can only be processed on main thread (in update())
    //   3. updateStateSnapshot() never called → state version never increments → timeout
    //
    // NEW DESIGN: Fire-and-forget with immediate callback
    //   1. Execute script immediately (enqueues commands)
    //   2. Call callback with script execution success/failure
    //   3. State will update asynchronously when notifications are processed
    //   4. Observers will be notified when state is ready
    
    // Execute script synchronously (enqueues commands to audio thread)
    Result result = eval(script);
    
    if (!result.success) {
        // Script execution failed - call callback with failure
        ofLogError("Engine") << "syncScriptToEngine: Script execution failed - " << result.error;
        if (callback) {
            callback(false);
        }
        return;
    }
    
    // Script executed successfully
    // Commands are now queued and will be processed by audio thread
    // State will update when notifications are processed in update() loop
    ofLogVerbose("Engine") << "syncScriptToEngine: Script executed successfully (fire-and-forget)";
    
    // Call callback immediately with success
    // State observers will be notified asynchronously when commands are processed
    if (callback) {
        callback(true);
    }
}

Engine::Result Engine::evalFile(const std::string& path) {
    if (!lua_) {
        setupLua();
    }
    
    if (!lua_ || !lua_->isValid()) {
        return Result(false, "Lua not initialized", "Failed to initialize Lua state");
    }
    
    try {
        // Set error callback to capture errors
        std::string luaError;
        lua_->setErrorCallback([&luaError](std::string& msg) {
            luaError = msg;
        });
        
        // Execute Lua script file
        bool success = lua_->doScript(path, false);
        
        if (success) {
            return Result(true, "Script file executed successfully");
        } else {
            std::string errorMsg = luaError.empty() ? lua_->getErrorMessage() : luaError;
            return Result(false, "Lua file execution failed", errorMsg);
        }
    } catch (const std::exception& e) {
        return Result(false, "Lua file execution failed", e.what());
    } catch (...) {
        return Result(false, "Lua file execution failed", "Unknown error");
    }
}

EngineState Engine::getState() const {
    // Use immutable snapshot instead of building during unsafe periods
    // This prevents memory corruption from building snapshots during script execution
    // MEMORY SAFETY: Returns copy (not reference) to ensure deep copy semantics
    auto snapshot = getImmutableStateSnapshot();
    
    if (isInUnsafeState()) {
        // Unsafe period - return copy of immutable snapshot (lock-free read)
        // Copy ensures deep copy semantics (EngineState copy constructor handles it)
        return *snapshot;  // Returns copy (EngineState copy constructor)
    }
    
    // Safe period - snapshot may be stale, but we can't update here (const method)
    // The snapshot will be updated by notifyStateChange() after commands complete
    // For now, return copy of current snapshot
    return *snapshot;  // Returns copy (EngineState copy constructor)
}

EngineState::ModuleState Engine::getModuleState(const std::string& name) const {
    auto state = getState();
    auto it = state.modules.find(name);
    if (it != state.modules.end()) {
        return it->second;
    }
    return EngineState::ModuleState();
}

// ═══════════════════════════════════════════════════════════
// IMMUTABLE STATE SNAPSHOT (Phase 7.9-9.2)
// ═══════════════════════════════════════════════════════════

std::shared_ptr<const EngineState> Engine::getImmutableStateSnapshot() const {
    // Lock-free read (mutex-protected for C++17 compatibility)
    // Memory barrier ensures we see the latest snapshot update
    std::atomic_thread_fence(std::memory_order_acquire);
    std::lock_guard<std::mutex> lock(immutableStateSnapshotMutex_);
    
    // Safety check: ensure snapshot is never null
    if (!immutableStateSnapshot_) {
        ofLogError("Engine") << "getImmutableStateSnapshot() called but snapshot is null - returning empty state";
        // Return empty snapshot as fallback (should never happen, but safety check)
        return std::make_shared<const EngineState>();
    }
    
    return immutableStateSnapshot_;  // Fast read with mutex (C++17 compatible)
}

void Engine::updateImmutableStateSnapshot() {
    ASSERT_MAIN_THREAD();
    
    // CRITICAL: Only update during safe periods
    if (isInUnsafeState()) {
        ofLogVerbose("Engine") << "updateImmutableStateSnapshot() blocked - unsafe state";
        return;  // Don't update during script execution or command processing
    }
    
    // Build state snapshot (from current implementation)
    EngineState state = buildStateSnapshot();
    
    // Set version before creating immutable snapshot
    state.version = stateVersion_.load();
    
    // Create new immutable snapshot (shared_ptr to const)
    auto newSnapshot = std::make_shared<const EngineState>(std::move(state));
    
    // Atomic pointer swap (mutex-protected for C++17 compatibility)
    // Memory barrier ensures all writes are visible before pointer swap
    std::atomic_thread_fence(std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(immutableStateSnapshotMutex_);
        auto oldSnapshot = immutableStateSnapshot_;
        immutableStateSnapshot_ = newSnapshot;
        // Old snapshot automatically deleted when last reader releases it (shared_ptr)
    }
    
    ofLogVerbose("Engine") << "Immutable state snapshot updated (version: " << newSnapshot->version << ")";
}

// ═══════════════════════════════════════════════════════════
// SHELL-SAFE API (for ScriptManager operations)
// ═══════════════════════════════════════════════════════════

void vt::Engine::setScriptUpdateCallback(std::function<void(const std::string&, uint64_t)> callback) {
    scriptManager_.setScriptUpdateCallback(callback);
}

void Engine::clearScriptUpdateCallback() {
    scriptManager_.clearScriptUpdateCallback();
    ofLogVerbose("Engine") << "Script update callback cleared via Shell-safe API";
}

void Engine::setScriptAutoUpdate(bool enabled) {
    
    scriptManager_.setAutoUpdate(enabled);
}

bool Engine::isScriptAutoUpdateEnabled() const {
    return scriptManager_.isAutoUpdateEnabled();
}

size_t Engine::subscribe(StateObserver callback) {
    std::unique_lock<std::shared_mutex> lock(stateMutex_);
    size_t id = nextObserverId_.fetch_add(1);
    observers_.emplace_back(id, callback);
    // Observers are notified in registration order (FIFO)
    // This ensures deterministic notification order
    return id;
}

void Engine::unsubscribe(size_t id) {
    std::unique_lock<std::shared_mutex> lock(stateMutex_);
    observers_.erase(
        std::remove_if(observers_.begin(), observers_.end(),
            [id](const auto& pair) { return pair.first == id; }),
        observers_.end()
    );
    // Note: If unsubscribe() is called during notification, it will block
    // until the current notification completes. The observer will still receive
    // the current notification, then be removed. This is safe and expected behavior.
}

void Engine::registerShell(vt::shell::Shell* shell) {
    if (!shell) {
        ofLogWarning("Engine") << "registerShell() called with null shell pointer";
        return;
    }
    
    std::unique_lock<std::shared_mutex> lock(stateMutex_);
    
    // Check if shell is already registered
    auto it = std::find(registeredShells_.begin(), registeredShells_.end(), shell);
    if (it != registeredShells_.end()) {
        ofLogWarning("Engine") << "Shell already registered: " << shell->getName();
        return;
    }
    
    // Add shell to registry (FIFO order)
    registeredShells_.push_back(shell);
    ofLogNotice("Engine") << "Shell registered: " << shell->getName() << " (total: " << registeredShells_.size() << ")";
    
    // Immediately notify shell of current state (with current version)
    lock.unlock();  // Release lock before calling shell method
    EngineState currentState = getState();
    uint64_t currentVersion = getStateVersion();
    shell->onStateChanged(currentState, currentVersion);
}

void Engine::unregisterShell(vt::shell::Shell* shell) {
    if (!shell) {
        ofLogWarning("Engine") << "unregisterShell() called with null shell pointer";
        return;
    }
    
    std::unique_lock<std::shared_mutex> lock(stateMutex_);
    
    // Remove shell from registry
    auto it = std::find(registeredShells_.begin(), registeredShells_.end(), shell);
    if (it != registeredShells_.end()) {
        registeredShells_.erase(it);
        ofLogNotice("Engine") << "Shell unregistered: " << shell->getName() << " (remaining: " << registeredShells_.size() << ")";
    } else {
        ofLogWarning("Engine") << "Shell not found in registry: " << shell->getName();
    }
}

void Engine::notifyAllShells(const EngineState& state, uint64_t stateVersion) {
    ASSERT_MAIN_THREAD();
    
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    
    // Notify all shells in registration order (FIFO)
    size_t notifiedCount = 0;
    for (vt::shell::Shell* shell : registeredShells_) {
        if (shell) {
            try {
                shell->onStateChanged(state, stateVersion);
                notifiedCount++;
            } catch (const std::exception& e) {
                ofLogError("Engine") << "Error notifying shell " << shell->getName() << ": " << e.what();
            } catch (...) {
                ofLogError("Engine") << "Unknown error notifying shell " << shell->getName();
            }
        }
    }
    
    if (notifiedCount > 0) {
        ofLogVerbose("Engine") << "Notified " << notifiedCount << " shells of state update (version: " << stateVersion << ")";
    }
}

void Engine::notifyStateChange() {
    ASSERT_MAIN_THREAD();
    
    // CRITICAL: Prevent recursive notifications
    // If an observer calls notifyStateChange() during notification, ignore it
    // This prevents infinite loops and ensures observers only read state, never modify
    bool expected = false;
    if (!notifyingObservers_.compare_exchange_strong(expected, true)) {
        // Already notifying - recursive call detected, ignore it
        ofLogWarning("Engine") << "Recursive notifyStateChange() call detected and ignored";
        return;
    }
    
    // Defer notifications during rendering
    // This prevents state observers from firing during ImGui rendering, which causes crashes
    // State updates during rendering can corrupt ImGui state and cause segmentation faults
    if (isRendering_.load()) {
        ofLogVerbose("Engine") << "Deferring state notification - rendering in progress";
        // Queue notification to be processed on main thread event loop (in update())
        // This ensures notifications happen after rendering completes, not during it
        notificationQueue_.enqueue([this]() {
            this->notifyObserversWithState();
        });
        // Release recursive notification guard before returning
        notifyingObservers_.store(false);
        return;
    }
    
    // Don't skip notifications during command processing
    // getState() now handles unsafe periods by returning cached state
    // This ensures observers always receive state updates, even during command processing
    // Observers will see the last known good state, which is acceptable
    // Notification queue ensures notifications happen on main thread after commands complete
    
    // Update immutable snapshot instead of building during unsafe periods
    // This prevents memory corruption from building snapshots during script execution
    if (!isInUnsafeState()) {
        // Safe period - update immutable snapshot
        updateImmutableStateSnapshot();
    } else {
        // Unsafe period - don't update (prevents race conditions)
        ofLogVerbose("Engine") << "notifyStateChange() deferred snapshot update - unsafe state";
    }
    
    // Get current immutable snapshot for observers
    auto snapshot = getImmutableStateSnapshot();
    
    // MEMORY SAFETY: snapshot is const, but we pass copy to observer
    // EngineState copy constructor handles deep copy
    EngineState state = *snapshot;  // Copy (EngineState copy constructor)
    
    
    
    // Collect broken observers during iteration, remove after
    // This prevents iterator invalidation and ensures all observers are called
    // Also handles edge cases:
    // - Observer exceptions: caught, logged, observer removed after iteration
    // - Observer unsubscription during notification: blocks until iteration completes (safe)
    // - Multiple observers: all called in registration order (FIFO)
    std::vector<size_t> brokenObservers;
    
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    size_t observerIndex = 0;
    // Iterate in registration order (FIFO) - ensures deterministic notification order
    for (const auto& [id, observer] : observers_) {
        try {
            // CRITICAL: Log immediately before calling observer
            {
            }
            observer(state);
            // CRITICAL: Log immediately after calling observer
            {
            }
        } catch (const std::exception& e) {
            ofLogError("Engine") << "Error in state observer " << id << ": " << e.what();
            brokenObservers.push_back(id);
        } catch (...) {
            ofLogError("Engine") << "Unknown error in state observer " << id;
            brokenObservers.push_back(id);
        }
        observerIndex++;
    }
    lock.unlock();  // Release lock before modifying observers map
    
    // Remove broken observers (after iteration to avoid iterator invalidation)
    if (!brokenObservers.empty()) {
        std::unique_lock<std::shared_mutex> writeLock(stateMutex_);
        // Create a set for efficient lookup
        std::set<size_t> brokenIds(brokenObservers.begin(), brokenObservers.end());
        // Remove all broken observers in one pass
        observers_.erase(
            std::remove_if(observers_.begin(), observers_.end(),
                [&brokenIds](const std::pair<size_t, StateObserver>& p) {
                    return brokenIds.find(p.first) != brokenIds.end();
                }),
            observers_.end()
        );
        for (size_t id : brokenObservers) {
            ofLogWarning("Engine") << "Removed broken observer " << id;
        }
    }
    
    
    // Release recursive notification guard
    notifyingObservers_.store(false);
}

void Engine::notifyObserversWithState() {
    // Helper method that actually calls all observers with current state
    // Called from queued notification callbacks (event-driven pattern)
    // Includes all safety checks (recursive guard, throttling, etc.)
    
    // MEMORY SAFETY (Phase 7.9 Plan 4): Observers receive deep copies of state
    // - getState() returns copy of immutableStateSnapshot_
    // - EngineState is passed by value to observers (copy of struct)
    // - All JSON data (typeSpecificData) is deep copied, no shared references
    // - Safe even if modules/connections are deleted after observer receives state
    
    // CRITICAL: Prevent recursive notifications
    bool expected = false;
    if (!notifyingObservers_.compare_exchange_strong(expected, true)) {
        // Already notifying - recursive call detected, ignore it
        ofLogWarning("Engine") << "Recursive notifyObserversWithState() call detected and ignored";
        return;
    }
    
    // Ensure state version matches engine version
    // State version only increments AFTER commands are processed and snapshot is updated
    // If state version is stale, rebuild snapshot to ensure observers see fresh state
    uint64_t currentEngineVersion = stateVersion_.load();
    
    // Get current state (with throttling - use immutableStateSnapshot_ as cache)
    uint64_t now = getCurrentTimestamp();
    uint64_t lastTime = lastStateSnapshotTime_.load();
    bool shouldBuildSnapshot = (now - lastTime >= STATE_SNAPSHOT_THROTTLE_MS);
    
    EngineState state;
    if (shouldBuildSnapshot) {
        lastStateSnapshotTime_.store(now);
        state = getState();  // Deep copy via immutableStateSnapshot_
        // Ensure state version matches engine version (should match after getState())
        state.version = currentEngineVersion;
    } else {
        // Throttled - use immutableStateSnapshot_ (already available)
        auto snapshot = getImmutableStateSnapshot();
        if (snapshot) {
            state = *snapshot;  // Copy of immutable snapshot (deep copies preserved)
            // Verify state version matches engine version
            // If stale, rebuild snapshot to ensure observers see fresh state
            if (state.version < currentEngineVersion) {
                ofLogVerbose("Engine") << "Immutable snapshot is stale (version: " << state.version 
                                       << ", engine: " << currentEngineVersion 
                                       << ") - rebuilding snapshot";
                state = getState();  // Deep copy via immutableStateSnapshot_
                state.version = currentEngineVersion;
            } else {
                // State is current - update version to match engine
                state.version = currentEngineVersion;
            }
        } else {
            // No snapshot available - build snapshot anyway
            ofLogWarning("Engine") << "notifyObserversWithState() throttled but no snapshot available - building snapshot anyway";
            state = getState();  // Deep copy via immutableStateSnapshot_
            state.version = currentEngineVersion;
        }
    }
    
    // Call all observers with deep-copied state (passed by value)
    std::vector<size_t> brokenObservers;
    
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    size_t observerIndex = 0;
    for (const auto& [id, observer] : observers_) {
        try {
            observer(state);
        } catch (const std::exception& e) {
            ofLogError("Engine") << "Error in state observer " << id << ": " << e.what();
            brokenObservers.push_back(id);
        } catch (...) {
            ofLogError("Engine") << "Unknown error in state observer " << id;
            brokenObservers.push_back(id);
        }
        observerIndex++;
    }
    lock.unlock();
    
    // Notify Lua callbacks (Phase 6.3)
    // CRITICAL: Lua callbacks can trigger engine methods that enqueue commands
    // These commands will be processed in the next update() cycle, not recursively
    // This prevents stack overflow from nested notifications
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex_);
        for (const auto& [id, callback] : luaCallbacks_) {
            try {
                if (callback && lua_) {
                    lua_State* L = *lua_;
                    // Callback will handle Lua stack operations
                    // Engine methods called from Lua will enqueue commands for next cycle
                    callback(L, state);
                }
            } catch (const std::exception& e) {
                ofLogError("Engine") << "Lua callback exception: " << e.what();
            }
        }
    }
    
    // Remove broken observers AFTER Lua callbacks to avoid iterator invalidation
    if (!brokenObservers.empty()) {
        std::unique_lock<std::shared_mutex> writeLock(stateMutex_);
        std::set<size_t> brokenIds(brokenObservers.begin(), brokenObservers.end());
        observers_.erase(
            std::remove_if(observers_.begin(), observers_.end(),
                [&brokenIds](const std::pair<size_t, StateObserver>& p) {
                    return brokenIds.find(p.first) != brokenIds.end();
                }),
            observers_.end()
        );
        for (size_t id : brokenObservers) {
            ofLogWarning("Engine") << "Removed broken observer " << id;
        }
    }
    
    // Release recursive notification guard
    notifyingObservers_.store(false);
    
    // Notify all registered shells with current state and version (Phase 7.9.2 Plan 3)
    // This ensures all shells receive state updates in registration order (FIFO)
    // Shells check state version to prevent processing stale updates
    notifyAllShells(state, currentEngineVersion);
}

// ═══════════════════════════════════════════════════════════
// LUA CALLBACK IMPLEMENTATION (Phase 6.3)
// ═══════════════════════════════════════════════════════════

size_t Engine::registerStateChangeCallback(LuaStateChangeCallback callback) {
    if (!callback) {
        ofLogWarning("Engine") << "registerStateChangeCallback: null callback ignored";
        return 0;
    }
    
    std::lock_guard<std::shared_mutex> lock(stateMutex_);
    
    // Generate unique ID (wrap-around is fine - we just need uniqueness for lifetime)
    size_t id = ++nextObserverId_;
    
    luaCallbacks_.push_back({id, std::move(callback)});
    
    ofLogNotice("Engine") << "Registered Lua state change callback: " << id;
    return id;
}

bool Engine::unregisterStateChangeCallback(size_t callbackId) {
    if (callbackId == 0) {
        return false;
    }
    
    std::lock_guard<std::shared_mutex> lock(stateMutex_);
    
    auto it = std::find_if(luaCallbacks_.begin(), luaCallbacks_.end(),
        [callbackId](const std::pair<size_t, LuaStateChangeCallback>& pair) {
            return pair.first == callbackId;
        });
    
    if (it != luaCallbacks_.end()) {
        luaCallbacks_.erase(it);
        ofLogNotice("Engine") << "Unregistered Lua state change callback: " << callbackId;
        return true;
    }
    
    ofLogWarning("Engine") << "unregisterStateChangeCallback: callback " << callbackId << " not found";
    return false;
}

void Engine::syncEngineToEditor(std::function<void(const EngineState&)> callback) {
    // Sync engine state to editor shells with completion guarantee (Engine → Editor Shell synchronization)
    // Ensures editor shells receive state updates with completion guarantee
    // Uses event-driven notification queue for proper ordering
    
    // Get current state snapshot (ensure it's up-to-date)
    EngineState state = getState();
    uint64_t currentVersion = state.version;
    
    // Queue notification to editor shells via notification queue
    // This ensures notifications happen on event loop, not during rendering
    notificationQueue_.enqueue([this, callback, state, currentVersion]() {
        // Notification is being delivered - verify state is current
        uint64_t actualVersion = stateVersion_.load();
        
        if (actualVersion >= currentVersion) {
            // State is current (or newer) - call callback with state snapshot
            if (callback) {
                callback(state);
            }
            ofLogVerbose("Engine") << "Engine → Editor Shell sync complete (version: " << currentVersion << ")";
        } else {
            // State is stale - get fresh state snapshot
            EngineState freshState = getState();
            if (callback) {
                callback(freshState);
            }
            ofLogWarning("Engine") << "Engine → Editor Shell sync used stale state (version: " << currentVersion << ", actual: " << actualVersion << ") - provided fresh state";
        }
    });
}

void Engine::enqueueStateNotification() {
    // Phase 2: Suppress duplicate notifications during parameter cascades
    // Only one notification needed even if multiple parameters change in cascade
    bool expected = false;
    if (!notificationEnqueued_.compare_exchange_strong(expected, true)) {
        // Already enqueued - skip to prevent notification storm
        return;
    }

    // Enqueue state notification to be processed on main thread
    // This replaces the stateNeedsNotification_ flag pattern - queue is single source of truth

    // Update monitoring: track enqueued notifications (Phase 7.9 Plan 8.2)
    queueMonitorStats_.notificationQueueTotalEnqueued++;

    notificationQueue_.enqueue([this]() {
        // Phase 2: Clear flag AFTER processing, before next notification batch
        notificationEnqueued_.store(false);

        // Update snapshot before notifying
        // This ensures state snapshot reflects processed commands before observers are notified
        // State version increments in updateStateSnapshot(), ensuring observers see fresh state
        updateStateSnapshot();
        
        // ALSO update the immutable state snapshot (EngineState)
        // This is needed because getState() returns immutableStateSnapshot_, not snapshotJson_
        // Both snapshots need to be in sync for state to be consistent
        updateImmutableStateSnapshot();
        
        // Add memory barrier to ensure snapshot update is visible to observers
        std::atomic_thread_fence(std::memory_order_release);
        
        // Notify observers with current state (state version verified in notifyObserversWithState())
        notifyObserversWithState();
    });
}

void Engine::processNotificationQueue() {
    // Process ALL queued notifications from notificationQueue_
    // Called from update() (main thread event loop)
    // Redesign: Remove rate-limiting to prevent state update timeouts
    // Process all notifications in a single frame to ensure consistency

    // Update monitoring: track queue depth before processing
    size_t queueDepthBefore = notificationQueue_.size_approx();
    queueMonitorStats_.notificationQueueCurrent = queueDepthBefore;
    if (queueDepthBefore > queueMonitorStats_.notificationQueueMax) {
        queueMonitorStats_.notificationQueueMax = queueDepthBefore;
    }

    std::function<void()> callback;
    while (notificationQueue_.try_dequeue(callback)) {
        if (callback) {
            try {
                callback();
                queueMonitorStats_.notificationQueueTotalProcessed++;
            } catch (const std::exception& e) {
                ofLogError("Engine") << "Error processing notification callback: " << e.what();
            } catch (...) {
                ofLogError("Engine") << "Unknown error processing notification callback";
            }
        }
    }

    // Update monitoring: track queue depth after processing
    size_t queueDepthAfter = notificationQueue_.size_approx();
    queueMonitorStats_.notificationQueueCurrent = queueDepthAfter;
}

void vt::Engine::notifyParameterChanged() {
    // Phase 2 Simplification: Removed parametersBeingModified_ check
    // All state changes go through command queue, so we always defer the notification
    // The notification will be processed on main thread via notification queue
    enqueueStateNotification();
}

void vt::Engine::onBPMChanged(float& newBpm) {

    // Defer state update if script is executing (prevents recursive updates)
    // The state will be updated after script execution completes
    if (notifyingObservers_.load(std::memory_order_acquire)) {
        ofLogVerbose("Engine") << "[BPM_CHANGE] BPM changed to " << newBpm << " during script execution - deferring state update";
        // Don't notify during script execution - it will be notified after execution completes
        return;
    }

    ofLogVerbose("Engine") << "[BPM_CHANGE] BPM changed to " << newBpm << ", notifying state change";
    notifyParameterChanged();
}

vt::EngineState vt::Engine::buildStateSnapshot() const {
    ASSERT_MAIN_THREAD();
    
    // Set thread-local flag to prevent recursive snapshot building
    // This prevents getCurrentScript() from calling getState() during snapshot building
    // Use RAII to ensure flag is always restored, even on exceptions or early returns
    struct SnapshotGuard {
        bool& flag_;
        bool wasBuilding_;
        SnapshotGuard(bool& flag) : flag_(flag), wasBuilding_(flag) {
            flag_ = true;
        }
        ~SnapshotGuard() {
            flag_ = wasBuilding_;
        }
    };
    SnapshotGuard guard(isBuildingSnapshot_);
    
    // Phase 2 Simplification: Use immutableStateSnapshot_ as fallback instead of cachedState_
    // The immutableStateSnapshot_ is maintained via the notification queue and is always up-to-date
    // This eliminates the need for cachedState_ and cachedStateMutex_
    
    // Check notification queue guard FIRST
    // If script is executing or commands are processing, return immutable snapshot immediately
    // This prevents state snapshot building during unsafe periods
    std::atomic_thread_fence(std::memory_order_acquire);
    bool isUnsafe = notifyingObservers_.load(std::memory_order_acquire);
    
    if (isUnsafe) {
        // Return immutable snapshot (always available after setup)
        auto snapshot = getImmutableStateSnapshot();
        
        if (snapshot) {
            EngineState result = *snapshot;
            result.version = stateVersion_.load();
            
            ofLogVerbose("Engine") << "buildStateSnapshot() BLOCKED during unsafe period - returning immutable snapshot (notifyingObservers: " 
                                   << isUnsafe << ")";
            return result;
        }
        
        // Safety fallback - should never happen
        ofLogError("Engine") << "buildStateSnapshot() - immutable snapshot not available, returning empty state";
        return EngineState();
    }
    
    // Prevent concurrent snapshot building
    // Use mutex for exclusive access during snapshot building
    // Note: Serialization no longer uses buildStateSnapshot() (uses getStateSnapshot() instead - lock-free)
    // This prevents crashes when multiple threads try to build snapshots simultaneously
    // (e.g., when ScriptManager observer fires while another snapshot is being built)
    // Phase 3: snapshotMutex_ is kept to prevent expensive concurrent buildStateSnapshot() calls
    // Unlike the previous pattern, we're using the notification queue guard for state detection,
    // not atomic flags - mutex only prevents concurrent expensive operations
    std::lock_guard<std::mutex> mutexGuard(snapshotMutex_);
    
    EngineState state;
    
    try {
        buildTransportState(state);
    } catch (const std::exception& e) {
        throw;
    } catch (...) {
        throw;
    }
    
    
    // Double-check unsafe state RIGHT BEFORE calling buildModuleStates()
    // State can change from safe to unsafe between the initial check and this point
    // (e.g., script execution can start on main thread while we're building snapshot)
    // This prevents buildModuleStates() from being called during script execution
    if (isInUnsafeState()) {
        ofLogVerbose("Engine") << "buildStateSnapshot() - unsafe state detected before buildModuleStates() - returning immutable snapshot";
        auto snapshot = getImmutableStateSnapshot();
        if (snapshot) {
            EngineState result = *snapshot;
            result.version = stateVersion_.load();
            return result;
        }
        // If no snapshot, return empty state (shouldn't happen)
        ofLogError("Engine") << "buildStateSnapshot() - unsafe state but no snapshot available";
        return EngineState();
    }
    
    try {
        // Check if buildModuleStates() completed successfully
        // If it returns false, it aborted due to unsafe period and left partial state
        // In this case, return immutable snapshot instead of partial state to prevent crashes
        bool buildSuccess = buildModuleStates(state);
        if (!buildSuccess) {
            // buildModuleStates() aborted - return immutable snapshot instead of partial state
            auto snapshot = getImmutableStateSnapshot();
            if (snapshot) {
                ofLogWarning("Engine") << "buildModuleStates() aborted due to unsafe period - returning immutable snapshot instead of partial state";
                EngineState result = *snapshot;
                result.version = stateVersion_.load();
                return result;
            } else {
                ofLogError("Engine") << "buildModuleStates() aborted but no snapshot available - returning partial state";
                // Return partial state as fallback (better than empty state)
            }
        } else {
            ofLogVerbose("Engine") << "buildModuleStates() completed successfully - state has " 
                                   << state.modules.size() << " modules, " 
                                   << state.connections.size() << " connections";
        }
        
    } catch (const std::exception& e) {
        throw;
    } catch (...) {
        throw;
    }
    
    try {
        buildConnectionStates(state);
    } catch (const std::exception& e) {
        throw;
    } catch (...) {
        throw;
    }
    
    // Build script state (from ScriptManager)
    // During snapshot building, only use cached script if available
    // Don't trigger script generation (which would call getState() and cause deadlock)
    // If script is empty, that's okay - it will be populated after snapshot completes
    if (scriptManager_.hasCachedScript()) {
        state.script.currentScript = scriptManager_.getCachedScript();
    } else {
        // Script not cached yet - use empty string (will be populated later)
        state.script.currentScript = "";
    }
    state.script.autoUpdateEnabled = scriptManager_.isAutoUpdateEnabled();
    
    // Set state version for consistency tracking
    state.version = stateVersion_.load();
    
    // Note: snapshotMutex_ is released automatically by lock_guard on function exit
    
    return state;
}

bool vt::Engine::waitForStateVersion(uint64_t targetVersion, uint64_t timeoutMs) {
    // DEPRECATED: This function is inherently broken when called from main thread
    // because it blocks the main thread while waiting for state version to increment,
    // but state version only increments in updateStateSnapshot() which is called from
    // processNotificationQueue() which runs on the main thread.
    // This creates a deadlock.
    //
    // Phase 1 Fix: Always return immediately with a warning.
    // Code that needs to wait for state should use the async observer pattern instead.
    
    ofLogWarning("Engine") << "waitForStateVersion() is DEPRECATED and causes deadlocks. "
                           << "Use async observer pattern instead. Returning immediately.";
    
    // Check current version - if already at target, return true for backward compat
    uint64_t currentVersion = stateVersion_.load(std::memory_order_acquire);
    if (currentVersion >= targetVersion) {
        return true;
    }
    
    // Otherwise return false - we can't safely wait
    return false;
}

void vt::Engine::updateStateSnapshot() {
    ASSERT_MAIN_THREAD();
    // Increment version number (atomic)
    // Use acquire-release semantics for state version updates
    uint64_t versionBefore = stateVersion_.load(std::memory_order_acquire);
    uint64_t version = stateVersion_.fetch_add(1, std::memory_order_acq_rel) + 1;
    // Memory barrier ensures version increment is visible to all threads
    std::atomic_thread_fence(std::memory_order_seq_cst);
    
    // Update monitoring: track state version increments and gaps (Phase 7.9 Plan 8.2)
    queueMonitorStats_.stateVersionIncrements++;
    uint64_t versionGap = version - queueMonitorStats_.stateVersionLastValue;
    if (versionGap > queueMonitorStats_.stateVersionMaxGap) {
        queueMonitorStats_.stateVersionMaxGap = versionGap;
    }
    if (versionGap > STATE_VERSION_GAP_WARNING_THRESHOLD) {
        ofLogWarning("Engine") << "State version gap detected: " << versionGap 
                               << " (previous: " << queueMonitorStats_.stateVersionLastValue 
                               << ", current: " << version << ")";
    }
    queueMonitorStats_.stateVersionLastValue = version;
    
    
    // Get transport state (clock_ is Engine member, no lock needed)
    EngineState::Transport transport;
    transport.isPlaying = clock_.isPlaying();
    transport.bpm = clock_.getTargetBPM();  // Use getTargetBPM() like buildTransportState()
    transport.currentBeat = 0;  // TODO: Get from Clock if available
    
    // Check if commands are processing before updating snapshots
    // Commands hold exclusive locks on moduleMutex_, so updateSnapshot() would block
    // Skip snapshot update if commands are processing (will be updated after commands complete)
    bool commandsProcessing = notifyingObservers_.load(std::memory_order_acquire);
    
    if (!commandsProcessing) {
        // Safe to update snapshots - no exclusive locks held
        // CRITICAL: Update all module snapshots before reading them
        // Module snapshots need to be refreshed after parameter changes
        // This ensures we have current state when aggregating snapshots
        moduleRegistry_.forEachModule([](const std::string& uuid, const std::string& humanName, std::shared_ptr<Module> module) {
            if (module) {
                module->updateSnapshot();  // ✅ Safe - no exclusive locks
            }
        });
    } else {
        // Commands processing - skip snapshot update (will be updated after commands complete)
        // Use existing snapshots (they're still valid, just may be slightly stale)
        ofLogVerbose("Engine") << "updateStateSnapshot() - skipping snapshot update (commands processing)";
    }
    
    // Get module snapshots (lock-free - Module::getSnapshot() uses mutex, but forEachModule handles registry locking)
    // This is the key difference from buildStateSnapshot() - we use module snapshots instead of building from scratch
    ofJson modulesJson;
    // forEachModule() handles registry locking internally
    moduleRegistry_.forEachModule([&modulesJson](const std::string& uuid, const std::string& humanName, std::shared_ptr<Module> module) {
        if (module) {
            // Hold shared_ptr reference to prevent destruction during copy
            auto moduleSnapshot = module->getSnapshot();  // Fast read with mutex (C++17 compatible)
            if (moduleSnapshot) {
                // Make a safe copy by serializing and deserializing
                // This prevents memory corruption if the original JSON is being destroyed
                // Add memory barrier to ensure snapshot is fully constructed before copying
                std::atomic_thread_fence(std::memory_order_acquire);
                try {
                    std::string jsonStr = moduleSnapshot->dump();
                    modulesJson[humanName] = ofJson::parse(jsonStr);
                } catch (...) {
                    // If parsing fails, skip this module (safer than crashing)
                    ofLogWarning("Engine") << "Failed to copy module snapshot for " << humanName;
                }
            }
        }
    });
    
    // Get connections (connectionManager_ is Engine member, no lock needed)
    std::vector<ConnectionInfo> connections;
    auto cmConnections = connectionManager_.getConnections();
    for (const auto& conn : cmConnections) {
        ConnectionInfo info;
        info.sourceModule = conn.sourceModule;
        info.targetModule = conn.targetModule;
        info.connectionType = (conn.type == ConnectionManager::ConnectionType::AUDIO) ? "AUDIO" :
                             (conn.type == ConnectionManager::ConnectionType::VIDEO) ? "VIDEO" :
                             (conn.type == ConnectionManager::ConnectionType::PARAMETER) ? "PARAMETER" : "EVENT";
        info.sourcePath = conn.sourcePath;
        info.targetPath = conn.targetPath;
        info.eventName = conn.eventName;
        info.active = conn.active;
        connections.push_back(info);
    }
    
    // Get script state (from ScriptManager)
    EngineState::ScriptState scriptState;
    scriptState.currentScript = scriptManager_.getCurrentScript();
    scriptState.autoUpdateEnabled = scriptManager_.isAutoUpdateEnabled();
    
    // Build JSON snapshot (similar to EngineState::toJson() but includes version)
    ofJson json;
    
    // Transport state
    ofJson transportJson;
    transportJson["isPlaying"] = transport.isPlaying;
    transportJson["bpm"] = transport.bpm;
    transportJson["currentBeat"] = transport.currentBeat;
    json["transport"] = transportJson;
    
    // Module snapshots (already JSON from Phase 7.1)
    json["modules"] = modulesJson;
    
    // Connections
    ofJson connectionsJson = ofJson::array();
    for (const auto& conn : connections) {
        connectionsJson.push_back(conn.toJson());
    }
    json["connections"] = connectionsJson;
    
    // Script state
    ofJson scriptJson;
    scriptJson["currentScript"] = scriptState.currentScript;
    scriptJson["autoUpdateEnabled"] = scriptState.autoUpdateEnabled;
    json["script"] = scriptJson;
    
    // Version (for conflict detection)
    json["version"] = version;
    
    // Ensure snapshot updates are atomic
    // Create immutable JSON snapshot and update pointer with mutex (C++17 compatible)
    // Add memory barrier to ensure snapshot update is visible to all threads
    {
        std::lock_guard<std::mutex> lock(snapshotJsonMutex_);
        snapshotJson_ = std::make_shared<const ofJson>(std::move(json));
        // Memory barrier ensures snapshot update is visible to all threads
        std::atomic_thread_fence(std::memory_order_release);
    }
}

void vt::Engine::buildTransportState(vt::EngineState& state) const {
    state.transport.isPlaying = clock_.isPlaying();
    // Use getTargetBPM() instead of getBPM() for state snapshots
    // getBPM() returns smoothed currentBpm (for audio/display), but for script generation
    // we want the target BPM (the value that was set, not the smoothed value)
    float bpm = clock_.getTargetBPM();
    state.transport.bpm = bpm;
    state.transport.currentBeat = 0;  // TODO: Get from Clock if available

}

bool vt::Engine::buildModuleStates(vt::EngineState& state) const {
    // CRITICAL: This function should NEVER be called during script execution
    // If it is, it means our guards failed - log extensively and abort immediately
    std::atomic_thread_fence(std::memory_order_acquire);
    bool isUnsafe = notifyingObservers_.load(std::memory_order_acquire);
    
    // CRITICAL: If we're here during script execution, something is very wrong
    if (isUnsafe) {
        ofLogError("Engine") << "CRITICAL: buildModuleStates() called during script execution! This should never happen! Aborting immediately.";
        // Log stack trace if possible
        return false;
    }
    
    // CRITICAL: Check unsafe periods at the point of module access
    // Even if buildStateSnapshot() checked, state can change between check and module access
    // Use memory barrier to ensure we see the latest unsafe state flags
    // This prevents race conditions where script execution starts but flag isn't visible yet
    std::atomic_thread_fence(std::memory_order_acquire);
    if (isInUnsafeState()) {
        ofLogError("Engine") << "buildModuleStates() called during unsafe period - ABORTING to prevent crash (notifyingObservers: " 
                             << notifyingObservers_.load(std::memory_order_acquire) << ")";
        return false;  // Aborted - return false to indicate failure
    }
    // Flag to track if we aborted due to unsafe state
    bool aborted = false;
    
    // Counters for diagnostic logging
    size_t modulesProcessed = 0;
    size_t modulesSkipped = 0;
    
    moduleRegistry_.forEachModule([&](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
        modulesProcessed++;
        // CRITICAL: Check for unsafe periods INSIDE the loop
        // State can change from safe to unsafe while iterating over modules
        // Use memory barrier to ensure we see the latest value
        std::atomic_thread_fence(std::memory_order_acquire);
        isUnsafe = notifyingObservers_.load(std::memory_order_acquire);
        // Phase 2 Simplification: Removed parametersBeingModified_ check
        if (isInUnsafeState()) {
            ofLogError("Engine") << "buildModuleStates() detected unsafe period while processing module " << name << " - ABORTING";
            aborted = true;
            return;  // Abort iteration - can't return bool from void callback
        }
        
        // MEMORY SAFETY (Phase 7.9 Plan 4): Validate module pointer before access
        // shared_ptr ensures module won't be deleted during iteration, but validate anyway
        if (!module) {
            modulesSkipped++;
            ofLogWarning("Engine") << "buildModuleStates() - null module pointer for " << name << " (skipping)";
            return;
        }
        
        // MEMORY SAFETY: Validate module is still valid (shared_ptr check)
        // This is defensive - shared_ptr should prevent deletion, but verify
        if (module.use_count() == 0) {
            ofLogError("Engine") << "MEMORY SAFETY VIOLATION: Module " << name << " has zero reference count";
            modulesSkipped++;
            return;
        }
        
        // CRITICAL: Check unsafe state again right before accessing module
        // Commands can start processing between the loop check and module access
        std::atomic_thread_fence(std::memory_order_acquire);
        if (isInUnsafeState()) {
            aborted = true;
            return;
        }
        
        EngineState::ModuleState moduleState;
        moduleState.name = name;
        std::string moduleType;
        try {
            // MEMORY SAFETY: Validate module before calling methods
            if (!module) {
                ofLogError("Engine") << "MEMORY SAFETY VIOLATION: Module " << name << " became null during processing";
                return;
            }
            moduleType = module->getTypeName();
            moduleState.type = moduleType;
        } catch (const std::exception& e) {
            ofLogError("Engine") << "Exception in getTypeName() for module " << name << ": " << e.what();
            return;
        } catch (...) {
            ofLogError("Engine") << "Unknown exception in getTypeName() for module " << name;
            return;
        }
        
        
        // CRITICAL: Check unsafe state again right before isEnabled() call
        std::atomic_thread_fence(std::memory_order_acquire);
        if (isInUnsafeState()) {
            aborted = true;
            return;
        }
        
        try {
            try {
                moduleState.enabled = module->isEnabled();
            } catch (const std::exception& e) {
                ofLogError("Engine") << "Exception in isEnabled() for module " << name << " (" << moduleType << "): " << e.what();
                moduleState.enabled = true;  // Default to enabled on error
            } catch (...) {
                ofLogError("Engine") << "Unknown exception in isEnabled() for module " << name << " (" << moduleType << ")";
                moduleState.enabled = true;  // Default to enabled on error
            }
        } catch (const std::exception& e) {
            ofLogError("Engine") << "Exception in isEnabled() for module " << name << ": " << e.what();
            return;
        } catch (...) {
            ofLogError("Engine") << "Unknown exception in isEnabled() for module " << name;
            return;
        }
        
        // Get state snapshot as JSON (clean separation: JSON for serialization)
        // Modules control their own serialization format, which is more robust
        
        // CRITICAL: Check unsafe state again right before getStateSnapshot() call
        std::atomic_thread_fence(std::memory_order_acquire);
        if (isInUnsafeState()) {
            aborted = true;
            return;
        }
        
        ofJson moduleSnapshot;
        try {
            
            moduleSnapshot = module->getStateSnapshot();
            
        } catch (const std::exception& e) {
            ofLogError("Engine") << "Exception in getStateSnapshot() for module " << name << " (" << moduleType << "): " << e.what();
            return;
        } catch (...) {
            ofLogError("Engine") << "Unknown exception in getStateSnapshot() for module " << name << " (" << moduleType << ")";
            return;
        }
        
        // Extract parameters from JSON snapshot
        // This approach is cleaner: modules serialize their own state, Engine extracts what it needs
        if (moduleSnapshot.is_object()) {
            // Extract top-level numeric fields as parameters
            try {
                for (auto it = moduleSnapshot.begin(); it != moduleSnapshot.end(); ++it) {
                    const std::string& key = it.key();
                    const auto& value = it.value();
                    
                    // Skip non-parameter fields (type, name, enabled, connections, samples, etc.)
                    if (key == "type" || key == "name" || key == "enabled" || 
                        key == "connections" || key == "samples" || key == "patterns" || 
                        key == "chain" || key == "audioDevice") {
                        continue;
                    }
                    
                    // Extract numeric values as parameters
                    if (value.is_number()) {
                        float paramValue = value.get<float>();
                        moduleState.parameters[key] = paramValue;
                        ofLogVerbose("Engine") << "[STATE_SYNC] Extracted " << name << "::" << key << " = " << paramValue << " from JSON";
                    }
                }
            } catch (const std::exception& e) {
                ofLogError("Engine") << "Exception in JSON iteration for module " << name << ": " << e.what();
            } catch (...) {
                ofLogError("Engine") << "Unknown exception in JSON iteration for module " << name;
            }
            
            // Extract connection-based parameters from connections array (VideoOutput, AudioOutput)
            if (moduleSnapshot.contains("connections") && moduleSnapshot["connections"].is_array()) {
                const auto& connections = moduleSnapshot["connections"];
                
                
                for (size_t i = 0; i < connections.size(); ++i) {
                    const auto& conn = connections[i];
                    if (conn.is_object()) {
                        // VideoOutput: extract opacity from each connection
                        if (conn.contains("opacity") && conn["opacity"].is_number()) {
                            std::string paramName = "connectionOpacity_" + std::to_string(i);
                            float opacity = conn["opacity"].get<float>();
                            moduleState.parameters[paramName] = opacity;
                            
                            
                            ofLogVerbose("Engine") << "[STATE_SYNC] Extracted " << name << "::" << paramName << " = " << opacity << " from connections array";
                        }
                        // AudioOutput: extract volume from each connection
                        if (conn.contains("volume") && conn["volume"].is_number()) {
                            std::string paramName = "connectionVolume_" + std::to_string(i);
                            float volume = conn["volume"].get<float>();
                            moduleState.parameters[paramName] = volume;
                            ofLogVerbose("Engine") << "[STATE_SYNC] Extracted " << name << "::" << paramName << " = " << volume << " from connections array";
                        }
                    }
                }
            }
        }
        
        // Fallback: For modules that don't serialize runtime parameters in JSON (e.g., MultiSampler),
        // use getParameters() to get current runtime values
        // This hybrid approach ensures we capture both serialized state and runtime parameters
        std::vector<ParameterDescriptor> params;
        try {
            params = module->getParameters();
        } catch (const std::exception& e) {
            ofLogError("Engine") << "Exception in getParameters() for module " << name << " (" << moduleType << "): " << e.what();
            params.clear();  // Use empty params on error
        } catch (...) {
            ofLogError("Engine") << "Unknown exception in getParameters() for module " << name << " (" << moduleType << ")";
            params.clear();  // Use empty params on error
        }
        for (const auto& param : params) {
            
            // Skip if already extracted from JSON
            if (moduleState.parameters.count(param.name) > 0) {
                continue;
            }
            
            // Skip connection-based parameters (already extracted from JSON connections array)
            if (param.name.find("connectionOpacity_") == 0 || param.name.find("connectionVolume_") == 0) {
                continue;
            }
            
            
            // CRITICAL: Check unsafe state again right before getParameter() call
            std::atomic_thread_fence(std::memory_order_acquire);
            if (isInUnsafeState()) {
                aborted = true;
                return;
            }
            
            try {
                float paramValue = module->getParameter(param.name);
                moduleState.parameters[param.name] = paramValue;
                ofLogVerbose("Engine") << "[STATE_SYNC] Fallback: captured " << name << "::" << param.name << " = " << paramValue << " from getParameter()";
            } catch (const std::exception& e) {
                ofLogWarning("Engine") << "Error getting parameter '" << param.name << "' from module '" << name << " (" << moduleType << ")': " << e.what();
            } catch (...) {
                ofLogWarning("Engine") << "Unknown error getting parameter '" << param.name << "' from module '" << name << " (" << moduleType << ")'";
            }
        }
        
        // Deep copy module snapshot JSON to prevent use-after-free
        // If module is deleted during snapshot iteration, shallow copy would become invalid
        // Deep copy via serialization/deserialization ensures snapshot is fully independent
        // This matches the pattern used in updateStateSnapshot() (Phase 7.2)
        ofJson deepCopiedSnapshot;
        try {
            if (moduleSnapshot.is_object() || moduleSnapshot.is_array()) {
                // Serialize and deserialize to create deep copy
                std::string jsonStr = moduleSnapshot.dump();
                deepCopiedSnapshot = ofJson::parse(jsonStr);
            } else {
                // For primitive types, assignment is already a copy
                deepCopiedSnapshot = moduleSnapshot;
            }
        } catch (const std::exception& e) {
            ofLogError("Engine") << "Exception creating deep copy of module snapshot for " << name << ": " << e.what();
            // Fallback to empty JSON if deep copy fails
            deepCopiedSnapshot = ofJson();
        } catch (...) {
            ofLogError("Engine") << "Unknown exception creating deep copy of module snapshot for " << name;
            deepCopiedSnapshot = ofJson();
        }
        
        // Store deep-copied snapshot (fully independent, no shared references)
        moduleState.typeSpecificData = deepCopiedSnapshot;
        
        try {
            state.modules[name] = moduleState;
        } catch (const std::exception& e) {
            ofLogError("Engine") << "Exception storing module state for " << name << ": " << e.what();
        } catch (...) {
            ofLogError("Engine") << "Unknown exception storing module state for " << name;
        }
    });
    
    // Log summary
    ofLogNotice("Engine") << "buildModuleStates() completed - processed: " << modulesProcessed 
                         << ", skipped: " << modulesSkipped 
                         << ", added to state: " << state.modules.size()
                         << ", aborted: " << (aborted ? "yes" : "no");
    
    // Return false if we aborted due to unsafe state, true if completed successfully
    return !aborted;
}

void vt::Engine::buildConnectionStates(vt::EngineState& state) const {
    // MEMORY SAFETY (Phase 7.9 Plan 4): Connection snapshots are already deep copies
    // ConnectionInfo contains only value types (strings, bool) - all copied by value
    // No shared references - snapshot is fully independent even if connection is deleted
    auto connections = connectionManager_.getConnections();
    for (const auto& conn : connections) {
        ConnectionInfo info;
        info.sourceModule = conn.sourceModule;      // String copy (deep)
        info.targetModule = conn.targetModule;     // String copy (deep)
        info.connectionType = (conn.type == ConnectionManager::ConnectionType::AUDIO) ? "AUDIO" :
                             (conn.type == ConnectionManager::ConnectionType::VIDEO) ? "VIDEO" :
                             (conn.type == ConnectionManager::ConnectionType::PARAMETER) ? "PARAMETER" : "EVENT";
        info.sourcePath = conn.sourcePath;         // String copy (deep)
        info.targetPath = conn.targetPath;         // String copy (deep)
        info.eventName = conn.eventName;           // String copy (deep)
        info.active = conn.active;                // Bool copy (value type)
        state.connections.push_back(info);
    }
}

void vt::Engine::audioOut(ofSoundBuffer& buffer) {
    // Track audio thread ID on first call (Phase 7.9 Plan 5)
    static std::once_flag audioThreadIdSet;
    std::call_once(audioThreadIdSet, [this]() {
        setAudioThreadId(std::this_thread::get_id());
    });
    
    ASSERT_AUDIO_THREAD();
    
    // CRITICAL: Process unified command queue (all commands: parameters, structural changes, etc.)
    // This handles all state mutations in a single, unified queue
    // Parameter changes (SetParameterCommand) and structural changes all go through here
    // Process commands even during script execution
    // Commands are safe to process - they're just state mutations in different threads
    // buildModuleStates() already has unsafe state checks to prevent crashes
    // Deferring command processing causes deadlock: script waits for commands that never get processed
    processCommands();
    
    // CRITICAL: Process Clock first to generate timing events
    clock_.audioOut(buffer);
    
    // Evaluate patterns (sample-accurate timing)
    patternRuntime_.evaluatePatterns(buffer);
    
    // Then process audio through master output
    if (masterAudioOut_) {
        masterAudioOut_->audioOut(buffer);
    }
}

void vt::Engine::update(float deltaTime) {
    ASSERT_MAIN_THREAD();
    
    // Increment frame counter
    size_t frameCount = updateFrameCount_.fetch_add(1) + 1;

    // CRITICAL: Process notification queue FIRST, before any state checks
    // This prevents deadlock where notifyingObservers_ is stuck at true
    // and update() returns early without processing notifications
    processNotificationQueue();

    // Periodic queue statistics logging (after first frame)
    if (frameCount > 1) {
        logQueueStatistics();
    }
    
    // Update session manager (handles auto-save)
    sessionManager_.update();
    
    // Update asset library
    assetLibrary_.update();
    
    // Update command executor
    commandExecutor_.update();
    
    // Process deferred script updates (with frame delay to ensure state is stable)
    // ScriptManager::update() handles the frame delay and safety checks
    // NOTE: Removed scriptManager_.update() - observer callback handles all updates immediately now
    
    // Don't update modules while commands are processing
    // Commands modify module state, and updating modules concurrently causes race conditions
    // This prevents crashes when modules access state that's being modified by commands
    bool commandsProcessing = notifyingObservers_.load(std::memory_order_acquire);
    if (commandsProcessing) {
        // Commands are still processing - skip module updates this frame
        // They'll be updated next frame after commands complete
        ofLogVerbose("Engine") << "Skipping module updates - commands still processing";
        return;
    }
    
    // Update all modules
    {
    }
    
    moduleRegistry_.forEachModule([&](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
        {
        }
        
        if (module) {
            try {
                module->update();
                
                {
                }
            } catch (const std::exception& e) {
                {
                }
                ofLogError("Engine") << "Error updating module '" << name << "': " << e.what();
            } catch (...) {
                {
                }
                ofLogError("Engine") << "Unknown exception updating module '" << name << "'";
            }
        }
    });
    
    {
    }
    
    {
    }
}

bool vt::Engine::loadSession(const std::string& path) {
    bool result = sessionManager_.loadSession(path);
    if (result) {
        // Use deferred notification pattern to prevent recursive notifications
        // Session loading can trigger state changes that might occur during notifications
        enqueueStateNotification();
    }
    return result;
}

bool vt::Engine::saveSession(const std::string& path) {
    return sessionManager_.saveSession(path);
}

std::string vt::Engine::serializeState() const {
    return getState().toJson();
}

bool vt::Engine::deserializeState(const std::string& data) {
    try {
        EngineState state = EngineState::fromJson(data);
        // TODO: Apply state to engine (Phase 2)
        return true;
    } catch (...) {
        return false;
    }
}

uint64_t vt::Engine::getCurrentTimestamp() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

bool vt::Engine::enqueueCommand(std::unique_ptr<Command> cmd) {
    if (!cmd) {
        ofLogWarning("Engine") << "Attempted to enqueue null command";
        return false;
    }
    
    // Set timestamp
    cmd->setTimestamp(getCurrentTimestamp());
    
    // Capture command description before moving
    std::string cmdDescription = cmd->describe();
    
    
    // Update monitoring: track command queue depth before enqueueing (Phase 7.9 Plan 8.2)
    size_t queueDepthBefore = commandQueue_.size_approx();
    queueMonitorStats_.commandQueueCurrent = queueDepthBefore;
    if (queueDepthBefore > queueMonitorStats_.commandQueueMax) {
        queueMonitorStats_.commandQueueMax = queueDepthBefore;
    }
    
    // Try to enqueue (lock-free)
    if (commandQueue_.try_enqueue(std::move(cmd))) {
        // Update monitoring: track enqueued commands (Phase 7.9 Plan 8.2)
        queueMonitorStats_.commandQueueTotalEnqueued++;
        
        // Check thresholds and log warnings (Phase 7.9 Plan 8.2)
        size_t queueDepthAfter = commandQueue_.size_approx();
        if (queueDepthAfter >= COMMAND_QUEUE_ERROR_THRESHOLD) {
            ofLogError("Engine") << "Command queue depth CRITICAL: " << queueDepthAfter 
                                 << " (threshold: " << COMMAND_QUEUE_ERROR_THRESHOLD << ")";
        } else if (queueDepthAfter >= COMMAND_QUEUE_WARNING_THRESHOLD) {
            ofLogWarning("Engine") << "Command queue depth high: " << queueDepthAfter 
                                   << " (threshold: " << COMMAND_QUEUE_WARNING_THRESHOLD << ")";
        }
        
        return true;
    } else {
        // Queue is full - command dropped
        commandStats_.queueOverflows++;
        commandStats_.commandsDropped++;
        
        // Log warning with command details (but don't spam)
        static int warningCount = 0;
        if (++warningCount % 100 == 0) {
            ofLogWarning("Engine") << "Command queue full (" 
                                   << commandStats_.queueOverflows 
                                   << " overflows, " 
                                   << commandStats_.commandsDropped 
                                   << " commands dropped) - "
                                   << "Queue capacity: 1024, current depth: " << queueDepthBefore
                                   << ". Commands are being dropped. Consider reducing script complexity or increasing queue capacity.";
        } else if (warningCount == 1) {
            // Log first overflow immediately with details
            ofLogError("Engine") << "Command queue overflow: Command dropped - " << cmdDescription
                                 << " (queue depth: " << queueDepthBefore 
                                 << ", capacity: 1024). Script execution may be incomplete.";
        }
        return false;
    }
}

int Engine::processCommands() {
    ASSERT_AUDIO_THREAD();

    // Phase 1 Fix: Restore COMMANDS_PROCESSING flag
    // This flag protects state snapshots from being built while commands modify state
    // Without this flag, isInUnsafeState() returns false during command processing,
    // allowing race conditions where state is read while being modified
    notifyingObservers_.store(true, std::memory_order_release);
    
    // Memory barrier ensures flag is visible before command processing begins
    std::atomic_thread_fence(std::memory_order_seq_cst);

    int processed = 0;
    std::unique_ptr<Command> cmd;

    // Update monitoring: track command queue depth before processing
    size_t queueDepthBefore = commandQueue_.size_approx();
    queueMonitorStats_.commandQueueCurrent = queueDepthBefore;

    // Process all queued commands (lock-free, called from audio thread)
    while (commandQueue_.try_dequeue(cmd)) {
        try {
            cmd->execute(*this);
            commandStats_.commandsProcessed++;
            queueMonitorStats_.commandQueueTotalProcessed++;
            processed++;
        } catch (const std::exception& e) {
            ofLogError("Engine") << "Command execution failed: " << e.what()
                                << " (" << cmd->describe() << ")";
            // Don't notify ScriptManager on error - state didn't change
        }
    }

    // Update monitoring: track command queue depth after processing
    size_t queueDepthAfter = commandQueue_.size_approx();
    queueMonitorStats_.commandQueueCurrent = queueDepthAfter;

    // Clear COMMANDS_PROCESSING flag before notification
    // Memory barrier ensures all command modifications are complete
    std::atomic_thread_fence(std::memory_order_release);
    notifyingObservers_.store(false, std::memory_order_release);

    if (processed > 0) {
        enqueueStateNotification();
    }

    return processed;
}

void Engine::executeCommandImmediate(std::unique_ptr<Command> cmd) {
    if (!cmd) {
        ofLogWarning("Engine") << "Attempted to execute null command";
        return;
    }
    
    try {
        cmd->execute(*this);
        
        // Don't call updateStateSnapshot() directly
        // executeCommandImmediate() can be called from any thread
        // updateStateSnapshot() must only be called from main thread (has ASSERT_MAIN_THREAD())
        // enqueueStateNotification() will call updateStateSnapshot() on main thread via notification queue
        // This prevents race conditions where both threads try to serialize modules simultaneously
        // This matches the pattern used by processCommands() (line 2436)
        enqueueStateNotification();  // Enqueues updateStateSnapshot() to main thread
        
        // NOTE: Removed onCommandExecuted() callback - state observer handles all sync now
    } catch (const std::exception& e) {
        ofLogError("Engine") << "Immediate command execution failed: " << e.what()
                            << " (" << cmd->describe() << ")";
    }
}

void Engine::logQueueStatistics() {
    // Periodic logging of queue statistics (Phase 7.9 Plan 8.2)
    uint64_t now = getCurrentTimestamp();
    
    // Initialize last log time on first call
    if (queueMonitorStats_.notificationQueueLastLogTime == 0) {
        queueMonitorStats_.notificationQueueLastLogTime = now;
        queueMonitorStats_.commandQueueLastLogTime = now;
        queueMonitorStats_.stateVersionLastLogTime = now;
        return;  // Skip first call, start accumulating
    }
    
    // Check if it's time to log (every MONITORING_LOG_INTERVAL_MS)
    if (now - queueMonitorStats_.notificationQueueLastLogTime < MONITORING_LOG_INTERVAL_MS) {
        return;  // Not time to log yet
    }
    
    // Update last log time
    queueMonitorStats_.notificationQueueLastLogTime = now;
    queueMonitorStats_.commandQueueLastLogTime = now;
    queueMonitorStats_.stateVersionLastLogTime = now;
    
    // Calculate rates (over last interval)
    uint64_t intervalMs = MONITORING_LOG_INTERVAL_MS;
    double notificationEnqueueRate = (queueMonitorStats_.notificationQueueTotalEnqueued * 1000.0) / intervalMs;
    double notificationProcessRate = (queueMonitorStats_.notificationQueueTotalProcessed * 1000.0) / intervalMs;
    double commandEnqueueRate = (queueMonitorStats_.commandQueueTotalEnqueued * 1000.0) / intervalMs;
    double commandProcessRate = (queueMonitorStats_.commandQueueTotalProcessed * 1000.0) / intervalMs;
    double stateVersionRate = (queueMonitorStats_.stateVersionIncrements * 1000.0) / intervalMs;
    
    // Check state version rate thresholds
    if (stateVersionRate >= STATE_VERSION_RATE_ERROR_THRESHOLD) {
        ofLogError("Engine") << "State version increment rate CRITICAL: " << stateVersionRate 
                             << " increments/sec (threshold: " << STATE_VERSION_RATE_ERROR_THRESHOLD << ")";
    } else if (stateVersionRate >= STATE_VERSION_RATE_WARNING_THRESHOLD) {
        ofLogWarning("Engine") << "State version increment rate high: " << stateVersionRate 
                               << " increments/sec (threshold: " << STATE_VERSION_RATE_WARNING_THRESHOLD << ")";
    }
    
    // Log summary statistics
    ofLogNotice("Engine") << "Queue Statistics (last " << (intervalMs / 1000.0) << "s):"
                          << " Notification queue: current=" << queueMonitorStats_.notificationQueueCurrent
                          << ", max=" << queueMonitorStats_.notificationQueueMax
                          << ", enqueue=" << notificationEnqueueRate << "/s"
                          << ", process=" << notificationProcessRate << "/s"
                          << " | Command queue: current=" << queueMonitorStats_.commandQueueCurrent
                          << ", max=" << queueMonitorStats_.commandQueueMax
                          << ", enqueue=" << commandEnqueueRate << "/s"
                          << ", process=" << commandProcessRate << "/s"
                          << " | State version: current=" << stateVersion_.load()
                          << ", rate=" << stateVersionRate << "/s"
                          << ", max_gap=" << queueMonitorStats_.stateVersionMaxGap
                          << ", timeouts=" << queueMonitorStats_.stateVersionSyncTimeouts;
    
    // Reset counters for next interval (keep max values)
    queueMonitorStats_.notificationQueueTotalEnqueued = 0;
    queueMonitorStats_.notificationQueueTotalProcessed = 0;
    queueMonitorStats_.commandQueueTotalEnqueued = 0;
    queueMonitorStats_.commandQueueTotalProcessed = 0;
    queueMonitorStats_.stateVersionIncrements = 0;
}

} // namespace vt

