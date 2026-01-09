#include "PatternRuntime.h"
#include "utils/Clock.h"
#include "ofLog.h"
#include "ofMath.h"
#include <algorithm>
#include <sstream>
#include <set>
#include <fstream>
#include <chrono>

PatternRuntime::PatternRuntime()
    : clock_(nullptr), nextPatternId_(1), nextChainId_(1) {
}

PatternRuntime::~PatternRuntime() {
    // Cleanup handled by member destructors
}

void PatternRuntime::setup(Clock* clock) {
    clock_ = clock;
    if (!clock_) {
        ofLogError("PatternRuntime") << "Clock is null in setup()";
    }
}

void PatternRuntime::evaluatePatterns(ofSoundBuffer& buffer) {
    if (!clock_) {
        return;
    }
    
    // Structure to hold pending pattern changes (sequencer -> new pattern)
    struct PendingPatternChange {
        std::string sequencerName;
        std::string oldPatternName;
        std::string newPatternName;
    };
    std::vector<PendingPatternChange> pendingChanges;
    
    // Thread-safe read access (shared_lock allows concurrent reads)
    {
        std::shared_lock<std::shared_mutex> lock(patternMutex_);
        
        // CRITICAL FIX: Build set of patterns that are bound to at least one sequencer
        // Only evaluate patterns that are bound to sequencers (prevents orphaned patterns from playing)
        std::set<std::string> boundPatternNames;
        for (const auto& [seqName, binding] : sequencerBindings_) {
            if (!binding.patternName.empty()) {
                boundPatternNames.insert(binding.patternName);
            }
        }
        
        // Evaluate only patterns that are:
        // 1. Bound to at least one sequencer, AND
        // 2. Currently playing
        for (auto& [name, pattern] : patterns_) {
            // Skip if pattern is not bound to any sequencer
            if (boundPatternNames.find(name) == boundPatternNames.end()) {
                continue;
            }
            
            auto stateIt = playbackStates_.find(name);
            if (stateIt == playbackStates_.end()) {
                continue;  // No playback state for this pattern
            }
            
            PatternPlaybackState& state = stateIt->second;
            
            // SIMPLIFIED: Only evaluate if pattern is playing AND transport is running AND bound to sequencer
            // Transport state drives playback - if transport stops, patterns pause automatically
            bool transportRunning = clock_ && clock_->isPlaying();
            if (state.isPlaying && transportRunning) {
                bool patternFinished = evaluatePattern(name, pattern, state, buffer);
                
                
                
                // If pattern finished, check for chain progression
                if (patternFinished) {
                    
                    
                    // Find sequencers bound to this pattern with chain enabled
                    for (const auto& [seqName, binding] : sequencerBindings_) {
                        
                        
                        if (binding.patternName == name && binding.chainEnabled && !binding.chainName.empty()) {
                            // Sequencer is bound to this pattern and has chain enabled
                            PatternChain* chain = getChain(binding.chainName);
                            
                            
                            
                            if (chain && chain->isEnabled()) {
                                // CRITICAL: Use peekNextPattern() to avoid modifying chain state while holding shared lock
                                std::string nextPatternName = chain->peekNextPattern();
                                
                                
                                
                                // Only queue pattern change if:
                                // 1. Next pattern name is valid and exists
                                // 2. Next pattern is DIFFERENT from current pattern (not a repeat)
                                // If nextPatternName == name, we should repeat the current pattern, not switch
                                if (!nextPatternName.empty() && patternExists(nextPatternName) && nextPatternName != name) {
                                    // Queue pattern change (apply after releasing shared lock)
                                    pendingChanges.push_back({seqName, name, nextPatternName});
                                    
                                    
                                } else if (nextPatternName == name) {
                                    // Pattern should repeat - increment chain repeat counter but don't change pattern
                                    // We need to increment currentRepeat in the chain, but we can't do it here (shared lock)
                                    // So we'll handle it when we apply changes (need to check if we should advance repeat)
                                    // Actually, we should increment the repeat counter now, but we need unique lock
                                    // For now, let's just log and handle repeat in the change application phase
                                    
                                    
                                    
                                    // Queue a "repeat" change (same pattern, but increment repeat counter)
                                    // We'll use a special marker to indicate this is a repeat, not a switch
                                    pendingChanges.push_back({seqName, name, name});  // Same pattern = repeat
                                }
                            }
                        }
                    }
                }
            }
        }
    }  // Release shared lock
    
    // Apply pending pattern changes (need unique lock for modifications)
    if (!pendingChanges.empty()) {
        
        
        std::unique_lock<std::shared_mutex> uniqueLock(patternMutex_);
        
        for (size_t i = 0; i < pendingChanges.size(); ++i) {
            const auto& change = pendingChanges[i];
            bool isLastChange = (i == pendingChanges.size() - 1);
            // Update sequencer binding
            auto it = sequencerBindings_.find(change.sequencerName);
            if (it != sequencerBindings_.end()) {
                bool isRepeat = (change.oldPatternName == change.newPatternName);
                std::string actualNewPattern = change.newPatternName;  // May be updated if repeat completes
                
                if (isRepeat) {
                    
                    
                    // Pattern should repeat - increment repeat counter and restart pattern
                    if (!it->second.chainName.empty()) {
                        PatternChain* chain = getChain(it->second.chainName);
                        
                        
                        
                        if (chain) {
                            // Call getNextPattern() - it will increment the counter and return the same pattern
                            // if we haven't completed all repeats, or advance if we have
                            std::string advancedPattern = chain->getNextPattern();
                            
                            
                            
                            // If getNextPattern() returned a different pattern, it means we completed all repeats
                            // and should advance (this shouldn't happen if peekNextPattern() was correct, but handle it)
                            if (advancedPattern != change.newPatternName && !advancedPattern.empty()) {
                                // Actually should advance - update binding
                                it->second.patternName = advancedPattern;
                                actualNewPattern = advancedPattern;  // Update for state handling below
                                isRepeat = false;  // Treat as pattern change, not repeat
                                
                                
                                
                                ofLogVerbose("PatternRuntime") << "Chain progression: sequencer '" << change.sequencerName 
                                                               << "' pattern '" << change.oldPatternName << "' -> '" << advancedPattern << "'";
                            } else {
                                // Still repeating - just reset pattern state (don't change binding)
                                
                                
                                
                                ofLogVerbose("PatternRuntime") << "Pattern repeat: sequencer '" << change.sequencerName 
                                                               << "' pattern '" << change.newPatternName << "' (repeat)";
                            }
                        }
                    } else {
                        
                    }
                } else {
                    // Pattern should advance - update binding and chain state
                    if (!it->second.chainName.empty()) {
                        PatternChain* chain = getChain(it->second.chainName);
                        if (chain) {
                            // Actually advance the chain state (modifies currentIndex, currentRepeat)
                            // This should return the same pattern we peeked earlier
                            std::string advancedPattern = chain->getNextPattern();
                            // Verify the advanced pattern matches what we peeked (should always match)
                            if (advancedPattern != change.newPatternName) {
                                ofLogWarning("PatternRuntime") << "Chain advancement mismatch: peeked '" 
                                                              << change.newPatternName << "', got '" << advancedPattern 
                                                              << "'. Using peeked value.";
                                // Use the peeked value to ensure consistency
                                // The chain state was already advanced, so we continue with the change
                            }
                        }
                    }
                    
                    it->second.patternName = change.newPatternName;
                    
                    
                    ofLogVerbose("PatternRuntime") << "Chain progression: sequencer '" << change.sequencerName 
                                                   << "' pattern '" << change.oldPatternName << "' -> '" << change.newPatternName << "'";
                }
                
                
                
                // CRITICAL: Ensure playback state exists for pattern
                // Access playbackStates_ directly (we already hold unique lock)
                auto nextStateIt = playbackStates_.find(actualNewPattern);
                if (nextStateIt == playbackStates_.end()) {
                    
                    
                    // Create playback state if it doesn't exist
                    playbackStates_[actualNewPattern] = PatternPlaybackState();
                    nextStateIt = playbackStates_.find(actualNewPattern);
                }
                
                
                
                PatternPlaybackState& nextState = nextStateIt->second;
                
                // SIMPLIFIED: Reset pattern state and start playing if transport is running
                // Transport state drives playback - patterns only play when transport is running
                bool transportRunning = clock_ && clock_->isPlaying();
                
                
                
                nextState.isPlaying = transportRunning;  // Only play if transport is running
                nextState.playbackStep = 0;
                nextState.patternCycleCount = 0;
                nextState.sampleAccumulator = 0;
                nextState.clearPlayingStep();
                
                
                
                if (transportRunning) {
                    ofLogVerbose("PatternRuntime") << "Started pattern '" << actualNewPattern 
                                                   << "' after chain progression (transport running)";
                } else {
                    ofLogVerbose("PatternRuntime") << "Pattern '" << actualNewPattern 
                                                   << "' ready but not playing (transport stopped)";
                }
                
                
                
                // Stop the old pattern only if we're actually switching (not repeating)
                // CRITICAL FIX: Only stop old pattern if it's no longer bound to any sequencer
                if (!isRepeat && change.oldPatternName != actualNewPattern) {
                    // Check if old pattern is still bound to other sequencers
                    bool stillBound = false;
                    for (const auto& [seqName, binding] : sequencerBindings_) {
                        if (seqName != change.sequencerName && binding.patternName == change.oldPatternName) {
                            stillBound = true;
                            break;
                        }
                    }
                    
                    // Only stop if pattern is no longer bound to any sequencer
                    if (!stillBound) {
                        auto oldStateIt = playbackStates_.find(change.oldPatternName);
                        if (oldStateIt != playbackStates_.end()) {
                            PatternPlaybackState& oldState = oldStateIt->second;
                            oldState.isPlaying = false;
                            oldState.clearPlayingStep();
                            
                            
                ofLogVerbose("PatternRuntime") << "Stopped unbound pattern '" << change.oldPatternName 
                                                           << "' after chain progression from sequencer '" << change.sequencerName << "'";
                        }
                    } else {
                        ofLogVerbose("PatternRuntime") << "Pattern '" << change.oldPatternName 
                                                       << "' still bound to other sequencers, not stopping";
                    }
                }
                
                
                
                // CRITICAL FIX: Notify sequencer of binding change so it can sync its state
                // This ensures the sequencer knows about the pattern change and can update its GUI
                // IMPORTANT: Unlock before notifying to avoid deadlock - event handlers may call PatternRuntime methods
                std::string nameCopy = change.sequencerName;
                uniqueLock.unlock();  // Release lock before notifying (event handlers may need to acquire locks)
                ofNotifyEvent(sequencerBindingChangedEvent, nameCopy);
                
                
                
                // Re-acquire lock for next iteration (if not the last change)
                if (!isLastChange) {
                    uniqueLock.lock();
                }
            }
        }
    }
}

