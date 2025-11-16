#include "ModuleGUI.h"
#include "core/ModuleRegistry.h"
#include "GUIConstants.h"
#include "ofLog.h"
#include "imgui_internal.h"

ModuleGUI::ModuleGUI() {
}

void ModuleGUI::drawCustomTitleBar() {
    // Draw custom title bar that integrates ON/OFF toggle into the actual window title bar
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window || window->SkipItems) return;
    
    ImGuiContext& g = *GImGui;
    ImGuiStyle& style = g.Style;
    
    // Get title bar rect (even with NoTitleBar, we can calculate where it would be)
    float titleBarHeight = ImGui::GetFrameHeight();
    ImRect titleBarRect;
    titleBarRect.Min = window->Pos;
    titleBarRect.Max = ImVec2(window->Pos.x + window->Size.x, window->Pos.y + titleBarHeight);
    
    // Calculate layout
    float padding = style.WindowPadding.x;
    float checkboxSize = ImGui::GetFrameHeight() * 0.6f;  // Slightly smaller checkbox
    std::string label = enabled_ ? "ON" : "OFF";
    ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
    float spacing = style.ItemSpacing.x;
    float totalControlsWidth = labelSize.x + spacing + checkboxSize;  // Label before checkbox
    
    // Make title bar area draggable (except where controls are)
    // This allows window dragging even with NoTitleBar flag
    ImRect draggableRect = titleBarRect;
    draggableRect.Max.x -= (padding + totalControlsWidth);  // Exclude control area from dragging
    ImGui::SetCursorScreenPos(draggableRect.Min);
    // CRITICAL: Ensure non-zero size for InvisibleButton (ImGui assertion requirement)
    float dragWidth = std::max(draggableRect.GetWidth(), 1.0f);
    float dragHeight = std::max(draggableRect.GetHeight(), 1.0f);
    ImGui::InvisibleButton("##title_bar_drag", ImVec2(dragWidth, dragHeight));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        ImVec2 newPos = ImVec2(window->Pos.x + delta.x, window->Pos.y + delta.y);
        ImGui::SetWindowPos(window, newPos);
    }
    
    // Draw title bar background (matches ImGui's title bar style)
    ImU32 titleBarBg = ImGui::GetColorU32(ImGui::IsWindowFocused() ? 
                                          ImGuiCol_TitleBgActive : ImGuiCol_TitleBg);
    window->DrawList->AddRectFilled(titleBarRect.Min, titleBarRect.Max, titleBarBg);
    
    // Draw module name (left side of title bar)
    ImVec2 textPos = ImVec2(titleBarRect.Min.x + padding, 
                           titleBarRect.Min.y + (titleBarHeight - ImGui::GetTextLineHeight()) * 0.5f);
    window->DrawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), 
                             instanceName.empty() ? "Unnamed Module" : instanceName.c_str());
    
    // Draw ON/OFF toggle (right side of title bar) - Label before checkbox
    // Position checkbox at the right edge
    ImVec2 checkboxPos = ImVec2(titleBarRect.Max.x - padding - checkboxSize,
                                titleBarRect.Min.y + (titleBarHeight - checkboxSize) * 0.5f);
    
    // Position label to the left of checkbox
    ImVec2 labelPos = ImVec2(checkboxPos.x - spacing - labelSize.x,
                            titleBarRect.Min.y + (titleBarHeight - ImGui::GetTextLineHeight()) * 0.5f);
    
    // Create invisible button area for checkbox interaction (covers both label and checkbox)
    ImVec2 buttonStartPos = ImVec2(labelPos.x, checkboxPos.y);
    ImGui::SetCursorScreenPos(buttonStartPos);
    // CRITICAL: Ensure non-zero size for InvisibleButton (ImGui assertion requirement)
    float safeControlsWidth = std::max(totalControlsWidth, 1.0f);
    float safeCheckboxSize = std::max(checkboxSize, 1.0f);
    ImGui::InvisibleButton(("##title_checkbox_" + instanceName).c_str(), 
                          ImVec2(safeControlsWidth, safeCheckboxSize));
    
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();
    
    if (clicked) {
        enabled_ = !enabled_;
        ofLogVerbose("ModuleGUI") << "Module " << instanceName << " " << (enabled_ ? "enabled" : "disabled");
    }
    
    // Draw ON/OFF label (before checkbox)
    ImU32 labelColor = enabled_ ? 
        ImGui::GetColorU32(ImGuiCol_Text) : 
        ImGui::GetColorU32(ImGuiCol_Text, 0.6f);
    window->DrawList->AddText(labelPos, labelColor, label.c_str());
    
    // Draw checkbox visual
    ImRect checkboxRect(checkboxPos, ImVec2(checkboxPos.x + checkboxSize, checkboxPos.y + checkboxSize));
    ImU32 borderColor = hovered ? 
        ImGui::GetColorU32(ImGuiCol_Border) : 
        ImGui::GetColorU32(ImGuiCol_Border, 0.5f);
    
    window->DrawList->AddRect(checkboxRect.Min, checkboxRect.Max, borderColor, 0.0f, 0, 1.5f);
    
    if (enabled_) {
        // Fill checkbox when enabled
        ImU32 fillColor = ImGui::GetColorU32(ImGuiCol_CheckMark, 0.3f);
        window->DrawList->AddRectFilled(checkboxRect.Min, checkboxRect.Max, fillColor);
        
        // Draw checkmark
        float checkmarkThickness = 2.0f;
        ImVec2 center = checkboxRect.GetCenter();
        ImVec2 p1 = ImVec2(center.x - checkboxSize * 0.2f, center.y);
        ImVec2 p2 = ImVec2(center.x - checkboxSize * 0.05f, center.y + checkboxSize * 0.15f);
        ImVec2 p3 = ImVec2(center.x + checkboxSize * 0.25f, center.y - checkboxSize * 0.15f);
        ImU32 checkmarkColor = ImGui::GetColorU32(ImGuiCol_CheckMark);
        window->DrawList->AddLine(p1, p2, checkmarkColor, checkmarkThickness);
        window->DrawList->AddLine(p2, p3, checkmarkColor, checkmarkThickness);
    }
    
    // Reserve space for title bar in content area
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + titleBarHeight + style.WindowPadding.y);
}

void ModuleGUI::draw() {
    // No title bar here - it's drawn by ViewManager using drawCustomTitleBar()
    // Just draw content
    
    if (enabled_) {
        drawContent();
    } else {
        // Show disabled state
        ImGui::PushStyleColor(ImGuiCol_Text, GUIConstants::Text::Disabled);
        ImGui::Text("Module disabled");
        ImGui::PopStyleColor();
    }
}

