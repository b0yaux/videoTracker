#include "MenuBar.h"
#include "ofLog.h"
#include "GUIConstants.h"
#include <cstring>

MenuBar::MenuBar() {
    initializeAvailableModules();
}

void MenuBar::setup(
    std::function<void()> onSavePattern_fn,
    std::function<void()> onLoadPattern_fn,
    std::function<void()> onSaveLayout_fn,
    std::function<void()> onLoadLayout_fn,
    std::function<void(const std::string& moduleType)> onAddModule_fn,
    std::function<void()> onToggleFileBrowser_fn,
    std::function<void()> onToggleConsole_fn,
    std::function<void()> onToggleDemoWindow_fn
) {
    onSavePattern = onSavePattern_fn;
    onLoadPattern = onLoadPattern_fn;
    onSaveLayout = onSaveLayout_fn;
    onLoadLayout = onLoadLayout_fn;
    onAddModule = onAddModule_fn;
    onToggleFileBrowser = onToggleFileBrowser_fn;
    onToggleConsole = onToggleConsole_fn;
    onToggleDemoWindow = onToggleDemoWindow_fn;

    ofLogNotice("MenuBar") << "Setup complete";
}

void MenuBar::initializeAvailableModules() {
    availableModules.clear();
    availableModules.push_back({"MediaPool", "Media Pool", "Video/audio media pool"});
    availableModules.push_back({"TrackerSequencer", "Tracker Sequencer", "Step sequencer for patterns"});
}

void MenuBar::draw() {
    if (ImGui::BeginMainMenuBar()) {
        drawFileMenu();
        drawViewMenu();
        drawAddMenu();
        drawLayoutMenu();
        drawHelpMenu();
        ImGui::EndMainMenuBar();
    }

    // Help popup - draw every frame if open
    // Note: OpenPopup must be called before BeginPopupModal in the same frame
    // So we check if the flag was set and open it, then show the modal
    if (showControlsHelp && ImGui::BeginPopupModal("Controls Help", &showControlsHelp, 
                                    ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(GUIConstants::Text::Warning, "Controls");
        ImGui::Text("SPACE: Play/Stop");
        ImGui::Text("R: Reset");
        ImGui::Text("G: Toggle GUI");
        ImGui::Text("N: Next media");
        ImGui::Text("M: Previous media");
        ImGui::Text("S: Save session");
        ImGui::Text("MAJ+A: Add Module");
        ImGui::Separator();
        ImGui::TextColored(GUIConstants::Text::Info, "Pattern Editing");
        ImGui::Text("Click cells to edit");
        ImGui::Text("Drag to set values");
        ImGui::Text("Right-click for options");
        ImGui::Separator();
        if (ImGui::Button("Close")) {
            showControlsHelp = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    
    // Add Module popup
    drawAddModulePopup();
}

bool MenuBar::handleKeyPress(int key, bool shiftPressed) {
    // MAJ+a (Shift+A) opens Add Module popup
    if (shiftPressed && (key == 'A' || key == 'a')) {
        showAddModulePopup = true;
        // Reset filter and selection when opening
        memset(addModuleFilter, 0, sizeof(addModuleFilter));
        selectedModuleIndex = 0;
        return true;
    }
    return false;
}

void MenuBar::drawFileMenu() {
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save Session")) {
            if (onSavePattern) onSavePattern();  // Callback name kept for compatibility
        }
        if (ImGui::MenuItem("Load Session")) {
            if (onLoadPattern) onLoadPattern();  // Callback name kept for compatibility
        }
        ImGui::EndMenu();
    }
}

void MenuBar::drawViewMenu() {
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("File Browser")) {
            if (onToggleFileBrowser) onToggleFileBrowser();
        }
        if (ImGui::MenuItem("Console")) {
            if (onToggleConsole) onToggleConsole();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("ImGui Demo", "Ctrl+D")) {
            if (onToggleDemoWindow) onToggleDemoWindow();
        }
        ImGui::EndMenu();
    }
}

