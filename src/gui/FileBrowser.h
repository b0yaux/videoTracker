#pragma once

#include "ofxImGui.h"
#include "gui/GUIConstants.h"
#include "MediaPlayer.h"
#include <string>
#include <vector>
#include <set>
#include <filesystem>
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
    
    // Selection state
    std::vector<std::string> selectedFiles_;
    std::string previewFile_;  // Currently previewed file
    int lastSelectedIndex_ = -1;  // Last selected file index (for shift-click range selection)
    
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
    
    // Path utilities (cross-platform)
    std::string normalizePath(const std::string& path) const;
    std::string getParentPath(const std::string& path) const;
    bool pathExists(const std::string& path) const;
    bool isDirectory(const std::string& path) const;
};