std::string PatternRuntime::addPattern(const Pattern& pattern, const std::string& name) {
    std::string patternName = name.empty() ? generatePatternName() : name;
    
    // Check if name already exists
    if (patternExists(patternName)) {
        ofLogWarning("PatternRuntime") << "Pattern name already exists: " << patternName;
        return "";
    }
    
    // Thread-safe write access (unique_lock for exclusive access)
    std::unique_lock<std::shared_mutex> lock(patternMutex_);
    
    // Add pattern
    patterns_[patternName] = pattern;
    
    // Create playback state
    PatternPlaybackState state;
    playbackStates_[patternName] = state;
    
    ofLogNotice("PatternRuntime") << "Added pattern: " << patternName << " (" << pattern.getStepCount() << " steps)";
    
    return patternName;
}

void PatternRuntime::updatePattern(const std::string& name, const Pattern& pattern) {
    if (!patternExists(name)) {
        ofLogError("PatternRuntime") << "Pattern not found: " << name;
        return;
    }
    
    // Thread-safe write access
    std::unique_lock<std::shared_mutex> lock(patternMutex_);
    
    patterns_[name] = pattern;
    
    // Notify pattern changed
    lock.unlock();  // Unlock before notifying (avoid deadlock)
    notifyPatternChanged(name);
}

