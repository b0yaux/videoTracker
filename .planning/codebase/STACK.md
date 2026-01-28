# Technology Stack

**Analysis Date:** 2026-01-28

## Languages

**Primary:**
- C++ (likely C++17) - Core application logic, OpenFrameworks framework.

**Secondary:**
- Lua - Scripting support via `ofxLua` (detected in `addons.make`).

## Runtime

**Environment:**
- OpenFrameworks v0.12.1 - Creative coding framework.

**Package Manager:**
- OpenFrameworks Addons - Managed via `addons.make`.
- Lockfile: N/A (Project uses `addons.make` and `Project.xcconfig`).

## Frameworks

**Core:**
- OpenFrameworks - Main application framework.

**UI:**
- ImGui v1.92.5 - Integrated directly (replaces `ofxImGui`).
- implot - Plotting library for ImGui.

**Testing:**
- Not detected (likely manual testing or internal scripts).

**Build/Dev:**
- Makefile - standard OF build system.
- CMake - `CMakeLists.txt` present.
- Xcode - `.xcodeproj` for macOS development.

## Key Dependencies

**Critical:**
- `ofxSoundObjects` - Audio routing and processing.
- `ofxVisualObjects` - Visual routing and processing.
- `ofxHapPlayer` - High-performance video playback.
- `ofxAVcpp` - Audio/Video processing utilities.
- `ofxPoco` - Networking, utilities, and UUID generation (`Poco/UUID.h`).

**Infrastructure:**
- `ofxAudioFile` - Audio file loading.
- `ofxFft` - Fast Fourier Transform for audio analysis.
- `ofxLua` - Scripting engine integration.

## Configuration

**Environment:**
- `Project.xcconfig` - Xcode build settings and search paths.
- `config.make` - Makefile build settings.
- `addons.make` - List of required OpenFrameworks addons.

**Build:**
- `CMakeLists.txt` - CMake configuration.
- `Makefile` - Project Makefile.

## Platform Requirements

**Development:**
- macOS (detected Xcode and homebrew paths for dependencies).
- Linux (detected via Makefile/Poco usage).

**Production:**
- Desktop application (macOS/Linux).

---

*Stack analysis: 2026-01-28*
