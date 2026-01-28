# Technology Stack: Modular A/V Framework (2025)

**Project:** videoTracker Refactor
**Researched:** 2026-01-28
**Overall Confidence:** HIGH

This stack is optimized for a modern C++ (C++17/20) modular A/V framework focusing on **lock-free concurrency**, **"Push" model reactivity**, and **strict UI/Engine decoupling**.

## Recommended Stack

### Core Framework & Concurrency
| Technology | Version | Purpose | Why |
|------------|---------|---------|-----|
| [moodycamel::ReaderWriterQueue](https://github.com/cameron314/readerwriterqueue) | v1.0.7+ | SPSC Lock-free Queue | The gold standard for ultra-low latency single-producer single-consumer data transfer (e.g., Frame buffers, UI-to-Engine commands). |
| [moodycamel::ConcurrentQueue](https://github.com/cameron314/concurrentqueue) | v1.0.4+ | MPMC Lock-free Queue | Used for many-to-one command systems where multiple modules or UI components need to signal a central manager. |
| [Taskflow](https://github.com/taskflow/taskflow) | v4.0.0+ | Execution Graph | Handles complex dependency graphs between modules. Replaces manual thread management with an efficient work-stealing task scheduler. |
| [EnTT](https://github.com/skypjack/entt) | v3.16.0+ | ECS & State Management | Provides a high-performance registry for modules. Its internal signal system and reflection capabilities are ideal for automated UI binding. |

### Communication & Reactivity
| Technology | Version | Purpose | Why |
|------------|---------|---------|-----|
| [sigslot](https://github.com/paladin-t/sigslot) | Latest (Header-only) | Push Connection Model | Lightweight, type-safe signal/slot implementation. Perfect for connecting "Out" ports to "In" ports in a modular DAG. |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | v0.18.0+ | Remote Control / API | If the framework needs a headless mode or remote monitoring, this is the most painless header-only option for 2025. |

### UI & Visualization
| Technology | Version | Purpose | Why |
|------------|---------|---------|-----|
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.9x (Docking) | Primary UI | Industry standard for A/V tools. Use the `docking` branch for professional multi-window layouts. |
| [ImPlot](https://github.com/epezent/implot) | v0.16+ | Data Visualization | Essential for real-time tracking graphs, histograms, and diagnostic A/V signals within ImGui. |
| [ImNodes](https://github.com/Nelua/imnodes) | Latest | Visual Graph Editor | If a visual "Push" connection editor is needed, this is the most stable ImGui-based node library. |

### Infrastructure & Utilities
| Technology | Version | Purpose | Why |
|------------|---------|---------|-----|
| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3+ | Configuration | De-facto standard for JSON in C++. Use for project save/load and module settings. |
| [spdlog](https://github.com/gabime/spdlog) | v1.15.0+ | Logging | Extremely fast, thread-safe logging. Supports async logging to avoid blocking the high-priority A/V thread. |
| [cereal](https://github.com/USCiLab/cereal) | v1.3.2+ | Binary Serialization | Fast, header-only binary serialization for high-performance state caching or network streaming. |

---

## Architecture Rationale

### 1. The "Push" Connection Model
Instead of modules polling for data (`pull`), we use `sigslot` to allow modules to emit signals when a new buffer/result is ready. 
- **Prescriptive Choice:** `sigslot::signal<T...>`
- **Rationale:** Minimizes latency. When Module A finishes, it immediately triggers the callback in Module B. 
- **Thread Safety:** Connections should be established during a setup phase. For cross-thread push, use a `moodycamel::ReaderWriterQueue` as a bridge.

### 2. UI Decoupling (The "Snapshot" Pattern)
To keep the engine's A/V thread deterministic, **never** allow ImGui to modify engine variables directly.
- **Pattern:** 
    1. **Engine -> UI:** Every frame, the engine pushes a "State Snapshot" (POD struct or EnTT snapshot) into a `moodycamel::ReaderWriterQueue`.
    2. **UI -> Engine:** UI interactions generate `Command` objects (e.g., `SetThresholdCommand`) pushed into a `moodycamel::ConcurrentQueue`.
    3. **Engine Update:** At the start of the engine loop, it drains the command queue and applies changes.

### 3. Lock-Free Data Structures
For A/V work, `std::mutex` in the hot path is a risk for priority inversion and jitter.
- **Prescriptive Choice:** `moodycamel` family.
- **Why NOT Boost.Lockfree?** `moodycamel` is generally faster, easier to integrate (header-only), and specifically designed for high-contention scenarios common in A/V.

---

## What NOT to Use (and Why)

| Technology | Why Avoid | Recommended Alternative |
|------------|-----------|-------------------------|
| `std::mutex` (in hot path) | Causes non-deterministic stalls in A/V threads. | `moodycamel::ReaderWriterQueue` |
| `ofxGui` | Too tightly coupled to openFrameworks lifecycle; hard to thread-separate. | Dear ImGui + Snapshot Pattern |
| `boost::signals2` | Heavyweight, complex thread-safety overhead, slower than `sigslot`. | `sigslot` (header-only) |
| `RxCpp` | Massive compile-time impact and steep learning curve for simple A/V push models. | `sigslot` or `Taskflow` |

---

## Installation Summary

```bash
# Recommended: Use vcpkg for 2025 dependency management
vcpkg install \
  moodycamel-concurrentqueue \
  moodycamel-readerwriterqueue \
  imgui[docking-experimental] \
  implot \
  entt \
  taskflow \
  sigslot \
  nlohmann-json \
  spdlog \
  cereal
```

## Sources
- [Dear ImGui Multithreading Best Practices](https://github.com/ocornut/imgui/wiki/Multithreading)
- [MoodyCamel Performance Benchmarks](http://moodycamel.com/blog/2014/a-fast-general-purpose-lock-free-queue-for-c++)
- [EnTT Documentation (v3.13+)](https://github.com/skypjack/entt/wiki)
- [Modern C++ Signals/Slots Comparison (2024)](https://github.com/paladin-t/sigslot#performance)
