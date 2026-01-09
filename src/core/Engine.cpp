#include "Engine.h"
#include "core/lua/LuaGlobals.h"
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

// Helper methods for unsafe state management
void Engine::setUnsafeState(UnsafeState state, bool active) {
    uint8_t flag = static_cast<uint8_t>(state);
    if (active) {
        unsafeStateFlags_.fetch_or(flag);
    } else {
        unsafeStateFlags_.fetch_and(~flag);
    }
}

bool Engine::hasUnsafeState(UnsafeState state) const {
    uint8_t flag = static_cast<uint8_t>(state);
    return (unsafeStateFlags_.load() & flag) != 0;
}

Engine::Engine() 
    : assetLibrary_(&projectManager_, &mediaConverter_, &moduleRegistry_)
    , scriptManager_(this) {
    // Initialize snapshot system
    snapshotJson_ = nullptr;  // Will be created on first updateStateSnapshot() call
    stateVersion_ = 0;
}

Engine::~Engine() {
    // Stop background script execution thread
    scriptExecutionThreadRunning_.store(false);
    if (scriptExecutionThread_.joinable()) {
        // Wake up thread with empty request to exit wait loop
        ScriptExecutionRequest emptyReq;
        scriptExecutionQueue_.enqueue(emptyReq);
        scriptExecutionThread_.join();
    }
    
    // Cleanup handled by member destructors
}

void Engine::setup(const EngineConfig& config) {
    
    if (isSetup_) {
        ofLogWarning("Engine") << "Engine already setup, skipping";
        return;
    }
    
    config_ = config;
    
    // Initialize cached state early to ensure it's always available
    // This prevents getState() from returning empty state during unsafe periods
    {
        std::unique_lock<std::shared_mutex> cacheLock(cachedStateMutex_);
        if (!cachedState_) {
            cachedState_ = std::make_unique<EngineState>();
        }
    }
    
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
    
    // Start background script execution thread (after Lua is initialized)
    scriptExecutionThreadRunning_.store(true);
    scriptExecutionThread_ = std::thread(&Engine::scriptExecutionThreadFunction, this);
    
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
        {
            std::unique_lock<std::shared_mutex> cacheLock(cachedStateMutex_);
            *cachedState_ = initialState;
        }
        ofLogNotice("Engine") << "Initial state snapshot built - modules: " << initialState.modules.size() 
                             << ", connections: " << initialState.connections.size();
        // Log module names for debugging
        if (initialState.modules.empty()) {
            ofLogError("Engine") << "ERROR: Initial state snapshot has NO modules!";
            ofLogError("Engine") << "ModuleRegistry has " << registryCountBefore << " modules, but snapshot has 0!";
            ofLogError("Engine") << "This will cause script generation to fail - modules won't appear in script!";
            // This is a critical error - the script will be empty
        } else {
            ofLogNotice("Engine") << "Modules in snapshot:";
            for (const auto& [name, moduleState] : initialState.modules) {
                ofLogNotice("Engine") << "  - " << name << " (" << moduleState.type << ")";
            }
        }
    } catch (const std::exception& e) {
        ofLogError("Engine") << "Failed to build initial state snapshot: " << e.what();
        ofLogError("Engine") << "Cached state will remain empty - script generation will fail!";
        // Cached state already initialized with empty state, which is NOT acceptable
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
    sessionManager_ = std::move(SessionManager(
        &projectManager_,
        &clock_,
        &moduleRegistry_,
        &moduleFactory_,
        &parameterRouter_,
        &connectionManager_
    ));
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
        // CRITICAL FIX: Use deferred notification pattern to prevent recursive notifications
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
        // CRITICAL FIX: Use deferred notification pattern to prevent recursive notifications
        enqueueStateNotification();
    });
    
    // Set parameter change notification callback for script sync
    // This will automatically update all existing modules' callbacks (including master outputs)
    moduleRegistry_.setParameterChangeNotificationCallback([this]() {
        
        // Increment counter to track parameter modification in progress
        parametersBeingModified_.fetch_add(1);
        
        // Defer notification - will be processed after parameter modification completes
        // This prevents building snapshots while parameters are being modified
        enqueueStateNotification();
        
        // Decrement counter
        parametersBeingModified_.fetch_sub(1);
        
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
        
        // Register helper functions in Lua that use exec()
        // These are simple, declarative functions for live-coding
        std::string registerHelpers = R"(
-- Simple command execution helper
local function execCommand(cmd)
    local result = exec(cmd)
    if result and result.success then
        return true
    else
        local errorMsg = result and result.error or "Unknown error"
        error("Command failed: " .. cmd .. " - " .. errorMsg)
    end
end

-- Create sampler module with optional config table
function sampler(name, config)
    if not name or name == "" then
        error("sampler() requires a name")
    end
    execCommand("add MultiSampler " .. name)
    
    -- Apply configuration if provided
    if config then
        for k, v in pairs(config) do
            execCommand("set " .. name .. " " .. k .. " " .. tostring(v))
        end
    end
    
    return name
end

-- Create sequencer module with optional config table
function sequencer(name, config)
    if not name or name == "" then
        error("sequencer() requires a name")
    end
    execCommand("add TrackerSequencer " .. name)
    
    -- Apply configuration if provided
    if config then
        for k, v in pairs(config) do
            execCommand("set " .. name .. " " .. k .. " " .. tostring(v))
        end
    end
    
    return name
end

-- Connect modules
function connect(source, target, connType)
    connType = connType or "audio"
    local cmd = "route " .. source .. " " .. target
    if connType == "event" then
        cmd = cmd .. " event"
    end
    return execCommand(cmd)
end

-- Set parameter
function setParam(moduleName, paramName, value)
    local cmd = "set " .. moduleName .. " " .. paramName .. " " .. tostring(value)
    return execCommand(cmd)
end

-- Get parameter (placeholder - will be improved)
function getParam(moduleName, paramName)
    -- TODO: Implement via command or SWIG
    return 0
end

-- Create pattern
function pattern(name, steps)
    steps = steps or 16
    local cmd = "pattern create " .. name .. " " .. tostring(steps)
    return execCommand(cmd)
end

-- System module helpers (for cleaner syntax)
-- IDEMPOTENT: System modules already exist, we just configure them
-- These functions match the SWIG-wrapped functions in videoTracker.i
function audioOut(name, config)
    config = config or {}
    -- System modules are created via ModuleFactory::ensureSystemModules()
    -- We just need to configure parameters (idempotent for live-coding)
    for k, v in pairs(config) do
        execCommand("set " .. name .. " " .. k .. " " .. tostring(v))
    end
    return name
end

function videoOut(name, config)
    config = config or {}
    -- System modules are created via ModuleFactory::ensureSystemModules()
    -- We just need to configure parameters (idempotent for live-coding)
    for k, v in pairs(config) do
        execCommand("set " .. name .. " " .. k .. " " .. tostring(v))
    end
    return name
end

function oscilloscope(name, config)
    config = config or {}
    -- System modules are created via ModuleFactory::ensureSystemModules()
    -- We just need to configure parameters (idempotent for live-coding)
    for k, v in pairs(config) do
        execCommand("set " .. name .. " " .. k .. " " .. tostring(v))
    end
    return name
end

function spectrogram(name, config)
    config = config or {}
    -- System modules are created via ModuleFactory::ensureSystemModules()
    -- We just need to configure parameters (idempotent for live-coding)
    for k, v in pairs(config) do
        execCommand("set " .. name .. " " .. k .. " " .. tostring(v))
    end
    return name
end

-- Engine wrapper for clock control
-- This provides a simple interface for clock operations
local engine = {
    getClock = function()
        return {
            setBPM = function(bpm)
                return execCommand("bpm " .. tostring(bpm))
            end,
            getBPM = function()
                -- TODO: Implement via command or SWIG
                return 120
            end,
            start = function()
                return execCommand("start")
            end,
            stop = function()
                return execCommand("stop")
            end,
            pause = function()
                return execCommand("stop")  -- pause uses stop for now
            end,
            play = function()
                return execCommand("start")
            end,
            isPlaying = function()
                -- TODO: Implement via command or SWIG
                return false
            end
        }
    end,
    executeCommand = function(cmd)
        local result = exec(cmd)
        return result
    end
}

-- Make engine global
_G.engine = engine
)";
        lua_->doString(registerHelpers);
        
        ofLogNotice("Engine") << "Lua initialized successfully - exec() and helper functions registered";
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
    
    notifyStateChange();
}

