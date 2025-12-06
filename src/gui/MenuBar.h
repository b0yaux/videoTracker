#pragma once
#include "ofMain.h"
#include <imgui.h>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

// Forward declarations
class ViewManager;
class AddMenu;
class SessionManager;
class ProjectManager;
class AssetLibrary;
class FileBrowser;

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
        std::function<void()> onToggleAssetLibrary = nullptr,
        std::function<void()> onToggleDemoWindow = nullptr,
        // Session menu callbacks
        std::function<void()> onSaveSession = nullptr,
        std::function<void()> onSaveSessionAs = nullptr,
        std::function<void()> onOpenSession = nullptr,
        std::function<void(const std::string& sessionPath)> onOpenRecentSession = nullptr,
        std::function<void()> onNewSession = nullptr,
        std::function<std::string()> getCurrentSessionName = nullptr,
        // Project menu callbacks
        std::function<void()> onOpenProject = nullptr,
        std::function<void()> onNewProject = nullptr,
        std::function<void()> onCloseProject = nullptr,
        std::function<std::string()> getCurrentProjectName = nullptr,
        std::function<std::vector<std::string>()> getProjectSessions = nullptr,
        std::function<void(const std::string& sessionName)> onOpenProjectSession = nullptr,
        // File menu callbacks (for imports)
        std::function<void()> onImportFile = nullptr,
        std::function<void()> onImportFolder = nullptr
    );
    
    // Recent sessions management
    void addToRecentSessions(const std::string& sessionPath);
    void loadRecentSessions();
    void saveRecentSessions();

    // Called every frame in ofApp::draw()
    // Returns true if menu bar is active (prevents input below)
    void draw();
    


    // Accessor for help popup state (optional, for external management)
    bool isHelpPopupOpen() const { return showControlsHelp; }
    void closeHelpPopup() { showControlsHelp = false; }
    
    // Accessor for Add Menu state
    bool isAddMenuOpen() const;
    void openAddMenu(float mouseX = -1, float mouseY = -1);
    void closeAddMenu();
    
    /**
     * Set ViewManager reference (for checking panel visibility state)
     */
    void setViewManager(ViewManager* viewManager) { viewManager_ = viewManager; }
    
    /**
     * Set AddMenu reference (for handling Add Menu)
     */
    void setAddMenu(AddMenu* addMenu) { addMenu_ = addMenu; }
    
    /**
     * Setup with dependencies - creates callbacks internally (Phase 13.6)
     * This reduces callback setup code in ofApp
     * @param sessionManager SessionManager for session operations
     * @param projectManager ProjectManager for project operations
     * @param assetLibrary AssetLibrary for import operations
     * @param viewManager ViewManager for panel visibility
     * @param fileBrowser FileBrowser for project directory management
     * @param onAddModule Callback for adding modules
     * @param onSaveLayout Callback for saving layout
     * @param onLoadLayout Callback for loading layout
     * @param onUpdateWindowTitle Callback for updating window title
     * @param onToggleDemoWindow Callback for toggling demo window (takes bool* to toggle)
     */
    void setupWithDependencies(
        class SessionManager* sessionManager,
        class ProjectManager* projectManager,
        class AssetLibrary* assetLibrary,
        ViewManager* viewManager,
        class FileBrowser* fileBrowser,
        std::function<void(const std::string&)> onAddModule,
        std::function<void()> onSaveLayout,
        std::function<void()> onLoadLayout,
        std::function<void()> onUpdateWindowTitle,
        bool* showDemoWindowPtr
    );

private:
    // Callback functions
    std::function<void()> onSavePattern;
    std::function<void()> onLoadPattern;
    std::function<void()> onSaveLayout;
    std::function<void()> onLoadLayout;
    std::function<void(const std::string& moduleType)> onAddModule;
    std::function<void()> onToggleFileBrowser;
    std::function<void()> onToggleConsole;
    std::function<void()> onToggleAssetLibrary;
    std::function<void()> onToggleDemoWindow;
    
    // Session menu callbacks
    std::function<void()> onSaveSession;
    std::function<void()> onSaveSessionAs;
    std::function<void()> onOpenSession;
    std::function<void(const std::string& sessionPath)> onOpenRecentSession;
    std::function<void()> onNewSession;
    std::function<std::string()> getCurrentSessionName;
    
    // Project menu callbacks
    std::function<void()> onOpenProject;
    std::function<void()> onNewProject;
    std::function<void()> onCloseProject;
    std::function<std::string()> getCurrentProjectName;
    std::function<std::vector<std::string>()> getProjectSessions;
    std::function<void(const std::string& sessionName)> onOpenProjectSession;
    
    // File menu callbacks (for imports)
    std::function<void()> onImportFile;
    std::function<void()> onImportFolder;

    // UI state
    bool showControlsHelp = false;
    
    // Recent sessions tracking
    std::vector<std::string> recentSessions_;
    static constexpr size_t MAX_RECENT_SESSIONS = 10;
    std::string recentSessionsPath_;
    
    // ViewManager reference (for checking panel visibility)
    ViewManager* viewManager_ = nullptr;
    
    // AddMenu reference (for handling menu)
    AddMenu* addMenu_ = nullptr;

    // Private helper methods for each menu section
    void drawFileMenu();      // Import File/Folder
    void drawSessionMenu();   // Session operations
    void drawProjectMenu();   // Project operations
    void drawViewMenu();
    void drawAddMenu();
    void drawLayoutMenu();
    void drawHelpMenu();
};