void PatternRuntime::removePattern(const std::string& name) {
    // Thread-safe check if pattern exists
    std::unique_lock<std::shared_mutex> lock(patternMutex_);
    if (patterns_.find(name) == patterns_.end()) {
        ofLogWarning("PatternRuntime") << "Pattern not found for removal: " << name;
        return;
    }
    
    // Fire deletion event BEFORE removing (so listeners can still access pattern if needed)
    // Unlock before notifying to avoid deadlock with listeners
    std::string nameCopy = name;
    lock.unlock();
    ofNotifyEvent(patternDeletedEvent, nameCopy);
    lock.lock();
    
    // Now remove the pattern
    patterns_.erase(name);
    playbackStates_.erase(name);
    
    ofLogNotice("PatternRuntime") << "Removed pattern: " << name;
}

Pattern* PatternRuntime::getPattern(const std::string& name) {
    // Thread-safe read access
    std::shared_lock<std::shared_mutex> lock(patternMutex_);
    
    auto it = patterns_.find(name);
    if (it == patterns_.end()) {
        return nullptr;
    }
    
    return &it->second;
}

const Pattern* PatternRuntime::getPattern(const std::string& name) const {
    // Thread-safe read access
    std::shared_lock<std::shared_mutex> lock(patternMutex_);
    
    auto it = patterns_.find(name);
    if (it == patterns_.end()) {
        return nullptr;
    }
    
    return &it->second;
}

std::vector<std::string> PatternRuntime::getPatternNames() const {
    // Thread-safe read access
    std::shared_lock<std::shared_mutex> lock(patternMutex_);
    
    std::vector<std::string> names;
    names.reserve(patterns_.size());
    
    for (const auto& [name, pattern] : patterns_) {
        names.push_back(name);
    }
    
    return names;
}

bool PatternRuntime::patternExists(const std::string& name) const {
    // Thread-safe read access
    std::shared_lock<std::shared_mutex> lock(patternMutex_);
    
    return patterns_.find(name) != patterns_.end();
}

int PatternRuntime::getPatternStepCount(const std::string& name) const {
    // Thread-safe read access - copy value while lock is held
    std::shared_lock<std::shared_mutex> lock(patternMutex_);
    
    auto it = patterns_.find(name);
    if (it == patterns_.end()) {
        return -1;
    }
    
    // Copy step count while lock is held (safe)
    return it->second.getStepCount();
}

PatternPlaybackState* PatternRuntime::getPlaybackState(const std::string& name) {
    // Thread-safe read access
    std::shared_lock<std::shared_mutex> lock(patternMutex_);
    
    auto it = playbackStates_.find(name);
    if (it == playbackStates_.end()) {
        return nullptr;
    }
    
    return &it->second;
}

const PatternPlaybackState* PatternRuntime::getPlaybackState(const std::string& name) const {
    // Thread-safe read access
    std::shared_lock<std::shared_mutex> lock(patternMutex_);
    
    auto it = playbackStates_.find(name);
    if (it == playbackStates_.end()) {
        return nullptr;
    }
    
    return &it->second;
}

void PatternRuntime::playPattern(const std::string& name) {
    
    
    if (!patternExists(name)) {
        ofLogError("PatternRuntime") << "Pattern not found: " << name;
        return;
    }
    
    
    
    PatternPlaybackState* state = getPlaybackState(name);
    
    
    
    if (!state) {
        return;
    }
    
    
    
    // Reset playback state when starting (always reset on play for clean start)
    state->reset();
    state->isPlaying = true;
    
    
    
    ofLogVerbose("PatternRuntime") << "Playing pattern: " << name << " (reset to step 0)";
}

