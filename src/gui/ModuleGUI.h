#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <map>
#include <memory>

class ModuleRegistry;  // Forward declaration
class ParameterRouter;  // Forward declaration
class Module;  // Forward declaration
struct ParameterDescriptor;  // Forward declaration (it's a struct, not a class)
class ParameterCell;  // Forward declaration (internal implementation detail)
class BaseCell;  // Forward declaration
class CellGrid;  // Forward declaration
struct CellGridColumnConfig;  // Forward declaration
struct CellGridCallbacks;  // Forward declaration
namespace vt { class Engine; }  // Forward declaration
#include <functional>  // For std::function

/**
 * ModuleGUI - Base class for all module GUI panels
 * 
 * Provides common behaviors:
 * - ON/OFF toggle (enable/disable module)
 * - Visibility toggle
 * - Standardized panel frame
 * - Registry connection
 * - Per-module-type default layout save/restore
 * 
 * Inspired by BespokeSynth's modular panel system
 */
class ModuleGUI {
public:
    ModuleGUI();
    virtual ~ModuleGUI() = default;
    
    // Instance management
    void setInstanceName(const std::string& name) { 
        instanceName = name; 
        syncEnabledState();  // Sync with backend module when name is set
    }
    std::string getInstanceName() const { return instanceName; }
    
    // Enable/disable state
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
    // Visibility state
    void setVisible(bool visible) { visible_ = visible; }
    bool isVisible() const { return visible_; }
    
    // Registry connection
    void setRegistry(ModuleRegistry* registry) { this->registry = registry; }
    ModuleRegistry* getRegistry() const { return registry; }
    
    // ParameterRouter connection
    void setParameterRouter(ParameterRouter* router) { this->parameterRouter = router; }
    ParameterRouter* getParameterRouter() const { return parameterRouter; }
    
    // ConnectionManager connection
    void setConnectionManager(class ConnectionManager* manager) { this->connectionManager = manager; }
    class ConnectionManager* getConnectionManager() const { return connectionManager; }
    
    // GUIManager connection (for rename operations)
    void setGUIManager(class GUIManager* manager) { this->guiManager = manager; }
    class GUIManager* getGUIManager() const { return guiManager; }
    
    // Set Engine reference (for command queue routing)
    void setEngine(vt::Engine* engine) { engine_ = engine; }
    
    // Get module type name (e.g., "TrackerSequencer", "MultiSampler")
    // Returns empty string if module not found in registry
    std::string getModuleTypeName() const;
    
    // Setup window properties before Begin() is called
    // This applies default size if saved, and should be called by ViewManager before ImGui::Begin()
    void setupWindow();
    
    // Draw ON/OFF toggle button in ImGui's native title bar
    // Uses foreground draw list to draw on top of title bar decorations
    void drawTitleBarToggle();
    
    // Draw module menu icon button in title bar (opens popup menu)
    // Positioned to the left of the ON/OFF toggle
    void drawTitleBarMenuIcon();
    
    // Draw module popup menu (rename, connections)
    // Called when menu icon is clicked
    void drawModulePopup();
    
    // Override this to hide toggle for specific module types (e.g., master outputs)
    // Returns true if toggle should be shown, false to hide it
    virtual bool shouldShowToggle() const { return true; }
    
    // Main draw function - draws panel content (window is created by ViewManager)
    // Subclasses should call this from their draw() method, or override drawContent()
    void draw();
    
    // Save current window size as default for this module type
    // Should be called when window is resized or when user wants to save layout
    void saveDefaultLayout();
    
    // Get default window size for this module type
    // Returns ImVec2(0, 0) if no default is saved
    ImVec2 getDefaultSize() const;
    
    // Static methods for managing default layouts
    // Save default size for a module type
    static void saveDefaultLayoutForType(const std::string& moduleTypeName, const ImVec2& size);
    
