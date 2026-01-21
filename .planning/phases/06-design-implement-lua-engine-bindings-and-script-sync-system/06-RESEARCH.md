# Phase 06: Research & Design Lua-Engine Integration Architecture - Research

**Researched:** 2026-01-21
**Domain:** Lua binding architecture, SWIG bindings, ScriptManager, CodeShell, Engine integration
**Confidence:** HIGH (based on source code analysis, prior phase research, and documented decisions)

---

## Summary

This research investigates the Lua binding architecture connecting scripts to the Engine in the videoTracker project. The codebase has evolved through multiple phases of simplification (Phases 7.9-7.10, 1-3), eliminating deferred update layers and async script execution threads. The current architecture uses **SWIG-generated bindings** in `videoTracker.i` for C++ class exposure, **ofxLua** for Lua state management, and **lock-free command queues** for thread-safe state mutations.

**Key Findings (Empirical Analysis):**

1. **SWIG provides complete bindings** - All Engine subsystems (Clock, ModuleRegistry, ConnectionManager, PatternRuntime) are exposed via `videoTracker.i` with helper functions for declarative syntax (`sampler()`, `sequencer()`, `connect()`)

2. **Inconsistency in LuaHelpers** - `createSampler()` and `createSequencer()` use STRING commands (`executeCommand("add MultiSampler " + name)`) while `setParameter()` correctly uses `SetParameterCommand`. This bypasses command queue for module creation.

3. **Inconsistent fallbacks in Clock SWIG bindings** - `setBPM()` uses direct `$self->setBPM(bpm)` fallback while `start()`/`stop()` correctly use `executeCommandImmediate()`. Direct calls bypass Engine notifications.

4. **ScriptManager is read-only observer** - It regenerates scripts FROM Engine state, NOT a direct script executor. Current sync is ONE-WAY: Engine → ScriptManager → CodeShell. Scripts cannot receive reactive state updates.

5. **"Always synced" requires new infrastructure** - Fire-and-forget pattern doesn't support reactive scripts. Recommended solution: add `onStateChange(callback)` API to Engine, exposing existing observer pattern to Lua.

**Primary Recommendations:**

| Priority | Action | Files to Change |
|----------|--------|-----------------|
| **HIGH** | Fix setBPM fallback to use `executeCommandImmediate()` | `videoTracker.i` |
| **HIGH** | Add `AddModuleCommand` to Command.h | `Command.h` |
| **HIGH** | Refactor createSampler/sequencer to use AddModuleCommand | `LuaHelpers.cpp` |
| **MEDIUM** | Add `onStateChange(callback)` API to Engine | `Engine.h`, `Engine.cpp` |
| **MEDIUM** | Expose onStateChange to Lua via SWIG | `videoTracker.i` |

**Sync Contract:** Current "fire-and-forget" remains for one-shot scripts. New "always synced" mode available via `engine:onStateChange(function(state) ... end)` callbacks.

---

## Standard Stack

### Core Technologies

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| **SWIG 4.x** | System | C++/Lua binding generation | Generates type-safe bindings from `videoTracker.i` interface file |
| **ofxLua** | Part of openFrameworks | Lua state management | Wraps lua_State, provides `doString()`/`doScript()` methods |
| **moodycamel::BlockingConcurrentQueue** | Part of libs | Thread-safe command queue | Lock-free, multi-producer/multi-consumer, Phase 7.3 standard |
| **std::atomic<std::shared_ptr>** | C++17 pattern | Immutable state snapshots | Lock-free reads, atomic pointer swap for updates |

### Supporting Components

| Component | Purpose | When to Use |
|-----------|---------|-------------|
| **LuaGlobals** | Global Engine pointer for SWIG helpers | All helper functions access Engine via `vt::lua::getGlobalEngine()` |
| **LuaHelpers** | Declarative helper functions (`sampler`, `sequencer`, `connect`) | Live-coding syntax, idempotent operations |
| **Command Pattern** | Unified state mutation (SetParameter, SetBPM, Connect, etc.) | All state changes from scripts |
| **EngineState** | Immutable snapshot of complete engine state | State observation, script generation |

### File Structure

```
src/
├── core/
│   ├── lua/
│   │   ├── videoTracker.i        # SWIG interface (427 lines)
│   │   ├── LuaGlobals.h/.cpp     # Global engine pointer
│   │   └── LuaHelpers.h/.cpp     # Declarative helpers
│   ├── Engine.h/.cpp             # setupLua(), eval(), syncScriptToEngine()
│   ├── ScriptManager.h/.cpp      # State observer, script generation
│   └── Command.h                 # Command base + 8 concrete commands
├── shell/
│   └── CodeShell.h/.cpp          # executeLuaScript(), editor integration
└── modules/
    └── Module.h                  # setParameter() interface
```

---

## Architecture Patterns

### Recommended Project Structure

```
src/core/lua/
├── videoTracker.i           # SWIG interface - DO NOT MODIFY directly
├── LuaGlobals.h/cpp         # Global state - minimal, stable interface
└── LuaHelpers.h/cpp         # Helper implementations - OK to extend

src/core/
├── Engine.h/cpp             # Lua setup + eval entry points
├── ScriptManager.h/cpp      # State observer → script generator
└── Command.h                # All command definitions
```

### Pattern 1: SWIG Binding Architecture

**What:** SWIG generates Lua bindings from C++ headers, exposing Engine subsystems as Lua-accessible objects.

**When to use:** For any new Engine functionality that needs script access.

**Example from videoTracker.i (lines 125-145):**
```cpp
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
}
```

**Usage in Lua:**
```lua
local clock = engine:getClock()
clock:setBPM(120)

local registry = engine:getModuleRegistry()
local sampler = registry:getModule("kick")
```

