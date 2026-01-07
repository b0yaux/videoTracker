#include "Engine.h"
#include "gui/GUIManager.h"
#include "gui/ViewManager.h"
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

Engine::Engine() 
    : assetLibrary_(&projectManager_, &mediaConverter_, &moduleRegistry_)
    , scriptManager_(this) {
}

Engine::~Engine() {
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
    
    // Initialize project and session FIRST (before ScriptManager)
    // This ensures session is loaded before script generation
    initializeProjectAndSession();
    
    // Setup ScriptManager AFTER session is loaded
    // This ensures script is generated from loaded session state
    scriptManager_.setup();
    
    // Build initial state snapshot and cache it
    // This ensures cached state is always valid, even before first getState() call
    try {
        EngineState initialState = buildStateSnapshot();
        {
            std::unique_lock<std::shared_mutex> cacheLock(cachedStateMutex_);
            *cachedState_ = initialState;
        }
    } catch (const std::exception& e) {
        ofLogWarning("Engine") << "Failed to build initial state snapshot: " << e.what();
        // Cached state already initialized with empty state, which is acceptable
    }
    
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
    
    // Initialize SessionManager with dependencies
    sessionManager_ = SessionManager(
        &projectManager_,
        &clock_,
        &moduleRegistry_,
        &moduleFactory_,
        &parameterRouter_,
        &connectionManager_,
        nullptr  // ViewManager set later via setupGUIManagers
    );
    sessionManager_.setConnectionManager(&connectionManager_);
    sessionManager_.setPatternRuntime(&patternRuntime_);  // Phase 2: Enable pattern migration
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
    // Setup CommandExecutor with dependencies
    // GUIManager will be set later via setupGUIManagers
    commandExecutor_.setup(&moduleRegistry_, nullptr, &connectionManager_, &assetLibrary_, &clock_, &patternRuntime_, this);
    
    // Set callbacks for module operations
    // These will be updated when GUIManager is available
    commandExecutor_.setOnAddModule([this](const std::string& moduleType) {
        moduleRegistry_.addModule(
            moduleFactory_,
            moduleType,
            &clock_,
            &connectionManager_,
            &parameterRouter_,
            &patternRuntime_,
            nullptr,  // GUIManager set later via setupGUIManagers
            config_.masterAudioOutName,
            config_.masterVideoOutName
        );
        notifyStateChange();
    });
    
    commandExecutor_.setOnRemoveModule([this](const std::string& instanceName) {
        moduleRegistry_.removeModule(
            instanceName,
            &connectionManager_,
            nullptr,  // GUIManager set later via setupGUIManagers
            config_.masterAudioOutName,
            config_.masterVideoOutName
        );
        notifyStateChange();
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
function audioOut(name, config)
    config = config or {}
    execCommand("add AudioOutput " .. name)
    for k, v in pairs(config) do
        execCommand("set " .. name .. " " .. k .. " " .. tostring(v))
    end
    return name
end

function videoOut(name, config)
    config = config or {}
    execCommand("add VideoOutput " .. name)
    for k, v in pairs(config) do
        execCommand("set " .. name .. " " .. k .. " " .. tostring(v))
    end
    return name
end

function oscilloscope(name, config)
    config = config or {}
    execCommand("add Oscilloscope " .. name)
    for k, v in pairs(config) do
        execCommand("set " .. name .. " " .. k .. " " .. tostring(v))
    end
    return name
end

function spectrogram(name, config)
    config = config or {}
    execCommand("add Spectrogram " .. name)
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

void Engine::setupGUIManagers(GUIManager* guiManager, ViewManager* viewManager) {
    guiManager_ = guiManager;
    
    if (guiManager) {
        guiManager->setRegistry(&moduleRegistry_);
        guiManager->setParameterRouter(&parameterRouter_);
        guiManager->setConnectionManager(&connectionManager_);
        sessionManager_.setGUIManager(guiManager);
        
        // Set parameter change notification callback for script sync
        // This will automatically update all existing modules' callbacks (including master outputs)
        moduleRegistry_.setParameterChangeNotificationCallback([this]() {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    int paramModCount = parametersBeingModified_.load();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"SIMPLIFIED\",\"hypothesisId\":\"GUI\",\"location\":\"Engine.cpp:parameterChangeCallback\",\"message\":\"parameterChangeCallback ENTRY\",\"data\":{\"parametersBeingModified\":" << paramModCount << "},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            
            // Increment counter to track parameter modification in progress
            parametersBeingModified_.fetch_add(1);
            
            // Defer notification - will be processed after parameter modification completes
            // This prevents building snapshots while parameters are being modified
            stateNeedsNotification_.store(true);
            
            // Decrement counter
            parametersBeingModified_.fetch_sub(1);
            
            ofLogVerbose("Engine") << "[PARAM_CHANGE] Parameter changed, deferring state notification";
        });
        
        // Subscribe to Clock BPM changes (proper event-based architecture)
        // Note: This is safe to call multiple times - ofAddListener handles duplicates
        ofAddListener(clock_.bpmChangedEvent, this, &Engine::onBPMChanged);
        
        // Update CommandExecutor with GUIManager
        commandExecutor_.setup(&moduleRegistry_, guiManager, &connectionManager_, &assetLibrary_, &clock_, &patternRuntime_, this);
        
        // Update callbacks to include GUIManager
        commandExecutor_.setOnAddModule([this, guiManager](const std::string& moduleType) {
            moduleRegistry_.addModule(
                moduleFactory_,
                moduleType,
                &clock_,
                &connectionManager_,
                &parameterRouter_,
                &patternRuntime_,
                guiManager,
                config_.masterAudioOutName,
                config_.masterVideoOutName
            );
            notifyStateChange();
        });
        
        commandExecutor_.setOnRemoveModule([this, guiManager](const std::string& instanceName) {
            moduleRegistry_.removeModule(
                instanceName,
                &connectionManager_,
                guiManager,
                config_.masterAudioOutName,
                config_.masterVideoOutName
            );
            notifyStateChange();
        });
    }
    
    if (viewManager) {
        sessionManager_.setViewManager(viewManager);
    }
    
    // Setup GUI coordination (initializes all modules)
    if (guiManager) {
        sessionManager_.setupGUI(guiManager);
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
        
        // Notify state change after command execution
        notifyStateChange();
        
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
    
    // Set script execution flag to prevent recursive state updates
    isExecutingScript_.store(true);
    
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"J\",\"location\":\"Engine.cpp:549\",\"message\":\"eval - before doString\",\"data\":{\"scriptLength\":" << script.length() << "},\"timestamp\":" << now << "}\n";
            logFile.close();
        }
    }
    // #endregion
    
    try {
        // Set error callback to capture errors
        std::string luaError;
        lua_->setErrorCallback([&luaError](std::string& msg) {
            luaError = msg;
        });
        
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"J\",\"location\":\"Engine.cpp:558\",\"message\":\"eval - calling doString\",\"data\":{},\"timestamp\":" << now << "}\n";
                logFile.close();
            }
        }
        // #endregion
        
        // Execute Lua script with additional safety
        // Wrap in try-catch to handle any C++ exceptions from SWIG wrappers
        bool success = false;
        try {
            success = lua_->doString(script);
        } catch (const std::bad_alloc& e) {
            // Memory allocation failure
            isExecutingScript_.store(false);
            return Result(false, "Lua execution failed", "Memory allocation error: " + std::string(e.what()));
        } catch (const std::exception& e) {
            // Other C++ exceptions from SWIG wrappers
            isExecutingScript_.store(false);
            return Result(false, "Lua execution failed", "C++ exception: " + std::string(e.what()));
        } catch (...) {
            // Unknown exceptions
            isExecutingScript_.store(false);
            return Result(false, "Lua execution failed", "Unknown C++ exception during script execution");
        }
        
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"J\",\"location\":\"Engine.cpp:560\",\"message\":\"eval - doString returned\",\"data\":{\"success\":" << (success ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
                logFile.close();
            }
        }
        // #endregion
        
        // Clear script execution flag before returning
        isExecutingScript_.store(false);
        
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                bool stateNeedsNotification = stateNeedsNotification_.load();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"A,B\",\"location\":\"Engine.cpp:eval\",\"message\":\"eval - script execution complete\",\"data\":{\"success\":" << (success ? "true" : "false") << ",\"stateNeedsNotification\":" << (stateNeedsNotification ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        
        // SIMPLIFIED: Request ScriptManager update after script execution
        // getState() will handle unsafe periods by returning cached state
        if (success) {
            scriptManager_.requestUpdate();
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
            
            return Result(false, "Lua execution failed", errorMsg);
        }
    } catch (const std::exception& e) {
        // Clear flag on exception
        isExecutingScript_.store(false);
        return Result(false, "Lua execution failed", e.what());
    } catch (...) {
        // Clear flag on unknown exception
        isExecutingScript_.store(false);
        return Result(false, "Lua execution failed", "Unknown error");
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
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            bool isExecuting = isExecutingScript_.load();
            bool commandsProcessing = commandsBeingProcessed_.load();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"SIMPLIFIED\",\"location\":\"Engine.cpp:getState\",\"message\":\"getState ENTRY\",\"data\":{\"isExecutingScript\":" << (isExecuting ? "true" : "false") << ",\"commandsBeingProcessed\":" << (commandsProcessing ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
            logFile.flush();
            logFile.close();
        }
    }
    // #endregion
    
    // CRITICAL FIX: If unsafe period, return last known good cached state (only updated during safe periods)
    // Never build snapshots during unsafe periods - this prevents race conditions
    // Cached state is always initialized during setup, so it should always be available
    if (isExecutingScript_.load() || commandsBeingProcessed_.load()) {
        std::shared_lock<std::shared_mutex> lock(cachedStateMutex_);
        if (cachedState_) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"SIMPLIFIED\",\"hypothesisId\":\"SIMPLIFIED\",\"location\":\"Engine.cpp:getState\",\"message\":\"getState - returning last good cached state (unsafe period)\",\"data\":{},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            return *cachedState_;
        }
        // CRITICAL FIX: Cached state should always be initialized during setup
        // If it's not available, initialize it now (shouldn't happen, but safety fallback)
        ofLogError("Engine") << "getState() called during unsafe period but cached state not initialized - initializing now";
        std::unique_lock<std::shared_mutex> cacheLock(cachedStateMutex_);
        if (!cachedState_) {
            cachedState_ = std::make_unique<EngineState>();
        }
        return *cachedState_;
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

size_t Engine::subscribe(StateObserver callback) {
    std::unique_lock<std::shared_mutex> lock(stateMutex_);
    size_t id = nextObserverId_.fetch_add(1);
    observers_.emplace_back(id, callback);
    return id;
}

void Engine::unsubscribe(size_t id) {
    std::unique_lock<std::shared_mutex> lock(stateMutex_);
    observers_.erase(
        std::remove_if(observers_.begin(), observers_.end(),
            [id](const auto& pair) { return pair.first == id; }),
        observers_.end()
    );
}

void Engine::notifyStateChange() {
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            bool isExecuting = isExecutingScript_.load();
            bool commandsProcessing = commandsBeingProcessed_.load();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"SIMPLIFIED\",\"hypothesisId\":\"SIMPLIFIED\",\"location\":\"Engine.cpp:notifyStateChange\",\"message\":\"notifyStateChange ENTRY\",\"data\":{\"isExecutingScript\":" << (isExecuting ? "true" : "false") << ",\"commandsBeingProcessed\":" << (commandsProcessing ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
            logFile.flush();
            logFile.close();
        }
    }
    // #endregion
    
    // CRITICAL FIX: Don't skip notifications during command processing
    // getState() now handles unsafe periods by returning cached state
    // This ensures observers always receive state updates, even during command processing
    // Observers will see the last known good state, which is acceptable
    // The stateNeedsNotification_ flag ensures notifications happen after commands complete
    
    // CRITICAL FIX: Throttle expensive state snapshot building
    // Prevents main thread blocking from excessive snapshot generation
    // which was causing engine to appear frozen
    uint64_t now = getCurrentTimestamp();
    uint64_t lastTime = lastStateSnapshotTime_.load();
    
    // Only build snapshot if enough time has passed since last one
    if (now - lastTime < STATE_SNAPSHOT_THROTTLE_MS) {
        // Skip this notification - too soon since last snapshot
        // This prevents excessive "MultiSampler: Serialized..." log spam
        // and prevents main thread blocking
        return;
    }
    
    lastStateSnapshotTime_.store(now);
    
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"A,B,D\",\"location\":\"Engine.cpp:notifyStateChange\",\"message\":\"notifyStateChange - calling getState\",\"data\":{},\"timestamp\":" << now << "}\n";
            logFile.flush();
            logFile.close();
        }
    }
    // #endregion
    
    // CRITICAL FIX: Use getState() instead of buildStateSnapshot() directly
    // getState() handles unsafe periods by returning cached state
    // This prevents crashes when notifyStateChange() is called during script execution
    EngineState state = getState();
    
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:notifyStateChange\",\"message\":\"BEFORE calling observers - about to iterate\",\"data\":{\"observerCount\":" << observers_.size() << "},\"timestamp\":" << now << "}\n";
            logFile.flush();
            logFile.close();
        }
    }
    // #endregion
    
    // CRITICAL FIX: Collect broken observers during iteration, remove after
    // This prevents iterator invalidation and ensures all observers are called
    std::vector<size_t> brokenObservers;
    
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    size_t observerIndex = 0;
    for (const auto& [id, observer] : observers_) {
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:notifyStateChange\",\"message\":\"BEFORE calling observer\",\"data\":{\"observerId\":" << id << ",\"observerIndex\":" << observerIndex << "},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        try {
            observer(state);
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:notifyStateChange\",\"message\":\"AFTER calling observer - SUCCESS\",\"data\":{\"observerId\":" << id << ",\"observerIndex\":" << observerIndex << "},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
        } catch (const std::exception& e) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:notifyStateChange\",\"message\":\"EXCEPTION in observer callback - CRASH POINT\",\"data\":{\"observerId\":" << id << ",\"observerIndex\":" << observerIndex << ",\"error\":\"" << e.what() << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            ofLogError("Engine") << "Error in state observer " << id << ": " << e.what();
            brokenObservers.push_back(id);
        } catch (...) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:notifyStateChange\",\"message\":\"UNKNOWN EXCEPTION in observer callback - CRASH POINT\",\"data\":{\"observerId\":" << id << ",\"observerIndex\":" << observerIndex << "},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
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
    
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:notifyStateChange\",\"message\":\"AFTER all observers - SUCCESS\",\"data\":{},\"timestamp\":" << now << "}\n";
            logFile.flush();
            logFile.close();
        }
    }
    // #endregion
}

