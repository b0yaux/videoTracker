//
//  Clock.h
//
//  Audio-rate clock - sample-accurate timing without PPQN
//

#pragma once

#include "ofMain.h"
#include "ofxSoundObjects.h"
#include "ofJson.h"
#include <functional>
#include <vector>
#include <utility>
#include <cstdint>
#include <algorithm>

// Configuration structure for Clock
struct ClockConfig {
    float minBPM = 20.0f;
    float maxBPM = 480.0f;
    float bpmSmoothFactor = 0.05f;
    float pulseFadeFactor = 0.75f;
    float pulseThreshold = 0.05f;
};

// Time event structure - Clock emits beat events
// Step timing is handled independently by each TrackerSequencer instance
struct TimeEvent {
    int beat;                // Beat number
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
    float getBPM() const;  // Returns smoothed currentBpm (for audio/display)
    float getTargetBPM() const;  // Returns targetBpm (for state snapshots/script generation)
    
    // Transport control
    // NOTE: Clock is the SINGLE SOURCE OF TRUTH for global transport state.
    // All other components (TrackerSequencer, MultiSampler, ofApp) should query
    // clock.isPlaying() rather than maintaining their own transport state.
    // This follows the BespokeSynth/SunVox pattern: master transport with derived local states.
    void start();
    void stop();
    void pause();
    void reset();
    bool isPlaying() const;  // Master transport state - single source of truth
    
    // Audio-rate listener system
    void addAudioListener(std::function<void(ofSoundBuffer&)> listener);
    void removeAudioListener();
    
    // Transport listener system for play/stop events
    typedef std::function<void(bool isPlaying)> TransportCallback;
    using TransportListenerId = uint64_t;
    TransportListenerId addTransportListener(TransportCallback listener);
    void removeTransportListener(TransportListenerId id);
    void removeTransportListener();
    
    // Time event system for sample-accurate beat timing
    ofEvent<TimeEvent> timeEvent;  // Fires BEAT events only (step timing is handled by TrackerSequencer instances)
    
    // BPM change event - fires when BPM is changed (for state sync)
    ofEvent<float> bpmChangedEvent;  // Fires with new BPM value when setBPM() is called
    
    // Configuration
    void setConfig(const ClockConfig& cfg);
    void setSampleRate(float rate);
    
    // Accessors for GUI and state
    float getBeatPulse() const;
    double getCurrentBeat() const;  // Returns current beat position from accumulator
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
    
    // BPM visualizer
    float beatPulse;
    float lastBeatTime;
    float beatInterval;
    
    // Sample-accurate timing
    double beatAccumulator;
    float samplesPerBeat;
    
    // Audio listeners
    std::vector<std::function<void(ofSoundBuffer&)>> audioListeners;
    
    // Transport listeners for play/stop events - stored with ID for safe removal
    std::vector<std::pair<TransportListenerId, TransportCallback>> transportListeners;
    
    // Counter for assigning unique IDs to listeners
    TransportListenerId nextListenerId;
    
    // Internal methods
    void onBPMChanged();
};
