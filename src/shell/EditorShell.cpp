#include "EditorShell.h"
#include "core/Engine.h"
#include "ofLog.h"

namespace vt {
namespace shell {

EditorShell::EditorShell(Engine* engine)
    : Shell(engine)
{
}

EditorShell::~EditorShell() {
}

void EditorShell::setup() {
    // EditorShell is a thin wrapper around ofApp's existing GUI
    // Setup is handled by ofApp, this shell just provides the interface
    ofLogNotice("EditorShell") << "Editor shell setup complete";
}

void EditorShell::update(float deltaTime) {
    if (!active_) return;
    // Updates are handled by ofApp
}

void EditorShell::draw() {
    if (!active_) return;
    
    // Call ofApp's drawGUI callback
    if (drawGUICallback_) {
        drawGUICallback_();
    }
}

void EditorShell::exit() {
    // Cleanup if needed
}

bool EditorShell::handleKeyPress(int key) {
    if (!active_) return false;
    
    // F3 toggles editor (handled by ofApp shell switching)
    if (key == OF_KEY_F3) {
        return false;  // Let ofApp handle shell switching
    }
    
    // Delegate to ofApp's key handler
    if (handleKeyPressCallback_) {
        return handleKeyPressCallback_(key);
    }
    
    return false;
}

bool EditorShell::handleMousePress(int x, int y, int button) {
    if (!active_) return false;
    // Mouse handling is typically done by ImGui
    return false;
}

bool EditorShell::handleWindowResize(int w, int h) {
    if (!active_) return false;
    // Window resize handling
    return false;
}

} // namespace shell
} // namespace vt

