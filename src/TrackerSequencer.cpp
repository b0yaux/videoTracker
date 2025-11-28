#include "TrackerSequencer.h"
#include "Clock.h"
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
    : clock(nullptr), stepsPerBeat(4), gatingEnabled(true),
      currentPatternIndex(0), currentChainIndex(0), currentChainRepeat(0), usePatternChain(true),
      playbackStep(0),
      draggingStep(-1), draggingColumn(-1), lastDragValue(0.0f), dragStartY(0.0f), dragStartX(0.0f),
      lastTriggeredStep(-1), playing(false),
      currentMediaStartStep(-1), currentMediaStepLength(0.0f),
      sampleAccumulator(0.0), lastBpm(120.0f),
      stepStartTime(0.0f), stepEndTime(0.0f),
      connectionManager_(nullptr),
      showGUI(true),
      currentPlayingStep(-1) {
    // Initialize with one empty pattern (default 16 steps)
    patterns.push_back(Pattern(16));
    // Initialize pattern chain with first pattern
    patternChain.push_back(0);
    patternChainRepeatCounts[0] = 1;
}

TrackerSequencer::~TrackerSequencer() {
}

void TrackerSequencer::setup(Clock* clockRef, int steps) {
    clock = clockRef;
    playbackStep = 0; // Initialize playback step
    // Note: GUI state initialization removed - managed by TrackerSequencerGUI
    
    // Initialize patterns (ensure at least one pattern exists)
    if (patterns.empty()) {
        patterns.push_back(Pattern(steps));
        currentPatternIndex = 0;
    } else {
        // Set step count for current pattern only (per-pattern step count)
        getCurrentPattern().setStepCount(steps);
    }
    
    // Column configuration is now per-pattern (initialized in Pattern constructor)
            
            // Connect to Clock's time events for sample-accurate timing
    if (clock) {
        ofAddListener(clock->timeEvent, this, &TrackerSequencer::onTimeEvent);
        // Sync Clock's SPB with TrackerSequencer's SPB
        clock->setStepsPerBeat(stepsPerBeat);
        
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
        // Use default step count of 16 (matches setup() default)
        setup(clock, 16);
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
                        // Found connected module with index parameter - return range
                        return static_cast<int>(param.maxValue) + 1;  // maxValue is inclusive, range is count
                    }
                }
            }
        }
    }
    
    // No connected module found - return default
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
}

//--------------------------------------------------------------
// DEPRECATED: Use initialize() instead
void TrackerSequencer::postCreateSetup(Clock* clock) {
    // Legacy implementation - delegates to initialize()
    initialize(clock, nullptr, nullptr, nullptr, false);
}

