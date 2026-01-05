#pragma once

#include "ofxSoundObjects.h"
#include "../core/Envelope.h"

/**
 * VoiceProcessor - Audio Source Wrapper with Envelope
 * 
 * Wraps an audio source (e.g., ofxSingleSoundPlayer) and applies
 * an ADSR envelope in real-time for click-free playback.
 * 
 * Audio processing chain:
 *   Source (audioPlayer) → VoiceProcessor (applies envelope) → Mixer
 * 
 * Usage:
 *   VoiceProcessor processor;
 *   processor.setSource(&audioPlayer);
 *   processor.getEnvelope().setAttack(5.0f);
 *   processor.getEnvelope().setRelease(20.0f);
 *   
 *   processor.trigger();  // Start playback + envelope
 *   processor.connectTo(mixer);
 *   
 *   // In audio thread, envelope is applied automatically
 *   
 *   processor.release();  // Start fade-out
 */
class VoiceProcessor : public ofxSoundObject {
public:
    VoiceProcessor();
    virtual ~VoiceProcessor();
    
    // Set audio source (e.g., ofxSingleSoundPlayer)
    void setSource(ofxSoundObject* source);
    ofxSoundObject* getSource() const { return source_; }
    
    // Envelope access
    Envelope& getEnvelope() { return envelope_; }
    const Envelope& getEnvelope() const { return envelope_; }
    
    // Voice control
    void trigger();  // Start playback + envelope (ATTACK phase)
    void release();  // Start release phase (fade-out)
    void stop();     // Immediate stop (for voice stealing - still applies minimum fade)
    
    // State queries
    bool isActive() const;
    bool isReleasing() const;
    
    // ofxSoundObject interface
    void audioOut(ofSoundBuffer& output) override;
    
private:
    ofxSoundObject* source_ = nullptr;
    Envelope envelope_;
    bool isActive_ = false;
    
    // Sample rate tracking (for envelope processing)
    float currentSampleRate_ = 44100.0f;
};



