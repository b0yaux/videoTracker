#include "MediaPlayer.h"
#include "ofJson.h"
#include "ofxSoundFile.h"
#include <fstream>
#include <chrono>

MediaPlayer::MediaPlayer() : isSetup(false), lastPosition(0.0f), lastSpeed(1.0f), lastLoop(false) {
    setup();
}

MediaPlayer::~MediaPlayer() {
    // Cleanup handled by member objects
}

void MediaPlayer::setup() {
    if (isSetup) return;
    
    // Setup audio player
    audioPlayer.setName("Audio Player");
    
    // Setup video player
    videoPlayer.setName("Video Player");
    
    // Setup synchronized parameters
    playheadPosition.set("Playhead position", 0.0f, 0.0f, 1.0f);  // Current playhead position (updates during playback)
    startPosition.set("Start position", 0.0f, 0.0f, 1.0f);  // Start position for playback (synced with tracker)
    speed.set("Speed", 1.0f, -10.0f, 10.0f);  // Support negative speeds for backward playback
    loop.set("Loop", true);
    regionStart.set("Region start", 0.0f, 0.0f, 1.0f);  // Playback region start - defines minimum playable position
    regionEnd.set("Region end", 1.0f, 0.0f, 1.0f);      // Playback region end - defines maximum playable position
    
    // Setup enable/disable toggles
    audioEnabled.set("Audio Enabled", true);
    videoEnabled.set("Video Enabled", true);
    
    // Setup audio-specific parameters
    volume.set("Volume", 1.0f, 0.0f, 2.0f);
    
    // Setup granular-style loop control
    loopSize.set("Loop size", 1.0f, 0.0f, 10.0f);  // Default 1 second, max 10 seconds (will be clamped to actual duration)
    
    // Add all parameters to the parameter group
    parameters.add(playheadPosition);
    parameters.add(startPosition);
    parameters.add(speed);
    parameters.add(loop);
    parameters.add(regionStart);
    parameters.add(regionEnd);
    parameters.add(audioEnabled);
    parameters.add(videoEnabled);
    parameters.add(volume);
    parameters.add(loopSize);
    
    // Setup parameter listeners
    audioEnabled.addListener(this, &MediaPlayer::onAudioEnabledChanged);
    videoEnabled.addListener(this, &MediaPlayer::onVideoEnabledChanged);
    playheadPosition.addListener(this, &MediaPlayer::onPlayheadPositionChanged);
    speed.addListener(this, &MediaPlayer::onSpeedChanged);
    loop.addListener(this, &MediaPlayer::onLoopChanged);
    volume.addListener(this, &MediaPlayer::onVolumeChanged);
    
    // Parameters are managed directly by MediaPlayer
    // No need to forward to underlying players since they don't have media parameters
    
    isSetup = true;
}

const ofParameter<float>* MediaPlayer::getFloatParameter(const std::string& name) const {
    // Support both "position" (for backward compat/sequencer) and "playheadPosition" (new name)
    if (name == "position" || name == "playheadPosition") return &playheadPosition;
    if (name == "startPosition") return &startPosition;
    if (name == "speed") return &speed;
    if (name == "volume") return &volume;
    if (name == "loopSize") return &loopSize;
    // Support both old names (for backward compat) and new names
    if (name == "loopStart" || name == "regionStart") return &regionStart;
    if (name == "loopEnd" || name == "regionEnd") return &regionEnd;
    return nullptr;
}

ofParameter<float>* MediaPlayer::getFloatParameter(const std::string& name) {
    // Use const_cast to avoid code duplication - safe since we're just removing const
    return const_cast<ofParameter<float>*>(static_cast<const MediaPlayer*>(this)->getFloatParameter(name));
}

bool MediaPlayer::load(const std::string& audioPath, const std::string& videoPath) {

    // CRITICAL: Stop any existing playback and unload before loading new media
    // This prevents crashes when reusing a player for different media
    stop();
    unload();

    bool audioLoaded = false;
    bool videoLoaded = false;

    // Load audio if path provided
    if (!audioPath.empty()) {
        audioLoaded = loadAudio(audioPath);
    }

    // Load video if path provided
    if (!videoPath.empty()) {

        videoLoaded = loadVideo(videoPath);
    }
    
    // CRITICAL: After loading both, ensure HAP audio is stopped if we have separate audio
    // This handles the case where video is loaded first, then audio is loaded
    // (loadVideo() already handles the reverse case)
    if (audioLoaded && videoLoaded && videoPlayer.getVideoFile().isUsingHap()) {
        videoPlayer.getVideoFile().stopHapAudio();
        ofLogNotice("MediaPlayer") << "Stopped HAP embedded audio after loading both audio and video";
    }
    
    return audioLoaded || videoLoaded;
}

