# videoTracker

openFrameworks / ImGui / C++ project - modular audiovisual sampling and synthesis with live-coding support.

## Overview

videoTracker is a modular audiovisual sequencer that combines:
- **Tracker-style sequencing** with step-based patterns
- **Modular architecture** for flexible routing and processing
- **Live-coding capabilities** via Lua scripting (in development)
- **Multiple interaction modes** via Shell system (Command, Code, Editor)

### Key Features

- **Tracker-Style Sequencer**: Step-based pattern sequencing with multi-pattern support and pattern chaining
- **Modular Architecture**: Plugin-style module system supporting sequencers, instruments, effects, and utilities
- **Media Management**: Automatic media file scanning, pairing, and playback with drag-and-drop support
- **Real-Time Mixing**: Separate audio and video mixers with routing capabilities
- **Sample-Accurate Timing**: Audio-rate clock system for precise synchronization
- **Session Management**: Complete project and session save/load with asset management
- **Parameter Routing**: Flexible parameter modulation and automation system
- **Connection Management**: Dynamic module connections for audio, video, parameters, and events
- **Live-Coding Shells**: Multiple interaction modes (Command, Code, Editor) with state synchronization

### Shell System

videoTracker provides three interaction modes:

- **CommandShell** (F1): Terminal-style REPL for quick commands and system management
- **CodeShell** (F2): Live-coding environment with Lua editor and REPL output
- **EditorShell** (F3): Traditional GUI with tiled windows and visual editing

All shells stay synchronized via ScriptManager, which generates Lua scripts from Engine state.

### Scripting & Live-Coding

**Current State**: ScriptManager generates session reconstruction scripts (command-based)  
**Goal**: Declarative live-coding syntax inspired by Tidal/Strudel/Hydra/SuperCollider  
**Status**: Theorically : Foundation complete, high-level API and pattern DSL compiler pending. In practice : live-coding not working (scripting sync system malfunctions, malloc errors & crashes)



## Remaining issues / needed enhancements :

### trackerSequencer (+ GUI) :

**Pattern grid :**

- 'Index' cells max value should depend on mediaPool's index count if connected and >0
- Pattern should be scrollable when too long to display in tracker window, and pattern should auto-scroll during playback (for now it only auto-scroll when user navigate using keyboard)

**Pattern controls GUI :**

- remove 'clear pattern' and 'D' big buttons in pattern control section

**Pattern chain GUI :**

- refactor existing pattern chaining to use a proper CellGrid table instead (inspired by how we use CellGrids elsewhere in our modules), where each column is a pattern in chain :
	- header : pattern selector with Header Popup allowing selection of any existing pattern. user can drag them to reoder patterns
	- row 0 : numbered chain position : replace existing '01' '02' ... buttons (theses should never move when reordering column headers)
	- row 1 : simple pattern count (similar to current)
- keep the existing +/-/D buttons for now aside the table on the right ON/OFF toggle on the left, and adapt all working features to this layout (like pattern mute when playing)

### multiSampler (+GUI)

**Parameter Grid :**

- should clarify PLAY/STOP toggle button and Index cell : index cell set the active index, PLAY button is used to control manual playback & display current playback state for active index
- 'loop Size' parameter cell is inconvenient because of custom mapping (impossible to edit properly like other cells, present issues when trying to edit buffer, should prevent this while preserving some kind of logarithmic precision)
- add BLEND MODE MULTI-STATE button when in POLY mode

### assetLibrary

**Refresh asset list :**

- re-scan project directory for converted assets (handle assets that user adds manually & folders that have been manually re-organized)

**Conversion settings GUI :**

- adjust conversion configuration to allow smaller file sizes w/ more compression in a GUI menu in assetLibrary

**Tooltip Preview :**

- Waveforms for Audio-only assets are not properly generated (displays : 'Waveform not cached (will be generated on re-import)'), despite it works for AV assets.

## Future features & ideas :

consider sorting ideas depending on complexity

**high priority : high value for performance purposes**

### Modulators

In our videoTracker app, we have a ParameterCell, a ParameterPath class, and ParameterRouter.

