#pragma once

#include "core/Engine.h"
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
 */
class Shell {
public:
    Shell(Engine* engine) : engine_(engine), active_(false) {}
    virtual ~Shell() = default;
    
    // Lifecycle
    virtual void setup() {}  // Called once during initialization
    virtual void update(float deltaTime) {}  // Called every frame
    virtual void draw() {}  // Called every frame for rendering
    virtual void exit() {}  // Called on shutdown
    
    // Input handling
    virtual bool handleKeyPress(int key) { return false; }  // Return true if handled
    virtual bool handleMousePress(int x, int y, int button) { return false; }
    virtual bool handleMouseDrag(int x, int y, int button) { return false; }
    virtual bool handleMouseRelease(int x, int y, int button) { return false; }
    virtual bool handleWindowResize(int w, int h) { return false; }
    
    // State management
    void setActive(bool active) { active_ = active; }
    bool isActive() const { return active_; }
    
    // Shell metadata
    virtual std::string getName() const = 0;  // e.g., "CLI", "Command", "Patcher", "Editor"
    virtual std::string getDescription() const { return ""; }
    
protected:
    Engine* engine_;  // Reference to the central engine
    bool active_;       // Whether this shell is currently active
};

} // namespace shell
} // namespace vt

