---
phase: "7"
plan: "01"
subsystem: "core"
tags: ["moodycamel", "queue", "thread-safety", "concurrency"]
requires: ["6.3"]
provides: "MPMC command queue eliminating undefined behavior"
affects: ["future Phase 7 sub-plans"]
tech-stack:
  added: []
  patterns: ["lock-free MPMC queue"]
key-files:
  created: []
  modified: ["src/core/Engine.h"]
decisions: []
---

# Phase 7 Plan 1: Replace SPSC Queue with MPMC Queue Summary

**Critical fix:** Replaced `moodycamel::ReaderWriterQueue` (SPSC) with `moodycamel::ConcurrentQueue` (MPMC) to eliminate undefined behavior.

## What Was Done

### Task 1: Update Engine.h Include and Declaration ✅

**File:** `src/core/Engine.h`

**Changes:**
1. **Line 24:** Updated include
   ```cpp
   // BEFORE
   #include "readerwriterqueue.h"
   
   // AFTER  
   #include "concurrentqueue.h"
   ```

2. **Line 633:** Updated queue declaration
   ```cpp
   // BEFORE
   moodycamel::ReaderWriterQueue<std::unique_ptr<Command>> commandQueue_{1024};
   
   // AFTER
   moodycamel::ConcurrentQueue<std::unique_ptr<Command>> commandQueue_{1024};
   ```

### Task 2: Verify Engine.cpp Compatibility ✅

**Verification:** Confirmed Engine.cpp uses compatible API:
- `commandQueue_.try_enqueue(std::move(cmd))` - Same signature ✅
- `commandQueue_.try_dequeue(cmd)` - Same signature ✅
- `commandQueue_.size_approx()` - Same signature ✅

**Result:** No changes required to Engine.cpp.

### Task 3: Update Comment (Documentation) ✅

**File:** `src/core/Engine.h` (lines 630-634)

**Updated comment:**
```cpp
// BEFORE
// Unified command queue (lock-free, processed in audio thread)
// Producer: GUI thread (enqueueCommand)
// Consumer: Audio thread (processCommands)

// AFTER
// Unified command queue (lock-free MPMC, processed in audio thread)
// Producers: GUI thread, Lua scripts, parameter router (multiple)
// Consumer: Audio thread (processCommands)
```

## Problem Solved

The command queue was using `moodycamel::ReaderWriterQueue` which is explicitly **Single-Producer Single-Consumer (SPSC)**, but the codebase has **6+ producers**:

1. `ClockGUI.cpp` - Transport controls
2. `ModuleGUI.cpp` - Parameter changes  
3. `CommandShell.cpp` - CLI commands
4. `LuaGlobals.cpp` - Script clock operations
5. `LuaHelpers.cpp` - Script module operations
6. `ParameterRouter.cpp` - Parameter routing

**This was undefined behavior** per the library documentation:
> "This is a single-producer, single-consumer queue. Do not use it from multiple threads simultaneously." - readerwriterqueue.h line 22

## Verification

- ✅ Include changed from readerwriterqueue.h to concurrentqueue.h
- ✅ Queue type changed from ReaderWriterQueue to ConcurrentQueue
- ✅ Comment updated to reflect MPMC nature
- ✅ Engine.cpp API usage verified compatible (no changes needed)
- ✅ Syntax verified (concurrentqueue.h is valid)

## Files Modified

| File | Change |
|------|--------|
| `src/core/Engine.h` | Include + declaration + comment |

## Risk Assessment

- **Risk Level:** LOW
- **Rationale:** Same library author, API-compatible, already have ConcurrentQueue in codebase
- **Rollback:** Single commit to revert

## Deviations from Plan

None - plan executed exactly as written.

## Authentication Gates

None - no external services required for this change.

## Performance Data

- **Duration:** ~5 minutes
- **Tasks completed:** 3/3
- **Files modified:** 1
- **Lines changed:** +4/-5

## Next Step

Ready for 07-02-PLAN.md in Phase 7 (Fix Command Queue Architecture)
