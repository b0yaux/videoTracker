#pragma once

#include "data/Pattern.h"
#include "data/PatternChain.h"
#include "modules/Module.h"  // For TriggerEvent
#include "utils/Clock.h"
#include "ofEvents.h"
#include "ofJson.h"
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <shared_mutex>

// Forward declarations
struct TimeEvent;

/**
 * PatternPlaybackState: Runtime state for pattern playback (separate from Pattern data)
 * 
 * Aligns with Strudel/TidalCycles: patterns are stateless, runtime manages state separately.
 * This structure contains all mutable state needed for pattern playback, while Pattern
 * contains only immutable data (steps, column config, stepsPerBeat).
 * 
 * Extracted from TrackerSequencer's PlaybackState to enable PatternRuntime evaluation.
 */
struct PatternPlaybackState {
    int playbackStep = 0;           // Sequencer position in pattern (advances every step, wraps around)
    int currentPlayingStep = -1;     // Step currently playing audio/media (-1 if none, set when media triggers)
    bool isPlaying = false;         // Whether pattern is actively playing
    
    // Timing state for current playing step
    float stepStartTime = 0.0f;     // When current step started
    float stepEndTime = 0.0f;       // When current step should end (calculated from duration)
    
    // Audio-rate timing system
    double sampleAccumulator = 0.0; // Sample accumulator for step timing
    float lastBpm = 120.0f;         // Last known BPM for timing calculations
    
    // Pattern cycle counting for ratio conditional triggering
    int patternCycleCount = 0;      // Global cycle counter (increments when pattern wraps, resets on transport stop)
    
    // Optional: pattern chain reference
    std::shared_ptr<PatternChain> chain;
    
    void reset() {
        playbackStep = 0;
        currentPlayingStep = -1;
        isPlaying = false;
        stepStartTime = 0.0f;
        stepEndTime = 0.0f;
        sampleAccumulator = 0.0;
        patternCycleCount = 0;
    }
    
    void clearPlayingStep() {
        currentPlayingStep = -1;
        stepStartTime = 0.0f;
        stepEndTime = 0.0f;
    }
};

/**
 * PatternRuntime - Foundational system for pattern management and evaluation
 * 
 * Responsibilities:
 * - Owns all Pattern objects (first-class, stateless data structures)
 * - Manages PatternPlaybackState separately (runtime state per pattern)
 * - Evaluates patterns on clock ticks (sample-accurate timing)
 * - Generates unified TriggerEvent stream (all active patterns)
 * - Provides direct pattern manipulation API (add, update, remove, get)
 * - Handles pattern serialization/deserialization
 * - Manages pattern lifecycle (create, update, delete, play, stop, reset)
 * - Supports pattern chaining (PatternChain integration)
 * 
 * Architectural Alignment:
 * - Matches Strudel/TidalCycles: patterns are stateless data, runtime manages state separately
 * - Game Engine Pattern: System that processes entities (patterns), doesn't own modules
 * - Patterns are first-class (Engine-level), not owned by modules
 * 
 * Thread Safety:
 * - Uses shared_mutex for concurrent reads/writes
 * - Readers (evaluation, GUI display) use shared_lock (concurrent reads OK)
 * - Writers (edits) use unique_lock (exclusive access)
 */
class PatternRuntime {
public:
    PatternRuntime();
    ~PatternRuntime();
    
    // ═══════════════════════════════════════════════════════════
    // SETUP
    // ═══════════════════════════════════════════════════════════
    
    /**
     * Setup PatternRuntime with Clock reference
     * @param clock Pointer to Clock instance (for timing and transport)
     */
    void setup(Clock* clock);
    
    // ═══════════════════════════════════════════════════════════
    // PATTERN EVALUATION (Called from Engine::audioOut)
    // ═══════════════════════════════════════════════════════════
    
    /**
     * Evaluate all playing patterns (sample-accurate timing)
     * Called from Engine::audioOut() for sample-accurate pattern evaluation
     * @param buffer Audio buffer for timing calculations
     */
    void evaluatePatterns(ofSoundBuffer& buffer);
    
    // ═══════════════════════════════════════════════════════════
    // PATTERN MANAGEMENT (Data)
    // ═══════════════════════════════════════════════════════════
    
    /**
     * Add a pattern to the runtime
     * @param pattern Pattern object to add
     * @param name Optional pattern name (auto-generated if empty)
     * @return Pattern name (for reference)
     */
    std::string addPattern(const Pattern& pattern, const std::string& name = "");
    
    /**
     * Update an existing pattern
     * @param name Pattern name
     * @param pattern Updated pattern data
     */
    void updatePattern(const std::string& name, const Pattern& pattern);
    
    /**
     * Remove a pattern from the runtime (removes both pattern and playback state)
     * @param name Pattern name
     */
    void removePattern(const std::string& name);
    
