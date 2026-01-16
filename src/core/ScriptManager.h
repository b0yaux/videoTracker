#pragma once

#include "EngineState.h"
#include <string>
#include <functional>
#include <memory>
#include <atomic>

namespace vt {

// Forward declarations
class Engine;
class Command;

/**
 * ScriptManager - Generates and maintains Lua scripts from Engine state
 * 
 * Responsibilities:
 * - Observe Engine state changes
 * - Generate Lua scripts representing current state
 * - Provide incremental updates (only changed parts)
 * - Support bidirectional sync (state <-> script)
 * 
 * This enables shells (CodeShell, CommandShell, EditorShell) to stay
 * synchronized with the current session state.
 * 
 * **Current Implementation**: Generates session reconstruction scripts
 * (command-based, imperative syntax). Future: Will generate live-coding
 * syntax (declarative, functional) inspired by Tidal/Strudel/Hydra.
 * 
 * See docs/SCRIPTING_ARCHITECTURE_ANALYSIS.md for roadmap.
 */
class ScriptManager {
public:
    ScriptManager(Engine* engine);
    ~ScriptManager();
    
    // Setup - subscribes to Engine state changes
    void setup();
    
    // Generate complete Lua script from state
    // CRITICAL: Accepts state parameter to avoid double snapshot building
    // Use this version when state is already available (e.g., from observer callback)
    std::string generateScriptFromState(const EngineState& state) const;
    
    // Generate script from current engine state (fallback for when state not available)
    // WARNING: This calls engine_->getState() which builds a snapshot - use with caution
    std::string generateScriptFromCurrentState() const;
    
    // Generate incremental update script (only changes since last snapshot)
    std::string generateIncrementalScript(const EngineState& previousState, 
                                         const EngineState& currentState) const;
    
    // Get current script representation
    // If script is empty, generates it from current state
    std::string getCurrentScript() const;
    
    // Get cached script without triggering generation (for use during snapshot building)
    // Returns empty string if script not cached yet
    std::string getCachedScript() const { return currentScript_; }
    
    // Check if script is cached (non-empty)
    bool hasCachedScript() const { return !currentScript_.empty(); }
    
    // Update script from state (called on state changes)
    void updateScriptFromState(const EngineState& state);
    
    // Check if script needs update
    bool needsUpdate() const { return scriptNeedsUpdate_; }
    void clearUpdateFlag() { scriptNeedsUpdate_ = false; }
    
    // Callback for shells to register (called when script updates)
    using ScriptUpdateCallback = std::function<void(const std::string& script)>;
    void setScriptUpdateCallback(ScriptUpdateCallback callback);
    
    // Enable/disable auto-updates (useful when user is manually editing)
    void setAutoUpdate(bool enabled) { autoUpdateEnabled_ = enabled; }
    bool isAutoUpdateEnabled() const { return autoUpdateEnabled_; }
    
    // Request deferred update (for use after script execution completes)
    void requestUpdate();
    
private:
    Engine* engine_;
    size_t observerId_;
    EngineState lastState_;
    mutable std::string currentScript_;  // Mutable for lazy initialization in const methods
    bool scriptNeedsUpdate_ = false;
    bool autoUpdateEnabled_ = true;
    ScriptUpdateCallback updateCallback_;
    
    // Simple atomic state machine for update coordination
    // Eliminates 3 layers of deferred updates with single atomic guard
    enum class UpdateState { IDLE, UPDATING };
    std::atomic<UpdateState> updateState_{UpdateState::IDLE};
    
    // Script generation helpers
    std::string generateTransportScript(const EngineState::Transport& transport) const;
    std::string generateModuleScript(const std::string& name, 
                                     const EngineState::ModuleState& module) const;
    std::string generateConnectionScript(const ConnectionInfo& conn) const;
    std::string generatePatternScript(const std::string& patternName) const;
    
    // State comparison
    bool hasStateChanged(const EngineState& oldState, 
                        const EngineState& newState) const;
    
    // Helper to format Lua values
    std::string formatLuaValue(float value) const;
    std::string formatLuaValue(bool value) const;
    std::string formatLuaValue(const std::string& value) const;
};

} // namespace vt

