# Project State

## Current Position

**Primary Milestone**: Live-Scripting System Overhaul
**Current Phase**: Phase 6.2 Complete - Ready for Phase 6.3
**Status**: ✅ Phase 6.2 Complete

**Next Steps:**
1. ✅ Plan Phase 1: Delete String-Based Lua Functions
2. ✅ Execute Phase 1: Delete registerHelpers string from Engine.cpp
3. ✅ Plan Phase 2: Fix Notification Cascade
4. ✅ Execute Phase 2: Add notification suppression (02-01)
5. ✅ Plan Phase 3: Complete Lock-Free Migration
6. ✅ Execute Phase 3: Remove unsafeStateFlags_ (03-01) - COMPLETE
7. ⏭️ **Phase 4 SKIPPED**: Analysis showed speculative complexity for unobserved edge case
8. ⏭️ **Phase 5 SKIPPED**: Undo system is unused infrastructure
9. ✅ **Phase 6 COMPLETE**: DESIGN.md and implementation sub-phases created
10. ✅ **Phase 6.1 COMPLETE**: Register Engine Global (CRITICAL blocker fixed)
11. ✅ **Phase 6.2 COMPLETE**: Standardize Command Routing (setBPM, createSampler, createSequencer)
12. **Next**: Phase 6.3 - Add Reactive Callback API (MEDIUM)

**Next Phase:** Phase 6.3 - Add Reactive Callback API

**Progress**: ████████████░ 94% (15/16 plans complete)

---

## Recent Progress

### 2026-01-21: Phase 6.2-01 COMPLETE - Standardize Command Routing

**Summary**: Standardized command routing so ALL Lua operations use the command queue for consistent behavior and thread safety.

**What was fixed**:

**Task 1 - Clock::setBPM fallback:**
- Replaced direct `$self->setBPM(bpm)` call with `engine->executeCommandImmediate()` for queue-full fallback
- Now matches pattern used by Clock::start() and Clock::stop()
- Ensures state notifications are sent even when command queue is full
- Direct call only remains for engine-not-available edge case

**Task 2 - createSampler/createSequencer:**
- Replaced string commands (`"add MultiSampler " + name`) with `AddModuleCommand` via `enqueueCommand()`
- Primary path now uses command queue for consistent thread safety
- String commands retained as fallback when queue is full
- Same pattern applied to both createSampler and createSequencer

**Files modified:**
- `src/core/lua/videoTracker.i` - setBPM fallback fix
- `src/core/lua/LuaHelpers.cpp` - createSampler/createSequencer refactor

**Build verification:** ✅ Successful (no errors)

**Decision**: Skip Phase 5 (Remove Incomplete Undo Methods) as the undo system is not implemented or used.

