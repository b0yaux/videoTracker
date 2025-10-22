#pragma once

#include "ofMain.h"
#include "ofxTimeObjects.h"

// Forward declarations
class MediaPool;
class Clock;

class TrackerSequencer {
public:
    struct PatternCell {
        int mediaIndex = -1;
        float position = 0.0f;
        float speed = 1.0f;
        float volume = 1.0f;
        float stepLength = 1.0f; // In beats
        bool audioEnabled = true;
        bool videoEnabled = true;

        PatternCell() = default;
        PatternCell(int mediaIdx, float pos, float spd, float vol, float len)
            : mediaIndex(mediaIdx), position(pos), speed(spd), volume(vol), stepLength(len) {}

        bool isEmpty() const { return mediaIndex < 0; }
        
        // Additional methods
        void clear();
        bool operator==(const PatternCell& other) const;
        bool operator!=(const PatternCell& other) const;
        std::string toString() const;
    };

    // Event for step triggers - using ofEvent with multiple parameters via lambda/std::function
    ofEvent<void> stepEvent;
    
    TrackerSequencer();
    ~TrackerSequencer();

    void setup(MediaPool* pool, Clock* clockRef, int steps = 16);
    void update(const ofxTimeBuffer& tick);
    
    // Event listener system
    void addStepEventListener(std::function<void(int, float, const PatternCell&)> listener);
    
    // Pattern management
    void setCell(int step, const PatternCell& cell);
    PatternCell getCell(int step) const;
    void setNumSteps(int steps);
    void clearCell(int step);
    void clearPattern();
    void randomizePattern();
    
    // Playback control
    void play();
    void pause();
    void stop();
    void reset();
    void setCurrentStep(int step);
    void triggerStep(int step);
    
    // State management
    bool loadState(const std::string& filename);
    bool saveState(const std::string& filename) const;
    
    // UI interaction
    bool handleKeyPress(int key);
    void handleMouseClick(int x, int y, int button);
    void drawTrackerInterface();
    
    // Getters
    int getNumSteps() const { return numSteps; }
    int getCurrentStep() const { return currentStep; }
    bool isPlaying() const { return playing; }
    float getCurrentBpm() const;
    int getStepsPerBeat() const { return stepsPerBeat; }
    
    // Setters
    void setStepsPerBeat(int steps) { 
        stepsPerBeat = std::max(1, std::min(16, steps)); 
        updateStepInterval();
    }

private:
    bool isValidStep(int step) const;
    bool isPatternEmpty() const;
    void notifyStepEvent(int step, float stepLength);
    void updateStepInterval();
    
    // UI drawing methods
    void drawTrackerStatus();
    void drawPatternGrid();
    void drawPatternRow(int step, bool isCurrentStep);
    void drawStepNumber(int step, bool isCurrentStep);
    void drawMediaIndex(int step);
    void drawPosition(int step);
    void drawSpeed(int step);
    void drawVolume(int step);
    void drawStepLength(int step);
    void drawAudioEnabled(int step);
    void drawVideoEnabled(int step);
    
    // Pattern interaction methods
    bool handlePatternGridClick(int x, int y);
    bool handlePatternRowClick(int step, int column);
    void cycleMediaIndex(int step);
    void cyclePosition(int step);
    void cycleSpeed(int step);
    void cycleVolume(int step);
    void cycleStepLength(int step);
    void toggleAudio(int step);
    void toggleVideo(int step);
    
    MediaPool* mediaPool;
    Clock* clock;
    std::vector<PatternCell> pattern;
    int numSteps;
    int currentStep;
    int lastTriggeredStep;
    bool playing;  // Renamed from isPlaying to avoid conflict
    
    // Track current media playback
    int currentMediaStartStep;
    float currentMediaStepLength;
    
    // Sequencer timing system
    int stepsPerBeat;  // How many sequencer steps per beat (e.g., 4 = 16th notes)
    float stepInterval; // Time between sequencer steps in seconds
    float lastStepTime; // When the last step was triggered
    uint64_t lastTickCount; // Last clock tick count for synchronization
    
    // Media playback timing (separate from sequencer timing)
    float currentStepStartTime;
    float currentStepDuration;
    bool stepActive;
    
    // Step event listeners
    std::vector<std::function<void(int, float, const PatternCell&)>> stepEventListeners;
    
    // UI state
    bool showGUI;
};








