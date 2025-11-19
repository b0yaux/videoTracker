#include "FileBrowser.h"
#include "gui/MediaPreview.h"
#include "gui/GUIConstants.h"
#include "ofFileUtils.h"
#include "ofSystemUtils.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

// Static storage for drag & drop payload (persists across frames during drag)
// We serialize file paths into a single string with null separators for drag & drop
// ImGui's drag drop payload copies data, so we can't use pointers - we need actual data
static std::string g_dragFilesPayload;

FileBrowser::FileBrowser() 
    : currentPath_(ofFilePath::getUserHomeDir()), previewLoaded_(false), directoryInitialized_(false) {
    
    // Initialize media extensions
    mediaExtensions_ = {
        ".mov", ".mp4", ".avi", ".mkv", ".webm", ".hap",  // Video
        ".wav", ".mp3", ".aiff", ".aif", ".m4a"           // Audio
    };
    
    // Initialize preview player
    previewPlayer_ = std::make_unique<MediaPlayer>();
    
    // DON'T call refreshDirectory() here - defer until first draw
    // This prevents blocking startup if home directory has many files
}

void FileBrowser::draw() {
    // Lazy initialization - only list directory on first draw
    if (!directoryInitialized_) {
        refreshDirectory();
        directoryInitialized_ = true;
    }
    
    // Navigation bar (path + search)
    drawNavigationBar();
    
    ImGui::Separator();
    ImGui::Spacing();
    
    // File list - takes remaining space
    float availableHeight = ImGui::GetContentRegionAvail().y;
    if (availableHeight < 50.0f) availableHeight = 50.0f; // Minimum height
    
    ImGui::BeginChild("FileList", ImVec2(0, availableHeight), true);
    drawFileList();
    ImGui::EndChild();
}

void FileBrowser::refreshDirectory() {
    directories_.clear();
    files_.clear();
    
    if (!pathExists(currentPath_) || !isDirectory(currentPath_)) {
        return;
    }
    
    try {
        ofDirectory dir(currentPath_);
        dir.listDir();
        
        for (int i = 0; i < dir.size(); i++) {
            std::string path = dir.getPath(i);
            std::string name = dir.getName(i);
            
            // Skip hidden files on macOS/Linux
            if (name[0] == '.') continue;
            
            if (isDirectory(path)) {
                directories_.push_back(name);
            } else {
                files_.push_back(name);
            }
        }
        
        // Sort directories and files
        std::sort(directories_.begin(), directories_.end());
        std::sort(files_.begin(), files_.end());
    } catch (const std::exception& e) {
        ofLogError("FileBrowser") << "Error refreshing directory: " << e.what();
    }
}

void FileBrowser::navigateToPath(const std::string& path) {
    std::string normalized = normalizePath(path);
    if (pathExists(normalized) && isDirectory(normalized)) {
        // Only navigate if path actually changed
        if (normalized != currentPath_) {
            currentPath_ = normalized;
            lastSelectedPath_.clear();  // Reset selection when navigating
            // Don't clear selection when navigating - allow cross-folder selection
            refreshDirectory();
            directoryInitialized_ = true;  // Mark as initialized
            // Clear preview when navigating
            previewFile_.clear();
            previewLoaded_ = false;
            if (previewPlayer_) {
                previewPlayer_->stop();
            }
        }
    }
}

void FileBrowser::navigateUp() {
    std::string parent = getParentPath(currentPath_);
    if (!parent.empty() && parent != currentPath_) {
        navigateToPath(parent);
    }
}

bool FileBrowser::isValidMediaFile(const std::string& filename) const {
    std::string ext = ofToLower(ofFilePath::getFileExt(filename));
    if (ext.empty()) return false;
    
    // Add dot if not present
    if (ext[0] != '.') {
        ext = "." + ext;
    }
    
    return mediaExtensions_.find(ext) != mediaExtensions_.end();
}

std::string FileBrowser::formatFileSize(size_t bytes) const {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unitIndex < 3) {
        size /= 1024.0;
        unitIndex++;
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << size << " " << units[unitIndex];
    return oss.str();
}

