#pragma once

#include <imgui.h>
#include <imcmd_command_palette.h>
#include <string>
#include "ofLog.h"
#include "ViewManager.h"  // For Panel enum

class CommandExecutor;
class ViewManager;
class GUIManager;

/**
 * CommandBar - Palette-based UI for direct actions and navigation
 * 
 * RESPONSIBILITY: UI rendering for command palette interface
 * 
 * Features:
 * - Fuzzy search of commands
 * - Visual command suggestions
 * - Subcommand prompts
 * - Keyboard navigation
 * - Direct module actions (add, remove, route)
 * - Instant navigation to module GUIs
 * 
 * Separation of Concerns:
 * - CommandExecutor: Command logic and execution
 * - Console: Text-based UI rendering (for console-specific commands)
 * - CommandBar: Palette-based UI rendering (for direct actions)
 * 
 * Shortcuts:
 *   Cmd+'='              - Toggle command bar
 */
class CommandBar {
public:
    CommandBar();
    ~CommandBar();
    
    // Setup with dependencies
    void setup(CommandExecutor* executor, ViewManager* viewManager, GUIManager* guiManager);
    
    // Draw command bar window
    void draw();
    
    // Visibility control
    void toggle();
    void open();
    void close();
    bool isOpen() const { return isOpen_; }
    
    // Refresh commands (e.g., when modules change)
    void refreshCommands();
    
private:
    bool isOpen_ = false;
    bool commandsRegistered = false;
    CommandExecutor* commandExecutor = nullptr;
    ViewManager* viewManager = nullptr;
    GUIManager* guiManager = nullptr;
    
    // Navigation state management
    bool previousNavKeyboardState_ = false;
    bool previousNavGamepadState_ = false;
    bool navigationStateSaved_ = false;
    
    // Command registration
    void registerCommands();
    void unregisterCommands();
    

    // Navigation control
    void disableImGuiNavigation();
    void restoreImGuiNavigation();
};

