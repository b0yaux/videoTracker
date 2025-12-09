#pragma once

#include "gui/ModuleGUI.h"
#include <imgui.h>
#include <string>
#include <functional>
#include "ofColor.h"

class Oscilloscope;  // Forward declaration
class ModuleRegistry;  // Forward declaration

/**
 * OscilloscopeGUI - GUI panel for Oscilloscope module
 * 
 * Displays:
 * - Enable/Disable toggle
 * - Scale control
 * - Time window control
 * - Line thickness
 * - Opacity
 * - Color picker
 */
class OscilloscopeGUI : public ModuleGUI {
public:
    OscilloscopeGUI();
    virtual ~OscilloscopeGUI() = default;
    
    // Override draw to call base class (which calls drawContent)
    void draw();
    
protected:
    // Implement ModuleGUI::drawContent() - draws panel-specific content
    void drawContent() override;
    
private:
    // Helper to get current Oscilloscope instance from registry
    Oscilloscope* getOscilloscope() const;
    
    // GUI section methods
    void drawControls();
    
    // Custom widget methods with CellWidget aesthetic
    void drawCustomSlider(const char* label, float value, float min, float max, const char* format, 
                         std::function<void(float)> onChanged);
    void drawCustomColorPicker(const char* label, const ofColor& color, 
                              std::function<void(const ofColor&)> onChanged);
};

