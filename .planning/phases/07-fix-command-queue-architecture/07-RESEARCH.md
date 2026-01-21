# Phase 7: Fix Command Queue Architecture - Research

**Researched:** 2026-01-21
**Domain:** Lock-free concurrency, MPMC queues, command pattern in real-time audio
**Confidence:** HIGH

## Summary

This phase addresses a critical architectural issue: the command queue uses `moodycamel::ReaderWriterQueue` (SPSC - Single Producer Single Consumer) but has 6+ producers attempting concurrent writes. This is undefined behavior that can cause data corruption, lost commands, and unpredictable failures.

The moodycamel library already provides the solution: `moodycamel::ConcurrentQueue` is a lock-free MPMC (Multi-Producer Multi-Consumer) queue from the same author, already present in the codebase at `libs/concurrentqueue/concurrentqueue.h`. The codebase already uses `BlockingConcurrentQueue` for the notification queue, proving the library is integrated.

**Primary recommendation:** Replace `ReaderWriterQueue` with `ConcurrentQueue` for the command queue. The API is nearly identical, making migration straightforward.

## Standard Stack

The established libraries/tools for this domain:

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `moodycamel::ConcurrentQueue` | Already in libs/ | MPMC lock-free command queue | Same author as ReaderWriterQueue, battle-tested, already in codebase |
| `moodycamel::BlockingConcurrentQueue` | Already in libs/ | Blocking MPMC queue (notifications) | Already in use for notificationQueue_ |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `std::atomic<T>` | C++17 | State flags, version counters | Simple boolean/integer flags |
| `std::shared_mutex` | C++17 | Observer list protection | When reads >> writes |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `ConcurrentQueue` | `boost::lockfree::queue` | More dependencies, no clear benefit |
| `ConcurrentQueue` | Custom MPMC | Hand-rolling concurrency is risky |
| Atomic flags | Mutex-based guards | Higher contention, worse for real-time |

**Installation:**
```bash
# Already in codebase - no installation needed
# libs/concurrentqueue/concurrentqueue.h
# libs/concurrentqueue/blockingconcurrentqueue.h
```

## Architecture Patterns

### Recommended Command Queue Structure
```
Producer Threads (6+)                    Consumer Thread (1)
┌──────────────┐                        ┌──────────────┐
│ ClockGUI     │──┐                     │ Audio Thread │
├──────────────┤  │                     │              │
│ ModuleGUI    │──┤                     │ processCommands()
├──────────────┤  │    ┌─────────────┐  │              │
│ CommandExec  │──┼───>│ MPMC Queue  │──│─>execute()   │
├──────────────┤  │    │ ConcurrentQ │  │              │
│ LuaGlobals   │──┤    └─────────────┘  └──────────────┘
├──────────────┤  │
│ LuaHelpers   │──┤
├──────────────┤  │
│ ParameterRtr │──┘
└──────────────┘
```

### Pattern 1: MPMC Command Queue with Implicit Producers
**What:** Use `ConcurrentQueue` without explicit producer tokens for simplified multi-producer access
**When to use:** When producers are diverse (GUI, scripts, routers) and don't need per-producer optimizations
**Example:**
```cpp
// Source: libs/concurrentqueue/concurrentqueue.h (verified in codebase)
// Declaration (Engine.h)
moodycamel::ConcurrentQueue<std::unique_ptr<Command>> commandQueue_{1024};

// Producer (any thread)
bool enqueueCommand(std::unique_ptr<Command> cmd) {
    // ConcurrentQueue::try_enqueue is thread-safe for multiple producers
    if (commandQueue_.try_enqueue(std::move(cmd))) {
        return true;
    }
    // Queue full handling...
    return false;
}

// Consumer (audio thread only)
int processCommands() {
    std::unique_ptr<Command> cmd;
    int processed = 0;
    // ConcurrentQueue::try_dequeue is thread-safe
    while (commandQueue_.try_dequeue(cmd)) {
        cmd->execute(*this);
        processed++;
    }
    return processed;
}
```

### Pattern 2: Unified Command Routing (All State Mutations)
**What:** ALL state-mutating operations go through command queue, no exceptions
**When to use:** Always - eliminates race conditions from mixed sync/async patterns
**Example:**
```cpp
// BAD: Direct call (bypasses queue, causes race conditions)
clock->pause();  // Current code in LuaGlobals.cpp:118
clock->reset();  // Current code in LuaGlobals.cpp:128
clock.reset();   // Current code in ClockGUI.cpp:211

// GOOD: Command-based (thread-safe, state notified)
auto cmd = std::make_unique<PauseTransportCommand>();
engine->enqueueCommand(std::move(cmd));

auto cmd = std::make_unique<ResetTransportCommand>();
engine->enqueueCommand(std::move(cmd));
```

