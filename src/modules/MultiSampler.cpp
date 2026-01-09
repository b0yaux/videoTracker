#include "MultiSampler.h"
#include "MediaPlayer.h"
#include "VoiceProcessor.h"
#include "core/ModuleFactory.h"
#include "core/PatternRuntime.h"
#include "utils/Clock.h"
#include "ofFileUtils.h"
#include "ofSystemUtils.h"
#include "ofUtils.h"
#include "ofJson.h"
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <limits>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <shared_mutex>

// ============================================================================
// SAMPLEREF IMPLEMENTATIONS
// ============================================================================

bool SampleRef::loadSharedAudio() {
    // Load audio into shared buffer if we have an audio path
    if (!audioPath.empty()) {
        sharedAudioFile = std::make_shared<ofxSoundFile>();
        if (!sharedAudioFile->load(audioPath)) {
            ofLogError("SampleRef") << "Failed to load audio: " << audioPath;
            sharedAudioFile.reset();
            return false;
        }
        ofLogNotice("SampleRef") << "Loaded shared audio: " << audioPath 
                                  << " (" << sharedAudioFile->getNumFrames() << " frames)";
        
        // Cache duration from shared audio file (getDuration returns milliseconds)
        if (sharedAudioFile->isLoaded()) {
            duration = sharedAudioFile->getDuration() / 1000.0f;
            metadataLoaded = true;
        }
    } else if (!videoPath.empty()) {
        // Video-only sample: load video to get duration
        // We'll use a temporary MediaPlayer just to get metadata
        auto tempPlayer = std::make_shared<MediaPlayer>();
        if (tempPlayer->load("", videoPath)) {
            duration = tempPlayer->getDuration();
            metadataLoaded = true;
        }
    }
    
    // Reset parameter state cache to defaults
    resetParameterState();
    
    return true;
}

void SampleRef::unloadSharedAudio() {
    sharedAudioFile.reset();
    previewPlayer.reset();
    isScrubbing = false;
    metadataLoaded = false;
    duration = 0.0f;
    resetParameterState();
}

void SampleRef::resetParameterState() {
    currentPlayheadPosition = 0.0f;
    currentStartPosition = 0.0f;
    currentSpeed = 1.0f;
    currentVolume = 1.0f;
    currentRegionStart = 0.0f;
    currentRegionEnd = 1.0f;
    currentGrainSize = 0.0f;
}

const ofSoundBuffer& SampleRef::getAudioBuffer() const {
    static ofSoundBuffer emptyBuffer;
    if (sharedAudioFile && sharedAudioFile->isLoaded()) {
        return sharedAudioFile->getBuffer();
    }
    return emptyBuffer;
}

// ============================================================================
// VOICE IMPLEMENTATIONS
// ============================================================================

float Voice::getDuration() const {
    // Use MediaPlayer's getDuration() which returns max(audio, video) duration
    return player.getDuration();
}

bool Voice::loadSample(const SampleRef& sample) {
    // CRITICAL: Stop any currently playing audio/video before loading new sample
    // This prevents re-triggering the previous sample when voice is reused
    // Without this, a reused voice may still be playing the old sample's audio
    player.stop();
    
    // Initialize ALL parameters from SampleRef defaults
    // These are the per-sample configuration values
    speed.set(sample.defaultSpeed);
    volume.set(sample.defaultVolume);
    startPosition.set(sample.defaultStartPosition);
    regionStart.set(sample.defaultRegionStart);
    regionEnd.set(sample.defaultRegionEnd);
    grainSize.set(sample.defaultGrainSize);
    
    // Load audio from shared buffer (instant - no file I/O)
    // MediaPlayer handles shared buffer loading via loadAudioFromShared()
    bool audioLoaded = false;
    if (sample.sharedAudioFile && sample.sharedAudioFile->isLoaded()) {
        audioLoaded = player.loadAudioFromShared(sample.sharedAudioFile);
        if (!audioLoaded) {
            ofLogWarning("Voice") << "Failed to load audio from shared buffer";
        }
    }
    
    // Load video if path differs from currently loaded (lazy load for efficiency)
    // MediaPlayer handles HAP audio disabling internally
    bool videoLoaded = false;
    if (!sample.videoPath.empty() && sample.videoPath != loadedVideoPath) {
        // MediaPlayer.loadVideo() handles HAP audio disabling automatically
        videoLoaded = player.loadVideo(sample.videoPath);
        if (videoLoaded) {
            loadedVideoPath = sample.videoPath;
        } else {
            ofLogWarning("Voice") << "Failed to load video: " << sample.videoPath;
        }
    } else if (!sample.videoPath.empty() && sample.videoPath == loadedVideoPath) {
        // Video already loaded - just mark as loaded
        videoLoaded = player.isVideoLoaded();
    } else if (sample.videoPath.empty() && player.isVideoLoaded()) {
        // CRITICAL FIX: New sample has no video, but voice still has video from previous sample
        // Unload the old video to prevent stale video from showing
        player.videoPlayer.stop();
        player.videoPlayer.getVideoFile().close();
        player.videoEnabled.set(false);
        player.videoPlayer.enabled.set(false);
        loadedVideoPath.clear();
        videoLoaded = false;
        ofLogVerbose("Voice") << "Unloaded video from voice (new sample is audio-only)";
    }
    
    return audioLoaded || videoLoaded;
}

void Voice::applyParameters(float spd, float vol, float pos, float regStart, float regEnd, float grainSz) {
    // Store parameters (current runtime values)
    speed.set(spd);
    volume.set(vol);
    startPosition.set(pos);
    regionStart.set(regStart);
    regionEnd.set(regEnd);
    grainSize.set(grainSz);
    
    // EXPLICIT PARAMETER SYNC: Update MediaPlayer parameters
    // MediaPlayer will handle position calculation and application internally
    player.speed.set(spd);
    player.volume.set(vol);
    player.startPosition.set(pos);
    player.regionStart.set(regStart);
    player.regionEnd.set(regEnd);
    player.loopSize.set(grainSz);
}

void Voice::resetToDefaults() {
    // Reset to default values (will be overridden by loadSample from SampleRef)
    speed.set(1.0f);
    volume.set(1.0f);
    startPosition.set(0.0f);
    regionStart.set(0.0f);
    regionEnd.set(1.0f);
    grainSize.set(0.0f);
    
    // Initialize envelope parameters with MultiSampler defaults
    // These will be set from MultiSampler constants when voice is allocated
    attackMs.set(0.0f);
    decayMs.set(0.0f);
    sustain.set(1.0f);
    releaseMs.set(10.0f);
    
    // Reset MediaPlayer to defaults
    player.speed.set(1.0f);
    player.volume.set(1.0f);
    player.startPosition.set(0.0f);
    player.regionStart.set(0.0f);
    player.regionEnd.set(1.0f);
    player.loopSize.set(0.0f);
    player.loop.set(false);
    
    // Setup VoiceProcessor with MediaPlayer's audio source (done once per voice)
    voiceProcessor.setSource(&player.audioPlayer);
}

void Voice::play() {
    // EXPLICIT PARAMETER SYNC: Configure MediaPlayer from Voice parameters
    // This ensures all playback parameters are synchronized before playback starts
    
    // Configure MediaPlayer parameters explicitly
    player.speed.set(speed.get());
    player.volume.set(volume.get());
    player.startPosition.set(startPosition.get());
    player.regionStart.set(regionStart.get());
    player.regionEnd.set(regionEnd.get());
    player.loopSize.set(grainSize.get());
    
    // Configure envelope parameters from Voice parameters
    voiceProcessor.getEnvelope().setAttack(attackMs.get());
    voiceProcessor.getEnvelope().setDecay(decayMs.get());
    voiceProcessor.getEnvelope().setSustain(sustain.get());
    voiceProcessor.getEnvelope().setRelease(releaseMs.get());
    
    // Setup VoiceProcessor with MediaPlayer's audio player
    // This must be done before play() to ensure envelope is applied
    voiceProcessor.setSource(&player.audioPlayer);
    
    // Start MediaPlayer playback (handles both audio and video)
    // MediaPlayer.play() will:
    // - Calculate target position from startPosition within region
    // - Set position on both audio and video players
    // - Start playback
    player.play();
    
    // Trigger envelope (starts ATTACK phase for smooth fade-in)
    voiceProcessor.trigger();
}

void Voice::release() {
    // CRITICAL: Start envelope release FIRST, keep player playing
    // The envelope will fade out the audio smoothly during release phase
    // The player will be stopped in MultiSampler::update() when envelope completes
    // This ensures smooth fade-out without clicks (professional sampler practice)
    voiceProcessor.release();
    
    // DON'T stop the player here - let it continue playing during release
    // Stopping the player immediately causes clicks because the envelope
    // has no audio to fade out
}

void Voice::stop() {
    // Immediate stop - use envelope release for minimum fade to prevent clicks
    voiceProcessor.stop();
    player.stop();  // MediaPlayer handles stopping both audio and video
}

void Voice::setPosition(float pos) {
    // Use MediaPlayer's setPosition (handles both audio and video)
    player.setPosition(pos);
}

MultiSampler::MultiSampler(const std::string& dataDir) 
    : dataDirectory(dataDir), isSetup(false),
      currentMode(PlaybackMode::IDLE), currentPlayStyle(PlayStyle::ONCE), 
      clock(nullptr), polyphonyMode_(PolyphonyMode::MONOPHONIC),  // Default to monophonic for backward compatibility
      voiceManager_() {  // VoiceManager uses template size (MAX_VOICES)
    // Voice pool is managed by VoiceManager (all voices FREE with no player)
    // setup() will be called later with clock reference
    
    // Initialize video mixer (similar to VideoMixer and VideoOutput)
    internalVideoMixer_.setName("MultiSampler Video Mixer");
    internalVideoMixer_.setMasterOpacity(1.0f);
    internalVideoMixer_.setBlendMode(OF_BLENDMODE_ADD);
    internalVideoMixer_.setAutoNormalize(true);
}

MultiSampler::~MultiSampler() noexcept {
    isDestroying_ = true;  // Prevent update() from running after destruction starts
    clear();
}

// ========================================================================
// COMPLETE PRELOADING SYSTEM IMPLEMENTATION
// ========================================================================

bool MultiSampler::preloadAllSamples() {
    if (sampleBank_.empty()) {
        ofLogNotice("MultiSampler") << "No samples to preload";
        return true;
    }

    ofLogNotice("MultiSampler") << "Starting preloading of " << sampleBank_.size() << " samples with shared audio architecture...";

    size_t successCount = 0;

    for (size_t i = 0; i < sampleBank_.size(); ++i) {
        auto& sample = sampleBank_[i];

        if (!sample.hasMedia()) {
            ofLogVerbose("MultiSampler") << "Sample " << i << " has no media, skipping";
            continue;
        }

        ofLogNotice("MultiSampler") << "Preloading sample " << i << ": " << sample.displayName;

        try {
            // Load audio into shared buffer + create display player
            // This loads audio ONCE per sample, not N times per voice
            if (sample.loadSharedAudio()) {
                ofLogNotice("MultiSampler") << "Successfully preloaded sample " << i 
                                            << " (duration: " << sample.duration << "s)";
                successCount++;
            } else {
                ofLogError("MultiSampler") << "Failed to preload sample " << i << ": " << sample.displayName;
            }

        } catch (const std::exception& e) {
            ofLogError("MultiSampler") << "Exception preloading sample " << i << ": " << e.what();
        } catch (...) {
            ofLogError("MultiSampler") << "Unknown exception preloading sample " << i;
        }
    }

    ofLogNotice("MultiSampler") << "Preloading complete: " << successCount << "/" << sampleBank_.size()
                                << " samples loaded (shared audio architecture - " << MAX_VOICES << " voice slots available)";

    return successCount == sampleBank_.size();
}



// ============================================================================
// VOICE ALLOCATION METHODS
// ============================================================================

Voice* MultiSampler::allocateVoice(int requestedSampleIndex) {
    // Use VoiceManager for unified voice allocation
    // FUTURE: Add warm voice prioritization (prefer voices with requestedSampleIndex already loaded)
    Voice* voice = voiceManager_.allocateVoice();
    
    if (voice) {
        // Reset voice to defaults (ensures VoiceProcessor is set up)
        voice->resetToDefaults();
        
        // If voice was stolen, release it first (VoiceManager handles stealing, we handle cleanup)
        if (!voice->isFree()) {
            ofLogVerbose("MultiSampler") << "Voice stealing: releasing oldest playing voice";
            releaseVoice(*voice);
        }
        
        return voice;
    }

    // All voices busy - this shouldn't happen with 16 voices, but handle gracefully
    ofLogWarning("MultiSampler") << "No voice available for allocation (all voices busy)";
    return nullptr;
}

