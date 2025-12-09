#include "ProjectManager.h"
#include "ofLog.h"
#include "ofFileUtils.h"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>

//--------------------------------------------------------------
ProjectManager::ProjectManager() {
    // Project starts closed
}

//--------------------------------------------------------------
bool ProjectManager::createProject(const std::string& projectPath, const std::string& projectName) {
    if (projectPath.empty() || projectName.empty()) {
        ofLogError("ProjectManager") << "Cannot create project: path or name is empty";
        return false;
    }
    
    // Normalize project path
    std::string normalizedPath = ofFilePath::getAbsolutePath(projectPath);
    
    // Check if directory exists
    ofDirectory dir(normalizedPath);
    if (dir.exists()) {
        // Check if it's already a project
        std::string existingConfig = ofFilePath::join(normalizedPath, ".project.json");
        if (ofFile(existingConfig).exists()) {
            ofLogWarning("ProjectManager") << "Directory already contains a project: " << normalizedPath;
            return openProject(normalizedPath);
        }
        
        // Directory exists but not a project - check if empty
        dir.listDir();
        if (dir.size() > 0) {
            ofLogError("ProjectManager") << "Cannot create project: directory is not empty: " << normalizedPath;
            return false;
        }
    } else {
        // Create directory
        if (!dir.create(true)) {
            ofLogError("ProjectManager") << "Failed to create project directory: " << normalizedPath;
            return false;
        }
    }
    
    // Set project root
    projectRoot_ = normalizedPath;
    projectName_ = projectName;
    projectConfigPath_ = ofFilePath::join(projectRoot_, ".project.json");
    
    // Initialize project structure
    initializeProjectStructure();
    
    // Create project metadata
    projectMetadata_ = ofJson::object();
    projectMetadata_["version"] = "1.0";
    projectMetadata_["name"] = projectName_;
    
    // Timestamp
    auto now = std::time(nullptr);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%SZ");
    projectMetadata_["created"] = ss.str();
    projectMetadata_["modified"] = ss.str();
    projectMetadata_["defaultSession"] = "";
    projectMetadata_["sessions"] = ofJson::array();
    
    // Save project metadata
    if (!saveProjectMetadata()) {
        ofLogError("ProjectManager") << "Failed to save project metadata";
        closeProject();
        return false;
    }
    
    ofLogNotice("ProjectManager") << "Project created: " << projectName_ << " at " << projectRoot_;
    return true;
}

//--------------------------------------------------------------
bool ProjectManager::openProject(const std::string& projectPath) {
    if (projectPath.empty()) {
        ofLogError("ProjectManager") << "Cannot open project: path is empty";
        return false;
    }
    
    // Normalize project path
    std::string normalizedPath = ofFilePath::getAbsolutePath(projectPath);
    
    // Check if .project.json exists
    std::string configPath = ofFilePath::join(normalizedPath, ".project.json");
    if (!ofFile(configPath).exists()) {
        ofLogError("ProjectManager") << "Not a valid project directory (missing .project.json): " << normalizedPath;
        return false;
    }
    
    // Validate project structure
    projectRoot_ = normalizedPath;
    projectConfigPath_ = configPath;
    
    if (!validateProjectStructure()) {
        ofLogError("ProjectManager") << "Project structure validation failed";
        closeProject();
        return false;
    }
    
    // Load project metadata
    if (!loadProjectMetadata()) {
        ofLogError("ProjectManager") << "Failed to load project metadata";
        closeProject();
        return false;
    }
    
    // Get project name from metadata
    if (projectMetadata_.contains("name") && projectMetadata_["name"].is_string()) {
        projectName_ = projectMetadata_["name"].get<std::string>();
    } else {
        // Fallback to directory name
        projectName_ = ofFilePath::getFileName(projectRoot_);
    }
    
    ofLogNotice("ProjectManager") << "Project opened: " << projectName_ << " at " << projectRoot_;
    return true;
}

//--------------------------------------------------------------
void ProjectManager::closeProject() {
    // Save metadata before closing
    if (isProjectOpen()) {
        saveProjectMetadata();
    }
    
    projectRoot_.clear();
    projectName_.clear();
    projectConfigPath_.clear();
    projectMetadata_ = ofJson::object();
    
    ofLogNotice("ProjectManager") << "Project closed";
}

//--------------------------------------------------------------
std::string ProjectManager::getAssetsDirectory() const {
    if (!isProjectOpen()) {
        return "";
    }
    return ofFilePath::join(projectRoot_, "Assets");
}

//--------------------------------------------------------------
std::string ProjectManager::getOrCreateAssetFolder(const std::string& folderName) {
    if (!isProjectOpen() || folderName.empty()) {
        return "";
    }
    
    std::string assetsDir = getAssetsDirectory();
    if (assetsDir.empty()) {
        return "";
    }
    
    std::string folderPath = ofFilePath::join(assetsDir, folderName);
    ofDirectory dir(folderPath);
    
    if (!dir.exists()) {
        if (!dir.create(true)) {
            ofLogError("ProjectManager") << "Failed to create asset folder: " << folderPath;
            return "";
        }
        ofLogNotice("ProjectManager") << "Created asset folder: " << folderPath;
    }
    
    return folderPath;
}

