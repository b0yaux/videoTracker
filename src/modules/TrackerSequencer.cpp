#include "TrackerSequencer.h"
#include "gui/TrackerSequencerGUI.h"  // For GUIState definition
#include "utils/Clock.h"
#include "Module.h"
#include "core/ModuleRegistry.h"
#include "core/ConnectionManager.h"
#include "core/ParameterRouter.h"
#include "core/ModuleFactory.h"
#include "ofLog.h"
#include "ofJson.h"
#include <imgui.h>
#include <cmath>  // For std::round
#include <limits>  // For std::numeric_limits
#include <set>
#include <atomic>  // For diagnostic event counter

// TrackerSequencer implementation
//--------------------------------------------------------------
TrackerSequencer::TrackerSequencer() 
    : clock(nullptr), stepsPerBeat(4.0f), gatingEnabled(true),
      currentPatternIndex(0),
      draggingStep(-1), draggingColumn(-1), lastDragValue(0.0f), dragStartY(0.0f), dragStartX(0.0f),
      connectionManager_(nullptr) {
    // PlaybackState is initialized with default values in struct definition
    // Initialize with one empty pattern (default 16 steps)
    patterns.push_back(Pattern(16));
    // Initialize pattern chain with first pattern
    patternChain.addEntry(0);
    patternChain.setEnabled(true);
}

TrackerSequencer::~TrackerSequencer() {
}

void TrackerSequencer::setup(Clock* clockRef) {
    clock = clockRef;
    playbackState.playbackStep = 0; // Initialize playback step
    // Note: GUI state initialization removed - managed by TrackerSequencerGUI
    
    // Initialize patterns (ensure at least one pattern exists)
    // Note: Constructor already creates Pattern(16), so this is just a safety check
    if (patterns.empty()) {
        patterns.push_back(Pattern(16));
        currentPatternIndex = 0;
    } else {
        // Only set step count if pattern is empty (newly created)
        // Don't overwrite existing pattern stepCounts (they may have been loaded from JSON)
        if (getCurrentPattern().isEmpty()) {
            getCurrentPattern().setStepCount(16);
        }
        // Otherwise preserve existing stepCount
    }
    
    // Column configuration is now per-pattern (initialized in Pattern constructor)
            
            // Connect to Clock's time events for beat synchronization
    if (clock) {
        ofAddListener(clock->timeEvent, this, &TrackerSequencer::onTimeEvent);
        
        // Register audio listener for sample-accurate step timing
        clock->addAudioListener([this](ofSoundBuffer& buffer) {
            this->processAudioBuffer(buffer);
        });
        
        // Subscribe to Clock transport changes
        clock->addTransportListener([this](bool isPlaying) {
            this->onClockTransportChanged(isPlaying);
        });
    }
    
    ofLogNotice("TrackerSequencer") << "Setup complete with " << getCurrentPattern().getStepCount() << " steps";
}


