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
    position.set("Position", 0.0f, 0.0f, 1.0f);
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
    // Get the target position before starting any playback
    float targetPosition = position.get();
    ofLogNotice("ofxMediaPlayer") << "MediaPlayer::play() called with target position: " << targetPosition << ", loop: " << loop.get();
    
    // Sync audio position before starting playback
    if (audioEnabled.get() && isAudioLoaded()) {
        audioEnabled.set(true);
        audioPlayer.setPosition(targetPosition);
        ofLogNotice("ofxMediaPlayer") << "Audio position set to: " << targetPosition;
        
        // Always call play() - it will handle paused state internally
        audioPlayer.play();
        ofLogNotice("ofxMediaPlayer") << "Audio play() called, actual position now: " << audioPlayer.getPosition();
        
        // If the position was reset by play(), set it again
        if (audioPlayer.getPosition() < targetPosition - 0.01f) {
            ofLogNotice("ofxMediaPlayer") << "Audio position was reset, setting again to: " << targetPosition;
            audioPlayer.setPosition(targetPosition);
        }
        
        ofLogNotice("ofxMediaPlayer") << "Playing audio - enabled: " << audioEnabled.get() 
                                      << ", loaded: " << isAudioLoaded() 
                                      << ", volume: " << volume.get()
                                      << ", position: " << targetPosition;
    }
    
    // Sync video position before starting playback
    if (videoEnabled.get() && isVideoLoaded()) {
        videoEnabled.set(true);
        videoPlayer.getVideoFile().setPosition(targetPosition);
        ofLogNotice("ofxMediaPlayer") << "Video position set to: " << targetPosition;
        
        // Force decode HAP videos to ensure immediate frame availability
        videoPlayer.getVideoFile().update();
        
        // Always call play() - it will handle paused state internally
        videoPlayer.play();
        ofLogNotice("ofxMediaPlayer") << "Video play() called, actual position now: " << videoPlayer.getVideoFile().getPosition();
        
        // If the position was reset by play(), set it again
        if (videoPlayer.getVideoFile().getPosition() < targetPosition - 0.01f) {
            ofLogNotice("ofxMediaPlayer") << "Video position was reset, setting again to: " << targetPosition;
            videoPlayer.getVideoFile().setPosition(targetPosition);
        }
        
        ofLogNotice("ofxMediaPlayer") << "Playing video - enabled: " << videoEnabled.get() 
                                      << ", loaded: " << isVideoLoaded()
                                      << ", position: " << targetPosition;
        
        // Force texture update after play to ensure HAP videos are properly decoded
        videoPlayer.getVideoFile().forceTextureUpdate();
        
        ofLogNotice("ofxMediaPlayer") << "Video play called - isPlaying: " << videoPlayer.isPlaying()
                                      << ", video file playing: " << videoPlayer.getVideoFile().isPlaying()
                                      << ", video file position: " << videoPlayer.getVideoFile().getPosition();
    } else {
        ofLogNotice("ofxMediaPlayer") << "Video not played - enabled: " << videoEnabled.get() 
                                      << ", loaded: " << isVideoLoaded();
    }
}

void MediaPlayer::stop() {
    audioPlayer.stop();
    videoPlayer.stop();
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
    // Update video player
    videoPlayer.update();
    
    // Video player state logging removed to reduce console spam
    
    // Sync position parameter with actual playback position
    // This makes position the single source of truth
    if (isPlaying()) {
        float currentPosition = 0.0f;
        
        // Get position from audio player if available and playing
        if (isAudioLoaded() && audioPlayer.isPlaying()) {
            currentPosition = audioPlayer.getPosition();
        }
        // Otherwise get position from video player if available and playing
        else if (isVideoLoaded() && videoPlayer.isPlaying()) {
            currentPosition = videoPlayer.getVideoFile().getPosition();
        }
        
        // Update the position parameter to reflect actual playback
        // Only update if the position has actually changed to avoid unnecessary updates
        if (abs(currentPosition - position.get()) > 0.001f) {
            position.set(currentPosition);
        }
        
    }
    
    // Check for scheduled stop (gating system)
    if (scheduledStopActive && ofGetElapsedTimef() >= stopTime) {
        stop();
        scheduledStopActive = false;
        ofLogNotice("ofxMediaPlayer") << "Gated stop triggered after " << gateDuration << " seconds";
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
    if (isAudioLoaded()) {
        // Use the underlying sound player's setPosition method
        audioPlayer.setPosition(pos);
    }
    
    if (isVideoLoaded()) {
        videoPlayer.getVideoFile().setPosition(pos);
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






