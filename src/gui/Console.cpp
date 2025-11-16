#include "Console.h"
#include "core/ModuleRegistry.h"
#include "gui/GUIManager.h"
#include "Module.h"
#include "ofLog.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>

Console::Console() {
    addLog("Console ready. Type 'help' for commands.");
}

void Console::setup(ModuleRegistry* registry_, GUIManager* guiManager_) {
    if (!registry_) {
        ofLogError("Console") << "Registry is null in setup";
        return;
    }
    if (!guiManager_) {
        ofLogError("Console") << "GUIManager is null in setup";
        return;
    }
    
    registry = registry_;
    guiManager = guiManager_;
    ofLogNotice("Console") << "Console setup complete";
}

bool Console::handleKeyPress(int key) {
    // Colon (:) handling moved to InputRouter as Cmd+':'
    // This method now only handles other console-specific keys if needed
    return false;
}

bool Console::handleArrowKeys(int key) {
    // Only handle arrow keys if console is open, has history, and InputText was focused
    if (!isOpen || history.empty() || !inputTextWasFocused) {
        return false;
    }
    
    if (key == OF_KEY_UP) {
        if (historyPos == -1) {
            // Start from most recent
            historyPos = static_cast<int>(history.size()) - 1;
        } else if (historyPos > 0) {
            // Go to previous entry
            historyPos--;
        }
        
        // Copy history entry to input buffer (with safe bounds check)
        if (historyPos >= 0 && historyPos < static_cast<int>(history.size())) {
            const std::string& histEntry = history[historyPos];
            if (!histEntry.empty()) {
                size_t copyLen = std::min(histEntry.length(), static_cast<size_t>(sizeof(inputBuffer) - 1));
                std::memcpy(inputBuffer, histEntry.c_str(), copyLen);
                inputBuffer[copyLen] = '\0';
                return true; // Consume the key
            }
        }
    } else if (key == OF_KEY_DOWN) {
        if (historyPos >= 0) {
            historyPos++;
            if (historyPos >= static_cast<int>(history.size())) {
                // Past end: clear input
                historyPos = -1;
                inputBuffer[0] = '\0';
            } else {
                // Copy next history entry
                const std::string& histEntry = history[historyPos];
                if (!histEntry.empty()) {
                    size_t copyLen = std::min(histEntry.length(), static_cast<size_t>(sizeof(inputBuffer) - 1));
                    std::memcpy(inputBuffer, histEntry.c_str(), copyLen);
                    inputBuffer[copyLen] = '\0';
                }
            }
            return true; // Consume the key
        }
    }
    
    return false;
}

void Console::draw() {
    // Always draw the window (even when collapsed) so ImGui can save its state
    // Set window size and position (follow MenuBar pattern)
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_FirstUseEver,
        ImVec2(0.5f, 0.5f)
    );
    
    // Collapse window when hidden to preserve layout state
    // Always sync collapse state with visibility so menu toggle works
    ImGui::SetNextWindowCollapsed(!isOpen, ImGuiCond_Always);
    
    // Disable scrolling on main window - only child region should scroll
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    
    // Always call Begin() so ImGui can track window state even when collapsed
    if (ImGui::Begin("Console", &isOpen, flags)) {
        // Sync visibility state with ImGui's window collapsed state
        // If user manually expands a collapsed window, update isOpen to true
        bool isCollapsed = ImGui::IsWindowCollapsed();
        if (!isCollapsed && !isOpen) {
            // User manually expanded the window - sync our state
            isOpen = true;
        }
        drawContent();
    }
    ImGui::End();
}