void PatternRuntime::stopPattern(const std::string& name) {
    PatternPlaybackState* state = getPlaybackState(name);
    if (!state) {
        return;
    }
    
    state->isPlaying = false;
    state->clearPlayingStep();
    
    ofLogVerbose("PatternRuntime") << "Stopped pattern: " << name;
}

void PatternRuntime::resetPattern(const std::string& name) {
    PatternPlaybackState* state = getPlaybackState(name);
    if (!state) {
        return;
    }
    
    state->reset();
    
    ofLogVerbose("PatternRuntime") << "Reset pattern: " << name;
}

void PatternRuntime::pausePattern(const std::string& name) {
    PatternPlaybackState* state = getPlaybackState(name);
    if (!state) {
        return;
    }
    
    state->isPlaying = false;
    // Keep state (don't clear playing step)
    
    ofLogVerbose("PatternRuntime") << "Paused pattern: " << name;
}

bool PatternRuntime::isPatternPlaying(const std::string& name) const {
    const PatternPlaybackState* state = getPlaybackState(name);
    if (!state) {
        return false;
    }
    
    return state->isPlaying;
}

void PatternRuntime::setPatternChain(const std::string& name, std::shared_ptr<PatternChain> chain) {
    PatternPlaybackState* state = getPlaybackState(name);
    if (!state) {
        return;
    }
    
    state->chain = chain;
}

std::shared_ptr<PatternChain> PatternRuntime::getPatternChain(const std::string& name) {
    PatternPlaybackState* state = getPlaybackState(name);
    if (!state) {
        return nullptr;
    }
    
    return state->chain;
}

// ═══════════════════════════════════════════════════════════
// CHAIN MANAGEMENT (First-Class Entities)
// ═══════════════════════════════════════════════════════════

std::string PatternRuntime::addChain(const std::string& name) {
    std::string chainName = name.empty() ? generateChainName() : name;
    
    if (chains_.find(chainName) != chains_.end()) {
        ofLogWarning("PatternRuntime") << "Chain '" << chainName << "' already exists";
        return chainName;
    }
    
    chains_[chainName] = std::make_shared<PatternChain>();
    ofLogVerbose("PatternRuntime") << "Created chain: " << chainName;
    
    return chainName;
}

void PatternRuntime::removeChain(const std::string& name) {
    auto it = chains_.find(name);
    if (it == chains_.end()) {
        ofLogWarning("PatternRuntime") << "Chain '" << name << "' not found";
        return;
    }
    
    // Remove chain from sequencer bindings that reference it
    for (auto& [seqName, binding] : sequencerBindings_) {
        if (binding.chainName == name) {
            binding.chainName.clear();
            binding.chainEnabled = false;
        }
    }
    
    chains_.erase(it);
    ofLogVerbose("PatternRuntime") << "Removed chain: " << name;
}

PatternChain* PatternRuntime::getChain(const std::string& name) {
    auto it = chains_.find(name);
    if (it == chains_.end()) {
        return nullptr;
    }
    return it->second.get();
}

const PatternChain* PatternRuntime::getChain(const std::string& name) const {
    auto it = chains_.find(name);
    if (it == chains_.end()) {
        return nullptr;
    }
    return it->second.get();
}

std::vector<std::string> PatternRuntime::getChainNames() const {
    std::vector<std::string> names;
    for (const auto& [name, chain] : chains_) {
        names.push_back(name);
    }
    return names;
}

bool PatternRuntime::chainExists(const std::string& name) const {
    return chains_.find(name) != chains_.end();
}

void PatternRuntime::chainAddPattern(const std::string& chainName, const std::string& patternName, int index) {
    PatternChain* chain = getChain(chainName);
    if (!chain) {
        ofLogError("PatternRuntime") << "Chain '" << chainName << "' not found";
        return;
    }
    
    if (!patternExists(patternName)) {
        ofLogError("PatternRuntime") << "Pattern '" << patternName << "' not found";
        return;
    }
    
    if (index < 0) {
        // Add to end
        chain->addEntry(patternName);
    } else {
        // Insert at specific index
        chain->addEntry(patternName);
        // PatternChain doesn't have insert, so we'll need to work with what we have
        // For now, addEntry adds to end - we can enhance PatternChain later if needed
    }
}

void PatternRuntime::chainRemovePattern(const std::string& chainName, int index) {
    PatternChain* chain = getChain(chainName);
    if (!chain) {
        ofLogError("PatternRuntime") << "Chain '" << chainName << "' not found";
        return;
    }
    
    chain->removeEntry(index);
}

void PatternRuntime::chainSetEntry(const std::string& chainName, int index, const std::string& patternName) {
    PatternChain* chain = getChain(chainName);
    if (!chain) {
        ofLogError("PatternRuntime") << "Chain '" << chainName << "' not found";
        return;
    }
    
    if (!patternExists(patternName)) {
        ofLogError("PatternRuntime") << "Pattern '" << patternName << "' not found";
        return;
    }
    
    // CRITICAL: Direct entry update preserves chain state (enabled, currentIndex, currentRepeat)
    // This ensures patterns continue playing during chain edits
    chain->setEntry(index, patternName);
    ofLogVerbose("PatternRuntime") << "Set chain '" << chainName << "' entry " << index << " to pattern '" << patternName << "' (state preserved)";
}

