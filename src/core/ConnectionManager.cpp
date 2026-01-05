#include "ConnectionManager.h"
#include "modules/AudioOutput.h"
#include "modules/VideoOutput.h"
#include "ModuleRegistry.h"
#include "utils/Clock.h"
#include "modules/Module.h"
#include "ofLog.h"

ConnectionManager::ConnectionManager(ModuleRegistry* registry)
    : registry_(registry)
    , parameterRouter_(nullptr)
    , patternRuntime_(nullptr)
    , audioRouter_(registry)
    , videoRouter_(registry)
    , eventRouter_(registry)
    , autoRouteMode_(AutoRouteMode::ORPHANED_ONLY) {
}

ConnectionManager::~ConnectionManager() {
    // Clear all connections
    clear();
}

void ConnectionManager::clear() {
    // Clear all routers
    audioRouter_.clear();
    videoRouter_.clear();
    eventRouter_.clear();
    
    // Clear parameter router connections (if set)
    if (parameterRouter_) {
        parameterRouter_->clear();
    }
    
    ofLogNotice("ConnectionManager") << "Cleared all connections";
}

void ConnectionManager::renameModule(const std::string& oldName, const std::string& newName) {
    if (oldName == newName || oldName.empty() || newName.empty()) {
        return;
    }
    
    // Note: AudioRouter, VideoRouter, and EventRouter now use UUID-based connections, so renaming doesn't affect them
    // Only ParameterRouter still uses name-based paths, so update it
    if (parameterRouter_) {
        parameterRouter_->renameModule(oldName, newName);
    }
    
    ofLogNotice("ConnectionManager") << "Renamed module (audio/video/event connections are UUID-based, no router updates needed): " << oldName << " -> " << newName;
}

bool ConnectionManager::disconnectAll(const std::string& moduleName) {
    if (moduleName.empty()) {
        ofLogWarning("ConnectionManager") << "Cannot disconnectAll with empty module name";
        return false;
    }
    
    bool disconnected = false;
    
    // Disconnect all connections via routers
    // The routers now handle disconnecting actual audio/video objects before cleanup
    if (audioRouter_.disconnectAll(moduleName)) {
        disconnected = true;
    }
    if (videoRouter_.disconnectAll(moduleName)) {
        disconnected = true;
    }
    if (eventRouter_.unsubscribeAll(moduleName)) {
        disconnected = true;
    }
    
    // Disconnect parameter connections (if ParameterRouter is set)
    if (parameterRouter_) {
        auto paramConnections = parameterRouter_->getConnections();
        for (const auto& conn : paramConnections) {
            // conn is a pair<string, string> where first=sourcePath, second=targetPath
            std::string sourceInstance = extractModuleName(conn.first);
            std::string targetInstance = extractModuleName(conn.second);
            if (sourceInstance == moduleName || targetInstance == moduleName) {
                parameterRouter_->disconnect(conn.first);
                disconnected = true;
            }
        }
    }
    
    if (disconnected) {
        ofLogNotice("ConnectionManager") << "Disconnected all connections for module: " << moduleName;
    }
    
    return disconnected;
}

int ConnectionManager::getTotalConnectionCount() const {
    int count = 0;
    
    // Count audio connections
    count += audioRouter_.getConnectionCount();
    
    // Count video connections
    count += videoRouter_.getConnectionCount();
    
    // Count parameter connections
    if (parameterRouter_) {
        count += static_cast<int>(parameterRouter_->getConnections().size());
    }
    
    // Count event subscriptions
    count += eventRouter_.getSubscriptionCount();
    
    return count;
}

std::shared_ptr<Module> ConnectionManager::getModule(const std::string& moduleName) const {
    if (!registry_) {
        return nullptr;
    }
    if (moduleName.empty()) {
        return nullptr;
    }
    return registry_->getModule(moduleName);
}

// ========================================================================
// AUDIO/VIDEO ROUTING - Placeholder implementations
// ========================================================================

bool ConnectionManager::connectAudio(const std::string& fromModule, const std::string& toModule) {
    bool success = audioRouter_.connect(fromModule, toModule);
    if (success && registry_) {
        auto sourceModule = registry_->getModule(fromModule);
        if (sourceModule) {
            sourceModule->onConnectionEstablished(toModule, Module::ConnectionType::AUDIO, this);
        }
    }
    return success;
}

bool ConnectionManager::connectVideo(const std::string& fromModule, const std::string& toModule) {
    bool success = videoRouter_.connect(fromModule, toModule);
    if (success && registry_) {
        auto sourceModule = registry_->getModule(fromModule);
        if (sourceModule) {
            sourceModule->onConnectionEstablished(toModule, Module::ConnectionType::VIDEO, this);
        }
    }
    return success;
}

bool ConnectionManager::connect(const std::string& fromModule, const std::string& toModule) {
    // Try audio first, then video
    if (connectAudio(fromModule, toModule)) {
        return true;
    }
    return connectVideo(fromModule, toModule);
}

bool ConnectionManager::connectAudioPort(const std::string& fromModule, const std::string& fromPort,
                                         const std::string& toModule, const std::string& toPort) {
    return audioRouter_.connectPort(fromModule, fromPort, toModule, toPort);
}

bool ConnectionManager::connectVideoPort(const std::string& fromModule, const std::string& fromPort,
                                         const std::string& toModule, const std::string& toPort) {
    return videoRouter_.connectPort(fromModule, fromPort, toModule, toPort);
}

