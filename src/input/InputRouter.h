#pragma once
#include "ofMain.h"

class Clock;
class TrackerSequencer;
class ViewManager;
class MediaPool;
class MediaPoolGUI;

class InputRouter {
public:
    InputRouter();
    ~InputRouter() = default;

    // Setup with references to controllable systems
    void setup(
        Clock* clock,
        TrackerSequencer* tracker,
        ViewManager* viewManager,
        MediaPool* mediaPool,
        MediaPoolGUI* mediaPoolGUI
    );

    // Callbacks for state that needs to be updated
    void setPlayState(bool* isPlaying);
    void setCurrentStep(int* currentStep);
    void setLastTriggeredStep(int* lastTriggeredStep);
    void setShowGUI(bool* showGUI);

    // Main keyboard handler - called from ofApp::keyPressed()
    // Returns true if the input was consumed (don't pass to others)
    bool handleKeyPress(ofKeyEventArgs& keyEvent);

    // System state flags
    bool isImGuiCapturingKeyboard() const;
    bool isSequencerInEditMode() const;

private:
    // System references
    Clock* clock = nullptr;
    TrackerSequencer* tracker = nullptr;
    ViewManager* viewManager = nullptr;
    MediaPool* mediaPool = nullptr;
    MediaPoolGUI* mediaPoolGUI = nullptr;

    // State references (optional - can be nullptr)
    bool* isPlaying = nullptr;
    int* currentStep = nullptr;
    int* lastTriggeredStep = nullptr;
    bool* showGUI = nullptr;

    // Keyboard capture state
    bool imGuiCapturingKeyboard = false;

    // Handler methods for different input categories
    bool handleGlobalShortcuts(int key);
    bool handlePanelNavigation(ofKeyEventArgs& keyEvent);
    bool handleTrackerInput(ofKeyEventArgs& keyEvent);

    // Helper to check ImGui capture state
    void updateImGuiCaptureState();
    
    // Helper to sync edit state from ImGui focus (if a cell is focused)
    void syncEditStateFromImGuiFocus();

    // Logging helper
    void logKeyPress(int key, const char* context);
};

