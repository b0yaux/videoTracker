#include "MediaConverter.h"
#include "ofLog.h"
#include "ofFileUtils.h"
#include "ofxFFmpeg.h"
#include <sstream>
#include <iomanip>
#include <thread>
#include <algorithm>

//--------------------------------------------------------------
MediaConverter::MediaConverter() 
    : shouldStop_(false), maxConcurrentJobs_(2), activeJobCount_(0) {
    // Determine optimal number of worker threads based on CPU cores
    // For video conversion, we can use more cores since FFmpeg processes are CPU-intensive
    // Use 75% of available cores, with smart caps based on core count
    unsigned int hwThreads = std::thread::hardware_concurrency();
    size_t numThreads;
    
    if (hwThreads >= 10) {
        // High-end systems (M1 Pro/Max, etc.): Use 6-8 threads
        // Leave 2-4 cores for system/GUI responsiveness
        numThreads = std::min(static_cast<size_t>(8), static_cast<size_t>(hwThreads - 2));
    } else if (hwThreads >= 6) {
        // Mid-range systems: Use 4-6 threads
        numThreads = std::min(static_cast<size_t>(6), static_cast<size_t>(hwThreads - 1));
    } else {
        // Lower-end systems: Use 2-4 threads
        numThreads = std::max(static_cast<size_t>(2), 
                             std::min(static_cast<size_t>(4), static_cast<size_t>(hwThreads)));
    }
    
    maxConcurrentJobs_ = numThreads;
    
    // Start worker thread pool
    for (size_t i = 0; i < numThreads; i++) {
        workerThreads_.emplace_back(&MediaConverter::workerThreadFunction, this);
    }
    ofLogNotice("MediaConverter") << "MediaConverter initialized with " << numThreads 
                                   << " worker threads (max " << maxConcurrentJobs_.load() 
                                   << " concurrent jobs)";
}

