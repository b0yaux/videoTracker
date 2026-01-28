# Coding Conventions

**Analysis Date:** 2026-01-28

## Naming Patterns

**Files:**
- PascalCase for headers and source files: `src/core/ModuleRegistry.cpp`, `src/gui/GUIManager.h`
- Subdirectories use lowercase: `src/core/`, `src/gui/`, `src/modules/`

**Functions:**
- camelCase for public and private methods: `setup()`, `update()`, `onProjectOpened()`, `handleKeyPress()`
- Function names in `ofApp` follow openFrameworks standard: `setup()`, `update()`, `draw()`, `exit()`, `keyPressed()`

**Variables:**
- camelCase for member variables: `clock`, `guiManager`, `projectManager`
- Trailing underscore sometimes used for internal/private state: `lastFrameTime_`, `frameCount_`, `enabled_`
- SCREAMING_SNAKE_CASE for static constants: `MASTER_AUDIO_OUT_NAME`, `FPS_LOG_INTERVAL`

**Types:**
- PascalCase for classes and structs: `ofApp`, `ModuleRegistry`, `TriggerEvent`, `ParameterDescriptor`
- SCREAMING_SNAKE_CASE for enum members (often within `enum class`): `PortType::AUDIO_IN`, `ModuleCapability::ACCEPTS_FILE_DROP`

## Code Style

**Formatting:**
- Indentation: 4 spaces
- Braces: same line for control flow (`if`, `while`, `for`), but separate line for function definitions in `.cpp` files
- Section separators: `//--------------------------------------------------------------` used to separate method implementations in `.cpp` files

**Linting:**
- No explicit `.clang-format` or `.eslintrc` found in the root
- Style appears consistent with standard openFrameworks and modern C++ practices

## Import Organization

**Order:**
1. Primary header: `#include "ModuleRegistry.h"`
2. Other local project headers: `#include "ModuleFactory.h"`, `#include "GUIManager.h"`
3. openFrameworks headers: `#include "ofLog.h"`, `#include "ofJson.h"`
4. Standard library headers: `#include <set>`, `#include <cctype>`

**Path Aliases:**
- No path aliases detected; relative paths from `src/` are used: `src/modules/AudioOutput.h`

## Error Handling

**Patterns:**
- Extensive use of `ofLogError` and `ofLogWarning` for diagnostic messages: `ofLogError("ModuleRegistry") << "Cannot register null module"`
- `try-catch` blocks used in critical update loops and cleanup: `src/ofApp.cpp` uses `try-catch` in `update()` and `exit()`
- Validation checks at the beginning of methods: `if (!module) return false;`

## Logging

**Framework:** `ofLog` (openFrameworks logging utility)

**Patterns:**
- Logging to different levels: `ofLogNotice`, `ofLogWarning`, `ofLogError`
- Performance monitoring logs: `[PERF] Slow module update '...'`
- Initialization checkpoints: `[CHECKPOINT] After session load, masterAudioOut...`

## Comments

**When to Comment:**
- Method headers in header files often have JSDoc-style descriptions
- Implementation details and edge cases in `.cpp` files
- Major phases in `setup()` are clearly marked: `// PHASE 1: Foundation Setup`

**JSDoc/TSDoc:**
- Doxygen-style comments used for class and method documentation: `/** ... */` with `@param` and `@return` tags

## Function Design

**Size:**
- methods in specialized classes (like `ModuleRegistry`) are generally small and focused
- `ofApp::setup()` and `ofApp::update()` are larger as they coordinate many systems

**Parameters:**
- Uses `const std::string&` for string parameters to avoid copies
- Uses `std::shared_ptr` and `std::weak_ptr` for resource management and avoiding circular dependencies
- Complex configurations passed as pointers or references to objects: `void setup(Clock* clock, ...)`

**Return Values:**
- Returns `bool` for success/failure in registration/setup methods
- Returns `std::shared_ptr<T>` for factory/registry lookups
- Returns `std::string` or `std::vector<T>` for discovery methods

## Module Design

**Exports:**
- Header-based declarations with implementation in `.cpp` files
- Use of `#pragma once` for include guards

**Barrel Files:**
- No explicit barrel files; modules are organized into functional directories: `src/core`, `src/gui`, `src/modules`, `src/utils`

---

*Convention analysis: 2026-01-28*