In GUI, Cellwidgets are editable cells that contains parameters values.

The goal of this system is to have a unified parameter system, in order to enable parameter mappings and modulations (as in ableton live, or bespokeSynth).

Analyze the codebase to explain the current state, and how far we are for proper mapping system and modulation (with a potential new LFO class)

### Better multi-input handling

- For now, our 'InputRouter' class handle keyboard input. it may better be refactored to a 'KeyboardInput' class, alongside which we could add a 'GamepadInput' class in order to handle mapping a gamePad to some parameter
- currently, the '0.1' increment works with cmd+arrow keys, which isn't right and conflicts with our navigation shortcut. How to properly use the 'Control' macos Key here if 'KeyCtrl' is command ? and the previous keyMods & imGuiMod_Ctrl doesn't work as well, why ?

Should we leverage openFrameworks for proper modifier state ?

**medium priority : high value, potentially complex to fully implement**

### Oscilloscope (or spectrogram?)

- implement simple oscilloscope / spectrogram audiovisualizations for usage across codebase ; for example as optional display above app background (inspired by MiniMeters audiovisualizations)

### AV feedback loops

- Integrate live system desktop captures to have a live AV input of the software window (with or without GUI) and use it for live AV feedback
- Use Audiovisual recording feature with external OBS (or integrated feature below)
- Use a webcam (insta360) inside OBS then use OBS as webcam in-app ?

### Audiovisual recording (ffmpeg integration ?)

- Simple integrated RECORD button to quickly record an AV media for saving purpose (with or without GUI)

**low priority : accessory value, potentially complex**

### Rolling sampler (Audio-only or AV)

- Inspired by bird's rolling sampler, w/ direct drag&drop

## Long-term emancipation plans [sur la comète] :

Forgetting oF dependency for better long term architecture, more control, but with more work to build video processing (hypothesis) :

hello-imgui (App Framework)
	├── ImGui (GUI)
	├── JUCE (Audio Engine)
	└── Custom Video Processing (or other library comparable to openFrameworks?)
		├── FFmpeg directly
		├── OpenCV
		└── Custom OpenGL rendering

# Key Features

- **Tracker-Style Sequencer**: Step-based pattern sequencing with multi-pattern support and pattern chaining
- **Modular Architecture**: Plugin-style module system supporting sequencers, instruments, effects, and utilities
- **Media Pool Management**: Automatic media file scanning, pairing, and playback with drag-and-drop support
- **Real-Time Mixing**: Separate audio and video mixers with routing capabilities
- **Sample-Accurate Timing**: Audio-rate clock system for precise synchronization
- **Session Management**: Complete project and session save/load with asset management
- **Parameter Routing**: Flexible parameter modulation and automation system
- **Connection Management**: Dynamic module connections for audio, video, parameters, and events


## Architecture

### Core Design Philosophy

videoTracker follows a **modular architecture** inspired by SunVox and BespokeSynth, with live-coding capabilities inspired by Tidal/Strudel/Hydra/SuperCollider:

**Current State**:
- ✅ Modular architecture with module-based routing
- ✅ PatternRuntime for pattern management
- ✅ ScriptManager for state synchronization
- ✅ Multiple shells (Command, Code, Editor) with auto-sync
- ⏳ Live-coding syntax (pattern DSL compiler pending)
- ⏳ High-level Lua API (declarative functions pending)

**Architecture Layers**:
1. **Engine** (Headless Core): ModuleRegistry, PatternRuntime, ConnectionManager, ScriptManager
2. **Shells** (UI Modes): CommandShell, CodeShell, EditorShell - all synchronized via ScriptManager
3. **Modules** (Plugins): TrackerSequencer, MultiSampler, AudioMixer, VideoMixer, etc.
4. **Pattern System**: PatternRuntime (first-class patterns) + PatternCompiler (future: DSL → Patterns)

videoTracker follows a **modular architecture** inspired by SunVox and BespokeSynth, where:

- **Modules** are self-contained units that can produce/consume audio, video, parameters, and events
- **TrackerSequencer** generates discrete trigger events that modules respond to
- **Clock** provides the single source of truth for global transport state and sample-accurate timing
- **ConnectionManager** handles dynamic routing between modules
- **ParameterRouter** enables flexible parameter modulation and automation

### Module System

The application is built around a unified `Module` base class that provides:

#### Module Types

- **SEQUENCER**: Generates trigger events (e.g., `TrackerSequencer`)
- **INSTRUMENT**: Responds to triggers (e.g., `MultiSampler`, `MIDIOutput`)
- **EFFECT**: Processes audio/video (future: video effects, audio effects)
- **UTILITY**: Routing, mixing, utilities (e.g., `AudioMixer`, `VideoMixer`)

#### Module Capabilities

Modules declare capabilities rather than relying on type checks:

- `ACCEPTS_FILE_DROP`: Can accept file drops (e.g., `MultiSampler`)
- `REQUIRES_INDEX_RANGE`: Needs index range callback (optional, defaults to 0-127)
- `PROVIDES_INDEX_RANGE`: Provides index range (e.g., `MultiSampler`)
- `EMITS_TRIGGER_EVENTS`: Emits trigger events (e.g., `TrackerSequencer`)
- `ACCEPTS_TRIGGER_EVENTS`: Accepts trigger events (e.g., `MultiSampler`)

#### Module Interface

All modules implement:

```cpp
// Identity
virtual std::string getName() const = 0;
virtual ModuleType getType() const = 0;

// Parameters
virtual std::vector<ParameterDescriptor> getParameters() = 0;
virtual void setParameter(const std::string& paramName, float value, bool notify = true) = 0;
virtual float getParameter(const std::string& paramName) const = 0;

// Trigger events
virtual void onTrigger(TriggerEvent& event) = 0;

// Routing
virtual ofxSoundObject* getAudioOutput() const { return nullptr; }
virtual ofxVisualObject* getVideoOutput() const { return nullptr; }

// Serialization
virtual ofJson toJson() const = 0;
virtual void fromJson(const ofJson& json) = 0;
```

### Core Components

#### Clock (`src/Clock.h`)

The **single source of truth** for global transport state and timing:

- Sample-accurate timing without PPQN
- Unified time event system (BEAT and STEP events)
- Transport control (start/stop/pause/reset)
- BPM control with smooth transitions
- Steps-per-beat configuration
- Transport listener system for module synchronization

**Key Principle**: All components query `clock.isPlaying()` rather than maintaining their own transport state.

#### TrackerSequencer (`src/TrackerSequencer.h`)

The main sequencer module that generates trigger events:

- Pattern-based step sequencing
- Multi-pattern support with pattern chaining
- Per-pattern step counts (configurable)
- Column-based parameter control (note, position, speed, volume, etc.)
- Pattern chain with repeat counts
- Sample-accurate step triggering
- Gating support for step duration control

**Trigger Events**: Emits `TriggerEvent` objects containing parameter maps that modules can respond to.

#### MultiSampler (`src/modules/MediaPool.h`)

AV sample playback instrument with polyphonic voice allocation (formerly MediaPool):

##### Parameters

| Parameter | Type | Range | Default | Description |
|-----------|------|-------|---------|-------------|
| `index` | INT | 0 to N-1 | 0 | Sample index to trigger |
| `position` | FLOAT | 0.0-1.0 | 0.0 | Start position within region (relative) |
| `speed` | FLOAT | -10.0 to 10.0 | 1.0 | Playback speed (negative = reverse) |
| `volume` | FLOAT | 0.0-2.0 | 1.0 | Output volume multiplier |
| `loopSize` | FLOAT | 0.001-10.0s | 1.0 | Loop region size in seconds |
| `regionStart` | FLOAT | 0.0-1.0 | 0.0 | Region start (absolute position) |
| `regionEnd` | FLOAT | 0.0-1.0 | 1.0 | Region end (absolute position) |
| `polyphonyMode` | INT | 0-1 | 0 | 0=Mono, 1=Poly |

##### Play Styles

