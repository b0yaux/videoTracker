# Phase 4: Make initialize() Idempotent - Research

**Researched:** 2026-01-21
**Domain:** C++ module initialization patterns, event subscription lifecycle
**Confidence:** HIGH

## Summary

This research investigates how to make module `initialize()` methods idempotent to prevent duplicate event subscriptions. The problem occurs because `initialize()` is called both during initial setup AND after session restore, leading to double-registration of Clock and PatternRuntime listeners.

The codebase already has a partial solution in place: `TrackerSequencer` uses a `listenersRegistered_` flag to prevent duplicate Clock subscriptions. However, this pattern is not applied to PatternRuntime subscriptions, nor is it used consistently across other modules.

**Primary recommendation:** Add `isInitialized_` flag to Module base class and use early-return pattern in all module `initialize()` methods.

## Standard Stack

### Current Architecture

| Component | Pattern | Status |
|-----------|---------|--------|
| Module base class | Virtual `initialize()` method, no idempotency | Needs update |
| TrackerSequencer | `listenersRegistered_` flag for Clock | Partial - missing PatternRuntime guards |
| MultiSampler | `isSetup` flag in `setup()` only | Needs `isInitialized_` in `initialize()` |
| AudioOutput/VideoOutput | No idempotency guards | Relies on no subscriptions |
| Oscilloscope/Spectrogram | No idempotency guards | Relies on no subscriptions |

### Existing Idempotency Patterns

The codebase already uses idempotency patterns in several places:

1. **TrackerSequencer::listenersRegistered_** (line 347 in TrackerSequencer.h)
   - Guards Clock subscriptions: `ofAddListener`, `addAudioListener`, `addTransportListener`
   - NOT guarding PatternRuntime subscriptions

2. **MultiSampler::isSetup** (MultiSampler.cpp:568)
   - Guards `setup()` method only, not `initialize()`
   - Pattern: `if (isSetup) return;`

3. **Engine::isSetup_** (Engine.cpp:45)
   - Guards Engine setup

## Architecture Patterns

### Recommended Pattern: Single isInitialized_ Flag

```cpp
// In Module.h (protected section)
protected:
    bool isInitialized_ = false;  // Flag to prevent duplicate initialization

// In derived class initialize() method
void TrackerSequencer::initialize(Clock* clock, ModuleRegistry* registry, 
                                   ConnectionManager* connectionManager,
                                   ParameterRouter* parameterRouter, 
                                   PatternRuntime* patternRuntime, 
                                   bool isRestored) {
    if (isInitialized_) return;  // Idempotency guard
    
    // ... all initialization logic ...
    
    isInitialized_ = true;  // Mark as initialized at the end
}
```

### Alternative: Per-Subscription Guards (Current TrackerSequencer Approach)

```cpp
// Separate flags for different subscription types
bool listenersRegistered_ = false;   // Clock subscriptions
bool patternListenersRegistered_ = false;  // PatternRuntime subscriptions

// In initialize()
if (!listenersRegistered_) {
    ofAddListener(clock->timeEvent, this, &TrackerSequencer::onTimeEvent);
    clock->addAudioListener([this](ofSoundBuffer& buffer) { ... });
    clock->addTransportListener([this](bool isPlaying) { ... });
    listenersRegistered_ = true;
}

if (!patternListenersRegistered_ && patternRuntime_) {
    ofAddListener(patternRuntime_->triggerEvent, this, &TrackerSequencer::onPatternRuntimeTrigger);
    ofAddListener(patternRuntime_->patternDeletedEvent, this, &TrackerSequencer::onPatternDeleted);
    ofAddListener(patternRuntime_->sequencerBindingChangedEvent, this, &TrackerSequencer::onSequencerBindingChanged);
    patternListenersRegistered_ = true;
}
```

### Recommended: Combined Approach

Use `isInitialized_` in base class for global guard, keep `listenersRegistered_` for granular control:

```cpp
// Module.h
protected:
    bool isInitialized_ = false;

// TrackerSequencer
void TrackerSequencer::initialize(...) {
    if (isInitialized_) return;  // Early exit if already done
    
    // Clock subscriptions (existing guard works)
    if (clock && !listenersRegistered_) {
        // ... subscriptions ...
        listenersRegistered_ = true;
    }
    
    // PatternRuntime subscriptions (NEW guard needed)
    if (patternRuntime_) {
        ofAddListener(patternRuntime_->triggerEvent, ...);  // Now safe due to isInitialized_
        ofAddListener(patternRuntime_->patternDeletedEvent, ...);
        ofAddListener(patternRuntime_->sequencerBindingChangedEvent, ...);
    }
    
    // ... rest of initialization ...
    
    isInitialized_ = true;
}
```

## Modules Requiring Updates

