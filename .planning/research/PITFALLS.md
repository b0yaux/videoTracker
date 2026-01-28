# Domain Pitfalls: Modular A/V Refactoring

**Domain:** Modular A/V Frameworks
**Researched:** 2026-01-28
**Focus:** Thread-safety, Command Synchronization, and UI-Core Coupling

## Critical Pitfalls

### 1. Priority Inversion via Lock Contention ("The Hidden Mutex")
**What goes wrong:** A low-priority thread (e.g., UI or Serialization) holds a lock that the high-priority Audio/Video thread needs to proceed. The RT thread stalls, causing audio glitches or dropped frames.
**Why it happens:** Even if using `shared_mutex` (readers-writer lock), a writer on the UI thread can block multiple readers on the audio thread. In `videoTracker`, the existing `stateMutex_` and `snapshotMutex_` are high-risk areas if held during long JSON serializations.
**Consequences:** Audio dropouts (xruns), UI freezing while waiting for the audio thread to release a resource.
**Prevention:** 
- Use **Lock-Free Queues** (e.g., `moodycamel::ConcurrentQueue`) for all communication *into* the RT thread.
- Use **Triple Buffering** or **RCU (Read-Copy-Update)** for state observation; the RT thread should never wait for a lock held by a non-RT thread.
- **Phase Mapping:** Address in Phase 4 (Threading Simplification).

### 2. State Desynchronization in Push Models ("The Partial Update")
**What goes wrong:** In a Push model, individual parameters are updated via discrete events. If a "Scene Change" involves 50 parameter updates, the A/V engine might process the first 10 in one block and the remaining 40 in the next, leading to audible/visible artifacts.
**Why it happens:** Lack of "transactional" or "atomic" batching for command groups.
**Consequences:** Glitches during transitions, inconsistent internal state where Module A has the new value but Module B (which depends on A) still has the old one.
**Prevention:** 
- Implement **Command Batching**: Groups of commands should be timestamped for the same processing block.
- Use a **Command Timestamp** system where commands are only "pushed" into the active state at the start of a processing cycle.
- **Phase Mapping:** Address in Workstream A, Phase 3 (API Rationalization).

### 3. RT-Safety Violations in Command Queues ("The Malloc-on-Push")
**What goes wrong:** Pushing a command onto a thread-safe queue triggers a memory allocation or a system call inside the RT thread (when popping) or in a performance-critical path.
**Why it happens:** Using `std::function` or `std::unique_ptr` without a custom allocator in a queue that isn't pre-allocated.
**Consequences:** Non-deterministic latency spikes. Even if it "usually" works, a rare heap fragmentation can cause a glitch.
**Prevention:** 
- Use **Fixed-Size Pools** for Command objects.
- Use **Lock-Free Queues with pre-allocated nodes** (like `moodycamel::ReaderWriterQueue` with a defined capacity).
- **Detection:** Use `ThreadSanitizer` and real-time safety linters (e.g., `clang-tidy` with RT-safety checks).
- **Phase Mapping:** Address in Phase 1 (Critical Safety Fixes).

### 7. OpenGL Texture Thread Violation (The "White Texture" Bug)
**What goes wrong:** Modifying or allocating `ofTexture` (or any GL object) on a non-GL thread.
**Why it happens:** Modules trying to "allocate" textures during their `process()` call on a worker thread.
**Consequences:** OpenGL "Invalid Operation" errors, crashes, or silent white/flickering textures.
**Prevention:** 
- All GPU resource allocation/update must happen in the Main Thread. 
- Modules should produce `ofPixels` or `cv::Mat` in worker threads.
- Upload to GPU in a central "Sync" phase on the main thread.

### 8. Circular Signal Dependencies ("The Feedback Storm")
**What goes wrong:** Module A pushes to B, which pushes back to A, creating an infinite recursion.
**Why it happens:** Improperly handled feedback loops in a "Push" model.
**Consequences:** Stack overflow and immediate crash.
**Prevention:** 
- Use a **DAG validator** during connection time to prevent cycles.
- For intentional feedback (e.g., delay loops), implement a "Delayed Port" that pushes to a queue for the *next* frame.

## Moderate Pitfalls

### 4. Recursive Command Loops ("The Feedback Storm")
**What goes wrong:** UI observes a state change -> UI pushes a "fix" command -> Core updates state -> UI observes again... creating an infinite loop.
**Why it happens:** UI components that act as both observers and controllers without "ignore-self" logic.
**Prevention:** 
- Each command should carry an **Originator ID**. Observers should ignore updates triggered by their own commands.
- Use a `notifyingObservers_` guard (already partially implemented in `videoTracker`).
**Detection:** High CPU usage when interacting with single sliders, "Recursion limit exceeded" logs.

### 5. UI Freeze via Blocking Snapshots ("The Snapshot Stutter")
**What goes wrong:** The UI thread requests a full state snapshot for rendering, but the Core is busy building that snapshot or holds a mutex while doing so.
**Why it happens:** "Immutable State Snapshot" pattern is safe for reading but can be expensive to *build*. If `videoTracker` builds a JSON snapshot every 100ms on the main thread, it can cause micro-stutters.
**Prevention:** 
- Build snapshots **incrementally** or on a **background worker thread**, then swap pointers atomically.
- Use **Dirty Flags** to only update parts of the snapshot that changed.
- **Phase Mapping:** Address in Workstream B, Phase 1 (Snapshot Consolidation).

## Minor Pitfalls

### 6. Circular Dependency Deadlocks
**What goes wrong:** Module A depends on Module B's state, and Module B depends on Module A's state. When trying to lock both for a synchronous update, a deadlock occurs.
**Prevention:** 
- Establish a strict **Lock Hierarchy** (always lock in the same order).
- Prefer **Message Passing** over direct state access between modules.
- Use `weak_ptr` for all cross-module references (already done in `ModuleRegistry`).

## Phase-Specific Warnings

| Phase Topic | Likely Pitfall | Mitigation |
|-------------|---------------|------------|
| Core Scripting P1 (Safety) | CircuitBreaker False Positives | Tunable thresholds for Lua execution time; clear error reporting. |
| Module Data P1 (Snapshots) | Stale UI Data | Ensure `stateVersion` is incremented *after* the snapshot pointer swap. |
| GUI Rendering P4 (AssetLibrary) | Main Thread Disk I/O | All asset indexing and metadata parsing must occur on a background thread. |

## Sources

- [Audio Developer Conference (ADC) 2024 - Real-Time Safety](https://conference.audio.dev/)
- [JUCE Best Practices - Threading & RT Safety](https://juce.com/learn/documentation/)
- [Moodycamel ConcurrentQueue Documentation](https://github.com/cameron314/concurrentqueue)
- [videoTracker Wave 1 Architecture Report (01_CORE_ENGINE_REPORT.md)](docs/research/wave1_reports/01_CORE_ENGINE_REPORT.md)