void MultiSampler::releaseVoice(Voice& voice) {
    // Disconnect video immediately when releasing (don't wait for envelope to complete)
    // This prevents stale video frames from showing during the release phase
    if (voice.videoConnected) {
        if (voice.player.isVideoLoaded()) {
            internalVideoMixer_.disconnectInput(&voice.player.videoPlayer);
        }
        voice.videoConnected = false;
        // Stop video to prevent last frame from showing
        if (voice.player.videoPlayer.isPlaying()) {
            voice.player.videoPlayer.stop();
        }
    }
    
    // Start release phase (smooth fade-out) instead of abrupt stop
    // This prevents audio clicks when voice is stolen or stopped
    if (voice.state == Voice::PLAYING) {
        voice.release();  // Start envelope fade-out, keep player playing
        voice.state = Voice::RELEASING;  // CRITICAL: Set state to RELEASING so update() can detect completion
        // Voice will transition to FREE when envelope completes (handled in update())
    } else {
        // Already releasing or free - just stop
        voice.stop();
        voice.startTime = 0.0f;
        voice.state = Voice::FREE;
    }
    // Keep audio connections for instant reuse (VoiceProcessor handles cleanup)
}

void MultiSampler::releaseAllVoices() {
    // Use VoiceManager's voice pool for iteration
    for (auto& v : voiceManager_.getVoicePool()) {
        if (v.state != Voice::FREE) {
            // Stop all playback
            v.stop();
            
            // Disconnect video when stopping (audio disconnects automatically via VoiceProcessor)
            if (v.videoConnected && v.player.isVideoLoaded()) {
                internalVideoMixer_.disconnectInput(&v.player.videoPlayer);
                v.videoConnected = false;
            }
            
            v.sampleIndex = -1;
            v.startTime = 0.0f;
            v.state = Voice::FREE;
            // Audio connections are kept for reuse (VoiceProcessor handles cleanup)
        }
    }
    scheduledStops_.clear();
    currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
}

// ============================================================================
// SAMPLE BANK API
// ============================================================================

const SampleRef& MultiSampler::getSample(size_t index) const {
    static SampleRef emptySample;
    if (index >= sampleBank_.size()) {
        ofLogWarning("MultiSampler") << "getSample: index " << index << " out of range";
        return emptySample;
    }
    return sampleBank_[index];
}

SampleRef& MultiSampler::getSampleMutable(size_t index) {
    static SampleRef emptySample;
    if (index >= sampleBank_.size()) {
        ofLogWarning("MultiSampler") << "getSampleMutable: index " << index << " out of range";
        return emptySample;
    }
    return sampleBank_[index];
}

// Note: getVoiceForSample() implementation is below

std::vector<Voice*> MultiSampler::getActiveVoices() {
    // Use VoiceManager's getActiveVoices() for unified voice query
    return voiceManager_.getActiveVoices();
}

// ============================================================================
// POLYPHONIC GUI SUPPORT - Voice tracking for visual feedback
// ============================================================================

std::vector<Voice*> MultiSampler::getVoicesForSample(int sampleIndex) {
    std::vector<Voice*> voices;
    // Use VoiceManager's voice pool for iteration
    for (auto& v : voiceManager_.getVoicePool()) {
        if (v.sampleIndex == sampleIndex && 
            (v.state == Voice::PLAYING || v.state == Voice::RELEASING)) {
            voices.push_back(&v);
        }
    }
    return voices;
}

std::vector<const Voice*> MultiSampler::getVoicesForSample(int sampleIndex) const {
    std::vector<const Voice*> voices;
    // Use VoiceManager's voice pool for iteration
    for (const auto& v : voiceManager_.getVoicePool()) {
        if (v.sampleIndex == sampleIndex && 
            (v.state == Voice::PLAYING || v.state == Voice::RELEASING)) {
            voices.push_back(&v);
        }
    }
    return voices;
}

int MultiSampler::getVoiceCountForSample(int sampleIndex) const {
    int count = 0;
    // Use VoiceManager's voice pool for iteration
    for (const auto& v : voiceManager_.getVoicePool()) {
        if (v.sampleIndex == sampleIndex && 
            (v.state == Voice::PLAYING || v.state == Voice::RELEASING)) {
            count++;
        }
    }
    return count;
}

bool MultiSampler::isSamplePlaying(int sampleIndex) const {
    // Check for both PLAYING and RELEASING states (GUI shows samples that are active)
    // Use VoiceManager's voice pool for iteration
    for (const auto& v : voiceManager_.getVoicePool()) {
        if (v.sampleIndex == sampleIndex && 
            (v.state == Voice::PLAYING || v.state == Voice::RELEASING)) {
            return true;
        }
    }
    return false;
}

Voice* MultiSampler::triggerSamplePreview(int sampleIndex, float gateDuration) {
    // Create a trigger event for preview with short gate duration
    TriggerEvent previewEvent;
    previewEvent.parameters["note"] = static_cast<float>(sampleIndex);
    previewEvent.duration = gateDuration;  // Auto-stop after gate duration
    
    // Use unified trigger system
    Voice* voice = triggerSample(sampleIndex, &previewEvent);
    
    if (voice && gateDuration > 0.0f) {
        // Schedule stop for preview (will auto-release after gate duration)
        scheduledStops_.push_back({voice, ofGetElapsedTimef() + gateDuration, voice->generation});
    }
    
    return voice;
}

// Get first voice playing a sample (convenience method)
Voice* MultiSampler::getVoiceForSample(int sampleIndex) {
    // Return first voice that is playing this sample
    // Use VoiceManager's voice pool for iteration
    for (auto& v : voiceManager_.getVoicePool()) {
        if (v.sampleIndex == sampleIndex && 
            (v.state == Voice::PLAYING || v.state == Voice::RELEASING)) {
            return &v;
        }
    }
    return nullptr;
}

const Voice* MultiSampler::getVoiceForSample(int sampleIndex) const {
    // Return first voice that is playing this sample
    // Use VoiceManager's voice pool for iteration
    for (const auto& v : voiceManager_.getVoicePool()) {
        if (v.sampleIndex == sampleIndex && 
            (v.state == Voice::PLAYING || v.state == Voice::RELEASING)) {
            return &v;
        }
    }
    return nullptr;
}


void MultiSampler::syncParameterStateFromVoice(size_t sampleIndex, Voice* voice) {
    if (sampleIndex >= sampleBank_.size() || !voice) return;
    
    SampleRef& sample = sampleBank_[sampleIndex];
    // Sync parameter state cache from MediaPlayer (single source of truth for position)
    // CRITICAL: Use MediaPlayer's playheadPosition which freezes when stopped
    // This updates the cached parameter values used by getParameter() API
    sample.currentPlayheadPosition = voice->player.playheadPosition.get();
    sample.currentSpeed = voice->player.speed.get();
    sample.currentVolume = voice->player.volume.get();
    sample.currentStartPosition = voice->player.startPosition.get();
    sample.currentRegionStart = voice->player.regionStart.get();
    sample.currentRegionEnd = voice->player.regionEnd.get();
    sample.currentGrainSize = voice->player.loopSize.get();
}

std::string MultiSampler::computeDisplayName(const SampleRef& sample) const {
    // Prefer audio path, fall back to video path
    std::string path = !sample.audioPath.empty() ? sample.audioPath : sample.videoPath;
    if (path.empty()) return "Empty";
    return ofFilePath::getBaseName(path);
}

void MultiSampler::addSampleToBank(const std::string& audioPath, const std::string& videoPath) {
    SampleRef sample;
    sample.audioPath = audioPath;
    sample.videoPath = videoPath;
    sample.displayName = computeDisplayName(sample);
    sample.duration = 0.0f;  // Will be updated when played
    sample.metadataLoaded = false;
    
    // Initialize parameter state cache to match defaults
    sample.currentSpeed = sample.defaultSpeed;
    sample.currentVolume = sample.defaultVolume;
    sample.currentStartPosition = sample.defaultStartPosition;
    sample.currentRegionStart = sample.defaultRegionStart;
    sample.currentRegionEnd = sample.defaultRegionEnd;
    sample.currentGrainSize = sample.defaultGrainSize;
    
    sampleBank_.push_back(sample);
}

void MultiSampler::setup(Clock* clockRef) {
    if (isSetup) return;
    
    clock = clockRef; // Store clock reference
    ofLogNotice("MultiSampler") << "Setting up media library with directory: " << dataDirectory;
    isSetup = true;
}

//--------------------------------------------------------------
//--------------------------------------------------------------
void MultiSampler::initialize(Clock* clock, ModuleRegistry* registry, ConnectionManager* connectionManager,
                          ParameterRouter* parameterRouter, PatternRuntime* patternRuntime, bool isRestored) {
    // COMPLETE PRELOADING ARCHITECTURE:
    // - Sample bank populated with file references
    // - ALL samples preloaded upfront during initialization
    // - Voices borrow preloaded players for instant triggering
    // - Zero loading during playback = no CoreAudio conflicts

    // 1. Basic setup
    if (clock) {
        setup(clock);
    }

    // 2. Preload all samples (blocking operation during init)
    if (!sampleBank_.empty()) {
        ofLogNotice("MultiSampler") << "Preloading " << sampleBank_.size() << " samples...";

        if (!preloadAllSamples()) {
            ofLogWarning("MultiSampler") << "Some samples failed to preload - they will not be playable";
        }
    }

    // 3. Log session restore status
    if (isRestored) {
        ofLogNotice("MultiSampler") << "Session restored with " << sampleBank_.size()
                                 << " samples (complete preloading finished)";
    } else {
        ofLogNotice("MultiSampler") << "Initialized with complete preloading system";
    }
}

void MultiSampler::setCustomPath(const std::string& absolutePath) {
    ofLogNotice("MultiSampler") << "Setting custom absolute path: " << absolutePath;
    
    ofDirectory dir(absolutePath);
    if (!dir.exists()) {
        ofLogError("MultiSampler") << "Custom path does not exist: " << absolutePath;
        return;
    }
    
    dataDirectory = absolutePath;
    clear();
    
    ofLogNotice("MultiSampler") << "✅ Using custom path: " << absolutePath;
    
    // Scan the custom directory
    scanMediaFiles(absolutePath, dir);
    
    // Auto-pair files
    mediaPair();
}

void MultiSampler::scanDirectory(const std::string& path) {
    dataDirectory = path;
    clear();
    
    ofLogNotice("MultiSampler") << "🔍 scanDirectory called with path: " << path;
    
    // Simple approach: just use the provided path
    ofDirectory dir(path);
    if (!dir.exists()) {
        ofLogError("MultiSampler") << "Directory does not exist: " << path;
        return;
    }
    
    ofLogNotice("MultiSampler") << "✅ Directory exists, scanning for media files...";
    
    // Scan for media files
    scanMediaFiles(path, dir);
}


void MultiSampler::mediaPair() {
    // MODERN ASYNC ARCHITECTURE: Build sample bank and queue for async preloading
    // Media is preloaded in background for instant triggering

    // Release any active voices first
    releaseAllVoices();

    // Clear existing sample bank
    sampleBank_.clear();
    
    // Build hash map of video files by base name for O(1) lookup
    std::unordered_map<std::string, std::string> videoMap;
    for (const auto& videoFile : videoFiles) {
        std::string videoBase = getBaseName(videoFile);
        videoMap[videoBase] = videoFile;
    }
    
    // Track which video files have been paired
    std::unordered_set<std::string> pairedVideos;
    
    // Create sample refs for matching audio/video files
    for (const auto& audioFile : audioFiles) {
        std::string audioBase = getBaseName(audioFile);
        auto it = videoMap.find(audioBase);
        
        if (it != videoMap.end()) {
            // Paired audio+video sample
            addSampleToBank(audioFile, it->second);
                pairedVideos.insert(audioBase);
            } else {
            // Audio-only sample
            addSampleToBank(audioFile, "");
        }
    }
    
    // Create sample refs for unmatched video files
    for (const auto& videoFile : videoFiles) {
        std::string videoBase = getBaseName(videoFile);
        if (pairedVideos.find(videoBase) == pairedVideos.end()) {
            // Video-only sample
            addSampleToBank("", videoFile);
        }
    }
    
    // Clear temporary file lists (no longer needed after building sample bank)
    audioFiles.clear();
    videoFiles.clear();

    ofLogNotice("MultiSampler") << "Sample bank populated with " << sampleBank_.size()
                             << " samples (complete preloading will happen during initialization)";
}

void MultiSampler::pairByIndex() {
    // Sampler-inspired: Build sample bank by index pairing (NO media loading)
    
    // Release any active voices first
    releaseAllVoices();
    
    // Clear existing sample bank (but keep file lists for pairing)
    sampleBank_.clear();
    
    ofLogNotice("MultiSampler") << "Pairing files by index";
    
    size_t maxPairs = std::max(audioFiles.size(), videoFiles.size());
    
    for (size_t i = 0; i < maxPairs; i++) {
        std::string audioFile = (i < audioFiles.size()) ? audioFiles[i] : "";
        std::string videoFile = (i < videoFiles.size()) ? videoFiles[i] : "";
        
        addSampleToBank(audioFile, videoFile);
            ofLogNotice("MultiSampler") << "Index pair " << i << ": " 
                                            << ofFilePath::getFileName(audioFile) 
                                            << " + " << ofFilePath::getFileName(videoFile);
        }
    
    // Clear temporary file lists
    audioFiles.clear();
    videoFiles.clear();

    ofLogNotice("MultiSampler") << "Sample bank populated with " << sampleBank_.size()
                             << " samples by index (complete preloading will happen during initialization)";
}