void Engine::notifyParameterChanged() {
    // SIMPLIFIED: If parameters are being modified, defer state notification
    // This prevents crashes when GUI changes parameters and triggers callbacks
    if (parametersBeingModified_.load() > 0) {
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"SIMPLIFIED\",\"hypothesisId\":\"GUI\",\"location\":\"Engine.cpp:notifyParameterChanged\",\"message\":\"notifyParameterChanged - deferring (parameter modification in progress)\",\"data\":{},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        ofLogVerbose("Engine") << "notifyParameterChanged() deferred - parameter modification in progress";
        stateNeedsNotification_.store(true);
        return;
    }
    
    // SIMPLIFIED: Direct state change notification (no complex chain)
    // Observers can use dirty flags to avoid unnecessary work
    notifyStateChange();
}

void Engine::onBPMChanged(float& newBpm) {
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            bool isExecuting = isExecutingScript_.load();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"A\",\"location\":\"Engine.cpp:687\",\"message\":\"onBPMChanged - event received\",\"data\":{\"newBpm\":" << newBpm << ",\"isExecutingScript\":" << (isExecuting ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
            logFile.close();
        }
    }
    // #endregion
    
    // Defer state update if script is executing (prevents recursive updates)
    // The state will be updated after script execution completes
    if (isExecutingScript_.load()) {
        ofLogVerbose("Engine") << "[BPM_CHANGE] BPM changed to " << newBpm << " during script execution - deferring state update";
        // Don't notify during script execution - it will be notified after execution completes
        return;
    }
    
    ofLogVerbose("Engine") << "[BPM_CHANGE] BPM changed to " << newBpm << ", notifying state change";
    notifyParameterChanged();
}

