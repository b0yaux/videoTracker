#include "CommandExecutor.h"
#include "ModuleRegistry.h"
#include "ConnectionManager.h"
#include "Module.h"
#include "AssetLibrary.h"
#include "Clock.h"
#include "PatternRuntime.h"
#include "data/Pattern.h"
#include "core/Engine.h"
#include "core/Command.h"
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
#include <cstdlib>  // for getenv
#include <sys/wait.h>  // for WIFEXITED, WEXITSTATUS
#include <sys/stat.h>  // for stat, st_mtime
#include <condition_variable>
#include <sstream>

CommandExecutor::CommandExecutor() 
    : shouldStopDownloadThread_(false) {
    // Start download thread
    downloadThread_ = std::thread(&CommandExecutor::downloadThreadFunction, this);
}

CommandExecutor::~CommandExecutor() {
    // Signal download thread to stop
    shouldStopDownloadThread_ = true;
    
    // CRITICAL: Notify condition variable to wake waiting thread
    // Without this, the thread may be stuck waiting on downloadCondition_
    downloadCondition_.notify_all();
    
    // Wait for thread to finish
    if (downloadThread_.joinable()) {
        downloadThread_.join();
    }
}

void CommandExecutor::setup(
    ModuleRegistry* registry_,
    ConnectionManager* connectionManager_,
    AssetLibrary* assetLibrary_,
    Clock* clock_,
    PatternRuntime* patternRuntime_,
    vt::Engine* engine_
) {
    registry = registry_;
    this->connectionManager_ = connectionManager_;
    this->assetLibrary = assetLibrary_;
    clock = clock_;
    patternRuntime = patternRuntime_;
    this->engine_ = engine_;
}

