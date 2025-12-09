# videoTracker

A modular audiovisual sequencer built with openFrameworks, featuring a tracker-style step sequencer, media pool management, and real-time audio/video mixing capabilities.

## Overview

videoTracker is a professional-grade audiovisual performance and composition tool that combines the precision of tracker-style sequencing with the flexibility of modular synthesis. It provides sample-accurate timing, real-time media playback, and a powerful module-based architecture for creating complex audiovisual compositions.

### Key Features

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
- **INSTRUMENT**: Responds to triggers (e.g., `MediaPool`, `MIDIOutput`)
- **EFFECT**: Processes audio/video (future: video effects, audio effects)
- **UTILITY**: Routing, mixing, utilities (e.g., `AudioMixer`, `VideoMixer`)

#### Module Capabilities

Modules declare capabilities rather than relying on type checks:

- `ACCEPTS_FILE_DROP`: Can accept file drops (e.g., `MediaPool`)
- `REQUIRES_INDEX_RANGE`: Needs index range callback (optional, defaults to 0-127)
- `PROVIDES_INDEX_RANGE`: Provides index range (e.g., `MediaPool`)
- `EMITS_TRIGGER_EVENTS`: Emits trigger events (e.g., `TrackerSequencer`)
- `ACCEPTS_TRIGGER_EVENTS`: Accepts trigger events (e.g., `MediaPool`)

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

#### MediaPool (`src/MediaPool.h`)

Media library and playback module:

- Automatic media file scanning and pairing
- Drag-and-drop file support
- Multiple playback modes (ONCE, LOOP, NEXT)
- Position scanning modes (NONE, PER_STEP, PER_MEDIA, GLOBAL)
- Polyphony modes (MONOPHONIC, POLYPHONIC)
- Lock-free event queue for sequencer triggers
- Audio/video output routing via internal mixers

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
├── MediaPool.h/cpp             # Media library and playback
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
│   ├── MediaPoolGUI.h/cpp
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

## Usage

### Basic Workflow

1. **Start the Application**: The app creates default modules (TrackerSequencer and MediaPool)
2. **Load Media**: Drag media files into the MediaPool or use the file browser
3. **Create Patterns**: Use the TrackerSequencer to create step patterns
4. **Connect Modules**: Modules automatically connect, or use the connection manager
5. **Play**: Use the Clock transport controls to start playback

### Module Management

- **Add Module**: Use the module factory to create new instances
- **Remove Module**: Delete modules through the GUI or programmatically
- **Configure Module**: Each module has its own GUI panel for configuration

### Pattern Sequencing

- **Set Steps**: Configure step count per pattern
- **Edit Cells**: Click cells in the sequencer grid to set values
- **Pattern Chain**: Create pattern chains with repeat counts
- **Parameter Columns**: Map sequencer columns to module parameters

### Media Management

- **Scan Directory**: MediaPool automatically scans for media files
- **File Pairing**: Audio/video files are automatically paired by base name
- **Playback Modes**: Configure ONCE, LOOP, or NEXT playback styles
- **Position Scanning**: Choose how position is tracked across triggers

### Session Management

- **Save Session**: Save complete project state including modules, connections, and media paths
- **Load Session**: Restore previous sessions with all state intact
- **Project Organization**: Organize work into projects with asset management

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

