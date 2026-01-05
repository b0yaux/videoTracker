#pragma once

#include "ofxSoundObjects.h"
#include "ofxVisualObjects.h"
#include "ofParameter.h"
#include "ofxSingleSoundPlayer.h"  // For loadAudioFromShared access to getPlayInstance

// Forward declaration
class MultiSampler;

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
    
    // Parameter group for GUI and modulation
    ofParameterGroup parameters;
    
    // Loading
    bool load(const std::string& audioPath, const std::string& videoPath);
    bool loadAudio(const std::string& audioPath);
    bool loadAudioFromShared(std::shared_ptr<ofxSoundFile> sharedFile);  // Load from shared buffer (for polyphonic samplers)
    bool loadVideo(const std::string& videoPath);
    
    // Playback control
    void play();
    void stop();
    void pause();
    void resume();
    void reset();
    void unload();  // Unload all media (for reuse with different sample)
    void setPosition(float pos);
    
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
    
    // Position capture helper - single source of truth for position reading
    // Prioritizes: playing audio > playing video > parameter > stopped audio > stopped video
    // Public for use by MultiSampler for position memory (NEXT mode only)
    float captureCurrentPosition() const;
    
    // Setup method to initialize parameters and connections
    void setup();
    
private:
    // Named constants for thresholds and magic numbers
    static constexpr float POSITION_VALID_THRESHOLD = 0.001f;      // Minimum valid position (0.1%)
    static constexpr float POSITION_SEEK_THRESHOLD = 0.01f;        // Threshold for seeking operations (1%)
    static constexpr float POSITION_UPDATE_THRESHOLD = 0.000001f;  // Position update threshold for smooth playhead (0.0001%)
    static constexpr float BACKWARD_WRAP_DETECT_HIGH = 0.9f;       // High threshold for backward loop wrap detection
    static constexpr float BACKWARD_WRAP_DETECT_LOW = 0.1f;        // Low threshold for backward loop wrap detection
    static constexpr float BACKWARD_WRAP_POSITION = 0.99f;          // Position to set when backward wrap detected
    static constexpr float MIN_REGION_SIZE = 0.001f;                // Minimum valid region size
    static constexpr float MS_TO_SECONDS = 0.001f;                 // Milliseconds to seconds conversion
    
    // Parameter listeners
    void onAudioEnabledChanged(bool& enabled);
    void onVideoEnabledChanged(bool& enabled);
    void onPlayheadPositionChanged(float& pos);
    void onSpeedChanged(float& speed);
    void onLoopChanged(bool& loop);
    void onVolumeChanged(float& vol);
    
    // Internal state
    bool isSetup;
    float lastPosition;
    float lastSpeed;
    bool lastLoop;
    
    // File path storage for display purposes
    std::string audioFilePath;
    std::string videoFilePath;
    
};