void FileBrowser::drawNavigationBar() {
    // Path bar - compact, handles narrow width
    ImGui::Text("Path:");
    ImGui::SameLine();
    
    // Always sync path buffer with current path to prevent desynchronization
    static char pathBuffer[1024] = {0};
    
    // Update buffer if path changed externally (e.g., from navigation)
    if (currentPath_ != lastSyncedPath_) {
        strncpy(pathBuffer, currentPath_.c_str(), sizeof(pathBuffer) - 1);
        pathBuffer[sizeof(pathBuffer) - 1] = '\0';
        lastSyncedPath_ = currentPath_;
    }
    
    float pathInputWidth = ImGui::GetContentRegionAvail().x - 60.0f; // Reserve space for buttons
    if (pathInputWidth < 80.0f) pathInputWidth = 80.0f;
    
    ImGui::SetNextItemWidth(pathInputWidth);
    if (ImGui::InputText("##Path", pathBuffer, sizeof(pathBuffer), 
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        navigateToPath(std::string(pathBuffer));
        // Update synced path after navigation
        lastSyncedPath_ = currentPath_;
    }
    
    ImGui::SameLine();
    if (ImGui::Button("^", ImVec2(20, 0))) {
        navigateUp();
        // Force refresh after navigation
        directoryInitialized_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("R", ImVec2(20, 0))) {
        refreshDirectory();
        directoryInitialized_ = true;  // Mark as refreshed
    }
    
    // Search bar
    ImGui::Text("Search:");
    ImGui::SameLine();
    char filterBuffer[256];
    strncpy(filterBuffer, searchFilter_.c_str(), sizeof(filterBuffer) - 1);
    filterBuffer[sizeof(filterBuffer) - 1] = '\0';
    
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputText("##Filter", filterBuffer, sizeof(filterBuffer))) {
        searchFilter_ = std::string(filterBuffer);
    }
}

void FileBrowser::drawFileList() {
    // Use table for columns: Name | Type | Size
    // Fixed width for Type and Size columns - they will be cropped when window is narrow
    // Name column stretches to fill remaining space
    // Horizontal scrolling enabled so fixed columns scroll off-screen when window is narrow
    // Note: With ScrollX, WidthStretch requires inner_width to be specified
    ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerV |
                                 ImGuiTableFlags_ScrollY |
                                 ImGuiTableFlags_ScrollX |
                                 ImGuiTableFlags_Resizable |
                                 ImGuiTableFlags_Reorderable |
                                 ImGuiTableFlags_Hideable;  // Enable column visibility toggling via context menu
    
    // Calculate inner width: fixed columns (80 + 100) + minimum Name width (200)
    // This ensures fixed columns maintain their width and Name can stretch
    // When window is narrower, Type and Size will scroll off-screen (cropped)
    float minInnerWidth = 80.0f + 100.0f + 200.0f; // Type + Size + min Name
    float availableWidth = ImGui::GetContentRegionAvail().x;
    // Use the larger of minimum or available width, so Name can stretch when space is available
    float innerWidth = std::max(minInnerWidth, availableWidth);
    
    if (ImGui::BeginTable("FileList", 3, tableFlags, ImVec2(0, 0), innerWidth)) {
        // Set column widths: Name stretches, Type and Size are fixed
        // When window is narrow, Type and Size will scroll off-screen (cropped)
        // Name column cannot be hidden (NoHide) as it's the most important column
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 1.0f);  // Stretch to fill available space, cannot be hidden
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);   // Fixed width, can be hidden via context menu
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100.0f);  // Fixed width, can be hidden via context menu
        ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row
        ImGui::TableHeadersRow();
        
        // Draw parent directory ".." - always show unless at root "/"
        // navigateUp() will handle edge cases safely
        if (!currentPath_.empty() && currentPath_ != "/") {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            
            ImGui::PushID("..");
            if (ImGui::Selectable("..", false, ImGuiSelectableFlags_SpanAllColumns)) {
                navigateUp();
                directoryInitialized_ = false;  // Force refresh
            }
            ImGui::PopID();
            
            ImGui::TableNextColumn();
            ImGui::TextDisabled("Folder");
            ImGui::TableNextColumn();
            ImGui::TextDisabled("--");
        }
        
        // Draw current directory tree
        drawDirectoryTree(currentPath_, 0);
        
        ImGui::EndTable();
    }
}

