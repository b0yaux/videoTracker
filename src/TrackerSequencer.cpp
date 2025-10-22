#include "TrackerSequencer.h"
#include "MediaPool.h"
#include "Clock.h"
#include "ofxImGui.h"
#include "ofLog.h"
#include "ofJson.h"
#include "ofxTimeObjects.h"

void TrackerSequencer::PatternCell::clear() {
    mediaIndex = -1;
    position = 0.0f;
    speed = 1.0f;
    volume = 1.0f;
    stepLength = 1.0f;
    audioEnabled = true;
    videoEnabled = true;
}


bool TrackerSequencer::PatternCell::operator==(const PatternCell& other) const {
    return mediaIndex == other.mediaIndex &&
           position == other.position &&
           speed == other.speed &&
           volume == other.volume &&
           stepLength == other.stepLength &&
           audioEnabled == other.audioEnabled &&
           videoEnabled == other.videoEnabled;
}

bool TrackerSequencer::PatternCell::operator!=(const PatternCell& other) const {
    return !(*this == other);
}

std::string TrackerSequencer::PatternCell::toString() const {
    if (isEmpty()) {
        return "---";
    }
    
    std::string result = "[" + ofToString(mediaIndex) + "]";
    result += " pos:" + ofToString(position, 2);
    result += " spd:" + ofToString(speed, 2);
    result += " vol:" + ofToString(volume, 2);
    result += " len:" + ofToString(stepLength, 2);
    result += " A:" + std::string(audioEnabled ? "Y" : "N");
    result += " V:" + std::string(videoEnabled ? "Y" : "N");
    
    return result;
}

// TrackerSequencer implementation
//--------------------------------------------------------------
TrackerSequencer::TrackerSequencer() 
    : mediaPool(nullptr), clock(nullptr), numSteps(16), currentStep(0), lastTriggeredStep(-1), 
      playing(false), currentMediaStartStep(-1), 
      currentMediaStepLength(0.0f), stepsPerBeat(4), stepInterval(0.0f), 
      lastStepTime(0.0f), lastTickCount(0), currentStepStartTime(0.0f), 
      currentStepDuration(0.0f), stepActive(false), showGUI(true) {
    updateStepInterval();
}

TrackerSequencer::~TrackerSequencer() {
}

void TrackerSequencer::setup(MediaPool* pool, Clock* clockRef, int steps) {
    mediaPool = pool;
    clock = clockRef;
    numSteps = steps;
    currentStep = 0;
    
    // Initialize pattern
    pattern.resize(numSteps);
    for (int i = 0; i < numSteps; i++) {
        pattern[i] = PatternCell();
    }
    
    ofLogNotice("TrackerSequencer") << "Setup complete with " << numSteps << " steps";
}


void TrackerSequencer::setNumSteps(int steps) {
    if (steps <= 0) return;
    
    numSteps = steps;
    pattern.resize(numSteps);
    
    // Clear any steps beyond the new size
    for (int i = numSteps; i < pattern.size(); i++) {
        pattern[i] = PatternCell();
    }
    
    ofLogNotice("TrackerSequencer") << "Number of steps changed to " << numSteps;
}

void TrackerSequencer::setCell(int step, const PatternCell& cell) {
    if (!isValidStep(step)) return;
    
    pattern[step] = cell;
    
    ofLogVerbose("TrackerSequencer") << "Step " << step << " updated: " << cell.toString();
}

TrackerSequencer::PatternCell TrackerSequencer::getCell(int step) const {
    if (!isValidStep(step)) return PatternCell();
    return pattern[step];
}

void TrackerSequencer::clearCell(int step) {
    if (!isValidStep(step)) return;
    
    pattern[step].clear();
    
    ofLogVerbose("TrackerSequencer") << "Step " << step << " cleared";
}

void TrackerSequencer::clearPattern() {
    for (int i = 0; i < numSteps; i++) {
        pattern[i].clear();
    }
    
    ofLogNotice("TrackerSequencer") << "Pattern cleared";
}

