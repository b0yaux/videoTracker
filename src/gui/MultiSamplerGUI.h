#pragma once
#include <imgui.h>
#include "ofxSoundObjects.h"
#include "modules/Module.h"  // For ParameterDescriptor
#include "gui/ModuleGUI.h"
#include "gui/CellGrid.h"
#include <string>
#include <vector>
#include <map>
#include <utility>  // For std::pair

class MultiSampler;   // Forward declaration
class MediaPlayer;    // Forward declaration
class ModuleRegistry; // Forward declaration

// MultiSamplerGUI: GUI for the MultiSampler module (AV sample playback instrument)
// Formerly known as MediaPoolGUI
class MultiSamplerGUI : public ModuleGUI {
public:
    MultiSamplerGUI();
    
    // Legacy method (for backward compatibility during migration)
    void setMultiSampler(MultiSampler& sampler);
    
    // Override draw to call base class (which calls drawContent)
    void draw();
    
    // Navigation state controls (for InputRouter)
    bool getIsParentWidgetFocused() const { return isParentWidgetFocused; }
    void requestFocusMoveToParent() { requestFocusMoveToParentWidget = true; }
    
    // Keyboard input handling (for InputRouter)
    bool handleKeyPress(int key, bool ctrlPressed = false, bool shiftPressed = false) override;
    
    // Static sync method (similar to TrackerSequencerGUI)
    static void syncEditStateFromImGuiFocus(MultiSamplerGUI& gui);
    
    // Override ModuleGUI generic interface (Phase 7.3/7.4)
    bool isEditingCell() const override { return cellFocusState.isEditing; }
    bool isKeyboardFocused() const override { return isCellFocused(); }
    void clearCellFocus() override;
    
protected:
    // Implement ModuleGUI::drawContent() - draws panel-specific content
    void drawContent() override;
    
    // Implement ModuleGUI::handleFileDrop() - handles file drops from FileBrowser
    bool handleFileDrop(const std::vector<std::string>& filePaths) override;
    
private:
    // Legacy: keep for backward compatibility (will be removed)
    MultiSampler* multiSampler_ = nullptr;
    
    // Helper to get current MultiSampler instance from registry
    MultiSampler* getMultiSampler() const;
    
    // Waveform visualization
    float waveformHeight;
    static constexpr int MAX_WAVEFORM_POINTS = 64000;  // Maximum number of points for extreme zoom precision (supports up to 10000x zoom)
    static constexpr int MIN_WAVEFORM_POINTS = 200;   // Minimum number of points for waveform rendering
    static constexpr int MAX_TOOLTIP_WAVEFORM_POINTS = 600;  // Maximum number of points for tooltip waveform preview
    static constexpr int MIN_WAVEFORM_POINTS_FOR_DRAW = 2;   // Minimum points required to draw waveform (need at least 2 for a line)
    static constexpr float WAVEFORM_AMPLITUDE_SCALE = 0.4f;  // Amplitude scaling factor (0.4 = 40% of canvas height, using 80% total range)
    static constexpr float ZOOM_PRECISION_MULTIPLIER = 2.0f; // Multiplier for precision when zoomed in (higher = more points when zoomed)
    // Per-index zoom and pan state (index -> {zoom, offset})
    std::map<size_t, std::pair<float, float>> waveformZoomState;  // Stores {zoom, offset} per media index
    
    // Waveform data buffers (instance-specific, not static - fixes performance issue with multiple MultiSampler instances)
    std::vector<float> waveformTimeData;
    std::vector<std::vector<float>> waveformChannelData;
    
    // Audio buffer cache (CRITICAL: getBuffer() takes ~10ms, so we cache it)
    ofSoundBuffer cachedAudioBuffer_;
    std::string cachedAudioFilePath_;
    bool audioBufferCacheValid_ = false;
    
    // Waveform cache for performance optimization (only recalculate when audio/zoom/canvas changes)
    // Uses min/max pairs for industry-standard waveform rendering
    std::vector<float> cachedWaveformTimeData_;
    std::vector<std::vector<float>> cachedWaveformMinData_;  // [channel][point] - minimum values
    std::vector<std::vector<float>> cachedWaveformMaxData_;  // [channel][point] - maximum values
    float cachedVisibleStart_ = -1.0f;
    float cachedVisibleRange_ = -1.0f;
    float cachedCanvasWidth_ = -1.0f;
    int cachedNumFrames_ = -1;
    int cachedNumChannels_ = -1;
    size_t cachedMediaIndex_ = SIZE_MAX;
    bool waveformCacheValid_ = false;
    
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
    
    // Waveform overlay modes (for switching between different editing modes)
    enum class WaveformOverlayMode {
        REGION,      // Edit region start/end, position, scrubbing
        AUTOMATION   // Edit parameter automation curves (volume, speed, etc.)
    };
    WaveformOverlayMode waveformOverlayMode_ = WaveformOverlayMode::REGION;
    