bool ConnectionManager::disconnectAudio(const std::string& fromModule, const std::string& toModule) {
    bool success = audioRouter_.disconnect(fromModule, toModule);
    if (success && registry_) {
        auto sourceModule = registry_->getModule(fromModule);
        if (sourceModule) {
            sourceModule->onConnectionBroken(toModule, Module::ConnectionType::AUDIO, this);
        }
    }
    return success;
}

bool ConnectionManager::disconnectVideo(const std::string& fromModule, const std::string& toModule) {
    bool success = videoRouter_.disconnect(fromModule, toModule);
    if (success && registry_) {
        auto sourceModule = registry_->getModule(fromModule);
        if (sourceModule) {
            sourceModule->onConnectionBroken(toModule, Module::ConnectionType::VIDEO, this);
        }
    }
    return success;
}

bool ConnectionManager::disconnect(const std::string& fromModule, const std::string& toModule) {
    bool audioDisconnected = disconnectAudio(fromModule, toModule);
    bool videoDisconnected = disconnectVideo(fromModule, toModule);
    return audioDisconnected || videoDisconnected;
}

// ========================================================================
// PARAMETER ROUTING - Placeholder implementations
// ========================================================================

bool ConnectionManager::connectParameter(const std::string& sourcePath, const std::string& targetPath,
                                        std::function<bool()> condition) {
    if (!parameterRouter_) {
        ofLogError("ConnectionManager") << "ParameterRouter not set";
        return false;
    }
    return parameterRouter_->connect(sourcePath, targetPath, condition);
}

bool ConnectionManager::connectParameterDirect(const std::string& sourceModule, const std::string& sourceParam,
                                               const std::string& targetModule, const std::string& targetParam,
                                               std::function<bool()> condition) {
    if (!parameterRouter_) {
        ofLogError("ConnectionManager") << "ParameterRouter not set";
        return false;
    }
    
    bool success = parameterRouter_->connectDirect(sourceModule, sourceParam, targetModule, targetParam, condition);
    if (success && registry_) {
        auto sourceModulePtr = registry_->getModule(sourceModule);
        if (sourceModulePtr) {
            sourceModulePtr->onConnectionEstablished(targetModule, Module::ConnectionType::PARAMETER, this);
        }
    }
    return success;
}

bool ConnectionManager::disconnectParameter(const std::string& sourcePath) {
    if (!parameterRouter_) {
        ofLogError("ConnectionManager") << "ParameterRouter not set";
        return false;
    }
    
    // Extract target module name from sourcePath before disconnecting
    // sourcePath format: "moduleName.paramName" or "moduleName.paramPath"
    // We need to get the target from ParameterRouter before disconnecting
    std::string targetModule;
    if (parameterRouter_) {
        auto connections = parameterRouter_->getConnections();
        for (const auto& conn : connections) {
            if (conn.first == sourcePath) {
                // Extract target module from targetPath (format: "targetModule.paramName")
                targetModule = extractModuleName(conn.second);
                break;
            }
        }
    }
    
    bool success = parameterRouter_->disconnect(sourcePath);
    if (success && registry_ && !targetModule.empty()) {
        std::string sourceModule = extractModuleName(sourcePath);
        auto sourceModulePtr = registry_->getModule(sourceModule);
        if (sourceModulePtr) {
            sourceModulePtr->onConnectionBroken(targetModule, Module::ConnectionType::PARAMETER, this);
        }
    }
    return success;
}

// ========================================================================
// EVENT SUBSCRIPTIONS - Placeholder implementations
// ========================================================================

bool ConnectionManager::subscribeEvent(const std::string& sourceModule, const std::string& eventName,
                                      const std::string& targetModule, const std::string& handlerName) {
    bool success = eventRouter_.subscribe(sourceModule, eventName, targetModule, handlerName);
    if (success && registry_) {
        auto sourceModulePtr = registry_->getModule(sourceModule);
        if (sourceModulePtr) {
            sourceModulePtr->onConnectionEstablished(targetModule, Module::ConnectionType::EVENT, this);
        }
    }
    return success;
}

bool ConnectionManager::unsubscribeEvent(const std::string& sourceModule, const std::string& eventName,
                                        const std::string& targetModule, const std::string& handlerName) {
    bool unsubscribed = false;
    
    if (handlerName.empty()) {
        // If handlerName not provided, unsubscribe all matching subscriptions
        auto subscriptions = eventRouter_.getSubscriptionsFrom(sourceModule);
        for (const auto& sub : subscriptions) {
            // Convert UUIDs to names for comparison and unsubscribe
            if (!registry_) continue;
            std::string subSourceName = registry_->getName(sub.sourceUUID);
            std::string subTargetName = registry_->getName(sub.targetUUID);
            if (sub.eventName == eventName && subTargetName == targetModule) {
                if (eventRouter_.unsubscribe(subSourceName, sub.eventName, subTargetName, sub.handlerName)) {
                    unsubscribed = true;
                }
            }
        }
        } else {
        // Unsubscribe specific subscription
        unsubscribed = eventRouter_.unsubscribe(sourceModule, eventName, targetModule, handlerName);
    if (unsubscribed) {
        ofLogNotice("ConnectionManager") << "Unsubscribed from event: " 
                                          << sourceModule << "." << eventName 
                                          << " -> " << targetModule;
    } else {
        ofLogWarning("ConnectionManager") << "Event subscription not found: " 
                                           << sourceModule << "." << eventName 
                                           << " -> " << targetModule;
    }
    }
    
    // Notify source module of disconnection
    if (unsubscribed && registry_) {
        auto sourceModulePtr = registry_->getModule(sourceModule);
        if (sourceModulePtr) {
            sourceModulePtr->onConnectionBroken(targetModule, Module::ConnectionType::EVENT, this);
        }
    }
    
    return unsubscribed;
}

