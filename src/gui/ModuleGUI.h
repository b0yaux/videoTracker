#pragma once

#include "ofxImGui.h"
#include <string>
#include <vector>
#include <map>

class ModuleRegistry;  // Forward declaration
class ParameterRouter;  // Forward declaration

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
    void setInstanceName(const std::string& name) { instanceName = name; }
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
    
    // Get module type name (e.g., "TrackerSequencer", "MediaPool")
    // Returns empty string if module not found in registry
    std::string getModuleTypeName() const;
    
    // Setup window properties before Begin() is called
    // This applies default size if saved, and should be called by ViewManager before ImGui::Begin()
    void setupWindow();
    
    // Draw ON/OFF toggle button in ImGui's native title bar
    // Uses foreground draw list to draw on top of title bar decorations
    void drawTitleBarToggle();
    
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
    
protected:
    // Subclasses implement this to draw panel-specific content
    virtual void drawContent() = 0;
    
    // Drag & drop support: override to handle file drops
    // Returns true if files were accepted, false otherwise
    virtual bool handleFileDrop(const std::vector<std::string>& filePaths) { return false; }
    
    // Helper to set up drag drop target (call at start of drawContent())
    // Checks for FILE_BROWSER_FILES payload and calls handleFileDrop() if found
    void setupDragDropTarget();
    
    // State
    std::string instanceName;
    bool enabled_ = true;
    bool visible_ = true;
    ModuleRegistry* registry = nullptr;
    ParameterRouter* parameterRouter = nullptr;
    
private:
    // Static storage for default layouts (module type name -> size)
    static std::map<std::string, ImVec2> defaultLayouts;
    static bool layoutsLoaded;
    static const std::string LAYOUTS_FILENAME;
};