bool MediaPlayer::loadAudio(const std::string& audioPath) {
    if (audioPath.empty()) return false;
    
    try {
        ofLogNotice("MediaPlayer") << "Loading audio: " << audioPath;
        bool success = audioPlayer.load(audioPath);
        
        if (success) {
            audioFilePath = audioPath;
            ofLogNotice("MediaPlayer") << "Audio loaded successfully: " << audioPath;
        } else {
            ofLogError("MediaPlayer") << "Failed to load audio: " << audioPath;
        }
        
        return success;
    } catch (const std::exception& e) {
        ofLogError("MediaPlayer") << "Exception loading audio: " << audioPath << " - " << e.what();
        return false;
    } catch (...) {
        ofLogError("MediaPlayer") << "Unknown exception loading audio: " << audioPath;
        return false;
    }
}

bool MediaPlayer::loadAudioFromShared(std::shared_ptr<ofxSoundFile> sharedFile) {
    if (!sharedFile || !sharedFile->isLoaded()) {
        ofLogWarning("MediaPlayer") << "Cannot load from invalid or unloaded shared audio file";
        return false;
    }
    
    try {
        ofLogVerbose("MediaPlayer") << "Loading audio from shared buffer";
        // ofxSoundPlayerObject is a typedef for ofxMultiSoundPlayer
        // We need to access the internal ofxSingleSoundPlayer instance to load from shared file
        // getPlayInstance(0) requires that an instance exists, so we need to ensure one is created
        // The simplest way is to check if we have any play instances, and if not, create one
        // by loading an empty buffer (which will be immediately replaced)
        if (audioPlayer.getNumPlayInstances() == 0) {
            // Create first instance by loading an empty buffer
            // This ensures getPlayInstance(0) will work
            // ofxMultiSoundPlayer::load() with buffer calls setNumInstances(1) internally
            ofSoundBuffer emptyBuffer;
            emptyBuffer.allocate(1, 1);  // 1 sample, 1 channel (sample rate is set separately if needed)
            if (!audioPlayer.load(emptyBuffer, "temp")) {
                ofLogError("MediaPlayer") << "Failed to create audio instance for shared file loading";
                return false;
            }
        }
        
        // Access the first play instance (ofxSingleSoundPlayer) which supports shared file loading
        ofxSingleSoundPlayer& instance = audioPlayer.getPlayInstance(0);
        
        // Load from shared file using ofxSingleSoundPlayer's load method
        // Note: load() takes a reference to shared_ptr, not the shared_ptr itself
        std::shared_ptr<ofxSoundFile> sharedFileRef = sharedFile;
        bool success = instance.load(sharedFileRef);
        
        if (success) {
            // Clear file path since we loaded from shared buffer
            audioFilePath.clear();
            ofLogVerbose("MediaPlayer") << "Audio loaded successfully from shared buffer";
            // Verify that isAudioLoaded() returns true after loading
            bool isLoaded = isAudioLoaded();
            ofLogVerbose("MediaPlayer") << "isAudioLoaded() after load: " << (isLoaded ? "true" : "false");
            if (!isLoaded) {
                ofLogWarning("MediaPlayer") << "WARNING: loadAudioFromShared succeeded but isAudioLoaded() returns false";
            }
        } else {
            ofLogError("MediaPlayer") << "Failed to load audio from shared buffer";
        }
        
        return success;
    } catch (const std::exception& e) {
        ofLogError("MediaPlayer") << "Exception loading audio from shared buffer: " << e.what();
        return false;
    } catch (...) {
        ofLogError("MediaPlayer") << "Unknown exception loading audio from shared buffer";
        return false;
    }
}