// ========================================================================
// AUTO-ROUTING - Placeholder implementations
// ========================================================================

int ConnectionManager::autoRouteOrphanedOutputs(const std::string& masterAudioMixer,
                                                 const std::string& masterVideoMixer) {
    if (!registry_) {
        ofLogError("ConnectionManager") << "Registry not set";
        return 0;
    }
    
    std::vector<std::string> orphaned = findOrphanedModules();
    if (orphaned.empty()) {
        ofLogNotice("ConnectionManager") << "No orphaned modules found";
        return 0;
    }
    
    int connectionsCreated = routeModulesToMixers(orphaned, masterAudioMixer, masterVideoMixer, false);
    
    if (connectionsCreated > 0) {
        ofLogNotice("ConnectionManager") << "Auto-routed " << connectionsCreated 
                                          << " connections for " << orphaned.size() << " orphaned modules";
    }
    
    return connectionsCreated;
}

int ConnectionManager::autoRouteChainEnds(const std::string& masterAudioMixer,
                                          const std::string& masterVideoMixer) {
    if (!registry_) {
        ofLogError("ConnectionManager") << "Registry not set";
        return 0;
    }
    
    std::vector<std::string> chainEnds = findChainEnds();
    if (chainEnds.empty()) {
        ofLogNotice("ConnectionManager") << "No chain ends found";
        return 0;
    }
    
    int connectionsCreated = routeModulesToMixers(chainEnds, masterAudioMixer, masterVideoMixer, true);
    
    if (connectionsCreated > 0) {
        ofLogNotice("ConnectionManager") << "Auto-routed " << connectionsCreated 
                                          << " connections for " << chainEnds.size() << " chain end modules";
    }
    
    return connectionsCreated;
}

int ConnectionManager::autoRouteToMasters(const std::string& masterAudioOutName,
                                         const std::string& masterVideoOutName) {
    if (!registry_) {
        ofLogError("ConnectionManager") << "Registry not set";
        return 0;
    }
    
    int connectionsCreated = 0;
    
    // Iterate through all modules and route unconnected outputs to masters
    registry_->forEachModule([this, &masterAudioOutName, &masterVideoOutName, &connectionsCreated](
        const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
        if (!module) return;
        
        // Skip master outputs themselves
        if (name == masterAudioOutName || name == masterVideoOutName) return;
        
        // Check what outputs this module has
        auto [hasAudioOut, hasVideoOut] = checkOutputPorts(module.get());
        
        // Check if audio output is already connected
        if (hasAudioOut && !masterAudioOutName.empty()) {
            auto audioTargets = audioRouter_.getTargets(name);
            if (audioTargets.empty()) {
                // No audio connections - route to master
                if (connectAudio(name, masterAudioOutName)) {
                    connectionsCreated++;
                    ofLogNotice("ConnectionManager") << "Auto-routed audio: " << name 
                                                      << " -> " << masterAudioOutName;
                } else {
                    ofLogWarning("ConnectionManager") << "Failed to auto-route audio: " << name 
                                                       << " -> " << masterAudioOutName;
                }
            }
        }
        
        // Check if video output is already connected
        if (hasVideoOut && !masterVideoOutName.empty()) {
            auto videoTargets = videoRouter_.getTargets(name);
            if (videoTargets.empty()) {
                // No video connections - route to master
                if (connectVideo(name, masterVideoOutName)) {
                    connectionsCreated++;
                    ofLogNotice("ConnectionManager") << "Auto-routed video: " << name 
                                                      << " -> " << masterVideoOutName;
                } else {
                    ofLogWarning("ConnectionManager") << "Failed to auto-route video: " << name 
                                                       << " -> " << masterVideoOutName;
                }
            }
        }
    });
    
    return connectionsCreated;
}

// ========================================================================
// QUERY METHODS - Placeholder implementations
// ========================================================================