void PatternRuntime::chainSetRepeat(const std::string& chainName, int index, int repeatCount) {
    PatternChain* chain = getChain(chainName);
    if (!chain) {
        ofLogError("PatternRuntime") << "Chain '" << chainName << "' not found";
        return;
    }
    
    chain->setRepeatCount(index, repeatCount);
}

void PatternRuntime::chainSetEnabled(const std::string& chainName, bool enabled) {
    PatternChain* chain = getChain(chainName);
    if (!chain) {
        ofLogError("PatternRuntime") << "Chain '" << chainName << "' not found";
        return;
    }
    
    chain->setEnabled(enabled);
}

void PatternRuntime::chainSetEntryDisabled(const std::string& chainName, int index, bool disabled) {
    PatternChain* chain = getChain(chainName);
    if (!chain) {
        ofLogError("PatternRuntime") << "Chain '" << chainName << "' not found";
        return;
    }
    
    chain->setEntryDisabled(index, disabled);
}

std::vector<std::string> PatternRuntime::chainGetPatterns(const std::string& chainName) const {
    const PatternChain* chain = getChain(chainName);
    if (!chain) {
        return {};
    }
    
    return chain->getChain();
}

void PatternRuntime::chainClear(const std::string& chainName) {
    PatternChain* chain = getChain(chainName);
    if (!chain) {
        ofLogError("PatternRuntime") << "Chain '" << chainName << "' not found";
        return;
    }
    
    chain->clear();
}

void PatternRuntime::chainReset(const std::string& chainName) {
    PatternChain* chain = getChain(chainName);
    if (!chain) {
        ofLogError("PatternRuntime") << "Chain '" << chainName << "' not found";
        return;
    }
    
    chain->reset();
}

// ═══════════════════════════════════════════════════════════
// SEQUENCER BINDING (Sequencer-Agnostic)
// ═══════════════════════════════════════════════════════════

void PatternRuntime::bindSequencerPattern(const std::string& sequencerName, const std::string& patternName) {
    if (!patternExists(patternName)) {
        ofLogError("PatternRuntime") << "Pattern '" << patternName << "' not found";
        return;
    }
    
    // CRITICAL FIX: Stop old pattern if it's no longer bound to any sequencer
    auto oldBindingIt = sequencerBindings_.find(sequencerName);
    if (oldBindingIt != sequencerBindings_.end() && !oldBindingIt->second.patternName.empty()) {
        std::string oldPatternName = oldBindingIt->second.patternName;
        
        // Check if old pattern is still bound to other sequencers
        bool stillBound = false;
        for (const auto& [seqName, binding] : sequencerBindings_) {
            if (seqName != sequencerName && binding.patternName == oldPatternName) {
                stillBound = true;
                break;
            }
        }
        
        // Stop old pattern if it's no longer bound to any sequencer
        if (!stillBound) {
            PatternPlaybackState* oldState = getPlaybackState(oldPatternName);
            if (oldState && oldState->isPlaying) {
                stopPattern(oldPatternName);
                ofLogVerbose("PatternRuntime") << "Stopped unbound pattern '" << oldPatternName << "'";
            }
        }
    }
    
    sequencerBindings_[sequencerName].patternName = patternName;
    ofLogVerbose("PatternRuntime") << "Bound sequencer '" << sequencerName << "' to pattern '" << patternName << "'";
    
    // Notify sequencer of binding change
    std::string nameCopy = sequencerName;
    ofNotifyEvent(sequencerBindingChangedEvent, nameCopy);
}

void PatternRuntime::bindSequencerChain(const std::string& sequencerName, const std::string& chainName) {
    if (!chainExists(chainName)) {
        ofLogError("PatternRuntime") << "Chain '" << chainName << "' not found";
        return;
    }
    
    
    
    sequencerBindings_[sequencerName].chainName = chainName;
    ofLogVerbose("PatternRuntime") << "Bound sequencer '" << sequencerName << "' to chain '" << chainName << "'";
    
    // Notify sequencer of binding change
    std::string nameCopy = sequencerName;
    ofNotifyEvent(sequencerBindingChangedEvent, nameCopy);
}

void PatternRuntime::unbindSequencerPattern(const std::string& sequencerName) {
    auto it = sequencerBindings_.find(sequencerName);
    if (it != sequencerBindings_.end()) {
        std::string oldPatternName = it->second.patternName;
        
        // Check if pattern is still bound to other sequencers
        bool stillBound = false;
        for (const auto& [seqName, binding] : sequencerBindings_) {
            if (seqName != sequencerName && binding.patternName == oldPatternName) {
                stillBound = true;
                break;
            }
        }
        
        // Stop pattern if it's no longer bound to any sequencer
        if (!stillBound && !oldPatternName.empty()) {
            PatternPlaybackState* state = getPlaybackState(oldPatternName);
            if (state && state->isPlaying) {
                stopPattern(oldPatternName);
                ofLogVerbose("PatternRuntime") << "Stopped unbound pattern '" << oldPatternName << "'";
            }
        }
        
        it->second.patternName.clear();
        ofLogVerbose("PatternRuntime") << "Unbound pattern from sequencer '" << sequencerName << "'";
        
        // Notify sequencer of binding change
        std::string nameCopy = sequencerName;
        ofNotifyEvent(sequencerBindingChangedEvent, nameCopy);
    }
}

