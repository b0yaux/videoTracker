#include "LuaGlobals.h"
#include "LuaHelpers.h"
#include "core/Engine.h"
#include "utils/Clock.h"
#include "core/ModuleRegistry.h"
#include "core/ConnectionManager.h"
#include "core/ParameterRouter.h"
#include "core/PatternRuntime.h"
#include "data/Pattern.h"
#include "core/Command.h"
#include <lua.hpp>
#include <string>
#include <map>
#include <sstream>

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
    Clock* clock = reinterpret_cast<Clock*>(lua_touserdata(L, 1));
    if (clock && lua_isnumber(L, 2)) {
        float bpm = static_cast<float>(lua_tonumber(L, 2));
        clock->setBPM(bpm);
        return 0;
    }
    return luaL_error(L, "Invalid arguments to setBPM");
}

// Lua C function: Clock:start()
static int lua_clock_start(lua_State* L) {
    Clock* clock = reinterpret_cast<Clock*>(lua_touserdata(L, 1));
    if (clock) {
        clock->start();
        return 0;
    }
    return luaL_error(L, "Invalid clock object");
}

// Lua C function: Clock:stop()
static int lua_clock_stop(lua_State* L) {
    Clock* clock = reinterpret_cast<Clock*>(lua_touserdata(L, 1));
    if (clock) {
        clock->stop();
        return 0;
    }
    return luaL_error(L, "Invalid clock object");
}

// Lua C function: Clock:pause()
static int lua_clock_pause(lua_State* L) {
    Clock* clock = reinterpret_cast<Clock*>(lua_touserdata(L, 1));
    if (clock) {
        clock->pause();
        return 0;
    }
    return luaL_error(L, "Invalid clock object");
}

// Lua C function: Clock:reset()
static int lua_clock_reset(lua_State* L) {
    Clock* clock = reinterpret_cast<Clock*>(lua_touserdata(L, 1));
    if (clock) {
        clock->reset();
        return 0;
    }
    return luaL_error(L, "Invalid clock object");
}

// Lua C function: Clock:isPlaying() -> boolean
static int lua_clock_isPlaying(lua_State* L) {
    Clock* clock = reinterpret_cast<Clock*>(lua_touserdata(L, 1));
    if (clock) {
        lua_pushboolean(L, clock->isPlaying());
        return 1;
    }
    return luaL_error(L, "Invalid clock object");
}

// Lua C function: Clock:getBPM() -> float
static int lua_clock_getBPM(lua_State* L) {
    Clock* clock = reinterpret_cast<Clock*>(lua_touserdata(L, 1));
    if (clock) {
        lua_pushnumber(L, clock->getBPM());
        return 1;
    }
    return luaL_error(L, "Invalid clock object");
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
    Engine* engine = reinterpret_cast<Engine*>(lua_touserdata(L, 1));
    if (engine) {
        // Create userdata for Clock pointer with proper metatable
        Clock* clock = &engine->getClock();
        Clock** clockPtr = static_cast<Clock**>(lua_newuserdata(L, sizeof(Clock*)));
        *clockPtr = clock;

        // Set the metatable for this userdata
        luaL_getmetatable(L, "vt_Clock");
        lua_setmetatable(L, -2);

        return 1;
    }
    return 0;
}

// Lua C function to execute command via engine
static int lua_engine_executeCommand(lua_State* L) {
    Engine* engine = reinterpret_cast<Engine*>(lua_touserdata(L, 1));
    if (engine && lua_isstring(L, 2)) {
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
    // Return error
    lua_newtable(L);
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "success");
    lua_pushstring(L, "Invalid command or engine not available");
    lua_setfield(L, -2, "error");
    return 1;
}