void CommandExecutor::executeCommand(const std::string& command) {
    output("> %s", command.c_str());
    
    // Parse command
    auto [cmd, args] = parseCommand(command);
    
    // Convert to lowercase for comparison
    std::string cmdLower = cmd;
    std::transform(cmdLower.begin(), cmdLower.end(), cmdLower.begin(), ::tolower);
    
    // Check for object-oriented commands (pattern, chain, sequencer, module, session)
    if (cmdLower == "pattern") {
        // Parse pattern command: pattern <action> [args...]
        // Handle case where args might be empty or just whitespace
        if (args.empty() || trim(args).empty()) {
            output("Usage: pattern [create|delete|play|stop|reset|pause|ls|info]");
            output("Example: pattern ls");
            return;
        }
        
        auto [action, actionArgs] = parseCommand(args);
        std::string actionLower = action;
        std::transform(actionLower.begin(), actionLower.end(), actionLower.begin(), ::tolower);
        
        if (actionLower == "ls" || actionLower == "list") {
            cmdPatternList();
        } else if (actionLower == "create") {
            cmdPatternCreate(actionArgs);
        } else if (actionLower == "delete" || actionLower == "del" || actionLower == "remove" || actionLower == "rm") {
            cmdPatternDelete(actionArgs);
        } else if (actionLower == "play") {
            cmdPatternPlay(actionArgs);
        } else if (actionLower == "stop") {
            cmdPatternStop(actionArgs);
        } else if (actionLower == "reset") {
            cmdPatternReset(actionArgs);
        } else if (actionLower == "pause") {
            cmdPatternPause(actionArgs);
        } else if (actionLower == "info") {
            cmdPatternInfo(actionArgs);
        } else {
            output("Error: Unknown pattern command '%s'. Use: pattern [create|delete|play|stop|reset|pause|ls|info]", action.c_str());
        }
        return;
    }
    
    // Check for chain commands
    if (cmdLower == "chain") {
        // Parse chain command: chain <action> [args...]
        if (args.empty() || trim(args).empty()) {
            output("Usage: chain [create|delete|ls|info|add|remove|repeat|enable|disable|clear|reset]");
            output("Example: chain ls");
            return;
        }
        
        auto [action, actionArgs] = parseCommand(args);
        std::string actionLower = action;
        std::transform(actionLower.begin(), actionLower.end(), actionLower.begin(), ::tolower);
        
        if (actionLower == "ls" || actionLower == "list") {
            cmdChainList();
        } else if (actionLower == "create") {
            cmdChainCreate(actionArgs);
        } else if (actionLower == "delete" || actionLower == "del" || actionLower == "remove" || actionLower == "rm") {
            cmdChainDelete(actionArgs);
        } else if (actionLower == "info") {
            cmdChainInfo(actionArgs);
        } else if (actionLower == "add") {
            cmdChainAdd(actionArgs);
        } else if (actionLower == "remove" || actionLower == "rm") {
            cmdChainRemove(actionArgs);
        } else if (actionLower == "repeat") {
            cmdChainRepeat(actionArgs);
        } else if (actionLower == "enable" || actionLower == "on") {
            cmdChainEnable(actionArgs);
        } else if (actionLower == "disable" || actionLower == "off") {
            cmdChainDisable(actionArgs);
        } else if (actionLower == "clear") {
            cmdChainClear(actionArgs);
        } else if (actionLower == "reset") {
            cmdChainReset(actionArgs);
        } else {
            output("Error: Unknown chain command '%s'. Use: chain [create|delete|ls|info|add|remove|repeat|enable|disable|clear|reset]", action.c_str());
        }
        return;
    }
    
    // Check for sequencer commands
    if (cmdLower == "sequencer" || cmdLower == "seq") {
        // Parse sequencer command: sequencer [<sequencerName>] <action> [args...]
        // or: sequencer <action> [args...] (for ls, info without name)
        if (args.empty() || trim(args).empty()) {
            output("Usage: sequencer [ls|info] or sequencer <name> [bind|unbind|enable|disable|info]");
            output("Example: sequencer ls");
            output("Example: sequencer trackerSequencer1 bind pattern P1");
            return;
        }
        
        // Try to parse as: sequencer <name> <action> [args...]
        // or: sequencer <action> [args...]
        auto [firstArg, restArgs] = parseCommand(args);
        std::string firstArgLower = firstArg;
        std::transform(firstArgLower.begin(), firstArgLower.end(), firstArgLower.begin(), ::tolower);
        
        // Check if first arg is an action (ls, info) or a sequencer name
        if (firstArgLower == "ls" || firstArgLower == "list") {
            cmdSequencerList();
        } else if (firstArgLower == "info") {
            cmdSequencerInfo(restArgs);
        } else {
            // First arg is likely a sequencer name, parse action
            if (restArgs.empty() || trim(restArgs).empty()) {
                output("Usage: sequencer <name> [bind|unbind|enable|disable|info]");
                output("Example: sequencer trackerSequencer1 bind pattern P1");
                return;
            }
            
            auto [action, actionArgs] = parseCommand(restArgs);
            std::string actionLower = action;
            std::transform(actionLower.begin(), actionLower.end(), actionLower.begin(), ::tolower);
            
            // Build full args with sequencer name: "<name> <actionArgs>"
            std::string fullArgs = firstArg + " " + actionArgs;
            
            if (actionLower == "info") {
                cmdSequencerInfo(firstArg);  // Just pass sequencer name
            } else if (actionLower == "bind") {
                // Parse: bind pattern <patternName> or bind chain <chainName>
                if (actionArgs.empty() || trim(actionArgs).empty()) {
                    output("Usage: sequencer <name> bind [pattern|chain] <name>");
                    return;
                }
                auto [bindType, bindTarget] = parseCommand(actionArgs);
                std::string bindTypeLower = bindType;
                std::transform(bindTypeLower.begin(), bindTypeLower.end(), bindTypeLower.begin(), ::tolower);
                
                if (bindTypeLower == "pattern") {
                    cmdSequencerBindPattern(firstArg + " " + bindTarget);
                } else if (bindTypeLower == "chain") {
                    cmdSequencerBindChain(firstArg + " " + bindTarget);
                } else {
                    output("Error: Unknown bind type '%s'. Use: bind [pattern|chain] <name>", bindType.c_str());
                }
            } else if (actionLower == "unbind") {
                // Parse: unbind pattern or unbind chain
                if (actionArgs.empty() || trim(actionArgs).empty()) {
                    output("Usage: sequencer <name> unbind [pattern|chain]");
                    return;
                }
                auto [unbindType, _] = parseCommand(actionArgs);
                std::string unbindTypeLower = unbindType;
                std::transform(unbindTypeLower.begin(), unbindTypeLower.end(), unbindTypeLower.begin(), ::tolower);
                
                if (unbindTypeLower == "pattern") {
                    cmdSequencerUnbindPattern(firstArg);
                } else if (unbindTypeLower == "chain") {
                    cmdSequencerUnbindChain(firstArg);
                } else {
                    output("Error: Unknown unbind type '%s'. Use: unbind [pattern|chain]", unbindType.c_str());
                }
            } else if (actionLower == "enable" || actionLower == "on") {
                // Parse: enable chain
                if (actionArgs.empty() || trim(actionArgs).empty()) {
                    output("Usage: sequencer <name> enable chain");
                    return;
                }
                auto [enableType, _] = parseCommand(actionArgs);
                std::string enableTypeLower = enableType;
                std::transform(enableTypeLower.begin(), enableTypeLower.end(), enableTypeLower.begin(), ::tolower);
                
                if (enableTypeLower == "chain") {
                    cmdSequencerEnableChain(firstArg);
                } else {
                    output("Error: Unknown enable type '%s'. Use: enable chain", enableType.c_str());
                }
            } else if (actionLower == "disable" || actionLower == "off") {
                // Parse: disable chain
                if (actionArgs.empty() || trim(actionArgs).empty()) {
                    output("Usage: sequencer <name> disable chain");
                    return;
                }
                auto [disableType, _] = parseCommand(actionArgs);
                std::string disableTypeLower = disableType;
                std::transform(disableTypeLower.begin(), disableTypeLower.end(), disableTypeLower.begin(), ::tolower);
                
                if (disableTypeLower == "chain") {
                    cmdSequencerDisableChain(firstArg);
                } else {
                    output("Error: Unknown disable type '%s'. Use: disable chain", disableType.c_str());
                }
            } else {
                output("Error: Unknown sequencer command '%s'. Use: sequencer <name> [bind|unbind|enable|disable|info]", action.c_str());
            }
        }
        return;
    }
    
    // Legacy commands (for backward compatibility)
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
    } else if (cmdLower == "play" || cmdLower == "start") {
        cmdPlay();
    } else if (cmdLower == "stop") {
        cmdStop();
    } else if (cmdLower == "bpm") {
        cmdBPM(args);
    } else if (cmdLower == "get" || cmdLower == "param") {
        cmdGetParam(args);
    } else if (cmdLower == "set") {
        cmdSetParam(args);
    } else if (cmdLower == "info") {
        cmdInfo(args);
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
            
            // Check if module has GUI (using callback if registered)
            bool hasGUI = false;
            if (hasGUICallback_) {
                hasGUI = hasGUICallback_(name);
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
    
    // Route through command queue
    if (engine_) {
        auto cmd = std::make_unique<vt::RemoveModuleCommand>(args);
        if (engine_->enqueueCommand(std::move(cmd))) {
            output("Removed module: %s", args.c_str());
            ofLogNotice("CommandExecutor") << "Removed module: " << args;
        } else {
            output("Error: Failed to enqueue remove module command");
        }
    } else {
        output("Error: Engine not available");
    }
}

void CommandExecutor::cmdAdd(const std::string& args) {
    if (args.empty()) {
        output("Usage: add <module_type>");
        output("Types: pool, tracker, MultiSampler, TrackerSequencer");
        return;
    }
    
    // Validate module type
    std::string typeLower = args;
    std::transform(typeLower.begin(), typeLower.end(), typeLower.begin(), ::tolower);
    
    std::string moduleType;
    if (typeLower == "pool" || typeLower == "multisampler") {
        moduleType = "MultiSampler";
    } else if (typeLower == "tracker" || typeLower == "trackersequencer") {
        moduleType = "TrackerSequencer";
    } else {
        output("Error: Unknown module type '%s'", args.c_str());
        output("Valid types: pool, tracker, MultiSampler, TrackerSequencer");
        return;
    }
    
    // Route through command queue
    if (engine_) {
        auto cmd = std::make_unique<vt::AddModuleCommand>(moduleType, "");  // Empty name = auto-generate
        if (engine_->enqueueCommand(std::move(cmd))) {
            output("Added module: %s", moduleType.c_str());
            ofLogNotice("CommandExecutor") << "Added module: " << moduleType;
            // Still call onAddModule callback for UI notifications if needed
            if (onAddModule) {
                onAddModule(moduleType);
            }
        } else {
            output("Error: Failed to enqueue add module command");
        }
    } else {
        output("Error: Engine not available");
    }
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
    // Route through command queue for thread safety
    bool audioConnected = false;
    bool videoConnected = false;
    bool paramConnected = false;
    bool reverseParamConnected = false;
    bool eventConnected = false;
    
    if (!engine_) {
        output("Error: Engine not available");
        return;
    }
    
    // Check for audio connection (AUDIO_OUT -> AUDIO_IN)
    if (sourceModule->hasOutput(PortType::AUDIO_OUT) && 
        targetModule->hasInput(PortType::AUDIO_IN)) {
        auto cmd = std::make_unique<vt::ConnectCommand>(moduleName, targetName, 
                                                         ConnectionManager::ConnectionType::AUDIO);
        if (engine_->enqueueCommand(std::move(cmd))) {
            audioConnected = true;
        }
    }
    
    // Check for video connection (VIDEO_OUT -> VIDEO_IN)
    if (sourceModule->hasOutput(PortType::VIDEO_OUT) && 
        targetModule->hasInput(PortType::VIDEO_IN)) {
        auto cmd = std::make_unique<vt::ConnectCommand>(moduleName, targetName, 
                                                         ConnectionManager::ConnectionType::VIDEO);
        if (engine_->enqueueCommand(std::move(cmd))) {
            videoConnected = true;
        }
    }
    
    // Check for parameter connection (PARAMETER_OUT -> PARAMETER_IN)
    // NOTE: ConnectCommand has TODO for parameter connections - using direct call for now
    // TODO: Route through ConnectCommand once parameter connection support is added
    if (sourceModule->hasOutput(PortType::PARAMETER_OUT) && 
        targetModule->hasInput(PortType::PARAMETER_IN)) {
        auto sourceMetadata = sourceModule->getMetadata();
        auto targetMetadata = targetModule->getMetadata();
        
        if (!sourceMetadata.parameterNames.empty() && !targetMetadata.parameterNames.empty()) {
            std::string sourceParamName = sourceMetadata.parameterNames[0];
            std::string targetParamName = targetMetadata.parameterNames[0];
            
            // Create bidirectional parameter connection (direct call until ConnectCommand supports it)
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
    // NOTE: ConnectCommand has TODO for event connections - using direct call for now
    // TODO: Route through ConnectCommand once event connection support is added
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
    
    // Route through command queue
    if (!engine_) {
        output("Error: Engine not available");
        return;
    }
    
    if (mixerName.empty()) {
        // Disconnect all connections for this module
        // Use DisconnectCommand with empty target (disconnect from all)
        auto cmd = std::make_unique<vt::DisconnectCommand>(moduleName, "", std::nullopt);
        if (engine_->enqueueCommand(std::move(cmd))) {
            output("Disconnected %s from all connections", moduleName.c_str());
        } else {
            output("Error: Failed to enqueue disconnect command");
        }
    } else {
        // Disconnect from specific target (all connection types)
        // Use DisconnectCommand with target and nullopt connection type (all types)
        // NOTE: DisconnectCommand may not fully handle parameter/event disconnection yet
        // TODO: Enhance DisconnectCommand to handle all connection types properly
        auto cmd = std::make_unique<vt::DisconnectCommand>(moduleName, mixerName, std::nullopt);
        if (engine_->enqueueCommand(std::move(cmd))) {
            output("Disconnected %s from %s", moduleName.c_str(), mixerName.c_str());
        } else {
            output("Error: Failed to enqueue disconnect command");
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

static std::string findYtDlpPath() {
    // Common installation paths for yt-dlp
    std::vector<std::string> commonPaths = {
        "/usr/local/bin/yt-dlp",
        "/opt/homebrew/bin/yt-dlp",
        "/usr/bin/yt-dlp",
        "~/Library/Python/3.11/bin/yt-dlp",
        "~/Library/Python/3.10/bin/yt-dlp",
        "~/Library/Python/3.9/bin/yt-dlp",
        "~/.local/bin/yt-dlp"
    };
    
    // First, try to find via 'which' command
    FILE* whichPipe = popen("which yt-dlp 2>/dev/null", "r");
    if (whichPipe) {
        char buffer[512];
        if (fgets(buffer, sizeof(buffer), whichPipe) != nullptr) {
            std::string path = CommandExecutor::trim(std::string(buffer));
            pclose(whichPipe);
            if (!path.empty() && ofFile::doesFileExist(path)) {
                return path;
            }
        } else {
            pclose(whichPipe);
        }
    }
    
    // Try common paths
    for (const auto& path : commonPaths) {
        std::string expandedPath = path;
        // Expand ~ to home directory
        if (expandedPath[0] == '~') {
            expandedPath = std::string(getenv("HOME")) + expandedPath.substr(1);
        }
        if (ofFile::doesFileExist(expandedPath)) {
            return expandedPath;
        }
    }
    
    return ""; // Not found
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
        // Find yt-dlp executable
        std::string ytdlpPath = findYtDlpPath();
        if (ytdlpPath.empty()) {
            output("Error: yt-dlp not found. Please install it:");
            output("  macOS: brew install yt-dlp");
            output("  Or: pip3 install yt-dlp");
            output("  Or: pip install yt-dlp");
            output("");
            output("After installation, ensure it's in your PATH or restart the application.");
            return;
        }
        
        output("Downloading from URL: %s", input.c_str());
        output("Using yt-dlp: %s", ytdlpPath.c_str());
        output("Starting download in background...");
        
        // Setup temp directory
        std::string tempDir = ofToDataPath("temp_downloads", true);
        ofDirectory dir(tempDir);
        if (!dir.exists()) {
            dir.create(true);
        }
        
        // Queue download job for background processing
        DownloadJob job;
        job.url = input;
        job.ytdlpPath = ytdlpPath;
        job.tempDir = tempDir;
        job.isActive = true;
        
        {
            std::lock_guard<std::mutex> lock(downloadMutex_);
            downloadQueue_.push(job);
            downloadCondition_.notify_one();
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
    output("  play, start           - Start transport");
    output("  stop                  - Stop transport");
    output("  bpm [value]           - Get or set BPM");
    output("  get <mod> <param>     - Get module parameter value");
    output("  set <mod> <param> <value> - Set module parameter value");
    output("  info <mod>            - Show module information");
    output("  clear, cls            - Clear console");
    output("  help, ?               - Show this help");
    output("");
    output("=== Pattern Commands ===");
    output("  pattern ls            - List all patterns");
    output("  pattern create <name> [steps] - Create pattern (default: 16 steps)");
    output("  pattern delete <name> - Delete pattern");
    output("  pattern play <name>   - Start pattern playback");
    output("  pattern stop <name>   - Stop pattern playback");
    output("  pattern reset <name> - Reset pattern to step 0");
    output("  pattern pause <name>  - Pause pattern");
    output("  pattern info <name>  - Show pattern details");
    output("");
    output("=== Chain Commands ===");
    output("  chain ls              - List all chains");
    output("  chain create [name]   - Create chain (auto-name if omitted)");
    output("  chain delete <name>   - Delete chain");
    output("  chain info <name>     - Show chain details");
    output("  chain add <chain> <pattern> [index] - Add pattern to chain");
    output("  chain remove <chain> <index> - Remove pattern from chain");
    output("  chain repeat <chain> <index> <count> - Set repeat count");
    output("  chain enable <name>   - Enable chain");
    output("  chain disable <name>  - Disable chain");
    output("  chain clear <name>    - Clear all entries from chain");
    output("  chain reset <name>    - Reset chain state");
    output("");
    output("=== Sequencer Commands ===");
    output("  sequencer ls          - List all sequencers");
    output("  sequencer info [<name>] - Show sequencer binding info (all if no name)");
    output("  sequencer <name> bind pattern <patternName> - Bind sequencer to pattern");
    output("  sequencer <name> bind chain <chainName> - Bind sequencer to chain");
    output("  sequencer <name> unbind pattern - Unbind pattern from sequencer");
    output("  sequencer <name> unbind chain - Unbind chain from sequencer");
    output("  sequencer <name> enable chain - Enable chain for sequencer");
    output("  sequencer <name> disable chain - Disable chain for sequencer");
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

}

void CommandExecutor::cmdClear() {
    // Clear is handled by the UI (Console), but we provide the command for consistency
    output("Console cleared.");
}

void CommandExecutor::cmdPlay() {
    if (!clock) {
        output("Error: Clock not available");
        return;
    }
    
    if (engine_) {
        auto cmd = std::make_unique<vt::StartTransportCommand>();
        if (engine_->enqueueCommand(std::move(cmd))) {
            output("Transport started");
        } else {
            output("Error: Failed to enqueue start transport command");
        }
    } else {
        output("Error: Engine not available");
    }
}

void CommandExecutor::cmdStop() {
    if (!clock) {
        output("Error: Clock not available");
        return;
    }
    
    if (engine_) {
        auto cmd = std::make_unique<vt::StopTransportCommand>();
        if (engine_->enqueueCommand(std::move(cmd))) {
            output("Transport stopped");
        } else {
            output("Error: Failed to enqueue stop transport command");
        }
    } else {
        output("Error: Engine not available");
    }
}

void CommandExecutor::cmdBPM(const std::string& args) {
    if (!clock) {
        output("Error: Clock not available");
        return;
    }
    
    if (args.empty()) {
        // Get current BPM (query, no command needed)
        float bpm = clock->getBPM();
        output("Current BPM: %.2f", bpm);
    } else {
        // Set BPM (use command)
        try {
            float value = std::stof(args);
            if (engine_ && value > 0.0f) {
                auto cmd = std::make_unique<vt::SetBPMCommand>(value);
                if (engine_->enqueueCommand(std::move(cmd))) {
                    output("BPM set to %.2f", value);
                } else {
                    output("Error: Failed to enqueue BPM command");
                }
            } else {
                output("Error: Invalid BPM value (must be > 0)");
            }
        } catch (const std::exception& e) {
            output("Error: %s", e.what());
        }
    }
}

void CommandExecutor::cmdGetParam(const std::string& args) {
    if (args.empty()) {
        output("Usage: get <module> <parameter>");
        output("Example: get pool1 volume");
        return;
    }
    
    if (!registry) {
        output("Error: Registry not set");
        return;
    }
    
    std::istringstream iss(args);
    std::string moduleName, paramName;
    iss >> moduleName;
    iss >> paramName;
    
    if (moduleName.empty() || paramName.empty()) {
        output("Error: Module and parameter names required");
        output("Usage: get <module> <parameter>");
        return;
    }
    
    auto module = registry->getModule(moduleName);
    if (!module) {
        output("Error: Module '%s' not found", moduleName.c_str());
        return;
    }
    
    try {
        float value = module->getParameter(paramName);
        output("%s.%s = %.4f", moduleName.c_str(), paramName.c_str(), value);
    } catch (const std::exception& e) {
        output("Error: %s", e.what());
    }
}

void CommandExecutor::cmdSetParam(const std::string& args) {
    if (args.empty()) {
        output("Usage: set <module> <parameter> <value>");
        output("Example: set pool1 volume 0.8");
        return;
    }
    
    if (!registry) {
        output("Error: Registry not set");
        return;
    }
    
    std::istringstream iss(args);
    std::string moduleName, paramName, valueStr;
    iss >> moduleName;
    iss >> paramName;
    iss >> valueStr;
    
    if (moduleName.empty() || paramName.empty() || valueStr.empty()) {
        output("Error: Module, parameter, and value required");
        output("Usage: set <module> <parameter> <value>");
        return;
    }
    
    auto module = registry->getModule(moduleName);
    if (!module) {
        output("Error: Module '%s' not found", moduleName.c_str());
        return;
    }
    
    try {
        float value = std::stof(valueStr);
        
        // CRITICAL FIX: Use command queue for thread safety
        // Direct parameter access from CommandExecutor (main thread) is unsafe
        // when audio thread is processing
        auto cmd = std::make_unique<vt::SetParameterCommand>(moduleName, paramName, value);
        if (engine_ && engine_->enqueueCommand(std::move(cmd))) {
            output("%s.%s = %.4f", moduleName.c_str(), paramName.c_str(), value);
        } else {
            // Fallback: direct access (only if command queue unavailable)
            // This should rarely happen, but provides backward compatibility
        module->setParameter(paramName, value);
        output("%s.%s = %.4f", moduleName.c_str(), paramName.c_str(), value);
        }
    } catch (const std::exception& e) {
        output("Error: %s", e.what());
    }
}

void CommandExecutor::cmdInfo(const std::string& args) {
    if (args.empty()) {
        output("Usage: info <module>");
        output("Example: info pool1");
        return;
    }
    
    if (!registry) {
        output("Error: Registry not set");
        return;
    }
    
    auto module = registry->getModule(args);
    if (!module) {
        output("Error: Module '%s' not found", args.c_str());
        return;
    }
    
    auto metadata = module->getMetadata();
    output("=== Module: %s ===", args.c_str());
    output("Type: %s", metadata.typeName.c_str());
    output("Enabled: %s", module->isEnabled() ? "yes" : "no");
    
    // List parameters
    if (!metadata.parameterNames.empty()) {
        output("Parameters:");
        for (const auto& paramName : metadata.parameterNames) {
            try {
                float value = module->getParameter(paramName);
                output("  %s = %.4f", paramName.c_str(), value);
            } catch (...) {
                output("  %s = (error reading)", paramName.c_str());
            }
        }
    }
    
    // List events
    if (!metadata.eventNames.empty()) {
        output("Events:");
        for (const auto& eventName : metadata.eventNames) {
            output("  %s", eventName.c_str());
        }
    }
    
    // Ports
    auto inputPorts = module->getInputPorts();
    auto outputPorts = module->getOutputPorts();
    if (!inputPorts.empty() || !outputPorts.empty()) {
        output("Ports:");
        if (!inputPorts.empty()) {
            output("  Inputs:");
            for (const auto& port : inputPorts) {
                output("    %s", port.name.c_str());
            }
        }
        if (!outputPorts.empty()) {
            output("  Outputs:");
            for (const auto& port : outputPorts) {
                output("    %s", port.name.c_str());
            }
        }
    }
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
    std::string candidateFromOutput;
    
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
        
        // Also look for "[download]" lines that show the filename (even if already downloaded)
        // Format: "[download] filename.mp4 has already been downloaded"
        // or: "[download] 100% of size filename.mp4"
        size_t downloadPos = line.find("[download]");
        if (downloadPos != std::string::npos) {
            // Extract potential filename from the line
            // Look for common patterns: "filename.mp4", "filename.mov", etc.
            std::string lowerLine = ofToLower(line);
            static const std::vector<std::string> mediaExts = {
                ".mp4", ".mov", ".webm", ".mkv", ".wav", ".mp3", ".m4a", ".aiff", ".flac", ".aif"
            };
            
            for (const auto& ext : mediaExts) {
                size_t extPos = lowerLine.find(ext);
                if (extPos != std::string::npos) {
                    // Find the start of the filename (look backwards for space or path separator)
                    size_t start = extPos;
                    while (start > 0 && line[start - 1] != ' ' && line[start - 1] != '/' && line[start - 1] != '\\') {
                        start--;
                    }
                    // Extract filename
                    std::string filename = line.substr(start, extPos + ext.length() - start);
                    // Trim whitespace
                    size_t first = filename.find_first_not_of(" \t");
                    size_t last = filename.find_last_not_of(" \t\n\r");
                    if (first != std::string::npos && last != std::string::npos) {
                        filename = filename.substr(first, last - first + 1);
                        // Check if it's a full path or just filename
                        std::string fullPath;
                        if (ofFilePath::isAbsolute(filename)) {
                            fullPath = filename;
                        } else {
                            fullPath = ofFilePath::join(tempDir, filename);
                        }
                        if (ofFile::doesFileExist(fullPath)) {
                            candidateFromOutput = fullPath;
                            // Don't return yet - continue to find the best match
                        }
                    }
                }
            }
        }
    }
    
    // If we found a candidate from output, use it
    if (!candidateFromOutput.empty()) {
        return candidateFromOutput;
    }
    
    // Fallback: search for most recently modified media file in temp directory
    // This is more reliable than largest file when multiple files exist
    // Wait a bit for file system to sync, then retry
    for (int retry = 0; retry < 5; retry++) {
        if (retry > 0) {
            usleep(100000);  // 100ms delay
        }
        
        dir.listDir();
        std::string newestFile;
        std::time_t newestTime = 0;
        
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
                    // Get modification time using stat (POSIX)
                    struct stat fileInfo;
                    if (stat(file.path().c_str(), &fileInfo) == 0) {
                        std::time_t modTime = fileInfo.st_mtime;
                        if (modTime > newestTime) {
                            newestTime = modTime;
                            newestFile = file.path();
                        }
                    }
                }
            }
        }
        
        if (!newestFile.empty() && ofFile::doesFileExist(newestFile)) {
            return newestFile;
        }
    }
    
    return "";
}

// Helper function to handle download errors with helpful messages
void CommandExecutor::handleDownloadError(const std::string& result, int status) {
    output("Error: yt-dlp failed with exit code %d", status);
    
    // Check for common error patterns
    if (status == 127 || status == 32512) {
        output("");
        output("Command not found or cannot be executed.");
        output("This usually means:");
        output("  1. yt-dlp is not installed");
        output("  2. yt-dlp is not in your PATH");
        output("  3. The executable lacks execute permissions");
        output("");
        output("Try:");
        output("  brew install yt-dlp");
        output("  Or: pip3 install yt-dlp");
        output("  Then restart the application");
        output("");
    } else if (result.find("HTTP Error 403") != std::string::npos || 
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
    } else if (result.find("Video unavailable") != std::string::npos) {
        output("");
        output("Video is unavailable or has been removed.");
        output("");
    }
    
    // Show error output (truncated)
    if (!result.empty()) {
        output("Error details:");
        std::istringstream iss(result);
        std::string line;
        int lineCount = 0;
        while (std::getline(iss, line) && lineCount < 20) {
            // Show all error-related lines, not just ERROR/WARNING
            if (line.find("ERROR") != std::string::npos || 
                line.find("WARNING") != std::string::npos ||
                line.find("error") != std::string::npos ||
                line.find("Error") != std::string::npos) {
                output("%s", line.c_str());
                lineCount++;
            }
        }
        // If no error lines found, show first few lines of output
        if (lineCount == 0) {
            iss.clear();
            iss.seekg(0);
            lineCount = 0;
            while (std::getline(iss, line) && lineCount < 10) {
                output("%s", line.c_str());
                lineCount++;
            }
        }
    } else {
        output("No error output captured. This may indicate a shell execution problem.");
    }
}

void CommandExecutor::update() {
    // Process messages from background download thread
    std::queue<std::string> messagesToProcess;
    {
        std::lock_guard<std::mutex> lock(messageMutex_);
        while (!messageQueue_.empty()) {
            messagesToProcess.push(messageQueue_.front());
            messageQueue_.pop();
        }
    }
    
    // Output messages on main thread
    while (!messagesToProcess.empty()) {
        output(messagesToProcess.front());
        messagesToProcess.pop();
    }
    
    // Process import jobs on main thread (assetLibrary operations should be on main thread)
    std::queue<ImportJob> importsToProcess;
    {
        std::lock_guard<std::mutex> lock(importMutex_);
        while (!importQueue_.empty()) {
            importsToProcess.push(importQueue_.front());
            importQueue_.pop();
        }
    }
    
    while (!importsToProcess.empty()) {
        const ImportJob& job = importsToProcess.front();
        if (assetLibrary) {
            std::string assetId = assetLibrary->importFile(job.filePath, "");
            if (!assetId.empty()) {
                output("Imported as asset: %s", assetId.c_str());
            } else {
                output("Error: Failed to import downloaded file");
            }
        }
        importsToProcess.pop();
    }
}

void CommandExecutor::queueMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(messageMutex_);
    messageQueue_.push(message);
}

void CommandExecutor::downloadThreadFunction() {
    while (!shouldStopDownloadThread_) {
        DownloadJob job;
        bool hasJob = false;
        
        // Wait for download job with condition variable
        {
            std::unique_lock<std::mutex> lock(downloadMutex_);
            downloadCondition_.wait(lock, [this] {
                return !downloadQueue_.empty() || shouldStopDownloadThread_;
            });
            
            if (shouldStopDownloadThread_) {
                break;
            }
            
            if (!downloadQueue_.empty()) {
                job = downloadQueue_.front();
                downloadQueue_.pop();
                hasJob = true;
            }
        }
        
        if (hasJob) {
            processDownload(job);
        }
    }
}

void CommandExecutor::processDownload(const DownloadJob& job) {
    queueMessage("Starting download: " + job.url);
    
    // Setup temp directory
    ofDirectory dir(job.tempDir);
    if (!dir.exists()) {
        dir.create(true);
    }
    
    // Build output template - yt-dlp will substitute %(title)s and %(ext)s
    std::string outputTemplate = ofFilePath::join(job.tempDir, "%(title)s.%(ext)s");
    
    // Escape single quotes for shell (but NOT % - needed for yt-dlp template substitution)
    std::string escapedTemplate;
    for (char c : outputTemplate) {
        if (c == '\'') {
            escapedTemplate += "'\\''";  // Escape single quote in shell
        } else {
            escapedTemplate += c;
        }
    }
    
    // Escape the URL for shell
    std::string escapedUrl;
    for (char c : job.url) {
        if (c == '\'') {
            escapedUrl += "'\\''";
        } else if (c == '"') {
            escapedUrl += "\\\"";
        } else if (c == '\\') {
            escapedUrl += "\\\\";
        } else if (c == '&' || c == '|' || c == ';' || c == '<' || c == '>') {
            escapedUrl += "\\";
            escapedUrl += c;
        } else {
            escapedUrl += c;
        }
    }
    
    // Try multiple download strategies (Android -> iOS -> Web with EJS)
    std::vector<std::string> strategies = {
        // Strategy 1: Android client (most reliable, no EJS needed)
        "\"" + job.ytdlpPath + "\" --user-agent \"com.google.android.youtube/19.09.37 (Linux; U; Android 11) gzip\" "
        "--retries 3 --fragment-retries 3 "
        "--extractor-args \"youtube:player_client=android\" "
        "-f \"bestvideo+bestaudio/best\" -o '" + escapedTemplate + "' \"" + escapedUrl + "\" 2>&1",
        
        // Strategy 2: iOS client (fallback)
        "\"" + job.ytdlpPath + "\" --user-agent \"com.google.ios.youtube/19.09.3 (iPhone14,1; U; CPU iOS 15_6 like Mac OS X)\" "
        "--retries 3 --fragment-retries 3 "
        "--extractor-args \"youtube:player_client=ios\" "
        "-f \"bestvideo+bestaudio/best\" -o '" + escapedTemplate + "' \"" + escapedUrl + "\" 2>&1",
        
        // Strategy 3: Web client with EJS (requires deno)
        "\"" + job.ytdlpPath + "\" --user-agent \"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\" "
        "--retries 3 --fragment-retries 3 "
        "--remote-components ejs:github "
        "--extractor-args \"youtube:player_client=web\" "
        "-f \"bestvideo+bestaudio/best\" -o '" + escapedTemplate + "' \"" + escapedUrl + "\" 2>&1"
    };
    
    bool success = false;
    std::string result;
    
    for (size_t i = 0; i < strategies.size() && !success; i++) {
        if (i > 0) {
            queueMessage("Retrying with different method...");
        }
        
        // Execute command with explicit shell and PATH
        // Use /bin/bash with explicit PATH to ensure yt-dlp dependencies are found
        // Escape single quotes in the strategy for use inside single quotes
        std::string escapedStrategy;
        for (char c : strategies[i]) {
            if (c == '\'') {
                escapedStrategy += "'\\''";  // Escape single quote: ' -> '\''
            } else {
                escapedStrategy += c;
            }
        }
        std::string shellCmd = "PATH=\"/usr/local/bin:/opt/homebrew/bin:/usr/bin:/bin:$PATH\" /bin/bash -c '" + escapedStrategy + "'";
        
        FILE* pipe = popen(shellCmd.c_str(), "r");
        
        if (!pipe) {
            queueMessage("Error: Failed to execute yt-dlp command.");
            queueMessage("This may indicate a system configuration issue.");
            continue; // Try next strategy
        }
        
        // Read output in real-time and queue progress messages
        char buffer[256];
        result.clear();
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
            
            // Queue important progress messages (filter out noisy warnings)
            std::string line(buffer);
            std::string lowerLine = line;
            std::transform(lowerLine.begin(), lowerLine.end(), lowerLine.begin(), ::tolower);
            
            bool shouldShow = false;
            
            // Always show download progress
            if (line.find("[download]") != std::string::npos) {
                shouldShow = true;
            }
            // Always show errors
            else if (line.find("ERROR") != std::string::npos || line.find("error:") != std::string::npos) {
                shouldShow = true;
            }
            // Filter warnings - only show critical ones, hide common non-critical ones
            else if (line.find("WARNING") != std::string::npos) {
                // Filter out common non-critical warnings that yt-dlp handles automatically
                if (lowerLine.find("unable to extract yt initial data") == std::string::npos &&
                    lowerLine.find("incomplete data received") == std::string::npos &&
                    lowerLine.find("incomplete yt initial data") == std::string::npos &&
                    lowerLine.find("gvs po token") == std::string::npos &&
                    lowerLine.find("retrying") == std::string::npos &&
                    lowerLine.find("giving up after") == std::string::npos) {
                    shouldShow = true;
                }
            }
            // Show important info, but filter playlist-related noise
            else if (line.find("[info]") != std::string::npos) {
                if (lowerLine.find("downloading playlist") == std::string::npos &&
                    lowerLine.find("add --no-playlist") == std::string::npos &&
                    lowerLine.find("downloading just video") == std::string::npos) {
                    shouldShow = true;
                }
            }
            // Show status messages, but filter webpage download noise
            else if (line.find("Downloading") != std::string::npos) {
                if (line.find("Downloading webpage") == std::string::npos) {
                    shouldShow = true;
                }
            }
            
            if (shouldShow) {
                // Trim newline for cleaner output
                std::string trimmedLine = line;
                if (!trimmedLine.empty() && trimmedLine.back() == '\n') {
                    trimmedLine.pop_back();
                }
                queueMessage(trimmedLine);
            }
        }
        
        int status = pclose(pipe);
        
        // Check if command succeeded
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            success = true;
            break;
        }
        
        // Show error on last strategy
        if (i == strategies.size() - 1) {
            int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : status;
            std::string errorMsg = "Error: yt-dlp failed with exit code " + std::to_string(exitCode);
            queueMessage(errorMsg);
            // Queue error details (handleDownloadError logic simplified for background thread)
            if (!result.empty()) {
                std::istringstream iss(result);
                std::string line;
                int lineCount = 0;
                while (std::getline(iss, line) && lineCount < 10) {
                    if (line.find("ERROR") != std::string::npos || 
                        line.find("WARNING") != std::string::npos ||
                        line.find("error") != std::string::npos) {
                        queueMessage(line);
                        lineCount++;
                    }
                }
            }
            return;
        }
    }
    
    if (!success) {
        queueMessage("Error: All download strategies failed");
        return;
    }
    
    // Find downloaded file - parse from output or search directory
    std::string downloadedFile = findDownloadedFile(result, job.tempDir, dir);
    
    if (downloadedFile.empty()) {
        queueMessage("Error: Could not find downloaded file");
        return;
    }
    
    queueMessage("Downloaded: " + ofFilePath::getFileName(downloadedFile));
    
    // Queue import job to be processed on main thread
    ImportJob importJob;
    importJob.filePath = downloadedFile;
    {
        std::lock_guard<std::mutex> lock(importMutex_);
        importQueue_.push(importJob);
    }
}

