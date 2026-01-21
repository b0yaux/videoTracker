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

} // namespace lua
} // namespace vt