void TrackerSequencer::randomizePattern() {
    if (!mediaPool) {
        ofLogWarning("TrackerSequencer") << "Cannot randomize pattern: MediaPool not set";
        return;
    }
    
    int numMedia = mediaPool->getNumPlayers();
    if (numMedia == 0) {
        ofLogWarning("TrackerSequencer") << "Cannot randomize pattern: No media available";
        return;
    }
    
    for (int i = 0; i < numSteps; i++) {
        PatternCell cell;
        
        // 70% chance of having a media item, 30% chance of being empty (rest)
        if (ofRandom(1.0f) < 0.7f) {
            cell.mediaIndex = ofRandom(0, numMedia);
            cell.position = ofRandom(0.0f, 1.0f);
            cell.speed = ofRandom(0.5f, 2.0f);
            cell.volume = ofRandom(0.3f, 1.0f);
            cell.stepLength = ofRandom(0.5f, 2.0f);
            // Keep A/V toggles unchanged - don't randomize them
            cell.audioEnabled = true;  // Always enable audio
            cell.videoEnabled = true;  // Always enable video
        } else {
            cell.clear(); // Empty/rest step
        }
        
        pattern[i] = cell;
    }
    
    ofLogNotice("TrackerSequencer") << "Pattern randomized with " << numMedia << " media items";
}

// Timing and playback control
void TrackerSequencer::update(const ofxTimeBuffer& tick) {
    if (!playing) return;
    
    // Use clock ticks instead of internal timing to stay synchronized
    // Each clock tick represents a step advancement
    uint64_t currentTickCount = tick.getAbsoluteTime();
    
    // If this is a new tick, advance to next step
    if (currentTickCount > lastTickCount) {
        // Trigger the current step first (for the first tick)
        if (lastTickCount == 0) {
            triggerStep(currentStep);
        } else {
            // Advance to next step for subsequent ticks
            currentStep = (currentStep + 1) % numSteps;
            triggerStep(currentStep);
        }
        lastTickCount = currentTickCount;
    }
}

void TrackerSequencer::updateStepInterval() {
    if (!clock) return;
    
    // Calculate time between sequencer steps based on BPM and steps per beat
    // For example: 120 BPM with 4 steps per beat = 16th notes
    // Each beat = 60/120 = 0.5 seconds
    // Each step = 0.5 / 4 = 0.125 seconds
    float bpm = clock->getBPM();
    stepInterval = (60.0f / bpm) / stepsPerBeat;
}

void TrackerSequencer::play() {
    playing = true;
    // Reset tick counter for fresh start
    lastTickCount = 0;
}

void TrackerSequencer::pause() {
    playing = false;
    // Keep current state for resume
}

void TrackerSequencer::stop() {
    playing = false;
    currentStep = 0;
    // Reset tick counter
    lastTickCount = 0;
}

void TrackerSequencer::reset() {
    currentStep = 0;
    playing = false;
    // Reset tick counter
    lastTickCount = 0;
}

void TrackerSequencer::setCurrentStep(int step) {
    if (isValidStep(step)) {
        currentStep = step;
    }
}

