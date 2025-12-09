#pragma once

#include "gui/ModuleGUI.h"
#include <string>
#include <vector>

class AudioMixer;  // Forward declaration
class ModuleRegistry;  // Forward declaration

/**
 * AudioMixerGUI - GUI panel for AudioMixer module
 * 
 * Displays:
 * - Master volume control
 * - Per-connection volume controls
 * - Connection list
 * - Audio level visualization
 */
class AudioMixerGUI : public ModuleGUI {
public:
    AudioMixerGUI();
    virtual ~AudioMixerGUI() = default;
    
    // Override draw to call base class (which calls drawContent)
    void draw();
    
protected:
    // Implement ModuleGUI::drawContent() - draws panel-specific content
    void drawContent() override;
    
private:
    // Helper to get current AudioMixer instance from registry
    AudioMixer* getAudioMixer() const;
    
    // GUI section methods
    void drawMasterVolume();
    void drawConnections();
    void drawConnectionVolume(size_t connectionIndex, const std::string& moduleName, float volume);
    void drawAudioLevel();
};

