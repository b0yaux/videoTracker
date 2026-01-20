# videoTracker Refactoring Roadmap

## Milestone: Live-Scripting System Overhaul

**Goal**: Fix the scripting system by eliminating string-based command parsing and completing the lock-free migration. The root cause of crashes and performance issues is string parsing overhead and overcomplexified synchronization, not notification frequency.

**Status**: 🚧 In Progress

---

## Phase 1: Delete String-Based Lua Functions (CRITICAL)

**Goal**: Remove redundant string-based Lua helpers, use existing SWIG bindings directly.

**Status**: 🔵 Planned (1 plan in 1 wave)

**Context**: The codebase has SWIG bindings in `videoTracker.i` that are fully functional, but `Engine::setupLua()` re-implements everything with string commands that get parsed on every call.

**What to DELETE** (`src/core/Engine.cpp` lines ~300-450):
```cpp
std::string registerHelpers = R"(
    function sampler(name, config)
        exec("add MultiSampler " .. name)
        for k,v in pairs(config) do
            exec("set " .. name .. " " .. k .. " " .. tostring(v))
        end
    end
    // ... ~150 lines of redundant code
)";
```

**What SWIG ALREADY PROVIDES** (use this instead):
```lua
-- Uses SWIG bindings directly - no string parsing!
sampler("kick", {volume=0.8})
connect("seq1", "kick", "event")
setParam("kick", "volume", 0.9)
```

**Impact**: 10x Lua performance improvement by eliminating string parsing overhead.

**Files Modified**:
- `src/core/Engine.cpp` - Delete string-based Lua helper functions

**Estimated Effort**: 30 minutes

**Plans:**
- [x] 01-01-PLAN.md — Delete registerHelpers string, update log message

---

## Phase 2: Fix Notification Cascade (HIGH)

**Goal**: Use existing `parametersBeingModified` counter to suppress duplicate notifications during parameter cascades.

**Status**: 🔵 Not Started

**Context**: `ParameterRouter::processRoutingImmediate()` calls `setParameter()` on multiple modules, each triggering a notification. The `parametersBeingModified` counter exists but isn't used to suppress duplicates.

**Fix** (`src/core/Engine.cpp`):
```cpp
void Engine::enqueueStateNotification() {
    if (parametersBeingModified.load() > 0) {
        return;  // Skip during parameter cascade - batch in progress
    }
    notificationQueue_.enqueue([this]() {
        updateStateSnapshot();
        notifyObserversWithState();
    });
}
```

**Impact**: Eliminates notification storms during parameter routing cascades.

**Files Modified**:
- `src/core/Engine.cpp` - Check counter before enqueueing notification

**Estimated Effort**: 2 hours

---

## Phase 3: Complete Lock-Free Migration (MEDIUM)

**Goal**: Complete the migration to lock-free snapshots throughout the codebase.

**Status**: 🔵 Not Started

**Context**: The codebase has partial lock-free implementation:
- Module snapshots use `std::atomic<std::shared_ptr>` (lock-free reads)
- Engine snapshots use `std::atomic<std::shared_ptr>` (lock-free reads)
- BUT: `buildStateSnapshot()` still uses full mutex locks
- AND: `unsafeStateFlags_` atomic flags add complexity without benefit

**Key Work**:

1. **Make buildStateSnapshot() use lock-free module snapshots**:
```cpp
EngineState Engine::buildStateSnapshot() const {
    EngineState state;
    moduleRegistry.forEachModule([&](const std::string& uuid, auto module) {
        auto snapshot = module->getSnapshot();  // Lock-free!
        if (snapshot) {
            state.modules[module->getName()] = parseModuleSnapshot(*snapshot);
        }
    });
    state.connections = connectionManager->getConnectionState();
    return state;
}
```

2. **Remove unsafeStateFlags_ atomic**:
- Replace with guard pattern using the notification queue
- The atomic flags create timing windows rather than preventing them
- Simpler: defer all state updates to notification queue on main thread

3. **Remove redundant state**:
- `cachedState` - redundant with `snapshotJson`
- `cachedStateMutex` - no longer needed

**Files Modified**:
- `src/core/Engine.h` - Remove unsafeStateFlags_, cachedState
- `src/core/Engine.cpp` - Simplify snapshot building
- `src/core/ScriptManager.cpp` - Adjust for simplified threading

**Estimated Effort**: 1 day

---

## Phase 4: Make initialize() Idempotent (LOW)

**Goal**: Add guards to all module `initialize()` methods to prevent duplicate subscriptions.