//--------------------------------------------------------------
MediaConverter::~MediaConverter() {
    // Signal all worker threads to stop
    shouldStop_ = true;
    
    // Wait for all worker threads to finish
    for (auto& thread : workerThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    ofLogNotice("MediaConverter") << "MediaConverter destroyed";
}

//--------------------------------------------------------------
void MediaConverter::setOutputDirectory(const std::string& outputDir) {
    std::lock_guard<std::mutex> lock(outputDirMutex_);
    outputDirectory_ = outputDir;
    
    // Create directory if it doesn't exist
    ofDirectory dir(outputDir);
    if (!dir.exists()) {
        dir.create(true); // Create recursively
        ofLogNotice("MediaConverter") << "Created output directory: " << outputDir;
    }
    
    ofLogNotice("MediaConverter") << "Output directory set to: " << outputDir;
}

//--------------------------------------------------------------
std::string MediaConverter::queueConversion(const std::string& sourcePath, 
                                              bool convertVideo, 
                                              bool extractAudio) {
    if (sourcePath.empty()) {
        ofLogError("MediaConverter") << "Cannot queue conversion: source path is empty";
        return "";
    }
    
    ofFile sourceFile(sourcePath);
    if (!sourceFile.exists()) {
        ofLogError("MediaConverter") << "Cannot queue conversion: source file does not exist: " << sourcePath;
        return "";
    }
    
    // Check output directory is set
    {
        std::lock_guard<std::mutex> lock(outputDirMutex_);
        if (outputDirectory_.empty()) {
            ofLogError("MediaConverter") << "Cannot queue conversion: output directory not set";
            return "";
        }
    }
    
    // Generate job ID and create job
    std::string jobId = generateJobId(sourcePath);
    
    ConversionJob job;
    job.id = jobId;
    job.sourcePath = sourcePath;
    job.convertVideo = convertVideo;
    job.extractAudio = extractAudio;
    job.status = ConversionStatus::PENDING;
    job.progress = 0.0f;
    
    // Generate output paths
    if (convertVideo) {
        job.outputVideoPath = generateOutputPath(sourcePath, true);
    }
    if (extractAudio) {
        job.outputAudioPath = generateOutputPath(sourcePath, false);
    }
    
    // Add job to queue
    {
        std::lock_guard<std::mutex> lock(jobsMutex_);
        jobs_[jobId] = job;
        jobQueue_.push(jobId);
    }
    
    ofLogNotice("MediaConverter") << "Queued conversion job " << jobId << " for: " << sourcePath;
    notifyProgress(jobId, 0.0f, ConversionStatus::PENDING);
    
    return jobId;
}

//--------------------------------------------------------------
bool MediaConverter::cancelJob(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(jobsMutex_);
    
    auto it = jobs_.find(jobId);
    if (it == jobs_.end()) {
        return false;
    }
    
    ConversionJob& job = it->second;
    if (job.status == ConversionStatus::COMPLETE || 
        job.status == ConversionStatus::FAILED ||
        job.status == ConversionStatus::CANCELLED) {
        return false; // Already finished
    }
    
    job.status = ConversionStatus::CANCELLED;
    job.progress = 0.0f;
    job.errorMessage = "Cancelled by user";
    
    ofLogNotice("MediaConverter") << "Cancelled job: " << jobId;
    notifyProgress(jobId, 0.0f, ConversionStatus::CANCELLED);
    
    return true;
}

//--------------------------------------------------------------
const ConversionJob* MediaConverter::getJobStatus(const std::string& jobId) const {
    std::lock_guard<std::mutex> lock(jobsMutex_);
    
    auto it = jobs_.find(jobId);
    if (it == jobs_.end()) {
        return nullptr;
    }
    
    return &it->second;
}

//--------------------------------------------------------------
std::vector<std::string> MediaConverter::getAllJobIds() const {
    std::lock_guard<std::mutex> lock(jobsMutex_);
    
    std::vector<std::string> ids;
    ids.reserve(jobs_.size());
    for (const auto& pair : jobs_) {
        ids.push_back(pair.first);
    }
    
    return ids;
}

//--------------------------------------------------------------
std::vector<std::string> MediaConverter::getJobsByStatus(ConversionStatus status) const {
    std::lock_guard<std::mutex> lock(jobsMutex_);
    
    std::vector<std::string> ids;
    for (const auto& pair : jobs_) {
        if (pair.second.status == status) {
            ids.push_back(pair.first);
        }
    }
    
    return ids;
}

//--------------------------------------------------------------
void MediaConverter::setProgressCallback(ProgressCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    progressCallback_ = callback;
}

//--------------------------------------------------------------
void MediaConverter::update() {
    // Update is called from main thread
    // Worker thread handles actual conversion
    // This method can be used for UI updates or other main-thread tasks
}

//--------------------------------------------------------------
bool MediaConverter::isBusy() const {
    return activeJobCount_.load() > 0 || getPendingCount() > 0;
}

//--------------------------------------------------------------
size_t MediaConverter::getPendingCount() const {
    std::lock_guard<std::mutex> lock(jobsMutex_);
    return getJobsByStatus(ConversionStatus::PENDING).size();
}

//--------------------------------------------------------------
size_t MediaConverter::getActiveCount() const {
    return activeJobCount_.load();
}

//--------------------------------------------------------------
void MediaConverter::setMaxConcurrentJobs(size_t maxJobs) {
    // Clamp between 1-12 for high-end systems (M1 Pro/Max can handle more)
    // Still safe because FFmpeg processes are isolated
    if (maxJobs < 1) maxJobs = 1;
    if (maxJobs > 12) maxJobs = 12;
    maxConcurrentJobs_ = maxJobs;
    ofLogNotice("MediaConverter") << "Max concurrent jobs set to: " << maxJobs;
}

//--------------------------------------------------------------
std::string MediaConverter::generateJobId(const std::string& sourcePath) const {
    // Generate unique job ID from source path and timestamp
    auto now = std::chrono::steady_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    std::string baseName = ofFilePath::getBaseName(sourcePath);
    std::stringstream ss;
    ss << baseName << "_" << timestamp;
    return ss.str();
}

//--------------------------------------------------------------
std::string MediaConverter::generateOutputPath(const std::string& sourcePath, bool isVideo) const {
    std::lock_guard<std::mutex> lock(outputDirMutex_);
    
    if (outputDirectory_.empty()) {
        return "";
    }
    
    std::string baseName = ofFilePath::getBaseName(sourcePath);
    std::string extension = isVideo ? ".mov" : ".wav";
    std::string outputPath = ofFilePath::join(outputDirectory_, baseName + extension);
    
    return outputPath;
}

//--------------------------------------------------------------
void MediaConverter::workerThreadFunction() {
    ofLogNotice("MediaConverter") << "Worker thread started (ID: " << std::this_thread::get_id() << ")";
    
    while (!shouldStop_) {
        // Check if we're at max concurrent job capacity
        if (activeJobCount_.load() >= maxConcurrentJobs_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        std::string jobId;
        
        // Get next job from queue
        {
            std::lock_guard<std::mutex> lock(jobsMutex_);
            if (!jobQueue_.empty()) {
                jobId = jobQueue_.front();
                jobQueue_.pop();
            }
        }
        
        if (jobId.empty()) {
            // No jobs, sleep briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // Get job
        ConversionJob* job = nullptr;
        {
            std::lock_guard<std::mutex> lock(jobsMutex_);
            auto it = jobs_.find(jobId);
            if (it != jobs_.end()) {
                job = &it->second;
            }
        }
        
        if (!job) {
            ofLogWarning("MediaConverter") << "Job not found: " << jobId;
            continue;
        }
        
        // Check if cancelled
        if (job->status == ConversionStatus::CANCELLED) {
            continue;
        }
        
        // Increment active job count before processing
        activeJobCount_++;
        
        // Process job
        ofLogNotice("MediaConverter") << "=== Processing job: " << jobId << " ===";
        ofLogNotice("MediaConverter") << "  Input: " << job->sourcePath;
        ofLogNotice("MediaConverter") << "  Video output: " << job->outputVideoPath;
        ofLogNotice("MediaConverter") << "  Audio output: " << job->outputAudioPath;
        ofLogNotice("MediaConverter") << "  Thread ID: " << std::this_thread::get_id();
        ofLogNotice("MediaConverter") << "  Active jobs: " << activeJobCount_.load() << "/" << maxConcurrentJobs_.load();
        
        job->status = ConversionStatus::CONVERTING;
        job->startTime = std::chrono::steady_clock::now();
        notifyProgress(jobId, 0.0f, ConversionStatus::CONVERTING);
        
        ofLogNotice("MediaConverter") << "Calling processJob...";
        bool success = processJob(*job);
        ofLogNotice("MediaConverter") << "processJob returned: " << (success ? "SUCCESS" : "FAILED");
        
        job->endTime = std::chrono::steady_clock::now();
        
        // Decrement active job count after processing
        activeJobCount_--;
        
        if (success) {
            job->status = ConversionStatus::COMPLETE;
            job->progress = 1.0f;
            ofLogNotice("MediaConverter") << "Job completed: " << jobId;
            notifyProgress(jobId, 1.0f, ConversionStatus::COMPLETE);
        } else {
            job->status = ConversionStatus::FAILED;
            ofLogError("MediaConverter") << "Job failed: " << jobId << " - " << job->errorMessage;
            notifyProgress(jobId, job->progress, ConversionStatus::FAILED);
        }
    }
    
    ofLogNotice("MediaConverter") << "Worker thread stopped (ID: " << std::this_thread::get_id() << ")";
}

//--------------------------------------------------------------
bool MediaConverter::processJob(ConversionJob& job) {
    ofLogNotice("MediaConverter") << "=== processJob START ===";
    ofLogNotice("MediaConverter") << "  Job ID: " << job.id;
    ofLogNotice("MediaConverter") << "  Source: " << job.sourcePath;
    ofLogNotice("MediaConverter") << "  Convert video: " << (job.convertVideo ? "YES" : "NO");
    ofLogNotice("MediaConverter") << "  Extract audio: " << (job.extractAudio ? "YES" : "NO");
    ofLogNotice("MediaConverter") << "  Video output: " << job.outputVideoPath;
    ofLogNotice("MediaConverter") << "  Audio output: " << job.outputAudioPath;
    ofLogNotice("MediaConverter") << "  Thread ID: " << std::this_thread::get_id();
    
    // Validate source file
    ofFile sourceFile(job.sourcePath);
    if (!sourceFile.exists()) {
        job.errorMessage = "Source file does not exist: " + job.sourcePath;
        ofLogError("MediaConverter") << job.errorMessage;
        return false;
    }
    if (!sourceFile.canRead()) {
        job.errorMessage = "Source file is not readable: " + job.sourcePath;
        ofLogError("MediaConverter") << job.errorMessage;
        return false;
    }
    ofLogNotice("MediaConverter") << "Source file validated: " << sourceFile.getSize() << " bytes";
    
    // Create ofxFFmpeg instance for this conversion
    ofLogNotice("MediaConverter") << "Creating ofxFFmpeg instance...";
    ofxFFmpeg converter;
    ofLogNotice("MediaConverter") << "ofxFFmpeg instance created";
    
    bool videoSuccess = true;
    bool audioSuccess = true;
    
    // Convert video to HAP
    if (job.convertVideo && !job.outputVideoPath.empty()) {
        ofLogNotice("MediaConverter") << "--- Starting video conversion ---";
        ofLogNotice("MediaConverter") << "  Input: " << job.sourcePath;
        ofLogNotice("MediaConverter") << "  Output: " << job.outputVideoPath;
        job.progress = 0.1f;
        notifyProgress(job.id, job.progress, ConversionStatus::CONVERTING);
        
        // Ensure output directory exists
        std::string outputDir = ofFilePath::getEnclosingDirectory(job.outputVideoPath);
        ofLogNotice("MediaConverter") << "  Output directory: " << outputDir;
        ofDirectory dir(outputDir);
        if (!dir.exists()) {
            ofLogNotice("MediaConverter") << "  Creating output directory...";
            dir.create(true);
        }
        
        ofLogNotice("MediaConverter") << "Calling converter.convertToHAP()...";
        ofLogNotice("MediaConverter") << "  Source file size: " << sourceFile.getSize() << " bytes";
        
        // Log codec info before conversion for debugging
        ofxFFmpeg codecChecker;
        std::string videoCodec, audioCodec;
        int width = 0, height = 0;
        float duration = 0.0f;
        size_t fileSize = 0;
        if (codecChecker.extractCodecInfo(job.sourcePath, videoCodec, audioCodec, width, height, duration, fileSize)) {
            ofLogNotice("MediaConverter") << "  Source codec: video=" << videoCodec << ", audio=" << audioCodec;
            ofLogNotice("MediaConverter") << "  Resolution: " << width << "x" << height << ", duration: " << duration << "s";
        } else {
            ofLogWarning("MediaConverter") << "  Could not extract codec info from source file";
        }
        
        // Calculate target dimensions if video is too large (resize videos larger than 1080p)
        const int MAX_VIDEO_HEIGHT = 1080;
        int targetWidth = 0;
        int targetHeight = 0;
        if (width > 0 && height > 0) {
            if (height > MAX_VIDEO_HEIGHT) {
                // Maintain aspect ratio, scale down to max 1080p height
                float aspect = (float)width / (float)height;
                targetHeight = MAX_VIDEO_HEIGHT;
                targetWidth = (int)(MAX_VIDEO_HEIGHT * aspect);
                ofLogNotice("MediaConverter") << "Video is " << width << "x" << height 
                                              << ", will resize to " << targetWidth << "x" << targetHeight;
            }
        }
        
        if (targetWidth > 0 && targetHeight > 0) {
            videoSuccess = converter.convertToHAP(job.sourcePath, job.outputVideoPath, 
                                                targetWidth, targetHeight);
        } else {
            videoSuccess = converter.convertToHAP(job.sourcePath, job.outputVideoPath);
        }
        ofLogNotice("MediaConverter") << "converter.convertToHAP() returned: " << (videoSuccess ? "SUCCESS" : "FAILED");
        
        if (!videoSuccess) {
            std::string errorMsg = converter.getLastError();
            job.errorMessage = "Video conversion failed: " + errorMsg;
            ofLogError("MediaConverter") << job.errorMessage;
            ofLogError("MediaConverter") << "  Source: " << job.sourcePath;
            ofLogError("MediaConverter") << "  Destination: " << job.outputVideoPath;
            if (!videoCodec.empty()) {
                ofLogError("MediaConverter") << "  Source codec was: " << videoCodec;
            }
        } else {
            // Verify output file was created
            ofFile outputFile(job.outputVideoPath);
            if (outputFile.exists()) {
                ofLogNotice("MediaConverter") << "--- Video conversion SUCCESS ---";
                ofLogNotice("MediaConverter") << "  Output file size: " << outputFile.getSize() << " bytes";
            } else {
                ofLogError("MediaConverter") << "--- Video conversion reported SUCCESS but output file missing ---";
                ofLogError("MediaConverter") << "  Expected output: " << job.outputVideoPath;
                videoSuccess = false;
                job.errorMessage = "Conversion reported success but output file was not created";
            }
        }
        job.progress = job.extractAudio ? 0.5f : 0.9f; // 50% if audio also, 90% if only video
        notifyProgress(job.id, job.progress, ConversionStatus::CONVERTING);
    }
    
    // Extract audio to WAV
    if (job.extractAudio && !job.outputAudioPath.empty()) {
        ofLogNotice("MediaConverter") << "--- Starting audio extraction ---";
        ofLogNotice("MediaConverter") << "  Input: " << job.sourcePath;
        ofLogNotice("MediaConverter") << "  Output: " << job.outputAudioPath;
        
        // Ensure output directory exists
        std::string outputDir = ofFilePath::getEnclosingDirectory(job.outputAudioPath);
        ofLogNotice("MediaConverter") << "  Output directory: " << outputDir;
        ofDirectory dir(outputDir);
        if (!dir.exists()) {
            ofLogNotice("MediaConverter") << "  Creating output directory...";
            dir.create(true);
        }
        
        ofLogNotice("MediaConverter") << "Calling converter.extractAudio()...";
        audioSuccess = converter.extractAudio(job.sourcePath, job.outputAudioPath);
        ofLogNotice("MediaConverter") << "converter.extractAudio() returned: " << (audioSuccess ? "SUCCESS" : "FAILED");
        
        if (!audioSuccess) {
            if (!job.errorMessage.empty()) {
                job.errorMessage += "; ";
            }
            job.errorMessage += "Audio extraction failed: " + converter.getLastError();
            ofLogError("MediaConverter") << "Audio extraction failed: " << converter.getLastError();
        } else {
            ofLogNotice("MediaConverter") << "--- Audio extraction SUCCESS ---";
        }
        job.progress = 0.9f;
        notifyProgress(job.id, job.progress, ConversionStatus::CONVERTING);
    }
    
    // Job succeeds if at least one operation succeeded
    bool overallSuccess = (job.convertVideo && videoSuccess) || 
                          (job.extractAudio && audioSuccess);
    
    if (!overallSuccess) {
        job.errorMessage = "All conversion operations failed. " + job.errorMessage;
    }
    
    job.progress = 1.0f;
    ofLogNotice("MediaConverter") << "=== processJob END ===";
    ofLogNotice("MediaConverter") << "  Overall success: " << (overallSuccess ? "YES" : "NO");
    if (!overallSuccess) {
        ofLogError("MediaConverter") << "  Error: " << job.errorMessage;
    }
    return overallSuccess;
}

//--------------------------------------------------------------
void MediaConverter::notifyProgress(const std::string& jobId, float progress, ConversionStatus status) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (progressCallback_) {
        // Call callback from worker thread
        // Note: Callback should be thread-safe or handle thread synchronization
        progressCallback_(jobId, progress, status);
    }
}