std::vector<ConnectionManager::Connection> ConnectionManager::getConnections() const {
    std::vector<Connection> connections;
    
    if (!registry_) {
        return connections;
    }
    
    // Cache module list to avoid multiple calls
    auto allModules = registry_->getAllHumanNames();
    
    // Add audio connections from AudioRouter
    for (const auto& moduleName : allModules) {
        auto targets = audioRouter_.getTargets(moduleName);
        for (const auto& target : targets) {
            connections.emplace_back(moduleName, target, ConnectionType::AUDIO);
        }
    }
    
    // Add video connections from VideoRouter
    for (const auto& moduleName : allModules) {
        auto targets = videoRouter_.getTargets(moduleName);
        for (const auto& target : targets) {
            connections.emplace_back(moduleName, target, ConnectionType::VIDEO);
        }
    }
    
    // Add parameter connections (if ParameterRouter is set)
    if (parameterRouter_) {
        auto paramConnections = parameterRouter_->getConnections();
        for (const auto& conn : paramConnections) {
            Connection cmConn;
            cmConn.type = ConnectionType::PARAMETER;
            cmConn.sourcePath = conn.first;
            cmConn.targetPath = conn.second;
            cmConn.sourceModule = extractModuleName(conn.first);
            cmConn.targetModule = extractModuleName(conn.second);
            connections.push_back(cmConn);
        }
    }
    
    // Add event subscriptions from EventRouter
    for (const auto& moduleName : allModules) {
        auto subs = eventRouter_.getSubscriptionsFrom(moduleName);
        for (const auto& sub : subs) {
            // Convert UUIDs to names for Connection object
            if (!registry_) continue;
            std::string sourceName = registry_->getName(sub.sourceUUID);
            std::string targetName = registry_->getName(sub.targetUUID);
            if (!sourceName.empty() && !targetName.empty()) {
                Connection conn(sourceName, targetName, ConnectionType::EVENT);
                conn.eventName = sub.eventName;
                conn.handlerName = sub.handlerName;
                connections.push_back(conn);
            }
        }
    }
    
    return connections;
}

std::vector<ConnectionManager::Connection> ConnectionManager::getConnectionsFrom(const std::string& moduleName) const {
    std::vector<Connection> result;
    
    if (!registry_) {
        return result;
    }
    
    // Query audio connections directly
    auto audioTargets = audioRouter_.getTargets(moduleName);
    for (const auto& target : audioTargets) {
        result.emplace_back(moduleName, target, ConnectionType::AUDIO);
    }
    
    // Query video connections directly
    auto videoTargets = videoRouter_.getTargets(moduleName);
    for (const auto& target : videoTargets) {
        result.emplace_back(moduleName, target, ConnectionType::VIDEO);
    }
    
    // Query parameter connections directly
    if (parameterRouter_) {
        auto paramConnections = parameterRouter_->getConnections();
        for (const auto& conn : paramConnections) {
            std::string sourceInstance = extractModuleName(conn.first);
            if (sourceInstance == moduleName) {
                Connection cmConn;
                cmConn.type = ConnectionType::PARAMETER;
                cmConn.sourcePath = conn.first;
                cmConn.targetPath = conn.second;
                cmConn.sourceModule = sourceInstance;
                cmConn.targetModule = extractModuleName(conn.second);
                result.push_back(cmConn);
            }
        }
    }
    
    // Query event subscriptions directly
    auto subs = eventRouter_.getSubscriptionsFrom(moduleName);
    for (const auto& sub : subs) {
        // Convert UUIDs to names for Connection object
        if (!registry_) continue;
        std::string sourceName = registry_->getName(sub.sourceUUID);
        std::string targetName = registry_->getName(sub.targetUUID);
        if (!sourceName.empty() && !targetName.empty()) {
            Connection conn(sourceName, targetName, ConnectionType::EVENT);
            conn.eventName = sub.eventName;
            conn.handlerName = sub.handlerName;
            result.push_back(conn);
        }
    }
    
    return result;
}

std::vector<ConnectionManager::Connection> ConnectionManager::getConnectionsTo(const std::string& moduleName) const {
    std::vector<Connection> result;
    
    if (!registry_) {
        return result;
    }
    
    // Query audio connections directly
    auto audioSources = audioRouter_.getSources(moduleName);
    for (const auto& source : audioSources) {
        result.emplace_back(source, moduleName, ConnectionType::AUDIO);
    }
    
    // Query video connections directly
    auto videoSources = videoRouter_.getSources(moduleName);
    for (const auto& source : videoSources) {
        result.emplace_back(source, moduleName, ConnectionType::VIDEO);
    }
    
    // Query parameter connections directly
    if (parameterRouter_) {
        auto paramConnections = parameterRouter_->getConnections();
        for (const auto& conn : paramConnections) {
            std::string targetInstance = extractModuleName(conn.second);
            if (targetInstance == moduleName) {
                Connection cmConn;
                cmConn.type = ConnectionType::PARAMETER;
                cmConn.sourcePath = conn.first;
                cmConn.targetPath = conn.second;
                cmConn.sourceModule = extractModuleName(conn.first);
                cmConn.targetModule = targetInstance;
                result.push_back(cmConn);
            }
        }
    }
    
    // Query event subscriptions directly (need to check all modules for subscriptions to this module)
    auto allModules = registry_->getAllHumanNames();
    for (const auto& sourceName : allModules) {
        auto subs = eventRouter_.getSubscriptionsFrom(sourceName);
        for (const auto& sub : subs) {
            // Convert UUIDs to names for comparison and Connection object
            std::string targetName = registry_->getName(sub.targetUUID);
            if (targetName == moduleName) {
                std::string sourceNameFromSub = registry_->getName(sub.sourceUUID);
                if (!sourceNameFromSub.empty()) {
                    Connection conn(sourceNameFromSub, targetName, ConnectionType::EVENT);
                    conn.eventName = sub.eventName;
                    conn.handlerName = sub.handlerName;
                    result.push_back(conn);
                }
            }
        }
    }
    
    return result;
}

