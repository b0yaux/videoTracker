#pragma once

#include "gui/ModuleGUI.h"
#include "gui/GUIConstants.h"
#include <imgui.h>
#include <string>
#include <functional>
#include "ofColor.h"

class Spectrogram;  // Forward declaration
class ModuleRegistry;  // Forward declaration

/**
 * SpectrogramGUI - GUI panel for Spectrogram module
 * 
 * Displays:
 * - Enable/Disable toggle
 * - FFT Size
 * - Window Type
 * - Color Scheme
 * - Frequency Range
 * - Time Window
 * - Opacity
 */
class SpectrogramGUI : public ModuleGUI {
public:
    SpectrogramGUI();
    virtual ~SpectrogramGUI() = default;
    
    // Override draw to call base class (which calls drawContent)
    void draw();
    
protected:
    // Implement ModuleGUI::drawContent() - draws panel-specific content
    void drawContent() override;
    
private:
    // Helper to get current Spectrogram instance from registry
    Spectrogram* getSpectrogram() const;
    
    // GUI section methods
    void drawControls();
    
    // Custom widget methods with CellWidget aesthetic
    void drawCustomSlider(const char* label, float value, float min, float max, const char* format, 
                         std::function<void(float)> onChanged);
    void drawCustomColorPicker(const char* label, const char* popupId, const ofColor& color, 
                              std::function<void(const ofColor&)> onChanged);
};