    // Get default size for a module type
    static ImVec2 getDefaultSizeForType(const std::string& moduleTypeName);
    
    // Load all default layouts from file
    static void loadDefaultLayouts();
    
    // Save all default layouts to file
    static void saveDefaultLayouts();
    
    // Get all default layouts (for serialization)
    static std::map<std::string, ImVec2> getAllDefaultLayouts();
    
    // Set all default layouts (for deserialization)
    static void setAllDefaultLayouts(const std::map<std::string, ImVec2>& layouts);
    
    // Generic focus and editing state interface (for Phase 7.3/7.4)
    // Modules that support cell editing should override these methods
    virtual bool isEditingCell() const { return false; }
    virtual bool isKeyboardFocused() const { return false; }
    virtual void clearCellFocus() {}
    
    // Generic input handling interface (for InputRouter refactoring)
    // Modules that handle keyboard input should override this method
    // @param key The key code
    // @param ctrlPressed Whether Ctrl is pressed
    // @param shiftPressed Whether Shift is pressed
    // @return true if the key was handled, false otherwise
    virtual bool handleKeyPress(int key, bool ctrlPressed = false, bool shiftPressed = false) { return false; }
    
    // Check if this GUI can handle a specific key
    // Used for routing decisions - returns true if GUI is focused and can handle the key
    virtual bool canHandleKeyPress(int key) const { return isKeyboardFocused(); }
    
    // Per-instance window state helpers (for session restoration validation)
    // These query ImGui's window state for this instance's window
    // Returns true if window exists and has state, false otherwise
    bool hasWindowState() const;
    
    // Get current window position (returns ImVec2(0,0) if window doesn't exist)
    ImVec2 getWindowPosition() const;
    
    // Get current window size (returns ImVec2(0,0) if window doesn't exist)
    ImVec2 getWindowSize() const;
    
    // Check if window is collapsed (returns false if window doesn't exist)
    bool isWindowCollapsed() const;
    
protected:
    // Subclasses implement this to draw panel-specific content
    virtual void drawContent() = 0;
    
    /**
     * Set parameter via command queue (thread-safe)
     * All GUI parameter changes should use this method instead of direct module->setParameter()
     * @param paramName Parameter name
     * @param value New parameter value
     * @return true if command was enqueued successfully
     */
    bool setParameterViaCommand(const std::string& paramName, float value);
    
    // Drag & drop support: override to handle file drops
    // Returns true if files were accepted, false otherwise
    virtual bool handleFileDrop(const std::vector<std::string>& filePaths) { return false; }
    
    // Helper to set up drag drop target (call at start of drawContent())
    // Checks for FILE_PATHS payload and calls handleFileDrop() if found
    void setupDragDropTarget();
    
    // Parameter editing helpers
    
    /**
     * Get the module instance for this GUI
     * Uses registry and instanceName to retrieve the module
     * @return Shared pointer to Module, or nullptr if not found
     */
    std::shared_ptr<Module> getModule() const;
    
    // ============================================================================
    // Unified CellGrid State Management
    // ============================================================================
    // These structures and helpers provide a unified approach to managing
    // cell focus, editing state, and callbacks across MediaPoolGUI and TrackerSequencerGUI
    
    /**
     * Unified cell focus state structure
     * Replaces separate focus tracking in MediaPoolGUI (editingColumnIndex, isEditingParameter_)
     * and TrackerSequencerGUI (FocusState struct)
     */
    struct CellFocusState {
        int row = -1;              // Focused row (-1 = none, 0 = single row for MultiSampler)
        int column = -1;           // Focused column (-1 = none)
        bool isEditing = false;    // True when in edit mode (typing numeric value)
        std::string editingParameter;  // Currently editing parameter name (empty if none)
        
        void clear() {
            row = -1;
            column = -1;
            isEditing = false;
            editingParameter.clear();
        }
        
        bool hasFocus() const {
            return row >= 0 && column >= 0;
        }
        
