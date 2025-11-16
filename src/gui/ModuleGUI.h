#pragma once

#include "ofxImGui.h"
#include <string>

class ModuleRegistry;  // Forward declaration

/**
 * ModuleGUI - Base class for all module GUI panels
 * 
 * Provides common behaviors:
 * - ON/OFF toggle (enable/disable module)
 * - Visibility toggle
 * - Standardized panel frame
 * - Registry connection
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
    
    // Draw custom title bar (called by ViewManager after Begin() but before content)
    // This integrates the ON/OFF toggle into the actual window title bar area
    void drawCustomTitleBar();
    
    // Main draw function - draws panel content (window is created by ViewManager)
    // Subclasses should call this from their draw() method, or override drawContent()
    void draw();
    
protected:
    // Subclasses implement this to draw panel-specific content
    virtual void drawContent() = 0;
    
    // State
    std::string instanceName;
    bool enabled_ = true;
    bool visible_ = true;
    ModuleRegistry* registry = nullptr;
};