bool TrackerSequencer::saveState(const std::string& filename) const {
    ofJson json;
    json["numSteps"] = numSteps;
    json["currentStep"] = currentStep;
    
    ofJson patternArray = ofJson::array();
    for (int i = 0; i < numSteps; i++) {
        ofJson cellJson;
        const auto& cell = pattern[i];
        cellJson["mediaIndex"] = cell.mediaIndex;
        cellJson["position"] = cell.position;
        cellJson["speed"] = cell.speed;
        cellJson["volume"] = cell.volume;
        cellJson["stepLength"] = cell.stepLength;
        cellJson["audioEnabled"] = cell.audioEnabled;
        cellJson["videoEnabled"] = cell.videoEnabled;
        patternArray.push_back(cellJson);
    }
    json["pattern"] = patternArray;
    
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
    if (json.contains("numSteps")) {
        int loadedSteps = json["numSteps"];
        if (loadedSteps > 0) {
            setNumSteps(loadedSteps);
        }
    }
    
    if (json.contains("currentStep")) {
        currentStep = json["currentStep"];
    }
    
    // Load pattern data
    if (json.contains("pattern") && json["pattern"].is_array()) {
        auto patternArray = json["pattern"];
        int maxSteps = std::min(numSteps, (int)patternArray.size());
        
        for (int i = 0; i < maxSteps; i++) {
            if (i < patternArray.size()) {
                auto cellJson = patternArray[i];
                PatternCell cell;
                
                if (cellJson.contains("mediaIndex")) cell.mediaIndex = cellJson["mediaIndex"];
                if (cellJson.contains("position")) cell.position = cellJson["position"];
                if (cellJson.contains("speed")) cell.speed = cellJson["speed"];
                if (cellJson.contains("volume")) cell.volume = cellJson["volume"];
                if (cellJson.contains("stepLength")) cell.stepLength = cellJson["stepLength"];
                if (cellJson.contains("audioEnabled")) cell.audioEnabled = cellJson["audioEnabled"];
                if (cellJson.contains("videoEnabled")) cell.videoEnabled = cellJson["videoEnabled"];
                
                pattern[i] = cell;
            }
        }
    }
    
    ofLogNotice("TrackerSequencer") << "State loaded from " << filename;
    return true;
}

void TrackerSequencer::addStepEventListener(std::function<void(int, float, const PatternCell&)> listener) {
    stepEventListeners.push_back(listener);
}

void TrackerSequencer::triggerStep(int step) {
    if (!isValidStep(step)) return;
    if (!clock) return;

    const PatternCell& cell = getCell(step);
    float bpm = clock->getBPM();
    
    // Only trigger if there's media and it's enabled
    if (cell.mediaIndex >= 0 && mediaPool) {
        // stepLength is already in beats, no conversion needed
        notifyStepEvent(step, cell.stepLength);
        ofLogVerbose("TrackerSequencer") << "Step " << (step + 1) << " triggered at " << bpm << " BPM, length: " << cell.stepLength << " beats";
    } else {
        // Empty step - still notify but with 0 length
        notifyStepEvent(step, 0.0f);
        ofLogVerbose("TrackerSequencer") << "Step " << (step + 1) << " (empty) triggered at " << bpm << " BPM";
    }
}

void TrackerSequencer::drawPatternGrid() {
    ImGui::Separator();
    ImGui::Text("Tracker Pattern Grid:");
    
    // Tracker-style styling
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1, 1));
    
    // Use monospace font for tracker feel
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Default font
    
    // Create a compact tracker-style table
    static ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                                   ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                   ImGuiTableFlags_NoHostExtendX;
    
    if (ImGui::BeginTable("TrackerGrid", 8, flags)) {
        // Setup columns with tracker-style sizing
        ImGui::TableSetupColumn("##", ImGuiTableColumnFlags_WidthFixed, 30.0f);      // Step number
        ImGui::TableSetupColumn("SMP", ImGuiTableColumnFlags_WidthFixed, 60.0f);    // Sample/Media
        ImGui::TableSetupColumn("POS", ImGuiTableColumnFlags_WidthFixed, 50.0f);     // Position
        ImGui::TableSetupColumn("SPD", ImGuiTableColumnFlags_WidthFixed, 50.0f);     // Speed
        ImGui::TableSetupColumn("VOL", ImGuiTableColumnFlags_WidthFixed, 50.0f);     // Volume
        ImGui::TableSetupColumn("LEN", ImGuiTableColumnFlags_WidthFixed, 50.0f);      // Length
        ImGui::TableSetupColumn("A", ImGuiTableColumnFlags_WidthFixed, 25.0f);       // Audio
        ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthFixed, 25.0f);       // Video
        ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row
        
        // Header row with tracker styling
        ImGui::TableHeadersRow();
        
        // Draw pattern rows
        for (int step = 0; step < numSteps; step++) {
            drawPatternRow(step, step == currentStep);
        }
        
        ImGui::EndTable();
    }
    
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::Separator();
}

