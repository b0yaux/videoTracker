#include "ParameterRouter.h"
#include "TrackerSequencer.h"
#include "MediaPool.h"
#include "MediaPlayer.h"
#include "ofLog.h"
#include "ofJson.h"
#include <cmath>

ParameterRouter::ParameterRouter(ModuleRegistry* registry) 
    : registry(registry) {
    if (!registry) {
        ofLogWarning("ParameterRouter") << "ModuleRegistry is null - routing will not work";
    }
}

ParameterRouter::~ParameterRouter() {
    clear();
}

bool ParameterRouter::connect(const std::string& sourcePath, const std::string& targetPath, 
                              std::function<bool()> condition) {
    ParameterPath source, target;
    
    if (!source.parse(sourcePath)) {
        ofLogError("ParameterRouter") << "Invalid source path: " << sourcePath;
        return false;
    }
    
    if (!target.parse(targetPath)) {
        ofLogError("ParameterRouter") << "Invalid target path: " << targetPath;
        return false;
    }
    
    return connect(source, target, condition);
}

bool ParameterRouter::connect(const ParameterPath& sourcePath, const ParameterPath& targetPath,
                              std::function<bool()> condition) {
    if (!registry) {
        ofLogError("ParameterRouter") << "Cannot connect: ModuleRegistry is null";
        return false;
    }
    
    // Validate paths
    if (!sourcePath.isValid() || !targetPath.isValid()) {
        ofLogError("ParameterRouter") << "Cannot connect: invalid paths";
        return false;
    }
    
    // Resolve source module
    auto sourceModule = resolvePath(sourcePath);
    if (!sourceModule) {
        ofLogError("ParameterRouter") << "Cannot connect: source module not found: " << sourcePath.getInstanceName();
        return false;
    }
    
    // Resolve target module
    auto targetModule = resolvePath(targetPath);
    if (!targetModule) {
        ofLogError("ParameterRouter") << "Cannot connect: target module not found: " << targetPath.getInstanceName();
        return false;
    }
    
    // Check if connection already exists
    auto existing = findConnectionsForSource(sourcePath);
    for (size_t idx : existing) {
        if (idx < connections.size() && connections[idx].targetPath == targetPath) {
            ofLogWarning("ParameterRouter") << "Connection already exists: " << sourcePath.toString() 
                                            << " -> " << targetPath.toString();
            return false;
        }
    }
    
    // Create connection
    Connection conn;
    conn.sourcePath = sourcePath;
    conn.targetPath = targetPath;
    conn.condition = condition ? condition : []() { return true; };
    conn.syncing.store(false, std::memory_order_relaxed);
    
    connections.emplace_back(std::move(conn));
    
    ofLogNotice("ParameterRouter") << "Connected: " << sourcePath.toString() << " -> " << targetPath.toString();
    
    return true;
}

bool ParameterRouter::disconnect(const std::string& sourcePath) {
    ParameterPath path;
    if (!path.parse(sourcePath)) {
        ofLogError("ParameterRouter") << "Invalid source path: " << sourcePath;
        return false;
    }
    
    return disconnect(path);
}

bool ParameterRouter::disconnect(const ParameterPath& sourcePath) {
    auto indices = findConnectionsForSource(sourcePath);
    if (indices.empty()) {
        return false;
    }
    
    // Remove connections (iterate backwards to maintain indices)
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        if (*it < connections.size()) {
            connections.erase(connections.begin() + *it);
        }
    }
    
    ofLogNotice("ParameterRouter") << "Disconnected: " << sourcePath.toString();
    
    return true;
}

void ParameterRouter::clear() {
    size_t count = connections.size();
    connections.clear();
    ofLogNotice("ParameterRouter") << "Cleared " << count << " connections";
}

std::vector<std::pair<std::string, std::string>> ParameterRouter::getConnections() const {
    std::vector<std::pair<std::string, std::string>> result;
    result.reserve(connections.size());
    
    for (const auto& conn : connections) {
        result.push_back({conn.sourcePath.toString(), conn.targetPath.toString()});
    }
    
    return result;
}

std::vector<std::pair<std::string, std::string>> ParameterRouter::getConnectionsFrom(const std::string& sourcePath) const {
    ParameterPath path;
    if (!path.parse(sourcePath)) {
        return {};
    }
    
    auto indices = findConnectionsForSource(path);
    std::vector<std::pair<std::string, std::string>> result;
    result.reserve(indices.size());
    
    for (size_t idx : indices) {
        if (idx < connections.size()) {
            result.push_back({connections[idx].sourcePath.toString(), 
                            connections[idx].targetPath.toString()});
        }
    }
    
    return result;
}

