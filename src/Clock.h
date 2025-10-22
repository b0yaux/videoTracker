//
//  Clock.h
//
//  Clock wrapper for ofxTimeObjects - single source of truth for BPM
//

#pragma once

#include "ofMain.h"
#include "ofxTimeObjects.h"

class Clock {
public:
    Clock();
    ~Clock();
    
    // Setup and configuration
    void setup();
    void setBPM(float bpm);
    float getBPM() const;
    void setTicksPerBeat(int ticks);
    int getTicksPerBeat() const;
    
    // Transport control
    void start();
    void stop();
    void pause();
    void reset();
    bool isPlaying() const;
    
    // Tick listener system
    void addTickListener(std::function<void(const ofxTimeBuffer&)> listener);
    void removeTickListener();
    
    // GUI integration
    void drawGUI();
    
private:
    // Core clock
    ofxTimeStream clock;
    
    // State
    bool playing;
    float currentBpm;
    int ticksPerBeat;
    
    // GUI state
    float bpmSlider;
    float lastBpmUpdate;
    float bpmChangeThreshold;
    bool isDragging;
    
    // Tick listener
    std::function<void(const ofxTimeBuffer&)> tickListener;
    
    // Internal methods
    void onBPMChanged();
};
