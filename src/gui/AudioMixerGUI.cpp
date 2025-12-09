#include "AudioMixerGUI.h"
#include "modules/AudioMixer.h"
#include "core/ModuleRegistry.h"
#include "core/ParameterRouter.h"
#include "gui/GUIConstants.h"
#include "gui/GUIManager.h"
#include "ofLog.h"
#include <imgui.h>

AudioMixerGUI::AudioMixerGUI() {
}

void AudioMixerGUI::draw() {
    // Call base class draw (handles visibility, title bar, enabled state)
    ModuleGUI::draw();
}

AudioMixer* AudioMixerGUI::getAudioMixer() const {
    ModuleRegistry* reg = ModuleGUI::getRegistry();
    std::string instanceName = ModuleGUI::getInstanceName();
    if (reg && !instanceName.empty()) {
        auto module = reg->getModule(instanceName);
        if (!module) return nullptr;
        return dynamic_cast<AudioMixer*>(module.get());
    }
    return nullptr;
}

void AudioMixerGUI::drawContent() {
    // Skip drawing when window is collapsed
    if (ImGui::IsWindowCollapsed()) {
        return;
    }
    
    AudioMixer* mixer = getAudioMixer();
    if (!mixer) {
        std::string instanceName = ModuleGUI::getInstanceName();
        ImGui::Text("Instance '%s' not found", instanceName.empty() ? "unknown" : instanceName.c_str());
        return;
    }
    
    // Draw master volume section
    drawMasterVolume();
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Draw connections section
    drawConnections();
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Draw audio level visualization
    drawAudioLevel();
}

void AudioMixerGUI::drawMasterVolume() {
    AudioMixer* mixer = getAudioMixer();
    if (!mixer) return;
    
    ImGui::Text("Master Volume");
    
    float masterVolume = mixer->getMasterVolume();
    if (ImGui::SliderFloat("##masterVolume", &masterVolume, 0.0f, 1.0f, "%.2f")) {
        mixer->setMasterVolume(masterVolume);
        ParameterRouter* router = getParameterRouter();
        if (router) {
            router->notifyParameterChange(mixer, "masterVolume", masterVolume);
        }
    }
    
    // Display as percentage
    ImGui::SameLine();
    ImGui::Text("%.0f%%", masterVolume * 100.0f);
}

void AudioMixerGUI::drawConnections() {
    AudioMixer* mixer = getAudioMixer();
    if (!mixer) return;
    
    ImGui::Text("Connections");
    
    size_t numConnections = mixer->getNumConnections();
    if (numConnections == 0) {
        ImGui::TextDisabled("No connections");
        return;
    }
    
    // Get module registry to look up module names
    ModuleRegistry* reg = ModuleGUI::getRegistry();
    
    // Draw connection list
    for (size_t i = 0; i < numConnections; i++) {
        // Try to get module name from registry
        std::string moduleName = "Connection " + std::to_string(i);
        
        // Note: AudioMixer stores weak_ptr to modules, but we don't have direct access
        // For now, just show connection index. In the future, we could add a method to
        // AudioMixer to get module names for connections.
        
        float volume = mixer->getConnectionVolume(i);
        drawConnectionVolume(i, moduleName, volume);
    }
}

void AudioMixerGUI::drawConnectionVolume(size_t connectionIndex, const std::string& moduleName, float volume) {
    AudioMixer* mixer = getAudioMixer();
    if (!mixer) return;
    
    ImGui::PushID(static_cast<int>(connectionIndex));
    
    // Module name label
    ImGui::Text("%s", moduleName.c_str());
    
    // Volume slider
    std::string sliderId = "##volume_" + std::to_string(connectionIndex);
    if (ImGui::SliderFloat(sliderId.c_str(), &volume, 0.0f, 1.0f, "%.2f")) {
        mixer->setConnectionVolume(connectionIndex, volume);
        ParameterRouter* router = getParameterRouter();
        if (router) {
            std::string paramName = "connectionVolume_" + std::to_string(connectionIndex);
            router->notifyParameterChange(mixer, paramName, volume);
        }
    }
    
    // Display as percentage
    ImGui::SameLine();
    ImGui::Text("%.0f%%", volume * 100.0f);
    
    ImGui::PopID();
}

void AudioMixerGUI::drawAudioLevel() {
    AudioMixer* mixer = getAudioMixer();
    if (!mixer) return;
    
    ImGui::Text("Audio Level");
    
    // Get audio level from AudioMixer
    float level = mixer->getCurrentAudioLevel();
    
    // Draw progress bar
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, GUIConstants::Plot::Histogram);
    ImGui::ProgressBar(level, ImVec2(-1, 0), "");
    ImGui::PopStyleColor();
    
    ImGui::Text("Level: %.3f", level);
}

//--------------------------------------------------------------
// GUI Factory Registration
//--------------------------------------------------------------
namespace {
    struct AudioMixerGUIRegistrar {
        AudioMixerGUIRegistrar() {
            GUIManager::registerGUIType("AudioMixer", 
                []() -> std::unique_ptr<ModuleGUI> {
                    return std::make_unique<AudioMixerGUI>();
                });
        }
    };
    static AudioMixerGUIRegistrar g_audioMixerGUIRegistrar;
}
