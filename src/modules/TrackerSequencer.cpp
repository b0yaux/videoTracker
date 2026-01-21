#include "TrackerSequencer.h"
#include "gui/TrackerSequencerGUI.h"  // For GUIState definition
#include "utils/Clock.h"
#include "Module.h"
#include "core/ModuleRegistry.h"
#include "core/ConnectionManager.h"
#include "core/ParameterRouter.h"
#include "core/PatternRuntime.h"
#include "core/ModuleFactory.h"
#include "ofLog.h"
#include "ofJson.h"
#include <imgui.h>
#include <cmath>  // For std::round
#include <limits>  // For std::numeric_limits
#include <set>
#include <atomic>  // For diagnostic event counter
#include <fstream>
#include <chrono>

// TrackerSequencer implementation
//--------------------------------------------------------------
TrackerSequencer::TrackerSequencer() 
    : clock(nullptr), stepsPerBeat(4.0f), gatingEnabled(true),
      draggingStep(-1), draggingColumn(-1), lastDragValue(0.0f), dragStartY(0.0f), dragStartX(0.0f),
      connectionManager_(nullptr) {
    // Phase 3: Patterns are created in PatternRuntime during initialize()
    // PlaybackState is initialized with default values in struct definition
}

TrackerSequencer::~TrackerSequencer() {
    // Remove transport listener to prevent dangling pointer crashes
    if (transportListenerId != 0 && clock) {
        clock->removeTransportListener(transportListenerId);
        transportListenerId = 0;
    }
    
    // Unsubscribe from PatternRuntime events
    if (patternRuntime_) {
        ofRemoveListener(patternRuntime_->triggerEvent, this, &TrackerSequencer::onPatternRuntimeTrigger);
        ofRemoveListener(patternRuntime_->patternDeletedEvent, this, &TrackerSequencer::onPatternDeleted);
    }
}

void TrackerSequencer::setup(Clock* clockRef) {
    clock = clockRef;
    playbackState.playbackStep = 0; // Initialize playback step
    // Note: GUI state initialization removed - managed by TrackerSequencerGUI
    
    // Phase 3: Patterns are in PatternRuntime, no initialization needed here
    // Pattern creation happens in initialize() when PatternRuntime is available
    
    // Register listeners ONLY if not already registered (prevents double-triggering)
    // This flag prevents issues when setup() or initialize() is called multiple times
    if (clock && !listenersRegistered_) {
        // Connect to Clock's time events for beat synchronization
        ofAddListener(clock->timeEvent, this, &TrackerSequencer::onTimeEvent);
        
        // Register audio listener for sample-accurate step timing
        clock->addAudioListener([this](ofSoundBuffer& buffer) {
            this->processAudioBuffer(buffer);
        });
        
        // Subscribe to Clock transport changes - store ID for cleanup
        transportListenerId = clock->addTransportListener([this](bool isPlaying) {
            this->onClockTransportChanged(isPlaying);
        });
        
        listenersRegistered_ = true;
    }
    
    ofLogNotice("TrackerSequencer") << "Setup complete with " << getCurrentPattern().getStepCount() << " steps";
}


//--------------------------------------------------------------
//--------------------------------------------------------------
void TrackerSequencer::initialize(Clock* clock, ModuleRegistry* registry, ConnectionManager* connectionManager, 
                                  ParameterRouter* parameterRouter, PatternRuntime* patternRuntime, bool isRestored) {
    // Unified initialization - combines postCreateSetup and configureSelf
    
    // 1. Basic setup (from postCreateSetup)
    if (clock) {
        if (isRestored) {
            // For restored modules, only set up clock connection without resetting pattern stepCount
            // Pattern stepCounts were already loaded from JSON in fromJson()
            this->clock = clock;
            playbackState.playbackStep = 0;
            
            // Register listeners ONLY if not already registered (prevents double-triggering)
            if (!listenersRegistered_) {
                // Connect to Clock's time events for beat synchronization
                ofAddListener(clock->timeEvent, this, &TrackerSequencer::onTimeEvent);
                
                // Register audio listener for sample-accurate step timing
                clock->addAudioListener([this](ofSoundBuffer& buffer) {
                    this->processAudioBuffer(buffer);
                });
                
                // Subscribe to Clock transport changes - store ID for cleanup
                transportListenerId = clock->addTransportListener([this](bool isPlaying) {
                    this->onClockTransportChanged(isPlaying);
                });
                
                listenersRegistered_ = true;
            }
            
            // Phase 3: Patterns are in PatternRuntime, no initialization needed here
        } else {
            // For new modules, setup clock connection
            // setup() will check listenersRegistered_ to avoid double-registration
            setup(clock);
        }
    }
    
    // Store PatternRuntime reference FIRST (Phase 3: Runtime-only architecture)
    // This must be set even if other dependencies are missing, as it's critical for pattern access
    patternRuntime_ = patternRuntime;
    
    if (!patternRuntime_) {
        ofLogWarning("TrackerSequencer") << "PatternRuntime not provided during initialization for '" << getInstanceName() 
                                          << "'. Pattern access will be unavailable until PatternRuntime is set.";
    }
    
    // 2. Self-configuration (from configureSelf) - only if we have all required dependencies
    if (!registry || !connectionManager || !parameterRouter) {
        // Even if other dependencies are missing, ensure we have a bound pattern if PatternRuntime is available
        // BUT: Don't auto-create for restored modules - let migration handle it
        if (patternRuntime_ && boundPatternName_.empty() && !isRestored) {
            Pattern defaultPattern(16);
            // Use simple pattern naming (P0, P1, P2, etc.) - let PatternRuntime generate the name
            std::string patternName = patternRuntime_->addPattern(defaultPattern, "");
            if (!patternName.empty()) {
                boundPatternName_ = patternName;
                ofLogNotice("TrackerSequencer") << "Auto-created pattern '" << patternName << "' in PatternRuntime";
            }
        }
        return;
    }
    
    // Auto-create/bind pattern in Runtime if needed
    if (patternRuntime_) {
        if (boundPatternName_.empty()) {
            // CRITICAL: Don't auto-create patterns for restored modules - let migration handle it
            // Migration will create patterns from legacy format or bind to existing patterns
            if (!isRestored) {
                // No pattern bound - create default pattern (only for NEW instances, not restored)
                Pattern defaultPattern(16);
                // Use simple pattern naming (P0, P1, P2, etc.) - let PatternRuntime generate the name
            std::string patternName = patternRuntime_->addPattern(defaultPattern, "");
                if (!patternName.empty()) {
                    boundPatternName_ = patternName;
                    ofLogNotice("TrackerSequencer") << "Auto-created pattern '" << patternName << "' in PatternRuntime";
                } else {
                    ofLogError("TrackerSequencer") << "Failed to create pattern in Runtime";
                }
            } else {
                // For restored modules, wait for migration to handle pattern creation/binding
                ofLogVerbose("TrackerSequencer") << "Skipping auto-creation for restored module '" << getName() 
                                                  << "' - migration will handle pattern binding";
            }
        } else if (!patternRuntime_->patternExists(boundPatternName_)) {
            // Bound pattern doesn't exist - try to bind to first available pattern or create default
            // But only if NOT restored (for restored, let migration handle it)
            if (!isRestored) {
                auto patternNames = patternRuntime_->getPatternNames();
                if (!patternNames.empty()) {
                    ofLogWarning("TrackerSequencer") << "Bound pattern '" << boundPatternName_ 
                                                      << "' not found, binding to first available pattern";
                    boundPatternName_ = patternNames[0];
                } else {
                    // No patterns exist - create default pattern
                    Pattern defaultPattern(16);
                    // Use simple pattern naming (P0, P1, P2, etc.) - let PatternRuntime generate the name
                    std::string actualName = patternRuntime_->addPattern(defaultPattern, "");
                    if (!actualName.empty()) {
                        boundPatternName_ = actualName;
                        ofLogNotice("TrackerSequencer") << "Created default pattern '" << actualName << "'";
                    }
                }
            } else {
                // For restored modules, migration will fix broken bindings
                ofLogVerbose("TrackerSequencer") << "Bound pattern '" << boundPatternName_ 
                                                  << "' not found for restored module - migration will fix";
            }
        }
    }
    
    // Subscribe to PatternRuntime events for forwarding
    if (patternRuntime_) {
        ofAddListener(patternRuntime_->triggerEvent, this, &TrackerSequencer::onPatternRuntimeTrigger);
        // Subscribe to pattern deletion events for cleanup
        ofAddListener(patternRuntime_->patternDeletedEvent, this, &TrackerSequencer::onPatternDeleted);
        // Subscribe to sequencer binding change events for immediate sync
        ofAddListener(patternRuntime_->sequencerBindingChangedEvent, this, &TrackerSequencer::onSequencerBindingChanged);
    }
    
    // Store ConnectionManager reference for querying connections
    connectionManager_ = connectionManager;
    
    // 2.1. Index range is now queried directly via getIndexRange() when needed
    // No setup required - ConnectionManager reference is stored for queries
    
    // 2.2. Set up parameter change callback for parameter routing
    auto metadata = getMetadata();
    setParameterChangeCallback([parameterRouter, this](const std::string& paramName, float value) {
        // Check if this parameter is in module's metadata
        auto modMetadata = getMetadata();
        for (const auto& param : modMetadata.parameterNames) {
            if (param == paramName) {
                parameterRouter->notifyParameterChange(this, paramName, value);
                break;
            }
        }
    });
    
    // 2.3. Initialize default pattern (if needed) - only for new modules, not restored ones
    if (!isRestored) {
        initializeDefaultPattern(registry, connectionManager);
    }
    
    // Phase 2: Migrate existing internal chain to PatternRuntime if not already bound
    // Skip migration during session restoration - bindings are restored separately by SessionManager
    if (patternRuntime_ && boundChainName_.empty() && patternChain.getSize() > 0 && !isRestored) {
        // Create a chain in PatternRuntime for this sequencer
        std::string chainName = getInstanceName() + "_chain";
        if (!patternRuntime_->chainExists(chainName)) {
            patternRuntime_->addChain(chainName);
        }
        
        // Copy chain entries to PatternRuntime
        const auto& chain = patternChain.getChain();
        for (size_t i = 0; i < chain.size(); i++) {
            patternRuntime_->chainAddPattern(chainName, chain[i]);
            int repeatCount = patternChain.getRepeatCount((int)i);
            if (repeatCount > 1) {
                patternRuntime_->chainSetRepeat(chainName, (int)i, repeatCount);
            }
            if (patternChain.isEntryDisabled((int)i)) {
                patternRuntime_->chainSetEntryDisabled(chainName, (int)i, true);
            }
        }
        
        // Set chain enabled state
        patternRuntime_->chainSetEnabled(chainName, patternChain.isEnabled());
        
        // Bind sequencer to the chain
        bindToChain(chainName);
        
        ofLogNotice("TrackerSequencer") << "Migrated existing chain to PatternRuntime: " << chainName;
    }
    
    // Note: Step event listeners for ofApp state tracking (lastTriggeredStep) are set up in ofApp
    // since they're tied to ofApp's internal state, not module state
}

//--------------------------------------------------------------
// Get index range from connected module (queries directly, no caching)
int TrackerSequencer::getIndexRange() const {
    if (!connectionManager_) {
        return 127;  // Default MIDI range
    }
    
    auto* registry = connectionManager_->getRegistry();
    if (!registry) {
        return 127;  // Default MIDI range
    }
    
    // Query directly from connected modules via ConnectionManager
    auto connections = connectionManager_->getConnectionsFrom(getName());
    for (const auto& conn : connections) {
        if (conn.type == ConnectionManager::ConnectionType::EVENT) {
            auto targetModule = registry->getModule(conn.targetModule);
            if (targetModule) {
                // Check if target has "index" parameter
                auto params = targetModule->getParameters();
                for (const auto& param : params) {
                    if (param.name == "index" || param.name == "note") {
                        // Found connected module with index parameter
                        // maxValue is inclusive, range is count
                        int indexCount = static_cast<int>(param.maxValue) + 1;
                        // Only use connected module's count if > 0, otherwise use default
                        if (indexCount > 0) {
                            return indexCount;
                        }
                        // If count is 0, fall through to default
                        break;
                    }
                }
            }
        }
    }
    
    // No connected module found or connected module has 0 items - return default
    return 127;  // Default MIDI range
}

//--------------------------------------------------------------
void TrackerSequencer::onConnectionEstablished(const std::string& targetModuleName,
                                                Module::ConnectionType connectionType,
                                                ConnectionManager* connectionManager) {
    // Only react to EVENT connections (tracker -> pool connections)
    if (connectionType != Module::ConnectionType::EVENT) {
        return;
    }
    
    // Store ConnectionManager reference if not already set
    // Index range is now queried directly via getIndexRange() when needed
    if (!connectionManager_) {
        connectionManager_ = connectionManager;
    }
    
    // Track connected module for parameter discovery
    connectedModuleNames_.insert(targetModuleName);
    
    ofLogNotice("TrackerSequencer") << "Connection established to " << targetModuleName 
                                    << " (total connected: " << connectedModuleNames_.size() << ")";
}

void TrackerSequencer::onConnectionBroken(const std::string& targetModuleName,
                                          Module::ConnectionType connectionType,
                                          ConnectionManager* connectionManager) {
    // Only react to EVENT connections (tracker -> pool connections)
    if (connectionType != Module::ConnectionType::EVENT) {
        return;
    }
    
    // Remove from connection tracking
    connectedModuleNames_.erase(targetModuleName);
    
    ofLogNotice("TrackerSequencer") << "Connection broken to " << targetModuleName 
                                    << " (remaining connected: " << connectedModuleNames_.size() << ")";
    
    // Note: Parameter cache invalidation will be handled by GUI layer
    // when it queries parameters next time (connection-aware query will exclude this module)
}

