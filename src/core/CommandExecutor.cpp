#include "CommandExecutor.h"
#include "ModuleRegistry.h"
#include "ConnectionManager.h"
#include "gui/GUIManager.h"
#include "Module.h"
#include "AssetLibrary.h"
#include "ofLog.h"
#include "ofFileUtils.h"
#include "ofSystemUtils.h"
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unistd.h>  // for usleep

CommandExecutor::CommandExecutor() {
}

void CommandExecutor::setup(
    ModuleRegistry* registry_,
    GUIManager* guiManager_,
    ConnectionManager* connectionManager_,
    AssetLibrary* assetLibrary_
) {
    registry = registry_;
    guiManager = guiManager_;
    this->connectionManager_ = connectionManager_;
    this->assetLibrary = assetLibrary_;
}

void CommandExecutor::executeCommand(const std::string& command) {
    output("> %s", command.c_str());
    
    // Parse command
    auto [cmd, args] = parseCommand(command);
    
    // Convert to lowercase for comparison
    std::string cmdLower = cmd;
    std::transform(cmdLower.begin(), cmdLower.end(), cmdLower.begin(), ::tolower);
    
    // Execute
    if (cmdLower == "list" || cmdLower == "ls") {
        cmdList();
    } else if (cmdLower == "remove" || cmdLower == "rm" || cmdLower == "delete" || cmdLower == "del") {
        cmdRemove(args);
    } else if (cmdLower == "add") {
        cmdAdd(args);
    } else if (cmdLower == "route") {
        cmdRoute(args);
    } else if (cmdLower == "unroute") {
        cmdUnroute(args);
    } else if (cmdLower == "connections" || cmdLower == "conn") {
        cmdConnections(args);
    } else if (cmdLower == "import") {
        cmdImport(args);
    } else if (cmdLower == "help" || cmdLower == "?") {
        cmdHelp();
    } else if (cmdLower == "clear" || cmdLower == "cls") {
        cmdClear();
    } else {
        output("Error: Unknown command '%s'. Type 'help' for commands.", cmd.c_str());
    }
}

std::pair<std::string, std::string> CommandExecutor::parseCommand(const std::string& line) {
    std::istringstream iss(line);
    std::string cmd, args;
    iss >> cmd;
    std::getline(iss, args);
    args = trim(args);
    return {cmd, args};
}

std::string CommandExecutor::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

std::string CommandExecutor::getModuleTypeString(int type) {
    switch (static_cast<ModuleType>(type)) {
        case ModuleType::SEQUENCER: return "SEQUENCER";
        case ModuleType::INSTRUMENT: return "INSTRUMENT";
        case ModuleType::EFFECT: return "EFFECT";
        case ModuleType::UTILITY: return "UTILITY";
        default: return "UNKNOWN";
    }
}

std::vector<std::string> CommandExecutor::getAllModuleNames() const {
    if (!registry) {
        return {};
    }
    return registry->getAllHumanNames();
}

void CommandExecutor::cmdList() {
    if (!registry) {
        output("Error: Registry not set");
        return;
    }
    
    output("=== Modules ===");
    auto allNames = registry->getAllHumanNames();
    
    if (allNames.empty()) {
        output("No modules registered");
        return;
    }
    
    for (const auto& name : allNames) {
        auto module = registry->getModule(name);
        if (module) {
            std::string typeStr = getModuleTypeString(static_cast<int>(module->getType()));
            
            // Check if module has GUI
            bool hasGUI = false;
            if (guiManager) {
                auto* gui = guiManager->getGUI(name);
                hasGUI = (gui != nullptr);
            }
            
            std::string guiStatus = hasGUI ? "[GUI]" : "[NO GUI]";
            output("  %s [%s] %s", name.c_str(), typeStr.c_str(), guiStatus.c_str());
        } else {
            output("  %s [ERROR: Module not found]", name.c_str());
        }
    }
    output("Total: %zu modules", allNames.size());
}

void CommandExecutor::cmdRemove(const std::string& args) {
    if (args.empty()) {
        output("Usage: remove <module_name>");
        output("Example: remove pool2");
        return;
    }
    
    if (!registry) {
        output("Error: Registry not set");
        return;
    }
    
    if (!registry->hasModule(args)) {
        output("Error: Module '%s' not found", args.c_str());
        return;
    }
    
    if (!onRemoveModule) {
        output("Error: Remove callback not set");
        return;
    }
    
    // Execute removal
    onRemoveModule(args);
    
    // Check if removal actually succeeded
    if (registry->hasModule(args)) {
        output("Error: Failed to remove module '%s' (may be the last instance of its type)", args.c_str());
        ofLogWarning("CommandExecutor") << "Failed to remove module: " << args;
    } else {
        output("Removed module: %s", args.c_str());
        ofLogNotice("CommandExecutor") << "Removed module: " << args;
    }
}

