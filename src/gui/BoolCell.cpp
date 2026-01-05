#include "BoolCell.h"
#include "gui/GUIConstants.h"
#include <imgui.h>
#include "ofLog.h"

BoolCell::BoolCell() {
}

CellInteraction BoolCell::draw(int uniqueId, bool isFocused, bool shouldFocusFirst) {
    CellInteraction result;
    
    ImGui::PushID(uniqueId);
    
    // Get current boolean value
    bool currentValue = false;
    if (getCurrentValue) {
        currentValue = getCurrentValue();
    }
    
    // Update focus state
    focused_ = isFocused;
    
    // Draw toggle button with ON/OFF labels
    std::string buttonLabel = currentValue ? "ON" : "OFF";
    
    // Apply styling for toggle state
    if (currentValue) {
        ImGui::PushStyleColor(ImGuiCol_Button, GUIConstants::Button::EditMode);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GUIConstants::Button::EditModeHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, GUIConstants::Button::EditModeActive);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, GUIConstants::Button::Transparent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GUIConstants::Button::Transparent);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, GUIConstants::Button::Transparent);
    }
    
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
    
    // Set focus if requested
    if (shouldFocusFirst) {
        ImGui::SetKeyboardFocusHere(0);
    }
    
    // Draw button
    bool buttonClicked = ImGui::Button(buttonLabel.c_str(), ImVec2(-1, 0));
    
    // Check if actually focused
    bool actuallyFocused = ImGui::IsItemFocused();
    focused_ = actuallyFocused;
    
    // Handle click - toggle value
    if (buttonClicked || (actuallyFocused && ImGui::IsKeyPressed(ImGuiKey_Space, false))) {
        bool newValue = !currentValue;
        
        // Apply via bool callback (for backward compatibility)
        if (onValueAppliedBool) {
            onValueAppliedBool(parameterName, newValue);
        }
        
        // Also call BaseCell string callback for unified interface
        if (onValueApplied) {
            onValueApplied(parameterName, newValue ? "1" : "0");
        }
        
        result.clicked = true;
        result.valueChanged = true;
    }
    
    // Handle Enter key to toggle
    if (actuallyFocused && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
        bool newValue = !currentValue;
        
        if (onValueAppliedBool) {
            onValueAppliedBool(parameterName, newValue);
        }
        if (onValueApplied) {
            onValueApplied(parameterName, newValue ? "1" : "0");
        }
        
        result.valueChanged = true;
    }
    
    // Draw outline if focused
    if (actuallyFocused) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (drawList) {
            ImVec2 cellMin = ImGui::GetItemRectMin();
            ImVec2 cellMax = ImGui::GetItemRectMax();
            ImVec2 outlineMin = ImVec2(cellMin.x - 1, cellMin.y - 1);
            ImVec2 outlineMax = ImVec2(cellMax.x + 1, cellMax.y + 1);
            ImU32 outlineColor = GUIConstants::toU32(GUIConstants::Outline::RedDim);
            drawList->AddRect(outlineMin, outlineMax, outlineColor, 0.0f, 0, 2.0f);
        }
    }
    
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(3);
    ImGui::PopID();
    
    return result;
}

void BoolCell::enterEditMode() {
    // BoolCell doesn't have a traditional edit mode - clicking toggles immediately
    editing_ = true;
    if (onEditModeChanged) {
        onEditModeChanged(true);
    }
}

void BoolCell::exitEditMode() {
    editing_ = false;
    if (onEditModeChanged) {
        onEditModeChanged(false);
    }
}

void BoolCell::configure(const ParameterDescriptor& desc,
                         std::function<float()> getter,
                         std::function<void(float)> setter,
                         std::function<void()> remover,
                         std::function<std::string(float)> formatter,
                         std::function<float(const std::string&)> parser) {
    // Set up getter callback (convert float to bool)
    getCurrentValue = [getter]() -> bool {
        float val = getter();
        return val > 0.5f;
    };
    
    // Set up setter callback (both bool and string versions)
    onValueAppliedBool = [setter](const std::string&, bool value) {
        setter(value ? 1.0f : 0.0f);
    };
    
    // Also set string callback for BaseCell interface
    onValueApplied = [setter](const std::string&, const std::string& valueStr) {
        bool value = (valueStr == "1" || valueStr == "true" || valueStr == "ON");
        setter(value ? 1.0f : 0.0f);
    };
    
    // Remover is optional for bool cells (can use setter with default)
    if (remover) {
        onValueRemoved = [remover](const std::string&) {
            remover();
        };
    } else {
        float defaultValue = desc.defaultValue;
        onValueRemoved = [setter, defaultValue](const std::string&) {
            setter(defaultValue);
        };
    }
}

