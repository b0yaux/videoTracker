#pragma once
#include "ofMain.h"
#include "GUIConstants.h"
#include <array>
#include <string>
#include <vector>

class Clock;
class ClockGUI;
class GUIManager;
class FileBrowser;
class Console;
class CommandBar;
class AssetLibraryGUI;
class ofxSoundOutput;



/**
 * ViewManager - Manages view/presentation layer and window rendering
 * 
 * RESPONSIBILITY: View rendering, window navigation, and view state management
 * 
 * Responsibilities:
 * - Render all windows (Clock, Audio Output, Tracker, Media Pool, File Browser, Console)
 * - Manage window navigation (switching between windows via Cmd+Arrow keys)
 * - Manage focus state (which window has keyboard focus)
 * - Manage window visibility for utility windows (FileBrowser, Console)
 * - Audio device selection UI and state (audio device list, selection)
 * - Audio volume/level visualization (UI only, actual audio processing in ofApp)
 * 
 * Separation of Concerns:
 * - ModuleFactory: Creates modules and manages identity
 * - ModuleRegistry: Stores and retrieves modules
 * - GUIManager: Creates/destroys GUI objects, manages instance visibility
 * - ViewManager: Renders windows, manages window navigation/focus, audio UI state
 * - ofApp: Audio processing, global volume application, audio level calculation
 * 
 * Note: ViewManager manages audio UI state (device selection, volume slider, level display)
 *       but actual audio processing happens in ofApp. ViewManager is view-only.
 * 
 * Usage Flow:
 *   1. ofApp calls viewManager.draw() each frame
 *   2. ViewManager gets GUI objects from GUIManager
 *   3. ViewManager renders each window based on current window state
 *   4. User interactions update view state (window selection, focus, visibility)
 */
class ViewManager {
public:
    ViewManager();
    ~ViewManager() = default;

    // Setup - pass all GUI objects and domains
    void setup(
        Clock* clock,
        ClockGUI* clockGUI,
        ofxSoundOutput* audioOutput,
        GUIManager* guiManager,  // GUIManager for multiple instances
        FileBrowser* fileBrowser,  // File browser panel
        Console* console,  // Console panel
        CommandBar* commandBar,  // Command bar panel
        AssetLibraryGUI* assetLibraryGUI  // Asset library panel
    );

    // Window-based navigation (works for ALL GUI panels - preferred method)
    void navigateToWindow(const std::string& windowName);
    std::string getCurrentFocusedWindow() const { return currentFocusedWindow; }
    
    // Simple window cycling (lightweight keyboard navigation)
    void nextWindow();        // Cmd+Right Arrow
    void previousWindow();    // Cmd+Left Arrow
    void upWindow();          // Cmd+Up Arrow
    void downWindow();        // Cmd+Down Arrow
    std::vector<std::string> getAvailableWindows() const;  // Get all navigable windows
    

    
    // FileBrowser visibility
    void setFileBrowserVisible(bool visible) { fileBrowserVisible_ = visible; }
    bool isFileBrowserVisible() const { return fileBrowserVisible_; }
    
    // Console visibility
    void setConsoleVisible(bool visible) { consoleVisible_ = visible; }
    bool isConsoleVisible() const { return consoleVisible_; }
    
    // AssetLibrary visibility
    void setAssetLibraryVisible(bool visible) { assetLibraryVisible_ = visible; }
    bool isAssetLibraryVisible() const { return assetLibraryVisible_; }
    
    // Master modules visibility (Clock + master outputs)
    void setMasterModulesVisible(bool visible) { masterModulesVisible_ = visible; }
    bool isMasterModulesVisible() const { return masterModulesVisible_; }
    
    // Layout loading state (used to defer Clock window drawing until layout is loaded)
    void setLayoutLoaded(bool loaded) { layoutLoaded_ = loaded; }
    bool isLayoutLoaded() const { return layoutLoaded_; }
    
    // Public method to draw Clock panel (needed for re-drawing after layout load)
    void drawClockPanel();

    // Mouse click detection and panel switching
    void handleMouseClick(int x, int y);

    // Main draw function - delegates to appropriate panels
    void draw();

    // Getters for current state
    const char* getCurrentWindowName() const;

    // Audio state access (for ofApp to update currentAudioLevel)
    // Note: Audio device management is now handled by AudioOutputGUI
    void setCurrentAudioLevel(float level) { currentAudioLevel = level; }
    float getGlobalVolume() const { return globalVolume; }

private:
    // Panel references
    Clock* clock = nullptr;
    ClockGUI* clockGUI = nullptr;
    ofxSoundOutput* audioOutput = nullptr;
    GUIManager* guiManager = nullptr;  // GUIManager for multiple instances
    FileBrowser* fileBrowser = nullptr;  // File browser panel
    Console* console = nullptr;  // Console panel
    CommandBar* commandBar = nullptr;  // Command bar panel
    AssetLibraryGUI* assetLibraryGUI = nullptr;  // Asset library panel

    // Audio state (owned by ViewManager)
    // Note: Audio device selection is now handled by AudioOutputGUI
    // Global volume is still managed here for ofApp::audioOut()
    float globalVolume = 1.0f;
    float currentAudioLevel = 0.0f;

    // State - Window name-based navigation (primary system)
    std::string currentFocusedWindow = "Clock ";  // Track focused window by name (works for ALL panels)
    std::string lastFocusedWindow;                // Track previous focused window for change detection
    

    bool fileBrowserVisible_ = false;  // FileBrowser visibility state
    bool consoleVisible_ = false;  // Console visibility state
    bool assetLibraryVisible_ = false;  // AssetLibrary visibility state
    bool masterModulesVisible_ = true;  // Master modules visibility state (clock + master outputs)
    bool layoutLoaded_ = false;  // Track if layout has been loaded (used to defer Clock window drawing)

    // Private draw methods for each window
    // Generic method to draw all visible module windows (handles all module types)
    void drawModulePanels();
    void drawFileBrowserPanel();  // Draw FileBrowser window
    void drawConsolePanel();  // Draw Console window
    void drawAssetLibraryPanel();  // Draw AssetLibrary window

    // Helper to set focus when window changes (not every frame)
    void setFocusIfChanged();
    
    // Spatial navigation helper - unified for all directions
    std::string findWindowInDirection(const std::string& currentWindow, int direction) const;
    std::string findAlignedCycleWindow(const std::string& currentWindow, int direction) const;
    // direction: 0=right, 1=left, 2=down, 3=up
    
    // Modular focus outline system - call from within window Begin/End context
    void drawWindowOutline();
};

