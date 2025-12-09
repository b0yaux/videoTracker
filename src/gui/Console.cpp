#include "Console.h"

#include "core/ModuleRegistry.h"
#include "core/CommandExecutor.h"

#include "gui/GUIManager.h"

#include "ofLog.h"

#include <algorithm>
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

void Console::setCommandExecutor(CommandExecutor* executor) {
    commandExecutor = executor;
    // Set output callback so CommandExecutor can send output to Console
    if (commandExecutor) {
        commandExecutor->setOutputCallback([this](const std::string& text) {
            addLog(text);
        });
    }
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
    // Simple, clean console window
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_FirstUseEver,
        ImVec2(0.5f, 0.5f)
    );
    
    // Collapse window when hidden
    ImGui::SetNextWindowCollapsed(!isOpen, ImGuiCond_Always);
    
    // Simple window flags - let ImGui handle scrolling naturally
    ImGuiWindowFlags flags = ImGuiWindowFlags_None;
    
    if (ImGui::Begin("Console", &isOpen, flags)) {
        // Sync visibility state
        bool isCollapsed = ImGui::IsWindowCollapsed();
        if (!isCollapsed && !isOpen) {
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
    
    // Build log text buffer from items (only rebuild if items changed)
    if (logTextDirty) {
        logTextBuffer.clear();
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            logTextBuffer += item;
            if (i < items.size() - 1) {
                logTextBuffer += "\n";
            }
        }
        // InputTextMultiline needs a mutable buffer with extra space
        size_t currentSize = logTextBuffer.size();
        size_t bufferSize = std::max(currentSize + 1024, static_cast<size_t>(8192));
        logTextBuffer.reserve(bufferSize);
        logTextBuffer.resize(bufferSize, '\0');
        logTextDirty = false;
    }
    
    // Simple approach: use InputTextMultiline directly for display, selection, and scrolling
    // Reserve space for input line at bottom
    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    float inputLineHeight = ImGui::GetFrameHeightWithSpacing();
    ImVec2 logSize(availableSize.x, availableSize.y - inputLineHeight);
    
    ImGuiInputTextFlags multilineFlags = ImGuiInputTextFlags_ReadOnly | 
                                         ImGuiInputTextFlags_NoHorizontalScroll;
    
    char* logTextPtr = const_cast<char*>(logTextBuffer.data());
    ImGui::InputTextMultiline("##ConsoleLog", logTextPtr, logTextBuffer.capacity(),
                              logSize, multilineFlags);
    
    // Auto-scroll to bottom when new content is added
    // InputTextMultiline has internal scrolling - SetScrollHereY works on the current window
    if (scrollToBottom) {
        ImGui::SetScrollHereY(1.0f);
        scrollToBottom = false;
    }
    
    ImGui::Separator();
    
    // Command input
    bool reclaimFocus = false;
    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue | 
                                     ImGuiInputTextFlags_EscapeClearsAll;
    
    // Auto-focus input on window appearing or when console is opened
    if (ImGui::IsWindowAppearing() || shouldFocusInput) {
        ImGui::SetKeyboardFocusHere();
        shouldFocusInput = false;
    }
    
    // Draw InputText widget
    // Focus management: input line gets focus on window open/command execution
    // User can click log area to select text, but typing will naturally go to input line
    bool inputTextReturned = ImGui::InputText("##input", inputBuffer, sizeof(inputBuffer), inputFlags);
    
    // Check if InputText is actually focused/active (must check AFTER InputText call)
    bool isInputTextActive = ImGui::IsItemActive() || ImGui::IsItemFocused();
    inputTextWasFocused = isInputTextActive;  // Store for arrow key handling in keyPressed()
    
    // Note: Arrow key handling is now done in handleArrowKeys() called from ofApp::keyPressed()
    // This ensures arrow keys are consumed before InputRouter can process them
    
    // Handle command execution
    if (inputTextReturned) {
        std::string command = CommandExecutor::trim(inputBuffer);
        
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
}

void Console::executeCommand(const std::string& command) {
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
    
    // Delegate to CommandExecutor
    if (commandExecutor) {
        commandExecutor->executeCommand(command);
    } else {
        addLog("Error: CommandExecutor not set");
    }
    
    scrollToBottom = true;
}

void Console::addLog(const std::string& text) {
    items.push_back(text);
    logTextDirty = true; // Mark buffer as dirty when items change
    // Keep last 1000 lines (more efficient than my original approach)
    if (items.size() > 1000) {
        items.erase(items.begin(), items.begin() + 500);
        logTextDirty = true;
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



