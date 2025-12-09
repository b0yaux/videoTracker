#include "AssetLibrary.h"
#include "core/ProjectManager.h"
#include "MediaConverter.h"
#include "core/ModuleRegistry.h"
#include "Module.h"
#include "MediaPool.h"
#include "MediaPlayer.h"
#include "ofxFFmpeg.h"
#include "ofLog.h"
#include "ofFileUtils.h"
#include "ofJson.h"
#include "ofImage.h"
#include <imgui.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <ctime>

//--------------------------------------------------------------
AssetLibrary::AssetLibrary(
    ProjectManager* projectManager,
    MediaConverter* mediaConverter,
    ModuleRegistry* moduleRegistry
) : projectManager_(projectManager), mediaConverter_(mediaConverter), moduleRegistry_(moduleRegistry) {
}

//--------------------------------------------------------------
AssetLibrary::~AssetLibrary() {
    if (!assetIndexPath_.empty()) {
        saveAssetIndex();
    }
}

//--------------------------------------------------------------
void AssetLibrary::initialize() {
    if (!projectManager_ || !projectManager_->isProjectOpen()) {
        ofLogWarning("AssetLibrary") << "Cannot initialize: no project open";
        return;
    }
    
    assetIndexPath_ = ofFilePath::join(projectManager_->getAssetsDirectory(), ".assetindex.json");
    loadAssetIndex();
    
    if (mediaConverter_) {
        mediaConverter_->setOutputDirectory(projectManager_->getAssetsDirectory());
    }
    
    ofLogNotice("AssetLibrary") << "AssetLibrary initialized for project: " << projectManager_->getProjectName();
}

