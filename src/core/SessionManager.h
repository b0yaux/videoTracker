#pragma once

#include <string>
#include <vector>
#include <functional>
#include "ofJson.h"

// Forward declarations
class Clock;
class ModuleRegistry;
class ModuleFactory;
class ParameterRouter;
class ConnectionManager;
class ViewManager;
class GUIManager;
class ProjectManager;
class ModuleGUI;

/**
 * SessionManager - Central coordinator for saving/loading complete application sessions
 * 
 * Features:
 * - Saves/loads all modules, routing, clock, and GUI state
 * - Supports modular architecture (no hardcoded module types)
 * - Version-aware for future migration support
 * - Handles partial loading gracefully
 * - Integrates with ProjectManager for project-aware session management
 */
class SessionManager {
public:
    // Default constructor (for member initialization)
    SessionManager() : projectManager_(nullptr), clock(nullptr), registry(nullptr), factory(nullptr), router(nullptr), connectionManager_(nullptr), viewManager_(nullptr), guiManager_(nullptr), pendingImGuiState_(""), pendingVisibilityState_(ofJson::object()), postLoadCallback_(nullptr), projectOpenedCallback_(nullptr) {}
    
    // Constructor with all dependencies (including ProjectManager)
    SessionManager(
        ProjectManager* projectManager,
        Clock* clock,
        ModuleRegistry* registry,
        ModuleFactory* factory,
        ParameterRouter* router,
        ConnectionManager* connectionManager = nullptr,  // Optional: for unified connection management
        ViewManager* viewManager = nullptr  // Optional: for GUI state
    );
    
    /**
     * Set ConnectionManager (can be called after construction)
     */
    void setConnectionManager(ConnectionManager* connectionManager) { connectionManager_ = connectionManager; }
    
    /**
     * Save complete session to file
     * @param sessionName Session name (if ProjectManager is set, uses project path; otherwise uses full path)
     * @return true if successful, false otherwise
     */
    bool saveSession(const std::string& sessionName);
    
    /**
     * Load complete session from file
     * @param sessionName Session name (if ProjectManager is set, uses project path; otherwise uses full path)
     * @return true if successful, false otherwise
     */
    bool loadSession(const std::string& sessionName);
    
    /**
     * Save session to a specific full path (bypasses ProjectManager)
     * @param fullPath Full path to session file
     * @return true if successful, false otherwise
     */
    bool saveSessionToPath(const std::string& fullPath);
    
    /**
     * Load session from a specific full path (bypasses ProjectManager)
     * @param fullPath Full path to session file
     * @return true if successful, false otherwise
     */
    bool loadSessionFromPath(const std::string& fullPath);
    
    /**
     * Get current session name
     */
    std::string getCurrentSessionName() const { return currentSessionName_; }
    
    /**
     * Set ViewManager (can be called after construction)
     */
    void setViewManager(ViewManager* viewManager) { viewManager_ = viewManager; }
    
    /**
     * Set GUIManager (can be called after construction)
     */
    void setGUIManager(GUIManager* guiManager) { guiManager_ = guiManager; }
    
    /**
     * Get current session as JSON (for inspection/debugging)
     * @return JSON object representing current session
     */
    ofJson serializeAll() const;
    
    /**
     * Deserialize session from JSON (for testing/custom loading)
     * @param json JSON object representing session
     * @return true if successful, false otherwise
     */
    bool deserializeAll(const ofJson& json);
    
    /**
     * Get session version
     */
    static constexpr const char* SESSION_VERSION = "1.0";
    
    /**
     * Set callback to be called after session load completes
     * Useful for re-initializing audio streams, viewports, etc.
     */
    void setPostLoadCallback(std::function<void()> callback) { postLoadCallback_ = callback; }
    
    /**
     * Set callback to be called when a project is opened (from session or otherwise)
     * Useful for notifying FileBrowser and AssetLibrary
     */
    void setProjectOpenedCallback(std::function<void()> callback) { projectOpenedCallback_ = callback; }
    
