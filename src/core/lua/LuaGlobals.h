#pragma once

// Forward declaration
namespace vt {
    class Engine;
}

namespace vt {
namespace lua {

/**
 * LuaGlobals - Global state for Lua helper functions
 * 
 * This provides a way for SWIG-generated Lua bindings to access
 * the Engine instance without requiring it to be passed as a parameter.
 */

// Set the global engine pointer (called by Engine::setupLua())
void setGlobalEngine(Engine* engine);

// Get the global engine pointer (used by helper functions)
Engine* getGlobalEngine();

// Register engine instance as global 'engine' in Lua state
// This function is declared in SWIG interface and implemented in Engine.cpp
extern void registerEngineGlobal(void* luaState);

} // namespace lua
} // namespace vt