void CommandExecutor::cmdAdd(const std::string& args) {
    if (args.empty()) {
        output("Usage: add <module_type>");
        output("Types: pool, tracker, MediaPool, TrackerSequencer");
        return;
    }
    
    if (!onAddModule) {
        output("Error: Add callback not set");
        return;
    }
    
    // Validate module type
    std::string typeLower = args;
    std::transform(typeLower.begin(), typeLower.end(), typeLower.begin(), ::tolower);
    
    std::string moduleType;
    if (typeLower == "pool" || typeLower == "mediapool") {
        moduleType = "MediaPool";
    } else if (typeLower == "tracker" || typeLower == "trackersequencer") {
        moduleType = "TrackerSequencer";
    } else {
        output("Error: Unknown module type '%s'", args.c_str());
        output("Valid types: pool, tracker, MediaPool, TrackerSequencer");
        return;
    }
    
    onAddModule(moduleType);
    output("Added module: %s", moduleType.c_str());
    ofLogNotice("CommandExecutor") << "Added module: " << moduleType;
}

void CommandExecutor::cmdRoute(const std::string& args) {
    if (args.empty()) {
        output("Usage: route <module> <target>");
        output("Example: route pool1 masterAudioOut");
        output("Example: route tracker1 pool1  (creates parameter/event connections)");
        return;
    }
    
    if (!connectionManager_) {
        output("Error: ConnectionManager not set");
        return;
    }
    
    if (!registry) {
        output("Error: ModuleRegistry not set");
        return;
    }
    
    // Parse: "module target" (simplified syntax)
    std::istringstream iss(args);
    std::string moduleName, targetName;
    iss >> moduleName;
    iss >> targetName;
    
    if (moduleName.empty() || targetName.empty()) {
        output("Error: Module and target names required");
        output("Usage: route <module> <target>");
        return;
    }
    
    // Get modules
    auto sourceModule = registry->getModule(moduleName);
    auto targetModule = registry->getModule(targetName);
    
    if (!sourceModule) {
        output("Error: Source module not found: %s", moduleName.c_str());
        return;
    }
    
    if (!targetModule) {
        output("Error: Target module not found: %s", targetName.c_str());
        return;
    }
    
    // Use port-based routing: automatically detect and create all compatible connections
    bool audioConnected = false;
    bool videoConnected = false;
    bool paramConnected = false;
    bool reverseParamConnected = false;
    bool eventConnected = false;
    
    // Check for audio connection (AUDIO_OUT -> AUDIO_IN)
    if (sourceModule->hasOutput(PortType::AUDIO_OUT) && 
        targetModule->hasInput(PortType::AUDIO_IN)) {
        audioConnected = connectionManager_->connectAudio(moduleName, targetName);
    }
    
    // Check for video connection (VIDEO_OUT -> VIDEO_IN)
    if (sourceModule->hasOutput(PortType::VIDEO_OUT) && 
        targetModule->hasInput(PortType::VIDEO_IN)) {
        videoConnected = connectionManager_->connectVideo(moduleName, targetName);
    }
    
    // Check for parameter connection (PARAMETER_OUT -> PARAMETER_IN)
    if (sourceModule->hasOutput(PortType::PARAMETER_OUT) && 
        targetModule->hasInput(PortType::PARAMETER_IN)) {
        auto sourceMetadata = sourceModule->getMetadata();
        auto targetMetadata = targetModule->getMetadata();
        
        if (!sourceMetadata.parameterNames.empty() && !targetMetadata.parameterNames.empty()) {
            std::string sourceParamName = sourceMetadata.parameterNames[0];
            std::string targetParamName = targetMetadata.parameterNames[0];
            
            // Create bidirectional parameter connection
            if (connectionManager_->connectParameterDirect(moduleName, sourceParamName, 
                                                          targetName, targetParamName, 
                                                          []() { return true; })) {
                paramConnected = true;
            }
            
            // Create reverse parameter connection (bidirectional sync)
            if (connectionManager_->connectParameterDirect(targetName, targetParamName, 
                                                           moduleName, sourceParamName, 
                                                           []() { return true; })) {
                reverseParamConnected = true;
            }
        }
    }
    
    // Check for event connection (EVENT_OUT -> EVENT_IN)
    if (sourceModule->hasOutput(PortType::EVENT_OUT) && 
        targetModule->hasInput(PortType::EVENT_IN)) {
        auto sourceMetadata = sourceModule->getMetadata();
        auto targetMetadata = targetModule->getMetadata();
        
        if (!sourceMetadata.eventNames.empty() && !targetMetadata.eventNames.empty()) {
            std::string eventName = sourceMetadata.eventNames[0];
            std::string handlerName = targetMetadata.eventNames[0];
            
            if (connectionManager_->subscribeEvent(moduleName, eventName, targetName, handlerName)) {
                eventConnected = true;
            }
        }
    }
    
    // Report results
    if (audioConnected || videoConnected || paramConnected || reverseParamConnected || eventConnected) {
        std::vector<std::string> types;
        if (audioConnected) types.push_back("audio");
        if (videoConnected) types.push_back("video");
        if (paramConnected || reverseParamConnected) types.push_back("parameter");
        if (eventConnected) types.push_back("event");
        
        std::string typesStr;
        for (size_t i = 0; i < types.size(); ++i) {
            if (i > 0) typesStr += ", ";
            typesStr += types[i];
        }
        
        output("Connected %s to %s [%s]", moduleName.c_str(), targetName.c_str(), typesStr.c_str());
    } else {
        output("Error: Failed to connect %s to %s", moduleName.c_str(), targetName.c_str());
        output("No compatible ports found between these modules");
    }
}

