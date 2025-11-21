#pragma once
#include "ofMain.h"
#include <imgui.h>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

class MenuBar {
public:
    MenuBar();
    ~MenuBar() = default;

    // Setup with callback functions
    void setup(
        std::function<void()> onSavePattern,
        std::function<void()> onLoadPattern,
        std::function<void()> onSaveLayout,
        std::function<void()> onLoadLayout,
        std::function<void(const std::string& moduleType)> onAddModule = nullptr,
        std::function<void()> onToggleFileBrowser = nullptr,
        std::function<void()> onToggleConsole = nullptr,
        std::function<void()> onToggleDemoWindow = nullptr
    );

    // Called every frame in ofApp::draw()
    // Returns true if menu bar is active (prevents input below)
    void draw();
    
    // Handle keyboard input (for MAJ+a shortcut)
    bool handleKeyPress(int key, bool shiftPressed);

    // Accessor for help popup state (optional, for external management)
    bool isHelpPopupOpen() const { return showControlsHelp; }
    void closeHelpPopup() { showControlsHelp = false; }
    
    // Accessor for Add Module popup state
    bool isAddModulePopupOpen() const { return showAddModulePopup; }
    void openAddModulePopup() { showAddModulePopup = true; }
    void closeAddModulePopup() { showAddModulePopup = false; }

private:
    // Callback functions
    std::function<void()> onSavePattern;
    std::function<void()> onLoadPattern;
    std::function<void()> onSaveLayout;
    std::function<void()> onLoadLayout;
    std::function<void(const std::string& moduleType)> onAddModule;
    std::function<void()> onToggleFileBrowser;
    std::function<void()> onToggleConsole;
    std::function<void()> onToggleDemoWindow;

    // UI state
    bool showControlsHelp = false;
    bool showAddModulePopup = false;
    
    // Add Module popup state
    char addModuleFilter[256] = "";
    int selectedModuleIndex = 0;
    
    // Available module types
    struct ModuleTypeInfo {
        std::string typeName;      // "MediaPool", "TrackerSequencer"
        std::string displayName;   // "Media Pool", "Tracker Sequencer"
        std::string description;   // "Video/audio media pool"
    };
    std::vector<ModuleTypeInfo> availableModules;

    // Private helper methods for each menu section
    void drawFileMenu();
    void drawViewMenu();
    void drawAddMenu();
    void drawLayoutMenu();
    void drawHelpMenu();
    void drawAddModulePopup();
    void initializeAvailableModules();
};