void TrackerSequencer::drawTrackerInterface() {
    // Create a comprehensive tracker window with all sequencer controls
    ImGui::SetNextWindowPos(ImVec2(300, 10), ImGuiCond_FirstUseEver);
    ImGui::Begin("Audiovisual Tracker", &showGUI);
    
    // Status section
    drawTrackerStatus();
    
    // Pattern grid
    drawPatternGrid();
    
    ImGui::End();
}

void TrackerSequencer::drawTrackerStatus() {
    // Status section with sequencer info only
    ImGui::Text("Status:");
    ImGui::Text("Current Step: %d", currentStep + 1); // Display 1-16 instead of 0-15
    ImGui::Text("Pattern Steps: %d", numSteps);
    ImGui::Text("Pattern Empty: %s", isPatternEmpty() ? "Yes" : "No");
    
    ImGui::Separator();
    
    // Pattern controls
    ImGui::Text("Pattern Controls:");
    
    // Pattern length control
    ImGui::Text("Pattern Length:");
    static int newNumSteps = 16; // Initialize with default
    newNumSteps = numSteps; // Sync with current value
    if (ImGui::SliderInt("Steps", &newNumSteps, 4, 64)) {
        if (newNumSteps != numSteps) {
            setNumSteps(newNumSteps);
        }
    }
    
    // Steps per beat control
    ImGui::Text("Steps Per Beat:");
    static int newStepsPerBeat = 4; // Initialize with default
    newStepsPerBeat = stepsPerBeat; // Sync with current value
    if (ImGui::SliderInt("SPB", &newStepsPerBeat, 1, 16)) {
        if (newStepsPerBeat != stepsPerBeat) {
            setStepsPerBeat(newStepsPerBeat);
        }
    }
    ImGui::Text("(Current: %d steps per beat = %s)", stepsPerBeat, 
                stepsPerBeat == 1 ? "whole notes" :
                stepsPerBeat == 2 ? "half notes" :
                stepsPerBeat == 4 ? "quarter notes" :
                stepsPerBeat == 8 ? "eighth notes" :
                stepsPerBeat == 16 ? "sixteenth notes" : "custom");
    
    if (ImGui::Button("Clear Pattern")) {
        clearPattern();
    }
    ImGui::SameLine();
    if (ImGui::Button("Randomize Pattern")) {
        randomizePattern();
    }
    
    ImGui::Separator();
}

void TrackerSequencer::handleMouseClick(int x, int y, int button) {
    // Handle pattern grid clicks
    if (showGUI) {
        handlePatternGridClick(x, y);
    }
}