// ========================================================================
// LEGACY API - Backward Compatibility
// ========================================================================

MediaPlayer* MultiSampler::getMediaPlayer(size_t index) {
    // Returns preview player only when scrubbing
    // For normal playback, voices are managed internally - use Voice API to access them
    std::shared_lock<std::shared_mutex> lock(stateMutex);
    
    if (index >= sampleBank_.size()) return nullptr;
    
    auto& sample = sampleBank_[index];
    return sample.isScrubbing ? sample.previewPlayer.get() : nullptr;
}

MediaPlayer* MultiSampler::getMediaPlayerByName(const std::string& name) {
    // Find sample by display name and return preview player if scrubbing
    std::shared_lock<std::shared_mutex> lock(stateMutex);
    for (size_t i = 0; i < sampleBank_.size(); i++) {
        if (sampleBank_[i].displayName == name) {
            auto& sample = sampleBank_[i];
            return sample.isScrubbing ? sample.previewPlayer.get() : nullptr;
        }
    }
    return nullptr;
}


std::vector<std::string> MultiSampler::getPlayerNames() const {
    // Return display names from sample bank
    std::vector<std::string> names;
    for (size_t i = 0; i < sampleBank_.size(); i++) {
        const SampleRef& sample = sampleBank_[i];
            std::string name = "[" + std::to_string(i) + "] ";
            
        bool hasAudio = !sample.audioPath.empty();
        bool hasVideo = !sample.videoPath.empty();
            
            if (hasAudio && hasVideo) {
                name += "A+V";
            } else if (hasAudio) {
                name += "Audio";
            } else if (hasVideo) {
                name += "Video";
            } else {
                name += "Empty";
            }
            
            names.push_back(name);
    }
    return names;
}

std::vector<std::string> MultiSampler::getPlayerFileNames() const {
    // Return file names from sample bank
    std::vector<std::string> fileNames;
    for (size_t i = 0; i < sampleBank_.size(); i++) {
        const SampleRef& sample = sampleBank_[i];
            std::string fileName = "";
            
        if (!sample.audioPath.empty() && !sample.videoPath.empty()) {
                // Paired files - show both
            fileName = ofFilePath::getFileName(sample.audioPath) + " | " + ofFilePath::getFileName(sample.videoPath);
        } else if (!sample.audioPath.empty()) {
                // Audio only
            fileName = ofFilePath::getFileName(sample.audioPath);
        } else if (!sample.videoPath.empty()) {
                // Video only
            fileName = ofFilePath::getFileName(sample.videoPath);
            } else {
                fileName = "empty_" + std::to_string(i);
            }
            
            fileNames.push_back(fileName);
    }
    return fileNames;
}

std::vector<std::string> MultiSampler::getAudioFiles() const {
    // Derive from sample bank
    std::vector<std::string> files;
    for (const auto& sample : sampleBank_) {
        if (!sample.audioPath.empty()) {
            files.push_back(sample.audioPath);
        }
    }
    return files;
}

std::vector<std::string> MultiSampler::getVideoFiles() const {
    // Derive from sample bank
    std::vector<std::string> files;
    for (const auto& sample : sampleBank_) {
        if (!sample.videoPath.empty()) {
            files.push_back(sample.videoPath);
        }
    }
    return files;
}

void MultiSampler::clear() {
    // Release all voices (stops playback, disconnects from mixers, frees memory)
    releaseAllVoices();
    
    // Clear sample bank
    sampleBank_.clear();
    
    // Clear temporary file lists
    audioFiles.clear();
    videoFiles.clear();
    
    // Note: Mixers are member objects - their destructors will safely clear
    // internal connections with proper locking. Routers already disconnected
    // external connections (AudioOutput/VideoOutput side).
}

void MultiSampler::refresh() {
    scanDirectory(dataDirectory);
    mediaPair();
}

//--------------------------------------------------------------
bool MultiSampler::removeSample(size_t index) {
    std::unique_lock<std::shared_mutex> lock(stateMutex);
    
    if (index >= sampleBank_.size()) {
        ofLogWarning("MultiSampler") << "Cannot remove sample: index " << index << " out of range";
        return false;
    }
    
    // Release any voice playing this sample (can't check sampleIndex anymore with borrowing)
    // Just release all voices to be safe when removing samples
    releaseAllVoices();
    ofLogNotice("MultiSampler") << "Released all voices before sample removal";
    
    // Remove scheduled stops for this sample's voices
    scheduledStops_.erase(
        std::remove_if(scheduledStops_.begin(), scheduledStops_.end(),
            [index](const ScheduledStop& stop) {
                // Can't check sampleIndex anymore, so just keep all scheduled stops
                // (they will be cleaned up naturally when voices are released)
                return false;
            }),
        scheduledStops_.end()
    );
    
    // Drain event queue to avoid stale references
    TriggerEvent dummy;
    size_t eventsDrained = 0;
    while (eventQueue.try_dequeue(dummy)) {
        eventsDrained++;
    }
    if (eventsDrained > 0) {
        ofLogNotice("MultiSampler") << "Drained " << eventsDrained << " events from queue when removing sample";
    }
    
    // Remove the sample from the bank
    sampleBank_.erase(sampleBank_.begin() + index);
    
    // GUI will manage selection state when samples are removed
    if (sampleBank_.empty()) {
        currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
    }
    
    ofLogNotice("MultiSampler") << "Removed sample at index " << index << " (remaining: " << sampleBank_.size() << ")";
    return true;
}

//--------------------------------------------------------------
// Individual file addition (for drag-and-drop support)
// SAMPLER-INSPIRED: Only adds to sample bank, doesn't load media
bool MultiSampler::addMediaFile(const std::string& filePath) {
    // Validate file exists
    ofFile file(filePath);
    if (!file.exists()) {
        ofLogWarning("MultiSampler") << "File does not exist: " << filePath;
        return false;
    }
    
    // Check if file is a valid media file
    std::string filename = ofFilePath::getFileName(filePath);
    bool isAudio = isAudioFile(filename);
    bool isVideo = isVideoFile(filename);
    
    if (!isAudio && !isVideo) {
        ofLogWarning("MultiSampler") << "File is not a valid media file: " << filePath;
        return false;
    }
    
    std::unique_lock<std::shared_mutex> lock(stateMutex);
    
    // Check if file is already in sample bank (avoid duplicates)
    for (const auto& sample : sampleBank_) {
        if (sample.audioPath == filePath || sample.videoPath == filePath) {
            ofLogNotice("MultiSampler") << "File already in sample bank: " << filePath;
            return false;
        }
    }
    
    // Try to pair with existing sample or create new one
    std::string baseName = getBaseName(filePath);
    
    // Look for existing sample to pair with
    for (auto& sample : sampleBank_) {
        std::string existingBase = computeDisplayName(sample);
        if (existingBase == baseName) {
            // Found a potential match
            if (isAudio && sample.audioPath.empty() && !sample.videoPath.empty()) {
                // Add audio to existing video-only sample
                sample.audioPath = filePath;
                sample.displayName = computeDisplayName(sample);
                ofLogNotice("MultiSampler") << "Paired audio with existing video sample: " << filename;
                    return true;
            } else if (isVideo && sample.videoPath.empty() && !sample.audioPath.empty()) {
                // Add video to existing audio-only sample
                sample.videoPath = filePath;
                sample.displayName = computeDisplayName(sample);
                ofLogNotice("MultiSampler") << "Paired video with existing audio sample: " << filename;
            return true;
            }
        }
    }
    
    // No matching sample found - create new sample
    SampleRef newSample;
    if (isAudio) {
        newSample.audioPath = filePath;
                } else {
        newSample.videoPath = filePath;
                }
    newSample.displayName = computeDisplayName(newSample);
    sampleBank_.push_back(newSample);

    // Preload the new sample immediately
    if (!preloadAllSamples()) {
        ofLogWarning("MultiSampler") << "Failed to preload newly added sample: " << newSample.displayName;
    }

    ofLogNotice("MultiSampler") << "Added and preloaded sample: " << newSample.displayName
                             << " (total: " << sampleBank_.size() << ")";
            return true;
}

void MultiSampler::addMediaFiles(const std::vector<std::string>& filePaths) {
    int successCount = 0;
    int failCount = 0;
    
    for (const auto& filePath : filePaths) {
        if (addMediaFile(filePath)) {
            successCount++;
        } else {
            failCount++;
        }
    }
    
    ofLogNotice("MultiSampler") << "Added " << successCount << " files to sample bank, " << failCount << " failed";
}

//--------------------------------------------------------------
bool MultiSampler::acceptFileDrop(const std::vector<std::string>& filePaths) {
    // Module interface implementation - delegate to addMediaFiles
    if (filePaths.empty()) {
        return false;
    }
    addMediaFiles(filePaths);
    return true;
}

// Helper methods
std::string MultiSampler::getBaseName(const std::string& filename) {
    std::string baseName = ofFilePath::getBaseName(filename);
    return baseName;
}


bool MultiSampler::isAudioFile(const std::string& filename) {
    std::string ext = ofToLower(ofFilePath::getFileExt(filename));
    return (ext == "wav" || ext == "mp3" || ext == "aiff" || ext == "aif" || ext == "m4a");
}

bool MultiSampler::isVideoFile(const std::string& filename) {
    std::string ext = ofToLower(ofFilePath::getFileExt(filename));
    return (ext == "mov" || ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "webm" || ext == "hap");
}


        

// DEPRECATED methods removed - voice pool architecture handles connections automatically

// ========================================================================
// VOICE MANAGEMENT API - Playback Control
// ========================================================================

