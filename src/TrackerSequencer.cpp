#include "TrackerSequencer.h"
#include "Clock.h"
#include "Module.h"
#include "ofLog.h"
#include "ofJson.h"
#include "ofxImGui.h"  // Add this line for ImGui support
#include <cmath>  // For std::round

// TrackerSequencer implementation
//--------------------------------------------------------------
TrackerSequencer::TrackerSequencer() 
    : clock(nullptr), stepsPerBeat(4), gatingEnabled(true), playbackStep(0), editStep(-1), lastTriggeredStep(-1), 
      playing(false), currentMediaStartStep(-1), 
      currentMediaStepLength(0.0f), 
      sampleAccumulator(0.0), lastBpm(120.0f),
      draggingStep(-1), draggingColumn(-1), lastDragValue(-1), dragStartY(0.0f), dragStartX(0.0f),
      stepStartTime(0.0f), stepEndTime(0.0f),
      showGUI(true),
      currentPlayingStep(-1), shouldFocusFirstCell(false), shouldRefocusCurrentCell(false), requestFocusMoveToParent(false), parentWidgetFocused(false),
      currentPatternIndex(0), currentChainIndex(0), currentChainRepeat(0), usePatternChain(true) {
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
    editStep = -1;    // No cell selected initially (user must click to select)
    editColumn = -1;  // No column selected initially
    isEditingCell = false; // Not in edit mode initially
    editBuffer.clear(); // Clear edit buffer
    editBufferInitialized = false; // Buffer not initialized yet
    shouldFocusFirstCell = false; // No focus request initially
    shouldRefocusCurrentCell = false; // No refocus request initially
    
    // Initialize patterns (ensure at least one pattern exists)
    if (patterns.empty()) {
        patterns.push_back(Pattern(steps));
        currentPatternIndex = 0;
    } else {
        // Set step count for current pattern only (per-pattern step count)
        getCurrentPattern().setStepCount(steps);
    }
    
    // Initialize default column configuration if empty
            if (columnConfig.empty()) {
                initializeDefaultColumns();
            }
            
            // Connect to Clock's step events for sample-accurate timing
    if (clock) {
        ofAddListener(clock->stepEvent, this, &TrackerSequencer::onStepEvent);
        // Sync Clock's SPB with TrackerSequencer's SPB
        clock->setStepsPerBeat(stepsPerBeat);
        
        // Subscribe to Clock transport changes
        clock->addTransportListener([this](bool isPlaying) {
            this->onClockTransportChanged(isPlaying);
        });
    }
    
    ofLogNotice("TrackerSequencer") << "Setup complete with " << getCurrentPattern().getStepCount() << " steps";
}

void TrackerSequencer::setIndexRangeCallback(IndexRangeCallback callback) {
    indexRangeCallback = callback;
}

