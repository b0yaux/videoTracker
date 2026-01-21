# Phase 06: Lua-Engine Integration Architecture - Design Document

**Status:** Target Architecture Design
**Based on:** 06-RESEARCH.md findings, 06-CONTEXT.md decisions
**Created:** 2026-01-21

---

## Overview

The target Lua-Engine integration architecture uses **SWIG bindings** for type-safe C++ class exposure, **command queue** for thread-safe state mutations, and **state observer pattern** for reactive script synchronization. This design addresses the current architecture problems identified in research while enabling the live-coding workflow specified in CONTEXT.md.

**Core Pattern:** Fire-and-forget for script execution + reactive callbacks for state synchronization

---

## Current Architecture Problems

Summarized from 06-RESEARCH.md:

### 1. Engine Global NEVER Created (CRITICAL BLOCKER)

**Impact:** ALL ScriptManager-generated scripts fail immediately.

- ScriptManager generates scripts containing `engine:getClock()`, `engine:getModuleRegistry()`
- `registerEngineGlobal()` function exists in `videoTracker.i` (lines 30-39) but is NEVER CALLED
- `Engine::setupLua()` sets `vt::lua::setGlobalEngine(this)` for C++ helpers but does NOT create Lua global
- Result: Lua error "attempt to index a nil value (global 'engine')"

### 2. LuaHelpers Use String Commands Instead of Command Objects

**Impact:** Inconsistent state mutation patterns, bypasses command queue.

| Helper Method | Current Pattern | Should Use |
|---------------|-----------------|------------|
| `createSampler()` | `"add MultiSampler "` string | `AddModuleCommand` |
| `createSequencer()` | `"add TrackerSequencer "` string | `AddModuleCommand` |
| `setParameter()` | `SetParameterCommand` | Correct |

### 3. setBPM Fallback Bypasses Engine Notifications

**Impact:** Inconsistent notification behavior when queue is full.

- `Clock::setBPM()` in videoTracker.i uses direct `$self->setBPM(bpm)` fallback
- `Clock::start()` and `Clock::stop()` correctly use `executeCommandImmediate()`
- Direct calls bypass Engine's notification system

### 4. No Reactive Callback Mechanism for Live Coding

**Impact:** Scripts cannot receive state updates from external changes.

- Current architecture is ONE-WAY: Engine → ScriptManager → CodeShell
- Scripts run once (fire-and-forget), then become stale
- No mechanism for scripts to stay synced with UI/other shell changes
- Live-coding workflow requires reactive sync (from CONTEXT.md)

---

## Target Architecture

### Lua State Initialization

Engine::setupLua() must perform complete Lua initialization:

1. Create ofxLua instance
2. Set `vt::lua::setGlobalEngine(this)` for C++ helpers  
3. Register `exec` function for command execution
4. **NEW: Create `engine` global for script access**

```cpp
// In Engine::setupLua(), add after line 295:
if (lua_ && lua_->isValid()) {
    lua_State* L = *lua_;
    lua_register(L, "exec", lua_execCommand);
    
    // CRITICAL: Create 'engine' global for ScriptManager-generated scripts
    // This makes engine:* bindings available in all Lua scripts
    SWIG_NewPointerObj(L, this, SWIGTYPE_p_vt__Engine, 0);
    lua_setglobal(L, "engine");
    
    ofLogNotice("Engine") << "Lua initialized with engine global";
}
```

**Result:** Scripts can access:
- `engine:getClock()` - Clock subsystem
- `engine:getModuleRegistry()` - Module access
- `engine:getConnectionManager()` - Connection routing
- `engine:getParameterRouter()` - Parameter modulation
- `engine:getPatternRuntime()` - Pattern playback

### LuaGlobals provides C++ access via getGlobalEngine()

SWIG helper functions (not scripts) continue using `vt::lua::getGlobalEngine()` for Engine pointer access. This is separate from the Lua `engine` global.

---

## Execution Model Contracts