bool TrackerSequencer::handleKeyPress(int key) {
    // Handle keyboard shortcuts for pattern editing
    switch (key) {
        // Navigation
        case OF_KEY_UP:
            if (currentStep > 0) {
                currentStep--;
                // Trigger the step event when navigating
                triggerStep(currentStep); // Use default BPM for manual navigation
                return true;
            }
            break;
            
        case OF_KEY_DOWN:
            if (currentStep < numSteps - 1) {
                currentStep++;
                // Trigger the step event when navigating
                triggerStep(currentStep); // Use default BPM for manual navigation
                return true;
            }
            break;
            
        case OF_KEY_LEFT:
        case OF_KEY_RIGHT:
            // Reserved for future track navigation
            return false;
            
        // Pattern editing
        case 'c':
        case 'C':
            if (isValidStep(currentStep)) {
                clearCell(currentStep);
                return true;
            }
            break;
            
        case 'x':
        case 'X':
            // Copy from previous step
            if (isValidStep(currentStep) && currentStep > 0) {
                pattern[currentStep] = pattern[currentStep - 1];
                ofLogNotice("TrackerSequencer") << "Copied from previous step";
                return true;
            }
            break;
            
        // Audio/Video toggles
        case 'a':
        case 'A':
            if (isValidStep(currentStep)) {
                toggleAudio(currentStep);
                return true;
            }
            break;
            
        case 'v':
        case 'V':
            if (isValidStep(currentStep)) {
                toggleVideo(currentStep);
                return true;
            }
            break;
            
        // Media selection (1-9, 0)
        case '1': case '2': case '3': case '4': case '5': 
        case '6': case '7': case '8': case '9': {
            int mediaIndex = key - '1';
            if (mediaPool && mediaIndex < (int)mediaPool->getNumPlayers()) {
                pattern[currentStep].mediaIndex = mediaIndex;
                ofLogNotice("TrackerSequencer") << "Set step " << currentStep << " to media " << mediaIndex;
                return true;
            }
            break;
        }
            
        case '0':
            // Clear media index (rest)
            pattern[currentStep].mediaIndex = -1;
            ofLogNotice("TrackerSequencer") << "Set step " << currentStep << " to rest";
            return true;
            
        // Parameter editing
        case 'p':
        case 'P':
            // Cycle position
            if (isValidStep(currentStep) && pattern[currentStep].mediaIndex >= 0) {
                float& pos = pattern[currentStep].position;
                if (pos < 0.25f) pos = 0.25f;
                else if (pos < 0.5f) pos = 0.5f;
                else if (pos < 0.75f) pos = 0.75f;
                else pos = 0.0f;
                ofLogNotice("TrackerSequencer") << "Set position to " << pos;
                return true;
            }
            break;
            
        case 's':
        case 'S':
            // Cycle speed
            if (isValidStep(currentStep) && pattern[currentStep].mediaIndex >= 0) {
                float& spd = pattern[currentStep].speed;
                if (spd < 1.0f) spd = 1.0f;
                else if (spd < 1.5f) spd = 1.5f;
                else if (spd < 2.0f) spd = 2.0f;
                else spd = 0.5f;
                ofLogNotice("TrackerSequencer") << "Set speed to " << spd;
                return true;
            }
            break;
            
        case 'w':
        case 'W':
            // Cycle volume
            if (isValidStep(currentStep) && pattern[currentStep].mediaIndex >= 0) {
                float& vol = pattern[currentStep].volume;
                if (vol < 0.5f) vol = 0.5f;
                else if (vol < 0.75f) vol = 0.75f;
                else if (vol < 1.0f) vol = 1.0f;
                else vol = 0.25f;
                ofLogNotice("TrackerSequencer") << "Set volume to " << vol;
                return true;
            }
            break;
            
        case 'l':
        case 'L':
            // Cycle step length
            if (isValidStep(currentStep) && pattern[currentStep].mediaIndex >= 0) {
                int stepCount = (int)pattern[currentStep].stepLength;
                stepCount = std::max(1, std::min(16, stepCount));
                stepCount = (stepCount % 16) + 1;
                pattern[currentStep].stepLength = (float)stepCount;
                ofLogNotice("TrackerSequencer") << "Set step length to " << stepCount;
                return true;
            }
            break;
            
        // Fine control shortcuts for precise 0-127 range adjustment
        case 'q':
        case 'Q':
            // Fine position control
            if (isValidStep(currentStep) && pattern[currentStep].mediaIndex >= 0) {
                cyclePosition(currentStep);
                return true;
            }
            break;
            
        case 'e':
        case 'E':
            // Fine speed control  
            if (isValidStep(currentStep) && pattern[currentStep].mediaIndex >= 0) {
                cycleSpeed(currentStep);
                return true;
            }
            break;
            
        case 'r':
        case 'R':
            // Fine volume control
            if (isValidStep(currentStep) && pattern[currentStep].mediaIndex >= 0) {
                cycleVolume(currentStep);
                return true;
            }
            break;
    }
    return false;
}

// Private methods
//--------------------------------------------------------------