EngineState Engine::buildStateSnapshot() const {
    // CRITICAL FIX: Only check commandsBeingProcessed_ and parametersBeingModified_
    // Script execution doesn't prevent snapshots - it just queues commands
    // Commands are processed atomically, so state is stable after commands are done
    // Only block if commands are actively processing or parameters are being modified
    if (commandsBeingProcessed_.load() || parametersBeingModified_.load() > 0) {
        // CRITICAL FIX: Never return empty state - return cached state instead
        // This ensures observers always receive valid state, even during unsafe periods
        std::shared_lock<std::shared_mutex> lock(cachedStateMutex_);
        if (cachedState_) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"SIMPLIFIED\",\"hypothesisId\":\"SIMPLIFIED\",\"location\":\"Engine.cpp:buildStateSnapshot\",\"message\":\"buildStateSnapshot - returning cached state (unsafe period)\",\"data\":{},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            ofLogVerbose("Engine") << "buildStateSnapshot() called during unsafe period - returning cached state";
            return *cachedState_;
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
    // This prevents crashes when multiple threads try to build snapshots simultaneously
    // (e.g., when ScriptManager observer fires while another snapshot is being built)
    
    // Try to acquire exclusive access (non-blocking check first)
    bool expected = false;
    if (!snapshotInProgress_.compare_exchange_strong(expected, true)) {
        // Another snapshot is in progress - return cached state to prevent crash
        std::shared_lock<std::shared_mutex> lock(cachedStateMutex_);
        if (cachedState_) {
            ofLogVerbose("Engine") << "buildStateSnapshot() called concurrently - returning cached state";
            return *cachedState_;
        }
        // If no cached state, wait briefly for the other snapshot to complete
        // This is a fallback - should rarely happen
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::shared_lock<std::shared_mutex> retryLock(cachedStateMutex_);
        if (cachedState_) {
            return *cachedState_;
        }
        // Last resort: wait for mutex (should be very rare)
        std::lock_guard<std::mutex> guard(snapshotMutex_);
        if (cachedState_) {
            return *cachedState_;
        }
    }
    
    // RAII guard to ensure flag is released even on exception
    struct SnapshotGuard {
        std::atomic<bool>& flag_;
        SnapshotGuard(std::atomic<bool>& flag) : flag_(flag) {}
        ~SnapshotGuard() { flag_.store(false); }
    };
    SnapshotGuard guard(snapshotInProgress_);
    
    // Acquire mutex for exclusive access during snapshot building
    std::lock_guard<std::mutex> mutexGuard(snapshotMutex_);
    
    EngineState state;
    
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildStateSnapshot\",\"message\":\"BEFORE buildTransportState()\",\"data\":{},\"timestamp\":" << now << "}\n";
            logFile.flush();
            logFile.close();
        }
    }
    // #endregion
    try {
        buildTransportState(state);
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildStateSnapshot\",\"message\":\"AFTER buildTransportState() - SUCCESS\",\"data\":{},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
    } catch (const std::exception& e) {
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildStateSnapshot\",\"message\":\"EXCEPTION in buildTransportState()\",\"data\":{\"error\":\"" << e.what() << "\"},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        throw;
    } catch (...) {
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildStateSnapshot\",\"message\":\"UNKNOWN EXCEPTION in buildTransportState()\",\"data\":{},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        throw;
    }
    
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildStateSnapshot\",\"message\":\"BEFORE buildModuleStates()\",\"data\":{},\"timestamp\":" << now << "}\n";
            logFile.flush();
            logFile.close();
        }
    }
    // #endregion
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            bool isExecuting = isExecutingScript_.load();
            bool commandsProcessing = commandsBeingProcessed_.load();
            int paramsModifying = parametersBeingModified_.load();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"SIMPLIFIED\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildStateSnapshot\",\"message\":\"BEFORE buildModuleStates()\",\"data\":{\"isExecutingScript\":" << (isExecuting ? "true" : "false") << ",\"commandsBeingProcessed\":" << (commandsProcessing ? "true" : "false") << ",\"parametersBeingModified\":" << paramsModifying << "},\"timestamp\":" << now << "}\n";
            logFile.flush();
            logFile.close();
        }
    }
    // #endregion
    
    try {
        buildModuleStates(state);
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"SIMPLIFIED\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildStateSnapshot\",\"message\":\"AFTER buildModuleStates() - SUCCESS\",\"data\":{},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
    } catch (const std::exception& e) {
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildStateSnapshot\",\"message\":\"EXCEPTION in buildModuleStates()\",\"data\":{\"error\":\"" << e.what() << "\"},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        throw;
    } catch (...) {
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildStateSnapshot\",\"message\":\"UNKNOWN EXCEPTION in buildModuleStates()\",\"data\":{},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        throw;
    }
    
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildStateSnapshot\",\"message\":\"BEFORE buildConnectionStates()\",\"data\":{},\"timestamp\":" << now << "}\n";
            logFile.flush();
            logFile.close();
        }
    }
    // #endregion
    try {
        buildConnectionStates(state);
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildStateSnapshot\",\"message\":\"AFTER buildConnectionStates() - SUCCESS\",\"data\":{},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
    } catch (const std::exception& e) {
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildStateSnapshot\",\"message\":\"EXCEPTION in buildConnectionStates()\",\"data\":{\"error\":\"" << e.what() << "\"},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        throw;
    } catch (...) {
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildStateSnapshot\",\"message\":\"UNKNOWN EXCEPTION in buildConnectionStates()\",\"data\":{},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        throw;
    }
    
    // Cache the result before releasing lock
    {
        std::unique_lock<std::shared_mutex> cacheLock(cachedStateMutex_);
        if (!cachedState_) {
            cachedState_ = std::make_unique<EngineState>();
        }
        *cachedState_ = state;
    }
    
    // Note: snapshotInProgress_ flag is released by RAII guard on function exit
    
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildStateSnapshot\",\"message\":\"buildStateSnapshot() EXIT - SUCCESS\",\"data\":{},\"timestamp\":" << now << "}\n";
            logFile.flush();
            logFile.close();
        }
    }
    // #endregion
    return state;
}