// ═══════════════════════════════════════════════════════════
// PATTERN MANAGEMENT COMMANDS
// ═══════════════════════════════════════════════════════════

void CommandExecutor::cmdPatternList() {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    auto patternNames = patternRuntime->getPatternNames();
    
    if (patternNames.empty()) {
        output("No patterns found");
        return;
    }
    
    output("=== Patterns ===");
    for (const auto& name : patternNames) {
        const Pattern* pattern = patternRuntime->getPattern(name);
        const PatternPlaybackState* state = patternRuntime->getPlaybackState(name);
        
        if (pattern && state) {
            std::string status = state->isPlaying ? "[PLAYING]" : "[STOPPED]";
            output("  %s %s (%d steps)", name.c_str(), status.c_str(), pattern->getStepCount());
        } else {
            output("  %s [ERROR]", name.c_str());
        }
    }
    output("Total: %zu patterns", patternNames.size());
}

void CommandExecutor::cmdPatternCreate(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (args.empty()) {
        output("Usage: pattern create <name> [steps=16]");
        output("Example: pattern create beat1 16");
        return;
    }
    
    // Parse name and optional steps
    std::istringstream iss(args);
    std::string name;
    int steps = 16;  // Default
    
    iss >> name;
    if (iss >> steps) {
        // Steps provided
    }
    
    if (name.empty()) {
        output("Error: Pattern name required");
        return;
    }
    
    // Check if pattern already exists
    if (patternRuntime->patternExists(name)) {
        output("Error: Pattern '%s' already exists", name.c_str());
        return;
    }
    
    // Create pattern
    Pattern pattern(steps);
    std::string createdName = patternRuntime->addPattern(pattern, name);
    
    output("Created pattern '%s' with %d steps", createdName.c_str(), steps);
}