### Pattern 2: LuaHelper Declarative Functions

**What:** High-level idempotent functions that wrap command execution with clean syntax.

**When to use:** For common operations that benefit from declarative syntax in live-coding.

**Example from videoTracker.i (lines 281-288):**
```cpp
// Create a MultiSampler module with declarative syntax
// Usage: sampler("mySampler") or sampler("mySampler", {volume=0.8, speed=1.2})
std::string sampler(const std::string& name, const std::map<std::string, std::string>& config = {}) {
    auto* engine = vt::lua::getGlobalEngine();
    if (!engine) {
        return "";
    }
    vt::lua::LuaHelpers helpers(engine);
    return helpers.createSampler(name, config);
}
```

**Usage in Lua:**
```lua
-- Simple creation
local kick = sampler("kick")

-- Declarative configuration (idempotent - safe to re-run)
local kick = sampler("kick", {
    volume = 0.8,
    speed = 1.2,
    attackMs = 5.0
})
```

### Pattern 3: Command Queue Thread Safety

**What:** All state mutations from scripts route through lock-free command queue, processed on audio thread.

**When to use:** For any function that modifies Engine state from Lua.

**Critical Pattern from LuaHelpers.cpp (lines 206-222):**
```cpp
bool LuaHelpers::setParameter(const std::string& moduleName, const std::string& paramName, const std::string& value) {
    if (!engine_) {
        ofLogError("LuaHelpers") << "Engine is null, cannot set parameter";
        return false;
    }
    
    // CRITICAL FIX: Always use command queue for thread safety
    // Direct module access from Lua (main thread) while audio thread processes is unsafe
    auto cmd = std::make_unique<vt::SetParameterCommand>(moduleName, paramName, floatValue);
    bool enqueued = engine_->enqueueCommand(std::move(cmd));
    
    if (!enqueued) {
        ofLogWarning("LuaHelpers") << "Command queue full, falling back to executeCommand";
        // Fallback to string command (slower but safe)
        std::string command = "set " + moduleName + " " + paramName + " " + value;
        auto result = engine_->executeCommand(command);
        // ...
    }
    
    return true;
}
```

### Pattern 4: State Observer → Script Generation

**What:** ScriptManager subscribes to Engine state changes and regenerates Lua script on each notification.

**When to use:** For any component that needs to stay synchronized with Engine state.

**Pattern from ScriptManager.cpp (lines 34-71):**
```cpp
void ScriptManager::setup() {
    if (!engine_) {
        ofLogError("ScriptManager") << "Engine is null, cannot setup";
        return;
    }
    
    // Subscribe to Engine state changes
    observerId_ = engine_->subscribe([this](const EngineState& state) {
        if (!autoUpdateEnabled_) {
            return;
        }
        
        // Single atomic guard replaces 4 nested guards
        // Use compare_exchange_strong to atomically transition from IDLE to UPDATING
        UpdateState expected = UpdateState::IDLE;
        if (updateState_.compare_exchange_strong(expected, UpdateState::UPDATING)) {
            // We acquired the lock - safe to update
            try {
                updateScriptFromState(state);
            } catch (...) {
                ofLogError("ScriptManager") << "Exception in updateScriptFromState";
            }
            // Release the lock
            updateState_.store(UpdateState::IDLE);
        }
        // If compare_exchange failed, state was already UPDATING - skip this update
    });
    
    // Generate initial script from CURRENT state (after session load)
    EngineState currentState = engine_->getState();
    updateScriptFromState(currentState);
}
```

### Pattern 5: Fire-and-Forget Script Execution

**What:** `Engine::eval()` executes script synchronously, but state updates happen asynchronously via notification queue.

**When to use:** For all script execution from CodeShell.

**Pattern from Engine.cpp (lines 519-555):**
```cpp
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
    if (callback) {
        callback(true);
    }
}
```

### Anti-Patterns to Avoid

- **DO NOT create new deferred update layers** - The 7.10 audit identified these as creating timing windows, not preventing them
- **DO NOT add background script execution threads** - Phase 7.10.1 removed async execution, simplified to sync only
- **DO NOT bypass command queue for "quick" state changes** - Always use `enqueueCommand()` or `executeCommandImmediate()`
- **DO NOT access modules directly from Lua** - Always route through commands for thread safety

---

## Don't Hand-Roll

Problems that look simple but have existing solutions:

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| **Lua/C++ bindings** | Write manual lua_push* calls | SWIG with `videoTracker.i` | SWIG handles type safety, memory management, error checking |
| **Thread-safe command queue** | Implement mutex-based queue | `moodycamel::BlockingConcurrentQueue` | Lock-free, multi-producer/multi-consumer, battle-tested |
| **Immutable state snapshots** | Build state on every read | `std::atomic<std::shared_ptr<const EngineState>>` | Lock-free reads, atomic updates, copy semantics |
| **Script execution** | Create new Lua state per script | `ofxLua` wrapper class | Handles lua_State lifecycle, error callbacks, library loading |
| **Parameter setting from Lua** | Call `module->setParameter()` directly | `SetParameterCommand` via `enqueueCommand()` | Thread safety, undo support, consistent notification |
| **Global Engine access** | Pass Engine* through call chain | `vt::lua::getGlobalEngine()` | SWIG helpers need Engine without parameter passing |
| **State versioning** | Use timestamps or sequence numbers | Atomic `stateVersion_` + compare-exchange | Prevents feedback loops, handles concurrent updates |
| **Connection creation** | Call `connectionManager->connect()` | `ConnectCommand` + `LuaHelpers::connect()` | Idempotent, error handling, consistent with other mutations |

**Key insight:** The command queue + state observer pattern is the foundation. All state changes flow through commands, all reads use snapshots, observers are notified of changes. Don't invent new patterns.

---