//--------------------------------------------------------------
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
    
    // Check if position parameter changed and notify if it's the current edit/playback step
    const PatternCell& oldCell = getCurrentPattern().getCell(step);
    float oldPosition = oldCell.getParameterValue("position", 0.0f);
    float newPosition = cell.getParameterValue("position", 0.0f);
    
    // Update the pattern
    getCurrentPattern().setCell(step, cell);
    
    // Notify if position changed and this is the current step (edit or playback)
    // BUT only if we're not actively editing the position column (to avoid interfering with edit mode)
    // MODULAR: Generic check - don't notify if we're actively editing the same parameter
    if (parameterChangeCallback && std::abs(oldPosition - newPosition) > 0.0001f) {
        if (step == editStep || step == playbackStep) {
            // Only notify if we're not actively typing in the position column
            // editColumn is 1-indexed: 0 = step number, 1+ = data columns
            bool shouldNotify = true;
            if (isEditingCell && editColumn > 0) {
                int colIdx = editColumn - 1;
                if (colIdx >= 0 && colIdx < (int)columnConfig.size()) {
                    const ColumnConfig& col = columnConfig[colIdx];
                    // MODULAR: Don't notify if we're actively editing the same parameter (user is typing)
                    // This prevents sync feedback while user is editing
                    if (col.parameterName == "position") {
                        shouldNotify = false;
                    }
                }
            }
            
            if (shouldNotify) {
                parameterChangeCallback("currentStepPosition", newPosition);
            }
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
    if (!indexRangeCallback) {
        ofLogWarning("TrackerSequencer") << "Cannot randomize pattern: IndexRangeCallback not set";
        return;
    }
    
    int numMedia = indexRangeCallback();
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
    if (columnIndex < 0 || columnIndex >= (int)columnConfig.size()) {
        ofLogWarning("TrackerSequencer") << "Invalid column index for randomization: " << columnIndex;
        return;
    }
    
    const auto& colConfig = columnConfig[columnIndex];
    
    if (colConfig.parameterName == "index") {
        // Randomize index column
        if (!indexRangeCallback) {
            ofLogWarning("TrackerSequencer") << "Cannot randomize index column: IndexRangeCallback not set";
            return;
        }
        
        int numMedia = indexRangeCallback();
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

void TrackerSequencer::onStepEvent(StepEventData& data) {
    if (!playing) return;

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

bool TrackerSequencer::saveState(const std::string& filename) const {
    ofJson json;
    json["currentStep"] = playbackStep;  // Save playback step for backward compatibility
    json["editStep"] = editStep;
    
    // Save column configuration
    ofJson columnArray = ofJson::array();
    for (const auto& col : columnConfig) {
        ofJson colJson;
        colJson["parameterName"] = col.parameterName;
        colJson["displayName"] = col.displayName;
        colJson["isFixed"] = col.isFixed;
        colJson["columnIndex"] = col.columnIndex;
        columnArray.push_back(colJson);
    }
    json["columnConfig"] = columnArray;
    
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
    
    // Load basic properties
    if (json.contains("currentStep")) {
        playbackStep = json["currentStep"];
    }
    if (json.contains("editStep")) {
        ofLogNotice("TrackerSequencer") << "[DEBUG] [SET editStep] State load - setting editStep to " << json["editStep"];
        editStep = json["editStep"];
    }
    
    // Load column configuration (migration: use defaults if missing)
    if (json.contains("columnConfig") && json["columnConfig"].is_array()) {
        columnConfig.clear();
        auto columnArray = json["columnConfig"];
        for (const auto& colJson : columnArray) {
            if (colJson.contains("parameterName") && colJson.contains("displayName")) {
                std::string paramName = colJson["parameterName"];
                std::string displayName = colJson["displayName"];
                bool isFixed = colJson.contains("isFixed") ? (bool)colJson["isFixed"] : false;
                int colIndex = colJson.contains("columnIndex") ? (int)colJson["columnIndex"] : (int)columnConfig.size();
                columnConfig.push_back(ColumnConfig(paramName, displayName, isFixed, colIndex));
            }
        }
        
        // Ensure we have at least the fixed columns (index and length)
        // MODULAR: Check for required fixed columns using isFixed flag
        bool hasIndex = false;
        bool hasLength = false;
        for (const auto& col : columnConfig) {
            if (col.isFixed && col.parameterName == "index") hasIndex = true;
            if (col.isFixed && col.parameterName == "length") hasLength = true;
        }
        
        if (!hasIndex || !hasLength || columnConfig.empty()) {
            ofLogNotice("TrackerSequencer") << "Column configuration missing or incomplete, using defaults";
            initializeDefaultColumns();
        }
    } else {
        // Migration: old file format - initialize default columns
        ofLogNotice("TrackerSequencer") << "Old pattern file format detected, initializing default column configuration";
        initializeDefaultColumns();
    }
    
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
    
    // Map PatternCell parameters to TrackerSequencer parameters
    // "note" is the sequencer's parameter name (maps to cell.index for MediaPool)
    if (cell.index >= 0) {
        triggerEvt.parameters["note"] = (float)cell.index;
    } else {
        triggerEvt.parameters["note"] = -1.0f; // Rest/empty step
    }
    
    // MODULAR: Iterate through all available parameters dynamically
    // Only add parameters to event if they are explicitly set in the cell
    // This allows position memory: if position is not set, MediaPool will use current position
    auto availableParams = getAvailableParameters();
    for (const auto& param : availableParams) {
        // Skip "note" - it's handled separately above
        if (param.name == "note") continue;
        
        // Only add parameter if explicitly set in cell
        // If not set, don't add to event - Module will use default or position memory
        if (cell.hasParameter(param.name)) {
            float value = cell.getParameterValue(param.name, param.defaultValue);
            // Validate value using parameter ranges (clamp to valid range)
            float clampedValue = std::max(param.minValue, std::min(param.maxValue, value));
            triggerEvt.parameters[param.name] = clampedValue;
        }
        // If parameter NOT set, don't add to event - Module will handle defaults/position memory
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

void TrackerSequencer::clearCellFocus() {
    // Guard: Don't clear if already cleared (prevents spam and unnecessary work)
    if (editStep == -1) {
        return;
    }
    ofLogNotice("TrackerSequencer") << "[DEBUG] [SET editStep] clearCellFocus() - clearing editStep to -1 (was: " << editStep << ")";
    editStep = -1;
    editColumn = -1;
    isEditingCell = false;
    editBuffer.clear();
    editBufferInitialized = false;
    shouldFocusFirstCell = false;
    shouldRefocusCurrentCell = false;
}

bool TrackerSequencer::handleKeyPress(int key, bool ctrlPressed, bool shiftPressed) {
    // Handle keyboard shortcuts for pattern editing
    switch (key) {
        // Enter key behavior:
        // - Enter on step number column (editColumn == 0): Trigger step
        // - Enter on data column: Enter/exit edit mode
        case OF_KEY_RETURN:
            if (ctrlPressed || shiftPressed) {
                // Ctrl+Enter or Shift+Enter: Exit grid navigation
                ofLogNotice("TrackerSequencer") << "[DEBUG] [SET editStep] Ctrl/Shift+Enter - clearing editStep to -1";
                editStep = -1;
                editColumn = -1;
                isEditingCell = false;
                editBuffer.clear();
                editBufferInitialized = false;
                shouldFocusFirstCell = false;
                return true;
            }
            
            if (isEditingCell) {
                // In edit mode: Confirm and exit edit mode
                // Always try to parse (empty buffer removes dynamic parameters, invalid input also removes)
                if (isValidStep(editStep) && editColumn > 0) {
                    parseAndApplyEditBuffer(editStep, editColumn, shouldQueueEdit());
                }
                // Preserve editStep/editColumn so cell remains selected after exiting edit mode
                // Set flag to request GUI to refocus the cell (ImGui may lose focus temporarily)
                if (isValidStep(editStep) && editColumn > 0) {
                    shouldRefocusCurrentCell = true;
                }
                isEditingCell = false;
                editBuffer.clear();
                editBufferInitialized = false;
                
                // Don't re-enable keyboard navigation immediately - let GUI refocus first
                // Keyboard navigation will be re-enabled in the next frame after refocus
                // This prevents ImGui from moving focus before we can refocus the cell
                return true;
            } else if (isValidStep(editStep) && editColumn >= 0) {
                if (editColumn == 0) {
                    // Step number column: Trigger step
                    triggerStep(editStep);
                    return true;
                }
                // Data column: Enter edit mode
                if (editColumn > 0 && editColumn <= (int)columnConfig.size()) {
                    isEditingCell = true;
                    initializeEditBuffer();
                    editBufferInitialized = true;
                    
                    // CRITICAL: Disable ImGui keyboard navigation when entering edit mode
                    // This prevents arrow keys from navigating away while editing
                    ImGuiIO& io = ImGui::GetIO();
                    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
                    
                    return true;
                }
                return false;
            } else {
                // No cell selected - check if we're on header row
                if (editStep == -1 && !isEditingCell) {
                    // On header row - don't select first cell, let ImGui handle it
                    return false;
                }
                // No cell selected: Enter grid and select first data cell
                int stepCount = getCurrentPattern().getStepCount();
                if (stepCount > 0 && !columnConfig.empty()) {
                    ofLogNotice("TrackerSequencer") << "[DEBUG] [SET editStep] Enter key - setting editStep to 0, editColumn to 1 (Enter grid)";
                    editStep = 0;
                    editColumn = 1;
                    shouldFocusFirstCell = true;
                    isEditingCell = false;
                    editBuffer.clear();
                    editBufferInitialized = false;
                    return true;
                }
            }
            return false;
            
        // Escape: Exit edit mode
        case OF_KEY_ESC:
            if (isEditingCell) {
                isEditingCell = false;
                editBuffer.clear();
                editBufferInitialized = false;
                
                // CRITICAL: Re-enable ImGui keyboard navigation when exiting edit mode
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                
                return true;
            }
            return false;
            
        // Backspace: Delete last character in edit buffer
        case OF_KEY_BACKSPACE:
            if (isEditingCell && !editBuffer.empty()) {
                editBuffer.pop_back();
                // If user edits the buffer (even with backspace), mark as user-edited
                editBufferInitialized = false;
                return true;
            }
            return false;
            
        // Delete key: Clear edit buffer
        case OF_KEY_DEL:
            if (isEditingCell) {
                editBuffer.clear();
                editBufferInitialized = false; // Clearing buffer means user is editing
                return true;
            }
            return false;
            
        // Tab: Always let ImGui handle for panel navigation
        // (Exit edit mode is handled by clicking away or pressing Escape)
        case OF_KEY_TAB:
            return false; // Always let ImGui handle Tab for panel/window navigation
            
        // Arrow keys: 
        // - Cmd+Arrow Up/Down: Move playback step (walk through steps)
        // - In edit mode: Adjust values ONLY (no navigation)
        // - Not in edit mode: Let ImGui handle navigation between cells
        case OF_KEY_UP:
            if (ctrlPressed && !isEditingCell) {
                // Cmd+Up: Move playback step up
                if (isValidStep(editStep)) {
                    int stepCount = getCurrentPattern().getStepCount();
                    playbackStep = (playbackStep - 1 + stepCount) % stepCount;
                    triggerStep(playbackStep);
                    return true;
                }
                return false;
            }
            if (isEditingCell) {
                // In edit mode: Adjust value
                // When adjusting during playback, update edit buffer but don't apply immediately
                // Edit will be applied when Enter is pressed (queued for next trigger)
                adjustParameterValue(1);
                // Force GUI refresh by ensuring edit buffer is updated
                // The GUI will display the updated edit buffer value
                return true;
            }
            // Not in edit mode: Check if at top boundary - exit grid focus
            if (isValidStep(editStep) && editStep == 0) {
                // At top of grid - exit grid focus to allow navigation to other widgets
                clearCellFocus();
                return false; // Let ImGui handle navigation to other widgets
            }
            // Not in edit mode: Check if on header row (editStep == -1 means no cell focused, likely on header)
            if (editStep == -1 && !isEditingCell) {
                // On header row - clear cell focus and let ImGui handle navigation naturally
                // This allows ImGui to move focus outside the table container
                clearCellFocus();
                return false; // Let ImGui handle the UP key to exit table navigation
            }
            // Not in edit mode: Let ImGui handle navigation
            return false;
            
        case OF_KEY_DOWN: {
            if (ctrlPressed && !isEditingCell) {
                // Cmd+Down: Move playback step down
                if (isValidStep(editStep)) {
                    int stepCount = getCurrentPattern().getStepCount();
                    playbackStep = (playbackStep + 1) % stepCount;
                    triggerStep(playbackStep);
                    return true;
                }
                return false;
            }
            if (isEditingCell) {
                // In edit mode: Adjust value
                // When adjusting during playback, update edit buffer but don't apply immediately
                // Edit will be applied when Enter is pressed (queued for next trigger)
                adjustParameterValue(-1);
                // Force GUI refresh by ensuring edit buffer is updated
                // The GUI will display the updated edit buffer value
                return true;
            }
            // Not in edit mode: Check if at bottom boundary - exit grid focus
            int stepCount = getCurrentPattern().getStepCount();
            if (isValidStep(editStep) && editStep == stepCount - 1) {
                // At bottom of grid - exit grid focus to allow navigation to other widgets
                clearCellFocus();
                return false; // Let ImGui handle navigation to other widgets
            }
            // Not in edit mode: Let ImGui handle navigation
            return false;
        }
            
        case OF_KEY_LEFT:
            if (isEditingCell) {
                // In edit mode: Adjust value
                adjustParameterValue(-1);
                return true;
            }
            // Not in edit mode: Let ImGui handle navigation
            return false;
            
        case OF_KEY_RIGHT:
            if (isEditingCell) {
                // In edit mode: Adjust value
                adjustParameterValue(1);
                return true;
            }
            // Not in edit mode: Let ImGui handle navigation
            return false;
            
        // Pattern editing - all operations use editStep
        case 'c':
        case 'C':
            if (isValidStep(editStep)) {
                clearCell(editStep);
                return true;
            }
            break;
            
        case 'x':
        case 'X':
            // Copy from previous step
            if (isValidStep(editStep) && editStep > 0) {
                        getCurrentPattern().setCell(editStep, getCurrentPattern().getCell(editStep - 1));
                return true;
            }
            break;
            
        // Numeric input (0-9) - Blender-style: direct typing enters edit mode and starts editing
        case '0': case '1': case '2': case '3': case '4': case '5': 
        case '6': case '7': case '8': case '9': {
            // Auto-enter edit mode if we have a valid cell focused (Blender-style)
            // CRITICAL: Only auto-enter edit mode if a cell is actually focused
            // Don't default to first cell - user must explicitly focus a cell first
            if (!isEditingCell) {
                // Check if we have a valid cell focused
                if (isValidStep(editStep) && editColumn > 0 && editColumn <= (int)columnConfig.size()) {
                    // We have a valid cell: enter edit mode immediately
                    isEditingCell = true;
                    editBuffer.clear();
                    editBufferInitialized = false;
                    
                    // Disable ImGui keyboard navigation when entering edit mode
                    ImGuiIO& io = ImGui::GetIO();
                    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
                } else {
                    // No valid cell focused - don't auto-enter edit mode
                    // User must explicitly focus a cell (click or navigate with arrow keys/Enter) first
                    ofLogNotice("TrackerSequencer") << "[DEBUG] Digit key ignored - no cell focused (editStep=" << editStep << ", editColumn=" << editColumn << ")";
                    return false;
                }
            } else {
                // Already in edit mode: if buffer was initialized from cell value, replace on first digit
                if (editBufferInitialized && !editBuffer.empty()) {
                    // Check if buffer is still the original initialized value (simple integer)
                    bool isSimpleInteger = true;
                    for (size_t i = 0; i < editBuffer.length(); i++) {
                        char c = editBuffer[i];
                        if (i == 0 && c == '-') continue;
                        if (c < '0' || c > '9') {
                            isSimpleInteger = false;
                            break;
                        }
                    }
                    if (isSimpleInteger) {
                        editBuffer.clear();
                        editBufferInitialized = false;
                    } else {
                        editBufferInitialized = false;
                    }
                } else {
                    editBufferInitialized = false;
                }
            }
            
            // In edit mode: append to edit buffer and apply immediately (Blender-style reactive editing)
            if (isEditingCell) {
                editBuffer += (char)key;
                // Allow up to 15 characters for longer numbers (e.g., "127", "1.56", "-0.5")
                if (editBuffer.length() > 15) {
                    editBuffer = editBuffer.substr(editBuffer.length() - 15);
                }
                
                // Apply value immediately as user types (Blender-style)
                if (!editBuffer.empty()) {
                    try {
                        float floatValue = std::stof(editBuffer);
                        
                        // Apply appropriate range based on column type
                        if (editColumn > 0 && editColumn <= (int)columnConfig.size()) {
                            int colIdx = editColumn - 1;
                            if (colIdx >= 0 && colIdx < (int)columnConfig.size()) {
                                const auto& col = columnConfig[colIdx];
                                // MODULAR: Handle fixed columns (index/length) - use isFixed flag
                                if (col.isFixed && col.parameterName == "length") {
                                    // Length must be integer between 1-16
                                    int newValue = std::max(1, std::min(16, (int)std::round(floatValue)));
                                    applyEditValue(newValue);
                                } else if (col.isFixed && col.parameterName == "index") {
                                    // Index: 0 = rest, 1+ = media index (1-based display)
                                    int maxIndex = indexRangeCallback ? indexRangeCallback() : 127;
                                    int newValue = std::max(0, std::min(maxIndex, (int)std::round(floatValue)));
                                    applyEditValue(newValue);
                                } else {
                                    // For parameter columns, accept float values directly
                                    applyEditValueFloat(floatValue, col.parameterName);
                                }
                            }
                        }
                    } catch (...) {
                        // Invalid number, ignore (e.g., incomplete decimal like ".")
                    }
                }
                return true;
            } else {
                // Not in edit mode and not on a data column: media selection (1-9, 0) for index column
                // This only works when editColumn == 0 (step number column) or when on index column
                if (!isValidStep(editStep)) return false;
                if (editColumn == 1) { // Index column
                    if (key == '0') {
                        // Clear media index (rest)
                        getCurrentPattern()[editStep].index = -1;
                        return true;
                    } else {
                        int mediaIndex = key - '1';
                        if (indexRangeCallback && mediaIndex < indexRangeCallback()) {
                            getCurrentPattern()[editStep].index = mediaIndex;
                            return true;
                        }
                    }
                }
            }
            break;
        }
        
        // Decimal point and minus sign for numeric input
        case '.':
        case '-': {
            // Auto-enter edit mode if we have a valid cell focused
            // CRITICAL: Only auto-enter edit mode if a cell is actually focused
            // Don't default to first cell - user must explicitly focus a cell first
            if (!isEditingCell) {
                // Check if we have a valid cell focused
                if (isValidStep(editStep) && editColumn > 0 && editColumn <= (int)columnConfig.size()) {
                    // We have a valid cell: enter edit mode immediately
                    isEditingCell = true;
                    editBuffer.clear();
                    
                    // Disable ImGui keyboard navigation when entering edit mode
                    ImGuiIO& io = ImGui::GetIO();
                    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
                } else {
                    // No valid cell focused - don't auto-enter edit mode
                    // User must explicitly focus a cell (click or navigate with arrow keys/Enter) first
                    ofLogNotice("TrackerSequencer") << "[DEBUG] Decimal/minus key ignored - no cell focused (editStep=" << editStep << ", editColumn=" << editColumn << ")";
                    return false;
                }
            }
            
            if (isEditingCell) {
                // CRITICAL: If buffer was initialized from cell value, clear it on first character typed
                // This allows user to type '--' or any value even if cell had a previous value
                if (editBufferInitialized && !editBuffer.empty()) {
                    editBuffer.clear();
                    editBufferInitialized = false;
                }
                
                // Allow decimal point and minus sign in edit buffer
                // For minus: allow multiple dashes (user might type '--' to clear)
                // Only allow minus at the start (if buffer has content, don't add minus)
                if (key == '-' && !editBuffer.empty() && editBuffer[0] != '-') {
                    // Minus sign only allowed at the beginning
                    return true;
                }
                // Only allow one decimal point
                if (key == '.' && editBuffer.find('.') != std::string::npos) {
                    return true;
                }
                editBuffer += (char)key;
                // Limit to 15 characters
                if (editBuffer.length() > 15) {
                    editBuffer = editBuffer.substr(editBuffer.length() - 15);
                }
                
                // Apply value immediately (Blender-style)
                // Skip if buffer is empty, single '.', or contains only dashes (any number)
                bool shouldSkipRealtimeParse = false;
                if (editBuffer.empty() || editBuffer == ".") {
                    shouldSkipRealtimeParse = true;
                } else {
                    // Check if buffer contains only dashes
                    bool onlyDashes = true;
                    for (char c : editBuffer) {
                        if (c != '-') {
                            onlyDashes = false;
                            break;
                        }
                    }
                    if (onlyDashes) {
                        shouldSkipRealtimeParse = true;
                    }
                }
                
                if (!shouldSkipRealtimeParse) {
                    try {
                        float floatValue = std::stof(editBuffer);
                        
                        // Apply appropriate range based on column type
                        if (editColumn > 0 && editColumn <= (int)columnConfig.size()) {
                            int colIdx = editColumn - 1;
                            if (colIdx >= 0 && colIdx < (int)columnConfig.size()) {
                                const auto& col = columnConfig[colIdx];
                                // MODULAR: Handle fixed columns (index/length) - use isFixed flag
                                if (col.isFixed && col.parameterName == "length") {
                                    int newValue = std::max(1, std::min(16, (int)std::round(floatValue)));
                                    applyEditValue(newValue);
                                } else if (col.isFixed && col.parameterName == "index") {
                                    int maxIndex = indexRangeCallback ? indexRangeCallback() : 127;
                                    int newValue = std::max(0, std::min(maxIndex, (int)std::round(floatValue)));
                                    applyEditValue(newValue);
                                } else {
                                    applyEditValueFloat(floatValue, col.parameterName);
                                }
                            }
                        }
                    } catch (...) {
                        // Invalid number (incomplete or non-numeric) - don't set anything
                        // Parameter will be removed when Enter is pressed if still invalid
                    }
                }
                return true;
            }
            return false;
        }
        
        // Note: Numpad keys are already handled above - openFrameworks converts
        // numpad 0-9 to regular '0'-'9' characters and numpad Enter to OF_KEY_RETURN
        // So numpad support works automatically!
        
    }
    return false;
}

// Private methods
//--------------------------------------------------------------

// GUI drawing methods removed - now in TrackerSequencerGUI class

// GUI drawing methods (drawPatternRow, drawStepNumber, drawMediaIndex, drawPosition, drawSpeed, drawVolume, drawStepLength, drawParameterCell) 
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

// Column configuration methods
//--------------------------------------------------------------
void TrackerSequencer::initializeDefaultColumns() {
    columnConfig.clear();
    columnConfig.push_back(ColumnConfig("index", "Index", true, 0));      // Fixed
    columnConfig.push_back(ColumnConfig("length", "Length", true, 1));    // Fixed
    columnConfig.push_back(ColumnConfig("position", "Position", false, 2));
    columnConfig.push_back(ColumnConfig("speed", "Speed", false, 3));
    columnConfig.push_back(ColumnConfig("volume", "Volume", false, 4));
}

void TrackerSequencer::addColumn(const std::string& parameterName, const std::string& displayName, int position) {
    // Don't allow duplicate parameter names
    for (const auto& col : columnConfig) {
        if (col.parameterName == parameterName) {
            ofLogWarning("TrackerSequencer") << "Column for parameter '" << parameterName << "' already exists";
            return;
        }
    }
    
    int insertPos = (position < 0 || position >= (int)columnConfig.size()) ? (int)columnConfig.size() : position;
    
    // Insert at specified position
    columnConfig.insert(columnConfig.begin() + insertPos, ColumnConfig(parameterName, displayName, false, insertPos));
    
    // Update column indices
    for (size_t i = 0; i < columnConfig.size(); i++) {
        columnConfig[i].columnIndex = (int)i;
    }
}

void TrackerSequencer::removeColumn(int columnIndex) {
    if (columnIndex < 0 || columnIndex >= (int)columnConfig.size()) {
        ofLogWarning("TrackerSequencer") << "Invalid column index: " << columnIndex;
        return;
    }
    
    // Don't allow removing fixed columns
    if (columnConfig[columnIndex].isFixed) {
        ofLogWarning("TrackerSequencer") << "Cannot remove fixed column: " << columnConfig[columnIndex].parameterName;
        return;
    }
    
    columnConfig.erase(columnConfig.begin() + columnIndex);
    
    // Update column indices
    for (size_t i = 0; i < columnConfig.size(); i++) {
        columnConfig[i].columnIndex = (int)i;
    }
}

void TrackerSequencer::reorderColumn(int fromIndex, int toIndex) {
    if (fromIndex < 0 || fromIndex >= (int)columnConfig.size() ||
        toIndex < 0 || toIndex >= (int)columnConfig.size()) {
        ofLogWarning("TrackerSequencer") << "Invalid column indices for reorder: " << fromIndex << " -> " << toIndex;
        return;
    }
    
    // Don't allow moving fixed columns
    if (columnConfig[fromIndex].isFixed) {
        ofLogWarning("TrackerSequencer") << "Cannot move fixed column: " << columnConfig[fromIndex].parameterName;
        return;
    }
    
    // Move the column
    ColumnConfig col = columnConfig[fromIndex];
    columnConfig.erase(columnConfig.begin() + fromIndex);
    columnConfig.insert(columnConfig.begin() + toIndex, col);
    
    // Update column indices
    for (size_t i = 0; i < columnConfig.size(); i++) {
        columnConfig[i].columnIndex = (int)i;
    }
}

bool TrackerSequencer::isColumnFixed(int columnIndex) const {
    if (columnIndex < 0 || columnIndex >= (int)columnConfig.size()) {
        return false;
    }
    return columnConfig[columnIndex].isFixed;
}

const TrackerSequencer::ColumnConfig& TrackerSequencer::getColumnConfig(int columnIndex) const {
    static ColumnConfig emptyConfig;
    if (columnIndex < 0 || columnIndex >= (int)columnConfig.size()) {
        return emptyConfig;
    }
    return columnConfig[columnIndex];
}

int TrackerSequencer::getColumnCount() const {
    return (int)columnConfig.size();
}

// Dynamic column drawing - drawParameterCell() has been moved to TrackerSequencerGUI class

// Edit mode helpers
//--------------------------------------------------------------
void TrackerSequencer::adjustParameterValue(int delta) {
    if (editColumn <= 0 || editColumn > (int)columnConfig.size() || !isValidStep(editStep)) {
        return;
    }
    
    int colIdx = editColumn - 1;
    if (colIdx < 0 || colIdx >= (int)columnConfig.size()) {
        return;
    }
    
    const auto& col = columnConfig[colIdx];
    auto& cell = getCurrentPattern()[editStep];
    
    // MODULAR: Handle fixed columns (index/length) - use isFixed flag
    if (col.isFixed && col.parameterName == "length") {
        int stepCount = cell.length;
        stepCount = std::max(1, std::min(16, stepCount));
        stepCount = std::max(1, std::min(16, stepCount + delta));
        
        // Update edit buffer for display
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", stepCount);
        editBuffer = buf;
        
        // During playback, if editing the current playback step, only update the edit buffer
        // Don't apply immediately - wait for Enter to queue the edit
        if (playing && editStep == playbackStep) {
            // Just update the edit buffer - value will be applied when Enter is pressed
            return;
        } else {
            // Not playing or editing a different step: apply immediately
            cell.length = stepCount;
            setCell(editStep, cell);
            return;
        }
    }
    
    // MODULAR: Handle fixed columns (index/length) - use isFixed flag
    if (col.isFixed && col.parameterName == "index") {
        int currentIndex = cell.index;
        int maxIndex = indexRangeCallback ? indexRangeCallback() : 127;
        // -1 = rest, 0+ = media index
        if (currentIndex < 0) {
            // Start from 0 if at rest
            currentIndex = 0;
        } else {
            currentIndex = std::max(-1, std::min(maxIndex - 1, currentIndex + delta));
        }
        
        // Update edit buffer for display
        char buf[8];
        if (currentIndex < 0) {
            snprintf(buf, sizeof(buf), "--");
        } else {
            snprintf(buf, sizeof(buf), "%02d", currentIndex + 1); // Display 1-based
        }
        editBuffer = buf;
        
        // During playback, if editing the current playback step, only update the edit buffer
        // Don't apply immediately - wait for Enter to queue the edit
        if (playing && editStep == playbackStep) {
            // Just update the edit buffer - value will be applied when Enter is pressed
            return;
        } else {
            // Not playing or editing a different step: apply immediately
            cell.index = currentIndex;
            setCell(editStep, cell);
            return;
        }
    }
    
    // Use actual default from ParameterDescriptor (position: 0.0, speed: 1.0, volume: 1.0)
    float defaultValue = getParameterDefault(col.parameterName);
    float value = cell.getParameterValue(col.parameterName, defaultValue);
    
    // For float parameters, apply delta directly to the actual value
    // Calculate a reasonable step size based on the parameter range
    auto range = getParameterRange(col.parameterName);
    float minVal = range.first;
    float maxVal = range.second;
    float rangeSize = maxVal - minVal;
    
    // Use 1% of range as step size, with minimum step of 0.01
    float stepSize = std::max(0.01f, rangeSize * 0.01f);
    float newValue = value + (delta * stepSize);
    
    // Clamp to parameter range
    newValue = std::max(minVal, std::min(maxVal, newValue));
    
    // Update edit buffer with actual float value
    // MODULAR: Use formatParameterValue() for type-based formatting
    editBuffer = formatParameterValue(col.parameterName, newValue);
    
    // During playback on current step: only update buffer (Enter will queue it)
    // Otherwise: apply immediately for real-time feedback
    if (playing && editStep == playbackStep) {
        // Edit buffer updated, will be applied when Enter is pressed
    } else {
        applyEditValueFloat(newValue, col.parameterName);
    }
}

void TrackerSequencer::applyEditValue(int displayValue) {
    if (editColumn <= 0 || editColumn > (int)columnConfig.size() || !isValidStep(editStep)) {
        return;
    }
    
    int colIdx = editColumn - 1;
    if (colIdx < 0 || colIdx >= (int)columnConfig.size()) {
        return;
    }
    
    const auto& col = columnConfig[colIdx];
    auto& cell = getCurrentPattern()[editStep];
    
    // MODULAR: Handle fixed columns (index/length) - use isFixed flag
    if (col.isFixed && col.parameterName == "length") {
        int stepCount = std::max(1, std::min(16, displayValue));
        cell.length = stepCount;
        setCell(editStep, cell);
        return;
    }
    
    // MODULAR: Handle fixed columns (index/length) - use isFixed flag
    if (col.isFixed && col.parameterName == "index") {
        // Display value is 1-based (01-99), convert to 0-based for storage
        // 0 = rest/empty, 1+ = media index (0-based)
        int maxIndex = indexRangeCallback ? indexRangeCallback() : 127;
        if (displayValue == 0) {
            cell.index = -1; // Rest
        } else {
            cell.index = std::max(0, std::min(maxIndex - 1, displayValue - 1)); // Convert 1-based to 0-based
        }
        setCell(editStep, cell);
        return;
    }
    
    // Convert from tracker display value (0-127) to actual parameter value using helper function
    float actualValue = displayValueToParameter(col.parameterName, (float)displayValue);
    
    cell.setParameterValue(col.parameterName, actualValue);
    setCell(editStep, cell);
}

void TrackerSequencer::applyEditValueFloat(float value, const std::string& parameterName) {
    if (!isValidStep(editStep)) {
        return;
    }
    
    // Get the parameter range to validate the value
    auto range = getParameterRange(parameterName);
    float minVal = range.first;
    float maxVal = range.second;
    
    // Clamp value to parameter range
    float clampedValue = std::max(minVal, std::min(maxVal, value));
    
    // Apply directly to the parameter (no display value conversion)
    auto& cell = getCurrentPattern()[editStep];
    cell.setParameterValue(parameterName, clampedValue);
    setCell(editStep, cell);
}

bool TrackerSequencer::parseAndApplyEditBuffer(int step, int column, bool queueForPlayback) {
    if (!isValidStep(step) || column <= 0 || column > (int)columnConfig.size()) {
        return false;
    }
    
    int colIdx = column - 1;
    if (colIdx < 0 || colIdx >= (int)columnConfig.size()) {
        return false;
    }
    
    const auto& col = columnConfig[colIdx];
    
    // For fixed columns, empty buffer is invalid
    if (col.isFixed && editBuffer.empty()) {
        return false;
    }
    
    // Handle empty buffer or invalid input for dynamic parameters (removes parameter, allows position memory)
    // Check for clear/empty patterns first (before trying to parse)
    if (!col.isFixed) {
        // Trim whitespace for comparison
        std::string trimmed = editBuffer;
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.back() == ' ')) {
            if (trimmed.front() == ' ') trimmed.erase(0, 1);
            if (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
        }
        
        // Check for clear patterns: empty, or strings containing only dashes (any number of '-')
        if (trimmed.empty()) {
            // Empty buffer - remove parameter
            if (queueForPlayback) {
                pendingEdit.step = step;
                pendingEdit.column = column;
                pendingEdit.parameterName = col.parameterName;
                pendingEdit.shouldRemove = true;
            } else {
                auto& cell = getCurrentPattern()[step];
                cell.removeParameter(col.parameterName);
                setCell(step, cell);
            }
            return true;
        }
        
        // Check if string contains only dashes (any number: '-', '--', '---', etc.)
        bool onlyDashes = true;
        for (char c : trimmed) {
            if (c != '-') {
                onlyDashes = false;
                break;
            }
        }
        if (onlyDashes) {
            // Only dashes - remove parameter
            if (queueForPlayback) {
                pendingEdit.step = step;
                pendingEdit.column = column;
                pendingEdit.parameterName = col.parameterName;
                pendingEdit.shouldRemove = true;
            } else {
                auto& cell = getCurrentPattern()[step];
                cell.removeParameter(col.parameterName);
                setCell(step, cell);
            }
            return true;
        }
    }
    
    // Try to parse as number
    try {
        // MODULAR: Handle fixed columns (index/length) - use isFixed flag
        if (col.isFixed && col.parameterName == "length") {
            int lengthValue = std::max(1, std::min(16, (int)std::round(std::stof(editBuffer))));
            if (queueForPlayback) {
                pendingEdit.step = step;
                pendingEdit.column = column;
                pendingEdit.parameterName = col.parameterName;
                pendingEdit.isLength = true;
                pendingEdit.lengthValue = lengthValue;
            } else {
                auto& cell = getCurrentPattern()[step];
                cell.length = lengthValue;
                setCell(step, cell);
            }
            return true;
        } else if (col.isFixed && col.parameterName == "index") {
            int maxIndex = indexRangeCallback ? indexRangeCallback() : 127;
            int indexValue = std::max(0, std::min(maxIndex, (int)std::round(std::stof(editBuffer))));
            // For index: 0 means empty/rest (index = -1), 1+ means media index (0-based)
            int finalIndex = (indexValue == 0) ? -1 : (indexValue - 1);
            if (queueForPlayback) {
                pendingEdit.step = step;
                pendingEdit.column = column;
                pendingEdit.parameterName = col.parameterName;
                pendingEdit.isIndex = true;
                pendingEdit.indexValue = finalIndex;
            } else {
                auto& cell = getCurrentPattern()[step];
                cell.index = finalIndex;
                setCell(step, cell);
            }
            return true;
        } else {
            // Dynamic parameter - parse as float
            float floatValue = std::stof(editBuffer);
            if (queueForPlayback) {
                pendingEdit.step = step;
                pendingEdit.column = column;
                pendingEdit.parameterName = col.parameterName;
                pendingEdit.value = floatValue;
            } else {
                applyEditValueFloat(floatValue, col.parameterName);
            }
            return true;
        }
    } catch (...) {
        // Parse failed - for dynamic parameters, remove it (allows position memory/defaults)
        // For fixed columns, invalid input is an error
        if (!col.isFixed) {
            if (queueForPlayback) {
                pendingEdit.step = step;
                pendingEdit.column = column;
                pendingEdit.parameterName = col.parameterName;
                pendingEdit.shouldRemove = true;
            } else {
                auto& cell = getCurrentPattern()[step];
                cell.removeParameter(col.parameterName);
                setCell(step, cell);
            }
            return true;
        }
        // Invalid value for fixed column
        return false;
    }
}

bool TrackerSequencer::shouldQueueEdit() const {
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

void TrackerSequencer::initializeEditBuffer() {
    // Initialize edit buffer with current value for arrow key editing
    if (!isValidStep(editStep) || editColumn <= 0 || editColumn > (int)columnConfig.size()) {
        editBuffer.clear();
        return;
    }
    
    const auto& cell = getCurrentPattern()[editStep];
    int colIdx = editColumn - 1;
    if (colIdx < 0 || colIdx >= (int)columnConfig.size()) {
        editBuffer.clear();
        return;
    }
    
    const auto& col = columnConfig[colIdx];
    
    // MODULAR: Handle fixed columns (index/length) - use isFixed flag
    if (col.isFixed && col.parameterName == "index") {
        int currentIndex = cell.index;
        if (currentIndex >= 0 && indexRangeCallback) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d", currentIndex + 1);
            editBuffer = buf;
        } else {
            editBuffer = "00";
        }
    } else if (col.isFixed && col.parameterName == "length") {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", cell.length);
        editBuffer = buf;
    } else {
        // Parameter column - initialize with actual float value (not converted to 0-127)
        // If parameter is not set, initialize with empty buffer (user can type to set it)
        if (!cell.hasParameter(col.parameterName)) {
            // Parameter not set - start with empty buffer (user can type value or '--' to keep it empty)
            editBuffer.clear();
        } else {
            // Parameter is set - initialize with actual value
            float defaultValue = getParameterDefault(col.parameterName);
            float value = cell.getParameterValue(col.parameterName, defaultValue);
            
            // Display actual float value with appropriate precision
            // MODULAR: Use formatParameterValue() for type-based formatting
            editBuffer = formatParameterValue(col.parameterName, value);
        }
    }
}

// Expose TrackerSequencer parameters for discovery
//--------------------------------------------------------------
std::vector<ParameterDescriptor> TrackerSequencer::getAvailableParameters() const {
    std::vector<ParameterDescriptor> params;
    
    // TrackerSequencer exposes its own parameters
    // These are the parameters that the sequencer sends in trigger events
    // Receivers map these to their own parameters (e.g., note → mediaIndex)
    params.push_back(ParameterDescriptor("note", ParameterType::INT, 0.0f, 127.0f, 60.0f, "Note"));
    params.push_back(ParameterDescriptor("position", ParameterType::FLOAT, 0.0f, 1.0f, 0.0f, "Position"));
    params.push_back(ParameterDescriptor("speed", ParameterType::FLOAT, -10.0f, 10.0f, 1.0f, "Speed"));
    params.push_back(ParameterDescriptor("volume", ParameterType::FLOAT, 0.0f, 2.0f, 1.0f, "Volume")); // Default to 1.0 (normal volume)
    
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
    // editColumn is 1-indexed: 0 = step number, 1+ = data columns (1=index, 2=length, 3=position, etc.)
    // Only return position if we're actually viewing/editing the position column
    if (editColumn > 0) {
        // Convert to 0-indexed columnConfig index
        int colIdx = editColumn - 1;
        if (colIdx >= 0 && colIdx < (int)columnConfig.size()) {
            const ColumnConfig& col = columnConfig[colIdx];
            if (col.parameterName != "position") {
                // Explicitly viewing/editing a different column (length, speed, volume, etc.), don't sync
                return 0.0f;
            }
        }
    }
    
    // Use editStep if available, otherwise playbackStep
    int step = (editStep >= 0) ? editStep : playbackStep;
    if (!isValidStep(step)) {
        return 0.0f;
    }
    
    const PatternCell& cell = getCurrentPattern()[step];
    return cell.getParameterValue("position", 0.0f);
}

void TrackerSequencer::setCurrentStepPosition(float position) {
    // editColumn is 1-indexed: 0 = step number, 1+ = data columns (1=index, 2=length, 3=position, etc.)
    // Only set position if we're actually viewing/editing the position column
    // MODULAR: Generic check - only sync if viewing/editing the target parameter
    if (editColumn > 0) {
        // Convert to 0-indexed columnConfig index
        int colIdx = editColumn - 1;
        if (colIdx >= 0 && colIdx < (int)columnConfig.size()) {
            const ColumnConfig& col = columnConfig[colIdx];
            // MODULAR: Only sync if we're viewing/editing the position column
            // If viewing a different column, don't sync (user is focused elsewhere)
            if (col.parameterName != "position") {
                return;
            }
            
            // MODULAR: Don't sync if we're actively editing the same parameter (user is typing)
            // This prevents sync from interfering with user's typing
            if (isEditingCell) {
                // Don't update while user is actively typing - let them finish editing first
                return;
            }
        }
    }
    
    // Clamp position to valid range
    position = std::max(0.0f, std::min(1.0f, position));
    
    // Use editStep if available, otherwise playbackStep
    int step = (editStep >= 0) ? editStep : playbackStep;
    if (!isValidStep(step)) {
        return;
    }
    
    PatternCell& cell = getCurrentPattern()[step];
    float oldValue = cell.getParameterValue("position", 0.0f);
    
    // Only update if value actually changed to avoid unnecessary notifications
    if (std::abs(oldValue - position) > 0.0001f) {
        cell.setParameterValue("position", position);
        // Use setCell to properly update the pattern and trigger notifications
        setCell(step, cell);
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
    static TrackerSequencer tempInstance;
    auto params = tempInstance.getAvailableParameters();
    for (const auto& param : params) {
        if (param.name == paramName) {
            return std::make_pair(param.minValue, param.maxValue);
        }
    }
    // Default range for unknown parameters
    return std::make_pair(0.0f, 1.0f);
}

float TrackerSequencer::parameterToDisplayValue(const std::string& paramName, float value) {
    auto range = getParameterRange(paramName);
    float minVal = range.first;
    float maxVal = range.second;
    float rangeSize = maxVal - minVal;
    
    if (rangeSize <= 0.0f) return 0.0f;
    
    // Normalize to 0-127 range for display (tracker-style)
    float normalized = (value - minVal) / rangeSize;
    return normalized * 127.0f;
}

float TrackerSequencer::displayValueToParameter(const std::string& paramName, float displayValue) {
    auto range = getParameterRange(paramName);
    float minVal = range.first;
    float maxVal = range.second;
    float rangeSize = maxVal - minVal;
    
    // Clamp display value to 0-127
    displayValue = std::max(0.0f, std::min(127.0f, displayValue));
    
    // Convert from 0-127 to actual parameter range
    float normalized = displayValue / 127.0f;
    return minVal + (normalized * rangeSize);
}

// Static helper to get default value
// MODULAR: Uses getAvailableParameters() dynamically instead of hardcoding
float TrackerSequencer::getParameterDefault(const std::string& paramName) {
    // MODULAR: Use getAvailableParameters() to get defaults dynamically
    // Create temporary instance to call getAvailableParameters() (it's non-static but doesn't depend on instance state)
    static TrackerSequencer tempInstance;
    auto params = tempInstance.getAvailableParameters();
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
    static TrackerSequencer tempInstance;
    auto params = tempInstance.getAvailableParameters();
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
        // Float parameters: 2 decimal places (standard for all float params)
        snprintf(buf, sizeof(buf), "%.2f", value);
    }
    
    return std::string(buf);
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


