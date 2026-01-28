# Project Research Summary

**Project:** videoTracker Refactor
**Domain:** Modular A/V Framework
**Researched:** 2026-01-28
**Confidence:** HIGH

## Executive Summary

The research concludes that a modern modular A/V framework should be built as a **Decoupled Reactive Graph**. This approach prioritizes ultra-low latency and deterministic processing by strictly separating the high-priority Engine thread from the UI and management logic. The core recommendation is to move from a polling ("Pull") model to a reactive ("Push") model using type-safe signals (`sigslot`) and lock-free data structures (`moodycamel` queues).

The recommended architecture follows a three-stage module lifecycle—**Scaffold, Configure, Hydrate**—which ensures that heavy resource allocation (like GPU buffers or file I/O) never blocks the UI or the main execution graph. State management is handled via the **Snapshot Pattern**, where the engine pushes immutable state snapshots to the UI, and the UI sends commands back through a lock-free queue, eliminating the need for shared mutexes in the hot path.

Key risks include **priority inversion** due to lock contention and **RT-safety violations** (like memory allocation in the processing loop). These are mitigated by adopting a lock-free first approach and using fixed-size pools for command objects. Additionally, circular dependencies in the push model must be prevented via DAG validation at connection time.

## Key Findings

### Recommended Stack

The stack is optimized for modern C++ (C++17/20) with a focus on high-performance concurrency and reactive data flow. It avoids traditional mutex-based synchronization in favor of lock-free queues and task-based execution.

**Core technologies:**
- **moodycamel::ReaderWriterQueue/ConcurrentQueue**: SPSC/MPMC Lock-free Queues — Ultra-low latency data transfer between UI and Engine.
- **sigslot**: Push Connection Model — Lightweight, type-safe signal/slot implementation for modular reactivity.
- **Taskflow**: Execution Graph — Efficient work-stealing task scheduler for complex module dependencies.
- **EnTT**: ECS & State Management — High-performance registry and state snapshotting.
- **Dear ImGui (Docking)**: Primary UI — Industry standard for professional multi-window A/V tools.

### Expected Features

The framework must provide a consistent experience for both developers and end-users, focusing on flexibility and stability.

**Must have (table stakes):**
- **Unified Port System** — Consistent interface for Audio, Video, and Data connections.
- **JSON Serialization** — Session save/load with complex nested module states.
- **Global Transport/Clock** — Precise BPM/Phase sync across all modules.
- **Parameter Mapping** — Centralized routing for external control (MIDI/OSC/LFO).

**Should have (competitive):**
- **Immediate Push Propagation** — Topological sorting to ensure zero-frame-lag updates.
- **Lifecycle-Aware Hydration** — Non-blocking asset loading and activation.
- **Strict Thread Separation** — Engine processing is never blocked by UI operations.

**Defer (v2+):**
- **Distributed Processing** — Offloading tasks to multiple machines.
- **Visual Node Editor** — While ImNodes is recommended, the MVP can focus on a headless-first API.

### Architecture Approach

The framework is structured as a Decoupled Reactive Graph, where components communicate via signals and lock-free message passing.

**Major components:**
1. **Core (Graph Manager)** — Orchestrates module entities and the Scaffold-Configure-Hydrate lifecycle.
2. **DSP / Module Engine** — High-priority execution thread using the Push propagation model.
3. **Interaction Layer** — Translates UI/External inputs into serializable Command objects.
4. **Resource Layer** — Manages shared assets (Media, Shaders) with background hydration.

### Critical Pitfalls

1. **Priority Inversion** — Prevented by using lock-free queues and the Snapshot Pattern instead of `std::mutex`.
2. **RT-Safety Violations** — Avoided by pre-allocating command nodes and avoiding `malloc` in the processing loop.
3. **OpenGL Texture Violations** — Mitigated by ensuring all GPU resource updates happen on the main thread during a sync phase.
4. **Circular Signal Dependencies** — Prevented by mandatory DAG validation during port connection.

## Implications for Roadmap