## Common Pitfalls

### Pitfall 1: Engine Pointer Becomes Null During Script Execution

**What goes wrong:** Scripts fail because `vt::lua::getGlobalEngine()` returns nullptr.

**Why it happens:** 
- `Engine::setupLua()` calls `vt::lua::setGlobalEngine(this)` AFTER Lua is initialized
- If SWIG helpers are called before setup completes, `getGlobalEngine()` returns nullptr
- Error handling in helpers should check for null and return gracefully

**How to avoid:**
```cpp
// Always check in SWIG helper functions (from videoTracker.i:283-288)
std::string sampler(const std::string& name, const std::map<std::string, std::string>& config = {}) {
    auto* engine = vt::lua::getGlobalEngine();
    if (!engine) {
        return "";  // Graceful failure
    }
    // ... rest of implementation
}
```

**Warning signs:**
- Lua error: "attempt to call a nil value"
- Log message: "Engine is null, cannot create sampler"

### Pitfall 2: State Snapshot Built During Unsafe Period

**What goes wrong:** `ScriptManager::generateScriptFromState()` builds snapshot while commands are processing, causing inconsistent or corrupted script output.

**Why it happens:**
- Observer callback fires during command processing
- `buildStateSnapshot()` detects unsafe state but may return stale data
- Multiple paths ( updateobserver + deferred) create race conditions

**How to avoid:**
```cpp
// From ScriptManager.cpp:48-62 - Single atomic guard pattern
UpdateState expected = UpdateState::IDLE;
if (updateState_.compare_exchange_strong(expected, UpdateState::UPDATING)) {
    // We acquired the lock - safe to update
    try {
        updateScriptFromState(state);  // Uses passed state, not getState()
    } catch (...) {
        ofLogError("ScriptManager") << "Exception in updateScriptFromState";
    }
    updateState_.store(UpdateState::IDLE);
}
// If compare_exchange failed, state was already UPDATING - skip this update
```

**Key fix:** Use the `state` parameter passed to observer callback, don't call `engine_->getState()` which may build snapshot during unsafe period.

**Warning signs:**
- Log: "Deferring script update - unsafe state"
- Incomplete or corrupted script output
- Missing modules/connections in generated script

### Pitfall 3: Bypassing Command Queue for Transport Operations

**What goes wrong:** Clock::start()/stop() called directly from Lua, bypassing command queue and causing thread safety issues.

**Why it happens:**
- SWIG bindings for Clock methods were implemented with fallback to direct calls
- When `enqueueCommand()` fails (queue full), fallback calls `clock->start()` directly
- This happens on main thread while audio thread may be processing

**How to avoid:**
```cpp
// From videoTracker.i:202-225 - Current implementation with fallback
void start() {
    auto* engine = vt::lua::getGlobalEngine();
    if (engine) {
        auto cmd = std::make_unique<vt::StartTransportCommand>();
        if (!engine->enqueueCommand(std::move(cmd))) {
            // CRITICAL FIX: Fallback executes immediately but still routes through Engine
            ofLogWarning("Clock") << "Command queue full, executing StartTransportCommand immediately";
            auto fallbackCmd = std::make_unique<vt::StartTransportCommand>();
            engine->executeCommandImmediate(std::move(fallbackCmd));
        }
    } else {
        $self->start();  // Fallback only if engine not available
    }
}
```

**Better pattern:** Always route through Engine's `executeCommandImmediate()` for thread safety, never call Clock methods directly.

**Warning signs:**
- Audio glitches during script execution
- Transport state inconsistent between shells
- Thread safety assertions firing in debug builds

### Pitfall 4: Script Execution During State Snapshot Building

**What goes wrong:** Recursive call to `getState()` during `buildStateSnapshot()` causes deadlock or infinite loop.

**Why it happens:**
- `buildStateSnapshot()` iterates modules and calls `getCurrentScript()`
- `getCurrentScript()` calls `engine_->getState()` if script not cached
- Recursive call while already in `buildStateSnapshot()`

**How to avoid:**
```cpp
// From Engine.h:653-677 - Thread-local flag pattern
static thread_local bool isBuildingSnapshot_;

// In buildStateSnapshot():
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

// From ScriptManager.cpp:571-576 - Check flag before calling getState()
std::string ScriptManager::getCurrentScript() const {
    if (engine_ && Engine::isBuildingSnapshot()) {
        ofLogVerbose("ScriptManager") << "getCurrentScript() called during snapshot building - returning cached script";
        return currentScript_;
    }
    // ... rest of implementation
}
```

**Warning signs:**
- Deadlock during script generation
- Stack overflow from recursive calls
- Log: "getCurrentScript() called during snapshot building"

### Pitfall 5: State Version Feedback Loops

**What goes wrong:** ScriptManager regenerates script, which triggers another state change, creating infinite loop.

**Why it happens:**
- Script regeneration was treated as state change
- Observer callback fires again after regeneration
- No version tracking to detect redundant updates

**How to avoid:**
```cpp
// From ScriptManager.cpp:449-487 - Version tracking
void ScriptManager::updateScriptFromState(const EngineState& state) {
    uint64_t stateVersion = state.version;
    if (engine_) {
        uint64_t currentEngineVersion = engine_->getStateVersion();
        
        // Reject stale state to prevent feedback loops
        if (stateVersion > 0 && stateVersion < currentEngineVersion) {
            ofLogVerbose("ScriptManager") << "State version is stale - deferring script generation";
            return;
        }
    }
    
    // Check if we've already regenerated from this state version
    if (stateVersion > 0 && stateVersion <= lastRegeneratedVersion_) {
        ofLogVerbose("ScriptManager") << "Skipping redundant script regeneration";
        return;
    }
    
    // Proceed with generation
    std::string generatedScript = generateScriptFromState(state);
    currentScript_ = generatedScript;
    lastRegeneratedVersion_ = stateVersion;
}
```