void MenuBar::drawAddMenu() {
    if (ImGui::BeginMenu("Add")) {
        if (ImGui::MenuItem("Add Module...", "MAJ+A")) {
            showAddModulePopup = true;
            // Reset filter and selection when opening
            memset(addModuleFilter, 0, sizeof(addModuleFilter));
            selectedModuleIndex = 0;
        }
        ImGui::EndMenu();
    }
}

void MenuBar::drawLayoutMenu() {
    if (ImGui::BeginMenu("Layout")) {
        if (ImGui::MenuItem("Save Layout as Default")) {
            if (onSaveLayout) onSaveLayout();
        }
        if (ImGui::MenuItem("Load Default Layout")) {
            if (onLoadLayout) onLoadLayout();
        }
        ImGui::EndMenu();
    }
}

void MenuBar::drawHelpMenu() {
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Controls")) {
            showControlsHelp = true;
            // OpenPopup must be called in the same frame as BeginPopupModal
            // We'll open it immediately here, then BeginPopupModal will be called in draw()
            ImGui::OpenPopup("Controls Help");
        }
        ImGui::EndMenu();
    }
}

void MenuBar::drawAddModulePopup() {
    if (!showAddModulePopup) return;
    
    // Open popup on first frame
    if (showAddModulePopup) {
        ImGui::OpenPopup("Add Module");
    }
    
    // Center popup on screen
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), 
                           ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
    if (ImGui::BeginPopupModal("Add Module", &showAddModulePopup, 
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        // Filter input (auto-focus on first frame)
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::InputText("##filter", addModuleFilter, sizeof(addModuleFilter));
        
        // Build filtered list
        std::vector<int> filteredIndices;
        std::string filterLower = addModuleFilter;
        std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
        
        for (size_t i = 0; i < availableModules.size(); i++) {
            if (filterLower.empty()) {
                filteredIndices.push_back(i);
            } else {
                std::string nameLower = availableModules[i].displayName;
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                
                if (nameLower.find(filterLower) != std::string::npos) {
                    filteredIndices.push_back(i);
                }
            }
        }
        
        // Clamp selected index to valid range
        if (selectedModuleIndex >= static_cast<int>(filteredIndices.size())) {
            selectedModuleIndex = 0;
        }
        if (selectedModuleIndex < 0 && !filteredIndices.empty()) {
            selectedModuleIndex = 0;
        }
        
        // Draw filtered list
        for (size_t listIdx = 0; listIdx < filteredIndices.size(); listIdx++) {
            int moduleIdx = filteredIndices[listIdx];
            const auto& module = availableModules[moduleIdx];
            
            bool isSelected = (static_cast<int>(listIdx) == selectedModuleIndex);
            
            if (ImGui::Selectable(module.displayName.c_str(), isSelected)) {
                selectedModuleIndex = static_cast<int>(listIdx);
            }
        }
        
        // Handle keyboard input
        if (ImGui::IsWindowFocused()) {
            // Arrow key navigation
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && selectedModuleIndex > 0) {
                selectedModuleIndex--;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && 
                selectedModuleIndex < static_cast<int>(filteredIndices.size()) - 1) {
                selectedModuleIndex++;
            }
            
            // Enter to add, Escape to cancel
            bool canAdd = !filteredIndices.empty() && selectedModuleIndex >= 0 && 
                          selectedModuleIndex < static_cast<int>(filteredIndices.size());
            
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) && canAdd) {
                if (onAddModule) {
                    int moduleIdx = filteredIndices[selectedModuleIndex];
                    onAddModule(availableModules[moduleIdx].typeName);
                    showAddModulePopup = false;
                    memset(addModuleFilter, 0, sizeof(addModuleFilter));
                    selectedModuleIndex = 0;
                }
            }
            
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                showAddModulePopup = false;
                memset(addModuleFilter, 0, sizeof(addModuleFilter));
                selectedModuleIndex = 0;
            }
        }
        
        ImGui::EndPopup();
    }
}