//--------------------------------------------------------------
std::vector<std::string> ProjectManager::listSessions() const {
    std::vector<std::string> sessions;
    
    if (!isProjectOpen()) {
        return sessions;
    }
    
    try {
        ofDirectory dir(projectRoot_);
        dir.listDir();
        
        for (int i = 0; i < dir.size(); i++) {
            std::string filename = dir.getName(i);
            
            // Check if it's a session file (ends with .json, not .project.json)
            if (isSessionFile(filename)) {
                sessions.push_back(filename);
            }
        }
        
        // Sort sessions by name
        std::sort(sessions.begin(), sessions.end());
    } catch (const std::exception& e) {
        ofLogError("ProjectManager") << "Error listing sessions: " << e.what();
    }
    
    return sessions;
}

//--------------------------------------------------------------
std::string ProjectManager::getSessionPath(const std::string& sessionName) const {
    if (!isProjectOpen() || sessionName.empty()) {
        return "";
    }
    
    std::string normalized = normalizeSessionName(sessionName);
    std::string sessionPath = ofFilePath::join(projectRoot_, normalized);
    
    // Check if file exists
    if (ofFile(sessionPath).exists()) {
        return sessionPath;
    }
    
    return "";
}

//--------------------------------------------------------------
bool ProjectManager::createSessionFile(const std::string& sessionName) {
    if (!isProjectOpen() || sessionName.empty()) {
        return false;
    }
    
    std::string normalized = normalizeSessionName(sessionName);
    std::string sessionPath = ofFilePath::join(projectRoot_, normalized);
    
    // Check if file already exists
    if (ofFile(sessionPath).exists()) {
        ofLogWarning("ProjectManager") << "Session file already exists: " << sessionPath;
        return false;
    }
    
    // Create empty session file with basic structure
    ofJson sessionJson = ofJson::object();
    sessionJson["version"] = "1.0";
    sessionJson["sessionName"] = normalized;
    sessionJson["projectRoot"] = projectRoot_;
    
    // Timestamp
    auto now = std::time(nullptr);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%SZ");
    sessionJson["metadata"] = ofJson::object();
    sessionJson["metadata"]["created"] = ss.str();
    sessionJson["metadata"]["modified"] = ss.str();
    
    // Write to file
    ofFile file(sessionPath, ofFile::WriteOnly);
    if (!file.is_open()) {
        ofLogError("ProjectManager") << "Failed to create session file: " << sessionPath;
        return false;
    }
    
    file << sessionJson.dump(4);
    file.close();
    
    // Update project metadata
    if (!projectMetadata_.contains("sessions") || !projectMetadata_["sessions"].is_array()) {
        projectMetadata_["sessions"] = ofJson::array();
    }
    
    // Add session to list if not already present
    auto& sessions = projectMetadata_["sessions"];
    bool found = false;
    for (const auto& session : sessions) {
        if (session.get<std::string>() == normalized) {
            found = true;
            break;
        }
    }
    if (!found) {
        sessions.push_back(normalized);
    }
    
    saveProjectMetadata();
    
    ofLogNotice("ProjectManager") << "Session file created: " << sessionPath;
    return true;
}

//--------------------------------------------------------------
bool ProjectManager::deleteSession(const std::string& sessionName) {
    if (!isProjectOpen() || sessionName.empty()) {
        return false;
    }
    
    std::string sessionPath = getSessionPath(sessionName);
    if (sessionPath.empty()) {
        ofLogWarning("ProjectManager") << "Session file not found: " << sessionName;
        return false;
    }
    
    // Delete file
    if (!ofFile::removeFile(sessionPath)) {
        ofLogError("ProjectManager") << "Failed to delete session file: " << sessionPath;
        return false;
    }
    
    // Update project metadata
    if (projectMetadata_.contains("sessions") && projectMetadata_["sessions"].is_array()) {
        auto& sessions = projectMetadata_["sessions"];
            std::string normalizedOld = normalizeSessionName(sessionName);
            auto it = std::remove_if(sessions.begin(), sessions.end(),
            [&sessionName, &normalizedOld](const ofJson& s) {
                std::string sessionStr = s.get<std::string>();
                return sessionStr == sessionName || sessionStr == normalizedOld;
            });
        sessions.erase(it, sessions.end());
    }
    
    saveProjectMetadata();
    
    ofLogNotice("ProjectManager") << "Session deleted: " << sessionPath;
    return true;
}

