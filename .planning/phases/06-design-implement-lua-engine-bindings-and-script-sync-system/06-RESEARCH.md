# Phase 06: Research & Design Lua-Engine Integration Architecture - Research

**Researched:** 2026-01-21
**Domain:** Lua binding architecture, SWIG bindings, ScriptManager, CodeShell, Engine integration
**Confidence:** HIGH (based on source code analysis, prior phase research, and documented decisions)

---

## Summary

This research investigates the Lua binding architecture connecting scripts to the Engine in the videoTracker project. The codebase has evolved through multiple phases of simplification (Phases 7.9-7.10, 1-3), eliminating deferred update layers and async script execution threads. The current architecture uses **SWIG-generated bindings** in `videoTracker.i` for C++ class exposure, **ofxLua** for Lua state management, and **lock-free command queues** for thread-safe state mutations.

**Key Findings:**

1. **SWIG provides complete bindings** - All Engine subsystems (Clock, ModuleRegistry, ConnectionManager, PatternRuntime) are exposed via `videoTracker.i` with helper functions for declarative syntax (`sampler()`, `sequencer()`, `connect()`)

2. **ScriptManager generates scripts from state** - It's a read-only observer that regenerates Lua when Engine state changes, NOT a direct script executor

3. **CodeShell executes scripts via Engine::eval()** - User input flows: CodeShell → Engine::eval() → ofxLua → SWIG bindings → Command execution → State notification → ScriptManager regeneration

4. **Single atomic guard pattern replaces complex deferred updates** - ScriptManager uses `UpdateState` atomic state machine instead of multiple deferred layers

5. **Thread safety model is consistent** - Commands route through `moodycamel::BlockingConcurrentQueue`, state snapshots use atomic shared_ptr, notifications process on main thread

**Primary Recommendation:** The current architecture is sound after Phases 1-3 simplifications. Focus implementation on:
- Ensuring SWIG helper functions always route through command queue (some bypass via `executeCommand()`)
- Documenting the sync contract clearly (fire-and-forget execution, state updates async)
- Adding integration tests for the complete execution path

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

### Question 1: Should all Clock methods use executeCommandImmediate() fallback?

**What we know:**
- Current fallback in SWIG bindings calls `$self->setBPM(bpm)` directly when queue is full
- This bypasses command queue and may cause thread safety issues

**What's unclear:**
- When does `enqueueCommand()` actually fail? Queue capacity is 1024 commands
- Is the fallback path ever hit in practice?

**Recommendation:**
- Add logging to track fallback usage frequency
- If fallback is never hit, remove it to simplify code
- If fallback is hit, investigate why queue is full

### Question 2: Should LuaHelpers use SetParameterCommand or executeCommand()?

**What we know:**
- `LuaHelpers::setParameter()` uses `SetParameterCommand` via `enqueueCommand()`
- Other helpers (createSampler, createSequencer, connect) use `executeCommand()` with string commands
- String parsing overhead was the root cause of Phase 1

**What's unclear:**
- Why don't all helpers use command objects?
- Is there a benefit to string commands for module creation?

**Recommendation:**
- Standardize all helpers to use command objects for consistency
- String commands should only be used for the high-level `exec()` function
- Document the contract: helpers use commands, exec() parses strings

### Question 3: What should the sync contract be for script execution?

**What we know:**
- Current implementation is "fire-and-forget" - eval() returns immediately, state updates async
- `syncScriptToEngine()` was removed because it caused deadlocks
- Callback fires when script execution completes, not when state is updated

**What's unclear:**
- Is fire-and-forget the right contract for all use cases?
- Should there be a way to wait for state stabilization?
- How should errors in command execution be reported to the script?

**Recommendation:**
- Document the current contract clearly: "Script executes, commands enqueued, state updates async"
- Add integration test to verify the contract
- Consider adding optional callback for state-ready if use cases emerge

---

## Sources

### Primary (HIGH confidence - Source code analysis)

- **videoTracker.i** (`src/core/lua/videoTracker.i`) - SWIG interface, 427 lines, complete bindings
- **Engine.cpp** (`src/core/Engine.cpp`) - setupLua(), eval(), syncScriptToEngine(), 1000+ lines
- **ScriptManager.cpp** (`src/core/ScriptManager.cpp`) - State observer, script generation, 621 lines
- **CodeShell.cpp** (`src/shell/CodeShell.cpp`) - executeLuaScript(), 1200+ lines

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
| `src/core/lua/LuaHelpers.cpp` | 290 | Helper implementations |

---

*Research completed: 2026-01-21*
*Ready for planning*