void CommandExecutor::cmdPatternDelete(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (args.empty()) {
        output("Usage: pattern delete <name>");
        output("Example: pattern delete beat1");
        return;
    }
    
    std::string name = trim(args);
    
    if (!patternRuntime->patternExists(name)) {
        output("Error: Pattern '%s' not found", name.c_str());
        return;
    }
    
    patternRuntime->removePattern(name);
    output("Deleted pattern '%s'", name.c_str());
}

void CommandExecutor::cmdPatternPlay(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (args.empty()) {
        output("Usage: pattern play <name>");
        output("Example: pattern play beat1");
        return;
    }
    
    std::string name = trim(args);
    
    if (!patternRuntime->patternExists(name)) {
        output("Error: Pattern '%s' not found", name.c_str());
        return;
    }
    
    patternRuntime->playPattern(name);
    output("Playing pattern '%s'", name.c_str());
}

void CommandExecutor::cmdPatternStop(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (args.empty()) {
        output("Usage: pattern stop <name>");
        output("Example: pattern stop beat1");
        return;
    }
    
    std::string name = trim(args);
    
    if (!patternRuntime->patternExists(name)) {
        output("Error: Pattern '%s' not found", name.c_str());
        return;
    }
    
    patternRuntime->stopPattern(name);
    output("Stopped pattern '%s'", name.c_str());
}

