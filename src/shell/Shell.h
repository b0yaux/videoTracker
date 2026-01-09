#pragma once

#include "core/Engine.h"
#include "core/Command.h"
#include <memory>

namespace vt {
namespace shell {

/**
 * Shell - Base class for different UI interaction modes
 * 
 * A Shell represents a complete UI mode that can:
 * - Render its interface (draw)
 * - Handle input events (handleKeyPress)
 * - Update its state (update)
 * - Be activated/deactivated (setActive)
 * 
 * Multiple shells can exist simultaneously, but typically only one is "active"
 * (visible and receiving input). Shells share the same Engine instance.
 * 
 * **Shell Abstraction Pattern:**
 * Shells should ONLY interact with Engine through the Shell-safe API:
 * - Use getState() to read state (immutable snapshots)
 * - Use executeCommand() or enqueueCommand() to change state
 * - Use subscribe() to receive state change notifications
 * - NEVER call getModuleRegistry(), getClock(), getScriptManager(), etc.
 * 
 * The Shell base class provides helper methods (see protected section) that
 * enforce this pattern. Use these helpers instead of calling Engine methods directly.
 * 
 * See .planning/phases/05-complete-shell-abstraction-shells/SHELL_ABSTRACTION.md
 * for complete pattern documentation.
 * 
 * See .planning/phases/05-complete-shell-abstraction-shells/SHELL_ABSTRACTION_CHECKLIST.md
 * for code review checklist.
 */
class Shell {
public:
    Shell(Engine* engine) : engine_(engine), active_(false) {}
    virtual ~Shell() {
        // Ensure cleanup even if exit() is not called (RAII pattern)
        if (observerId_ > 0 && engine_) {
            unsubscribe(observerId_);
            observerId_ = 0;
        }
    }
    
    // Lifecycle
    virtual void setup() {
        // Subscribe to state changes if engine is available
        if (engine_ && observerId_ == 0) {
            observerId_ = subscribe([this](const EngineState& state) {
                this->onStateChanged(state);
            });
        }
    }
    virtual void update(float deltaTime) {}  // Called every frame
    virtual void draw() {}  // Called every frame for rendering
    virtual void exit() {
        // Unsubscribe from state changes
        if (observerId_ > 0 && engine_) {
            unsubscribe(observerId_);
            observerId_ = 0;
        }
    }
    
    // Input handling
    virtual bool handleKeyPress(int key) { return false; }  // Return true if handled
    virtual bool handleMousePress(int x, int y, int button) { return false; }
    virtual bool handleMouseDrag(int x, int y, int button) { return false; }
    virtual bool handleMouseRelease(int x, int y, int button) { return false; }
    virtual bool handleWindowResize(int w, int h) { return false; }
    
    // State management
    virtual void setActive(bool active) { active_ = active; }
    bool isActive() const { return active_; }
    
    // Shell metadata
    virtual std::string getName() const = 0;  // e.g., "CLI", "Command", "Patcher", "Editor"
    virtual std::string getDescription() const { return ""; }
    
protected:
    Engine* engine_;  // Reference to the central engine
    bool active_;       // Whether this shell is currently active
    size_t observerId_ = 0;  // Subscription ID for state change notifications
    
    /**
     * Called when engine state changes (override in derived classes if needed).
     * Receives immutable state snapshot (thread-safe).
     * 
     * Default implementation is empty - shells override to handle state updates.
     */
    virtual void onStateChanged(const EngineState& state) {}
    
    // ═══════════════════════════════════════════════════════════
    // SHELL-SAFE API HELPERS (enforce abstraction pattern)
    // ═══════════════════════════════════════════════════════════
    // 
    // These helpers enforce the Shell abstraction pattern by providing
    // a clean interface to Engine's Shell-safe API. Always use these
    // helpers instead of calling engine_->getState(), etc. directly.
    // 
    // This ensures all Shells follow the same pattern and makes it
    // easier to catch violations during code review.
    
    /**
     * Get engine state snapshot (Shell-safe API)
     * Shells should use this instead of accessing Engine internals directly.
     * 
     * Example:
     *   EngineState state = getState();
     *   float bpm = state.transport.bpm;
     */
    EngineState getState() const {
        return engine_->getState();
    }
    
    /**
     * Execute a command (Shell-safe API)
     * Shells should use this for all state changes.
     */
    void executeCommand(const std::string& command) {
        engine_->executeCommand(command);
    }
    
    /**
     * Enqueue a command for audio thread processing (Shell-safe API)
     * Shells should use this for audio-thread-safe state changes.
     */
    bool enqueueCommand(std::unique_ptr<Command> cmd) {
        return engine_->enqueueCommand(std::move(cmd));
    }
    
    /**
     * Subscribe to state changes (Shell-safe API)
     * Shells can use this to receive notifications when state changes.
     */
    size_t subscribe(Engine::StateObserver callback) {
        return engine_->subscribe(callback);
    }
    
    /**
     * Unsubscribe from state changes (Shell-safe API)
     */
    void unsubscribe(size_t id) {
        engine_->unsubscribe(id);
    }
};

} // namespace shell
} // namespace vt