//--------------------------------------------------------------
void TrackerSequencer::initializeDefaultPattern(ModuleRegistry* registry, ConnectionManager* connectionManager) {
    if (!registry || !connectionManager) {
        return;
    }
    
    // Use ConnectionManager to find event connections (simplified approach)
    std::shared_ptr<Module> connectedProvider;
    auto connections = connectionManager->getConnectionsFrom(getName());
    
    for (const auto& conn : connections) {
        if (conn.type == ConnectionManager::ConnectionType::EVENT) {
            // Found event connection - check if target has "index" parameter
            auto targetModule = registry->getModule(conn.targetModule);
            if (targetModule) {
                auto params = targetModule->getParameters();
                for (const auto& param : params) {
                    if (param.name == "index" || param.name == "note") {
                        connectedProvider = targetModule;
                        break;
                    }
                }
                if (connectedProvider) break;
            }
        }
    }
    
    // Initialize default pattern cells if provider is available and has items
    if (connectedProvider) {
        // Query "index" parameter from getParameters() to get dynamic range
        auto params = connectedProvider->getParameters();
        int indexRange = 127;  // Default MIDI range
        for (const auto& param : params) {
            if (param.name == "index" || param.name == "note") {
                indexRange = static_cast<int>(param.maxValue) + 1;  // maxValue is inclusive, range is count
                break;
            }
        }
        if (indexRange > 0) {
            Step step0(0, 0.0f, 1.0f, 1.0f, 1.0f);
            setStep(0, step0);
            
            if (indexRange > 1) {
                Step step4(1, 0.0f, 1.2f, 1.0f, 1.0f);
                setStep(4, step4);
                
                Step step8(0, 0.5f, 1.0f, 1.0f, 1.0f);
                setStep(8, step8);
            }
            ofLogNotice("TrackerSequencer") << "Initialized default pattern for " << getName() 
                                            << " (index range: 0-" << (indexRange - 1) << ")";
        }
    }
}

//--------------------------------------------------------------
void TrackerSequencer::onTransportChanged(bool isPlaying) {
    // Module interface implementation - delegate to internal method
    onClockTransportChanged(isPlaying);
}

void TrackerSequencer::onClockTransportChanged(bool isPlaying) {
    if (isPlaying) {
        // Clock started - start the sequencer
        play();
        
        // CRITICAL: Start pattern playback in PatternRuntime
        // PatternRuntime will manage its own playback state (playbackStep, etc.)
        if (patternRuntime_ && !boundPatternName_.empty()) {
            patternRuntime_->playPattern(boundPatternName_);
            // Sync local playback state FROM PatternRuntime (don't overwrite it)
            PatternPlaybackState* state = patternRuntime_->getPlaybackState(boundPatternName_);
            if (state) {
                playbackState.playbackStep = state->playbackStep;
                playbackState.currentPlayingStep = state->currentPlayingStep;
                playbackState.patternCycleCount = state->patternCycleCount;
            }
        } else {
            // Fallback: if PatternRuntime not available, use local state
            playbackState.playbackStep = 0;
            playbackState.clearPlayingStep();
            playbackState.patternCycleCount = 0;
        }
        
        // Note: triggerStep() is now handled by PatternRuntime during evaluation
        // PatternRuntime will trigger steps as it evaluates patterns on clock ticks
        ofLogNotice("TrackerSequencer") << "Clock transport started - sequencer playing from step " << (playbackState.playbackStep + 1);
    } else {
        // Clock stopped - pause the sequencer (don't reset step)
        pause();
        
        // CRITICAL: Stop pattern playback in PatternRuntime
        if (patternRuntime_ && !boundPatternName_.empty()) {
            patternRuntime_->stopPattern(boundPatternName_);
            // Sync local state from PatternRuntime after stopping
            PatternPlaybackState* state = patternRuntime_->getPlaybackState(boundPatternName_);
            if (state) {
                playbackState.playbackStep = state->playbackStep;
                playbackState.currentPlayingStep = state->currentPlayingStep;
            }
        }
        
        playbackState.patternCycleCount = 0; // Reset cycle counter on transport stop
        ofLogNotice("TrackerSequencer") << "Clock transport stopped - sequencer paused at step " << (playbackState.playbackStep + 1);
    }
}


void TrackerSequencer::setStepCount(int steps) {
    if (steps <= 0) return;
    
    // Only update current pattern (per-pattern step count)
    getCurrentPattern().setStepCount(steps);

    ofLogNotice("TrackerSequencer") << "Step count changed to " << steps << " for current pattern";
}

int TrackerSequencer::getStepCount() const {
    // Returns current pattern's step count
    return getCurrentPattern().getStepCount();
}

// Helper to get current pattern (Phase 3: Runtime-only)
Pattern& TrackerSequencer::getCurrentPattern() {
    if (patternRuntime_ && !boundPatternName_.empty()) {
        Pattern* pattern = patternRuntime_->getPattern(boundPatternName_);
        if (pattern) {
            return *pattern;
        }
        // Pattern not found - create a default pattern
        ofLogWarning("TrackerSequencer") << "Pattern not found in Runtime: " << boundPatternName_ << ", creating default pattern";
        Pattern defaultPattern(16);
        // Use simple pattern naming (P0, P1, P2, etc.) - let PatternRuntime generate the name
        boundPatternName_ = patternRuntime_->addPattern(defaultPattern, "");
        Pattern* newPattern = patternRuntime_->getPattern(boundPatternName_);
        if (newPattern) {
            return *newPattern;
        }
    }
    
    // Fallback: return static empty pattern (should never happen if PatternRuntime is available)
    // This can happen if PatternRuntime wasn't set during initialization or if called before initialization
    static Pattern emptyPattern(16);
    static std::set<std::string> loggedInstances;
    if (loggedInstances.find(getName()) == loggedInstances.end()) {
        ofLogError("TrackerSequencer") << "PatternRuntime not available for '" << getName() 
                                        << "', using empty pattern. This may indicate initialization issue.";
        loggedInstances.insert(getName());  // Only log once per instance to avoid spam
    }
    return emptyPattern;
}

const Pattern& TrackerSequencer::getCurrentPattern() const {
    if (patternRuntime_ && !boundPatternName_.empty()) {
        const Pattern* pattern = patternRuntime_->getPattern(boundPatternName_);
        if (pattern) {
            return *pattern;
        }
    }
    
    // Fallback: return static empty pattern
    static Pattern emptyPattern(16);
    return emptyPattern;
}

void TrackerSequencer::setStep(int stepIndex, const Step& step) {
    if (!isValidStep(stepIndex)) return;
    
    // Check if position parameter changed and notify if it's the current playback step
    const Step& oldStep = getCurrentPattern().getStep(stepIndex);
    float oldPosition = oldStep.getParameterValue("position", 0.0f);
    float newPosition = step.getParameterValue("position", 0.0f);
    
    // Update the pattern
    Pattern& pattern = getCurrentPattern();
    pattern.setStep(stepIndex, step);
    
    // CRITICAL: Notify PatternRuntime of pattern change
    if (patternRuntime_ && !boundPatternName_.empty()) {
        patternRuntime_->updatePattern(boundPatternName_, pattern);
        patternRuntime_->notifyPatternChanged(boundPatternName_);
    }
    
    // Notify if position changed and this is the current playback step
    // Note: Edit step checking removed - GUI state is managed by TrackerSequencerGUI
    // The GUI will handle edit step notifications separately if needed
    if (parameterChangeCallback && std::abs(oldPosition - newPosition) > 0.0001f) {
        if (stepIndex == playbackState.playbackStep) {
            parameterChangeCallback("currentStepPosition", newPosition);
        }
    }
    
    // Removed verbose logging for performance
}

Step TrackerSequencer::getStep(int stepIndex) const {
    if (!isValidStep(stepIndex)) return Step();
    return getCurrentPattern().getStep(stepIndex);
}

void TrackerSequencer::clearStep(int stepIndex) {
    if (!isValidStep(stepIndex)) return;
    
    getCurrentPattern().clearStep(stepIndex);
    
    // Removed verbose logging for performance
}

void TrackerSequencer::clearPattern() {
    getCurrentPattern().clear();
    
    ofLogNotice("TrackerSequencer") << "Pattern cleared";
}

void TrackerSequencer::randomizePattern() {
    int numMedia = getIndexRange();
    if (numMedia == 0) {
        ofLogWarning("TrackerSequencer") << "Cannot randomize pattern: No media available";
        return;
    }
    
    int stepCount = getCurrentPattern().getStepCount();
    for (int i = 0; i < stepCount; i++) {
        Step step;
        
        // 70% chance of having a media item, 30% chance of being empty (rest)
        if (ofRandom(1.0f) < 0.7f) {
            step.index = ofRandom(0, numMedia);
            
            // Use parameter ranges dynamically instead of hardcoded values
            auto posRange = getParameterRange("position");
            auto speedRange = getParameterRange("speed");
            auto volumeRange = getParameterRange("volume");
            
            step.setParameterValue("position", ofRandom(posRange.first, posRange.second));
            step.setParameterValue("speed", ofRandom(speedRange.first, speedRange.second));
            // Use 25% to 75% of volume range for randomization (avoiding extremes)
            float volumeRangeSize = volumeRange.second - volumeRange.first;
            step.setParameterValue("volume", ofRandom(
                volumeRange.first + volumeRangeSize * 0.25f,
                volumeRange.first + volumeRangeSize * 0.75f
            ));
            const int MAX_STEP_LENGTH = 64;
            step.length = ofRandom(1, MAX_STEP_LENGTH + 1);
        } else {
            step.clear(); // Empty/rest step
        }
        
            getCurrentPattern().setStep(i, step);
    }
    
    ofLogNotice("TrackerSequencer") << "Pattern randomized with " << numMedia << " media items";
}

void TrackerSequencer::randomizeColumn(int columnIndex) {
    // columnIndex is absolute column index (1 = index, 2 = length, 3+ = parameter columns)
    // Convert to parameter-relative index for columnConfig access
    if (columnIndex <= 0) {
        ofLogWarning("TrackerSequencer") << "Invalid column index for randomization: " << columnIndex;
        return;
    }
    
    const auto& columnConfig = getCurrentPattern().getColumnConfiguration();
    int paramColIdx = columnIndex - 1;  // Convert absolute to parameter-relative
    if (paramColIdx < 0 || paramColIdx >= (int)columnConfig.size()) {
        ofLogWarning("TrackerSequencer") << "Invalid column index for randomization: " << columnIndex;
        return;
    }
    
    const auto& colConfig = columnConfig[paramColIdx];
    
    if (colConfig.parameterName == "index") {
        // Randomize index column
        int numMedia = getIndexRange();
        if (numMedia == 0) {
            ofLogWarning("TrackerSequencer") << "Cannot randomize index column: No media available";
            return;
        }
        
        int stepCount = getCurrentPattern().getStepCount();
        for (int i = 0; i < stepCount; i++) {
            // 70% chance of having a media item, 30% chance of being empty (rest)
            if (ofRandom(1.0f) < 0.7f) {
                getCurrentPattern()[i].index = ofRandom(0, numMedia);
            } else {
                getCurrentPattern()[i].index = -1; // Empty/rest
            }
        }
        ofLogNotice("TrackerSequencer") << "Index column randomized";
    } else if (colConfig.parameterName == "length") {
        // Randomize length column
        const int MAX_STEP_LENGTH = 64;
        int stepCount = getCurrentPattern().getStepCount();
        for (int i = 0; i < stepCount; i++) {
            if (getCurrentPattern()[i].index >= 0) { // Only randomize if step has a media item
                getCurrentPattern()[i].length = ofRandom(1, MAX_STEP_LENGTH + 1);
            }
        }
        ofLogNotice("TrackerSequencer") << "Length column randomized";
    } else {
        // Randomize parameter column
        auto range = getParameterRange(colConfig.parameterName);
        int stepCount = getCurrentPattern().getStepCount();
        for (int i = 0; i < stepCount; i++) {
            if (getCurrentPattern()[i].index >= 0) { // Only randomize if step has a media item
                if (colConfig.parameterName == "volume") {
                    // Use 25% to 75% of volume range for randomization (avoiding extremes)
                    float volumeRangeSize = range.second - range.first;
                    getCurrentPattern()[i].setParameterValue(colConfig.parameterName, ofRandom(
                        range.first + volumeRangeSize * 0.25f,
                        range.first + volumeRangeSize * 0.75f
                    ));
                } else {
                    getCurrentPattern()[i].setParameterValue(colConfig.parameterName, ofRandom(range.first, range.second));
                }
            }
        }
        ofLogNotice("TrackerSequencer") << "Parameter column '" << colConfig.parameterName << "' randomized";
    }
}

void TrackerSequencer::applyLegato() {
    // Apply legato: set each step's length to the number of steps until the next step with a note
    // This creates smooth transitions between steps (no gaps)
    int stepCount = getCurrentPattern().getStepCount();
    for (int i = 0; i < stepCount; i++) {
        if (getCurrentPattern()[i].index >= 0) {
            // This step has a note - find the next step with a note
            int stepsToNext = 1;
            bool foundNext = false;
            
            for (int j = i + 1; j < stepCount; j++) {
                if (getCurrentPattern()[j].index >= 0) {
                    // Found the next step with a note
                    stepsToNext = j - i;
                    foundNext = true;
                    break;
                }
            }
            
            if (foundNext) {
                // Set length to reach the next step (clamp to max 64)
                const int MAX_STEP_LENGTH = 64;
                getCurrentPattern()[i].length = std::min(MAX_STEP_LENGTH, stepsToNext);
            } else {
                // No next step found - keep current length or set to remaining steps
                int remainingSteps = stepCount - i;
                const int MAX_STEP_LENGTH = 64;
                getCurrentPattern()[i].length = std::min(MAX_STEP_LENGTH, remainingSteps);
            }
        }
    }
    ofLogNotice("TrackerSequencer") << "Legato applied to length column";
}

