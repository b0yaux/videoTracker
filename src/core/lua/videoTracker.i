/*
 * videoTracker SWIG Interface File
 * 
 * This file defines the bindings between C++ classes and Lua
 * for the videoTracker project.
 */

%module videoTracker

%{
#include "core/Engine.h"
#include "core/Command.h"
#include "utils/Clock.h"
#include "core/ModuleRegistry.h"
#include "modules/Module.h"
#include "core/ConnectionManager.h"
#include "core/ParameterRouter.h"
#include "core/PatternRuntime.h"
#include "data/Pattern.h"
#include "core/lua/LuaHelpers.h"
#include "core/lua/LuaGlobals.h"
#include "ofLog.h"
#include <memory>
#include <sstream>
#include <map>
#include <string>
#include "lua.h"
#include "lauxlib.h"

// Function to register engine instance as global 'engine' variable
// This will be called from Engine::setupLua() after SWIG module is loaded
void registerEngineGlobal(lua_State* L) {
    auto* engine = vt::lua::getGlobalEngine();
    if (engine && L) {
        // Create SWIG wrapper for Engine
        SWIG_NewPointerObj(L, engine, SWIGTYPE_p_vt__Engine, 0);
        lua_setglobal(L, "engine");
    }
}
%}

// Enable smart pointer support
%include <std_shared_ptr.i>
%include <std_string.i>
%include <std_vector.i>
%include <std_map.i>

// Typemap for Lua table -> std::map<std::string, std::string> (for config parameters)
%typemap(in) std::map<std::string, std::string> (std::map<std::string, std::string> temp) {
    if (lua_istable(L, $input)) {
        // Save stack state
        int top = lua_gettop(L);
        
        // Push nil to start iteration
        lua_pushnil(L);
        
        int iterationCount = 0;
        const int MAX_ITERATIONS = 1000; // Prevent infinite loops
        
        // Safely iterate through table
        while (lua_next(L, $input) != 0 && iterationCount < MAX_ITERATIONS) {
            iterationCount++;
            
            // Check stack integrity
            if (lua_gettop(L) < 2) {
                // Stack corrupted, break
                lua_pop(L, lua_gettop(L) - top); // Restore stack
                break;
            }
            
            // Key is at -2, value is at -1
            if (lua_isstring(L, -2)) {
                const char* keyStr = lua_tostring(L, -2);
                if (keyStr) {
                    std::string key = keyStr;
                    std::string value;
                    
                    // Safely extract value based on type
                    if (lua_isnumber(L, -1)) {
                        value = std::to_string(lua_tonumber(L, -1));
                    } else if (lua_isboolean(L, -1)) {
                        value = lua_toboolean(L, -1) ? "1" : "0";
                    } else if (lua_isstring(L, -1)) {
                        const char* valueStr = lua_tostring(L, -1);
                        if (valueStr) {
                            value = valueStr;
                        }
                    }
                    // Only add non-empty keys
                    if (!key.empty()) {
                        temp[key] = value;
                    }
                }
            }
            
            // Remove value, keep key for next iteration
            lua_pop(L, 1);
        }
        
        // Ensure stack is restored
        lua_settop(L, top);
    }
    $1 = temp;
}

%typemap(typecheck) std::map<std::string, std::string> {
    $1 = lua_istable(L, $input) ? 1 : 0;
}

// Shared pointer templates
%shared_ptr(vt::Engine)
%shared_ptr(Module)
%shared_ptr(Clock)

// Namespace handling
%nspace vt::Engine;

// Include headers
%include "core/Engine.h"
%include "utils/Clock.h"
%include "core/ModuleRegistry.h"
%include "modules/Module.h"

// Extend Engine to expose key methods
%extend vt::Engine {
    Clock* getClock() {
        return &$self->getClock();
    }
    
    ModuleRegistry* getModuleRegistry() {
        return &$self->getModuleRegistry();
    }
    
    ConnectionManager* getConnectionManager() {
        return &$self->getConnectionManager();
    }
    
    ParameterRouter* getParameterRouter() {
        return &$self->getParameterRouter();
    }
    
    PatternRuntime* getPatternRuntime() {
        return &$self->getPatternRuntime();
    }
    
    /**
     * Register Lua callback for state changes (Phase 6.3).
     * Usage: local id = engine:onStateChange(function(state) ... end)
     * @param callback Lua function that receives state table
     * @return Callback ID for unregistration
     */
    size_t onStateChange(lua_State* L) {
        return engineOnStateChange(L, $self);
    }
    
    /**
     * Unregister a previously registered callback (Phase 6.3).
     * Usage: engine:removeStateChangeCallback(id)
     * @param callbackId ID returned from onStateChange
     * @return true if successful
     */
    bool removeStateChangeCallback(size_t callbackId) {
        return engineRemoveStateChangeCallback(callbackId, $self);
    }
}

