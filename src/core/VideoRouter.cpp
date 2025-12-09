#include "VideoRouter.h"
#include "VideoOutput.h"
#include "VideoMixer.h"
#include "ModuleRegistry.h"
#include "ofLog.h"
#include "ofxVisualObjects.h"

VideoRouter::VideoRouter(ModuleRegistry* registry)
    : registry_(registry) {
}

VideoRouter::~VideoRouter() {
    clear();
}

void VideoRouter::clear() {
    // Disconnect all connections first
    // Collect all connections to disconnect
    std::vector<std::pair<std::string, std::string>> toDisconnect;
    for (const auto& [sourcePath, targetPaths] : portConnections_) {
        size_t sourceDot = sourcePath.find('.');
        if (sourceDot != std::string::npos) {
            std::string sourceModule = sourcePath.substr(0, sourceDot);
            for (const auto& targetPath : targetPaths) {
                size_t targetDot = targetPath.find('.');
                if (targetDot != std::string::npos) {
                    std::string targetModule = targetPath.substr(0, targetDot);
                    toDisconnect.push_back({sourceModule, targetModule});
                }
            }
        }
    }
    
    // Disconnect all
    for (const auto& [source, target] : toDisconnect) {
        try {
            disconnectInternal(source, target);
        } catch (...) {
            // Silently handle any disconnection errors during cleanup
        }
    }
    
    portConnections_.clear();
    
    ofLogNotice("VideoRouter") << "Cleared all video connections";
}

bool VideoRouter::connect(const std::string& fromModule, const std::string& toModule) {
    if (!registry_) {
        ofLogError("VideoRouter") << "Registry not set";
        return false;
    }
    
    // Validate module names are not empty
    if (fromModule.empty() || toModule.empty()) {
        ofLogError("VideoRouter") << "Cannot connect: empty module name(s) - from: \"" 
                                  << fromModule << "\", to: \"" << toModule << "\"";
        return false;
    }
    
    // Phase 2: Use port-based routing (auto-select compatible ports)
    auto fromMod = getModule(fromModule);
    auto toMod = getModule(toModule);
    if (!fromMod || !toMod) {
        ofLogError("VideoRouter") << "Module not found for connection: " << fromModule << " -> " << toModule;
        return false;
    }
    
    // Find first compatible video port pair
    auto sourcePorts = fromMod->getOutputPorts();
    auto targetPorts = toMod->getInputPorts();
    
    for (const auto& sourcePort : sourcePorts) {
        if (sourcePort.type != PortType::VIDEO_OUT) continue;
        
        for (const auto& targetPort : targetPorts) {
            if (targetPort.type != PortType::VIDEO_IN) continue;
            if (!Port::areCompatible(sourcePort, targetPort)) continue;
            
            // Found compatible pair - connect
            if (connectPort(fromModule, sourcePort.name, toModule, targetPort.name)) {
                return true;
            }
        }
    }
    
    ofLogError("VideoRouter") << "No compatible video ports found: " << fromModule << " -> " << toModule;
    return false;
}

bool VideoRouter::disconnect(const std::string& fromModule, const std::string& toModule) {
    if (toModule.empty()) {
        // Disconnect from all - find all port connections from this module
        std::string prefix = fromModule + ".";
        std::vector<std::pair<std::string, std::string>> toDisconnect;
        
        for (const auto& [sourcePath, targetPaths] : portConnections_) {
            if (sourcePath.find(prefix) == 0) {  // Starts with "fromModule."
                for (const auto& targetPath : targetPaths) {
                    // Extract target module name
                    size_t dotPos = targetPath.find('.');
                    if (dotPos != std::string::npos) {
                        std::string targetModule = targetPath.substr(0, dotPos);
                        toDisconnect.push_back({fromModule, targetModule});
                    }
                }
            }
        }
        
        // Disconnect all found connections
        bool disconnected = false;
        for (const auto& [from, to] : toDisconnect) {
            if (disconnectInternal(from, to)) {
                disconnected = true;
            }
        }
        return disconnected;
    } else {
        return disconnectInternal(fromModule, toModule);
    }
}

