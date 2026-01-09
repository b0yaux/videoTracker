#include "LuaGlobals.h"
#include "core/Engine.h"

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

} // namespace lua
} // namespace vt