//--------------------------------------------------------------
std::string AssetLibrary::importFile(const std::string& filePath, const std::string& assetFolder) {
    if (filePath.empty()) {
        ofLogError("AssetLibrary") << "Cannot import: file path is empty";
        return "";
    }
    
    // Ensure we have a valid output directory (use default if no project)
    std::string outputDir;
    if (projectManager_ && projectManager_->isProjectOpen()) {
        outputDir = projectManager_->getAssetsDirectory();
    } else {
        // Use default location in app's data directory if no project
        // This is better than user home directory - keeps assets with the app
        outputDir = ofToDataPath("Assets", true);
        ofLogNotice("AssetLibrary") << "No project open, using default assets directory: " << outputDir;
    }
    
    // Ensure output directory exists and is set in MediaConverter
    if (mediaConverter_) {
        mediaConverter_->setOutputDirectory(outputDir);
    }
    
    // Set asset index path if not already set
    if (assetIndexPath_.empty()) {
        assetIndexPath_ = ofFilePath::join(outputDir, ".assetindex.json");
        loadAssetIndex();  // Try to load existing index
    }
    
    ofFile file(filePath);
    if (!file.exists()) {
        ofLogError("AssetLibrary") << "Cannot import: file does not exist: " << filePath;
        return "";
    }
    
    std::string assetId = generateAssetId(filePath);
    if (assets_.find(assetId) != assets_.end()) {
        ofLogWarning("AssetLibrary") << "Asset already exists: " << assetId;
        return assetId;
    }
    
    AssetInfo asset;
    asset.assetId = assetId;
    asset.originalPath = filePath;
    asset.assetFolder = assetFolder;
    asset.isVideo = isVideoFile(filePath);
    asset.isAudio = isAudioFile(filePath);
    asset.needsConversion = needsConversion(filePath);
    
    if (asset.isVideo) {
        asset.convertedVideoPath = getAssetStoragePath(assetId, true, assetFolder);
    }
    if (asset.isAudio || asset.isVideo) {
        asset.convertedAudioPath = getAssetStoragePath(assetId, false, assetFolder);
    }
    
    // Always attempt conversion if needed (even without project)
    if (asset.needsConversion && mediaConverter_) {
        bool convertVideo = asset.isVideo;
        bool extractAudio = asset.isVideo || asset.isAudio;
        
        // Ensure output directory is set
        mediaConverter_->setOutputDirectory(outputDir);
        
        std::string jobId = mediaConverter_->queueConversion(filePath, convertVideo, extractAudio);
        if (!jobId.empty()) {
            asset.conversionJobId = jobId;
            asset.conversionStatus = ConversionStatus::PENDING;
            jobToAssetMap_[jobId] = assetId;
            ofLogNotice("AssetLibrary") << "Queued conversion for: " << ofFilePath::getFileName(filePath);
        } else {
            ofLogWarning("AssetLibrary") << "Failed to queue conversion for: " << ofFilePath::getFileName(filePath);
            asset.conversionStatus = ConversionStatus::FAILED;
            asset.errorMessage = "Failed to queue conversion";
        }
    } else {
        // File doesn't need conversion, but we still need to copy it to project directory
        // This ensures all assets are in a known location and preview works correctly
        bool copySuccess = true;
        
        // Copy video file if it exists and doesn't need conversion
        if (asset.isVideo && !asset.convertedVideoPath.empty()) {
            ofFile originalVideo(filePath);
            if (originalVideo.exists()) {
                // Ensure destination directory exists
                std::string videoDir = ofFilePath::getEnclosingDirectory(asset.convertedVideoPath);
                ofDirectory dir(videoDir);
                if (!dir.exists()) {
                    dir.create(true);
                }
                
                // Copy file
                if (!originalVideo.copyTo(asset.convertedVideoPath, false, true)) {
                    ofLogError("AssetLibrary") << "Failed to copy video file: " << filePath 
                                               << " to " << asset.convertedVideoPath;
                    copySuccess = false;
                } else {
                    ofLogNotice("AssetLibrary") << "Copied video file (no conversion needed): " 
                                               << ofFilePath::getFileName(filePath);
                }
            } else {
                ofLogWarning("AssetLibrary") << "Video file does not exist: " << filePath;
                copySuccess = false;
            }
        }
        
        // Copy audio file if it exists and doesn't need conversion
        // Only copy for audio-only files (not video files with embedded audio)
        if (asset.isAudio && !asset.isVideo && !asset.convertedAudioPath.empty()) {
            ofFile originalAudio(filePath);
            if (originalAudio.exists()) {
                // Ensure destination directory exists
                std::string audioDir = ofFilePath::getEnclosingDirectory(asset.convertedAudioPath);
                ofDirectory dir(audioDir);
                if (!dir.exists()) {
                    dir.create(true);
                }
                
                // Copy file
                if (!originalAudio.copyTo(asset.convertedAudioPath, false, true)) {
                    ofLogError("AssetLibrary") << "Failed to copy audio file: " << filePath 
                                               << " to " << asset.convertedAudioPath;
                    copySuccess = false;
                } else {
                    ofLogNotice("AssetLibrary") << "Copied audio file (no conversion needed): " 
                                               << ofFilePath::getFileName(filePath);
                }
            } else {
                ofLogWarning("AssetLibrary") << "Audio file does not exist: " << filePath;
                copySuccess = false;
            }
        }
        
        if (copySuccess) {
            asset.conversionStatus = ConversionStatus::COMPLETE;
            // Track newly completed asset for GUI highlighting
            newAssets_.push_back(assetId);
        } else {
            asset.conversionStatus = ConversionStatus::FAILED;
            asset.errorMessage = "Failed to copy file to project directory";
        }
    }
    
    // Defer codec extraction - will be done during conversion or on-demand to avoid blocking import
    // This prevents GUI freezes when importing multiple files
    // Codec info will be extracted during conversion (MediaConverter already does this)
    // or can be extracted on-demand when viewing asset details
    asset.codecInfoLoaded = false;
    asset.videoCodec = "";
    asset.audioCodec = "";
    asset.videoWidth = 0;
    asset.videoHeight = 0;
    asset.duration = 0.0f;
    asset.resolution = "";
    
    // Get file size at least (fast operation)
    ofFile sizeFile(filePath);
    if (sizeFile.exists()) {
        asset.fileSize = sizeFile.getSize();
    } else {
        asset.fileSize = 0;
    }
    
    ofLogVerbose("AssetLibrary") << "Codec extraction deferred for: " << ofFilePath::getFileName(filePath);
    
    // Defer waveform extraction - will be done in background or on-demand to avoid blocking import
    // This prevents GUI freezes when importing multiple files
    asset.waveformCached = false;
    asset.waveformData.clear();
    ofLogVerbose("AssetLibrary") << "Waveform extraction deferred for: " << ofFilePath::getFileName(filePath);
    
    // Defer thumbnail extraction - will be done in background or on-demand to avoid blocking import
    // This prevents GUI freezes when importing multiple files
    asset.thumbnailCached = false;
    asset.thumbnailPath = "";
    ofLogVerbose("AssetLibrary") << "Thumbnail extraction deferred for: " << ofFilePath::getFileName(filePath);
    
    // TODO: Implement background worker thread to process deferred thumbnails/waveforms
    // For now, these will be generated on-demand when viewing assets in the GUI
    
    assets_[assetId] = asset;
    if (!assetFolder.empty()) {
        assetFolders_.insert(assetFolder);
    }
    saveAssetIndex();
    
    ofLogNotice("AssetLibrary") << "Imported asset: " << assetId << " from " << filePath;
    return assetId;
}

//--------------------------------------------------------------
std::vector<std::string> AssetLibrary::importFiles(const std::vector<std::string>& filePaths, const std::string& assetFolder) {
    std::vector<std::string> assetIds;
    assetIds.reserve(filePaths.size());
    for (const auto& filePath : filePaths) {
        std::string assetId = importFile(filePath, assetFolder);
        assetIds.push_back(assetId);
    }
    return assetIds;
}