**Warning signs:**
- Continuous script regeneration
- CPU spike from endless loop
- Log: "Skipping redundant script regeneration" appearing frequently

### Pitfall 6: ScriptManager Generates Scripts That Reference 'engine' Global Which Is NEVER Created

**What goes wrong:** Scripts generated by ScriptManager contain references to `engine:getClock()`, `engine:getModuleRegistry()`, etc., but these scripts fail at runtime with "attempt to index a nil value (global 'engine')".

**Why it happens:** Critical infrastructure gap in Lua initialization:

1. **ScriptManager generates engine references** - At `ScriptManager.cpp:81` and `:221`, generated scripts contain:
   ```lua
   local clock = engine:getClock()
   ```

2. **registerEngineGlobal() is defined but never called** - The `videoTracker.i` SWIG interface (lines 30-39) defines:
   ```cpp
   // This will be called from Engine::setupLua() after SWIG module is loaded
   void registerEngineGlobal(vt::Engine* engine) {
       SWIG_NewPointerObj(L, engine, SWIGTYPE_p_vt__Engine, 0);
       lua_setglobal(L, "engine");
   }
   ```

3. **Engine::setupLua() does NOT call registerEngineGlobal()** - At `Engine.cpp:269-301`, setupLua() only:
   - Creates `ofxLua` instance
   - Sets `vt::lua::setGlobalEngine(this)` (for C++ helpers)
   - Registers `exec` function
   - **DOES NOT call `registerEngineGlobal()`**

4. **SWIG bindings are not integrated into CMake** - The `videoTracker.i` file exists but is not used in the build system

**How to avoid:** Add the missing engine global registration in `Engine::setupLua()`:

```cpp
// In Engine::setupLua(), add after line 295 (after lua_register for "exec"):
if (lua_ && lua_->isValid()) {
    lua_State* L = *lua_;
    lua_register(L, "exec", lua_execCommand);

    // CRITICAL FIX: Create 'engine' global for ScriptManager-generated scripts
    // This makes engine:* bindings available in all Lua scripts
    SWIG_NewPointerObj(L, this, SWIGTYPE_p_vt__Engine, 0);
    lua_setglobal(L, "engine");

    ofLogNotice("Engine") << "Lua initialized successfully with engine global";
}
```

**Verification that fix is working:**
```lua
-- Test script to verify engine global exists
if engine and type(engine) == "userdata" then
    print("SUCCESS: engine global is available")
    local clock = engine:getClock()
    print("SUCCESS: engine:getClock() works")
else
    print("FAILURE: engine global is nil or incorrect type")
end
```

**Warning signs:**
- Lua error: "attempt to index a nil value (global 'engine')"
- ScriptManager-generated scripts fail immediately
- SWIG bindings appear to do nothing in live scripts

**Files affected:**
| File | Line | Issue |
|------|------|-------|
| `ScriptManager.cpp` | 81, 221 | Generates scripts using `engine:*` |
| `videoTracker.i` | 30-39 | Defines `registerEngineGlobal()` but never called |
| `Engine.cpp` | 269-301 | `setupLua()` missing engine global registration |
| `CMakeLists.txt` | - | SWIG not integrated |

**Root cause evidence:**
- Comment in `videoTracker.i:32-33`: "This will be called from Engine::setupLua() after SWIG module is loaded"
- Comment exists but implementation never added to `setupLua()`
- Error log confirms: "attempt to index a nil value (global 'engine')"

**Recommendation priority:** **CRITICAL** - Without this fix, no ScriptManager-generated scripts can execute.

---

## Code Examples

### Example 1: Complete Script Execution Flow

**From CodeShell.cpp:929-977**
```cpp
void CodeShell::executeLuaScript(const std::string& script) {
    if (!engine_) return;

    // Clear previous errors
    clearErrors();

    // Redesign: Fire-and-forget - remove blocking wait
    bool wasAutoUpdate = false;
    if (engine_) {
        wasAutoUpdate = engine_->isScriptAutoUpdateEnabled();
        engine_->setScriptAutoUpdate(false);  // Prevent overwrite during execution
    }

    // Execute via Engine (non-blocking)
    Engine::Result result = engine_->eval(script);

    // Re-enable auto-update immediately
    if (result.success) {
        editorMode_ = EditorMode::VIEW;
        userEditBuffer_.clear();
        if (engine_) {
            engine_->setScriptAutoUpdate(true);  // Re-enable for VIEW mode
        }
        ofLogNotice("CodeShell") << "Script executed - will update when state changes (fire-and-forget design)";
    } else {
        // Error handling - stay in EDIT mode on failure
        ofLogError("CodeShell") << "Script execution failed - staying in EDIT mode: " << result.error;
    }

    // Display result in REPL
    if (replShell_) {
        if (result.success) {
            replShell_->appendOutput(result.message);
        } else {
            replShell_->appendError(result.error);
            int errorLine = parseErrorLine(result.error);
            if (errorLine > 0) {
                markErrorInEditor(errorLine - 1, result.error);
            }
        }
    }
}
```

### Example 2: Engine::eval() Implementation

