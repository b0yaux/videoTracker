#include "VoiceProcessor.h"
#include "ofLog.h"

VoiceProcessor::VoiceProcessor()
    : source_(nullptr)
    , isActive_(false)
    , currentSampleRate_(44100.0f)
{
}

VoiceProcessor::~VoiceProcessor() {
    // No cleanup needed - we don't own the source
}

void VoiceProcessor::setSource(ofxSoundObject* source) {
    source_ = source;
}

void VoiceProcessor::trigger() {
    if (source_) {
        envelope_.trigger();
        isActive_ = true;
    }
}

void VoiceProcessor::release() {
    if (isActive_) {
        envelope_.release();
    }
}

void VoiceProcessor::stop() {
    // Immediate stop - but still use envelope release for minimum fade
    // This prevents clicks even during voice stealing
    if (isActive_) {
        // Always use release phase for smooth stop (even if very short)
        // This ensures we never have abrupt stops that cause clicks
        if (envelope_.isActive()) {
            envelope_.release();
        } else {
            // If envelope already idle, just reset
            envelope_.reset();
            isActive_ = false;
        }
    } else {
        // Not active - just reset envelope
        envelope_.reset();
    }
}

bool VoiceProcessor::isActive() const {
    return isActive_ && envelope_.isActive();
}

bool VoiceProcessor::isReleasing() const {
    return envelope_.getPhase() == Envelope::Phase::RELEASE;
}

void VoiceProcessor::audioOut(ofSoundBuffer& output) {
    if (!source_) {
        // No source - output silence
        output.set(0.0f);
        return;
    }
    
    // Get sample rate from output buffer
    float sampleRate = output.getSampleRate();
    if (sampleRate != currentSampleRate_) {
        currentSampleRate_ = sampleRate;
    }
    
    // Get audio from source
    source_->audioOut(output);
    
    // Apply envelope gain to each sample
    // This is sample-accurate processing in the audio thread
    // We process envelope even if isActive_ is false to ensure smooth transitions
    int numFrames = output.getNumFrames();
    int numChannels = output.getNumChannels();
    
    for (int frame = 0; frame < numFrames; ++frame) {
        // Process envelope once per frame (mono envelope applied to all channels)
        float gain = envelope_.processSample(sampleRate);
        
        // Check if envelope completed (went to IDLE) during this frame
        if (!envelope_.isActive() && isActive_) {
            // Envelope completed - mark as inactive
            isActive_ = false;
            // gain is already 0.0 from envelope (IDLE phase returns 0.0)
        }
        
        // Apply gain to all channels in this frame
        for (int ch = 0; ch < numChannels; ++ch) {
            int sampleIndex = frame * numChannels + ch;
            output[sampleIndex] *= gain;
        }
    }
}