void CommandExecutor::cmdUnroute(const std::string& args) {
    if (args.empty()) {
        output("Usage: unroute <module> [from <mixer>]");
        output("Example: unroute pool1 from masterAudioMixer");
        output("Example: unroute pool1  (disconnects from all mixers)");
        return;
    }
    
    if (!connectionManager_) {
        output("Error: ConnectionManager not set");
        return;
    }
    
    // Parse: "module from mixer" or "module" or "module mixer"
    std::istringstream iss(args);
    std::string moduleName, fromKeyword, mixerName;
    iss >> moduleName;
    iss >> fromKeyword;
    iss >> mixerName;
    
    // Handle different formats
    if (fromKeyword == "from" && !mixerName.empty()) {
        // Format: "module from mixer"
    } else if (!fromKeyword.empty() && mixerName.empty()) {
        // Format: "module mixer" (fromKeyword is actually mixerName)
        mixerName = fromKeyword;
        fromKeyword = "";
    } else {
        // Format: "module" (disconnect from all)
        mixerName = "";
    }
    
    if (moduleName.empty()) {
        output("Error: Module name required");
        return;
    }
    
    if (mixerName.empty()) {
        // Disconnect all connections for this module
        if (connectionManager_->disconnectAll(moduleName)) {
            output("Disconnected %s from all connections", moduleName.c_str());
        } else {
            output("Error: Failed to disconnect %s", moduleName.c_str());
        }
    } else {
        // Disconnect from specific target (all connection types: audio, video, parameter, event)
        // Get all connections from source to target first
        auto connections = connectionManager_->getConnectionsFrom(moduleName);
        
        bool audioDisconnected = false;
        bool videoDisconnected = false;
        bool paramDisconnected = false;
        bool eventDisconnected = false;
        
        // Process each connection type
        for (const auto& conn : connections) {
            if (conn.targetModule != mixerName) continue;
            
            switch (conn.type) {
                case ConnectionManager::ConnectionType::AUDIO:
                    if (connectionManager_->disconnectAudio(moduleName, mixerName)) {
                        audioDisconnected = true;
                    }
                    break;
                case ConnectionManager::ConnectionType::VIDEO:
                    if (connectionManager_->disconnectVideo(moduleName, mixerName)) {
                        videoDisconnected = true;
                    }
                    break;
                case ConnectionManager::ConnectionType::PARAMETER:
                    if (!conn.sourcePath.empty() && connectionManager_->disconnectParameter(conn.sourcePath)) {
                        paramDisconnected = true;
                    }
                    break;
                case ConnectionManager::ConnectionType::EVENT:
                    // Use eventName and handlerName from the connection
                    if (!conn.eventName.empty()) {
                        if (connectionManager_->unsubscribeEvent(moduleName, conn.eventName, mixerName, conn.handlerName)) {
                        eventDisconnected = true;
                        }
                    }
                    break;
            }
        }
        
        if (audioDisconnected || videoDisconnected || paramDisconnected || eventDisconnected) {
            std::vector<std::string> types;
            if (audioDisconnected) types.push_back("audio");
            if (videoDisconnected) types.push_back("video");
            if (paramDisconnected) types.push_back("parameter");
            if (eventDisconnected) types.push_back("event");
            
            std::string typesStr;
            for (size_t i = 0; i < types.size(); ++i) {
                if (i > 0) typesStr += ", ";
                typesStr += types[i];
            }
            output("Disconnected %s from %s [%s]", moduleName.c_str(), mixerName.c_str(), typesStr.c_str());
        } else {
            output("Error: Failed to disconnect %s from %s (no connections found)", moduleName.c_str(), mixerName.c_str());
        }
    }
}

