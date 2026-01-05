#include "MenuCell.h"
#include "gui/GUIConstants.h"
#include <imgui.h>
#include "ofLog.h"
#include <algorithm>

MenuCell::MenuCell() : currentIndex_(0) {
}

void MenuCell::setEnumOptions(const std::vector<std::string>& options) {
    enumOptions_ = options;
    // Clamp current index to valid range
    if (!enumOptions_.empty() && currentIndex_ >= (int)enumOptions_.size()) {
        currentIndex_ = (int)enumOptions_.size() - 1;
    }
}

void MenuCell::setCurrentIndex(int index) {
    if (!enumOptions_.empty()) {
        currentIndex_ = std::max(0, std::min(index, (int)enumOptions_.size() - 1));
    } else {
        currentIndex_ = 0;
    }
}

std::string MenuCell::getCurrentOptionText() const {
    if (enumOptions_.empty()) {
        return "--";
    }
    if (currentIndex_ >= 0 && currentIndex_ < (int)enumOptions_.size()) {
        return enumOptions_[currentIndex_];
    }
    return "--";
}

CellInteraction MenuCell::draw(int uniqueId, bool isFocused, bool shouldFocusFirst) {
    CellInteraction result;
    
    ImGui::PushID(uniqueId);
    
    // Get current enum index
    int currentIdx = currentIndex_;
    if (getIndex) {
        currentIdx = getIndex();
        currentIndex_ = currentIdx;  // Sync internal state
    }
    
    // Clamp to valid range
    if (!enumOptions_.empty()) {
        currentIdx = std::max(0, std::min(currentIdx, (int)enumOptions_.size() - 1));
    }
    
    // Update focus state
    focused_ = isFocused;
    
    // Get current option text
    std::string buttonLabel = getCurrentOptionText();
    
    // Apply styling
    ImGui::PushStyleColor(ImGuiCol_Button, GUIConstants::Button::Transparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GUIConstants::Button::Transparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, GUIConstants::Button::Transparent);
    
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
    
    // Handle click - cycle through options
    if (buttonClicked || (actuallyFocused && ImGui::IsKeyPressed(ImGuiKey_Space, false))) {
        if (!enumOptions_.empty()) {
            // Cycle to next option
            currentIdx = (currentIdx + 1) % (int)enumOptions_.size();
            currentIndex_ = currentIdx;
            
            // Apply via enum callback (for backward compatibility)
            if (onValueAppliedEnum) {
                onValueAppliedEnum(parameterName, currentIdx);
            }
            
            // Also call BaseCell string callback for unified interface
            if (onValueApplied) {
                onValueApplied(parameterName, enumOptions_[currentIdx]);
            }
            
            result.clicked = true;
            result.valueChanged = true;
        }
    }
    
    // Handle arrow keys to cycle
    if (actuallyFocused) {
        bool leftPressed = ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false);
        bool rightPressed = ImGui::IsKeyPressed(ImGuiKey_RightArrow, false);
        
        if ((leftPressed || rightPressed) && !enumOptions_.empty()) {
            int delta = rightPressed ? 1 : -1;
            currentIdx = (currentIdx + delta + (int)enumOptions_.size()) % (int)enumOptions_.size();
            currentIndex_ = currentIdx;
            
            if (onValueAppliedEnum) {
                onValueAppliedEnum(parameterName, currentIdx);
            }
            if (onValueApplied) {
                onValueApplied(parameterName, enumOptions_[currentIdx]);
            }
            
            result.valueChanged = true;
        }
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

void MenuCell::enterEditMode() {
    // MenuCell doesn't have a traditional edit mode - clicking cycles immediately
    editing_ = true;
    if (onEditModeChanged) {
        onEditModeChanged(true);
    }
}

void MenuCell::exitEditMode() {
    editing_ = false;
    if (onEditModeChanged) {
        onEditModeChanged(false);
    }
}

void MenuCell::configure(const ParameterDescriptor& desc,
                          std::function<float()> getter,
                          std::function<void(float)> setter,
                          std::function<void()> remover,
                          std::function<std::string(float)> formatter,
                          std::function<float(const std::string&)> parser) {
    // Set up getter callback (convert float to enum index)
    getIndex = [getter]() -> int {
        float val = getter();
        // Convert float to index (assuming enum values are stored as indices)
        return (int)std::round(val);
    };
    
    // Set up setter callback (both enum index and string versions)
    onValueAppliedEnum = [setter](const std::string&, int index) {
        setter((float)index);
    };
    
    // Also set string callback for BaseCell interface
    auto enumOptions = desc.enumOptions;  // Capture by value
    onValueApplied = [setter, enumOptions](const std::string&, const std::string& valueStr) {
        // Find index of valueStr in enumOptions
        for (size_t i = 0; i < enumOptions.size(); ++i) {
            if (enumOptions[i] == valueStr) {
                setter((float)i);
                return;
            }
        }
    };
    
    // Remover is optional for enum cells (can use setter with default index)
    if (remover) {
        onValueRemoved = [remover](const std::string&) {
            remover();
        };
    } else {
        int defaultIndex = desc.defaultEnumIndex;
        onValueRemoved = [setter, defaultIndex](const std::string&) {
            setter((float)defaultIndex);
        };
    }
}

