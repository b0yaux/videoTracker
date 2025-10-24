#pragma once
#include "ofxImGui.h"
#include "ofxSoundObjects.h"
#include "implot.h"
#include <string>
#include <vector>

class MediaPool;  // Forward declaration

class MediaPoolGUI {
public:
    MediaPoolGUI();
    void setMediaPool(MediaPool& pool);
    void draw();
    
    // GUI state controls
    void setShowAdvancedOptions(bool show);
    void setShowFileDetails(bool show);
    
private:
    MediaPool* mediaPool;
    bool showAdvancedOptions;
    bool showFileDetails;
    
    // Search functionality
    char searchBuffer[256];
    std::string searchFilter;
    
    // Waveform visualization
    float waveformHeight;
    
    // GUI section methods
    void drawDirectoryControls();
    void drawSearchBar();
    void drawMediaList();
    void drawPlayerStatus();
    void drawWaveform();
};