bool VideoRouter::disconnectAll(const std::string& moduleName) {
    if (moduleName.empty()) {
        ofLogWarning("VideoRouter") << "Cannot disconnectAll with empty module name";
        return false;
    }
    
    bool disconnected = false;
    std::string prefix = moduleName + ".";
    
    // Collect all connections to disconnect (from and to this module)
    std::vector<std::pair<std::string, std::string>> toDisconnect;
    
    // Find connections FROM this module
    for (const auto& [sourcePath, targetPaths] : portConnections_) {
        if (sourcePath.find(prefix) == 0) {  // Starts with "moduleName."
            for (const auto& targetPath : targetPaths) {
                size_t dotPos = targetPath.find('.');
                if (dotPos != std::string::npos) {
                    std::string targetModule = targetPath.substr(0, dotPos);
                    toDisconnect.push_back({moduleName, targetModule});
                }
            }
        }
    }
    
    // Find connections TO this module
    for (const auto& [sourcePath, targetPaths] : portConnections_) {
        for (const auto& targetPath : targetPaths) {
            if (targetPath.find(prefix) == 0) {  // Starts with "moduleName."
                size_t dotPos = sourcePath.find('.');
                if (dotPos != std::string::npos) {
                    std::string sourceModule = sourcePath.substr(0, dotPos);
                    toDisconnect.push_back({sourceModule, moduleName});
                }
            }
        }
    }
    
    // Disconnect all found connections
    for (const auto& [from, to] : toDisconnect) {
        if (disconnectInternal(from, to)) {
            disconnected = true;
        }
    }
    
    return disconnected;
}

bool VideoRouter::hasConnection(const std::string& fromModule, const std::string& toModule) const {
    std::string prefix = fromModule + ".";
    std::string targetPrefix = toModule + ".";
    
    // Check if any port from fromModule connects to any port in toModule
    for (const auto& [sourcePath, targetPaths] : portConnections_) {
        if (sourcePath.find(prefix) == 0) {  // Starts with "fromModule."
            for (const auto& targetPath : targetPaths) {
                if (targetPath.find(targetPrefix) == 0) {  // Starts with "toModule."
                    return true;
                }
            }
        }
    }
    return false;
}

std::set<std::string> VideoRouter::getTargets(const std::string& fromModule) const {
    std::set<std::string> targets;
    std::string prefix = fromModule + ".";
    
    // Extract target module names from portConnections_
    for (const auto& [sourcePath, targetPaths] : portConnections_) {
        if (sourcePath.find(prefix) == 0) {  // Starts with "fromModule."
            for (const auto& targetPath : targetPaths) {
                // Extract module name from "targetModule.targetPort"
                size_t dotPos = targetPath.find('.');
                if (dotPos != std::string::npos) {
                    targets.insert(targetPath.substr(0, dotPos));
                }
            }
        }
    }
    return targets;
}

std::set<std::string> VideoRouter::getSources(const std::string& toModule) const {
    std::set<std::string> sources;
    std::string prefix = toModule + ".";
    
    // Extract source module names from portConnections_
    for (const auto& [sourcePath, targetPaths] : portConnections_) {
        for (const auto& targetPath : targetPaths) {
            if (targetPath.find(prefix) == 0) {  // Starts with "toModule."
                // Extract module name from "sourceModule.sourcePort"
                size_t dotPos = sourcePath.find('.');
                if (dotPos != std::string::npos) {
                    sources.insert(sourcePath.substr(0, dotPos));
                }
            }
        }
    }
    return sources;
}

int VideoRouter::getConnectionCount() const {
    int count = 0;
    for (const auto& [sourcePath, targetPaths] : portConnections_) {
        count += targetPaths.size();
    }
    return count;
}

ofJson VideoRouter::toJson() const {
    ofJson json = ofJson::array();
    for (const auto& [sourcePath, targetPaths] : portConnections_) {
        for (const auto& targetPath : targetPaths) {
            ofJson conn;
            // Parse "module.port" format
            size_t sourceDot = sourcePath.find('.');
            size_t targetDot = targetPath.find('.');
            if (sourceDot != std::string::npos && targetDot != std::string::npos) {
                conn["fromModule"] = sourcePath.substr(0, sourceDot);
                conn["fromPort"] = sourcePath.substr(sourceDot + 1);
                conn["toModule"] = targetPath.substr(0, targetDot);
                conn["toPort"] = targetPath.substr(targetDot + 1);
                conn["type"] = "video";
                json.push_back(conn);
            } else {
                // Legacy format support: if no port info, use module-to-module
                conn["from"] = sourcePath;
                conn["to"] = targetPath;
                conn["type"] = "video";
                json.push_back(conn);
            }
        }
    }
    return json;
}