std::vector<ConnectionManager::Connection> ConnectionManager::getConnectionsByType(ConnectionType type) const {
    std::vector<Connection> result;
    
    if (!registry_) {
        return result;
    }
    
    switch (type) {
        case ConnectionType::AUDIO: {
            auto allModules = registry_->getAllHumanNames();
            for (const auto& moduleName : allModules) {
                auto targets = audioRouter_.getTargets(moduleName);
                for (const auto& target : targets) {
                    result.emplace_back(moduleName, target, ConnectionType::AUDIO);
                }
            }
            break;
        }
        case ConnectionType::VIDEO: {
            auto allModules = registry_->getAllHumanNames();
            for (const auto& moduleName : allModules) {
                auto targets = videoRouter_.getTargets(moduleName);
                for (const auto& target : targets) {
                    result.emplace_back(moduleName, target, ConnectionType::VIDEO);
                }
            }
            break;
        }
        case ConnectionType::PARAMETER: {
            if (parameterRouter_) {
                auto paramConnections = parameterRouter_->getConnections();
                for (const auto& conn : paramConnections) {
                    Connection cmConn;
                    cmConn.type = ConnectionType::PARAMETER;
                    cmConn.sourcePath = conn.first;
                    cmConn.targetPath = conn.second;
                    cmConn.sourceModule = extractModuleName(conn.first);
                    cmConn.targetModule = extractModuleName(conn.second);
                    result.push_back(cmConn);
                }
            }
            break;
        }
        case ConnectionType::EVENT: {
            auto allModules = registry_->getAllHumanNames();
            for (const auto& moduleName : allModules) {
                auto subs = eventRouter_.getSubscriptionsFrom(moduleName);
                for (const auto& sub : subs) {
                    // Convert UUIDs to names for Connection object
                    std::string sourceName = registry_->getName(sub.sourceUUID);
                    std::string targetName = registry_->getName(sub.targetUUID);
                    if (!sourceName.empty() && !targetName.empty()) {
                        Connection conn(sourceName, targetName, ConnectionType::EVENT);
                        conn.eventName = sub.eventName;
                        conn.handlerName = sub.handlerName;
                        result.push_back(conn);
                    }
                }
            }
            break;
        }
    }
    
    return result;
}

bool ConnectionManager::hasConnection(const std::string& fromModule, const std::string& toModule,
                                     ConnectionType type) const {
    switch (type) {
        case ConnectionType::AUDIO:
            return audioRouter_.hasConnection(fromModule, toModule);
        case ConnectionType::VIDEO:
            return videoRouter_.hasConnection(fromModule, toModule);
        case ConnectionType::EVENT: {
            auto subs = eventRouter_.getSubscriptionsFrom(fromModule);
            for (const auto& sub : subs) {
                // Convert UUID to name for comparison
                if (!registry_) return false;
                std::string targetName = registry_->getName(sub.targetUUID);
                if (targetName == toModule) {
                    return true;
                }
            }
            return false;
        }
        case ConnectionType::PARAMETER: {
            if (parameterRouter_) {
                auto paramConnections = parameterRouter_->getConnections();
                for (const auto& conn : paramConnections) {
                    std::string sourceInstance = extractModuleName(conn.first);
                    std::string targetInstance = extractModuleName(conn.second);
                    if (sourceInstance == fromModule && targetInstance == toModule) {
                        return true;
                    }
                }
            }
            return false;
        }
        default:
            return false;
    }
}

// ========================================================================
// SERIALIZATION - Placeholder implementations
// ========================================================================

ofJson ConnectionManager::toJson() const {
    ofJson json = ofJson::object();
    
    // Serialize audio connections via AudioRouter
    json["audioConnections"] = audioRouter_.toJson();
    
    // Serialize video connections via VideoRouter
    json["videoConnections"] = videoRouter_.toJson();
    
    // Serialize parameter connections (if ParameterRouter is set)
    if (parameterRouter_) {
        json["parameterConnections"] = parameterRouter_->toJson();
    }
    
    // Serialize event subscriptions via EventRouter
    json["eventSubscriptions"] = eventRouter_.toJson();
    
    return json;
}

bool ConnectionManager::fromJson(const ofJson& json) {
    if (!registry_) {
        ofLogError("ConnectionManager") << "Registry not set";
        return false;
    }
    
    // DEBUG: Log what's in the JSON
    int audioConnectionsInJson = 0;
    int videoConnectionsInJson = 0;
    if (json.contains("audioConnections") && json["audioConnections"].is_array()) {
        audioConnectionsInJson = json["audioConnections"].size();
    }
    if (json.contains("videoConnections") && json["videoConnections"].is_array()) {
        videoConnectionsInJson = json["videoConnections"].size();
    }
    ofLogNotice("ConnectionManager") << "fromJson() called - JSON contains " 
                                     << audioConnectionsInJson << " audio connections, "
                                     << videoConnectionsInJson << " video connections";
    
    // DEBUG: Check current connection counts before clear
    int audioConnectionsBefore = audioRouter_.getConnectionCount();
    ofLogNotice("ConnectionManager") << "Current audio connections before clear: " << audioConnectionsBefore;
    
    // Clear existing connections
    clear();
    
    // Load audio connections via AudioRouter
    if (json.contains("audioConnections")) {
        if (!audioRouter_.fromJson(json["audioConnections"])) {
            ofLogWarning("ConnectionManager") << "Failed to restore audio connections";
        } else {
            int audioConnectionsAfter = audioRouter_.getConnectionCount();
            ofLogNotice("ConnectionManager") << "Audio connections after restore: " << audioConnectionsAfter;
        }
    } else {
        ofLogWarning("ConnectionManager") << "JSON does not contain 'audioConnections' key";
    }
    
    // Load video connections via VideoRouter
    if (json.contains("videoConnections")) {
        if (!videoRouter_.fromJson(json["videoConnections"])) {
            ofLogWarning("ConnectionManager") << "Failed to restore video connections";
        }
    }
    
    // Load parameter connections (if ParameterRouter is set)
    if (parameterRouter_ && json.contains("parameterConnections")) {
        if (!parameterRouter_->fromJson(json["parameterConnections"])) {
            ofLogWarning("ConnectionManager") << "Failed to restore parameter connections";
        }
    }
    
    // Load event subscriptions via EventRouter
    // EventRouter::fromJson() handles both UUID-based (new) and name-based (legacy) formats
    if (json.contains("eventSubscriptions")) {
        if (!eventRouter_.fromJson(json["eventSubscriptions"])) {
            ofLogWarning("ConnectionManager") << "Failed to restore event subscriptions";
        }
    }
    
    return true;
}

