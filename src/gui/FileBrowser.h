#pragma once

#include <imgui.h>
#include "gui/GUIConstants.h"
#include "MediaPlayer.h"
#include <string>
#include <vector>
#include <set>
#include <memory>
#include <functional>

/**
 * FileBrowser - Utility panel for navigating filesystem and importing media
 * 
 * Features:
 * - Navigate filesystem
 * - Preview media files (video thumbnail, audio waveform)
 * - Select files for import
 * - Fully modular - uses callback for import (no module-specific dependencies)
 * 
 * Note: This is a utility panel, not a module. It doesn't extend ModuleGUI.
 */
class FileBrowser {
public:
    // Import callback: (selectedFiles, targetModuleInstanceName) -> void
    using ImportCallback = std::function<void(const std::vector<std::string>&, const std::string&)>;
    
    // Get available module instances callback: () -> vector of instance names
    using GetInstancesCallback = std::function<std::vector<std::string>()>;
    
    FileBrowser();
    ~FileBrowser() = default;
    
    // Set callbacks for import functionality
    void setImportCallback(ImportCallback callback) { importCallback_ = callback; }
    void setGetInstancesCallback(GetInstancesCallback callback) { getInstancesCallback_ = callback; }
    
    // Main draw function - draws the panel content
    // Window is created by ViewManager, this just draws the content
    void draw();
    
private:
    // File system navigation
    std::string currentPath_;
    std::vector<std::string> directories_;
    std::vector<std::string> files_;
    std::string searchFilter_;
    bool directoryInitialized_ = false;  // Track if directory has been loaded
    std::string lastSyncedPath_;  // Track last synced path for navigation bar
    
    // Selection state
    std::set<std::string> selectedFiles_;  // Changed to set for faster lookup
    std::string previewFile_;  // Currently previewed file
    std::string lastSelectedPath_;  // Last selected file path (for shift-click range selection)
    
    // Media preview
    std::unique_ptr<MediaPlayer> previewPlayer_;  // For previewing selected media
    bool previewLoaded_;
    
    // Import target
    std::string targetModuleInstance_;  // Instance name (e.g., "pool1")
    
    // Allowed extensions for media files
    std::set<std::string> mediaExtensions_;
    
    // Import callbacks (set by caller)
    ImportCallback importCallback_;
    GetInstancesCallback getInstancesCallback_;
    
    // Helper methods
    void refreshDirectory();
    void navigateToPath(const std::string& path);
    void navigateUp();
    bool isValidMediaFile(const std::string& filename) const;
    std::string formatFileSize(size_t bytes) const;
    
    // UI sections
    void drawNavigationBar();
    void drawFileList();
    void drawMediaPreview();
    void drawImportControls();
    
    // Tree view methods
    void drawDirectoryTree(const std::string& path, int depth = 0);
    void drawDirectoryNode(const std::string& fullPath, const std::string& name, int depth);
    void drawFileNode(const std::string& fullPath, const std::string& name, int depth);
    
    // Selection handling
    void handleDirectoryClick(const std::string& fullPath);
    void handleFileClick(const std::string& fullPath);
    std::vector<std::string> getSelectedMediaFiles() const;  // Get only media files from selection
    
    // Path utilities (cross-platform)
    std::string normalizePath(const std::string& path) const;
    std::string getParentPath(const std::string& path) const;
    bool pathExists(const std::string& path) const;
    bool isDirectory(const std::string& path) const;
};