### Pattern 3: Script Execution Tracking to Prevent Infinite Loops
**What:** Track last failed script hash to prevent re-execution of failing scripts
**When to use:** Auto-evaluation in live coding environments
**Example:**
```cpp
// Track execution state
struct ScriptExecutionState {
    std::string lastExecutedScript;      // Successfully executed
    std::string lastFailedScript;        // Last script that failed
    std::string lastFailedScriptHash;    // Hash of failed script
    uint64_t lastFailureTime{0};         // When it failed
    int failureCount{0};                 // How many times
    
    static constexpr int MAX_RETRIES = 3;
    static constexpr uint64_t RETRY_COOLDOWN_MS = 5000;
};

// Before auto-execution
bool shouldExecute(const std::string& script) {
    std::string scriptHash = hashScript(script);
    
    // Don't retry same failing script too quickly
    if (scriptHash == state.lastFailedScriptHash) {
        auto now = getCurrentTimeMs();
        if (now - state.lastFailureTime < RETRY_COOLDOWN_MS) {
            return false;  // Still in cooldown
        }
        if (state.failureCount >= MAX_RETRIES) {
            return false;  // Max retries exceeded
        }
    }
    return true;
}

// After execution
void recordResult(const std::string& script, bool success) {
    std::string scriptHash = hashScript(script);
    if (success) {
        state.lastExecutedScript = script;
        state.failureCount = 0;
        state.lastFailedScriptHash.clear();
    } else {
        state.lastFailedScript = script;
        state.lastFailedScriptHash = scriptHash;
        state.lastFailureTime = getCurrentTimeMs();
        state.failureCount++;
    }
}
```

### Pattern 4: Simplified State Guard (Two Flags Only)
**What:** Reduce 6+ flags to 2 clear-purpose flags
**When to use:** State synchronization between threads
**Example:**
```cpp
// CURRENT: 6+ flags that don't coordinate
// std::atomic<bool> notifyingObservers_{false};
// std::atomic<bool> notificationEnqueued_{false};
// std::atomic<bool> isRendering_{false};
// std::atomic<bool> transportStateChangedDuringScript_{false};
// thread_local bool isBuildingSnapshot_;
// std::mutex snapshotMutex_;
// std::mutex immutableStateSnapshotMutex_;

// SIMPLIFIED: 2 clear-purpose flags
struct StateGuards {
    // Flag 1: Is the engine in a state-modifying operation?
    // Set during: command processing, script execution
    // Checked by: state snapshot building, notifications
    std::atomic<bool> stateModificationInProgress{false};
    
    // Flag 2: Is ImGui rendering?
    // Set during: ImGui draw() calls
    // Checked by: state notifications (to defer)
    std::atomic<bool> renderingInProgress{false};
};

// Usage
bool isStateSafe() const {
    return !guards_.stateModificationInProgress.load(std::memory_order_acquire) &&
           !guards_.renderingInProgress.load(std::memory_order_acquire);
}
```

### Anti-Patterns to Avoid
- **Mixed sync/async execution:** NEVER call state-mutating methods directly when command queue exists
- **Flag proliferation:** NEVER add new atomic flags without removing old ones
- **Direct clock manipulation:** NEVER call `clock->pause()`, `clock->reset()` etc. directly from scripts
- **Retry loops without backoff:** NEVER retry failed script execution every frame

## Don't Hand-Roll

Problems that look simple but have existing solutions:

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| MPMC queue | Custom ring buffer | `moodycamel::ConcurrentQueue` | Lock-free MPMC is notoriously hard to get right |
| Producer tokens | Thread ID tracking | Implicit producers in ConcurrentQueue | Library handles this automatically |
| Queue overflow | Manual ring wrapping | `try_enqueue()` return value | Library provides proper semantics |
| Memory ordering | Manual memory barriers | Library's internal barriers | Already correct in moodycamel |
| Execution debouncing | Custom timer logic | Simple last-execution tracking | Just track script hash + timestamp |

**Key insight:** The moodycamel library is specifically designed for the SPSC→MPMC upgrade path. The author created both queues with compatible APIs precisely for this migration scenario.

## Common Pitfalls

### Pitfall 1: Using SPSC Queue with Multiple Producers
**What goes wrong:** Undefined behavior - data corruption, lost elements, crashes
**Why it happens:** `ReaderWriterQueue` explicitly states "single-producer, single-consumer" in its header
**How to avoid:** Use `ConcurrentQueue` when multiple producers exist
**Warning signs:** Intermittent command loss, state inconsistencies, rare crashes

### Pitfall 2: Mixing Direct Calls with Command Queue
**What goes wrong:** Race conditions - state changes without notifications, inconsistent state
**Why it happens:** Some code paths bypass queue for "simplicity" or "performance"
**How to avoid:** Audit ALL state mutations, route 100% through commands
**Warning signs:** State not syncing to scripts, GUI showing stale data

