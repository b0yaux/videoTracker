#include "OscilloscopeGUI.h"
#include "Oscilloscope.h"
#include "Module.h"
#include "core/ModuleRegistry.h"
#include "gui/ModuleGUI.h"
#include "gui/GUIManager.h"
#include "gui/GUIConstants.h"
#include "ofMain.h"
#include "ofLog.h"
#include <imgui.h>
#include <algorithm>
#include <iomanip>

OscilloscopeGUI::OscilloscopeGUI() {
    // Base class handles module reference setup
}

void OscilloscopeGUI::draw() {
    // Use base class draw which handles visibility, ON/OFF toggle, etc.
    ModuleGUI::draw();
}

void OscilloscopeGUI::drawContent() {
    Oscilloscope* oscilloscope = getOscilloscope();
    if (!oscilloscope) {
        ImGui::Text("No Oscilloscope module found");
        return;
    }
    
    float controlsStartTime = ofGetElapsedTimef();
    drawControls();
    float controlsTime = (ofGetElapsedTimef() - controlsStartTime) * 1000.0f;
    if (controlsTime > 1.0f) {
        ofLogNotice("OscilloscopeGUI") << "[PERF] drawControls(): " 
                                       << std::fixed << std::setprecision(2) << controlsTime << "ms";
    }
}

Oscilloscope* OscilloscopeGUI::getOscilloscope() const {
    ModuleRegistry* reg = ModuleGUI::getRegistry();
    std::string instanceName = ModuleGUI::getInstanceName();
    if (reg && !instanceName.empty()) {
        auto module = reg->getModule(instanceName);
        if (!module) return nullptr;
        return dynamic_cast<Oscilloscope*>(module.get());
    }
    return nullptr;
}

void OscilloscopeGUI::drawControls() {
    Oscilloscope* oscilloscope = getOscilloscope();
    if (!oscilloscope) return;
    
    // Parameters in a 4-column table - equal width columns
    if (ImGui::BeginTable("OscilloscopeParams", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
        // Header row
        ImGui::TableSetupColumn("Scale", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Thickness", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Background", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        
        // Data row
        ImGui::TableNextRow();
        
        // Scale column - custom slider with CellWidget aesthetic
        ImGui::TableSetColumnIndex(0);
        float scale = oscilloscope->getScale();
        drawCustomSlider("##Scale", scale, 0.1f, 5.0f, "%.2f", 
            [oscilloscope](float value) { oscilloscope->setScale(value); });
        
        // Thickness column - custom slider with CellWidget aesthetic
        ImGui::TableSetColumnIndex(1);
        float thickness = oscilloscope->getThickness();
        drawCustomSlider("##Thickness", thickness, 0.5f, 2.0f, "%.2f",
            [oscilloscope](float value) { oscilloscope->setThickness(value); });
        
        // Color column - rectangle button that fills cell
        ImGui::TableSetColumnIndex(2);
        ofColor color = oscilloscope->getColor();
        drawCustomColorPicker("##Color", color,
            [oscilloscope](const ofColor& newColor) { oscilloscope->setColor(newColor); });
        
        // Background color column - rectangle button that fills cell
        ImGui::TableSetColumnIndex(3);
        ofColor backgroundColor = oscilloscope->getBackgroundColor();
        drawCustomColorPicker("##BackgroundColor", backgroundColor,
            [oscilloscope](const ofColor& newColor) { oscilloscope->setBackgroundColor(newColor); });
        
        ImGui::EndTable();
    }
}

void OscilloscopeGUI::drawCustomSlider(const char* label, float value, float min, float max, 
                                      const char* format, std::function<void(float)> onChanged) {
    // Get available width (full column width)
    float width = ImGui::GetContentRegionAvail().x;
    float height = ImGui::GetFrameHeight();
    
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImVec2(width, height);
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    
    // Create invisible button for interaction (full width)
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton(label, canvasSize);
    
    bool isActive = ImGui::IsItemActive();
    bool isHovered = ImGui::IsItemHovered();
    
    // Calculate fill percent (normalized 0-1)
    float fillPercent = (value - min) / (max - min);
    fillPercent = std::max(0.0f, std::min(1.0f, fillPercent));
    
    // Draw fill bar (CellWidget style)
    if (fillPercent > 0.01f) {
        ImVec2 fillEnd = ImVec2(canvasPos.x + canvasSize.x * fillPercent, canvasPos.y + canvasSize.y);
        drawList->AddRectFilled(canvasPos, fillEnd, GUIConstants::toU32(GUIConstants::CellWidget::FillBar));
    }
    
    // Handle drag interaction
    if (isActive && ImGui::IsMouseDragging(0)) {
        ImVec2 mousePos = io.MousePos;
        float mouseX = mousePos.x - canvasPos.x;
        float normalizedX = std::max(0.0f, std::min(1.0f, mouseX / canvasSize.x));
        float newValue = min + normalizedX * (max - min);
        
        // For integer-like values, snap to reasonable precision
        if (max - min < 10.0f) {
            newValue = std::round(newValue * 100.0f) / 100.0f;
        }
        
        onChanged(newValue);
    }
    
    // Draw value text (right-aligned, like CellWidget)
    char valueText[32];
    snprintf(valueText, sizeof(valueText), format, value);
    ImVec2 textSize = ImGui::CalcTextSize(valueText);
    ImVec2 textPos = ImVec2(
        canvasPos.x + canvasSize.x - textSize.x - 4.0f,
        canvasPos.y + (canvasSize.y - textSize.y) * 0.5f
    );
    drawList->AddText(textPos, GUIConstants::toU32(GUIConstants::Text::Default), valueText);
    
    // Draw hover outline (subtle feedback)
    if (isHovered || isActive) {
        drawList->AddRect(canvasPos, 
                         ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                         GUIConstants::toU32(GUIConstants::Border::Light),
                         0.0f, 0, 1.0f);
    }
    
    // Advance cursor
    ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, canvasPos.y + canvasSize.y));
}

