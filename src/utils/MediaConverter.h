#pragma once

#include "ofMain.h"
#include <string>
#include <vector>
#include <queue>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>

/**
 * ConversionStatus - Status of a conversion job
 */
enum class ConversionStatus {
    PENDING,      // Queued, waiting to start
    CONVERTING,   // Currently converting
    COMPLETE,     // Successfully completed
    FAILED,       // Conversion failed
    CANCELLED     // User cancelled
};

/**
 * ConversionJob - Represents a single conversion request
 */
struct ConversionJob {
    std::string id;                    // Unique job ID
    std::string sourcePath;            // Input file path
    std::string outputVideoPath;       // Output HAP video path (if video conversion)
    std::string outputAudioPath;       // Output WAV audio path (if audio extraction)
    bool convertVideo;                 // Convert video to HAP
    bool extractAudio;                 // Extract audio to WAV
    ConversionStatus status;           // Current status
    float progress;                     // Progress 0.0-1.0
    std::string errorMessage;          // Error message if failed
    std::chrono::time_point<std::chrono::steady_clock> startTime;
    std::chrono::time_point<std::chrono::steady_clock> endTime;
    
    ConversionJob() : convertVideo(false), extractAudio(false), 
                      status(ConversionStatus::PENDING), progress(0.0f) {}
};

/**
 * MediaConverter - Background video conversion service
 * 
 * Converts video files to HAP format and extracts audio to WAV.
 * Uses ofxAVcpp for actual conversion work.
 * Manages a queue of conversion jobs with progress tracking.
 */
class MediaConverter {
public:
    MediaConverter();
    ~MediaConverter();
    
    /**
     * Set output directory for converted files
     * @param outputDir Directory where converted files will be stored
     */
    void setOutputDirectory(const std::string& outputDir);
    
    /**
     * Queue a video file for conversion
     * @param sourcePath Path to input video file
     * @param convertVideo If true, convert video to HAP
     * @param extractAudio If true, extract audio to WAV
     * @return Job ID if queued successfully, empty string on error
     */
    std::string queueConversion(const std::string& sourcePath, 
                                 bool convertVideo = true, 
                                 bool extractAudio = true);
    
    /**
     * Cancel a conversion job
     * @param jobId Job ID to cancel
     * @return true if job was cancelled, false if not found or already completed
     */
    bool cancelJob(const std::string& jobId);
    
    /**
     * Get job status
     * @param jobId Job ID
     * @return Job status, or nullptr if not found
     */
    const ConversionJob* getJobStatus(const std::string& jobId) const;
    
    /**
     * Get all jobs
     * @return Vector of all job IDs
     */
    std::vector<std::string> getAllJobIds() const;
    
    /**
     * Get jobs by status
     * @param status Status to filter by
     * @return Vector of job IDs with the specified status
     */
    std::vector<std::string> getJobsByStatus(ConversionStatus status) const;
    
    /**
     * Update conversion progress (called from worker thread)
     * This is a callback that can be set to update UI
     */
    using ProgressCallback = std::function<void(const std::string& jobId, float progress, ConversionStatus status)>;
    void setProgressCallback(ProgressCallback callback);
    
    /**
     * Update - Call this from main thread to process queue and update status
     */
    void update();
    
    /**
     * Check if converter is busy (has active conversions)
     */
    bool isBusy() const;
    
    /**
     * Get number of pending jobs
     */
    size_t getPendingCount() const;
    
    /**
     * Get number of active (converting) jobs
     */
    size_t getActiveCount() const;
    
    /**
     * Set maximum number of concurrent conversion jobs
     * @param maxJobs Maximum parallel conversions (1-12, default: auto-detected based on CPU cores)
     *                For M1 Pro/Max systems, 6-8 is optimal. Lower-end systems should use 2-4.
     */
    void setMaxConcurrentJobs(size_t maxJobs);
    
    /**
     * Get maximum number of concurrent conversion jobs
     */
    size_t getMaxConcurrentJobs() const { return maxConcurrentJobs_.load(); }
    
private:
    // Output directory for converted files
    std::string outputDirectory_;
    mutable std::mutex outputDirMutex_;
    
    // Conversion queue and jobs
    std::queue<std::string> jobQueue_;              // Queue of job IDs
    std::map<std::string, ConversionJob> jobs_;     // All jobs by ID
    mutable std::mutex jobsMutex_;
    
    // Worker thread pool for parallel conversion
    std::vector<std::thread> workerThreads_;
    std::atomic<bool> shouldStop_;
    std::atomic<size_t> maxConcurrentJobs_;  // Max parallel conversions
    std::atomic<size_t> activeJobCount_;     // Current active conversions
    
    // Progress callback
    ProgressCallback progressCallback_;
    mutable std::mutex callbackMutex_;
    
    // ofxAVcpp instance (thread-safe for read-only, but we'll use one per conversion)
    // Note: ofxAVcpp is not thread-safe, so we create instances per job
    
    // Helper methods
    std::string generateJobId(const std::string& sourcePath) const;
    std::string generateOutputPath(const std::string& sourcePath, bool isVideo) const;
    void workerThreadFunction();
    bool processJob(ConversionJob& job);
    void notifyProgress(const std::string& jobId, float progress, ConversionStatus status);
};