**From Engine.cpp:425-517**
```cpp
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
    notifyingObservers_.store(true, std::memory_order_release);
    
    // CRITICAL: Ensure flag is visible to all threads immediately
    std::atomic_thread_fence(std::memory_order_seq_cst);
    
    try {
        // Set error callback to capture errors
        std::string luaError;
        lua_->setErrorCallback([&luaError](std::string& msg) {
            luaError = msg;
        });
        
        // Execute Lua script with additional safety
        bool success = false;
        try {
            success = lua_->doString(script);
        } catch (const std::bad_alloc& e) {
            notifyingObservers_.store(false, std::memory_order_release);
            return Result(false, "Lua execution failed", "Memory allocation error: " + std::string(e.what()));
        } catch (const std::exception& e) {
            notifyingObservers_.store(false, std::memory_order_release);
            return Result(false, "Lua execution failed", "C++ exception: " + std::string(e.what()));
        } catch (...) {
            notifyingObservers_.store(false, std::memory_order_release);
            return Result(false, "Lua execution failed", "Unknown C++ exception during script execution");
        }
        
        // Clear script execution flag before returning
        notifyingObservers_.store(false, std::memory_order_release);
        
        if (success) {
            return Result(true, "Script executed successfully");
        } else {
            // Extract error information
            std::string errorMsg = luaError.empty() ? lua_->getErrorMessage() : luaError;
            return Result(false, "Lua execution failed", errorMsg);
        }
    } catch (const std::exception& e) {
        notifyingObservers_.store(false, std::memory_order_release);
        return Result(false, "Script execution failed", "C++ exception: " + std::string(e.what()));
    }
}
```

### Example 3: Command Registration in SWIG

**From videoTracker.i:181-255**
```cpp
// Extend Clock to expose key methods
%extend Clock {
    void setBPM(float bpm) {
        // Route BPM changes through command queue for thread safety
        auto* engine = vt::lua::getGlobalEngine();
        if (engine) {
            auto cmd = std::make_unique<vt::SetBPMCommand>(bpm);
            if (!engine->enqueueCommand(std::move(cmd))) {
                ofLogWarning("Clock") << "Failed to enqueue SetBPMCommand, falling back to direct call";
                $self->setBPM(bpm);
            }
        } else {
            // Fallback to direct call if engine not available
            $self->setBPM(bpm);
        }
    }
    
    void start() {
        // CRITICAL FIX: Route through command queue instead of direct call
        auto* engine = vt::lua::getGlobalEngine();
        if (engine) {
            auto cmd = std::make_unique<vt::StartTransportCommand>();
            if (!engine->enqueueCommand(std::move(cmd))) {
                ofLogWarning("Clock") << "Command queue full, executing StartTransportCommand immediately";
                auto fallbackCmd = std::make_unique<vt::StartTransportCommand>();
                engine->executeCommandImmediate(std::move(fallbackCmd));
            }
        } else {
            $self->start();
        }
    }
    
    void stop() {
        // Same pattern as start()
        auto* engine = vt::lua::getGlobalEngine();
        if (engine) {
            auto cmd = std::make_unique<vt::StopTransportCommand>();
            if (!engine->enqueueCommand(std::move(cmd))) {
                ofLogWarning("Clock") << "Command queue full, executing StopTransportCommand immediately";
                auto fallbackCmd = std::make_unique<vt::StopTransportCommand>();
                engine->executeCommandImmediate(std::move(fallbackCmd));
            }
        } else {
            $self->stop();
        }
    }
}
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| String-based Lua helpers in Engine.cpp | SWIG bindings in videoTracker.i | Phase 1 (2026-01-20) | ~10x Lua performance improvement |
| Multiple deferred update layers | Single atomic guard (UpdateState) | Phase 7.10.1 (2026-01-16) | Eliminated timing windows |
| Async script execution with background thread | Synchronous execution only | Phase 7.10.1 (2026-01-16) | Simplified threading, removed deadlock potential |
| 3 EditorMode states (VIEW, EDIT, LOCKED) | 2 EditorMode states (VIEW, EDIT) | Phase 7.10.1 (2026-01-16) | Simplified state machine |
| Notification storms with duplicate callbacks | Atomic flag with compare-exchange suppression | Phase 2 (2026-01-20) | Eliminated cascade notifications |
| Multiple unsafeStateFlags_ atoms | Single notifyingObservers_ atom | Phase 3 (2026-01-20) | Simplified state detection |

### Deprecated/Outdated Patterns

- **registerHelpers string in Engine.cpp** - REMOVED in Phase 1. Was 160 lines of string-based helpers that got parsed on every call.
- **onCommandExecuted() callback path** - REMOVED in Phase 7.10.1. Created dual sync paths with race conditions.
- **Frame-based update delays (1, 2, 5 frames)** - REMOVED in Phase 7.10.1. Replaced with immediate event-driven updates.
- **Background script execution thread** - REMOVED in Phase 7.10.1. Simplified to synchronous execution only.

---

## Open Questions

## Open Questions

### Question 1: Should all Clock methods use executeCommandImmediate() fallback?

**EMPIRICAL FINDING:** Clock methods have INCONSISTENT fallback patterns:

| Method | Fallback Pattern | Line | Issue |
|--------|------------------|------|-------|
| `setBPM()` | Direct call `$self->setBPM(bpm)` | videoTracker.i:190 | Bypasses Engine notifications |
| `start()` | `executeCommandImmediate()` | videoTracker.i:213 | Correct pattern |
| `stop()` | `executeCommandImmediate()` | videoTracker.i:240 | Correct pattern |
| `play()` | Direct call `$self->start()` | videoTracker.i:224 | Inconsistent alias |

**Analysis:**
- Direct call in `setBPM()` bypasses Engine's notification system
- `executeCommandImmediate()` routes through Engine, ensuring proper notifications
- Queue capacity is 1024 commands; overflow is unlikely in normal operation

**Recommendation:**
- **REMOVE** direct call fallback from `setBPM()` - use `executeCommandImmediate()` only
- **KEEP** `executeCommandImmediate()` fallback for all transport operations
- **ADD** logging to track if fallbacks are ever hit (should be rare/never)
- **RATIONALE:** `executeCommandImmediate()` ensures consistent notification behavior even when queue is full

### Question 2: Should LuaHelpers use SetParameterCommand or executeCommand()?

**EMPIRICAL FINDING:** LuaHelpers.cpp has INCONSISTENT command patterns:

| Helper Method | Current Pattern | Lines | Should Use |
|---------------|-----------------|-------|------------|
| `setParameter()` | `SetParameterCommand` via `enqueueCommand()` | LuaHelpers.cpp:206-222 | Correct ✓ |
| `createSampler()` | String `"add MultiSampler "` via `executeCommand()` | LuaHelpers.cpp:43-44 | AddModuleCommand |
| `createSequencer()` | String `"add TrackerSequencer "` via `executeCommand()` | LuaHelpers.cpp:80-81 | AddModuleCommand |
| `createSystemModule()` | String via `executeCommand()` | LuaHelpers.cpp:100+ | AddModuleCommand |

**Analysis:**
- `setParameter()` is CORRECT - uses command object with proper queuing
- Module creation helpers BYPASS command queue via string parsing
- String parsing overhead was the root cause of Phase 1 performance issues
- Module creation IS a state mutation and should use command pattern

**Recommendation:**
- **ADD** `AddModuleCommand` to Command.h for module creation
- **REFACTOR** `createSampler()`, `createSequencer()`, `createSystemModule()` to use `AddModuleCommand`
- **KEEP** `setParameter()` as-is (already correct)
- **RATIONALE:** Consistent command pattern for all state mutations, eliminates string parsing overhead

**Code Changes Required:**

1. **Command.h** - Add AddModuleCommand:
```cpp
class AddModuleCommand : public Command {
public:
    AddModuleCommand(const std::string& moduleType, const std::string& name)
        : moduleType_(moduleType), name_(name) {}
    Result execute(Engine* engine) override;
    std::string describe() const override { return "add " + moduleType_ + " " + name_; }
private:
    std::string moduleType_, name_;
};
```

2. **LuaHelpers.cpp** - Refactor createSampler:
```cpp
std::string LuaHelpers::createSampler(const std::string& name, const std::map<std::string, std::string>& config) {
    // ... existing idempotent check ...

    // Use AddModuleCommand instead of string
    auto cmd = std::make_unique<AddModuleCommand>("MultiSampler", name);
    auto result = engine_->enqueueCommand(std::move(cmd));

    if (!result) {
        ofLogError("LuaHelpers") << "Failed to enqueue AddModuleCommand for: " << name;
        return "";
    }

    // ... apply config via setParameter() ...
}
```

### Question 3: What should the sync contract be for "always synced" scripts?

**EMPIRICAL FINDING:** Current architecture is ONE-WAY with limitations:

| Component | Direction | Mechanism |
|-----------|-----------|-----------|
| Engine → ScriptManager | State observation | Observer callback with atomic guard |
| ScriptManager → CodeShell | Script push | `updateScriptFromState()` generates text |
| Engine → Script (variables) | **NONE** | Scripts cannot receive reactive updates |
| Script → Engine | Command queue | `enqueueCommand()` for mutations |

**Current Flow:**
```
User types script → CodeShell → Engine::eval() → SWIG bindings → Command queue
                                                            ↓