Voice* MultiSampler::triggerSample(int sampleIndex, const TriggerEvent* event) {
    // Primary method for triggering sample playback
    // Allocates a voice from the pool, loads the sample, and starts playback
    // Returns the allocated voice, or nullptr if allocation failed
    
    std::unique_lock<std::shared_mutex> lock(stateMutex);

    // Validate sample index
    if (sampleIndex < 0 || sampleIndex >= static_cast<int>(sampleBank_.size())) {
        ofLogWarning("MultiSampler") << "Invalid sample index: " << sampleIndex;
        return nullptr;
    }

    SampleRef& sample = sampleBank_[sampleIndex];

    // Check if sample is preloaded and ready
    if (!sample.isReadyForPlayback()) {
        ofLogWarning("MultiSampler") << "Sample " << sampleIndex << " (" << sample.displayName << ") is not preloaded";
        return nullptr;
    }

    // In MONOPHONIC mode, release all voices first
    if (polyphonyMode_ == PolyphonyMode::MONOPHONIC) {
        releaseAllVoices();
    }

    // Allocate a voice from the pool
    Voice* voice = allocateVoice(sampleIndex);
    if (!voice) {
        ofLogWarning("MultiSampler") << "No available voice for sample " << sampleIndex;
        return nullptr;
    }

    // Load sample into the voice's own players (audio from shared buffer = instant)
    if (!voice->loadSample(sample)) {
        ofLogError("MultiSampler") << "Failed to load sample " << sampleIndex << " into voice";
        releaseVoice(*voice);
        return nullptr;
    }
    
    
    // Initialize envelope parameters with MultiSampler ADSR defaults (now routable/modulatable)
    voice->attackMs.set(defaultAttackMs_);
    voice->decayMs.set(defaultDecayMs_);
    voice->sustain.set(defaultSustain_);
    voice->releaseMs.set(defaultReleaseMs_);
    
    // Setup VoiceProcessor with MediaPlayer's audio source (ensure it's configured)
    voice->voiceProcessor.setSource(&voice->player.audioPlayer);
    
    // Update voice state
    voice->state = Voice::PLAYING;
    voice->startTime = ofGetElapsedTimef();
    voice->sampleIndex = sampleIndex;
    voice->generation++;  // Increment generation to invalidate any stale scheduled stops

    // Voice parameters are already initialized from SampleRef defaults in loadSample()
    // Now override with trigger event parameters if provided
    if (event) {
        float spd = voice->speed.get();  // Start with default
        float vol = voice->volume.get();
        float pos = voice->startPosition.get();
        float regStart = voice->regionStart.get();
        float regEnd = voice->regionEnd.get();
        float grainSz = voice->grainSize.get();
        
        // Override with event parameters if provided
        auto it = event->parameters.find("speed");
        if (it != event->parameters.end()) spd = it->second;
        
        it = event->parameters.find("volume");
        if (it != event->parameters.end()) vol = it->second;
        
        it = event->parameters.find("position");
        if (it != event->parameters.end()) {
            // CRITICAL: Position from sequencer is always relative (0.0-1.0 within region)
            // Clamp to valid relative range
            float eventPos = it->second;
            pos = std::max(0.0f, std::min(1.0f, eventPos));
        }
        
        it = event->parameters.find("regionStart");
        if (it != event->parameters.end()) regStart = it->second;
        
        it = event->parameters.find("regionEnd");
        if (it != event->parameters.end()) regEnd = it->second;
        
        it = event->parameters.find("grainSize");
        if (it == event->parameters.end()) {
            // Support legacy "loopSize" parameter name
            it = event->parameters.find("loopSize");
        }
        if (it != event->parameters.end()) grainSz = it->second;
        
        // Apply trigger parameters to voice (overrides defaults)
        voice->applyParameters(spd, vol, pos, regStart, regEnd, grainSz);
    }

    // Connect voice audio/video to internal mixers if not already connected
    // CRITICAL: Connect VoiceProcessor (which wraps player.audioPlayer + envelope) instead of audioPlayer directly
    if (!voice->audioConnected) {
        // Setup VoiceProcessor with MediaPlayer's audio source (done once per voice)
        voice->voiceProcessor.setSource(&voice->player.audioPlayer);
        // Connect VoiceProcessor to mixer (envelope is applied in audio thread)
        voice->voiceProcessor.connectTo(internalAudioMixer_);
        voice->audioConnected = true;
    }
    
    // CRITICAL: If this is an audio-only sample, disconnect AND STOP ALL video from other voices
    // This prevents video from previous samples (like sample 21) from showing
    if (!voice->player.isVideoLoaded()) {
        // Disconnect video from ALL other voices to prevent stale video
        // Use VoiceManager's voice pool for iteration
        for (auto& otherVoice : voiceManager_.getVoicePool()) {
            if (&otherVoice != voice && otherVoice.player.isVideoLoaded()) {
                // Stop video playback first
                if (otherVoice.player.videoPlayer.isPlaying()) {
                    otherVoice.player.videoPlayer.stop();
                }
                // Disconnect from mixer if connected
                if (otherVoice.videoConnected) {
                    internalVideoMixer_.disconnectInput(&otherVoice.player.videoPlayer);
                    otherVoice.videoConnected = false;
                }
                // Disable video output
                otherVoice.player.videoEnabled.set(false);
                otherVoice.player.videoPlayer.enabled.set(false);
            }
        }
        
        // Note: Scrubbing voice is handled by normal voice disconnection logic above
        // No separate preview player disconnection needed (scrubbing uses actual voices now)
        
        // Disconnect this voice's video if it was previously connected
        if (voice->videoConnected) {
            if (voice->player.isVideoLoaded()) {
                internalVideoMixer_.disconnectInput(&voice->player.videoPlayer);
            }
            voice->videoConnected = false;
        }
        voice->player.videoEnabled.set(false);
    } else if (!voice->videoConnected && voice->player.isVideoLoaded()) {
        // Connect video to mixer
        internalVideoMixer_.setInput(&voice->player.videoPlayer);
        voice->videoConnected = true;
    }

    
    // Start playback
    voice->play();
    
    // Update display to triggered sample (sticky behavior)
    currentMode.store(PlaybackMode::PLAYING, std::memory_order_relaxed);

    ofLogVerbose("MultiSampler") << "Triggered sample " << sampleIndex << " (" << sample.displayName << ")";

    return voice;
}

// ========================================================================
// PLAYBACK CONTROL API
// ========================================================================

bool MultiSampler::playMediaManual(size_t index) {
    // Manual playback control for GUI buttons
    // Triggers playback using current default parameters from SampleRef
    
    // CRITICAL: Check if module is enabled before triggering
    if (!isEnabled()) {
        ofLogWarning("MultiSampler") << "Cannot trigger sample - module is disabled";
        return false;
    }
    
    return triggerSample(static_cast<int>(index), nullptr) != nullptr;
}

// Scrubbing playback: Simple preview player with parameter sync from GUI state
// This ensures scrubbing uses the same parameters as normal playback without needing a full Voice
void MultiSampler::startScrubbingPlayback(size_t index, float position) {
    if (index >= sampleBank_.size()) return;
    
    std::unique_lock<std::shared_mutex> lock(stateMutex);
    
    SampleRef& sample = sampleBank_[index];
    
    // Create preview player on-demand for scrubbing
    if (!sample.previewPlayer) {
        sample.previewPlayer = std::make_shared<MediaPlayer>();
        if (!sample.previewPlayer->load(sample.audioPath, sample.videoPath)) {
            ofLogError("MultiSampler") << "Failed to create preview player for scrubbing";
            sample.previewPlayer.reset();
            return;
        }
    }
    
    // Sync parameters from parameter state cache to match normal playback
    sample.previewPlayer->speed.set(sample.currentSpeed);
    sample.previewPlayer->volume.set(sample.currentVolume);
    sample.previewPlayer->regionStart.set(sample.currentRegionStart);
    sample.previewPlayer->regionEnd.set(sample.currentRegionEnd);
    sample.previewPlayer->loopSize.set(sample.currentGrainSize);
    
    // Set position and play
    sample.previewPlayer->setPosition(position);
    if (!sample.previewPlayer->isPlaying()) {
        sample.previewPlayer->play();
    }
    
    sample.isScrubbing = true;
    sample.currentPlayheadPosition = position;
    
    // Connect preview player to mixers for audio feedback
    connectPlayerToInternalMixers(sample.previewPlayer.get());
}

void MultiSampler::stopScrubbingPlayback() {
    std::unique_lock<std::shared_mutex> lock(stateMutex);
    
    // Stop all preview players and ensure video is properly disconnected
    for (auto& sample : sampleBank_) {
        if (sample.previewPlayer && sample.isScrubbing) {
            sample.previewPlayer->stop();
            
            // Explicitly disconnect video if connected (prevents grey buffer artifacts)
            if (sample.previewPlayer->isVideoLoaded()) {
                if (sample.previewPlayer->videoPlayer.isPlaying()) {
                    sample.previewPlayer->videoPlayer.stop();
                }
                internalVideoMixer_.disconnectInput(&sample.previewPlayer->videoPlayer);
                sample.previewPlayer->videoEnabled.set(false);
                sample.previewPlayer->videoPlayer.enabled.set(false);
            }
            
            disconnectPlayerFromInternalMixers(sample.previewPlayer.get());
            sample.isScrubbing = false;
            // Keep previewPlayer for potential reuse, but mark as not scrubbing
        }
    }
}

//--------------------------------------------------------------
void MultiSampler::stopAllMedia() {
    std::unique_lock<std::shared_mutex> lock(stateMutex);
    
    // Clear lock-free event queue (drain all pending events)
    TriggerEvent dummy;
    while (eventQueue.try_dequeue(dummy)) {
        // Drain queue - events are discarded
    }
    
    // Clear all scheduled stops
    scheduledStops_.clear();
    
    // Release all voices
    releaseAllVoices();
    
    // Transition to IDLE
    currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
}
//--------------------------------------------------------------
void MultiSampler::setDataDirectory(const std::string& path) {
    ofLogNotice("MultiSampler") << "Setting data directory to: " << path;
    
    // CRITICAL: Lock mutex to prevent GUI/update loop from accessing players during directory change
    // This prevents race conditions where GUI tries to access players while they're being cleared/recreated
    std::unique_lock<std::shared_mutex> lock(stateMutex);
    
    try {
        ofDirectory dir(path);
        if (!dir.exists()) {
            ofLogError("MultiSampler") << "Directory does not exist: " << path;
            return;
        }
        
        ofLogNotice("MultiSampler") << "✅ Using data directory: " << path;
        
        // Scan and build sample bank
        scanDirectory(path);
        mediaPair();
        
        if (!sampleBank_.empty()) {
            ofLogNotice("MultiSampler") << "Sample bank ready with " << sampleBank_.size() << " samples";
        } else {
            ofLogWarning("MultiSampler") << "No samples created from directory: " << path;
        }
    } catch (const std::exception& e) {
        ofLogError("MultiSampler") << "Exception in setDataDirectory: " << e.what();
        releaseAllVoices();
    } catch (...) {
        ofLogError("MultiSampler") << "Unknown exception in setDataDirectory";
        releaseAllVoices();
    }
    
    // Notify ofApp about directory change (call outside mutex to avoid deadlock)
    // Note: This callback might access MultiSampler, so we need to be careful
    // But since we're done modifying players and mutex is released, it should be safe
    if (onDirectoryChanged) {
        onDirectoryChanged(path);
    }
}

//--------------------------------------------------------------
void MultiSampler::scanMediaFiles(const std::string& path, ofDirectory& dir) {
    // Configure directory to allow media file extensions (case-insensitive via allowExt)
    dir.allowExt("wav");
    dir.allowExt("mp3");
    dir.allowExt("aiff");
    dir.allowExt("aif");
    dir.allowExt("m4a");
    dir.allowExt("mov");
    dir.allowExt("mp4");
    dir.allowExt("avi");
    dir.allowExt("mkv");
    dir.allowExt("webm");
    dir.allowExt("hap");
    
    dir.listDir();
    
    ofLogNotice("MultiSampler") << "Found " << dir.size() << " files in directory";
    
    // Separate audio and video files
    for (int i = 0; i < dir.size(); i++) {
        std::string filename = dir.getName(i);
        std::string fullPath = dir.getPath(i);
        
        if (isAudioFile(filename)) {
            audioFiles.push_back(fullPath);
        } else if (isVideoFile(filename)) {
            videoFiles.push_back(fullPath);
        }
    }
    
    ofLogNotice("MultiSampler") << "Found " << audioFiles.size() << " audio files, " << videoFiles.size() << " video files";
}

//--------------------------------------------------------------

//--------------------------------------------------------------
void MultiSampler::browseForDirectory() {
    ofLogNotice("MultiSampler") << "Opening directory browser...";
    
    // Use OpenFrameworks file dialog to select directory
    ofFileDialogResult result = ofSystemLoadDialog("Select Media Directory", true);
    
    if (result.bSuccess) {
        std::string selectedPath = result.getPath();
        ofLogNotice("MultiSampler") << "Selected directory: " << selectedPath;
        setDataDirectory(selectedPath);
    } else {
        ofLogNotice("MultiSampler") << "Directory selection cancelled";
    }
}

//--------------------------------------------------------------
// Query methods for state checking
PlaybackMode MultiSampler::getCurrentMode() const {
    // Lock-free read (atomic)
    return currentMode.load(std::memory_order_relaxed);
}

bool MultiSampler::isPlaying() const {
    // Lock-free read (atomic)
    // Returns true if mode is PLAYING (any playback active)
    return currentMode.load(std::memory_order_relaxed) == PlaybackMode::PLAYING;
}

void MultiSampler::setModeIdle() {
    // Thread-safe transition to IDLE mode
    // Used by button handlers to immediately transition when stopping
    std::unique_lock<std::shared_mutex> lock(stateMutex);
    currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
}

//--------------------------------------------------------------
// Play style control (applies to both manual preview and sequencer playback)
void MultiSampler::setPlayStyle(PlayStyle style) {
    std::unique_lock<std::shared_mutex> lock(stateMutex);
    currentPlayStyle = style;
    ofLogNotice("MultiSampler") << "Play style set to: " << (int)style;
    
    // Apply the new style to all currently playing voices
    // CRITICAL: Don't enable underlying player loop for LOOP mode
    // The underlying players loop at full media level (0.0-1.0), but we need region-level looping
    // (loopStart to loopEnd based on loopSize). We handle looping manually in update().
    // Use VoiceManager's voice pool for iteration
    for (auto& voice : voiceManager_.getVoicePool()) {
        if (voice.state == Voice::PLAYING) {
            // Always disable underlying loop - looping is handled manually at region level
            // Update MediaPlayer's loop parameter
            voice.player.loop.set(false);
        }
    }
}

PlayStyle MultiSampler::getPlayStyle() const {
    std::shared_lock<std::shared_mutex> lock(stateMutex);
    return currentPlayStyle;
}

