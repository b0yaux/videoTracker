#include "AssetLibraryGUI.h"
#include "utils/AssetLibrary.h"
#include "utils/MediaConverter.h"
#include "gui/GUIConstants.h"
#include "gui/MediaPreview.h"
#include "modules/MediaPlayer.h"
#include "modules/AudioOutput.h"
#include "ofxSoundObjects.h"
#include "ofFileUtils.h"
#include "ofSystemUtils.h"
#include "ofLog.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <memory>
#include <map>
#include <chrono>
#include <cmath>

// Static storage for drag & drop payload (unified FILE_PATHS format)
static std::string g_dragFilesPayload;

//--------------------------------------------------------------
AssetLibraryGUI::AssetLibraryGUI(AssetLibrary* assetLibrary)
    : assetLibrary_(assetLibrary) {
    if (!assetLibrary_) {
        ofLogError("AssetLibraryGUI") << "AssetLibraryGUI initialized with null AssetLibrary";
    }
}

//--------------------------------------------------------------
void AssetLibraryGUI::draw() {
    if (!assetLibrary_) {
        ImGui::Text("AssetLibrary not available");
        return;
    }
    
    // Check for newly completed conversions
    auto completedAssetIds = assetLibrary_->getNewAssets();
    for (const auto& assetId : completedAssetIds) {
        newAssets_.insert(assetId);
    }
    if (!completedAssetIds.empty()) {
        assetLibrary_->clearNewAssets();
    }
    
    // Update preview player if playing
    // Stop preview if not hovering the previewing asset
    if (previewPlayer_ && !previewingAssetId_.empty()) {
        // Check if we're still hovering the previewing asset
        bool stillHovering = (hoveredAssetId_ == previewingAssetId_);
        
        if (!stillHovering) {
            // No longer hovering - stop preview
            stopAssetPreview();
        } else {
            // Still hovering - update player
            try {
                if (previewPlayer_->isPlaying()) {
                    previewPlayer_->update();
                } else {
                    // Player stopped - clean up
                    stopAssetPreview();
                }
            } catch (...) {
                stopAssetPreview();
            }
        }
    }
    
    // Import controls at top
    drawImportControls();
    
    ImGui::Separator();
    ImGui::Spacing();
    
    // Asset list - takes remaining space
    float availableHeight = ImGui::GetContentRegionAvail().y;
    if (availableHeight < 50.0f) availableHeight = 50.0f;
    
    ImGui::BeginChild("AssetList", ImVec2(0, availableHeight), true);
    drawAssetList();
    ImGui::EndChild();
    
    // Setup drag & drop target for entire panel
    setupDragDropTarget();
}

//--------------------------------------------------------------
void AssetLibraryGUI::drawImportControls() {
    // Simplified import controls - just buttons and asset count
    if (ImGui::Button("Import File...")) {
        auto result = ofSystemLoadDialog("Select media file", false);
        if (result.bSuccess) {
            std::vector<std::string> files;
            files.push_back(result.filePath);
            assetLibrary_->importFiles(files, "");
        }
    }
    
    ImGui::SameLine();
    
    if (ImGui::Button("Import Folder...")) {
        auto result = ofSystemLoadDialog("Select folder", true);
        if (result.bSuccess) {
            // Extract folder name from path to use as subfolder name
            std::string folderName = ofFilePath::getFileName(result.filePath);
            if (folderName.empty()) {
                folderName = ofFilePath::getBaseName(result.filePath);
            }
            // User can rename the folder later via context menu
            assetLibrary_->importFolder(result.filePath, folderName);
        }
    }
    
    // Show asset count and total size inline
    ImGui::SameLine();
    size_t totalAssets = assetLibrary_->getAllAssetIds().size();
    size_t totalSize = assetLibrary_->getTotalLibrarySize();
    ImGui::TextDisabled("(%zu assets, %s)", totalAssets, formatFileSize(totalSize).c_str());
}

