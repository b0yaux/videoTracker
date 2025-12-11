#pragma once

#include "ofMain.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include "MediaConverter.h"

// Forward declarations
class ProjectManager;
class MediaConverter;
class ModuleRegistry;

/**
 * AssetInfo - Metadata for a single asset in the library
 */
struct AssetInfo {
    std::string assetId;                    // Unique asset ID
    std::string originalPath;               // Original imported file path
    std::string convertedVideoPath;         // Converted HAP video path (empty if no video)
    std::string convertedAudioPath;         // Converted WAV audio path (empty if no audio)
    std::string assetFolder;                 // Custom folder within Assets/ (e.g., "Voices", "Drums")
    bool isVideo;                           // Is this a video file?
    bool isAudio;                           // Is this an audio file?
    bool needsConversion;                   // Does this asset need conversion?
    std::string conversionJobId;            // MediaConverter job ID if converting
    ConversionStatus conversionStatus;      // Current conversion status
    std::string errorMessage;               // Error message if conversion failed
    
    // Codec information
    std::string videoCodec;
    std::string audioCodec;
    std::string resolution;                 // e.g., "1920x1080"
    int videoWidth;                         // Video width in pixels (0 if no video)
    int videoHeight;                        // Video height in pixels (0 if no video)
    float duration;                         // Duration in seconds
    size_t fileSize;                        // File size in bytes
    bool codecInfoLoaded;                   // Whether codec info has been extracted
    
    // Waveform cache (for tooltip preview without loading audio)
    std::vector<float> waveformData;        // Downsampled waveform samples
    bool waveformCached;                    // Whether waveform has been extracted
    
    // Thumbnail cache (for tooltip preview without loading video)
    std::string thumbnailPath;              // Path to cached thumbnail image (e.g., "Assets/thumbnails/assetId.jpg")
    bool thumbnailCached;                   // Whether thumbnail has been extracted
    
    AssetInfo() : isVideo(false), isAudio(false), needsConversion(false),
                  conversionStatus(ConversionStatus::PENDING),
                  videoWidth(0), videoHeight(0), duration(0.0f), fileSize(0),
                  codecInfoLoaded(false), waveformCached(false), thumbnailCached(false) {}
};

/**
 * AssetLibrary - Project-based asset management system
 */
class AssetLibrary {
public:
    AssetLibrary(ProjectManager* projectManager, MediaConverter* mediaConverter, ModuleRegistry* moduleRegistry);
    ~AssetLibrary();
    void initialize();
    std::string importFile(const std::string& filePath, const std::string& assetFolder = "");
    std::vector<std::string> importFiles(const std::vector<std::string>& filePaths, const std::string& assetFolder = "");
    std::vector<std::string> importFolder(const std::string& folderPath, const std::string& assetFolder = "");
    bool needsConversion(const std::string& filePath) const;
    const AssetInfo* getAssetInfo(const std::string& assetId) const;
    std::vector<std::string> getAllAssetIds() const;
    std::vector<std::string> getAssetsByFolder(const std::string& folderName) const;
    std::string getAssetPath(const std::string& assetId, bool preferVideo = true) const;
    bool sendToModule(const std::string& assetId, const std::string& moduleInstanceName);
    std::vector<std::string> getModuleTargets() const;
    void update();
    void draw();
    bool handleDrop(const std::vector<std::string>& filePaths);
    bool canAcceptDrop(const std::vector<std::string>& filePaths) const;
    
    // Asset management methods
    bool deleteAsset(const std::string& assetId);
    bool moveAsset(const std::string& assetId, const std::string& newFolder);
    bool createFolder(const std::string& folderPath);
    bool deleteFolder(const std::string& folderName);
    std::string getAssetsDirectory() const;
    
    // Get assets that just completed conversion (for GUI highlighting)
    std::vector<std::string> getNewAssets();
    void clearNewAssets();
    
    // Get total size of all assets in library
    size_t getTotalLibrarySize() const;
    
    // Refresh asset list by scanning directory and updating index
    void refreshAssetList();
    
    // Generate waveform for asset from audio buffer (caller provides buffer from file or player)
    void generateWaveformForAsset(AssetInfo& asset, const ofSoundBuffer& buffer);
    
    // Save asset index to disk (public for GUI to call after on-demand waveform generation)
    void saveAssetIndex();
    
private:
    ProjectManager* projectManager_;
    MediaConverter* mediaConverter_;
    ModuleRegistry* moduleRegistry_;
    std::map<std::string, AssetInfo> assets_;
    std::map<std::string, std::string> jobToAssetMap_;
    std::string assetIndexPath_;
    std::set<std::string> assetFolders_;
    std::vector<std::string> newAssets_;  // Track assets that just completed conversion
    std::string generateAssetId(const std::string& filePath) const;
    std::string getAssetStoragePath(const std::string& assetId, bool isVideo, const std::string& assetFolder) const;
    void loadAssetIndex();
    void processConversionUpdates();
    void scanDirectoryForAssets(const std::string& dirPath, const std::string& relativeFolder, std::map<std::string, std::pair<std::string, std::string>>& foundFiles);
    bool isVideoFile(const std::string& filePath) const;
    bool isAudioFile(const std::string& filePath) const;
    bool isHAPCodec(const std::string& filePath) const;
    void drawAssetList();
    void drawContextMenu(const std::string& assetId);
    void drawImportControls();
};
