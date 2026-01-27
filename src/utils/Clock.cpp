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
    , bpm(120.0f)
    , beatPulse(0.0f)
    , lastBeatTime(0.0f)
    , beatInterval(0.0f)
    , beatAccumulator(0.0)
    , samplesPerBeat(0.0f) {
    
    params.setName("Clock");
    params.add(bpmParam.set("BPM", 120.0f, 20.0f, 480.0f));
    
    // Listen for parameter changes
    bpmParam.addListener(this, &Clock::onBpmParamChanged);
}

//--------------------------------------------------------------
Clock::~Clock() {
    stop();
}

//--------------------------------------------------------------
void Clock::setup() {
    // Audio-rate clock doesn't need to connect to sound system
    // It will be called directly from ofApp::audioOut()
    
    ofLogNotice("Clock") << "Audio-rate clock setup complete - BPM: " << bpm.load();
}

//--------------------------------------------------------------
void Clock::setBPM(float bpm) {
    bpmParam.set(bpm);
}

void Clock::onBpmParamChanged(float& bpmValue) {
    this->bpm.store(bpmValue);
    onBPMChanged();
}


//--------------------------------------------------------------
float Clock::getBPM() const {
    return bpm.load();
}

//--------------------------------------------------------------
void Clock::start() {
    if (!playing) {
        playing = true;
        // Reset accumulator
        beatAccumulator = 0.0;
        // Don't calculate samplesPerBeat here - wait for first audioOut() call
        // to get accurate sample rate from the actual audio stream
        // This ensures sample-accurate timing from the start
        ofLogNotice("Clock") << "Audio-rate clock started at BPM: " << bpm.load() << " (will detect SR from first buffer)";
        
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
    beatAccumulator = 0.0; // Reset beat timing
    ofLogNotice("Clock") << "Audio-rate clock reset";
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
    
    // Auto-detect sample rate from buffer (only when it changes significantly)
    // Protect against spurious sample rate changes from device probing
    float bufferSampleRate = buffer.getSampleRate();
    
    // Only update if sample rate is valid (> 0) and change is significant (> 1 Hz)
    // This protects against device probing interference that might report 0 or invalid rates
    if (bufferSampleRate > 0.0f) {
        bool sampleRateChanged = abs(bufferSampleRate - sampleRate) > 1.0f;
        if (sampleRateChanged && sampleRate > 0.0f) {
            // Only update if we already have a valid sample rate (protect from initial probing)
            sampleRate = bufferSampleRate;
            // NO LOGGING IN AUDIO THREAD - removed ofLogNotice
        } else if (sampleRate <= 0.0f) {
            // First valid sample rate detection
            sampleRate = bufferSampleRate;
        }
    }
    
    // Recalculate timing when sample rate changes OR when starting (samplesPerBeat == 0)
    // This ensures we always have valid timing values
    if ((sampleRate > 0.0f) && (samplesPerBeat == 0.0f || abs(bufferSampleRate - sampleRate) > 1.0f)) {
        float currentBpm = bpm.load();
        float beatsPerSecond = currentBpm / 60.0f;
        samplesPerBeat = sampleRate / beatsPerSecond;
    }
    
    // Update samples per beat for sample-accurate timing
    float currentBpm = bpm.load();
    float beatsPerSecond = currentBpm / 60.0f;
    samplesPerBeat = sampleRate / beatsPerSecond;
    
    // Sample-accurate beat detection
    for (int i = 0; i < buffer.getNumFrames(); i++) {
        beatAccumulator += 1.0;
        
        // Check for beat event
        if (beatAccumulator >= samplesPerBeat) {
            beatAccumulator -= samplesPerBeat;
            beatCounter++;
            
            TimeEvent beatEvent;
            beatEvent.beat = beatCounter;
            beatEvent.timestamp = ofGetElapsedTimef();
            beatEvent.bpm = currentBpm;
            
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
            float currentBpm = bpm.load();
            float beatsPerSecond = currentBpm / 60.0f;
            samplesPerBeat = sampleRate / beatsPerSecond;
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
    ofSerialize(json, params);
    return json;
}

//--------------------------------------------------------------
void Clock::fromJson(const ofJson& json) {
    ofDeserialize(json, params);
    // Sync atomic bpm with loaded parameter
    bpm.store(bpmParam.get());
    onBPMChanged();
}
