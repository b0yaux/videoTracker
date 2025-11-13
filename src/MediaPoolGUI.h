#pragma once
#include "ofxImGui.h"
#include "ofxSoundObjects.h"
#include "ParameterCell.h"
#include "Module.h"  // For ParameterDescriptor
#include <string>
#include <vector>
#include <map>

class MediaPool;  // Forward declaration

class MediaPoolGUI {
public:
    MediaPoolGUI();
    void setMediaPool(MediaPool& pool);
    void draw();
    
    // Navigation state controls (for InputRouter)
    bool getIsParentWidgetFocused() const { return isParentWidgetFocused; }
    void requestFocusMoveToParent() { requestFocusMoveToParentWidget = true; }
    
    // Keyboard input handling (for InputRouter)
    bool handleKeyPress(int key, bool ctrlPressed = false, bool shiftPressed = false);
    bool isKeyboardFocused() const { return editingColumnIndex >= 0; }
    
    // Static sync method (similar to TrackerSequencerGUI)
    static void syncEditStateFromImGuiFocus(MediaPoolGUI& gui);
    
    // Clear cell focus (for focus restoration when window regains focus)
    void clearCellFocus();
    
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
    
    // Parameter editing state (similar to TrackerSequencer)
    std::string editingParameter;  // Currently editing parameter name (empty if none)
    int editingColumnIndex;        // Currently editing column index (0 = media index button, 1+ = parameter columns, -1 = none)
    bool isEditingParameter = false;
    std::string editBufferCache;
    bool editBufferInitializedCache = false;
    
    // Focus management (similar to TrackerSequencer)
    bool shouldFocusFirstCell = false;      // Flag to request focus on first cell when entering table
    bool shouldRefocusCurrentCell = false;  // Flag to request focus on current cell after exiting edit mode
    bool anyCellFocusedThisFrame = false;   // Track if any cell was focused this frame
    
    // Drag state for parameter editing
    std::string draggingParameter;  // Parameter being dragged (empty if none)
    float dragStartY = 0.0f;
    float dragStartX = 0.0f;
    float lastDragValue = 0.0f;
    
    // Helper methods for focus management
    bool isCellFocused() const { return editingColumnIndex >= 0; }
    
    // GUI section methods
    void drawDirectoryControls();
    void drawSearchBar();
    void drawMediaList();
    void drawWaveform();
    void drawParameters();  // New: Draw parameter editing section as one-row table
    void drawMediaIndexButton(int columnIndex, size_t numParamColumns);  // Draw media index button (play/pause trigger)
    
    // Helper method to create and configure ParameterCell for a parameter (similar to TrackerSequencer)
    ParameterCell createParameterCellForParameter(const ParameterDescriptor& paramDesc);
    
    // Helper method to handle ParameterCell keyboard input (reduces duplication)
    // Returns true if key was handled, false otherwise
    bool handleParameterCellKeyPress(const ParameterDescriptor& paramDesc, int key, bool ctrlPressed, bool shiftPressed);
};

