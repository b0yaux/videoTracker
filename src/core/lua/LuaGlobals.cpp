#include "LuaGlobals.h"
#include "core/Engine.h"
#include "utils/Clock.h"
#include "core/ModuleRegistry.h"
#include "core/ConnectionManager.h"
#include "core/ParameterRouter.h"
#include "core/PatternRuntime.h"
#include <lua.hpp>
#include <string>

namespace vt {
namespace lua {

// Global engine pointer for helper functions
static Engine* g_engine = nullptr;

void setGlobalEngine(Engine* engine) {
    g_engine = engine;
}

Engine* getGlobalEngine() {
    return g_engine;
}

// ═══════════════════════════════════════════════════════════
// CLOCK LUA BINDINGS
// ═══════════════════════════════════════════════════════════

// Lua C function: Clock:setBPM(float)
static int lua_clock_setBPM(lua_State* L) {
    void* userdata = lua_touserdata(L, 1);
    if (!userdata) {
        return luaL_error(L, "Invalid clock userdata");
    }
    Clock* clock = *static_cast<Clock**>(userdata);
    if (!clock) {
        return luaL_error(L, "Clock has been destroyed");
    }
    if (lua_isnumber(L, 2)) {
        float bpm = static_cast<float>(lua_tonumber(L, 2));
        clock->setBPM(bpm);
        return 0;
    }
    return luaL_error(L, "Invalid arguments to setBPM");
}

// Lua C function: Clock:start()
static int lua_clock_start(lua_State* L) {
    void* userdata = lua_touserdata(L, 1);
    if (!userdata) {
        return luaL_error(L, "Invalid clock userdata");
    }
    Clock* clock = *static_cast<Clock**>(userdata);
    if (!clock) {
        return luaL_error(L, "Clock has been destroyed");
    }
    clock->start();
    return 0;
}

// Lua C function: Clock:stop()
static int lua_clock_stop(lua_State* L) {
    void* userdata = lua_touserdata(L, 1);
    if (!userdata) {
        return luaL_error(L, "Invalid clock userdata");
    }
    Clock* clock = *static_cast<Clock**>(userdata);
    if (!clock) {
        return luaL_error(L, "Clock has been destroyed");
    }
    clock->stop();
    return 0;
}

// Lua C function: Clock:pause()
static int lua_clock_pause(lua_State* L) {
    void* userdata = lua_touserdata(L, 1);
    if (!userdata) {
        return luaL_error(L, "Invalid clock userdata");
    }
    Clock* clock = *static_cast<Clock**>(userdata);
    if (!clock) {
        return luaL_error(L, "Clock has been destroyed");
    }
    clock->pause();
    return 0;
}

// Lua C function: Clock:reset()
static int lua_clock_reset(lua_State* L) {
    void* userdata = lua_touserdata(L, 1);
    if (!userdata) {
        return luaL_error(L, "Invalid clock userdata");
    }
    Clock* clock = *static_cast<Clock**>(userdata);
    if (!clock) {
        return luaL_error(L, "Clock has been destroyed");
    }
    clock->reset();
    return 0;
}

// Lua C function: Clock:isPlaying() -> boolean
static int lua_clock_isPlaying(lua_State* L) {
    void* userdata = lua_touserdata(L, 1);
    if (!userdata) {
        return luaL_error(L, "Invalid clock userdata");
    }
    Clock* clock = *static_cast<Clock**>(userdata);
    if (!clock) {
        return luaL_error(L, "Clock has been destroyed");
    }
    lua_pushboolean(L, clock->isPlaying());
    return 1;
}

// Lua C function: Clock:getBPM() -> float
static int lua_clock_getBPM(lua_State* L) {
    void* userdata = lua_touserdata(L, 1);
    if (!userdata) {
        return luaL_error(L, "Invalid clock userdata");
    }
    Clock* clock = *static_cast<Clock**>(userdata);
    if (!clock) {
        return luaL_error(L, "Clock has been destroyed");
    }
    lua_pushnumber(L, clock->getBPM());
    return 1;
}

// Register Clock metatable with all methods
static void registerClockMetatable(lua_State* L) {
    luaL_newmetatable(L, "vt_Clock");

    // __index function - returns methods when field not found in userdata
    lua_pushcfunction(L, [](lua_State* L) -> int {
        // First arg is userdata, second is method name
        lua_getmetatable(L, 1);
        lua_getfield(L, -1, lua_tostring(L, 2));
        return 1;
    });
    lua_setfield(L, -2, "__index");

    // Register methods
    lua_pushcfunction(L, lua_clock_setBPM);
    lua_setfield(L, -2, "setBPM");

    lua_pushcfunction(L, lua_clock_start);
    lua_setfield(L, -2, "start");

    lua_pushcfunction(L, lua_clock_stop);
    lua_setfield(L, -2, "stop");

    lua_pushcfunction(L, lua_clock_pause);
    lua_setfield(L, -2, "pause");

    lua_pushcfunction(L, lua_clock_reset);
    lua_setfield(L, -2, "reset");

    lua_pushcfunction(L, lua_clock_isPlaying);
    lua_setfield(L, -2, "isPlaying");

    lua_pushcfunction(L, lua_clock_getBPM);
    lua_setfield(L, -2, "getBPM");

    lua_pop(L, 1);  // Pop metatable
}

// Lua C function to get clock from engine userdata
static int lua_engine_getClock(lua_State* L) {
    void* userdata = lua_touserdata(L, 1);
    if (!userdata) {
        return luaL_error(L, "Invalid engine userdata");
    }
    Engine* engine = *static_cast<Engine**>(userdata);
    if (!engine) {
        return luaL_error(L, "Engine has been destroyed");
    }
    // Create userdata for Clock pointer with proper metatable
    Clock* clock = &engine->getClock();
        Clock** clockPtr = static_cast<Clock**>(lua_newuserdata(L, sizeof(Clock*)));
        *clockPtr = clock;

        // Set the metatable for this userdata
        luaL_getmetatable(L, "vt_Clock");
        lua_setmetatable(L, -2);

        return 1;
}

// Lua C function to execute command via engine
static int lua_engine_executeCommand(lua_State* L) {
    void* userdata = lua_touserdata(L, 1);
    if (!userdata) {
        return luaL_error(L, "Invalid engine userdata");
    }
    Engine* engine = *static_cast<Engine**>(userdata);
    if (!engine) {
        return luaL_error(L, "Engine has been destroyed");
    }
    if (lua_isstring(L, 2)) {
        const char* cmd = lua_tostring(L, 2);
        auto result = engine->executeCommand(std::string(cmd));
        // Push result table
        lua_newtable(L);
        lua_pushboolean(L, result.success);
        lua_setfield(L, -2, "success");
        lua_pushstring(L, result.message.c_str());
        lua_setfield(L, -2, "message");
        lua_pushstring(L, result.error.c_str());
        lua_setfield(L, -2, "error");
        return 1;
    }
    // Return error - command argument is not a string
    lua_newtable(L);
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "success");
    lua_pushstring(L, "Command must be a string");
    lua_setfield(L, -2, "error");
    return 1;
}

// Lua C function: audioOut(name) or audioOut(name, configTable)
// Creates or configures an audio output module
static int lua_audioOut(lua_State* L) {
    if (!g_engine) {
        return luaL_error(L, "Engine not available");
    }
    
    const char* name = luaL_checkstring(L, 1);
    std::string moduleName(name);
    
    // Check if module exists
    auto& registry = g_engine->getModuleRegistry();
    auto module = registry.getModule(moduleName);
    
    if (!module) {
        // Module doesn't exist - try to create it via command
        std::string cmd = "add AudioOutput " + moduleName;
        g_engine->executeCommand(cmd);
    }
    
    // If config table is provided, apply parameters
    if (lua_istable(L, 2)) {
        lua_pushnil(L);  // First key
        while (lua_next(L, 2) != 0) {
            const char* key = lua_tostring(L, -2);
            float value = static_cast<float>(lua_tonumber(L, -1));
            std::string cmd = "set " + moduleName + " " + key + " " + std::to_string(value);
            g_engine->executeCommand(cmd);
            lua_pop(L, 1);  // Remove value, keep key for next iteration
        }
    }
    
    // Return module name (idempotent - can be called multiple times safely)
    lua_pushstring(L, moduleName.c_str());
    return 1;
}

// Lua C function: videoOut(name) or videoOut(name, configTable)
// Creates or configures a video output module
static int lua_videoOut(lua_State* L) {
    if (!g_engine) {
        return luaL_error(L, "Engine not available");
    }
    
    const char* name = luaL_checkstring(L, 1);
    std::string moduleName(name);
    
    // Check if module exists
    auto& registry = g_engine->getModuleRegistry();
    auto module = registry.getModule(moduleName);
    
    if (!module) {
        // Module doesn't exist - try to create it via command
        std::string cmd = "add VideoOutput " + moduleName;
        g_engine->executeCommand(cmd);
    }
    
    // If config table is provided, apply parameters
    if (lua_istable(L, 2)) {
        lua_pushnil(L);  // First key
        while (lua_next(L, 2) != 0) {
            const char* key = lua_tostring(L, -2);
            float value = static_cast<float>(lua_tonumber(L, -1));
            std::string cmd = "set " + moduleName + " " + key + " " + std::to_string(value);
            g_engine->executeCommand(cmd);
            lua_pop(L, 1);  // Remove value, keep key for next iteration
        }
    }
    
    // Return module name
    lua_pushstring(L, moduleName.c_str());
    return 1;
}

// Lua C function: oscilloscope(name) or oscilloscope(name, configTable)
// Creates or configures an oscilloscope module
static int lua_oscilloscope(lua_State* L) {
    if (!g_engine) {
        return luaL_error(L, "Engine not available");
    }
    
    const char* name = luaL_checkstring(L, 1);
    std::string moduleName(name);
    
    // Check if module exists
    auto& registry = g_engine->getModuleRegistry();
    auto module = registry.getModule(moduleName);
    
    if (!module) {
        // Module doesn't exist - try to create it via command
        std::string cmd = "add Oscilloscope " + moduleName;
        g_engine->executeCommand(cmd);
    }
    
    // If config table is provided, apply parameters
    if (lua_istable(L, 2)) {
        lua_pushnil(L);
        while (lua_next(L, 2) != 0) {
            const char* key = lua_tostring(L, -2);
            float value = static_cast<float>(lua_tonumber(L, -1));
            std::string cmd = "set " + moduleName + " " + key + " " + std::to_string(value);
            g_engine->executeCommand(cmd);
            lua_pop(L, 1);
        }
    }
    
    lua_pushstring(L, moduleName.c_str());
    return 1;
}

// Lua C function: spectrogram(name) or spectrogram(name, configTable)
// Creates or configures a spectrogram module
static int lua_spectrogram(lua_State* L) {
    if (!g_engine) {
        return luaL_error(L, "Engine not available");
    }
    
    const char* name = luaL_checkstring(L, 1);
    std::string moduleName(name);
    
    // Check if module exists
    auto& registry = g_engine->getModuleRegistry();
    auto module = registry.getModule(moduleName);
    
    if (!module) {
        std::string cmd = "add Spectrogram " + moduleName;
        g_engine->executeCommand(cmd);
    }
    
    // If config table is provided, apply parameters
    if (lua_istable(L, 2)) {
        lua_pushnil(L);
        while (lua_next(L, 2) != 0) {
            const char* key = lua_tostring(L, -2);
            float value = static_cast<float>(lua_tonumber(L, -1));
            std::string cmd = "set " + moduleName + " " + key + " " + std::to_string(value);
            g_engine->executeCommand(cmd);
            lua_pop(L, 1);
        }
    }
    
    lua_pushstring(L, moduleName.c_str());
    return 1;
}

// Lua C function: sampler(name) or sampler(name, configTable)
// Creates or configures a MultiSampler module
static int lua_sampler(lua_State* L) {
    if (!g_engine) {
        return luaL_error(L, "Engine not available");
    }
    
    const char* name = luaL_checkstring(L, 1);
    std::string moduleName(name);
    
    // Check if module exists
    auto& registry = g_engine->getModuleRegistry();
    auto module = registry.getModule(moduleName);
    
    if (!module) {
        std::string cmd = "add MultiSampler " + moduleName;
        g_engine->executeCommand(cmd);
    }
    
    // If config table is provided, apply parameters
    if (lua_istable(L, 2)) {
        lua_pushnil(L);
        while (lua_next(L, 2) != 0) {
            const char* key = lua_tostring(L, -2);
            float value = static_cast<float>(lua_tonumber(L, -1));
            std::string cmd = "set " + moduleName + " " + key + " " + std::to_string(value);
            g_engine->executeCommand(cmd);
            lua_pop(L, 1);
        }
    }
    
    lua_pushstring(L, moduleName.c_str());
    return 1;
}

// Lua C function: sequencer(name) or sequencer(name, configTable)
// Creates or configures a TrackerSequencer module
static int lua_sequencer(lua_State* L) {
    if (!g_engine) {
        return luaL_error(L, "Engine not available");
    }
    
    const char* name = luaL_checkstring(L, 1);
    std::string moduleName(name);
    
    // Check if module exists
    auto& registry = g_engine->getModuleRegistry();
    auto module = registry.getModule(moduleName);
    
    if (!module) {
        std::string cmd = "add TrackerSequencer " + moduleName;
        g_engine->executeCommand(cmd);
    }
    
    // If config table is provided, apply parameters
    if (lua_istable(L, 2)) {
        lua_pushnil(L);
        while (lua_next(L, 2) != 0) {
            const char* key = lua_tostring(L, -2);
            float value = static_cast<float>(lua_tonumber(L, -1));
            std::string cmd = "set " + moduleName + " " + key + " " + std::to_string(value);
            g_engine->executeCommand(cmd);
            lua_pop(L, 1);
        }
    }
    
    lua_pushstring(L, moduleName.c_str());
    return 1;
}

// Lua C function: pattern(name, stepCount)
// Declares a pattern with the given name and step count
static int lua_pattern(lua_State* L) {
    if (!g_engine) {
        return luaL_error(L, "Engine not available");
    }
    
    const char* name = luaL_checkstring(L, 1);
    int stepCount = static_cast<int>(luaL_optinteger(L, 2, 16));
    
    // Use PatternRuntime to create the pattern
    auto& patternRuntime = g_engine->getPatternRuntime();
    
    // Create default pattern with stepCount
    struct Pattern {
        int steps;
        Pattern(int s) : steps(s) {}
    };
    
    // Create pattern via command (let the system handle it)
    std::string cmd = "add pattern " + std::string(name) + " " + std::to_string(stepCount);
    g_engine->executeCommand(cmd);
    
    lua_pushstring(L, name);
    return 1;
}

// Lua C function: connect(source, target, type)
// Creates a connection between two modules
static int lua_connect(lua_State* L) {
    if (!g_engine) {
        return luaL_error(L, "Engine not available");
    }
    
    const char* source = luaL_checkstring(L, 1);
    const char* target = luaL_checkstring(L, 2);
    const char* type = luaL_optstring(L, 3, "audio");
    
    std::string cmd = "route " + std::string(source) + " " + std::string(target);
    if (std::string(type) != "audio") {
        cmd += " " + std::string(type);
    }
    
    auto result = g_engine->executeCommand(cmd);
    
    lua_pushboolean(L, result.success);
    return 1;
}

// Lua C function: onStateChange(callback)
// Registers a Lua callback for engine state changes
static int lua_onStateChange(lua_State* L) {
    if (!g_engine) {
        return luaL_error(L, "Engine not available");
    }
    
    // Check if callback is a function
    if (!lua_isfunction(L, 1)) {
        return luaL_error(L, "onStateChange requires a function argument");
    }
    
    // Store the callback reference in registry and get its reference ID
    lua_pushvalue(L, 1);  // Push the function onto stack
    int callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
    
    // Create a callback that will call the Lua function
    Engine::LuaStateChangeCallback callback = [callbackRef](lua_State* L, const EngineState& state) {
        // Retrieve the stored function reference
        lua_rawgeti(L, LUA_REGISTRYINDEX, callbackRef);
        
        // Push state info as arguments
        lua_pushstring(L, "state_changed");
        lua_pushinteger(L, state.version);
        
        // Call the function with 2 arguments
        if (lua_pcall(L, 2, 0, 0) != 0) {
            ofLogError("LuaGlobals") << "onStateChange callback error: " << lua_tostring(L, -1);
            lua_pop(L, 1);
        }
    };
    
    size_t id = g_engine->registerStateChangeCallback(callback);
    
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

// Register all global Lua functions
static void registerHelperFunctions(lua_State* L) {
    // Module helper functions
    lua_register(L, "audioOut", lua_audioOut);
    lua_register(L, "videoOut", lua_videoOut);
    lua_register(L, "oscilloscope", lua_oscilloscope);
    lua_register(L, "spectrogram", lua_spectrogram);
    lua_register(L, "sampler", lua_sampler);
    lua_register(L, "sequencer", lua_sequencer);
    lua_register(L, "pattern", lua_pattern);
    
    // Connection helper function
    lua_register(L, "connect", lua_connect);
    
    // State change callback
    lua_register(L, "onStateChange", lua_onStateChange);
}

// Register engine instance as global 'engine' in Lua state
void registerEngineGlobal(void* luaState) {
    lua_State* L = static_cast<lua_State*>(luaState);
    
    if (!L || !g_engine) {
        return;
    }
    
    // Register Clock metatable first (before creating clock userdata)
    registerClockMetatable(L);
    
    // Register helper functions as globals
    registerHelperFunctions(L);
    
    // Create userdata for engine pointer
    void* userdata = lua_newuserdata(L, sizeof(Engine*));
    *reinterpret_cast<Engine**>(userdata) = g_engine;
    
    // Create and set metatable for engine userdata
    luaL_newmetatable(L, "vt_Engine");
    
    // Add __index method that returns functions
    lua_pushcfunction(L, lua_engine_getClock);
    lua_setfield(L, -2, "getClock");
    
    lua_pushcfunction(L, lua_engine_executeCommand);
    lua_setfield(L, -2, "executeCommand");
    
    // Set metatable's __index to itself (so methods are found)
    lua_pushvalue(L, -1);  // Copy metatable
    lua_setfield(L, -2, "__index");
    
    // Set metatable for userdata
    lua_setmetatable(L, -2);
    
    // Set as global "engine"
    lua_setglobal(L, "engine");
}

} // namespace lua
} // namespace vt

