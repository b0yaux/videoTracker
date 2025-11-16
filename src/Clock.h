//
//  Clock.h
//
//  Audio-rate clock - sample-accurate timing without PPQN
//

#pragma once

#include "ofMain.h"
#include "ofxSoundObjects.h"
#include "ofJson.h"

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

// Unified time event structure (replaces BeatEventData and StepEventData)
enum class TimeEventType {
    BEAT,   // Beat event (once per beat)
    STEP    // Step event (multiple per beat)
};

struct TimeEvent {
    TimeEventType type;      // BEAT or STEP
    int beatNumber;          // Beat number (valid for both types)
    int stepNumber;          // Step number (valid only for STEP type, -1 for BEAT)
    double timestamp;        // Timestamp when event occurred
    float bpm;              // Current BPM at time of event
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
    // NOTE: Clock is the SINGLE SOURCE OF TRUTH for global transport state.
    // All other components (TrackerSequencer, MediaPool, ofApp) should query
    // clock.isPlaying() rather than maintaining their own transport state.
    // This follows the BespokeSynth/SunVox pattern: master transport with derived local states.
    void start();
    void stop();
    void pause();
    void reset();
    bool isPlaying() const;  // Master transport state - single source of truth
    
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
    
    // Unified time event system for sample-accurate timing
    ofEvent<TimeEvent> timeEvent;  // Fires for both beats and steps (use type field to distinguish)
    
    // Configuration
    void setConfig(const ClockConfig& cfg);
    void setSampleRate(float rate);
    
    // Accessors for GUI
    float getBeatPulse() const;
    float getMinBPM() const;
    float getMaxBPM() const;
    float getSampleRate() const;
    
    // Serialization
    ofJson toJson() const;
    void fromJson(const ofJson& json);
    
    // Audio callback (inherited from ofxSoundOutput)
    void audioOut(ofSoundBuffer& buffer) override;
    
private:
    // State
    // Master transport state - single source of truth for global playback
    // All transport control goes through Clock (start/stop/pause/reset)
    // Other components subscribe via addTransportListener() to be notified of changes
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