//--------------------------------------------------------------
void MultiSampler::update() {
    // Early exit if we're being destroyed
    if (isDestroying_.load(std::memory_order_acquire)) {
        return;
    }
    
    // Process event queue first (handles new triggers)
    try {
        processEventQueue();
    } catch (const std::exception& e) {
        ofLogError("MultiSampler") << "Exception processing event queue: " << e.what();
    } catch (...) {
        ofLogError("MultiSampler") << "Unknown exception processing event queue";
    }
    
    // Update all active voices and check for playback completion
    bool anyVoicePlaying = false;
    
    // Use VoiceManager's voice pool for iteration
    for (auto& voice : voiceManager_.getVoicePool()) {
        if (voice.state == Voice::PLAYING || voice.state == Voice::RELEASING) {
            try {
                // Update MediaPlayer (handles video updates and position sync)
                voice.player.update();
                
                // Check if envelope completed (for RELEASING voices)
                if (voice.state == Voice::RELEASING && !voice.voiceProcessor.isActive()) {
                    // Envelope completed - NOW stop the player and capture position
                    // CRITICAL: Stop player AFTER envelope has faded to prevent clicks
                    // This follows professional sampler practice: envelope controls gain,
                    // audio source continues playing during release, only stops after fade completes
                    float finalPosition = voice.player.captureCurrentPosition();
                    voice.player.stop();  // Stop after envelope has faded (MediaPlayer handles position freezing)
                    
                    // Safety check: Disconnect video if still connected (should already be disconnected in releaseVoice())
                    // This is a safety net in case videoConnected wasn't properly cleared
                    if (voice.videoConnected && voice.player.isVideoLoaded()) {
                        internalVideoMixer_.disconnectInput(&voice.player.videoPlayer);
                        voice.videoConnected = false;
                    }
                    
                    voice.startTime = 0.0f;
                    voice.state = Voice::FREE;
                    ofLogVerbose("MultiSampler") << "[VOICE] Release phase completed, transitioning to FREE";
                }
                // Check if playback ended (audio or video finished)
                // SIMPLIFIED LOGIC: Handle differently based on play style
                else if (voice.state == Voice::PLAYING && !voice.player.isPlaying()) {
                    if (currentPlayStyle == PlayStyle::ONCE) {
                        // Sample ended - trigger release (auto-release may have already happened, but this is a fallback)
                        // Note: If auto-release already occurred, state would be RELEASING and we wouldn't be in this block
                        ofLogVerbose("MultiSampler") << "[VOICE] Playback ended naturally (ONCE mode) - triggering release";
                        releaseVoice(voice);
                    } else {
                        // LOOP/NEXT/GRAIN: Player stopped at end of region, restart at loop point
                        float regStart = voice.regionStart.get();
                        float regEnd = voice.regionEnd.get();
                        float currentSpeed = voice.speed.get();
                        
                        // Calculate loop point based on GRAIN mode parameters
                        float loopStartPos = regStart;
                        if (currentPlayStyle == PlayStyle::GRAIN) {
                            float grainSz = voice.grainSize.get();
                            float duration = voice.getDuration();
                            if (grainSz > 0.0f && duration > MIN_DURATION) {
                                float startPos = voice.startPosition.get();
                                float regionSize = regEnd - regStart;
                                loopStartPos = regStart + startPos * regionSize;
                            }
                        }
                        
                        // Restart at appropriate position based on playback direction
                        if (currentSpeed < 0.0f) {
                            voice.setPosition(regEnd);  // Backward: restart at end
                        } else {
                            voice.setPosition(loopStartPos);  // Forward: restart at start
                        }
                        voice.player.play();
                        anyVoicePlaying = true;
                        ofLogVerbose("MultiSampler") << "[VOICE] Looped playback (sample " << voice.sampleIndex << ")";
                    }
                } else if (voice.state == Voice::PLAYING) {
                    anyVoicePlaying = true;
                    
                    // ONCE mode boundary check: auto-release before sample ends to prevent clicks
                    // Modern sampler practice: start release phase while audio is still available
                    if (currentPlayStyle == PlayStyle::ONCE) {
                        float rawPosition = voice.player.playheadPosition.get();
                        float regEnd = voice.regionEnd.get();
                        const float ONCE_MODE_EPSILON = 0.00001f;
                        
                        // Only check for auto-release if we're still in sustain phase
                        if (voice.state == Voice::PLAYING && 
                            voice.voiceProcessor.getEnvelope().getPhase() == Envelope::Phase::SUSTAIN) {
                            
                            float releaseMs = voice.releaseMs.get();
                            float sampleDurationSeconds = voice.getDuration();
                            
                            // Calculate how much normalized time the release phase takes
                            float releaseDurationNormalized = (releaseMs / 1000.0f) / sampleDurationSeconds;
                            float releaseStartPos = regEnd - releaseDurationNormalized;
                            
                            // Auto-release: start release phase while there's still audio to fade
                            // This prevents clicks when sample ends abruptly
                            if (rawPosition >= releaseStartPos && rawPosition < regEnd) {
                                ofLogVerbose("MultiSampler") << "[VOICE] Auto-releasing before sample end (position: " 
                                    << rawPosition << ", release starts at: " << releaseStartPos << ")";
                                releaseVoice(voice);
                            }
                        }
                        
                        // Stop when we've reached or exceeded the region end
                        if (rawPosition >= regEnd - ONCE_MODE_EPSILON) {
                            voice.setPosition(regEnd);
                            
                            // Update GUI state before releasing (GUI will sync based on its selection)
                            if (static_cast<size_t>(voice.sampleIndex) < sampleBank_.size()) {
                                sampleBank_[voice.sampleIndex].currentPlayheadPosition = regEnd;
                            }
                            
                            // If not already releasing, trigger release now (may be too late, but better than nothing)
                            if (voice.state == Voice::PLAYING) {
                                releaseVoice(voice);
                            }
                        }
                    }
                }
            } catch (const std::exception& e) {
                ofLogError("MultiSampler") << "Exception updating voice: " << e.what();
                releaseVoice(voice);
            }
        }
    }
    
    // Sync GUI state when scrubbing (from voice if reused, otherwise from preview player)
    // Note: GUI state sync for playing samples is handled in GUI layer (MultiSamplerGUI)
    for (size_t i = 0; i < sampleBank_.size(); ++i) {
        auto& sample = sampleBank_[i];
        
        if (sample.isScrubbing) {
            // Check if we're reusing a voice for scrubbing
            Voice* scrubbingVoice = getVoiceForSample(static_cast<int>(i));
            if (scrubbingVoice && scrubbingVoice->state == Voice::PLAYING) {
                // Reusing voice - sync from voice (scrubbing is backend state management)
                syncParameterStateFromVoice(i, scrubbingVoice);
            } else if (sample.previewPlayer) {
                // Using preview player - sync from preview player
                sample.previewPlayer->update();
                sample.currentPlayheadPosition = sample.previewPlayer->playheadPosition.get();
                sample.currentSpeed = sample.previewPlayer->speed.get();
                sample.currentVolume = sample.previewPlayer->volume.get();
                sample.currentStartPosition = sample.previewPlayer->startPosition.get();
                sample.currentRegionStart = sample.previewPlayer->regionStart.get();
                sample.currentRegionEnd = sample.previewPlayer->regionEnd.get();
                sample.currentGrainSize = sample.previewPlayer->loopSize.get();
                
                // Stop scrubbing if preview player finished
                if (!sample.previewPlayer->isPlaying()) {
                    sample.isScrubbing = false;
                }
            }
        }
    }
    
    // Check scheduled stops (gate duration expired)
    float currentTime = ofGetElapsedTimef();
    for (auto it = scheduledStops_.begin(); it != scheduledStops_.end();) {
        if (currentTime >= it->stopTime) {
            // Gate duration expired - check if voice is still playing the same note
            // Generation mismatch means voice was reused for a new trigger - skip this stop
            if (it->voice && it->voice->state == Voice::PLAYING && 
                it->voice->generation == it->expectedGeneration) {
                ofLogVerbose("MultiSampler") << "[GATE_STOP] Releasing voice after gate duration expired";
                releaseVoice(*it->voice);
            } else if (it->voice && it->voice->generation != it->expectedGeneration) {
                ofLogVerbose("MultiSampler") << "[GATE_STOP] Skipping stale stop (voice reused for new trigger)";
            }
            it = scheduledStops_.erase(it);
        } else {
            ++it;
        }
    }
    
    // Update playback mode based on voice activity
    PlaybackMode mode = currentMode.load(std::memory_order_relaxed);
    if (mode == PlaybackMode::PLAYING && !anyVoicePlaying) {
        currentMode.store(PlaybackMode::IDLE, std::memory_order_relaxed);
        ofLogVerbose("MultiSampler") << "[STOP] No active voices - transitioning to IDLE";
    }
}

//--------------------------------------------------------------
// Process a single trigger event using voice allocation
void MultiSampler::processEvent(const TriggerEvent& event) {
    // Extract sample index from "note" parameter
    auto noteIt = event.parameters.find("note");
    int sampleIndex = (noteIt != event.parameters.end()) ? static_cast<int>(noteIt->second) : -1;
    
    
    // Handle empty cells (rests) - in MONO mode, stop all voices
    if (sampleIndex < 0) {
        if (polyphonyMode_ == PolyphonyMode::MONOPHONIC) {
            releaseAllVoices();
        }
        return;
    }
    
    // Use unified triggerSample method (handles allocation, loading, connecting, playing)
    Voice* voice = triggerSample(sampleIndex, &event);
    if (!voice) return;
    
    // Schedule stop after gate duration expires (sequencer-specific)
    // Capture voice generation so stale stops are ignored if voice is reused
    if (event.duration > 0.0f) {
        scheduledStops_.push_back({voice, ofGetElapsedTimef() + event.duration, voice->generation});
    }
}

//--------------------------------------------------------------
void MultiSampler::processEventQueue() {
    // Don't process events if module is disabled
    if (!isEnabled()) {
        return;
    }
    
    // Process events from lock-free queue (no mutex needed!)
    // Consumer: GUI thread (this function)
    // Producer: Audio thread (onTrigger)
    // 
    // Limit processing per frame to prevent GUI thread blocking
    // Increased for sequencer support (16th notes at 140 BPM = ~37 triggers/sec)
    const int maxEventsPerFrame = 500;
    int eventsProcessed = 0;
    
    
    TriggerEvent event;
    while (eventQueue.try_dequeue(event) && eventsProcessed < maxEventsPerFrame) {
        eventsProcessed++;
        processEvent(event);
    }
    
    
    // Log warning if queue is backing up (but only occasionally to avoid spam)
    static int warningFrameCount = 0;
    if (++warningFrameCount % 300 == 0) { // Every 5 seconds at 60fps
        size_t remainingQueueSize = eventQueue.size_approx();
        if (remainingQueueSize > 100) {
            ofLogWarning("MultiSampler") << "Event queue backing up - " << remainingQueueSize 
                                      << " events remaining (processed " << eventsProcessed 
                                      << " this frame, maxEventsPerFrame: " << maxEventsPerFrame << ")";
        }
    }
    
    // Check if we hit the processing limit (indicates queue might be backing up)
    if (eventsProcessed >= maxEventsPerFrame) {
        // Check if there are still events in queue
        size_t remainingEvents = eventQueue.size_approx();
        if (remainingEvents > 0) {
            ofLogWarning("MultiSampler") << "Event queue processing limit reached (" << maxEventsPerFrame 
                                      << " events processed this frame). " << remainingEvents 
                                      << " events still in queue. Consider increasing maxEventsPerFrame or reducing trigger rate.";
        }
    }
}



//--------------------------------------------------------------
// Module interface implementation
//--------------------------------------------------------------
std::string MultiSampler::getName() const {
    return "MultiSampler";
}

// getTypeName() uses default implementation from Module base class (returns getName())

void MultiSampler::setEnabledImpl(bool enabled) {
    // Base class handles enabled_ state (lock already held)
    // We can directly access enabled_ since lock is held by wrapper
    bool wasEnabled = enabled_;
    enabled_ = enabled;
    
    // Stop all media when disabled
    if (wasEnabled && !enabled) {
        stopAllMedia();
    }
}

ofJson MultiSampler::toJson(class ModuleRegistry* registry) const {
    ofJson json;
    
    // Save enabled state
    json["enabled"] = isEnabled();
    
    // Save current sample index
    // NOTE: displayIndex_ serialization removed - GUI now manages selectedSampleIndex_
    
    // Save play style
    json["playStyle"] = static_cast<int>(currentPlayStyle);
    
    // Save polyphony mode
    json["polyphonyMode"] = (polyphonyMode_ == PolyphonyMode::POLYPHONIC) ? 1.0f : 0.0f;
    
    // SAMPLER-INSPIRED: Save sample bank with default parameters (per-sample configuration)
    ofJson samplesArray = ofJson::array();
    for (const auto& sample : sampleBank_) {
        ofJson sampleJson;
        sampleJson["audio"] = sample.audioPath;
        sampleJson["video"] = sample.videoPath;
        // Save default parameters (per-sample configuration)
        sampleJson["defaultRegionStart"] = sample.defaultRegionStart;
        sampleJson["defaultRegionEnd"] = sample.defaultRegionEnd;
        sampleJson["defaultStartPosition"] = sample.defaultStartPosition;
        sampleJson["defaultSpeed"] = sample.defaultSpeed;
        sampleJson["defaultVolume"] = sample.defaultVolume;
        sampleJson["defaultGrainSize"] = sample.defaultGrainSize;
        samplesArray.push_back(sampleJson);
    }
    json["samples"] = samplesArray;
    
    ofLogNotice("MultiSampler") << "Serialized " << sampleBank_.size() 
                             << " sample references to session (no player state)";
    
    return json;
}