void PatternRuntime::unbindSequencerChain(const std::string& sequencerName) {
    auto it = sequencerBindings_.find(sequencerName);
    if (it != sequencerBindings_.end()) {
        it->second.chainName.clear();
        it->second.chainEnabled = false;
        ofLogVerbose("PatternRuntime") << "Unbound chain from sequencer '" << sequencerName << "'";
        
        // Notify sequencer of binding change
        std::string nameCopy = sequencerName;
        ofNotifyEvent(sequencerBindingChangedEvent, nameCopy);
    }
}

void PatternRuntime::setSequencerChainEnabled(const std::string& sequencerName, bool enabled) {
    auto it = sequencerBindings_.find(sequencerName);
    if (it != sequencerBindings_.end()) {
        it->second.chainEnabled = enabled;
        ofLogVerbose("PatternRuntime") << "Set chain enabled=" << enabled << " for sequencer '" << sequencerName << "'";
        
        // Notify sequencer of binding change
        std::string nameCopy = sequencerName;
        ofNotifyEvent(sequencerBindingChangedEvent, nameCopy);
    }
}

PatternRuntime::SequencerBinding PatternRuntime::getSequencerBinding(const std::string& sequencerName) const {
    auto it = sequencerBindings_.find(sequencerName);
    if (it != sequencerBindings_.end()) {
        return it->second;
    }
    
    // Return empty binding if not found
    SequencerBinding empty;
    return empty;
}

std::vector<std::string> PatternRuntime::getSequencerNames() const {
    std::vector<std::string> names;
    for (const auto& [name, binding] : sequencerBindings_) {
        names.push_back(name);
    }
    return names;
}

void PatternRuntime::notifyPatternChanged(const std::string& name) {
    std::string nameCopy = name;  // Make non-const copy for ofEvent
    ofNotifyEvent(patternChangedEvent, nameCopy);
}

ofJson PatternRuntime::toJson() const {
    // Thread-safe read access
    std::shared_lock<std::shared_mutex> lock(patternMutex_);
    
    ofJson json;
    json["patterns"] = ofJson::object();
    
    for (const auto& [name, pattern] : patterns_) {
        json["patterns"][name] = pattern.toJson();
    }
    
    // Serialize chains
    if (!chains_.empty()) {
        json["chains"] = ofJson::object();
        for (const auto& [name, chain] : chains_) {
            ofJson chainJson;
            chain->toJson(chainJson);
            json["chains"][name] = chainJson;
        }
    }
    
    // Serialize sequencer bindings
    if (!sequencerBindings_.empty()) {
        json["sequencerBindings"] = ofJson::object();
        for (const auto& [seqName, binding] : sequencerBindings_) {
            
            ofJson bindingJson;
            bindingJson["patternName"] = binding.patternName;
            bindingJson["chainName"] = binding.chainName;
            bindingJson["chainEnabled"] = binding.chainEnabled;
            json["sequencerBindings"][seqName] = bindingJson;
        }
    }
    
    return json;
}

void PatternRuntime::fromJson(const ofJson& json) {
    // Thread-safe write access
    std::unique_lock<std::shared_mutex> lock(patternMutex_);
    
    patterns_.clear();
    playbackStates_.clear();
    chains_.clear();
    sequencerBindings_.clear();
    
    // Load patterns
    if (json.contains("patterns") && json["patterns"].is_object()) {
        for (const auto& [name, patternJson] : json["patterns"].items()) {
            Pattern pattern(16);  // Default step count
            pattern.fromJson(patternJson);
            
            patterns_[name] = pattern;
            
            // Create playback state
            PatternPlaybackState state;
            playbackStates_[name] = state;
        }
    }
    
    // Load chains
    if (json.contains("chains") && json["chains"].is_object()) {
        // Build pattern names directly (we already have exclusive access via unique_lock)
        std::vector<std::string> availablePatternNames;
        availablePatternNames.reserve(patterns_.size());
        for (const auto& [name, pattern] : patterns_) {
            availablePatternNames.push_back(name);
        }
        
        for (const auto& [name, chainJson] : json["chains"].items()) {
            auto chain = std::make_shared<PatternChain>();
            chain->fromJson(chainJson, availablePatternNames);
            chains_[name] = chain;
        }
    }
    
    // Load sequencer bindings
    if (json.contains("sequencerBindings") && json["sequencerBindings"].is_object()) {
        for (const auto& [seqName, bindingJson] : json["sequencerBindings"].items()) {
            SequencerBinding binding;
            if (bindingJson.contains("patternName")) {
                binding.patternName = bindingJson["patternName"].get<std::string>();
            }
            if (bindingJson.contains("chainName")) {
                binding.chainName = bindingJson["chainName"].get<std::string>();
            }
            if (bindingJson.contains("chainEnabled")) {
                binding.chainEnabled = bindingJson["chainEnabled"].get<bool>();
            }
            
            sequencerBindings_[seqName] = binding;
        }
    }
}