std::vector<std::pair<std::string, std::string>> ParameterRouter::getConnectionsTo(const std::string& targetPath) const {
    ParameterPath path;
    if (!path.parse(targetPath)) {
        return {};
    }
    
    auto indices = findConnectionsForTarget(path);
    std::vector<std::pair<std::string, std::string>> result;
    result.reserve(indices.size());
    
    for (size_t idx : indices) {
        if (idx < connections.size()) {
            result.push_back({connections[idx].sourcePath.toString(), 
                            connections[idx].targetPath.toString()});
        }
    }
    
    return result;
}

void ParameterRouter::notifyParameterChange(Module* module, const std::string& paramName, float value) {
    if (!registry || !module) {
        return;
    }
    
    // Find module identifier (UUID or human name) for command queue
    std::string moduleIdentifier;
    registry->forEachModule([&](const std::string& uuid, const std::string& humanName, std::shared_ptr<Module> m) {
        if (m.get() == module) {
            moduleIdentifier = uuid;  // Use UUID as identifier
        }
    });
    
    if (moduleIdentifier.empty()) {
        ofLogWarning("ParameterRouter") << "Module not found in registry for parameter change notification";
        return;
    }
    
    // Enqueue parameter command for lock-free processing in audio thread
    ParameterCommand cmd(moduleIdentifier, paramName, value);
    if (!commandQueue.try_enqueue(cmd)) {
        // Queue is full - log warning but don't block
        ofLogWarning("ParameterRouter") << "Command queue full, dropping parameter change: " 
                                        << moduleIdentifier << "." << paramName << " = " << value;
    }
    
    // Also process routing immediately for GUI updates (non-blocking, GUI thread only)
    // This ensures UI stays responsive while audio thread processes commands separately
    processRoutingImmediate(module, paramName, value);
}

void ParameterRouter::update() {
    // Periodic update can be used for polling-based sync if needed
    // For now, we rely on parameter change notifications
}

int ParameterRouter::processCommands() {
    if (!registry) {
        return 0;
    }
    
    int processed = 0;
    ParameterCommand cmd;
    
    // Process all queued commands (lock-free, called from audio thread)
    while (commandQueue.try_dequeue(cmd)) {
        // Resolve module from identifier
        auto module = registry->getModule(cmd.moduleIdentifier);
        if (!module) {
            ofLogWarning("ParameterRouter") << "Module not found for command: " << cmd.moduleIdentifier;
            continue;
        }
        
        // Process routing for this parameter change
        processRoutingImmediate(module.get(), cmd.paramName, cmd.value);
        processed++;
    }
    
    return processed;
}

std::shared_ptr<Module> ParameterRouter::resolvePath(const ParameterPath& path) const {
    if (!registry || !path.isValid()) {
        return nullptr;
    }
    
    // Get module by instance name (UUID or human name)
    return registry->getModule(path.getInstanceName());
}

float ParameterRouter::getParameterValue(std::shared_ptr<Module> module, const ParameterPath& path) const {
    if (!module || !path.isValid()) {
        return 0.0f;
    }
    
    // Handle indexed parameters
    if (path.hasIndex()) {
        return getIndexedParameterValue(module, path);
    }
    
    // Special case: TrackerSequencer "currentStepPosition"
    if (path.getParameterName() == "currentStepPosition") {
        TrackerSequencer* ts = dynamic_cast<TrackerSequencer*>(module.get());
        if (ts) {
            return ts->getCurrentStepPosition();
        }
    }
    
    // Special case: MediaPool "position" (get startPosition, not playhead)
    if (path.getParameterName() == "position") {
        MediaPool* mp = dynamic_cast<MediaPool*>(module.get());
        if (mp) {
            auto* player = mp->getActivePlayer();
            if (player) {
                return player->startPosition.get();
            }
        }
    }
    
    // For other parameters, try to use Module interface
    // Note: Module interface doesn't have getParameter(), so we use special cases above
    // Future: Add getParameter() to Module interface if needed
    
    return 0.0f;
}

void ParameterRouter::setParameterValue(std::shared_ptr<Module> module, const ParameterPath& path, float value) {
    if (!module || !path.isValid()) {
        return;
    }
    
    // Handle indexed parameters
    if (path.hasIndex()) {
        setIndexedParameterValue(module, path, value);
        return;
    }
    
    // Special case: TrackerSequencer "currentStepPosition"
    if (path.getParameterName() == "currentStepPosition") {
        TrackerSequencer* ts = dynamic_cast<TrackerSequencer*>(module.get());
        if (ts) {
            ts->setCurrentStepPosition(value);
            return;
        }
    }
    
    // For MediaPool and other modules, use standard Module interface
    module->setParameter(path.getParameterName(), value, false);
}