void FileBrowser::drawDirectoryTree(const std::string& path, int depth) {
    if (!pathExists(path) || !isDirectory(path)) {
        return;
    }
    
    try {
        ofDirectory dir(path);
        dir.listDir();
        
        // Separate directories and files
        std::vector<std::string> dirs, files;
        for (int i = 0; i < dir.size(); i++) {
            std::string name = dir.getName(i);
            std::string fullPath = dir.getPath(i);
            
            // Skip hidden files
            if (name[0] == '.') continue;
            
            // Apply search filter
            if (!searchFilter_.empty()) {
                std::string lowerName = ofToLower(name);
                std::string lowerFilter = ofToLower(searchFilter_);
                if (lowerName.find(lowerFilter) == std::string::npos) {
                    continue;
                }
            }
            
            if (isDirectory(fullPath)) {
                dirs.push_back(name);
            } else if (isValidMediaFile(name)) {
                files.push_back(name);
            }
        }
        
        // Sort
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());
        
        // Draw directories as tree nodes
        for (const auto& dirName : dirs) {
            std::string fullPath = path + "/" + dirName;
            drawDirectoryNode(fullPath, dirName, depth);
        }
        
        // Draw files as leaf nodes
        for (const auto& fileName : files) {
            std::string fullPath = path + "/" + fileName;
            drawFileNode(fullPath, fileName, depth);
        }
    } catch (const std::exception& e) {
        ofLogError("FileBrowser") << "Error drawing directory tree: " << e.what();
    }
}

void FileBrowser::drawDirectoryNode(const std::string& fullPath, const std::string& name, int depth) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    
    // Tree node flags for folders
    // Remove OpenOnArrow and OpenOnDoubleClick to allow single-click on label to open
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth |
                                ImGuiTreeNodeFlags_DrawLinesToNodes |
                                ImGuiTreeNodeFlags_NavLeftJumpsToParent;
    
    // Check if selected
    bool isSelected = selectedFiles_.find(fullPath) != selectedFiles_.end();
    if (isSelected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    
    ImGui::PushID(fullPath.c_str());
    
    // Apply custom selection color for better visibility
    if (isSelected) {
        ImGui::PushStyleColor(ImGuiCol_Header, GUIConstants::FileBrowser::Selected);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, GUIConstants::FileBrowser::SelectedHovered);
    }
    
    // Clean folder display - TreeNodeEx arrow icon already indicates it's a folder
    // Use a subtle visual indicator: small bullet or just the name
    std::string displayName = name;
    bool isOpen = ImGui::TreeNodeEx(displayName.c_str(), flags);
    
    // Pop selection colors if we pushed them
    if (isSelected) {
        ImGui::PopStyleColor(2);
    }
    
    // Handle double-click to navigate into folder (set as new root)
    // Check this BEFORE checking single click to avoid conflicts
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        // Only navigate if this is not the ".." entry (handled separately)
        if (name != "..") {
            navigateToPath(fullPath);
            // Force tree refresh by invalidating directory cache
            directoryInitialized_ = false;
        }
    }
    
    // Handle selection (click without toggling open) - only if not toggled
    // Since we removed OpenOnArrow, single-click will toggle open, so we only select if it didn't toggle
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        handleDirectoryClick(fullPath);
    }
    
    // Drag source for folders
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        // Serialize file paths: each path is null-terminated, final path is double-null-terminated
        g_dragFilesPayload.clear();
        g_dragFilesPayload.append(fullPath);
        g_dragFilesPayload.append(1, '\0'); // Null terminator for this path
        g_dragFilesPayload.append(1, '\0'); // Double null to mark end
        
        ImGui::SetDragDropPayload("FILE_BROWSER_FILES", g_dragFilesPayload.data(), g_dragFilesPayload.size());
        ImGui::Text("%s", name.c_str());
        ImGui::EndDragDropSource();
    }
    
    // Type column
    ImGui::TableNextColumn();
    ImGui::TextDisabled("Folder");
    
    // Size column
    ImGui::TableNextColumn();
    ImGui::TextDisabled("--");
    
    // Recursively draw children if open
    if (isOpen) {
        drawDirectoryTree(fullPath, depth + 1);
        ImGui::TreePop();
    }
    
    ImGui::PopID();
}

