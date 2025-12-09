#pragma once

#include "gui/ModuleGUI.h"
#include <imgui.h>
#include <string>
#include <vector>
#include <functional>

class AudioOutput;  // Forward declaration
class ModuleRegistry;  // Forward declaration

/**
 * AudioOutputGUI - GUI panel for AudioOutput module
 * 
 * Displays:
 * - Audio device selection
 * - Audio level visualization
 * - Device information
 */
class AudioOutputGUI : public ModuleGUI {
public:
    AudioOutputGUI();
    virtual ~AudioOutputGUI() = default;
    
    // Override draw to call base class (which calls drawContent)
    void draw();
    
protected:
    // Implement ModuleGUI::drawContent() - draws panel-specific content
    void drawContent() override;
    
    // Hide toggle for master audio output
    bool shouldShowToggle() const override { return false; }
    
private:
    // Constants for audio visualization
    static constexpr float MIN_LINEAR_VOLUME = 0.001f;  // -60dB
    static constexpr float MIN_DB = -60.0f;
    static constexpr float MAX_DB = 0.0f;
    static constexpr float AUDIO_LEVEL_WARNING = 0.6f;  // Yellow threshold
    static constexpr float AUDIO_LEVEL_CLIPPING = 0.8f; // Red threshold
    static constexpr float DRAG_SENSITIVITY = 0.1f;     // dB per pixel
    
    // Helper to get current AudioOutput instance from registry
    AudioOutput* getAudioOutput() const;
    
    // GUI section methods
    void drawDeviceSelection();
    void drawMasterVolume();
    void drawConnections();
    
    // Draggable audio visualization widget
    struct DraggableAudioViz {
        bool isDragging = false;
        float dragStartY = 0.0f;
        float dragStartValue = 0.0f;
        size_t connectionIndex = 0;
        
        void startDrag(float startY, float startValue, size_t index);
        void updateDrag(float currentY, float& valueOut);
        void endDrag();
    };
    
    std::vector<DraggableAudioViz> audioVizStates_;
    DraggableAudioViz masterVolumeVizState_;
    
    // Helper functions for audio visualization
    static float linearToDb(float linear);
    static float dbToLinear(float db);
    static std::string formatDbText(float volume, float volumeDb);
    static std::string formatAudioLevelText(float audioLevel);
    static ImU32 getAudioLevelColor(float audioLevel);
    
    // Unified draggable visualization (used by both master and connections)
    struct AudioVizConfig {
        ImVec2 canvasSize;
        ImU32 bgColor;
        ImU32 borderColor;
        ImU32 volumeFillColor;
        bool showAudioLevelText;
        ImU32 audioLevelTextColor;
    };
    
    void drawDraggableAudioVizInternal(
        const std::string& id,
        float volume,
        float audioLevel,
        const AudioVizConfig& config,
        DraggableAudioViz& vizState,
        std::function<void(float newVolume)> onVolumeChanged
    );
    
    void drawDraggableAudioViz(size_t connectionIndex, 
                                float volume, 
                                float audioLevel,
                                float minDb, 
                                float maxDb);
    
    void drawDraggableMasterVolume(float volume, float audioLevel);
};