// Extend ModuleRegistry to expose getModule
%extend ModuleRegistry {
    std::shared_ptr<Module> getModule(const std::string& identifier) {
        return $self->getModule(identifier);
    }
    
    std::vector<std::string> getAllModuleNames() {
        return $self->getAllModuleNames();
    }
}

// Extend Module to expose key methods
%extend Module {
    void setParameter(const std::string& paramName, float value) {
        $self->setParameter(paramName, value, true);
    }
    
    float getParameter(const std::string& paramName) {
        return $self->getParameter(paramName);
    }
    
    std::vector<ParameterDescriptor> getParameters() {
        return $self->getParameters();
    }
    
    std::string getName() {
        return $self->getName();
    }
    
    std::string getInstanceName() {
        return $self->getInstanceName();
    }
}

// Extend Clock to expose key methods
%extend Clock {
    void setBPM(float bpm) {
        // Route BPM changes through command queue for thread safety
        auto* engine = vt::lua::getGlobalEngine();
        if (engine) {
            auto cmd = std::make_unique<vt::SetBPMCommand>(bpm);
            if (!engine->enqueueCommand(std::move(cmd))) {
                // Fallback: execute immediately if queue is full (ensures state notifications)
                ofLogWarning("Clock") << "Command queue full, executing SetBPMCommand immediately";
                auto fallbackCmd = std::make_unique<vt::SetBPMCommand>(bpm);
                engine->executeCommandImmediate(std::move(fallbackCmd));
            }
        } else {
            // Fallback to direct call if engine not available
            $self->setBPM(bpm);
        }
    }
    
    float getBPM() {
        return $self->getBPM();
    }
    
    void start() {
        // CRITICAL FIX: Route through command queue instead of direct call
        // This ensures thread safety and prevents crashes during script execution
        // All state changes must go through command queue (Phase 6 requirement)
        auto* engine = vt::lua::getGlobalEngine();
        if (engine) {
            auto cmd = std::make_unique<vt::StartTransportCommand>();
            if (!engine->enqueueCommand(std::move(cmd))) {
                // Fallback: execute immediately if queue is full (ensures state notifications)
                ofLogWarning("Clock") << "Command queue full, executing StartTransportCommand immediately";
                auto fallbackCmd = std::make_unique<vt::StartTransportCommand>();
                engine->executeCommandImmediate(std::move(fallbackCmd));
            }
        } else {
            // Fallback to direct call if engine not available (shouldn't happen in normal operation)
            ofLogWarning("Clock") << "Engine not available, using direct start() call";
            $self->start();
        }
    }
    
    void play() {
        // Route through command queue like start() does
        auto* engine = vt::lua::getGlobalEngine();
        if (engine) {
            auto cmd = std::make_unique<vt::StartTransportCommand>();
            if (!engine->enqueueCommand(std::move(cmd))) {
                ofLogWarning("Clock") << "Command queue full, executing StartTransportCommand immediately";
                auto fallbackCmd = std::make_unique<vt::StartTransportCommand>();
                engine->executeCommandImmediate(std::move(fallbackCmd));
            }
        } else {
            ofLogWarning("Clock") << "Engine not available, using direct start() call";
            $self->start();
        }
    }
    
    void stop() {
        // CRITICAL FIX: Route through command queue instead of direct call
        // This ensures thread safety and prevents crashes during script execution
        // All state changes must go through command queue (Phase 6 requirement)
        auto* engine = vt::lua::getGlobalEngine();
        if (engine) {
            auto cmd = std::make_unique<vt::StopTransportCommand>();
            if (!engine->enqueueCommand(std::move(cmd))) {
                // Fallback: execute immediately if queue is full (ensures state notifications)
                ofLogWarning("Clock") << "Command queue full, executing StopTransportCommand immediately";
                auto fallbackCmd = std::make_unique<vt::StopTransportCommand>();
                engine->executeCommandImmediate(std::move(fallbackCmd));
            }
        } else {
            // Fallback to direct call if engine not available (shouldn't happen in normal operation)
            ofLogWarning("Clock") << "Engine not available, using direct stop() call";
            $self->stop();
        }
    }
    
    void pause() {
        // Route through command queue like stop() does
        auto* engine = vt::lua::getGlobalEngine();
        if (engine) {
            auto cmd = std::make_unique<vt::StopTransportCommand>();
            if (!engine->enqueueCommand(std::move(cmd))) {
                ofLogWarning("Clock") << "Command queue full, executing StopTransportCommand immediately";
                auto fallbackCmd = std::make_unique<vt::StopTransportCommand>();
                engine->executeCommandImmediate(std::move(fallbackCmd));
            }
        } else {
            ofLogWarning("Clock") << "Engine not available, using direct stop() call";
            $self->stop();
        }
    }
    
    bool isPlaying() {
        return $self->isPlaying();
    }
}

