#include "MediaPlayer.h"

MediaPlayer::MediaPlayer() : isSetup(false), lastPosition(0.0f), lastSpeed(1.0f), lastLoop(false),
                                   scheduledStopActive(false), stopTime(0.0f), gateDuration(0.0f) {
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
    position.set("Position", 0.0f, 0.0f, 1.0f);  // Current playhead position (updates during playback)
    startPosition.set("Start Position", 0.0f, 0.0f, 1.0f);  // Start position for playback (synced with tracker)
    speed.set("Speed", 1.0f, -10.0f, 10.0f);  // Support negative speeds for backward playback
    loop.set("Loop", true);
    
    // Setup enable/disable toggles
    audioEnabled.set("Audio Enabled", true);
    videoEnabled.set("Video Enabled", true);
    
    // Setup audio-specific parameters
    volume.set("Volume", 1.0f, 0.0f, 2.0f);
    pitch.set("Pitch", 1.0f, 0.5f, 2.0f);
    
    // Setup video-specific parameters
    brightness.set("Brightness", 1.0f, 0.0f, 2.0f);
    hue.set("Hue", 0.0f, 0.0f, 360.0f);
    saturation.set("Saturation", 1.0f, 0.0f, 2.0f);
    
    // Add all parameters to the parameter group
    parameters.add(position);
    parameters.add(startPosition);
    parameters.add(speed);
    parameters.add(loop);
    parameters.add(audioEnabled);
    parameters.add(videoEnabled);
    parameters.add(volume);
    parameters.add(pitch);
    parameters.add(brightness);
    parameters.add(hue);
    parameters.add(saturation);
    
    // Setup parameter listeners
    audioEnabled.addListener(this, &MediaPlayer::onAudioEnabledChanged);
    videoEnabled.addListener(this, &MediaPlayer::onVideoEnabledChanged);
    position.addListener(this, &MediaPlayer::onPositionChanged);
    speed.addListener(this, &MediaPlayer::onSpeedChanged);
    loop.addListener(this, &MediaPlayer::onLoopChanged);
    volume.addListener(this, &MediaPlayer::onVolumeChanged);
    
    // Parameters are managed directly by ofxMediaPlayer
    // No need to forward to underlying players since they don't have media parameters
    
    isSetup = true;
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
    
    return audioLoaded || videoLoaded;
}

bool MediaPlayer::loadAudio(const std::string& audioPath) {
    if (audioPath.empty()) return false;
    
    ofLogNotice("ofxMediaPlayer") << "Loading audio: " << audioPath;
    bool success = audioPlayer.load(audioPath);
    
    if (success) {
        audioFilePath = audioPath;
        ofLogNotice("ofxMediaPlayer") << "Audio loaded successfully: " << audioPath;
    } else {
        ofLogError("ofxMediaPlayer") << "Failed to load audio: " << audioPath;
    }
    
    return success;
}

bool MediaPlayer::loadVideo(const std::string& videoPath) {
    if (videoPath.empty()) return false;
    
    ofLogNotice("ofxMediaPlayer") << "Loading video: " << videoPath;
    bool success = videoPlayer.load(videoPath);
    
    if (success) {
        videoFilePath = videoPath;
        ofLogNotice("ofxMediaPlayer") << "Video loaded successfully: " << videoPath;
    } else {
        ofLogError("ofxMediaPlayer") << "Failed to load video: " << videoPath;
    }
    
    return success;
}

void MediaPlayer::play() {
    // Get the start position (use startPosition - it's set by MediaPool based on trigger event)
    // Position memory is now handled at MediaPool level when retriggering same media
    // 0.0 is a valid position (start of media), not a sentinel for position memory
    float targetPosition = startPosition.get();
    float currentSpeed = speed.get();
    bool currentLoop = loop.get();
    
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
        if (audioPlayer.getPosition() < targetPosition - 0.01f) {
            audioPlayer.setPosition(targetPosition);
        }
    }
    
    // Sync video position before starting playback
    if (videoEnabled.get() && isVideoLoaded()) {
        videoEnabled.set(true);
        
        // PERFORMANCE CRITICAL: Check if position is already correct before expensive setPosition() call
        // HAP video seeking takes 200ms+, so we should avoid it if possible
        float currentVideoPos = videoPlayer.getVideoFile().getPosition();
        bool positionNeedsUpdate = std::abs(currentVideoPos - targetPosition) > 0.01f;
        
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
        
        // REMOVED: Position correction check - ofxVideoFile::play() doesn't reset position
        // This was causing a second expensive setPosition() call (another 200ms+)
        // The position is already set correctly before play(), so no correction needed
        
        // Update position parameter for UI display (after actual position is set)
        // This ensures the UI shows the correct position without triggering expensive setPosition()
        // via the listener (since we already set it above)
        if (std::abs(position.get() - targetPosition) > 0.001f) {
            // Temporarily disable listener to avoid triggering expensive setPosition() again
            // We can't easily disable the listener, so we'll just update it and let the update() loop
            // sync it during playback. The position is already set correctly above.
            position.set(targetPosition);
        }
    }
}