//--------------------------------------------------------------
std::vector<std::string> AssetLibrary::importFolder(const std::string& folderPath, const std::string& assetFolder) {
    std::vector<std::string> assetIds;
    if (folderPath.empty()) return assetIds;
    
    ofDirectory dir(folderPath);
    if (!dir.exists() || !dir.isDirectory()) {
        ofLogError("AssetLibrary") << "Cannot import folder: " << folderPath;
        return assetIds;
    }
    
    dir.listDir();
    for (int i = 0; i < dir.size(); i++) {
        std::string path = dir.getPath(i);
        ofFile file(path);
        if (file.isDirectory()) {
            auto subAssetIds = importFolder(path, assetFolder);
            assetIds.insert(assetIds.end(), subAssetIds.begin(), subAssetIds.end());
        } else {
            if (isVideoFile(path) || isAudioFile(path)) {
                std::string assetId = importFile(path, assetFolder);
                if (!assetId.empty()) {
                    assetIds.push_back(assetId);
                }
            }
        }
    }
    return assetIds;
}

//--------------------------------------------------------------
bool AssetLibrary::needsConversion(const std::string& filePath) const {
    std::string ext = ofToLower(ofFilePath::getFileExt(filePath));
    if (isVideoFile(filePath)) {
        // Optimized: Assume all .mov files need conversion to avoid blocking import
        // The conversion process will check if it's already HAP and skip if needed
        // This prevents GUI freezes when importing many files
        if (ext == "mov") {
            // Defer HAP check - assume conversion needed (safer approach)
            // MediaConverter can check during conversion and skip if already HAP
            return true;
        }
        // All other video formats need conversion
        return true;
    }
    if (isAudioFile(filePath)) {
        return ext != "wav";
    }
    return false;
}

//--------------------------------------------------------------
const AssetInfo* AssetLibrary::getAssetInfo(const std::string& assetId) const {
    auto it = assets_.find(assetId);
    return (it != assets_.end()) ? &it->second : nullptr;
}

//--------------------------------------------------------------
std::vector<std::string> AssetLibrary::getAllAssetIds() const {
    std::vector<std::string> ids;
    ids.reserve(assets_.size());
    for (const auto& pair : assets_) {
        ids.push_back(pair.first);
    }
    return ids;
}

//--------------------------------------------------------------
std::vector<std::string> AssetLibrary::getAssetsByFolder(const std::string& folderName) const {
    std::vector<std::string> ids;
    for (const auto& pair : assets_) {
        if (pair.second.assetFolder == folderName) {
            ids.push_back(pair.first);
        }
    }
    return ids;
}

//--------------------------------------------------------------
std::string AssetLibrary::getAssetPath(const std::string& assetId, bool preferVideo) const {
    const AssetInfo* asset = getAssetInfo(assetId);
    if (!asset) return "";
    
    if (asset->conversionStatus == ConversionStatus::COMPLETE) {
        if (preferVideo && !asset->convertedVideoPath.empty() && ofFile(asset->convertedVideoPath).exists()) {
            return asset->convertedVideoPath;
        }
        if (!asset->convertedAudioPath.empty() && ofFile(asset->convertedAudioPath).exists()) {
            return asset->convertedAudioPath;
        }
    }
    
    if (ofFile(asset->originalPath).exists()) {
        return asset->originalPath;
    }
    return "";
}

//--------------------------------------------------------------
bool AssetLibrary::sendToModule(const std::string& assetId, const std::string& moduleInstanceName) {
    std::string assetPath = getAssetPath(assetId);
    if (assetPath.empty() || !moduleRegistry_) {
        return false;
    }
    
    auto module = moduleRegistry_->getModule(moduleInstanceName);
    if (!module) {
        ofLogError("AssetLibrary") << "Module not found: " << moduleInstanceName;
        return false;
    }
    
    auto mediaPool = std::dynamic_pointer_cast<MediaPool>(module);
    if (mediaPool) {
        if (mediaPool->addMediaFile(assetPath)) {
            ofLogNotice("AssetLibrary") << "Sent asset " << assetId << " to MediaPool: " << moduleInstanceName;
            return true;
        }
    }
    return false;
}

//--------------------------------------------------------------
std::vector<std::string> AssetLibrary::getModuleTargets() const {
    std::vector<std::string> targets;
    if (!moduleRegistry_) return targets;
    
    // Use forEachModule to iterate through all modules
    moduleRegistry_->forEachModule([&targets](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
        auto mediaPool = std::dynamic_pointer_cast<MediaPool>(module);
        if (mediaPool) {
            targets.push_back(name);
        }
    });
    return targets;
}

