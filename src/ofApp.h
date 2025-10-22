//
//  ofApp.h
//
//  Audiovisual Sequencer Example - Time + Sound + Video coordination
//

#pragma once

#include "ofMain.h"
#include "ofxTimeObjects.h"
#include "ofxSoundObjects.h"
#include "ofxVisualObjects.h"
#include "MediaPlayer.h"
#include "MediaPool.h"
#include "TrackerSequencer.h"
#include "Clock.h"
#include "ofxImGui.h"

class ofApp : public ofBaseApp {
public:
    ~ofApp() noexcept;
    void setup();
    void update();
    void draw();
    void exit();
    
    void keyPressed(int key);
    void mousePressed(int x, int y, int button);
    
    // Audio callbacks
    void audioOut(ofSoundBuffer& buffer);
    
    // Step event handler for TrackerSequencer
    void onTrackerStepEvent(int step, float bpm, const TrackerSequencer::PatternCell& cell);
    
private:
    // Time objects
    Clock clock;
    
    // Media pool system
    MediaPool mediaPool;
    
    // TrackerSequencer for pattern management
    TrackerSequencer trackerSequencer;
    
    // Sound objects
    ofxSoundOutput soundOutput;
    
    // Visual objects
    ofxVisualOutput visualOutput;
    
    // No bridge system needed - direct connections only
    
    // Audio system
    ofSoundStream soundStream;
    ofSoundBuffer soundBuffer;
    
    // GUI system
    ofxImGui::Gui gui;
    
    // GUI state
    bool showGUI = true;
    bool isPlaying = false;
    float globalVolume = 1.0f;  // Global volume control
    int numSteps = 16;
    
    // BPM control is now handled by Clock
    
    // Audio level for visualization
    float currentAudioLevel = 0.0f;
    
    // Audio device selection
    std::vector<ofSoundDevice> audioDevices;
    int selectedAudioDevice = 0;
    bool audioDeviceChanged = false;
    
    // Current step for GUI display (last triggered step)
    int currentStep = 0;
    int lastTriggeredStep = 0;  // Track the last step that was actually triggered
    
    // Media directory persistence
    std::string loadMediaDirectory();
    void saveMediaDirectory(const std::string& path);
    
    // Methods
    void setupSoundObjects();
    void setupAudioStream();
    void setupVisualObjects();
    void setupGUI();
    
    void drawGUI();
    void drawMediaLibraryInfo();
    void drawClockControls();
    void drawAudioOutput();
    
    
    // GUI methods
    void drawMediaLibraryGUI();
    void drawControlsOverlay();
    
    // GUI callbacks
    void onBPMChanged(float& value);
    void onStepChanged(int& value);
};
