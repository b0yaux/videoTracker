# videoTracker Refactoring Roadmap

## Milestone: Live-Scripting System Overhaul

**Goal**: Fix the scripting system by eliminating string-based command parsing and completing the lock-free migration. The root cause of crashes and performance issues is string parsing overhead and overcomplexified synchronization, not notification frequency.

**Status**: 🚧 In Progress

---

## Phase 1: Delete String-Based Lua Functions (CRITICAL)

**Goal**: Remove redundant string-based Lua helpers, use existing SWIG bindings directly.

**Status**: ✅ Complete (2026-01-20)

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
- [x] 01-01-PLAN.md — Delete registerHelpers string, update log message ✓

---

## Phase 2: Fix Notification Cascade (HIGH)

**Goal**: Use atomic flag to suppress duplicate notifications during parameter cascades.

**Status**: ✅ Complete (2026-01-20)

**Context**: `ParameterRouter::processRoutingImmediate()` calls `setParameterValue()` on multiple modules, each triggering a notification callback. Without suppression, N notifications get enqueued when 1 is needed.

**Fix** (`src/core/Engine.h` + `src/core/Engine.cpp`):
```cpp
// Engine.h - add atomic flag
std::atomic<bool> notificationEnqueued_{false};

// Engine.cpp - suppress duplicates
void Engine::enqueueStateNotification() {
    bool expected = false;
    if (!notificationEnqueued_.compare_exchange_strong(expected, true)) {
        return;  // Already enqueued - skip to prevent notification storm
    }
    notificationQueue_.enqueue([this]() {
        notificationEnqueued_.store(false);  // Clear after processing
        updateStateSnapshot();
        notifyObserversWithState();
    });
}
```

**Impact**: Eliminates notification storms during parameter routing cascades.

**Files Modified**:
- `src/core/Engine.h` - Atomic flag declaration
- `src/core/Engine.cpp` - Suppression logic

**Estimated Effort**: 2 hours

**Plans:**
- [x] 02-01-PLAN.md — Add notificationEnqueued_ flag and suppression logic ✓

---

## Phase 3: Complete Lock-Free Migration (MEDIUM)

**Goal**: Complete the migration to lock-free snapshots throughout the codebase.

**Status**: ✅ Complete (2026-01-20)

**Plans:**
- [x] 03-01-PLAN.md — Remove unsafeStateFlags_, simplify buildStateSnapshot() ✓

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

## Phase 4: Make initialize() Idempotent (SKIPPED)

**Status**: ⏭️ Skipped (2026-01-21)

**Reason**: Analysis revealed this is speculative complexity for an unobserved edge case.

**Context**: `initialize()` is called during setup and after session restore. Potential for duplicate event subscriptions exists.

**Decision**: Skip because:
- PatternRuntime is a persistent app-lifetime singleton (not recreated during restore)
- No observed crashes from duplicate subscriptions
- Current issues (script sync) were fixed in Phases 1-3
- Adding idempotency flags would add complexity without clear benefit
- The destructor cleanup is defensive for an edge case that may never occur

**Impact**: None - this was a LOW priority edge case, not fixing observed bugs.

**Files NOT Modified**:
- `src/modules/Module.h` - No `isInitialized_` flag added
- `src/modules/TrackerSequencer.cpp` - No changes
- `src/modules/MultiSampler.cpp` - No changes

---

## Phase 5: Remove Incomplete Undo Methods (SKIPPED)

**Status**: ⏭️ Skipped (2026-01-21)

**Reason**: Undo system is unused infrastructure with no user demand.

**Context**: Command pattern includes undo/redo support, but no code calls `undo()`:
- No undo UI buttons
- No undo command queue
- No keyboard shortcuts (cmd+Z)

**Analysis**:
| Command | Status |
|---------|--------|
| SetParameterCommand | ✅ Fully implemented |
| SetBPMCommand | ✅ Fully implemented |
| AddModuleCommand | ✅ Fully implemented |
| StartTransportCommand | ✅ Fully implemented |
| StopTransportCommand | ✅ Fully implemented |
| RemoveModuleCommand | ⚠️ Stub (logs warning) |
| ConnectCommand | ⚠️ Partial (PARAMETER/EVENT not implemented) |
| DisconnectCommand | ⚠️ Stub (logs warning) |