### Fire-and-Forget (Initial Execution)

For one-shot scripts that mutate state and complete:

```
User types script → CodeShell
                        ↓
                   Engine::eval()
                        ↓
                   SWIG bindings execute
                        ↓
                   Commands enqueued to command queue
                        ↓
                   Audio thread processes commands
                        ↓
                   State changes → Notification queue
                        ↓
                   Observers receive updates asynchronously
```

**Contract:**
- Script runs once via Engine::eval()
- Commands enqueued to lock-free command queue
- State updates happen asynchronously via notification queue
- Callback returns immediately with success/failure
- Script does NOT wait for state to settle

**Usage:**
```lua
-- Fire-and-forget: Create modules, set up routing, configure parameters
sampler("kick", {volume = 0.8})
sequencer("seq1")
connect("seq1", "kick", "event")

-- Script completes immediately, state updates in background
```

### Reactive Sync (Live Coding)

For scripts that need to stay synchronized with Engine state:

```
Script registers callback → engine:onStateChange(fn)
                                    ↓
                            Engine stores callback reference
                                    ↓
         [State changes from ANY source: UI, other shells, audio thread]
                                    ↓
                            Engine::notifyObservers()
                                    ↓
                            Callback invoked with EngineState snapshot
                                    ↓
                            Script updates local variables
```

**Contract:**
- Script registers callback via `engine:onStateChange(fn)`
- Callback invoked when Engine state changes (from ANY source)
- Callback receives immutable EngineState snapshot
- Callback returns integer ID for unregistration
- Used for scripts that need to stay synced with external changes

**Usage:**
```lua
-- Reactive sync: Keep local variables synchronized
local callbackId = engine:onStateChange(function(state)
    currentBPM = state.clock.bpm
    if state.modules.kick then
        kickVolume = state.modules.kick.parameters.volume
    end
end)

-- Later, to stop receiving updates:
engine:removeStateChangeCallback(callbackId)
```

---

## State Sync Architecture

The complete state synchronization flow:

```
User Edit → Engine::eval() → Command Queue → Audio Thread
                                                  ↓
                                           State Changed
                                                  ↓
Observers ← Notification Queue ← updateStateSnapshot()
    ↓
ScriptManager regenerates script (VIEW mode display)
    ↓
Lua callbacks invoked (if registered via onStateChange)
    ↓
All CodeShells display consistent Engine state
```

**Key Invariants:**
1. ALL state mutations route through command queue
2. State snapshots are immutable (copy-on-read)
3. Observers receive complete EngineState, not deltas
4. ScriptManager regeneration and Lua callbacks are parallel paths
5. No cross-thread access to mutable state

---

## Command Routing

ALL state mutations from Lua MUST route through command queue:

### Current Commands (from Command.h)

| Command | Purpose | Status |
|---------|---------|--------|
| `SetParameterCommand` | Module parameter updates | Correct |
| `SetBPMCommand` | Clock tempo changes | Correct |
| `StartTransportCommand` | Start playback | Correct |
| `StopTransportCommand` | Stop playback | Correct |
| `ConnectCommand` | Create connections | Correct |
| `DisconnectCommand` | Remove connections | Correct |
| `AddModuleCommand` | Module creation | **NEW - Required** |
| `RemoveModuleCommand` | Module deletion | Exists |

### New: AddModuleCommand

Required for consistent module creation:

```cpp
class AddModuleCommand : public Command {
public:
    AddModuleCommand(const std::string& moduleType, const std::string& name)
        : moduleType_(moduleType), name_(name) {}
    
    Result execute(Engine* engine) override {
        if (!engine) return Result::error("Engine is null");
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

### Fallback Pattern

When command queue is full, fallback uses `executeCommandImmediate()`, NEVER direct method calls:

```cpp
// CORRECT fallback pattern:
if (!engine->enqueueCommand(std::move(cmd))) {
    ofLogWarning("Module") << "Queue full, executing immediately";
    auto fallbackCmd = std::make_unique<SomeCommand>(args);
    engine->executeCommandImmediate(std::move(fallbackCmd));
}