void CommandExecutor::cmdPatternReset(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (args.empty()) {
        output("Usage: pattern reset <name>");
        output("Example: pattern reset beat1");
        return;
    }
    
    std::string name = trim(args);
    
    if (!patternRuntime->patternExists(name)) {
        output("Error: Pattern '%s' not found", name.c_str());
        return;
    }
    
    patternRuntime->resetPattern(name);
    output("Reset pattern '%s'", name.c_str());
}

void CommandExecutor::cmdPatternPause(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (args.empty()) {
        output("Usage: pattern pause <name>");
        output("Example: pattern pause beat1");
        return;
    }
    
    std::string name = trim(args);
    
    if (!patternRuntime->patternExists(name)) {
        output("Error: Pattern '%s' not found", name.c_str());
        return;
    }
    
    patternRuntime->pausePattern(name);
    output("Paused pattern '%s'", name.c_str());
}

void CommandExecutor::cmdPatternInfo(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (args.empty()) {
        output("Usage: pattern info <name>");
        output("Example: pattern info beat1");
        return;
    }
    
    std::string name = trim(args);
    
    if (!patternRuntime->patternExists(name)) {
        output("Error: Pattern '%s' not found", name.c_str());
        return;
    }
    
    const Pattern* pattern = patternRuntime->getPattern(name);
    const PatternPlaybackState* state = patternRuntime->getPlaybackState(name);
    
    if (!pattern || !state) {
        output("Error: Could not get pattern or state for '%s'", name.c_str());
        return;
    }
    
    output("=== Pattern: %s ===", name.c_str());
    output("Steps: %d", pattern->getStepCount());
    output("Steps per beat: %.1f", pattern->getStepsPerBeat());
    output("Status: %s", state->isPlaying ? "PLAYING" : "STOPPED");
    output("Playback step: %d", state->playbackStep);
    output("Current playing step: %d", state->currentPlayingStep);
    output("Pattern cycle count: %d", state->patternCycleCount);
}