std::string PatternRuntime::generateChainName() {
    std::ostringstream oss;
    oss << "chain" << nextChainId_++;
    return oss.str();
}

std::string PatternRuntime::generatePatternName() const {
    // Generate simple pattern names: P0, P1, P2, etc.
    // Find the next available number by checking existing pattern names
    std::shared_lock<std::shared_mutex> lock(patternMutex_);
    
    int nextNumber = 0;
    bool found = true;
    
    // Find the next available number
    while (found) {
        std::stringstream ss;
        ss << "P" << nextNumber;
        std::string candidateName = ss.str();
        
        if (patterns_.find(candidateName) == patterns_.end()) {
            // This number is available
            break;
        }
        nextNumber++;
        
        // Safety limit to prevent infinite loop
        if (nextNumber > 10000) {
            ofLogWarning("PatternRuntime") << "Too many patterns, using fallback naming";
            std::stringstream fallback;
            fallback << "P" << nextPatternId_;
            return fallback.str();
        }
    }
    
    std::stringstream ss;
    ss << "P" << nextNumber;
    return ss.str();
}

bool PatternRuntime::evaluatePattern(const std::string& name, Pattern& pattern, PatternPlaybackState& state, ofSoundBuffer& buffer) {
    if (!clock_) {
        return false;
    }
    
    float bpm = clock_->getBPM();
    int bufferSize = buffer.getNumFrames();
    
    // Calculate samples per step (sample-accurate timing)
    int samplesPerStep = calculateSamplesPerStep(pattern, bpm, bufferSize);
    if (samplesPerStep <= 0) {
        return false;  // Invalid timing
    }
    
    // Update BPM in state (for timing calculations)
    state.lastBpm = bpm;
    
    // Accumulate samples
    state.sampleAccumulator += bufferSize;
    
    // Check if we should advance to next step
    bool patternFinishedThisEvaluation = false;
    while (state.sampleAccumulator >= samplesPerStep) {
        state.sampleAccumulator -= samplesPerStep;
        
        // Advance to next step
        bool patternFinished = advanceStep(pattern, state);
        if (patternFinished) {
            patternFinishedThisEvaluation = true;
        }
        
        // Check if we should trigger the new step
        int currentStep = state.playbackStep;
        const Step& stepData = pattern.getStep(currentStep);
        
        // Trigger if:
        // 1. No step is currently playing, OR
        // 2. New step has media (index >= 0) - this overrides current playing step
        if (state.currentPlayingStep < 0 || stepData.index >= 0) {
            triggerStep(name, pattern, state, currentStep);
        }
    }
    
    // Return whether pattern finished (for chain progression handling in evaluatePatterns)
    return patternFinishedThisEvaluation;
}

void PatternRuntime::triggerStep(const std::string& name, Pattern& pattern, PatternPlaybackState& state, int step) {
    if (step < 0 || step >= pattern.getStepCount()) {
        return;
    }
    
    const Step& stepData = pattern.getStep(step);
    float bpm = clock_ ? clock_->getBPM() : 120.0f;
    
    // Update playback step
    state.playbackStep = step;
    
    // Check ratio parameter (internal) - only trigger if current cycle matches ratio
    // Ratio is A:B format, where A is which cycle to trigger (1-based) and B is total cycles
    if (stepData.index >= 0) {  // Only check ratio if step has a trigger
        int ratioA = std::max(1, std::min(16, stepData.ratioA)); // Clamp to 1-16
        int ratioB = std::max(1, std::min(16, stepData.ratioB)); // Clamp to 1-16
        
        // Calculate current cycle position in ratio loop (1-based)
        int currentCycle = state.patternCycleCount + 1;
        int cycleInLoop = ((currentCycle - 1) % ratioB) + 1;  // 1-based position in loop
        
        if (cycleInLoop != ratioA) {
            // Ratio condition failed - don't trigger this step
            state.clearPlayingStep();
            return;
        }
    }
    
    // Check chance parameter (internal) - only trigger if random roll succeeds
    int chance = stepData.chance;
    chance = std::max(0, std::min(100, chance)); // Clamp to 0-100
    
    // Roll for chance (0-100)
    if (chance < 100) {
        int roll = (int)(ofRandom(0.0f, 100.0f));
        if (roll >= chance) {
            // Chance failed - don't trigger this step
            state.clearPlayingStep();
            return;
        }
    }
    
    // All trigger conditions passed - proceed with triggering
    // Calculate duration in seconds
    float stepLength = stepData.index >= 0 ? (float)stepData.length : 1.0f;
    float duration = calculateStepDuration(pattern, stepLength, bpm);
    
    // Set timing for current playing step
    if (stepData.index >= 0) {
        float currentTime = ofGetElapsedTimef();
        state.stepStartTime = currentTime;
        state.stepEndTime = currentTime + duration;
        state.currentPlayingStep = step;
    } else {
        // Empty step - clear playing state
        state.clearPlayingStep();
    }
    
    // Create TriggerEvent with pattern metadata
    TriggerEvent triggerEvt;
    triggerEvt.duration = duration;
    triggerEvt.step = step;
    triggerEvt.patternName = name;  // CRITICAL: Set pattern name for event routing
    
    // Map Step parameters to TriggerEvent parameters
    // "note" is the sequencer's parameter name (maps to stepData.index for MultiSampler)
    if (stepData.index >= 0) {
        triggerEvt.parameters["note"] = (float)stepData.index;
    } else {
        triggerEvt.parameters["note"] = -1.0f; // Rest/empty step
    }
    
    // Tracker-specific parameters that are NOT sent to external modules
    std::set<std::string> trackerOnlyParams = {"index", "length", "note", "chance", "ratio"};
    
    // Only send parameters that are in the current pattern's column configuration
    const auto& columnConfig = pattern.getColumnConfiguration();
    std::set<std::string> columnParamNames;
    for (const auto& col : columnConfig) {
        // Skip required columns (index, length) - they're handled separately
        if (col.parameterName != "index" && col.parameterName != "length") {
            columnParamNames.insert(col.parameterName);
        }
    }
    
    // Only send parameters that are both:
    // 1. In the step's parameterValues (explicitly set)
    // 2. In the current pattern's column configuration (actually displayed in grid)
    // 3. Not tracker-specific parameters
    for (const auto& paramPair : stepData.parameterValues) {
        const std::string& paramName = paramPair.first;
        float paramValue = paramPair.second;
        
        // Skip tracker-specific parameters
        if (trackerOnlyParams.find(paramName) != trackerOnlyParams.end()) {
            continue;
        }
        
        // Only send if parameter is in the current pattern's column configuration
        if (columnParamNames.find(paramName) != columnParamNames.end()) {
            triggerEvt.parameters[paramName] = paramValue;
        }
    }
    
    // Broadcast trigger event to all subscribers (unified stream from PatternRuntime)
    // NOTE: ofNotifyEvent is called from audio thread - this is acceptable for event dispatch
    ofNotifyEvent(triggerEvent, triggerEvt);
}