bool TrackerSequencer::duplicateRange(int fromStep, int toStep, int destinationStep) {
    // Delegate to Pattern class
    return getCurrentPattern().duplicateRange(fromStep, toStep, destinationStep);
}

// Static clipboard definition (must be defined outside class)
TrackerSequencer::StepClipboard TrackerSequencer::clipboard;

//--------------------------------------------------------------
// Clipboard operations
//--------------------------------------------------------------
void TrackerSequencer::copySteps(int fromStep, int toStep) {
    if (!isValidStep(fromStep) || !isValidStep(toStep)) {
        ofLogWarning("TrackerSequencer") << "Invalid step range for copy: " << fromStep << " to " << toStep;
        return;
    }
    
    if (fromStep > toStep) {
        std::swap(fromStep, toStep);
    }
    
    // Clear existing clipboard
    clipboard.clear();
    
    // Copy steps to clipboard
    for (int i = fromStep; i <= toStep; i++) {
        clipboard.steps.push_back(getStep(i));
    }
    
    clipboard.startStep = fromStep;
    clipboard.endStep = toStep;
    
    ofLogNotice("TrackerSequencer") << "Copied " << (toStep - fromStep + 1) << " steps (" 
                                     << (fromStep + 1) << "-" << (toStep + 1) << ")";
}

void TrackerSequencer::cutSteps(int fromStep, int toStep) {
    if (!isValidStep(fromStep) || !isValidStep(toStep)) {
        ofLogWarning("TrackerSequencer") << "Invalid step range for cut: " << fromStep << " to " << toStep;
        return;
    }
    
    if (fromStep > toStep) {
        std::swap(fromStep, toStep);
    }
    
    // Copy steps to clipboard first
    copySteps(fromStep, toStep);
    
    // Then clear the steps
    clearStepRange(fromStep, toStep);
    
    ofLogNotice("TrackerSequencer") << "Cut " << (toStep - fromStep + 1) << " steps (" 
                                     << (fromStep + 1) << "-" << (toStep + 1) << ")";
}

bool TrackerSequencer::pasteSteps(int destinationStep) {
    if (clipboard.isEmpty()) {
        ofLogWarning("TrackerSequencer") << "Clipboard is empty, nothing to paste";
        return false;
    }
    
    if (!isValidStep(destinationStep)) {
        ofLogWarning("TrackerSequencer") << "Invalid destination step for paste: " << destinationStep;
        return false;
    }
    
    // Check if paste would exceed pattern bounds
    int numSteps = (int)clipboard.steps.size();
    if (destinationStep + numSteps > getStepCount()) {
        ofLogWarning("TrackerSequencer") << "Paste would exceed pattern bounds. Pattern has " 
                                          << getStepCount() << " steps, paste requires " 
                                          << (destinationStep + numSteps) << " steps";
        return false;
    }
    
    // Paste steps from clipboard
    for (size_t i = 0; i < clipboard.steps.size(); i++) {
        int targetStep = destinationStep + (int)i;
        if (isValidStep(targetStep)) {
            setStep(targetStep, clipboard.steps[i]);
        }
    }
    
    ofLogNotice("TrackerSequencer") << "Pasted " << numSteps << " steps starting at step " 
                                     << (destinationStep + 1);
    return true;
}

void TrackerSequencer::duplicateSteps(int fromStep, int toStep, int destinationStep) {
    // Wrapper around duplicateRange for consistency with other clipboard operations
    if (!isValidStep(fromStep) || !isValidStep(toStep) || !isValidStep(destinationStep)) {
        ofLogWarning("TrackerSequencer") << "Invalid step range for duplicate: " << fromStep 
                                          << " to " << toStep << " at " << destinationStep;
        return;
    }
    
    if (fromStep > toStep) {
        std::swap(fromStep, toStep);
    }
    
    // Use existing duplicateRange method
    if (duplicateRange(fromStep, toStep, destinationStep)) {
        ofLogNotice("TrackerSequencer") << "Duplicated " << (toStep - fromStep + 1) << " steps (" 
                                         << (fromStep + 1) << "-" << (toStep + 1) 
                                         << ") to step " << (destinationStep + 1);
    } else {
        ofLogWarning("TrackerSequencer") << "Failed to duplicate steps";
    }
}

void TrackerSequencer::clearStepRange(int fromStep, int toStep) {
    if (!isValidStep(fromStep) || !isValidStep(toStep)) {
        ofLogWarning("TrackerSequencer") << "Invalid step range for clear: " << fromStep << " to " << toStep;
        return;
    }
    
    if (fromStep > toStep) {
        std::swap(fromStep, toStep);
    }
    
    // Clear each step in the range
    for (int i = fromStep; i <= toStep; i++) {
        clearStep(i);
    }
    
    ofLogNotice("TrackerSequencer") << "Cleared " << (toStep - fromStep + 1) << " steps (" 
                                     << (fromStep + 1) << "-" << (toStep + 1) << ")";
}

// Timing and playback control
void TrackerSequencer::processAudioBuffer(ofSoundBuffer& buffer) {
    // Phase 3: PatternRuntime handles evaluation, TrackerSequencer only forwards events
    // Sync sequencer binding and playback state from PatternRuntime
    if (patternRuntime_ && !getInstanceName().empty()) {
        PatternRuntime::SequencerBinding binding = patternRuntime_->getSequencerBinding(getInstanceName());
        
        // CRITICAL: Sync boundPatternName_ if PatternRuntime binding changed (e.g., from chain progression)
        if (!binding.patternName.empty() && binding.patternName != boundPatternName_) {
            // Pattern binding changed in PatternRuntime - sync our state
            std::string oldPattern = boundPatternName_;
            boundPatternName_ = binding.patternName;
            
            // Unsubscribe from old pattern events
            if (!oldPattern.empty()) {
                ofRemoveListener(patternRuntime_->triggerEvent, this, &TrackerSequencer::onPatternRuntimeTrigger);
            }
            
            // Subscribe to new pattern events
            ofAddListener(patternRuntime_->triggerEvent, this, &TrackerSequencer::onPatternRuntimeTrigger);
            
            // Sync chain index with new pattern
            PatternChain* chain = getCurrentChain();
            if (chain && chain->isEnabled()) {
                const auto& chainPatterns = chain->getChain();
                for (size_t i = 0; i < chainPatterns.size(); ++i) {
                    if (chainPatterns[i] == boundPatternName_) {
                        chain->setCurrentIndex((int)i);
                        break;
                    }
                }
            }
            
            ofLogVerbose("TrackerSequencer") << "Synced pattern binding from PatternRuntime: '" 
                                            << oldPattern << "' -> '" << boundPatternName_ << "'";
        }
        
        // CRITICAL: Sync boundChainName_ from PatternRuntime (for GUI display)
        // Note: This is a fallback sync - primary sync happens via onSequencerBindingChanged event
        if (binding.chainName != boundChainName_) {
            std::string oldChain = boundChainName_;
            boundChainName_ = binding.chainName;
            if (!boundChainName_.empty()) {
                ofLogVerbose("TrackerSequencer") << "Synced chain binding from PatternRuntime: '" 
                                                << oldChain << "' -> '" << boundChainName_ << "'";
            } else if (!oldChain.empty()) {
                ofLogVerbose("TrackerSequencer") << "Synced chain unbinding from PatternRuntime: '" 
                                                << oldChain << "' -> [unbound]";
            }
        }
        
        // Sync playback state for GUI display
        if (!boundPatternName_.empty()) {
            PatternPlaybackState* state = patternRuntime_->getPlaybackState(boundPatternName_);
            if (state) {
                playbackState.isPlaying = state->isPlaying;
                playbackState.playbackStep = state->playbackStep;
                playbackState.currentPlayingStep = state->currentPlayingStep;
            }
        }
    }
    return;  // Skip evaluation - PatternRuntime handles it in Engine::audioOut()
    
    // Calculate samples per step from our own stepsPerBeat
    float bpm = clock->getBPM();
    float sampleRate = buffer.getSampleRate();
    if (sampleRate <= 0.0f || bpm <= 0.0f) return;
    
    float beatsPerSecond = bpm / 60.0f;
    float samplesPerBeat = sampleRate / beatsPerSecond;
    float patternSPB = getCurrentPattern().getStepsPerBeat();
    float samplesPerStep = samplesPerBeat / std::abs(patternSPB);  // Use absolute value for timing calculation
    
    // Sample-accurate step detection
    int numFrames = buffer.getNumFrames();
    for (int i = 0; i < numFrames; i++) {
        playbackState.sampleAccumulator += 1.0;
        
        if (playbackState.sampleAccumulator >= samplesPerStep) {
            playbackState.sampleAccumulator -= samplesPerStep;
            advanceStep();  // Advance to next step
        }
    }
}

void TrackerSequencer::onTimeEvent(TimeEvent& data) {
    if (!playbackState.isPlaying) return;
    
    // Update BPM from beat event (for synchronization)
    // DO NOT reset step accumulator - it's handled by processAudioBuffer() for sample accuracy
    // Resetting here causes timing drift when beat events arrive at slightly different times
    playbackState.lastBpm = data.bpm;
}

//--------------------------------------------------------------
void TrackerSequencer::setStepsPerBeat(float steps) {
    // Set stepsPerBeat for current pattern only (per-pattern timing)
    getCurrentPattern().setStepsPerBeat(steps);
    updateStepInterval();
    // Note: stepsPerBeat is now per-pattern, not per-instance
}

void TrackerSequencer::updateStepInterval() {
    if (!clock) return;
    
    // Get steps per beat from current pattern (per-pattern timing)
    // Use absolute value for timing calculations (direction only affects step advancement)
    float spb = std::abs(getCurrentPattern().getStepsPerBeat());
    
    // Calculate time between sequencer steps based on BPM and steps per beat
    // For example: 120 BPM with 4 steps per beat = 16th notes
    // Each beat = 60/120 = 0.5 seconds
    // Each step = 0.5 / 4 = 0.125 seconds
    float bpm = clock->getBPM();
    float stepInterval = (60.0f / bpm) / spb;
    
    ofLogNotice("TrackerSequencer") << "Updated timing: SPB=" << spb 
                                   << ", stepInterval=" << stepInterval << "s";
}

void TrackerSequencer::play() {
    playbackState.isPlaying = true;
    playbackState.clearPlayingStep();
    // Reset audio-rate timing for fresh start
    playbackState.sampleAccumulator = 0.0;
    playbackState.lastBpm = clock ? clock->getBPM() : 120.0f;
}

void TrackerSequencer::pause() {
    playbackState.isPlaying = false;
    // Clear current playing step so GUI shows inactive state when paused
    // This ensures visual feedback matches the paused state
    playbackState.clearPlayingStep();
    // Keep playbackStep and timing state for resume (if needed)
}

void TrackerSequencer::stop() {
    playbackState.isPlaying = false;
    playbackState.reset();  // reset() already resets patternCycleCount to 0
}

void TrackerSequencer::reset() {
    playbackState.reset();
}

void TrackerSequencer::setCurrentStep(int step) {
    if (isValidStep(step)) {
        playbackState.playbackStep = step; // Update playback step indicator
    }
}

ofJson TrackerSequencer::toJson(class ModuleRegistry* registry) const {
    ofJson json;
    json["currentStep"] = playbackState.playbackStep;  // Save playback step for backward compatibility
    // Note: GUI state (editStep, etc.) no longer saved here - managed by TrackerSequencerGUI
    
    // Save enabled state
    json["enabled"] = isEnabled();
    
    // Phase 3: Runtime-only architecture - save binding info only
    if (!boundPatternName_.empty()) {
        json["boundPatternName"] = boundPatternName_;
    }
    
    // Save chain binding for GUI restoration
    if (!boundChainName_.empty()) {
        json["boundChainName"] = boundChainName_;
    }
    
    // Note: stepsPerBeat is now saved per-pattern (in Pattern::toJson)
    // Keep sequencer-level stepsPerBeat for backward compatibility only (legacy files)
    json["stepsPerBeat"] = stepsPerBeat;  // Legacy: keep for backward compatibility
    
    // Column configuration is now saved per-pattern (in Pattern::toJson)
    // No need to save it here - each pattern saves its own columnConfig
    
    // Phase 2: Patterns are saved in PatternRuntime (not here)
    // Phase 2: Chains are saved in PatternRuntime (not here)
    // Only save binding info - chains are saved separately in PatternRuntime
    // Note: Internal patternChain is kept for backward compatibility during migration
    
    return json;
}

// getTypeName() uses default implementation from Module base class (returns getName())

bool TrackerSequencer::saveState(const std::string& filename) const {
    ofJson json = toJson();
    
    ofFile file(filename, ofFile::WriteOnly);
    if (file.is_open()) {
        file << json.dump(4); // Pretty print with 4 spaces
        file.close();
        ofLogNotice("TrackerSequencer") << "State saved to " << filename;
        return true;
    } else {
        ofLogError("TrackerSequencer") << "Failed to save state to " << filename;
        return false;
    }
}

