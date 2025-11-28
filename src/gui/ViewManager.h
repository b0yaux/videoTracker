#pragma once
#include "ofMain.h"
#include "GUIConstants.h"
#include <array>
#include <string>

class Clock;
class ClockGUI;
class GUIManager;
class FileBrowser;
class Console;
class CommandBar;
class AssetLibraryGUI;
class ofxSoundOutput;

// Panel identifiers
enum class Panel {
    CLOCK = 0,
    AUDIO_OUTPUT = 1,
    TRACKER = 2,
    MEDIA_POOL = 3,
    FILE_BROWSER = 4,
    CONSOLE = 5,
    ASSET_LIBRARY = 6,
    COUNT = 7
};

/**
 * ViewManager - Manages view/presentation layer and panel rendering
 * 
 * RESPONSIBILITY: View rendering, panel navigation, and view state management
 * 
 * Responsibilities:
 * - Render all panels (Clock, Audio Output, Tracker, Media Pool, File Browser, Console)
 * - Manage panel navigation (switching between panels)
 * - Manage focus state (which panel has keyboard focus)
 * - Manage panel visibility for utility panels (FileBrowser, Console)
 * - Audio device selection UI and state (audio device list, selection)
 * - Audio volume/level visualization (UI only, actual audio processing in ofApp)
 * 
 * Separation of Concerns:
 * - ModuleFactory: Creates modules and manages identity
 * - ModuleRegistry: Stores and retrieves modules
 * - GUIManager: Creates/destroys GUI objects, manages instance visibility
 * - ViewManager: Renders panels, manages panel navigation/focus, audio UI state
 * - ofApp: Audio processing, global volume application, audio level calculation
 * 
 * Note: ViewManager manages audio UI state (device selection, volume slider, level display)
 *       but actual audio processing happens in ofApp. ViewManager is view-only.
 * 
 * Usage Flow:
 *   1. ofApp calls viewManager.draw() each frame
 *   2. ViewManager gets GUI objects from GUIManager
 *   3. ViewManager renders each panel based on current panel state
 *   4. User interactions update view state (panel selection, focus, visibility)
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
    
    // Panel navigation (DEPRECATED: Use navigateToWindow() instead - kept for backward compatibility)
    void navigateToPanel(Panel panel);
    void nextPanel();
    void previousPanel();
    Panel getCurrentPanel() const { return currentPanel; }
    
    // FileBrowser visibility
    void setFileBrowserVisible(bool visible) { fileBrowserVisible_ = visible; }
    bool isFileBrowserVisible() const { return fileBrowserVisible_; }
    
    // Console visibility
    void setConsoleVisible(bool visible) { consoleVisible_ = visible; }
    bool isConsoleVisible() const { return consoleVisible_; }
    
    // AssetLibrary visibility
    void setAssetLibraryVisible(bool visible) { assetLibraryVisible_ = visible; }
    bool isAssetLibraryVisible() const { return assetLibraryVisible_; }

    // Mouse click detection and panel switching
    void handleMouseClick(int x, int y);

    // Main draw function - delegates to appropriate panels
    void draw();

    // Getters for current state
    int getCurrentPanelIndex() const { return static_cast<int>(currentPanel); }
    const char* getCurrentPanelName() const;

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
    
    // State - Panel enum (DEPRECATED: Kept for backward compatibility)
    Panel currentPanel = Panel::CLOCK;
    Panel lastPanel = Panel::COUNT;  // Invalid, triggers focus on first draw
    bool fileBrowserVisible_ = false;  // FileBrowser visibility state
    bool consoleVisible_ = false;  // Console visibility state
    bool assetLibraryVisible_ = false;  // AssetLibrary visibility state

    // Panel names for debugging/logging
    static constexpr std::array<const char*, 7> PANEL_NAMES = {{
        "Clock ",
        "Audio Output",
        "Tracker Sequencer",
        "Media Pool",
        "File Browser",
        "Console",
        "Asset Library"
    }};

    // Private draw methods for each panel
    void drawClockPanel();
    
    // Generic method to draw all visible module panels (handles all module types)
    void drawModulePanels();
    void drawFileBrowserPanel();  // Draw FileBrowser panel
    void drawConsolePanel();  // Draw Console panel
    void drawAssetLibraryPanel();  // Draw AssetLibrary panel

    // Helper to set focus when panel changes (not every frame)
    void setFocusIfChanged();
    
    // Modular focus outline system - call from within window Begin/End context
    void drawWindowOutline();
};