        bool matches(int r, int c) const {
            return row == r && column == c;
        }
    };
    
    /**
     * Unified callback state tracking
     * Tracks frame-level state for CellGrid callbacks
     */
    struct CellGridCallbacksState {
        bool headerClickedThisFrame = false;
        bool anyCellFocusedThisFrame = false;
        int lastClearedFrame = -1;  // Frame number when focus was last cleared (for guard against same-frame callbacks)
        
        void resetFrame() {
            headerClickedThisFrame = false;
            anyCellFocusedThisFrame = false;
            // Note: lastClearedFrame is NOT reset - it persists across frames for comparison
            // We compare against current frame number, so old values are automatically invalid
        }
    };
    
    // Helper methods for CellFocusState management
    void setCellFocus(CellFocusState& state, int row, int column, const std::string& paramName = "");
    void clearCellFocus(CellFocusState& state);
    bool isCellFocused(const CellFocusState& state, int row, int column) const;
    int getFocusedRow(const CellFocusState& state) const;
    
    // Navigation restoration helper
    static void restoreImGuiKeyboardNavigation();
    
    // ============================================================================
    // Unified CellGrid Configuration
    // ============================================================================
    // These structures and helpers provide a unified approach to configuring
    // CellGrid instances across MediaPoolGUI and TrackerSequencerGUI
    
    /**
     * Unified CellGrid configuration structure
     * Encapsulates all common CellGrid configuration options
     */
    struct CellGridConfig {
        std::string tableId;
        ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                                     ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable;
        ImVec2 cellPadding = ImVec2(2, 2);
        ImVec2 itemSpacing = ImVec2(1, 1);
        bool enableReordering = true;
        bool enableScrolling = false;
        float scrollHeight = 0.0f;  // 0.0f = auto-calculate
        float scrollbarSize = 8.0f;
        
        // Default constructor with sensible defaults
        CellGridConfig() = default;
        
        // Constructor for common cases
        CellGridConfig(const std::string& id, ImGuiTableFlags flags, bool scrolling = false, float height = 0.0f)
            : tableId(id), tableFlags(flags), enableScrolling(scrolling), scrollHeight(height) {}
    };
    
    // Helper methods for CellGrid configuration
    void configureCellGrid(CellGrid& grid, const CellGridConfig& config);
    void updateColumnConfigIfChanged(CellGrid& grid, 
                                      const std::vector<CellGridColumnConfig>& newConfig,
                                      std::vector<CellGridColumnConfig>& lastConfig);
    
    // ============================================================================
    // Unified CellGrid Callback Setup
    // ============================================================================
    // These helpers provide a unified approach to setting up standard CellGrid callbacks
    // across MediaPoolGUI and TrackerSequencerGUI
    
    /**
     * Setup standard CellGrid callbacks that are common across all module GUIs
     * This includes focus tracking, edit mode handling, and state synchronization.
     * 
     * @param callbacks The CellGridCallbacks struct to populate
     * @param cellFocusState Reference to the module's CellFocusState
     * @param callbacksState Reference to the module's CellGridCallbacksState
     * @param cellGrid Reference to the CellGrid instance (for column config access)
     * @param isSingleRow If true, treats the grid as single-row (row always 0, like MediaPoolGUI)
     */
    void setupStandardCellGridCallbacks(CellGridCallbacks& callbacks,
                                         CellFocusState& cellFocusState,
                                         CellGridCallbacksState& callbacksState,
                                         CellGrid& cellGrid,
                                         bool isSingleRow = false);
    
    // ============================================================================
    // Unified Input Handling Helpers
    // ============================================================================
    
    /**
     * Check if a key is a typing key (numeric digits, decimal point, operators)
     * These keys should trigger auto-enter edit mode and be delegated to BaseCell
     */
    static bool isTypingKey(int key);
    