bool MediaPlayer::loadVideo(const std::string& videoPath) {
    if (videoPath.empty()) return false;

    try {
        ofLogNotice("MediaPlayer") << "Starting video load process for: " << videoPath;

        // CRITICAL: Close any existing video before loading new one
        // This prevents device conflicts and ensures clean state
        if (videoPlayer.isLoaded()) {
            ofLogNotice("MediaPlayer") << "Closing existing video before loading new one";
            videoPlayer.stop();
            videoPlayer.getVideoFile().close();
        }

        // CRITICAL: Disable HAP audio BEFORE loading if we have separate audio
        // This prevents device probing during load and audio stream creation
        if (isAudioLoaded()) {
            ofLogNotice("MediaPlayer") << "Audio already loaded, disabling HAP audio before video load";
            videoPlayer.getVideoFile().disableHapAudio();
            ofLogNotice("MediaPlayer") << "Disabled HAP embedded audio before load (using separate audio file)";
        }

        ofLogNotice("MediaPlayer") << "Loading video: " << videoPath;

        // CRITICAL: Ensure no audio interference during video loading
        // Force disable all audio-related components before video load
        ofLogNotice("MediaPlayer") << "Force disabling HAP audio before videoPlayer.load()";
        videoPlayer.getVideoFile().disableHapAudio();

        ofLogNotice("MediaPlayer") << "About to call videoPlayer.load() for: " << videoPath;
        bool success = videoPlayer.load(videoPath);
        ofLogNotice("MediaPlayer") << "videoPlayer.load() returned: " << (success ? "SUCCESS" : "FAILED");
        
        if (success) {
            videoFilePath = videoPath;
            ofLogNotice("MediaPlayer") << "Video loaded successfully: " << videoPath;
            
            // CRITICAL: Double-check HAP audio is disabled after load
            // Some video loaders may re-enable audio during initialization,
            // which can interfere with the audio stream and cause sample rate issues
            if (isAudioLoaded() && videoPlayer.getVideoFile().isUsingHap()) {
                videoPlayer.getVideoFile().disableHapAudio();
                videoPlayer.getVideoFile().stopHapAudio();
                ofLogNotice("MediaPlayer") << "Re-disabled HAP audio after video load to prevent audio interference";
            }
        } else {
            ofLogError("MediaPlayer") << "Failed to load video: " << videoPath;
        }
        
        return success;
    } catch (const std::exception& e) {
        ofLogError("MediaPlayer") << "Exception loading video: " << videoPath << " - " << e.what();
        return false;
    } catch (...) {
        ofLogError("MediaPlayer") << "Unknown exception loading video: " << videoPath;
        return false;
    }
}

