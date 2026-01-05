#pragma once

#include <array>
#include <vector>
#include <limits>
#include <algorithm>

/**
 * VoiceManager - Unified voice pool management
 * 
 * Provides reusable voice allocation, release, and stealing logic.
 * Template-based to work with any voice type that has:
 * - isFree() method
 * - startTime member (for LRU stealing)
 * - State enum with FREE, PLAYING, RELEASING
 * - isActive() method
 * 
 * Uses fixed-size array since Voice types are typically not move-insertable
 * (contain non-copyable members like ofParameter, VoiceProcessor, etc.)
 * 
 * Usage:
 *   VoiceManager<Voice, 16> voiceManager;  // 16 voices
 *   voiceManager.setPolyphonyMode(PolyphonyMode::POLYPHONIC);
 *   voiceManager.setStealingStrategy(StealingStrategy::LRU);
 *   
 *   Voice* voice = voiceManager.allocateVoice();
 *   if (voice) {
 *       // Use voice...
 *   }
 */
template<typename VoiceType, size_t MaxVoices>
class VoiceManager {
public:
    enum class StealingStrategy {
        LRU,        // Least Recently Used (oldest startTime) - current implementation
        OLDEST,     // Oldest voice (same as LRU for now)
        // FUTURE: QUIETEST - quietest voice (requires volume tracking per voice)
        // FUTURE: PRIORITY - priority-based (requires priority field in Voice)
    };
    
    enum class PolyphonyMode {
        MONOPHONIC,
        POLYPHONIC
    };
    
    VoiceManager()
        : stealingStrategy_(StealingStrategy::LRU)
        , polyphonyMode_(PolyphonyMode::POLYPHONIC)
    {
        // Array is default-initialized (all voices in default state)
    }
    
    void setStealingStrategy(StealingStrategy strategy) {
        stealingStrategy_ = strategy;
    }
    
    void setPolyphonyMode(PolyphonyMode mode) {
        polyphonyMode_ = mode;
    }
    
    static constexpr size_t getMaxVoices() { return MaxVoices; }
    PolyphonyMode getPolyphonyMode() const { return polyphonyMode_; }
    
    // Get voice pool (for module-specific voice initialization)
    std::array<VoiceType, MaxVoices>& getVoicePool() { return voicePool_; }
    const std::array<VoiceType, MaxVoices>& getVoicePool() const { return voicePool_; }
    
    // Allocate a voice (returns nullptr if allocation failed)
    // Note: Module-specific initialization (resetToDefaults, etc.) should be done after allocation
    VoiceType* allocateVoice() {
        // First, try to find a FREE voice
        for (auto& voice : voicePool_) {
            if (voice.isFree()) {
                return &voice;
            }
        }
        
        // No free voice - use stealing strategy
        return stealVoice();
    }
    
    // Get active voices (PLAYING or RELEASING state)
    std::vector<VoiceType*> getActiveVoices() {
        std::vector<VoiceType*> active;
        for (auto& voice : voicePool_) {
            if (voice.isActive()) {
                active.push_back(&voice);
            }
        }
        return active;
    }
    
    size_t getActiveVoiceCount() const {
        size_t count = 0;
        for (const auto& voice : voicePool_) {
            if (voice.isActive()) {
                count++;
            }
        }
        return count;
    }
    
    bool hasFreeVoice() const {
        for (const auto& voice : voicePool_) {
            if (voice.isFree()) {
                return true;
            }
        }
        return false;
    }
    
private:
    // Voice stealing logic (called when no FREE voices available)
    VoiceType* stealVoice() {
        switch (stealingStrategy_) {
            case StealingStrategy::LRU:
            case StealingStrategy::OLDEST: {
                // Find oldest PLAYING voice (Least Recently Used)
                VoiceType* oldest = nullptr;
                float oldestTime = std::numeric_limits<float>::max();
                for (auto& voice : voicePool_) {
                    // FUTURE: Add support for RELEASING voices in stealing (currently only steals PLAYING)
                    // Note: VoiceType must have 'state' enum with PLAYING value and 'startTime' float member
                    // Voice::State is an enum (not enum class), so PLAYING = 1
                    if (static_cast<int>(voice.state) == 1 && voice.startTime < oldestTime) {
                        oldest = &voice;
                        oldestTime = voice.startTime;
                    }
                }
                return oldest;
            }
            // FUTURE: Implement QUIETEST strategy (requires volume tracking)
            // FUTURE: Implement PRIORITY strategy (requires priority field in Voice)
            default:
                return nullptr;
        }
    }
    
    std::array<VoiceType, MaxVoices> voicePool_;  // Fixed-size array (Voice is not move-insertable)
    StealingStrategy stealingStrategy_;
    PolyphonyMode polyphonyMode_;
};