//--------------------------------------------------------------
//--------------------------------------------------------------
void TrackerSequencer::initialize(Clock* clock, ModuleRegistry* registry, ConnectionManager* connectionManager, 
                                  ParameterRouter* parameterRouter, bool isRestored) {
    // Phase 2.2: Unified initialization - combines postCreateSetup and configureSelf
    
    // 1. Basic setup (from postCreateSetup)
    if (clock) {
        if (isRestored) {
            // For restored modules, only set up clock connection without resetting pattern stepCount
            // Pattern stepCounts were already loaded from JSON in fromJson()
            this->clock = clock;
            playbackState.playbackStep = 0;
            
            // Connect to Clock's time events for beat synchronization
            ofAddListener(clock->timeEvent, this, &TrackerSequencer::onTimeEvent);
            
            // Register audio listener for sample-accurate step timing
            clock->addAudioListener([this](ofSoundBuffer& buffer) {
                this->processAudioBuffer(buffer);
            });
            
            // Subscribe to Clock transport changes
            clock->addTransportListener([this](bool isPlaying) {
                this->onClockTransportChanged(isPlaying);
            });
            
            // Ensure at least one pattern exists (should already exist from fromJson, but safety check)
            if (patterns.empty()) {
                patterns.push_back(Pattern(16));
                currentPatternIndex = 0;
            }
        } else {
            // For new modules, setup clock connection
            // Pattern already exists from constructor (Pattern(16))
            setup(clock);
        }
    }
    
    // 2. Self-configuration (from configureSelf) - only if we have all required dependencies
    if (!registry || !connectionManager || !parameterRouter) {
        return;
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
        // Clock started - start the sequencer from step 1
        play();
        // Reset to step 1 and trigger it
        playbackState.playbackStep = 0; // Start playback at step 0 (0-based internally, so step 1 is index 0)
        playbackState.clearPlayingStep();
        playbackState.patternCycleCount = 0; // Reset cycle counter on transport start
        triggerStep(0);  // Trigger step 1 (0-based)
        ofLogNotice("TrackerSequencer") << "Clock transport started - sequencer playing from step 1";
    } else {
        // Clock stopped - pause the sequencer (don't reset step)
        pause();
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

// Helper to get current pattern
Pattern& TrackerSequencer::getCurrentPattern() {
    if (patterns.empty()) {
        // Safety: create a pattern if none exist (default 16 steps)
        patterns.push_back(Pattern(16));
        currentPatternIndex = 0;
    }
    if (currentPatternIndex < 0 || currentPatternIndex >= (int)patterns.size()) {
        currentPatternIndex = 0;
    }
    return patterns[currentPatternIndex];
}

const Pattern& TrackerSequencer::getCurrentPattern() const {
    if (patterns.empty() || currentPatternIndex < 0 || currentPatternIndex >= (int)patterns.size()) {
        static Pattern emptyPattern(16);
        return emptyPattern;
    }
    return patterns[currentPatternIndex];
}

void TrackerSequencer::setStep(int stepIndex, const Step& step) {
    if (!isValidStep(stepIndex)) return;
    
    // Check if position parameter changed and notify if it's the current playback step
    const Step& oldStep = getCurrentPattern().getStep(stepIndex);
    float oldPosition = oldStep.getParameterValue("position", 0.0f);
    float newPosition = step.getParameterValue("position", 0.0f);
    
    // Update the pattern
    getCurrentPattern().setStep(stepIndex, step);
    
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
    // Sample-accurate step timing based on this TrackerSequencer's own stepsPerBeat
    if (!playbackState.isPlaying || !clock) return;
    
    // Calculate samples per step from our own stepsPerBeat
    float bpm = clock->getBPM();
    float sampleRate = buffer.getSampleRate();
    if (sampleRate <= 0.0f || bpm <= 0.0f) return;
    
    float beatsPerSecond = bpm / 60.0f;
    float samplesPerBeat = sampleRate / beatsPerSecond;
    float samplesPerStep = samplesPerBeat / std::abs(stepsPerBeat);  // Use absolute value for timing calculation
    
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
    // Support fractional values (1/2, 1/4, 1/8) and negative for backward reading
    // Clamp to reasonable range: -96 to 96, excluding 0
    if (steps == 0.0f) {
        steps = 4.0f;  // Default fallback if 0
    }
    stepsPerBeat = std::max(-96.0f, std::min(96.0f, steps));
    updateStepInterval();
    // Note: stepsPerBeat is now per-instance, no Clock coupling needed
}

void TrackerSequencer::updateStepInterval() {
    if (!clock) return;
    
    // Get steps per beat from pattern sequencer (single source of truth)
    // Use absolute value for timing calculations (direction only affects step advancement)
    float spb = std::abs(stepsPerBeat);
    
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
    
    // Save stepsPerBeat (per-instance step timing)
    json["stepsPerBeat"] = stepsPerBeat;
    
    // Column configuration is now saved per-pattern (in Pattern::toJson)
    // No need to save it here - each pattern saves its own columnConfig
    
    // Save multi-pattern support
    json["currentPatternIndex"] = currentPatternIndex;
    patternChain.toJson(json);
    
    // Save all patterns
    ofJson patternsArray = ofJson::array();
    for (const auto& p : patterns) {
        patternsArray.push_back(p.toJson());
    }
    json["patterns"] = patternsArray;
    
    // Pattern chain serialization is handled by PatternChain::toJson()
    
    // Legacy: Save single pattern for backward compatibility
    json["pattern"] = getCurrentPattern().toJson();
    
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
    
    // Load basic properties
    if (json.contains("currentStep")) {
        playbackState.playbackStep = json["currentStep"];
    }
    // Note: GUI state (editStep, etc.) no longer loaded here - managed by TrackerSequencerGUI
    
    // Load stepsPerBeat (default to 4.0 if not present)
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
    
    // Load multi-pattern support (new format)
    if (json.contains("patterns") && json["patterns"].is_array()) {
        patterns.clear();
        auto patternsArray = json["patterns"];
        for (const auto& patternJson : patternsArray) {
            // Create pattern with default step count - fromJson will set correct count from JSON
            Pattern p(16);  // Default step count - actual count comes from JSON stepCount field or array size
            p.fromJson(patternJson);
            // Pattern size is now per-pattern, preserved from JSON stepCount field
            patterns.push_back(p);
        }
        
        // Load current pattern index
        if (json.contains("currentPatternIndex")) {
            int loadedIndex = json["currentPatternIndex"];
            if (loadedIndex >= 0 && loadedIndex < (int)patterns.size()) {
                currentPatternIndex = loadedIndex;
            } else {
                currentPatternIndex = 0;
            }
        }
        
        // Load pattern chain (handles both new and legacy formats)
        patternChain.fromJson(json, (int)patterns.size());
        
        ofLogNotice("TrackerSequencer") << "Loaded " << patterns.size() << " patterns, current pattern: " << currentPatternIndex;
    } else if (json.contains("pattern") && json["pattern"].is_array()) {
        // Legacy: Load single pattern (backward compatibility)
        patterns.clear();
        Pattern p(16);  // Default step count - actual count comes from JSON stepCount field or array size
        p.fromJson(json["pattern"]);
        // Pattern size is now per-pattern, preserved from JSON stepCount field
        patterns.push_back(p);
        currentPatternIndex = 0;
        patternChain.clear();
        // Initialize pattern chain with the single pattern for legacy files
        patternChain.addEntry(0);
        patternChain.setEnabled(true);
        ofLogNotice("TrackerSequencer") << "Loaded legacy single pattern format";
    } else {
        // No pattern data - ensure we have at least one empty pattern
        if (patterns.empty()) {
            patterns.push_back(Pattern(16));  // Default step count
            currentPatternIndex = 0;
        }
        // Initialize pattern chain with the first pattern
        if (patternChain.getSize() == 0 && !patterns.empty()) {
            patternChain.addEntry(0);
            patternChain.setEnabled(true);
        }
    }
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
    
    bool patternFinished;
    if (stepsPerBeat < 0.0f) {
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
    
    // If pattern finished and using pattern chain, handle repeat counts
    if (patternFinished) {
        int nextPatternIdx = patternChain.advanceOnPatternFinish((int)patterns.size());
        if (nextPatternIdx >= 0) {
            currentPatternIndex = nextPatternIdx;
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
    // step is now 0-based internally
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
            // Clear playing state since step didn't trigger
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
            // Clear playing state since step didn't trigger
            playbackState.clearPlayingStep();
            return;
        }
    }
    
    // All trigger conditions passed - proceed with triggering
    // Calculate duration in seconds (same for both manual and playback)
    float stepLength = stepData.index >= 0 ? (float)stepData.length : 1.0f;
    float duration = (stepLength * 60.0f) / (bpm * std::abs(stepsPerBeat));  // Use absolute value for duration calculation
    
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
    // "note" is the sequencer's parameter name (maps to stepData.index for MediaPool)
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
            // MediaPool will validate ranges when it receives the parameter
            triggerEvt.parameters[paramName] = paramValue;
        }
    }
    
    // Broadcast trigger event to all subscribers (modular!)
    // NOTE: ofNotifyEvent is called from audio thread - this is acceptable for event dispatch
    // The actual handlers (MediaPool::onTrigger) now use lock-free queue
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

std::vector<ParameterDescriptor> TrackerSequencer::getParameters() const {
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

void TrackerSequencer::setParameter(const std::string& paramName, float value, bool notify) {
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

float TrackerSequencer::getParameter(const std::string& paramName) const {
    if (paramName == "currentStepPosition") {
        return getCurrentStepPosition();
    }
    // For other parameters that might be added in the future
    return Module::getParameter(paramName); // Default
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
    
    // Calculate duration in seconds using patternSequencer's stepsPerBeat
    float spb = std::abs(stepsPerBeat);  // Use absolute value for duration calculation
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
void TrackerSequencer::setCurrentPatternIndex(int index) {
    if (index >= 0 && index < (int)patterns.size()) {
        currentPatternIndex = index;
        ofLogNotice("TrackerSequencer") << "Switched to pattern " << index;
    } else {
        ofLogWarning("TrackerSequencer") << "Invalid pattern index: " << index;
    }
}

int TrackerSequencer::addPattern() {
    // New pattern uses same step count as current pattern
    int stepCount = getCurrentPattern().getStepCount();
    Pattern newPattern(stepCount);
    patterns.push_back(newPattern);
    int newIndex = (int)patterns.size() - 1;
    ofLogNotice("TrackerSequencer") << "Added new pattern at index " << newIndex << " with " << stepCount << " steps";
    return newIndex;
}

void TrackerSequencer::removePattern(int index) {
    if (patterns.size() <= 1) {
        ofLogWarning("TrackerSequencer") << "Cannot remove pattern: must have at least one pattern";
        return;
    }
    
    if (index < 0 || index >= (int)patterns.size()) {
        ofLogWarning("TrackerSequencer") << "Invalid pattern index for removal: " << index;
        return;
    }
    
    patterns.erase(patterns.begin() + index);
    
    // Adjust current pattern index if necessary
    if (currentPatternIndex >= (int)patterns.size()) {
        currentPatternIndex = (int)patterns.size() - 1;
    }
    
    // Adjust pattern chain indices
    const auto& chain = patternChain.getChain();
    for (int i = (int)chain.size() - 1; i >= 0; i--) {
        if (chain[i] == index) {
            // Remove entry from pattern chain
            patternChain.removeEntry(i);
        } else if (chain[i] > index) {
            // Decrement indices greater than removed index
            patternChain.setEntry(i, chain[i] - 1);
        }
    }
    
    // Adjust current chain index if necessary
    if (patternChain.getCurrentIndex() >= patternChain.getSize()) {
        patternChain.setCurrentIndex(std::max(0, patternChain.getSize() - 1));
    }
    
    ofLogNotice("TrackerSequencer") << "Removed pattern at index " << index;
}

void TrackerSequencer::copyPattern(int sourceIndex, int destIndex) {
    if (sourceIndex < 0 || sourceIndex >= (int)patterns.size()) {
        ofLogWarning("TrackerSequencer") << "Invalid source pattern index: " << sourceIndex;
        return;
    }
    
    if (destIndex < 0 || destIndex >= (int)patterns.size()) {
        ofLogWarning("TrackerSequencer") << "Invalid destination pattern index: " << destIndex;
        return;
    }
    
    // Copy pattern data
    patterns[destIndex] = patterns[sourceIndex];
    ofLogNotice("TrackerSequencer") << "Copied pattern " << sourceIndex << " to pattern " << destIndex;
}

void TrackerSequencer::duplicatePattern(int index) {
    if (index < 0 || index >= (int)patterns.size()) {
        ofLogWarning("TrackerSequencer") << "Invalid pattern index for duplication: " << index;
        return;
    }
    
    Pattern newPattern = patterns[index];
    patterns.push_back(newPattern);
    int newIndex = (int)patterns.size() - 1;
    ofLogNotice("TrackerSequencer") << "Duplicated pattern " << index << " to new pattern " << newIndex;
}

// Pattern chain (pattern chaining) implementation
//--------------------------------------------------------------
void TrackerSequencer::setCurrentChainIndex(int index) {
    patternChain.setCurrentIndex(index);
    // Update current pattern index based on pattern chain
    if (patternChain.isEnabled()) {
        int patternIdx = patternChain.getEntry(index);
        if (patternIdx >= 0 && patternIdx < (int)patterns.size()) {
            currentPatternIndex = patternIdx;
        }
    }
}

void TrackerSequencer::addToPatternChain(int patternIndex) {
    if (patternIndex < 0 || patternIndex >= (int)patterns.size()) {
        ofLogWarning("TrackerSequencer") << "Invalid pattern index for chain: " << patternIndex;
        return;
    }
    patternChain.addEntry(patternIndex);
}

void TrackerSequencer::removeFromPatternChain(int chainIndex) {
    patternChain.removeEntry(chainIndex);
    
    // Switch to the pattern at the new current chain index
    int newCurrentIndex = patternChain.getCurrentIndex();
    if (newCurrentIndex >= 0 && newCurrentIndex < patternChain.getSize()) {
        int newPatternIndex = patternChain.getEntry(newCurrentIndex);
        if (newPatternIndex >= 0 && newPatternIndex < (int)patterns.size()) {
            setCurrentPatternIndex(newPatternIndex);
        }
    }
}

void TrackerSequencer::setPatternChainEntry(int chainIndex, int patternIndex) {
    if (patternIndex < 0 || patternIndex >= (int)patterns.size()) {
        ofLogWarning("TrackerSequencer") << "Invalid pattern index: " << patternIndex;
        return;
    }
    patternChain.setEntry(chainIndex, patternIndex);
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