void Engine::setupAudio(int sampleRate, int bufferSize) {
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
        
        // CRITICAL FIX: Update state snapshot immediately after command execution
        // This ensures state is synchronized before notifications are sent
        // This matches the pattern used by executeCommandImmediate()
        updateStateSnapshot();  // Create new immutable JSON snapshot
        
        // CRITICAL FIX: Use deferred notification pattern instead of calling notifyStateChange() directly
        // This prevents recursive notifications when commands are executed during state notifications.
        // Notifications are enqueued and processed on main thread, ensuring thread safety and preventing infinite recursion.
        enqueueStateNotification();
        
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
    
    // CRITICAL FIX: Lock script execution mutex FIRST to completely block state snapshot building
    // This prevents ANY thread from building state snapshots during script execution
    // Even if they somehow bypass the flag check, the mutex will block them
    std::lock_guard<std::mutex> scriptLock(scriptExecutionMutex_);
    
    
    // CRITICAL FIX: Set script execution flag BEFORE any script execution begins
    // This must be set as early as possible to prevent any code path from calling
    // getState() or buildStateSnapshot() during script execution
    // This prevents crashes when script execution triggers state changes or callbacks
    setUnsafeState(UnsafeState::SCRIPT_EXECUTING, true);
    
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
            setUnsafeState(UnsafeState::SCRIPT_EXECUTING, false);
            return Result(false, "Lua execution failed", "Memory allocation error: " + std::string(e.what()));
        } catch (const std::exception& e) {
            // Other C++ exceptions from SWIG wrappers
            setUnsafeState(UnsafeState::SCRIPT_EXECUTING, false);
            return Result(false, "Lua execution failed", "C++ exception: " + std::string(e.what()));
        } catch (...) {
            // Unknown exceptions
            setUnsafeState(UnsafeState::SCRIPT_EXECUTING, false);
            return Result(false, "Lua execution failed", "Unknown C++ exception during script execution");
        }
        
        
        // Clear script execution flag before returning
        setUnsafeState(UnsafeState::SCRIPT_EXECUTING, false);
        
        
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
            
            return Result(false, "Lua execution failed", errorMsg);
        }
    } catch (const std::exception& e) {
        // Clear flag on exception
        setUnsafeState(UnsafeState::SCRIPT_EXECUTING, false);
        return Result(false, "Lua execution failed", e.what());
    } catch (...) {
        // Clear flag on unknown exception
        setUnsafeState(UnsafeState::SCRIPT_EXECUTING, false);
        return Result(false, "Lua execution failed", "Unknown error");
    }
}