void Engine::buildTransportState(EngineState& state) const {
    state.transport.isPlaying = clock_.isPlaying();
    float bpm = clock_.getBPM();
    state.transport.bpm = bpm;
    state.transport.currentBeat = 0;  // TODO: Get from Clock if available
    
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"B\",\"location\":\"Engine.cpp:680\",\"message\":\"buildTransportState - BPM captured\",\"data\":{\"bpm\":" << bpm << "},\"timestamp\":" << now << "}\n";
            logFile.close();
        }
    }
    // #endregion
}

void Engine::buildModuleStates(EngineState& state) const {
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            bool isExecuting = isExecutingScript_.load();
            bool commandsProcessing = commandsBeingProcessed_.load();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"SIMPLIFIED\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"buildModuleStates() ENTRY\",\"data\":{\"isExecutingScript\":" << (isExecuting ? "true" : "false") << ",\"commandsBeingProcessed\":" << (commandsProcessing ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
            logFile.flush();
            logFile.close();
        }
    }
    // #endregion
    
    // CRITICAL: Check unsafe periods at the point of module access
    // Even if buildStateSnapshot() checked, state can change between check and module access
    if (isInUnsafeState()) {
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"SIMPLIFIED\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"buildModuleStates - ABORTING (unsafe period detected)\",\"data\":{},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        ofLogError("Engine") << "buildModuleStates() called during unsafe period - ABORTING to prevent crash";
        return;  // Don't access modules - return empty state
    }
    moduleRegistry_.forEachModule([&](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
        // CRITICAL: Check for unsafe periods INSIDE the loop
        // State can change from safe to unsafe while iterating over modules
        if (isInUnsafeState()) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"SIMPLIFIED\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"buildModuleStates - ABORTING in loop (unsafe period)\",\"data\":{\"moduleName\":\"" << name << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            ofLogError("Engine") << "buildModuleStates() detected unsafe period while processing module " << name << " - ABORTING";
            return;  // Abort iteration - don't access this or any remaining modules
        }
        
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"SIMPLIFIED\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"forEachModule callback - processing module\",\"data\":{\"moduleName\":\"" << name << "\",\"uuid\":\"" << uuid << "\",\"moduleValid\":" << (module ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        if (!module) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"forEachModule callback - module is null, skipping\",\"data\":{\"moduleName\":\"" << name << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            return;
        }
        
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"BEFORE getTypeName()\",\"data\":{\"moduleName\":\"" << name << "\"},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        EngineState::ModuleState moduleState;
        moduleState.name = name;
        std::string moduleType;
        try {
            moduleType = module->getTypeName();
            moduleState.type = moduleType;
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"AFTER getTypeName() - SUCCESS\",\"data\":{\"moduleName\":\"" << name << "\",\"type\":\"" << moduleType << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
        } catch (const std::exception& e) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"EXCEPTION in getTypeName()\",\"data\":{\"moduleName\":\"" << name << "\",\"error\":\"" << e.what() << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            ofLogError("Engine") << "Exception in getTypeName() for module " << name << ": " << e.what();
            return;
        } catch (...) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"UNKNOWN EXCEPTION in getTypeName()\",\"data\":{\"moduleName\":\"" << name << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            ofLogError("Engine") << "Unknown exception in getTypeName() for module " << name;
            return;
        }
        
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"BEFORE isEnabled()\",\"data\":{\"moduleName\":\"" << name << "\"},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        try {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"BEFORE isEnabled() - about to call\",\"data\":{\"moduleName\":\"" << name << "\",\"moduleType\":\"" << moduleType << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            try {
                moduleState.enabled = module->isEnabled();
                // #region agent log
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"AFTER isEnabled() - SUCCESS\",\"data\":{\"moduleName\":\"" << name << "\",\"enabled\":" << (moduleState.enabled ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
                        logFile.flush();
                        logFile.close();
                    }
                }
                // #endregion
            } catch (const std::exception& e) {
                // #region agent log
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"EXCEPTION in isEnabled() - CRASH POINT\",\"data\":{\"moduleName\":\"" << name << "\",\"moduleType\":\"" << moduleType << "\",\"error\":\"" << e.what() << "\"},\"timestamp\":" << now << "}\n";
                        logFile.flush();
                        logFile.close();
                    }
                }
                // #endregion
                ofLogError("Engine") << "Exception in isEnabled() for module " << name << " (" << moduleType << "): " << e.what();
                moduleState.enabled = true;  // Default to enabled on error
            } catch (...) {
                // #region agent log
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"UNKNOWN EXCEPTION in isEnabled() - CRASH POINT\",\"data\":{\"moduleName\":\"" << name << "\",\"moduleType\":\"" << moduleType << "\"},\"timestamp\":" << now << "}\n";
                        logFile.flush();
                        logFile.close();
                    }
                }
                // #endregion
                ofLogError("Engine") << "Unknown exception in isEnabled() for module " << name << " (" << moduleType << ")";
                moduleState.enabled = true;  // Default to enabled on error
            }
        } catch (const std::exception& e) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"EXCEPTION in isEnabled()\",\"data\":{\"moduleName\":\"" << name << "\",\"error\":\"" << e.what() << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            ofLogError("Engine") << "Exception in isEnabled() for module " << name << ": " << e.what();
            return;
        } catch (...) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"UNKNOWN EXCEPTION in isEnabled()\",\"data\":{\"moduleName\":\"" << name << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            ofLogError("Engine") << "Unknown exception in isEnabled() for module " << name;
            return;
        }
        
        // Get state snapshot as JSON (clean separation: JSON for serialization)
        // Modules control their own serialization format, which is more robust
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"BEFORE getStateSnapshot()\",\"data\":{\"moduleName\":\"" << name << "\"},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        ofJson moduleSnapshot;
        try {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"BEFORE getStateSnapshot() - about to call\",\"data\":{\"moduleName\":\"" << name << "\",\"moduleType\":\"" << moduleType << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            
            moduleSnapshot = module->getStateSnapshot();
            
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"AFTER getStateSnapshot() - SUCCESS\",\"data\":{\"moduleName\":\"" << name << "\",\"isObject\":" << (moduleSnapshot.is_object() ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
        } catch (const std::exception& e) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"EXCEPTION in getStateSnapshot() - CRASH POINT\",\"data\":{\"moduleName\":\"" << name << "\",\"moduleType\":\"" << moduleType << "\",\"error\":\"" << e.what() << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            ofLogError("Engine") << "Exception in getStateSnapshot() for module " << name << " (" << moduleType << "): " << e.what();
            return;
        } catch (...) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"UNKNOWN EXCEPTION in getStateSnapshot() - CRASH POINT\",\"data\":{\"moduleName\":\"" << name << "\",\"moduleType\":\"" << moduleType << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            ofLogError("Engine") << "Unknown exception in getStateSnapshot() for module " << name << " (" << moduleType << ")";
            return;
        }
        
        // Extract parameters from JSON snapshot
        // This approach is cleaner: modules serialize their own state, Engine extracts what it needs
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"BEFORE JSON parameter extraction\",\"data\":{\"moduleName\":\"" << name << "\",\"isObject\":" << (moduleSnapshot.is_object() ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        if (moduleSnapshot.is_object()) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"BEFORE JSON iteration loop\",\"data\":{\"moduleName\":\"" << name << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
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
                // #region agent log
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"AFTER JSON iteration loop - SUCCESS\",\"data\":{\"moduleName\":\"" << name << "\",\"paramCount\":" << moduleState.parameters.size() << "},\"timestamp\":" << now << "}\n";
                        logFile.flush();
                        logFile.close();
                    }
                }
                // #endregion
            } catch (const std::exception& e) {
                // #region agent log
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"EXCEPTION in JSON iteration loop\",\"data\":{\"moduleName\":\"" << name << "\",\"error\":\"" << e.what() << "\"},\"timestamp\":" << now << "}\n";
                        logFile.flush();
                        logFile.close();
                    }
                }
                // #endregion
                ofLogError("Engine") << "Exception in JSON iteration for module " << name << ": " << e.what();
            } catch (...) {
                // #region agent log
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"UNKNOWN EXCEPTION in JSON iteration loop\",\"data\":{\"moduleName\":\"" << name << "\"},\"timestamp\":" << now << "}\n";
                        logFile.flush();
                        logFile.close();
                    }
                }
                // #endregion
                ofLogError("Engine") << "Unknown exception in JSON iteration for module " << name;
            }
            
            // Extract connection-based parameters from connections array (VideoOutput, AudioOutput)
            if (moduleSnapshot.contains("connections") && moduleSnapshot["connections"].is_array()) {
                const auto& connections = moduleSnapshot["connections"];
                
                // #region agent log
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        int connCount = connections.size();
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"F\",\"location\":\"Engine.cpp:712\",\"message\":\"processing connections array\",\"data\":{\"moduleName\":\"" << name << "\",\"connectionCount\":" << connCount << "},\"timestamp\":" << now << "}\n";
                        logFile.close();
                    }
                }
                // #endregion
                
                for (size_t i = 0; i < connections.size(); ++i) {
                    const auto& conn = connections[i];
                    if (conn.is_object()) {
                        // VideoOutput: extract opacity from each connection
                        if (conn.contains("opacity") && conn["opacity"].is_number()) {
                            std::string paramName = "connectionOpacity_" + std::to_string(i);
                            float opacity = conn["opacity"].get<float>();
                            moduleState.parameters[paramName] = opacity;
                            
                            // #region agent log
                            {
                                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                                if (logFile.is_open()) {
                                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"F\",\"location\":\"Engine.cpp:720\",\"message\":\"extracted connectionOpacity\",\"data\":{\"moduleName\":\"" << name << "\",\"paramName\":\"" << paramName << "\",\"opacity\":" << opacity << ",\"index\":" << i << "},\"timestamp\":" << now << "}\n";
                                    logFile.close();
                                }
                            }
                            // #endregion
                            
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
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"BEFORE getParameters()\",\"data\":{\"moduleName\":\"" << name << "\"},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        std::vector<ParameterDescriptor> params;
        try {
            params = module->getParameters();
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"AFTER getParameters() - SUCCESS\",\"data\":{\"moduleName\":\"" << name << "\",\"paramCount\":" << params.size() << "},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
        } catch (const std::exception& e) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"EXCEPTION in getParameters() - CRASH POINT\",\"data\":{\"moduleName\":\"" << name << "\",\"moduleType\":\"" << moduleType << "\",\"error\":\"" << e.what() << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            ofLogError("Engine") << "Exception in getParameters() for module " << name << " (" << moduleType << "): " << e.what();
            params.clear();  // Use empty params on error
        } catch (...) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"UNKNOWN EXCEPTION in getParameters() - CRASH POINT\",\"data\":{\"moduleName\":\"" << name << "\",\"moduleType\":\"" << moduleType << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            ofLogError("Engine") << "Unknown exception in getParameters() for module " << name << " (" << moduleType << ")";
            params.clear();  // Use empty params on error
        }
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"BEFORE parameter iteration loop\",\"data\":{\"moduleName\":\"" << name << "\",\"paramCount\":" << params.size() << "},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        for (const auto& param : params) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"Processing parameter\",\"data\":{\"moduleName\":\"" << name << "\",\"paramName\":\"" << param.name << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            
            // Skip if already extracted from JSON
            if (moduleState.parameters.count(param.name) > 0) {
                // #region agent log
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"Parameter already in moduleState, skipping\",\"data\":{\"moduleName\":\"" << name << "\",\"paramName\":\"" << param.name << "\"},\"timestamp\":" << now << "}\n";
                        logFile.flush();
                        logFile.close();
                    }
                }
                // #endregion
                continue;
            }
            
            // Skip connection-based parameters (already extracted from JSON connections array)
            if (param.name.find("connectionOpacity_") == 0 || param.name.find("connectionVolume_") == 0) {
                // #region agent log
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"Parameter is connection-based, skipping\",\"data\":{\"moduleName\":\"" << name << "\",\"paramName\":\"" << param.name << "\"},\"timestamp\":" << now << "}\n";
                        logFile.flush();
                        logFile.close();
                    }
                }
                // #endregion
                continue;
            }
            
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"BEFORE getParameter() call\",\"data\":{\"moduleName\":\"" << name << "\",\"paramName\":\"" << param.name << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            try {
                float paramValue = module->getParameter(param.name);
                // #region agent log
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"AFTER getParameter() - SUCCESS\",\"data\":{\"moduleName\":\"" << name << "\",\"paramName\":\"" << param.name << "\",\"paramValue\":" << paramValue << "},\"timestamp\":" << now << "}\n";
                        logFile.flush();
                        logFile.close();
                    }
                }
                // #endregion
                moduleState.parameters[param.name] = paramValue;
                ofLogVerbose("Engine") << "[STATE_SYNC] Fallback: captured " << name << "::" << param.name << " = " << paramValue << " from getParameter()";
            } catch (const std::exception& e) {
                // #region agent log
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"EXCEPTION in getParameter() - CRASH POINT\",\"data\":{\"moduleName\":\"" << name << "\",\"moduleType\":\"" << moduleType << "\",\"paramName\":\"" << param.name << "\",\"error\":\"" << e.what() << "\"},\"timestamp\":" << now << "}\n";
                        logFile.flush();
                        logFile.close();
                    }
                }
                // #endregion
                ofLogWarning("Engine") << "Error getting parameter '" << param.name << "' from module '" << name << " (" << moduleType << ")': " << e.what();
            } catch (...) {
                // #region agent log
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"UNKNOWN EXCEPTION in getParameter() - CRASH POINT\",\"data\":{\"moduleName\":\"" << name << "\",\"moduleType\":\"" << moduleType << "\",\"paramName\":\"" << param.name << "\"},\"timestamp\":" << now << "}\n";
                        logFile.flush();
                        logFile.close();
                    }
                }
                // #endregion
                ofLogWarning("Engine") << "Unknown error getting parameter '" << param.name << "' from module '" << name << " (" << moduleType << ")'";
            }
        }
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"AFTER parameter iteration loop - SUCCESS\",\"data\":{\"moduleName\":\"" << name << "\"},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        
        // SIMPLIFIED: Store module snapshot JSON directly (no variant type checking)
        // Modules control their own serialization, Engine just stores it
        // This eliminates variant complexity and makes serialization straightforward
        moduleState.typeSpecificData = moduleSnapshot;
        
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"BEFORE storing moduleState in state.modules\",\"data\":{\"moduleName\":\"" << name << "\",\"paramCount\":" << moduleState.parameters.size() << "},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        try {
            state.modules[name] = moduleState;
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"AFTER storing moduleState - SUCCESS\",\"data\":{\"moduleName\":\"" << name << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
        } catch (const std::exception& e) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"EXCEPTION storing moduleState\",\"data\":{\"moduleName\":\"" << name << "\",\"error\":\"" << e.what() << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            ofLogError("Engine") << "Exception storing module state for " << name << ": " << e.what();
        } catch (...) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"UNKNOWN EXCEPTION storing moduleState\",\"data\":{\"moduleName\":\"" << name << "\"},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            ofLogError("Engine") << "Unknown exception storing module state for " << name;
        }
    });
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H\",\"location\":\"Engine.cpp:buildModuleStates\",\"message\":\"buildModuleStates() EXIT - SUCCESS\",\"data\":{\"moduleCount\":" << state.modules.size() << "},\"timestamp\":" << now << "}\n";
            logFile.flush();
            logFile.close();
        }
    }
    // #endregion
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

