# Architecture

**Analysis Date:** 2026-01-28

## Pattern Overview

**Overall:** Modular Multi-Router System

**Key Characteristics:**
- **Capability-Based Modular Design**: Functional units are encapsulated in `Module` instances that declare capabilities (audio/video production, event handling).
- **Decoupled Routing**: Communication between modules (audio, video, parameters, events) is handled by specialized router classes coordinated by a central `ConnectionManager`.
- **JSON-Driven State Persistence**: The entire application state (modules, connections, project settings) is serialized to JSON for session and project management.

## Layers

**Core Management Layer:**
- Purpose: Manages the lifecycle of modules, session persistence, and project organization.
- Location: `src/core/`
- Contains: `ModuleRegistry.h`, `ModuleFactory.h`, `SessionManager.h`, `ProjectManager.h`
- Depends on: `src/modules/`
- Used by: `ofApp.h`

**Routing Layer:**
- Purpose: Facilitates all inter-module communication and external signal flow.
- Location: `src/core/` (Routers)
- Contains: `ConnectionManager.h`, `AudioRouter.h`, `VideoRouter.h`, `ParameterRouter.h`, `EventRouter.h`
- Depends on: `src/modules/`
- Used by: `src/core/SessionManager.h`, `ofApp.h`

**Module Layer:**
- Purpose: Implements specific audiovisual functionality (sequencing, playback, mixing).
- Location: `src/modules/`
- Contains: `Module.h` (base), `MediaPlayer.h`, `AudioMixer.h`, `TrackerSequencer.h`
- Depends on: `ofMain.h`, `ofxSoundObjects`, `ofxVisualObjects`
- Used by: `src/core/ModuleRegistry.h`, `src/gui/`

**Presentation Layer (GUI):**
- Purpose: Provides user interface for module control and system configuration.
- Location: `src/gui/`
- Contains: `GUIManager.h`, `ViewManager.h`, `ModuleGUI.h`, and specific module GUIs (e.g., `AudioMixerGUI.h`)
- Depends on: `ImGui`, `src/core/ModuleRegistry.h`
- Used by: `ofApp.h`

## Data Flow

**Control & Sequencing Flow:**

1. `TrackerSequencer` triggers a step based on `Clock` timing.
2. An `onTrigger` event is emitted containing a `TriggerEvent` with parameter values.
3. The `EventRouter` (via `ConnectionManager`) delivers the event to target `Module` instances (e.g., `MediaPlayer`).
4. Modules update their internal state (e.g., `mediaIndex`) based on the trigger.

**Signal Flow (Audio/Video):**

1. Source modules (e.g., `MediaPlayer`) produce `ofxSoundObject` or `ofxVisualObject` outputs.
2. `ConnectionManager` routes these outputs to processing modules (e.g., `AudioMixer`) or final outputs.
3. Final outputs (`AudioOutput`, `VideoOutput`) send buffers to the hardware via openFrameworks.

**State Management:**
- **Runtime State**: Centralized in `ModuleRegistry` (active modules) and `ConnectionManager` (active links).
- **Persistence**: `SessionManager` captures the state of all managers into a unified JSON structure for saving/loading.

## Key Abstractions

**Module:**
- Purpose: Unified interface for all functional units.
- Examples: `src/modules/MediaPlayer.h`, `src/modules/TrackerSequencer.h`
- Pattern: Strategy/Plugin pattern for extensible functionality.

**Port:**
- Purpose: Explicit declaration of input/output capabilities on a module.
- Examples: `Port` struct in `src/modules/Module.h`
- Pattern: Port-based routing.

**Connection:**
- Purpose: Logical link between ports of different modules.
- Examples: `Connection` struct in `src/core/ConnectionManager.h`
- Pattern: Directed graph edges.

## Entry Points

**Main Application:**
- Location: `src/main.cpp`
- Triggers: OS application launch.
- Responsibilities: OpenGL setup and openFrameworks application execution.

**App Delegate (ofApp):**
- Location: `src/ofApp.cpp`
- Triggers: openFrameworks event loop.
- Responsibilities: Initialization of all core managers, main update/draw loops, and coordinating high-level events (key/mouse/drag-drop).

## Error Handling

**Strategy:** Defensive programming with fallback states and logging via `Console`.

**Patterns:**
- **Null Checks**: Extensive use of null checks for module lookup in registry.
- **Graceful Degradation**: Modules provide default implementations for optional features (e.g., `update()`, `draw()`).

## Cross-Cutting Concerns

**Logging:** Centralized via `src/gui/Console.h` for user-visible logs.
**Validation:** `src/core/ModuleRegistry.h` validates module names and UUID uniqueness.
**Authentication:** Not applicable (local application).

---

*Architecture analysis: 2026-01-28*