| Style | Behavior |
|-------|----------|
| **ONCE** | Play once, stop at region end |
| **LOOP** | Loop within loopSize region from startPosition |
| **NEXT** | Loop with position memory (continues where left off) |

##### Features

- 16-voice polyphony pool with LRU voice stealing
- 4 players per sample for true polyphonic playback of same sample
- Audio + Video mixing via internal mixers
- Complete preloading for zero-latency triggers
- Region trimming (regionStart/regionEnd)
- Variable-size loop regions (loopSize)
- Bidirectional playback (negative speed)
- File drag-and-drop support
- Auto-pairing (audio+video by filename)
- Sequencer trigger integration via `onTrigger`
- Waveform display with zoom/pan (up to 10000x zoom)
- Preview scrubbing in IDLE mode

##### Ports

| Port | Type | Description |
|------|------|-------------|
| `trigger_in` | EVENT_IN | Receives trigger events from sequencer |
| `audio_out` | AUDIO_OUT | Mixed audio output |
| `video_out` | VIDEO_OUT | Mixed video output |

##### Capabilities

- `ACCEPTS_FILE_DROP` - Drag files directly to add samples
- `ACCEPTS_TRIGGER_EVENTS` - Receives sequencer triggers

#### ModuleRegistry (`src/core/ModuleRegistry.h`)

Centralized storage and lookup for module instances:

- UUID-based module identification
- Human-readable name mapping
- Thread-safe access (GUI thread)
- Weak pointer support to avoid circular dependencies
- Module lifecycle management

#### ConnectionManager (`src/core/ConnectionManager.h`)

Dynamic routing system for module connections:

- Audio connections (ofxSoundObject routing)
- Video connections (ofxVisualObject routing)
- Parameter connections (parameter modulation)
- Event connections (trigger event subscriptions)
- Connection discovery and validation
- Session save/load for connections

#### ParameterRouter (`src/core/ParameterRouter.h`)

Flexible parameter modulation system:

- Parameter path syntax (e.g., `"moduleName.parameterName"`)
- Real-time parameter updates
- Parameter change callbacks
- Integration with ConnectionManager for automated routing

#### SessionManager (`src/core/SessionManager.h`)

Unified session and project management:

- Project-based organization
- Session save/load (JSON format)
- Asset management integration
- Module state serialization
- Connection restoration

### Audio/Video Pipeline

#### Audio Mixing

- **AudioMixer** (`src/AudioMixer.h`): Module for mixing multiple audio sources
- **AudioOutput** (`src/AudioOutput.h`): Master audio output with device management
- Uses `ofxSoundObjects` for audio routing

#### Video Mixing

- **VideoMixer** (`src/VideoMixer.h`): Module for mixing multiple video sources
- **VideoOutput** (`src/VideoOutput.h`): Master video output with display management
- Uses `ofxVisualObjects` for video routing

## Project Structure

```
src/
├── Module.h                    # Base module interface
├── TrackerSequencer.h/cpp      # Main sequencer module
├── MediaPool.h/cpp             # MultiSampler - AV sample instrument
├── Pattern.h/cpp               # Pattern data structure
├── Clock.h/cpp                 # Sample-accurate timing
├── MediaPlayer.h/cpp           # Individual media playback
├── AudioMixer.h/cpp            # Audio mixing module
├── VideoMixer.h/cpp            # Video mixing module
├── AudioOutput.h/cpp           # Master audio output
├── VideoOutput.h/cpp            # Master video output
├── core/                       # Core systems
│   ├── ModuleRegistry.h/cpp    # Module storage and lookup
│   ├── ModuleFactory.h/cpp     # Module creation
│   ├── ConnectionManager.h/cpp # Connection routing
│   ├── ParameterRouter.h/cpp   # Parameter modulation
│   ├── SessionManager.h/cpp    # Session management
│   └── ProjectManager.h/cpp    # Project management
├── gui/                        # GUI components
│   ├── GUIManager.h/cpp        # GUI coordination
│   ├── ViewManager.h/cpp       # View management
│   ├── ModuleGUI.h/cpp          # Base module GUI
│   ├── TrackerSequencerGUI.h/cpp
│   ├── MediaPoolGUI.h/cpp      # MultiSamplerGUI
│   └── ...
└── input/                      # Input handling
    └── InputRouter.h/cpp       # Input routing
```

