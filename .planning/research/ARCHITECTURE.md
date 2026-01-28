# Architecture Patterns: Modular A/V Framework

**Domain:** Modular A/V Framework (Refactor)
**Researched:** 2026-01-28

## Recommended Architecture

The framework is structured as a **Decoupled Reactive Graph**. High-priority A/V processing is separated from the UI and management logic via lock-free synchronization.

### Component Boundaries

| Component | Responsibility | Communicates With |
|-----------|---------------|-------------------|
| **Core (Graph Manager)** | Orchestrates the Unified Connection Graph and manages the "Scaffold -> Configure -> Hydrate" lifecycle. | Module Registry, Interaction Layer. |
| **Interaction Layer** | Translates external inputs (GUI, MIDI, OSC) into Core commands. | Core (via Command Queue). |
| **DSP / Module Engine** | High-priority execution of module logic (CV, video I/O, Audio). | Downstream Modules (via Push), Registry. |
| **Resource Layer** | Manages shared assets (Media Pool, Shaders, Buffers). | Core, DSP modules. |

### Data Flow

1. **Push Pipeline:** As soon as a module (e.g., Camera) produces data, it emits a `sigslot::signal`. Downstream modules connected to its ports receive the data immediately in the same thread context.
2. **Command Loop:** User interactions in ImGui are serialized into `Command` objects and pushed to a `moodycamel::ConcurrentQueue`.
3. **Synchronization Point:** At the start of each Engine frame, all pending commands are applied. The frame's result/state is then cloned into a "Snapshot" struct and sent to the UI thread.

---

## Connection Lifecycle

To ensure stability and resource efficiency, the Unified Connection Graph follows a strict three-stage lifecycle:

### 1. Scaffold (Structural)
- **What:** Create the module entity and its ports (Input/Output).
- **Goal:** Logical existence in the registry. No resources allocated yet.
- **Trigger:** User adds a module to the graph.

### 2. Configure (Logical)
- **What:** Set static parameters, link ports to other modules, and define data types.
- **Goal:** Establish the DAG (Directed Acyclic Graph) structure and resolve dependencies.
- **Trigger:** User connects modules or updates non-realtime settings.

### 3. Hydrate (Resource)
- **What:** Allocate buffers (PBOs, FBOs), initialize DSP kernels, and start threads.
- **Goal:** Ready for processing.
- **Trigger:** Finalized graph state or "Play" signal. Allows for validation (e.g., "Is the camera available?") before the stream starts.

---

## Suggested Build Order

Based on component dependencies, the refactor should proceed in this order:

1. **Foundation (Core/Graph):** Implement the `Port` and `Connection` models using `sigslot`. Define the basic `Module` interface and Registry (`EnTT`).
2. **Push Mechanism (DSP):** Build the reactive propagation engine. Verify "glitch-free" updates with a simple 3-node graph.
3. **Lifecycle Engine (Lifecycle):** Implement the `Scaffold -> Configure -> Hydrate` state machine for modules.
4. **Headless-First API (Interaction):** Build the `CommandQueue` and `Snapshot` bridge. Verify the engine can run via CLI/OSC without a window.
5. **UI Front-end (GUI):** Rebuild the ImGui layer to observe the `Snapshot` and push to the `CommandQueue`.

---

## Patterns to Follow

### Pattern 1: The Snapshot Pattern (UI Decoupling)
**What:** The UI thread and Engine thread never share mutable memory without a lock-free bridge.
**When:** All communication between ImGui and the Core.
**Example:**
```cpp
// Engine Thread
auto snapshot = engine.captureState();
snapshotQueue.try_enqueue(std::move(snapshot));

// UI Thread
StateSnapshot currentFrame;
while(snapshotQueue.try_dequeue(currentFrame)) {
    // Only the latest snapshot is used for rendering
}
renderUI(currentFrame);
```

### Pattern 2: Unified Push Ports
**What:** Every module has a set of `InputPort<T>` and `OutputPort<T>` based on `sigslot`.
**Instead of:** Modules reaching into other modules to get pointers or data.
**Example:**
```cpp
// module_a.out_port.connect(&module_b, &ModuleB::onDataReceived);
void ModuleA::process() {
    T result = do_work();
    out_port.emit(result); // Pushes result downstream
}
```

---

## Anti-Patterns to Avoid

### Anti-Pattern 1: The "Everything is an ofBaseApp" Trap
**What:** Inheriting from openFrameworks classes in core logic.
**Why bad:** Forces the whole framework to stay in the OF main thread, preventing high-performance multi-threading and making headless mode impossible.
**Instead:** Write pure C++ modules; only the "OF Runner" should know about `ofApp`.

### Anti-Pattern 2: Direct Pointer Modification from UI
**What:** ImGui sliders modifying `module->threshold` directly.
**Why bad:** Causes race conditions and memory corruption if the Engine is reading `threshold` on another thread.
**Instead:** Use `CommandQueue` or `std::atomic<float>`.

---

## Scalability Considerations

| Concern | 1-5 Modules | 50+ Modules | 500+ Modules |
|---------|--------------|--------------|-------------|
| **Execution** | Single-threaded is fine. | Use `Taskflow` for parallel graph execution. | Distributed processing / GPU offloading. |
| **UI Rendering** | Individual windows. | Node-based visual editor (ImNodes). | Searchable module lists & categories. |
| **State Sync** | Full state copy. | Delta-updates or EnTT snapshots. | Hierarchical state management. |

## Sources
- [Data-Oriented Design in C++ (ECS)](https://www.dataorienteddesign.com/dodmain/)
- [Reactive Programming for A/V (Rx Concepts)](http://reactivex.io/intro.html)
- [Lock-free Multithreading in Audio (JUCE Best Practices)](https://juce.com/)