bool VideoRouter::fromJson(const ofJson& json) {
    if (!json.is_array()) {
        ofLogError("VideoRouter") << "Invalid JSON format: expected array";
        return false;
    }
    
    clear();
    
    for (const auto& connJson : json) {
        if (connJson.contains("type") && connJson["type"] == "video") {
            // New format: port-based
            if (connJson.contains("fromModule") && connJson.contains("fromPort") &&
                connJson.contains("toModule") && connJson.contains("toPort")) {
                std::string fromModule = connJson["fromModule"].get<std::string>();
                std::string fromPort = connJson["fromPort"].get<std::string>();
                std::string toModule = connJson["toModule"].get<std::string>();
                std::string toPort = connJson["toPort"].get<std::string>();
                connectPort(fromModule, fromPort, toModule, toPort);
            }
            // Legacy format: module-to-module (auto-select ports)
            else if (connJson.contains("from") && connJson.contains("to")) {
                std::string from = connJson["from"].get<std::string>();
                std::string to = connJson["to"].get<std::string>();
                connect(from, to);
            }
        }
    }
    
    return true;
}

std::shared_ptr<Module> VideoRouter::getModule(const std::string& moduleName) const {
    if (!registry_) {
        return nullptr;
    }
    return registry_->getModule(moduleName);
}

