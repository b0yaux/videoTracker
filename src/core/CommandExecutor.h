#pragma once

#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>

class ModuleRegistry;
class GUIManager;
class ConnectionManager;
class Module;
class AssetLibrary;
class ofDirectory;

/**
 * CommandExecutor - Backend for command execution logic
 * 
 * RESPONSIBILITY: Command parsing, validation, and execution
 * 
 * This class contains all command logic that is shared between:
 * - Console (text-based UI)
 * - CommandBar (palette-based UI)
 * 
 * Separation of Concerns:
 * - CommandExecutor: Command logic and execution
 * - Console: Text-based UI rendering
 * - CommandBar: Palette-based UI rendering
 */
class CommandExecutor {
public:
    CommandExecutor();
    ~CommandExecutor();
    
    // Setup with dependencies
    void setup(
        ModuleRegistry* registry_,
        GUIManager* guiManager_,
        ConnectionManager* connectionManager_,
        AssetLibrary* assetLibrary_ = nullptr
    );
    
    // Set callbacks for module operations
    void setOnAddModule(std::function<void(const std::string&)> callback) { onAddModule = callback; }
    void setOnRemoveModule(std::function<void(const std::string&)> callback) { onRemoveModule = callback; }
    
    // Set output callback (for logging results)
    void setOutputCallback(std::function<void(const std::string&)> callback) { outputCallback = callback; }
    
    // Execute a command string (parses and executes)
    void executeCommand(const std::string& command);
    
    // Update method - call from main thread to process background download messages
    void update();
    
    // Command handlers (can be called directly or via executeCommand)
    void cmdList();
    void cmdRemove(const std::string& args);
    void cmdAdd(const std::string& args);
    void cmdRoute(const std::string& args);
    void cmdUnroute(const std::string& args);
    void cmdConnections(const std::string& args);
    void cmdImport(const std::string& args);
    void cmdHelp();
    void cmdClear();
    
    // Helper methods
    static std::pair<std::string, std::string> parseCommand(const std::string& line);
    static std::string trim(const std::string& str);
    static std::string getModuleTypeString(int type);
    
    // Get module names for command bar registration
    std::vector<std::string> getAllModuleNames() const;
    
private:
    ModuleRegistry* registry = nullptr;
    GUIManager* guiManager = nullptr;
    ConnectionManager* connectionManager_ = nullptr;
    AssetLibrary* assetLibrary = nullptr;
    
    std::function<void(const std::string&)> onAddModule;
    std::function<void(const std::string&)> onRemoveModule;
    std::function<void(const std::string&)> outputCallback;
    
    // Internal helper to output messages
    void output(const std::string& text);
    void output(const char* fmt, ...);
    
    // Helper functions for import command
    std::string findDownloadedFile(const std::string& ytdlpOutput, const std::string& tempDir, ofDirectory& dir);
    void handleDownloadError(const std::string& result, int status);
    
    // Background download system
    struct DownloadJob {
        std::string url;
        std::string ytdlpPath;
        std::string tempDir;
        bool isActive = true;
    };
    
    struct ImportJob {
        std::string filePath;
    };
    
    std::thread downloadThread_;
    std::atomic<bool> shouldStopDownloadThread_;
    std::mutex downloadMutex_;
    std::condition_variable downloadCondition_;
    std::queue<DownloadJob> downloadQueue_;
    std::mutex messageMutex_;
    std::queue<std::string> messageQueue_;
    std::mutex importMutex_;
    std::queue<ImportJob> importQueue_;
    
    void downloadThreadFunction();
    void processDownload(const DownloadJob& job);
    void queueMessage(const std::string& message);
};

