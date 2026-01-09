#include "ScriptManager.h"
#include "core/Engine.h"
#include "core/Command.h"
#include "core/PatternRuntime.h"
#include "data/Pattern.h"
#include "ofLog.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <variant>
#include <fstream>
#include <chrono>

namespace vt {

ScriptManager::ScriptManager(Engine* engine)
    : engine_(engine)
    , observerId_(0)
    , scriptNeedsUpdate_(false)
    , autoUpdateEnabled_(true)
{
}

ScriptManager::~ScriptManager() {
    if (engine_ && observerId_ > 0) {
        engine_->unsubscribe(observerId_);
    }
}

void ScriptManager::setup() {
    if (!engine_) {
        ofLogError("ScriptManager") << "Engine is null, cannot setup";
        return;
    }
    
    // Subscribe to Engine state changes
    observerId_ = engine_->subscribe([this](const EngineState& state) {
        if (!autoUpdateEnabled_) {
            return;
        }
        
        
        // CRITICAL FIX: Completely skip script updates during script execution
        // This prevents the callback from being called during script execution,
        // which could cause crashes when SetText() is called at unsafe times
        // The state observer will fire again after script execution completes
        if (engine_ && engine_->isExecutingScript()) {
            ofLogVerbose("ScriptManager") << "Skipping script update - script execution in progress";
            return;  // Don't update at all during script execution
        }
        
        // Also skip during command processing to be extra safe
        if (engine_ && engine_->commandsBeingProcessed()) {
            ofLogVerbose("ScriptManager") << "Skipping script update - commands processing";
            return;  // Don't update during command processing either
        }
        
        // State version verification handles pending command detection:
        // State version only increments AFTER commands are processed by audio thread (in updateStateSnapshot())
        // Therefore, if state.version < engine_->getStateVersion(), commands are pending → defer regeneration
        // This is more reliable than hasPendingCommands() with size_approx() which has race conditions
        // (confirmed by debug logs: commands enqueued but hasPendingCommands() returned false)
        
        // State is safe - update immediately (state version check happens in updateScriptFromState())
        
        // CRITICAL: Log immediately before calling updateScriptFromState
        {
        }
        
        updateScriptFromState(state);
        
        // CRITICAL: Log immediately after calling updateScriptFromState
        {
        }
    });
    
    // Generate initial script from CURRENT state (after session load)
    // This will now contain the loaded session data
    // WARNING: This builds a snapshot during setup - acceptable since setup is synchronous
    EngineState currentState = engine_->getState();
    updateScriptFromState(currentState);
    
    ofLogNotice("ScriptManager") << "ScriptManager setup complete - script generated from loaded session";
}

std::string ScriptManager::generateScriptFromState(const EngineState& state) const {
    // CRITICAL FIX: Use passed state instead of calling getState() again
    // This prevents double snapshot building and crashes
    // The state is already provided by the observer callback
    std::ostringstream script;
    
    script << "-- videoTracker Session Script\n\n";
    
    // Transport - simplified (engine global is always available now)
    script << "-- Transport\n";
    script << "local clock = engine:getClock()\n";
    script << "clock:setBPM(" << formatLuaValue(state.transport.bpm) << ")\n";
    if (state.transport.isPlaying) {
        script << "clock:start()\n";
    } else {
        script << "clock:stop()\n";
    }
    script << "\n";
    
    // Modules
    if (!state.modules.empty()) {
        script << "-- Modules\n";
        bool firstModule = true;
        for (const auto& [name, moduleState] : state.modules) {
            if (!firstModule) {
                script << "\n";
            }
            std::string moduleScript = generateModuleScript(name, moduleState);
            script << moduleScript;
            firstModule = false;
        }
        script << "\n";
    }
    
    // Connections
    if (!state.connections.empty()) {
        script << "-- Connections\n";
        int activeConnections = 0;
        for (const auto& conn : state.connections) {
            if (conn.active) {
                activeConnections++;
                // All connections use declarative connect() syntax (idempotent, works for all types)
                // Convert connection type enum string to lowercase for connect() function
                std::string connType = "audio";  // Default to audio
                if (conn.connectionType == "EVENT") {
                    connType = "event";
                } else if (conn.connectionType == "VIDEO") {
                    connType = "video";
                } else if (conn.connectionType == "PARAMETER") {
                    connType = "parameter";
                } else if (conn.connectionType == "AUDIO") {
                    connType = "audio";
                }
                // Use declarative connect() for all connections (system-to-system, user-to-user, user-to-system)
                // connect() is idempotent and handles all connection types
                script << "connect(\"" << conn.sourceModule << "\", \"" 
                       << conn.targetModule << "\", \"" << connType << "\")\n";
            }
        }
        script << "\n";
    }
    
    // Patterns (from PatternRuntime)
    if (engine_) {
        
        try {
            auto& patternRuntime = engine_->getPatternRuntime();
            
            
            auto patternNames = patternRuntime.getPatternNames();
            
            
            if (!patternNames.empty()) {
                script << "-- Patterns\n";
                for (const auto& patternName : patternNames) {
                    script << generatePatternScript(patternName);
                }
            }
        } catch (const std::exception& e) {
            ofLogError("ScriptManager") << "Exception accessing PatternRuntime: " << e.what();
        } catch (...) {
            ofLogError("ScriptManager") << "Unknown exception accessing PatternRuntime";
        }
    }
    
    return script.str();
}

std::string ScriptManager::generateScriptFromCurrentState() const {
    // Fallback: Generate from current engine state
    // WARNING: This builds a snapshot - only use when state is not available
    if (!engine_) {
        return "-- Engine not available\n";
    }
    
    EngineState state = engine_->getState();
    return generateScriptFromState(state);
}

std::string ScriptManager::generateIncrementalScript(
    const EngineState& previousState,
    const EngineState& currentState) const {
    
    std::ostringstream script;
    
    // Check transport changes
    if (previousState.transport.bpm != currentState.transport.bpm ||
        previousState.transport.isPlaying != currentState.transport.isPlaying) {
        script << generateTransportScript(currentState.transport);
        script << "\n";
    }
    
    // Check for new/modified modules
    for (const auto& [name, moduleState] : currentState.modules) {
        auto it = previousState.modules.find(name);
        if (it == previousState.modules.end() || 
            it->second.toJson() != moduleState.toJson()) {
            script << generateModuleScript(name, moduleState);
            script << "\n";
        }
    }
    
    // Check for removed modules (not handled in incremental - would need explicit remove command)
    
    // Check for new/modified connections
    for (const auto& conn : currentState.connections) {
        if (conn.active) {
            bool found = false;
            for (const auto& prevConn : previousState.connections) {
                if (prevConn.sourceModule == conn.sourceModule &&
                    prevConn.targetModule == conn.targetModule &&
                    prevConn.connectionType == conn.connectionType) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                script << generateConnectionScript(conn);
                script << "\n";
            }
        }
    }
    
    return script.str();
}

std::string ScriptManager::generateTransportScript(
    const EngineState::Transport& transport) const {
    std::ostringstream script;
    script << "-- Transport\n";
    script << "local clock = engine:getClock()\n";
    script << "clock:setBPM(" << formatLuaValue(transport.bpm) << ")\n";
    if (transport.isPlaying) {
        script << "clock:start()\n";
    } else {
        script << "clock:stop()\n";
    }
    return script.str();
}

std::string ScriptManager::generateModuleScript(
    const std::string& name,
    const EngineState::ModuleState& module) const {
    std::ostringstream script;
    
    // Check if this is a system module (should use command syntax)
    bool isSystemModule = (name == "masterAudioOut" || 
                          name == "masterVideoOut" || 
                          name == "masterOscilloscope" || 
                          name == "masterSpectrogram");
    
    std::string moduleType = module.type;
    
    // System modules: use declarative helper functions (cleaner syntax)
    if (isSystemModule) {
        // Collect parameters for config table
        std::vector<std::pair<std::string, float>> configParams;
        for (const auto& [paramName, value] : module.parameters) {
            configParams.push_back({paramName, value});
        }
        
        // Generate helper function call with config table
        std::string helperName;
        if (moduleType == "AudioOutput") {
            helperName = "audioOut";
        } else if (moduleType == "VideoOutput") {
            helperName = "videoOut";
        } else if (moduleType == "Oscilloscope") {
            helperName = "oscilloscope";
        } else if (moduleType == "Spectrogram") {
            helperName = "spectrogram";
        } else {
            // Fallback to command syntax for unknown system modules
            helperName = "";
        }
        
        if (!helperName.empty()) {
            // Use declarative helper function
            if (configParams.empty()) {
                script << "local " << name << " = " << helperName << "(\"" << name << "\")\n";
            } else {
                script << "local " << name << " = " << helperName << "(\"" << name << "\", {\n";
                for (size_t i = 0; i < configParams.size(); ++i) {
                    script << "    " << configParams[i].first << " = " 
                           << formatLuaValue(configParams[i].second);
                    if (i < configParams.size() - 1) {
                        script << ",";
                    }
                    script << "\n";
                }
                script << "})\n";
            }
            
            // Add comment if disabled
            if (!module.enabled) {
                script << "-- Module disabled\n";
            }
        } else {
            // Fallback: use command syntax
            script << "-- Module: " << name << " (" << moduleType << ")\n";
            script << "engine:executeCommand(\"add " << moduleType << " " << name << "\")\n";
            if (!module.enabled) {
                script << "-- Module disabled\n";
            }
            for (const auto& [paramName, value] : module.parameters) {
                script << "engine:executeCommand(\"set " << name << " " 
                       << paramName << " " << formatLuaValue(value) << "\")\n";
            }
        }
        return script.str();
    }
    
    // User modules: use declarative syntax (live-coding style)
    // Generate declarative syntax for user modules
    if (moduleType == "MultiSampler") {
        // Helper to check if value is default
        auto isDefaultValue = [](const std::string& paramName, float value) -> bool {
            // MultiSampler defaults from MultiSampler.cpp
            if (paramName == "attackMs" && std::abs(value - 0.0f) < 0.001f) return true;
            if (paramName == "decayMs" && std::abs(value - 0.0f) < 0.001f) return true;
            if (paramName == "sustain" && std::abs(value - 1.0f) < 0.001f) return true;
            if (paramName == "releaseMs" && std::abs(value - 10.0f) < 0.001f) return true;
            if (paramName == "speed" && std::abs(value - 1.0f) < 0.001f) return true;
            if (paramName == "volume" && std::abs(value - 1.0f) < 0.001f) return true;
            if (paramName == "position" && std::abs(value - 0.0f) < 0.001f) return true;
            if (paramName == "regionStart" && std::abs(value - 0.0f) < 0.001f) return true;
            if (paramName == "regionEnd" && std::abs(value - 1.0f) < 0.001f) return true;
            if (paramName == "grainSize" && std::abs(value - 0.0f) < 0.001f) return true;
            if (paramName == "polyphonyMode" && std::abs(value - 1.0f) < 0.001f) return true;
            if (paramName == "index" && std::abs(value - 0.0f) < 0.001f) return true;
            return false;
        };
        
        // Collect all parameters for config table (better for live-coding - explicit and editable)
        // Filter out pattern-related params that are handled separately
        std::vector<std::pair<std::string, float>> configParams;
        for (const auto& [paramName, value] : module.parameters) {
            // Skip pattern-related parameters
            if (paramName != "index") {
                // Include all parameters for explicit live-coding (users can see and edit all values)
                configParams.push_back({paramName, value});
            }
        }
        
        // Generate sampler() with config table
        if (configParams.empty()) {
            script << "local " << name << " = sampler(\"" << name << "\")\n";
        } else {
            script << "local " << name << " = sampler(\"" << name << "\", {\n";
            for (size_t i = 0; i < configParams.size(); ++i) {
                script << "    " << configParams[i].first << " = " 
                       << formatLuaValue(configParams[i].second);
                if (i < configParams.size() - 1) {
                    script << ",";
                }
                script << "\n";
            }
            script << "})\n";
        }
    } else if (moduleType == "TrackerSequencer") {
        // TrackerSequencer: pattern parameters (index, length, note, chance, ratio) are handled in pattern data
        // Collect other parameters for config table (explicit for live-coding)
        std::vector<std::pair<std::string, float>> configParams;
        for (const auto& [paramName, value] : module.parameters) {
            // Skip pattern-related parameters (handled in pattern data)
            if (paramName != "index" && paramName != "length" && 
                paramName != "note" && paramName != "chance" && paramName != "ratio") {
                // Include all non-pattern params for explicit live-coding
                configParams.push_back({paramName, value});
            }
        }
        
        // Generate sequencer() with config table
        if (configParams.empty()) {
            script << "local " << name << " = sequencer(\"" << name << "\")\n";
        } else {
            script << "local " << name << " = sequencer(\"" << name << "\", {\n";
            for (size_t i = 0; i < configParams.size(); ++i) {
                script << "    " << configParams[i].first << " = " 
                       << formatLuaValue(configParams[i].second);
                if (i < configParams.size() - 1) {
                    script << ",";
                }
                script << "\n";
            }
            script << "})\n";
        }
        
        // Type-specific state (step count handled by pattern data)
    } else {
        // Other user modules: use command syntax for now (can be enhanced later)
        script << "engine:executeCommand(\"add " << moduleType << " " << name << "\")\n";
        
        // Set parameters
        for (const auto& [paramName, value] : module.parameters) {
            script << "engine:executeCommand(\"set " << name << " " 
                   << paramName << " " << formatLuaValue(value) << "\")\n";
        }
    }
    
    return script.str();
}

std::string ScriptManager::generateConnectionScript(
    const ConnectionInfo& conn) const {
    std::ostringstream script;
    
    // Build connection command based on type
    if (conn.connectionType == "AUDIO" || conn.connectionType == "VIDEO") {
        script << "engine:executeCommand(\"route " << conn.sourceModule 
               << " " << conn.targetModule << "\")\n";
    } else if (conn.connectionType == "PARAMETER") {
        script << "-- Parameter connection: " << conn.sourceModule 
               << "." << conn.sourcePath << " -> " 
               << conn.targetModule << "." << conn.targetPath << "\n";
        // Parameter routing might need a different command format
        script << "engine:executeCommand(\"route " << conn.sourceModule 
               << " " << conn.targetModule << " parameter\")\n";
    } else if (conn.connectionType == "EVENT") {
        script << "-- Event connection: " << conn.sourceModule 
               << " -> " << conn.targetModule << " (" << conn.eventName << ")\n";
    }
    
    return script.str();
}

std::string ScriptManager::generatePatternScript(
    const std::string& patternName) const {
    if (!engine_) {
        return "-- Pattern: " + patternName + " (engine not available)\n";
    }
    
    std::ostringstream script;
    
    
    try {
        auto& patternRuntime = engine_->getPatternRuntime();
        
        
        // CRITICAL FIX: Use thread-safe method that returns step count directly
        // This ensures the value is copied while the lock is still held
        
        int stepCount = patternRuntime.getPatternStepCount(patternName);
        
        
        if (stepCount < 0) {
            script << "-- Pattern: " << patternName << " (not found)\n";
            return script.str();
        }
        
        script << "pattern(\"" << patternName << "\", " << stepCount << ")\n";
    } catch (const std::exception& e) {
        ofLogError("ScriptManager") << "Exception in generatePatternScript for " << patternName << ": " << e.what();
        script << "-- Pattern: " << patternName << " (error: " << e.what() << ")\n";
    } catch (...) {
        ofLogError("ScriptManager") << "Unknown exception in generatePatternScript for " << patternName;
        script << "-- Pattern: " << patternName << " (unknown error)\n";
    }
    
    return script.str();
}

void ScriptManager::updateScriptFromState(const EngineState& state) {
    
    // CRITICAL: Check if script is executing BEFORE any script generation
    // If script is executing, defer update - script execution will complete, then observer will fire again
    // This prevents ScriptManager from regenerating script during execution, which causes crashes
    if (engine_ && engine_->isExecutingScript()) {
        ofLogVerbose("ScriptManager") << "Deferring script update - script execution in progress";
        return;  // Return immediately without calling generateScriptFromState()
    }
    
    // CRITICAL FIX: Check render guard to prevent script updates during rendering
    // This prevents crashes from script updates during ImGui rendering
    // Script generation is now event-driven (via notification queue), not timing-based
    if (engine_ && engine_->isRendering()) {
        ofLogVerbose("ScriptManager") << "Deferring script update - rendering in progress";
        return;  // Return immediately without calling generateScriptFromState()
    }
    
    // CRITICAL: Verify state version is current (not stale)
    // This ensures script generation sees consistent, up-to-date state
    // State version should match or be close to current engine version
    uint64_t stateVersion = state.version;
    if (engine_) {
        uint64_t currentEngineVersion = engine_->getStateVersion();
        
        
        // CRITICAL: Reject ANY stale state to prevent feedback loops
        // State version only increments AFTER commands are processed by audio thread
        // If state.version < currentEngineVersion, commands are pending → defer regeneration
        // Exception: Allow version 0 during initialization (valid initial state)
        if (stateVersion > 0 && stateVersion < currentEngineVersion) {
            ofLogWarning("ScriptManager") << "State version is stale (state: " << stateVersion 
                                          << ", engine: " << currentEngineVersion 
                                          << ") - deferring script generation (commands pending)";
            return;  // Defer until commands are processed and state version increments
        }
        
        // Log when state version check passes (state is current)
        if (stateVersion >= currentEngineVersion || stateVersion == 0) {
            ofLogNotice("ScriptManager") << "State version check passed (state: " << stateVersion 
                                         << ", engine: " << currentEngineVersion 
                                         << ") - proceeding with script generation";
        }
    }
    
    // Don't defer during command processing - state is already consistent when this is called
    
    // Check if we've already regenerated from this state version (prevent redundant regenerations)
    if (stateVersion > 0 && stateVersion <= lastRegeneratedVersion_) {
        ofLogVerbose("ScriptManager") << "Skipping redundant script regeneration (state version: " << stateVersion 
                                      << ", last regenerated: " << lastRegeneratedVersion_ << ")";
        return;
    }
    
    // Check if state actually changed
    if (!hasStateChanged(lastState_, state)) {
        return;
    }
    
    
    // CRITICAL FIX: Use passed state instead of calling getState() again
    // This prevents double snapshot building and crashes
    // The state is already provided by the observer callback
    try {
        std::string generatedScript = generateScriptFromState(state);
        currentScript_ = generatedScript;
        
        // Update lastRegeneratedVersion_ immediately after successful script generation
        // This prevents redundant regenerations if callback triggers recursive state changes
        lastRegeneratedVersion_ = stateVersion;
        ofLogNotice("ScriptManager") << "Script successfully regenerated (state version: " << stateVersion 
                                     << ", lastRegeneratedVersion updated to: " << lastRegeneratedVersion_ << ")";
        
    } catch (const std::exception& e) {
        ofLogError("ScriptManager") << "Exception in generateScriptFromState: " << e.what();
        return;
    } catch (...) {
        ofLogError("ScriptManager") << "Unknown exception in generateScriptFromState";
        return;
    }
    
    scriptNeedsUpdate_ = true;
    lastState_ = state;
    
    
    // Notify callback (CodeShell will register this)
    if (updateCallback_) {
        // CRITICAL: Log immediately before calling callback
        {
        }
        
        try {
            updateCallback_(currentScript_);
            
            // CRITICAL: Log immediately after calling callback
            {
            }
        } catch (const std::exception& e) {
            ofLogError("ScriptManager") << "Exception in updateCallback: " << e.what();
        } catch (...) {
            ofLogError("ScriptManager") << "Unknown exception in updateCallback";
        }
    }
    
}

bool ScriptManager::hasStateChanged(
    const EngineState& oldState,
    const EngineState& newState) const {
    // Simple comparison using JSON serialization
    // Can be optimized later with more granular comparison
    return oldState.toJson() != newState.toJson();
}

void ScriptManager::setScriptUpdateCallback(ScriptUpdateCallback callback) {
    updateCallback_ = callback;
    
    // CRITICAL FIX: Immediately generate and call callback with current script
    // This ensures CodeShell gets the FULL script (with modules/connections) when it registers
    // We force regeneration from current state, not cached script, to ensure it's up-to-date
    if (updateCallback_ && engine_) {
        try {
            // Force regeneration from current state (don't use cached script)
            // This ensures we get modules/connections even if script was generated before they were loaded
            EngineState currentState = engine_->getState();
            std::string script = generateScriptFromState(currentState);
            if (!script.empty()) {
                // Update cached script and notify callback
                currentScript_ = script;
                updateCallback_(script);
                ofLogNotice("ScriptManager") << "Immediately notified callback with regenerated script (" 
                                            << script.length() << " chars, " 
                                            << currentState.modules.size() << " modules, "
                                            << currentState.connections.size() << " connections)";
            } else {
                ofLogVerbose("ScriptManager") << "No script generated yet - callback will be called when script is generated";
            }
        } catch (const std::exception& e) {
            ofLogError("ScriptManager") << "Exception in immediate callback: " << e.what();
        } catch (...) {
            ofLogError("ScriptManager") << "Unknown exception in immediate callback";
        }
    }
}

std::string ScriptManager::getCurrentScript() const {
    
    // CRITICAL FIX: Prevent recursive snapshot building
    // If we're currently building a snapshot, return cached script without regenerating
    // This prevents deadlock when buildStateSnapshot() calls getCurrentScript()
    if (engine_ && Engine::isBuildingSnapshot()) {
        ofLogVerbose("ScriptManager") << "getCurrentScript() called during snapshot building - returning cached script to prevent deadlock";
        // Return cached script (may be empty, but that's okay - it will be populated after snapshot completes)
        return currentScript_;
    }
    
    // SIMPLIFIED: getState() handles unsafe periods, so we can call it safely
    // It will return cached state during unsafe periods
    if (currentScript_.empty() && engine_) {
        
        currentScript_ = generateScriptFromCurrentState();
    }
    return currentScript_;
}

std::string ScriptManager::formatLuaValue(float value) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << value;
    std::string str = oss.str();
    // Remove trailing zeros
    str.erase(str.find_last_not_of('0') + 1, std::string::npos);
    str.erase(str.find_last_not_of('.') + 1, std::string::npos);
    return str;
}

std::string ScriptManager::formatLuaValue(bool value) const {
    return value ? "true" : "false";
}

std::string ScriptManager::formatLuaValue(const std::string& value) const {
    // Escape quotes and wrap in quotes
    std::string escaped = value;
    size_t pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos) {
        escaped.replace(pos, 1, "\\\"");
        pos += 2;
    }
    return "\"" + escaped + "\"";
}

void ScriptManager::requestUpdate() {
    // CRITICAL FIX: Always defer update and use frame delay to ensure state is stable
    // This prevents updating from potentially inconsistent state
    if (!engine_) {
        return;
    }
    
    
    // Request update - state observer will handle it when state is safe
    scriptNeedsUpdate_ = true;
    
    ofLogVerbose("ScriptManager") << "Requested script update - state observer will handle when safe";
}

} // namespace vt

