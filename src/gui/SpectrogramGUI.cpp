#include "SpectrogramGUI.h"
#include "Spectrogram.h"
#include "Module.h"
#include "core/ModuleRegistry.h"
#include "gui/ModuleGUI.h"
#include "gui/GUIManager.h"
#include "gui/GUIConstants.h"
#include "ofxFft.h"
#include <imgui.h>
#include <algorithm>

SpectrogramGUI::SpectrogramGUI() {
    // Base class handles module reference setup
}

void SpectrogramGUI::draw() {
    // Use base class draw which handles visibility, ON/OFF toggle, etc.
    ModuleGUI::draw();
}

void SpectrogramGUI::drawContent() {
    Spectrogram* spectrogram = getSpectrogram();
    if (!spectrogram) {
        ImGui::Text("No Spectrogram module found");
        return;
    }
    
    drawControls();
}

Spectrogram* SpectrogramGUI::getSpectrogram() const {
    ModuleRegistry* reg = ModuleGUI::getRegistry();
    std::string instanceName = ModuleGUI::getInstanceName();
    if (reg && !instanceName.empty()) {
        auto module = reg->getModule(instanceName);
        if (!module) return nullptr;
        return dynamic_cast<Spectrogram*>(module.get());
    }
    return nullptr;
}

void SpectrogramGUI::drawControls() {
    Spectrogram* spectrogram = getSpectrogram();
    if (!spectrogram) return;
    
    // Parameters in a 4-column table - equal width columns
    if (ImGui::BeginTable("SpectrogramParams", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
        // Header row
        ImGui::TableSetupColumn("FFT Scale", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("FFT Size", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Window Type", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Speed", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        
        // Data row
        ImGui::TableNextRow();
        
        // FFT Scale column
        ImGui::TableSetColumnIndex(0);
        ImGui::SetNextItemWidth(-1); // Fill cell width with no padding
        Spectrogram::FftScale fftScale = spectrogram->getFftScale();
        const char* fftScales[] = { "Linear", "Log", "Mel" };
        int fftScaleIndex = 0;
        if (fftScale == Spectrogram::FFT_SCALE_LINEAR) fftScaleIndex = 0;
        else if (fftScale == Spectrogram::FFT_SCALE_LOG) fftScaleIndex = 1;
        else if (fftScale == Spectrogram::FFT_SCALE_MEL) fftScaleIndex = 2;
        
        char comboLabel[64];
        snprintf(comboLabel, sizeof(comboLabel), "##FFTScale");
        if (ImGui::Combo(comboLabel, &fftScaleIndex, fftScales, IM_ARRAYSIZE(fftScales))) {
            Spectrogram::FftScale newScales[] = {
                Spectrogram::FFT_SCALE_LINEAR,
                Spectrogram::FFT_SCALE_LOG,
                Spectrogram::FFT_SCALE_MEL
            };
            spectrogram->setFftScale(newScales[fftScaleIndex]);
        }
        
        // FFT Size column
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1); // Fill cell width with no padding
        int fftSize = spectrogram->getFftSize();
        const char* fftSizes[] = { "256", "512", "1024", "2048", "4096", "8192" };
        int fftSizeIndex = 0;
        if (fftSize == 256) fftSizeIndex = 0;
        else if (fftSize == 512) fftSizeIndex = 1;
        else if (fftSize == 1024) fftSizeIndex = 2;
        else if (fftSize == 2048) fftSizeIndex = 3;
        else if (fftSize == 4096) fftSizeIndex = 4;
        else if (fftSize == 8192) fftSizeIndex = 5;
        
        char comboLabelSize[64];
        snprintf(comboLabelSize, sizeof(comboLabelSize), "##FFTSize");
        if (ImGui::Combo(comboLabelSize, &fftSizeIndex, fftSizes, IM_ARRAYSIZE(fftSizes))) {
            int newSizes[] = { 256, 512, 1024, 2048, 4096, 8192 };
            spectrogram->setFftSize(newSizes[fftSizeIndex]);
        }
        
        // Window Type column
        ImGui::TableSetColumnIndex(2);
        ImGui::SetNextItemWidth(-1); // Fill cell width with no padding
        fftWindowType windowType = spectrogram->getWindowType();
        const char* windowTypes[] = { "Rectangular", "Bartlett", "Hann", "Hamming", "Sine" };
        int windowTypeIndex = 3;  // Default to Hamming
        if (windowType == OF_FFT_WINDOW_RECTANGULAR) windowTypeIndex = 0;
        else if (windowType == OF_FFT_WINDOW_BARTLETT) windowTypeIndex = 1;
        else if (windowType == OF_FFT_WINDOW_HANN) windowTypeIndex = 2;
        else if (windowType == OF_FFT_WINDOW_HAMMING) windowTypeIndex = 3;
        else if (windowType == OF_FFT_WINDOW_SINE) windowTypeIndex = 4;
        
        char comboLabelWindow[64];
        snprintf(comboLabelWindow, sizeof(comboLabelWindow), "##WindowType");
        if (ImGui::Combo(comboLabelWindow, &windowTypeIndex, windowTypes, IM_ARRAYSIZE(windowTypes))) {
            fftWindowType newTypes[] = { 
                OF_FFT_WINDOW_RECTANGULAR, 
                OF_FFT_WINDOW_BARTLETT, 
                OF_FFT_WINDOW_HANN, 
                OF_FFT_WINDOW_HAMMING, 
                OF_FFT_WINDOW_SINE 
            };
            spectrogram->setWindowType(newTypes[windowTypeIndex]);
        }
        
        // Speed column - custom slider
        ImGui::TableSetColumnIndex(3);
        float speed = spectrogram->getSpeed();
        drawCustomSlider("##Speed", speed, 0.2f, 5.0f, "%.2f",
            [spectrogram](float value) { spectrogram->setSpeed(value); });
        
        ImGui::EndTable();
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Volume-based Color Controls - 8-column table
    if (ImGui::BeginTable("SpectrogramVolumeColors", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
        // Header row with dB labels
        for (int i = 0; i < 8; i++) {
            float volumeDb = spectrogram->getVolumeStop(i);
            char headerLabel[32];
            snprintf(headerLabel, sizeof(headerLabel), "%.0fdB", volumeDb);
            ImGui::TableSetupColumn(headerLabel, ImGuiTableColumnFlags_WidthStretch);
        }
        ImGui::TableHeadersRow();
        
        // Data row
        ImGui::TableNextRow();
        
        for (int i = 0; i < 8; i++) {
            ImGui::TableSetColumnIndex(i);
            ofColor volumeColor = spectrogram->getVolumeColor(i);
            char popupId[64];
            snprintf(popupId, sizeof(popupId), "VolumeColorPicker%d", i);
            char buttonId[32];
            snprintf(buttonId, sizeof(buttonId), "##Volume%d", i);
            drawCustomColorPicker(buttonId, popupId, volumeColor,
                [spectrogram, i](const ofColor& newColor) { spectrogram->setVolumeColor(i, newColor); });
        }
        
        ImGui::EndTable();
    }
}

void SpectrogramGUI::drawCustomSlider(const char* label, float value, float min, float max, 
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

void SpectrogramGUI::drawCustomColorPicker(const char* label, const char* popupId, const ofColor& color, 
                                           std::function<void(const ofColor&)> onChanged) {
    // Get available width (full column width)
    float width = ImGui::GetContentRegionAvail().x;
    float height = ImGui::GetFrameHeight();
    
    // Use default ImGui ColorButton (rectangle that fills cell)
    float colorArray[3] = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f };
    if (ImGui::ColorButton(label, ImVec4(colorArray[0], colorArray[1], colorArray[2], 1.0f), 
                          ImGuiColorEditFlags_NoTooltip, ImVec2(width, height))) {
        ImGui::OpenPopup(popupId);
    }
    
    // Default color picker popup with unique ID
    if (ImGui::BeginPopup(popupId)) {
        if (ImGui::ColorPicker3("##ColorPicker", colorArray)) {
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
    struct SpectrogramGUIRegistrar {
        SpectrogramGUIRegistrar() {
            GUIManager::registerGUIType("Spectrogram", 
                []() -> std::unique_ptr<ModuleGUI> {
                    return std::make_unique<SpectrogramGUI>();
                });
        }
    };
    static SpectrogramGUIRegistrar g_spectrogramGUIRegistrar;
}