void Engine::update(float deltaTime) {
    // CRITICAL FIX: Check if state needs notification (set by audio thread or parameter changes)
    // This ensures state notifications happen on main thread, not audio thread
    // Prevents thread safety issues with buildStateSnapshot() and crashes
    if (stateNeedsNotification_.load()) {
        // CRITICAL FIX: Only defer if commands are actively processing
        // If commands are done, state is stable and we can notify (even if script is executing)
        // Commands are processed atomically, so state is stable after commands are done
        if (commandsBeingProcessed_.load()) {
            // Commands still processing - defer
            stateNeedsNotification_.store(true);
        } else {
            // Commands are done - safe to notify
            // Note: Script might still be executing, but that's OK - commands are done, state is stable
            stateNeedsNotification_.store(false);
            notifyStateChange();
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
    scriptManager_.update();
    
    // Update all modules
    moduleRegistry_.forEachModule([&](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
        if (module) {
            try {
                module->update();
            } catch (const std::exception& e) {
                ofLogError("Engine") << "Error updating module '" << name << "': " << e.what();
            }
        }
    });
}

bool Engine::loadSession(const std::string& path) {
    bool result = sessionManager_.loadSession(path);
    if (result) {
        notifyStateChange();
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
    commandsBeingProcessed_.store(true);
    
    int processed = 0;
    std::unique_ptr<Command> cmd;
    
    // Process all queued commands (lock-free, called from audio thread)
    while (commandQueue_.try_dequeue(cmd)) {
        try {
            cmd->execute(*this);
            commandStats_.commandsProcessed++;
            processed++;
            
        } catch (const std::exception& e) {
            ofLogError("Engine") << "Command execution failed: " << e.what()
                                << " (" << cmd->describe() << ")";
        }
    }
    
    // Clear flag after all commands are processed
    commandsBeingProcessed_.store(false);
    
    // CRITICAL FIX: Don't call notifyStateChange() from audio thread
    // Set flag instead - main thread will check and notify in update()
    // This prevents thread safety issues with buildStateSnapshot()
    if (processed > 0) {
        stateNeedsNotification_.store(true);
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
        notifyStateChange();
    } catch (const std::exception& e) {
        ofLogError("Engine") << "Immediate command execution failed: " << e.what()
                            << " (" << cmd->describe() << ")";
    }
}

} // namespace vt