// ========================================================================
// INTERNAL METHODS - Removed (now in routers)
// ========================================================================
// Note: connectAudioInternal, connectVideoInternal, disconnectAudioInternal,
// disconnectVideoInternal, validateAudioConnection, and validateVideoConnection
// have been moved to AudioRouter and VideoRouter classes.

// Legacy internal methods removed - functionality now in routers
// All connection logic has been moved to AudioRouter, VideoRouter, and EventRouter

std::vector<std::string> ConnectionManager::findOrphanedModules() const {
    std::vector<std::string> orphaned;
    
    if (!registry_) {
        return orphaned;
    }
    
    std::vector<std::string> allModuleNames = registry_->getAllHumanNames();
    
    for (const auto& moduleName : allModuleNames) {
        auto module = getModule(moduleName);
        if (!module || isMixer(module.get())) {
            continue;
        }
        
        auto [hasAudioOutPort, hasVideoOutPort] = checkOutputPorts(module.get());
        if (!hasAudioOutPort && !hasVideoOutPort) {
            continue;
        }
        
        auto audioTargets = audioRouter_.getTargets(moduleName);
        auto videoTargets = videoRouter_.getTargets(moduleName);
        auto audioSources = audioRouter_.getSources(moduleName);
        auto videoSources = videoRouter_.getSources(moduleName);
        
        bool hasAudioOut = !audioTargets.empty();
        bool hasVideoOut = !videoTargets.empty();
        bool hasAudioIn = !audioSources.empty();
        bool hasVideoIn = !videoSources.empty();
        
        // Orphaned: has output ports but has NO outgoing AND NO incoming connections
        bool isOrphaned = (hasAudioOutPort && !hasAudioOut && !hasAudioIn) ||
                          (hasVideoOutPort && !hasVideoOut && !hasVideoIn);
        
        if (isOrphaned) {
            orphaned.push_back(moduleName);
        }
    }
    
    return orphaned;
}

std::vector<std::string> ConnectionManager::findChainEnds() const {
    std::vector<std::string> chainEnds;
    
    if (!registry_) {
        return chainEnds;
    }
    
    std::vector<std::string> allModuleNames = registry_->getAllHumanNames();
    
    for (const auto& moduleName : allModuleNames) {
        auto module = getModule(moduleName);
        if (!module || isMixer(module.get())) {
            continue;
        }
        
        auto [hasAudioOutPort, hasVideoOutPort] = checkOutputPorts(module.get());
        if (!hasAudioOutPort && !hasVideoOutPort) {
            continue;
        }
        
        auto audioTargets = audioRouter_.getTargets(moduleName);
        auto videoTargets = videoRouter_.getTargets(moduleName);
        
        bool hasAudioOut = !audioTargets.empty();
        bool hasVideoOut = !videoTargets.empty();
        
        // Chain end: has output ports but has NO outgoing connections
        bool isChainEnd = (hasAudioOutPort && !hasAudioOut) || (hasVideoOutPort && !hasVideoOut);
        
        if (isChainEnd) {
            chainEnds.push_back(moduleName);
        }
    }
    
    return chainEnds;
}

// Validation methods removed - functionality now in AudioRouter and VideoRouter

// ========================================================================
// INTERNAL HELPER METHODS
// ========================================================================

bool ConnectionManager::isMixer(const Module* module) const {
    if (!module) return false;
    std::string moduleNameLower = module->getName();
    std::transform(moduleNameLower.begin(), moduleNameLower.end(), moduleNameLower.begin(), ::tolower);
    return moduleNameLower.find("mixer") != std::string::npos;
}

std::string ConnectionManager::extractModuleName(const std::string& path) const {
    size_t dotPos = path.find('.');
    return (dotPos != std::string::npos) ? path.substr(0, dotPos) : path;
}

std::pair<bool, bool> ConnectionManager::checkOutputPorts(const Module* module) const {
    if (!module) return {false, false};
    
    bool hasAudio = false;
    bool hasVideo = false;
    auto outputPorts = module->getOutputPorts();
    for (const auto& port : outputPorts) {
        if (port.type == PortType::AUDIO_OUT) {
            hasAudio = true;
        } else if (port.type == PortType::VIDEO_OUT) {
            hasVideo = true;
        }
        if (hasAudio && hasVideo) break; // Early exit if both found
    }
    return {hasAudio, hasVideo};
}

