#pragma once

#include "ofMain.h"
#include <functional>
#include "Clock.h"

// Forward declarations
class MediaPool;

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

    // Callback types for querying external state
    using IndexRangeCallback = std::function<int()>;
    
    void setup(Clock* clockRef, int steps = 16);
    void setIndexRangeCallback(IndexRangeCallback callback);
    void processAudioBuffer(ofSoundBuffer& buffer);
    void onStepEvent(StepEventData& data); // Sample-accurate step event from Clock
    
    // Event listener system
    void addStepEventListener(std::function<void(int, float, const PatternCell&)> listener);
    
    // Transport listener for Clock play/stop events
    void onClockTransportChanged(bool isPlaying);
    
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
    void advanceStep();
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
    void setStepsPerBeat(int steps);

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
    
    Clock* clock;
    
    // Pattern sequencer state (app-specific)
    int stepsPerBeat = 4;
    bool gatingEnabled = true;
    std::vector<float> stepLengths;  // Per-step gate lengths
    
    std::vector<PatternCell> pattern;
    int numSteps;
    int currentStep;
    int lastTriggeredStep;
    bool playing;  // Renamed from isPlaying to avoid conflict
    
    // Track current media playback
    int currentMediaStartStep;
    float currentMediaStepLength;
    
    // Audio-rate timing system
    double sampleAccumulator; // Sample accumulator for step timing
    float lastBpm; // Last known BPM for timing calculations
    
    // Media playback timing (separate from sequencer timing)
    float currentStepStartTime;
    float currentStepDuration;
    bool stepActive;
    
    // Step event listeners
    std::vector<std::function<void(int, float, const PatternCell&)>> stepEventListeners;
    
    // Callback for querying external state
    IndexRangeCallback indexRangeCallback;
    
    // UI state
    bool showGUI;
};