void CommandExecutor::cmdConnections(const std::string& args) {
    (void)args;  // Unused for now
    
    if (!connectionManager_) {
        output("Error: ConnectionManager not set");
        return;
    }
    
    auto connections = connectionManager_->getConnections();
    
    if (connections.empty()) {
        output("No connections");
        return;
    }
    
    output("=== Connections ===");
    for (const auto& conn : connections) {
        std::string typeStr;
        switch (conn.type) {
            case ConnectionManager::ConnectionType::AUDIO:
                typeStr = "audio";
                break;
            case ConnectionManager::ConnectionType::VIDEO:
                typeStr = "video";
                break;
            case ConnectionManager::ConnectionType::PARAMETER:
                typeStr = "parameter";
                break;
            case ConnectionManager::ConnectionType::EVENT:
                typeStr = "event";
                break;
        }
        output("  %s -> %s [%s]", conn.sourceModule.c_str(), conn.targetModule.c_str(), typeStr.c_str());
    }
    output("Total: %zu connections", connections.size());
}

void CommandExecutor::cmdImport(const std::string& args) {
    if (args.empty()) {
        output("Usage: import <url_or_path>");
        output("Examples:");
        output("  import https://youtu.be/kPUdhm2VE-o");
        output("  import /path/to/video.mp4");
        output("  import /path/to/folder");
        return;
    }
    
    if (!assetLibrary) {
        output("Error: AssetLibrary not available");
        return;
    }
    
    std::string input = trim(args);
    
    // Check if input is a URL (YouTube, etc.)
    bool isURL = (input.find("http://") == 0 || input.find("https://") == 0);
    
    if (isURL) {
        output("Downloading from URL: %s", input.c_str());
        output("Starting download...");  // Immediate feedback to reduce perceived freeze
        
        // Setup temp directory
        std::string tempDir = ofToDataPath("temp_downloads", true);
        ofDirectory dir(tempDir);
        if (!dir.exists()) {
            dir.create(true);
        }
        
        // Build output template - yt-dlp will substitute %(title)s and %(ext)s
        std::string outputTemplate = ofFilePath::join(tempDir, "%(title)s.%(ext)s");
        
        // Escape single quotes for shell (but NOT % - needed for yt-dlp template substitution)
        std::string escapedTemplate;
        for (char c : outputTemplate) {
            if (c == '\'') {
                escapedTemplate += "'\\''";  // Escape single quote in shell
            } else {
                escapedTemplate += c;
            }
        }
        
        // Try multiple download strategies (Android -> iOS -> Web with EJS)
        std::vector<std::string> strategies = {
            // Strategy 1: Android client (most reliable, no EJS needed)
            "yt-dlp --user-agent \"com.google.android.youtube/19.09.37 (Linux; U; Android 11) gzip\" "
            "--retries 3 --fragment-retries 3 "
            "--extractor-args \"youtube:player_client=android\" "
            "-f \"bestvideo+bestaudio/best\" -o '" + escapedTemplate + "' \"" + input + "\" 2>&1",
            
            // Strategy 2: iOS client (fallback)
            "yt-dlp --user-agent \"com.google.ios.youtube/19.09.3 (iPhone14,1; U; CPU iOS 15_6 like Mac OS X)\" "
            "--retries 3 --fragment-retries 3 "
            "--extractor-args \"youtube:player_client=ios\" "
            "-f \"bestvideo+bestaudio/best\" -o '" + escapedTemplate + "' \"" + input + "\" 2>&1",
            
            // Strategy 3: Web client with EJS (requires deno)
            "yt-dlp --user-agent \"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\" "
            "--retries 3 --fragment-retries 3 "
            "--remote-components ejs:github "
            "--extractor-args \"youtube:player_client=web\" "
            "-f \"bestvideo+bestaudio/best\" -o '" + escapedTemplate + "' \"" + input + "\" 2>&1"
        };
        
        bool success = false;
        std::string result;
        
        for (size_t i = 0; i < strategies.size() && !success; i++) {
            if (i > 0) {
                output("Retrying with different method...");
            }
            
            // Execute command
            FILE* pipe = popen(strategies[i].c_str(), "r");
            
            if (!pipe) {
                output("Error: Failed to execute yt-dlp. Is yt-dlp installed?");
                output("Install with: pip install yt-dlp");
                output("Or update with: pip install --upgrade yt-dlp");
                return;
            }
            
            // Read output in real-time and show progress
            char buffer[256];
            result.clear();
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                result += buffer;
                
                // Show important progress messages
                std::string line(buffer);
                if (line.find("[download]") != std::string::npos || 
                    line.find("ERROR") != std::string::npos ||
                    line.find("WARNING") != std::string::npos ||
                    line.find("[info]") != std::string::npos) {
                    output("%s", line.c_str());
                }
            }
            
            int status = pclose(pipe);
            
            if (status == 0) {
                success = true;
                break;
            }
            
            // Show error on last strategy
            if (i == strategies.size() - 1) {
                handleDownloadError(result, status);
                return;
            }
        }
        
        if (!success) {
            output("Error: All download strategies failed");
            return;
        }
        
        // Find downloaded file - parse from output or search directory
        std::string downloadedFile = findDownloadedFile(result, tempDir, dir);
        
        if (downloadedFile.empty()) {
            output("Error: Could not find downloaded file");
            return;
        }
        
        output("Downloaded: %s", ofFilePath::getFileName(downloadedFile).c_str());
        
        // Import the downloaded file
        std::string assetId = assetLibrary->importFile(downloadedFile, "");
        if (!assetId.empty()) {
            output("Imported as asset: %s", assetId.c_str());
        } else {
            output("Error: Failed to import downloaded file");
        }
        
    } else {
        // Regular file or folder path import
        if (!ofFile::doesFileExist(input)) {
            output("Error: File or folder does not exist: %s", input.c_str());
            return;
        }
        
        // Check if input is a directory
        ofFile file(input);
        if (file.isDirectory()) {
            // Import folder
            output("Importing folder: %s", ofFilePath::getFileName(input).c_str());
            
            // Extract folder name from path to use as subfolder name
            std::string folderName = ofFilePath::getFileName(input);
            if (folderName.empty()) {
                folderName = ofFilePath::getBaseName(input);
            }
            
            std::vector<std::string> assetIds = assetLibrary->importFolder(input, folderName);
            if (!assetIds.empty()) {
                output("Imported %zu asset(s) from folder: %s", assetIds.size(), folderName.c_str());
            } else {
                output("Error: Failed to import folder or folder contains no media files");
            }
        } else {
            // Import single file
            output("Importing file: %s", ofFilePath::getFileName(input).c_str());
            std::string assetId = assetLibrary->importFile(input, "");
            if (!assetId.empty()) {
                output("Imported as asset: %s", assetId.c_str());
            } else {
                output("Error: Failed to import file");
            }
        }
    }
}