### Pitfall 3: Infinite Re-execution of Failing Scripts
**What goes wrong:** CPU spins at 100%, log spam, unresponsive UI
**Why it happens:** Auto-eval triggers every frame, failed script hasn't changed
**How to avoid:** Track last failed script hash, implement retry backoff
**Warning signs:** Log flooding with same error, app becomes sluggish after script error

### Pitfall 4: Flag Overloading (Single Flag for Multiple Purposes)
**What goes wrong:** Flag contention, incorrect state detection
**Why it happens:** `notifyingObservers_` used for both command processing AND script execution
**How to avoid:** One flag = one purpose, document each flag's contract
**Warning signs:** Spurious "unsafe state" detections, missed state updates

### Pitfall 5: Memory Ordering Issues in Lock-Free Code
**What goes wrong:** Rare crashes, impossible states, Heisenbugs
**Why it happens:** Incorrect use of `memory_order_relaxed` vs `memory_order_acquire/release`
**How to avoid:** Use library defaults, only optimize after profiling proves necessity
**Warning signs:** Bugs that disappear in debug builds, platform-specific failures

## Code Examples

Verified patterns from official sources:

### MPMC Queue Declaration and Usage
```cpp
// Source: libs/concurrentqueue/concurrentqueue.h line 813
// Constructor with initial capacity
moodycamel::ConcurrentQueue<std::unique_ptr<Command>> commandQueue_{1024};

// Source: libs/concurrentqueue/concurrentqueue.h line 995-1010
// Thread-safe enqueue (may allocate if needed)
bool success = commandQueue_.enqueue(std::move(cmd));

// Thread-safe enqueue (never allocates, fails if full)  
bool success = commandQueue_.try_enqueue(std::move(cmd));

// Source: libs/concurrentqueue/concurrentqueue.h line 1120-1156
// Thread-safe dequeue
std::unique_ptr<Command> cmd;
if (commandQueue_.try_dequeue(cmd)) {
    cmd->execute(*this);
}

// Source: libs/concurrentqueue/concurrentqueue.h line 1319-1332
// Approximate size (for monitoring)
size_t depth = commandQueue_.size_approx();
```

### Migration: ReaderWriterQueue → ConcurrentQueue
```cpp
// BEFORE (Engine.h:634)
#include "readerwriterqueue.h"
moodycamel::ReaderWriterQueue<std::unique_ptr<Command>> commandQueue_{1024};

// AFTER
#include "concurrentqueue.h"  // Already at libs/concurrentqueue/
moodycamel::ConcurrentQueue<std::unique_ptr<Command>> commandQueue_{1024};

// API is compatible:
// - try_enqueue() works the same
// - try_dequeue() works the same
// - size_approx() works the same
```

### Adding Missing Command Types
```cpp
// Source: Current Command.h pattern (verified in codebase)

// PauseTransportCommand (new - currently uses direct call)
class PauseTransportCommand : public Command {
public:
    void execute(Engine& engine) override {
        engine.getClock().pause();
        // State notification handled by Engine after command completes
    }
    void undo(Engine& engine) override {
        engine.getClock().start();  // Resume from paused state
    }
    std::string describe() const override {
        return "pause transport";
    }
};

// ResetTransportCommand (new - currently uses direct call)
class ResetTransportCommand : public Command {
public:
    ResetTransportCommand() : wasPlaying_(false) {}
    
    void execute(Engine& engine) override {
        wasPlaying_ = engine.getClock().isPlaying();
        engine.getClock().reset();
    }
    void undo(Engine& engine) override {
        if (wasPlaying_) {
            engine.getClock().start();
        }
    }
    std::string describe() const override {
        return "reset transport";
    }
private:
    bool wasPlaying_;
};
```

