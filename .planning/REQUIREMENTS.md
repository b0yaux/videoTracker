# Requirements: videoTracker Refactor

**Defined:** 2026-01-28
**Core Value:** A maintainable, modular A/V framework with a unified connection graph and a clean separation between core logic and interaction layers.

## v1 Requirements

### Infrastructure & Safety
- [ ] **INF-01**: Implement lock-free command/state bridges (moodycamel) between Engine and UI.
- [ ] **INF-02**: Eliminate all legacy mutexes from the real-time A/V hot path.
- [ ] **INF-03**: Implement parallel graph execution (Taskflow integration) for multi-core scaling.
- [ ] **INF-04**: Add real-time performance monitoring and profiling hooks.

### Connection Graph (Push Model)
- [ ] **GRPH-01**: Implement Unified Port API (Audio, Video, Data) with Push-based propagation.
- [ ] **GRPH-02**: Implement the full Scaffold -> Configure -> Hydrate lifecycle for module/graph state changes.
- [ ] **GRPH-03**: Implement topological sorting and deterministic execution for the Push graph.
- [ ] **GRPH-04**: Handle cyclic feedback loops via explicit delay nodes or validation.

### Modularization (Composition)
- [ ] **MOD-01**: Refactor module base class to favor Composition over deep Inheritance.
- [ ] **MOD-02**: Decompose current monolithic modules into smaller, reusable components.
- [ ] **MOD-03**: Standardize internal APIs for module registration and port declaration.

### UI & Interaction
- [ ] **UI-01**: Extract all ImGui presentation logic from Core classes into a decoupled layer.
- [ ] **UI-02**: Implement the Command Shell for deep command-style interaction.
- [ ] **UI-03**: Ensure UI strictly uses Snapshot/Command bridges to interact with the Engine.

## v2 Requirements (Deferred)
- **UI-04**: Full Headless mode (Core runs as a separate process or without GL window).
- **INT-01**: Lua Scripting as a primary, first-class interface (beyond existing experimentation).
- **MOD-04**: Metadata-driven parameter system for automated UI/Binding generation.

## Out of Scope
| Feature | Reason |
|---------|--------|
| Engine Replacement | Refactor is architectural; underlying DSP/GL engines (ofxSoundObjects) remain. |
| Mobile Platforms | Target is restricted to macOS/Linux desktop. |
| Scripting-Ref Branch | Deprecated over-complexified experimentation. |

## Traceability
| Requirement | Phase | Status |
|-------------|-------|--------|
| INF-01 | Pending | Pending |
| INF-02 | Pending | Pending |
| INF-03 | Pending | Pending |
| INF-04 | Pending | Pending |
| GRPH-01 | Pending | Pending |
| GRPH-02 | Pending | Pending |
| GRPH-03 | Pending | Pending |
| GRPH-04 | Pending | Pending |
| MOD-01 | Pending | Pending |
| MOD-02 | Pending | Pending |
| MOD-03 | Pending | Pending |
| UI-01 | Pending | Pending |
| UI-02 | Pending | Pending |
| UI-03 | Pending | Pending |

**Coverage:**
- v1 requirements: 14 total
- Mapped to phases: 0
- Unmapped: 14 ⚠️

---
*Requirements defined: 2026-01-28*