//--------------------------------------------------------------
void AssetLibrary::update() {
    processConversionUpdates();
}

//--------------------------------------------------------------
void AssetLibrary::draw() {
    ImGui::Text("AssetLibrary (GUI coming soon)");
    ImGui::Text("Assets: %zu", assets_.size());
}

//--------------------------------------------------------------
bool AssetLibrary::handleDrop(const std::vector<std::string>& filePaths) {
    if (!canAcceptDrop(filePaths)) {
        return false;
    }
    
    // Separate files and folders
    std::vector<std::string> files;
    std::vector<std::string> folders;
    
    for (const auto& filePath : filePaths) {
        ofFile file(filePath);
        if (file.exists() && file.isDirectory()) {
            folders.push_back(filePath);
        } else {
            files.push_back(filePath);
        }
    }
    
    // Import files
    if (!files.empty()) {
        importFiles(files);
    }
    
    // Import folders (each folder becomes a subfolder in Assets/)
    for (const auto& folderPath : folders) {
        // Extract folder name from path to use as subfolder name
        std::string folderName = ofFilePath::getFileName(folderPath);
        if (folderName.empty()) {
            folderName = ofFilePath::getBaseName(folderPath);
        }
        importFolder(folderPath, folderName);
    }
    
    return true;
}

//--------------------------------------------------------------
bool AssetLibrary::canAcceptDrop(const std::vector<std::string>& filePaths) const {
    // Allow drops even without project (will use default directory)
    if (filePaths.empty()) {
        return false;
    }
    for (const auto& filePath : filePaths) {
        // Accept video/audio files
        if (isVideoFile(filePath) || isAudioFile(filePath)) {
            return true;
        }
        // Accept folders (for drag & drop folder import)
        ofFile file(filePath);
        if (file.exists() && file.isDirectory()) {
            return true;
        }
    }
    return false;
}

//--------------------------------------------------------------
std::string AssetLibrary::generateAssetId(const std::string& filePath) const {
    // Use filename-based ID (without timestamp) to avoid duplicates
    // If same file is imported again, it will reuse the same asset ID
    std::string baseName = ofFilePath::getBaseName(filePath);
    
    // Sanitize baseName to be filesystem-safe (remove special chars)
    std::string sanitized;
    for (char c : baseName) {
        if (std::isalnum(c) || c == '_' || c == '-' || c == ' ') {
            sanitized += c;
        } else {
            sanitized += '_';
        }
    }
    
    return sanitized;
}

//--------------------------------------------------------------
std::string AssetLibrary::getAssetStoragePath(const std::string& assetId, bool isVideo, const std::string& assetFolder) const {
    // Directory structure:
    // - With project: <ProjectRoot>/Assets/ (or Assets/<folder>/ for custom folders)
    // - Without project: bin/data/Assets/ (or Assets/<folder>/ for custom folders)
    // Files are stored directly in Assets/ (or subfolder), NOT in Assets/converted/
    // The "converted" subfolder is legacy and should not be used for new imports.
    
    std::string baseDir;
    if (projectManager_ && projectManager_->isProjectOpen()) {
        baseDir = projectManager_->getAssetsDirectory();
    } else {
        // Use app's data directory instead of user home
        baseDir = ofToDataPath("Assets", true);
    }
    
    // Add custom folder if specified (e.g., "Voices", "Drums")
    // This creates: Assets/Voices/, Assets/Drums/, etc.
    if (!assetFolder.empty()) {
        baseDir = ofFilePath::join(baseDir, assetFolder);
        ofDirectory dir(baseDir);
        if (!dir.exists()) {
            dir.create(true);
        }
    }
    
    // Files are stored directly in baseDir, not in a "converted" subfolder
    std::string extension = isVideo ? ".mov" : ".wav";
    return ofFilePath::join(baseDir, assetId + extension);
}

//--------------------------------------------------------------
std::string AssetLibrary::getAssetsDirectory() const {
    if (!projectManager_ || !projectManager_->isProjectOpen()) {
        return "";
    }
    return projectManager_->getAssetsDirectory();
}

//--------------------------------------------------------------
bool AssetLibrary::deleteAsset(const std::string& assetId) {
    auto it = assets_.find(assetId);
    if (it == assets_.end()) {
        ofLogError("AssetLibrary") << "Cannot delete: asset not found: " << assetId;
        return false;
    }
    
    AssetInfo& asset = it->second;
    
    // Delete converted files
    if (!asset.convertedVideoPath.empty()) {
        ofFile videoFile(asset.convertedVideoPath);
        if (videoFile.exists()) {
            videoFile.remove();
            ofLogNotice("AssetLibrary") << "Deleted video file: " << asset.convertedVideoPath;
        }
    }
    
    if (!asset.convertedAudioPath.empty()) {
        ofFile audioFile(asset.convertedAudioPath);
        if (audioFile.exists()) {
            audioFile.remove();
            ofLogNotice("AssetLibrary") << "Deleted audio file: " << asset.convertedAudioPath;
        }
    }
    
    // Remove from job map if converting
    if (!asset.conversionJobId.empty()) {
        jobToAssetMap_.erase(asset.conversionJobId);
    }
    
    // Remove from assets map
    assets_.erase(it);
    
    // Save updated index
    saveAssetIndex();
    
    ofLogNotice("AssetLibrary") << "Deleted asset: " << assetId;
    return true;
}