bool VideoRouter::connectPort(const std::string& fromModule, const std::string& fromPort,
                               const std::string& toModule, const std::string& toPort) {
    if (!registry_) {
        ofLogError("VideoRouter") << "Registry not set";
        return false;
    }
    
    // Validate inputs
    if (fromModule.empty() || fromPort.empty() || toModule.empty() || toPort.empty()) {
        ofLogError("VideoRouter") << "Cannot connect: empty module or port name";
        return false;
    }
    
    auto fromMod = getModule(fromModule);
    auto toMod = getModule(toModule);
    if (!fromMod || !toMod) {
        ofLogError("VideoRouter") << "Module not found for port connection";
        return false;
    }
    
    // Get ports (now safe from dangling pointers due to thread-local cache)
    const Port* sourcePort = fromMod->getOutputPort(fromPort);
    const Port* targetPort = toMod->getInputPort(toPort);
    
    if (!sourcePort || !targetPort) {
        ofLogError("VideoRouter") << "Port not found: " << fromModule << "." << fromPort 
                                  << " or " << toModule << "." << toPort;
        if (!sourcePort) {
            ofLogError("VideoRouter") << "  Source port '" << fromPort << "' not found in module '" << fromModule << "'";
            auto availablePorts = fromMod->getOutputPorts();
            if (!availablePorts.empty()) {
                std::string portList;
                for (const auto& p : availablePorts) {
                    if (!portList.empty()) portList += ", ";
                    portList += p.name;
                }
                ofLogError("VideoRouter") << "  Available output ports: " << portList;
            } else {
                ofLogError("VideoRouter") << "  Module '" << fromModule << "' has no output ports";
            }
        }
        if (!targetPort) {
            ofLogError("VideoRouter") << "  Target port '" << toPort << "' not found in module '" << toModule << "'";
            auto availablePorts = toMod->getInputPorts();
            if (!availablePorts.empty()) {
                std::string portList;
                for (const auto& p : availablePorts) {
                    if (!portList.empty()) portList += ", ";
                    portList += p.name;
                }
                ofLogError("VideoRouter") << "  Available input ports: " << portList;
            } else {
                ofLogError("VideoRouter") << "  Module '" << toModule << "' has no input ports";
            }
        }
        return false;
    }
    
    // Validate port compatibility with detailed logging
    if (!Port::areCompatible(*sourcePort, *targetPort)) {
        ofLogError("VideoRouter") << "Ports not compatible: " << fromModule << "." << fromPort 
                                  << " -> " << toModule << "." << toPort;
        ofLogError("VideoRouter") << "  Source: type=" << static_cast<int>(sourcePort->type)
                                  << " (VIDEO_OUT=" << static_cast<int>(PortType::VIDEO_OUT) << ")"
                                  << ", name=" << sourcePort->name
                                  << ", dataPtr=" << (sourcePort->dataPtr ? "valid" : "null");
        ofLogError("VideoRouter") << "  Target: type=" << static_cast<int>(targetPort->type)
                                  << " (VIDEO_IN=" << static_cast<int>(PortType::VIDEO_IN) << ")"
                                  << ", name=" << targetPort->name
                                  << ", dataPtr=" << (targetPort->dataPtr ? "valid" : "null");
        return false;
    }
    
    // Check if port already connected (for non-multi-connect ports)
    if (!targetPort->isMultiConnect) {
        std::string targetPath = toModule + "." + toPort;
        auto it = portConnections_.find(targetPath);
        if (it != portConnections_.end() && !it->second.empty()) {
            ofLogWarning("VideoRouter") << "Port already connected: " << toModule << "." << toPort;
            return false;
        }
    }
    
    // Perform actual connection
    if (sourcePort->type == PortType::VIDEO_OUT && targetPort->type == PortType::VIDEO_IN) {
        // Try module's connection management interface first
        // If it returns -1, the module doesn't support connection management, fall back to direct connection
        int connectionIndex = toMod->connectModule(fromMod);
        if (connectionIndex >= 0) {
            // Module managed the connection - track in router for querying
            std::string sourcePath = fromModule + "." + fromPort;
            std::string targetPath = toModule + "." + toPort;
            portConnections_[sourcePath].insert(targetPath);
            
            ofLogNotice("VideoRouter") << "Connected via module connection management: " 
                                      << fromModule << "." << fromPort 
                                      << " -> " << toModule << "." << toPort
                                      << " (index: " << connectionIndex << ")";
            return true;
        }
        
        // Fallback: Direct connection for modules that don't manage connections
        if (!sourcePort->dataPtr) {
            ofLogError("VideoRouter") << "Source port dataPtr is null: " << fromModule << "." << fromPort;
            return false;
        }
        if (!targetPort->dataPtr) {
            ofLogError("VideoRouter") << "Target port dataPtr is null: " << toModule << "." << toPort;
            return false;
        }
        
        auto* sourceObj = static_cast<ofxVisualObject*>(sourcePort->dataPtr);
        auto* targetObj = static_cast<ofxVisualObject*>(targetPort->dataPtr);
        
        if (sourceObj && targetObj) {
            try {
                sourceObj->connectTo(*targetObj);
                
                // Track connection
                std::string sourcePath = fromModule + "." + fromPort;
                std::string targetPath = toModule + "." + toPort;
                portConnections_[sourcePath].insert(targetPath);
                
                ofLogNotice("VideoRouter") << "Connected video port (direct): " << fromModule << "." << fromPort 
                                          << " -> " << toModule << "." << toPort;
                return true;
            } catch (const std::exception& e) {
                ofLogError("VideoRouter") << "Exception during video connection: " << e.what();
                return false;
            }
        } else {
            ofLogError("VideoRouter") << "Failed to cast dataPtr to ofxVisualObject*";
            ofLogError("VideoRouter") << "  Source dataPtr: " << sourcePort->dataPtr 
                                      << ", cast result: " << (sourceObj ? "valid" : "null");
            ofLogError("VideoRouter") << "  Target dataPtr: " << targetPort->dataPtr 
                                      << ", cast result: " << (targetObj ? "valid" : "null");
        }
    } else {
        ofLogError("VideoRouter") << "Port type mismatch (should not happen after compatibility check)";
        ofLogError("VideoRouter") << "  Source type: " << static_cast<int>(sourcePort->type)
                                  << ", expected: " << static_cast<int>(PortType::VIDEO_OUT);
        ofLogError("VideoRouter") << "  Target type: " << static_cast<int>(targetPort->type)
                                  << ", expected: " << static_cast<int>(PortType::VIDEO_IN);
    }
    
    return false;
}

// Legacy method - now uses port-based routing
bool VideoRouter::connectInternal(const std::string& from, const std::string& to) {
    // Delegate to port-based connect() method
    return connect(from, to);
}

