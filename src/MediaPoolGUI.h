#pragma once
#include <imgui.h>
#include "ofxSoundObjects.h"
#include "CellWidget.h"
#include "Module.h"  // For ParameterDescriptor
#include "gui/ModuleGUI.h"
#include "gui/CellGrid.h"
#include <string>
#include <vector>
#include <map>
#include <utility>  // For std::pair

class MediaPool;  // Forward declaration
class MediaPlayer;  // Forward declaration
class ModuleRegistry;  // Forward declaration

class MediaPoolGUI : public ModuleGUI {
public:
    MediaPoolGUI();
    
    // Legacy method (for backward compatibility during migration)
    void setMediaPool(MediaPool& pool);
    
    // Override draw to call base class (which calls drawContent)
    void draw();
    
    // Navigation state controls (for InputRouter)
    bool getIsParentWidgetFocused() const { return isParentWidgetFocused; }
    void requestFocusMoveToParent() { requestFocusMoveToParentWidget = true; }
    
    // Keyboard input handling (for InputRouter)
    bool handleKeyPress(int key, bool ctrlPressed = false, bool shiftPressed = false) override;
    
    // Static sync method (similar to TrackerSequencerGUI)
    static void syncEditStateFromImGuiFocus(MediaPoolGUI& gui);
    
    // Override ModuleGUI generic interface (Phase 7.3/7.4)
    bool isEditingCell() const override { return isEditingParameter_; }
    bool isKeyboardFocused() const override { return isCellFocused(); }
    void clearCellFocus() override;
    
protected:
    // Implement ModuleGUI::drawContent() - draws panel-specific content
    void drawContent() override;
    
    // Implement ModuleGUI::handleFileDrop() - handles file drops from FileBrowser
    bool handleFileDrop(const std::vector<std::string>& filePaths) override;
    
private:
    // Legacy: keep for backward compatibility (will be removed)
    MediaPool* mediaPool = nullptr;
    
    // Helper to get current MediaPool instance from registry
    MediaPool* getMediaPool() const;
    
    // Waveform visualization
    float waveformHeight;
    static constexpr int MAX_WAVEFORM_POINTS = 4000;  // Maximum number of points for smooth waveform rendering (increased for better default precision)
    static constexpr int MIN_WAVEFORM_POINTS = 200;   // Minimum number of points for waveform rendering (increased for better default precision)
    static constexpr int MAX_TOOLTIP_WAVEFORM_POINTS = 600;  // Maximum number of points for tooltip waveform preview
    static constexpr int MIN_WAVEFORM_POINTS_FOR_DRAW = 2;   // Minimum points required to draw waveform (need at least 2 for a line)
    static constexpr float WAVEFORM_AMPLITUDE_SCALE = 0.4f;  // Amplitude scaling factor (0.4 = 40% of canvas height, using 80% total range)
    static constexpr float ZOOM_PRECISION_MULTIPLIER = 2.0f; // Multiplier for precision when zoomed in (higher = more points when zoomed)
    // Per-index zoom and pan state (index -> {zoom, offset})
    std::map<size_t, std::pair<float, float>> waveformZoomState;  // Stores {zoom, offset} per media index
    
    // Waveform marker dragging state
    enum class WaveformMarker {
        NONE,
        PLAYHEAD,
        POSITION,
        REGION_START,
        REGION_END
    };
    WaveformMarker draggingMarker = WaveformMarker::NONE;
    float waveformDragStartX = 0.0f;
    bool isScrubbing = false;  // Track if user is currently scrubbing (for temporary playback during IDLE)
    
    // Navigation state (parent widget pattern, similar to TrackerSequencerGUI)
    ImGuiID parentWidgetId = 0;
    bool isParentWidgetFocused = false;
    bool requestFocusMoveToParentWidget = false;
    
    // Parameter editing state (similar to TrackerSequencer)
    std::string editingParameter;  // Currently editing parameter name (empty if none)
    int editingColumnIndex;        // Currently editing column index (0 = media index button, 1+ = parameter columns, -1 = none)
    bool isEditingParameter_ = false;  // True when in edit mode (typing numeric value) - standardized naming
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
    
    // Scroll sync state - track previous index to only sync when it changes
    size_t previousMediaIndex = SIZE_MAX;  // Initialize to invalid value
    
    // Helper methods for focus management
    bool isCellFocused() const { return editingColumnIndex >= 0; }
    
    // GUI section methods
    void drawDirectoryControls();
    void drawMediaList();
    void drawWaveform();
    void drawWaveformControls(const ImVec2& canvasPos, const ImVec2& canvasMax, float canvasWidth, float canvasHeight);  // Draw markers and controls on top of waveform
    void drawWaveformPreview(MediaPlayer* player, float width, float height);  // Draw waveform preview in tooltip
    void drawParameters();  // New: Draw parameter editing section as one-row table
    
    // Helper method to create and configure CellWidget for a parameter
    // Renamed from createParameterCellForParameter (ParameterCell abstraction removed)
    CellWidget createCellWidgetForParameter(const ParameterDescriptor& paramDesc);
    
    // Helper method to handle CellWidget keyboard input (reduces duplication)
    // Returns true if key was handled, false otherwise
    bool handleParameterCellKeyPress(const ParameterDescriptor& paramDesc, int key, bool ctrlPressed, bool shiftPressed);
    
    // Helper method to truncate text to fit within available width
    // showEnd: if true, truncates from start (shows end with ellipsis prefix), 
    //          if false, truncates from end (shows start with ellipsis suffix)
    std::string truncateTextToWidth(const std::string& text, float maxWidth, bool showEnd = false, const std::string& ellipsis = "...");
    
    // Helper method to get editable parameters (filters out "note" parameter)
    std::vector<ParameterDescriptor> getEditableParameters() const;
    
    // Helper methods for per-index zoom state
    std::pair<float, float> getWaveformZoomState(size_t index) const;  // Returns {zoom, offset} for given index
    void setWaveformZoomState(size_t index, float zoom, float offset);  // Sets zoom and offset for given index
    
    // Drag-and-drop visual feedback
    void drawDragDropOverlay();
    
    // CellGrid instance for reusable table rendering
    CellGrid cellGrid;
    
    // Cache for CellWidgets used in drawSpecialColumn (for non-button columns)
    // Key: (row, col) pair, Value: CellWidget instance
    std::map<std::pair<int, int>, CellWidget> specialColumnWidgetCache;
};