void MediaPlayer::play() {
    // Get the start position (startPosition is relative: 0.0-1.0 within region)
    // Position memory is now handled at MultiSampler level when retriggering same media
    // 0.0 is a valid position (start of region), not a sentinel for position memory
    float relativeStartPos = startPosition.get(); // Relative position (0.0-1.0 within region)
    float currentSpeed = speed.get();
    bool currentLoop = loop.get();
    
    // Map relative startPosition to absolute position within region
    float regionStartVal = regionStart.get();
    float regionEndVal = regionEnd.get();
    float regionSize = regionEndVal - regionStartVal;
    float targetPosition = 0.0f;
    
    if (regionSize > MIN_REGION_SIZE) {
        // Map relative position (0.0-1.0) to absolute position (regionStart-regionEnd)
        targetPosition = regionStartVal + relativeStartPos * regionSize;
    } else {
        // If region is invalid or collapsed, use relative position directly (clamped to valid range)
        targetPosition = std::max(0.0f, std::min(1.0f, relativeStartPos));
    }
    
    // Ensure loop and speed state are set on underlying players before playing
    // This ensures backward looping works correctly via the addons' internal handling
    if (isAudioLoaded()) {
        audioPlayer.setLoop(currentLoop);
        audioPlayer.setSpeed(currentSpeed);
    }
    
    if (isVideoLoaded()) {
        videoPlayer.getVideoFile().setLoopState(currentLoop ? OF_LOOP_NORMAL : OF_LOOP_NONE);
        videoPlayer.getVideoFile().setSpeed(currentSpeed);
    }
    
    // Sync audio position before starting playback
    // CRITICAL: Ensure audioEnabled is true and audio is loaded before attempting playback
    bool audioIsLoaded = isAudioLoaded();
    bool audioIsEnabled = audioEnabled.get();
    
    ofLogVerbose("MediaPlayer") << "play() - audioIsLoaded: " << (audioIsLoaded ? "true" : "false") 
                                 << ", audioIsEnabled: " << (audioIsEnabled ? "true" : "false");
    
    if (audioIsLoaded) {
        // Enable audio if not already enabled
        if (!audioIsEnabled) {
            ofLogVerbose("MediaPlayer") << "Enabling audio (was disabled)";
            audioEnabled.set(true);
        }
        
        // CRITICAL FIX: Force stop before play to ensure retrigger works
        // ofxSoundPlayerObject::play() may be a no-op if already playing
        // This guarantees playback restarts from the target position
        if (audioPlayer.isPlaying()) {
            ofLogVerbose("MediaPlayer") << "Stopping already playing audio before restart";
            audioPlayer.stop();
        }
        
        audioPlayer.setPosition(targetPosition);
        
        // Always call play() - it will handle paused state internally
        ofLogVerbose("MediaPlayer") << "Calling audioPlayer.play() at position: " << targetPosition;
        int playResult = audioPlayer.play();
        ofLogVerbose("MediaPlayer") << "audioPlayer.play() returned: " << playResult;
        
        // If the position was reset by play(), set it again
        if (audioPlayer.getPosition() < targetPosition - POSITION_SEEK_THRESHOLD) {
            ofLogVerbose("MediaPlayer") << "Position was reset, setting again to: " << targetPosition;
            audioPlayer.setPosition(targetPosition);
        }
    } else {
        ofLogWarning("MediaPlayer") << "Cannot play audio: audio not loaded (isAudioLoaded() returned false)";
    }
    
    // Sync video position before starting playback
    // FIXED: Check isVideoLoaded() first, then enable video if needed
    // The old condition (videoEnabled.get() && isVideoLoaded()) prevented video from playing
    // if videoEnabled was false, creating a chicken-and-egg problem
    if (isVideoLoaded()) {
        // Enable video if not already enabled
        if (!videoEnabled.get()) {
            videoEnabled.set(true);
        }

        // CRITICAL FIX: Force stop before play to ensure retrigger works (same as audio)
        // When a voice is reused, video might still be playing from previous trigger
        // Stopping ensures video restarts from the target position, maintaining sync with audio
        bool wasVideoPlaying = videoPlayer.isPlaying();
        if (wasVideoPlaying) {
            ofLogVerbose("MediaPlayer") << "Stopping already playing video before restart";
            videoPlayer.stop();
        }

        // CRITICAL: Ensure HAP embedded audio stays stopped if we have separate audio
        // The HAP player's AudioThread tries to start audio automatically when playing,
        // so we need to stop it again right before playing
        if (isAudioLoaded() && videoPlayer.getVideoFile().isUsingHap()) {
            videoPlayer.getVideoFile().stopHapAudio();
        }
        
        // CRITICAL FIX: Always reset video position when restarting playback
        // When a voice is reused, video position may be stale from previous playback
        // We must ALWAYS sync to targetPosition on play(), regardless of threshold
        // The threshold optimization only applies during active playback, not on restart
        float currentVideoPos = videoPlayer.getVideoFile().getPosition();
        bool positionNeedsUpdate = std::abs(currentVideoPos - targetPosition) > POSITION_SEEK_THRESHOLD;
        
        // CRITICAL: Always update position when starting playback (!wasVideoPlaying) or when diff exceeds threshold
        // When starting fresh, video position may be stale from previous playback (lazy loading reuses video)
        // We must ALWAYS sync to targetPosition on play(), regardless of threshold
        // The threshold optimization only applies during active playback, not on restart
        if (!wasVideoPlaying || positionNeedsUpdate) {
            videoPlayer.getVideoFile().setPosition(targetPosition);
            // PERFORMANCE CRITICAL: Only call update() after position change - it's needed for HAP seeking
            // Removed forceTextureUpdate() - it runs 5 update() calls in a loop (800ms+)
            // The normal update loop in ofApp will handle texture updates during playback
            videoPlayer.getVideoFile().update();
        }
        
        // Always call play() - it will handle paused state internally
        // Note: ofxVideoFile::play() does NOT reset position, so we don't need position correction
        videoPlayer.play();
        
        // CRITICAL: Enable video output for frame gating (allows frames to be displayed)
        // This syncs with videoEnabled parameter for proper frame gating
        videoPlayer.enabled.set(true);
        
        // CRITICAL: Stop HAP audio again after play() in case AudioThread started it
        // The AudioThread checks playing state and automatically starts audio, so we need to stop it
        if (isAudioLoaded() && videoPlayer.getVideoFile().isUsingHap()) {
            videoPlayer.getVideoFile().stopHapAudio();
        }
        
        // REMOVED: Position correction check - ofxVideoFile::play() doesn't reset position
        // This was causing a second expensive setPosition() call (another 200ms+)
        // The position is already set correctly before play(), so no correction needed
        
        // Update playheadPosition parameter for UI display (after actual position is set)
        // This ensures the UI shows the correct position without triggering expensive setPosition()
        // via the listener (since we already set it above)
        if (std::abs(playheadPosition.get() - targetPosition) > POSITION_VALID_THRESHOLD) {
            // Temporarily disable listener to avoid triggering expensive setPosition() again
            // We can't easily disable the listener, so we'll just update it and let the update() loop
            // sync it during playback. The position is already set correctly above.
            playheadPosition.set(targetPosition);
        }
    }
}

