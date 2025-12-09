#include "VideoMixerGUI.h"
#include "VideoMixer.h"
#include "core/ModuleRegistry.h"
#include "core/ParameterRouter.h"
#include "gui/GUIConstants.h"
#include "gui/GUIManager.h"
#include "ofMain.h"
#include "ofLog.h"
#include <imgui.h>

VideoMixerGUI::VideoMixerGUI() {
}

void VideoMixerGUI::draw() {
    // Call base class draw (handles visibility, title bar, enabled state)
    ModuleGUI::draw();
}

VideoMixer* VideoMixerGUI::getVideoMixer() const {
    ModuleRegistry* reg = ModuleGUI::getRegistry();
    std::string instanceName = ModuleGUI::getInstanceName();
    if (reg && !instanceName.empty()) {
        auto module = reg->getModule(instanceName);
        if (!module) return nullptr;
        return dynamic_cast<VideoMixer*>(module.get());
    }
    return nullptr;
}

void VideoMixerGUI::drawContent() {
    // Skip drawing when window is collapsed
    if (ImGui::IsWindowCollapsed()) {
        return;
    }
    
    VideoMixer* mixer = getVideoMixer();
    if (!mixer) {
        std::string instanceName = ModuleGUI::getInstanceName();
        ImGui::Text("Instance '%s' not found", instanceName.empty() ? "unknown" : instanceName.c_str());
        return;
    }
    
    // Draw master controls section
    drawMasterControls();
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Draw blend mode section
    drawBlendMode();
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Draw connections section
    drawConnections();
}

void VideoMixerGUI::drawMasterControls() {
    VideoMixer* mixer = getVideoMixer();
    if (!mixer) return;
    
    ImGui::Text("Master Controls");
    
    // Master opacity
    float masterOpacity = mixer->getMasterOpacity();
    if (ImGui::SliderFloat("Master Opacity##masterOpacity", &masterOpacity, 0.0f, 1.0f, "%.2f")) {
        mixer->setMasterOpacity(masterOpacity);
        ParameterRouter* router = getParameterRouter();
        if (router) {
            router->notifyParameterChange(mixer, "masterOpacity", masterOpacity);
        }
    }
    
    // Auto-normalize toggle
    bool autoNormalize = mixer->getAutoNormalize();
    if (ImGui::Checkbox("Auto Normalize", &autoNormalize)) {
        mixer->setAutoNormalize(autoNormalize);
        ParameterRouter* router = getParameterRouter();
        if (router) {
            router->notifyParameterChange(mixer, "autoNormalize", autoNormalize ? 1.0f : 0.0f);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Automatically normalize opacity for ADD mode to prevent white-out");
    }
}

void VideoMixerGUI::drawBlendMode() {
    VideoMixer* mixer = getVideoMixer();
    if (!mixer) return;
    
    ImGui::Text("Blend Mode");
    
    ofBlendMode currentMode = mixer->getBlendMode();
    int currentModeIndex = 0;
    if (currentMode == OF_BLENDMODE_ADD) currentModeIndex = 0;
    else if (currentMode == OF_BLENDMODE_MULTIPLY) currentModeIndex = 1;
    else if (currentMode == OF_BLENDMODE_ALPHA) currentModeIndex = 2;
    
    const char* blendModes[] = { "Add", "Multiply", "Alpha" };
    
    if (ImGui::Combo("##blendMode", &currentModeIndex, blendModes, IM_ARRAYSIZE(blendModes))) {
        ofBlendMode newMode = OF_BLENDMODE_ADD;
        if (currentModeIndex == 0) newMode = OF_BLENDMODE_ADD;
        else if (currentModeIndex == 1) newMode = OF_BLENDMODE_MULTIPLY;
        else if (currentModeIndex == 2) newMode = OF_BLENDMODE_ALPHA;
        
        mixer->setBlendMode(newMode);
        ParameterRouter* router = getParameterRouter();
        if (router) {
            router->notifyParameterChange(mixer, "blendMode", static_cast<float>(currentModeIndex));
        }
    }
    
    // Show current blend mode name
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", getBlendModeName(currentMode));
}

void VideoMixerGUI::drawConnections() {
    VideoMixer* mixer = getVideoMixer();
    if (!mixer) return;
    
    ImGui::Text("Connections");
    
    size_t numConnections = mixer->getNumConnections();
    if (numConnections == 0) {
        ImGui::TextDisabled("No connections");
        return;
    }
    
    // Draw connection list
    for (size_t i = 0; i < numConnections; i++) {
        // Try to get module name from registry
        std::string moduleName = "Connection " + std::to_string(i);
        
        // Note: VideoMixer stores weak_ptr to modules, but we don't have direct access
        // For now, just show connection index. In the future, we could add a method to
        // VideoMixer to get module names for connections.
        
        float opacity = mixer->getConnectionOpacity(i);
        drawConnectionOpacity(i, moduleName, opacity);
    }
}

void VideoMixerGUI::drawConnectionOpacity(size_t connectionIndex, const std::string& moduleName, float opacity) {
    VideoMixer* mixer = getVideoMixer();
    if (!mixer) return;
    
    ImGui::PushID(static_cast<int>(connectionIndex));
    
    // Module name label
    ImGui::Text("%s", moduleName.c_str());
    
    // Opacity slider
    std::string sliderId = "##opacity_" + std::to_string(connectionIndex);
    if (ImGui::SliderFloat(sliderId.c_str(), &opacity, 0.0f, 1.0f, "%.2f")) {
        mixer->setConnectionOpacity(connectionIndex, opacity);
        ParameterRouter* router = getParameterRouter();
        if (router) {
            std::string paramName = "connectionOpacity_" + std::to_string(connectionIndex);
            router->notifyParameterChange(mixer, paramName, opacity);
        }
    }
    
    // Display as percentage
    ImGui::SameLine();
    ImGui::Text("%.0f%%", opacity * 100.0f);
    
    ImGui::PopID();
}

const char* VideoMixerGUI::getBlendModeName(ofBlendMode mode) {
    switch (mode) {
        case OF_BLENDMODE_ADD: return "Add";
        case OF_BLENDMODE_MULTIPLY: return "Multiply";
        case OF_BLENDMODE_ALPHA: return "Alpha";
        default: return "Unknown";
    }
}

//--------------------------------------------------------------
// GUI Factory Registration
//--------------------------------------------------------------
namespace {
    struct VideoMixerGUIRegistrar {
        VideoMixerGUIRegistrar() {
            GUIManager::registerGUIType("VideoMixer", 
                []() -> std::unique_ptr<ModuleGUI> {
                    return std::make_unique<VideoMixerGUI>();
                });
        }
    };
    static VideoMixerGUIRegistrar g_videoMixerGUIRegistrar;
}
