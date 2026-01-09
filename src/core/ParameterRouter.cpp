#include "ParameterRouter.h"
#include "Command.h"  // For SetParameterCommand
#include "Engine.h"
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

bool ParameterRouter::connectDirect(const std::string& sourceModule, const std::string& sourceParam,
                                    const std::string& targetModule, const std::string& targetParam,
                                    std::function<bool()> condition) {
    // Build ParameterPath objects from module and parameter names
    ParameterPath sourcePath;
    sourcePath.setInstanceName(sourceModule);
    sourcePath.setParameterName(sourceParam);
    
    ParameterPath targetPath;
    targetPath.setInstanceName(targetModule);
    targetPath.setParameterName(targetParam);
    
    // Validate that paths are valid (instance and parameter names are non-empty)
    if (sourceModule.empty() || sourceParam.empty() || targetModule.empty() || targetParam.empty()) {
        ofLogError("ParameterRouter") << "Cannot connectDirect: empty module or parameter name";
        return false;
    }
    
    // Use existing connect() method with constructed paths
    return connect(sourcePath, targetPath, condition);
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

void ParameterRouter::renameModule(const std::string& oldName, const std::string& newName) {
    if (oldName == newName || oldName.empty() || newName.empty()) {
        return;
    }
    
    // Update all connections where source or target instance name matches oldName
    for (auto& conn : connections) {
        if (conn.sourcePath.getInstanceName() == oldName) {
            conn.sourcePath.setInstanceName(newName);
        }
        if (conn.targetPath.getInstanceName() == oldName) {
            conn.targetPath.setInstanceName(newName);
        }
    }
    
    ofLogNotice("ParameterRouter") << "Renamed module in parameter connections: " << oldName << " -> " << newName;
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

std::vector<std::shared_ptr<Module>> ParameterRouter::getConnectedModules(const std::string& instanceName) const {
    std::set<std::string> connectedInstanceNames;
    
    // Find all connections where this instance appears as source or target
    for (const auto& conn : connections) {
        std::string sourceInstance = conn.sourcePath.getInstanceName();
        std::string targetInstance = conn.targetPath.getInstanceName();
        
        if (sourceInstance == instanceName) {
            connectedInstanceNames.insert(targetInstance);
        } else if (targetInstance == instanceName) {
            connectedInstanceNames.insert(sourceInstance);
        }
    }
    
    // Resolve instance names to modules
    std::vector<std::shared_ptr<Module>> result;
    if (!registry) return result;
    
    result.reserve(connectedInstanceNames.size());
    for (const auto& name : connectedInstanceNames) {
        auto module = registry->getModule(name);
        if (module) {
            result.push_back(module);
        }
    }
    
    return result;
}

void ParameterRouter::notifyParameterChange(Module* module, const std::string& paramName, float value) {
    if (!registry || !module || !engine_) {
        return;
    }
    
    // Find module human name for SetParameterCommand
    std::string moduleName;
    registry->forEachModule([&](const std::string& uuid, const std::string& humanName, std::shared_ptr<Module> m) {
        if (m.get() == module) {
            moduleName = humanName;  // Use human name for command
        }
    });
    
    if (moduleName.empty()) {
        ofLogWarning("ParameterRouter") << "Module not found in registry for parameter change notification";
        return;
    }
    
    // UNIFIED APPROACH: Enqueue SetParameterCommand to Engine's unified queue
    // This ensures all parameter changes go through the same command processing path
    // SetParameterCommand will handle both parameter setting and routing
    auto cmd = std::make_unique<vt::SetParameterCommand>(moduleName, paramName, value);
    if (!engine_->enqueueCommand(std::move(cmd))) {
        ofLogWarning("ParameterRouter") << "Command queue full, dropping parameter change: " 
                                        << moduleName << "." << paramName << " = " << value;
    }
    
    // NOTE: Routing is handled by SetParameterCommand::execute() which calls
    // processRoutingImmediate() in the audio thread after setting the parameter
}

void ParameterRouter::update() {
    // Periodic update can be used for polling-based sync if needed
    // For now, we rely on parameter change notifications
}

int ParameterRouter::processCommands() {
    // DEPRECATED: Commands are now processed via Engine's unified queue
    // This method is kept for backward compatibility but does nothing
    // All parameter changes are handled by SetParameterCommand in Engine's queue
    return 0;
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
    
    // Use generic Module interface - all modules implement getParameter()
    return module->getParameter(path.getParameterName());
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
    
    // Use generic Module interface - all modules implement setParameter()
    module->setParameter(path.getParameterName(), value, false);
}

float ParameterRouter::getIndexedParameterValue(std::shared_ptr<Module> module, const ParameterPath& path) const {
    if (!path.hasIndex()) {
        return getParameterValue(module, path);
    }
    
    // Use Module interface for indexed parameter access (fully modular)
    // Format: "tracker1.position[4]" where index is the step index (0-based)
    // Modules that support indexing implement supportsIndexedParameters() and getIndexedParameter()
    if (module && module->supportsIndexedParameters()) {
        int index = path.getIndex();
        float value = module->getIndexedParameter(path.getParameterName(), index);
        // If module returns 0.0f, it might be invalid index or unsupported parameter
        // We still return it as the module is responsible for validation
        return value;
    }
    
    // FUTURE EXTENSIONS:
    // - Support nested indexing (e.g., "tracker1.step[4].position[2]" for multi-dimensional data)
    // - Support dynamic index resolution (e.g., "tracker1.position[currentStep]" where currentStep is resolved at runtime)
    // - Add parameter validation and range checking for indexed access
    // - Support wildcard indices for bulk operations (e.g., "tracker1.position[*]")
    // - Add index range queries to Module interface for validation
    
    // Fallback to non-indexed access for modules that don't support indexing
    ParameterPath nonIndexedPath = path;
    nonIndexedPath.clearIndex();
    return getParameterValue(module, nonIndexedPath);
}

void ParameterRouter::setIndexedParameterValue(std::shared_ptr<Module> module, const ParameterPath& path, float value) {
    if (!path.hasIndex()) {
        setParameterValue(module, path, value);
        return;
    }
    
    // Use Module interface for indexed parameter access (fully modular)
    // Format: "tracker1.position[4]" where index is the step index (0-based)
    // Modules that support indexing implement supportsIndexedParameters() and setIndexedParameter()
    if (module && module->supportsIndexedParameters()) {
        int index = path.getIndex();
        module->setIndexedParameter(path.getParameterName(), index, value, true);
        return;
    }
    
    // FUTURE EXTENSIONS:
    // - Support batch updates (e.g., update multiple steps at once for efficiency)
    // - Support conditional updates (e.g., only update if step is currently playing)
    // - Add undo/redo support for indexed parameter changes
    // - Support parameter interpolation across indices (e.g., smooth transitions between steps)
    // - Add validation hooks for indexed parameter values (e.g., range checking per step)
    // - Add index range queries to Module interface for validation
    
    // Fallback to non-indexed access for modules that don't support indexing
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