void TrackerSequencer::fromJson(const ofJson& json) {
    // Load enabled state
    if (json.contains("enabled")) {
        setEnabled(json["enabled"].get<bool>());
    }
    
    // Phase 3: Runtime-only architecture - load binding info
    if (json.contains("boundPatternName")) {
        boundPatternName_ = json["boundPatternName"].get<std::string>();
        // Note: Actual binding happens after PatternRuntime is loaded by SessionManager
    }
    
    // Load chain binding for GUI restoration
    if (json.contains("boundChainName")) {
        boundChainName_ = json["boundChainName"].get<std::string>();
        // Note: Actual binding happens after PatternRuntime is loaded by SessionManager
        // Migration of old "TrackerSequencer_chain" to instance-specific chains happens in SessionManager
    }
    
    // Load basic properties
    if (json.contains("currentStep")) {
        playbackState.playbackStep = json["currentStep"];
    }
    // Note: GUI state (editStep, etc.) no longer loaded here - managed by TrackerSequencerGUI
    
    // Load stepsPerBeat (backward compatibility - kept for legacy files)
    if (json.contains("stepsPerBeat")) {
        // Support both int (legacy) and float (new) formats
        if (json["stepsPerBeat"].is_number_float()) {
            stepsPerBeat = json["stepsPerBeat"];
        } else if (json["stepsPerBeat"].is_number_integer()) {
            stepsPerBeat = static_cast<float>(json["stepsPerBeat"]);
        } else {
            stepsPerBeat = 4.0f;  // Default fallback
        }
        // Clamp to valid range: -96 to 96, excluding 0
        if (stepsPerBeat == 0.0f) {
            stepsPerBeat = 4.0f;
        }
        stepsPerBeat = std::max(-96.0f, std::min(96.0f, stepsPerBeat));
    } else {
        stepsPerBeat = 4.0f;  // Default fallback
    }
    
    // Column configuration is now per-pattern (loaded in Pattern::fromJson)
    
    // Phase 2: Patterns are loaded from PatternRuntime by SessionManager
    // Phase 2: Chains are loaded from PatternRuntime by SessionManager
    // Legacy pattern chain in JSON will be migrated to PatternRuntime
    // Load pattern chain info (uses pattern names) and migrate to PatternRuntime
    if (json.contains("patternChain")) {
        // Load pattern chain with available pattern names (may be empty if patterns not migrated yet)
        if (patternRuntime_) {
            auto patternNames = patternRuntime_->getPatternNames();
            patternChain.fromJson(json, patternNames);
            
            // Phase 2: Migrate chain to PatternRuntime if it has entries
            if (patternChain.getSize() > 0) {
                // Create a chain in PatternRuntime for this sequencer
                std::string chainName = getInstanceName() + "_chain";  // Use sequencer name + "_chain"
                if (!patternRuntime_->chainExists(chainName)) {
                    patternRuntime_->addChain(chainName);
                }
                
                // Copy chain entries to PatternRuntime
                const auto& chain = patternChain.getChain();
                for (size_t i = 0; i < chain.size(); i++) {
                    patternRuntime_->chainAddPattern(chainName, chain[i]);
                    int repeatCount = patternChain.getRepeatCount((int)i);
                    if (repeatCount > 1) {
                        patternRuntime_->chainSetRepeat(chainName, (int)i, repeatCount);
                    }
                    if (patternChain.isEntryDisabled((int)i)) {
                        patternRuntime_->chainSetEntryDisabled(chainName, (int)i, true);
                    }
                }
                
                // Set chain enabled state
                patternRuntime_->chainSetEnabled(chainName, patternChain.isEnabled());
                
                // Bind sequencer to the chain
                bindToChain(chainName);
                
                ofLogNotice("TrackerSequencer") << "Migrated chain to PatternRuntime: " << chainName;
            }
        } else {
            // PatternRuntime not available yet - load to internal chain for later migration
            patternChain.fromJson(json, std::vector<std::string>());
        }
    }
}

void TrackerSequencer::reloadPatternChain(const ofJson& json, const std::vector<std::string>& availablePatternNames) {
    // Phase 2: Reload pattern chain and migrate to PatternRuntime
    // This is called by SessionManager after migration when all patterns are available
    patternChain.fromJson(json, availablePatternNames);
    
    // Migrate to PatternRuntime if not already bound
    if (patternRuntime_ && boundChainName_.empty() && patternChain.getSize() > 0) {
        // Create a chain in PatternRuntime for this sequencer
        std::string chainName = getInstanceName() + "_chain";
        if (!patternRuntime_->chainExists(chainName)) {
            patternRuntime_->addChain(chainName);
        }
        
        // Copy chain entries to PatternRuntime
        const auto& chain = patternChain.getChain();
        for (size_t i = 0; i < chain.size(); i++) {
            patternRuntime_->chainAddPattern(chainName, chain[i]);
            int repeatCount = patternChain.getRepeatCount((int)i);
            if (repeatCount > 1) {
                patternRuntime_->chainSetRepeat(chainName, (int)i, repeatCount);
            }
            if (patternChain.isEntryDisabled((int)i)) {
                patternRuntime_->chainSetEntryDisabled(chainName, (int)i, true);
            }
        }
        
        // Set chain enabled state
        patternRuntime_->chainSetEnabled(chainName, patternChain.isEnabled());
        
        // Bind sequencer to the chain
        bindToChain(chainName);
        
        ofLogNotice("TrackerSequencer") << "Migrated chain to PatternRuntime: " << chainName;
    }
    
    ofLogNotice("TrackerSequencer") << "Reloaded pattern chain with " << availablePatternNames.size() << " available patterns";
}

bool TrackerSequencer::loadState(const std::string& filename) {
    ofFile file(filename, ofFile::ReadOnly);
    if (!file.is_open()) {
        ofLogError("TrackerSequencer") << "Failed to load state from " << filename;
        return false;
    }
    
    std::string jsonString = file.readToBuffer().getText();
    file.close();
    
    ofJson json;
    try {
        json = ofJson::parse(jsonString);
    } catch (const std::exception& e) {
        ofLogError("TrackerSequencer") << "Failed to parse JSON: " << e.what();
        return false;
    }
    
    fromJson(json);
    
    ofLogNotice("TrackerSequencer") << "State loaded from " << filename;
    return true;
}

void TrackerSequencer::addStepEventListener(std::function<void(int, float, const Step&)> listener) {
    stepEventListeners.push_back(listener);
}

void TrackerSequencer::advanceStep() {
    if (!playbackState.isPlaying) return;
    
    float currentTime = ofGetElapsedTimef();
    
    // Check if current step duration has expired
    bool currentStepExpired = (playbackState.currentPlayingStep >= 0 && playbackState.stepEndTime > 0.0f && currentTime >= playbackState.stepEndTime);
    
    if (currentStepExpired) {
        // Current step finished - clear playing state
        playbackState.clearPlayingStep();
    }
    
    // Always advance playback step (for visual indicator)
    // Support backward reading when stepsPerBeat is negative
    int stepCount = getCurrentPattern().getStepCount();
    int previousStep = playbackState.playbackStep;
    float currentSPB = getCurrentPattern().getStepsPerBeat();
    
    bool patternFinished;
    if (currentSPB < 0.0f) {
        // Backward reading: decrement step
        playbackState.playbackStep = (playbackState.playbackStep - 1 + stepCount) % stepCount;
        // Check if we wrapped around (pattern finished - went from 0 to stepCount-1)
        patternFinished = (playbackState.playbackStep == stepCount - 1 && previousStep == 0);
    } else {
        // Forward reading: increment step
        playbackState.playbackStep = (playbackState.playbackStep + 1) % stepCount;
        // Check if we wrapped around (pattern finished - went from stepCount-1 to 0)
        patternFinished = (playbackState.playbackStep == 0 && previousStep == stepCount - 1);
    }
    
    // Increment pattern cycle count when pattern wraps (one pattern repeat = one cycle)
    if (patternFinished) {
        playbackState.patternCycleCount++;
    }
    
    // Phase 2: Pattern chain handling (uses PatternRuntime chains)
    // Check both chain enabled state AND sequencer binding chainEnabled flag
    PatternChain* chain = getCurrentChain();
    bool sequencerChainEnabled = false;
    if (patternRuntime_ && !getInstanceName().empty()) {
        PatternRuntime::SequencerBinding binding = patternRuntime_->getSequencerBinding(getInstanceName());
        sequencerChainEnabled = binding.chainEnabled && !binding.chainName.empty();
    }
    
    // Chain progression requires: chain exists, chain is enabled, AND sequencer binding chainEnabled is true
    if (patternFinished && chain && chain->isEnabled() && sequencerChainEnabled) {
        std::string nextPatternName = chain->getNextPattern();
        
        if (!nextPatternName.empty()) {
            bindToPattern(nextPatternName);
        }
    }
    
    // Check if we should trigger the new step
    const Step& newStep = getStep(playbackState.playbackStep);
    
    // Trigger new step if:
    // 1. No step is currently playing (currentPlayingStep < 0), OR
    // 2. New step has media (index >= 0) - this overrides current playing step
    if (playbackState.currentPlayingStep < 0 || newStep.index >= 0) {
        triggerStep(playbackState.playbackStep);
    }
}

void TrackerSequencer::triggerStep(int step) {
    if (!isValidStep(step)) return;
    if (!clock) return;
    
    // Don't send triggers if module is disabled
    if (!isEnabled()) return;
    
    // Apply any pending edit for this step before triggering
    if (pendingEdit.step == step && pendingEdit.isValid()) {
        applyPendingEdit();
        pendingEdit.clear();
    }

    const Step& stepData = getStep(step); // Direct 0-based array access
    float bpm = clock->getBPM();
    
    playbackState.playbackStep = step;
    
    // Check ratio parameter (internal) - only trigger if current cycle matches ratio
    // Ratio is A:B format, where A is which cycle to trigger (1-based) and B is total cycles
    // Default is 1:1 (always trigger)
    // Use direct field access for performance (ratioA/ratioB are now direct fields)
    if (stepData.index >= 0) {  // Only check ratio if step has a trigger
        int ratioA = std::max(1, std::min(16, stepData.ratioA)); // Clamp to 1-16
        int ratioB = std::max(1, std::min(16, stepData.ratioB)); // Clamp to 1-16
        
        // Calculate current cycle position in ratio loop (1-based)
        // patternCycleCount is 0-based, so add 1 for 1-based cycle position
        int currentCycle = playbackState.patternCycleCount + 1;
        int cycleInLoop = ((currentCycle - 1) % ratioB) + 1;  // 1-based position in loop
        
        if (cycleInLoop != ratioA) {
            // Ratio condition failed - don't trigger this step
            playbackState.clearPlayingStep();
            return;
        }
    }
    
    // Check chance parameter (internal) - only trigger if random roll succeeds
    // Chance is 0-100, default 100 (always trigger)
    // Use direct field access for performance (chance is now a direct field)
    int chance = stepData.chance;
    chance = std::max(0, std::min(100, chance)); // Clamp to 0-100 (safety check)
    
    // Roll for chance (0-100)
    if (chance < 100) {
        int roll = (int)(ofRandom(0.0f, 100.0f));
        if (roll >= chance) {
            // Chance failed - don't trigger this step
            playbackState.clearPlayingStep();
            return;
        }
    }
    
    // All trigger conditions passed - proceed with triggering
    // Calculate duration in seconds (same for both manual and playback)
    float stepLength = stepData.index >= 0 ? (float)stepData.length : 1.0f;
    float currentSPB = getCurrentPattern().getStepsPerBeat();
    float duration = (stepLength * 60.0f) / (bpm * std::abs(currentSPB));  // Use absolute value for duration calculation
    
    // Set timing for ALL triggers (unified for manual and playback)
    // Only set currentPlayingStep if step actually triggered (all conditions passed)
    if (stepData.index >= 0) {
        float currentTime = ofGetElapsedTimef();
        playbackState.stepStartTime = currentTime;
        playbackState.stepEndTime = currentTime + duration;
        playbackState.currentPlayingStep = step;
    } else {
        // Empty step - clear playing state
        playbackState.clearPlayingStep();
    }
    
    // Create TriggerEvent with TrackerSequencer parameters
    // TrackerSequencer exposes its own parameters (note, position, speed, volume)
    // Modules will map these to their own parameters
    TriggerEvent triggerEvt;
    triggerEvt.duration = duration;
    triggerEvt.step = step;  // Include step number for position memory modes
    
    // Map Step parameters to TrackerSequencer parameters
    // "note" is the sequencer's parameter name (maps to stepData.index for MultiSampler)
    if (stepData.index >= 0) {
        triggerEvt.parameters["note"] = (float)stepData.index;
    } else {
        triggerEvt.parameters["note"] = -1.0f; // Rest/empty step
    }
    
    // Tracker-specific parameters that are NOT sent to external modules
    // These are sequencer-specific: index, length, note, chance, ratio
    std::set<std::string> trackerOnlyParams = {"index", "length", "note", "chance", "ratio"};
    
    // MODULAR: Only send parameters that are in the current pattern's column configuration
    // This ensures we only send parameters that are actually displayed/used in the grid
    // Skip tracker-specific parameters - they're sequencer-specific and not sent to modules
    const auto& columnConfig = getCurrentPattern().getColumnConfiguration();
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
    // 3. Not tracker-specific parameters (index, length, note, chance)
    for (const auto& paramPair : stepData.parameterValues) {
        const std::string& paramName = paramPair.first;
        float paramValue = paramPair.second;
        
        // Skip tracker-specific parameters - they're sequencer-specific and not sent to modules
        if (trackerOnlyParams.find(paramName) != trackerOnlyParams.end()) {
            continue;
        }
        
        // Only send if parameter is in the current pattern's column configuration
        if (columnParamNames.find(paramName) != columnParamNames.end()) {
            // Add parameter to trigger event
            // Note: We don't validate ranges here because we don't have access to parameter descriptors
            // MultiSampler will validate ranges when it receives the parameter
            triggerEvt.parameters[paramName] = paramValue;
        }
    }
    
    // Broadcast trigger event to all subscribers (modular!)
    // NOTE: ofNotifyEvent is called from audio thread - this is acceptable for event dispatch
    // The actual handlers (MultiSampler::onTrigger) now use lock-free queue
    ofNotifyEvent(triggerEvent, triggerEvt);
    
    // Legacy: Also notify old event system for backward compatibility
    // Optimized: Pass step and duration directly to avoid redundant lookups
    if (!stepEventListeners.empty()) {
        float noteDuration = duration; // Already calculated above
        int step1Based = step + 1; // Convert to 1-based for display
        for (auto& callback : stepEventListeners) {
            callback(step1Based, noteDuration, stepData);
        }
    }
}