void CommandExecutor::cmdHelp() {
    output("=== Commands ===");
    output("  list, ls              - List all modules");
    output("  remove <name>, rm     - Remove a module");
    output("  add <type>            - Add a module (pool, tracker)");
    output("  route <mod> <target> - Connect module to target");
    output("  unroute <mod> [from <target>] - Disconnect module from target");
    output("  connections, conn     - List all connections");
    output("  import <url_or_path>  - Import media from URL, file path, or folder");
    output("  clear, cls            - Clear console");
    output("  help, ?               - Show this help");
    output("");
    output("=== Examples ===");
    output("  list");
    output("  add pool");
    output("  add tracker");
    output("  import https://youtu.be/kPUdhm2VE-o");
    output("  import /path/to/video.mp4");
    output("  import /path/to/folder");
    output("  route pool1 masterAudioOut");
    output("  route pool2 masterVideoOut");
    output("  route tracker2 pool2  (creates parameter/event connections)");
    output("  unroute pool1 masterAudioOut");
    output("  connections");
    output("  remove pool2");
    output("");
    output("=== Shortcuts ===");
    output("  :                    - Toggle console");
    output("  Cmd+'='              - Toggle command bar");
    output("  Up/Down arrows       - Navigate command history");
    output("  Ctrl+C / Cmd+C       - Copy selected text");
}