void FileBrowser::drawFileNode(const std::string& fullPath, const std::string& name, int depth) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    
    // Tree node flags for files (leaf nodes)
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | 
                                ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                ImGuiTreeNodeFlags_Bullet |
                                ImGuiTreeNodeFlags_SpanFullWidth |
                                ImGuiTreeNodeFlags_DrawLinesToNodes;
    
    // Check if selected
    bool isSelected = selectedFiles_.find(fullPath) != selectedFiles_.end();
    if (isSelected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    
    // Style media files
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f)); // Light blue
    
    // Apply custom selection color for better visibility
    if (isSelected) {
        ImGui::PushStyleColor(ImGuiCol_Header, GUIConstants::FileBrowser::Selected);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, GUIConstants::FileBrowser::SelectedHovered);
    }
    
    ImGui::PushID(fullPath.c_str());
    
    // Draw leaf node with just the name
    std::string displayName = "  " + name;
    ImGui::TreeNodeEx(displayName.c_str(), flags);
    
    // Pop selection colors if we pushed them (before popping text color)
    if (isSelected) {
        ImGui::PopStyleColor(2);
    }
    
    // Handle selection (following ImGui demo pattern)
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        handleFileClick(fullPath);
    }
    
    // Drag source for selected files
    if (isSelected) {
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            // Get only selected media files
            std::vector<std::string> selectedMedia = getSelectedMediaFiles();
            
            // Serialize file paths: each path is null-terminated, final path is double-null-terminated
            g_dragFilesPayload.clear();
            for (const auto& path : selectedMedia) {
                g_dragFilesPayload.append(path);
                g_dragFilesPayload.append(1, '\0'); // Null terminator for this path
            }
            g_dragFilesPayload.append(1, '\0'); // Double null to mark end
            
            // Set payload with actual data (not pointer)
            ImGui::SetDragDropPayload("FILE_BROWSER_FILES", g_dragFilesPayload.data(), g_dragFilesPayload.size());
            
            // Visual feedback
            if (selectedMedia.size() == 1) {
                ImGui::Text("%s", ofFilePath::getFileName(selectedMedia[0]).c_str());
            } else {
                ImGui::Text("%zu file(s)", selectedMedia.size());
            }
            
            ImGui::EndDragDropSource();
        }
    }
    
    // Hover tooltip with preview for media files
    if (ImGui::IsItemHovered()) {
        static std::unique_ptr<MediaPlayer> tooltipPlayer = std::make_unique<MediaPlayer>();
        static std::string tooltipFile;
        static std::string tooltipPath;
        
        // Load file if needed
        if (tooltipFile != fullPath || tooltipPath != currentPath_) {
            tooltipPlayer->stop();
            std::string ext = ofToLower(ofFilePath::getFileExt(fullPath));
            bool isAudio = (ext == "wav" || ext == "mp3" || ext == "aiff" || ext == "aif" || ext == "m4a");
            bool isVideo = (ext == "mov" || ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "webm" || ext == "hap");
            
            if (isAudio) {
                tooltipPlayer->loadAudio(fullPath);
            } else if (isVideo) {
                tooltipPlayer->loadVideo(fullPath);
                if (tooltipPlayer->isVideoLoaded()) {
                    tooltipPlayer->setPosition(0.1f);
                    tooltipPlayer->getVideoPlayer().getVideoFile().update();
                }
            }
            tooltipFile = fullPath;
            tooltipPath = currentPath_;
        }
        
        // Update video player
        if (tooltipPlayer->isVideoLoaded()) {
            tooltipPlayer->getVideoPlayer().getVideoFile().update();
        }
        
        // Draw tooltip preview
        MediaPreview::drawMediaTooltip(tooltipPlayer.get(), -1);
    }
    
    ImGui::PopID();
    ImGui::PopStyleColor();
    
    // Type column
    ImGui::TableNextColumn();
    std::string ext = ofToLower(ofFilePath::getFileExt(name));
    if (!ext.empty() && ext[0] != '.') {
        ext = "." + ext;
    }
    if (ext.empty()) {
        ImGui::TextDisabled("--");
    } else {
        // Remove leading dot for display
        std::string displayExt = ext.substr(1);
        std::transform(displayExt.begin(), displayExt.end(), displayExt.begin(), ::toupper);
        ImGui::Text("%s", displayExt.c_str());
    }
    
    // Size column
    ImGui::TableNextColumn();
    try {
        if (pathExists(fullPath)) {
            size_t fileSize = ofFile(fullPath).getSize();
            ImGui::Text("%s", formatFileSize(fileSize).c_str());
        } else {
            ImGui::TextDisabled("--");
        }
    } catch (...) {
        ImGui::TextDisabled("--");
    }
}

