#pragma once

#include <string>
#include <vector>
#include "ofJson.h"
#include "ofFileUtils.h"

/**
 * ProjectManager - Manages project directory structure and multiple sessions
 * 
 * Project Structure:
 *   MyProject/
 *   ├── .project.json              # Project metadata
 *   ├── session_25-11-21.json      # Session files (flat structure)
 *   ├── experiment244.json         # Custom named sessions
 *   └── Assets/                     # Shared assets directory
 *       ├── Voices/                 # Custom asset folders
 *       ├── Drums/
 *       └── .assetindex.json
 * 
 * Features:
 * - Project creation and management
 * - Multiple sessions per project (flat structure in project root)
 * - Asset directory management
 * - Project metadata storage
 */
class ProjectManager {
public:
    ProjectManager();
    ~ProjectManager() = default;
    
    /**
     * Create a new project
     * @param projectPath Full path where project should be created
     * @param projectName Name of the project
     * @return true if successful, false otherwise
     */
    bool createProject(const std::string& projectPath, const std::string& projectName);
    
    /**
     * Open an existing project
     * @param projectPath Full path to project directory (must contain .project.json)
     * @return true if successful, false otherwise
     */
    bool openProject(const std::string& projectPath);
    
    /**
     * Close current project (clears project state)
     */
    void closeProject();
    
    /**
     * Check if a project is currently open
     */
    bool isProjectOpen() const { return !projectRoot_.empty(); }
    
    /**
     * Get project root directory
     */
    std::string getProjectRoot() const { return projectRoot_; }
    
    /**
     * Get project name
     */
    std::string getProjectName() const { return projectName_; }
    
    /**
     * Get assets directory (ProjectRoot/Assets/)
     */
    std::string getAssetsDirectory() const;
    
    /**
     * Get or create a custom asset folder within Assets/
     * @param folderName Name of the folder (e.g., "Voices", "Drums")
     * @return Full path to the folder, empty string on error
     */
    std::string getOrCreateAssetFolder(const std::string& folderName);
    
    /**
     * List all session files in project root
     * @return Vector of session file names (without path, e.g., "session_25-11-21.json")
     */
    std::vector<std::string> listSessions() const;
    
    /**
     * Get full path to a session file
     * @param sessionName Session file name (with or without .json extension)
     * @return Full path to session file, empty string if not found
     */
    std::string getSessionPath(const std::string& sessionName) const;
    
    /**
     * Create a new session file (empty, ready for SessionManager to populate)
     * @param sessionName Name of session (will add .json if not present)
     * @return true if successful, false otherwise
     */
    bool createSessionFile(const std::string& sessionName);
    
    /**
     * Delete a session file
     * @param sessionName Session file name
     * @return true if successful, false otherwise
     */
    bool deleteSession(const std::string& sessionName);
    
    /**
     * Rename a session file
     * @param oldName Current session file name
     * @param newName New session file name
     * @return true if successful, false otherwise
     */
    bool renameSession(const std::string& oldName, const std::string& newName);
    
    /**
     * Get default session name (for new projects)
     * Format: session_YY-MM-DD.json
     */
    std::string generateDefaultSessionName() const;
    
    /**
     * Get project metadata
     */
    ofJson getProjectMetadata() const;
    
    /**
     * Set project metadata
     */
    void setProjectMetadata(const ofJson& metadata);
    
    /**
     * Save project metadata to .project.json
     */
    bool saveProjectMetadata();
    
    /**
     * Load project metadata from .project.json
     */
    bool loadProjectMetadata();
    
private:
    std::string projectRoot_;
    std::string projectName_;
    std::string projectConfigPath_;  // ProjectRoot/.project.json
    ofJson projectMetadata_;
    
    /**
     * Initialize project directory structure
     * Creates Assets/ directory if it doesn't exist
     */
    void initializeProjectStructure();
    
    /**
     * Validate that project structure is correct
     */
    bool validateProjectStructure() const;
    
    /**
     * Normalize session name (add .json if missing)
     */
    std::string normalizeSessionName(const std::string& sessionName) const;
    
    /**
     * Check if a file is a session file (ends with .json, not .project.json)
     */
    bool isSessionFile(const std::string& filename) const;
};

