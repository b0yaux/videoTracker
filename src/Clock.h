//
//  Clock.h
//
//  Audio-rate clock - sample-accurate timing without PPQN
//

#pragma once

#include "ofMain.h"
#include "ofxSoundObjects.h"

// Configuration structure for Clock
struct ClockConfig {
    float minBPM = 20.0f;
    float maxBPM = 480.0f;
    int minStepsPerBeat = 1;
    int maxStepsPerBeat = 96;
    float bpmSmoothFactor = 0.05f;
    float pulseFadeFactor = 0.75f;
    float pulseThreshold = 0.05f;
};

// Event data structures
struct BeatEventData {
    int beatNumber;
    double timestamp;
    float bpm;
};

struct StepEventData {
    int stepNumber;
    int beatNumber;
    double timestamp;
    float bpm;
};

class Clock : public ofxSoundOutput {
public:
    Clock();
    ~Clock();
    
    // Setup and configuration
    void setup();
    void setBPM(float bpm);
    float getBPM() const;
    
    // Transport control
    void start();
    void stop();
    void pause();
    void reset();
    bool isPlaying() const;
    
    // Steps per beat control
    void setStepsPerBeat(int spb);
    int getStepsPerBeat() const;
    
    // Audio-rate listener system
    void addAudioListener(std::function<void(ofSoundBuffer&)> listener);
    void removeAudioListener();
    
    // Transport listener system for play/stop events
    typedef std::function<void(bool isPlaying)> TransportCallback;
    void addTransportListener(TransportCallback listener);
    void removeTransportListener();
    
    // Beat and step event systems for sample-accurate timing
    ofEvent<BeatEventData> beatEvent;  // For visualizer (once per beat)
    ofEvent<StepEventData> stepEvent;  // For TrackerSequencer (multiple per beat)
    
    // Configuration
    void setConfig(const ClockConfig& cfg);
    void setSampleRate(float rate);
    
    // Accessors for GUI
    float getBeatPulse() const;
    float getMinBPM() const;
    float getMaxBPM() const;
    float getSampleRate() const;
    
    // Audio callback (inherited from ofxSoundOutput)
    void audioOut(ofSoundBuffer& buffer) override;
    
private:
    // State
    bool playing;
    std::atomic<float> currentBpm;
    std::atomic<float> targetBpm;
    
    // Configuration
    ClockConfig config;
    float sampleRate = 44100.0f;
    int beatCounter = 0;
    int stepCounter = 0;
    
    // BPM visualizer
    float beatPulse;
    float lastBeatTime;
    float beatInterval;
    
    // Sample-accurate timing
    double sampleAccumulator;
    double beatAccumulator;
    float samplesPerStep;
    float samplesPerBeat;
    int stepsPerBeat;
    
    // Audio listeners
    std::vector<std::function<void(ofSoundBuffer&)>> audioListeners;
    
    // Transport listeners for play/stop events
    std::vector<TransportCallback> transportListeners;
    
    // Internal methods
    void onBPMChanged();
};