void FileBrowser::handleDirectoryClick(const std::string& fullPath) {
    // Directory click: select (for drag) but don't navigate
    // Navigation happens via arrow/double-click
    ImGuiIO& io = ImGui::GetIO();
    bool ctrlPressed = io.KeyCtrl;
    bool shiftPressed = io.KeyShift;
    
    if (shiftPressed && !lastSelectedPath_.empty()) {
        // Range selection: select all directories/files between last and current
        // For now, just toggle current - range selection for folders is complex
        // TODO: Implement proper range selection for tree view
        if (selectedFiles_.find(fullPath) != selectedFiles_.end()) {
            selectedFiles_.erase(fullPath);
        } else {
            selectedFiles_.insert(fullPath);
        }
    } else if (ctrlPressed) {
        // Ctrl+Click: toggle selection
        if (selectedFiles_.find(fullPath) != selectedFiles_.end()) {
            selectedFiles_.erase(fullPath);
        } else {
            selectedFiles_.insert(fullPath);
        }
    } else {
        // Single click: select only this folder
        selectedFiles_.clear();
        selectedFiles_.insert(fullPath);
    }
    
    lastSelectedPath_ = fullPath;
}

void FileBrowser::handleFileClick(const std::string& fullPath) {
    // File click: handle selection
    ImGuiIO& io = ImGui::GetIO();
    bool ctrlPressed = io.KeyCtrl;
    bool shiftPressed = io.KeyShift;
    
    if (shiftPressed && !lastSelectedPath_.empty()) {
        // Range selection: select all media files between last and current
        // This is simplified - in a tree view, range selection is more complex
        // For now, we'll just add to selection
        selectedFiles_.insert(fullPath);
    } else if (ctrlPressed) {
        // Ctrl+Click: toggle selection
        if (selectedFiles_.find(fullPath) != selectedFiles_.end()) {
            selectedFiles_.erase(fullPath);
        } else {
            selectedFiles_.insert(fullPath);
        }
    } else {
        // Single click: toggle selection (standard file browser behavior)
        if (selectedFiles_.find(fullPath) != selectedFiles_.end()) {
            selectedFiles_.erase(fullPath);
        } else {
            selectedFiles_.insert(fullPath);
        }
    }
    
    lastSelectedPath_ = fullPath;
    previewFile_ = fullPath;
}

std::vector<std::string> FileBrowser::getSelectedMediaFiles() const {
    std::vector<std::string> mediaFiles;
    for (const auto& path : selectedFiles_) {
        if (pathExists(path) && !isDirectory(path) && isValidMediaFile(ofFilePath::getFileName(path))) {
            mediaFiles.push_back(path);
        }
    }
    return mediaFiles;
}

void FileBrowser::drawMediaPreview() {
    // Not used in new design - preview is in tooltip only
}

void FileBrowser::drawImportControls() {
    // Not used in new design - drag & drop replaces import controls
}

std::string FileBrowser::normalizePath(const std::string& path) const {
    // Use openFrameworks path utilities
    return ofFilePath::getAbsolutePath(path);
}

std::string FileBrowser::getParentPath(const std::string& path) const {
    // Use openFrameworks path utilities
    return ofFilePath::getEnclosingDirectory(path, false);
}

bool FileBrowser::pathExists(const std::string& path) const {
    // Use openFrameworks file utilities
    ofFile file(path);
    return file.exists();
}

bool FileBrowser::isDirectory(const std::string& path) const {
    // Use openFrameworks directory utilities
    ofDirectory dir(path);
    return dir.exists() && dir.isDirectory();
}