float MediaPlayer::captureCurrentPosition() const {
    // CRITICAL: If player is playing, ALWAYS read from players (most accurate source)
    // The playheadPosition parameter might not be up-to-date if update() hasn't run recently
    if (isPlaying()) {
        // Player is still playing - read position directly from players (most accurate)
        // Read from audio first (usually more accurate for timing)
        if (isAudioLoaded() && audioPlayer.isPlaying()) {
            float audioPos = audioPlayer.getPosition();
            if (audioPos > POSITION_VALID_THRESHOLD) {
                return audioPos;
            }
        }
        
        // If audio position wasn't valid, try video
        if (isVideoLoaded() && videoPlayer.isPlaying()) {
            // Need non-const reference for getVideoFile() - safe since we're only reading
            auto& nonConstVideo = const_cast<ofxVideoPlayerObject&>(videoPlayer);
            float videoPos = nonConstVideo.getVideoFile().getPosition();
            if (videoPos > POSITION_VALID_THRESHOLD) {
                return videoPos;
            }
        }
        
        // If still no valid position from players, fall back to playheadPosition parameter
        float paramPos = playheadPosition.get();
        if (paramPos > POSITION_VALID_THRESHOLD) {
            return paramPos;
        }
    } else {
        // Not playing - use playheadPosition parameter (should have correct value from last update())
        // BUT: If playheadPosition parameter is valid, use it. Otherwise, try to read from players
        // (they might still have the position even if not playing)
        float paramPos = playheadPosition.get();
        if (paramPos > POSITION_VALID_THRESHOLD) {
            return paramPos;
        }
        
        // PlayheadPosition parameter is 0 or invalid - try reading from players as last resort
        if (isAudioLoaded()) {
            float audioPos = audioPlayer.getPosition();
            if (audioPos > POSITION_VALID_THRESHOLD) {
                return audioPos;
            }
        }
        if (isVideoLoaded()) {
            // Need non-const reference for getVideoFile() - safe since we're only reading
            auto& nonConstVideo = const_cast<ofxVideoPlayerObject&>(videoPlayer);
            float videoPos = nonConstVideo.getVideoFile().getPosition();
            if (videoPos > POSITION_VALID_THRESHOLD) {
                return videoPos;
            }
        }
    }
    
    // No valid position found - return 0.0f
    return 0.0f;
}

void MediaPlayer::stop() {
    // CRITICAL: Capture position BEFORE stopping to freeze playheadPosition
    // This ensures GUI shows correct position when playback stops (gate duration, etc.)
    float finalPosition = captureCurrentPosition();
    
    // Stop the underlying players
    audioPlayer.stop();
    videoPlayer.stop();
    
    // CRITICAL: Force stop and clear state on underlying players
    // Some players may not fully stop, so we need to be explicit
    if (isAudioLoaded()) {
        audioPlayer.setPaused(false);  // Ensure not paused
        audioPlayer.stop();             // Stop again to be sure
    }
    
    if (isVideoLoaded()) {
        videoPlayer.setPaused(false);   // Ensure not paused
        videoPlayer.stop();             // Stop again to be sure
        // PERFORMANCE: Only update video if it was actually playing
        // This avoids expensive update() calls when stopping already-stopped videos
        // (e.g., when NEXT mode switches media and stops the next player before it starts)
        // Video output is disabled below anyway, so stale frames won't be visible
        if (videoPlayer.isPlaying()) {
            // Force texture update to clear any stale frames (only if was playing)
            videoPlayer.getVideoFile().update();
        }
    }
    
    // CRITICAL: Freeze playheadPosition at captured position
    // This ensures position doesn't continue advancing after stop() is called
    // MultiSampler can read this frozen position for GUI sync
    if (finalPosition > POSITION_VALID_THRESHOLD) {
        playheadPosition.set(finalPosition);
    } else {
        // If position is invalid, use current playheadPosition value (don't reset to 0)
        // This preserves position memory for NEXT mode
    }
    
    // CRITICAL: Disable video output to gate out frames when stopped
    // This ensures stopped videos don't show stale frames in the mixer
    if (videoEnabled.get()) {
        videoEnabled.set(false);
    }
    
    // Sync enabled state to gate out video output (frame gating)
    if (isVideoLoaded()) {
        videoPlayer.enabled.set(false);  // Gate out stopped video
    }
    
    if (audioEnabled.get()) {
        audioEnabled.set(false);
    }
}

void MediaPlayer::unload() {
    // Unload audio
    if (isAudioLoaded()) {
        audioPlayer.unload();
        audioFilePath.clear();
    }
    
    // Unload video
    if (isVideoLoaded()) {
        videoPlayer.getVideoFile().close();
        videoFilePath.clear();
    }
    
    // Reset parameters to defaults
    playheadPosition.set(0.0f);
    startPosition.set(0.0f);
}

void MediaPlayer::pause() {
    audioPlayer.setPaused(true);
    videoPlayer.setPaused(true);
}

