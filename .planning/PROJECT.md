# videoTracker: Architecture Refactor

## What This Is

A refactor of the videoTracker framework (OpenFrameworks-based) to replace its over-engineered multi-router system with a cleaner, highly modular architecture. It targets a unified "Push" connection model, composition-based modularization, and a fully decoupled UI to support multiple interaction modes (GUI, CLI, Scripting).

## Core Value

A maintainable, modular A/V framework with a unified connection graph and a clean separation between core logic and interaction layers.

## Requirements

### Validated

- ✓ **JSON Persistence** — State and project management via JSON serialization.
- ✓ **Capability-Based Units** — Modules that declare and produce audio/video/event signals.
- ✓ **DSP/GL Integration** — Leveraging ofxSoundObjects and ofxVisualObjects for processing.
- ✓ **High-Performance Video** — Optimized playback via ofxHapPlayer.
- ✓ **Scripting Support** — Lua integration via ofxLua for live interaction.

### Active

- [ ] **Push Connection Model** — Shift from Pull-based data requests to a unified Push-based propagation system.
- [ ] **Connection Lifecycle** — Implement the Scaffold -> Configure -> Hydrate lifecycle for the connection graph.
- [ ] **Composition over Inheritance** — Refactor module architecture to favor composition, decomposing monolithic classes into smaller functional units.
- [ ] **API Consolidation** — Simplify and unify internal APIs for consistency and better maintainability.
- [ ] **UI Decoupling** — Extract ImGui-based presentation logic into a dedicated layer, enabling headless operation and alternative UIs (commandShell).
- [ ] **Thread Safety & Command Queues** — Implement robust real-time safety and command-style interaction patterns.

### Out of Scope

- **Engine Replacement** — Replacing ofxSoundObjects or ofxVisualObjects (refactor focuses on management/routing, not the underlying DSP/GL).
- **Mobile Support** — The focus remains on desktop (macOS/Linux) environments.
- **Master Scripting Branch** — The over-engineered scripting experimentation is deprecated in favor of the new modular framework.

## Context

The current `videoTracker` codebase has become over-complexified with fragmented routing (AudioRouter, VideoRouter, etc.) and tightly coupled UI. This refactor aims to simplify the system into a "simple yet modular" framework inspired by modular A/V tools like Mosaic, BespokeSynth, and PureData.

## Constraints

- **Tech Stack**: C++17, openFrameworks v0.12.1.
- **Platform**: macOS (primary), Linux.
- **Threading**: Must maintain real-time performance for A/V processing.
- **Backward Compatibility**: Aim to preserve existing JSON state structure where possible, or provide a clear migration path.

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Push Connection Model | Simplifies data propagation and reduces redundant state requests. | — Pending |
| Composition Refactor | Improves modularity and allows for more flexible module assembly. | — Pending |
| Headless-First UI | Decouples core logic from ImGui, enabling CLI and scripting interactions. | — Pending |

---
*Last updated: 2026-01-28 after initialization*