bool PatternRuntime::advanceStep(Pattern& pattern, PatternPlaybackState& state) {
    if (!state.isPlaying) {
        return false;
    }
    
    // Always advance playback step (for visual indicator)
    // Support backward reading when stepsPerBeat is negative
    int stepCount = pattern.getStepCount();
    int previousStep = state.playbackStep;
    float currentSPB = pattern.getStepsPerBeat();
    
    bool patternFinished;
    if (currentSPB < 0.0f) {
        // Backward reading: decrement step
        state.playbackStep = (state.playbackStep - 1 + stepCount) % stepCount;
        // Check if we wrapped around (pattern finished - went from 0 to stepCount-1)
        patternFinished = (state.playbackStep == stepCount - 1 && previousStep == 0);
    } else {
        // Forward reading: increment step
        state.playbackStep = (state.playbackStep + 1) % stepCount;
        // Check if we wrapped around (pattern finished - went from stepCount-1 to 0)
        patternFinished = (state.playbackStep == 0 && previousStep == stepCount - 1);
    }
    
    
    
    // Increment pattern cycle count when pattern wraps (one pattern repeat = one cycle)
    if (patternFinished) {
        state.patternCycleCount++;
    }
    
    return patternFinished;
}

bool PatternRuntime::shouldTriggerStep(const Pattern& pattern, const PatternPlaybackState& state, int step) const {
    if (step < 0 || step >= pattern.getStepCount()) {
        return false;
    }
    
    const Step& stepData = pattern.getStep(step);
    
    // Check if step has media
    if (stepData.index < 0) {
        return false;  // Empty step
    }
    
    // Check ratio parameter
    int ratioA = std::max(1, std::min(16, stepData.ratioA));
    int ratioB = std::max(1, std::min(16, stepData.ratioB));
    int currentCycle = state.patternCycleCount + 1;
    int cycleInLoop = ((currentCycle - 1) % ratioB) + 1;
    
    if (cycleInLoop != ratioA) {
        return false;  // Ratio condition failed
    }
    
    // Check chance parameter
    int chance = std::max(0, std::min(100, stepData.chance));
    if (chance < 100) {
        int roll = (int)(ofRandom(0.0f, 100.0f));
        if (roll >= chance) {
            return false;  // Chance failed
        }
    }
    
    return true;  // All conditions passed
}

float PatternRuntime::calculateStepDuration(const Pattern& pattern, int stepLength, float bpm) const {
    float currentSPB = pattern.getStepsPerBeat();
    // Use absolute value for duration calculation (negative SPB is for backward reading)
    return (stepLength * 60.0f) / (bpm * std::abs(currentSPB));
}

int PatternRuntime::calculateSamplesPerStep(const Pattern& pattern, float bpm, int bufferSize) const {
    if (!clock_) {
        return 0;
    }
    
    int sampleRate = clock_->getSampleRate();
    if (sampleRate <= 0) {
        return 0;
    }
    
    float currentSPB = pattern.getStepsPerBeat();
    if (currentSPB == 0.0f) {
        return 0;  // Invalid stepsPerBeat
    }
    
    // Calculate samples per step (sample-accurate timing)
    // Use absolute value for calculation (negative SPB is for backward reading)
    float secondsPerStep = 60.0f / (bpm * std::abs(currentSPB));
    int samplesPerStep = (int)(secondsPerStep * sampleRate);
    
    // Ensure minimum of 1 sample per step (avoid division by zero)
    return std::max(1, samplesPerStep);
}