bool VideoRouter::disconnectInternal(const std::string& from, const std::string& to) {
    if (!registry_) {
        return false;
    }
    
    // Validate module names are not empty
    if (from.empty() || to.empty()) {
        ofLogWarning("VideoRouter") << "Skipping disconnect with empty module name: \"" 
                                    << from << "\" -> \"" << to << "\"";
        return false;
    }
    
    // Capture strings for logging before any operations that might invalidate them
    std::string fromCopy = from;
    std::string toCopy = to;
    
    bool disconnected = false;
    
    // CRITICAL: Get modules BEFORE they might be deleted (while still in registry)
    // This allows us to safely disconnect the actual ofxVisualObject connections
    auto fromModule = getModule(from);
    auto toModule = getModule(to);
    
    // Port-based disconnection: Find all connected port pairs between these modules
    std::string fromPrefix = from + ".";
    std::string toPrefix = to + ".";
    
    // Collect port pairs to disconnect BEFORE modifying portConnections_
    // This allows us to disconnect actual objects while modules still exist
    std::vector<std::pair<std::string, std::string>> portPairsToDisconnect;
    
    // Find connections FROM 'from' module TO 'to' module
    for (const auto& [sourcePath, targetPaths] : portConnections_) {
        if (sourcePath.find(fromPrefix) == 0) {  // Source is from 'from' module
            for (const auto& targetPath : targetPaths) {
                if (targetPath.find(toPrefix) == 0) {  // Target is 'to' module
                    portPairsToDisconnect.push_back({sourcePath, targetPath});
                }
            }
        }
    }
    
    // Find connections TO 'to' module FROM 'from' module (reverse direction)
    for (const auto& [sourcePath, targetPaths] : portConnections_) {
        if (sourcePath.find(fromPrefix) == 0) continue;  // Already handled above
        
        for (const auto& targetPath : targetPaths) {
            if (targetPath.find(toPrefix) == 0) {
                size_t dotPos = sourcePath.find('.');
                if (dotPos != std::string::npos) {
                    std::string sourceModule = sourcePath.substr(0, dotPos);
                    if (sourceModule == from) {
                        portPairsToDisconnect.push_back({sourcePath, targetPath});
                    }
                }
            }
        }
    }
    
    // CRITICAL: Disconnect actual ofxVisualObject connections FIRST
    // Do this while modules still exist in registry to prevent video thread crashes
    // MODULAR PATTERN: Disconnect from the TARGET (mixer) side, not the source side
    // This matches BespokeSynth/Pure Data pattern where mixers manage their connections
    if (fromModule && toModule) {
        // Try module's disconnectModule() - it's a no-op if module doesn't support it
        try {
            toModule->disconnectModule(fromModule);
            disconnected = true;
            ofLogNotice("VideoRouter") << "Disconnected via module connection management: " 
                                        << fromCopy << " -> " << toCopy;
        } catch (const std::exception& e) {
            ofLogWarning("VideoRouter") << "Error disconnecting via module interface: " << e.what();
        } catch (...) {
            ofLogWarning("VideoRouter") << "Unknown error disconnecting via module interface";
        }
        
        // Also handle direct port-based disconnection for modules that don't manage connections
        // (This ensures cleanup even if disconnectModule() is a no-op)
        for (const auto& [sourcePath, targetPath] : portPairsToDisconnect) {
            // Extract port names
            size_t sourceDot = sourcePath.find('.');
            size_t targetDot = targetPath.find('.');
            if (sourceDot != std::string::npos && targetDot != std::string::npos) {
                std::string sourcePortName = sourcePath.substr(sourceDot + 1);
                std::string targetPortName = targetPath.substr(targetDot + 1);
                
                // Get ports and disconnect actual objects
                // Wrap in try-catch in case module is partially destroyed
                try {
                    const Port* sourcePort = fromModule->getOutputPort(sourcePortName);
                    const Port* targetPort = toModule->getInputPort(targetPortName);
                    
                    if (sourcePort && sourcePort->dataPtr && targetPort && targetPort->dataPtr) {
                        auto* sourceObj = static_cast<ofxVisualObject*>(sourcePort->dataPtr);
                        auto* targetObj = static_cast<ofxVisualObject*>(targetPort->dataPtr);
                        
                        if (sourceObj && targetObj) {
                            // Disconnect the actual ofxVisualObject connection
                            // This prevents the video thread from accessing freed memory
                            // Use disconnectInput on the target (mixer) if available
                            if (auto* mixer = dynamic_cast<ofxVideoMixer*>(targetObj)) {
                                mixer->disconnectInput(sourceObj);
                            } else {
                                // Fallback: disconnect from source
                                sourceObj->disconnect();
                            }
                            disconnected = true;
                        }
                    }
                } catch (const std::exception& e) {
                    ofLogWarning("VideoRouter") << "Error disconnecting video object: " << e.what();
                } catch (...) {
                    ofLogWarning("VideoRouter") << "Unknown error disconnecting video object";
                }
            }
        }
    }
    
    // Now clean up tracking (safe after actual disconnections)
    for (auto portIt = portConnections_.begin(); portIt != portConnections_.end();) {
        if (portIt->first.find(fromPrefix) == 0) {  // Source is from 'from' module
            std::string sourcePath = portIt->first;
            auto& targetPaths = portIt->second;
            
            for (auto targetIt = targetPaths.begin(); targetIt != targetPaths.end();) {
                if (targetIt->find(toPrefix) == 0) {  // Target is 'to' module
                    // Always remove from tracking (critical - prevents dangling references)
                    targetIt = targetPaths.erase(targetIt);
                } else {
                    ++targetIt;
                }
            }
            
            // Remove source entry if no targets left
            if (targetPaths.empty()) {
                portIt = portConnections_.erase(portIt);
            } else {
                ++portIt;
            }
        } else {
            ++portIt;
        }
    }
    
    // Find connections TO 'to' module FROM 'from' module (reverse direction)
    for (auto portIt = portConnections_.begin(); portIt != portConnections_.end();) {
        if (portIt->first.find(fromPrefix) == 0) {  // Already handled above
            ++portIt;
            continue;
        }
        
        auto& targetPaths = portIt->second;
        for (auto targetIt = targetPaths.begin(); targetIt != targetPaths.end();) {
            if (targetIt->find(toPrefix) == 0) {
                // Connection TO 'to' module - check if FROM 'from' module
                size_t dotPos = portIt->first.find('.');
                if (dotPos != std::string::npos) {
                    std::string sourceModule = portIt->first.substr(0, dotPos);
                    if (sourceModule == from) {
                        // Always remove from tracking (critical - prevents dangling references)
                        targetIt = targetPaths.erase(targetIt);
                    } else {
                        ++targetIt;
                    }
                } else {
                    ++targetIt;
                }
            } else {
                ++targetIt;
            }
        }
        
        // Remove source entry if no targets left
        if (targetPaths.empty()) {
            portIt = portConnections_.erase(portIt);
        } else {
            ++portIt;
        }
    }
    
    // Log using captured copies to avoid any string access issues
    if (disconnected) {
        try {
            ofLogNotice("VideoRouter") << "Disconnected video: " << fromCopy << " -> " << toCopy;
        } catch (...) {
            // Silently fail logging if there's an issue
        }
    }
    
    return disconnected;
}

