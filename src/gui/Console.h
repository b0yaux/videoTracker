#pragma once

#include <imgui.h>
#include "GUIConstants.h"
#include <string>
#include <vector>
#include <functional>
#include <cstdarg>

class ModuleRegistry;
class GUIManager;

/**
 * Console - Command-line interface for programmatic module management
 * 
 * Features:
 * - Command history with arrow key navigation
 * - Auto-scrolling output
 * - Filterable module listing
 * - Safe module removal with validation
 * 
 * Commands:
 *   list, ls              - List all modules
 *   remove <name>, rm     - Remove a module by name
 *   add <type>            - Add a module (pool, tracker)
 *   clear, cls            - Clear console output
 *   help, ?               - Show help
 * 
 * Shortcuts:
 *   : (colon)             - Toggle console
 *   Up/Down arrows        - Navigate command history
 *   Ctrl+C / Cmd+C        - Copy selected text to clipboard
 *   Tab                   - Auto-complete (future)
 */
class Console {
public:
    Console();
    ~Console() = default;
    
    void setup(ModuleRegistry* registry, GUIManager* guiManager);
    
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
    
    // Add callback for module operations
    void setOnAddModule(std::function<void(const std::string&)> callback) { onAddModule = callback; }
    void setOnRemoveModule(std::function<void(const std::string&)> callback) { onRemoveModule = callback; }
    
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
    
    std::function<void(const std::string&)> onAddModule;
    std::function<void(const std::string&)> onRemoveModule;
    
    // Command execution
    void executeCommand(const std::string& command);
    void addLog(const std::string& text);
    void addLog(const char* fmt, ...);
    
    // Command handlers
    void cmdList();
    void cmdRemove(const std::string& args);
    void cmdAdd(const std::string& args);
    void cmdHelp();
    void cmdClear();
    
    // Helper to split command and args
    std::pair<std::string, std::string> parseCommand(const std::string& line);
    
    // Helper to trim whitespace
    static std::string trim(const std::string& str);
    
    // Helper to get module type string
    static std::string getModuleTypeString(int type);
};