//--------------------------------------------------------------
bool TrackerSequencer::isValidStep(int step) const {
    return step >= 0 && step < getCurrentPattern().getStepCount();
}
//--------------------------------------------------------------
bool TrackerSequencer::shouldQueueEdit(int editStep, int editColumn) const {
    return playbackState.isPlaying && isValidStep(editStep) && editStep == playbackState.playbackStep && editColumn > 0;
}

void TrackerSequencer::applyPendingEdit() {
    if (!pendingEdit.isValid() || !isValidStep(pendingEdit.step)) {
        return;
    }
    
    Step& step = getPatternStep(pendingEdit.step);
    
    // Apply edit based on type
    switch (pendingEdit.type) {
        case PendingEdit::EditType::REMOVE:
            step.removeParameter(pendingEdit.parameterName);
            break;
            
        case PendingEdit::EditType::PARAMETER:
            if (!pendingEdit.parameterName.empty()) {
                auto range = getParameterRange(pendingEdit.parameterName);
                float clampedValue = std::max(range.first, std::min(range.second, pendingEdit.value));
                step.setParameterValue(pendingEdit.parameterName, clampedValue);
            }
            break;
            
        case PendingEdit::EditType::NONE:
            break;
    }
    
    // Clear after applying
    pendingEdit.clear();
}

// initializeEditBuffer() removed - use ParameterCell::enterEditMode() instead
// ParameterCell manages its own edit buffer initialization

// Expose TrackerSequencer parameters for discovery
//--------------------------------------------------------------
// Module interface implementation
//--------------------------------------------------------------
std::string TrackerSequencer::getName() const {
    return "TrackerSequencer";
}

ModuleType TrackerSequencer::getType() const {
    return ModuleType::SEQUENCER;
}

//--------------------------------------------------------------
bool TrackerSequencer::hasCapability(ModuleCapability capability) const {
    switch (capability) {
        case ModuleCapability::EMITS_TRIGGER_EVENTS:
            return true;
        default:
            return false;
    }
}

//--------------------------------------------------------------
std::vector<ModuleCapability> TrackerSequencer::getCapabilities() const {
    return {
        ModuleCapability::EMITS_TRIGGER_EVENTS
    };
}

//--------------------------------------------------------------
Module::ModuleMetadata TrackerSequencer::getMetadata() const {
    Module::ModuleMetadata metadata;
    metadata.typeName = "TrackerSequencer";
    metadata.eventNames = {"triggerEvent"};
    metadata.parameterNames = {"currentStepPosition"};
    metadata.parameterDisplayNames["currentStepPosition"] = "Step Position";
    return metadata;
}

std::vector<ParameterDescriptor> TrackerSequencer::getParametersImpl() const {
    // Module interface - return available parameters (without external params for backward compatibility)
    return getAvailableParameters({});
}

ofEvent<TriggerEvent>* TrackerSequencer::getEvent(const std::string& eventName) {
    if (eventName == "triggerEvent") {
        return &triggerEvent;
    }
    return nullptr;
}

void TrackerSequencer::onTrigger(TriggerEvent& event) {
    // Sequencers don't receive triggers - they generate them
    // This method must exist to satisfy Module interface, but does nothing
}

void TrackerSequencer::setParameterImpl(const std::string& paramName, float value, bool notify) {
    // Handle "currentStepPosition" parameter (for ParameterRouter synchronization)
    if (paramName == "currentStepPosition") {
        setCurrentStepPosition(value);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback(paramName, value);
        }
        return;
    }
    
    // Sequencers don't have other settable parameters in the traditional sense
    // Parameters are set per-step via pattern cells
    // This method exists for Module interface compliance but does nothing for other parameters
    if (notify && parameterChangeCallback) {
        parameterChangeCallback(paramName, value);
    }
}

float TrackerSequencer::getParameterImpl(const std::string& paramName) const {
    if (paramName == "currentStepPosition") {
        return getCurrentStepPosition();
    }
    // Unknown parameter - return default (base class default is 0.0f)
    // NOTE: Cannot call Module::getParameter() here as it would deadlock (lock already held)
    return 0.0f;
}

//--------------------------------------------------------------
// Indexed parameter support (for ParameterRouter step[4].position access)
//--------------------------------------------------------------
float TrackerSequencer::getIndexedParameter(const std::string& paramName, int index) const {
    // Validate step index
    if (!isValidStep(index)) {
        ofLogWarning("TrackerSequencer") << "Invalid step index " << index << " for getIndexedParameter";
        return 0.0f;
    }
    
    // Get step and return parameter value
    Step step = getStep(index);
    return step.getParameterValue(paramName, 0.0f);
}

void TrackerSequencer::setIndexedParameter(const std::string& paramName, int index, float value, bool notify) {
    // Validate step index
    if (!isValidStep(index)) {
        ofLogWarning("TrackerSequencer") << "Invalid step index " << index << " for setIndexedParameter";
        return;
    }
    
    // Get step, update parameter, and save back
    // This triggers proper notifications and pattern updates via setStep()
    Step step = getStep(index);
    step.setParameterValue(paramName, value);
    setStep(index, step);  // This triggers notifications and pattern updates
    
    // Notify parameter change callback if requested
    if (notify && parameterChangeCallback) {
        parameterChangeCallback(paramName, value);
    }
}

// Keyboard input handling has been moved to TrackerSequencerGUI::handleKeyPress()

// Static helper to get internal parameters (doesn't depend on instance state)
// Unified parameter registry for tracker-specific parameters
std::vector<ParameterDescriptor> TrackerSequencer::getTrackerParameters() const {
    std::vector<ParameterDescriptor> params;
    
    // Index: media index (dynamic range based on connected module)
    int maxIndex = getIndexRange();
    params.push_back(ParameterDescriptor("index", ParameterType::INT, 0.0f, (float)maxIndex, 0.0f, "Index"));
    
    // Note: MIDI note (0-127, can replace or work alongside index column)
    params.push_back(ParameterDescriptor("note", ParameterType::INT, 0.0f, 127.0f, 60.0f, "Note"));
    
    // Length: step length (fixed maximum of 64, can exceed pattern length)
    const int MAX_STEP_LENGTH = 64;
    params.push_back(ParameterDescriptor("length", ParameterType::INT, 1.0f, (float)MAX_STEP_LENGTH, 1.0f, "Length"));
    
    // Chance: trigger probability (0-100, controls whether step triggers)
    params.push_back(ParameterDescriptor("chance", ParameterType::INT, 0.0f, 100.0f, 100.0f, "Chance"));
    
    // Ratio: conditional triggering (A:B format, 1-16 range for both A and B)
    // Note: Ratio is encoded as A * 1000 + B for storage, but displayed as A:B
    params.push_back(ParameterDescriptor("ratio", ParameterType::INT, 1001.0f, 16016.0f, 1001.0f, "Ratio"));
    
    return params;
}

// Static helper to get tracker parameter descriptor (for static contexts)
ParameterDescriptor TrackerSequencer::getTrackerParameterDescriptor(const std::string& paramName) {
    if (paramName == "index") {
        return ParameterDescriptor("index", ParameterType::INT, 0.0f, 127.0f, 0.0f, "Index");
    } else if (paramName == "note") {
        return ParameterDescriptor("note", ParameterType::INT, 0.0f, 127.0f, 60.0f, "Note");
    } else if (paramName == "length") {
        return ParameterDescriptor("length", ParameterType::INT, 1.0f, 64.0f, 1.0f, "Length");
    } else if (paramName == "chance") {
        return ParameterDescriptor("chance", ParameterType::INT, 0.0f, 100.0f, 100.0f, "Chance");
    } else if (paramName == "ratio") {
        // Ratio is encoded as A * 1000 + B (e.g., 2:4 = 2004)
        return ParameterDescriptor("ratio", ParameterType::INT, 1001.0f, 16016.0f, 1001.0f, "Ratio");
    }
    // Return empty descriptor for unknown parameters
    return ParameterDescriptor();
}

std::vector<ParameterDescriptor> TrackerSequencer::getInternalParameters() {
    std::vector<ParameterDescriptor> params;
    
    // Internal parameters: sequencer-specific, not sent to external modules
    // "note" - can replace or work alongside index column (0-127, maps to cell.index)
    params.push_back(ParameterDescriptor("note", ParameterType::INT, 0.0f, 127.0f, 60.0f, "Note"));
    // "chance" - trigger probability (0-100, controls whether step triggers)
    params.push_back(ParameterDescriptor("chance", ParameterType::INT, 0.0f, 100.0f, 100.0f, "Chance"));
    // "ratio" - conditional triggering (A:B format, encoded as A * 1000 + B)
    params.push_back(ParameterDescriptor("ratio", ParameterType::INT, 1001.0f, 16016.0f, 1001.0f, "Ratio"));
    
    return params;
}

// Static helper to get default parameters (for backward compatibility when no external params)
std::vector<ParameterDescriptor> TrackerSequencer::getDefaultParameters() {
    std::vector<ParameterDescriptor> params;
    
    // Hardcoded defaults for backward compatibility when external params are not available
    params.push_back(ParameterDescriptor("position", ParameterType::FLOAT, 0.0f, 1.0f, 0.0f, "Position"));
    params.push_back(ParameterDescriptor("speed", ParameterType::FLOAT, -10.0f, 10.0f, 1.0f, "Speed"));
    params.push_back(ParameterDescriptor("volume", ParameterType::FLOAT, 0.0f, 2.0f, 1.0f, "Volume"));
    
    return params;
}

std::vector<ParameterDescriptor> TrackerSequencer::getAvailableParameters(const std::vector<ParameterDescriptor>& externalParams) const {
    std::vector<ParameterDescriptor> params;
    
    // Start with tracker-specific parameters (index, note, length, chance)
    // These use dynamic ranges from the instance
    auto trackerParams = getTrackerParameters();
    params.insert(params.end(), trackerParams.begin(), trackerParams.end());
    
    // Add external parameters if provided (from connected modules)
    if (!externalParams.empty()) {
        // Filter out any tracker parameters that might be in external params (safety check)
        std::set<std::string> trackerParamNames;
        for (const auto& param : trackerParams) {
            trackerParamNames.insert(param.name);
        }
        
        // Use a map to deduplicate by name
        std::map<std::string, ParameterDescriptor> uniqueParams;
        
        // Add external params from connected modules (no hardcoded defaults)
        // This ensures we only show parameters from modules that are actually connected
        for (const auto& param : externalParams) {
            if (trackerParamNames.find(param.name) == trackerParamNames.end()) {
                uniqueParams[param.name] = param;
            }
        }
        
        // Convert map to vector
        for (const auto& pair : uniqueParams) {
            params.push_back(pair.second);
        }
    }
    // If externalParams is empty, it means no modules are connected
    // In this case, we only show tracker-specific parameters, maintaining true modularity
    
    return params;
}

bool TrackerSequencer::isPatternEmpty() const {
    return getCurrentPattern().isEmpty();
}

void TrackerSequencer::notifyStepEvent(int step, float stepLength) {
    // step is 1-based from PatternSequencer, convert to 0-based for internal access
    const Step& stepData = getStep(step - 1);
    float bpm = clock ? clock->getBPM() : 120.0f;
    
    // Calculate duration in seconds using current pattern's stepsPerBeat
    float currentSPB = getCurrentPattern().getStepsPerBeat();
    float spb = std::abs(currentSPB);  // Use absolute value for duration calculation
    float stepDuration = (60.0f / bpm) / spb;  // Duration of ONE step
    float noteDuration = stepDuration * stepLength;     // Duration for THIS note
    
    for (auto& callback : stepEventListeners) {
        callback(step, noteDuration, stepData);  // Pass 1-based step number for display
    }
}

float TrackerSequencer::getCurrentStepPosition() const {
    // Note: GUI state (editStep, editColumn) is now managed by TrackerSequencerGUI
    // This method now only returns position for the playback step
    // The GUI should handle edit step position separately if needed
    if (!isValidStep(playbackState.playbackStep)) {
        return 0.0f;
    }
    
    const Step& step = getCurrentPattern()[playbackState.playbackStep];
    return step.getParameterValue("position", 0.0f);
}

void TrackerSequencer::setCurrentStepPosition(float position) {
    // Note: GUI state (editStep, editColumn, isEditingCell) is now managed by TrackerSequencerGUI
    // This method now only sets position for the playback step
    // The GUI should handle edit step position separately if needed
    
    // Clamp position to valid range
    position = std::max(0.0f, std::min(1.0f, position));
    
    if (!isValidStep(playbackState.playbackStep)) {
        return;
    }
    
    Step& step = getCurrentPattern()[playbackState.playbackStep];
    float oldValue = step.getParameterValue("position", 0.0f);
    
    // Only update if value actually changed to avoid unnecessary notifications
    if (std::abs(oldValue - position) > 0.0001f) {
        step.setParameterValue("position", position);
        // Use setStep to properly update the pattern and trigger notifications
        setStep(playbackState.playbackStep, step);
    }
}