void MultiSampler::fromJson(const ofJson& json) {
    // Load enabled state
    if (json.contains("enabled")) {
        setEnabled(json["enabled"].get<bool>());
    }
    
    // Load play style
    if (json.contains("playStyle")) {
        int styleInt = json["playStyle"];
        // Support old enum values (0=ONCE, 1=LOOP, 2=NEXT) and new values (3=GRAIN)
        if (styleInt >= 0 && styleInt <= 3) {
            setPlayStyle(static_cast<PlayStyle>(styleInt));
        }
    }
    
    // Load polyphony mode (default to MONOPHONIC for backward compatibility)
    if (json.contains("polyphonyMode")) {
        float modeValue = json["polyphonyMode"];
        polyphonyMode_ = (modeValue >= 0.5f) ? PolyphonyMode::POLYPHONIC : PolyphonyMode::MONOPHONIC;
        ofLogNotice("MultiSampler") << "Loaded polyphony mode: " 
                                  << (polyphonyMode_ == PolyphonyMode::POLYPHONIC ? "POLYPHONIC" : "MONOPHONIC");
    } else {
        polyphonyMode_ = PolyphonyMode::MONOPHONIC;
    }
    
    // Clear existing state
    releaseAllVoices();
    sampleBank_.clear();
    
    // SAMPLER-INSPIRED: Load sample bank (new format)
    if (json.contains("samples") && json["samples"].is_array()) {
        for (const auto& sampleJson : json["samples"]) {
            SampleRef sample;
            sample.audioPath = sampleJson.value("audio", "");
            sample.videoPath = sampleJson.value("video", "");
            sample.displayName = computeDisplayName(sample);
            sample.duration = 0.0f;  // Will be set when played
            sample.metadataLoaded = false;
            
            // Load default parameters (with fallback to struct defaults)
            sample.defaultRegionStart = sampleJson.value("defaultRegionStart", 0.0f);
            sample.defaultRegionEnd = sampleJson.value("defaultRegionEnd", 1.0f);
            sample.defaultStartPosition = sampleJson.value("defaultStartPosition", 0.0f);
            sample.defaultSpeed = sampleJson.value("defaultSpeed", 1.0f);
            sample.defaultVolume = sampleJson.value("defaultVolume", 1.0f);
            // Support legacy "defaultLoopSize" for backward compatibility
            sample.defaultGrainSize = sampleJson.value("defaultGrainSize", 
                                                       sampleJson.value("defaultLoopSize", 0.0f));
            
            // Initialize parameter state cache to match defaults
            sample.currentSpeed = sample.defaultSpeed;
            sample.currentVolume = sample.defaultVolume;
            sample.currentStartPosition = sample.defaultStartPosition;
            sample.currentRegionStart = sample.defaultRegionStart;
            sample.currentRegionEnd = sample.defaultRegionEnd;
            sample.currentGrainSize = sample.defaultGrainSize;
            
            if (sample.hasMedia()) {
                sampleBank_.push_back(sample);
            }
        }
        ofLogNotice("MultiSampler") << "Loaded " << sampleBank_.size() 
                                 << " samples from session (media loaded on trigger)";
                }
    // LEGACY: Support old "players" array format
    else if (json.contains("players") && json["players"].is_array()) {
        for (const auto& playerJson : json["players"]) {
            SampleRef sample;
            sample.audioPath = playerJson.value("audioFile", "");
            sample.videoPath = playerJson.value("videoFile", "");
            sample.displayName = computeDisplayName(sample);
            sample.duration = 0.0f;
            sample.metadataLoaded = false;
            
            if (sample.hasMedia()) {
                sampleBank_.push_back(sample);
            }
        }
        ofLogNotice("MultiSampler") << "Migrated " << sampleBank_.size() 
                                 << " samples from legacy 'players' format";
    }
    // LEGACY: Support old directory-based format
    else if (json.contains("directory")) {
        std::string dir = json["directory"];
        if (!dir.empty() && ofDirectory(dir).exists()) {
            setDataDirectory(dir);
        } else {
            ofLogWarning("MultiSampler") << "Legacy directory not found: " << dir;
        }
    }
    
    // Restore current index
    // NOTE: displayIndex_ serialization removed - GUI now manages selectedSampleIndex_
    // Legacy fields are ignored (GUI will handle selection state)
}

ModuleType MultiSampler::getType() const {
    return ModuleType::INSTRUMENT;
}

//--------------------------------------------------------------
bool MultiSampler::hasCapability(ModuleCapability capability) const {
    switch (capability) {
        case ModuleCapability::ACCEPTS_FILE_DROP:
        case ModuleCapability::ACCEPTS_TRIGGER_EVENTS:
            return true;
        default:
            return false;
    }
}

//--------------------------------------------------------------
std::vector<ModuleCapability> MultiSampler::getCapabilities() const {
    return {
        ModuleCapability::ACCEPTS_FILE_DROP,
        ModuleCapability::ACCEPTS_TRIGGER_EVENTS
    };
}

//--------------------------------------------------------------
// REMOVED: getIndexRange() - use getSampleCount() instead
    // NOTE: "index" is NOT a parameter - it's GUI display state (GUI manages selectedSampleIndex_)

//--------------------------------------------------------------
Module::ModuleMetadata MultiSampler::getMetadata() const {
    Module::ModuleMetadata metadata;
    metadata.typeName = "MultiSampler";
    metadata.eventNames = {"onTrigger"};  // Event it accepts (not emits)
    metadata.parameterNames = {"position"};
    metadata.parameterDisplayNames["position"] = "Position";
    return metadata;
}

std::vector<ParameterDescriptor> MultiSampler::getParametersImpl() const {
    std::vector<ParameterDescriptor> params;
    
    // MultiSampler parameters that can be controlled by TrackerSequencer
    // These are the parameters that TrackerSequencer sends in trigger events
    // MultiSampler maps these to MediaPlayer parameters
    
    // Core playback parameters
    // NOTE: "index" is NOT a parameter - it's GUI display state (use getDisplayIndex() / setDisplayIndex() directly)
    // NOTE: "note" is handled in trigger events for playback selection, but not exposed as a parameter
    // to avoid conflicts with TrackerSequencer's internal "note" parameter (musical notes)
    params.push_back(ParameterDescriptor("position", ParameterType::FLOAT, 0.0f, 1.0f, 0.0f, "Position"));
    params.push_back(ParameterDescriptor("speed", ParameterType::FLOAT, -10.0f, 10.0f, 1.0f, "Speed"));
    params.push_back(ParameterDescriptor("volume", ParameterType::FLOAT, 0.0f, 2.0f, 1.0f, "Volume"));
    
    // Region and loop control
    // CRITICAL: Default value is 0.0f (use full region) to match SampleRef defaultGrainSize
    // The GUI will handle logarithmic mapping and display appropriately
    params.push_back(ParameterDescriptor("grainSize", ParameterType::FLOAT, 0.0f, 10.0f, 0.0f, "Grain Size (seconds)"));
    params.push_back(ParameterDescriptor("regionStart", ParameterType::FLOAT, 0.0f, 1.0f, 0.0f, "Region Start"));
    params.push_back(ParameterDescriptor("regionEnd", ParameterType::FLOAT, 0.0f, 1.0f, 1.0f, "Region End"));
    
    // Polyphony control
    params.push_back(ParameterDescriptor("polyphonyMode", ParameterType::INT, 0.0f, 1.0f, 0.0f, "Polyphony Mode"));
    
    // ADSR envelope parameters (now routable/modulatable via ParameterRouter)
    // These control the default ADSR for new voices
    // FUTURE: Per-voice ADSR modulation (route LFO to individual voice ADSR parameters)
    params.push_back(ParameterDescriptor("attackMs", ParameterType::FLOAT, 0.0f, std::numeric_limits<float>::max(), 0.0f, "Attack (ms)"));
    params.push_back(ParameterDescriptor("decayMs", ParameterType::FLOAT, 0.0f, std::numeric_limits<float>::max(), 0.0f, "Decay (ms)"));
    params.push_back(ParameterDescriptor("sustain", ParameterType::FLOAT, 0.0f, 1.0f, 1.0f, "Sustain"));
    params.push_back(ParameterDescriptor("releaseMs", ParameterType::FLOAT, 5.0f, 5000.0f, 10.0f, "Release (ms)"));
    
    return params;
}