    // Envelope editor state (reusable for ADSR and future modulation envelopes)
    struct EnvelopeEditorState {
        bool isDragging = false;
        int draggedBreakpoint = -1;  // 0=Attack, 1=Decay, 2=Sustain, 3=Release
        ImVec2 dragStartPos;
        float dragStartValue = 0.0f;   // X-axis parameter (time)
        float dragStartValueY = 0.0f;  // Y-axis parameter (level)
    };
    EnvelopeEditorState adsrEditorState_;
    
    // ========================================================================
    // ENVELOPE CURVE DRAWING SYSTEM
    // ========================================================================
    // Modular system for drawing sample-accurate envelope curves overlaid on waveforms.
    // All positions are in normalized sample time (0.0-1.0) and use the same coordinate
    // mapping as the waveform display for pixel-perfect alignment.
    
    /// @brief Single point on an envelope curve with position and level
    struct EnvelopePoint {
        float samplePos;    // Normalized position in sample (0.0-1.0, absolute)
        float level;        // Envelope level (0.0-1.0)
    };
    
    /// @brief Parameters for drawing an ADSR envelope curve
    struct EnvelopeCurveParams {
        // ADSR times in milliseconds
        float attackMs = 0.0f;
        float decayMs = 0.0f;
        float sustain = 1.0f;      // Level (0.0-1.0)
        float releaseMs = 10.0f;
        
        // Sample timing context
        float sampleDurationSeconds = 0.0f;
        float regionStart = 0.0f;   // Where playback starts (0.0-1.0)
        float regionEnd = 1.0f;     // Where playback ends (0.0-1.0)
        float startPosition = 0.0f; // Relative trigger position within region (0.0-1.0)
        float playbackSpeed = 1.0f; // Playback speed multiplier (affects envelope position scaling)
        
        // Visual options
        bool showReleasePreview = true;  // Show release curve at end of region
        float releasePreviewPos = -1.0f; // Custom release start pos (-1 = use sustain end)
    };
    
    /// @brief Calculate envelope points from ADSR parameters
    /// Returns a vector of points representing the envelope curve in sample coordinates
    std::vector<EnvelopePoint> calculateEnvelopePoints(const EnvelopeCurveParams& params) const;
    
    /// @brief Draw envelope curve using pre-calculated points
    /// @param points Pre-calculated envelope points (from calculateEnvelopePoints)
    /// @param canvasPos Screen position of the waveform canvas
    /// @param canvasSize Size of the waveform canvas
    /// @param mapToScreenX Function to map sample position to screen X (same as waveform)
    /// @param curveColor Color for the envelope line
    /// @param fillColor Color for the area under the curve (use alpha for transparency)
    void drawEnvelopeCurve(
        const std::vector<EnvelopePoint>& points,
        const ImVec2& canvasPos,
        const ImVec2& canvasSize,
        std::function<float(float)> mapToScreenX,
        ImU32 curveColor,
        ImU32 fillColor
    );
    
    // Navigation state (parent widget pattern, similar to TrackerSequencerGUI)
    ImGuiID parentWidgetId = 0;
    bool isParentWidgetFocused = false;
    bool requestFocusMoveToParentWidget = false;
    
    // Unified cell focus state (replaces editingColumnIndex, isEditingParameter_, editingParameter)
    CellFocusState cellFocusState;
    
    // Unified callback state tracking
    CellGridCallbacksState callbacksState;
    
    // Focus management
    bool shouldFocusFirstCell = false;      // Flag to request focus on first cell when entering table
    
    // Note: Edit buffer and drag state are now managed by CellWidget internally
    
    // Scroll sync state - track previous index to only sync when it changes
    size_t previousMediaIndex = SIZE_MAX;  // Initialize to invalid value
    
    // GUI state: which sample is currently selected/displayed (moved from MultiSampler)
    // This is purely GUI presentation state, not part of the module's serialized state
    size_t selectedSampleIndex_ = 0;
    
    // Helper methods for focus management (using unified state)
    bool isCellFocused() const { return cellFocusState.hasFocus(); }
    
    // GUI section methods
    void drawGlobalControls();  // Simple button bar for global controls (PLAY, PLAY STYLE, POLYPHONY) - NOT in child window
    void drawMediaList();
    void drawWaveform();
    void drawWaveformControls(const ImVec2& canvasPos, const ImVec2& canvasMax, float canvasWidth, float canvasHeight);  // Draw markers and controls on top of waveform
    void drawWaveformPreview(MediaPlayer* player, float width, float height);  // Draw waveform preview in tooltip
    void drawADSRParameters();  // Draw ADSR envelope parameters in dedicated cellgrid (ONCE/LOOP modes only)
    void drawGranularControls();  // Draw granular synthesis controls in dedicated cellgrid (GRAIN mode only)
    void drawParameters();  // New: Draw parameter editing section as one-row table
    