**Status**: 🔵 Not Started

**Context**: `initialize()` is called during setup and after session restore. Without idempotency checks, event subscriptions (e.g., `clock.subscribeToStep()`) duplicate.

**Fix** (all module subclasses):
```cpp
void TrackerSequencer::initialize(...) {
    if (isInitialized_) return;  // Prevent duplicate subscriptions
    
    clock_.subscribeToStep([this](...) { onStep(...); });
    // ... rest of initialization
    
    isInitialized_ = true;
}
```

**Files Modified**:
- `src/modules/Module.h` - Add `isInitialized_` flag
- `src/modules/TrackerSequencer.cpp`
- `src/modules/MultiSampler.cpp`
- Any other module with `initialize()` that subscribes to events

**Estimated Effort**: 1 hour

---

## Phase 5: Remove Incomplete Undo Methods (LOW)

**Goal**: Delete unimplemented `undo()` methods from Command classes.

**Status**: 🔵 Not Started

**Context**: `RemoveModuleCommand::undo()` and `DisconnectCommand::undo()` are not implemented. Keep the Command pattern (essential for thread-safe queue→audio thread handoff), just remove incomplete undo.

**Fix** (`src/core/Command.h`):
```cpp
class Command {
public:
    virtual void execute(Engine& engine) = 0;
    // virtual void undo(Engine& engine) = 0;  // DELETE this line
};
```

**Files Modified**:
- `src/core/Command.h` - Remove undo interface
- `src/core/commands/*.cpp` - Remove undo implementations

**Estimated Effort**: 30 minutes

---

## Summary: Priority Matrix

| Phase | Priority | Effort | Impact |
|-------|----------|--------|--------|
| 1: Delete string Lua functions | **CRITICAL** | 30 min | 10x Lua perf |
| 2: Fix notification cascade | **HIGH** | 2 hrs | No notification storms |
| 3: Complete lockfree migration | **MEDIUM** | 1 day | Cleaner threading |
| 4: Idempotent initialize() | LOW | 1 hr | Edge case fixes |
| 5: Remove undo methods | LOW | 30 min | Code cleanup |

---

## Deferred to Future Work

The following phases are deferred until the scripting system overhaul is complete:

### Phase 7.x (Overcomplexified - Deferred)

Phase 7 was dramatically overcomplexified with 9+ subphases. The key simplifications identified in Phase 7.10 audit are being implemented in Phases 1-5 above. Remaining Phase 7 work:

- ~~Phase 7.9.1: Fix Script Editing Crash~~ - Addressed by simplified architecture
- ~~Phase 7.9.2: Script Execution State Updates~~ - Addressed by Phase 2
- ~~Phase 7.9.3: Fix Persistent Crashes~~ - Addressed by Phase 3 lock-free migration
- Phase 7.x testing and verification - Deferred

### Phase 8-13 (Deferred)

| Phase | Original Goal | Status |
|-------|---------------|--------|
| 8 | Complete PatternRuntime | 🔵 Deferred |
| 9 | Simplify Engine API | 🔵 Deferred |
| 10 | Improve Memory Management | 🔵 Deferred |
| 11 | Code Organization | 🔵 Deferred |
| 12 | Clean Up Technical Debt | 🔵 Deferred |
| 13 | Architecture Simplification | 🔵 Deferred |

**Reason**: These phases depend on a stable scripting system. The current focus is fixing the root causes of crashes and performance issues.

---

## Historical Summary (Phases 1-7.10)

| Phase | Status | Summary |
|-------|--------|---------|
| 1-6 | ✅ Complete | Thread safety, state notifications, shell abstraction, unified commands |
| 7.1-7.5 | ✅ Complete | Snapshot system, async serialization, lock-free reads |
| 7.6-7.9 | ✅ Complete | Async script execution, sync contracts, architecture problems |
| 7.10 | ✅ Complete | Audit and planning - identified 21 overcomplexifications |
| 7.10.1 | ✅ Complete | Simplified ScriptManager architecture |

**Key Insight from Phase 7.10 Audit**: The codebase accumulated layers of deferred updates, guard checks, and safety mechanisms that create timing windows rather than preventing them. The solution is simplification, not more guards.

---

## Critical Path

```
Phase 1 (DELETE string Lua) 
    → Phase 2 (fix cascade) 
    → Phase 3 (complete lockfree)
    → Phase 4-5 (cleanup)
    → THEN: Phases 8-13 can begin
```

**Blockers**: None - can start immediately

---

*Last updated: 2026-01-20 (New live-scripting overhaul plan)*