// ═══════════════════════════════════════════════════════════
// CHAIN MANAGEMENT COMMANDS
// ═══════════════════════════════════════════════════════════

void CommandExecutor::cmdChainList() {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    auto chainNames = patternRuntime->getChainNames();
    
    if (chainNames.empty()) {
        output("No chains found");
        return;
    }
    
    output("=== Chains ===");
    for (const auto& name : chainNames) {
        const PatternChain* chain = patternRuntime->getChain(name);
        if (chain) {
            std::string status = chain->isEnabled() ? "[ENABLED]" : "[DISABLED]";
            int size = chain->getSize();
            output("  %s %s (%d entries)", name.c_str(), status.c_str(), size);
        } else {
            output("  %s [ERROR]", name.c_str());
        }
    }
    output("Total: %zu chains", chainNames.size());
}

void CommandExecutor::cmdChainCreate(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    std::string name = trim(args);
    
    if (name.empty()) {
        // Auto-generate name
        std::string createdName = patternRuntime->addChain();
        output("Created chain '%s'", createdName.c_str());
    } else {
        if (patternRuntime->chainExists(name)) {
            output("Error: Chain '%s' already exists", name.c_str());
            return;
        }
        std::string createdName = patternRuntime->addChain(name);
        output("Created chain '%s'", createdName.c_str());
    }
}

void CommandExecutor::cmdChainDelete(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (args.empty()) {
        output("Usage: chain delete <name>");
        output("Example: chain delete chain1");
        return;
    }
    
    std::string name = trim(args);
    
    if (!patternRuntime->chainExists(name)) {
        output("Error: Chain '%s' not found", name.c_str());
        return;
    }
    
    patternRuntime->removeChain(name);
    output("Deleted chain '%s'", name.c_str());
}

