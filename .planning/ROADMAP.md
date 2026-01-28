# Roadmap: videoTracker Refactor

## Overview

This roadmap outlines the systematic refactor of the videoTracker framework from a monolithic, pull-based architecture to a decoupled, reactive, and composition-based system. The transition is divided into five phases, starting with real-time safety and moving through modularity, connection logic, and finally UI decoupling.

## Phases

### Phase 1: Foundation & RT-Safety
**Goal:** Establish a thread-safe, high-performance infrastructure for the A/V engine.

- **Requirements:** INF-01, INF-02, INF-03, INF-04
- **Success Criteria:**
  - Real-time A/V callbacks operate without mutex-based lock contention.
  - Command and State bridges (moodycamel) handle bi-directional thread communication with zero allocation in the hot path.
  - Taskflow integration enables parallel execution of independent graph branches.
  - Real-time profiling hooks provide visibility into execution latency.

### Phase 2: Core Modularization
**Goal:** Refactor the module architecture to favor composition and standardized APIs.

- **Dependencies:** Phase 1
- **Requirements:** MOD-01, MOD-02, MOD-03
- **Success Criteria:**
  - Monolithic module classes are decomposed into functional components.
  - Module registration and port declaration use a unified, minimal API.
  - Composition-based assembly of modules is demonstrated with existing core features.

### Phase 3: Reactive Connection Graph
**Goal:** Implement the unified Push-based propagation model for deterministic signal flow.

- **Dependencies:** Phase 2
- **Requirements:** GRPH-01, GRPH-03, GRPH-04
- **Success Criteria:**
  - Unified Port API supports Audio, Video, and Data signals via Push propagation.
  - Topological sorting ensures zero-frame-lag updates across the connection graph.
  - Feedback loops are detected and handled via explicit validation or delay nodes.

### Phase 4: Lifecycle & Persistence
**Goal:** Implement robust graph state management and non-blocking resource hydration.

- **Dependencies:** Phase 3
- **Requirements:** GRPH-02
- **Success Criteria:**
  - Full Scaffold -> Configure -> Hydrate lifecycle manages all graph state changes.
  - Asset loading (textures, buffers) occurs asynchronously without blocking the UI or Engine.
  - Project state (JSON) is correctly mapped to the new lifecycle-aware graph.

### Phase 5: Interaction & UI Decoupling
**Goal:** Fully decouple the UI from the engine and enable alternative interaction modes.

- **Dependencies:** Phase 4
- **Requirements:** UI-01, UI-02, UI-03
- **Success Criteria:**
  - Engine runs headlessly with all UI logic extracted into a presentation layer.
  - UI updates are driven exclusively by state snapshots from the Engine.
  - Command Shell enables deep interaction with the system via CLI/Scripting commands.

## Progress Tracking

| Phase | Description | Status | Progress |
|-------|-------------|--------|----------|
| 1 | Foundation & RT-Safety | Pending | 0% |
| 2 | Core Modularization | Pending | 0% |
| 3 | Reactive Connection Graph | Pending | 0% |
| 4 | Lifecycle & Persistence | Pending | 0% |
| 5 | Interaction & UI Decoupling | Pending | 0% |

---
*Last updated: 2026-01-28*
