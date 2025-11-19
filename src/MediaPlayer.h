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
    ofxHSV hsvAdjust;  // HSV color adjustment processor for video
    
    // Enable/disable toggles
    ofParameter<bool> audioEnabled;
    ofParameter<bool> videoEnabled;
    
    // Synchronized parameters (control both A/V)
    ofParameter<float> playheadPosition; // 0.0-1.0 (current playhead position during playback)
    ofParameter<float> startPosition;    // 0.0-1.0 (start position for playback - synced with tracker)
    ofParameter<float> speed;         // playback rate
    ofParameter<bool> loop;
    ofParameter<float> regionStart;   // 0.0-1.0 (playback region start - defines minimum playable position)
    ofParameter<float> regionEnd;     // 0.0-1.0 (playback region end - defines maximum playable position)
    
    // Audio-specific parameters (forwarded from audioPlayer)
    ofParameter<float> volume;
    
    // Granular-style loop control
    ofParameter<float> loopSize;  // Loop size in seconds (0.0 to duration, affects playback when in LOOP play style)
    
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
    const ofParameterGroup& getParameters() const { return parameters; }
    
    // Helper methods for parameter access by name (for use in callbacks/mapping)
    // Returns nullptr if parameter name not found
    const ofParameter<float>* getFloatParameter(const std::string& name) const;
    ofParameter<float>* getFloatParameter(const std::string& name);
    
    // Setup method to initialize parameters and connections
    void setup();
    
private:
    // Parameter listeners
    void onAudioEnabledChanged(bool& enabled);
    void onVideoEnabledChanged(bool& enabled);
    void onPlayheadPositionChanged(float& pos);
    void onSpeedChanged(float& speed);
    void onLoopChanged(bool& loop);
    void onVolumeChanged(float& vol);
    void onBrightnessChanged(float& value);
    void onHueChanged(float& value);
    void onSaturationChanged(float& value);
    
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