### Detailed Analysis

| Module | Has initialize()? | Subscribes to Events? | Needs Update? | Files |
|--------|-------------------|----------------------|---------------|-------|
| **TrackerSequencer** | Yes | Clock (3), PatternRuntime (3) | **YES - CRITICAL** | TrackerSequencer.h/cpp |
| **MultiSampler** | Yes | None in initialize() | LOW - add guard for consistency | MultiSampler.h/cpp |
| **Module (base)** | Yes (virtual, empty default) | N/A | **YES - add isInitialized_ flag** | Module.h |
| AudioOutput | No (uses default) | No | No | N/A |
| VideoOutput | No (uses default) | No | No | N/A |
| AudioMixer | No (uses default) | No | No | N/A |
| VideoMixer | No (uses default) | No | No | N/A |
| Oscilloscope | No (uses default) | No | No | N/A |
| Spectrogram | No (uses default) | No | No | N/A |

### TrackerSequencer Subscriptions (Current Code Analysis)

**Clock Subscriptions** (already guarded by `listenersRegistered_`):
- Line 50: `ofAddListener(clock->timeEvent, this, &TrackerSequencer::onTimeEvent);`
- Line 53-55: `clock->addAudioListener([this](ofSoundBuffer& buffer) { ... });`
- Line 58-60: `clock->addTransportListener([this](bool isPlaying) { ... });`

**PatternRuntime Subscriptions** (NOT guarded - the bug):
- Line 184: `ofAddListener(patternRuntime_->triggerEvent, this, &TrackerSequencer::onPatternRuntimeTrigger);`
- Line 186: `ofAddListener(patternRuntime_->patternDeletedEvent, this, &TrackerSequencer::onPatternDeleted);`
- Line 188: `ofAddListener(patternRuntime_->sequencerBindingChangedEvent, this, &TrackerSequencer::onSequencerBindingChanged);`

**Additional PatternRuntime subscriptions in other methods** (also problematic):
- Line 835: `ofAddListener(patternRuntime_->triggerEvent, ...)` - in update() sync logic
- Line 2579: `ofAddListener(patternRuntime_->triggerEvent, ...)` - in bindToPattern()
- Line 2652: `ofAddListener(patternRuntime_->triggerEvent, ...)` - in onSequencerBindingChanged()

## Common Pitfalls

### Pitfall 1: Not Guarding All Subscription Paths

**What goes wrong:** TrackerSequencer subscribes to PatternRuntime events in multiple places:
- `initialize()` (line 184)
- `bindToPattern()` (line 2579)
- `onSequencerBindingChanged()` (line 2652)
- Update sync logic (line 835)

**Why it happens:** Pattern changes trigger re-subscription without unsubscribing first.

**How to avoid:** Always unsubscribe before subscribing, or use a flag to track subscription state.

**Current mitigation:** Lines 829, 2565, 2647 have `ofRemoveListener` calls before new subscriptions, but the pattern is inconsistent.

### Pitfall 2: Destructor Not Unsubscribing All Listeners

**What goes wrong:** Destructor at line 30-36 only unsubscribes from `triggerEvent` and `patternDeletedEvent`, but NOT from `sequencerBindingChangedEvent`.

**Why it happens:** Incomplete cleanup when listener list grows.

**How to avoid:** Destructor should unsubscribe from ALL listeners.

```cpp
TrackerSequencer::~TrackerSequencer() {
    if (patternRuntime_) {
        ofRemoveListener(patternRuntime_->triggerEvent, this, &TrackerSequencer::onPatternRuntimeTrigger);
        ofRemoveListener(patternRuntime_->patternDeletedEvent, this, &TrackerSequencer::onPatternDeleted);
        ofRemoveListener(patternRuntime_->sequencerBindingChangedEvent, this, &TrackerSequencer::onSequencerBindingChanged);  // MISSING!
    }
}
```

### Pitfall 3: isRestored Flag Not Preventing Re-initialization

**What goes wrong:** `initialize()` receives `isRestored` flag but doesn't use it to prevent duplicate work.

**Why it happens:** The flag is for conditional logic, not idempotency.

**How to avoid:** Use `isInitialized_` flag regardless of `isRestored` value.

## Code Examples

### Example 1: Adding isInitialized_ to Module.h

```cpp
// Source: Module.h (line ~900, protected section)
protected:
    // Parameter change callback for synchronization systems
    std::function<void(const std::string&, float)> parameterChangeCallback;
    std::atomic<bool> enabled_{true};  // Module enabled state (atomic, lock-free)
    
    // ADD: Idempotency flag for initialize()
    bool isInitialized_ = false;  // Prevents duplicate initialization
    
    // Instance name (set by ModuleRegistry during initialization)
    std::string instanceName_;
```