Engine state changes → Observer callback → ScriptManager regenerates script
                                                            ↓
                                                    CodeShell receives new script
```

**The Problem:** Scripts run once (fire-and-forget), cannot receive reactive state updates.

**Architectural Options for "Always Synced":**

| Option | Approach | Pros | Cons | Empirical Support |
|--------|----------|------|------|-------------------|
| **A: Polling** | Script calls `engine:getState()` periodically | Simple, no new infrastructure | CPU overhead, latency | ✓ Works with existing `getState()` |
| **B: Callbacks** | Script registers Lua callbacks for state changes | Reactive, low overhead | Complex lifecycle management | ✗ Requires new Engine observer API |
| **C: Two-way binding** | Lua variables stay synced with Engine state | Most elegant | Significant implementation | ✗ No existing infrastructure |
| **D: Event-driven (RECOMMENDED)** | Script uses `engine:onStateChange(fn)` | Matches existing observer pattern | Requires callback registration | ✓ Natural extension of ScriptManager |

**EMPIRICAL ANALYSIS:**
- ScriptManager ALREADY uses observer pattern - extend this to scripts
- Engine has `subscribe()` method for observers - could expose to Lua
- State version tracking exists - enables change detection
- `getState()` returns immutable snapshot - safe for polling

**Recommendation: Option D (Event-driven with onStateChange)**

This extends the existing observer infrastructure rather than building new patterns:

1. **Add Lua callback registration to Engine** (videoTracker.i):
```cpp
%extend Engine {
    // Register Lua callback for state changes
    void onStateChange(std::function<void(const EngineState&)> callback) {
        // Store callback in a map, invoke on each state change
    }

    // Remove callback by ID
    void removeStateChangeCallback(int callbackId) {
        // Remove from map
    }
}
```

2. **Usage in Lua for "always synced" pattern:**
```lua
-- Option 1: Reactive variable binding
engine:onStateChange(function(state)
    currentBPM = state.clock.bpm
    playheadPosition = state.patternRuntime.playhead
end)

-- Option 2: Watch specific module
engine:onStateChange(function(state)
    if state.modules.kick then
        kickVol = state.modules.kick.parameters.volume
    end
end)

-- Option 3: Manual sync trigger
engine:onStateChange(function()
    myScriptVars = engine:getState().clone()
end)
```

3. **Implementation in ScriptManager:**
```cpp
// Existing infrastructure - extend to Lua callbacks
void ScriptManager::notifyStateChange(const EngineState& state) {
    // Existing: updateScriptFromState(state)

    // NEW: Invoke Lua callbacks
    for (auto& [id, callback] : luaCallbacks_) {
        callback(state);
    }
}
```

**Alternative: Option A (Polling) for simpler use cases**

If callback lifecycle is too complex, provide polling helper:
```lua
-- Simple polling - works with existing infrastructure
function sync()
    local state = engine:getState()
    -- Update Lua variables from state