//--------------------------------------------------------------
bool ProjectManager::renameSession(const std::string& oldName, const std::string& newName) {
    if (!isProjectOpen() || oldName.empty() || newName.empty()) {
        return false;
    }
    
    std::string oldPath = getSessionPath(oldName);
    if (oldPath.empty()) {
        ofLogWarning("ProjectManager") << "Session file not found: " << oldName;
        return false;
    }
    
    std::string normalizedNew = normalizeSessionName(newName);
    std::string newPath = ofFilePath::join(projectRoot_, normalizedNew);
    
    // Check if new name already exists
    if (ofFile(newPath).exists()) {
        ofLogError("ProjectManager") << "Session file already exists: " << newPath;
        return false;
    }
    
    // Rename file
    if (!ofFile::moveFromTo(oldPath, newPath)) {
        ofLogError("ProjectManager") << "Failed to rename session file";
        return false;
    }
    
    // Update project metadata
    if (projectMetadata_.contains("sessions") && projectMetadata_["sessions"].is_array()) {
        auto& sessions = projectMetadata_["sessions"];
        for (auto& session : sessions) {
            std::string sessionName = session.get<std::string>();
            if (sessionName == oldName || sessionName == normalizeSessionName(oldName)) {
                session = normalizedNew;
                break;
            }
        }
    }
    
    saveProjectMetadata();
    
    ofLogNotice("ProjectManager") << "Session renamed: " << oldName << " -> " << normalizedNew;
    return true;
}

//--------------------------------------------------------------
std::string ProjectManager::generateDefaultSessionName() const {
    auto now = std::time(nullptr);
    std::stringstream ss;
    ss << "session_" << std::put_time(std::localtime(&now), "%y-%m-%d");
    return ss.str();
}

//--------------------------------------------------------------
ofJson ProjectManager::getProjectMetadata() const {
    return projectMetadata_;
}

//--------------------------------------------------------------
void ProjectManager::setProjectMetadata(const ofJson& metadata) {
    projectMetadata_ = metadata;
}

//--------------------------------------------------------------
bool ProjectManager::saveProjectMetadata() {
    if (!isProjectOpen() || projectConfigPath_.empty()) {
        return false;
    }
    
    // Update modified timestamp
    auto now = std::time(nullptr);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%SZ");
    projectMetadata_["modified"] = ss.str();
    
    // Write to file
    ofFile file(projectConfigPath_, ofFile::WriteOnly);
    if (!file.is_open()) {
        ofLogError("ProjectManager") << "Failed to open project config for writing: " << projectConfigPath_;
        return false;
    }
    
    file << projectMetadata_.dump(4);
    file.close();
    
    return true;
}

//--------------------------------------------------------------
bool ProjectManager::loadProjectMetadata() {
    if (!isProjectOpen() || projectConfigPath_.empty()) {
        return false;
    }
    
    if (!ofFile(projectConfigPath_).exists()) {
        ofLogError("ProjectManager") << "Project config file not found: " << projectConfigPath_;
        return false;
    }
    
    ofFile file(projectConfigPath_, ofFile::ReadOnly);
    if (!file.is_open()) {
        ofLogError("ProjectManager") << "Failed to open project config for reading: " << projectConfigPath_;
        return false;
    }
    
    std::string jsonString = file.readToBuffer().getText();
    file.close();
    
    try {
        projectMetadata_ = ofJson::parse(jsonString);
    } catch (const std::exception& e) {
        ofLogError("ProjectManager") << "Failed to parse project metadata: " << e.what();
        return false;
    }
    
    return true;
}

//--------------------------------------------------------------
void ProjectManager::initializeProjectStructure() {
    if (!isProjectOpen()) {
        return;
    }
    
    // Create Assets directory
    std::string assetsDir = getAssetsDirectory();
    ofDirectory dir(assetsDir);
    if (!dir.exists()) {
        if (dir.create(true)) {
            ofLogNotice("ProjectManager") << "Created Assets directory: " << assetsDir;
        } else {
            ofLogError("ProjectManager") << "Failed to create Assets directory: " << assetsDir;
        }
    }
}

//--------------------------------------------------------------
bool ProjectManager::validateProjectStructure() const {
    if (!isProjectOpen()) {
        return false;
    }
    
    // Check if project root exists
    if (!ofDirectory(projectRoot_).exists()) {
        ofLogError("ProjectManager") << "Project root does not exist: " << projectRoot_;
        return false;
    }
    
    // Check if .project.json exists
    if (!ofFile(projectConfigPath_).exists()) {
        ofLogError("ProjectManager") << "Project config file not found: " << projectConfigPath_;
        return false;
    }
    
    // Assets directory is optional (will be created on first use)
    
    return true;
}

//--------------------------------------------------------------
std::string ProjectManager::normalizeSessionName(const std::string& sessionName) const {
    std::string normalized = sessionName;
    
    // Add .json extension if missing
    if (normalized.length() < 5 || 
        normalized.substr(normalized.length() - 5) != ".json") {
        normalized += ".json";
    }
    
    return normalized;
}

//--------------------------------------------------------------
bool ProjectManager::isSessionFile(const std::string& filename) const {
    // Must end with .json
    if (filename.length() < 5 || filename.substr(filename.length() - 5) != ".json") {
        return false;
    }
    
    // Exclude project config and other system files
    if (filename == ".project.json") {
        return false;
    }
    
    // Exclude module layout files (these are not sessions)
    if (filename == "module_layouts.json") {
        return false;
    }
    
    // Exclude backup and migrated files
    if (filename.find(".backup") != std::string::npos || 
        filename.find(".migrated") != std::string::npos) {
        return false;
    }
    
    return true;
}