void MediaPlayer::resume() {
    audioPlayer.setPaused(false);
    videoPlayer.setPaused(false);
}

void MediaPlayer::reset() {
    // Stop all playback
    audioPlayer.stop();
    videoPlayer.stop();
    
    // Reset playheadPosition to beginning
    playheadPosition.set(0.0f);
    
    // Re-enable audio/video if they were loaded
    if (isAudioLoaded()) {
        audioEnabled.set(true);
    }
    if (isVideoLoaded()) {
        videoEnabled.set(true);
        // Re-enable video output for frame gating
        videoPlayer.enabled.set(true);
    }
    
    ofLogNotice("MediaPlayer") << "Player reset - ready for fresh playback";
}

void MediaPlayer::setPosition(float pos) {
    playheadPosition.set(pos);
    if (isAudioLoaded()) {audioPlayer.setPosition(pos);}
    if (isVideoLoaded()) {videoPlayer.getVideoFile().setPosition(pos);}
}

bool MediaPlayer::isAudioLoaded() const {
    return audioPlayer.isLoaded();
}

bool MediaPlayer::isVideoLoaded() const {
    return videoPlayer.isLoaded();
}

bool MediaPlayer::isPlaying() const {
    return audioPlayer.isPlaying() || videoPlayer.isPlaying();
}

float MediaPlayer::getDuration() const {
    float audioDuration = 0.0f;
    if (isAudioLoaded()) {
        auto& nonConstAudio = const_cast<ofxSoundPlayerObject&>(audioPlayer);
        if (nonConstAudio.isLoaded()) {
            // getDurationMS() returns milliseconds, convert to seconds once
            audioDuration = nonConstAudio.getDurationMS() * MS_TO_SECONDS;
        }
    }
    
    float videoDuration = 0.0f;
    if (isVideoLoaded()) {
        auto& nonConstVideo = const_cast<ofxVideoPlayerObject&>(videoPlayer);
        if (nonConstVideo.isLoaded()) {
            // getDuration() returns milliseconds (uint64_t), convert to seconds once
            videoDuration = nonConstVideo.getVideoFile().getDuration() * MS_TO_SECONDS;
        }
    }
    
    return std::max(audioDuration, videoDuration);
}