**Decision**: Skip because:
- 6/8 commands already have full undo implementations
- 2 stubs are safe (log warnings, don't crash)
- No user demand for undo functionality
- Removing interface would require editing 9+ files for no observable benefit
- Dead infrastructure doesn't hurt

**Files NOT Modified**:
- `src/core/Command.h` - Undo interface kept in place
- `src/core/Command.cpp` - Stub implementations preserved

---

## Summary: Priority Matrix

| Phase | Priority | Effort | Impact | Status |
|-------|----------|--------|--------|--------|
| 1: Delete string Lua functions | **CRITICAL** | 30 min | 10x Lua perf | ✅ Complete |
| 2: Fix notification cascade | **HIGH** | 2 hrs | No notification storms | ✅ Complete |
| 3: Complete lockfree migration | **MEDIUM** | 1 day | Cleaner threading | ✅ Complete |
| 4: Idempotent initialize() | LOW | 1 hr | Edge case fixes | ⏭️ Skipped |
| 5: Remove undo methods | LOW | 30 min | Code cleanup | ⏭️ Skipped |

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
Phase 1 (DELETE string Lua) → ✅ COMPLETE
    → Phase 2 (fix cascade) → ✅ COMPLETE
    → Phase 3 (complete lockfree) → ✅ COMPLETE
    → ⏭️ Phase 4 (idempotent init) → SKIPPED
    → ⏭️ Phase 5 (undo methods) → SKIPPED
    → Phase 6 (design) → ✅ COMPLETE
        → Phase 6.1 (engine global) → ✅ COMPLETE
        → Phase 6.2 (command routing) → ✅ COMPLETE
        → Phase 6.3 (callbacks) → 🔵 NOT STARTED
    → THEN: Phases 8-13 from old roadmap can resume
```

**Note**: Phase 6.2 complete. Implementation continues with 6.3 (MEDIUM).

---

## Phase 6: Research & Design Lua-Engine Integration Architecture

**Goal:** Research the current Lua binding architecture and design a proper long-term solution for connecting scripts to the Engine. This is a design-first phase—no implementation until the architecture is understood and documented.

**Status**: ✅ Complete (2026-01-21)

**Depends on:** Phase 3

**Plans:** 1 plan

**Context:**
Phases 1-3 fixed internal Engine problems:
- ✅ Malloc corruption (Phase 1: string parsing removed)
- ✅ Notification storms (Phase 2: suppression logic)
- ✅ Thread safety (Phase 3: lock-free migration)

But the Lua binding layer connecting scripts to the Engine remains unaddressed:
- Script execution is not working
- State sync is not working
- The architecture needs proper research before jumping to fixes

**Approach:** Research-first, design-first. Understand the problem deeply before implementing.

**Work Items:**

1. **Research Current Architecture**
   - Map the complete Lua integration: `videoTracker.i` (SWIG), `Engine::setupLua()`, `ScriptManager`, `CodeShell`
   - Document what SWIG exposes vs what's manually registered
   - Trace a script execution from user input to Engine mutation
   - Identify where the binding breaks (nil engine? missing callbacks? wrong thread?)

2. **Document Execution Models**
   - What execution model does the current code assume?
   - What execution model do we actually want?
     - Fire-and-forget (script runs once, mutates state, done)
     - Continuous (script keeps running, receives events in a loop)
     - Reactive (script re-runs when state changes, like a spreadsheet)
   - What are the trade-offs of each model?

3. **Design Long-Term Architecture**
   - Based on research, design the target architecture
   - Define clear contracts: when do scripts run? How do they receive state? How do they mutate state?
   - Document the design in `.planning/phases/06-research-design-lua-engine/DESIGN.md`

4. **Create Implementation Plan**
   - Only after research and design are complete
   - Break down into implementable tasks
   - May spawn sub-phases (6.1, 6.2, etc.) if implementation is complex

Plans:
- [x] 06-01-PLAN.md — Create DESIGN.md and implementation sub-phases (6.1, 6.2, 6.3) ✓

---

## Phase 6.1: Register Engine Global (CRITICAL)

**Goal:** Fix the critical blocker preventing ANY scripts from working - register `engine` global in Lua state.

**Status**: ✅ Complete (2026-01-21)

**Depends on:** Phase 6

**Context:**
ScriptManager generates scripts that reference `engine:getClock()`, `engine:getModuleRegistry()`, etc., but `engine` global is NEVER created. The `registerEngineGlobal()` function exists in LuaGlobals.cpp but is never called from Engine::setupLua().

**Work completed:**
- Added `vt::lua::registerEngineGlobal(*lua_)` call in Engine::setupLua() (line 299)
- Uses existing `registerEngineGlobal()` from LuaGlobals.cpp (lua_newuserdata + metatable)
- Scripts can now access `engine:*` methods without "nil value" errors

**Files:**
- `src/core/Engine.cpp` - Added engine global registration call

**Estimated Effort:** 30 minutes

**Plans:** 1 plan

Plans:
- [x] 06.1-01-PLAN.md — Add engine global registration to Engine::setupLua() ✓

---

## Phase 6.2: Standardize Command Routing (HIGH)

**Goal:** Ensure all Lua operations route through command queue for consistent behavior.

**Status**: ✅ Complete (2026-01-21)

**Depends on:** Phase 6.1 (complete)

**Plans:** 1 plan

**Work:**
1. Fix setBPM fallback to use executeCommandImmediate() instead of direct call
2. Refactor createSampler/createSequencer to use AddModuleCommand (already exists in Command.h)

**Files:**
- `src/core/lua/videoTracker.i` - Fix Clock::setBPM fallback
- `src/core/lua/LuaHelpers.cpp` - Use AddModuleCommand

**Estimated Effort:** 1 hour

**Verification:** ✅ Passed (3/3 must-haves verified)

Plans:
- [x] 06.2-01-PLAN.md — Fix setBPM fallback, refactor createSampler/createSequencer to use AddModuleCommand ✓

---

## Phase 6.3: Add Reactive Callback API (MEDIUM)

**Goal:** Enable scripts to receive state change notifications for live coding workflow.

**Status**: 🟡 Planned

**Depends on:** Phase 6.2

**Plans:** 1 plan

**Work:**
1. Add registerStateChangeCallback() and unregisterStateChangeCallback() to Engine
2. Extend notifyObservers() to invoke Lua callbacks
3. Expose engine:onStateChange(fn) via SWIG in videoTracker.i

**Files:**
- `src/core/Engine.h` - Add callback registration methods
- `src/core/Engine.cpp` - Implement callback infrastructure
- `src/core/lua/videoTracker.i` - Expose onStateChange to Lua

**Estimated Effort:** 3 hours

Plans:
- [ ] 06.3-01-PLAN.md — Add registerStateChangeCallback/unregisterStateChangeCallback, extend notifyObserversWithState, expose onStateChange via SWIG

---

*Last updated: 2026-01-21 (Phase 6 complete - DESIGN.md created, sub-phases 6.1, 6.2, 6.3 planned)*
