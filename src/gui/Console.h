#pragma once

#include <imgui.h>
#include "GUIConstants.h"
#include <string>
#include <vector>
#include <functional>
#include <cstdarg>

class ModuleRegistry;
class GUIManager;
class ConnectionManager;
class CommandExecutor;

/**
 * Console - Text-based UI for command execution
 * 
 * RESPONSIBILITY: UI rendering for text-based command interface
 * 
 * Features:
 * - Command history with arrow key navigation
 * - Auto-scrolling output
 * - Text input and output display
 * 
 * Separation of Concerns:
 * - CommandExecutor: Command logic and execution
 * - Console: Text-based UI rendering
 * - CommandBar: Palette-based UI rendering
 * 
 * Shortcuts:
 *   : (colon)             - Toggle console
 *   Up/Down arrows        - Navigate command history
 *   Ctrl+C / Cmd+C        - Copy selected text to clipboard
 */
class Console {
public:
    Console();
    ~Console() = default;
    
    void setup(ModuleRegistry* registry, GUIManager* guiManager);
    
    /**
     * Set command executor (backend for command execution)
     */
    void setCommandExecutor(CommandExecutor* executor);
    
    // Draw console window (for standalone use)
    void draw();
    
    // Draw console content only (for dockable use - no Begin/End)
    void drawContent();
    
    // Toggle console visibility
    void toggle() { isOpen = !isOpen; }
    void open() { isOpen = true; }
    void close() { isOpen = false; }
    bool isConsoleOpen() const { return isOpen; }
    
    // Handle keyboard input (for : key to open and arrow keys for history)
    bool handleKeyPress(int key);
    
    // Handle arrow keys for history navigation (called from ofApp::keyPressed)
    bool handleArrowKeys(int key);
    
    // Check if InputText is focused (for disabling ImGui navigation)
    bool isInputTextFocused() const { return inputTextWasFocused; }
    
    // Add log entry (public for CommandExecutor output callback)
    void addLog(const std::string& text);
    void addLog(const char* fmt, ...);
    
private:
    bool isOpen = false;
    char inputBuffer[512] = "";
    std::vector<std::string> history;
    std::vector<std::string> items;  // Console output lines
    std::string logTextBuffer;  // Combined log text for multiline input (for text selection)
    bool logTextDirty = true;  // Flag to rebuild logTextBuffer when items change
    int historyPos = -1;
    bool scrollToBottom = false;
    bool inputTextWasFocused = false;  // Track if InputText was focused in last draw()
    bool shouldFocusInput = false;  // Flag to focus input field in next frame (after ViewManager processing)
    
    ModuleRegistry* registry = nullptr;
    GUIManager* guiManager = nullptr;
    CommandExecutor* commandExecutor = nullptr;
    
    // Execute command via CommandExecutor
    void executeCommand(const std::string& command);
};