//--------------------------------------------------------------
// DEPRECATED: Use initialize() instead
void TrackerSequencer::configureSelf(ModuleRegistry* registry, ConnectionManager* connectionManager, ParameterRouter* parameterRouter) {
    // Legacy implementation - delegates to initialize()
    // Note: clock is nullptr here since configureSelf doesn't have it
    initialize(nullptr, registry, connectionManager, parameterRouter, false);
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
            PatternCell cell0(0, 0.0f, 1.0f, 1.0f, 1.0f);
            setCell(0, cell0);
            
            if (indexRange > 1) {
                PatternCell cell4(1, 0.0f, 1.2f, 1.0f, 1.0f);
                setCell(4, cell4);
                
                PatternCell cell8(0, 0.5f, 1.0f, 1.0f, 1.0f);
                setCell(8, cell8);
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
        playbackStep = 0; // Start playback at step 0 (0-based internally, so step 1 is index 0)
        currentPlayingStep = -1;  // Reset current playing step
        stepStartTime = 0.0f;
        stepEndTime = 0.0f;
        triggerStep(0);  // Trigger step 1 (0-based)
        ofLogNotice("TrackerSequencer") << "Clock transport started - sequencer playing from step 1";
    } else {
        // Clock stopped - pause the sequencer (don't reset step)
        pause();
        ofLogNotice("TrackerSequencer") << "Clock transport stopped - sequencer paused at step " << (playbackStep + 1);
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

void TrackerSequencer::setCell(int step, const PatternCell& cell) {
    if (!isValidStep(step)) return;
    
    // Check if position parameter changed and notify if it's the current playback step
    const PatternCell& oldCell = getCurrentPattern().getCell(step);
    float oldPosition = oldCell.getParameterValue("position", 0.0f);
    float newPosition = cell.getParameterValue("position", 0.0f);
    
    // Update the pattern
    getCurrentPattern().setCell(step, cell);
    
    // Notify if position changed and this is the current playback step
    // Note: Edit step checking removed - GUI state is managed by TrackerSequencerGUI
    // The GUI will handle edit step notifications separately if needed
    if (parameterChangeCallback && std::abs(oldPosition - newPosition) > 0.0001f) {
        if (step == playbackStep) {
            parameterChangeCallback("currentStepPosition", newPosition);
        }
    }
    
    // Removed verbose logging for performance
}

PatternCell TrackerSequencer::getCell(int step) const {
    if (!isValidStep(step)) return PatternCell();
    return getCurrentPattern().getCell(step);
}

void TrackerSequencer::clearCell(int step) {
    if (!isValidStep(step)) return;
    
    getCurrentPattern().clearCell(step);
    
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
        PatternCell cell;
        
        // 70% chance of having a media item, 30% chance of being empty (rest)
        if (ofRandom(1.0f) < 0.7f) {
            cell.index = ofRandom(0, numMedia);
            
            // Use parameter ranges dynamically instead of hardcoded values
            auto posRange = getParameterRange("position");
            auto speedRange = getParameterRange("speed");
            auto volumeRange = getParameterRange("volume");
            
            cell.setParameterValue("position", ofRandom(posRange.first, posRange.second));
            cell.setParameterValue("speed", ofRandom(speedRange.first, speedRange.second));
            // Use 25% to 75% of volume range for randomization (avoiding extremes)
            float volumeRangeSize = volumeRange.second - volumeRange.first;
            cell.setParameterValue("volume", ofRandom(
                volumeRange.first + volumeRangeSize * 0.25f,
                volumeRange.first + volumeRangeSize * 0.75f
            ));
            cell.length = ofRandom(1, stepCount);
        } else {
            cell.clear(); // Empty/rest step
        }
        
        getCurrentPattern().setCell(i, cell);
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
        int stepCount = getCurrentPattern().getStepCount();
        for (int i = 0; i < stepCount; i++) {
            if (getCurrentPattern()[i].index >= 0) { // Only randomize if step has a media item
                getCurrentPattern()[i].length = ofRandom(1, stepCount + 1);
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
                // Set length to reach the next step (clamp to max 16)
                getCurrentPattern()[i].length = std::min(16, stepsToNext);
            } else {
                // No next step found - keep current length or set to remaining steps
                int remainingSteps = stepCount - i;
                getCurrentPattern()[i].length = std::min(16, remainingSteps);
            }
        }
    }
    ofLogNotice("TrackerSequencer") << "Legato applied to length column";
}

bool TrackerSequencer::duplicateRange(int fromStep, int toStep, int destinationStep) {
    // Delegate to Pattern class
    return getCurrentPattern().duplicateRange(fromStep, toStep, destinationStep);
}

// Timing and playback control
void TrackerSequencer::processAudioBuffer(ofSoundBuffer& buffer) {
    // This method is now deprecated - timing is handled by Clock's beat events
    // Keep for compatibility but do nothing
}

void TrackerSequencer::onTimeEvent(TimeEvent& data) {
    if (!playing) return;
    
    // Only process STEP events (ignore BEAT events)
    if (data.type != TimeEventType::STEP) return;

    // Advance to next step (sample-accurate timing from Clock!)
    advanceStep();
}

//--------------------------------------------------------------
void TrackerSequencer::setStepsPerBeat(int steps) {
    stepsPerBeat = std::max(1, std::min(96, steps));
    updateStepInterval();
    // Sync with Clock's SPB
    if (clock) {
        clock->setStepsPerBeat(stepsPerBeat);
    }
}

void TrackerSequencer::updateStepInterval() {
    if (!clock) return;
    
    // Get steps per beat from pattern sequencer (single source of truth)
    int spb = stepsPerBeat;
    
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
    playing = true;
    // Reset timing state
    currentPlayingStep = -1;
    stepStartTime = 0.0f;
    stepEndTime = 0.0f;
    // Reset audio-rate timing for fresh start
    sampleAccumulator = 0.0;
}

void TrackerSequencer::pause() {
    playing = false;
    // Clear current playing step so GUI shows inactive state when paused
    // This ensures visual feedback matches the paused state
    currentPlayingStep = -1;
    // Keep playbackStep and timing state for resume (if needed)
}

void TrackerSequencer::stop() {
    playing = false;
    playbackStep = 0; // Reset playback step indicator
    currentPlayingStep = -1;
    stepStartTime = 0.0f;
    stepEndTime = 0.0f;
    // Reset audio-rate timing
    sampleAccumulator = 0.0;
}

void TrackerSequencer::reset() {
    playbackStep = 0; // Reset playback step indicator
    playing = false;
    currentPlayingStep = -1;
    stepStartTime = 0.0f;
    stepEndTime = 0.0f;
    // Reset audio-rate timing
    sampleAccumulator = 0.0;
}

void TrackerSequencer::setCurrentStep(int step) {
    if (isValidStep(step)) {
        playbackStep = step; // Update playback step indicator
    }
}

ofJson TrackerSequencer::toJson() const {
    ofJson json;
    json["currentStep"] = playbackStep;  // Save playback step for backward compatibility
    // Note: GUI state (editStep, etc.) no longer saved here - managed by TrackerSequencerGUI
    
    // Column configuration is now saved per-pattern (in Pattern::toJson)
    // No need to save it here - each pattern saves its own columnConfig
    
    // Save multi-pattern support
    json["currentPatternIndex"] = currentPatternIndex;
    json["usePatternChain"] = usePatternChain;
    json["currentChainIndex"] = currentChainIndex;
    
    // Save all patterns
    ofJson patternsArray = ofJson::array();
    for (const auto& p : patterns) {
        patternsArray.push_back(p.toJson());
    }
    json["patterns"] = patternsArray;
    
    // Save pattern chain with repeat counts
    ofJson chainArray = ofJson::array();
    for (size_t i = 0; i < patternChain.size(); i++) {
        ofJson entryJson;
        entryJson["patternIndex"] = patternChain[i];
        entryJson["repeatCount"] = getPatternChainRepeatCount((int)i);
        chainArray.push_back(entryJson);
    }
    json["patternChain"] = chainArray;
    json["currentChainRepeat"] = currentChainRepeat;
    
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
    // Load basic properties
    if (json.contains("currentStep")) {
        playbackStep = json["currentStep"];
    }
    // Note: GUI state (editStep, etc.) no longer loaded here - managed by TrackerSequencerGUI
    
    // Column configuration is now per-pattern (loaded in Pattern::fromJson)
    
    // Load multi-pattern support (new format)
    if (json.contains("patterns") && json["patterns"].is_array()) {
        patterns.clear();
        auto patternsArray = json["patterns"];
        for (const auto& patternJson : patternsArray) {
            Pattern p(16);  // Default step count - actual count comes from JSON array size
            p.fromJson(patternJson);
            // Pattern size is now per-pattern, so we don't force it to match
            // Each pattern keeps its own step count from JSON
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
        
        // Load pattern chain with repeat counts (support both new and legacy keys)
        ofJson chainArray;
        if (json.contains("patternChain") && json["patternChain"].is_array()) {
            chainArray = json["patternChain"];
        } else if (json.contains("orderList") && json["orderList"].is_array()) {
            // Legacy: support old "orderList" key
            chainArray = json["orderList"];
        }
        
        if (!chainArray.is_null() && chainArray.is_array()) {
            patternChain.clear();
            patternChainRepeatCounts.clear();
            for (size_t i = 0; i < chainArray.size(); i++) {
                const auto& chainEntry = chainArray[i];
                int patternIdx = -1;
                int repeatCount = 1;
                
                // Support both old format (int) and new format (object)
                if (chainEntry.is_number()) {
                    // Legacy format: just pattern index
                    patternIdx = chainEntry;
                } else if (chainEntry.is_object()) {
                    // New format: object with patternIndex and repeatCount
                    if (chainEntry.contains("patternIndex")) {
                        patternIdx = chainEntry["patternIndex"];
                    }
                    if (chainEntry.contains("repeatCount")) {
                        repeatCount = chainEntry["repeatCount"];
                        repeatCount = std::max(1, std::min(99, repeatCount));
                    }
                }
                
                if (patternIdx >= 0 && patternIdx < (int)patterns.size()) {
                    patternChain.push_back(patternIdx);
                    patternChainRepeatCounts[(int)i] = repeatCount;
                    patternChainDisabled[(int)i] = false;  // Default to enabled when loading
                }
            }
        }
        
        // Load pattern chain settings (support both new and legacy keys)
        if (json.contains("usePatternChain")) {
            usePatternChain = json["usePatternChain"];
        } else if (json.contains("useOrderList")) {
            // Legacy: support old "useOrderList" key
            usePatternChain = json["useOrderList"];
        } else {
            // Default to enabled for new files
            usePatternChain = true;
        }
        
        if (json.contains("currentChainIndex")) {
            int loadedChainIndex = json["currentChainIndex"];
            if (loadedChainIndex >= 0 && loadedChainIndex < (int)patternChain.size()) {
                currentChainIndex = loadedChainIndex;
            } else {
                currentChainIndex = 0;
            }
        } else if (json.contains("currentOrderIndex")) {
            // Legacy: support old "currentOrderIndex" key
            int loadedChainIndex = json["currentOrderIndex"];
            if (loadedChainIndex >= 0 && loadedChainIndex < (int)patternChain.size()) {
                currentChainIndex = loadedChainIndex;
            } else {
                currentChainIndex = 0;
            }
        }
        
        if (json.contains("currentChainRepeat")) {
            currentChainRepeat = json["currentChainRepeat"];
        } else if (json.contains("currentOrderRepeat")) {
            // Legacy: support old "currentOrderRepeat" key
            currentChainRepeat = json["currentOrderRepeat"];
        } else {
            currentChainRepeat = 0;
        }
        
        // If pattern chain is empty but enabled, initialize with all patterns
        if (usePatternChain && patternChain.empty() && !patterns.empty()) {
            for (size_t i = 0; i < patterns.size(); i++) {
                patternChain.push_back((int)i);
                patternChainRepeatCounts[(int)i] = 1;
            }
            currentChainIndex = 0;
            currentChainRepeat = 0;
        }
        
        ofLogNotice("TrackerSequencer") << "Loaded " << patterns.size() << " patterns, current pattern: " << currentPatternIndex;
    } else if (json.contains("pattern") && json["pattern"].is_array()) {
        // Legacy: Load single pattern (backward compatibility)
        patterns.clear();
        Pattern p(16);  // Default step count - actual count comes from JSON array size
        p.fromJson(json["pattern"]);
        // Pattern size is now per-pattern, so we don't force it to match
        patterns.push_back(p);
        currentPatternIndex = 0;
        patternChain.clear();
        patternChainRepeatCounts.clear();
        // Initialize pattern chain with the single pattern for legacy files
        patternChain.push_back(0);
        patternChainRepeatCounts[0] = 1;
        usePatternChain = true;  // Enable by default
        currentChainIndex = 0;
        currentChainRepeat = 0;
        ofLogNotice("TrackerSequencer") << "Loaded legacy single pattern format";
    } else {
        // No pattern data - ensure we have at least one empty pattern
        if (patterns.empty()) {
            patterns.push_back(Pattern(16));  // Default step count
            currentPatternIndex = 0;
        }
        // Initialize pattern chain with the first pattern
        if (patternChain.empty() && !patterns.empty()) {
            patternChain.push_back(0);
            patternChainRepeatCounts[0] = 1;
            usePatternChain = true;  // Enable by default
            currentChainIndex = 0;
            currentChainRepeat = 0;
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

void TrackerSequencer::addStepEventListener(std::function<void(int, float, const PatternCell&)> listener) {
    stepEventListeners.push_back(listener);
}

void TrackerSequencer::advanceStep() {
    if (!playing) return;
    
    float currentTime = ofGetElapsedTimef();
    
    // Check if current step duration has expired
    bool currentStepExpired = (currentPlayingStep >= 0 && stepEndTime > 0.0f && currentTime >= stepEndTime);
    
    if (currentStepExpired) {
        // Current step finished - clear playing state
        currentPlayingStep = -1;
        stepStartTime = 0.0f;
        stepEndTime = 0.0f;
    }
    
    // Always advance playback step (for visual indicator)
    int stepCount = getCurrentPattern().getStepCount();
    int previousStep = playbackStep;
    playbackStep = (playbackStep + 1) % stepCount;
    
    // Check if we wrapped around (pattern finished)
    bool patternFinished = (playbackStep == 0 && previousStep == stepCount - 1);
    
    // If pattern finished and using pattern chain, handle repeat counts
    if (patternFinished && usePatternChain && !patternChain.empty()) {
        // Increment repeat counter
        currentChainRepeat++;
        
        // Get repeat count for current chain entry (default to 1 if not set)
        int repeatCount = 1;
        auto it = patternChainRepeatCounts.find(currentChainIndex);
        if (it != patternChainRepeatCounts.end()) {
            repeatCount = it->second;
        }
        
        // Check if we've finished all repeats for current chain entry
        if (currentChainRepeat >= repeatCount) {
            // Move to next chain entry (skip disabled entries)
            currentChainRepeat = 0;
            int startIndex = currentChainIndex;
            do {
                currentChainIndex = (currentChainIndex + 1) % (int)patternChain.size();
                // If we've looped back to start and all are disabled, break to avoid infinite loop
                if (currentChainIndex == startIndex) break;
            } while (isPatternChainEntryDisabled(currentChainIndex) && currentChainIndex != startIndex);
        }
        
        // Update current pattern index (only if not disabled)
        if (!isPatternChainEntryDisabled(currentChainIndex)) {
            int nextPatternIdx = patternChain[currentChainIndex];
            if (nextPatternIdx >= 0 && nextPatternIdx < (int)patterns.size()) {
                currentPatternIndex = nextPatternIdx;
                ofLogVerbose("TrackerSequencer") << "Pattern finished, advancing to pattern " << nextPatternIdx 
                                                 << " (chain position " << currentChainIndex 
                                                 << ", repeat " << (currentChainRepeat + 1) << "/" << repeatCount << ")";
            }
        }
    }
    
    // Check if we should trigger the new step
    const PatternCell& newCell = getCell(playbackStep);
    
    // Trigger new step if:
    // 1. No step is currently playing (currentPlayingStep < 0), OR
    // 2. New step has media (index >= 0) - this overrides current playing step
    if (currentPlayingStep < 0 || newCell.index >= 0) {
        triggerStep(playbackStep);
    }
}

void TrackerSequencer::triggerStep(int step) {
    // step is now 0-based internally
    if (!isValidStep(step)) return;
    if (!clock) return;
    
    // Don't send triggers if module is disabled
    if (!isEnabled()) return;
    
    // Apply any pending edit for this step before triggering
    if (pendingEdit.step == step && pendingEdit.step >= 0) {
        applyPendingEdit();
        // Clear pending edit after applying
        pendingEdit = PendingEdit();
    }

    const PatternCell& cell = getCell(step); // Direct 0-based array access
    float bpm = clock->getBPM();
    
    playbackStep = step;
    
    // Calculate duration in seconds (same for both manual and playback)
    float stepLength = cell.index >= 0 ? (float)cell.length : 1.0f;
    float duration = (stepLength * 60.0f) / (bpm * stepsPerBeat);
    
    // Set timing for ALL triggers (unified for manual and playback)
    if (cell.index >= 0) {
        float currentTime = ofGetElapsedTimef();
        stepStartTime = currentTime;
        stepEndTime = currentTime + duration;
        currentPlayingStep = step;
    } else {
        // Empty step - clear playing state
        currentPlayingStep = -1;
        stepStartTime = 0.0f;
        stepEndTime = 0.0f;
    }
    
    // Create TriggerEvent with TrackerSequencer parameters
    // TrackerSequencer exposes its own parameters (note, position, speed, volume)
    // Modules will map these to their own parameters
    TriggerEvent triggerEvt;
    triggerEvt.duration = duration;
    triggerEvt.step = step;  // Include step number for position memory modes
    
    // Check chance parameter (internal) - only trigger if random roll succeeds
    // Chance is 0-100, default 100 (always trigger)
    int chance = 100;
    if (cell.hasParameter("chance")) {
        chance = (int)std::round(cell.getParameterValue("chance", 100.0f));
        chance = std::max(0, std::min(100, chance)); // Clamp to 0-100
    }
    
    // Roll for chance (0-100)
    if (chance < 100) {
        int roll = (int)(ofRandom(0.0f, 100.0f));
        if (roll >= chance) {
            // Chance failed - don't trigger this step
            return;
        }
    }
    
    // Map PatternCell parameters to TrackerSequencer parameters
    // "note" is the sequencer's parameter name (maps to cell.index for MediaPool)
    if (cell.index >= 0) {
        triggerEvt.parameters["note"] = (float)cell.index;
    } else {
        triggerEvt.parameters["note"] = -1.0f; // Rest/empty step
    }
    
    // Get internal parameter names to exclude from trigger event
    // Internal parameters (note, chance) are sequencer-specific and not sent to external modules
    auto internalParams = getInternalParameters();
    std::set<std::string> internalParamNames;
    for (const auto& param : internalParams) {
        internalParamNames.insert(param.name);
    }
    
    // MODULAR: Only send parameters that are in the current pattern's column configuration
    // This ensures we only send parameters that are actually displayed/used in the grid
    // Skip internal parameters (note, chance) - they're sequencer-specific and not sent to modules
    const auto& columnConfig = getCurrentPattern().getColumnConfiguration();
    std::set<std::string> columnParamNames;
    for (const auto& col : columnConfig) {
        // Skip required columns (index, length) - they're handled separately
        if (col.parameterName != "index" && col.parameterName != "length") {
            columnParamNames.insert(col.parameterName);
        }
    }
    
    // Only send parameters that are both:
    // 1. In the cell's parameterValues (explicitly set)
    // 2. In the current pattern's column configuration (actually displayed in grid)
    // 3. Not internal parameters (note, chance)
    for (const auto& paramPair : cell.parameterValues) {
        const std::string& paramName = paramPair.first;
        float paramValue = paramPair.second;
        
        // Skip internal parameters - they're sequencer-specific and not sent to modules
        if (internalParamNames.find(paramName) != internalParamNames.end()) {
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
    // Optimized: Pass cell and duration directly to avoid redundant lookups
    if (!stepEventListeners.empty()) {
        float noteDuration = duration; // Already calculated above
        int step1Based = step + 1; // Convert to 1-based for display
        for (auto& callback : stepEventListeners) {
            callback(step1Based, noteDuration, cell);
        }
    }
    
    // NO LOGGING IN AUDIO THREAD - removed ofLogVerbose calls
}

///MARK: - DRAW
// GUI drawing methods have been moved to TrackerSequencerGUI class
// All ImGui drawing code is now in TrackerSequencerGUI::draw() and related methods

void TrackerSequencer::handleMouseClick(int x, int y, int button) {
    // Handle pattern grid clicks
    if (showGUI) {
        handlePatternGridClick(x, y);
    }
}

bool TrackerSequencer::handleKeyPress(int key, bool ctrlPressed, bool shiftPressed, GUIState& guiState) {
    // NOTE: guiState is a temporary parameter - actual state lives in TrackerSequencerGUI
    // This method modifies guiState and returns it, which gets synced back to TrackerSequencerGUI
    
    // If a cell is selected (editStep/editColumn are valid), handle special keys only
    // NOTE: Typed characters (0-9, ., -, +, *, /) are NOT processed here.
    // They are handled by TrackerSequencerGUI::processCellInput() via InputQueueCharacters during draw().
    // This prevents double-processing: InputRouter calls handleKeyPress() AND processCellInput() processes InputQueueCharacters.
    if (isValidStep(guiState.editStep) && guiState.editColumn > 0) {
        // Skip typed characters - let processCellInput() handle them via InputQueueCharacters
        if ((key >= '0' && key <= '9') || key == '.' || key == '-' || key == '+' || key == '*' || key == '/') {
            // Just enter edit mode if not already editing, then let processCellInput() handle the input
            if (!guiState.isEditingCell) {
                guiState.isEditingCell = true;
                // NOTE: Navigation remains enabled for gamepad support
            }
            // Return false to let the key pass through to ImGui so processCellInput() can process it
            return false;
        }
        
        // For special keys (Enter, Escape, Arrow keys, etc.), delegate to CellWidget
        CellWidget cell = createParameterCellForColumn(guiState.editStep, guiState.editColumn);
        
        // Sync state from GUI state to CellWidget
        cell.setSelected(true);
        if (guiState.isEditingCell) {
            // Set editing state first (this will initialize buffer with current value)
            cell.setEditing(true);
            // Then restore the cached buffer to preserve state across frames
            // This overwrites the initialized buffer with the cached one
            cell.setEditBuffer(guiState.editBufferCache, guiState.editBufferInitializedCache);
        } else {
            cell.setEditing(false);
        }
        
        // Delegate keyboard handling to ParameterCell
        bool handled = cell.handleKeyPress(key, ctrlPressed, shiftPressed);
        
        if (handled) {
            // Check state changes BEFORE syncing
            bool wasEditing = guiState.isEditingCell;
            bool nowEditing = cell.isEditingMode();
            
            // Sync edit mode state back from ParameterCell to GUI state
            guiState.isEditingCell = nowEditing;
            // Cache edit buffer for persistence across frames (ParameterCell owns the logic)
            if (nowEditing) {
                guiState.editBufferCache = cell.getEditBuffer();
                guiState.editBufferInitializedCache = cell.isEditBufferInitialized();
            } else {
                guiState.editBufferCache.clear();
                guiState.editBufferInitializedCache = false;
            }
            
            // If CellWidget exited edit mode via Enter, signal refocus needed
            // This maintains focus after saving with Enter (unified refocus system)
            // Note: Refocus is signaled via GUI state, GUI layer will handle it on next frame
            if (!nowEditing && wasEditing) {
                guiState.shouldRefocusCurrentCell = true;
            }
            
            // NOTE: Navigation remains enabled at all times for gamepad support
            // No need to disable/enable navigation when entering/exiting edit mode
            return true;
        }
        // If ParameterCell didn't handle it, fall through to grid navigation logic
    }
    
    // Handle keyboard shortcuts for pattern editing (grid navigation)
    switch (key) {
        // Enter key behavior:
        // - Enter on step number column (editColumn == 0): Trigger step
        // - Enter on data column: Enter/exit edit mode
        case OF_KEY_RETURN:
            if (ctrlPressed || shiftPressed) {
                // Ctrl+Enter or Shift+Enter: Exit grid navigation
                // If we were in edit mode, restore ImGui keyboard navigation
                if (guiState.isEditingCell) {
                    ImGuiIO& io = ImGui::GetIO();
                    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                }
                guiState.editStep = -1;
                guiState.editColumn = -1;
                guiState.isEditingCell = false;
                guiState.editBufferCache.clear();
                guiState.editBufferInitializedCache = false;
                return true;
            }
            
            // Enter key for data columns (editColumn > 0) is handled by CellWidget delegation above
            // Only handle Enter for step number column (editColumn == 0) or when no cell is selected
            if (isValidStep(guiState.editStep) && guiState.editColumn == 0) {
                // Step number column: Trigger step
                triggerStep(guiState.editStep);
                return true;
            } else if (isValidStep(guiState.editStep) && guiState.editColumn > 0) {
                // Data column: Should have been handled by CellWidget delegation above
                // If we reach here, it means CellWidget didn't handle it (cell not selected?)
                // Don't handle it here - let it fall through or return false
                return false;
            } else {
                // No cell selected - check if we're on header row
                if (guiState.editStep == -1 && !guiState.isEditingCell) {
                    // On header row - don't select first cell, let ImGui handle it
                    return false;
                }
                // No cell selected: Enter grid and select first data cell
                int stepCount = getCurrentPattern().getStepCount();
                const auto& columnConfig = getCurrentPattern().getColumnConfiguration();
                if (stepCount > 0 && !columnConfig.empty()) {
                    guiState.editStep = 0;
                    guiState.editColumn = 1;
                    guiState.isEditingCell = false;
                    guiState.editBufferCache.clear();
                    guiState.editBufferInitializedCache = false;
                    return true;
                }
            }
            return false;
            
        // Escape: Exit edit mode (should be handled by ParameterCell, but handle fallback)
        // IMPORTANT: Only handle ESC when in edit mode. When NOT in edit mode, let ESC pass through
        // to ImGui so it can use ESC to escape contained navigation contexts (like scrollable tables)
        case OF_KEY_ESC:
            if (guiState.isEditingCell) {
                guiState.isEditingCell = false;
                guiState.editBufferCache.clear();
                guiState.editBufferInitializedCache = false;
                
                // CRITICAL: Re-enable ImGui keyboard navigation when exiting edit mode
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                
                return true;
            }
            // NOT in edit mode: Let ESC pass through to ImGui for navigation escape
            return false;
            
        // Backspace and Delete: Should be handled by ParameterCell above
        case OF_KEY_BACKSPACE:
        case OF_KEY_DEL:
            // These should have been handled by ParameterCell if in edit mode
            return false;
            
        // Tab: Always let ImGui handle for panel navigation
        // (Exit edit mode is handled by clicking away or pressing Escape)
        case OF_KEY_TAB:
            return false; // Always let ImGui handle Tab for panel/window navigation
            
        // Arrow keys: 
        // - Cmd+Arrow Up/Down: Move playback step (walk through steps)
        // - In edit mode: Adjust values ONLY (no navigation) - handled by ParameterCell
        // - Not in edit mode: Let ImGui handle navigation between cells
        case OF_KEY_UP:
            if (ctrlPressed && !guiState.isEditingCell) {
                // Cmd+Up: Move playback step up
                if (isValidStep(guiState.editStep)) {
                    int stepCount = getCurrentPattern().getStepCount();
                    playbackStep = (playbackStep - 1 + stepCount) % stepCount;
                    triggerStep(playbackStep);
                    return true;
                }
                return false;
            }
            if (guiState.isEditingCell) {
                // In edit mode: Should be handled by ParameterCell above
                // Fallback: adjust value directly
                if (isValidStep(guiState.editStep) && guiState.editColumn > 0) {
                    CellWidget cell = createParameterCellForColumn(guiState.editStep, guiState.editColumn);
                    cell.setSelected(true);
                    cell.setEditing(true);
                    cell.adjustValue(1);
                    return true;
                }
                return false;
            }
            // Not in edit mode: Navigate to cell above
            if (isValidStep(guiState.editStep) && guiState.editColumn >= 0) {
                if (guiState.editStep > 0) {
                    // Move to cell above (same column)
                    guiState.editStep--;
                    return true;
                } else {
                    // At top of grid - exit grid focus to allow navigation to other widgets
                    guiState.editStep = -1;
                    guiState.editColumn = -1;
                    guiState.isEditingCell = false;
                    guiState.editBufferCache.clear();
                    guiState.editBufferInitializedCache = false;
                    return false; // Let ImGui handle navigation to other widgets
                }
            }
            // Not in edit mode: Check if on header row (editStep == -1 means no cell focused, likely on header)
            if (guiState.editStep == -1 && !guiState.isEditingCell) {
                // On header row - clear cell focus and let ImGui handle navigation naturally
                guiState.editStep = -1;
                guiState.editColumn = -1;
                guiState.isEditingCell = false;
                guiState.editBufferCache.clear();
                guiState.editBufferInitializedCache = false;
                return false; // Let ImGui handle the UP key to navigate to other widgets
            }
            // Not in edit mode: Let ImGui handle navigation
            return false;
            
        case OF_KEY_DOWN: {
            if (ctrlPressed && !guiState.isEditingCell) {
                // Cmd+Down: Move playback step down
                if (isValidStep(guiState.editStep)) {
                    int stepCount = getCurrentPattern().getStepCount();
                    playbackStep = (playbackStep + 1) % stepCount;
                    triggerStep(playbackStep);
                    return true;
                }
                return false;
            }
            if (guiState.isEditingCell) {
                // In edit mode: Should be handled by ParameterCell above
                // Fallback: adjust value directly
                if (isValidStep(guiState.editStep) && guiState.editColumn > 0) {
                    CellWidget cell = createParameterCellForColumn(guiState.editStep, guiState.editColumn);
                    cell.setSelected(true);
                    cell.setEditing(true);
                    cell.adjustValue(-1);
                    return true;
                }
                return false;
            }
            // Not in edit mode: Navigate to cell below
            if (isValidStep(guiState.editStep) && guiState.editColumn >= 0) {
                int stepCount = getCurrentPattern().getStepCount();
                if (guiState.editStep < stepCount - 1) {
                    // Move to cell below (same column)
                    guiState.editStep++;
                    return true;
                } else {
                    // At bottom of grid - exit grid focus to allow navigation to other widgets
                    guiState.editStep = -1;
                    guiState.editColumn = -1;
                    guiState.isEditingCell = false;
                    guiState.editBufferCache.clear();
                    guiState.editBufferInitializedCache = false;
                    return false; // Let ImGui handle navigation to other widgets
                }
            }
            // Not in edit mode: Let ImGui handle navigation
            return false;
        }
            
        case OF_KEY_LEFT:
            if (guiState.isEditingCell) {
                // In edit mode: Should be handled by ParameterCell above
                // Fallback: adjust value directly
                if (isValidStep(guiState.editStep) && guiState.editColumn > 0) {
                    CellWidget cell = createParameterCellForColumn(guiState.editStep, guiState.editColumn);
                    cell.setSelected(true);
                    cell.setEditing(true);
                    cell.adjustValue(-1);
                    return true;
                }
                return false;
            }
            // Not in edit mode: Navigate to cell to the left
            if (isValidStep(guiState.editStep) && guiState.editColumn >= 0) {
                if (guiState.editColumn > 1) {
                    // Move to cell to the left (decrement column)
                    guiState.editColumn--;
                    return true;
                } else if (guiState.editColumn == 1) {
                    // At first data column - move to step number column (column 0)
                    guiState.editColumn = 0;
                    return true;
                } else {
                    // At step number column (column 0) - exit grid focus
                    return false;
                }
            }
            // Not in edit mode: Let ImGui handle navigation
            return false;
            
        case OF_KEY_RIGHT:
            if (guiState.isEditingCell) {
                // In edit mode: Should be handled by ParameterCell above
                // Fallback: adjust value directly
                if (isValidStep(guiState.editStep) && guiState.editColumn > 0) {
                    CellWidget cell = createParameterCellForColumn(guiState.editStep, guiState.editColumn);
                    cell.setSelected(true);
                    cell.setEditing(true);
                    cell.adjustValue(1);
                    return true;
                }
                return false;
            }
            // Not in edit mode: Navigate to cell to the right
            if (isValidStep(guiState.editStep) && guiState.editColumn >= 0) {
                const auto& columnConfig = getCurrentPattern().getColumnConfiguration();
                int maxColumn = (int)columnConfig.size();
                if (guiState.editColumn == 0) {
                    // At step number column - move to first data column (column 1)
                    guiState.editColumn = 1;
                    return true;
                } else if (guiState.editColumn < maxColumn) {
                    // Move to cell to the right (increment column)
                    guiState.editColumn++;
                    return true;
                } else {
                    // At rightmost column - exit grid focus
                    return false;
                }
            }
            // Not in edit mode: Let ImGui handle navigation
            return false;
            
        // Pattern editing - all operations use editStep
        case 'c':
        case 'C':
            if (isValidStep(guiState.editStep)) {
                clearCell(guiState.editStep);
                return true;
            }
            break;
            
        case 'x':
        case 'X':
            // Copy from previous step
            if (isValidStep(guiState.editStep) && guiState.editStep > 0) {
                        getCurrentPattern().setCell(guiState.editStep, getCurrentPattern().getCell(guiState.editStep - 1));
                return true;
            }
            break;
            
        // Numeric input (0-9) - Blender-style: direct typing enters edit mode
        // NOTE: Don't process the key here - let TrackerSequencerGUI::processCellInput() process it from InputQueueCharacters
        // This prevents double-processing: InputRouter calls handleKeyPress() AND CellWidget processes InputQueueCharacters
        case '0': case '1': case '2': case '3': case '4': case '5': 
        case '6': case '7': case '8': case '9': {
            // Special case: index column (column 1) uses numeric keys for quick media selection
            if (isValidStep(guiState.editStep) && guiState.editColumn == 1 && !guiState.isEditingCell) {
                if (key == '0') {
                    // Clear media index (rest)
                    getCurrentPattern()[guiState.editStep].index = -1;
                    return true;
                } else {
                    int mediaIndex = key - '1';
                    if (mediaIndex < getIndexRange()) {
                        getCurrentPattern()[guiState.editStep].index = mediaIndex;
                        return true;
                    }
                }
            }
            // For parameter columns: just enter edit mode, let CellWidget handle the input
            // This is handled by the early return in the cell delegation section above
            break;
        }
        
        // Decimal point and minus sign for numeric input
        // NOTE: Don't process the key here - let TrackerSequencerGUI::processCellInput() process it from InputQueueCharacters
        case '.':
        case '-': {
            // Just enter edit mode if needed, let CellWidget handle the input
            // This is handled by the early return in the cell delegation section above
            break;
        }
        
        // Note: Numpad keys are already handled above - openFrameworks converts
        // numpad 0-9 to regular '0'-'9' characters and numpad Enter to OF_KEY_RETURN
        // So numpad support works automatically!
        
    }
    return false;
}

// Private methods
//--------------------------------------------------------------

// GUI drawing methods (drawPatternRow, drawStepNumber, drawParameterCell) 
// have been moved to TrackerSequencerGUI class

// drawAudioEnabled and drawVideoEnabled removed - A/V toggles no longer used

bool TrackerSequencer::handlePatternGridClick(int x, int y) {
    // Calculate grid position (simplified - would need proper coordinate mapping)
    // This is a placeholder implementation
    return false;
}

bool TrackerSequencer::handlePatternRowClick(int step, int column) {
    // Unused - cycling functionality removed
    return false;
}

// toggleAudio and toggleVideo removed - A/V toggles no longer used
// Audio/video always enabled when available

// Additional missing method implementations
//--------------------------------------------------------------
bool TrackerSequencer::isValidStep(int step) const {
    return step >= 0 && step < getCurrentPattern().getStepCount();
}

// Column configuration methods are now in Pattern class
// TrackerSequencer methods delegate to current pattern (see TrackerSequencer.h)

// Dynamic column drawing - drawParameterCell() has been moved to TrackerSequencerGUI class

// Edit mode helpers
//--------------------------------------------------------------
// Legacy edit methods removed - use ParameterCell methods instead
// The pending edit queue is handled via ParameterCell callbacks

bool TrackerSequencer::shouldQueueEdit(int editStep, int editColumn) const {
    return playing && isValidStep(editStep) && editStep == playbackStep && editColumn > 0;
}

void TrackerSequencer::applyPendingEdit() {
    if (!isValidStep(pendingEdit.step)) {
        return;
    }
    
    auto& cell = getCurrentPattern()[pendingEdit.step];
    
    if (pendingEdit.shouldRemove) {
        cell.removeParameter(pendingEdit.parameterName);
        setCell(pendingEdit.step, cell);
    } else if (pendingEdit.isLength) {
        cell.length = pendingEdit.lengthValue;
        setCell(pendingEdit.step, cell);
    } else if (pendingEdit.isIndex) {
        cell.index = pendingEdit.indexValue;
        setCell(pendingEdit.step, cell);
    } else if (!pendingEdit.parameterName.empty()) {
        auto range = getParameterRange(pendingEdit.parameterName);
        float clampedValue = std::max(range.first, std::min(range.second, pendingEdit.value));
        cell.setParameterValue(pendingEdit.parameterName, clampedValue);
        setCell(pendingEdit.step, cell);
    }
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

bool TrackerSequencer::handleKeyPress(ofKeyEventArgs& keyEvent, GUIState& guiState) {
    // Convert ofKeyEventArgs to existing handleKeyPress signature
    int key = keyEvent.key;
    bool ctrlPressed = keyEvent.hasModifier(OF_KEY_CONTROL);
    bool shiftPressed = keyEvent.hasModifier(OF_KEY_SHIFT);
    return handleKeyPress(key, ctrlPressed, shiftPressed, guiState);
}

std::vector<ParameterDescriptor> TrackerSequencer::getInternalParameters() const {
    std::vector<ParameterDescriptor> params;
    
    // Internal parameters: sequencer-specific, not sent to external modules
    // "note" - can replace or work alongside index column (0-127, maps to cell.index)
    params.push_back(ParameterDescriptor("note", ParameterType::INT, 0.0f, 127.0f, 60.0f, "Note"));
    // "chance" - trigger probability (0-100, controls whether step triggers)
    params.push_back(ParameterDescriptor("chance", ParameterType::INT, 0.0f, 100.0f, 100.0f, "Chance"));
    
    return params;
}

std::vector<ParameterDescriptor> TrackerSequencer::getAvailableParameters(const std::vector<ParameterDescriptor>& externalParams) const {
    std::vector<ParameterDescriptor> params;
    
    // Start with internal parameters
    auto internalParams = getInternalParameters();
    params.insert(params.end(), internalParams.begin(), internalParams.end());
    
    // Add external parameters if provided
    if (!externalParams.empty()) {
        // Filter out any internal parameters that might be in external params (safety check)
        auto internalParamNames = getInternalParameters();
        std::set<std::string> internalNames;
        for (const auto& param : internalParamNames) {
            internalNames.insert(param.name);
        }
        
        // Use a map to deduplicate by name (external params take precedence over hardcoded defaults)
        std::map<std::string, ParameterDescriptor> uniqueParams;
        
        // First add hardcoded defaults (for backward compatibility when external params don't cover them)
        uniqueParams["position"] = ParameterDescriptor("position", ParameterType::FLOAT, 0.0f, 1.0f, 0.0f, "Position");
        uniqueParams["speed"] = ParameterDescriptor("speed", ParameterType::FLOAT, -10.0f, 10.0f, 1.0f, "Speed");
        uniqueParams["volume"] = ParameterDescriptor("volume", ParameterType::FLOAT, 0.0f, 2.0f, 1.0f, "Volume");
        
        // Then add external params (will overwrite hardcoded defaults if they have the same name)
        for (const auto& param : externalParams) {
            if (internalNames.find(param.name) == internalNames.end()) {
                uniqueParams[param.name] = param; // External params take precedence
            }
        }
        
        // Convert map to vector
        for (const auto& pair : uniqueParams) {
            params.push_back(pair.second);
        }
    } else {
        // Backward compatibility: if no external params provided, return hardcoded defaults
        // This maintains existing behavior when external params are not available
    params.push_back(ParameterDescriptor("position", ParameterType::FLOAT, 0.0f, 1.0f, 0.0f, "Position"));
    params.push_back(ParameterDescriptor("speed", ParameterType::FLOAT, -10.0f, 10.0f, 1.0f, "Speed"));
        params.push_back(ParameterDescriptor("volume", ParameterType::FLOAT, 0.0f, 2.0f, 1.0f, "Volume"));
    }
    
    return params;
}

bool TrackerSequencer::isPatternEmpty() const {
    return getCurrentPattern().isEmpty();
}

void TrackerSequencer::notifyStepEvent(int step, float stepLength) {
    // step is 1-based from PatternSequencer, convert to 0-based for internal access
    const PatternCell& cell = getCell(step - 1);
    float bpm = clock ? clock->getBPM() : 120.0f;
    
    // Calculate duration in seconds using patternSequencer's stepsPerBeat
    int spb = stepsPerBeat;
    float stepDuration = (60.0f / bpm) / spb;  // Duration of ONE step
    float noteDuration = stepDuration * stepLength;     // Duration for THIS note
    
    for (auto& callback : stepEventListeners) {
        callback(step, noteDuration, cell);  // Pass 1-based step number for display
    }
}

float TrackerSequencer::getCurrentStepPosition() const {
    // Note: GUI state (editStep, editColumn) is now managed by TrackerSequencerGUI
    // This method now only returns position for the playback step
    // The GUI should handle edit step position separately if needed
    if (!isValidStep(playbackStep)) {
        return 0.0f;
    }
    
    const PatternCell& cell = getCurrentPattern()[playbackStep];
    return cell.getParameterValue("position", 0.0f);
}

void TrackerSequencer::setCurrentStepPosition(float position) {
    // Note: GUI state (editStep, editColumn, isEditingCell) is now managed by TrackerSequencerGUI
    // This method now only sets position for the playback step
    // The GUI should handle edit step position separately if needed
    
    // Clamp position to valid range
    position = std::max(0.0f, std::min(1.0f, position));
    
    if (!isValidStep(playbackStep)) {
        return;
    }
    
    PatternCell& cell = getCurrentPattern()[playbackStep];
    float oldValue = cell.getParameterValue("position", 0.0f);
    
    // Only update if value actually changed to avoid unnecessary notifications
    if (std::abs(oldValue - position) > 0.0001f) {
        cell.setParameterValue("position", position);
        // Use setCell to properly update the pattern and trigger notifications
        setCell(playbackStep, cell);
    }
}

float TrackerSequencer::getCurrentBpm() const {
    return clock ? clock->getBPM() : 120.0f;
}

// Parameter range conversion helpers
// These use actual parameter ranges from getAvailableParameters() dynamically
//--------------------------------------------------------------
std::pair<float, float> TrackerSequencer::getParameterRange(const std::string& paramName) {
    // MODULAR: Use getAvailableParameters() to get ranges dynamically
    // Create temporary instance to call getAvailableParameters() (it's non-static but doesn't depend on instance state)
    // Note: Static helper uses empty external params (backward compatibility - returns hardcoded defaults)
    static TrackerSequencer tempInstance;
    auto params = tempInstance.getAvailableParameters({});
    for (const auto& param : params) {
        if (param.name == paramName) {
            return std::make_pair(param.minValue, param.maxValue);
        }
    }
    // Default range for unknown parameters
    return std::make_pair(0.0f, 1.0f);
}

// Static helper to get default value
// MODULAR: Uses getAvailableParameters() dynamically instead of hardcoding
float TrackerSequencer::getParameterDefault(const std::string& paramName) {
    // MODULAR: Use getAvailableParameters() to get defaults dynamically
    // Create temporary instance to call getAvailableParameters() (it's non-static but doesn't depend on instance state)
    // Note: Static helper uses empty external params (backward compatibility - returns hardcoded defaults)
    static TrackerSequencer tempInstance;
    auto params = tempInstance.getAvailableParameters({});
    for (const auto& param : params) {
        if (param.name == paramName) {
            return param.defaultValue;
        }
    }
    // Fallback default
    return 0.0f;
}

// MODULAR: Get parameter type dynamically from getAvailableParameters()
ParameterType TrackerSequencer::getParameterType(const std::string& paramName) {
    // Note: Static helper uses empty external params (backward compatibility - returns hardcoded defaults)
    static TrackerSequencer tempInstance;
    auto params = tempInstance.getAvailableParameters({});
    for (const auto& param : params) {
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
    if (currentPlayingStep >= 0 && stepEndTime > 0.0f) {
        float currentTime = ofGetElapsedTimef();
        if (currentTime >= stepEndTime) {
            // Step duration expired - clear playing state
            currentPlayingStep = -1;
            stepStartTime = 0.0f;
            stepEndTime = 0.0f;
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
    for (size_t i = 0; i < patternChain.size(); i++) {
        if (patternChain[i] == index) {
            // Remove entry from pattern chain
            patternChain.erase(patternChain.begin() + i);
            i--; // Adjust index after removal
        } else if (patternChain[i] > index) {
            // Decrement indices greater than removed index
            patternChain[i]--;
        }
    }
    
    // Adjust current chain index if necessary
    if (currentChainIndex >= (int)patternChain.size()) {
        currentChainIndex = std::max(0, (int)patternChain.size() - 1);
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
    if (index >= 0 && index < (int)patternChain.size()) {
        currentChainIndex = index;
        currentChainRepeat = 0;  // Reset repeat counter
        // Update current pattern index based on pattern chain
        if (usePatternChain) {
            int patternIdx = patternChain[currentChainIndex];
            if (patternIdx >= 0 && patternIdx < (int)patterns.size()) {
                currentPatternIndex = patternIdx;
            }
        }
        ofLogNotice("TrackerSequencer") << "Set chain index to " << index;
    } else {
        ofLogWarning("TrackerSequencer") << "Invalid chain index: " << index;
    }
}

void TrackerSequencer::addToPatternChain(int patternIndex) {
    if (patternIndex < 0 || patternIndex >= (int)patterns.size()) {
        ofLogWarning("TrackerSequencer") << "Invalid pattern index for chain: " << patternIndex;
        return;
    }
    
    int newIndex = (int)patternChain.size();
    patternChain.push_back(patternIndex);
    patternChainRepeatCounts[newIndex] = 1;  // Default repeat count
    ofLogNotice("TrackerSequencer") << "Added pattern " << patternIndex << " to chain";
}

void TrackerSequencer::removeFromPatternChain(int chainIndex) {
    if (chainIndex < 0 || chainIndex >= (int)patternChain.size()) {
        ofLogWarning("TrackerSequencer") << "Invalid chain index for removal: " << chainIndex;
        return;
    }
    
    patternChain.erase(patternChain.begin() + chainIndex);
    
    // Remove repeat count and adjust indices
    patternChainRepeatCounts.erase(chainIndex);
    std::map<int, int> newRepeatCounts;
    for (const auto& pair : patternChainRepeatCounts) {
        if (pair.first < chainIndex) {
            newRepeatCounts[pair.first] = pair.second;
        } else if (pair.first > chainIndex) {
            newRepeatCounts[pair.first - 1] = pair.second;
        }
    }
    patternChainRepeatCounts = newRepeatCounts;
    
    // Adjust current chain index if necessary
    bool wasCurrentIndex = (currentChainIndex == chainIndex);
    if (currentChainIndex > chainIndex) {
        // If current index is after the removed one, decrement it
        // (the pattern that was at currentChainIndex is now at currentChainIndex - 1)
        currentChainIndex--;
    }
    // If we removed the current index, currentChainIndex stays the same
    // (it now points to the pattern that was at chainIndex+1, which shifted down)
    // If current index is out of bounds, clamp to last valid index
    if (currentChainIndex >= (int)patternChain.size()) {
        currentChainIndex = std::max(0, (int)patternChain.size() - 1);
    }
    if (wasCurrentIndex) {
        // If we removed the current index, reset repeat counter
        currentChainRepeat = 0;
    }
    
    // Switch to the pattern at the new current chain index
    if (!patternChain.empty() && currentChainIndex >= 0 && currentChainIndex < (int)patternChain.size()) {
        int newPatternIndex = patternChain[currentChainIndex];
        setCurrentPatternIndex(newPatternIndex);
    }
    
    ofLogNotice("TrackerSequencer") << "Removed chain entry at index " << chainIndex;
}

void TrackerSequencer::clearPatternChain() {
    patternChain.clear();
    patternChainRepeatCounts.clear();
    patternChainDisabled.clear();
    currentChainIndex = 0;
    currentChainRepeat = 0;
    usePatternChain = false;
    ofLogNotice("TrackerSequencer") << "Pattern chain cleared";
}

int TrackerSequencer::getPatternChainEntry(int chainIndex) const {
    if (chainIndex >= 0 && chainIndex < (int)patternChain.size()) {
        return patternChain[chainIndex];
    }
    return -1;
}

void TrackerSequencer::setPatternChainEntry(int chainIndex, int patternIndex) {
    if (chainIndex < 0) {
        ofLogWarning("TrackerSequencer") << "Invalid chain index: " << chainIndex;
        return;
    }
    
    if (patternIndex < 0 || patternIndex >= (int)patterns.size()) {
        ofLogWarning("TrackerSequencer") << "Invalid pattern index: " << patternIndex;
        return;
    }
    
    // Resize pattern chain if necessary
    if (chainIndex >= (int)patternChain.size()) {
        patternChain.resize(chainIndex + 1, 0);
        // Set default repeat count for new entries
        if (patternChainRepeatCounts.find(chainIndex) == patternChainRepeatCounts.end()) {
            patternChainRepeatCounts[chainIndex] = 1;
        }
    }
    
    patternChain[chainIndex] = patternIndex;
    ofLogNotice("TrackerSequencer") << "Set chain entry " << chainIndex << " to pattern " << patternIndex;
}

int TrackerSequencer::getPatternChainRepeatCount(int chainIndex) const {
    if (chainIndex < 0 || chainIndex >= (int)patternChain.size()) {
        return 1;  // Default repeat count
    }
    auto it = patternChainRepeatCounts.find(chainIndex);
    if (it != patternChainRepeatCounts.end()) {
        return it->second;
    }
    return 1;  // Default repeat count
}

void TrackerSequencer::setPatternChainRepeatCount(int chainIndex, int repeatCount) {
    if (chainIndex < 0 || chainIndex >= (int)patternChain.size()) {
        ofLogWarning("TrackerSequencer") << "Invalid chain index: " << chainIndex;
        return;
    }
    
    repeatCount = std::max(1, std::min(99, repeatCount));  // Clamp to 1-99
    patternChainRepeatCounts[chainIndex] = repeatCount;
    ofLogNotice("TrackerSequencer") << "Set chain entry " << chainIndex << " repeat count to " << repeatCount;
}

bool TrackerSequencer::isPatternChainEntryDisabled(int chainIndex) const {
    if (chainIndex < 0 || chainIndex >= (int)patternChain.size()) {
        return false;
    }
    auto it = patternChainDisabled.find(chainIndex);
    return (it != patternChainDisabled.end() && it->second);
}

void TrackerSequencer::setPatternChainEntryDisabled(int chainIndex, bool disabled) {
    if (chainIndex < 0 || chainIndex >= (int)patternChain.size()) {
        ofLogWarning("TrackerSequencer") << "Invalid chain index: " << chainIndex;
        return;
    }
    patternChainDisabled[chainIndex] = disabled;
    ofLogVerbose("TrackerSequencer") << "Set chain entry " << chainIndex << " disabled: " << (disabled ? "true" : "false");
}

// CellWidget adapter methods - bridge PatternCell to CellWidget
//--------------------------------------------------------------
CellWidget TrackerSequencer::createParameterCellForColumn(int step, int column) {
    // column is absolute column index (0=step number, 1+=parameter columns)
    // For parameter columns, convert to parameter-relative index: paramColIdx = column - 1
    if (!isValidStep(step) || column <= 0) {
        return CellWidget(); // Return empty cell for invalid step or step number column (column=0)
    }
    
    const auto& columnConfig = getCurrentPattern().getColumnConfiguration();
    int paramColIdx = column - 1;  // Convert absolute column index to parameter-relative index
    if (paramColIdx < 0 || paramColIdx >= (int)columnConfig.size()) {
        return CellWidget();
    }
    
    const auto& col = columnConfig[paramColIdx];
    CellWidget cell;
    
    // Configure basic properties
    cell.parameterName = col.parameterName;
    cell.isRemovable = col.isRemovable; // Columns (index, length) cannot be removed
    if (!col.isRemovable) {
        // Required columns (index, length) are always integers
        cell.isInteger = true;
        cell.stepIncrement = 1.0f;
    }
    
    // Set value range based on column type
    if (!col.isRemovable && col.parameterName == "index") {
        // Index column: 0 = rest, 1+ = media index (1-based display)
        int maxIndex = getIndexRange();
        cell.setValueRange(0.0f, (float)maxIndex, 0.0f);
        cell.getMaxIndex = [this]() { return getIndexRange(); };
    } else if (!col.isRemovable && col.parameterName == "length") {
        // Length column: 1-16 range
        cell.setValueRange(1.0f, 16.0f, 1.0f);
    } else {
        // Dynamic parameter column - use parameter ranges
        auto range = getParameterRange(col.parameterName);
        float defaultValue = getParameterDefault(col.parameterName);
        cell.setValueRange(range.first, range.second, defaultValue);
        
        // Determine if parameter is integer or float
        ParameterType paramType = getParameterType(col.parameterName);
        cell.isInteger = (paramType == ParameterType::INT);
        
        // Calculate optimal step increment based on range and type
        cell.calculateStepIncrement();
    }
    
    // Configure callbacks
    configureParameterCellCallbacks(cell, step, column);
    
    return cell;
}

void TrackerSequencer::configureParameterCellCallbacks(CellWidget& cell, int step, int column) {
    // column is absolute column index (0=step number, 1+=parameter columns)
    // For parameter columns, convert to parameter-relative index: paramColIdx = column - 1
    if (!isValidStep(step) || column <= 0) {
        return;  // Invalid step or step number column (column=0)
    }
    
    const auto& columnConfig = getCurrentPattern().getColumnConfiguration();
    int paramColIdx = column - 1;  // Convert absolute column index to parameter-relative index
    if (paramColIdx < 0 || paramColIdx >= (int)columnConfig.size()) {
        return;
    }
    
    const auto& col = columnConfig[paramColIdx];
    std::string paramName = col.parameterName; // Capture by value for lambda
    bool isRequiredCol = !col.isRemovable; // Capture by value
    std::string requiredTypeCol = !col.isRemovable ? col.parameterName : ""; // Capture by value
    
    // getCurrentValue callback - returns current value from PatternCell
    // Returns NaN to indicate empty/not set (will display as "--")
    // Unified system: all empty values (Index, Length, dynamic parameters) use NaN
    cell.getCurrentValue = [this, step, paramName, isRequiredCol, requiredTypeCol]() -> float {
        if (!isValidStep(step)) {
            // Return NaN for invalid step (will display as "--")
            return std::numeric_limits<float>::quiet_NaN();
        }
        
        auto& patternCell = getCurrentPattern()[step];
        
        if (isRequiredCol && requiredTypeCol == "index") {
            // Index: return NaN when empty (index <= 0), otherwise return 1-based display value
            int idx = patternCell.index;
            return (idx < 0) ? std::numeric_limits<float>::quiet_NaN() : (float)(idx + 1);
        } else if (isRequiredCol && requiredTypeCol == "length") {
            // Length: return NaN when index < 0 (rest), otherwise return length
            if (patternCell.index < 0) {
                return std::numeric_limits<float>::quiet_NaN(); // Use NaN instead of -1.0f
            }
            return (float)patternCell.length;
        } else {
            // Dynamic parameter: return NaN if parameter doesn't exist (will display as "--")
            // This allows parameters with negative ranges (like speed -10 to 10) to distinguish
            // between "not set" (NaN/--) and explicit values like 1.0 or -1.0
            if (!patternCell.hasParameter(paramName)) {
                return std::numeric_limits<float>::quiet_NaN();
            }
            return patternCell.getParameterValue(paramName, 0.0f);
        }
    };
    
    // onValueApplied callback - applies value to PatternCell
    cell.onValueApplied = [this, step, column, paramName, isRequiredCol, requiredTypeCol](const std::string&, float value) {
        if (!isValidStep(step)) return;
        
        // Check if we should queue this edit (playback editing)
        // Note: GUI state (editStep, editColumn) is now managed by TrackerSequencerGUI
        // Since this callback is only called for the cell being edited, we just check if it's the playback step
        bool shouldQueue = playing && isValidStep(step) && step == playbackStep && column > 0;
        
        if (shouldQueue) {
            // Queue edit for next trigger
            pendingEdit.step = step;
            pendingEdit.column = column;
            pendingEdit.parameterName = paramName;
            
            if (isRequiredCol && requiredTypeCol == "index") {
                // Index: value is 1-based display, convert to 0-based storage
                // 0 = rest (-1), 1+ = media index (0-based)
                int indexValue = (int)std::round(value);
                pendingEdit.isIndex = true;
                pendingEdit.indexValue = (indexValue == 0) ? -1 : (indexValue - 1);
            } else if (isRequiredCol && requiredTypeCol == "length") {
                // Length: clamp to 1-16
                int lengthValue = std::max(1, std::min(16, (int)std::round(value)));
                pendingEdit.isLength = true;
                pendingEdit.lengthValue = lengthValue;
            } else {
                // Dynamic parameter
                pendingEdit.value = value;
            }
            pendingEdit.shouldRemove = false;
        } else {
            // Apply immediately
            auto& patternCell = getCurrentPattern()[step];
            if (isRequiredCol && requiredTypeCol == "index") {
                // Index: value is 1-based display, convert to 0-based storage
                int indexValue = (int)std::round(value);
                patternCell.index = (indexValue == 0) ? -1 : (indexValue - 1);
            } else if (isRequiredCol && requiredTypeCol == "length") {
                // Length: clamp to 1-16
                patternCell.length = std::max(1, std::min(16, (int)std::round(value)));
            } else {
                // Dynamic parameter
                patternCell.setParameterValue(paramName, value);
            }
            setCell(step, patternCell);
        }
    };
    
    // onValueRemoved callback - removes parameter from PatternCell
    cell.onValueRemoved = [this, step, column, paramName, isRequiredCol, requiredTypeCol](const std::string&) {
        if (!isValidStep(step)) return;
        
        // Check if we should queue this edit (playback editing)
        // Note: GUI state (editStep, editColumn) is now managed by TrackerSequencerGUI
        // Since this callback is only called for the cell being edited, we just check if it's the playback step
        bool shouldQueue = playing && isValidStep(step) && step == playbackStep && column > 0;
        
        if (shouldQueue) {
            // Queue removal for next trigger
            pendingEdit.step = step;
            pendingEdit.column = column;
            pendingEdit.parameterName = paramName;
            pendingEdit.shouldRemove = true;
        } else {
            // Remove immediately (only for removable parameters)
            if (isRequiredCol) {
                // Required columns (index, length) cannot be removed - reset to default
                auto& patternCell = getCurrentPattern()[step];
                if (requiredTypeCol == "index") {
                    patternCell.index = -1; // Rest
                } else if (requiredTypeCol == "length") {
                    patternCell.length = 1; // Default length
                }
                setCell(step, patternCell);
            } else {
                // Removable parameter - remove it
                auto& patternCell = getCurrentPattern()[step];
                patternCell.removeParameter(paramName);
                setCell(step, patternCell);
            }
        }
    };
    
    // formatValue callback - tracker-specific formatting for all columns
    if (isRequiredCol && requiredTypeCol == "index") {
        // Index column: 1-based display (01-99), NaN = rest
        cell.formatValue = [](float value) -> std::string {
            if (std::isnan(value)) {
                return "--"; // Show "--" for NaN (empty/rest)
            }
            int indexVal = (int)std::round(value);
            if (indexVal <= 0) {
                return "--"; // Also handle edge case
            }
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d", indexVal);
            return std::string(buf);
        };
    } else if (isRequiredCol && requiredTypeCol == "length") {
        // Length column: 1-16 range, formatted as "02", NaN = not set
        cell.formatValue = [](float value) -> std::string {
            if (std::isnan(value)) {
                return "--"; // Show "--" for NaN (empty/not set)
            }
            int lengthVal = (int)std::round(value);
            lengthVal = std::max(1, std::min(16, lengthVal)); // Clamp to 1-16
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d", lengthVal); // Zero-padded to 2 digits
            return std::string(buf);
        };
    } else {
        // Dynamic parameter: use TrackerSequencer's formatting
        cell.formatValue = [paramName](float value) -> std::string {
            return formatParameterValue(paramName, value);
        };
    }
    
    // parseValue callback - tracker-specific parsing for index/length
    if (isRequiredCol && requiredTypeCol == "index") {
        // Index: parse as integer, handle "--" as NaN
        cell.parseValue = [](const std::string& str) -> float {
            if (str == "--" || str.empty()) {
                return std::numeric_limits<float>::quiet_NaN();
            }
            try {
                int val = std::stoi(str);
                return (float)val;
            } catch (...) {
                return std::numeric_limits<float>::quiet_NaN();
            }
        };
    } else if (isRequiredCol && requiredTypeCol == "length") {
        // Length: parse as integer (1-16), handle "--" as NaN
        cell.parseValue = [](const std::string& str) -> float {
            if (str == "--" || str.empty()) {
                return std::numeric_limits<float>::quiet_NaN();
            }
            try {
                int val = std::stoi(str);
                val = std::max(1, std::min(16, val)); // Clamp to 1-16
                return (float)val;
            } catch (...) {
                return std::numeric_limits<float>::quiet_NaN();
            }
        };
    }
    // Dynamic parameters use ParameterCell's default parsing (expression evaluation)
}

//--------------------------------------------------------------
//--------------------------------------------------------------
// Port-based routing interface (Phase 1)
std::vector<Port> TrackerSequencer::getInputPorts() const {
    // TrackerSequencer doesn't accept inputs (it's a source)
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