    /**
     * Get a pattern by name (non-const, for editing)
     * @param name Pattern name
     * @return Pointer to pattern, or nullptr if not found
     */
    Pattern* getPattern(const std::string& name);
    
    /**
     * Get a pattern by name (const, for reading)
     * @param name Pattern name
     * @return Pointer to pattern, or nullptr if not found
     */
    const Pattern* getPattern(const std::string& name) const;
    
    /**
     * Get all pattern names
     * @return Vector of pattern names
     */
    std::vector<std::string> getPatternNames() const;
    
    /**
     * Check if a pattern exists
     * @param name Pattern name
     * @return true if pattern exists
     */
    bool patternExists(const std::string& name) const;
    
    // ═══════════════════════════════════════════════════════════
    // PLAYBACK STATE MANAGEMENT
    // ═══════════════════════════════════════════════════════════
    
    /**
     * Get playback state for a pattern (non-const, for modification)
     * @param name Pattern name
     * @return Pointer to playback state, or nullptr if not found
     */
    PatternPlaybackState* getPlaybackState(const std::string& name);
    
    /**
     * Get playback state for a pattern (const, for reading)
     * @param name Pattern name
     * @return Pointer to playback state, or nullptr if not found
     */
    const PatternPlaybackState* getPlaybackState(const std::string& name) const;
    
    // ═══════════════════════════════════════════════════════════
    // PLAYBACK CONTROL
    // ═══════════════════════════════════════════════════════════
    
    /**
     * Start playing a pattern
     * @param name Pattern name
     */
    void playPattern(const std::string& name);
    
    /**
     * Stop playing a pattern
     * @param name Pattern name
     */
    void stopPattern(const std::string& name);
    
    /**
     * Reset a pattern (stop and reset playback state)
     * @param name Pattern name
     */
    void resetPattern(const std::string& name);
    
    /**
     * Pause a pattern (stop but keep state)
     * @param name Pattern name
     */
    void pausePattern(const std::string& name);
    
    /**
     * Check if a pattern is playing
     * @param name Pattern name
     * @return true if pattern is playing
     */
    bool isPatternPlaying(const std::string& name) const;
    
    // ═══════════════════════════════════════════════════════════
    // CHAIN MANAGEMENT (First-Class Entities)
    // ═══════════════════════════════════════════════════════════
    
    /**
     * Create a new chain (auto-generates name if empty)
     * @param name Optional chain name (auto-generated if empty)
     * @return Chain name (for reference)
     */
    std::string addChain(const std::string& name = "");
    
    /**
     * Remove a chain
     * @param name Chain name
     */
    void removeChain(const std::string& name);
    
    /**
     * Get a chain by name
     * @param name Chain name
     * @return Pointer to chain, or nullptr if not found
     */
    PatternChain* getChain(const std::string& name);
    const PatternChain* getChain(const std::string& name) const;
    
    /**
     * Get all chain names
     * @return Vector of chain names
     */
    std::vector<std::string> getChainNames() const;
    
    /**
     * Check if a chain exists
     * @param name Chain name
     * @return true if chain exists
     */
    bool chainExists(const std::string& name) const;
    
    // Chain operations
    void chainAddPattern(const std::string& chainName, const std::string& patternName, int index = -1);
    void chainRemovePattern(const std::string& chainName, int index);
    void chainSetRepeat(const std::string& chainName, int index, int repeatCount);
    void chainSetEnabled(const std::string& chainName, bool enabled);
    void chainSetEntryDisabled(const std::string& chainName, int index, bool disabled);
    std::vector<std::string> chainGetPatterns(const std::string& chainName) const;
    void chainClear(const std::string& chainName);
    void chainReset(const std::string& chainName);
    
    // ═══════════════════════════════════════════════════════════
    // SEQUENCER BINDING (Sequencer-Agnostic)
    // ═══════════════════════════════════════════════════════════
    
    /**
     * Sequencer binding information
     */
    struct SequencerBinding {
        std::string patternName;    // Current active pattern (can be empty)
        std::string chainName;       // Progression chain (can be empty)
        bool chainEnabled = false;   // Whether chain is enabled
    };
    
    /**
     * Bind a sequencer to a pattern (current active)
     * @param sequencerName Sequencer module name
     * @param patternName Pattern name to bind to
     */
    void bindSequencerPattern(const std::string& sequencerName, const std::string& patternName);
    
    /**
     * Bind a sequencer to a chain (progression logic)
     * @param sequencerName Sequencer module name
     * @param chainName Chain name to bind to
     */
    void bindSequencerChain(const std::string& sequencerName, const std::string& chainName);
    
    /**
     * Unbind pattern from sequencer (keep chain)
     * @param sequencerName Sequencer module name
     */
    void unbindSequencerPattern(const std::string& sequencerName);
    