void CommandExecutor::cmdChainInfo(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (args.empty()) {
        output("Usage: chain info <name>");
        output("Example: chain info chain1");
        return;
    }
    
    std::string name = trim(args);
    const PatternChain* chain = patternRuntime->getChain(name);
    
    if (!chain) {
        output("Error: Chain '%s' not found", name.c_str());
        return;
    }
    
    output("=== Chain: %s ===", name.c_str());
    output("Enabled: %s", chain->isEnabled() ? "yes" : "no");
    output("Size: %d entries", chain->getSize());
    output("Current index: %d", chain->getCurrentIndex());
    
    auto patterns = patternRuntime->chainGetPatterns(name);
    if (patterns.empty()) {
        output("Patterns: (empty)");
    } else {
        output("Patterns:");
        for (size_t i = 0; i < patterns.size(); i++) {
            int repeatCount = chain->getRepeatCount((int)i);
            bool disabled = chain->isEntryDisabled((int)i);
            std::string disabledStr = disabled ? " [DISABLED]" : "";
            output("  [%zu] %s (repeat: %d)%s", i, patterns[i].c_str(), repeatCount, disabledStr.c_str());
        }
    }
}

void CommandExecutor::cmdChainAdd(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (args.empty()) {
        output("Usage: chain add <chainName> <patternName> [index]");
        output("Example: chain add chain1 beat1");
        output("Example: chain add chain1 beat1 0  (insert at index 0)");
        return;
    }
    
    std::istringstream iss(args);
    std::string chainName, patternName;
    int index = -1;
    
    iss >> chainName >> patternName;
    if (iss >> index) { /* index parsed */ }
    
    if (chainName.empty() || patternName.empty()) {
        output("Error: Both chain name and pattern name are required");
        return;
    }
    
    if (!patternRuntime->chainExists(chainName)) {
        output("Error: Chain '%s' not found", chainName.c_str());
        return;
    }
    
    if (!patternRuntime->patternExists(patternName)) {
        output("Error: Pattern '%s' not found", patternName.c_str());
        return;
    }
    
    patternRuntime->chainAddPattern(chainName, patternName, index);
    output("Added pattern '%s' to chain '%s'", patternName.c_str(), chainName.c_str());
}

void CommandExecutor::cmdChainRemove(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (args.empty()) {
        output("Usage: chain remove <chainName> <index>");
        output("Example: chain remove chain1 0");
        return;
    }
    
    std::istringstream iss(args);
    std::string chainName;
    int index = -1;
    
    iss >> chainName >> index;
    
    if (chainName.empty() || index < 0) {
        output("Error: Chain name and valid index are required");
        return;
    }
    
    if (!patternRuntime->chainExists(chainName)) {
        output("Error: Chain '%s' not found", chainName.c_str());
        return;
    }
    
    patternRuntime->chainRemovePattern(chainName, index);
    output("Removed entry at index %d from chain '%s'", index, chainName.c_str());
}

void CommandExecutor::cmdChainRepeat(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (args.empty()) {
        output("Usage: chain repeat <chainName> <index> <repeatCount>");
        output("Example: chain repeat chain1 0 4");
        return;
    }
    
    std::istringstream iss(args);
    std::string chainName;
    int index = -1;
    int repeatCount = 1;
    
    iss >> chainName >> index >> repeatCount;
    
    if (chainName.empty() || index < 0 || repeatCount < 1) {
        output("Error: Chain name, valid index, and repeat count (>=1) are required");
        return;
    }
    
    if (!patternRuntime->chainExists(chainName)) {
        output("Error: Chain '%s' not found", chainName.c_str());
        return;
    }
    
    patternRuntime->chainSetRepeat(chainName, index, repeatCount);
    output("Set repeat count to %d for entry %d in chain '%s'", repeatCount, index, chainName.c_str());
}

void CommandExecutor::cmdChainEnable(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (args.empty()) {
        output("Usage: chain enable <name>");
        output("Example: chain enable chain1");
        return;
    }
    
    std::string name = trim(args);
    
    if (!patternRuntime->chainExists(name)) {
        output("Error: Chain '%s' not found", name.c_str());
        return;
    }
    
    patternRuntime->chainSetEnabled(name, true);
    output("Enabled chain '%s'", name.c_str());
}

void CommandExecutor::cmdChainDisable(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (args.empty()) {
        output("Usage: chain disable <name>");
        output("Example: chain disable chain1");
        return;
    }
    
    std::string name = trim(args);
    
    if (!patternRuntime->chainExists(name)) {
        output("Error: Chain '%s' not found", name.c_str());
        return;
    }
    
    patternRuntime->chainSetEnabled(name, false);
    output("Disabled chain '%s'", name.c_str());
}

void CommandExecutor::cmdChainClear(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (args.empty()) {
        output("Usage: chain clear <name>");
        output("Example: chain clear chain1");
        return;
    }
    
    std::string name = trim(args);
    
    if (!patternRuntime->chainExists(name)) {
        output("Error: Chain '%s' not found", name.c_str());
        return;
    }
    
    patternRuntime->chainClear(name);
    output("Cleared chain '%s'", name.c_str());
}

void CommandExecutor::cmdChainReset(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (args.empty()) {
        output("Usage: chain reset <name>");
        output("Example: chain reset chain1");
        return;
    }
    
    std::string name = trim(args);
    
    if (!patternRuntime->chainExists(name)) {
        output("Error: Chain '%s' not found", name.c_str());
        return;
    }
    
    patternRuntime->chainReset(name);
    output("Reset chain '%s'", name.c_str());
}

// ═══════════════════════════════════════════════════════════
// SEQUENCER BINDING COMMANDS
// ═══════════════════════════════════════════════════════════

void CommandExecutor::cmdSequencerList() {
    if (!registry) {
        output("Error: Registry not set");
        return;
    }
    
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    // Discover all sequencer modules from ModuleRegistry (not just those with bindings)
    auto sequencerModules = registry->getModulesByType(ModuleType::SEQUENCER);
    
    if (sequencerModules.empty()) {
        output("No sequencers found");
        return;
    }
    
    output("=== Sequencers ===");
    for (const auto& module : sequencerModules) {
        if (!module) continue;
        
        // Get sequencer name from registry
        std::string name = registry->getName(module);
        if (name.empty()) {
            // Fallback: try to get name from module directly if available
            continue;
        }
        
        // Get binding from PatternRuntime (may be empty if sequencer has no binding yet)
        auto binding = patternRuntime->getSequencerBinding(name);
        
        std::string status = "";
        if (!binding.patternName.empty()) {
            status += "pattern:" + binding.patternName;
        }
        if (!binding.chainName.empty()) {
            if (!status.empty()) status += ", ";
            status += "chain:" + binding.chainName;
            if (binding.chainEnabled) {
                status += " [ENABLED]";
            } else {
                status += " [DISABLED]";
            }
        }
        if (status.empty()) {
            status = "[no bindings]";
        }
        output("  %s - %s", name.c_str(), status.c_str());
    }
    output("Total: %zu sequencer(s)", sequencerModules.size());
}