    // Reusable envelope curve editor (for ADSR and future modulation envelopes)
    // Returns true if any breakpoint was dragged
    // Uses EnvelopeCurveParams for all timing/region parameters
    // mapToScreenX: Function to map absolute sample position (0.0-1.0) to screen X coordinate
    // visibleStart: Current visible start position (0.0-1.0) for zoom/pan
    // visibleRange: Current visible range (0.0-1.0) for zoom/pan
    bool drawEnvelopeCurveEditor(
        const ImVec2& canvasPos,
        const ImVec2& canvasSize,
        const EnvelopeCurveParams& curveParams,
        std::function<float(float)> mapToScreenX,
        float visibleStart,
        float visibleRange,
        std::function<void(const std::string& param, float value)> onParameterChanged,
        EnvelopeEditorState& editorState,
        bool drawOnly = false  // If true, skip input processing and only draw the curve
    );
    
    // ========================================================================
    // AUTOMATION CURVE SYSTEM (Position-Based)
    // ========================================================================
    // Modular system for drawing position-based automation curves overlaid on waveforms.
    // All positions are in normalized sample time (0.0-1.0) and map directly to sample positions.
    // This is different from ADSR which is time-based (milliseconds).
    
    /// @brief Automation parameter type
    enum class AutomationParameter {
        VOLUME,  // Volume automation (0.0-2.0)
        SPEED    // Speed automation (-10.0 to 10.0)
        // Future: GRAIN_SIZE, POSITION, etc.
    };
    
    /// @brief Single point on an automation curve with position and value
    struct AutomationPoint {
        float position;  // Normalized position in sample (0.0-1.0, absolute)
        float value;     // Parameter value at this position
    };
    
    /// @brief Automation curve editor state
    struct AutomationEditorState {
        bool isDragging = false;
        int draggedPoint = -1;  // Index of dragged point
        ImVec2 dragStartPos;
        float dragStartValue = 0.0f;   // X-axis parameter (position)
        float dragStartValueY = 0.0f;  // Y-axis parameter (value)
    };
    AutomationEditorState automationEditorState_;
    AutomationParameter currentAutomationParam_ = AutomationParameter::VOLUME;
    
    /// @brief Draw automation curve using pre-calculated points
    /// @param points Pre-calculated automation points
    /// @param canvasPos Screen position of the waveform canvas
    /// @param canvasSize Size of the waveform canvas
    /// @param mapToScreenX Function to map sample position to screen X (same as waveform)
    /// @param minValue Minimum parameter value (for Y-axis mapping)
    /// @param maxValue Maximum parameter value (for Y-axis mapping)
    /// @param curveColor Color for the automation line
    /// @param fillColor Color for the area under the curve (use alpha for transparency)
    void drawAutomationCurve(
        const std::vector<AutomationPoint>& points,
        const ImVec2& canvasPos,
        const ImVec2& canvasSize,
        std::function<float(float)> mapToScreenX,
        float minValue,
        float maxValue,
        ImU32 curveColor,
        ImU32 fillColor
    );
    
    // Automation curve editor (position-based parameter automation)
    // Returns true if any point was dragged
    // mapToScreenX: Function to map absolute sample position (0.0-1.0) to screen X coordinate
    // visibleStart: Current visible start position (0.0-1.0) for zoom/pan
    // visibleRange: Current visible range (0.0-1.0) for zoom/pan
    bool drawAutomationCurveEditor(
        AutomationParameter param,
        const ImVec2& canvasPos,
        const ImVec2& canvasSize,
        std::function<float(float)> mapToScreenX,
        float visibleStart,
        float visibleRange,
        std::function<void(float position, float value)> onPointChanged,
        AutomationEditorState& editorState,
        bool drawOnly = false  // If true, skip input processing and only draw the curve
    );
    
    // Helper method to create and configure BaseCell for a parameter
    std::unique_ptr<BaseCell> createCellWidgetForParameter(const ParameterDescriptor& paramDesc);
    
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
    
    // Separate CellGrid instances for ADSR and granular controls
    CellGrid adsrCellGrid;
    CellGrid granularCellGrid;
    
    // Cache for BaseCell widgets used in drawSpecialColumn (for non-button columns)
    // Key: (row, col) pair, Value: BaseCell instance
    std::map<std::pair<int, int>, std::unique_ptr<BaseCell>> specialColumnWidgetCache;
    
    // Track last column configuration to avoid clearing cache unnecessarily
    std::vector<CellGridColumnConfig> lastColumnConfig;
    std::vector<CellGridColumnConfig> lastADSRColumnConfig;
    std::vector<CellGridColumnConfig> lastGranularColumnConfig;
};

// Backward compatibility alias (deprecated - use MultiSamplerGUI)
using MediaPoolGUI = MultiSamplerGUI;