void Engine::scriptExecutionThreadFunction() {
    // Initialize separate Lua state for background thread
    asyncLua_ = std::make_unique<ofxLua>();
    bool success = asyncLua_->init(false, true, false);
    
    if (!success) {
        ofLogError("Engine") << "Failed to initialize async Lua state";
        asyncLua_.reset();
        return;
    }
    
    // Set global engine pointer for helper functions (same as main Lua state)
    vt::lua::setGlobalEngine(this);
    
    // Register exec() function and helper functions (same as main Lua state)
    if (asyncLua_ && asyncLua_->isValid()) {
        lua_State* L = *asyncLua_;
        lua_register(L, "exec", lua_execCommand);
        
        // Register helper functions (same as main Lua state setup)
        // Use the same full helper registration for consistency
        std::string registerHelpers = R"(
-- Simple command execution helper
local function execCommand(cmd)
    local result = exec(cmd)
    if result and result.success then
        return true
    else
        local errorMsg = result and result.error or "Unknown error"
        error("Command failed: " .. cmd .. " - " .. errorMsg)
    end
end

-- Create sampler module with optional config table
function sampler(name, config)
    if not name or name == "" then
        error("sampler() requires a name")
    end
    execCommand("add MultiSampler " .. name)
    
    -- Apply configuration if provided
    if config then
        for k, v in pairs(config) do
            execCommand("set " .. name .. " " .. k .. " " .. tostring(v))
        end
    end
    
    return name
end

-- Create sequencer module with optional config table
function sequencer(name, config)
    if not name or name == "" then
        error("sequencer() requires a name")
    end
    execCommand("add TrackerSequencer " .. name)
    
    -- Apply configuration if provided
    if config then
        for k, v in pairs(config) do
            execCommand("set " .. name .. " " .. k .. " " .. tostring(v))
        end
    end
    
    return name
end

-- Connect modules
function connect(source, target, connType)
    connType = connType or "audio"
    local cmd = "route " .. source .. " " .. target
    if connType == "event" then
        cmd = cmd .. " event"
    end
    return execCommand(cmd)
end

-- Set parameter
function setParam(moduleName, paramName, value)
    local cmd = "set " .. moduleName .. " " .. paramName .. " " .. tostring(value)
    return execCommand(cmd)
end

-- Get parameter (placeholder - will be improved)
function getParam(moduleName, paramName)
    -- TODO: Implement via command or SWIG
    return 0
end

-- Create pattern
function pattern(name, steps)
    steps = steps or 16
    local cmd = "pattern create " .. name .. " " .. tostring(steps)
    return execCommand(cmd)
end

-- System module helpers (for cleaner syntax)
-- IDEMPOTENT: System modules already exist, we just configure them
-- These functions match the SWIG-wrapped functions in videoTracker.i
function audioOut(name, config)
    config = config or {}
    -- System modules are created via ModuleFactory::ensureSystemModules()
    -- We just need to configure parameters (idempotent for live-coding)
    for k, v in pairs(config) do
        execCommand("set " .. name .. " " .. k .. " " .. tostring(v))
    end
    return name
end

function videoOut(name, config)
    config = config or {}
    -- System modules are created via ModuleFactory::ensureSystemModules()
    -- We just need to configure parameters (idempotent for live-coding)
    for k, v in pairs(config) do
        execCommand("set " .. name .. " " .. k .. " " .. tostring(v))
    end
    return name
end

function oscilloscope(name, config)
    config = config or {}
    -- System modules are created via ModuleFactory::ensureSystemModules()
    -- We just need to configure parameters (idempotent for live-coding)
    for k, v in pairs(config) do
        execCommand("set " .. name .. " " .. k .. " " .. tostring(v))
    end
    return name
end

function spectrogram(name, config)
    config = config or {}
    -- System modules are created via ModuleFactory::ensureSystemModules()
    -- We just need to configure parameters (idempotent for live-coding)
    for k, v in pairs(config) do
        execCommand("set " .. name .. " " .. k .. " " .. tostring(v))
    end
    return name
end

-- Engine wrapper for clock control
-- This provides a simple interface for clock operations
local engine = {
    getClock = function()
        return {
            setBPM = function(bpm)
                return execCommand("bpm " .. tostring(bpm))
            end,
            getBPM = function()
                -- TODO: Implement via command or SWIG
                return 120
            end,
            start = function()
                return execCommand("start")
            end,
            stop = function()
                return execCommand("stop")
            end,
            pause = function()
                return execCommand("stop")  -- pause uses stop for now
            end,
            play = function()
                return execCommand("start")
            end,
            isPlaying = function()
                -- TODO: Implement via command or SWIG
                return false
            end
        }
    end,
    executeCommand = function(cmd)
        local result = exec(cmd)
        return result
    end
}

-- Make engine global
_G.engine = engine
)";
        asyncLua_->doString(registerHelpers);
        ofLogNotice("Engine") << "Async Lua state initialized successfully";
    }
    
    // Process script execution requests
    ScriptExecutionRequest req;
    while (scriptExecutionThreadRunning_.load()) {
        // Blocking wait with timeout (wakes up every 100ms to check running_)
        if (scriptExecutionQueue_.wait_dequeue_timed(req, std::chrono::milliseconds(100))) {
            // Execute script in background thread
            Result result = executeScriptInBackground(req.script, req.timeoutMs);
            
            // Post callback to main thread (via message queue)
            postScriptResultToMainThread(req.id, result, req.callback);
        }
    }
    
    // Cleanup async Lua state
    asyncLua_.reset();
    ofLogNotice("Engine") << "Script execution thread stopped";
}

Engine::Result Engine::executeScriptInBackground(const std::string& script, int timeoutMs) {
    if (!asyncLua_ || !asyncLua_->isValid()) {
        return Result(false, "Async Lua not initialized", "Failed to initialize async Lua state");
    }
    
    // Set execution flag (coordinate with main thread)
    setUnsafeState(UnsafeState::SCRIPT_EXECUTING, true);
    
    auto startTime = std::chrono::steady_clock::now();
    
    try {
        // Set error callback
        std::string luaError;
        asyncLua_->setErrorCallback([&luaError](std::string& msg) {
            luaError = msg;
        });
        
        // Execute script
        bool success = asyncLua_->doString(script);
        
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        
        // Check timeout (if specified)
        if (timeoutMs > 0 && std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeoutMs) {
            setUnsafeState(UnsafeState::SCRIPT_EXECUTING, false);
            return Result(false, "Script execution timed out", 
                         "Execution exceeded timeout of " + std::to_string(timeoutMs) + "ms");
        }
        
        // Clear execution flag
        setUnsafeState(UnsafeState::SCRIPT_EXECUTING, false);
        
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        
        if (success) {
            // CRITICAL: Wait for command queue to be processed before returning
            // This ensures state is updated before callback fires
            // Commands from async execution are enqueued during script execution
            // Audio thread processes commands asynchronously at ~86Hz (44.1kHz / 512 samples)
            ofLogVerbose("Engine") << "Script execution completed successfully (elapsed: " << elapsedMs << "ms), waiting for command processing...";
            
            auto startWait = std::chrono::steady_clock::now();
            const int MAX_WAIT_MS = 1000;  // 1 second max wait
            
            // Wait for commands to be processed by audio thread
            // Since ReaderWriterQueue doesn't have empty() method, we use a timeout-based approach
            // Audio thread runs at ~86Hz, so 100ms gives ~8-9 cycles to process commands
            int waitIterations = 0;
            while (std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - startWait).count() < MAX_WAIT_MS) {
                // Check if commands are being processed (audio thread is active)
                // If commands are not being processed and we've waited a bit, commands are likely processed
                if (!hasUnsafeState(UnsafeState::COMMANDS_PROCESSING) && waitIterations > 5) {
                    // Commands not being processed and we've waited a bit - likely all processed
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                waitIterations++;
            }
            
            auto waitTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startWait).count();
            
            // Give audio thread one more cycle to update state snapshots
            // Audio thread runs at ~86Hz, so 50ms is ~4-5 cycles
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            if (waitTime < MAX_WAIT_MS) {
                ofLogVerbose("Engine") << "Command processing wait completed after " << waitTime << "ms (" << waitIterations << " iterations)";
            } else {
                ofLogWarning("Engine") << "Command processing wait timed out after " << waitTime << "ms - some commands may not be processed";
            }
            
            return Result(true, "Script executed successfully");
        } else {
            std::string errorMsg = luaError.empty() ? asyncLua_->getErrorMessage() : luaError;
            ofLogVerbose("Engine") << "Script execution failed (elapsed: " << elapsedMs << "ms): " << errorMsg;
            return Result(false, "Lua execution failed", errorMsg);
        }
    } catch (const std::exception& e) {
        setUnsafeState(UnsafeState::SCRIPT_EXECUTING, false);
        return Result(false, "Script execution failed", e.what());
    } catch (...) {
        setUnsafeState(UnsafeState::SCRIPT_EXECUTING, false);
        return Result(false, "Script execution failed", "Unknown error");
    }
}

