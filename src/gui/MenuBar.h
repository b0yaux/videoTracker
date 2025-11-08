#pragma once
#include "ofMain.h"
#include "ofxImGui.h"
#include <functional>
#include <string>

class MenuBar {
public:
    MenuBar();
    ~MenuBar() = default;

    // Setup with callback functions
    void setup(
        std::function<void()> onSavePattern,
        std::function<void()> onLoadPattern,
        std::function<void()> onSaveLayout,
        std::function<void()> onLoadLayout
    );

    // Called every frame in ofApp::draw()
    // Returns true if menu bar is active (prevents input below)
    void draw();

    // Accessor for help popup state (optional, for external management)
    bool isHelpPopupOpen() const { return showControlsHelp; }
    void closeHelpPopup() { showControlsHelp = false; }

private:
    // Callback functions
    std::function<void()> onSavePattern;
    std::function<void()> onLoadPattern;
    std::function<void()> onSaveLayout;
    std::function<void()> onLoadLayout;

    // UI state
    bool showControlsHelp = false;

    // Private helper methods for each menu section
    void drawFileMenu();
    void drawLayoutMenu();
    void drawHelpMenu();
};

