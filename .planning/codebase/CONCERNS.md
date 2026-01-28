# Codebase Concerns

**Analysis Date:** 2026-01-28

## Tech Debt

**TrackerSequencerGUI State Management:**
- Issue: Immediate application of UI edits to sequencer state without a robust command pattern or transaction system.
- Files: `src/gui/TrackerSequencerGUI.cpp`
- Impact: Potential data races or inconsistent state if edits occur during high-speed playback or pattern switching.
- Fix approach: Implement a "pending edit" queue or command pattern as suggested by TODOs in lines 2083 and 2126.

**Module Parameter Access:**
- Issue: Lack of indexed parameter access for modulated parameters (e.g., modulating a specific step in a sequence).
- Files: `src/core/ParameterRouter.cpp`
- Impact: Prevents advanced modulation features where individual sequence steps or multi-index parameters need to be targets.
- Fix approach: Implement indexed access logic in `setIndexedParameterValue` and `getIndexedParameterValue`.

**Session Management Stubs:**
- Issue: "Save As", "Open Recent", and "New Session" functionalities are currently stubs.
- Files: `src/ofApp.cpp`
- Impact: Users cannot manage multiple session files or easily access previous work through the UI.
- Fix approach: Implement the logic in `SessionManager` and wire it to the `ofApp` menu callbacks.

**Large "God Object" Files:**
- Issue: Several files exceed 1500 lines, mixing logic, state, and UI handling.
- Files: `src/modules/MediaPool.cpp` (2652 lines), `src/gui/TrackerSequencerGUI.cpp` (2419 lines), `src/gui/MediaPoolGUI.cpp` (1971 lines).
- Impact: High cognitive load for maintenance, difficult to test in isolation, and prone to side effects.
- Fix approach: Refactor into smaller, focused components or helper classes (e.g., separate TrackerSequencer logic from its ImGui rendering).

## Known Bugs

**Pattern Column Reordering Sync:**
- Symptoms: Reordering columns in the UI may not be reflected in the underlying pattern chain logic.
- Files: `src/gui/TrackerSequencerGUI.cpp`
- Trigger: User drags columns in the tracker sequencer view.
- Workaround: Avoid reordering columns or manually verifying sequence integrity.

## Security Considerations

**Local File System Access:**
- Risk: Potential path traversal or unauthorized file access if user-provided paths (e.g., via session JSON or command line) are not properly sanitized.
- Files: `src/utils/AssetLibrary.cpp`, `src/gui/FileBrowser.cpp`
- Current mitigation: Basic path handling using `ofFilePath`.
- Recommendations: Implement strict path validation and sandboxing to the project/data directories.

## Performance Bottlenecks

**MediaPool State Locking:**
- Problem: O(N) iteration over players while holding a mutex lock.
- Files: `src/modules/MediaPool.cpp` (lines 954-965)
- Cause: `getActivePlayer()` iterates through all players to verify a pointer's validity while holding `stateMutex`.
- Improvement path: Maintain a `std::set` of valid player pointers or use `std::weak_ptr` to avoid O(N) validation under lock.

**Synchronous Parameter Lookup:**
- Problem: O(N) lookup for parameter descriptors by name in GUI code.
- Files: `src/gui/MediaPoolGUI.cpp` (line 461 TODO)
- Cause: Linear search through parameter lists.
- Improvement path: Use a `std::unordered_map<std::string, ParameterDescriptor>` for O(1) lookups.

**Blocking Audio Extraction:**
- Problem: Potential for UI blocking during certain audio extraction tasks.
- Files: `src/gui/AssetLibraryGUI.cpp` (line 967 TODO)
- Cause: Audio extraction not always queued through the background `MediaConverter`.
- Improvement path: Ensure all heavy media processing is routed through `MediaConverter`'s worker threads.

## Fragile Areas

**MediaPool Manual Pointer Management:**
- Files: `src/modules/MediaPool.cpp`
- Why fragile: Uses raw pointers for `activePlayer` which requires manual validation against a `std::vector` of unique pointers to prevent dangling references.
- Safe modification: Transition to `std::shared_ptr` or `std::weak_ptr` for tracking the active player.
- Test coverage: Gaps in thread-safety testing for rapid player switching.

## Scaling Limits

**Simultaneous Media Players:**
- Current capacity: Dependent on CPU/GPU and available file handles.
- Limit: Performance degrades as more `MediaPlayer` instances are added, especially for high-resolution video.
- Scaling path: Implement a "virtualization" or "culling" system for players that are not currently contributing to audio/video output.

## Missing Critical Features

**Project/Session File Dialogs:**
- Problem: "Open Project" and "New Project" actions are missing file picker dialogs.
- Blocks: Easy project creation and navigation for non-technical users.

**Audio Extraction Queue:**
- Problem: Lack of a unified queue for all audio asset processing.
- Blocks: Efficient batch processing of new assets added to the library.

## Test Coverage Gaps

**Entire Codebase:**
- What's not tested: No automated unit, integration, or E2E tests detected in the repository.
- Files: All `src/` files.
- Risk: Regressions are highly likely during refactoring or feature additions; logic errors in sequencer timing or parameter routing may go unnoticed.
- Priority: High

**Build/Environment Fragility:**
- Issue: Significant number of LSP errors related to missing headers and undeclared identifiers in core files.
- Files: `src/modules/Module.h`, `src/core/ModuleRegistry.h`, `src/gui/TrackerSequencerGUI.cpp`, etc.
- Impact: Difficult to use automated refactoring tools or maintain code quality without a stable build environment that properly resolves all openFrameworks and addon dependencies.
- Fix approach: Standardize the build system (e.g., via a consolidated CMake or Makefile) and ensure all include paths are consistently defined across different development environments.

---

*Concerns audit: 2026-01-28*
