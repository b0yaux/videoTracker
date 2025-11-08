#pragma once
#include "ofMain.h"
#include <array>
#include <string>

class Clock;
class ClockGUI;
class MediaPool;
class MediaPoolGUI;
class TrackerSequencer;
class TrackerSequencerGUI;
class ofxSoundOutput;
class ofSoundStream;

// Panel identifiers
enum class Panel {
    CLOCK = 0,
    AUDIO_OUTPUT = 1,
    TRACKER = 2,
    MEDIA_POOL = 3,
    COUNT = 4
};

class ViewManager {
public:
    ViewManager();
    ~ViewManager() = default;

    // Setup - pass all GUI objects and domains
    void setup(
        Clock* clock,
        ClockGUI* clockGUI,
        ofxSoundOutput* audioOutput,
        TrackerSequencer* tracker,
        TrackerSequencerGUI* trackerGUI,
        MediaPool* mediaPool,
        MediaPoolGUI* mediaPoolGUI,
        ofSoundStream* soundStream  // For audio device management
    );

    // Panel navigation
    void navigateToPanel(Panel panel);
    void nextPanel();
    void previousPanel();
    Panel getCurrentPanel() const { return currentPanel; }

    // Mouse click detection and panel switching
    void handleMouseClick(int x, int y);

    // Main draw function - delegates to appropriate panels
    void draw();

    // Getters for current state
    int getCurrentPanelIndex() const { return static_cast<int>(currentPanel); }
    const char* getCurrentPanelName() const;

    // Audio state access (for ofApp to update currentAudioLevel and set listener)
    void setCurrentAudioLevel(float level) { currentAudioLevel = level; }
    float getGlobalVolume() const { return globalVolume; }
    void setAudioListener(ofBaseApp* listener) { audioListener = listener; }
    
    // Audio device management (public for ofApp to call after setting listener)
    void setupAudioStream(ofBaseApp* audioListener = nullptr);

private:
    // Panel references
    Clock* clock = nullptr;
    ClockGUI* clockGUI = nullptr;
    ofxSoundOutput* audioOutput = nullptr;
    TrackerSequencer* tracker = nullptr;
    TrackerSequencerGUI* trackerGUI = nullptr;
    MediaPool* mediaPool = nullptr;
    MediaPoolGUI* mediaPoolGUI = nullptr;
    ofSoundStream* soundStream = nullptr;  // For audio device management

    // Audio Output panel state (owned by ViewManager)
    std::vector<ofSoundDevice> audioDevices;
    int selectedAudioDevice = 0;
    bool audioDeviceChanged = false;
    float globalVolume = 1.0f;
    float currentAudioLevel = 0.0f;
    ofBaseApp* audioListener = nullptr;  // Store listener for device changes

    // State
    Panel currentPanel = Panel::CLOCK;
    Panel lastPanel = Panel::COUNT;  // Invalid, triggers focus on first draw

    // Panel names for debugging/logging
    static constexpr std::array<const char*, 4> PANEL_NAMES = {{
        "Clock ",
        "Audio Output",
        "Tracker Sequencer",
        "Media Pool"
    }};

    // Private draw methods for each panel
    void drawClockPanel(Panel previousPanel);
    void drawAudioOutputPanel(Panel previousPanel);
    void drawTrackerPanel(Panel previousPanel);
    void drawMediaPoolPanel(Panel previousPanel);

    // Helper to set focus when panel changes (not every frame)
    void setFocusIfChanged();
};