void Engine::postScriptResultToMainThread(uint64_t id, Result result, std::function<void(Result)> callback) {
    // Store callback for execution on main thread
    // This will be processed in Engine::update() method
    pendingScriptCallbacks_.enqueue({id, result, callback});
}

void Engine::syncScriptToEngine(const std::string& script, std::function<void(bool success)> callback) {
    // Execute script with sync contract (Script → Engine synchronization)
    // Guarantees script changes are reflected in engine state before callback fires
    
    
    // Get current state version before execution
    uint64_t currentVersion = stateVersion_.load();
    uint64_t targetVersion = currentVersion + 1;  // Expect version to increment after script execution
    
    // Execute script asynchronously
    uint64_t executionId = evalAsync(script, [this, callback, currentVersion, targetVersion](Result result) {
        // Script execution completed - now wait for state to be updated
        
        if (!result.success) {
            // Script execution failed - call callback with failure
            if (callback) {
                callback(false);
            }
            return;
        }
        
        // Script executed successfully - wait for command queue to be processed
        // Commands are already processed by evalAsync callback (from Phase 7.7)
        // Now wait for state version to be updated
        
        // Wait for state version to reach target version
        // This ensures state snapshot is updated with script changes
        waitForStateVersion(targetVersion, 1000);  // 1 second timeout
        
        // Verify state version was updated
        uint64_t finalVersion = stateVersion_.load();
        bool syncComplete = (finalVersion >= targetVersion);
        
        
        if (syncComplete) {
            ofLogVerbose("Engine") << "Script → Engine sync complete (version: " << currentVersion << " → " << finalVersion << ")";
        } else {
            ofLogWarning("Engine") << "Script → Engine sync incomplete (version: " << currentVersion << " → " << finalVersion << ", expected: " << targetVersion << ")";
        }
        
        // Call callback with sync status
        if (callback) {
            callback(syncComplete);
        }
    }, 0);  // No timeout for sync contract
    
    if (executionId == 0) {
        // Fallback to synchronous execution (queue full or thread not running)
        Result result = eval(script);
        
        // Wait for state version to be updated
        waitForStateVersion(targetVersion, 1000);
        
        // Verify state version was updated
        uint64_t finalVersion = stateVersion_.load();
        bool syncComplete = (finalVersion >= targetVersion);
        
        if (callback) {
            callback(syncComplete && result.success);
        }
    }
}