//--------------------------------------------------------------
bool AssetLibrary::moveAsset(const std::string& assetId, const std::string& targetFolder) {
    auto it = assets_.find(assetId);
    if (it == assets_.end()) {
        ofLogError("AssetLibrary") << "Cannot move: asset not found: " << assetId;
        return false;
    }
    
    AssetInfo& asset = it->second;
    
    // If moving to same folder, do nothing
    if (asset.assetFolder == targetFolder) {
        return true;
    }
    
    std::string assetsDir = getAssetsDirectory();
    if (assetsDir.empty()) {
        ofLogError("AssetLibrary") << "Cannot move: no assets directory";
        return false;
    }
    
    // Create target folder if it doesn't exist
    if (!targetFolder.empty()) {
        std::string targetPath = ofFilePath::join(assetsDir, targetFolder);
        ofDirectory dir(targetPath);
        if (!dir.exists()) {
            if (!dir.create(true)) {
                ofLogError("AssetLibrary") << "Failed to create target folder: " << targetPath;
                return false;
            }
        }
    }
    
    // Move files
    std::string oldVideoPath = asset.convertedVideoPath;
    std::string oldAudioPath = asset.convertedAudioPath;
    
    if (!oldVideoPath.empty()) {
        std::string newVideoPath = getAssetStoragePath(assetId, true, targetFolder);
        ofFile videoFile(oldVideoPath);
        if (videoFile.exists()) {
            videoFile.moveTo(newVideoPath);
            asset.convertedVideoPath = newVideoPath;
        }
    }
    
    if (!oldAudioPath.empty()) {
        std::string newAudioPath = getAssetStoragePath(assetId, false, targetFolder);
        ofFile audioFile(oldAudioPath);
        if (audioFile.exists()) {
            audioFile.moveTo(newAudioPath);
            asset.convertedAudioPath = newAudioPath;
        }
    }
    
    // Update asset folder
    asset.assetFolder = targetFolder;
    
    // Save updated index
    saveAssetIndex();
    
    ofLogNotice("AssetLibrary") << "Moved asset " << assetId << " to folder: " << targetFolder;
    return true;
}

//--------------------------------------------------------------
bool AssetLibrary::createFolder(const std::string& folderName) {
    if (folderName.empty()) {
        return false;
    }
    
    std::string assetsDir = getAssetsDirectory();
    if (assetsDir.empty()) {
        ofLogError("AssetLibrary") << "Cannot create folder: no assets directory";
        return false;
    }
    
    std::string folderPath = ofFilePath::join(assetsDir, folderName);
    ofDirectory dir(folderPath);
    
    if (dir.exists()) {
        ofLogWarning("AssetLibrary") << "Folder already exists: " << folderPath;
        return true;  // Already exists, consider it success
    }
    
    if (dir.create(true)) {
        ofLogNotice("AssetLibrary") << "Created folder: " << folderPath;
        assetFolders_.insert(folderName);
        saveAssetIndex();
        return true;
    } else {
        ofLogError("AssetLibrary") << "Failed to create folder: " << folderPath;
        return false;
    }
}

//--------------------------------------------------------------
bool AssetLibrary::deleteFolder(const std::string& folderName) {
    if (folderName.empty()) {
        ofLogError("AssetLibrary") << "Cannot delete folder: folder name is empty";
        return false;
    }
    
    std::string assetsDir = getAssetsDirectory();
    if (assetsDir.empty()) {
        ofLogError("AssetLibrary") << "Cannot delete folder: no assets directory";
        return false;
    }
    
    // Get all assets in this folder
    std::vector<std::string> assetsInFolder = getAssetsByFolder(folderName);
    
    // Delete all assets in the folder first
    for (const auto& assetId : assetsInFolder) {
        if (!deleteAsset(assetId)) {
            ofLogWarning("AssetLibrary") << "Failed to delete asset " << assetId << " from folder " << folderName;
        }
    }
    
    // Delete the folder directory itself
    std::string folderPath = ofFilePath::join(assetsDir, folderName);
    ofDirectory dir(folderPath);
    
    if (dir.exists()) {
        // Remove directory (recursive - removes all contents)
        if (dir.remove(true)) {
            ofLogNotice("AssetLibrary") << "Deleted folder: " << folderPath;
            assetFolders_.erase(folderName);
            saveAssetIndex();
            return true;
        } else {
            ofLogError("AssetLibrary") << "Failed to delete folder directory: " << folderPath;
            return false;
        }
    } else {
        // Folder doesn't exist on disk, but remove from tracking anyway
        ofLogWarning("AssetLibrary") << "Folder directory does not exist: " << folderPath;
        assetFolders_.erase(folderName);
        saveAssetIndex();
        return true;
    }
}