void MediaPlayer::update() {
    // CRITICAL: Keep HAP embedded audio disabled and stopped if we have separate audio
    // The HAP player's AudioThread runs in a separate thread and may try to restart audio
    // during playback. We disable and stop it periodically (every 60 frames ≈ 1 second at 60fps)
    // to prevent it from creating a conflicting audio stream that interferes with the audio clock
    if (isAudioLoaded() && isVideoLoaded() && videoPlayer.getVideoFile().isUsingHap()) {
        static int frameCounter = 0;
        if (++frameCounter % 60 == 0) {
            // Disable prevents audio from starting, stop ensures it's not playing
            videoPlayer.getVideoFile().disableHapAudio();
            videoPlayer.getVideoFile().stopHapAudio();
        }
    }
    
    // Update video player when video is loaded (needed for texture updates)
    // Process visual chain whenever video is loaded (not just when playing) so HSV adjustments
    // are visible even when paused or when adjusting sliders
    if (isVideoLoaded()) {
        // CRITICAL: Frame gating logic - must check videoEnabled FIRST
        // Check if video is actually playing (not just stopped at end)
        bool actuallyPlaying = videoPlayer.isPlaying();
        
        // CRITICAL FIX: Detect when video naturally reached end
        // When video ends naturally, isPlaying() becomes false but position might be at end (1.0)
        // We should gate out in this case to prevent expensive update() calls on stopped players
        float videoPos = videoPlayer.getVideoFile().getPosition();
        bool atEnd = videoPos >= 0.99f; // Near end of video (account for floating point precision)
        
        bool shouldBeEnabled = false;
        if (videoEnabled.get()) {
            // Only enable if actually playing AND not at end (unless looping)
            // This prevents expensive processing when video naturally ends before sequencer trigger ends
            if (actuallyPlaying && (!atEnd || loop.get())) {
                shouldBeEnabled = true;
            }
            // Allow scrubbing when paused (not stopped) - position > threshold but not playing
            else if (!actuallyPlaying && videoPos > POSITION_VALID_THRESHOLD && videoPos < 0.99f) {
                shouldBeEnabled = true; // Paused for scrubbing
            }
            // else: shouldBeEnabled stays false (video stopped at end or not playing)
        }
        
        if (videoPlayer.enabled.get() != shouldBeEnabled) {
            videoPlayer.enabled.set(shouldBeEnabled);
        }
        
        // CRITICAL: Never update video when audio is playing to prevent timing interference
        // that causes audio playback to speed up drastically ("sample rate changed")
        bool shouldUpdateVideo = videoEnabled.get() && actuallyPlaying && (!atEnd || loop.get());
        if (isAudioLoaded() && audioEnabled.get()) {
            // When audio is active, COMPLETELY disable video updates to prevent timing conflicts
            shouldUpdateVideo = false;
        }

        if (shouldUpdateVideo) {
            videoPlayer.update();
        }
        
        // Process video player only if enabled
        if (videoEnabled.get() && shouldBeEnabled) {
            ofFbo emptyInput;
            videoPlayer.process(emptyInput, videoPlayer.getOutputBuffer());
        }
    }
    
    // Sync position parameter with actual playback position
    // CRITICAL: Read position whenever audio/video is loaded, not just when isPlaying() is true
    // When wrapped by VoiceProcessor, audioPlayer.isPlaying() may be false even during playback,
    // but getPosition() still returns valid position values
    // When stopped, playheadPosition is frozen at the last position (set in stop())
    // This ensures GUI shows correct position when playback stops (gate duration, etc.)
    
    // Cache parameter reads to avoid redundant calls
    float speedVal = speed.get();
    bool loopVal = loop.get();
    float lastPosition = playheadPosition.get();  // Cache for backward loop detection
    
    // Get position from audio player if available (regardless of isPlaying() state)
    // CRITICAL: VoiceProcessor wraps audioPlayer, so isPlaying() may be false even during playback
    // But getPosition() still returns valid position values
    float currentPosition = 0.0f;
    bool hasValidPosition = false;
    
    if (isAudioLoaded()) {
        // Always try to read position from audio player if loaded
        // Position is valid even if isPlaying() is false (e.g., during envelope fade-out)
        currentPosition = audioPlayer.getPosition();
        
        // Only use audio position if it's valid (> threshold or at start)
        // Position of 0.0 is valid at start of playback, but we need to distinguish
        // between "at start" (0.0) and "not playing/invalid" (also 0.0)
        // Use isPlaying() as a hint, but don't require it
        if (currentPosition > POSITION_VALID_THRESHOLD || audioPlayer.isPlaying()) {
            hasValidPosition = true;
            
            // Workaround for addon bug: ofxSingleSoundPlayer uses unsigned size_t for position,
            // which causes unsigned underflow when playing backward with negative speed.
            // When position wraps from 0 backwards, it becomes a huge unsigned number, which
            // after modulo can result in incorrect position values, causing audio glitches.
            // Fix: Detect and correct backward looping wrap issues
            if (loopVal && speedVal < 0.0f) {
                // If position is > 1.0 (invalid), it's due to unsigned wrap - wrap it back
                if (currentPosition > 1.0f) {
                    currentPosition = fmod(currentPosition, 1.0f);
                    audioPlayer.setPosition(currentPosition);
                }
                // If position jumped from near 0 to near 1 (backward wrap detected incorrectly)
                else if (currentPosition > BACKWARD_WRAP_DETECT_HIGH && lastPosition < BACKWARD_WRAP_DETECT_LOW && lastPosition > 0.0f) {
                    // Position wrapped incorrectly - set it to near the end for smooth backward playback
                    currentPosition = BACKWARD_WRAP_POSITION;
                    audioPlayer.setPosition(currentPosition);
                }
                // If position is very close to 0 and we're going backward, wrap to near end
                else if (currentPosition <= POSITION_SEEK_THRESHOLD && lastPosition > POSITION_SEEK_THRESHOLD) {
                    currentPosition = BACKWARD_WRAP_POSITION;
                    audioPlayer.setPosition(currentPosition);
                }
            }
        }
    }
    
    // Fallback to video player position if audio position is not available
    // ofxVideoFile handles backward looping internally in updatePlayback()
    if (!hasValidPosition && isVideoLoaded() && videoPlayer.isPlaying()) {
        currentPosition = videoPlayer.getVideoFile().getPosition();
        if (currentPosition > POSITION_VALID_THRESHOLD) {
            hasValidPosition = true;
        }
    }
    
    // Update the playheadPosition parameter to reflect actual playhead position during playback
    // Only update if we have a valid position and it has changed significantly
    // NOTE: This updates the playhead position, not startPosition
    // CRITICAL: When stopped, playheadPosition is frozen at last position (set in stop())
    // Only update if we have a valid new position
    if (hasValidPosition) {
        // Update if position changed significantly (smooth movement)
        // Use small threshold for better precision when zoomed
        // This ensures smooth playhead movement even when viewing a small portion of a long sample
        if (abs(currentPosition - lastPosition) > POSITION_UPDATE_THRESHOLD) {
            playheadPosition.set(currentPosition);
        }
    }
    // If no valid position and we were playing, keep last position (frozen at stop)
    // This ensures GUI shows correct position when playback stops
    // NOTE: When stopped, position management is handled by MultiSampler.
    // MultiSampler captures position before stop() for NEXT mode and manages position memory
    // according to play style. We don't update playheadPosition when stopped to avoid
    // overwriting values set by MultiSampler.
    // NOTE: Gating (scheduled stops) is now handled by MultiSampler, not MediaPlayer
}