void Console::drawContent() {
    // Draw focus outline if this window is focused (like other panels)
    if (ImGui::IsWindowFocused()) {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (drawList) {
            ImVec2 windowPos = ImGui::GetWindowPos();
            ImVec2 windowSize = ImGui::GetWindowSize();
            ImVec2 min = windowPos;
            ImVec2 max = ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y);
            drawList->AddRect(min, max, GUIConstants::toU32(GUIConstants::Outline::Focus), 
                             0.0f, 0, GUIConstants::Outline::FocusThickness);
        }
    }
    // Output log with scrolling
    ImGui::BeginChild("ScrollingRegion", 
                     ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), 
                     false, 
                     ImGuiWindowFlags_HorizontalScrollbar);
    
    // Draw all log items
    for (const auto& item : items) {
        // Color code output based on prefix
        if (item.find("Error:") == 0 || item.find("error:") == 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, GUIConstants::Text::Warning);
            ImGui::TextUnformatted(item.c_str());
            ImGui::PopStyleColor();
        } else if (item.find(">") == 0) {
            // Command input (slightly dimmed)
            ImGui::PushStyleColor(ImGuiCol_Text, GUIConstants::Text::Info);
            ImGui::TextUnformatted(item.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextUnformatted(item.c_str());
        }
    }
    
    // Auto-scroll to bottom (always enabled)
    if (scrollToBottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
        ImGui::SetScrollHereY(1.0f);
        scrollToBottom = false;
    }
    
    ImGui::EndChild();
    
    ImGui::Separator();
    
    // Command input
    bool reclaimFocus = false;
    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue | 
                                     ImGuiInputTextFlags_EscapeClearsAll;
    
    // Auto-focus input on window appearing
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    
    // Draw InputText widget first
    bool inputTextReturned = ImGui::InputText("##input", inputBuffer, sizeof(inputBuffer), inputFlags);
    
    // Check if InputText is actually focused/active (must check AFTER InputText call)
    bool isInputTextActive = ImGui::IsItemActive() || ImGui::IsItemFocused();
    inputTextWasFocused = isInputTextActive;  // Store for arrow key handling in keyPressed()
    
    // Note: Arrow key handling is now done in handleArrowKeys() called from ofApp::keyPressed()
    // This ensures arrow keys are consumed before InputRouter can process them
    
    // Handle command execution
    if (inputTextReturned) {
        std::string command = trim(inputBuffer);
        
        if (!command.empty()) {
            executeCommand(command);
            inputBuffer[0] = '\0';
        }
        reclaimFocus = true;
        historyPos = -1;  // Reset history position after executing
    }
    
    // Reclaim focus after command execution or history navigation
    if (reclaimFocus) {
        ImGui::SetKeyboardFocusHere(-1);
    }
    
    // Show hint
    ImGui::SameLine();
    ImGui::TextDisabled("(Press : to close)");
}

void Console::executeCommand(const std::string& command) {
    addLog("> %s", command.c_str());
    
    // Add to history (avoid duplicates)
    historyPos = -1;
    auto it = std::find(history.begin(), history.end(), command);
    if (it != history.end()) {
        history.erase(it);
    }
    history.push_back(command);
    
    // Limit history size
    if (history.size() > 50) {
        history.erase(history.begin());
    }
    
    // Parse command
    auto [cmd, args] = parseCommand(command);
    
    // Convert to lowercase for comparison
    std::string cmdLower = cmd;
    std::transform(cmdLower.begin(), cmdLower.end(), cmdLower.begin(), ::tolower);
    
    // Execute
    if (cmdLower == "list" || cmdLower == "ls") {
        cmdList();
    } else if (cmdLower == "remove" || cmdLower == "rm" || cmdLower == "delete" || cmdLower == "del") {
        cmdRemove(args);
    } else if (cmdLower == "add") {
        cmdAdd(args);
    } else if (cmdLower == "help" || cmdLower == "?") {
        cmdHelp();
    } else if (cmdLower == "clear" || cmdLower == "cls") {
        cmdClear();
    } else {
        addLog("Error: Unknown command '%s'. Type 'help' for commands.", cmd.c_str());
    }
    
    scrollToBottom = true;
}

std::pair<std::string, std::string> Console::parseCommand(const std::string& line) {
    std::istringstream iss(line);
    std::string cmd, args;
    iss >> cmd;
    std::getline(iss, args);
    args = trim(args);
    return {cmd, args};
}

std::string Console::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

std::string Console::getModuleTypeString(int type) {
    switch (static_cast<ModuleType>(type)) {
        case ModuleType::SEQUENCER: return "SEQUENCER";
        case ModuleType::INSTRUMENT: return "INSTRUMENT";
        case ModuleType::EFFECT: return "EFFECT";
        case ModuleType::UTILITY: return "UTILITY";
        default: return "UNKNOWN";
    }
}