//--------------------------------------------------------------
void AssetLibrary::loadAssetIndex() {
    if (assetIndexPath_.empty() || !ofFile(assetIndexPath_).exists()) {
        return;
    }
    
    ofFile file(assetIndexPath_, ofFile::ReadOnly);
    if (!file.is_open()) return;
    
    std::string jsonString = file.readToBuffer().getText();
    file.close();
    
    try {
        ofJson json = ofJson::parse(jsonString);
        if (json.contains("assets") && json["assets"].is_array()) {
            for (const auto& assetJson : json["assets"]) {
                AssetInfo asset;
                asset.assetId = assetJson.value("assetId", "");
                asset.originalPath = assetJson.value("originalPath", "");
                asset.convertedVideoPath = assetJson.value("convertedVideoPath", "");
                asset.convertedAudioPath = assetJson.value("convertedAudioPath", "");
                asset.assetFolder = assetJson.value("assetFolder", "");
                asset.isVideo = assetJson.value("isVideo", false);
                asset.isAudio = assetJson.value("isAudio", false);
                asset.needsConversion = assetJson.value("needsConversion", false);
                asset.conversionJobId = assetJson.value("conversionJobId", "");
                
                std::string statusStr = assetJson.value("conversionStatus", "PENDING");
                if (statusStr == "COMPLETE") asset.conversionStatus = ConversionStatus::COMPLETE;
                else if (statusStr == "CONVERTING") asset.conversionStatus = ConversionStatus::CONVERTING;
                else if (statusStr == "FAILED") asset.conversionStatus = ConversionStatus::FAILED;
                else if (statusStr == "CANCELLED") asset.conversionStatus = ConversionStatus::CANCELLED;
                else asset.conversionStatus = ConversionStatus::PENDING;
                
                // Load codec information
                asset.videoCodec = assetJson.value("videoCodec", "");
                asset.audioCodec = assetJson.value("audioCodec", "");
                asset.resolution = assetJson.value("resolution", "");
                asset.videoWidth = assetJson.value("videoWidth", 0);
                asset.videoHeight = assetJson.value("videoHeight", 0);
                asset.duration = assetJson.value("duration", 0.0f);
                asset.fileSize = assetJson.value("fileSize", static_cast<size_t>(0));
                asset.codecInfoLoaded = assetJson.value("codecInfoLoaded", false);
                
                // Load waveform cache (optional - will be regenerated if missing)
                asset.waveformCached = assetJson.value("waveformCached", false);
                if (asset.waveformCached && assetJson.contains("waveformData") && assetJson["waveformData"].is_array()) {
                    asset.waveformData.clear();
                    for (const auto& sample : assetJson["waveformData"]) {
                        if (sample.is_number()) {
                            asset.waveformData.push_back(sample.get<float>());
                        }
                    }
                }
                
                // Load thumbnail cache (optional - will be regenerated if missing)
                asset.thumbnailCached = assetJson.value("thumbnailCached", false);
                asset.thumbnailPath = assetJson.value("thumbnailPath", "");
                
                if (!asset.assetId.empty()) {
                    assets_[asset.assetId] = asset;
                    if (!asset.assetFolder.empty()) {
                        assetFolders_.insert(asset.assetFolder);
                    }
                }
            }
        }
        ofLogNotice("AssetLibrary") << "Loaded " << assets_.size() << " assets from index";
    } catch (const std::exception& e) {
        ofLogError("AssetLibrary") << "Failed to load asset index: " << e.what();
    }
}