int ConnectionManager::routeModulesToMixers(const std::vector<std::string>& modules,
                                            const std::string& masterAudioMixer,
                                            const std::string& masterVideoMixer,
                                            bool checkExistingConnections) {
    int connectionsCreated = 0;
    
    for (const auto& moduleName : modules) {
        auto module = getModule(moduleName);
        if (!module) continue;
        
        auto [hasAudio, hasVideo] = checkOutputPorts(module.get());
        
        if (hasAudio && !masterAudioMixer.empty()) {
            bool shouldConnect = true;
            if (checkExistingConnections) {
                auto audioTargets = audioRouter_.getTargets(moduleName);
                shouldConnect = audioTargets.empty();
            }
            if (shouldConnect && connectAudio(moduleName, masterAudioMixer)) {
                connectionsCreated++;
                ofLogNotice("ConnectionManager") << "Auto-routed audio: " 
                                                  << moduleName << " -> " << masterAudioMixer;
            }
        }
        
        if (hasVideo && !masterVideoMixer.empty()) {
            bool shouldConnect = true;
            if (checkExistingConnections) {
                auto videoTargets = videoRouter_.getTargets(moduleName);
                shouldConnect = videoTargets.empty();
            }
            if (shouldConnect && connectVideo(moduleName, masterVideoMixer)) {
                connectionsCreated++;
                ofLogNotice("ConnectionManager") << "Auto-routed video: " 
                                                  << moduleName << " -> " << masterVideoMixer;
            }
        }
    }
    
    return connectionsCreated;
}

// ========================================================================
// CONNECTION DISCOVERY (Phase 9.2)
// ========================================================================

int ConnectionManager::discoverConnectionsForModule(const std::string& moduleName) {
    if (!registry_) {
        ofLogWarning("ConnectionManager") << "Registry not set for connection discovery";
        return 0;
    }
    
    auto sourceModule = registry_->getModule(moduleName);
    if (!sourceModule) {
        ofLogWarning("ConnectionManager") << "Module not found: " << moduleName;
        return 0;
    }
    
    int connectionsCreated = 0;
    
    // Discover EVENT connections using ports (Phase 2: port-based discovery)
    auto sourceOutputPorts = sourceModule->getOutputPorts();
    for (const auto& sourcePort : sourceOutputPorts) {
        if (sourcePort.type != PortType::EVENT_OUT) continue;
        
        // Find modules with compatible EVENT_IN ports
        registry_->forEachModule([this, &moduleName, &sourcePort, sourceModule, &connectionsCreated](
            const std::string& targetUuid, const std::string& targetName, std::shared_ptr<Module> targetModule) {
            if (!targetModule || targetName == moduleName) return;
            
            auto targetInputPorts = targetModule->getInputPorts();
            for (const auto& targetPort : targetInputPorts) {
                if (targetPort.type != PortType::EVENT_IN) continue;
                if (!Port::areCompatible(sourcePort, targetPort)) continue;
                
                // Use metadata for event/handler names (ports don't store this)
                auto sourceMetadata = sourceModule->getMetadata();
                auto targetMetadata = targetModule->getMetadata();
                if (sourceMetadata.eventNames.empty() || targetMetadata.eventNames.empty()) continue;
                
                std::string eventName = sourceMetadata.eventNames[0];
                std::string handlerName = targetMetadata.eventNames[0];
                
                if (subscribeEvent(moduleName, eventName, targetName, handlerName)) {
                    connectionsCreated++;
                }
            }
        });
    }
    
    // Discover PARAMETER connections (for bidirectional sync)
    auto sourceMetadata2 = sourceModule->getMetadata();
    if (!sourceMetadata2.parameterNames.empty()) {
        std::string sourceParam = sourceMetadata2.parameterNames[0];
        
        registry_->forEachModule([this, &moduleName, &sourceParam, &connectionsCreated](
            const std::string& targetUuid, const std::string& targetName, std::shared_ptr<Module> targetModule) {
            if (!targetModule || targetName == moduleName) return;
            auto targetMetadata = targetModule->getMetadata();
            if (targetMetadata.parameterNames.empty()) return;
            std::string targetParam = targetMetadata.parameterNames[0];
            
            // Use connectParameterDirect for simpler direct connections
            if (connectParameterDirect(moduleName, sourceParam, targetName, targetParam)) {
                connectionsCreated++;
            }
        });
    }
    
    // Discover AUDIO/VIDEO connections (lower priority - usually explicit)
    // These are typically set up explicitly, but we can auto-connect to master outputs
    // This is handled by autoRouteOrphanedOutputs() which is called separately
    
    return connectionsCreated;
}

