#include "AudioOutputGUI.h"
#include "AudioOutput.h"
#include "Module.h"
#include "core/ModuleRegistry.h"
#include "core/ParameterRouter.h"
#include "gui/ModuleGUI.h"
#include "gui/GUIManager.h"
#include "ofSoundStream.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <string>

AudioOutputGUI::AudioOutputGUI() {
    // Base class handles module reference setup
}

void AudioOutputGUI::draw() {
    // Use base class draw which handles visibility, ON/OFF toggle, etc.
    ModuleGUI::draw();
}

void AudioOutputGUI::drawContent() {
    AudioOutput* audioOutput = getAudioOutput();
    if (!audioOutput) {
        ImGui::Text("No AudioOutput module found");
        return;
    }

    // Draw master volume with integrated audio level visualization
    drawMasterVolume();
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Draw connections as a compact table
    drawConnections();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Draw device selection and info in a table
    drawDeviceSelection();
}

AudioOutput* AudioOutputGUI::getAudioOutput() const {
    ModuleRegistry* reg = ModuleGUI::getRegistry();
    std::string instanceName = ModuleGUI::getInstanceName();
    if (reg && !instanceName.empty()) {
        auto module = reg->getModule(instanceName);
        if (!module) return nullptr;
        return dynamic_cast<AudioOutput*>(module.get());
    }
    return nullptr;
}