    /**
     * Check if a key should be delegated to CellWidget for processing during draw()
     * Returns true if the key should be handled by CellWidget, false otherwise
     */
    static bool shouldDelegateToCellWidget(int key, bool isEditing);
    
    /**
     * Handle cell input key - centralizes logic for when to delegate to CellWidget vs handle directly
     * Returns true if key was handled (caller should process it), false to let it pass through to CellWidget
     */
    static bool handleCellInputKey(int key, bool isEditing, bool ctrlPressed, bool shiftPressed);
    
    // ============================================================================
    // Unified Focus Clearing Helpers
    // ============================================================================
    
    /**
     * Check if cell focus should be cleared based on common conditions
     * Common pattern: clear if header clicked OR (cell focused but no cell focused this frame AND not editing)
     * @param cellFocusState Current focus state
     * @param callbacksState Current callback state (headerClickedThisFrame, anyCellFocusedThisFrame)
     * @param additionalCondition Optional additional condition that must be true to clear (e.g., !dragging)
     * @return true if focus should be cleared
     */
    static bool shouldClearCellFocus(const CellFocusState& cellFocusState,
                                     const CellGridCallbacksState& callbacksState,
                                     std::function<bool()> additionalCondition = nullptr);
    
    /**
     * Handle focus clearing - checks conditions and clears focus if needed
     * This is a convenience wrapper around shouldClearCellFocus() + clearCellFocus()
     * @param cellFocusState Focus state to clear (modified if conditions are met)
     * @param callbacksState Current callback state (modified to set focusClearingThisFrame flag)
     * @param additionalCondition Optional additional condition
     * @return true if focus was cleared
     */
    static bool handleFocusClearing(CellFocusState& cellFocusState,
                                    CellGridCallbacksState& callbacksState,
                                    std::function<bool()> additionalCondition = nullptr);
    
    /**
     * Create a BaseCell for a module parameter
     * Helper method that centralizes the common pattern:
     * - Gets module from registry using instanceName
     * - Gets ParameterRouter
     * - Creates BaseCell (NumCell, BoolCell, or MenuCell) with routing awareness
     * 
     * Subclasses can override for special cases (e.g., MultiSampler activePlayer)
     * or use this directly for standard Module parameter editing.
     * 
     * @param paramDesc Parameter descriptor
     * @param customGetter Optional custom getter function (overrides default Module::getParameter)
     * @param customSetter Optional custom setter function (overrides default Module::setParameter)
     * @param customRemover Optional custom remover function (overrides default reset to defaultValue)
     * @param customFormatter Optional custom formatter function (overrides default formatting)
     * @param customParser Optional custom parser function (overrides default parsing)
     * @return BaseCell instance configured for the parameter (unique_ptr for polymorphism)
     */
    std::unique_ptr<BaseCell> createCellWidget(
        const ParameterDescriptor& paramDesc,
        std::function<float()> customGetter = nullptr,
        std::function<void(float)> customSetter = nullptr,
        std::function<void()> customRemover = nullptr,
        std::function<std::string(float)> customFormatter = nullptr,
        std::function<float(const std::string&)> customParser = nullptr
    );
    
    // State
    std::string instanceName;
    bool enabled_ = true;
    bool visible_ = true;
    ModuleRegistry* registry = nullptr;
    ParameterRouter* parameterRouter = nullptr;
    class ConnectionManager* connectionManager = nullptr;
    class GUIManager* guiManager = nullptr;
    vt::Engine* engine_ = nullptr;  // For command queue routing
    
    // Module popup menu state
    char renameBuffer_[256] = {0};  // Buffer for rename input field
    
private:
    // Sync enabled state from backend module (implemented in .cpp)
    void syncEnabledState();
    
    // Static storage for default layouts (module type name -> size)
    static std::map<std::string, ImVec2> defaultLayouts;
    static bool layoutsLoaded;
    static const std::string LAYOUTS_FILENAME;
};