Based on research, the suggested phase structure focuses on establishing a thread-safe core before building out the reactive features and UI.

### Phase 1: Foundation & RT-Safety
**Rationale:** Critical safety fixes and core abstractions must be established first to prevent architectural debt.
**Delivers:** Lock-free queue integration, basic Module/Port interfaces, and EnTT registry setup.
**Addresses:** Unified Port System.
**Avoids:** Priority Inversion and RT-Safety Violations.

### Phase 2: Reactive Engine & Push Pipeline
**Rationale:** The core value proposition depends on immediate propagation and topological sorting.
**Delivers:** `sigslot` integration, DAG validator, and the Push propagation logic.
**Uses:** `sigslot`, `Taskflow`.
**Implements:** DSP / Module Engine.

### Phase 3: Lifecycle & State Management
**Rationale:** Essential for stability and session management before the UI is fully realized.
**Delivers:** Scaffold-Configure-Hydrate state machine and JSON serialization.
**Addresses:** JSON Serialization, Lifecycle-Aware Hydration.

### Phase 4: Headless API & Interaction
**Rationale:** Enables testing and remote control without the overhead of a GUI.
**Delivers:** CommandQueue implementation and the Snapshot Pattern bridge.
**Implements:** Interaction Layer.

### Phase 5: UI Refactor & Visualization
**Rationale:** High-fidelity UI depends on the stability of the underlying state snapshots.
**Delivers:** Dear ImGui integration, ImPlot diagnostics, and project-wide UI/Engine decoupling.
**Uses:** `Dear ImGui`, `ImPlot`.

### Phase Ordering Rationale

- **Dependencies:** The Snapshot Pattern requires the CommandQueue to be functional first. The UI depends on the Snapshot bridge.
- **Safety:** RT-safety fixes are prioritized (Phase 1) because they are harder to retrofit later.
- **Grouping:** Lifecycle and State are grouped (Phase 3) as they both deal with the persistent and transient state of the graph.

### Research Flags

Phases likely needing deeper research during planning:
- **Phase 2 (Reactive Engine):** Needs detailed research on topological sorting algorithms for cyclic graphs (for the "Delayed Port" exception).
- **Phase 5 (UI Refactor):** Complex integration of ImGui docking with openFrameworks' windowing system might need dedicated prototyping.

Phases with standard patterns (skip research-phase):
- **Phase 1 (Foundation):** Lock-free patterns and EnTT usage are well-documented and standard.
- **Phase 3 (Lifecycle):** The Scaffold-Configure-Hydrate pattern is an established industrial practice.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | Based on modern C++ best practices and industry-standard libraries (moodycamel, EnTT). |
| Features | HIGH | Derived from competitive analysis of BespokeSynth and Mosaic. |
| Architecture | HIGH | Follows proven decoupled A/V patterns used in high-performance software. |
| Pitfalls | HIGH | Aligns with known real-time C++ risks documented by ADC and JUCE. |

**Overall confidence:** HIGH

### Gaps to Address

- **GPU/CPU Sync:** The exact boundary for OpenGL resource updates (Pitfall #7) needs careful implementation during Phase 5.
- **Macro-Batching:** How to handle atomic multi-parameter updates in the Push model needs a specific implementation design in Phase 2.

## Sources

### Primary (HIGH confidence)
- [BespokeSynth Architecture](https://github.com/BespokeSynth/BespokeSynth) — Push model and topological sorting.
- [Audio Developer Conference (ADC) 2024](https://conference.audio.dev/) — Real-time safety and lock-free patterns.
- [MoodyCamel Documentation](https://github.com/cameron314/concurrentqueue) — Lock-free performance benchmarks and usage.

### Secondary (MEDIUM confidence)
- [Mosaic (ofxVisualProgramming)](https://github.com/d3cod3/Mosaic) — Modular structure in openFrameworks context.
- [EnTT Wiki](https://github.com/skypjack/entt/wiki) — State management and ECS patterns.

---
*Research completed: 2026-01-28*
*Ready for roadmap: yes*