void AudioOutputGUI::drawDeviceSelection() {
    AudioOutput* audioOutput = getAudioOutput();
    if (!audioOutput) return;
    
    // Get device list
    std::vector<ofSoundDevice> devices = audioOutput->getAudioDevices();
    int currentDevice = audioOutput->getAudioDevice();
    
    // Create child window for device selection table
    if (ImGui::BeginChild("AudioDeviceChild", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None)) {
        // Create table for device selection and info
        if (ImGui::BeginTable("AudioDeviceTable", 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Audio Device", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            
            // Row 1: Device selection combo
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            
            if (devices.empty()) {
                ImGui::TextDisabled("No audio devices available");
            } else {
                // Create device list for combo box
                std::vector<std::string> deviceNames;
                for (const auto& device : devices) {
                    std::string name = device.name;
                    if (device.isDefaultOutput) {
                        name += " (Default)";
                    }
                    deviceNames.push_back(name);
                }
                
                // Device selection combo - make it full width
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##audioDevice", 
                                      currentDevice >= 0 && currentDevice < (int)deviceNames.size()
                                          ? deviceNames[currentDevice].c_str()
                                          : "Select Device")) {
                    for (size_t i = 0; i < deviceNames.size(); i++) {
                        bool isSelected = (currentDevice == static_cast<int>(i));
                        if (ImGui::Selectable(deviceNames[i].c_str(), isSelected)) {
                            // Set parameter directly on module
                            AudioOutput* audioOutput = getAudioOutput();
                            if (audioOutput) {
                                audioOutput->setParameter("audioDevice", static_cast<float>(i), true);
                            }
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            
            // Row 2+: Device details
            if (currentDevice >= 0 && currentDevice < static_cast<int>(devices.size())) {
                const auto& device = devices[currentDevice];
                
                // Channels
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("Channels: %d", device.outputChannels);
                
                // Sample Rate
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("Sample Rate: %d Hz", device.sampleRates.empty() ? 44100 : device.sampleRates[0]);
                
                // Default
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("Default: %s", device.isDefaultOutput ? "Yes" : "No");
            } else if (!devices.empty()) {
                // No device selected but devices are available
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("No device selected");
            }
            
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}


void AudioOutputGUI::drawMasterVolume() {
    AudioOutput* audioOutput = getAudioOutput();
    if (!audioOutput) return;

    float masterVolume = audioOutput->getMasterVolume();
    float audioLevel = audioOutput->getCurrentAudioLevel();

    if (ImGui::BeginChild("MasterVolumeChild", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None)) {
        if (ImGui::BeginTable("MasterVolumeTable", 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Master Volume", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            drawDraggableMasterVolume(masterVolume, audioLevel);

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

void AudioOutputGUI::drawConnections() {
    AudioOutput* audioOutput = getAudioOutput();
    if (!audioOutput) return;

    size_t numConnections = audioOutput->getNumConnections();
    if (numConnections == 0) {
        ImGui::TextDisabled("No connections");
        return;
    }
    
    // Create child window for connections table
    if (ImGui::BeginChild("ConnectionsChild", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None)) {
        // Get module registry to look up human-readable names
        ModuleRegistry* reg = ModuleGUI::getRegistry();
        
        // Remove cell padding and item spacing for compact rows
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        
        // Build table header
        if (ImGui::BeginTable("connections", 2, 
                              ImGuiTableFlags_Borders | 
                              ImGuiTableFlags_RowBg | 
                              ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Volume", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            
            // For each connection, get the module pointer and find its human-readable name
            for (size_t i = 0; i < numConnections; i++) {
                ImGui::PushID(static_cast<int>(i)); // Isolate widgets per row
                ImGui::TableNextRow();
                
                // Source column - get human-readable name using registry's getName method
                ImGui::TableSetColumnIndex(0);
                std::string displayName = "Connection " + std::to_string(i);
                
                if (reg) {
                    auto module = audioOutput->getConnectionModule(i);
                    if (module) {
                        std::string humanName = reg->getName(module);
                        if (!humanName.empty()) {
                            displayName = humanName;
                        }
                    }
                }
                
                ImGui::Text("%s", displayName.c_str());
                
                // Volume column - draggable audio visualization
                ImGui::TableSetColumnIndex(1);
                
                // Get current volume in linear scale (0.0-1.0)
                float volume = audioOutput->getConnectionVolume(i);
                
                // Get per-connection audio level
                float audioLevel = audioOutput->getConnectionAudioLevel(i);
                
                // Draw draggable visualization (minDb/maxDb use constants internally)
                drawDraggableAudioViz(i, volume, audioLevel, MIN_DB, MAX_DB);
                
                ImGui::PopID(); // End widget isolation
            }
            
            ImGui::EndTable();
        }
        
        // Restore style vars
        ImGui::PopStyleVar(2);
    }
    ImGui::EndChild();
}


//--------------------------------------------------------------
// Helper Functions
//--------------------------------------------------------------

float AudioOutputGUI::linearToDb(float linear) {
    return linear > MIN_LINEAR_VOLUME ? 20.0f * log10f(linear) : MIN_DB;
}

float AudioOutputGUI::dbToLinear(float db) {
    return powf(10.0f, db / 20.0f);
}

std::string AudioOutputGUI::formatDbText(float volume, float volumeDb) {
    if (volume <= MIN_LINEAR_VOLUME || volumeDb <= MIN_DB) {
        return "-inf dB";
    }
    char text[32];
    snprintf(text, sizeof(text), "%.1f dB", volumeDb);
    return std::string(text);
}

std::string AudioOutputGUI::formatAudioLevelText(float audioLevel) {
    if (audioLevel <= 0.0f) {
        return "-inf dB";
    }
    float levelDb = 20.0f * log10f(audioLevel);
    char text[32];
    snprintf(text, sizeof(text), "%.1f dB", levelDb);
    return std::string(text);
}

ImU32 AudioOutputGUI::getAudioLevelColor(float audioLevel) {
    if (audioLevel > AUDIO_LEVEL_CLIPPING) {
        return IM_COL32(255, 0, 0, 180); // Red (clipping)
    } else if (audioLevel > AUDIO_LEVEL_WARNING) {
        return IM_COL32(255, 255, 0, 180); // Yellow (warning)
    }
    return IM_COL32(0, 255, 0, 180); // Green
}

//--------------------------------------------------------------
// Unified Draggable Visualization
//--------------------------------------------------------------

void AudioOutputGUI::drawDraggableAudioVizInternal(
    const std::string& id,
    float volume,
    float audioLevel,
    const AudioVizConfig& config,
    DraggableAudioViz& vizState,
    std::function<void(float newVolume)> onVolumeChanged) {
    
    float volumeDb = linearToDb(volume);
    
    // Get widget area
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = config.canvasSize;
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    
    // Make entire area draggable
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton(id.c_str(), canvasSize);
    
    bool isActive = ImGui::IsItemActive();
    
    // Background
    drawList->AddRectFilled(
        canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        config.bgColor
    );
    
    // Audio level visualization
    if (audioLevel > 0.0f) {
        float levelWidth = canvasSize.x * audioLevel;
        ImVec2 levelMin = ImVec2(canvasPos.x, canvasPos.y);
        ImVec2 levelMax = ImVec2(canvasPos.x + levelWidth, canvasPos.y + canvasSize.y);
        drawList->AddRectFilled(levelMin, levelMax, getAudioLevelColor(audioLevel));
    }
    
    // Volume indicator (grey overlay, inverted)
    float volumeNormalized = (volumeDb - MIN_DB) / (MAX_DB - MIN_DB);
    volumeNormalized = std::max(0.0f, std::min(1.0f, volumeNormalized));
    float volumeFillWidth = canvasSize.x * (1.0f - volumeNormalized);
    ImVec2 volumeFillMin = ImVec2(canvasPos.x + canvasSize.x - volumeFillWidth, canvasPos.y);
    ImVec2 volumeFillMax = ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
    drawList->AddRectFilled(volumeFillMin, volumeFillMax, config.volumeFillColor);
    
    // Border
    drawList->AddRect(
        canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        config.borderColor
    );
    
    // Handle drag
    if (isActive && ImGui::IsMouseDragging(0)) {
        if (!vizState.isDragging) {
            vizState.startDrag(io.MousePos.y, volumeDb, 0);
        } else {
            float newVolumeDb = volumeDb;
            vizState.updateDrag(io.MousePos.y, newVolumeDb);
            
            // Clamp and convert back to linear
            newVolumeDb = std::max(MIN_DB, std::min(MAX_DB, newVolumeDb));
            float newVolume = dbToLinear(newVolumeDb);
            newVolume = ofClamp(newVolume, 0.0f, 1.0f);
            
            // Apply change
            onVolumeChanged(newVolume);
        }
    } else if (vizState.isDragging && !isActive) {
        vizState.endDrag();
    }
    
    // Visual feedback during drag
    if (vizState.isDragging) {
        drawList->AddRect(
            canvasPos,
            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
            IM_COL32(255, 255, 255, 100),
            0.0f, 0, 2.0f
        );
    }
    
    // Volume text overlay (dB value) - right aligned
    std::string dbText = formatDbText(volume, volumeDb);
    ImVec2 textSize = ImGui::CalcTextSize(dbText.c_str());
    ImVec2 textPos = ImVec2(
        canvasPos.x + canvasSize.x - textSize.x - 4.0f,
        canvasPos.y + (canvasSize.y - textSize.y) * 0.5f
    );
    drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), dbText.c_str());
    
    // Audio level text overlay (optional, for master volume)
    if (config.showAudioLevelText) {
        std::string levelText = formatAudioLevelText(audioLevel);
        ImVec2 levelTextSize = ImGui::CalcTextSize(levelText.c_str());
        ImVec2 levelTextPos = ImVec2(
            canvasPos.x + 4.0f,
            canvasPos.y + (canvasSize.y - levelTextSize.y) * 0.5f
        );
        drawList->AddText(levelTextPos, config.audioLevelTextColor, levelText.c_str());
    }
    
    // Advance cursor
    ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, canvasPos.y + canvasSize.y));
}

//--------------------------------------------------------------
// Public Visualization Functions
//--------------------------------------------------------------

void AudioOutputGUI::drawDraggableAudioViz(size_t connectionIndex, 
                                            float volume, 
                                            float audioLevel,
                                            float minDb, 
                                            float maxDb) {
    // Ensure we have state for this connection
    if (connectionIndex >= audioVizStates_.size()) {
        audioVizStates_.resize(connectionIndex + 1);
    }
    auto& vizState = audioVizStates_[connectionIndex];
    vizState.connectionIndex = connectionIndex;
    
    // Configure for connection rows (compact, darker)
    AudioVizConfig config;
    config.canvasSize = ImVec2(ImGui::GetContentRegionAvail().x, 22.0f);
    config.bgColor = IM_COL32(20, 20, 20, 255);
    config.borderColor = IM_COL32(100, 100, 100, 255);
    config.volumeFillColor = IM_COL32(150, 150, 150, 200);
    config.showAudioLevelText = false;
    
    // Use unified visualization function
    drawDraggableAudioVizInternal(
        "##audioViz_" + std::to_string(connectionIndex),
        volume,
        audioLevel,
        config,
        vizState,
        [this, connectionIndex](float newVolume) {
            AudioOutput* audioOutput = getAudioOutput();
            if (audioOutput) {
                audioOutput->setConnectionVolume(connectionIndex, newVolume);
                ParameterRouter* router = getParameterRouter();
                if (router) {
                    std::string paramName = "connectionVolume_" + std::to_string(connectionIndex);
                    router->notifyParameterChange(audioOutput, paramName, newVolume);
                }
            }
        }
    );
}

void AudioOutputGUI::DraggableAudioViz::startDrag(float startY, float startValue, size_t index) {
    isDragging = true;
    dragStartY = startY;
    dragStartValue = startValue;
    connectionIndex = index;
}

void AudioOutputGUI::DraggableAudioViz::updateDrag(float currentY, float& valueOut) {
    if (!isDragging) return;
    
    // Calculate drag delta (up = increase, down = decrease)
    float dragDelta = dragStartY - currentY; // Positive when dragging up
    
    // Convert pixel movement to dB change
    float dbDelta = dragDelta * AudioOutputGUI::DRAG_SENSITIVITY;
    
    valueOut = dragStartValue + dbDelta;
}

void AudioOutputGUI::DraggableAudioViz::endDrag() {
    isDragging = false;
    dragStartY = 0.0f;
    dragStartValue = 0.0f;
}

void AudioOutputGUI::drawDraggableMasterVolume(float volume, float audioLevel) {
    // Configure for master volume (larger, lighter, with audio level text)
    AudioVizConfig config;
    config.canvasSize = ImVec2(ImGui::GetContentRegionAvail().x, 30.0f);
    config.bgColor = IM_COL32(20, 20, 20, 255);
    config.borderColor = IM_COL32(255, 255, 255, 255);
    config.volumeFillColor = IM_COL32(150, 150, 150, 120);
    config.showAudioLevelText = true;
    config.audioLevelTextColor = IM_COL32(255, 255, 255, 200);
    
    // Use unified visualization function
    drawDraggableAudioVizInternal(
        "##masterVolumeViz",
        volume,
        audioLevel,
        config,
        masterVolumeVizState_,
        [this](float newVolume) {
            AudioOutput* audioOutput = getAudioOutput();
            if (audioOutput) {
                audioOutput->setMasterVolume(newVolume);
                ParameterRouter* router = getParameterRouter();
                if (router) {
                    router->notifyParameterChange(audioOutput, "masterVolume", newVolume);
                }
            }
        }
    );
}

//--------------------------------------------------------------
// GUI Factory Registration
//--------------------------------------------------------------
namespace {
    struct AudioOutputGUIRegistrar {
        AudioOutputGUIRegistrar() {
            GUIManager::registerGUIType("AudioOutput", 
                []() -> std::unique_ptr<ModuleGUI> {
                    return std::make_unique<AudioOutputGUI>();
                });
        }
    };
    static AudioOutputGUIRegistrar g_audioOutputGUIRegistrar;
}
