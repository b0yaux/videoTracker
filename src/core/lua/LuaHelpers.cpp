#include "LuaHelpers.h"
#include "core/Engine.h"
#include "core/ModuleRegistry.h"
#include "core/ConnectionManager.h"
#include "core/Command.h"
#include "modules/Module.h"
#include "ofLog.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace vt {
namespace lua {

LuaHelpers::LuaHelpers(Engine* engine)
    : engine_(engine)
{
    if (!engine_) {
        ofLogError("LuaHelpers") << "Engine is null";
    }
}

std::string LuaHelpers::createSampler(const std::string& name, const std::map<std::string, std::string>& config) {
    if (!engine_) {
        ofLogError("LuaHelpers") << "Engine is null, cannot create sampler";
        return "";
    }
    
    // IDEMPOTENT: Check if module already exists
    auto& registry = engine_->getModuleRegistry();
    auto existingModule = registry.getModule(name);
    
    if (existingModule) {
        // Module exists - update parameters (idempotent for live-coding)
        ofLogVerbose("LuaHelpers") << "Module already exists, updating parameters: " << name;
        for (const auto& [paramName, value] : config) {
            setParameter(name, paramName, value);
        }
        return name;  // Return existing module name
    }
    
    // Module doesn't exist - create it using command queue
    auto cmd = std::make_unique<vt::AddModuleCommand>("MultiSampler", name);
    bool enqueued = engine_->enqueueCommand(std::move(cmd));
    
    if (!enqueued) {
        ofLogWarning("LuaHelpers") << "Command queue full, falling back to executeCommand";
        std::string command = "add MultiSampler " + name;
        auto result = engine_->executeCommand(command);
        if (!result.success) {
            ofLogError("LuaHelpers") << "Failed to create sampler: " << name << " - " << result.error;
            return "";
        }
    } else {
        // Give command queue a moment to process (module creation is async)
        ofLogNotice("LuaHelpers") << "Enqueued AddModuleCommand for sampler: " << name;
    }
    
    // Apply configuration
    for (const auto& [paramName, value] : config) {
        setParameter(name, paramName, value);
    }
    
    ofLogNotice("LuaHelpers") << "Created sampler: " << name;
    return name;
}

std::string LuaHelpers::createSequencer(const std::string& name, const std::map<std::string, std::string>& config) {
    if (!engine_) {
        ofLogError("LuaHelpers") << "Engine is null, cannot create sequencer";
        return "";
    }
    
    // IDEMPOTENT: Check if module already exists
    auto& registry = engine_->getModuleRegistry();
    auto existingModule = registry.getModule(name);
    
    if (existingModule) {
        // Module exists - update parameters (idempotent for live-coding)
        ofLogVerbose("LuaHelpers") << "Module already exists, updating parameters: " << name;
        for (const auto& [paramName, value] : config) {
            setParameter(name, paramName, value);
        }
        return name;  // Return existing module name
    }
    
    // Module doesn't exist - create it using command queue
    auto cmd = std::make_unique<vt::AddModuleCommand>("TrackerSequencer", name);
    bool enqueued = engine_->enqueueCommand(std::move(cmd));
    
    if (!enqueued) {
        ofLogWarning("LuaHelpers") << "Command queue full, falling back to executeCommand";
        std::string command = "add TrackerSequencer " + name;
        auto result = engine_->executeCommand(command);
        if (!result.success) {
            ofLogError("LuaHelpers") << "Failed to create sequencer: " << name << " - " << result.error;
            return "";
        }
    } else {
        // Give command queue a moment to process (module creation is async)
        ofLogNotice("LuaHelpers") << "Enqueued AddModuleCommand for sequencer: " << name;
    }
    
    // Apply configuration
    for (const auto& [paramName, value] : config) {
        setParameter(name, paramName, value);
    }
    
    ofLogNotice("LuaHelpers") << "Created sequencer: " << name;
    return name;
}

std::string LuaHelpers::createSystemModule(const std::string& moduleType, const std::string& name, const std::map<std::string, std::string>& config) {    if (!engine_) {
        ofLogError("LuaHelpers") << "Engine is null, cannot create system module";
        return "";
    }
    
    // System modules already exist, we just need to configure them
    auto& registry = engine_->getModuleRegistry();
    auto module = registry.getModule(name);
    
    if (!module) {
        ofLogWarning("LuaHelpers") << "System module not found: " << name << " (type: " << moduleType << ")";
        return "";
    }
    
    // Apply configuration (set parameters)
    size_t paramIndex = 0;
    for (const auto& [paramName, value] : config) {        try {
            setParameter(name, paramName, value);
        } catch (const std::exception& e) {
            ofLogError("LuaHelpers") << "Exception setting parameter " << paramName << " = " << value << ": " << e.what();        } catch (...) {
            ofLogError("LuaHelpers") << "Unknown exception setting parameter " << paramName << " = " << value;        }
        paramIndex++;
    }
    
    ofLogNotice("LuaHelpers") << "Configured system module: " << name << " (" << moduleType << ")";
    return name;
}

bool LuaHelpers::connect(const std::string& source, const std::string& target, const std::string& type) {
    if (!engine_) {
        ofLogError("LuaHelpers") << "Engine is null, cannot create connection";
        return false;
    }
    
    // IDEMPOTENT: Check if connection already exists
    auto& connectionManager = engine_->getConnectionManager();
    
    // Convert string connection type to enum
    ConnectionManager::ConnectionType connectionType = ConnectionManager::ConnectionType::AUDIO;
    if (type == "audio") {
        connectionType = ConnectionManager::ConnectionType::AUDIO;
    } else if (type == "video") {
        connectionType = ConnectionManager::ConnectionType::VIDEO;
    } else if (type == "event") {
        connectionType = ConnectionManager::ConnectionType::EVENT;
    } else if (type == "parameter") {
        connectionType = ConnectionManager::ConnectionType::PARAMETER;
    } else {
        // Default to AUDIO if not specified
        connectionType = ConnectionManager::ConnectionType::AUDIO;
    }
    
    // Check if connection already exists (idempotent for live-coding)
    if (connectionManager.hasConnection(source, target, connectionType)) {
        ofLogVerbose("LuaHelpers") << "Connection already exists (skipping): " << source << " -> " << target << " (" << type << ")";
        return true;  // Return true (no-op, idempotent)
    }
    
    // Connection doesn't exist - create it
    std::string command;
    if (type == "event") {
        // Event connections use different syntax
        command = "route " + source + " " + target + " event";
    } else if (type == "video") {
        // Video connections (same as audio for now, route command handles both)
        command = "route " + source + " " + target;
    } else {
        // Audio connections (default)
        command = "route " + source + " " + target;
    }
    
    auto result = engine_->executeCommand(command);
    
    if (!result.success) {
        ofLogError("LuaHelpers") << "Failed to connect " << source << " -> " << target 
                                  << " (" << type << "): " << result.error;
        return false;
    }
    
    ofLogNotice("LuaHelpers") << "Connected " << source << " -> " << target << " (" << type << ")";
    return true;
}

bool LuaHelpers::setParameter(const std::string& moduleName, const std::string& paramName, const std::string& value) {
    if (!engine_) {
        ofLogError("LuaHelpers") << "Engine is null, cannot set parameter";
        return false;
    }
    
    // CRITICAL FIX: Always use command queue for thread safety
    // Direct module access from Lua (main thread) while audio thread processes is unsafe
    // Verify module exists first (for better error messages)
    auto& registry = engine_->getModuleRegistry();
    auto module = registry.getModule(moduleName);
    
    if (!module) {
        ofLogError("LuaHelpers") << "Module not found: " << moduleName;
        return false;
                    }
    
    // Parse value to validate it
    float floatValue = 0.0f;
    try {
        floatValue = parseFloat(value);
        } catch (...) {
        ofLogError("LuaHelpers") << "Invalid parameter value: " << value;
        return false;
    }
    
    // Use command queue for thread-safe parameter setting
    // This ensures all parameter changes are processed atomically on the audio thread
    auto cmd = std::make_unique<vt::SetParameterCommand>(moduleName, paramName, floatValue);
    bool enqueued = engine_->enqueueCommand(std::move(cmd));
    
    if (!enqueued) {
        ofLogWarning("LuaHelpers") << "Command queue full, falling back to executeCommand";
        // Fallback to string command (slower but safe)
    std::string command = "set " + moduleName + " " + paramName + " " + value;
    auto result = engine_->executeCommand(command);
    
    if (!result.success) {
        ofLogError("LuaHelpers") << "Failed to set parameter " << moduleName << "." << paramName 
                                  << " = " << value << ": " << result.error;
        return false;
        }
    }
    
    return true;
}

std::string LuaHelpers::getParameter(const std::string& moduleName, const std::string& paramName) {
    if (!engine_) {
        ofLogError("LuaHelpers") << "Engine is null, cannot get parameter";
        return "";
    }
    
    auto& registry = engine_->getModuleRegistry();
    auto module = registry.getModule(moduleName);
    
    if (!module) {
        ofLogError("LuaHelpers") << "Module not found: " << moduleName;
        return "";
    }
    
    try {
        float value = module->getParameter(paramName);
        std::ostringstream oss;
        oss << value;
        return oss.str();
    } catch (...) {
        ofLogError("LuaHelpers") << "Failed to get parameter " << moduleName << "." << paramName;
        return "";
    }
}

float LuaHelpers::parseFloat(const std::string& value, float defaultValue) {
    if (value.empty()) return defaultValue;
    
    try {
        return std::stof(value);
    } catch (...) {
        return defaultValue;
    }
}

int LuaHelpers::parseInt(const std::string& value, int defaultValue) {
    if (value.empty()) return defaultValue;
    
    try {
        return std::stoi(value);
    } catch (...) {
        return defaultValue;
    }
}

bool LuaHelpers::parseBool(const std::string& value, bool defaultValue) {
    if (value.empty()) return defaultValue;
    
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") {
        return true;
    } else if (lower == "false" || lower == "0" || lower == "no" || lower == "off") {
        return false;
    }
    
    return defaultValue;
}

} // namespace lua
} // namespace vt