void TrackerSequencer::drawPatternRow(int step, bool isCurrentStep) {
    ImGui::TableNextRow();
    
    // Highlight current step with intense background
    if (isCurrentStep) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 0.0f, 0.3f))); // Yellow highlight
    }
    
    // Draw all columns for this step
    drawStepNumber(step, isCurrentStep);
    drawMediaIndex(step);
    drawPosition(step);
    drawSpeed(step);
    drawVolume(step);
    drawStepLength(step);
    drawAudioEnabled(step);
    drawVideoEnabled(step);
}

void TrackerSequencer::drawStepNumber(int step, bool isCurrentStep) {
    ImGui::TableNextColumn();
    ImGui::Text("%02d", step + 1); // Decimal display (1-16)
    if (ImGui::IsItemClicked()) {
        currentStep = step;
        ofLogNotice("TrackerSequencer") << "Selected step: " << step;
    }
}

void TrackerSequencer::drawMediaIndex(int step) {
    ImGui::TableNextColumn();
    ImGui::PushID(step);
    
    const auto& cell = pattern[step];
    int currentMediaIdx = cell.mediaIndex;
    
    if (currentMediaIdx >= 0 && mediaPool && currentMediaIdx < (int)mediaPool->getNumPlayers()) {
        // Show as decimal index
        ImGui::Text("%02d", currentMediaIdx + 1);
        if (ImGui::IsItemClicked()) {
            cycleMediaIndex(step);
        }
    } else {
        ImGui::Text("--"); // Rest
        if (ImGui::IsItemClicked()) {
            cycleMediaIndex(step);
        }
    }
    
    ImGui::PopID();
}

void TrackerSequencer::drawPosition(int step) {
    ImGui::TableNextColumn();
    ImGui::PushID(step);
    
    const auto& cell = pattern[step];
    int posDec = (int)(cell.position * 127.0f);
    ImGui::Text("%03d", posDec);
    if (ImGui::IsItemClicked()) {
        cyclePosition(step);
    }
    
    ImGui::PopID();
}

void TrackerSequencer::drawSpeed(int step) {
    ImGui::TableNextColumn();
    ImGui::PushID(step);
    
    const auto& cell = pattern[step];
    int spdDec = (int)((cell.speed + 4.0f) / 8.0f * 127.0f);
    spdDec = std::max(0, std::min(127, spdDec));
    ImGui::Text("%03d", spdDec);
    if (ImGui::IsItemClicked()) {
        cycleSpeed(step);
    }
    
    ImGui::PopID();
}

void TrackerSequencer::drawVolume(int step) {
    ImGui::TableNextColumn();
    ImGui::PushID(step);
    
    const auto& cell = pattern[step];
    int volDec = (int)(cell.volume * 127.0f);
    ImGui::Text("%03d", volDec);
    if (ImGui::IsItemClicked()) {
        cycleVolume(step);
    }
    
    ImGui::PopID();
}

void TrackerSequencer::drawStepLength(int step) {
    ImGui::TableNextColumn();
    ImGui::PushID(step);
    
    const auto& cell = pattern[step];
    int stepCount = (int)cell.stepLength;
    stepCount = std::max(1, std::min(16, stepCount));
    ImGui::Text("%02d", stepCount);
    if (ImGui::IsItemClicked()) {
        cycleStepLength(step);
    }
    
    ImGui::PopID();
}

void TrackerSequencer::drawAudioEnabled(int step) {
    ImGui::TableNextColumn();
    ImGui::PushID(step);
    
    const auto& cell = pattern[step];
    ImGui::Text("%s", cell.audioEnabled ? "01" : "00");
    if (ImGui::IsItemClicked()) {
        toggleAudio(step);
    }
    
    ImGui::PopID();
}

void TrackerSequencer::drawVideoEnabled(int step) {
    ImGui::TableNextColumn();
    ImGui::PushID(step);
    
    const auto& cell = pattern[step];
    ImGui::Text("%s", cell.videoEnabled ? "01" : "00");
    if (ImGui::IsItemClicked()) {
        toggleVideo(step);
    }
    
    ImGui::PopID();
}

bool TrackerSequencer::handlePatternGridClick(int x, int y) {
    // Calculate grid position (simplified - would need proper coordinate mapping)
    // This is a placeholder implementation
    return false;
}