bool VideoRouter::validateConnection(const std::string& from, const std::string& to) const {
    if (!registry_) {
        return false;
    }
    
    if (from == to) {
        ofLogError("VideoRouter") << "Cannot connect module to itself: " << from;
        return false;
    }
    
    auto fromModule = getModule(from);
    if (!fromModule) {
        ofLogError("VideoRouter") << "Source module not found: " << from;
        return false;
    }
    
    auto toModule = getModule(to);
    if (!toModule) {
        ofLogError("VideoRouter") << "Target module not found: " << to;
        return false;
    }
    
    // Port-based validation: Check if source has VIDEO_OUT port and target has VIDEO_IN port
    auto sourcePorts = fromModule->getOutputPorts();
    auto targetPorts = toModule->getInputPorts();
    
    bool hasVideoOut = false;
    for (const auto& port : sourcePorts) {
        if (port.type == PortType::VIDEO_OUT) {
            hasVideoOut = true;
            break;
        }
    }
    
    if (!hasVideoOut) {
        ofLogError("VideoRouter") << "Source module does not have video output port: " << from;
        return false;
    }
    
    bool hasVideoIn = false;
    for (const auto& port : targetPorts) {
        if (port.type == PortType::VIDEO_IN) {
            hasVideoIn = true;
            break;
        }
    }
    
    if (!hasVideoIn) {
        ofLogError("VideoRouter") << "Target module does not have video input port: " << to;
        return false;
    }
    
    return true;
}

