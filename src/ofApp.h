//
//  ofApp.h
//
//  Audiovisual Sequencer Example - Time + Sound + Video coordination
//

#pragma once

#include "ofMain.h"
#include "ofxSoundObjects.h"
#include "ofxVisualObjects.h"
#include "MediaPlayer.h"
#include "MediaPool.h"
#include "MediaPoolGUI.h"
#include "TrackerSequencer.h"
#include "TrackerSequencerGUI.h"
#include "Pattern.h"
#include "Clock.h"
#include "ClockGUI.h"
#include "ofxImGui.h"
#include "imgui_internal.h" // For DockBuilder API
#include "implot.h"
#include "gui/MenuBar.h"
#include "gui/ViewManager.h"
#include "input/InputRouter.h"
#include "ParameterSync.h"

class ofApp : public ofBaseApp {
public:
    ~ofApp() noexcept;
    void setup();
    void update();
    void draw();
    void exit();
    
    void keyPressed(ofKeyEventArgs& keyEvent);
    void mousePressed(int x, int y, int button);
    void windowResized(int w, int h);
    
    // Audio callbacks
    void audioOut(ofSoundBuffer& buffer);
    
    // Step event handler for TrackerSequencer
    void onTrackerStepEvent(int step, float duration, const PatternCell& cell);
    
private:
    // Time objects
    Clock clock;
    ClockGUI clockGUI;
    
    // Media pool system
    MediaPool mediaPool;
    MediaPoolGUI mediaPoolGUI;
    
    // TrackerSequencer for pattern management
    TrackerSequencer trackerSequencer;
    TrackerSequencerGUI trackerSequencerGUI;
    
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
    
    // GUI managers
    MenuBar menuBar;
    ViewManager viewManager;
    InputRouter inputRouter;
    
    // Parameter synchronization system
    ParameterSync parameterSync;
    
    // GUI state
    bool showGUI = true;
    bool isPlaying = false;
    int numSteps = 16;
    
    // Note: Audio state (devices, volume, level) is now managed by ViewManager
    
    // Current step for GUI display (last triggered step)
    int currentStep = 0;
    int lastTriggeredStep = 0;  // Track the last step that was actually triggered
    
    // Media directory persistence
    std::string loadMediaDirectory();
    void saveMediaDirectory(const std::string& path);
    
    // Methods
    void setupSoundObjects();  // Minimal setup - audio device management is in ViewManager
    void setupVisualObjects();
    void setupGUI();
    
    void drawGUI();
    void setupDefaultLayout(bool forceReset = false);
    
    // Layout management
    void saveLayout();
    void loadLayout();
};
