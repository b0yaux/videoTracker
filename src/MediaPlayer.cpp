#include "MediaPlayer.h"

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

bool MediaPlayer::loadVideo(const std::string& videoPath) {
    if (videoPath.empty()) return false;
    
    try {
        // CRITICAL: Disable HAP audio BEFORE loading if we have separate audio
        // This prevents device probing during load and audio stream creation
        if (isAudioLoaded()) {
            videoPlayer.getVideoFile().disableHapAudio();
            ofLogNotice("MediaPlayer") << "Disabled HAP embedded audio before load (using separate audio file)";
        }
        
        ofLogNotice("MediaPlayer") << "Loading video: " << videoPath;
        bool success = videoPlayer.load(videoPath);
        
        if (success) {
            videoFilePath = videoPath;
            
            ofLogNotice("MediaPlayer") << "Video loaded successfully: " << videoPath;
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
    // Position memory is now handled at MediaPool level when retriggering same media
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
    if (audioEnabled.get() && isAudioLoaded()) {
        audioEnabled.set(true);
        audioPlayer.setPosition(targetPosition);
        
        // Always call play() - it will handle paused state internally
        audioPlayer.play();
        
        // If the position was reset by play(), set it again
        if (audioPlayer.getPosition() < targetPosition - POSITION_SEEK_THRESHOLD) {
            audioPlayer.setPosition(targetPosition);
        }
    }
    
    // Sync video position before starting playback
    if (videoEnabled.get() && isVideoLoaded()) {
        videoEnabled.set(true);
        
        // CRITICAL: Ensure HAP embedded audio stays stopped if we have separate audio
        // The HAP player's AudioThread tries to start audio automatically when playing,
        // so we need to stop it again right before playing
        if (isAudioLoaded() && videoPlayer.getVideoFile().isUsingHap()) {
            videoPlayer.getVideoFile().stopHapAudio();
        }
        
        // PERFORMANCE CRITICAL: Check if position is already correct before expensive setPosition() call
        // HAP video seeking takes 200ms+, so we should avoid it if possible
        float currentVideoPos = videoPlayer.getVideoFile().getPosition();
        bool positionNeedsUpdate = std::abs(currentVideoPos - targetPosition) > POSITION_SEEK_THRESHOLD;
        
        if (positionNeedsUpdate) {
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
    // CRITICAL: Capture actual playback position BEFORE stopping underlying players
    // Once we call stop() on the players, they may reset their position to 0
    // So we must capture position from the players BEFORE stopping them
    float actualPlaybackPosition = captureCurrentPosition();
    
    // NOW stop the players (after capturing position)
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
        // Force texture update to clear any stale frames
        videoPlayer.getVideoFile().update();
    }
    
    // CRITICAL: Preserve position IMMEDIATELY after stopping players
    // The players may have reset their position to 0, and any callbacks/listeners
    // might try to read from them and reset the playheadPosition parameter
    // So we MUST set the playheadPosition parameter to the captured value right away
    // 
    // NOTE: We preserve position even if it's at the end - this allows scanning
    // to work properly. The position will be reset by MediaPool when appropriate
    // (e.g., when transport starts, or when explicitly cleared).
    if (actualPlaybackPosition > POSITION_VALID_THRESHOLD) {
        playheadPosition.set(actualPlaybackPosition);
        ofLogNotice("MediaPlayer") << "Preserved playback position in stop(): " << actualPlaybackPosition 
                                       << " (startPosition: " << startPosition.get() << ")";
    } else {
        // If position is still 0, check if we had a valid position before (might be a race condition)
        // In this case, keep the existing playheadPosition parameter value if it's valid
        float existingPos = playheadPosition.get();
        if (existingPos > POSITION_VALID_THRESHOLD) {
            ofLogNotice("MediaPlayer") << "Keeping existing playheadPosition parameter in stop(): " << existingPos
                                           << " (startPosition: " << startPosition.get() << ")";
            // Don't overwrite - playheadPosition parameter already has a valid value
        } else {
            ofLogVerbose("MediaPlayer") << "No valid position to preserve in stop() (was: " << existingPos << ")";
        }
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
    // CRITICAL: Keep HAP embedded audio stopped if we have separate audio
    // The HAP player's AudioThread runs in a separate thread and may try to restart audio
    // during playback. We stop it periodically (every 60 frames ≈ 1 second at 60fps)
    // to prevent it from creating a conflicting audio stream
    if (isAudioLoaded() && isVideoLoaded() && videoPlayer.getVideoFile().isUsingHap() && isPlaying()) {
        static int frameCounter = 0;
        if (++frameCounter % 60 == 0) {
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
        
        // Only update video player when actually playing (performance optimization)
        if (videoEnabled.get() && actuallyPlaying && (!atEnd || loop.get())) {
            videoPlayer.update();
        }
        
        // Process video player only if enabled
        if (videoEnabled.get() && shouldBeEnabled) {
            ofFbo emptyInput;
            videoPlayer.process(emptyInput, videoPlayer.getOutputBuffer());
        }
    }
    
    // Sync position parameter with actual playback position
    // CRITICAL: Only update position when actively playing
    // When stopped, position is preserved by stop() method and should NOT be overwritten
    // by reading from underlying players (which are reset to 0)
    if (isPlaying()) {
        // Cache parameter reads to avoid redundant calls
        float speedVal = speed.get();
        bool loopVal = loop.get();
        float lastPosition = playheadPosition.get();  // Cache for backward loop detection
        
        // Get position from audio player if available and playing
        float currentPosition = 0.0f;
        if (isAudioLoaded() && audioPlayer.isPlaying()) {
            currentPosition = audioPlayer.getPosition();
            
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
        // Otherwise get position from video player if available and playing
        // ofxVideoFile handles backward looping internally in updatePlayback()
        else if (isVideoLoaded() && videoPlayer.isPlaying()) {
            currentPosition = videoPlayer.getVideoFile().getPosition();
        }
        
        // Update the playheadPosition parameter to reflect actual playhead position during playback
        // Only update if the position has actually changed to avoid unnecessary updates
        // NOTE: This updates the playhead position, not startPosition
        // CRITICAL: Update position even if it's 0.0 (valid at start of playback)
        // Use small threshold for better precision when zoomed
        // This ensures smooth playhead movement even when viewing a small portion of a long sample
        if (abs(currentPosition - lastPosition) > POSITION_UPDATE_THRESHOLD) {
            playheadPosition.set(currentPosition);
        }
    }
    // CRITICAL: When stopped, position is preserved by stop() method
    // DO NOT read from underlying players (which are reset to 0) and overwrite the preserved position
    // The playheadPosition parameter contains the preserved playback position for position memory
    // NOTE: Gating (scheduled stops) is now handled by MediaPool, not MediaPlayer
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
    
    // CRITICAL: When stopped, the playheadPosition parameter contains the preserved playback position
    // for position memory. We should NOT read from the players (which are reset to 0) and
    // overwrite the playheadPosition parameter. Only seek if the position is being explicitly set
    // (e.g., by user seeking), not if it's being preserved from stop().
    // 
    // If position is > 0.01f, it's likely a preserved position from stop() - don't overwrite it
    // by reading from players (which are at 0). Only seek if we're explicitly setting a new position.
    // 
    // However, we still need to allow seeking when paused/stopped for user interaction.
    // The key is: if playheadPosition parameter is valid (> 0.01f), trust it and seek to it.
    // Don't read from players and potentially reset it to 0.
    
    // PERFORMANCE CRITICAL: Only seek when NOT playing (paused/stopped state)
    // This is for seeking while paused, not for playback position updates
    // CRITICAL: Don't read from players and reset position - if pos is valid, seek to it
    if (pos > POSITION_VALID_THRESHOLD) {
        // Position is valid - seek to it (user seeking or preserved position)
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
    // If pos is 0 or very small, don't seek - this might be a reset that we want to ignore
    // (e.g., if players reset to 0 but we want to preserve the playheadPosition parameter)
    
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