float ParameterRouter::getIndexedParameterValue(std::shared_ptr<Module> module, const ParameterPath& path) const {
    // Future: Implement indexed parameter access
    // For TrackerSequencer: step[index].position would access pattern cell at step index
    // For now, ignore index and access parameter directly
    
    if (!path.hasIndex()) {
        return getParameterValue(module, path);
    }
    
    // TODO: Implement indexed access
    // Example for TrackerSequencer:
    //   if (path.getParameterName() == "position" && path.hasIndex()) {
    //       TrackerSequencer* ts = dynamic_cast<TrackerSequencer*>(module.get());
    //       if (ts) {
    //           int step = path.getIndex();
    //           return ts->getCell(step).getParameterValue("position", 0.0f);
    //       }
    //   }
    
    // For now, fall back to non-indexed access
    ParameterPath nonIndexedPath = path;
    nonIndexedPath.clearIndex();
    return getParameterValue(module, nonIndexedPath);
}

void ParameterRouter::setIndexedParameterValue(std::shared_ptr<Module> module, const ParameterPath& path, float value) {
    // Future: Implement indexed parameter access
    // For now, ignore index and set parameter directly
    
    if (!path.hasIndex()) {
        setParameterValue(module, path, value);
        return;
    }
    
    // TODO: Implement indexed access
    // For now, fall back to non-indexed access
    ParameterPath nonIndexedPath = path;
    nonIndexedPath.clearIndex();
    setParameterValue(module, nonIndexedPath, value);
}

std::vector<size_t> ParameterRouter::findConnectionsForSource(const ParameterPath& sourcePath) const {
    std::vector<size_t> indices;
    for (size_t i = 0; i < connections.size(); ++i) {
        if (connections[i].sourcePath == sourcePath) {
            indices.push_back(i);
        }
    }
    return indices;
}

std::vector<size_t> ParameterRouter::findConnectionsForTarget(const ParameterPath& targetPath) const {
    std::vector<size_t> indices;
    for (size_t i = 0; i < connections.size(); ++i) {
        if (connections[i].targetPath == targetPath) {
            indices.push_back(i);
        }
    }
    return indices;
}

//--------------------------------------------------------------
ofJson ParameterRouter::toJson() const {
    ofJson json = ofJson::array();
    
    for (const auto& conn : connections) {
        ofJson connJson;
        connJson["source"] = conn.sourcePath.toString();
        connJson["target"] = conn.targetPath.toString();
        // Note: condition is not serialized (runtime-only)
        json.push_back(connJson);
    }
    
    return json;
}

//--------------------------------------------------------------
bool ParameterRouter::fromJson(const ofJson& json) {
    if (!json.is_array()) {
        ofLogError("ParameterRouter") << "Invalid JSON format: expected array";
        return false;
    }
    
    clear(); // Clear existing connections
    
    for (const auto& connJson : json) {
        if (!connJson.is_object() || !connJson.contains("source") || !connJson.contains("target")) {
            ofLogWarning("ParameterRouter") << "Skipping connection with missing required fields";
            continue;
        }
        
        std::string sourcePath = connJson["source"];
        std::string targetPath = connJson["target"];
        
        // Connect without condition (default: always true)
        if (!connect(sourcePath, targetPath)) {
            ofLogWarning("ParameterRouter") << "Failed to restore connection: " << sourcePath << " -> " << targetPath;
        }
    }
    
    return true;
}

//--------------------------------------------------------------
void ParameterRouter::processRoutingImmediate(Module* module, const std::string& paramName, float value) {
    if (!registry || !module) {
        return;
    }
    
    // Find all connections where this module is the source
    // We need to match by module instance and parameter name
    for (size_t i = 0; i < connections.size(); ++i) {
        Connection& conn = connections[i];
        
        // Resolve source module
        auto sourceModule = resolvePath(conn.sourcePath);
        if (!sourceModule || sourceModule.get() != module) {
            continue;
        }
        
        // Check if parameter name matches
        if (conn.sourcePath.getParameterName() != paramName) {
            continue;
        }
        
        // Check if sync should be active
        if (!conn.condition()) {
            continue;
        }
        
        // Prevent feedback loop
        if (conn.syncing.load(std::memory_order_acquire)) {
            continue;
        }
        
        // Set syncing flag
        conn.syncing.store(true, std::memory_order_release);
        
        // Get current target value to check if update is needed
        auto targetModule = resolvePath(conn.targetPath);
        if (targetModule) {
            float currentTargetValue = getParameterValue(targetModule, conn.targetPath);
            
            // Only update if value actually changed (avoid unnecessary updates)
            // Also, for position sync, don't sync 0 if current value is non-zero (preserve position)
            bool shouldUpdate = std::abs(currentTargetValue - value) > 0.0001f;
            if (conn.targetPath.getParameterName() == "position" && value == 0.0f && currentTargetValue > 0.001f) {
                // Don't sync 0 if we have a valid position - this prevents unwanted resets
                shouldUpdate = false;
            }
            
            if (shouldUpdate) {
                setParameterValue(targetModule, conn.targetPath, value);
            }
        }
        
        // Clear syncing flag
        conn.syncing.store(false, std::memory_order_release);
    }
}