end

-- Call sync() periodically
of.addTimer(sync, 0.1)  -- Every 100ms
```

**Final Recommendation:**
- **PRIMARY:** Implement Option D (event-driven callbacks) as the "proper" solution
- **ALSO SUPPORT:** Option A (polling) as escape hatch for simple cases
- **DO NOT IMPLEMENT:** Option C (two-way binding) - requires significant new infrastructure
- **CONTRACT DOCUMENTATION:** "Fire-and-forget for one-shot scripts, callbacks for reactive sync"

---

## Specific Code Recommendations

Based on empirical analysis, here are the EXACT changes needed:

### 1. videoTracker.i - Fix Clock Fallback Inconsistencies

**File:** `src/core/lua/videoTracker.i`

| Line | Current | Change To | Why |
|------|---------|-----------|-----|
| 189-190 | `$self->setBPM(bpm)` direct call | `engine->executeCommandImmediate()` | Consistent notification behavior |
| 224 | `$self->start()` | Remove (alias handled by start()) | Duplicate, causes confusion |

**Diff for setBPM():**
```cpp
// BEFORE (lines 183-196)
void setBPM(float bpm) {
    auto* engine = vt::lua::getGlobalEngine();
    if (engine) {
        auto cmd = std::make_unique<vt::SetBPMCommand>(bpm);
        if (!engine->enqueueCommand(std::move(cmd))) {
            ofLogWarning("Clock") << "Failed to enqueue SetBPMCommand, falling back to direct call";
            $self->setBPM(bpm);  // <-- BAD: bypasses notifications
        }
    } else {
        $self->setBPM(bpm);
    }
}

// AFTER
void setBPM(float bpm) {
    auto* engine = vt::lua::getGlobalEngine();
    if (engine) {
        auto cmd = std::make_unique<vt::SetBPMCommand>(bpm);
        if (!engine->enqueueCommand(std::move(cmd))) {
            ofLogWarning("Clock") << "Command queue full, executing SetBPMCommand immediately";
            auto fallbackCmd = std::make_unique<vt::SetBPMCommand>(bpm);
            engine->executeCommandImmediate(std::move(fallbackCmd));
        }
    } else {
        ofLogWarning("Clock") << "Engine not available, using direct setBPM call";
        $self->setBPM(bpm);  // Only if engine truly unavailable
    }
}
```

### 2. Command.h - Add AddModuleCommand

**File:** `src/core/Command.h`

**Add after SetParameterCommand (around line 100):**
```cpp
class AddModuleCommand : public Command {
public:
    AddModuleCommand(const std::string& moduleType, const std::string& name)
        : moduleType_(moduleType), name_(name) {}

    Result execute(Engine* engine) override {
        if (!engine) {
            return Result::error("Engine is null");
        }
        auto& registry = engine->getModuleRegistry();
        return registry.addModule(moduleType_, name_);
    }

    std::string describe() const override {
        return "add " + moduleType_ + " " + name_;
    }

private:
    std::string moduleType_;
    std::string name_;
};
```

### 3. LuaHelpers.cpp - Standardize on Command Objects

**File:** `src/core/lua/LuaHelpers.cpp`

| Function | Line | Current | Change To |
|----------|------|---------|-----------|
| `createSampler()` | 43-44 | `executeCommand("add MultiSampler " + name)` | `enqueueCommand(make_unique<AddModuleCommand>("MultiSampler", name))` |
| `createSequencer()` | 80-81 | `executeCommand("add TrackerSequencer " + name)` | `enqueueCommand(make_unique<AddModuleCommand>("TrackerSequencer", name))` |
| `createSystemModule()` | 100+ | String command | Add `AddModuleCommand` variant |

**Diff for createSampler():**
```cpp
// BEFORE (lines 23-58)
std::string LuaHelpers::createSampler(const std::string& name, const std::map<std::string, std::string>& config) {
    // ... idempotent check ...

    // Module doesn't exist - create it
    std::string command = "add MultiSampler " + name;  // <-- STRING COMMAND
    auto result = engine_->executeCommand(command);

    // ... apply config ...
}

// AFTER
std::string LuaHelpers::createSampler(const std::string& name, const std::map<std::string, std::string>& config) {
    // ... idempotent check ...

    // Module doesn't exist - create it
    auto cmd = std::make_unique<AddModuleCommand>("MultiSampler", name);
    bool enqueued = engine_->enqueueCommand(std::move(cmd));

    if (!enqueued) {
        ofLogError("LuaHelpers") << "Failed to enqueue AddModuleCommand for sampler: " << name;
        return "";
    }

    // ... apply config via setParameter() (already correct) ...
}
```

### 4. Engine.h/.cpp - Add onStateChange Callback API

**File:** `src/core/Engine.h`

**Add to Engine class (around existing subscription methods):**
```cpp
// Callback type for state change notifications
using StateChangeCallback = std::function<void(const EngineState&)>;

// Register Lua callback for state changes
int registerStateChangeCallback(StateChangeCallback callback);

// Remove callback by ID
bool unregisterStateChangeCallback(int callbackId);
```

**File:** `src/core/Engine.cpp`

**Add implementation (around line 400):**
```cpp
int Engine::registerStateChangeCallback(StateChangeCallback callback) {
    std::lock_guard<std::mutex> lock(stateChangeMutex_);
    int id = nextCallbackId_++;
    stateChangeCallbacks_[id] = std::move(callback);
    return id;
}

