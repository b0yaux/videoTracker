#pragma once
#include "ofxImGui.h"
#include "ofxSoundObjects.h"
#include <string>
#include <vector>

class MediaPool;  // Forward declaration

class MediaPoolGUI {
public:
    MediaPoolGUI();
    void setMediaPool(MediaPool& pool);
    void draw();
    
    // Navigation state controls (for InputRouter)
    bool getIsParentWidgetFocused() const { return isParentWidgetFocused; }
    void requestFocusMoveToParent() { requestFocusMoveToParentWidget = true; }
    
private:
    MediaPool* mediaPool;
    
    // Search functionality
    char searchBuffer[256];
    std::string searchFilter;
    
    // Waveform visualization
    float waveformHeight;
    
    // Navigation state (parent widget pattern, similar to TrackerSequencerGUI)
    ImGuiID parentWidgetId = 0;
    bool isParentWidgetFocused = false;
    bool requestFocusMoveToParentWidget = false;
    
    // GUI section methods
    void drawDirectoryControls();
    void drawSearchBar();
    void drawMediaList();
    void drawWaveform();
};
