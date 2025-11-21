//
//  Clock.cpp
//
//  Audio-rate clock - sample-accurate timing without PPQN
//

#include "Clock.h"
#include <imgui.h>
#include "ofJson.h"

//--------------------------------------------------------------
Clock::Clock() 
    : playing(false)
    , currentBpm(120.0f)
    , targetBpm(120.0f)
    , beatPulse(0.0f)
    , lastBeatTime(0.0f)
    , beatInterval(0.0f)
    , sampleAccumulator(0.0)
    , beatAccumulator(0.0)
    , samplesPerStep(0.0f)
    , samplesPerBeat(0.0f)
    , stepsPerBeat(4) {
}

//--------------------------------------------------------------
Clock::~Clock() {
    stop();
}

//--------------------------------------------------------------
void Clock::setup() {
    // Audio-rate clock doesn't need to connect to sound system
    // It will be called directly from ofApp::audioOut()
    
    ofLogNotice("Clock") << "Audio-rate clock setup complete - BPM: " << currentBpm.load();
}

//--------------------------------------------------------------
void Clock::setBPM(float bpm) {
    // Silent clamping using config
    float clampedBpm = ofClamp(bpm, config.minBPM, config.maxBPM);
    if (clampedBpm > 0 && clampedBpm != targetBpm.load()) {
        targetBpm.store(clampedBpm);
        onBPMChanged();
    }
}

//--------------------------------------------------------------
float Clock::getBPM() const {
    return currentBpm.load();
}

//--------------------------------------------------------------
void Clock::start() {
    if (!playing) {
        playing = true;
        // Calculate samples per beat for immediate first beat
        float current = currentBpm.load();
        float beatsPerSecond = current / 60.0f;
        samplesPerBeat = sampleRate / beatsPerSecond; // Use current sample rate
        beatAccumulator = samplesPerBeat; // Trigger first beat immediately
        ofLogNotice("Clock") << "Audio-rate clock started at BPM: " << currentBpm.load() << " (SR: " << sampleRate << ")";
        
        // Notify transport listeners
        for (auto& listener : transportListeners) {
            listener(true);
        }
    }
}

//--------------------------------------------------------------
void Clock::stop() {
    if (playing) {
        playing = false;
        beatPulse = 0.0f; // Reset visualizer
        sampleAccumulator = 0.0; // Reset sample timing
        beatAccumulator = 0.0; // Reset beat timing
        ofLogNotice("Clock") << "Audio-rate clock stopped";
        
        // Notify transport listeners
        for (auto& listener : transportListeners) {
            listener(false);
        }
    }
}

//--------------------------------------------------------------
void Clock::pause() {
    if (playing) {
        playing = false;
        ofLogNotice("Clock") << "Audio-rate clock paused";
        
        // Notify transport listeners
        for (auto& listener : transportListeners) {
            listener(false);
        }
    }
}

//--------------------------------------------------------------
void Clock::reset() {
    playing = false;
    beatPulse = 0.0f; // Reset visualizer
    sampleAccumulator = 0.0; // Reset sample timing
    beatAccumulator = 0.0; // Reset beat timing
    ofLogNotice("Clock") << "Audio-rate clock reset";
}

//--------------------------------------------------------------
void Clock::setStepsPerBeat(int spb) {
    // Silent clamping using config
    int clampedSpb = ofClamp(spb, config.minStepsPerBeat, config.maxStepsPerBeat);
    stepsPerBeat = clampedSpb;
    ofLogNotice("Clock") << "Steps per beat set to: " << stepsPerBeat;
}

//--------------------------------------------------------------
int Clock::getStepsPerBeat() const {
    return stepsPerBeat;
}

//--------------------------------------------------------------
bool Clock::isPlaying() const {
    return playing;
}

//--------------------------------------------------------------
void Clock::addAudioListener(std::function<void(ofSoundBuffer&)> listener) {
    audioListeners.push_back(listener);
}

//--------------------------------------------------------------
void Clock::removeAudioListener() {
    audioListeners.clear();
}

//--------------------------------------------------------------
void Clock::addTransportListener(TransportCallback listener) {
    transportListeners.push_back(listener);
    ofLogNotice("Clock") << "Transport listener added (total: " << transportListeners.size() << ")";
}

//--------------------------------------------------------------
void Clock::removeTransportListener() {
    transportListeners.clear();
    ofLogNotice("Clock") << "All transport listeners removed";
}

