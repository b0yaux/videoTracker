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
    
    // GUI state controls
    
private:
    MediaPool* mediaPool;
    
    // Search functionality
    char searchBuffer[256];
    std::string searchFilter;
    
    // Waveform visualization
    float waveformHeight;
    
    // GUI section methods
    void drawDirectoryControls();
    void drawSearchBar();
    void drawMediaList();
    void drawWaveform();
};