float TrackerSequencer::getCurrentBpm() const {
    return clock ? clock->getBPM() : 120.0f;
}

// Parameter range conversion helpers
// These use actual parameter ranges from getAvailableParameters() dynamically
//--------------------------------------------------------------
std::pair<float, float> TrackerSequencer::getParameterRange(const std::string& paramName) {
    // Check tracker parameters first (index, note, length, chance)
    auto trackerParam = getTrackerParameterDescriptor(paramName);
    if (!trackerParam.name.empty()) {
        return std::make_pair(trackerParam.minValue, trackerParam.maxValue);
    }
    
    // Check default parameters (for backward compatibility)
    auto defaultParams = getDefaultParameters();
    for (const auto& param : defaultParams) {
        if (param.name == paramName) {
            return std::make_pair(param.minValue, param.maxValue);
        }
    }
    
    // Default range for unknown parameters
    return std::make_pair(0.0f, 1.0f);
}

// Static helper to get default value
float TrackerSequencer::getParameterDefault(const std::string& paramName) {
    // Check tracker parameters first (index, note, length, chance)
    auto trackerParam = getTrackerParameterDescriptor(paramName);
    if (!trackerParam.name.empty()) {
        return trackerParam.defaultValue;
    }
    
    // Check default parameters (for backward compatibility)
    auto defaultParams = getDefaultParameters();
    for (const auto& param : defaultParams) {
        if (param.name == paramName) {
            return param.defaultValue;
        }
    }
    
    // Fallback default
    return 0.0f;
}

// Get parameter type dynamically from static helpers
ParameterType TrackerSequencer::getParameterType(const std::string& paramName) {
    // Check tracker parameters first (index, note, length, chance)
    auto trackerParam = getTrackerParameterDescriptor(paramName);
    if (!trackerParam.name.empty()) {
        return trackerParam.type;
    }
    
    // Check default parameters (for backward compatibility)
    auto defaultParams = getDefaultParameters();
    for (const auto& param : defaultParams) {
        if (param.name == paramName) {
            return param.type;
        }
    }
    
    // Default to FLOAT for unknown parameters
    return ParameterType::FLOAT;
}

// MODULAR: Format parameter value based on parameter type, not hardcoded names
std::string TrackerSequencer::formatParameterValue(const std::string& paramName, float value) {
    ParameterType type = getParameterType(paramName);
    char buf[16];
    
    if (type == ParameterType::INT) {
        // Integer parameters: no decimal places
        snprintf(buf, sizeof(buf), "%d", (int)std::round(value));
    } else {
        // Float parameters: 3 decimal places (0.001 precision) - unified for all float params
        snprintf(buf, sizeof(buf), "%.3f", value);
    }
    
    return std::string(buf);
}

void TrackerSequencer::update() {
    // Update step active state (clears manually triggered steps when duration expires)
    updateStepActiveState();
}

void TrackerSequencer::updateStepActiveState() {
    // Check if current step duration has expired (works for both manual and playback)
    // PERFORMANCE: Early return checks BEFORE expensive system call
    if (playbackState.currentPlayingStep >= 0 && playbackState.stepEndTime > 0.0f) {
        float currentTime = ofGetElapsedTimef();
        if (currentTime >= playbackState.stepEndTime) {
            // Step duration expired - clear playing state
            playbackState.clearPlayingStep();
        }
    }
}

// Multi-pattern support implementation
//--------------------------------------------------------------
// Phase 3: Pattern management methods updated for Runtime-only architecture
void TrackerSequencer::setCurrentPatternName(const std::string& patternName) {
    if (patternRuntime_ && patternRuntime_->patternExists(patternName)) {
        bindToPattern(patternName);
        ofLogNotice("TrackerSequencer") << "Switched to pattern: " << patternName;
    } else {
        ofLogWarning("TrackerSequencer") << "Invalid pattern name: " << patternName;
    }
}

int TrackerSequencer::getNumPatterns() const {
    if (patternRuntime_) {
        return (int)patternRuntime_->getPatternNames().size();
    }
    return 0;
}

std::vector<std::string> TrackerSequencer::getAllPatternNames() const {
    if (patternRuntime_) {
        return patternRuntime_->getPatternNames();
    }
    return std::vector<std::string>();
}

// Index-based methods (for GUI compatibility)
int TrackerSequencer::addPattern() {
    std::string patternName = addPatternByName();  // Call name-based helper
    if (patternName.empty()) {
        return -1;
    }
    // Return index of newly added pattern
    auto patternNames = patternRuntime_->getPatternNames();
    for (size_t i = 0; i < patternNames.size(); ++i) {
        if (patternNames[i] == patternName) {
            return (int)i;
        }
    }
    return -1;
}

void TrackerSequencer::removePattern(int index) {
    if (!patternRuntime_) {
        ofLogError("TrackerSequencer") << "PatternRuntime not available";
        return;
    }
    auto patternNames = patternRuntime_->getPatternNames();
    if (index >= 0 && index < (int)patternNames.size()) {
        removePatternByName(patternNames[index]);  // Call name-based helper
    } else {
        ofLogWarning("TrackerSequencer") << "Invalid pattern index: " << index;
    }
}

void TrackerSequencer::copyPattern(int sourceIndex, int destIndex) {
    if (!patternRuntime_) {
        ofLogError("TrackerSequencer") << "PatternRuntime not available";
        return;
    }
    auto patternNames = patternRuntime_->getPatternNames();
    if (sourceIndex >= 0 && sourceIndex < (int)patternNames.size() &&
        destIndex >= 0 && destIndex < (int)patternNames.size()) {
        copyPatternByName(patternNames[sourceIndex], patternNames[destIndex]);  // Call name-based helper
    } else {
        ofLogWarning("TrackerSequencer") << "Invalid pattern indices: " << sourceIndex << ", " << destIndex;
    }
}

void TrackerSequencer::duplicatePattern(int index) {
    if (!patternRuntime_) {
        ofLogError("TrackerSequencer") << "PatternRuntime not available";
        return;
    }
    auto patternNames = patternRuntime_->getPatternNames();
    if (index >= 0 && index < (int)patternNames.size()) {
        duplicatePatternByName(patternNames[index]);  // Call name-based helper
    } else {
        ofLogWarning("TrackerSequencer") << "Invalid pattern index: " << index;
    }
}

// Name-based methods (internal implementation)
std::string TrackerSequencer::addPatternByName() {
    if (!patternRuntime_) {
        ofLogError("TrackerSequencer") << "PatternRuntime not available";
        return "";
    }
    
    // New pattern uses same step count and stepsPerBeat as current pattern
    int stepCount = getCurrentPattern().getStepCount();
    float currentSPB = getCurrentPattern().getStepsPerBeat();
    Pattern newPattern(stepCount);
    newPattern.setStepsPerBeat(currentSPB);
    
    // Use simple pattern naming (P0, P1, P2, etc.) - let PatternRuntime generate the name
    std::string patternName = patternRuntime_->addPattern(newPattern, "");
    ofLogNotice("TrackerSequencer") << "Added new pattern '" << patternName << "' with " << stepCount << " steps, SPB=" << currentSPB;
    return patternName;
}

void TrackerSequencer::removePatternByName(const std::string& patternName) {
    if (!patternRuntime_) {
        ofLogError("TrackerSequencer") << "PatternRuntime not available";
        return;
    }
    
    if (!patternRuntime_->patternExists(patternName)) {
        ofLogWarning("TrackerSequencer") << "Pattern not found: " << patternName;
        return;
    }
    
    // Don't remove if it's the only pattern
    if (getNumPatterns() <= 1) {
        ofLogWarning("TrackerSequencer") << "Cannot remove pattern: must have at least one pattern";
        return;
    }
    
    // If removing bound pattern, bind to another pattern first
    if (boundPatternName_ == patternName) {
        auto patternNames = patternRuntime_->getPatternNames();
        for (const auto& name : patternNames) {
            if (name != patternName) {
                bindToPattern(name);
                break;
            }
        }
    }
    
    patternRuntime_->removePattern(patternName);
    ofLogNotice("TrackerSequencer") << "Removed pattern: " << patternName;
}

void TrackerSequencer::copyPatternByName(const std::string& sourceName, const std::string& destName) {
    if (!patternRuntime_) {
        ofLogError("TrackerSequencer") << "PatternRuntime not available";
        return;
    }
    
    const Pattern* sourcePattern = patternRuntime_->getPattern(sourceName);
    if (!sourcePattern) {
        ofLogWarning("TrackerSequencer") << "Source pattern not found: " << sourceName;
        return;
    }
    
    if (!patternRuntime_->patternExists(destName)) {
        ofLogWarning("TrackerSequencer") << "Destination pattern not found: " << destName;
        return;
    }
    
    patternRuntime_->updatePattern(destName, *sourcePattern);
    ofLogNotice("TrackerSequencer") << "Copied pattern '" << sourceName << "' to '" << destName << "'";
}

std::string TrackerSequencer::duplicatePatternByName(const std::string& patternName) {
    if (!patternRuntime_) {
        ofLogError("TrackerSequencer") << "PatternRuntime not available";
        return "";
    }
    
    const Pattern* sourcePattern = patternRuntime_->getPattern(patternName);
    if (!sourcePattern) {
        ofLogWarning("TrackerSequencer") << "Pattern not found: " << patternName;
        return "";
    }
    
    // Use simple pattern naming for duplicates (P0, P1, P2, etc.) - let PatternRuntime generate the name
    std::string newName = patternRuntime_->addPattern(*sourcePattern, "");
    ofLogNotice("TrackerSequencer") << "Duplicated pattern '" << patternName << "' to '" << newName << "'";
    return newName;
}

// Helper: Get pattern name by index (for PatternChain compatibility)
std::string TrackerSequencer::getPatternNameByIndex(int index) const {
    if (!patternRuntime_) {
        return "";
    }
    auto patternNames = patternRuntime_->getPatternNames();
    if (index >= 0 && index < (int)patternNames.size()) {
        return patternNames[index];
    }
    return "";
}

// DEPRECATED: Index-based compatibility methods (kept for backward compatibility, use name-based methods instead)
int TrackerSequencer::getCurrentPatternIndex() const {
    if (!patternRuntime_ || boundPatternName_.empty()) {
        return 0;
    }
    auto patternNames = patternRuntime_->getPatternNames();
    for (size_t i = 0; i < patternNames.size(); ++i) {
        if (patternNames[i] == boundPatternName_) {
            return (int)i;
        }
    }
    return 0;
}

void TrackerSequencer::setCurrentPatternIndex(int index) {
    if (!patternRuntime_) {
        ofLogError("TrackerSequencer") << "PatternRuntime not available";
        return;
    }
    auto patternNames = patternRuntime_->getPatternNames();
    if (index >= 0 && index < (int)patternNames.size()) {
        bindToPattern(patternNames[index]);
        ofLogNotice("TrackerSequencer") << "Switched to pattern index " << index << " (" << patternNames[index] << ")";
    } else {
        ofLogWarning("TrackerSequencer") << "Invalid pattern index: " << index;
    }
}

// Pattern chain (pattern chaining) implementation
// Phase 2: Chains are now in PatternRuntime, accessed via sequencer bindings
//--------------------------------------------------------------

// Helper to get current chain (from PatternRuntime or fallback to internal)
PatternChain* TrackerSequencer::getCurrentChain() {
    
    
    // CRITICAL: Check PatternRuntime binding first (source of truth)
    // This ensures we use the correct chain even if boundChainName_ is out of sync
    // Instance name should now be properly set by ModuleRegistry::registerModule()
    std::string instanceName = getInstanceName();
    if (patternRuntime_ && !instanceName.empty() && instanceName != getName()) {
        PatternRuntime::SequencerBinding binding = patternRuntime_->getSequencerBinding(instanceName);
        if (!binding.chainName.empty()) {
            PatternChain* chain = patternRuntime_->getChain(binding.chainName);
            if (chain) {
                // Sync boundChainName_ if it's different (for GUI consistency)
                if (boundChainName_ != binding.chainName) {
                    boundChainName_ = binding.chainName;
                    ofLogVerbose("TrackerSequencer") << "Synced boundChainName_ from PatternRuntime: '" << binding.chainName << "'";
                }
                
                return chain;
            }
        }
    }
    
    // Fallback: Use boundChainName_ if PatternRuntime binding is empty
    if (patternRuntime_ && !boundChainName_.empty()) {
        PatternChain* chain = patternRuntime_->getChain(boundChainName_);
        
        if (chain) {
            return chain;
        }
    }
    
    // Fallback to internal chain for backward compatibility
    // NOTE: Each TrackerSequencer instance has its own patternChain member, so this is instance-specific
    return &patternChain;
}

const PatternChain* TrackerSequencer::getCurrentChain() const {
    // CRITICAL: Check PatternRuntime binding first (source of truth)
    // This ensures we use the correct chain even if boundChainName_ is out of sync
    // Instance name should now be properly set by ModuleRegistry::registerModule()
    std::string instanceName = getInstanceName();
    if (patternRuntime_ && !instanceName.empty() && instanceName != getName()) {
        PatternRuntime::SequencerBinding binding = patternRuntime_->getSequencerBinding(instanceName);
        if (!binding.chainName.empty()) {
            const PatternChain* chain = patternRuntime_->getChain(binding.chainName);
            if (chain) {
                return chain;
            }
        }
    }
    
    // Fallback: Use boundChainName_ if PatternRuntime binding is empty
    if (patternRuntime_ && !boundChainName_.empty()) {
        const PatternChain* chain = patternRuntime_->getChain(boundChainName_);
        if (chain) {
            return chain;
        }
    }
    // Fallback to internal chain for backward compatibility
    // NOTE: Each TrackerSequencer instance has its own patternChain member, so this is instance-specific
    return &patternChain;
}

