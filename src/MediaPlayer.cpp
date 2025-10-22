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
    speed.set("Speed", 1.0f, -4.0f, 4.0f);  // Support negative speeds for backward playback
    loop.set("Loop", false);
    
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
    if (audioEnabled.get() && isAudioLoaded()) {
        ofLogNotice("ofxMediaPlayer") << "Playing audio - enabled: " << audioEnabled.get() 
                                      << ", loaded: " << isAudioLoaded() 
                                      << ", volume: " << volume.get();
        audioPlayer.play();
    }
    
    if (videoEnabled.get() && isVideoLoaded()) {
        ofLogNotice("ofxMediaPlayer") << "Playing video - enabled: " << videoEnabled.get() 
                                      << ", loaded: " << isVideoLoaded();
        
        // Ensure video is not paused before playing
        videoPlayer.setPaused(false);
        
        // Play the video player
        videoPlayer.play();
        
        // Also ensure the underlying video file is playing
        auto& videoFile = videoPlayer.getVideoFile();
        videoFile.play();
        
        // Set position to the parameter value
        videoFile.setPosition(position.get());
        
        // Force texture update to ensure frame is available
        videoFile.forceTextureUpdate();
        
        ofLogNotice("ofxMediaPlayer") << "Video play called - isPlaying: " << videoPlayer.isPlaying()
                                      << ", video file playing: " << videoFile.isPlaying()
                                      << ", video file position: " << videoFile.getPosition();
    } else {
        ofLogNotice("ofxMediaPlayer") << "Video not played - enabled: " << videoEnabled.get() 
                                      << ", loaded: " << isVideoLoaded();
    }
}

void MediaPlayer::stop() {
    audioPlayer.stop();
    videoPlayer.stop();
}

void MediaPlayer::pause() {
    audioPlayer.setPaused(true);
    videoPlayer.setPaused(true);
}

void MediaPlayer::setPosition(float pos) {
    position.set(pos);
    
    if (isAudioLoaded()) {
        // Use the underlying sound player's setPosition method
        audioPlayer.setPosition(pos);
    }
    
    if (isVideoLoaded()) {
        videoPlayer.getVideoFile().setPosition(pos);
    }
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

// Gating system implementation
void MediaPlayer::playWithGate(float durationSeconds) {
    // Cancel any existing scheduled stop
    cancelScheduledStop();
    
    // Start playback
    play();
    
    // Schedule stop
    scheduleStop(durationSeconds);
    
    ofLogNotice("ofxMediaPlayer") << "Playing with gate for " << durationSeconds << " seconds";
}

void MediaPlayer::scheduleStop(float delaySeconds) {
    scheduledStopActive = true;
    stopTime = ofGetElapsedTimef() + delaySeconds;
    gateDuration = delaySeconds;
    
    ofLogNotice("ofxMediaPlayer") << "Scheduled stop in " << delaySeconds << " seconds";
}

void MediaPlayer::cancelScheduledStop() {
    scheduledStopActive = false;
    stopTime = 0.0f;
    gateDuration = 0.0f;
    
    ofLogNotice("ofxMediaPlayer") << "Cancelled scheduled stop";
}

bool MediaPlayer::hasScheduledStop() const {
    return scheduledStopActive;
}