void MultiSampler::setParameterImpl(const std::string& paramName, float value, bool notify) {
    // Handle polyphonyMode parameter (module-level, not player-level)
    if (paramName == "polyphonyMode") {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        PolyphonyMode oldMode = polyphonyMode_;
        polyphonyMode_ = (value >= 0.5f) ? PolyphonyMode::POLYPHONIC : PolyphonyMode::MONOPHONIC;
        
        // Update VoiceManager's polyphony mode
        voiceManager_.setPolyphonyMode(
            polyphonyMode_ == PolyphonyMode::POLYPHONIC 
                ? VoiceManager<Voice, MAX_VOICES>::PolyphonyMode::POLYPHONIC 
                : VoiceManager<Voice, MAX_VOICES>::PolyphonyMode::MONOPHONIC
        );
        
        if (polyphonyMode_ != oldMode) {
            // Mode changed - handle transition
            // NOTE: We don't stop all non-active players here because:
            // 1. It breaks video mixing (stopped players output transparent but remain connected)
            // 2. The original mono behavior only stopped the previous active player when switching
            // 3. Players will be properly stopped when switching in processEventQueue() and playMediaManual()
            if (polyphonyMode_ == PolyphonyMode::MONOPHONIC) {
                ofLogNotice("MultiSampler") << "[POLYPHONY] Switched to MONOPHONIC mode - will stop previous player on next switch";
            } else {
                // Switching to POLY: no action needed - allow all to play
                ofLogNotice("MultiSampler") << "[POLYPHONY] Switched to POLYPHONIC mode - multiple players can play simultaneously";
            }
            
            if (notify && parameterChangeCallback) {
                parameterChangeCallback(paramName, value);
            }
        }
        return;
    }
    
    // Handle ADSR envelope parameters (now routable/modulatable)
    // These update the default ADSR values for new voices
    // FUTURE: Apply to all active voices in real-time (currently only affects new voices)
    if (paramName == "attackMs") {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        defaultAttackMs_ = std::max(0.0f, value);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback(paramName, value);
        }
        return;
    }
    if (paramName == "decayMs") {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        defaultDecayMs_ = std::max(0.0f, value);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback(paramName, value);
        }
        return;
    }
    if (paramName == "sustain") {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        defaultSustain_ = ofClamp(value, 0.0f, 1.0f);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback(paramName, value);
        }
        return;
    }
    if (paramName == "releaseMs") {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        defaultReleaseMs_ = std::max(ANTI_CLICK_FADE_MS, value);  // Enforce minimum release time
        if (notify && parameterChangeCallback) {
            parameterChangeCallback(paramName, value);
        }
        return;
    }
    
    // Handle granular synthesis parameters (GRAIN mode)
    if (paramName == "grainEnvelope") {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        defaultGrainEnvelope_ = static_cast<int>(std::max(0.0f, std::min(4.0f, value)));  // Clamp to 0-4
        if (notify && parameterChangeCallback) {
            parameterChangeCallback(paramName, value);
        }
        return;
    }
    
    // Continuous parameter modulation (for modulators, envelopes, etc.)
    // For MultiSampler, we apply this to parameter state cache (synced to voices on next trigger)
    // NOTE: Parameters operate on sample at index 0 as default (GUI manages selection)
    std::unique_lock<std::shared_mutex> lock(stateMutex);
    
    if (sampleBank_.empty()) return;
    SampleRef& displaySample = sampleBank_[0];  // Use index 0 as default
    
    // Get parameter descriptor to validate range
    auto paramDescriptors = getParameters();
    const ParameterDescriptor* paramDesc = nullptr;
    for (const auto& param : paramDescriptors) {
        if (param.name == paramName) {
            paramDesc = &param;
            break;
        }
    }
    
    // Clamp value to parameter range if descriptor found
    float clampedValue = value;
    if (paramDesc) {
        clampedValue = std::max(paramDesc->minValue, std::min(paramDesc->maxValue, value));
    }
    
    float oldValue = 0.0f;
    bool valueChanged = false;
    
        // Update both SampleRef default parameters (for next trigger) and parameter state cache (for getParameter API)
    if (paramName == "volume") {
        oldValue = displaySample.defaultVolume;
        displaySample.defaultVolume = clampedValue;  // Update default (for next trigger)
        displaySample.currentVolume = clampedValue;      // Update GUI state (for display)
        valueChanged = std::abs(oldValue - clampedValue) > PARAMETER_EPSILON;
    } else if (paramName == "speed") {
        oldValue = displaySample.defaultSpeed;
        displaySample.defaultSpeed = clampedValue;
        displaySample.currentSpeed = clampedValue;
        valueChanged = std::abs(oldValue - clampedValue) > PARAMETER_EPSILON;
    } else if (paramName == "grainSize" || paramName == "loopSize") {
        // Support both new name (grainSize) and old name (loopSize) for backward compatibility
        oldValue = displaySample.defaultGrainSize;
        // Clamp grainSize to valid range (0.0 = use full region, or 0.001s minimum, up to duration or 10s max)
        float duration = displaySample.duration;
        float maxAllowed = (duration > MIN_DURATION) ? duration : 10.0f;
        // Allow 0.0 to mean "use full region", otherwise clamp to valid range
        float clampedVal = (clampedValue <= 0.0f) ? 0.0f : std::max(MIN_LOOP_SIZE, std::min(maxAllowed, clampedValue));
        displaySample.defaultGrainSize = clampedVal;
        displaySample.currentGrainSize = clampedVal;
        valueChanged = std::abs(oldValue - clampedVal) > PARAMETER_EPSILON;
    } else if (paramName == "regionStart" || paramName == "loopStart") {
        // Support both new name (regionStart) and old name (loopStart) for backward compatibility
        oldValue = displaySample.defaultRegionStart;
        displaySample.defaultRegionStart = clampedValue;
        displaySample.currentRegionStart = clampedValue;
        valueChanged = std::abs(oldValue - clampedValue) > PARAMETER_EPSILON;
        
        // CRITICAL: When regionStart changes, recalculate playheadPosition from current relative position
        // This ensures the playhead stays at the same relative position within the new region bounds
        // BUT: Only update playheadPosition if we have a valid relative position
        if (valueChanged && displaySample.currentStartPosition >= 0.0f && displaySample.currentStartPosition <= 1.0f) {
            float regionSize = displaySample.currentRegionEnd - clampedValue;
            if (regionSize > MIN_REGION_SIZE) {
                float absolutePos = clampedValue + displaySample.currentStartPosition * regionSize;
                displaySample.currentPlayheadPosition = absolutePos;
            }
        }
    } else if (paramName == "regionEnd" || paramName == "loopEnd") {
        // Support both new name (regionEnd) and old name (loopEnd) for backward compatibility
        oldValue = displaySample.defaultRegionEnd;
        displaySample.defaultRegionEnd = clampedValue;
        displaySample.currentRegionEnd = clampedValue;
        valueChanged = std::abs(oldValue - clampedValue) > PARAMETER_EPSILON;
        
        // CRITICAL: When regionEnd changes, recalculate playheadPosition from current relative position
        // This ensures the playhead stays at the same relative position within the new region bounds
        if (valueChanged && displaySample.currentStartPosition >= 0.0f && displaySample.currentStartPosition <= 1.0f) {
            float regionSize = clampedValue - displaySample.currentRegionStart;
            if (regionSize > MIN_REGION_SIZE) {
                float absolutePos = displaySample.currentRegionStart + displaySample.currentStartPosition * regionSize;
                displaySample.currentPlayheadPosition = absolutePos;
            }
        }
    } else if (paramName == "position") {
        // Position is relative (0.0-1.0 within region)
        oldValue = displaySample.defaultStartPosition;
        if (std::abs(oldValue - clampedValue) > PARAMETER_EPSILON) {
            // Clamp to valid relative range using helper (handles ONCE mode clamping)
            float relativePos = clampPositionForPlayback(clampedValue, currentPlayStyle);
            displaySample.defaultStartPosition = relativePos;
            displaySample.currentStartPosition = relativePos;
            
            // Update playheadPosition for UI display (map to absolute using helper function)
            float regionStartVal = displaySample.currentRegionStart;
            float regionEndVal = displaySample.currentRegionEnd;
            float absolutePos = mapRelativeToAbsolute(relativePos, regionStartVal, regionEndVal);
            displaySample.currentPlayheadPosition = absolutePos;
            valueChanged = true;
        }
    }
    
    // Also update preview player if scrubbing (for immediate audio feedback)
    if (displaySample.isScrubbing && displaySample.previewPlayer) {
        MediaPlayer* previewPlayer = displaySample.previewPlayer.get();
        if (paramName == "volume") {
            previewPlayer->volume.set(clampedValue);
        } else if (paramName == "speed") {
            previewPlayer->speed.set(clampedValue);
        } else if (paramName == "grainSize" || paramName == "loopSize") {
            // Use clamped value for consistency
            float grainVal = (clampedValue <= 0.0f) ? 0.0f : std::max(MIN_LOOP_SIZE, std::min(10.0f, clampedValue));
            previewPlayer->loopSize.set(grainVal);  // MediaPlayer still uses loopSize
        } else if (paramName == "regionStart" || paramName == "loopStart") {
            previewPlayer->regionStart.set(clampedValue);
        } else if (paramName == "regionEnd" || paramName == "loopEnd") {
            previewPlayer->regionEnd.set(clampedValue);
        } else if (paramName == "position") {
            previewPlayer->startPosition.set(displaySample.currentStartPosition);
            previewPlayer->playheadPosition.set(displaySample.currentPlayheadPosition);
        }
    }
    
    // Also update any active Voice playing sample at index 0 (for real-time modulation)
    // NOTE: Parameters operate on index 0 as default (GUI manages which sample is selected)
    // Use VoiceManager's voice pool for iteration
    for (auto& voice : voiceManager_.getVoicePool()) {
        if (voice.state == Voice::PLAYING && voice.sampleIndex == 0) {
            if (paramName == "volume") {
                voice.volume.set(clampedValue);
                // Update MediaPlayer parameter (handles audio volume)
                voice.player.volume.set(clampedValue);
            } else if (paramName == "speed") {
                voice.speed.set(clampedValue);
                // Update MediaPlayer parameter (handles both audio and video speed)
                voice.player.speed.set(clampedValue);
            } else if (paramName == "grainSize" || paramName == "loopSize") {
                // CRITICAL: Update Voice grainSize from the clamped value, not from GUI state
                // This ensures the Voice gets the actual updated value
                voice.grainSize.set(clampedValue);
            } else if (paramName == "regionStart" || paramName == "loopStart") {
                voice.regionStart.set(clampedValue);
                
                // CRITICAL: When region changes during playback, seek players to new bounds
                if (voice.isPlaying()) {
                    // Update MediaPlayer region parameter
                    voice.player.regionStart.set(clampedValue);
                    
                    // Get current position from MediaPlayer (single source of truth)
                    float currentPos = voice.player.playheadPosition.get();
                    float newRegionStart = clampedValue;
                    float newRegionEnd = voice.regionEnd.get();
                    
                    // Clamp current position to new region bounds
                    float clampedPos = std::max(newRegionStart, std::min(newRegionEnd, currentPos));
                    
                    // Seek MediaPlayer to clamped position (handles both audio and video)
                    voice.player.setPosition(clampedPos);
                    
                    // Update relative startPosition to reflect new position within new region
                    float regionSize = newRegionEnd - newRegionStart;
                    if (regionSize > MIN_REGION_SIZE) {
                        float newRelativePos = (clampedPos - newRegionStart) / regionSize;
                        voice.startPosition.set(newRelativePos);
                    }
                }
            } else if (paramName == "regionEnd" || paramName == "loopEnd") {
                voice.regionEnd.set(clampedValue);
                
                // CRITICAL: When region changes during playback, seek players to new bounds
                if (voice.isPlaying()) {
                    // Update MediaPlayer region parameter
                    voice.player.regionEnd.set(clampedValue);
                    
                    // Get current position from MediaPlayer (single source of truth)
                    float currentPos = voice.player.playheadPosition.get();
                    float newRegionStart = voice.regionStart.get();
                    float newRegionEnd = clampedValue;
                    
                    // Clamp current position to new region bounds
                    float clampedPos = std::max(newRegionStart, std::min(newRegionEnd, currentPos));
                    
                    // Seek MediaPlayer to clamped position (handles both audio and video)
                    voice.player.setPosition(clampedPos);
                    
                    // Update relative startPosition to reflect new position within new region
                    float regionSize = newRegionEnd - newRegionStart;
                    if (regionSize > MIN_REGION_SIZE) {
                        float newRelativePos = (clampedPos - newRegionStart) / regionSize;
                        voice.startPosition.set(newRelativePos);
                    }
                }
            } else if (paramName == "position") {
                // Update startPosition (for next trigger) - does NOT affect current playback
                // When scrubbing while playing, we only update startPosition, not playheadPosition
                voice.startPosition.set(clampedValue);
                // DO NOT call voice.player.setPosition() - that would seek current playback
            }
        }
    }
    
    // Notify parameter change callback if set and value changed
    if (notify && valueChanged && parameterChangeCallback) {
        parameterChangeCallback(paramName, clampedValue);
    }
    
    // Note: "note" parameter can't be set continuously - it's only for triggers
}

float MultiSampler::getParameterImpl(const std::string& paramName) const {
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"K\",\"location\":\"MultiSampler.cpp:getParameter\",\"message\":\"getParameter() ENTRY\",\"data\":{\"paramName\":\"" << paramName << "\",\"thisPtr\":\"" << this << "\"},\"timestamp\":" << now << "}\n";
            logFile.flush();
            logFile.close();
        }
    }
    // #endregion
    
    // Handle "polyphonyMode" - module-level parameter
    if (paramName == "polyphonyMode") {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        return (polyphonyMode_ == PolyphonyMode::POLYPHONIC) ? 1.0f : 0.0f;
    }
    
    // Handle ADSR envelope parameters
    if (paramName == "attackMs") {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        return defaultAttackMs_;
    }
    if (paramName == "decayMs") {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        return defaultDecayMs_;
    }
    if (paramName == "sustain") {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        return defaultSustain_;
    }
    if (paramName == "releaseMs") {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        return defaultReleaseMs_;
    }
    if (paramName == "grainEnvelope") {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        return static_cast<float>(defaultGrainEnvelope_);
    }
    
    // NOTE: "index" is NOT a parameter - it's GUI display state
    // GUI manages selectedSampleIndex_ for navigation
    // Use "note" in trigger events for playback selection
    
    // Read from parameter state cache (synced from voice during playback, editable when idle)
    // NOTE: Parameters operate on sample at index 0 as default (GUI manages selection)
    std::shared_lock<std::shared_mutex> lock(stateMutex);
    
    // Safety check: ensure sampleBank_ is not empty
    if (sampleBank_.empty()) {
        // No samples - return default value
        if (paramName == "position" || paramName == "speed" || paramName == "volume" ||
            paramName == "regionStart" || paramName == "loopStart" ||
            paramName == "regionEnd" || paramName == "loopEnd" ||
            paramName == "grainSize" || paramName == "loopSize") {
            return 0.0f;  // Default values for these parameters
        }
        return 0.0f;
    }
    
    // Use index 0 as default (GUI manages which sample is selected for editing)
    const SampleRef& displaySample = sampleBank_[0];
    
    // Read from parameter state cache
    if (paramName == "position") {
        return displaySample.currentStartPosition;
    } else if (paramName == "speed") {
        return displaySample.currentSpeed;
    } else if (paramName == "volume") {
        return displaySample.currentVolume;
    } else if (paramName == "regionStart" || paramName == "loopStart") {
        return displaySample.currentRegionStart;
    } else if (paramName == "regionEnd" || paramName == "loopEnd") {
        return displaySample.currentRegionEnd;
    } else if (paramName == "grainSize" || paramName == "loopSize") {
        return displaySample.currentGrainSize;
    } else if (paramName == "note") {
        // Note parameter represents the media index (not a continuous parameter)
        return 0.0f;
    }
    
    // Unknown parameter - return default (base class default is 0.0f)
    // NOTE: Cannot call Module::getParameter() here as it would deadlock (lock already held)
    return 0.0f;
}

void MultiSampler::onTrigger(TriggerEvent& event) {
    // Don't process triggers if module is disabled
    if (!isEnabled()) return;
    
    // Increment diagnostic counter (atomic, safe from audio thread)
    onTriggerCallCount_.fetch_add(1, std::memory_order_relaxed);
    
    // LOCK-FREE: Push event to queue from audio thread (no mutex needed!)
    // Position memory application happens in processEventQueue() (GUI thread) to avoid race conditions
    // 
    // CRITICAL: NO LOGGING IN AUDIO THREAD - logging can allocate memory and cause crashes
    // All logging is done in processEventQueue() (GUI thread) where it's safe
    
    // Make a copy of the event to ensure safe enqueueing
    // This ensures the std::map inside TriggerEvent is properly copied
    TriggerEvent eventCopy = event;
    
    // Queue event using lock-free queue (no mutex needed!)
    // If queue is full, event is dropped silently (logging happens in GUI thread)
    if (!eventQueue.try_enqueue(eventCopy)) {
        // Queue full - drop event silently (can't log from audio thread)
        // The GUI thread will log a warning if it detects queue issues
        // Use atomic counter to track dropped events (increment a separate counter)
    }
}

//--------------------------------------------------------------
// Position scan mode control
PolyphonyMode MultiSampler::getPolyphonyMode() const {
    std::shared_lock<std::shared_mutex> lock(stateMutex);
    return polyphonyMode_;
}