void CommandExecutor::cmdClear() {
    // Clear is handled by the UI (Console), but we provide the command for consistency
    output("Console cleared.");
}

void CommandExecutor::output(const std::string& text) {
    if (outputCallback) {
        outputCallback(text);
    } else {
        ofLogNotice("CommandExecutor") << text;
    }
}

void CommandExecutor::output(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    output(std::string(buf));
}

// Helper function to find downloaded file from yt-dlp output or directory search
std::string CommandExecutor::findDownloadedFile(const std::string& ytdlpOutput, const std::string& tempDir, ofDirectory& dir) {
    // Try to parse filename from yt-dlp output
    std::istringstream stream(ytdlpOutput);
    std::string line;
    while (std::getline(stream, line)) {
        // Look for "[download] Destination:" line
        size_t destPos = line.find("[download] Destination:");
        if (destPos != std::string::npos) {
            size_t colonPos = line.find(":", destPos);
            if (colonPos != std::string::npos) {
                std::string path = line.substr(colonPos + 1);
                // Trim whitespace
                size_t first = path.find_first_not_of(" \t");
                size_t last = path.find_last_not_of(" \t\n\r");
                if (first != std::string::npos && last != std::string::npos) {
                    std::string candidate = path.substr(first, last - first + 1);
                    // Only use if template was substituted (no %( or \% in path)
                    if (candidate.find("%(") == std::string::npos && 
                        candidate.find("\\%") == std::string::npos &&
                        ofFile::doesFileExist(candidate)) {
                        return candidate;
                    }
                }
            }
        }
    }
    
    // Fallback: search for largest media file in temp directory
    // Wait a bit for file system to sync, then retry
    for (int retry = 0; retry < 5; retry++) {
        if (retry > 0) {
            usleep(100000);  // 100ms delay
        }
        
        dir.listDir();
        std::string largestFile;
        size_t largestSize = 0;
        
        // Media file extensions
        static const std::vector<std::string> mediaExts = {
            "mp4", "mov", "webm", "mkv", "wav", "mp3", "m4a", "aiff", "flac", "aif"
        };
        
        for (int i = 0; i < dir.size(); i++) {
            ofFile file = dir.getFile(i);
            if (file.isFile()) {
                std::string ext = ofToLower(ofFilePath::getFileExt(file.path()));
                bool isMediaFile = std::find(mediaExts.begin(), mediaExts.end(), ext) != mediaExts.end();
                
                if (isMediaFile) {
                    size_t fileSize = file.getSize();
                    if (fileSize > largestSize) {
                        largestSize = fileSize;
                        largestFile = file.path();
                    }
                }
            }
        }
        
        if (!largestFile.empty() && ofFile::doesFileExist(largestFile)) {
            return largestFile;
        }
    }
    
    return "";
}

// Helper function to handle download errors with helpful messages
void CommandExecutor::handleDownloadError(const std::string& result, int status) {
    output("Error: yt-dlp failed with exit code %d", status);
    
    // Provide helpful error messages based on common issues
    if (result.find("HTTP Error 403") != std::string::npos || 
        result.find("403 Forbidden") != std::string::npos) {
        output("");
        output("YouTube blocked the download (403 Forbidden).");
        output("Try updating yt-dlp: pip install --upgrade yt-dlp");
        output("Or use: brew upgrade yt-dlp");
        output("");
    } else if (result.find("Requested format is not available") != std::string::npos ||
               result.find("Only images are available") != std::string::npos) {
        output("");
        output("YouTube is blocking video formats (EJS challenge).");
        output("Try installing deno for challenge solving:");
        output("  brew install deno");
        output("Or update yt-dlp: pip install --upgrade yt-dlp");
        output("");
    } else if (result.find("Sign in to confirm your age") != std::string::npos) {
        output("");
        output("Video requires age verification. Cannot download.");
        output("");
    } else if (result.find("Private video") != std::string::npos) {
        output("");
        output("Video is private. Cannot download.");
        output("");
    }
    
    // Show error output (truncated)
    if (!result.empty()) {
        output("Error details:");
        std::istringstream iss(result);
        std::string line;
        int lineCount = 0;
        while (std::getline(iss, line) && lineCount < 15) {
            if (line.find("ERROR") != std::string::npos || 
                line.find("WARNING") != std::string::npos) {
                output("%s", line.c_str());
                lineCount++;
            }
        }
    }
}