### Example 2: TrackerSequencer::initialize() with Guard

```cpp
// Source: TrackerSequencer.cpp (line 71)
void TrackerSequencer::initialize(Clock* clock, ModuleRegistry* registry, 
                                  ConnectionManager* connectionManager, 
                                  ParameterRouter* parameterRouter, 
                                  PatternRuntime* patternRuntime, 
                                  bool isRestored) {
    // Idempotency guard - prevent duplicate subscriptions
    if (isInitialized_) return;
    
    // ... existing initialization logic (lines 73-247) ...
    
    // Mark as initialized at the end
    isInitialized_ = true;
}
```

### Example 3: MultiSampler::initialize() with Guard

```cpp
// Source: MultiSampler.cpp (line 577)
void MultiSampler::initialize(Clock* clock, ModuleRegistry* registry, 
                              ConnectionManager* connectionManager,
                              ParameterRouter* parameterRouter, 
                              PatternRuntime* patternRuntime, 
                              bool isRestored) {
    // Idempotency guard - prevent duplicate initialization
    if (isInitialized_) return;
    
    // ... existing initialization logic (lines 579-605) ...
    
    // Mark as initialized at the end
    isInitialized_ = true;
}
```

### Example 4: TrackerSequencer Destructor Fix

```cpp
// Source: TrackerSequencer.cpp (line 30)
TrackerSequencer::~TrackerSequencer() {
    // Unsubscribe from ALL PatternRuntime events
    if (patternRuntime_) {
        ofRemoveListener(patternRuntime_->triggerEvent, this, &TrackerSequencer::onPatternRuntimeTrigger);
        ofRemoveListener(patternRuntime_->patternDeletedEvent, this, &TrackerSequencer::onPatternDeleted);
        ofRemoveListener(patternRuntime_->sequencerBindingChangedEvent, this, &TrackerSequencer::onSequencerBindingChanged);
    }
    
    // Note: Clock listener removal should happen here too, but Clock API may not support it
    // Consider adding clock_ member and using ofRemoveListener for timeEvent
}
```

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Event subscription tracking | Custom subscription registry | Simple boolean flag | Flags are sufficient for "called once" semantics |
| Listener removal on destroy | Manual tracking of each listener | Store patternRuntime_ and clean up in destructor | Already done for some listeners |

## Open Questions

### Question 1: Clock Listener Removal

**What we know:** TrackerSequencer subscribes to `clock->timeEvent` via `ofAddListener`, but the destructor doesn't unsubscribe from it.

**What's unclear:** Does Clock outlive TrackerSequencer? If so, dangling listener could cause crashes.

**Recommendation:** Verify Clock lifetime. If Clock can outlive modules, add `ofRemoveListener(clock->timeEvent, ...)` to destructor.

### Question 2: listenersRegistered_ vs isInitialized_

**What we know:** TrackerSequencer uses `listenersRegistered_` specifically for Clock subscriptions.

**What's unclear:** Should we keep both flags or consolidate to just `isInitialized_`?

**Recommendation:** Keep `listenersRegistered_` for backward compatibility and add `isInitialized_` to Module base class for a unified pattern.

## Files to Modify

| File | Change |
|------|--------|
| `src/modules/Module.h` | Add `bool isInitialized_ = false;` to protected section (~line 901) |
| `src/modules/TrackerSequencer.cpp` | Add `if (isInitialized_) return;` at line 72, add `isInitialized_ = true;` at end of initialize() |
| `src/modules/TrackerSequencer.cpp` | Fix destructor to unsubscribe from `sequencerBindingChangedEvent` |
| `src/modules/MultiSampler.cpp` | Add `if (isInitialized_) return;` at line 578, add `isInitialized_ = true;` at end |

## Sources

### Primary (HIGH confidence)
- `src/modules/Module.h` - Base class analysis (complete file review)
- `src/modules/TrackerSequencer.h` - `listenersRegistered_` flag at line 347
- `src/modules/TrackerSequencer.cpp` - initialize() and subscription code (lines 71-248)
- `src/modules/MultiSampler.cpp` - initialize() implementation (lines 577-606)

### Secondary (MEDIUM confidence)
- `src/core/Engine.cpp` - Call sites for initialize() (lines 164-176, 330-342)
- `src/core/SessionManager.cpp` - Call sites for initialize() (lines 413, 1173, 2349)

## Metadata

**Confidence breakdown:**
- Module analysis: HIGH - Direct code review of all module files
- Subscription patterns: HIGH - Grepped for all ofAddListener/subscribe calls
- Fix approach: HIGH - Based on existing pattern (listenersRegistered_)

**Research date:** 2026-01-21
**Valid until:** N/A (internal codebase analysis, patterns are stable)