// Helper function to get engine instance (for scripts that need it)
%inline %{
// Get the global engine instance
// This allows scripts to access engine:getClock(), etc.
vt::Engine* getEngine() {
    return vt::lua::getGlobalEngine();
}

// Execute command directly (for engine wrapper)
// This allows the Lua wrapper to call Engine::executeCommand without SWIG bindings
Engine::Result executeCommandNative(const std::string& command) {
    auto* engine = vt::lua::getGlobalEngine();
    if (!engine) {
        return Engine::Result(false, "Engine not available", "Engine pointer is null");
    }
    return engine->executeCommand(command);
}
%}

// High-level declarative helper functions for live-coding
%inline %{
// Create a MultiSampler module with declarative syntax
// Usage: sampler("mySampler") or sampler("mySampler", {volume=0.8, speed=1.2})
// Config table is optional - only non-default values need to be set
std::string sampler(const std::string& name, const std::map<std::string, std::string>& config = {}) {
    auto* engine = vt::lua::getGlobalEngine();
    if (!engine) {
        return "";
    }
    vt::lua::LuaHelpers helpers(engine);
    return helpers.createSampler(name, config);
}

// Create a TrackerSequencer module
// Usage: sequencer("mySeq") or sequencer("mySeq", {param=value})
std::string sequencer(const std::string& name, const std::map<std::string, std::string>& config = {}) {
    auto* engine = vt::lua::getGlobalEngine();
    if (!engine) {
        return "";
    }
    vt::lua::LuaHelpers helpers(engine);
    return helpers.createSequencer(name, config);
}

// Connect two modules
bool connect(const std::string& source, const std::string& target, const std::string& type = "audio") {
    auto* engine = vt::lua::getGlobalEngine();
    if (!engine) {
        return false;
    }
    vt::lua::LuaHelpers helpers(engine);
    return helpers.connect(source, target, type);
}

// Set a module parameter directly (bypasses executeCommand for performance)
bool setParam(const std::string& moduleName, const std::string& paramName, float value) {
    auto* engine = vt::lua::getGlobalEngine();
    if (!engine) {
        return false;
    }
    vt::lua::LuaHelpers helpers(engine);
    std::ostringstream oss;
    oss << value;
    return helpers.setParameter(moduleName, paramName, oss.str());
}

// Get a module parameter value
float getParam(const std::string& moduleName, const std::string& paramName) {
    auto* engine = vt::lua::getGlobalEngine();
    if (!engine) {
        return 0.0f;
    }
    vt::lua::LuaHelpers helpers(engine);
    std::string valueStr = helpers.getParameter(moduleName, paramName);
    if (valueStr.empty()) {
        return 0.0f;
    }
    try {
        return std::stof(valueStr);
    } catch (...) {
        return 0.0f;
    }
}

// ═══════════════════════════════════════════════════════════
// LUA CALLBACK API (Phase 6.3 - Reactive Sync for Live Coding)
// ═══════════════════════════════════════════════════════════

/**
 * Register a Lua callback for state changes.
 * Usage:
 *   local callbackId = engine:onStateChange(function(state)
 *       print("BPM: " .. state.clock.bpm)
 *       if state.modules.kick then
 *           print("Kick volume: " .. state.modules.kick.parameters.volume)
 *       end
 *   end)
 * 
 * @param callback Lua function that receives EngineState table
 * @return Integer callback ID for use with removeStateChangeCallback
 */
size_t engineOnStateChange(lua_State* L, vt::Engine* engine) {
    if (!lua_isfunction(L, 1)) {
        lua_pushinteger(L, 0);
        return 1;
    }
    
    // Stack: [function]
    lua_pushvalue(L, 1);  // Push function reference
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);  // Store in registry, ref on stack
    
    // Create callback that will dereference and call the Lua function
    auto callback = [ref, engine](lua_State* L, const vt::EngineState& state) {
        // Get function from registry
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            return;
        }
        
        // Push EngineState as table
        lua_newtable(L);
        
        // Add clock info
        lua_pushnumber(L, state.clock.bpm);
        lua_setfield(L, -2, "bpm");
        
        // Add modules table
        lua_newtable(L);
        
        // For each module, add its parameters
        for (const auto& [name, moduleState] : state.modules) {
            lua_pushstring(L, name.c_str());
            lua_newtable(L);
            
            lua_pushnumber(L, moduleState.parameters.volume);
            lua_setfield(L, -2, "volume");
            
            // Add more parameters as needed
            lua_settable(L, -3);
        }
        lua_setfield(L, -2, "modules");
        
        // Call the function with state table as argument
        // pcall: pop function + 1 arg, push 0 or error
        if (lua_pcall(L, 1, 0, 0) != 0) {
            ofLogError("Engine") << "Lua callback error: " << lua_tostring(L, -1);
            lua_pop(L, 1);
        }
    };
    
    size_t callbackId = engine->registerStateChangeCallback(callback);
    
    if (callbackId == 0) {
        // Registration failed, release the Lua reference
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        lua_pushinteger(L, 0);
    } else {
        lua_pushinteger(L, callbackId);
    }
    
    return 1;
}

