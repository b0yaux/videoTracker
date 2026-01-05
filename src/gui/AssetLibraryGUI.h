#pragma once

#include <imgui.h>
#include "gui/GUIConstants.h"
#include <string>
#include <vector>
#include <set>
#include <memory>
#include <map>
#include <chrono>

// Forward declarations
class AssetLibrary;
struct AssetInfo;
enum class ConversionStatus : int;

/**
 * AssetLibraryGUI - GUI panel for AssetLibrary
 * 
 * Features:
 * - Display asset list with folders
 * - Import controls (file/folder buttons)
 * - Drag & drop support (from OS and FileBrowser)
 * - Context menus for asset operations
 * - Conversion progress display
 * - Send to module functionality
 * 
 * Note: This is a utility panel, similar to FileBrowser
 */
class AssetLibraryGUI {
public:
    AssetLibraryGUI(AssetLibrary* assetLibrary);
    ~AssetLibraryGUI() = default;
    
    /**
     * Main draw function - draws the panel content
     * Window is created by ViewManager, this just draws the content
     */
    void draw();
    
    /**
     * Set audio output for preview playback routing
     * Called from ofApp setup() after master audio output is initialized
     * Preview audio will be routed through the output's internal mixer
     */
    void setAudioMixer(class AudioOutput* audioOutput) { audioOutput_ = audioOutput; }
    
private:
    AssetLibrary* assetLibrary_;
    class AudioOutput* audioOutput_ = nullptr;  // For preview routing (replaces audioMixer_)
    
    // UI state
    std::string selectedFolder_;  // Currently selected folder filter (empty = root Assets/)
    std::set<std::string> selectedAssets_;  // Selected asset IDs
    std::string searchFilter_;  // Search filter text
    bool showOnlyConverting_ = false;  // Filter: show only converting assets
    bool showOnlyComplete_ = false;  // Filter: show only complete assets
    std::set<std::string> expandedFolders_;  // Set of expanded folder paths in tree view
    
    // Import state
    std::string importFolderName_;  // Custom folder name for imports
    
    // Folder rename state
    std::string renamingFolder_;  // Currently renaming folder name
    char renameFolderBuffer_[128] = {0};  // Buffer for rename input
    
    // New folder state
    char newFolderBuffer_[128] = {0};  // Buffer for new folder input
    
    // Player cache for tooltip previews (inspired by MultiSampler pattern)
    struct CachedPlayer {
        std::string assetId;
        std::unique_ptr<class MediaPlayer> player;
        std::chrono::steady_clock::time_point lastUsed;
        std::string videoPath;
        std::string audioPath;
    };
    
    static constexpr size_t MAX_CACHED_PLAYERS = 5;  // Limit cache size
    std::map<std::string, CachedPlayer> playerCache_;
    
    // Hover state for debouncing
    std::string hoveredAssetId_;
    std::chrono::steady_clock::time_point hoverStartTime_;
    
    // Click-to-preview state
    std::string previewingAssetId_;
    std::unique_ptr<class MediaPlayer> previewPlayer_;
    
    // Track newly converted assets (green until hovered)
    std::set<std::string> newAssets_;
    
    // Auto-sync state
    std::chrono::steady_clock::time_point lastRefreshTime_;  // Last refresh time (for rate limiting)
    
    // Performance caches
    size_t cachedTotalSize_ = 0;  // Cached total library size
    size_t cachedAssetCount_ = 0;  // Asset count when size was cached
    bool assetGroupingDirty_ = true;  // Flag to rebuild asset grouping cache
    std::vector<std::string> cachedAssetIds_;  // Cached asset IDs for change detection
    std::map<std::string, std::vector<std::string>> cachedAssetsByFolder_;  // Cached asset grouping by folder
    std::vector<std::string> cachedRootAssets_;  // Cached root assets (no folder)
    std::vector<std::string> cachedFolderNames_;  // Cached sorted folder names
    
    // UI sections
    void drawImportControls();
    void drawFolderTree();
    void drawAssetList();
    void drawAssetRow(const std::string& assetId, int indentLevel = 0);
    void drawAssetItem(const std::string& assetId, const AssetInfo& asset);
    void drawContextMenu(const std::string& assetId, const AssetInfo& asset);
    void drawConversionProgress(const AssetInfo& asset);
    void drawAssetTooltip(const std::string& assetId, const AssetInfo& asset);
    
    // Folder tree helpers
    void buildFolderTree(const std::string& basePath, const std::string& displayPath, int depth = 0);
    std::vector<std::string> getFoldersInDirectory(const std::string& dirPath) const;
    
    // Helper methods
    std::string formatFileSize(size_t bytes) const;
    std::string getStatusIcon(ConversionStatus status) const;
    std::string getStatusColor(ConversionStatus status) const;
    std::vector<std::string> getFilteredAssets() const;
    bool matchesSearchFilter(const AssetInfo& asset) const;
    
    // Drag & drop
    void setupDragDropSource(const std::string& assetId, const AssetInfo& asset);
    void setupFolderDragDropSource(const std::string& folderName, const std::vector<std::string>& assetIds);
    void setupDragDropTarget();
    
    // Player cache helpers (for lazy-loading tooltip previews)
    class MediaPlayer* getOrLoadPlayer(const std::string& assetId, const AssetInfo& asset);
    void cleanupPlayerCache();
    void updateCachedPlayerFrame(class MediaPlayer* player);
    
    // Click-to-preview
    void playAssetPreview(const std::string& assetId, const AssetInfo& asset);
    void stopAssetPreview();
    
    // Waveform with playhead drawing
    void drawWaveformWithPlayhead(const std::vector<float>& waveformData, 
                                  float width, float height, 
                                  float position);
};

