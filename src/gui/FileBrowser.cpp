#include "FileBrowser.h"
#include "gui/MediaPreview.h"
#include "ofFileUtils.h"
#include "ofSystemUtils.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

FileBrowser::FileBrowser() 
    : currentPath_(ofFilePath::getUserHomeDir()), previewLoaded_(false), lastSelectedIndex_(-1) {
    
    // Initialize media extensions
    mediaExtensions_ = {
        ".mov", ".mp4", ".avi", ".mkv", ".webm", ".hap",  // Video
        ".wav", ".mp3", ".aiff", ".aif", ".m4a"           // Audio
    };
    
    // Initialize preview player
    previewPlayer_ = std::make_unique<MediaPlayer>();
    
    refreshDirectory();
}

void FileBrowser::draw() {
    // Draw navigation bar
    drawNavigationBar();
    
    ImGui::Separator();
    ImGui::Spacing();
    
    // Single column layout: file list takes full width
    // Reserve space for import controls at bottom
    float availableHeight = ImGui::GetContentRegionAvail().y - 60.0f;
    
    // File list (full width, compact column layout)
    ImGui::BeginChild("FileList", ImVec2(0, availableHeight), true);
    drawFileList();
    ImGui::EndChild();
    
    ImGui::Spacing();
    ImGui::Separator();
    
    // Import controls (bottom)
    drawImportControls();
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
                // Show all files, but highlight media files
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
        currentPath_ = normalized;
        lastSelectedIndex_ = -1;  // Reset selection when navigating
        refreshDirectory();
        // Clear preview when navigating
        previewFile_.clear();
        previewLoaded_ = false;
        if (previewPlayer_) {
            previewPlayer_->stop();
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
    // Compact navigation bar for column layout
    // First row: path and navigation buttons
    ImGui::Text("Path:");
    ImGui::SameLine();
    
    // Make path editable (compact width)
    char pathBuffer[1024];
    strncpy(pathBuffer, currentPath_.c_str(), sizeof(pathBuffer) - 1);
    pathBuffer[sizeof(pathBuffer) - 1] = '\0';
    
    float pathInputWidth = ImGui::GetContentRegionAvail().x - 120.0f; // Reserve space for buttons
    if (pathInputWidth < 100.0f) pathInputWidth = 100.0f;
    
    ImGui::SetNextItemWidth(pathInputWidth);
    if (ImGui::InputText("##Path", pathBuffer, sizeof(pathBuffer), 
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        navigateToPath(std::string(pathBuffer));
    }
    
    ImGui::SameLine();
    if (ImGui::Button("^", ImVec2(25, 0))) {
        navigateUp();
    }
    ImGui::SameLine();
    if (ImGui::Button("R", ImVec2(25, 0))) {
        refreshDirectory();
    }
    
    // Second row: filter (optional, can be hidden if space is tight)
    ImGui::Text("Filter:");
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
    // Parent directory entry
    if (!currentPath_.empty() && currentPath_ != "/") {
        if (ImGui::Selectable("  ..##ParentDir", false)) {
            navigateUp();
        }
    }
    
    // Directories
    for (const auto& dir : directories_) {
        // Apply search filter
        if (!searchFilter_.empty()) {
            std::string lowerDir = ofToLower(dir);
            std::string lowerFilter = ofToLower(searchFilter_);
            if (lowerDir.find(lowerFilter) == std::string::npos) {
                continue;
            }
        }
        
        if (ImGui::Selectable(("📁 " + dir + "##Dir").c_str(), false)) {
            navigateToPath(currentPath_ + "/" + dir);
        }
    }
    
    // Build filtered file list for shift-click range selection
    // We need to track indices in the original files_ array, not filtered
    std::vector<int> visibleFileIndices;
    for (size_t i = 0; i < files_.size(); i++) {
        const auto& file = files_[i];
        // Apply search filter
        if (!searchFilter_.empty()) {
            std::string lowerFile = ofToLower(file);
            std::string lowerFilter = ofToLower(searchFilter_);
            if (lowerFile.find(lowerFilter) == std::string::npos) {
                continue;
            }
        }
        visibleFileIndices.push_back(static_cast<int>(i));
    }
    
    // Files - track index for shift-click range selection
    int visibleIndex = 0;
    for (int originalIndex : visibleFileIndices) {
        const auto& file = files_[originalIndex];
        std::string fullPath = currentPath_ + "/" + file;
        bool isSelected = std::find(selectedFiles_.begin(), selectedFiles_.end(), fullPath) != selectedFiles_.end();
        bool isMediaFile = isValidMediaFile(file);
        bool isPreviewed = (previewFile_ == fullPath);
        
        // Get file size for display
        std::string displayName = file;
        try {
            if (pathExists(fullPath)) {
                size_t fileSize = ofFile(fullPath).getSize();
                displayName += " (" + formatFileSize(fileSize) + ")";
            }
        } catch (...) {
            // Ignore errors getting file size
        }
        
        // Style media files differently
        if (isMediaFile) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f)); // Light blue for media
        }
        if (isPreviewed) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f)); // Yellow for previewed
        }
        
        // Create unique ID for this file item
        ImGui::PushID(originalIndex);
        
        if (ImGui::Selectable(("  " + displayName + "##File").c_str(), isSelected)) {
            if (isMediaFile) {
                bool shiftPressed = ImGui::GetIO().KeyShift;
                
                if (shiftPressed && lastSelectedIndex_ >= 0) {
                    // Range selection: select all visible files between lastSelectedIndex_ and current index
                    int startVisibleIdx = -1, endVisibleIdx = -1;
                    for (size_t i = 0; i < visibleFileIndices.size(); i++) {
                        if (visibleFileIndices[i] == lastSelectedIndex_) {
                            startVisibleIdx = static_cast<int>(i);
                        }
                        if (visibleFileIndices[i] == originalIndex) {
                            endVisibleIdx = static_cast<int>(i);
                        }
                    }
                    
                    if (startVisibleIdx >= 0 && endVisibleIdx >= 0) {
                        int startIdx = std::min(startVisibleIdx, endVisibleIdx);
                        int endIdx = std::max(startVisibleIdx, endVisibleIdx);
                        
                        for (int i = startIdx; i <= endIdx; i++) {
                            if (i >= 0 && i < static_cast<int>(visibleFileIndices.size())) {
                                int fileIdx = visibleFileIndices[i];
                                std::string rangePath = currentPath_ + "/" + files_[fileIdx];
                                if (isValidMediaFile(files_[fileIdx])) {
                                    auto it = std::find(selectedFiles_.begin(), selectedFiles_.end(), rangePath);
                                    if (it == selectedFiles_.end()) {
                                        selectedFiles_.push_back(rangePath);
                                    }
                                }
                            }
                        }
                    }
                } else {
                    // Single click: toggle selection
                    auto it = std::find(selectedFiles_.begin(), selectedFiles_.end(), fullPath);
                    if (it != selectedFiles_.end()) {
                        selectedFiles_.erase(it);
                    } else {
                        selectedFiles_.push_back(fullPath);
                    }
                }
                
                // Update last selected index (store original index, not visible index)
                lastSelectedIndex_ = originalIndex;
                
                // Set preview file for highlighting (tooltip preview uses its own player)
                previewFile_ = fullPath;
            }
        }
        
        // Add hover tooltip with preview (like MediaPoolGUI)
        if (isMediaFile && ImGui::IsItemHovered()) {
            // Create temporary player for tooltip preview
            static std::unique_ptr<MediaPlayer> tooltipPlayer = std::make_unique<MediaPlayer>();
            static std::string tooltipFile;
            static std::string tooltipPath;
            
            // Load file if not already loaded or different file
            if (tooltipFile != fullPath || tooltipPath != currentPath_) {
                tooltipPlayer->stop();
                std::string ext = ofToLower(ofFilePath::getFileExt(fullPath));
                bool isAudio = (ext == "wav" || ext == "mp3" || ext == "aiff" || ext == "aif" || ext == "m4a");
                bool isVideo = (ext == "mov" || ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "webm" || ext == "hap");
                
                if (isAudio) {
                    tooltipPlayer->loadAudio(fullPath);
                } else if (isVideo) {
                    tooltipPlayer->loadVideo(fullPath);
                    // Seek to a frame for thumbnail
                    if (tooltipPlayer->isVideoLoaded()) {
                        tooltipPlayer->setPosition(0.1f); // Seek to 10% for thumbnail
                        tooltipPlayer->getVideoPlayer().getVideoFile().update();
                    }
                }
                tooltipFile = fullPath;
                tooltipPath = currentPath_;
            }
            
            // Update tooltip player for video
            if (tooltipPlayer->isVideoLoaded()) {
                tooltipPlayer->getVideoPlayer().getVideoFile().update();
            }
            
            // Draw tooltip with preview
            MediaPreview::drawMediaTooltip(tooltipPlayer.get(), -1);
        }
        
        ImGui::PopID();
        
        if (isPreviewed) {
            ImGui::PopStyleColor();
        }
        if (isMediaFile) {
            ImGui::PopStyleColor();
        }
        
        visibleIndex++;
    }
}