//--------------------------------------------------------------
void AssetLibrary::saveAssetIndex() {
    if (assetIndexPath_.empty()) return;
    
    ofJson json = ofJson::object();
    json["version"] = "1.0";
    
    auto now = std::time(nullptr);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%SZ");
    json["modified"] = ss.str();
    
    json["assets"] = ofJson::array();
    for (const auto& pair : assets_) {
        const AssetInfo& asset = pair.second;
        ofJson assetJson = ofJson::object();
        assetJson["assetId"] = asset.assetId;
        assetJson["originalPath"] = asset.originalPath;
        assetJson["convertedVideoPath"] = asset.convertedVideoPath;
        assetJson["convertedAudioPath"] = asset.convertedAudioPath;
        assetJson["assetFolder"] = asset.assetFolder;
        assetJson["isVideo"] = asset.isVideo;
        assetJson["isAudio"] = asset.isAudio;
        assetJson["needsConversion"] = asset.needsConversion;
        assetJson["conversionJobId"] = asset.conversionJobId;
        
        std::string statusStr = "PENDING";
        if (asset.conversionStatus == ConversionStatus::COMPLETE) statusStr = "COMPLETE";
        else if (asset.conversionStatus == ConversionStatus::CONVERTING) statusStr = "CONVERTING";
        else if (asset.conversionStatus == ConversionStatus::FAILED) statusStr = "FAILED";
        else if (asset.conversionStatus == ConversionStatus::CANCELLED) statusStr = "CANCELLED";
        assetJson["conversionStatus"] = statusStr;
        
        // Save codec information
        assetJson["videoCodec"] = asset.videoCodec;
        assetJson["audioCodec"] = asset.audioCodec;
        assetJson["resolution"] = asset.resolution;
        assetJson["videoWidth"] = asset.videoWidth;
        assetJson["videoHeight"] = asset.videoHeight;
        assetJson["duration"] = asset.duration;
        assetJson["fileSize"] = asset.fileSize;
        assetJson["codecInfoLoaded"] = asset.codecInfoLoaded;
        
        // Save waveform cache (if available)
        assetJson["waveformCached"] = asset.waveformCached;
        if (asset.waveformCached && !asset.waveformData.empty()) {
            assetJson["waveformData"] = ofJson::array();
            for (float sample : asset.waveformData) {
                assetJson["waveformData"].push_back(sample);
            }
        }
        
        // Save thumbnail cache (if available)
        assetJson["thumbnailCached"] = asset.thumbnailCached;
        assetJson["thumbnailPath"] = asset.thumbnailPath;
        
        json["assets"].push_back(assetJson);
    }
    
    ofFile file(assetIndexPath_, ofFile::WriteOnly);
    if (!file.is_open()) {
        ofLogError("AssetLibrary") << "Failed to save asset index: " << assetIndexPath_;
        return;
    }
    
    file << json.dump(4);
    file.close();
}

//--------------------------------------------------------------
void AssetLibrary::processConversionUpdates() {
    if (!mediaConverter_) return;
    
    bool statusChanged = false;  // Track if any status changed
    
    for (auto& pair : assets_) {
        AssetInfo& asset = pair.second;
        if (asset.conversionStatus == ConversionStatus::PENDING || 
            asset.conversionStatus == ConversionStatus::CONVERTING) {
            if (!asset.conversionJobId.empty()) {
                const ConversionJob* job = mediaConverter_->getJobStatus(asset.conversionJobId);
                if (job) {
                    ConversionStatus oldStatus = asset.conversionStatus;
                    asset.conversionStatus = job->status;
                    
                    // Check if status actually changed
                    if (oldStatus != asset.conversionStatus) {
                        statusChanged = true;
                        ofLogNotice("AssetLibrary") << "Asset " << asset.assetId 
                                                    << " status changed from " 
                                                    << static_cast<int>(oldStatus) 
                                                    << " to " << static_cast<int>(asset.conversionStatus);
                    }
                    
                    if (job->status == ConversionStatus::COMPLETE) {
                        // Track newly completed asset for GUI highlighting
                        if (oldStatus != ConversionStatus::COMPLETE) {
                            newAssets_.push_back(asset.assetId);
                        }
                        
                        if (asset.isVideo && !job->outputVideoPath.empty()) {
                            asset.convertedVideoPath = job->outputVideoPath;
                        }
                        if (!job->outputAudioPath.empty()) {
                            asset.convertedAudioPath = job->outputAudioPath;
                            // If video file had audio extracted, mark it as having audio
                            if (asset.isVideo) {
                                asset.isAudio = true;
                            }
                            
                            // Extract waveform from converted audio (if not already cached)
                            if (!asset.waveformCached && ofFile::doesFileExist(job->outputAudioPath)) {
                                MediaPlayer tempPlayer;
                                if (tempPlayer.loadAudio(job->outputAudioPath) && tempPlayer.isAudioLoaded()) {
                                    try {
                                        ofSoundBuffer buffer = tempPlayer.getAudioPlayer().getBuffer();
                                        int numFrames = buffer.getNumFrames();
                                        int numChannels = buffer.getNumChannels();
                                        
                                        if (numFrames > 0 && numChannels > 0) {
                                            const int maxPoints = 600;
                                            int stepSize = std::max(1, numFrames / maxPoints);
                                            int actualPoints = std::min(maxPoints, numFrames / stepSize);
                                            
                                            if (actualPoints >= 2) {
                                                asset.waveformData.resize(actualPoints);
                                                for (int i = 0; i < actualPoints; i++) {
                                                    int sampleIndex = i * stepSize;
                                                    sampleIndex = std::max(0, std::min(numFrames - 1, sampleIndex));
                                                    float sample = buffer.getSample(sampleIndex, 0);
                                                    if (numChannels > 1) {
                                                        float sum = sample;
                                                        for (int ch = 1; ch < numChannels; ch++) {
                                                            sum += buffer.getSample(sampleIndex, ch);
                                                        }
                                                        sample = sum / numChannels;
                                                    }
                                                    asset.waveformData[i] = sample;
                                                }
                                                asset.waveformCached = true;
                                                ofLogVerbose("AssetLibrary") << "Cached waveform after conversion for: " << asset.assetId;
                                            }
                                        }
                                    } catch (...) {
                                        // Silently fail - waveform is optional
                                    }
                                }
                                tempPlayer.stop();
                                tempPlayer.reset();
                            }
                        }
                        // CRITICAL: Save index when conversion completes
                        saveAssetIndex();
                        ofLogNotice("AssetLibrary") << "Asset conversion completed: " << asset.assetId;
                    } else if (job->status == ConversionStatus::FAILED) {
                        asset.errorMessage = job->errorMessage;
                        ofLogError("AssetLibrary") << "Asset conversion failed: " << asset.assetId 
                                                    << " - " << asset.errorMessage;
                        // Save index even on failure to persist error state
                        saveAssetIndex();
                    }
                } else {
                    // Job not found - might have been removed or completed
                    ofLogWarning("AssetLibrary") << "Job not found for asset: " << asset.assetId 
                                                  << ", jobId: " << asset.conversionJobId;
                }
            }
        }
    }
    
    // Save index if any status changed (backup save)
    if (statusChanged) {
        saveAssetIndex();
    }
}