void TrackerSequencer::bindToChain(const std::string& chainName) {
    
    if (!patternRuntime_) {
        ofLogError("TrackerSequencer") << "PatternRuntime not available for chain binding";
        return;
    }
    
    if (!patternRuntime_->chainExists(chainName)) {
        ofLogError("TrackerSequencer") << "Chain not found in Runtime: " << chainName;
        return;
    }
    
    boundChainName_ = chainName;
    
    // Update PatternRuntime sequencer binding
    // Use getInstanceName() instead of getName() to get instance name (e.g., "trackerSequencer1") not type name
    patternRuntime_->bindSequencerChain(getInstanceName(), chainName);
    
    // Sync chain enabled state
    PatternChain* chain = patternRuntime_->getChain(chainName);
    if (chain) {
        patternRuntime_->setSequencerChainEnabled(getInstanceName(), chain->isEnabled());
        
        // CRITICAL: Sync chain's current index with bound pattern
        // This ensures chain progression starts from the correct position
        if (!boundPatternName_.empty()) {
            const auto& chainPatterns = chain->getChain();
            for (size_t i = 0; i < chainPatterns.size(); ++i) {
                if (chainPatterns[i] == boundPatternName_) {
                    chain->setCurrentIndex((int)i);
                    ofLogVerbose("TrackerSequencer") << "Synced chain index to " << i << " for pattern '" << boundPatternName_ << "'";
                    break;
                }
            }
        }
    }
    
    ofLogNotice("TrackerSequencer") << "Bound to chain: " << chainName;
}

void TrackerSequencer::unbindChain() {
    if (!boundChainName_.empty()) {
        if (patternRuntime_) {
            patternRuntime_->unbindSequencerChain(getInstanceName());
        }
        boundChainName_.clear();
        ofLogNotice("TrackerSequencer") << "Unbound from chain";
    }
}

int TrackerSequencer::getPatternChainSize() const {
    const PatternChain* chain = getCurrentChain();
    return chain ? chain->getSize() : 0;
}

int TrackerSequencer::getCurrentChainIndex() const {
    const PatternChain* chain = getCurrentChain();
    return chain ? chain->getCurrentIndex() : 0;
}

void TrackerSequencer::setCurrentChainIndex(int index) {
    PatternChain* chain = getCurrentChain();
    if (!chain) return;
    
    chain->setCurrentIndex(index);
    
    // Update bound pattern based on pattern chain
    if (chain->isEnabled()) {
        std::string patternName = chain->getEntry(index);
        if (!patternName.empty()) {
            bindToPattern(patternName);
        }
    }
}

void TrackerSequencer::addToPatternChain(const std::string& patternName) {
    if (!patternRuntime_) {
        ofLogError("TrackerSequencer") << "PatternRuntime not available";
        return;
    }
    if (!patternRuntime_->patternExists(patternName)) {
        ofLogWarning("TrackerSequencer") << "Invalid pattern name for chain: '" << patternName << "'";
        return;
    }
    
    // Phase 2: Auto-migrate to PatternRuntime if not already bound
    // This ensures chains created/modified via GUI are immediately stored in PatternRuntime
    // Only migrate if we have existing chain entries OR this is the first pattern being added
    if (boundChainName_.empty()) {
        // Create a chain in PatternRuntime for this sequencer
        std::string chainName = getInstanceName() + "_chain";
        if (!patternRuntime_->chainExists(chainName)) {
            patternRuntime_->addChain(chainName);
        }
        
        // Migrate existing internal chain entries if any
        if (patternChain.getSize() > 0) {
            const auto& chain = patternChain.getChain();
            for (size_t i = 0; i < chain.size(); i++) {
                patternRuntime_->chainAddPattern(chainName, chain[i]);
                int repeatCount = patternChain.getRepeatCount((int)i);
                if (repeatCount > 1) {
                    patternRuntime_->chainSetRepeat(chainName, (int)i, repeatCount);
                }
                if (patternChain.isEntryDisabled((int)i)) {
                    patternRuntime_->chainSetEntryDisabled(chainName, (int)i, true);
                }
            }
            // Set chain enabled state (always enable by default)
            patternRuntime_->chainSetEnabled(chainName, true);
            // Sync current index
            PatternChain* runtimeChain = patternRuntime_->getChain(chainName);
            if (runtimeChain) {
                runtimeChain->setCurrentIndex(patternChain.getCurrentIndex());
            }
        } else {
            // No existing chain entries - just ensure chain is enabled (will be populated by this add call)
            patternRuntime_->chainSetEnabled(chainName, true);
        }
        
        // Bind sequencer to the chain
        bindToChain(chainName);
        patternRuntime_->setSequencerChainEnabled(getInstanceName(), true);
        ofLogVerbose("TrackerSequencer") << "Auto-migrated chain to PatternRuntime: " << chainName;
    }
    
    // Now add the new pattern (chain is guaranteed to be bound at this point)
    patternRuntime_->chainAddPattern(boundChainName_, patternName);
    
    // Ensure chain is enabled after adding pattern (chains are enabled by default)
    PatternChain* chain = patternRuntime_->getChain(boundChainName_);
    if (chain && !chain->isEnabled()) {
        patternRuntime_->chainSetEnabled(boundChainName_, true);
        patternRuntime_->setSequencerChainEnabled(getInstanceName(), true);
    }
}

void TrackerSequencer::removeFromPatternChain(int chainIndex) {
    PatternChain* chain = getCurrentChain();
    if (!chain) return;
    
    // CRITICAL: Check if we're removing the currently bound pattern
    // If so, we need to switch to a new pattern from the chain
    bool wasCurrentPattern = false;
    std::string removedPatternName;
    if (chainIndex >= 0 && chainIndex < chain->getSize()) {
        removedPatternName = chain->getEntry(chainIndex);
        wasCurrentPattern = (removedPatternName == boundPatternName_);
    }
    
    // If using PatternRuntime chain, use PatternRuntime API
    if (patternRuntime_ && !boundChainName_.empty()) {
        patternRuntime_->chainRemovePattern(boundChainName_, chainIndex);
    } else {
        // Fallback to internal chain
        chain->removeEntry(chainIndex);
    }
    
    // CRITICAL FIX: Only rebind if we removed the current pattern AND chain has valid entries
    // This prevents stopping patterns that are still bound but just removed from one chain
    if (wasCurrentPattern) {
        int newCurrentIndex = chain->getCurrentIndex();
        if (newCurrentIndex >= 0 && newCurrentIndex < chain->getSize()) {
            std::string newPatternName = chain->getEntry(newCurrentIndex);
            if (!newPatternName.empty() && newPatternName != removedPatternName) {
                // Only rebind if we have a valid new pattern (different from removed one)
                bindToPattern(newPatternName);
            }
        }
        // If chain is now empty or no valid new pattern, keep current binding
        // Don't unbind - the pattern might still be valid for this sequencer
    }
    // If we didn't remove the current pattern, don't change binding at all
    // This preserves playback when editing chains that don't affect current pattern
}

void TrackerSequencer::clearPatternChain() {
    PatternChain* chain = getCurrentChain();
    if (!chain) return;
    
    // If using PatternRuntime chain, use PatternRuntime API
    if (patternRuntime_ && !boundChainName_.empty()) {
        patternRuntime_->chainClear(boundChainName_);
    } else {
        // Fallback to internal chain
        chain->clear();
    }
}

std::string TrackerSequencer::getPatternChainEntry(int chainIndex) const {
    const PatternChain* chain = getCurrentChain();
    return chain ? chain->getEntry(chainIndex) : "";
}

void TrackerSequencer::setPatternChainEntry(int chainIndex, const std::string& patternName) {
    if (!patternRuntime_) {
        ofLogError("TrackerSequencer") << "PatternRuntime not available";
        return;
    }
    if (!patternRuntime_->patternExists(patternName)) {
        ofLogWarning("TrackerSequencer") << "Invalid pattern name: '" << patternName << "'";
        return;
    }
    
    // Phase 2: Auto-migrate to PatternRuntime if not already bound
    if (boundChainName_.empty() && patternChain.getSize() > 0) {
        // Create a chain in PatternRuntime for this sequencer
        std::string chainName = getInstanceName() + "_chain";
        if (!patternRuntime_->chainExists(chainName)) {
            patternRuntime_->addChain(chainName);
        }
        
        // Migrate existing internal chain entries
        const auto& chain = patternChain.getChain();
        for (size_t i = 0; i < chain.size(); i++) {
            patternRuntime_->chainAddPattern(chainName, chain[i]);
            int repeatCount = patternChain.getRepeatCount((int)i);
            if (repeatCount > 1) {
                patternRuntime_->chainSetRepeat(chainName, (int)i, repeatCount);
            }
            if (patternChain.isEntryDisabled((int)i)) {
                patternRuntime_->chainSetEntryDisabled(chainName, (int)i, true);
            }
        }
        // Set chain enabled state
        patternRuntime_->chainSetEnabled(chainName, patternChain.isEnabled());
        // Sync current index
        PatternChain* runtimeChain = patternRuntime_->getChain(chainName);
        if (runtimeChain) {
            runtimeChain->setCurrentIndex(patternChain.getCurrentIndex());
        }
        
        // Bind sequencer to the chain
        bindToChain(chainName);
        ofLogVerbose("TrackerSequencer") << "Auto-migrated chain to PatternRuntime: " << chainName;
    }
    
    PatternChain* chain = getCurrentChain();
    if (!chain) return;
    
    // CRITICAL FIX: Use chainSetEntry() instead of chainClear() + rebuild
    // This preserves chain state (enabled, currentIndex, currentRepeat) during playback
    if (patternRuntime_ && !boundChainName_.empty()) {
        // Direct entry update - preserves chain state
        auto patterns = patternRuntime_->chainGetPatterns(boundChainName_);
        if (chainIndex >= 0 && chainIndex < (int)patterns.size()) {
            // Update existing entry - preserves chain state
            patternRuntime_->chainSetEntry(boundChainName_, chainIndex, patternName);
        } else {
            // Index out of bounds - add to end (doesn't affect current playback)
            patternRuntime_->chainAddPattern(boundChainName_, patternName);
        }
    } else {
        // Fallback to internal chain (should not happen after full migration)
        chain->setEntry(chainIndex, patternName);
    }
}

const std::vector<std::string>& TrackerSequencer::getPatternChain() const {
    
    const PatternChain* chain = getCurrentChain();
    
    if (chain) {
        return chain->getChain();
    }
    // Fallback: return empty vector
    static const std::vector<std::string> empty;
    return empty;
}

int TrackerSequencer::getPatternChainRepeatCount(int chainIndex) const {
    const PatternChain* chain = getCurrentChain();
    return chain ? chain->getRepeatCount(chainIndex) : 1;
}

void TrackerSequencer::setPatternChainRepeatCount(int chainIndex, int repeatCount) {
    // Phase 2: Auto-migrate to PatternRuntime if not already bound and chain has entries
    if (patternRuntime_ && boundChainName_.empty() && patternChain.getSize() > 0) {
        // Create a chain in PatternRuntime for this sequencer
        std::string chainName = getInstanceName() + "_chain";
        if (!patternRuntime_->chainExists(chainName)) {
            patternRuntime_->addChain(chainName);
        }
        
        // Migrate existing internal chain entries
        const auto& chain = patternChain.getChain();
        for (size_t i = 0; i < chain.size(); i++) {
            patternRuntime_->chainAddPattern(chainName, chain[i]);
            int oldRepeatCount = patternChain.getRepeatCount((int)i);
            if (oldRepeatCount > 1) {
                patternRuntime_->chainSetRepeat(chainName, (int)i, oldRepeatCount);
            }
            if (patternChain.isEntryDisabled((int)i)) {
                patternRuntime_->chainSetEntryDisabled(chainName, (int)i, true);
            }
        }
        // Set chain enabled state
        patternRuntime_->chainSetEnabled(chainName, patternChain.isEnabled());
        // Sync current index
        PatternChain* runtimeChain = patternRuntime_->getChain(chainName);
        if (runtimeChain) {
            runtimeChain->setCurrentIndex(patternChain.getCurrentIndex());
        }
        
        // Bind sequencer to the chain
        bindToChain(chainName);
        ofLogVerbose("TrackerSequencer") << "Auto-migrated chain to PatternRuntime: " << chainName;
    }
    
    PatternChain* chain = getCurrentChain();
    if (!chain) return;
    
    // If using PatternRuntime chain, use PatternRuntime API
    if (patternRuntime_ && !boundChainName_.empty()) {
        patternRuntime_->chainSetRepeat(boundChainName_, chainIndex, repeatCount);
    } else {
        // Fallback to internal chain
        chain->setRepeatCount(chainIndex, repeatCount);
    }
}

bool TrackerSequencer::getUsePatternChain() const {
    const PatternChain* chain = getCurrentChain();
    bool result = chain ? chain->isEnabled() : false;
    
    return result;
}