void OscilloscopeGUI::drawCustomColorPicker(const char* label, const ofColor& color, 
                                           std::function<void(const ofColor&)> onChanged) {
    // Get available width (full column width)
    float width = ImGui::GetContentRegionAvail().x;
    float height = ImGui::GetFrameHeight();
    
    // Create unique popup ID based on label to avoid conflicts between multiple color pickers
    std::string popupId = std::string("ColorPickerPopup_") + label;
    
    // Use default ImGui ColorButton (rectangle that fills cell)
    float colorArray[3] = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f };
    if (ImGui::ColorButton(label, ImVec4(colorArray[0], colorArray[1], colorArray[2], 1.0f), 
                          ImGuiColorEditFlags_NoTooltip, ImVec2(width, height))) {
        ImGui::OpenPopup(popupId.c_str());
    }
    
    // Color picker popup with unique ID
    if (ImGui::BeginPopup(popupId.c_str())) {
        std::string pickerId = std::string("##ColorPicker_") + label;
        if (ImGui::ColorPicker3(pickerId.c_str(), colorArray)) {
            ofColor newColor;
            newColor.r = static_cast<int>(colorArray[0] * 255.0f);
            newColor.g = static_cast<int>(colorArray[1] * 255.0f);
            newColor.b = static_cast<int>(colorArray[2] * 255.0f);
            newColor.a = 255;
            onChanged(newColor);
        }
        ImGui::EndPopup();
    }
}

//--------------------------------------------------------------
// GUI Factory Registration
//--------------------------------------------------------------
namespace {
    struct OscilloscopeGUIRegistrar {
        OscilloscopeGUIRegistrar() {
            GUIManager::registerGUIType("Oscilloscope", 
                []() -> std::unique_ptr<ModuleGUI> {
                    return std::make_unique<OscilloscopeGUI>();
                });
        }
    };
    static OscilloscopeGUIRegistrar g_oscilloscopeGUIRegistrar;
}