    /**
     * Unbind chain from sequencer (keep pattern)
     * @param sequencerName Sequencer module name
     */
    void unbindSequencerChain(const std::string& sequencerName);
    
    /**
     * Set chain enabled state for sequencer
     * @param sequencerName Sequencer module name
     * @param enabled Whether chain is enabled
     */
    void setSequencerChainEnabled(const std::string& sequencerName, bool enabled);
    
    /**
     * Get sequencer binding information
     * @param sequencerName Sequencer module name
     * @return Binding information
     */
    SequencerBinding getSequencerBinding(const std::string& sequencerName) const;
    
    /**
     * Get all sequencer names with bindings
     * @return Vector of sequencer names
     */
    std::vector<std::string> getSequencerNames() const;
    
    // ═══════════════════════════════════════════════════════════
    // LEGACY PATTERN CHAINING (Deprecated - use chain management instead)
    // ═══════════════════════════════════════════════════════════
    
    /**
     * Set pattern chain for a pattern (legacy - for backward compatibility)
     * @deprecated Use chain management API instead
     * @param name Pattern name
     * @param chain Pattern chain to associate
     */
    void setPatternChain(const std::string& name, std::shared_ptr<PatternChain> chain);
    
    /**
     * Get pattern chain for a pattern (legacy - for backward compatibility)
     * @deprecated Use chain management API instead
     * @param name Pattern name
     * @return Pattern chain, or nullptr if not set
     */
    std::shared_ptr<PatternChain> getPatternChain(const std::string& name);
    
    // ═══════════════════════════════════════════════════════════
    // EVENT OUTPUT
    // ═══════════════════════════════════════════════════════════
    
    /**
     * Unified trigger event stream (all active patterns)
     * Modules can subscribe to this for pattern events
     * Events include patternName metadata for routing
     */
    ofEvent<TriggerEvent> triggerEvent;
    
    /**
     * Pattern change notification event
     * Emits pattern name when pattern is modified
     */
    ofEvent<std::string> patternChangedEvent;
    
    /**
     * Pattern deletion notification event
     * Emits pattern name when pattern is deleted
     * TrackerSequencer instances should subscribe to this to handle cleanup
     */
    ofEvent<std::string> patternDeletedEvent;
    
    /**
     * Sequencer binding change notification event
     * Emits sequencer name when sequencer binding changes (pattern or chain)
     * TrackerSequencer instances should subscribe to this to sync immediately
     */
    ofEvent<std::string> sequencerBindingChangedEvent;
    
    /**
     * Notify that a pattern has changed (for GUI updates)
     * @param name Pattern name
     */
    void notifyPatternChanged(const std::string& name);
    
    // ═══════════════════════════════════════════════════════════
    // SERIALIZATION
    // ═══════════════════════════════════════════════════════════
    
    /**
     * Serialize patterns to JSON
     * @return JSON object containing all patterns
     */
    ofJson toJson() const;
    
    /**
     * Deserialize patterns from JSON
     * @param json JSON object containing patterns
     */
    void fromJson(const ofJson& json);
    
private:
    Clock* clock_;
    
    // Separate maps: pattern data (stateless) and playback state (runtime)
    // Aligns with Strudel/TidalCycles: patterns are pure data, runtime manages state
    std::map<std::string, Pattern> patterns_;                      // pattern name -> Pattern (data)
    std::map<std::string, PatternPlaybackState> playbackStates_;    // pattern name -> PlaybackState (runtime)
    
    // Chain management (first-class entities)
    std::map<std::string, std::shared_ptr<PatternChain>> chains_;  // chain name -> PatternChain
    
    // Sequencer bindings (sequencer-agnostic)
    std::map<std::string, SequencerBinding> sequencerBindings_;     // sequencerName -> binding
    
    // Concurrency safety for pattern edits
    mutable std::shared_mutex patternMutex_;  // Protects pattern data during edits
    
    // Pattern name generation
    std::string generatePatternName() const;
    int nextPatternId_ = 1;
    
    // Chain name generation
    std::string generateChainName();
    int nextChainId_ = 1;
    
    // Internal evaluation helpers
    bool evaluatePattern(const std::string& name, Pattern& pattern, PatternPlaybackState& state, ofSoundBuffer& buffer);  // Returns true if pattern finished
    void triggerStep(const std::string& name, Pattern& pattern, PatternPlaybackState& state, int step);
    bool advanceStep(Pattern& pattern, PatternPlaybackState& state);  // Returns true if pattern finished
    bool shouldTriggerStep(const Pattern& pattern, const PatternPlaybackState& state, int step) const;
    
    // Timing helpers
    float calculateStepDuration(const Pattern& pattern, int stepLength, float bpm) const;
    int calculateSamplesPerStep(const Pattern& pattern, float bpm, int bufferSize) const;
};