// Parameter listeners
void MediaPlayer::onAudioEnabledChanged(bool& enabled) {
    if (!enabled && audioPlayer.isPlaying()) {
        audioPlayer.stop();
    }
}

void MediaPlayer::onVideoEnabledChanged(bool& enabled) {
    if (!enabled && videoPlayer.isPlaying()) {
        videoPlayer.stop();
    }
    
    // CRITICAL: Frame gating logic - must check enabled parameter FIRST
    // If enabled=false, always gate out (enabled=false) regardless of position/playing state
    // This prevents stopped videos from showing stale frames
    if (isVideoLoaded()) {
        bool shouldBeEnabled = false;
        if (enabled) {
            // Video is enabled - allow frames if playing OR paused for scrubbing
            // Only check position when video is enabled to allow scrubbing preview
            shouldBeEnabled = isPlaying() || (videoPlayer.getVideoFile().getPosition() > POSITION_VALID_THRESHOLD);
        }
        // else: shouldBeEnabled stays false (gate out when disabled/stopped)
        
        videoPlayer.enabled.set(shouldBeEnabled);
    }
}

void MediaPlayer::onPlayheadPositionChanged(float& pos) {
    // CRITICAL FIX: During playback, playheadPosition parameter is updated by update() to reflect
    // the actual playhead position. We should NOT seek during playback - only when paused/stopped.
    // Seeking during playback causes video to freeze at a fixed position.
    if (isPlaying()) {
        // PlayheadPosition is being updated by playback - don't seek, just update lastPosition for tracking
        lastPosition = pos;
        return;
    }
    
    // PERFORMANCE CRITICAL: Only seek when NOT playing (paused/stopped state)
    // This is for seeking while paused/stopped, not for playback position updates.
    // NOTE: Position memory is managed by MultiSampler, not MediaPlayer. When stopped,
    // MultiSampler may set playheadPosition for NEXT mode position memory. We trust
    // the playheadPosition parameter value and seek to it when it changes.
    if (pos > POSITION_VALID_THRESHOLD) {
        // Position is valid - seek to it (user seeking or position set by MultiSampler)
        if (isAudioLoaded()) {
            // Only set audio position if it's significantly different (audio seeking is fast, but still avoid unnecessary calls)
            float currentAudioPos = audioPlayer.getPosition();
            if (std::abs(currentAudioPos - pos) > POSITION_VALID_THRESHOLD) {
                audioPlayer.setPosition(pos);
            }
        }
        
        if (isVideoLoaded()) {
            // CRITICAL: Only set video position if it's significantly different
            // HAP video seeking takes 200ms+, so we MUST avoid redundant calls
            float currentVideoPos = videoPlayer.getVideoFile().getPosition();
            if (std::abs(currentVideoPos - pos) > POSITION_SEEK_THRESHOLD) {
                videoPlayer.getVideoFile().setPosition(pos);
            }
        }
    }
    // If pos is 0 or very small, don't seek - this might be a reset or MultiSampler clearing position
    
    lastPosition = pos;
}

void MediaPlayer::onSpeedChanged(float& speed) {
    if (isAudioLoaded()) {
        // Use the underlying sound player's setSpeed method
        audioPlayer.setSpeed(speed);
    }
    
    if (isVideoLoaded()) {
        videoPlayer.getVideoFile().setSpeed(speed);
    }
    
    lastSpeed = speed;
}

void MediaPlayer::onLoopChanged(bool& loop) {
    if (isAudioLoaded()) {
        // Use the underlying sound player's setLoop method
        audioPlayer.setLoop(loop);
    }
    
    if (isVideoLoaded()) {
        // Use the underlying video player's setLoopState method
        videoPlayer.getVideoFile().setLoopState(loop ? OF_LOOP_NORMAL : OF_LOOP_NONE);
    }
    
    lastLoop = loop;
}

void MediaPlayer::onVolumeChanged(float& vol) {
    if (isAudioLoaded()) {
        // Use the underlying sound player's setVolume method
        audioPlayer.setVolume(vol);
    }
}
