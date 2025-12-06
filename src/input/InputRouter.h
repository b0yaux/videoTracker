#pragma once
#include "ofMain.h"
#include <functional>

class Clock;
class ViewManager;
class Console;
class CommandBar;
class ModuleRegistry;
class GUIManager;
class AddMenu;

class InputRouter {
public:
    InputRouter();
    ~InputRouter() = default;

    // Setup with references to controllable systems (Phase 10.1)
    void setup(
        Clock* clock,
        ModuleRegistry* registry,
        GUIManager* guiManager,
        ViewManager* viewManager,
        Console* console,
        CommandBar* commandBar
    );
    
    // Set AddMenu reference for handling Add Menu
    void setAddMenu(AddMenu* addMenu);
    
    // Set callbacks for session save/load (called by 'S' key)
    void setSessionCallbacks(
        std::function<void()> onSaveSession,
        std::function<void()> onLoadSession
    );
    
    // Set callbacks for File menu shortcuts (Cmd+S, Cmd+Shift+S, Cmd+O, Cmd+Shift+O)
    void setFileMenuCallbacks(
        std::function<void()> onSave,
        std::function<void()> onSaveAs,
        std::function<void()> onOpen,
        std::function<void()> onOpenRecent
    );
    
    /**
     * Setup with dependencies and callbacks (Phase 13.6)
     * @param clock Clock for timing
     * @param registry ModuleRegistry for module access
     * @param guiManager GUIManager for GUI access
     * @param viewManager ViewManager for panel management
     * @param console Console for console access
     * @param sessionManager SessionManager for session operations
     * @param projectManager ProjectManager for project operations
     * @param menuBar MenuBar for recent sessions
     * @param onUpdateWindowTitle Callback for updating window title
     * @param currentStep Pointer to current step state
     * @param lastTriggeredStep Pointer to last triggered step state
     * @param showGUI Pointer to show GUI state
     */
    void setupWithCallbacks(
        Clock* clock,
        ModuleRegistry* registry,
        GUIManager* guiManager,
        ViewManager* viewManager,
        Console* console,
        CommandBar* commandBar,
        class SessionManager* sessionManager,
        class ProjectManager* projectManager,
        std::function<void()> onUpdateWindowTitle,
        int* currentStep,
        int* lastTriggeredStep,
        bool* showGUI
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

    void update();  // Check for Tab/Shift+Tab for panel navigation (when not in text fields)
    
    /**
     * Handle drag and drop events (Phase 13.8)
     * Routes file drops to appropriate modules or AssetLibrary
     * @param dragInfo Drag info containing files and position
     * @param assetLibrary AssetLibrary for importing files
     * @param projectManager ProjectManager for checking project assets
     */
    void handleDragEvent(ofDragInfo dragInfo, class AssetLibrary* assetLibrary, class ProjectManager* projectManager);

private:
    // System references (Phase 10.1)
    Clock* clock = nullptr;
    ModuleRegistry* registry = nullptr;
    GUIManager* guiManager = nullptr;
    ViewManager* viewManager = nullptr;
    Console* console = nullptr;
    CommandBar* commandBar = nullptr;
    AddMenu* addMenu = nullptr;
    
    // Focus-based routing helpers (InputRouter refactoring)
    class ModuleGUI* getFocusedGUI() const;
    class Module* getModuleForGUI(class ModuleGUI* gui) const;
    

    // State references (optional - can be nullptr)
    // Note: Play state comes from Clock reference (single source of truth)
    int* currentStep = nullptr;
    int* lastTriggeredStep = nullptr;
    bool* showGUI = nullptr;

    // Session save/load callbacks
    std::function<void()> onSaveSession;
    std::function<void()> onLoadSession;
    
    // File menu callbacks
    std::function<void()> onSave;
    std::function<void()> onSaveAs;
    std::function<void()> onOpen;
    std::function<void()> onOpenRecent;

    // Keyboard capture state
    bool imGuiCapturingKeyboard = false;

    // Handler methods for different input categories
    bool handleGlobalShortcuts(int key);

    // Helper to check ImGui capture state
    void updateImGuiCaptureState();

    // Logging helper
    void logKeyPress(int key, const char* context);
};

