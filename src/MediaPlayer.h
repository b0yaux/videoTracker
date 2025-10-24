#pragma once

#include "ofxSoundObjects.h"
#include "ofxVisualObjects.h"
#include "ofParameter.h"

// Forward declaration
class MediaPool;

class MediaPlayer {
public:
    MediaPlayer();
    ~MediaPlayer();
    
    // Composition - contains audio and video players
    ofxSoundPlayerObject audioPlayer;
    ofxVideoPlayerObject videoPlayer;
    
    // Enable/disable toggles
    ofParameter<bool> audioEnabled;
    ofParameter<bool> videoEnabled;
    
    // Synchronized parameters (control both A/V)
    ofParameter<float> position;      // 0.0-1.0
    ofParameter<float> speed;         // playback rate
    ofParameter<bool> loop;
    
    // Audio-specific parameters (forwarded from audioPlayer)
    ofParameter<float> volume;
    ofParameter<float> pitch;
    
    // Video-specific parameters (forwarded from videoPlayer)
    ofParameter<float> brightness;
    ofParameter<float> hue;
    ofParameter<float> saturation;
    
    // Parameter group for GUI and modulation
    ofParameterGroup parameters;
    
    // Loading
    bool load(const std::string& audioPath, const std::string& videoPath);
    bool loadAudio(const std::string& audioPath);
    bool loadVideo(const std::string& videoPath);
    
    // Playback control
    void play();
    void stop();
    void pause();
    void resume();
    void reset();
    void setPosition(float pos);
    
    // Gating system for tracker-style step control
    void playWithGate(float durationSeconds);
    
    // File path getters for display purposes
    std::string getAudioFilePath() const { return audioFilePath; }
    std::string getVideoFilePath() const { return videoFilePath; }
    
    // State queries
    bool isAudioLoaded() const;
    bool isVideoLoaded() const;
    bool isPlaying() const;
    float getDuration() const;  // Returns max(audio, video) duration
    
    // Update (call in ofApp::update)
    void update();
    
    // Accessors for underlying players
    ofxSoundPlayerObject& getAudioPlayer() { return audioPlayer; }
    ofxVideoPlayerObject& getVideoPlayer() { return videoPlayer; }
    
    // Parameter group accessor
    ofParameterGroup& getParameters() { return parameters; }
    
    
    // Setup method to initialize parameters and connections
    void setup();
    
private:
    // Parameter listeners
    void onAudioEnabledChanged(bool& enabled);
    void onVideoEnabledChanged(bool& enabled);
    void onPositionChanged(float& pos);
    void onSpeedChanged(float& speed);
    void onLoopChanged(bool& loop);
    void onVolumeChanged(float& vol);
    
    // Internal state
    bool isSetup;
    float lastPosition;
    float lastSpeed;
    bool lastLoop;
    
    // Gating system state
    bool scheduledStopActive;
    float stopTime;
    float gateDuration;
    
    // File path storage for display purposes
    std::string audioFilePath;
    std::string videoFilePath;
    
};