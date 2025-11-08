#include "MenuBar.h"
#include "ofLog.h"

MenuBar::MenuBar() {
}

void MenuBar::setup(
    std::function<void()> onSavePattern_fn,
    std::function<void()> onLoadPattern_fn,
    std::function<void()> onSaveLayout_fn,
    std::function<void()> onLoadLayout_fn
) {
    onSavePattern = onSavePattern_fn;
    onLoadPattern = onLoadPattern_fn;
    onSaveLayout = onSaveLayout_fn;
    onLoadLayout = onLoadLayout_fn;

    ofLogNotice("MenuBar") << "Setup complete";
}

void MenuBar::draw() {
    if (ImGui::BeginMainMenuBar()) {
        drawFileMenu();
        drawLayoutMenu();
        drawHelpMenu();
        ImGui::EndMainMenuBar();
    }

    // Help popup - draw every frame if open
    // Note: OpenPopup must be called before BeginPopupModal in the same frame
    // So we check if the flag was set and open it, then show the modal
    if (showControlsHelp && ImGui::BeginPopupModal("Controls Help", &showControlsHelp, 
                                    ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Controls");
        ImGui::Text("SPACE: Play/Stop");
        ImGui::Text("R: Reset");
        ImGui::Text("G: Toggle GUI");
        ImGui::Text("N: Next media");
        ImGui::Text("M: Previous media");
        ImGui::Text("S: Save pattern");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Pattern Editing");
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
}

void MenuBar::drawFileMenu() {
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save Pattern")) {
            if (onSavePattern) onSavePattern();
        }
        if (ImGui::MenuItem("Load Pattern")) {
            if (onLoadPattern) onLoadPattern();
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