## Building

### Prerequisites

- openFrameworks v0.12.1
- Xcode (macOS) or appropriate IDE for your platform
- Required addons (see `addons.make`)

### Build Instructions

1. Ensure openFrameworks is properly installed
2. Navigate to the project directory
3. Generate project files:
   ```bash
   make
   ```
4. Open the generated Xcode project (or use your IDE)
5. Build and run

### Required Addons

Check `addons.make` for the complete list. Key addons include:

- `ofxSoundObjects` - Audio routing and processing
- `ofxVisualObjects` - Video routing and processing
- `ofxImGui` (or direct ImGui integration) - GUI system

## Development

### Adding a New Module

1. **Inherit from Module**:
   ```cpp
   class MyModule : public Module {
   public:
       std::string getName() const override { return "MyModule"; }
       ModuleType getType() const override { return ModuleType::INSTRUMENT; }
       // ... implement required methods
   };
   ```

2. **Register with ModuleFactory**:
   ```cpp
   moduleFactory.registerModuleType("MyModule", []() {
       return std::make_shared<MyModule>();
   });
   ```

3. **Implement Required Interfaces**:
   - `getParameters()` - Declare available parameters
   - `setParameter()` / `getParameter()` - Parameter access
   - `onTrigger()` - Handle trigger events (if applicable)
   - `toJson()` / `fromJson()` - Serialization
   - `getMetadata()` - Module metadata

4. **Add GUI** (optional):
   - Create `MyModuleGUI` inheriting from `ModuleGUI`
   - Register with `GUIManager`

### Module Lifecycle

1. **Creation**: Module created via `ModuleFactory`
2. **Registration**: Added to `ModuleRegistry` with UUID and name
3. **Setup**: `postCreateSetup(Clock*)` called for initialization
4. **Configuration**: `configureSelf()` called after connections discovered
5. **Restoration**: `completeRestore()` called after session load for deferred operations

### Connection Types

Modules can connect via:

- **AUDIO (0)**: Audio signal routing (`ofxSoundObject`)
- **VIDEO (1)**: Video signal routing (`ofxVisualObject`)
- **PARAMETER (2)**: Parameter modulation
- **EVENT (3)**: Trigger event subscriptions

### Parameter System

Parameters are declared via `ParameterDescriptor`:

```cpp
ParameterDescriptor(
    "position",              // name
    ParameterType::FLOAT,    // type
    0.0f,                    // min
    1.0f,                    // max
    0.5f,                    // default
    "Position"              // display name
)
```

Parameters can be:
- Set directly via `setParameter()`
- Modulated via `ParameterRouter`
- Mapped to sequencer columns
- Automated via connections

## Technical Details

### Threading Model

- **GUI Thread**: Module creation, destruction, and state updates
- **Audio Thread**: Sample-accurate timing and audio processing
- **Lock-Free Communication**: Event queues for cross-thread communication

### Timing System

- **Sample-Accurate**: Clock operates at audio sample rate
- **No PPQN**: Direct time-based calculation without pulse-per-quarter-note
- **Event-Based**: Time events fire at precise sample boundaries
- **Smooth BPM**: BPM changes are smoothed to avoid clicks

### Serialization

All modules implement JSON serialization:

- **toJson()**: Export module state to JSON
- **fromJson()**: Restore module state from JSON
- **completeRestore()**: Handle deferred operations (e.g., file loading)

Sessions are saved as JSON files with:
- Module instances and their state
- Connections between modules
- Project metadata
- Asset references

## License

[Specify your license here]

## Contributing

[Contributing guidelines if applicable]

## Acknowledgments

- Built with [openFrameworks](https://openframeworks.cc/)
- Inspired by SunVox, BespokeSynth, and other modular synthesis environments
- Uses [ofxSoundObjects](https://github.com/npisanti/ofxSoundObjects) and [ofxVisualObjects](https://github.com/npisanti/ofxVisualObjects) for audio/video routing