//--------------------------------------------------------------
// Module routing interface - expose stable audio/video outputs via internal mixers
ofxSoundObject* MultiSampler::getAudioOutput() const {
    return const_cast<ofxSoundMixer*>(&internalAudioMixer_);
}

ofxVisualObject* MultiSampler::getVideoOutput() const {
    return const_cast<ofxVideoMixer*>(&internalVideoMixer_);
}

//--------------------------------------------------------------
// Port-based routing interface (Phase 1)
std::vector<Port> MultiSampler::getInputPorts() const {
    return {
        Port("trigger_in", PortType::EVENT_IN, false, "Trigger Input")
    };
}

std::vector<Port> MultiSampler::getOutputPorts() const {
    return {
        Port("audio_out", PortType::AUDIO_OUT, false, "Audio Output", 
             const_cast<void*>(static_cast<const void*>(&internalAudioMixer_))),
        Port("video_out", PortType::VIDEO_OUT, false, "Video Output",
             const_cast<void*>(static_cast<const void*>(&internalVideoMixer_)))
    };
}

// ========================================================================
// INTERNAL CONNECTION MANAGEMENT (For Preview Players Only)
// ========================================================================
// NOTE: Voice connections are handled automatically during allocation/release.
// These methods are only for preview players used during scrubbing.

void MultiSampler::connectPlayerToInternalMixers(MediaPlayer* player) {
    if (!player) return;
    
    try {
        // Connect audio if available
        if (player->isAudioLoaded()) {
            player->getAudioPlayer().connectTo(internalAudioMixer_);
        }
        
        // Connect video if available
        if (player->isVideoLoaded()) {
            internalVideoMixer_.setInput(&player->getVideoPlayer());
        }
        
        ofLogNotice("MultiSampler") << "Connected player to internal mixers";
    } catch (const std::exception& e) {
        ofLogError("MultiSampler") << "Failed to connect player to internal mixers: " << e.what();
    }
}

void MultiSampler::disconnectPlayerFromInternalMixers(MediaPlayer* player) {
    if (!player) return;
    
    try {
        // Disconnect audio if connected
        if (player->isAudioLoaded()) {
            player->getAudioPlayer().disconnect();
        }
        
        // Disconnect video if connected
        // FIXED: Use disconnectInput() on mixer (correct API for ofxVideoMixer)
        if (player->isVideoLoaded()) {
            internalVideoMixer_.disconnectInput(&player->getVideoPlayer());
        }
        
        ofLogNotice("MultiSampler") << "Disconnected player from internal mixers";
    } catch (const std::exception& e) {
        ofLogError("MultiSampler") << "Failed to disconnect player from internal mixers: " << e.what();
    }
}

// Note: ensurePlayer*Connected/Disconnected methods removed - 
// Connection management is now handled during voice allocation/release

//--------------------------------------------------------------
// Helper functions for position mapping and loop calculations
//--------------------------------------------------------------
float MultiSampler::mapRelativeToAbsolute(float relativePos, float regionStart, float regionEnd) const {
    float regionSize = regionEnd - regionStart;
    if (regionSize > MIN_REGION_SIZE) {
        return regionStart + relativePos * regionSize;
    } else {
        // Invalid or collapsed region - clamp relative position to valid range
        return std::max(0.0f, std::min(1.0f, relativePos));
    }
}

float MultiSampler::mapAbsoluteToRelative(float absolutePos, float regionStart, float regionEnd) const {
    float regionSize = regionEnd - regionStart;
    if (regionSize > MIN_REGION_SIZE) {
        // Clamp absolute position to region bounds first
        float clampedAbsolute = std::max(regionStart, std::min(regionEnd, absolutePos));
        return (clampedAbsolute - regionStart) / regionSize;
    } else {
        // Invalid or collapsed region - return absolute position clamped to 0-1
        return std::max(0.0f, std::min(1.0f, absolutePos));
    }
}

MultiSampler::LoopBounds MultiSampler::calculateLoopBounds(MediaPlayer* player, PlayStyle playStyle) const {
    if (!player) {
        return {0.0f, 1.0f};
    }
    
    float regionStart = player->regionStart.get();
    float regionEnd = player->regionEnd.get();
    
    // Ensure region bounds are valid
    if (regionStart > regionEnd) {
        std::swap(regionStart, regionEnd);
    }
    
    // Calculate loop start from relative startPosition
    float relativeStart = player->startPosition.get();
    float loopStart = mapRelativeToAbsolute(relativeStart, regionStart, regionEnd);
    float loopEnd = regionEnd;
    
    // Calculate loop end based on grainSize when in GRAIN play style
    if (playStyle == PlayStyle::GRAIN) {
        float loopSizeSeconds = player->loopSize.get();
        if (loopSizeSeconds > MIN_LOOP_SIZE) {
            float duration = player->getDuration();
            if (duration > MIN_DURATION) {
                // CRITICAL FIX: Work in absolute time (seconds) first to preserve precision
                // Converting small time values to normalized positions loses precision for long samples
                // Convert normalized positions to absolute time
                float loopStartSeconds = loopStart * duration;
                float regionEndSeconds = regionEnd * duration;
                
                // Calculate loop end in absolute time
                float calculatedLoopEndSeconds = loopStartSeconds + loopSizeSeconds;
                
                // Clamp to region end and media duration
                float clampedLoopEndSeconds = std::min(regionEndSeconds, std::min(duration, calculatedLoopEndSeconds));
                
                // Convert back to normalized position (0-1)
                loopEnd = clampedLoopEndSeconds / duration;
            }
        }
    }
    
    return {loopStart, loopEnd};
}

void MultiSampler::seekPlayerToPosition(MediaPlayer* player, float position) const {
    if (!player) return;
    
    // Use MediaPlayer::setPosition() which handles both audio and video internally
    player->setPosition(position);
}

void MultiSampler::handleRegionEnd(MediaPlayer* player, float currentPosition, 
                                 float effectiveRegionEnd, float loopStartPos, 
                                 PlayStyle playStyle) {
    if (!player) return;
    
    // CRITICAL: This function only handles STOPPING at region end, not looping
    // LOOP and NEXT modes handle looping manually in update() by checking loopEnd and seeking to loopStart
    // This function should never be called for LOOP or NEXT modes - if it is, it's a bug
    switch (playStyle) {
        case PlayStyle::ONCE:
            // ONCE mode: Stop playback cleanly to prevent audio pop
            // Mute audio before stopping to avoid click/pop from position jump
            if (player->isAudioLoaded()) {
                player->audioPlayer.stop();  // Stop audio directly (no position seek)
            }
            if (player->isVideoLoaded()) {
                player->videoPlayer.stop();  // Stop video directly
            }
            // Disable outputs without calling player->stop() which would reset position
            player->audioEnabled.set(false);
            player->videoEnabled.set(false);
            break;
        case PlayStyle::LOOP:
        case PlayStyle::GRAIN:
            // LOOP/GRAIN modes should never reach here - looping is handled in update()
            // If we do reach here, it's a bug - log warning and loop back as fallback
            ofLogWarning("MultiSampler") << "handleRegionEnd() called for LOOP/GRAIN mode - this should not happen. Looping back to loopStart.";
            seekPlayerToPosition(player, loopStartPos);
            break;
        case PlayStyle::NEXT:
            // NEXT mode: Loop behavior (handled in update() like LOOP mode)
            // This should never be reached - NEXT mode loops in update(), not stops
            ofLogWarning("MultiSampler") << "handleRegionEnd() called for NEXT mode - this should not happen. NEXT mode loops in update().";
            break;
    }
}

// Position scan functions removed - position memory now handled directly via MediaPlayer::playheadPosition


float MultiSampler::clampPositionForPlayback(float position, PlayStyle playStyle) const {
    // First clamp to valid range (0.0-1.0)
    float clampedPosition = std::max(0.0f, std::min(1.0f, position));
    
    // CRITICAL: In ONCE mode with position memory, if position is at the end (>= END_POSITION_THRESHOLD),
    // clamp to just before the end so playback can continue instead of immediately stopping.
    // This preserves position memory behavior while allowing playback to work.
    if (playStyle == PlayStyle::ONCE && clampedPosition >= END_POSITION_THRESHOLD) {
        clampedPosition = END_POSITION_THRESHOLD;
    }
    
    return clampedPosition;
}

//--------------------------------------------------------------
void MultiSampler::resetPlayerToDefaults(MediaPlayer* player) {
    if (!player) return;
    
    // Reset all playback parameters to their default values
    // This prevents parameter inheritance from previous triggers
    
    // Core playback parameters - reset to safe defaults
    player->speed.set(1.0f);           // Default speed (normal)
    player->volume.set(1.0f);          // Default volume (full)
    player->startPosition.set(0.0f);   // Default start position (beginning)
    player->loop.set(false);           // Default loop off
    
    // Enable audio/video by default
    if (player->isAudioLoaded()) {
        player->audioEnabled.set(true);
    }
    if (player->isVideoLoaded()) {
        player->videoEnabled.set(true);
    }
    
    // Note: grainSize, regionStart, regionEnd are sample-level parameters
    // and should not be reset per-trigger (they're part of the sample's configuration)
}

void MultiSampler::applyEventParameters(MediaPlayer* player, const TriggerEvent& event, 
                                     const std::vector<ParameterDescriptor>& descriptors) {
    if (!player) return;
    
    // Special handling for "position" parameter (maps to startPosition, needs region clamping)
    float position = 0.0f;
    bool positionInEvent = false;
    auto positionIt = event.parameters.find("position");
    if (positionIt != event.parameters.end()) {
        position = positionIt->second;
        positionInEvent = true;
    } else {
        // Position not in event - use current player value (position memory)
        position = player->startPosition.get();
    }
    
    // Clamp position using helper method (handles ONCE mode end position clamping)
    float clampedPosition = clampPositionForPlayback(position, currentPlayStyle);
    
    // Update playheadPosition if we clamped for ONCE mode
    if (currentPlayStyle == PlayStyle::ONCE && position >= END_POSITION_THRESHOLD && 
        clampedPosition == END_POSITION_THRESHOLD) {
        player->playheadPosition.set(END_POSITION_THRESHOLD);
        ofLogVerbose("MultiSampler") << "[ONCE_MODE] Clamped end position to allow playback continuation";
    }
    
    // Set position if it was in event or if it changed
    if (positionInEvent && std::abs(player->startPosition.get() - clampedPosition) > PARAMETER_EPSILON) {
        player->startPosition.set(clampedPosition);
    }
    
    // Audio/video always enabled for sequencer triggers
    if (!player->audioEnabled.get()) {
        player->audioEnabled.set(true);
    }
    if (!player->videoEnabled.get()) {
        player->videoEnabled.set(true);
    }
    
    // Process all other parameters from event dynamically
    // Skip "note" (handled separately) and "position" (handled above)
    for (const auto& paramPair : event.parameters) {
        const std::string& paramName = paramPair.first;
        float paramValue = paramPair.second;
        
        // Skip special parameters
        if (paramName == "note" || paramName == "position") {
            continue;
        }
        
        // Get parameter descriptor for validation
        auto descIt = std::find_if(descriptors.begin(), descriptors.end(),
            [&](const ParameterDescriptor& desc) { return desc.name == paramName; });
        
        // Clamp value to parameter range if descriptor found
        float clampedValue = paramValue;
        if (descIt != descriptors.end()) {
            clampedValue = std::max(descIt->minValue, std::min(descIt->maxValue, paramValue));
        }
        
        // Use MediaPlayer's getFloatParameter to check if parameter exists
        auto* param = player->getFloatParameter(paramName);
        if (param) {
            // Parameter exists on player - set it
            float currentValue = param->get();
            if (std::abs(currentValue - clampedValue) > PARAMETER_EPSILON) {
                param->set(clampedValue);
            }
        } else {
            // Parameter doesn't exist on player - log warning but continue
            ofLogVerbose("MultiSampler") << "Parameter '" << paramName << "' not found on MediaPlayer, skipping";
        }
    }
    
    // CRITICAL: Don't enable underlying player loop for LOOP mode
    // The underlying players loop at full media level (0.0-1.0), but we need region-level looping
    // (loopStart to loopEnd based on loopSize). We handle looping manually in update().
    // Always disable underlying loop - looping is handled manually at region level
    player->loop.set(false);
}


//--------------------------------------------------------------
// Module Factory Registration
//--------------------------------------------------------------
// Auto-register MultiSampler with ModuleFactory on static initialization
// This enables true modularity - no hardcoded dependencies in ModuleFactory
namespace {
    struct MultiSamplerRegistrar {
        MultiSamplerRegistrar() {
            // Register with new name
            ModuleFactory::registerModuleType("MultiSampler", 
                []() -> std::shared_ptr<Module> {
                    return std::make_shared<MultiSampler>();
                });
            // Also register with legacy name for backward compatibility
            ModuleFactory::registerModuleType("MediaPool", 
                []() -> std::shared_ptr<Module> {
                    return std::make_shared<MultiSampler>();
                });
        }
    };
    static MultiSamplerRegistrar g_multiSamplerRegistrar;
}


