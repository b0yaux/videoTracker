#pragma once

#include "ofMain.h"

/**
 * Envelope - ADSR Envelope Generator
 * 
 * Modular, reusable envelope generator for sample-accurate audio processing.
 * Designed for use in audio thread (lock-free, no allocations).
 * 
 * State machine: IDLE → ATTACK → DECAY → SUSTAIN → RELEASE → IDLE
 * 
 * Usage:
 *   Envelope env;
 *   env.setAttack(5.0f);  // 5ms attack
 *   env.setSustain(0.8f); // 80% sustain level
 *   env.setRelease(20.0f); // 20ms release
 *   
 *   env.trigger();  // Start envelope
 *   
 *   // In audio thread:
 *   float gain = env.processSample(sampleRate);
 *   output = input * gain;
 *   
 *   env.release();  // Start release phase
 */
class Envelope {
public:
    enum class Phase {
        IDLE,      // Not active (output = 0.0)
        ATTACK,    // Rising from 0.0 to 1.0
        DECAY,     // Falling from 1.0 to sustain level
        SUSTAIN,   // Holding at sustain level
        RELEASE    // Falling from current level to 0.0
    };
    
    Envelope();
    
    // ADSR parameter setters (in milliseconds)
    void setAttack(float ms);
    void setDecay(float ms);
    void setSustain(float level);  // 0.0-1.0
    void setRelease(float ms);
    
    // Get current parameter values
    float getAttack() const { return attackMs_; }
    float getDecay() const { return decayMs_; }
    float getSustain() const { return sustainLevel_; }
    float getRelease() const { return releaseMs_; }
    
    // Control
    void trigger();  // Start envelope (IDLE → ATTACK)
    void release();  // Start release phase (any phase → RELEASE)
    void reset();    // Immediately go to IDLE (abrupt stop)
    
    // Sample-accurate processing (called from audio thread)
    // Returns current envelope level (0.0-1.0)
    // Must be called once per sample at the audio sample rate
    float processSample(float sampleRate);
    
    // State queries
    Phase getPhase() const { return currentPhase_; }
    bool isActive() const { return currentPhase_ != Phase::IDLE; }
    float getCurrentLevel() const { return currentLevel_; }
    
    // Check if envelope has completed release phase
    bool isReleased() const { return currentPhase_ == Phase::IDLE && wasReleased_; }
    
private:
    Phase currentPhase_ = Phase::IDLE;
    float currentLevel_ = 0.0f;
    bool wasReleased_ = false;  // Track if we've been through release phase
    
    // ADSR parameters (in milliseconds)
    float attackMs_ = 0.0f;
    float decayMs_ = 0.0f;
    float sustainLevel_ = 1.0f;
    float releaseMs_ = 10.0f;
    
    // ADSR parameters (in samples, calculated from ms)
    // These are recalculated when sample rate changes
    int attackSamples_ = 0;
    int decaySamples_ = 0;
    int releaseSamples_ = 0;
    float lastSampleRate_ = 0.0f;
    
    // Internal state for phase progression
    int phaseSampleCount_ = 0;
    float releaseStartLevel_ = 0.0f;  // Level when release phase started
    
    // Helper: Convert ms to samples (cached per sample rate)
    int msToSamples(float ms, float sampleRate);
    
    // Helper: Recalculate sample-based parameters
    void recalculateSampleParameters(float sampleRate);
    
    // Phase transition helpers
    void transitionToAttack();
    void transitionToDecay();
    void transitionToSustain();
    void transitionToRelease();
    void transitionToIdle();
};