bool Engine::unregisterStateChangeCallback(int callbackId) {
    std::lock_guard<std::mutex> lock(stateChangeMutex_);
    return stateChangeCallbacks_.erase(callbackId) > 0;
}
```

**Add to notifyObservers() (around line 340):**
```cpp
void Engine::notifyObservers(const EngineState& state) {
    // Existing observer notifications...

    // NEW: Invoke Lua state change callbacks
    {
        std::lock_guard<std::mutex> lock(stateChangeMutex_);
        for (const auto& [id, callback] : stateChangeCallbacks_) {
            try {
                callback(state);
            } catch (const std::exception& e) {
                ofLogError("Engine") << "State change callback exception: " << e.what();
            }
        }
    }
}
```

### 5. videoTracker.i - Expose onStateChange to Lua

**Add to %extend Engine section (around line 150):**
```cpp
// State change callbacks for "always synced" scripts
int onStateChange(std::function<void(const EngineState&)> callback) {
    return engine_->registerStateChangeCallback(callback);
}

bool removeStateChangeCallback(int callbackId) {
    return engine_->unregisterStateChangeCallback(callbackId);
}
```

### Summary of Changes

| File | Changes | Priority |
|------|---------|----------|
| `videoTracker.i` | Fix setBPM fallback, expose onStateChange | HIGH |
| `Command.h` | Add AddModuleCommand | HIGH |
| `LuaHelpers.cpp` | Refactor createSampler/ Sequencer to use AddModuleCommand | HIGH |
| `Engine.h` | Add callback registration methods | MEDIUM |
| `Engine.cpp` | Implement callback infrastructure | MEDIUM |

**Testing Strategy:**
1. Verify Clock::setBPM routes through executeCommandImmediate when queue full
2. Verify createSampler uses AddModuleCommand (check logs for "add MultiSampler")
3. Verify onStateChange callback fires when Engine state changes
4. Verify no regressions in existing fire-and-forget scripts

---

## Sources

### Primary (HIGH confidence - Source code analysis)

- **videoTracker.i** (`src/core/lua/videoTracker.i`) - SWIG interface, 427 lines, complete bindings
  - **Key lines:** 183-196 (Clock::setBPM fallback), 202-220 (Clock::start/stop), 281-288 (sampler helper)
- **Engine.cpp** (`src/core/Engine.cpp`) - setupLua(), eval(), syncScriptToEngine(), 1000+ lines
- **ScriptManager.cpp** (`src/core/ScriptManager.cpp`) - State observer, script generation, 621 lines
- **CodeShell.cpp** (`src/shell/CodeShell.cpp`) - executeLuaScript(), 1200+ lines
- **LuaHelpers.cpp** (`src/core/lua/LuaHelpers.cpp`) - Helper implementations, 290 lines
  - **Key lines:** 43-44 (createSampler string command), 80-81 (createSequencer string command), 206-222 (setParameter command pattern)
- **Command.h** (`src/core/Command.h`) - Command definitions, 305 lines

### Secondary (MEDIUM confidence - Prior phase research)

- **Phase 7.10.1 Research** (`.planning/phases/7.10-audit-simplify-scriptmanager/7.10-01-RESEARCH.md`) - ScriptManager audit, simplification patterns
- **Phase 6.5 Research** (`.planning/phases/06.5-fix-scripting-system-synchronization/06.5-RESEARCH.md`) - Event-driven synchronization patterns
- **STATE.md** (`.planning/STATE.md`) - Key decisions and current state
- **ROADMAP.md** (`.planning/ROADMAP.md`) - Phase requirements and context

### Tertiary (LOW confidence - Not directly verified)

- **swig.org** - SWIG documentation for binding patterns (general knowledge)
- **ofxLua** (openFrameworks addon) - Lua wrapper patterns (general knowledge)

---

## Metadata

### Confidence Assessment

| Area | Level | Reason |
|------|-------|--------|
| Standard Stack | HIGH | Based on source code analysis of working implementation |
| Architecture Patterns | HIGH | Verified through code inspection and prior phase research |
| Don't Hand-Roll | HIGH | Based on existing infrastructure and Phase 7.10.1 audit |
| Common Pitfalls | HIGH | Documented from crash analysis and implementation experience |
| Code Examples | HIGH | Directly from source files with verified context |
| **Specific Recommendations** | **HIGH** | Based on empirical analysis of LuaHelpers.cpp:43-44, 80-81, videoTracker.i:183-196 |
| **Always Synced Design** | **MEDIUM** | Extension of existing observer pattern, not yet implemented |

**Confidence breakdown by empirical finding:**
- Clock fallback inconsistency: HIGH (verified videoTracker.i:189-190)
- LuaHelpers string commands: HIGH (verified LuaHelpers.cpp:43-44, 80-81)
- ScriptManager one-way sync: HIGH (verified ScriptManager.cpp observer pattern)
- onStateChange API design: MEDIUM (proposed extension, not yet validated)

### Research Date and Validity

**Research date:** 2026-01-21
**Valid until:** 2026-02-21 (30 days - architecture is stable, unlikely to change)
**Review after:** Any changes to videoTracker.i, ScriptManager, or command queue

### Key Files Referenced

| File | Lines | Purpose |
|------|-------|---------|
| `src/core/lua/videoTracker.i` | 427 | SWIG interface - all bindings |
| `src/core/Engine.cpp` | 1000+ | Lua setup and execution |
| `src/core/ScriptManager.cpp` | 621 | State observation, script generation |
| `src/shell/CodeShell.cpp` | 1200+ | Script execution from editor |
| `src/core/Command.h` | 305 | Command definitions |
| `src/core/lua/LuaHelpers.cpp` | 290 | Helper implementations (KEY: lines 43-44, 80-81 for string command issue) |

---

*Research completed: 2026-01-21*
*Ready for planning*
