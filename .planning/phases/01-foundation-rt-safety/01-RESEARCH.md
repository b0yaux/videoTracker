# Phase 01: Foundation & RT-Safety - Research

**Researched:** 2026-01-28
**Domain:** C++17 Real-Time Audio/Video Infrastructure
**Confidence:** HIGH

## Summary

This research establishes the implementation strategy for a lock-free, parallelized infrastructure within openFrameworks using `moodycamel::ReaderWriterQueue` and `Taskflow`. The primary goal is to achieve "RT-Safety" by eliminating all mutexes and allocations from the high-priority processing threads.

Key findings:
- **`moodycamel::ReaderWriterQueue`** is the standard for SPSC (Single-Producer Single-Consumer) bridges.
- **Taskflow's `tf::Runtime`** is the recommended way to handle dynamic graph updates without the overhead of rebuilding the entire taskflow every frame.
- **openFrameworks internal mutexes** (e.g., in `ofParameter`, `ofThreadChannel`, `ofVideoGrabber`) are major sources of jitter and must be bypassed using lock-free bridges.

**Primary recommendation:** Implement a stable "Skeleton Taskflow" for the main engine loop, and use a `tf::Runtime`-driven Controller task to inject dynamic processing modules frame-by-frame.

## Standard Stack

The established libraries/tools for this domain:

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `moodycamel::ReaderWriterQueue` | Latest (v1.0.7+) | Lock-free SPSC Bridge | Extremely fast, wait-free, header-only, handles moving objects. |
| `Taskflow` | 4.0.0+ | Parallel Task Execution | Expressive graph API, built-in profiler, efficient work-stealing. |
| `std::variant` | C++17 | Type-safe Commands | Zero-overhead tagged unions for command dispatch. |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `std::chrono` | C++17 | High-precision timing | All profiling and timestamping. |
| `std::atomic` | C++17 | Lock-free synchronization | Shared flags and triple-buffer indices. |

**Installation:**
```bash
# Taskflow and ReaderWriterQueue are header-only.
# Add to project include paths.
```

## Architecture Patterns

### Recommended Project Structure
```
src/
├── engine/
│   ├── bridge/          # Lock-free Command/State bridges
│   ├── graph/           # Taskflow skeleton and dynamic modules
│   └── util/            # Profiling and timing hooks
├── drivers/             # Thread-isolated A/V drivers (Grabbers, etc.)
└── ui/                  # Main thread UI (consumes State bridges)
```

### Pattern 1: Skeleton Taskflow with Dynamic Runtime
**What:** Maintain a static `tf::Taskflow` for the engine lifecycle and use `tf::Runtime` to spawn frame-specific tasks.
**When to use:** When the processing topology (e.g., active filters) changes dynamically based on user input.
**Example:**
```cpp
// Source: Taskflow Documentation (Runtime Tasking)
tf::Taskflow skeleton;
skeleton.emplace([](tf::Runtime& rt) {
    // 1. Get current active modules from Lock-free queue
    // 2. Spawn them dynamically
    for(auto& mod : activeModules) {
        rt.silent_async([&mod](){ mod.process(); });
    }
    // 3. Implicit join happens at end of scope
});
```

### Pattern 2: Lock-free Snapshot (Triple Buffer)
**What:** Use three buffers and atomic indices to pass the "latest" state snapshot from RT to UI without blocking.
**When to use:** Passing analysis results (tracked points, levels) to the UI thread for visualization.
**Anti-Patterns to Avoid:**
- **`ofThreadChannel`:** Uses mutexes and condition variables; will cause jitter if used in the RT thread.
- **`std::vector` resizing:** Never resize or allocate containers inside the RT loop. Use pre-allocated pools.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Lock-free Queue | Custom CAS-loop queue | `moodycamel::ReaderWriterQueue` | SOTA performance, handles memory ordering edge cases correctly. |
| Task Scheduling | Manual `std::thread` pool | `tf::Executor` | Work-stealing is significantly better for unbalanced workloads. |
| Thread-safe Params | `std::mutex` wrappers | Command Bridge | Mutexes cause priority inversion in A/V contexts. |

## Common Pitfalls

### Pitfall 1: Priority Inversion in `ofParameter`
**What goes wrong:** Calling `param.get()` in the RT thread while the UI thread is calling `param.set()`. `ofParameter` uses a mutex.
**How to avoid:** Use a `ReaderWriterQueue<Command>` to push parameter updates from UI to RT. The RT thread applies updates locally at the start of the frame.

### Pitfall 2: `ofVideoGrabber` Blocking
**What goes wrong:** `grabber.update()` often blocks waiting for the next frame or locks an internal mutex.
**How to avoid:** Run the grabber in its own dedicated "Driver Thread". Use a lock-free pool of `ofPixels` to pass the latest frame to the engine.

## Code Examples

### Command Dispatch with `std::visit`
```cpp
struct SetThreshold { float value; };
struct AddFilter { int type; };
using Command = std::variant<SetThreshold, AddFilter>;

// In RT Thread
Command cmd;
while(commandQueue.try_dequeue(cmd)) {
    std::visit([](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, SetThreshold>) {
            engine.setThreshold(arg.value);
        } else if constexpr (std::is_same_v<T, AddFilter>) {
            engine.addFilter(arg.type);
        }
    }, cmd);
}
```

### High-Precision Timing Hook
```cpp
struct TimerHook {
    std::chrono::high_resolution_clock::time_point start;
    TimerHook() : start(std::chrono::high_resolution_clock::now()) {}
    ~TimerHook() {
        auto end = std::chrono::high_resolution_clock::now();
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        // Push micros to lock-free profiling queue
    }
};
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `ofThread` + `mutex` | Taskflow + Lock-free | 2020+ | Multi-core scaling without jitter. |
| `ofThreadChannel` | `moodycamel::RWQueue` | 2018+ | Eliminates priority inversion. |
| Double Buffering | Triple Buffering | - | Non-blocking "latest frame" access. |

## Open Questions

1. **Taskflow Thread Affinity**
   - What we know: Taskflow uses a work-stealing pool.
   - What's unclear: How to pin specific tasks (like Audio) to high-priority cores within Taskflow.
   - Recommendation: Keep the Audio callback outside Taskflow or use a high-priority worker group if supported.

2. **`ofVideoGrabber` Mutex bypass**
   - What we know: Most OF grabbers are not RT-safe.
   - What's unclear: Exact mutex locations in the specific grabber implementation used (e.g., `ofAVFoundationGrabber`).
   - Recommendation: Isolation via Driver Thread is the safest default.

## Sources

### Primary (HIGH confidence)
- `moodycamel::ReaderWriterQueue` GitHub README - API and SPSC semantics.
- `Taskflow` Official Documentation - Runtime tasking and executor behavior.
- C++17 Standard Library - `std::variant`, `std::chrono`, `std::atomic`.

### Secondary (MEDIUM confidence)
- openFrameworks `ofThreadChannel.h` source - Confirmed mutex/condition usage.
- openFrameworks Forum - `ofParameter` thread-safety discussions.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - Libraries are mature and industry standards.
- Architecture: HIGH - SOTA patterns for A/V systems.
- Pitfalls: HIGH - Common OF bottlenecks are well-documented.

**Research date:** 2026-01-28
**Valid until:** 2026-03-01