/**
 * Remove a previously registered state change callback.
 * Usage:
 *   engine:removeStateChangeCallback(callbackId)
 * 
 * @param callbackId Integer ID returned from onStateChange
 * @return true if removed, false if not found
 */
bool engineRemoveStateChangeCallback(size_t callbackId, vt::Engine* engine) {
    return engine->unregisterStateChangeCallback(callbackId);
}

// System module helper functions (for live-coding syntax)
// These configure existing system modules (they already exist, we just set parameters)

// Configure AudioOutput module
// Usage: audioOut("masterAudioOut") or audioOut("masterAudioOut", {volume=0.8, connectionVolume_0=0.5})
std::string audioOut(const std::string& name, const std::map<std::string, std::string>& config = {}) {
    auto* engine = vt::lua::getGlobalEngine();
    if (!engine) {
        return "";
    }
    vt::lua::LuaHelpers helpers(engine);
    return helpers.createSystemModule("AudioOutput", name, config);
}

// Configure VideoOutput module
// Usage: videoOut("masterVideoOut") or videoOut("masterVideoOut", {masterOpacity=0.8, connectionOpacity_0=0.5})
std::string videoOut(const std::string& name, const std::map<std::string, std::string>& config = {}) {
    try {
        auto* engine = vt::lua::getGlobalEngine();
        if (!engine) {
            return "";
        }
        vt::lua::LuaHelpers helpers(engine);
        return helpers.createSystemModule("VideoOutput", name, config);
    } catch (const std::exception& e) {
        // Return empty string on error (Lua will handle it)
        return "";
    } catch (...) {
        // Return empty string on unknown error
        return "";
    }
}

// Configure Oscilloscope module
// Usage: oscilloscope("masterOscilloscope") or oscilloscope("masterOscilloscope", {param=value})
std::string oscilloscope(const std::string& name, const std::map<std::string, std::string>& config = {}) {
    auto* engine = vt::lua::getGlobalEngine();
    if (!engine) {
        return "";
    }
    vt::lua::LuaHelpers helpers(engine);
    return helpers.createSystemModule("Oscilloscope", name, config);
}

// Configure Spectrogram module
// Usage: spectrogram("masterSpectrogram") or spectrogram("masterSpectrogram", {param=value})
std::string spectrogram(const std::string& name, const std::map<std::string, std::string>& config = {}) {
    auto* engine = vt::lua::getGlobalEngine();
    if (!engine) {
        return "";
    }
    vt::lua::LuaHelpers helpers(engine);
    return helpers.createSystemModule("Spectrogram", name, config);
}

// Create a pattern with declarative syntax (IDEMPOTENT for live-coding)
// Usage: pattern("P0", 16) - creates pattern P0 with 16 steps, or updates if exists
std::string pattern(const std::string& name, int steps = 16) {
    auto* engine = vt::lua::getGlobalEngine();
    if (!engine) {
        return "";
    }
    
    // IDEMPOTENT: Check if pattern already exists
    auto* patternRuntime = engine->getPatternRuntime();
    if (patternRuntime && patternRuntime->patternExists(name)) {
        // Pattern exists - update if step count changed
        int currentStepCount = patternRuntime->getPatternStepCount(name);
        if (currentStepCount != steps) {
            // Step count changed - create new Pattern and update
            Pattern updatedPattern(steps);
            patternRuntime->updatePattern(name, updatedPattern);
        }
        // Return pattern name (idempotent - can be called multiple times safely)
        return name;
    }
    
    // Pattern doesn't exist - create it via command
    std::string command = "pattern create " + name + " " + std::to_string(steps);
    auto result = engine->executeCommand(command);
    if (result.success) {
        return name;
    }
    return "";
}
%}