// Register engine instance as global 'engine' in Lua state
void registerEngineGlobal(void* luaState) {
    lua_State* L = static_cast<lua_State*>(luaState);
    
    if (!L || !g_engine) {
        return;
    }
    
    // Register Clock metatable first (before creating clock userdata)
    registerClockMetatable(L);
    
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

// ═══════════════════════════════════════════════════════════
// LUA HELPER FUNCTIONS (for live-coding syntax)
// ═══════════════════════════════════════════════════════════

// Helper to parse Lua table to std::map<std::string, std::string>
static std::map<std::string, std::string> parseConfigTable(lua_State* L, int arg) {
    std::map<std::string, std::string> config;
    
    if (lua_istable(L, arg)) {
        lua_pushnil(L);
        while (lua_next(L, arg) != 0) {
            // Key at -2, value at -1
            if (lua_isstring(L, -2)) {
                std::string key = lua_tostring(L, -2);
                std::string value;
                
                if (lua_isnumber(L, -1)) {
                    value = std::to_string(lua_tonumber(L, -1));
                } else if (lua_isboolean(L, -1)) {
                    value = lua_toboolean(L, -1) ? "1" : "0";
                } else if (lua_isstring(L, -1)) {
                    value = lua_tostring(L, -1);
                }
                
                if (!key.empty()) {
                    config[key] = value;
                }
            }
            lua_pop(L, 1);  // Pop value, keep key for next iteration
        }
    }
    
    return config;
}

// Lua C function: sampler(name, config) -> string
static int lua_sampler(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    std::map<std::string, std::string> config = parseConfigTable(L, 2);
    
    Engine* engine = getGlobalEngine();
    if (!engine) {
        return luaL_error(L, "Engine not available");
    }
    
    LuaHelpers helpers(engine);
    std::string result = helpers.createSampler(name, config);
    lua_pushstring(L, result.c_str());
    return 1;
}

// Lua C function: sequencer(name, config) -> string
static int lua_sequencer(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    std::map<std::string, std::string> config = parseConfigTable(L, 2);
    
    Engine* engine = getGlobalEngine();
    if (!engine) {
        return luaL_error(L, "Engine not available");
    }
    
    LuaHelpers helpers(engine);
    std::string result = helpers.createSequencer(name, config);
    lua_pushstring(L, result.c_str());
    return 1;
}

// Lua C function: audioOut(name, config) -> string
static int lua_audioOut(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    std::map<std::string, std::string> config = parseConfigTable(L, 2);
    
    Engine* engine = getGlobalEngine();
    if (!engine) {
        return luaL_error(L, "Engine not available");
    }
    
    LuaHelpers helpers(engine);
    std::string result = helpers.createSystemModule("AudioOutput", name, config);
    lua_pushstring(L, result.c_str());
    return 1;
}

// Lua C function: videoOut(name, config) -> string
static int lua_videoOut(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    std::map<std::string, std::string> config = parseConfigTable(L, 2);
    
    Engine* engine = getGlobalEngine();
    if (!engine) {
        return luaL_error(L, "Engine not available");
    }
    
    LuaHelpers helpers(engine);
    std::string result = helpers.createSystemModule("VideoOutput", name, config);
    lua_pushstring(L, result.c_str());
    return 1;
}

// Lua C function: oscilloscope(name, config) -> string
static int lua_oscilloscope(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    std::map<std::string, std::string> config = parseConfigTable(L, 2);
    
    Engine* engine = getGlobalEngine();
    if (!engine) {
        return luaL_error(L, "Engine not available");
    }
    
    LuaHelpers helpers(engine);
    std::string result = helpers.createSystemModule("Oscilloscope", name, config);
    lua_pushstring(L, result.c_str());
    return 1;
}

// Lua C function: spectrogram(name, config) -> string
static int lua_spectrogram(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    std::map<std::string, std::string> config = parseConfigTable(L, 2);
    
    Engine* engine = getGlobalEngine();
    if (!engine) {
        return luaL_error(L, "Engine not available");
    }
    
    LuaHelpers helpers(engine);
    std::string result = helpers.createSystemModule("Spectrogram", name, config);
    lua_pushstring(L, result.c_str());
    return 1;
}

// Lua C function: connect(source, target, type) -> bool
static int lua_connect(lua_State* L) {
    const char* source = luaL_checkstring(L, 1);
    const char* target = luaL_checkstring(L, 2);
    const char* type = luaL_optstring(L, 3, "audio");
    
    Engine* engine = getGlobalEngine();
    if (!engine) {
        return luaL_error(L, "Engine not available");
    }
    
    LuaHelpers helpers(engine);
    bool result = helpers.connect(source, target, type);
    lua_pushboolean(L, result);
    return 1;
}

// Lua C function: setParam(module, param, value) -> bool
static int lua_setParam(lua_State* L) {
    const char* module = luaL_checkstring(L, 1);
    const char* param = luaL_checkstring(L, 2);
    float value = static_cast<float>(luaL_checknumber(L, 3));
    
    Engine* engine = getGlobalEngine();
    if (!engine) {
        return luaL_error(L, "Engine not available");
    }
    
    LuaHelpers helpers(engine);
    std::ostringstream oss;
    oss << value;
    bool result = helpers.setParameter(module, param, oss.str());
    lua_pushboolean(L, result);
    return 1;
}

// Lua C function: getParam(module, param) -> float
static int lua_getParam(lua_State* L) {
    const char* module = luaL_checkstring(L, 1);
    const char* param = luaL_checkstring(L, 2);
    
    Engine* engine = getGlobalEngine();
    if (!engine) {
        return luaL_error(L, "Engine not available");
    }
    
    LuaHelpers helpers(engine);
    std::string valueStr = helpers.getParameter(module, param);
    
    if (valueStr.empty()) {
        lua_pushnumber(L, 0.0);
    } else {
        try {
            lua_pushnumber(L, std::stof(valueStr));
        } catch (...) {
            lua_pushnumber(L, 0.0);
        }
    }
    return 1;
}

// Lua C function: pattern(name, steps) -> string
static int lua_pattern(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    int steps = static_cast<int>(luaL_optinteger(L, 2, 16));
    
    Engine* engine = getGlobalEngine();
    if (!engine) {
        return luaL_error(L, "Engine not available");
    }
    
    // IDEMPOTENT: Check if pattern already exists
    PatternRuntime& patternRuntime = engine->getPatternRuntime();
    if (patternRuntime.patternExists(name)) {
        // Pattern exists - update if step count changed
        int currentStepCount = patternRuntime.getPatternStepCount(name);
        if (currentStepCount != steps) {
            Pattern updatedPattern(steps);
            patternRuntime.updatePattern(name, updatedPattern);
        }
        lua_pushstring(L, name);
        return 1;
    }
    
    // Pattern doesn't exist - create it via command
    std::string command = "pattern create " + std::string(name) + " " + std::to_string(steps);
    auto result = engine->executeCommand(command);
    
    if (result.success) {
        lua_pushstring(L, name);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

// Register all helper functions as Lua globals
void registerHelperFunctions(void* luaState) {
    lua_State* L = static_cast<lua_State*>(luaState);
    
    if (!L) {
        return;
    }
    
    lua_register(L, "sampler", lua_sampler);
    lua_register(L, "sequencer", lua_sequencer);
    lua_register(L, "audioOut", lua_audioOut);
    lua_register(L, "videoOut", lua_videoOut);
    lua_register(L, "oscilloscope", lua_oscilloscope);
    lua_register(L, "spectrogram", lua_spectrogram);
    lua_register(L, "connect", lua_connect);
    lua_register(L, "setParam", lua_setParam);
    lua_register(L, "getParam", lua_getParam);
    lua_register(L, "pattern", lua_pattern);
}

} // namespace lua
} // namespace vt