bool TrackerSequencer::handlePatternRowClick(int step, int column) {
    if (!isValidStep(step)) return false;
    
    // Handle column-specific clicks
    switch (column) {
        case 1: cycleMediaIndex(step); break;
        case 2: cyclePosition(step); break;
        case 3: cycleSpeed(step); break;
        case 4: cycleVolume(step); break;
        case 5: cycleStepLength(step); break;
        case 6: toggleAudio(step); break;
        case 7: toggleVideo(step); break;
        default: return false;
    }
    
    return true;
}

void TrackerSequencer::cycleMediaIndex(int step) {
    if (!isValidStep(step) || !mediaPool) return;
    
    auto& cell = pattern[step];
    int currentMediaIdx = cell.mediaIndex;
    
    // Cycle through available media
    currentMediaIdx = (currentMediaIdx + 1) % (mediaPool->getNumPlayers() + 1);
    if (currentMediaIdx == (int)mediaPool->getNumPlayers()) {
        cell.mediaIndex = -1; // Rest
    } else {
        cell.mediaIndex = currentMediaIdx;
    }
    
    setCell(step, cell);
}

void TrackerSequencer::cyclePosition(int step) {
    if (!isValidStep(step)) return;
    
    auto& cell = pattern[step];
    int posDec = (int)(cell.position * 127.0f);
    posDec = (posDec + 1) % 128; // Increment by 1 for full 0-127 range
    cell.position = posDec / 127.0f;
    
    setCell(step, cell);
}

void TrackerSequencer::cycleSpeed(int step) {
    if (!isValidStep(step)) return;
    
    auto& cell = pattern[step];
    int spdDec = (int)((cell.speed + 4.0f) / 8.0f * 127.0f);
    spdDec = (spdDec + 1) % 128; // Increment by 1 for full 0-127 range
    cell.speed = -4.0f + (spdDec / 127.0f) * 8.0f;
    
    setCell(step, cell);
}

void TrackerSequencer::cycleVolume(int step) {
    if (!isValidStep(step)) return;
    
    auto& cell = pattern[step];
    int volDec = (int)(cell.volume * 127.0f);
    volDec = (volDec + 1) % 128; // Increment by 1 for full 0-127 range
    cell.volume = volDec / 127.0f;
    
    setCell(step, cell);
}

void TrackerSequencer::cycleStepLength(int step) {
    if (!isValidStep(step)) return;
    
    auto& cell = pattern[step];
    // Cycle through step length values (how many sequencer steps the media plays for)
    int stepCount = (int)cell.stepLength;
    stepCount = (stepCount % numSteps) + 1; // Cycle 1 to pattern length
    cell.stepLength = (float)stepCount;
    
    setCell(step, cell);
}

void TrackerSequencer::toggleAudio(int step) {
    if (!isValidStep(step)) return;
    
    auto& cell = pattern[step];
    cell.audioEnabled = !cell.audioEnabled;
    
    setCell(step, cell);
}

void TrackerSequencer::toggleVideo(int step) {
    if (!isValidStep(step)) return;
    
    auto& cell = pattern[step];
    cell.videoEnabled = !cell.videoEnabled;
    
    setCell(step, cell);
}

// Additional missing method implementations
//--------------------------------------------------------------
bool TrackerSequencer::isValidStep(int step) const {
    return step >= 0 && step < numSteps;
}

bool TrackerSequencer::isPatternEmpty() const {
    for (int i = 0; i < numSteps; i++) {
        if (!pattern[i].isEmpty()) {
            return false;
        }
    }
    return true;
}

void TrackerSequencer::notifyStepEvent(int step, float stepLength) {
    const PatternCell& cell = getCell(step);
    float bpm = clock ? clock->getBPM() : 120.0f;
    
    for (auto& callback : stepEventListeners) {
        callback(step, bpm, cell);
    }
}

float TrackerSequencer::getCurrentBpm() const {
    return clock ? clock->getBPM() : 120.0f;
}


