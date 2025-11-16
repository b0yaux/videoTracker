#pragma once
#include "ofMain.h"
#include <functional>

class Clock;
class TrackerSequencer;
class TrackerSequencerGUI;
class ViewManager;
class MediaPool;
class MediaPoolGUI;
class Console;

class InputRouter {
public:
    InputRouter();
    ~InputRouter() = default;

    // Setup with references to controllable systems
    void setup(
        Clock* clock,
        TrackerSequencer* tracker,
        TrackerSequencerGUI* trackerGUI,
        ViewManager* viewManager,
        MediaPool* mediaPool,
        MediaPoolGUI* mediaPoolGUI,
        Console* console
    );
    
    // Set callbacks for session save/load (called by 'S' key)
    void setSessionCallbacks(
        std::function<void()> onSaveSession,
        std::function<void()> onLoadSession
    );

    // Callbacks for state that needs to be updated
    // Note: Play state now comes directly from Clock (single source of truth)
    // Clock reference is provided in setup() - no need for separate setPlayState()
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
    TrackerSequencerGUI* trackerGUI = nullptr;
    ViewManager* viewManager = nullptr;
    MediaPool* mediaPool = nullptr;
    MediaPoolGUI* mediaPoolGUI = nullptr;
    Console* console = nullptr;

    // State references (optional - can be nullptr)
    // Note: Play state comes from Clock reference (single source of truth)
    int* currentStep = nullptr;
    int* lastTriggeredStep = nullptr;
    bool* showGUI = nullptr;

    // Session save/load callbacks
    std::function<void()> onSaveSession;
    std::function<void()> onLoadSession;

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