//--------------------------------------------------------------
void AssetLibraryGUI::drawAssetList() {
    // Get all assets and group by folder
    auto allAssetIds = assetLibrary_->getAllAssetIds();
    
    if (allAssetIds.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No assets found. Import files using buttons above or drag & drop.");
        return;
    }
    
    // Group assets by folder
    std::map<std::string, std::vector<std::string>> assetsByFolder;
    std::vector<std::string> rootAssets;  // Assets with no folder
    
    for (const auto& assetId : allAssetIds) {
        const AssetInfo* asset = assetLibrary_->getAssetInfo(assetId);
        if (!asset) continue;
        
        if (asset->assetFolder.empty()) {
            rootAssets.push_back(assetId);
        } else {
            assetsByFolder[asset->assetFolder].push_back(assetId);
        }
    }
    
    // Simplified table - just Name column with badges and inline conversion status
    if (ImGui::BeginTable("Assets", 1, ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        
        // Draw root assets (no folder) first - always visible, no collapsible root
        for (const auto& assetId : rootAssets) {
            drawAssetRow(assetId);
        }
        
        // Draw folders with their assets (sorted alphabetically)
        std::vector<std::string> folderNames;
        for (const auto& pair : assetsByFolder) {
            folderNames.push_back(pair.first);
        }
        std::sort(folderNames.begin(), folderNames.end());
        
        for (const auto& folderName : folderNames) {
            const auto& folderAssets = assetsByFolder[folderName];
            
            // Check if folder is expanded (persist state across frames)
            bool isExpanded = expandedFolders_.find(folderName) != expandedFolders_.end();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth;
            if (isExpanded) {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }
            
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            
            // Draw folder as tree node (use text-based folder indicator)
            std::string folderLabel = "[F] " + folderName;
            bool folderOpen = ImGui::TreeNodeEx(folderLabel.c_str(), flags);
            
            // Update expansion state based on TreeNodeEx return value
            if (folderOpen != isExpanded) {
                if (folderOpen) {
                    expandedFolders_.insert(folderName);
                } else {
                    expandedFolders_.erase(folderName);
                }
            }
            
            // Folder context menu
            if (ImGui::BeginPopupContextItem(("FolderContext_" + folderName).c_str())) {
                if (ImGui::MenuItem("Rename Folder...")) {
                    // TODO: Implement folder rename
                }
                if (ImGui::MenuItem("Delete Folder")) {
                    if (assetLibrary_->deleteFolder(folderName)) {
                        // Remove from expanded folders if it was expanded
                        expandedFolders_.erase(folderName);
                    }
                }
                ImGui::EndPopup();
            }
            
            // Setup drag & drop source for folder (allows dragging folder to modules)
            setupFolderDragDropSource(folderName, folderAssets);
            
            // Draw assets in this folder if expanded
            if (folderOpen) {
                for (const auto& assetId : folderAssets) {
                    drawAssetRow(assetId, 1);  // Indent level 1 for nested assets
                }
                ImGui::TreePop();
            }
        }
        
        ImGui::EndTable();
    }
}

//--------------------------------------------------------------
void AssetLibraryGUI::drawFolderTree() {
    std::string assetsDir = assetLibrary_->getAssetsDirectory();
    if (assetsDir.empty()) {
        ImGui::TextDisabled("No project open");
        return;
    }
    
    // Root "Assets" folder (always visible)
    ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth;
    if (selectedFolder_.empty()) {
        rootFlags |= ImGuiTreeNodeFlags_Selected;
    }
    
    bool rootOpen = ImGui::TreeNodeEx("Assets", rootFlags);
    if (ImGui::IsItemClicked()) {
        selectedFolder_ = "";  // Root folder
    }
    
    if (rootOpen) {
        // Build and display folder tree
        buildFolderTree(assetsDir, "", 0);
        ImGui::TreePop();
    }
}

//--------------------------------------------------------------
void AssetLibraryGUI::buildFolderTree(const std::string& basePath, const std::string& displayPath, int depth) {
    ofDirectory dir(basePath);
    if (!dir.exists()) {
        return;
    }
    
    dir.listDir();
    std::vector<std::string> folders = getFoldersInDirectory(basePath);
    
    for (const auto& folderName : folders) {
        std::string folderPath = ofFilePath::join(basePath, folderName);
        std::string relativePath = displayPath.empty() ? folderName : ofFilePath::join(displayPath, folderName);
        
        // Check if this folder should be expanded
        bool isExpanded = expandedFolders_.find(relativePath) != expandedFolders_.end();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth;
        if (isExpanded) {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        if (selectedFolder_ == relativePath) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        
        // Indent for nested folders
        if (depth > 0) {
            ImGui::Indent(15.0f * depth);
        }
        
        bool nodeOpen = ImGui::TreeNodeEx(folderName.c_str(), flags);
        
        if (ImGui::IsItemClicked()) {
            selectedFolder_ = relativePath;
        }
        
        if (nodeOpen) {
            if (isExpanded) {
                expandedFolders_.insert(relativePath);
            } else {
                expandedFolders_.erase(relativePath);
            }
            
            // Recursively build subfolders
            buildFolderTree(folderPath, relativePath, depth + 1);
            ImGui::TreePop();
        } else {
            if (isExpanded) {
                expandedFolders_.erase(relativePath);
            }
        }
        
        if (depth > 0) {
            ImGui::Unindent(15.0f * depth);
        }
    }
}

//--------------------------------------------------------------
std::vector<std::string> AssetLibraryGUI::getFoldersInDirectory(const std::string& dirPath) const {
    std::vector<std::string> folders;
    
    ofDirectory dir(dirPath);
    if (!dir.exists()) {
        return folders;
    }
    
    dir.listDir();
    for (int i = 0; i < dir.size(); i++) {
        if (dir.getFile(i).isDirectory()) {
            std::string name = dir.getName(i);
            // Skip hidden/system folders
            if (name[0] != '.' && name != "__MACOSX") {
                folders.push_back(name);
            }
        }
    }
    
    // Sort alphabetically
    std::sort(folders.begin(), folders.end());
    
    return folders;
}

//--------------------------------------------------------------
void AssetLibraryGUI::drawAssetItem(const std::string& assetId, const AssetInfo& asset) {
    // This function is no longer needed - info is now in the name column
    // Keeping for compatibility but it's not called anymore
}

//--------------------------------------------------------------
void AssetLibraryGUI::drawAssetRow(const std::string& assetId, int indentLevel) {
    const AssetInfo* asset = assetLibrary_->getAssetInfo(assetId);
    if (!asset) return;
            
            ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    
    // Add indentation for nested assets
    if (indentLevel > 0) {
        ImGui::Indent(20.0f * indentLevel);
    }
    
            bool isSelected = selectedAssets_.find(assetId) != selectedAssets_.end();
    
    // Build asset badge [AV]/[A]/[V]
    std::string badge;
    if (asset->conversionStatus == ConversionStatus::COMPLETE) {
        if (asset->isVideo && asset->isAudio && 
            !asset->convertedVideoPath.empty() && !asset->convertedAudioPath.empty()) {
            badge = "[AV] ";
        } else if (asset->isAudio && !asset->convertedAudioPath.empty()) {
            badge = "[A] ";
        } else if (asset->isVideo && !asset->convertedVideoPath.empty()) {
            badge = "[V] ";
        }
    }
            
            // Build display name - show converted file name if available, otherwise original
            std::string displayName;
            std::string fileType;
            
            if (asset->conversionStatus == ConversionStatus::COMPLETE) {
                // Show converted file name
                if (asset->isVideo && !asset->convertedVideoPath.empty()) {
                    displayName = ofFilePath::getBaseName(asset->convertedVideoPath);
                    fileType = ".mov";
                } else if (!asset->convertedAudioPath.empty()) {
                    displayName = ofFilePath::getBaseName(asset->convertedAudioPath);
                    fileType = ".wav";
                } else {
                    // Fallback to original if no converted path
                    displayName = ofFilePath::getBaseName(asset->originalPath);
                    fileType = ofFilePath::getFileExt(asset->originalPath);
                }
            } else {
                // Show original file name while converting
                displayName = ofFilePath::getBaseName(asset->originalPath);
                fileType = ofFilePath::getFileExt(asset->originalPath);
            }
            
    // Build display string with badge
    std::string displayText = badge + displayName + fileType;
    
    // Show inline conversion status only when converting
    if (asset->conversionStatus == ConversionStatus::CONVERTING) {
        displayText += " ⚙️ Converting...";
    }
    
    // Determine text color based on conversion status
    ImVec4 textColor = GUIConstants::Text::Default;
    if (asset->conversionStatus == ConversionStatus::COMPLETE) {
        // Green for newly converted (until hovered)
        if (newAssets_.find(assetId) != newAssets_.end()) {
            textColor = GUIConstants::Text::Playing; // Light green
        }
    } else if (asset->conversionStatus == ConversionStatus::CONVERTING) {
        textColor = GUIConstants::Text::Warning; // Yellow
    } else if (asset->conversionStatus == ConversionStatus::PENDING) {
        textColor = GUIConstants::Outline::Orange; // Orange
    } else if (asset->conversionStatus == ConversionStatus::FAILED) {
        textColor = GUIConstants::Outline::Red; // Red
    }
    
    // Apply color and draw selectable
    ImGui::PushStyleColor(ImGuiCol_Text, textColor);
    ImGui::Selectable(displayText.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns);
    ImGui::PopStyleColor();
    
    // Setup drag source (standard ImGui pattern: call unconditionally, only succeeds if dragging)
    setupDragDropSource(assetId, *asset);
    
    // Remove indentation
    if (indentLevel > 0) {
        ImGui::Unindent(20.0f * indentLevel);
    }
    
    // Keyboard navigation: Enter to preview, Cmd+Enter for context menu
    // Check if this item is selected or focused, and handle keyboard input
    bool itemActive = isSelected || ImGui::IsItemFocused() || ImGui::IsItemActive();
    if (itemActive) {
        ImGuiIO& io = ImGui::GetIO();
        bool cmdOrCtrlPressed = io.KeySuper || io.KeyCtrl; // Cmd on macOS, Ctrl on others
        
        // Enter key: start/stop preview
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
            if (!cmdOrCtrlPressed) {
                // Regular Enter: toggle preview
                if (previewPlayer_ && previewingAssetId_ == assetId && previewPlayer_->isPlaying()) {
                    stopAssetPreview();
                } else {
                    playAssetPreview(assetId, *asset);
                }
            } else {
                // Cmd+Enter: open context menu
                ImGui::OpenPopup(("AssetContext_" + assetId).c_str());
            }
        }
    }
    
    // Handle click-to-preview (only if not dragging)
    // Use IsItemClicked(0) to detect left-click, and check if mouse was released without dragging
    if (ImGui::IsItemClicked(0)) {
        // Check if mouse was dragged (drag distance threshold)
        ImVec2 mouseDragDelta = ImGui::GetMouseDragDelta(0);
        float dragDistance = sqrtf(mouseDragDelta.x * mouseDragDelta.x + mouseDragDelta.y * mouseDragDelta.y);
        
        // If drag distance is small (< 5 pixels), treat as click for preview
        if (dragDistance < 5.0f) {
            // Toggle preview: if already previewing this asset, stop it; otherwise start preview
            if (previewPlayer_ && previewingAssetId_ == assetId && previewPlayer_->isPlaying()) {
                // Already playing this asset - stop preview
                stopAssetPreview();
            } else {
                // Not playing or different asset - start preview
                playAssetPreview(assetId, *asset);
            }
        } else {
            // Large drag distance - handle as selection only (drag & drop)
            if (ImGui::GetIO().KeyCtrl) {
                // Toggle selection
                if (isSelected) {
                    selectedAssets_.erase(assetId);
                } else {
                    selectedAssets_.insert(assetId);
                }
            } else {
                // Single selection
                selectedAssets_.clear();
                selectedAssets_.insert(assetId);
            }
        }
    }
    
    // Context menu - use BeginPopupContextItem for proper right-click handling
    // The popup ID must be unique per item, so we use the asset ID
    if (ImGui::BeginPopupContextItem(("AssetContext_" + assetId).c_str())) {
        drawContextMenu(assetId, *asset);
        ImGui::EndPopup();
    }
            
    // Track hover state for debouncing
    if (ImGui::IsItemHovered()) {
        if (hoveredAssetId_ != assetId) {
            // New asset hovered - reset timer
            hoverStartTime_ = std::chrono::steady_clock::now();
            hoveredAssetId_ = assetId;
            
            // Remove from newly converted set when user hovers (green -> normal)
            newAssets_.erase(assetId);
        }
    } else if (hoveredAssetId_ == assetId) {
        // No longer hovering this asset
        hoveredAssetId_.clear();
    }
    
    // Show tooltip
    if (ImGui::IsItemHovered()) {
        drawAssetTooltip(assetId, *asset);
    }
}

//--------------------------------------------------------------
void AssetLibraryGUI::drawAssetTooltip(const std::string& assetId, const AssetInfo& asset) {
    ImGui::BeginTooltip();
    
    // Draw media preview using cached data OR lazy-loaded player OR live preview
    bool hasCachedWaveform = asset.waveformCached && !asset.waveformData.empty();
    bool hasCachedThumbnail = asset.thumbnailCached && !asset.thumbnailPath.empty();
    
    // PRIORITY 0: If preview is playing, show live video frame and waveform with playhead
    if (previewPlayer_ && previewingAssetId_ == assetId) {
        if (previewPlayer_->isVideoLoaded() && previewPlayer_->isPlaying()) {
            // Update video frame
            try {
                previewPlayer_->update();
                auto& videoFile = previewPlayer_->getVideoPlayer().getVideoFile();
                if (videoFile.isLoaded()) {
                    videoFile.update();
                }
            } catch (...) {
                // Silently handle errors
            }
            
            // Show live video frame from preview player
            float thumbnailHeight = MediaPreview::drawVideoThumbnail(previewPlayer_.get(), 160.0f);
            if (thumbnailHeight > 0.0f && hasCachedWaveform) {
                ImGui::Spacing();
                // Draw waveform with playhead
                drawWaveformWithPlayhead(asset.waveformData, 160.0f, 40.0f, 
                                        previewPlayer_->playheadPosition.get());
            }
        } else if (previewPlayer_->isAudioLoaded() && previewPlayer_->isPlaying()) {
            // Audio-only preview - show waveform with playhead
            if (hasCachedWaveform) {
                drawWaveformWithPlayhead(asset.waveformData, 160.0f, 60.0f,
                                        previewPlayer_->playheadPosition.get());
            }
        }
    }
    // PRIORITY 1: Use cached thumbnail (fastest, no loading)
    else if (asset.isVideo) {
        if (hasCachedThumbnail) {
            float thumbnailHeight = MediaPreview::drawCachedVideoThumbnail(asset.thumbnailPath, 160.0f);
            if (thumbnailHeight > 0.0f && hasCachedWaveform) {
                ImGui::Spacing();
                MediaPreview::drawWaveformPreview(asset.waveformData, 160.0f, 40.0f);
            }
        }
        // PRIORITY 2: Lazy load player if hovered for >500ms AND no cached thumbnail
        else {
            // Check if player is already cached (instant display, no debounce)
            MediaPlayer* cachedPlayer = nullptr;
            auto cacheIt = playerCache_.find(assetId);
            if (cacheIt != playerCache_.end() && cacheIt->second.player) {
                cachedPlayer = cacheIt->second.player.get();
                // Update last used time
                cacheIt->second.lastUsed = std::chrono::steady_clock::now();
            }
            
            // If player is cached, use it immediately (smooth like MediaPool)
            if (cachedPlayer && cachedPlayer->isVideoLoaded()) {
                // Update video frame for animation (like MediaPool)
                updateCachedPlayerFrame(cachedPlayer);
                
                // Show live video frame (like MediaPool)
                float thumbnailHeight = MediaPreview::drawVideoThumbnail(cachedPlayer, 160.0f);
                if (thumbnailHeight > 0.0f && hasCachedWaveform) {
                    ImGui::Spacing();
                    MediaPreview::drawWaveformPreview(asset.waveformData, 160.0f, 40.0f);
                }
            }
            // Player not cached - debounce before loading
            else {
                auto now = std::chrono::steady_clock::now();
                auto hoverDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - hoverStartTime_).count();
                
                // Debounce: only load after 20ms of hovering (first time only)
                if (hoverDuration > 20 && hoveredAssetId_ == assetId) {
                    MediaPlayer* player = getOrLoadPlayer(assetId, asset);
                    if (player && player->isVideoLoaded()) {
                        // Update video frame for animation (like MediaPool)
                        updateCachedPlayerFrame(player);
                        
                        // Show live video frame (like MediaPool)
                        float thumbnailHeight = MediaPreview::drawVideoThumbnail(player, 160.0f);
                        if (thumbnailHeight > 0.0f && hasCachedWaveform) {
                            ImGui::Spacing();
                            MediaPreview::drawWaveformPreview(asset.waveformData, 160.0f, 40.0f);
                        }
                    } else {
                        // Still loading - show placeholder
                        ImGui::TextDisabled("Loading preview...");
                    }
                } else {

                }
            }
        }
    }
    // Audio-only: use cached waveform OR lazy-load and generate on-demand
    else if (asset.isAudio && !asset.isVideo) {
        if (hasCachedWaveform) {
            MediaPreview::drawWaveformPreview(asset.waveformData, 160.0f, 60.0f);
        } else {
            // Lazy-load player and generate waveform on-demand (same pattern as AV assets)
            MediaPlayer* cachedPlayer = nullptr;
            auto cacheIt = playerCache_.find(assetId);
            if (cacheIt != playerCache_.end() && cacheIt->second.player) {
                cachedPlayer = cacheIt->second.player.get();
                cacheIt->second.lastUsed = std::chrono::steady_clock::now();
            }
            
            // If player is cached and has audio, extract waveform from it
            if (cachedPlayer && cachedPlayer->isAudioLoaded()) {
                try {
                    ofSoundBuffer buffer = cachedPlayer->getAudioPlayer().getBuffer();
                    AssetInfo* mutableAsset = const_cast<AssetInfo*>(assetLibrary_->getAssetInfo(assetId));
                    if (mutableAsset && !mutableAsset->waveformCached) {
                        assetLibrary_->generateWaveformForAsset(*mutableAsset, buffer);
                        assetLibrary_->saveAssetIndex();
                    }
                } catch (...) {
                    // Silently fail
                }
                // Re-fetch asset to get updated waveform
                const AssetInfo* updatedAsset = assetLibrary_->getAssetInfo(assetId);
                if (updatedAsset && updatedAsset->waveformCached && !updatedAsset->waveformData.empty()) {
                    MediaPreview::drawWaveformPreview(updatedAsset->waveformData, 160.0f, 60.0f);
                }
            }
            // Player not cached - debounce before loading
            else {
                auto now = std::chrono::steady_clock::now();
                auto hoverDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - hoverStartTime_).count();
                
                // Debounce: only load after 20ms of hovering
                if (hoverDuration > 20 && hoveredAssetId_ == assetId) {
                    MediaPlayer* player = getOrLoadPlayer(assetId, asset);
                    if (player && player->isAudioLoaded()) {
                        try {
                            ofSoundBuffer buffer = player->getAudioPlayer().getBuffer();
                            AssetInfo* mutableAsset = const_cast<AssetInfo*>(assetLibrary_->getAssetInfo(assetId));
                            if (mutableAsset && !mutableAsset->waveformCached) {
                                assetLibrary_->generateWaveformForAsset(*mutableAsset, buffer);
                                assetLibrary_->saveAssetIndex();
                            }
                        } catch (...) {
                            // Silently fail
                        }
                        // Re-fetch asset to get updated waveform
                        const AssetInfo* updatedAsset = assetLibrary_->getAssetInfo(assetId);
                        if (updatedAsset && updatedAsset->waveformCached && !updatedAsset->waveformData.empty()) {
                            MediaPreview::drawWaveformPreview(updatedAsset->waveformData, 160.0f, 60.0f);
                        } else {
                            ImGui::TextDisabled("Loading...");
                        }
                    } else {
                        ImGui::TextDisabled("Loading...");
                    }
                } else {
                    ImGui::TextDisabled("Loading...");
                }
            }
        }
    }
    
    ImGui::Separator();

    // Show original filename
    std::string baseName = ofFilePath::getBaseName(asset.originalPath);
    ImGui::TextUnformatted(baseName.c_str());
    
    // Show important technical details
    if (asset.codecInfoLoaded) {
        // Video information - show converted codec if available, otherwise original
        if (asset.isVideo) {
            if (asset.conversionStatus == ConversionStatus::COMPLETE && !asset.convertedVideoPath.empty()) {
                // Converted files are always HAP
                ImGui::Text("Video: HAP");
            } else if (!asset.videoCodec.empty()) {
                ImGui::Text("Video: %s", asset.videoCodec.c_str());
            }
            if (asset.videoWidth > 0 && asset.videoHeight > 0) {
                ImGui::Text("Resolution: %dx%d", asset.videoWidth, asset.videoHeight);
            }
        }
        
        // Audio information - show converted codec if available, otherwise original
        if (asset.isAudio) {
            if (asset.conversionStatus == ConversionStatus::COMPLETE && !asset.convertedAudioPath.empty()) {
                // Converted files are always PCM (WAV)
                ImGui::Text("Audio: PCM");
            } else if (!asset.audioCodec.empty()) {
                ImGui::Text("Audio: %s", asset.audioCodec.c_str());
            }
        }
        
        // Duration
        if (asset.duration > 0.0f) {
            int minutes = static_cast<int>(asset.duration) / 60;
            int seconds = static_cast<int>(asset.duration) % 60;
            ImGui::Text("Duration: %d:%02d", minutes, seconds);
        }
        
        // File size - show total of converted files if conversion is complete
        if (asset.conversionStatus == ConversionStatus::COMPLETE) {
            size_t totalSize = 0;
            if (!asset.convertedVideoPath.empty() && ofFile::doesFileExist(asset.convertedVideoPath)) {
                ofFile videoFile(asset.convertedVideoPath);
                if (videoFile.exists()) {
                    totalSize += videoFile.getSize();
                }
            }
            if (!asset.convertedAudioPath.empty() && ofFile::doesFileExist(asset.convertedAudioPath)) {
                ofFile audioFile(asset.convertedAudioPath);
                if (audioFile.exists()) {
                    totalSize += audioFile.getSize();
                }
            }
            if (totalSize > 0) {
                ImGui::Text("Size: %s", formatFileSize(totalSize).c_str());
            } else if (asset.fileSize > 0) {
                // Fallback to original size if converted files don't exist
                ImGui::Text("Size: %s", formatFileSize(asset.fileSize).c_str());
            }
        } else if (asset.fileSize > 0) {
            // Show original file size if not converted
            ImGui::Text("Size: %s", formatFileSize(asset.fileSize).c_str());
        }
    }
    
    // Show conversion status only if not complete (useful information)
    if (asset.conversionStatus != ConversionStatus::COMPLETE) {
        ImGui::Separator();
        std::string statusText;
        switch (asset.conversionStatus) {
            case ConversionStatus::PENDING:
                statusText = "Pending conversion";
                break;
            case ConversionStatus::CONVERTING:
                statusText = "Converting...";
                break;
            case ConversionStatus::FAILED:
                statusText = "Conversion failed";
                if (!asset.errorMessage.empty()) {
                    statusText += ": " + asset.errorMessage;
                }
                break;
            case ConversionStatus::CANCELLED:
                statusText = "Conversion cancelled";
                break;
            default:
                statusText = "Unknown status";
        }
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%s", statusText.c_str());
    }
    
    // Show folder if available (less prominent)
    if (!asset.assetFolder.empty()) {
        ImGui::TextDisabled("Folder: %s", asset.assetFolder.c_str());
    }
    
    ImGui::EndTooltip();
}