void CommandExecutor::cmdSequencerInfo(const std::string& args) {
    if (!registry) {
        output("Error: Registry not set");
        return;
    }
    
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    std::string sequencerName = trim(args);
    
    if (sequencerName.empty()) {
        // Show info for all sequencers - discover from ModuleRegistry
        auto sequencerModules = registry->getModulesByType(ModuleType::SEQUENCER);
        if (sequencerModules.empty()) {
            output("No sequencers found");
            return;
        }
        
        for (const auto& module : sequencerModules) {
            if (!module) continue;
            
            std::string name = registry->getName(module);
            if (name.empty()) continue;
            
            auto binding = patternRuntime->getSequencerBinding(name);
            output("");
            output("=== Sequencer: %s ===", name.c_str());
            if (!binding.patternName.empty()) {
                output("Pattern: %s", binding.patternName.c_str());
            } else {
                output("Pattern: [not bound]");
            }
            if (!binding.chainName.empty()) {
                output("Chain: %s", binding.chainName.c_str());
                output("Chain enabled: %s", binding.chainEnabled ? "yes" : "no");
            } else {
                output("Chain: [not bound]");
            }
        }
    } else {
        // Show info for specific sequencer
        auto binding = patternRuntime->getSequencerBinding(sequencerName);
        
        // Check if sequencer exists (has any binding)
        auto sequencerNames = patternRuntime->getSequencerNames();
        bool exists = std::find(sequencerNames.begin(), sequencerNames.end(), sequencerName) != sequencerNames.end();
        
        if (!exists) {
            output("Error: Sequencer '%s' not found", sequencerName.c_str());
            output("Available sequencers:");
            for (const auto& name : sequencerNames) {
                output("  %s", name.c_str());
            }
            return;
        }
        
        output("=== Sequencer: %s ===", sequencerName.c_str());
        if (!binding.patternName.empty()) {
            output("Pattern: %s", binding.patternName.c_str());
            
            // Show pattern details
            Pattern* pattern = patternRuntime->getPattern(binding.patternName);
            if (pattern) {
                PatternPlaybackState* state = patternRuntime->getPlaybackState(binding.patternName);
                output("  Steps: %d", pattern->getStepCount());
                output("  Steps per beat: %.1f", pattern->getStepsPerBeat());
                if (state) {
                    output("  Playing: %s", state->isPlaying ? "yes" : "no");
                    output("  Current step: %d", state->playbackStep);
                    output("  Cycle count: %d", state->patternCycleCount);
                }
            }
        } else {
            output("Pattern: [not bound]");
        }
        
        if (!binding.chainName.empty()) {
            output("Chain: %s", binding.chainName.c_str());
            output("Chain enabled: %s", binding.chainEnabled ? "yes" : "no");
            
            // Show chain details
            PatternChain* chain = patternRuntime->getChain(binding.chainName);
            if (chain) {
                auto chainPatterns = patternRuntime->chainGetPatterns(binding.chainName);
                output("  Chain size: %zu entries", chainPatterns.size());
                output("  Current index: %d", chain->getCurrentIndex());
                output("  Chain patterns:");
                for (size_t i = 0; i < chainPatterns.size(); ++i) {
                    int repeatCount = chain->getRepeatCount((int)i);
                    bool disabled = chain->isEntryDisabled((int)i);
                    std::string status = "";
                    if (i == (size_t)chain->getCurrentIndex()) {
                        status = " [CURRENT]";
                    }
                    if (disabled) {
                        status += " [DISABLED]";
                    }
                    output("    [%zu] %s (repeat: %d)%s", i, chainPatterns[i].c_str(), repeatCount, status.c_str());
                }
            }
        } else {
            output("Chain: [not bound]");
        }
    }
}

void CommandExecutor::cmdSequencerBindPattern(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (!registry) {
        output("Error: ModuleRegistry not set");
        return;
    }
    
    std::istringstream iss(args);
    std::string sequencerName, patternName;
    iss >> sequencerName >> patternName;
    
    if (sequencerName.empty() || patternName.empty()) {
        output("Usage: sequencer <sequencerName> bind pattern <patternName>");
        output("Example: sequencer trackerSequencer1 bind pattern P1");
        return;
    }
    
    // Verify sequencer module exists (sequencer-agnostic check)
    auto module = registry->getModule(sequencerName);
    if (!module) {
        output("Error: Sequencer '%s' not found", sequencerName.c_str());
        return;
    }
    
    if (!patternRuntime->patternExists(patternName)) {
        output("Error: Pattern '%s' not found", patternName.c_str());
        return;
    }
    
    // Bind sequencer to pattern via PatternRuntime
    // The sequencer module will sync immediately via sequencerBindingChangedEvent
    patternRuntime->bindSequencerPattern(sequencerName, patternName);
    
    output("Bound sequencer '%s' to pattern '%s'", sequencerName.c_str(), patternName.c_str());
}

void CommandExecutor::cmdSequencerBindChain(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (!registry) {
        output("Error: ModuleRegistry not set");
        return;
    }
    
    std::istringstream iss(args);
    std::string sequencerName, chainName;
    iss >> sequencerName >> chainName;
    
    if (sequencerName.empty() || chainName.empty()) {
        output("Usage: sequencer <sequencerName> bind chain <chainName>");
        output("Example: sequencer trackerSequencer1 bind chain chain1");
        return;
    }
    
    // Verify sequencer module exists (sequencer-agnostic check)
    auto module = registry->getModule(sequencerName);
    if (!module) {
        output("Error: Sequencer '%s' not found", sequencerName.c_str());
        return;
    }
    
    if (!patternRuntime->chainExists(chainName)) {
        output("Error: Chain '%s' not found", chainName.c_str());
        return;
    }
    
    // Bind sequencer to chain via PatternRuntime
    // The sequencer module will sync immediately via sequencerBindingChangedEvent
    patternRuntime->bindSequencerChain(sequencerName, chainName);
    
    // Enable chain by default if chain is enabled
    PatternChain* chain = patternRuntime->getChain(chainName);
    if (chain && chain->isEnabled()) {
        patternRuntime->setSequencerChainEnabled(sequencerName, true);
    }
    
    output("Bound sequencer '%s' to chain '%s'", sequencerName.c_str(), chainName.c_str());
}

void CommandExecutor::cmdSequencerUnbindPattern(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (!registry) {
        output("Error: ModuleRegistry not set");
        return;
    }
    
    std::string sequencerName = trim(args);
    
    if (sequencerName.empty()) {
        output("Usage: sequencer <sequencerName> unbind pattern");
        return;
    }
    
    // Verify sequencer module exists (sequencer-agnostic check)
    auto module = registry->getModule(sequencerName);
    if (!module) {
        output("Error: Sequencer '%s' not found", sequencerName.c_str());
        return;
    }
    
    // Unbind pattern via PatternRuntime
    // The sequencer module will sync immediately via sequencerBindingChangedEvent
    patternRuntime->unbindSequencerPattern(sequencerName);
    
    output("Unbound pattern from sequencer '%s'", sequencerName.c_str());
}

void CommandExecutor::cmdSequencerUnbindChain(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    if (!registry) {
        output("Error: ModuleRegistry not set");
        return;
    }
    
    std::string sequencerName = trim(args);
    
    if (sequencerName.empty()) {
        output("Usage: sequencer <sequencerName> unbind chain");
        return;
    }
    
    // Verify sequencer module exists (sequencer-agnostic check)
    auto module = registry->getModule(sequencerName);
    if (!module) {
        output("Error: Sequencer '%s' not found", sequencerName.c_str());
        return;
    }
    
    // Unbind chain via PatternRuntime
    // The sequencer module will sync immediately via sequencerBindingChangedEvent
    patternRuntime->unbindSequencerChain(sequencerName);
    
    output("Unbound chain from sequencer '%s'", sequencerName.c_str());
}

void CommandExecutor::cmdSequencerEnableChain(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    std::string sequencerName = trim(args);
    
    if (sequencerName.empty()) {
        output("Usage: sequencer <sequencerName> enable chain");
        return;
    }
    
    auto binding = patternRuntime->getSequencerBinding(sequencerName);
    if (binding.chainName.empty()) {
        output("Error: Sequencer '%s' is not bound to a chain", sequencerName.c_str());
        return;
    }
    
    patternRuntime->setSequencerChainEnabled(sequencerName, true);
    output("Enabled chain for sequencer '%s'", sequencerName.c_str());
}

void CommandExecutor::cmdSequencerDisableChain(const std::string& args) {
    if (!patternRuntime) {
        output("Error: PatternRuntime not set");
        return;
    }
    
    std::string sequencerName = trim(args);
    
    if (sequencerName.empty()) {
        output("Usage: sequencer <sequencerName> disable chain");
        return;
    }
    
    auto binding = patternRuntime->getSequencerBinding(sequencerName);
    if (binding.chainName.empty()) {
        output("Error: Sequencer '%s' is not bound to a chain", sequencerName.c_str());
        return;
    }
    
    patternRuntime->setSequencerChainEnabled(sequencerName, false);
    output("Disabled chain for sequencer '%s'", sequencerName.c_str());
}