void Console::cmdList() {
    if (!registry) {
        addLog("Error: Registry not set");
        return;
    }
    
    addLog("=== Modules ===");
    auto allNames = registry->getAllHumanNames();
    
    if (allNames.empty()) {
        addLog("No modules registered");
        return;
    }
    
    for (const auto& name : allNames) {
        auto module = registry->getModule(name);
        if (module) {
            std::string typeStr = getModuleTypeString(static_cast<int>(module->getType()));
            
            // Check if module has GUI
            bool hasGUI = false;
            if (guiManager) {
                auto mediaPoolGUI = guiManager->getMediaPoolGUI(name);
                auto trackerGUI = guiManager->getTrackerGUI(name);
                hasGUI = (mediaPoolGUI != nullptr || trackerGUI != nullptr);
            }
            
            std::string guiStatus = hasGUI ? "[GUI]" : "[NO GUI]";
            addLog("  %s [%s] %s", name.c_str(), typeStr.c_str(), guiStatus.c_str());
        } else {
            addLog("  %s [ERROR: Module not found]", name.c_str());
        }
    }
    addLog("Total: %zu modules", allNames.size());
}

void Console::cmdRemove(const std::string& args) {
    if (args.empty()) {
        addLog("Usage: remove <module_name>");
        addLog("Example: remove pool2");
        return;
    }
    
    if (!registry) {
        addLog("Error: Registry not set");
        return;
    }
    
    if (!registry->hasModule(args)) {
        addLog("Error: Module '%s' not found", args.c_str());
        return;
    }
    
    // Prevent removing default instances
    if (args == "pool1" || args == "tracker1") {
        addLog("Error: Cannot remove default instances (pool1, tracker1)");
        return;
    }
    
    if (!onRemoveModule) {
        addLog("Error: Remove callback not set");
        return;
    }
    
    // Confirm removal
    onRemoveModule(args);
    addLog("Removed module: %s", args.c_str());
    ofLogNotice("Console") << "Removed module via console: " << args;
}

void Console::cmdAdd(const std::string& args) {
    if (args.empty()) {
        addLog("Usage: add <module_type>");
        addLog("Types: pool, tracker, MediaPool, TrackerSequencer");
        return;
    }
    
    if (!onAddModule) {
        addLog("Error: Add callback not set");
        return;
    }
    
    // Validate module type
    std::string typeLower = args;
    std::transform(typeLower.begin(), typeLower.end(), typeLower.begin(), ::tolower);
    
    std::string moduleType;
    if (typeLower == "pool" || typeLower == "mediapool") {
        moduleType = "MediaPool";
    } else if (typeLower == "tracker" || typeLower == "trackersequencer") {
        moduleType = "TrackerSequencer";
    } else {
        addLog("Error: Unknown module type '%s'", args.c_str());
        addLog("Valid types: pool, tracker, MediaPool, TrackerSequencer");
        return;
    }
    
    onAddModule(moduleType);
    addLog("Added module: %s", moduleType.c_str());
    ofLogNotice("Console") << "Added module via console: " << moduleType;
}

void Console::cmdHelp() {
    addLog("=== Commands ===");
    addLog("  list, ls              - List all modules");
    addLog("  remove <name>, rm     - Remove a module");
    addLog("  add <type>            - Add a module (pool, tracker)");
    addLog("  clear, cls            - Clear console");
    addLog("  help, ?               - Show this help");
    addLog("");
    addLog("=== Examples ===");
    addLog("  list");
    addLog("  add pool");
    addLog("  add tracker");
    addLog("  remove pool2");
    addLog("");
    addLog("=== Shortcuts ===");
    addLog("  :                    - Toggle console");
    addLog("  Up/Down arrows       - Navigate command history");
}

void Console::cmdClear() {
    items.clear();
    addLog("Console cleared.");
}

void Console::addLog(const std::string& text) {
    items.push_back(text);
    // Keep last 1000 lines (more efficient than my original approach)
    if (items.size() > 1000) {
        items.erase(items.begin(), items.begin() + 500);
    }
}

void Console::addLog(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    addLog(std::string(buf));
}