int ConnectionManager::discoverConnections(ConnectionType connectionType) {
    if (!registry_) {
        return 0;
    }
    
    int connectionsCreated = 0;
    
    registry_->forEachModule([this, connectionType, &connectionsCreated](
        const std::string& sourceUuid, const std::string& sourceName, std::shared_ptr<Module> sourceModule) {
        if (!sourceModule) return;
        
        auto compatible = findCompatibleModules(sourceName, connectionType);
        for (const auto& targetName : compatible) {
            bool connected = false;
            switch (connectionType) {
                case ConnectionType::AUDIO:
                    connected = connectAudio(sourceName, targetName);
                    break;
                case ConnectionType::VIDEO:
                    connected = connectVideo(sourceName, targetName);
                    break;
                case ConnectionType::EVENT: {
                    // Phase 2: Use ports for event discovery
                    auto sourceOutputPorts = sourceModule->getOutputPorts();
                    auto targetModule = registry_->getModule(targetName);
                    if (!targetModule) break;
                    
                    auto targetInputPorts = targetModule->getInputPorts();
                    for (const auto& sourcePort : sourceOutputPorts) {
                        if (sourcePort.type != PortType::EVENT_OUT) continue;
                        for (const auto& targetPort : targetInputPorts) {
                            if (targetPort.type == PortType::EVENT_IN && Port::areCompatible(sourcePort, targetPort)) {
                                auto sourceMetadata = sourceModule->getMetadata();
                                auto targetMetadata = targetModule->getMetadata();
                                if (!sourceMetadata.eventNames.empty() && !targetMetadata.eventNames.empty()) {
                                    connected = subscribeEvent(sourceName, sourceMetadata.eventNames[0], 
                                                               targetName, targetMetadata.eventNames[0]);
                                    if (connected) break;
                                }
                            }
                        }
                        if (connected) break;
                    }
                    break;
                }
                case ConnectionType::PARAMETER: {
                    auto sourceMetadata = sourceModule->getMetadata();
                    auto targetModule = registry_->getModule(targetName);
                    if (targetModule && !sourceMetadata.parameterNames.empty() && !targetModule->getMetadata().parameterNames.empty()) {
                        // Use connectParameterDirect for simpler direct connections
                        std::string sourceParam = sourceMetadata.parameterNames[0];
                        std::string targetParam = targetModule->getMetadata().parameterNames[0];
                        connected = connectParameterDirect(sourceName, sourceParam, targetName, targetParam);
                    }
                    break;
                }
            }
            if (connected) connectionsCreated++;
        }
    });
    
    return connectionsCreated;
}

std::vector<std::string> ConnectionManager::findCompatibleModules(
    const std::string& moduleName, ConnectionType connectionType) const {
    std::vector<std::string> compatible;
    
    if (!registry_) return compatible;
    
    auto sourceModule = registry_->getModule(moduleName);
    if (!sourceModule) return compatible;
    
    int typeInt = static_cast<int>(connectionType);
    
    registry_->forEachModule([&sourceModule, &moduleName, typeInt, &compatible](
        const std::string& targetUuid, const std::string& targetName, std::shared_ptr<Module> targetModule) {
        if (!targetModule || targetName == moduleName) return;
        if (sourceModule->canConnectTo(targetModule.get(), typeInt)) {
            compatible.push_back(targetName);
        }
    });
    
    return compatible;
}

// ========================================================================
// CONNECTION METADATA QUERIES (Phase 9.5)
// ========================================================================

std::vector<std::string> ConnectionManager::getConnectedModules(
    const std::string& moduleName, ConnectionType connectionType) const {
    std::vector<std::string> connected;
    
    auto connections = getConnectionsFrom(moduleName);
    for (const auto& conn : connections) {
        if (conn.type == connectionType) {
            connected.push_back(conn.targetModule);
        }
    }
    
    // Also check reverse connections (modules connected TO this one)
    auto reverseConnections = getConnectionsTo(moduleName);
    for (const auto& conn : reverseConnections) {
        if (conn.type == connectionType) {
            connected.push_back(conn.sourceModule);
        }
    }
    
    return connected;
}

std::string ConnectionManager::findConnectedModuleByCapability(
    const std::string& moduleName, ModuleCapability capability, ConnectionType connectionType) const {
    if (!registry_) return "";
    
    auto connections = getConnectionsFrom(moduleName);
    for (const auto& conn : connections) {
        if (conn.type == connectionType) {
            auto targetModule = registry_->getModule(conn.targetModule);
            if (targetModule && targetModule->hasCapability(capability)) {
                return conn.targetModule;
            }
        }
    }
    
    return "";
}

//--------------------------------------------------------------
void ConnectionManager::setupDefaultConnections(Clock* clock,
                                                const std::string& masterAudioOutName,
                                                const std::string& masterVideoOutName) {
    if (!registry_) {
        ofLogError("ConnectionManager") << "Cannot setup default connections: Registry is null";
        return;
    }
    
    if (!clock) {
        ofLogError("ConnectionManager") << "Cannot setup default connections: Clock is null";
        return;
    }
    
    // Setup all modules (Clock subscriptions, etc.) - generic, works for all module types
    // NOTE: This may re-initialize already-initialized modules, but modules check listenersRegistered_ to avoid double-registration
    // Pass PatternRuntime if available (for TrackerSequencer pattern access)
    registry_->setupAllModules(clock, registry_, this, parameterRouter_, patternRuntime_, false);
    
    // Auto-route unconnected outputs to master outputs
    // This will only connect modules that aren't already connected
    int autoRouted = autoRouteToMasters(masterAudioOutName, masterVideoOutName);
    if (autoRouted > 0) {
        ofLogNotice("ConnectionManager") << "Auto-routed " << autoRouted 
                                          << " unconnected outputs to master outputs";
    }
    
    // NOTE: Automatic connection discovery between modules has been removed.
    // Previously, this would auto-connect all compatible modules (e.g., all TrackerSequencers
    // to all MediaPools), which created confusing routing. Now only master outputs are
    // auto-connected. Users should manually connect modules via GUI or console for explicit control.
    // The discoverConnectionsForModule() function still exists for manual use if needed.
}

