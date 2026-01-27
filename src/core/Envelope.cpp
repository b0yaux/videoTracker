#include "Envelope.h"
#include "ofLog.h"

Envelope::Envelope()
    : currentPhase_(Phase::IDLE)
    , currentLevel_(0.0f)
    , wasReleased_(false)
    , attackMs_(0.0f)
    , decayMs_(0.0f)
    , sustainLevel_(1.0f)
    , releaseMs_(10.0f)
    , attackSamples_(0)
    , decaySamples_(0)
    , releaseSamples_(0)
    , lastSampleRate_(0.0f)
    , phaseSampleCount_(0)
    , releaseStartLevel_(0.0f)
{
}

void Envelope::setAttack(float ms) {
    attackMs_ = std::max(0.0f, ms);
    if (lastSampleRate_ > 0.0f) {
        attackSamples_ = msToSamples(attackMs_, lastSampleRate_);
    }
}

void Envelope::setDecay(float ms) {
    decayMs_ = std::max(0.0f, ms);
    if (lastSampleRate_ > 0.0f) {
        decaySamples_ = msToSamples(decayMs_, lastSampleRate_);
    }
}

void Envelope::setSustain(float level) {
    sustainLevel_ = ofClamp(level, 0.0f, 1.0f);
}

void Envelope::setRelease(float ms) {
    releaseMs_ = std::max(0.0f, ms);
    if (lastSampleRate_ > 0.0f) {
        releaseSamples_ = msToSamples(releaseMs_, lastSampleRate_);
    }
}

void Envelope::trigger() {
    if (currentPhase_ == Phase::IDLE) {
        transitionToAttack();
    } else if (currentPhase_ == Phase::RELEASE) {
        // Retrigger during release: start attack from current level
        transitionToAttack();
    } else {
        // Retrigger during attack/decay/sustain: restart from beginning
        transitionToAttack();
    }
}

void Envelope::release() {
    if (currentPhase_ != Phase::IDLE && currentPhase_ != Phase::RELEASE) {
        transitionToRelease();
    }
}

void Envelope::reset() {
    transitionToIdle();
}

float Envelope::processSample(float sampleRate) {
    // Recalculate sample-based parameters if sample rate changed
    if (sampleRate != lastSampleRate_) {
        recalculateSampleParameters(sampleRate);
    }
    
    // Process current phase
    switch (currentPhase_) {
        case Phase::IDLE:
            // Already at 0.0, no processing needed
            break;
            
        case Phase::ATTACK:
            if (attackSamples_ > 0) {
                // Linear attack: 0.0 → 1.0 over attackSamples_
                currentLevel_ = static_cast<float>(phaseSampleCount_) / static_cast<float>(attackSamples_);
                phaseSampleCount_++;
                
                if (phaseSampleCount_ >= attackSamples_) {
                    currentLevel_ = 1.0f;
                    transitionToDecay();
                }
            } else {
                // Instant attack (0ms)
                currentLevel_ = 1.0f;
                transitionToDecay();
            }
            break;
            
        case Phase::DECAY:
            if (decaySamples_ > 0) {
                // Linear decay: 1.0 → sustainLevel_ over decaySamples_
                float progress = static_cast<float>(phaseSampleCount_) / static_cast<float>(decaySamples_);
                currentLevel_ = 1.0f - (progress * (1.0f - sustainLevel_));
                phaseSampleCount_++;
                
                if (phaseSampleCount_ >= decaySamples_) {
                    currentLevel_ = sustainLevel_;
                    transitionToSustain();
                }
            } else {
                // Instant decay (0ms)
                currentLevel_ = sustainLevel_;
                transitionToSustain();
            }
            break;
            
        case Phase::SUSTAIN:
            // Hold at sustain level (no processing needed)
            currentLevel_ = sustainLevel_;
            break;
            
        case Phase::RELEASE:
            if (releaseSamples_ > 0) {
                // Linear release: releaseStartLevel_ → 0.0 over releaseSamples_
                float progress = static_cast<float>(phaseSampleCount_) / static_cast<float>(releaseSamples_);
                currentLevel_ = releaseStartLevel_ * (1.0f - progress);
                phaseSampleCount_++;
                
                if (phaseSampleCount_ >= releaseSamples_ || currentLevel_ <= 0.0f) {
                    currentLevel_ = 0.0f;
                    transitionToIdle();
                }
            } else {
                // Instant release (0ms)
                currentLevel_ = 0.0f;
                transitionToIdle();
            }
            break;
    }
    
    return currentLevel_;
}

int Envelope::msToSamples(float ms, float sampleRate) {
    return static_cast<int>(ms * sampleRate / 1000.0f);
}

void Envelope::recalculateSampleParameters(float sampleRate) {
    attackSamples_ = msToSamples(attackMs_, sampleRate);
    decaySamples_ = msToSamples(decayMs_, sampleRate);
    releaseSamples_ = msToSamples(releaseMs_, sampleRate);
    lastSampleRate_ = sampleRate;
}

void Envelope::transitionToAttack() {
    currentPhase_ = Phase::ATTACK;
    phaseSampleCount_ = 0;
    currentLevel_ = 0.0f;
    wasReleased_ = false;
}

void Envelope::transitionToDecay() {
    currentPhase_ = Phase::DECAY;
    phaseSampleCount_ = 0;
    currentLevel_ = 1.0f;
}

void Envelope::transitionToSustain() {
    currentPhase_ = Phase::SUSTAIN;
    phaseSampleCount_ = 0;
    currentLevel_ = sustainLevel_;
}

void Envelope::transitionToRelease() {
    currentPhase_ = Phase::RELEASE;
    phaseSampleCount_ = 0;
    releaseStartLevel_ = currentLevel_;  // Capture current level
    wasReleased_ = true;
}

void Envelope::transitionToIdle() {
    currentPhase_ = Phase::IDLE;
    phaseSampleCount_ = 0;
    currentLevel_ = 0.0f;
    releaseStartLevel_ = 0.0f;
}
