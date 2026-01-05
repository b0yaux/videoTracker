#pragma once

#include "Shell.h"
#include <functional>

namespace vt {
namespace shell {

/**
 * EditorShell - Wraps existing ImGui-based editor interface
 * 
 * This shell provides the traditional tiled-window editor interface
 * that currently exists in ofApp. It wraps all the existing GUI components
 * and manages their lifecycle.
 * 
 * Toggle: F3
 */
class EditorShell : public Shell {
public:
    EditorShell(Engine* engine);
    ~EditorShell() override;
    
    void setup() override;
    void update(float deltaTime) override;
    void draw() override;
    void exit() override;
    
    bool handleKeyPress(int key) override;
    bool handleMousePress(int x, int y, int button) override;
    bool handleWindowResize(int w, int h) override;
    
    std::string getName() const override { return "Editor"; }
    std::string getDescription() const override { return "Traditional tiled-window ImGui editor"; }
    
    // Set callbacks to ofApp's GUI components (called by ofApp during setup)
    void setDrawGUICallback(std::function<void()> callback) { drawGUICallback_ = callback; }
    void setHandleKeyPressCallback(std::function<bool(int)> callback) { handleKeyPressCallback_ = callback; }
    
private:
    // Callbacks to ofApp's GUI components (set during setup)
    std::function<void()> drawGUICallback_;
    std::function<bool(int)> handleKeyPressCallback_;
};

} // namespace shell
} // namespace vt