//--------------------------------------------------------------
void AssetLibraryGUI::drawContextMenu(const std::string& assetId, const AssetInfo& asset) {
    // Note: This is called from BeginPopupContextItem, so we don't need BeginPopup here
    
    // Send to module
    if (ImGui::BeginMenu("Send to Module")) {
        auto modules = assetLibrary_->getModuleTargets();
        if (modules.empty()) {
            ImGui::TextDisabled("No modules available");
        } else {
            for (const auto& moduleName : modules) {
                if (ImGui::MenuItem(moduleName.c_str())) {
                    assetLibrary_->sendToModule(assetId, moduleName);
                }
            }
        }
        ImGui::EndMenu();
    }
    
    // Move to folder - simplified: root, existing folders, and new folder in one list
    if (ImGui::BeginMenu("Move to Folder")) {
        std::string assetsDir = assetLibrary_->getAssetsDirectory();
        if (!assetsDir.empty()) {
            // Root folder option
            bool isRoot = asset.assetFolder.empty();
            if (ImGui::MenuItem("Assets (root)", nullptr, isRoot)) {
                assetLibrary_->moveAsset(assetId, "");
            }
            
            ImGui::Separator();
            
            // List existing folders directly (no nested submenu)
            std::vector<std::string> folders = getFoldersInDirectory(assetsDir);
            for (const auto& folder : folders) {
                bool isCurrent = (asset.assetFolder == folder);
                if (ImGui::MenuItem(folder.c_str(), nullptr, isCurrent)) {
                    assetLibrary_->moveAsset(assetId, folder);
                }
            }
            
            if (!folders.empty()) {
                ImGui::Separator();
            }
            
            // Create new folder option
            if (ImGui::MenuItem("New Folder...")) {
                static char folderNameBuffer[128] = {0};
                ImGui::OpenPopup("CreateFolderPopup");
            }
        } else {
            ImGui::TextDisabled("No project open");
        }
        ImGui::EndMenu();
    }
    
    // Show in Finder
    if (ImGui::MenuItem("Show in Finder")) {
        std::string path = assetLibrary_->getAssetPath(assetId);
        if (!path.empty()) {
            ofSystem("open -R \"" + path + "\"");
        }
    }
    
    // Extract Audio to WAV (for video files with audio)
    if (asset.isVideo && asset.isAudio) {
        if (ImGui::MenuItem("Extract Audio to WAV")) {
            // TODO: Queue audio extraction
            ofLogNotice("AssetLibraryGUI") << "Extract Audio: " << assetId;
        }
    }
    
    ImGui::Separator();
    
    // Delete asset
    if (ImGui::MenuItem("Delete Asset")) {
        if (assetLibrary_->deleteAsset(assetId)) {
            // Remove from selection if selected
            selectedAssets_.erase(assetId);
        }
    }
    
    // Create folder popup
    if (ImGui::BeginPopup("CreateFolderPopup")) {
        static char folderNameBuffer[128] = {0};
        ImGui::InputText("Folder Name", folderNameBuffer, sizeof(folderNameBuffer));
        if (ImGui::Button("Create")) {
            std::string folderName(folderNameBuffer);
            if (!folderName.empty()) {
                if (assetLibrary_->createFolder(folderName)) {
                    // Move asset to new folder
                    assetLibrary_->moveAsset(assetId, folderName);
                    folderNameBuffer[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            folderNameBuffer[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    
    // Note: EndPopup is called by the caller (BeginPopupContextItem)
}

//--------------------------------------------------------------
void AssetLibraryGUI::drawConversionProgress(const AssetInfo& asset) {
    if (asset.conversionStatus == ConversionStatus::PENDING) {
        ImGui::TextDisabled("Pending");
    } else if (asset.conversionStatus == ConversionStatus::CONVERTING) {
        // Get progress from MediaConverter
        // For now, just show "Converting..."
        ImGui::Text("Converting...");
    } else if (asset.conversionStatus == ConversionStatus::COMPLETE) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Complete");
    } else if (asset.conversionStatus == ConversionStatus::FAILED) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Failed");
        if (ImGui::IsItemHovered() && !asset.errorMessage.empty()) {
            ImGui::SetTooltip("%s", asset.errorMessage.c_str());
        }
    } else if (asset.conversionStatus == ConversionStatus::CANCELLED) {
        ImGui::TextDisabled("Cancelled");
    } else {
        ImGui::TextDisabled("Unknown");
    }
}

//--------------------------------------------------------------
std::string AssetLibraryGUI::formatFileSize(size_t bytes) const {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
    if (bytes < 1024ULL * 1024 * 1024) return std::to_string(bytes / (1024 * 1024)) + " MB";
    // Handle GB with one decimal place for precision
    double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.1f GB", gb);
    return std::string(buffer);
}

//--------------------------------------------------------------
std::string AssetLibraryGUI::getStatusIcon(ConversionStatus status) const {
    switch (status) {
        case ConversionStatus::PENDING: return "⏳";
        case ConversionStatus::CONVERTING: return "⚙️";
        case ConversionStatus::COMPLETE: return "✓";
        case ConversionStatus::FAILED: return "✗";
        case ConversionStatus::CANCELLED: return "⊘";
        default: return "?";
    }
}

//--------------------------------------------------------------
std::string AssetLibraryGUI::getStatusColor(ConversionStatus status) const {
    switch (status) {
        case ConversionStatus::PENDING: return "#888888";
        case ConversionStatus::CONVERTING: return "#FFAA00";
        case ConversionStatus::COMPLETE: return "#00FF00";
        case ConversionStatus::FAILED: return "#FF0000";
        case ConversionStatus::CANCELLED: return "#666666";
        default: return "#FFFFFF";
    }
}

//--------------------------------------------------------------
std::vector<std::string> AssetLibraryGUI::getFilteredAssets() const {
    if (!assetLibrary_) {
        return {};
    }
    
    std::vector<std::string> assets;
    
    // Get assets by folder filter
    if (selectedFolder_.empty()) {
        assets = assetLibrary_->getAllAssetIds();
    } else {
        assets = assetLibrary_->getAssetsByFolder(selectedFolder_);
    }
    
    // Apply search filter
    if (!searchFilter_.empty()) {
        assets.erase(
            std::remove_if(assets.begin(), assets.end(),
                [this](const std::string& assetId) {
                    const AssetInfo* asset = assetLibrary_->getAssetInfo(assetId);
                    return asset && !matchesSearchFilter(*asset);
                }),
            assets.end()
        );
    }
    
    // Apply status filters
    if (showOnlyConverting_) {
        assets.erase(
            std::remove_if(assets.begin(), assets.end(),
                [this](const std::string& assetId) {
                    const AssetInfo* asset = assetLibrary_->getAssetInfo(assetId);
                    return !asset || asset->conversionStatus != ConversionStatus::CONVERTING;
                }),
            assets.end()
        );
    }
    
    if (showOnlyComplete_) {
        assets.erase(
            std::remove_if(assets.begin(), assets.end(),
                [this](const std::string& assetId) {
                    const AssetInfo* asset = assetLibrary_->getAssetInfo(assetId);
                    return !asset || asset->conversionStatus != ConversionStatus::COMPLETE;
                }),
            assets.end()
        );
    }
    
    return assets;
}

//--------------------------------------------------------------
bool AssetLibraryGUI::matchesSearchFilter(const AssetInfo& asset) const {
    if (searchFilter_.empty()) {
        return true;
    }
    
    std::string filter = ofToLower(searchFilter_);
    std::string fileName = ofToLower(ofFilePath::getFileName(asset.originalPath));
    std::string folder = ofToLower(asset.assetFolder);
    
    return fileName.find(filter) != std::string::npos ||
           folder.find(filter) != std::string::npos;
}

//--------------------------------------------------------------
void AssetLibraryGUI::setupDragDropSource(const std::string& assetId, const AssetInfo& asset) {
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        // Resolve asset to file paths (prefer converted, fallback to original)
        std::vector<std::string> filePaths;
        
        if (asset.conversionStatus == ConversionStatus::COMPLETE) {
            // For AV files: send audio first, then video, so MediaPool can pair them correctly
            // Audio-only files: just send audio
            // Video-only files: just send video
            if (asset.isAudio && asset.isVideo) {
                // AV file: send audio first, then video (MediaPool pairs by base name)
                if (!asset.convertedAudioPath.empty() && 
                    ofFile::doesFileExist(asset.convertedAudioPath) &&
                    asset.convertedAudioPath != asset.convertedVideoPath) {
                    filePaths.push_back(asset.convertedAudioPath);
                }
                if (!asset.convertedVideoPath.empty() && 
                    ofFile::doesFileExist(asset.convertedVideoPath)) {
                    filePaths.push_back(asset.convertedVideoPath);
                }
            } else if (asset.isAudio && !asset.isVideo) {
                // Audio-only: send audio
                if (!asset.convertedAudioPath.empty() && 
                    ofFile::doesFileExist(asset.convertedAudioPath)) {
                    filePaths.push_back(asset.convertedAudioPath);
                }
            } else if (asset.isVideo && !asset.isAudio) {
                // Video-only: send video
                if (!asset.convertedVideoPath.empty() && 
                    ofFile::doesFileExist(asset.convertedVideoPath)) {
                    filePaths.push_back(asset.convertedVideoPath);
                }
            }
        }
        
        // Fallback to original file if no converted files available
        if (filePaths.empty() && ofFile::doesFileExist(asset.originalPath)) {
            filePaths.push_back(asset.originalPath);
        }
        
        // Serialize file paths in same format as FileBrowser: each path null-terminated, double null at end
        g_dragFilesPayload.clear();
        for (const auto& path : filePaths) {
            g_dragFilesPayload.append(path);
            g_dragFilesPayload.append(1, '\0'); // Null terminator for this path
        }
        g_dragFilesPayload.append(1, '\0'); // Double null to mark end
        
        // Use unified FILE_PATHS payload name
        ImGui::SetDragDropPayload("FILE_PATHS", g_dragFilesPayload.data(), g_dragFilesPayload.size());
        
        // Display preview
        std::string displayName = ofFilePath::getFileName(asset.originalPath);
        ImGui::Text("Asset: %s", displayName.c_str());
        
        ImGui::EndDragDropSource();
    }
}

//--------------------------------------------------------------
void AssetLibraryGUI::setupFolderDragDropSource(const std::string& folderName, const std::vector<std::string>& assetIds) {
    // Make folder row draggable - check if item is being dragged
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        // Collect all asset paths from assets in this folder
        std::vector<std::string> filePaths;
        
        for (const auto& assetId : assetIds) {
            const AssetInfo* asset = assetLibrary_->getAssetInfo(assetId);
            if (!asset) continue;
            
            // Track if we added any paths for this asset
            bool addedPathsForThisAsset = false;
            
            // Resolve asset to file paths (prefer converted, fallback to original)
            // Same logic as setupDragDropSource for individual assets
            if (asset->conversionStatus == ConversionStatus::COMPLETE) {
                // For AV files: send audio first, then video, so MediaPool can pair them correctly
                if (asset->isAudio && asset->isVideo) {
                    // AV file: send audio first, then video
                    if (!asset->convertedAudioPath.empty() && 
                        ofFile::doesFileExist(asset->convertedAudioPath) &&
                        asset->convertedAudioPath != asset->convertedVideoPath) {
                        filePaths.push_back(asset->convertedAudioPath);
                        addedPathsForThisAsset = true;
                    }
                    if (!asset->convertedVideoPath.empty() && 
                        ofFile::doesFileExist(asset->convertedVideoPath)) {
                        filePaths.push_back(asset->convertedVideoPath);
                        addedPathsForThisAsset = true;
                    }
                } else if (asset->isAudio && !asset->isVideo) {
                    // Audio-only: send audio
                    if (!asset->convertedAudioPath.empty() && 
                        ofFile::doesFileExist(asset->convertedAudioPath)) {
                        filePaths.push_back(asset->convertedAudioPath);
                        addedPathsForThisAsset = true;
                    }
                } else if (asset->isVideo && !asset->isAudio) {
                    // Video-only: send video
                    if (!asset->convertedVideoPath.empty() && 
                        ofFile::doesFileExist(asset->convertedVideoPath)) {
                        filePaths.push_back(asset->convertedVideoPath);
                        addedPathsForThisAsset = true;
                    }
                }
            }
            
            // Fallback to original file if no converted files were added for this asset
            if (!addedPathsForThisAsset && ofFile::doesFileExist(asset->originalPath)) {
                filePaths.push_back(asset->originalPath);
            }
        }
        
        if (!filePaths.empty()) {
            // Serialize file paths in same format as FileBrowser: each path null-terminated, double null at end
            g_dragFilesPayload.clear();
            for (const auto& path : filePaths) {
                g_dragFilesPayload.append(path);
                g_dragFilesPayload.append(1, '\0'); // Null terminator for this path
            }
            g_dragFilesPayload.append(1, '\0'); // Double null to mark end
            
            // Use unified FILE_PATHS payload name
            ImGui::SetDragDropPayload("FILE_PATHS", g_dragFilesPayload.data(), g_dragFilesPayload.size());
            
            // Display preview
            ImGui::Text("Folder: %s (%zu asset(s))", folderName.c_str(), assetIds.size());
        }
        
        ImGui::EndDragDropSource();
    }
}

//--------------------------------------------------------------
void AssetLibraryGUI::setupDragDropTarget() {
    if (ImGui::BeginDragDropTarget()) {
        // Accept files from OS, FileBrowser, or other AssetLibrary instances (unified FILE_PATHS payload)
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATHS")) {
            // Extract file paths from payload
            const char* data = static_cast<const char*>(payload->Data);
            std::vector<std::string> files;
            
            // Parse null-separated file paths
            const char* start = data;
            while (*start) {
                files.push_back(std::string(start));
                start += strlen(start) + 1;
            }
            
            if (!files.empty()) {
                assetLibrary_->handleDrop(files);
            }
        }
        
        // FILE_PATHS payload already handled above (unified for all sources)
        
        ImGui::EndDragDropTarget();
    }
}

//--------------------------------------------------------------
MediaPlayer* AssetLibraryGUI::getOrLoadPlayer(const std::string& assetId, const AssetInfo& asset) {
    // Check if already cached
    auto it = playerCache_.find(assetId);
    if (it != playerCache_.end()) {
        // Update last used time
        it->second.lastUsed = std::chrono::steady_clock::now();
        return it->second.player.get();
    }
    
    // Cache is full - remove least recently used
    if (playerCache_.size() >= MAX_CACHED_PLAYERS) {
        cleanupPlayerCache();
    }
    
    // Determine paths
    std::string videoPath;
    std::string audioPath;
    
    if (asset.conversionStatus == ConversionStatus::COMPLETE) {
        if (asset.isVideo && !asset.convertedVideoPath.empty()) {
            videoPath = asset.convertedVideoPath;
        }
        if (asset.isAudio && !asset.convertedAudioPath.empty()) {
            audioPath = asset.convertedAudioPath;
        }
    } else {
        if (asset.isVideo) {
            videoPath = asset.originalPath;
        }
        if (asset.isAudio && !asset.isVideo) {
            audioPath = asset.originalPath;
        }
    }
    
    // Create and load player
    CachedPlayer cached;
    cached.assetId = assetId;
    cached.videoPath = videoPath;
    cached.audioPath = audioPath;
    cached.player = std::make_unique<MediaPlayer>();
    
    try {
        if (asset.isVideo && !videoPath.empty() && ofFile::doesFileExist(videoPath)) {
            if (asset.isAudio && !audioPath.empty() && ofFile::doesFileExist(audioPath)) {
                cached.player->load(audioPath, videoPath);
            } else {
                cached.player->loadVideo(videoPath);
            }
            // Seek to 10% for good frame
            cached.player->setPosition(0.1f);
        } else if (asset.isAudio && !asset.isVideo && !audioPath.empty() && ofFile::doesFileExist(audioPath)) {
            cached.player->loadAudio(audioPath);
        }
        
        cached.lastUsed = std::chrono::steady_clock::now();
        playerCache_[assetId] = std::move(cached);
        return playerCache_[assetId].player.get();
    } catch (...) {
        // Failed to load - don't cache
        return nullptr;
    }
}

//--------------------------------------------------------------
void AssetLibraryGUI::cleanupPlayerCache() {
    if (playerCache_.empty()) return;
    
    // Find least recently used
    auto lru = playerCache_.begin();
    for (auto it = playerCache_.begin(); it != playerCache_.end(); ++it) {
        if (it->second.lastUsed < lru->second.lastUsed) {
            lru = it;
        }
    }
    
    // Remove it
    playerCache_.erase(lru);
}

//--------------------------------------------------------------
void AssetLibraryGUI::updateCachedPlayerFrame(MediaPlayer* player) {
    if (!player || !player->isVideoLoaded()) return;
    
    try {
        // Update video frame for animation (like MediaPool does)
        auto& videoFile = player->getVideoPlayer().getVideoFile();
        if (videoFile.isLoaded()) {
            // Keep position at 10% for preview (or cycle through for animation)
            player->setPosition(0.1f);
            videoFile.update();
        }
    } catch (...) {
        // Silently handle errors
    }
}

//--------------------------------------------------------------
void AssetLibraryGUI::playAssetPreview(const std::string& assetId, const AssetInfo& asset) {
    // Stop any existing preview
    stopAssetPreview();
    
    // Determine paths (prefer converted, fallback to original)
    std::string videoPath;
    std::string audioPath;
    
    if (asset.conversionStatus == ConversionStatus::COMPLETE) {
        if (asset.isVideo && !asset.convertedVideoPath.empty()) {
            videoPath = asset.convertedVideoPath;
        }
        if (asset.isAudio && !asset.convertedAudioPath.empty()) {
            audioPath = asset.convertedAudioPath;
        }
    } else {
        if (asset.isVideo) {
            videoPath = asset.originalPath;
        }
        if (asset.isAudio && !asset.isVideo) {
            audioPath = asset.originalPath;
        }
    }
    
    // Create preview player
    previewPlayer_ = std::make_unique<MediaPlayer>();
    
    try {
        // Load media files - MediaPlayer will automatically block HAP embedded audio
        // when separate audio files are loaded
        if (asset.isVideo && !videoPath.empty() && ofFile::doesFileExist(videoPath)) {
            if (asset.isAudio && !audioPath.empty() && ofFile::doesFileExist(audioPath)) {
                // Load both audio and video - MediaPlayer will stop HAP embedded audio automatically
                previewPlayer_->load(audioPath, videoPath);
            } else {
                // Video-only: load video but disable audio
                previewPlayer_->loadVideo(videoPath);
                previewPlayer_->audioEnabled.set(false);
            }
        } else if (asset.isAudio && !asset.isVideo && !audioPath.empty() && ofFile::doesFileExist(audioPath)) {
            previewPlayer_->loadAudio(audioPath);
        }
        
        // CRITICAL: Connect audio through master mixer (modular routing)
        // This ensures preview audio goes through the same mixing pipeline as other modules
        // and is automatically sent to monitoring connections (Oscilloscope/Spectrogram)
        if (audioOutput_ && previewPlayer_->isAudioLoaded()) {
            // Verify audioOutput_ is still valid (not destroyed after session load)
            try {
                // Connect the audio player to the mixer - this adds it to the mixer's connection list
                // The mixer will process this connection during audioOut() and include it in the mix
                // Monitoring connections (Oscilloscope/Spectrogram) receive the mixed output automatically
                previewPlayer_->getAudioPlayer().connectTo(audioOutput_->getSoundMixer());
                previewPlayer_->audioEnabled.set(true);
                
                // Verify connection was successful by checking mixer connection count
                int mixerConnections = static_cast<int>(audioOutput_->getSoundMixer().getNumConnections());
                ofLogNotice("AssetLibraryGUI") << "Preview audio connected to mixer (total mixer connections: " 
                                               << mixerConnections << ")";
            } catch (const std::exception& e) {
                ofLogError("AssetLibraryGUI") << "Failed to connect preview audio: " << e.what();
                // Continue without audio - video preview can still work
                previewPlayer_->audioEnabled.set(false);
            } catch (...) {
                ofLogError("AssetLibraryGUI") << "Unknown error connecting preview audio";
                previewPlayer_->audioEnabled.set(false);
            }
        } else if (previewPlayer_->isAudioLoaded()) {
            ofLogWarning("AssetLibraryGUI") << "Preview audio loaded but audioOutput_ is null - audio preview disabled";
            previewPlayer_->audioEnabled.set(false);
        }
        
        // Enable video if loaded
        if (previewPlayer_->isVideoLoaded()) {
            previewPlayer_->videoEnabled.set(true);
        }
        
        // Play from start
        previewPlayer_->setPosition(0.0f);
        previewPlayer_->play();
        previewingAssetId_ = assetId;
        
        // Verify player is actually playing and audio is enabled
        bool isPlaying = previewPlayer_->isPlaying();
        bool audioEnabled = previewPlayer_->isAudioLoaded() && previewPlayer_->audioEnabled.get();
        bool videoEnabled = previewPlayer_->isVideoLoaded() && previewPlayer_->videoEnabled.get();
        
        ofLogNotice("AssetLibraryGUI") << "Playing preview for: " << assetId 
                                       << " (playing: " << (isPlaying ? "yes" : "no")
                                       << ", audio: " << (audioEnabled ? "enabled" : "disabled") 
                                       << ", video: " << (videoEnabled ? "enabled" : "disabled") << ")";
        
        if (audioEnabled && !isPlaying) {
            ofLogWarning("AssetLibraryGUI") << "Preview audio enabled but player is not playing - audio may not be routed";
        }
    } catch (const std::exception& e) {
        ofLogError("AssetLibraryGUI") << "Failed to play preview: " << e.what();
        previewPlayer_.reset();
        previewingAssetId_.clear();
    }
}

//--------------------------------------------------------------
void AssetLibraryGUI::stopAssetPreview() {
    if (previewPlayer_) {
        try {
            // CRITICAL: Disconnect audio before stopping (like MediaPool does)
            // This removes the preview player from the mixer's connection list
            if (previewPlayer_->isAudioLoaded()) {
                previewPlayer_->getAudioPlayer().disconnect();
                if (audioOutput_) {
                    int mixerConnections = static_cast<int>(audioOutput_->getSoundMixer().getNumConnections());
                    ofLogNotice("AssetLibraryGUI") << "Preview audio disconnected from mixer (remaining connections: " 
                                                   << mixerConnections << ")";
                }
            }
            previewPlayer_->stop();
            previewPlayer_->reset();
        } catch (...) {
            // Silently handle errors
        }
        previewPlayer_.reset();
    }
    previewingAssetId_.clear();
}

//--------------------------------------------------------------
void AssetLibraryGUI::drawWaveformWithPlayhead(const std::vector<float>& waveformData, 
                                               float width, float height, 
                                               float position) {
    // Get canvas position before drawing
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    
    // Draw waveform
    MediaPreview::drawWaveformPreview(waveformData, width, height);
    
    // Draw playhead if position is valid
    if (position >= 0.0f && position <= 1.0f) {
        try {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (drawList) {
                float playheadX = canvasPos.x + position * width;
                ImU32 playheadColor = GUIConstants::toU32(GUIConstants::Waveform::Playhead);
                drawList->AddLine(
                    ImVec2(playheadX, canvasPos.y),
                    ImVec2(playheadX, canvasPos.y + height),
                    playheadColor, 2.0f
                );
            }
        } catch (...) {
            // Silently handle errors
        }
    }
}


