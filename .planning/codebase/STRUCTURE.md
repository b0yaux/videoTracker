# Codebase Structure

**Analysis Date:** 2026-01-28

## Directory Layout

```
[project-root]/
├── src/            # Application source code
│   ├── core/       # Central management and routing systems
│   ├── modules/    # Functional audiovisual modules (SunVox-style)
│   ├── gui/        # ImGui-based user interface components
│   ├── input/      # Command execution and input routing
│   ├── data/       # Project data structures (Patterns, Chains)
│   └── utils/      # Helper utilities and background services
├── libs/           # Integrated third-party libraries (source/headers)
├── bin/            # Compiled binaries and runtime assets
├── addons/         # openFrameworks addons (ImGui, FileBrowser)
├── screenshots/    # Application screenshots and recordings
└── docs/           # Research, diagrams, and refactoring plans
```

## Directory Purposes

**src/core/:**
- Purpose: Orchestrates the modular system and handles persistence.
- Contains: Registries, factories, session managers, and routers.
- Key files: `ModuleRegistry.h`, `ConnectionManager.h`, `SessionManager.h`, `ProjectManager.h`.

**src/modules/:**
- Purpose: Contains the "brain" of the application—individual functional units.
- Contains: Audio/video sources, sequencers, and mixers.
- Key files: `Module.h`, `TrackerSequencer.h`, `MediaPlayer.h`, `AudioMixer.h`.

**src/gui/:**
- Purpose: Implements the visual representation and control for all systems.
- Contains: Global UI managers and module-specific control panels.
- Key files: `GUIManager.h`, `ViewManager.h`, `MenuBar.h`, `Console.h`.

**src/input/:**
- Purpose: Handles user commands and maps physical inputs to system actions.
- Contains: Command executors and input routers.
- Key files: `CommandExecutor.h`, `InputRouter.h`.

**src/data/:**
- Purpose: Defines the core data models for the sequencer.
- Contains: Pattern and Chain definitions.
- Key files: `Pattern.h`, `PatternChain.h`.

**src/utils/:**
- Purpose: Provides supporting services for the main application.
- Contains: Timing, file conversion, and asset management.
- Key files: `Clock.h`, `AssetLibrary.h`, `MediaConverter.h`.

## Key File Locations

**Entry Points:**
- `src/main.cpp`: Standard C++ entry point; initializes OpenGL and openFrameworks.
- `src/ofApp.cpp`: Main application loop and event coordination.

**Configuration:**
- `Project.xcconfig`: Xcode build configuration.
- `addons.make`: List of openFrameworks addons to include.

**Core Logic:**
- `src/core/ConnectionManager.h`: The "master patch bay" of the application.
- `src/modules/Module.h`: The base contract for all functional expansion.

**Testing:**
- `test_include.cpp`: Basic include test file.

## Naming Conventions

**Files:**
- PascalCase for classes: `TrackerSequencer.cpp`
- camelCase for descriptors: `ParameterDescriptor` (structs)

**Directories:**
- lower_case or kebab-case: `core`, `gui`, `input`

## Where to Add New Code

**New Feature (e.g., a new synth or effect):**
- Primary code: `src/modules/NewFeature.h` (must inherit from `Module`)
- Registration: Add to `src/core/ModuleFactory.cpp`
- UI: `src/gui/NewFeatureGUI.h` (optional, can use generic `ModuleGUI`)

**New Component/Module:**
- Implementation: `src/modules/`

**Utilities:**
- Shared helpers: `src/utils/`

## Special Directories

**bin/data/:**
- Purpose: Contains runtime assets like fonts, icons, and temporary files.
- Generated: No
- Committed: Usually contains essential assets; large media is typically ignored.

**.planning/:**
- Purpose: Metadata for agentic coordination and project planning.
- Generated: Yes
- Committed: Yes

---

*Structure analysis: 2026-01-28*