// WRONG fallback pattern (bypasses notifications):
if (!engine->enqueueCommand(std::move(cmd))) {
    $self->directMethodCall();  // BAD - bypasses Engine
}
```

---

## API Contracts

### engine:onStateChange(callback) → callbackId

Register a Lua function to receive state change notifications.

**Signature:**
```lua
local callbackId = engine:onStateChange(function(state)
    -- state is complete EngineState snapshot
    -- Called whenever Engine state changes
end)
```

**Contract:**
- Returns integer ID for later unregistration
- Callback receives EngineState (immutable snapshot)
- Callback invoked on main thread (safe for Lua)
- Multiple callbacks supported (each gets unique ID)
- Callback exceptions are caught and logged (don't crash Engine)

**Implementation Location:** 
- `Engine.h` - Add `registerStateChangeCallback(callback)` method
- `Engine.cpp` - Callback storage and invocation in `notifyObservers()`
- `videoTracker.i` - Expose `onStateChange(fn)` to Lua

### engine:removeStateChangeCallback(callbackId) → boolean

Remove a previously registered callback.

**Signature:**
```lua
local success = engine:removeStateChangeCallback(callbackId)
```

**Contract:**
- Returns true if callback found and removed
- Returns false if callbackId not found
- Safe to call with invalid ID (no crash)

---

## Implementation Priority

Based on research findings and critical path analysis:

### Phase 6.1: Register Engine Global (CRITICAL)

**Goal:** Unblock ALL ScriptManager-generated scripts.

**Work:**
- Add engine global registration to `Engine::setupLua()` after line 295
- Use `SWIG_NewPointerObj()` to create userdata
- Verify scripts can access `engine:*` methods

**Estimated Effort:** 30 minutes

**Verification:**
```lua
if engine and type(engine) == "userdata" then
    print("SUCCESS: engine global available")
end
```

### Phase 6.2: Standardize Command Routing (HIGH)

**Goal:** Ensure all Lua operations route through command queue for consistent behavior.

**Work:**
1. Fix `setBPM` fallback to use `executeCommandImmediate()` instead of direct call
2. Add `AddModuleCommand` to Command.h
3. Refactor `createSampler`/`createSequencer` to use `AddModuleCommand`

**Estimated Effort:** 2 hours

### Phase 6.3: Add Reactive Callback API (MEDIUM)

**Goal:** Enable scripts to receive state change notifications for live coding workflow.

**Work:**
1. Add `registerStateChangeCallback()` and `unregisterStateChangeCallback()` to Engine
2. Extend `notifyObservers()` to invoke Lua callbacks
3. Expose `engine:onStateChange(fn)` via SWIG in videoTracker.i

**Estimated Effort:** 3 hours

---

## Decisions from CONTEXT.md

All user decisions from Phase 6 context gathering are addressed:

| Decision | Design Element | Location |
|----------|----------------|----------|
| Fire-and-forget for initial execution | Engine::eval() pattern | Execution Model Contracts |
| Reactive callbacks for live coding | engine:onStateChange(fn) API | API Contracts |
| Full state sync | EngineState passed to callbacks | State Sync Architecture |
| All shells display same Engine state | Observer pattern + notification queue | State Sync Architecture |

---

## Decision Verification

| Decision from CONTEXT.md | Addressed In | Notes |
|--------------------------|--------------|-------|
| Fire-and-forget for initial execution | Execution Model Contracts | Engine::eval() returns immediately, state updates async |
| Reactive callbacks for live coding | API Contracts | engine:onStateChange(fn) enables reactive sync |
| Full state sync | State Sync Architecture | EngineState passed to callbacks contains complete state |
| All shells display same Engine state | State Sync Architecture | Single source of truth via observer pattern, notification queue |

---

*Phase: 06-research-design-lua-engine-integration*
*Design Document Created: 2026-01-21*