//--------------------------------------------------------------
void Clock::audioOut(ofSoundBuffer& buffer) {
    if (!playing) return;
    
    // Auto-detect sample rate from buffer (only when it changes)
    float bufferSampleRate = buffer.getSampleRate();
    if (abs(bufferSampleRate - sampleRate) > 1.0f) {
        sampleRate = bufferSampleRate;
        // NO LOGGING IN AUDIO THREAD - removed ofLogNotice
        // Recalculate timing when sample rate changes
        float current = currentBpm.load();
        float beatsPerSecond = current / 60.0f;
        samplesPerBeat = sampleRate / beatsPerSecond;
        samplesPerStep = samplesPerBeat / stepsPerBeat;
    }
    
    // Smooth BPM changes for audio-rate transitions using config
    float current = currentBpm.load();
    float target = targetBpm.load();
    if (abs(current - target) > 0.1f) {
        current = current * (1.0f - config.bpmSmoothFactor) + target * config.bpmSmoothFactor;
        currentBpm.store(current);
    }
    
    // Update samples per beat and step for sample-accurate timing
    float beatsPerSecond = current / 60.0f;
    samplesPerBeat = sampleRate / beatsPerSecond;
    samplesPerStep = samplesPerBeat / stepsPerBeat;
    
    // Sample-accurate beat and step detection
    for (int i = 0; i < buffer.getNumFrames(); i++) {
        sampleAccumulator += 1.0;
        beatAccumulator += 1.0;
        
        // Check for step event (for TrackerSequencer)
        if (sampleAccumulator >= samplesPerStep) {
            sampleAccumulator -= samplesPerStep;
            stepCounter++;
            
            TimeEvent stepEvent;
            stepEvent.type = TimeEventType::STEP;
            stepEvent.stepNumber = stepCounter;
            stepEvent.beatNumber = beatCounter;
            stepEvent.timestamp = ofGetElapsedTimef();
            stepEvent.bpm = current;
            
            ofNotifyEvent(timeEvent, stepEvent);
        }
        
        // Check for beat event (for visualizer) - independent timing
        if (beatAccumulator >= samplesPerBeat) {
            beatAccumulator -= samplesPerBeat;
            beatCounter++;
            
            TimeEvent beatEvent;
            beatEvent.type = TimeEventType::BEAT;
            beatEvent.stepNumber = -1;  // Not applicable for beat events
            beatEvent.beatNumber = beatCounter;
            beatEvent.timestamp = ofGetElapsedTimef();
            beatEvent.bpm = current;
            
            ofNotifyEvent(timeEvent, beatEvent);
            beatPulse = 1.0f;
        }
    }
    
    // Fade the pulse over time using config
    beatPulse *= config.pulseFadeFactor;
    if (beatPulse < config.pulseThreshold) beatPulse = 0.0f;
    
    // Notify all audio listeners
    for (auto& listener : audioListeners) {
        listener(buffer);
    }
}


//--------------------------------------------------------------
void Clock::setConfig(const ClockConfig& cfg) {
    config = cfg;
    ofLogNotice("Clock") << "Configuration updated";
}

//--------------------------------------------------------------
void Clock::setSampleRate(float rate) {
    if (rate > 0 && rate != sampleRate) {
        sampleRate = rate;
        ofLogNotice("Clock") << "Sample rate set to: " << sampleRate;
        
        // Recalculate timing if playing
        if (playing) {
            float current = currentBpm.load();
            float beatsPerSecond = current / 60.0f;
            samplesPerBeat = sampleRate / beatsPerSecond;
            samplesPerStep = samplesPerBeat / stepsPerBeat;
        }
    }
}

//--------------------------------------------------------------
float Clock::getBeatPulse() const {
    return beatPulse;
}

//--------------------------------------------------------------
float Clock::getMinBPM() const {
    return config.minBPM;
}

//--------------------------------------------------------------
float Clock::getMaxBPM() const {
    return config.maxBPM;
}

//--------------------------------------------------------------
float Clock::getSampleRate() const {
    return sampleRate;
}

//--------------------------------------------------------------
void Clock::onBPMChanged() {
    // This method can be extended to notify other components
    // about BPM changes if needed in the future
}

//--------------------------------------------------------------
ofJson Clock::toJson() const {
    ofJson json;
    json["bpm"] = currentBpm.load();
    json["stepsPerBeat"] = stepsPerBeat;
    // Note: isPlaying is intentionally not saved (transient state)
    return json;
}

//--------------------------------------------------------------
void Clock::fromJson(const ofJson& json) {
    if (json.contains("bpm")) {
        float bpm = json["bpm"];
        setBPM(bpm);
    }
    if (json.contains("stepsPerBeat")) {
        int spb = json["stepsPerBeat"];
        setStepsPerBeat(spb);
    }
    // Note: isPlaying is intentionally not loaded (transient state)
}