#pragma once

#include <array>
#include <vector>
#include <limits>
#include <algorithm>

/**
 * VoicePool - Unified polyphonic voice management
 * 
 * Provides reusable voice allocation, release, and stealing logic.
 * Template-based to work with any voice type that has:
 * - isFree() method
 * - startTime member (for LRU stealing)
 * - State enum with FREE, PLAYING, RELEASING
 * - isActive() method
 */
template<typename VoiceType, size_t MaxVoices>
class VoicePool {
public:
    enum class StealingStrategy {
        LRU,        // Least Recently Used (oldest startTime)
        OLDEST,     // Oldest voice (same as LRU for now)
    };
    
    enum class PolyphonyMode {
        MONOPHONIC,
        POLYPHONIC
    };
    
    VoicePool()
        : stealingStrategy_(StealingStrategy::LRU)
        , polyphonyMode_(PolyphonyMode::POLYPHONIC)
    {
    }
    
    void setStealingStrategy(StealingStrategy strategy) {
        stealingStrategy_ = strategy;
    }
    
    void setPolyphonyMode(PolyphonyMode mode) {
        polyphonyMode_ = mode;
    }
    
    static constexpr size_t getMaxVoices() { return MaxVoices; }
    PolyphonyMode getPolyphonyMode() const { return polyphonyMode_; }
    
    std::array<VoiceType, MaxVoices>& getVoices() { return voicePool_; }
    const std::array<VoiceType, MaxVoices>& getVoices() const { return voicePool_; }
    
    // Allocate a voice (returns nullptr if allocation failed)
    VoiceType* allocateVoice() {
        for (auto& voice : voicePool_) {
            if (voice.isFree()) {
                return &voice;
            }
        }
        return stealVoice();
    }
    
    std::vector<VoiceType*> getActiveVoices() {
        std::vector<VoiceType*> active;
        for (auto& voice : voicePool_) {
            if (voice.isActive()) {
                active.push_back(&voice);
            }
        }
        return active;
    }
    
private:
    VoiceType* stealVoice() {
        switch (stealingStrategy_) {
            case StealingStrategy::LRU:
            case StealingStrategy::OLDEST: {
                VoiceType* oldest = nullptr;
                float oldestTime = std::numeric_limits<float>::max();
                for (auto& voice : voicePool_) {
                    if (static_cast<int>(voice.state) == 1 && voice.startTime < oldestTime) {
                        oldest = &voice;
                        oldestTime = voice.startTime;
                    }
                }
                return oldest;
            }
            default:
                return nullptr;
        }
    }
    
    std::array<VoiceType, MaxVoices> voicePool_;
    StealingStrategy stealingStrategy_;
    PolyphonyMode polyphonyMode_;
};