void MediaPlayer::stop() {
    audioPlayer.stop();
    videoPlayer.stop();
    
    // When stopped, immediately sync position with startPosition for display/syncing
    // This ensures the playhead shows the start position when paused (for sync with tracker)
    float newPos = startPosition.get();
    float oldPos = position.get();
    if (std::abs(oldPos - newPos) > 0.001f) {
        position.set(newPos);
        // Note: onPositionChanged will be triggered, but we don't have direct access to MediaPool callback here
        // The sync will happen via MediaPool's setParameter when it detects the change
    }
    
    if (audioEnabled.get()) {
        audioEnabled.set(false);
    }
    if (videoEnabled.get()) {
        videoEnabled.set(false);
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
    
    // Reset position to beginning
    position.set(0.0f);
    
    // Re-enable audio/video if they were loaded
    if (isAudioLoaded()) {
        audioEnabled.set(true);
    }
    if (isVideoLoaded()) {
        videoEnabled.set(true);
    }
    
    ofLogNotice("ofxMediaPlayer") << "Player reset - ready for fresh playback";
}

void MediaPlayer::setPosition(float pos) {
    position.set(pos);
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
        // Note: getDurationMS() is not const, so we'll use a workaround
        // For now, we'll return 0 for audio duration
        audioDuration = 0.0f;
    }
    
    float videoDuration = 0.0f;
    if (isVideoLoaded()) {
        // Note: getVideoFile() is not const, so we'll use a workaround
        // For now, we'll return 0 for video duration
        videoDuration = 0.0f;
    }
    
    return std::max(audioDuration, videoDuration);
}

void MediaPlayer::update() {
    // PERFORMANCE CRITICAL: Only update video player when actually playing
    // videoPlayer.update() can be expensive (texture updates, buffer operations)
    // Don't call it when stopped/paused - this causes lag even with empty patterns
    if (isPlaying() && isVideoLoaded() && videoEnabled.get()) {
        videoPlayer.update();  // Only update when actually playing
    }
    
    // Sync position parameter with actual playback position
    if (isPlaying()) {
        float currentPosition = 0.0f;
        float speedVal = speed.get();
        bool loopVal = loop.get();
        
        // Get position from audio player if available and playing
        if (isAudioLoaded() && audioPlayer.isPlaying()) {
            currentPosition = audioPlayer.getPosition();
            
            // Workaround for addon bug: ofxSingleSoundPlayer uses unsigned size_t for position,
            // which causes unsigned underflow when playing backward with negative speed.
            // When position wraps from 0 backwards, it becomes a huge unsigned number, which
            // after modulo can result in incorrect position values, causing audio glitches.
            // Fix: Detect and correct backward looping wrap issues
            if (loopVal && speedVal < 0.0f) {
                float lastPosition = position.get();
                
                // If position is > 1.0 (invalid), it's due to unsigned wrap - wrap it back
                if (currentPosition > 1.0f) {
                    currentPosition = fmod(currentPosition, 1.0f);
                    audioPlayer.setPosition(currentPosition);
                }
                // If position jumped from near 0 to near 1 (backward wrap detected incorrectly)
                else if (currentPosition > 0.9f && lastPosition < 0.1f && lastPosition > 0.0f) {
                    // Position wrapped incorrectly - set it to near the end for smooth backward playback
                    currentPosition = 0.99f;
                    audioPlayer.setPosition(currentPosition);
                }
                // If position is very close to 0 and we're going backward, wrap to near end
                else if (currentPosition <= 0.01f && lastPosition > 0.01f) {
                    currentPosition = 0.99f;
                    audioPlayer.setPosition(currentPosition);
                }
            }
        }
        // Otherwise get position from video player if available and playing
        // ofxVideoFile handles backward looping internally in updatePlayback()
        else if (isVideoLoaded() && videoPlayer.isPlaying()) {
            currentPosition = videoPlayer.getVideoFile().getPosition();
        }
        
        // Update the position parameter to reflect actual playhead position during playback
        // Only update if the position has actually changed to avoid unnecessary updates
        // NOTE: This updates the playhead position, not startPosition
        if (abs(currentPosition - position.get()) > 0.001f) {
            position.set(currentPosition);
        }
    }
    // Note: When stopped, position is synced immediately in stop() method
    // No need to sync in update loop - this avoids delay
    
    // Check for scheduled stop (gating system)
    if (scheduledStopActive && ofGetElapsedTimef() >= stopTime) {
        stop();
        scheduledStopActive = false;
        // Changed to verbose to avoid performance issues during playback
        ofLogVerbose("ofxMediaPlayer") << "Gated stop triggered after " << gateDuration << " seconds";
    }
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
}

void MediaPlayer::onPositionChanged(float& pos) {
    // CRITICAL FIX: During playback, position parameter is updated by update() to reflect
    // the actual playhead position. We should NOT seek during playback - only when paused/stopped.
    // Seeking during playback causes video to freeze at a fixed position.
    if (isPlaying()) {
        // Position is being updated by playback - don't seek, just update lastPosition for tracking
        lastPosition = pos;
        return;
    }
    
    // PERFORMANCE CRITICAL: Only seek when NOT playing (paused/stopped state)
    // This is for seeking while paused, not for playback position updates
    if (isAudioLoaded()) {
        // Only set audio position if it's significantly different (audio seeking is fast, but still avoid unnecessary calls)
        float currentAudioPos = audioPlayer.getPosition();
        if (std::abs(currentAudioPos - pos) > 0.001f) {
            audioPlayer.setPosition(pos);
        }
    }
    
    if (isVideoLoaded()) {
        // CRITICAL: Only set video position if it's significantly different
        // HAP video seeking takes 200ms+, so we MUST avoid redundant calls
        float currentVideoPos = videoPlayer.getVideoFile().getPosition();
        if (std::abs(currentVideoPos - pos) > 0.01f) {
            videoPlayer.getVideoFile().setPosition(pos);
        }
    }
    
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

// Simple gating - just play and schedule a stop
void MediaPlayer::playWithGate(float durationSeconds) {
    play();
    scheduledStopActive = true;
    stopTime = ofGetElapsedTimef() + durationSeconds;
}