### Script Execution Tracking
```cpp
// In CodeShell.h - add execution tracking state
struct ScriptExecutionTracker {
    std::string lastFailedScriptHash;
    uint64_t lastFailureTimeMs{0};
    int consecutiveFailures{0};
    
    static constexpr int MAX_CONSECUTIVE_FAILURES = 3;
    static constexpr uint64_t FAILURE_COOLDOWN_MS = 2000;
    
    bool shouldRetry(const std::string& scriptHash, uint64_t nowMs) const {
        if (scriptHash != lastFailedScriptHash) {
            return true;  // Different script, try it
        }
        if (consecutiveFailures >= MAX_CONSECUTIVE_FAILURES) {
            return false;  // Too many failures
        }
        if (nowMs - lastFailureTimeMs < FAILURE_COOLDOWN_MS) {
            return false;  // Still in cooldown
        }
        return true;  // Retry allowed
    }
    
    void recordSuccess() {
        lastFailedScriptHash.clear();
        consecutiveFailures = 0;
    }
    
    void recordFailure(const std::string& scriptHash, uint64_t nowMs) {
        if (scriptHash == lastFailedScriptHash) {
            consecutiveFailures++;
        } else {
            lastFailedScriptHash = scriptHash;
            consecutiveFailures = 1;
        }
        lastFailureTimeMs = nowMs;
    }
};
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| SPSC queue for all | MPMC queue for multi-producer | Always was required | Correctness |
| Mixed direct/queued calls | All mutations through commands | Best practice | Thread safety |
| Retry every frame | Backoff on failure | Standard pattern | CPU efficiency |
| Many flags | Minimal flag set | Simplification trend | Maintainability |

**Deprecated/outdated:**
- `ReaderWriterQueue` for command queue: Wrong for MPMC scenario, use `ConcurrentQueue`
- Direct `clock->pause()/reset()` calls: Bypass thread safety, use commands
- Frame-based retry loops: Wasteful, use hash-based deduplication

## Migration Strategy

### Step 1: Replace Queue Type (Priority 1, Lowest Risk)
1. Change include from `readerwriterqueue.h` to `concurrentqueue.h`
2. Change type from `ReaderWriterQueue` to `ConcurrentQueue`
3. API is compatible - `try_enqueue()`, `try_dequeue()`, `size_approx()` all work
4. Test: Verify commands still flow through
5. **No behavior change expected** - just fixes undefined behavior

### Step 2: Add Missing Command Types (Priority 2)
1. Create `PauseTransportCommand` in Command.h
2. Create `ResetTransportCommand` in Command.h
3. Test: Verify pause/reset work via commands

### Step 3: Route Direct Calls Through Commands (Priority 2)
Locations to fix:
- `LuaGlobals.cpp:118` - `clock->pause()` → `PauseTransportCommand`
- `LuaGlobals.cpp:128` - `clock->reset()` → `ResetTransportCommand`  
- `ClockGUI.cpp:211` - `clock.reset()` → `ResetTransportCommand`

Test: Verify all clock operations notify state correctly

### Step 4: Add Script Execution Tracking (Priority 3)
1. Add `ScriptExecutionTracker` to CodeShell
2. Hash script content before execution
3. Check tracker before auto-eval
4. Update tracker after execution result
5. Test: Verify failing script doesn't retry every frame

### Step 5: Simplify State Guards (Priority 4, Higher Risk)
1. Document current flag usage
2. Identify redundant flags
3. Consolidate to 2 primary flags
4. Remove unused flags
5. Test: Comprehensive state synchronization testing

## Open Questions

Things that couldn't be fully resolved:

1. **Producer Token Optimization**
   - What we know: ConcurrentQueue supports producer tokens for better performance with dedicated producers
   - What's unclear: Whether our mixed producer pattern (GUI + scripts + routers) benefits from tokens
   - Recommendation: Start with implicit producers (simpler), profile later if needed

2. **Queue Capacity Sizing**
   - What we know: Current capacity is 1024, `ConcurrentQueue` constructor takes capacity
   - What's unclear: Optimal capacity for MPMC vs SPSC (MPMC may need more due to per-producer blocks)
   - Recommendation: Keep 1024, monitor with existing queue depth logging

3. **Notification Queue Relationship**
   - What we know: `notificationQueue_` already uses `BlockingConcurrentQueue`
   - What's unclear: Whether command and notification queues should be unified
   - Recommendation: Keep separate - they serve different purposes (commands = mutations, notifications = UI updates)

## Sources

### Primary (HIGH confidence)
- `libs/concurrentqueue/concurrentqueue.h` - Line 1-200 (header docs), 767-1400 (ConcurrentQueue class)
- `libs/readerwriterqueue/readerwriterqueue.h` - Line 22-33 (SPSC warning)
- `src/core/Engine.h` - Line 634 (current queue declaration)
- `src/core/Engine.cpp` - Line 1973-2081 (enqueue/process implementation)

### Secondary (MEDIUM confidence)
- moodycamel.com/blog/2014/a-fast-general-purpose-lock-free-queue-for-c++ - Design rationale
- github.com/cameron314/concurrentqueue - API documentation, samples.md

### Tertiary (LOW confidence)
- Community patterns for command queue architecture (multiple sources agreeing)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - Library already in codebase, API verified in headers
- Architecture patterns: HIGH - Patterns derived from existing codebase analysis
- Migration strategy: HIGH - API compatibility verified in source files
- Pitfalls: MEDIUM - Based on codebase analysis + general concurrency knowledge

**Research date:** 2026-01-21
**Valid until:** 2026-02-21 (30 days - stable library, mature patterns)