**Analysis Findings**:
- No code calls `undo()` - grep found zero call sites
- No undo UI buttons, command queue, or keyboard shortcuts (cmd+Z)
- 6/8 commands already have full undo implementations
- 2 stubs (RemoveModuleCommand, DisconnectCommand) are safe (log warnings, don't crash)
- Dead infrastructure doesn't cause bugs, removing it introduces change risk

**Impact**: No code changes. Undo interface preserved for potential future use.

### 2026-01-21: Phase 6.1 COMPLETE - Register Engine Global (CRITICAL)

**Summary**: Fixed the critical blocker preventing ANY scripts from working.

**What was fixed**:
- Added `vt::lua::registerEngineGlobal(*lua_)` call in `Engine::setupLua()` (line 299)
- Uses existing `registerEngineGlobal()` from LuaGlobals.cpp (no SWIG required)
- Creates engine userdata with vt_Engine metatable
- Scripts can now access `engine:*` methods without "nil value" errors

**Technical correction**:
- Original plan used SWIG internals (`SWIGTYPE_p_vt__Engine`)
- Fixed to call existing `registerEngineGlobal()` from LuaGlobals.cpp
- Uses `lua_newuserdata()` + metatable (cleaner approach)

**Files modified**:
- `src/core/Engine.cpp` - 2 lines added (comment + function call)

### 2026-01-21: Phase 4 SKIPPED - Analysis Shows Speculative Complexity

**Decision**: Skip Phase 4 (Make initialize() Idempotent) as it adds flags for an unobserved edge case.

**Analysis Findings**:
- PatternRuntime is persistent (app-lifetime singleton), not recreated during session restore
- `isRestored` parameter on `initialize()` already exists for this purpose
- No crashes observed from duplicate subscriptions
- Current issues (script sync, malloc corruption) were fixed in Phases 1-3
- Adding `isInitialized_` and `patternTriggerListenerRegistered_` flags would be overcomplexification

**Impact**: No code changes. Analysis preserved in `.planning/phases/04-make-initialize-idempotent/`.

### 2026-01-20: New Roadmap Created - Live-Scripting System Overhaul

**Roadmap Restructure**: Created new milestone-level roadmap that focuses on the actual root causes:
- Phase 1: Delete String-Based Lua Functions (CRITICAL) - ~10x Lua performance improvement
- Phase 2: Fix Notification Cascade (HIGH) - Eliminate notification storms during parameter routing
- Phase 3: Complete Lock-Free Migration (MEDIUM) - Simplify threading model
- Phase 4: ~~Make initialize() Idempotent (LOW)~~ - ⏭️ SKIPPED
- Phase 5: ~~Remove Incomplete Undo Methods (LOW)~~ - ⏭️ SKIPPED

**Key Insight**: The 7.x phases were dramatically overcomplexified. The new roadmap consolidates to focused phases targeting the real root causes. Phases 4 and 5 were later skipped as speculative complexity.

### 2026-01-16: Phase 7.10.1 COMPLETE - Simplified ScriptManager Architecture

**Summary**:
- Simplified ScriptManager (removed 3 deferred update layers, single atomic guard)
- Simplified CodeShell (reduced EditorMode from 3 to 2 states)
- Removed async script execution (synchronous execution only)
- Cleaned up 60+ "CRITICAL FIX" comments
- ~40% code reduction achieved

**Files Modified**:
- `src/core/Engine.h/.cpp` - Removed async infrastructure
- `src/core/ScriptManager.h/.cpp` - Simplified implementation
- `src/shell/CodeShell.h/.cpp` - Simplified synchronization

**Legacy Context**: Phase 7.10.1 work is complete and its insights informed the new roadmap structure.

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

**Note**: Phase 6.2 complete. Ready to start Phase 6.3 (Add Reactive Callback API).

**Blockers**: None - ready for Phase 6.3

---

## Roadmap Evolution

- Phase 6 revised: Research & Design Lua-Engine Integration Architecture (research-first approach, 2026-01-21)

---

## Key Decisions (Consolidated)

| Decision | Rationale | Status |
|----------|-----------|--------|
| **Fix root causes, not symptoms** | String parsing overhead causes 10x performance hit | ✅ Confirmed |
| **Use SWIG bindings directly** | videoTracker.i provides all helper functions | ✅ Confirmed |
| **Headless Engine pattern** | Engine has no UI dependencies, Shells query via snapshots | ✅ Confirmed |
| **Lock-free command queue** | moodycamel::BlockingConcurrentQueue for thread safety | ✅ Confirmed |
| **Immutable state snapshots** | Observers read copies for thread safety | ✅ Confirmed |
| **Command-based state changes** | All state mutations route through command queue | ✅ Confirmed |
| **Synchronous script execution** | Removed async execution, simplified to sync only | ✅ Confirmed (7.10.1) |
| **Delete registerHelpers string** | ~160 lines removed, eliminate string parsing overhead | ✅ Confirmed (01-01) |
| **Notification suppression** | Compare-exchange prevents duplicate notifications during cascades | ✅ Confirmed (02-01) |
| **Remove unsafeStateFlags_** | Simplified state detection using notification queue guard | ✅ Confirmed (03-01) |
| **Convenience methods** | isExecutingScript/commandsBeingProcessed delegate to isInUnsafeState() | ✅ Confirmed (03-02) |
| **Skip Phase 4 idempotency** | PatternRuntime is persistent, no observed bugs, adding flags is overcomplexification | ⏭️ Confirmed (2026-01-21) |
| **Skip Phase 5 undo methods** | Undo system is unused infrastructure, no user demand, dead code doesn't hurt | ⏭️ Confirmed (2026-01-21) |
| **Fire-and-forget execution** | Scripts run once via Engine::eval(), state updates async | ✅ Confirmed (06-01) |
| **Reactive callbacks for live coding** | engine:onStateChange(fn) enables scripts to sync with external changes | ✅ Confirmed (06-01) |
| **Engine global is CRITICAL blocker** | registerEngineGlobal() never called - must fix in Phase 6.1 | ✅ Confirmed (06-01) |
| **AddModuleCommand needed** | Standardize module creation to match SetParameterCommand pattern | ✅ Confirmed (06-01) |
| **Use executeCommandImmediate for setBPM fallback** | Ensures state notifications are sent even when queue is full | ✅ Confirmed (06.2-01) |

---

## Architecture Summary

### Current State (After Phase 1 Complete)
- Engine: Headless, command-based, simplified synchronization
- ScriptManager: Single atomic guard, no deferred update layers
- CodeShell: 2 EditorMode states (VIEW, EDIT), no LOCKED mode
- State: Snapshots via atomic pointers, lock-free reads
- Commands: Unified queue, all mutations route through it
- Lua: setupLua() only registers exec(), helpers via SWIG bindings

### Phase 2 Complete
- ✅ Added atomic notificationEnqueued_ flag with compare-exchange suppression
- enqueueStateNotification() prevents duplicate notifications during parameter cascades
- Flag set before enqueue, cleared after callback executes
- Expected: Eliminates notification storms during parameter routing

### Phase 3 Complete (03-01 + 03-02)
- ✅ Phase 3.01: Removed unsafeStateFlags_ atomic from Engine.h
- ✅ Phase 3.01: Removed UnsafeState enum from Engine.h
- ✅ Phase 3.01: Removed isExecutingScript() and commandsBeingProcessed() methods
- ✅ Phase 3.01: Simplified isInUnsafeState() to use notifyingObservers_ pattern
- ✅ Phase 3.01: Removed setUnsafeState/hasUnsafeState implementations from Engine.cpp
- ✅ Phase 3.01: Replaced all setUnsafeState calls with notifyingObservers_ store operations
- ✅ Phase 3.01: Replaced all hasUnsafeState calls with notifyingObservers_ load operations
- ✅ Phase 3.01: Updated buildStateSnapshot() comments to reflect Phase 3 simplification
- ✅ Phase 3.02: Added isExecutingScript() convenience method delegating to isInUnsafeState()
- ✅ Phase 3.02: Added commandsBeingProcessed() convenience method delegating to isInUnsafeState()
- ✅ Phase 3.02: Restored backward compatibility for 5 call sites across 4 files
- **Result**: -60 lines of code, simplified state detection, maintained API compatibility

### Immediate Work (Phases 1-3 Complete, 4-5 Skipped)
- ✅ Plan Phase 3: Complete Lock-Free Migration (03-01-PLAN.md created)
- ✅ Execute Phase 3: Remove unsafeStateFlags_ (03-01-PLAN.md executed)
- ✅ Execute Phase 3: Add convenience methods (03-02-PLAN.md executed)
- ⏭️ Phase 4: Skipped - no code changes, speculative complexity for unobserved edge case
- ⏭️ Phase 5: Skipped - undo system is unused infrastructure
- **Next**: Resume old roadmap phases 8-13 (PatternRuntime completion, Engine API simplification, etc.)

---

## Blockers

None currently.

---

## Open Questions

- None for Phase 1 (work is clearly defined)

---

*Last updated: 2026-01-21 (Phase 6.2-01 complete - Standardize Command Routing)*