void TrackerSequencer::setUsePatternChain(bool use) {
    // Phase 2: Auto-migrate to PatternRuntime if not already bound and chain has entries
    if (patternRuntime_ && boundChainName_.empty() && patternChain.getSize() > 0) {
        // Create a chain in PatternRuntime for this sequencer
        std::string chainName = getInstanceName() + "_chain";
        if (!patternRuntime_->chainExists(chainName)) {
            patternRuntime_->addChain(chainName);
        }
        
        // Migrate existing internal chain entries
        const auto& chain = patternChain.getChain();
        for (size_t i = 0; i < chain.size(); i++) {
            patternRuntime_->chainAddPattern(chainName, chain[i]);
            int repeatCount = patternChain.getRepeatCount((int)i);
            if (repeatCount > 1) {
                patternRuntime_->chainSetRepeat(chainName, (int)i, repeatCount);
            }
            if (patternChain.isEntryDisabled((int)i)) {
                patternRuntime_->chainSetEntryDisabled(chainName, (int)i, true);
            }
        }
        // Set chain enabled state
        patternRuntime_->chainSetEnabled(chainName, patternChain.isEnabled());
        // Sync current index
        PatternChain* runtimeChain = patternRuntime_->getChain(chainName);
        if (runtimeChain) {
            runtimeChain->setCurrentIndex(patternChain.getCurrentIndex());
        }
        
        // Bind sequencer to the chain
        bindToChain(chainName);
        ofLogVerbose("TrackerSequencer") << "Auto-migrated chain to PatternRuntime: " << chainName;
    }
    
    PatternChain* chain = getCurrentChain();
    if (!chain) return;
    
    // If using PatternRuntime chain, use PatternRuntime API
    if (patternRuntime_ && !boundChainName_.empty()) {
        patternRuntime_->chainSetEnabled(boundChainName_, use);
        patternRuntime_->setSequencerChainEnabled(getInstanceName(), use);
    } else {
        // Fallback to internal chain
        chain->setEnabled(use);
    }
}

bool TrackerSequencer::isPatternChainEntryDisabled(int chainIndex) const {
    const PatternChain* chain = getCurrentChain();
    return chain ? chain->isEntryDisabled(chainIndex) : false;
}

void TrackerSequencer::setPatternChainEntryDisabled(int chainIndex, bool disabled) {
    // Phase 2: Auto-migrate to PatternRuntime if not already bound and chain has entries
    if (patternRuntime_ && boundChainName_.empty() && patternChain.getSize() > 0) {
        // Create a chain in PatternRuntime for this sequencer
        std::string chainName = getInstanceName() + "_chain";
        if (!patternRuntime_->chainExists(chainName)) {
            patternRuntime_->addChain(chainName);
        }
        
        // Migrate existing internal chain entries
        const auto& chain = patternChain.getChain();
        for (size_t i = 0; i < chain.size(); i++) {
            patternRuntime_->chainAddPattern(chainName, chain[i]);
            int repeatCount = patternChain.getRepeatCount((int)i);
            if (repeatCount > 1) {
                patternRuntime_->chainSetRepeat(chainName, (int)i, repeatCount);
            }
            if (patternChain.isEntryDisabled((int)i)) {
                patternRuntime_->chainSetEntryDisabled(chainName, (int)i, true);
            }
        }
        // Set chain enabled state
        patternRuntime_->chainSetEnabled(chainName, patternChain.isEnabled());
        // Sync current index
        PatternChain* runtimeChain = patternRuntime_->getChain(chainName);
        if (runtimeChain) {
            runtimeChain->setCurrentIndex(patternChain.getCurrentIndex());
        }
        
        // Bind sequencer to the chain
        bindToChain(chainName);
        ofLogVerbose("TrackerSequencer") << "Auto-migrated chain to PatternRuntime: " << chainName;
    }
    
    PatternChain* chain = getCurrentChain();
    if (!chain) return;
    
    // If using PatternRuntime chain, use PatternRuntime API
    if (patternRuntime_ && !boundChainName_.empty()) {
        patternRuntime_->chainSetEntryDisabled(boundChainName_, chainIndex, disabled);
    } else {
        // Fallback to internal chain
        chain->setEntryDisabled(chainIndex, disabled);
    }
}

// CellWidget adapter methods removed - moved to TrackerSequencerGUI
// Use TrackerSequencerGUI::createParameterCellForColumn() instead

//--------------------------------------------------------------
/// MARK: - Ports 
/// Port-based routing interface 
std::vector<Port> TrackerSequencer::getInputPorts() const {
    // TrackerSequencer doesn't have inputs ports (for now)
    return {};
}

std::vector<Port> TrackerSequencer::getOutputPorts() const {
    return {
        Port("trigger_out", PortType::EVENT_OUT, false, "Trigger Event Output",
             const_cast<void*>(static_cast<const void*>(&triggerEvent)))
    };
}

//--------------------------------------------------------------
// Module Factory Registration
//--------------------------------------------------------------
// Auto-register TrackerSequencer with ModuleFactory on static initialization
// This enables true modularity - no hardcoded dependencies in ModuleFactory
namespace {
    struct TrackerSequencerRegistrar {
        TrackerSequencerRegistrar() {
            ModuleFactory::registerModuleType("TrackerSequencer", 
                []() -> std::shared_ptr<Module> {
                    return std::make_shared<TrackerSequencer>();
                });
        }
    };
    static TrackerSequencerRegistrar g_trackerSequencerRegistrar;
}

// ═══════════════════════════════════════════════════════════
// PATTERN RUNTIME INTEGRATION (Phase 3: Runtime-Only Architecture)
// ═══════════════════════════════════════════════════════════

bool TrackerSequencer::bindToPattern(const std::string& patternName) {
    if (!patternRuntime_) {
        ofLogError("TrackerSequencer") << "PatternRuntime not available for binding";
        return false;
    }
    
    if (!patternRuntime_->patternExists(patternName)) {
        ofLogError("TrackerSequencer") << "Pattern not found in Runtime: " << patternName;
        return false;
    }
    
    // CRITICAL FIX: Don't stop old pattern here - let bindSequencerPattern() handle it
    // bindSequencerPattern() checks if pattern is still bound to other sequencers before stopping
    // This prevents stopping patterns that are still in use by other sequencers
    
    // Unsubscribe from previous pattern events
    if (!boundPatternName_.empty()) {
        ofRemoveListener(patternRuntime_->triggerEvent, this, &TrackerSequencer::onPatternRuntimeTrigger);
    }
    
    boundPatternName_ = patternName;
    
    // CRITICAL: Update PatternRuntime sequencer binding so it's persisted
    // This ensures the binding is saved/restored correctly
    // bindSequencerPattern() will handle stopping old pattern only if it's not bound elsewhere
    // Use getInstanceName() instead of getName() to get instance name (e.g., "trackerSequencer1") not type name
    patternRuntime_->bindSequencerPattern(getInstanceName(), patternName);
    
    // Subscribe to PatternRuntime events for forwarding
    ofAddListener(patternRuntime_->triggerEvent, this, &TrackerSequencer::onPatternRuntimeTrigger);
    
    // CRITICAL: If clock is already playing, start the new pattern immediately
    // This ensures pattern changes take effect right away
    if (clock && clock->isPlaying()) {
        patternRuntime_->playPattern(patternName);
        // Sync local playback state FROM PatternRuntime
        PatternPlaybackState* state = patternRuntime_->getPlaybackState(patternName);
        if (state) {
            playbackState.playbackStep = state->playbackStep;
            playbackState.currentPlayingStep = state->currentPlayingStep;
            playbackState.patternCycleCount = state->patternCycleCount;
        }
        ofLogVerbose("TrackerSequencer") << "Started pattern '" << patternName << "' (clock already playing)";
    }
    
    // CRITICAL: Sync pattern chain current index with bound pattern
    // If pattern chain is enabled and contains this pattern, update current index
    PatternChain* chain = getCurrentChain();
    if (chain && chain->isEnabled()) {
        const auto& chainPatterns = chain->getChain();
        for (size_t i = 0; i < chainPatterns.size(); ++i) {
            if (chainPatterns[i] == patternName) {
                chain->setCurrentIndex((int)i);
                ofLogVerbose("TrackerSequencer") << "Synced chain index to " << i << " for pattern '" << patternName << "'";
                break;
            }
        }
    }
    
    ofLogNotice("TrackerSequencer") << "Bound to pattern: " << patternName;
    return true;
}

void TrackerSequencer::onPatternRuntimeTrigger(TriggerEvent& evt) {
    // Forward PatternRuntime events to TrackerSequencer's triggerEvent
    // This preserves module-based routing (TrackerSequencer → MultiSampler)
    // Only forward events for the bound pattern
    // Phase 3: Forward events from bound pattern only
    if (!boundPatternName_.empty() && evt.patternName == boundPatternName_) {
        // Forward the event (preserves all metadata including patternName)
        ofNotifyEvent(triggerEvent, evt);
    }
}

void TrackerSequencer::onSequencerBindingChanged(std::string& sequencerName) {
    
    
    // CRITICAL FIX: Use getInstanceName() instead of getName() to match sequencer instance names
    // Command executor uses instance names (e.g., "trackerSequencer1"), not type names
    if (sequencerName != getInstanceName() || !patternRuntime_) {
        
        return;
    }
    
    
    
    // Immediately sync bindings from PatternRuntime
    PatternRuntime::SequencerBinding binding = patternRuntime_->getSequencerBinding(getInstanceName());
    
    
    
    // Sync pattern binding
    if (!binding.patternName.empty() && binding.patternName != boundPatternName_) {
        std::string oldPattern = boundPatternName_;
        boundPatternName_ = binding.patternName;
        
        // Unsubscribe from old pattern events
        if (!oldPattern.empty()) {
            ofRemoveListener(patternRuntime_->triggerEvent, this, &TrackerSequencer::onPatternRuntimeTrigger);
        }
        
        // Subscribe to new pattern events
        ofAddListener(patternRuntime_->triggerEvent, this, &TrackerSequencer::onPatternRuntimeTrigger);
        
        // CRITICAL FIX: Start pattern if transport is playing (same logic as bindToPattern)
        // Note: PatternRuntime now unlocks before notifying, so we can safely call playPattern() here
        if (clock && clock->isPlaying()) {
            
            
            patternRuntime_->playPattern(boundPatternName_);
            
            
            
            // Sync local playback state FROM PatternRuntime
            PatternPlaybackState* state = patternRuntime_->getPlaybackState(boundPatternName_);
            
            
            
            if (state) {
                playbackState.playbackStep = state->playbackStep;
                playbackState.currentPlayingStep = state->currentPlayingStep;
                playbackState.patternCycleCount = state->patternCycleCount;
                
                
            }
            ofLogVerbose("TrackerSequencer") << "Started pattern '" << boundPatternName_ << "' (clock already playing)";
        }
        
        ofLogVerbose("TrackerSequencer") << "Synced pattern binding from PatternRuntime: '" 
                                        << oldPattern << "' -> '" << boundPatternName_ << "'";
    } else if (binding.patternName.empty() && !boundPatternName_.empty()) {
        // Pattern was unbound
        std::string oldPattern = boundPatternName_;
        boundPatternName_.clear();
        
        // Unsubscribe from pattern events
        ofRemoveListener(patternRuntime_->triggerEvent, this, &TrackerSequencer::onPatternRuntimeTrigger);
        
        ofLogVerbose("TrackerSequencer") << "Synced pattern unbinding from PatternRuntime: '" 
                                        << oldPattern << "' -> [unbound]";
    }
    
    
    
    // Sync chain binding
    if (binding.chainName != boundChainName_) {
        std::string oldChain = boundChainName_;
        boundChainName_ = binding.chainName;
        
        
        
        if (!boundChainName_.empty()) {
            ofLogVerbose("TrackerSequencer") << "Synced chain binding from PatternRuntime: '" 
                                            << oldChain << "' -> '" << boundChainName_ << "'";
        } else {
            ofLogVerbose("TrackerSequencer") << "Synced chain unbinding from PatternRuntime: '" 
                                            << oldChain << "' -> [unbound]";
        }
    }
    
    
}

void TrackerSequencer::onPatternDeleted(std::string& deletedPatternName) {
    // Handle pattern deletion: rebind if needed and clean up pattern chain
    if (!patternRuntime_) {
        return;
    }
    
    // If bound to deleted pattern, rebind to another pattern
    if (boundPatternName_ == deletedPatternName) {
        auto patternNames = patternRuntime_->getPatternNames();
        if (!patternNames.empty()) {
            // Bind to first available pattern
            bindToPattern(patternNames[0]);
            ofLogNotice("TrackerSequencer") << "Rebound to pattern '" << patternNames[0] 
                                            << "' after deletion of bound pattern '" << deletedPatternName << "'";
        } else {
            // No patterns left - create a default one
            Pattern defaultPattern(16);
            std::string newName = patternRuntime_->addPattern(defaultPattern, "");
            if (!newName.empty()) {
                bindToPattern(newName);
                ofLogNotice("TrackerSequencer") << "Created and bound to default pattern '" << newName 
                                                << "' after deletion of bound pattern '" << deletedPatternName << "'";
            } else {
                boundPatternName_.clear();
                ofLogWarning("TrackerSequencer") << "Failed to create default pattern after deletion";
            }
        }
    }
    
    // Remove deleted pattern from pattern chain
    PatternChain* chain = getCurrentChain();
    if (chain) {
        const auto& chainPatterns = chain->getChain();
        bool chainModified = false;
        for (int i = (int)chainPatterns.size() - 1; i >= 0; --i) {
            if (chainPatterns[i] == deletedPatternName) {
                // If using PatternRuntime chain, use PatternRuntime API
                if (patternRuntime_ && !boundChainName_.empty()) {
                    patternRuntime_->chainRemovePattern(boundChainName_, i);
                } else {
                    // Fallback to internal chain
                    chain->removeEntry(i);
                }
                chainModified = true;
                ofLogVerbose("TrackerSequencer") << "Removed deleted pattern '" << deletedPatternName 
                                                 << "' from pattern chain at index " << i;
            }
        }
        
        // Update current chain index if needed
        if (chainModified) {
            int currentIndex = chain->getCurrentIndex();
            if (currentIndex >= chain->getSize()) {
                chain->setCurrentIndex(std::max(0, chain->getSize() - 1));
            }
        }
    }
}

// Phase 3: migrateToPatternRuntime() removed - patterns are always in Runtime