//--------------------------------------------------------------
bool AssetLibrary::isVideoFile(const std::string& filePath) const {
    std::string ext = ofToLower(ofFilePath::getFileExt(filePath));
    return (ext == "mov" || ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "webm" || ext == "hap");
}

//--------------------------------------------------------------
bool AssetLibrary::isAudioFile(const std::string& filePath) const {
    std::string ext = ofToLower(ofFilePath::getFileExt(filePath));
    return (ext == "wav" || ext == "mp3" || ext == "aiff" || ext == "aif" || ext == "m4a" || ext == "flac");
}

//--------------------------------------------------------------
bool AssetLibrary::isHAPCodec(const std::string& filePath) const {
    std::string ext = ofToLower(ofFilePath::getFileExt(filePath));
    if (ext != "mov") {
        return false;  // Only .mov files can be HAP
    }
    
    // Actually check the codec using ofxFFmpeg
    ofxFFmpeg ffmpeg;
    std::string videoCodec, audioCodec;
    int width = 0, height = 0;
    float duration = 0.0f;
    size_t fileSize = 0;
    
    if (ffmpeg.extractCodecInfo(filePath, videoCodec, audioCodec, width, height, duration, fileSize)) {
        // Log detected codec for debugging
        ofLogVerbose("AssetLibrary") << "Detected codec for " << ofFilePath::getFileName(filePath) 
                                      << ": video=" << videoCodec << ", audio=" << audioCodec;
        
        // Check if video codec is HAP (could be "hap", "hapq", "hapa", "hapalpha", etc.)
        std::string codecLower = ofToLower(videoCodec);
        bool isHAP = (codecLower.find("hap") != std::string::npos);
        
        if (isHAP) {
            ofLogVerbose("AssetLibrary") << "File is HAP codec, no conversion needed";
        } else {
            ofLogVerbose("AssetLibrary") << "File is NOT HAP codec (" << videoCodec 
                                          << "), conversion will be needed";
        }
        
        return isHAP;
    }
    
    // If we can't determine codec, assume it's NOT HAP (conservative approach)
    // This ensures non-HAP .mov files get converted
    ofLogWarning("AssetLibrary") << "Could not extract codec info for: " << ofFilePath::getFileName(filePath) 
                                  << ", assuming NOT HAP (will attempt conversion)";
    return false;
}

//--------------------------------------------------------------
void AssetLibrary::drawAssetList() {
    // Deprecated - use AssetLibraryGUI
}

//--------------------------------------------------------------
void AssetLibrary::drawContextMenu(const std::string& assetId) {
    // Deprecated - use AssetLibraryGUI
}

//--------------------------------------------------------------
void AssetLibrary::drawImportControls() {
    // Deprecated - use AssetLibraryGUI
}

//--------------------------------------------------------------
std::vector<std::string> AssetLibrary::getNewAssets() {
    return newAssets_;
}

//--------------------------------------------------------------
void AssetLibrary::clearNewAssets() {
    newAssets_.clear();
}
