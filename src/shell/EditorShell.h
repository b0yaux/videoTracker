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
    
    // Set UI managers for state serialization
    void setViewManager(class ViewManager* viewManager) { viewManager_ = viewManager; }
    void setGUIManager(class GUIManager* guiManager) { guiManager_ = guiManager; }
    
    /**
     * Serialize UI state (view state, visibility, ImGui layout)
     * @return JSON with UI state
     */
    ofJson serializeUIState() const;
    
    /**
     * Deserialize UI state
     * @param json JSON with UI state
     * @return true if successful, false otherwise
     */
    bool loadUIState(const ofJson& json);
    
    /**
     * Load pending ImGui state (call this after ImGui is initialized)
     * This should be called from ofApp::drawGUI() or after ImGui setup
     * @return true if layout was loaded, false otherwise
     */
    bool loadPendingImGuiState();
    
private:
    // Callbacks to ofApp's GUI components (set during setup)
    std::function<void()> drawGUICallback_;
    std::function<bool(int)> handleKeyPressCallback_;
    
    // UI managers for state serialization
    class ViewManager* viewManager_ = nullptr;
    class GUIManager* guiManager_ = nullptr;
    
    // Pending ImGui state (loaded from session but ImGui not initialized yet)
    std::string pendingImGuiState_;
    bool imguiStateLoaded_ = false;
    
    // State change handler (override from Shell base class)
    void onStateChanged(const EngineState& state, uint64_t stateVersion) override;
    
    // Cached state for thread-safe access
    EngineState cachedState_;
};

} // namespace shell
} // namespace vt