void FileBrowser::drawMediaPreview() {
    if (previewFile_.empty()) {
        ImGui::Text("Select a media file to preview");
        return;
    }
    
    if (!pathExists(previewFile_)) {
        ImGui::Text("File not found: %s", previewFile_.c_str());
        return;
    }
    
    // Update preview player (must be called each frame for video)
    if (previewPlayer_) {
        previewPlayer_->update();
    }
    
    // Load preview if not loaded
    if (!previewLoaded_ && previewPlayer_) {
        std::string ext = ofToLower(ofFilePath::getFileExt(previewFile_));
        bool isAudio = (ext == "wav" || ext == "mp3" || ext == "aiff" || ext == "aif" || ext == "m4a");
        bool isVideo = (ext == "mov" || ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "webm" || ext == "hap");
        
        if (isAudio) {
            previewLoaded_ = previewPlayer_->loadAudio(previewFile_);
        } else if (isVideo) {
            previewLoaded_ = previewPlayer_->loadVideo(previewFile_);
            // Seek to beginning for thumbnail
            if (previewLoaded_) {
                previewPlayer_->setPosition(0.0f);
                previewPlayer_->getVideoPlayer().getVideoFile().update();
            }
        }
        
        if (previewLoaded_) {
            previewPlayer_->loop.set(true);
            previewPlayer_->play();
        }
    }
    
    if (!previewLoaded_ || !previewPlayer_) {
        ImGui::Text("Failed to load preview");
        return;
    }
    
    // Display file info
    ImGui::Text("Preview: %s", ofFilePath::getFileName(previewFile_).c_str());
    ImGui::Separator();
    
    // Video preview
    if (previewPlayer_->isVideoLoaded()) {
        float availableWidth = ImGui::GetContentRegionAvail().x;
        
        // Draw video thumbnail using shared utility
        float thumbnailHeight = MediaPreview::drawVideoThumbnail(previewPlayer_.get(), availableWidth);
        
        // Playback controls
        if (ImGui::Button("Play/Pause")) {
            if (previewPlayer_->isPlaying()) {
                previewPlayer_->stop();
            } else {
                previewPlayer_->play();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            previewPlayer_->stop();
            previewPlayer_->setPosition(0.0f);
        }
        
        // Show audio waveform below video if audio is also loaded
        if (previewPlayer_->isAudioLoaded()) {
            ImGui::Spacing();
            ImGui::Text("Audio waveform:");
            MediaPreview::drawWaveformPreview(previewPlayer_.get(), availableWidth, 60.0f);
        }
    }
    
    // Audio-only preview
    if (previewPlayer_->isAudioLoaded() && !previewPlayer_->isVideoLoaded()) {
        ImGui::Text("Duration: %.2f seconds", previewPlayer_->getDuration());
        ImGui::Spacing();
        
        // Draw waveform preview using shared utility
        float availableWidth = ImGui::GetContentRegionAvail().x;
        MediaPreview::drawWaveformPreview(previewPlayer_.get(), availableWidth, 100.0f);
        
        // Playback controls
        if (ImGui::Button("Play/Pause")) {
            if (previewPlayer_->isPlaying()) {
                previewPlayer_->stop();
            } else {
                previewPlayer_->play();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            previewPlayer_->stop();
            previewPlayer_->setPosition(0.0f);
        }
    }
}

void FileBrowser::drawImportControls() {
    // Selected files count
    ImGui::Text("Selected: %zu file(s)", selectedFiles_.size());
    
    ImGui::SameLine();
    if (ImGui::Button("Clear Selection")) {
        selectedFiles_.clear();
    }
    
    ImGui::SameLine();
    ImGui::Text("Import to:");
    ImGui::SameLine();
    
    // Get available instances via callback
    std::vector<std::string> instances;
    if (getInstancesCallback_) {
        instances = getInstancesCallback_();
    }
    
    if (instances.empty()) {
        ImGui::Text("No target modules available");
        ImGui::SameLine();
        ImGui::BeginDisabled();
        ImGui::Button("Import", ImVec2(100, 0));
        ImGui::EndDisabled();
    } else {
        static int currentInstance = 0;
        if (currentInstance >= instances.size()) currentInstance = 0;
        
        std::vector<const char*> instanceNames;
        for (const auto& inst : instances) {
            instanceNames.push_back(inst.c_str());
        }
        
        ImGui::Combo("##Instance", &currentInstance, instanceNames.data(), instanceNames.size());
        
        ImGui::SameLine();
        bool canImport = !selectedFiles_.empty() && currentInstance < instances.size() && importCallback_;
        if (!canImport) {
            ImGui::BeginDisabled();
        }
        
        if (ImGui::Button("Import", ImVec2(100, 0))) {
            if (importCallback_ && !selectedFiles_.empty() && currentInstance < instances.size()) {
                targetModuleInstance_ = instances[currentInstance];
                importCallback_(selectedFiles_, targetModuleInstance_);
                // Clear selection after import
                selectedFiles_.clear();
            }
        }
        
        if (!canImport) {
            ImGui::EndDisabled();
        }
    }
}

std::string FileBrowser::normalizePath(const std::string& path) const {
    std::filesystem::path p(path);
    return p.lexically_normal().string();
}

std::string FileBrowser::getParentPath(const std::string& path) const {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        return p.parent_path().string();
    }
    return "";
}

bool FileBrowser::pathExists(const std::string& path) const {
    return std::filesystem::exists(path);
}

bool FileBrowser::isDirectory(const std::string& path) const {
    return std::filesystem::is_directory(path);
}
