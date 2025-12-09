#pragma once

#include "gui/ModuleGUI.h"
#include "ofMain.h"  // For ofBlendMode
#include <string>
#include <vector>
#include <functional>

class VideoOutput;  // Forward declaration
class ModuleRegistry;  // Forward declaration

/**
 * VideoOutputGUI - GUI panel for VideoOutput module
 * 
 * Displays:
 * - FPS information
 * - Output information (resolution, aspect ratio)
 * - Master opacity (from mixer functionality)
 * - Blend mode (optional)
 * - Connections (from mixer functionality)
 */
class VideoOutputGUI : public ModuleGUI {
public:
    VideoOutputGUI();
    virtual ~VideoOutputGUI() = default;
    
    // Override draw to call base class (which calls drawContent)
    void draw();
    
protected:
    // Implement ModuleGUI::drawContent() - draws panel-specific content
    void drawContent() override;
    
    // Hide toggle for master video output
    bool shouldShowToggle() const override { return false; }
    
private:
    // Constants for opacity visualization
    static constexpr float DRAG_SENSITIVITY = 0.002f;  // Opacity change per pixel (0-1 range)
    
    // Helper to get current VideoOutput instance from registry
    VideoOutput* getVideoOutput() const;
    
    // GUI section methods
    void drawOutputInfo();
    void drawMasterControls();
    void drawConnections();
    
    // Draggable opacity visualization widget
    struct DraggableOpacityViz {
        bool isDragging = false;
        float dragStartY = 0.0f;
        float dragStartValue = 0.0f;
        size_t sourceIndex = 0;
        
        void startDrag(float startY, float startValue, size_t index);
        void updateDrag(float currentY, float& valueOut);
        void endDrag();
    };
    
    std::vector<DraggableOpacityViz> opacityVizStates_;
    
    // Opacity visualization config
    struct OpacityVizConfig {
        ImVec2 canvasSize;
        ImU32 bgColor;
        ImU32 borderColor;
        ImU32 opacityFillColor;
    };
    
    void drawDraggableOpacityVizInternal(
        const std::string& id,
        float opacity,
        const OpacityVizConfig& config,
        DraggableOpacityViz& vizState,
        std::function<void(float newOpacity)> onOpacityChanged
    );
    
    void drawDraggableOpacityViz(size_t sourceIndex, float opacity);
    
    // Helper functions
    static std::string formatOpacityText(float opacity);
};