uint64_t Engine::evalAsync(const std::string& script, std::function<void(Result)> callback, int timeoutMs) {
    if (!scriptExecutionThreadRunning_.load()) {
        // Background thread not running - fallback to synchronous execution
        if (callback) {
            Result result = eval(script);  // Synchronous fallback
            callback(result);
        }
        return 0;
    }
    
    // Create execution request
    ScriptExecutionRequest req;
    req.script = script;
    req.callback = callback;
    req.id = nextScriptExecutionId_.fetch_add(1);
    req.timestamp = std::chrono::steady_clock::now();
    req.timeoutMs = timeoutMs;
    
    // Queue request (non-blocking)
    if (scriptExecutionQueue_.try_enqueue(req)) {
        return req.id;
    } else {
        // Queue full - fallback to synchronous execution
        ofLogWarning("Engine") << "Script execution queue full, falling back to synchronous execution";
        if (callback) {
            Result result = eval(script);  // Synchronous fallback
            callback(result);
        }
        return 0;
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
    
    // CRITICAL FIX: Check unsafe state flags FIRST
    // If script is executing, return cached state immediately
    // This prevents state snapshot building during script execution
    if (hasUnsafeState(UnsafeState::SCRIPT_EXECUTING)) {
        // Script is executing - return cached state immediately
        std::shared_lock<std::shared_mutex> lock(cachedStateMutex_);
        if (cachedState_) {
            ofLogVerbose("Engine") << "getState() BLOCKED by script execution - returning cached state";
            return *cachedState_;
        }
        ofLogError("Engine") << "getState() blocked but no cached state available";
        return EngineState();
    }
    
    // CRITICAL FIX: If unsafe period, return last known good cached state (only updated during safe periods)
    // Never build snapshots during unsafe periods - this prevents race conditions
    // Cached state is always initialized during setup, so it should always be available
    // CRITICAL: Use isInUnsafeState() to match buildStateSnapshot() behavior
    // This ensures consistent checking across all code paths
    if (isInUnsafeState()) {
        std::shared_lock<std::shared_mutex> lock(cachedStateMutex_);
        if (cachedState_) {
            // CRITICAL FIX: Update version of cached state before returning
            // Cached state may have been built earlier with version 0, but we need current version
            // This ensures observers receive state with correct version for consistency tracking
            EngineState result = *cachedState_;
            result.version = stateVersion_.load();
            
            return result;
        }
        // CRITICAL FIX: Cached state should always be initialized during setup
        // If it's not available, initialize it now (shouldn't happen, but safety fallback)
        ofLogError("Engine") << "getState() called during unsafe period but cached state not initialized - initializing now";
        std::unique_lock<std::shared_mutex> cacheLock(cachedStateMutex_);
        if (!cachedState_) {
            cachedState_ = std::make_unique<EngineState>();
        }
        EngineState result = *cachedState_;
        result.version = stateVersion_.load();
        return result;
    }
    
    // Safe period: build snapshot and cache it (only update cache during safe periods)
    try {
        std::shared_lock<std::shared_mutex> lock(stateMutex_);
        EngineState state = buildStateSnapshot();
        
        // Cache state for use during unsafe periods (only updated during safe periods)
        {
            std::unique_lock<std::shared_mutex> cacheLock(cachedStateMutex_);
            if (!cachedState_) {
                cachedState_ = std::make_unique<EngineState>();
            }
            *cachedState_ = state;
        }
        
        return state;
    } catch (const std::exception& e) {
        ofLogError("Engine") << "Exception in getState(): " << e.what();
        throw;
    } catch (...) {
        ofLogError("Engine") << "Unknown exception in getState()";
        throw;
    }
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
// SHELL-SAFE API (for ScriptManager operations)
// ═══════════════════════════════════════════════════════════

void Engine::setScriptUpdateCallback(std::function<void(const std::string&)> callback) {
    scriptManager_.setScriptUpdateCallback(callback);
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

void Engine::notifyStateChange() {
    
    // CRITICAL: Prevent recursive notifications
    // If an observer calls notifyStateChange() during notification, ignore it
    // This prevents infinite loops and ensures observers only read state, never modify
    bool expected = false;
    if (!notifyingObservers_.compare_exchange_strong(expected, true)) {
        // Already notifying - recursive call detected, ignore it
        ofLogWarning("Engine") << "Recursive notifyStateChange() call detected and ignored";
        return;
    }
    
    // CRITICAL FIX: Defer notifications during rendering
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
    
    // CRITICAL FIX: Don't skip notifications during command processing
    // getState() now handles unsafe periods by returning cached state
    // This ensures observers always receive state updates, even during command processing
    // Observers will see the last known good state, which is acceptable
    // Notification queue ensures notifications happen on main thread after commands complete
    
    // CRITICAL FIX: Throttle expensive state snapshot building
    // Prevents main thread blocking from excessive snapshot generation
    // which was causing engine to appear frozen
    // BUT: Always notify observers - use cached state when throttled
    uint64_t now = getCurrentTimestamp();
    uint64_t lastTime = lastStateSnapshotTime_.load();
    bool shouldBuildSnapshot = (now - lastTime >= STATE_SNAPSHOT_THROTTLE_MS);
    
    EngineState state;
    if (shouldBuildSnapshot) {
        // Enough time has passed - build fresh snapshot
        lastStateSnapshotTime_.store(now);
        
        
        // CRITICAL FIX: Use getState() instead of buildStateSnapshot() directly
        // getState() handles unsafe periods by returning cached state
        // This prevents crashes when notifyStateChange() is called during script execution
        state = getState();
    } else {
        // Throttled - use cached state to avoid expensive snapshot building
        // This ensures observers still get updates, but we don't block the main thread
        std::shared_lock<std::shared_mutex> lock(cachedStateMutex_);
        if (cachedState_) {
            // CRITICAL FIX: Update version of cached state before using
            // Cached state may have been built earlier with version 0, but we need current version
            // This ensures observers receive state with correct version for consistency tracking
            state = *cachedState_;
            state.version = stateVersion_.load();
            
        } else {
            // No cached state available - build snapshot anyway (shouldn't happen, but safety fallback)
            ofLogWarning("Engine") << "notifyStateChange() throttled but no cached state available - building snapshot anyway";
            state = getState();
        }
    }
    
    
    
    // CRITICAL FIX: Collect broken observers during iteration, remove after
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
    
    // CRITICAL: Prevent recursive notifications
    bool expected = false;
    if (!notifyingObservers_.compare_exchange_strong(expected, true)) {
        // Already notifying - recursive call detected, ignore it
        ofLogWarning("Engine") << "Recursive notifyObserversWithState() call detected and ignored";
        return;
    }
    
    // Get current state (with throttling and caching)
    uint64_t now = getCurrentTimestamp();
    uint64_t lastTime = lastStateSnapshotTime_.load();
    bool shouldBuildSnapshot = (now - lastTime >= STATE_SNAPSHOT_THROTTLE_MS);
    
    EngineState state;
    if (shouldBuildSnapshot) {
        lastStateSnapshotTime_.store(now);
        state = getState();
    } else {
        // Throttled - use cached state
        std::shared_lock<std::shared_mutex> lock(cachedStateMutex_);
        if (cachedState_) {
            state = *cachedState_;
        } else {
            // No cached state available - build snapshot anyway
            ofLogWarning("Engine") << "notifyObserversWithState() throttled but no cached state available - building snapshot anyway";
            state = getState();
        }
    }
    
    // Call all observers
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
    
    // Remove broken observers
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
    // Enqueue state notification to be processed on main thread
    // This replaces the stateNeedsNotification_ flag pattern - queue is single source of truth
    notificationQueue_.enqueue([this]() {
        // Update snapshot before notifying (ensures state is current)
        updateStateSnapshot();
        // Notify observers with current state
        notifyObserversWithState();
    });
}

void Engine::processNotificationQueue() {
    
    // Process queued notifications from notificationQueue_
    // Called from update() (main thread event loop) to ensure notifications happen on event loop, not during rendering
    // Use non-blocking dequeue with per-frame limit to avoid blocking the event loop
    // Limit prevents processing too many notifications in one frame, which could delay window rendering
    
    // Progressive limit: start conservative, increase over time
    size_t frameCount = updateFrameCount_.load();
    size_t MAX_NOTIFICATIONS_PER_FRAME;
    if (frameCount < 20) {
        MAX_NOTIFICATIONS_PER_FRAME = 1;  // Very conservative for first 20 frames
    } else if (frameCount < 50) {
        MAX_NOTIFICATIONS_PER_FRAME = 3;  // Moderate for next 30 frames
    } else {
        MAX_NOTIFICATIONS_PER_FRAME = 10; // Normal limit after 50 frames
    }
    
    
    std::function<void()> callback;
    size_t processed = 0;
    while (processed < MAX_NOTIFICATIONS_PER_FRAME && notificationQueue_.try_dequeue(callback)) {
        if (callback) {
            try {
                callback();  // Execute callback (calls notifyObserversWithState() or syncEngineToEditor callback)
                processed++;
            } catch (const std::exception& e) {
                ofLogError("Engine") << "Error processing notification callback: " << e.what();
            } catch (...) {
                ofLogError("Engine") << "Unknown error processing notification callback";
            }
        }
    }
    
    
    // Log if we hit the limit (indicates many notifications queued)
    if (processed >= MAX_NOTIFICATIONS_PER_FRAME) {
        size_t remaining = notificationQueue_.size_approx();
        if (remaining > 0) {
            ofLogVerbose("Engine") << "Notification queue processing limit reached (" << processed 
                                   << " processed, ~" << remaining << " remaining)";
        }
    }
}

void Engine::notifyParameterChanged() {
    // SIMPLIFIED: If parameters are being modified, defer state notification
    // This prevents crashes when GUI changes parameters and triggers callbacks
    if (parametersBeingModified_.load() > 0) {
        ofLogVerbose("Engine") << "notifyParameterChanged() deferred - parameter modification in progress";
        enqueueStateNotification();
        return;
    }
    
    // CRITICAL FIX: Use deferred notification pattern to prevent recursive notifications
    // notifyParameterChanged() can be called during state notifications (e.g., when observers trigger parameter changes)
    // Using deferred pattern ensures notifications happen on main thread and prevents infinite recursion
    enqueueStateNotification();
}

void Engine::onBPMChanged(float& newBpm) {
    
    // Defer state update if script is executing (prevents recursive updates)
    // The state will be updated after script execution completes
    if (hasUnsafeState(UnsafeState::SCRIPT_EXECUTING)) {
        ofLogVerbose("Engine") << "[BPM_CHANGE] BPM changed to " << newBpm << " during script execution - deferring state update";
        // Don't notify during script execution - it will be notified after execution completes
        return;
    }
    
    ofLogVerbose("Engine") << "[BPM_CHANGE] BPM changed to " << newBpm << ", notifying state change";
    notifyParameterChanged();
}

EngineState Engine::buildStateSnapshot() const {
    // CRITICAL FIX: Set thread-local flag to prevent recursive snapshot building
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
    
    // CRITICAL FIX: Check unsafe state flags FIRST
    // If script is executing, return cached state immediately
    // This prevents state snapshot building during script execution
    // Note: Serialization no longer uses buildStateSnapshot() (uses getStateSnapshot() instead - lock-free)
    if (hasUnsafeState(UnsafeState::SCRIPT_EXECUTING)) {
        // Script is executing - return cached state immediately
        // Still needed: Yes - getState() needs fallback during unsafe periods
        // Can be simplified: Potentially - If Shells migrate to snapshots, getState() may become unused
        // Note: Serialization no longer uses cachedState_ (uses getStateSnapshot() instead - lock-free)
        std::shared_lock<std::shared_mutex> lock(cachedStateMutex_);
        if (cachedState_) {
            // CRITICAL FIX: Update version of cached state before returning
            // Cached state may have been built earlier with version 0, but we need current version
            // This ensures observers receive state with correct version for consistency tracking
            EngineState result = *cachedState_;
            result.version = stateVersion_.load();
            
            // CRITICAL: If cached state is empty (no modules), log a warning
            // This indicates the initial snapshot was built before modules were loaded
            if (cachedState_->modules.empty() && cachedState_->connections.empty()) {
                ofLogWarning("Engine") << "buildStateSnapshot() BLOCKED by script execution - returning EMPTY cached state (modules: " 
                                      << cachedState_->modules.size() << ", connections: " << cachedState_->connections.size() << ")";
            } else {
                ofLogVerbose("Engine") << "buildStateSnapshot() BLOCKED by script execution - returning cached state (modules: " 
                                      << cachedState_->modules.size() << ", connections: " << cachedState_->connections.size() << ")";
            }
            return result;
        }
        ofLogError("Engine") << "buildStateSnapshot() blocked but no cached state available";
        return EngineState();
    }
    
    // CRITICAL FIX: Check ALL unsafe conditions using isInUnsafeState()
    // Still needed: Yes - Must detect unsafe periods before building snapshots
    // Can be simplified: No - Multiple flags needed to detect all unsafe conditions
    // Script execution CAN directly modify module state (via Lua bindings), not just queue commands
    // So we must check unsafe state flags (script executing, commands processing) as well as parametersBeingModified_
    // This prevents buildModuleStates() from aborting mid-iteration and leaving partial state
    
    // CRITICAL: Use memory barrier to ensure we see latest flag values
    // Still needed: Yes - Ensures we see latest atomic flag values
    // Can be simplified: No - Memory barrier is necessary for correctness
    std::atomic_thread_fence(std::memory_order_acquire);
    bool isExecuting = hasUnsafeState(UnsafeState::SCRIPT_EXECUTING);
    bool commandsProcessing = hasUnsafeState(UnsafeState::COMMANDS_PROCESSING);
    int paramsModifying = parametersBeingModified_.load();
    
    if (isExecuting || commandsProcessing || paramsModifying > 0) {
        // CRITICAL FIX: Never return empty state - return cached state instead
        // This ensures observers always receive valid state, even during unsafe periods
        std::shared_lock<std::shared_mutex> lock(cachedStateMutex_);
        if (cachedState_) {
            // CRITICAL FIX: Update version of cached state before returning
            // Cached state may have been built earlier with version 0, but we need current version
            // This ensures observers receive state with correct version for consistency tracking
            EngineState result = *cachedState_;
            result.version = stateVersion_.load();
            
            ofLogWarning("Engine") << "buildStateSnapshot() BLOCKED during unsafe period - returning cached state (isExecutingScript: " 
                                   << isExecuting << ", commandsProcessing: " << commandsProcessing 
                                   << ", parametersModifying: " << paramsModifying << ")";
            return result;
        }
        // CRITICAL FIX: Cached state should always be initialized during setup
        // If it's not available, initialize it now (shouldn't happen, but safety fallback)
        ofLogError("Engine") << "buildStateSnapshot() called during unsafe period but cached state not initialized - initializing now";
        std::unique_lock<std::shared_mutex> cacheLock(cachedStateMutex_);
        if (!cachedState_) {
            cachedState_ = std::make_unique<EngineState>();
        }
        return *cachedState_;
    }
    
    // CRITICAL FIX: Prevent concurrent snapshot building
    // Use mutex for exclusive access during snapshot building
    // Note: Serialization no longer uses buildStateSnapshot() (uses getStateSnapshot() instead - lock-free)
    // This prevents crashes when multiple threads try to build snapshots simultaneously
    // (e.g., when ScriptManager observer fires while another snapshot is being built)
    std::lock_guard<std::mutex> mutexGuard(snapshotMutex_);
    
    EngineState state;
    
    try {
        buildTransportState(state);
    } catch (const std::exception& e) {
        throw;
    } catch (...) {
        throw;
    }
    
    
    // CRITICAL FIX: Double-check unsafe state RIGHT BEFORE calling buildModuleStates()
    // State can change from safe to unsafe between the initial check and this point
    // (e.g., script execution can start on main thread while we're building snapshot)
    // This prevents buildModuleStates() from being called during script execution
    if (isInUnsafeState()) {
        ofLogVerbose("Engine") << "buildStateSnapshot() - unsafe state detected before buildModuleStates() - returning cached state";
        std::shared_lock<std::shared_mutex> lock(cachedStateMutex_);
        if (cachedState_) {
            return *cachedState_;
        }
        // If no cached state, return empty state (shouldn't happen)
        ofLogError("Engine") << "buildStateSnapshot() - unsafe state but no cached state available";
        return EngineState();
    }
    
    try {
        // CRITICAL FIX: Check if buildModuleStates() completed successfully
        // If it returns false, it aborted due to unsafe period and left partial state
        // In this case, return cached state instead of partial state to prevent crashes
        bool buildSuccess = buildModuleStates(state);
        if (!buildSuccess) {
            // buildModuleStates() aborted - return cached state instead of partial state
            std::shared_lock<std::shared_mutex> lock(cachedStateMutex_);
            if (cachedState_) {
                ofLogWarning("Engine") << "buildModuleStates() aborted due to unsafe period - returning cached state instead of partial state";
                ofLogWarning("Engine") << "Cached state has " << cachedState_->modules.size() << " modules, " 
                                      << cachedState_->connections.size() << " connections";
                return *cachedState_;
            } else {
                ofLogError("Engine") << "buildModuleStates() aborted but no cached state available - returning partial state";
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
    // CRITICAL FIX: During snapshot building, only use cached script if available
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
    
    // Cache the result before releasing lock
    {
        std::unique_lock<std::shared_mutex> cacheLock(cachedStateMutex_);
        if (!cachedState_) {
            cachedState_ = std::make_unique<EngineState>();
        }
        *cachedState_ = state;
    }
    
    // Note: snapshotMutex_ is released automatically by lock_guard on function exit
    
    return state;
}

void Engine::waitForStateVersion(uint64_t targetVersion, uint64_t timeoutMs) {
    // Wait for state to reach target version (for future sync contracts)
    // Uses std::this_thread::yield() in wait loop (non-blocking)
    // Returns when stateVersion_ >= targetVersion or timeout occurs
    uint64_t startTime = getCurrentTimestamp();
    uint64_t timeoutTime = startTime + timeoutMs;
    
    while (stateVersion_.load() < targetVersion) {
        uint64_t now = getCurrentTimestamp();
        if (now >= timeoutTime) {
            // Timeout reached
            ofLogWarning("Engine") << "waitForStateVersion() timed out waiting for version " << targetVersion 
                                   << " (current: " << stateVersion_.load() << ", timeout: " << timeoutMs << "ms)";
            return;
        }
        // Yield to other threads (non-blocking)
        std::this_thread::yield();
    }
}

void Engine::updateStateSnapshot() {
    // Increment version number (atomic)
    uint64_t versionBefore = stateVersion_.load();
    uint64_t version = stateVersion_.fetch_add(1) + 1;
    
    
    // Get transport state (clock_ is Engine member, no lock needed)
    EngineState::Transport transport;
    transport.isPlaying = clock_.isPlaying();
    transport.bpm = clock_.getTargetBPM();  // Use getTargetBPM() like buildTransportState()
    transport.currentBeat = 0;  // TODO: Get from Clock if available
    
    // CRITICAL: Update all module snapshots before reading them
    // Module snapshots need to be refreshed after parameter changes
    // This ensures we have current state when aggregating snapshots
    moduleRegistry_.forEachModule([](const std::string& uuid, const std::string& humanName, std::shared_ptr<Module> module) {
        if (module) {
            module->updateSnapshot();  // Update snapshot with current module state
        }
    });
    
    // Get module snapshots (lock-free - Module::getSnapshot() uses mutex, but forEachModule handles registry locking)
    // This is the key difference from buildStateSnapshot() - we use module snapshots instead of building from scratch
    ofJson modulesJson;
    // forEachModule() handles registry locking internally
    moduleRegistry_.forEachModule([&modulesJson](const std::string& uuid, const std::string& humanName, std::shared_ptr<Module> module) {
        if (module) {
            // Hold shared_ptr reference to prevent destruction during copy
            auto moduleSnapshot = module->getSnapshot();  // Fast read with mutex (C++17 compatible)
            if (moduleSnapshot) {
                // CRITICAL: Make a safe copy by serializing and deserializing
                // This prevents memory corruption if the original JSON is being destroyed
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
    
    // Create immutable JSON snapshot and update pointer with mutex (C++17 compatible)
    {
        std::lock_guard<std::mutex> lock(snapshotJsonMutex_);
        snapshotJson_ = std::make_shared<const ofJson>(std::move(json));
    }
}

void Engine::buildTransportState(EngineState& state) const {
    state.transport.isPlaying = clock_.isPlaying();
    // CRITICAL FIX: Use getTargetBPM() instead of getBPM() for state snapshots
    // getBPM() returns smoothed currentBpm (for audio/display), but for script generation
    // we want the target BPM (the value that was set, not the smoothed value)
    float bpm = clock_.getTargetBPM();
    state.transport.bpm = bpm;
    state.transport.currentBeat = 0;  // TODO: Get from Clock if available
    
}

bool Engine::buildModuleStates(EngineState& state) const {
    // CRITICAL: This function should NEVER be called during script execution
    // If it is, it means our guards failed - log extensively and abort immediately
    std::atomic_thread_fence(std::memory_order_acquire);
    bool isExecuting = hasUnsafeState(UnsafeState::SCRIPT_EXECUTING);
    bool commandsProcessing = hasUnsafeState(UnsafeState::COMMANDS_PROCESSING);
    int paramsModifying = parametersBeingModified_.load();
    
    
    // CRITICAL: If we're here during script execution, something is very wrong
    if (isExecuting) {
        ofLogError("Engine") << "CRITICAL: buildModuleStates() called during script execution! This should never happen! Aborting immediately.";
        // Log stack trace if possible
        return false;
    }
    
    // CRITICAL: Check unsafe periods at the point of module access
    // Even if buildStateSnapshot() checked, state can change between check and module access
    // CRITICAL FIX: Use memory barrier to ensure we see the latest unsafe state flags
    // This prevents race conditions where script execution starts but flag isn't visible yet
    std::atomic_thread_fence(std::memory_order_acquire);
    if (isInUnsafeState()) {
        ofLogError("Engine") << "buildModuleStates() called during unsafe period - ABORTING to prevent crash (isExecutingScript: " 
                             << hasUnsafeState(UnsafeState::SCRIPT_EXECUTING) << ", commandsProcessing: " << hasUnsafeState(UnsafeState::COMMANDS_PROCESSING) 
                             << ", parametersModifying: " << parametersBeingModified_.load() << ")";
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
        // CRITICAL FIX: Use memory barrier to ensure we see the latest value
        std::atomic_thread_fence(std::memory_order_acquire);
        bool isExecuting = hasUnsafeState(UnsafeState::SCRIPT_EXECUTING);
        bool commandsProcessing = hasUnsafeState(UnsafeState::COMMANDS_PROCESSING);
        int paramsModifying = parametersBeingModified_.load();
        if (isInUnsafeState()) {
            ofLogError("Engine") << "buildModuleStates() detected unsafe period while processing module " << name << " - ABORTING";
            aborted = true;
            return;  // Abort iteration - can't return bool from void callback
        }
        
        if (!module) {
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
        
        // SIMPLIFIED: Store module snapshot JSON directly (no variant type checking)
        // Modules control their own serialization, Engine just stores it
        // This eliminates variant complexity and makes serialization straightforward
        moduleState.typeSpecificData = moduleSnapshot;
        
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

void Engine::buildConnectionStates(EngineState& state) const {
    auto connections = connectionManager_.getConnections();
    for (const auto& conn : connections) {
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
        state.connections.push_back(info);
    }
}

void Engine::audioOut(ofSoundBuffer& buffer) {
    // CRITICAL: Process unified command queue (all commands: parameters, structural changes, etc.)
    // This handles all state mutations in a single, unified queue
    // Parameter changes (SetParameterCommand) and structural changes all go through here
    // CRITICAL FIX: Don't process commands during script execution - defer until after script completes
    // This prevents state changes during script execution which cause buildModuleStates() to abort and crash
    if (!hasUnsafeState(UnsafeState::SCRIPT_EXECUTING)) {
        processCommands();
    } else {
        // Script is executing - defer command processing until after script completes
        // Commands will be processed on next audio buffer after script execution finishes
        ofLogVerbose("Engine") << "Deferring command processing - script execution in progress";
    }
    
    // CRITICAL: Process Clock first to generate timing events
    clock_.audioOut(buffer);
    
    // Evaluate patterns (sample-accurate timing)
    patternRuntime_.evaluatePatterns(buffer);
    
    // Then process audio through master output
    if (masterAudioOut_) {
        masterAudioOut_->audioOut(buffer);
    }
}

void Engine::update(float deltaTime) {
    
    // Increment frame counter
    size_t frameCount = updateFrameCount_.fetch_add(1) + 1;
    
    
    // CRITICAL: Skip notification processing for first 10 frames to allow window to appear
    // During initialization, many notifications may be queued, and processing them
    // before the window is visible can delay window appearance
    // Window needs several frames to become visible and stable
    if (frameCount > 10) {
        // Process notification queue (after window has appeared)
        // This ensures deferred notifications are delivered on main thread event loop
        // Notifications are delivered before any other update logic
        // Process with limit to prevent blocking the event loop
        processNotificationQueue();
    }
    
    
    // Process pending script execution callbacks (from background thread)
    PendingCallback callback;
    while (pendingScriptCallbacks_.try_dequeue(callback)) {
        if (callback.callback) {
            callback.callback(callback.result);  // Execute on main thread
        }
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
    
    // CRITICAL FIX: Don't update modules while commands are processing
    // Commands modify module state, and updating modules concurrently causes race conditions
    // This prevents crashes when modules access state that's being modified by commands
    bool commandsProcessing = hasUnsafeState(UnsafeState::COMMANDS_PROCESSING);
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

bool Engine::loadSession(const std::string& path) {
    bool result = sessionManager_.loadSession(path);
    if (result) {
        // CRITICAL FIX: Use deferred notification pattern to prevent recursive notifications
        // Session loading can trigger state changes that might occur during notifications
        enqueueStateNotification();
    }
    return result;
}

bool Engine::saveSession(const std::string& path) {
    return sessionManager_.saveSession(path);
}

std::string Engine::serializeState() const {
    return getState().toJson();
}

bool Engine::deserializeState(const std::string& data) {
    try {
        EngineState state = EngineState::fromJson(data);
        // TODO: Apply state to engine (Phase 2)
        return true;
    } catch (...) {
        return false;
    }
}

uint64_t Engine::getCurrentTimestamp() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

bool Engine::enqueueCommand(std::unique_ptr<Command> cmd) {
    if (!cmd) {
        ofLogWarning("Engine") << "Attempted to enqueue null command";
        return false;
    }
    
    // Set timestamp
    cmd->setTimestamp(getCurrentTimestamp());
    
    // Capture command description before moving
    std::string cmdDescription = cmd->describe();
    
    
    // Try to enqueue (lock-free)
    if (commandQueue_.try_enqueue(std::move(cmd))) {
        return true;
    } else {
        // Queue is full
        commandStats_.queueOverflows++;
        commandStats_.commandsDropped++;
        
        // Log warning (but don't spam)
        static int warningCount = 0;
        if (++warningCount % 100 == 0) {
            ofLogWarning("Engine") << "Command queue full (" 
                                   << commandStats_.queueOverflows 
                                   << " overflows, " 
                                   << commandStats_.commandsDropped 
                                   << " commands dropped)";
        }
        return false;
    }
}

int Engine::processCommands() {
    // CRITICAL FIX: Set flag to prevent state snapshots during command execution
    // This prevents crashes when commands trigger state changes (e.g., clock:start())
    setUnsafeState(UnsafeState::COMMANDS_PROCESSING, true);
    
    
    int processed = 0;
    std::unique_ptr<Command> cmd;
    
    // Process all queued commands (lock-free, called from audio thread)
    while (commandQueue_.try_dequeue(cmd)) {
        try {
            uint64_t versionBefore = stateVersion_.load();
            EngineState stateBefore = getState();
            
            cmd->execute(*this);
            commandStats_.commandsProcessed++;
            processed++;
            
            uint64_t versionAfter = stateVersion_.load();
            EngineState stateAfter = getState();
            
            // NOTE: Removed onCommandExecuted() callback - state observer handles all sync now
            
        } catch (const std::exception& e) {
            ofLogError("Engine") << "Command execution failed: " << e.what()
                                << " (" << cmd->describe() << ")";
            // Don't notify ScriptManager on error - state didn't change
        }
    }
    
    // Clear flag after all commands are processed
    setUnsafeState(UnsafeState::COMMANDS_PROCESSING, false);
    
    // Video drawing now relies on event-driven state updates via notification queue
    // No cooldown needed - state updates are deferred to main thread event loop
    
    
    // CRITICAL FIX: Don't call notifyStateChange() or updateStateSnapshot() from audio thread
    // Enqueue notification instead - main thread will process queue in update()
    // This prevents thread safety issues with buildStateSnapshot() and module registry access
    // updateStateSnapshot() accesses moduleRegistry_ which is not safe from audio thread
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
        // CRITICAL FIX: Update state snapshot immediately after command execution
        // This ensures state is synchronized before notifications are sent
        // This matches the pattern used by processCommands()
        updateStateSnapshot();  // Create new immutable JSON snapshot
        
        // CRITICAL FIX: Use deferred notification pattern instead of calling notifyStateChange() directly
        // This prevents recursive notifications when commands are executed during state notifications
        // (e.g., when an observer triggers a command). Notifications are enqueued and processed on main thread,
        // ensuring thread safety and preventing infinite recursion.
        enqueueStateNotification();
        
        // NOTE: Removed onCommandExecuted() callback - state observer handles all sync now
    } catch (const std::exception& e) {
        ofLogError("Engine") << "Immediate command execution failed: " << e.what()
                            << " (" << cmd->describe() << ")";
    }
}

} // namespace vt

