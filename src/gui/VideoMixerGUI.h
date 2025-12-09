#pragma once

#include "gui/ModuleGUI.h"
#include "ofMain.h"  // For ofBlendMode
#include <string>
#include <vector>

class VideoMixer;  // Forward declaration
class ModuleRegistry;  // Forward declaration

/**
 * VideoMixerGUI - GUI panel for VideoMixer module
 * 
 * Displays:
 * - Master opacity control
 * - Blend mode selection
 * - Auto-normalize toggle
 * - Per-connection opacity controls
 * - Connection list
 */
class VideoMixerGUI : public ModuleGUI {
public:
    VideoMixerGUI();
    virtual ~VideoMixerGUI() = default;
    
    // Override draw to call base class (which calls drawContent)
    void draw();
    
protected:
    // Implement ModuleGUI::drawContent() - draws panel-specific content
    void drawContent() override;
    
private:
    // Helper to get current VideoMixer instance from registry
    VideoMixer* getVideoMixer() const;
    
    // GUI section methods
    void drawMasterControls();
    void drawBlendMode();
    void drawConnections();
    void drawConnectionOpacity(size_t connectionIndex, const std::string& moduleName, float opacity);
    
    // Blend mode names
    static const char* getBlendModeName(ofBlendMode mode);
};