    /**
     * Load pending ImGui state (call this after ImGui is initialized)
     * This should be called from the post-load callback or after setupGUI()
     */
    void loadPendingImGuiState();
    
    /**
     * Restore module instance visibility state (call this after syncWithRegistry())
     * This should be called from the post-load callback after GUIs are created
     */
    void restoreVisibilityState();

    /**
     * Update ImGui state in current session (call this after adding new modules)
     * This ensures newly added module windows are included when session is saved
     */
    void updateImGuiStateInSession();

    /**
     * Initialize project and session on application startup
     * Handles project open/create and default session loading
     * @param dataPath Path to data directory (typically from ofToDataPath("", true))
     * @return true if a session was loaded, false otherwise
     */
    bool initializeProjectAndSession(const std::string& dataPath);
    
    /**
     * Ensure default modules exist in the registry
     * Creates default module types if registry is empty
     * @param defaultModuleTypes Vector of module type names to create (e.g., {"TrackerSequencer", "MediaPool"})
     * @return true if modules were created or already exist, false on error
     */
    bool ensureDefaultModules(const std::vector<std::string>& defaultModuleTypes = {"TrackerSequencer", "MediaPool"});
    
    /**
     * Setup GUI coordination
     * Initializes GUIManager, syncs with registry, configures modules, and loads ImGui state
     * @param guiManager GUIManager instance to setup
     * @return true if setup succeeded, false on error
     */
    bool setupGUI(class GUIManager* guiManager);
    
    /**
     * Enable auto-save functionality
     * @param intervalSeconds Auto-save interval in seconds (default: 30.0)
     * @param onUpdateWindowTitle Callback to update window title after save
     */
    void enableAutoSave(float intervalSeconds = 30.0f, std::function<void()> onUpdateWindowTitle = nullptr);
    
    /**
     * Disable auto-save functionality
     */
    void disableAutoSave() { autoSaveEnabled_ = false; }
    
    /**
     * Update auto-save timer (call from ofApp::update())
     * Performs periodic auto-save if enabled
     */
    void update();
    
    /**
     * Auto-save session before exit (call from ofApp::exit())
     * @return true if save succeeded, false otherwise
     */
    bool autoSaveOnExit();

private:
    ProjectManager* projectManager_;
    Clock* clock;
    ModuleRegistry* registry;
    ModuleFactory* factory;
    ParameterRouter* router;
    ConnectionManager* connectionManager_;  // For unified connection management
    ViewManager* viewManager_;  // For GUI state
    GUIManager* guiManager_;  // For visibility state
    std::string currentSessionName_;
    std::function<void()> postLoadCallback_;  // Called after session load completes
    std::function<void()> projectOpenedCallback_;  // Called when a project is opened
    std::string pendingImGuiState_;  // ImGui state to load after ImGui is initialized
    ofJson pendingVisibilityState_;  // Visibility state to restore after GUIs are created
    
    // Auto-save state
    bool autoSaveEnabled_ = false;
    float autoSaveInterval_ = 30.0f;
    float lastAutoSave_ = 0.0f;
    bool saveInProgress_ = false;
    std::function<void()> onUpdateWindowTitle_;  // Callback to update window title after save
    
    /**
     * Resolve session path (uses ProjectManager if available, otherwise assumes full path)
     */
    std::string resolveSessionPath(const std::string& sessionName) const;
    
    /**
     * Migrate legacy format to new format
     */
    bool migrateLegacyFormat(const ofJson& json);
    
    /**
     * Migrate legacy state files into unified session format
     * Consolidates tracker_sequencer_state.json, sequencer_state.json, 
     * media_settings.json, and module_layouts.json into session.json
     * @param sessionPath Path to session file to create/update
     * @return true if migration was successful or no legacy files found
     */
    bool migrateLegacyFiles(const std::string& sessionPath);
    
    /**
     * Restore mixer connections after all modules are loaded
     * @param modulesJson Array of module JSON data
     */
    void restoreMixerConnections(const ofJson& modulesJson);
};

