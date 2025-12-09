#include "VideoOutputGUI.h"
#include "VideoOutput.h"
#include "core/ModuleRegistry.h"
#include "core/ParameterRouter.h"
#include "gui/ModuleGUI.h"
#include "gui/GUIManager.h"
#include "ofMain.h"
#include <imgui.h>
#include <algorithm>
#include <string>

VideoOutputGUI::VideoOutputGUI() {
    // Base class handles module reference setup
}

void VideoOutputGUI::draw() {
    // Use base class draw which handles visibility, ON/OFF toggle, etc.
    ModuleGUI::draw();
}

void VideoOutputGUI::drawContent() {
    VideoOutput* videoOutput = getVideoOutput();
    if (!videoOutput) {
        ImGui::Text("No VideoOutput module found");
        return;
    }
    
    // Draw output information in a table
    drawOutputInfo();
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Draw master controls in a table
    drawMasterControls();
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Draw connections (from mixer functionality)
    drawConnections();
}

VideoOutput* VideoOutputGUI::getVideoOutput() const {
    ModuleRegistry* reg = ModuleGUI::getRegistry();
    std::string instanceName = ModuleGUI::getInstanceName();
    if (reg && !instanceName.empty()) {
        auto module = reg->getModule(instanceName);
        if (!module) return nullptr;
        return dynamic_cast<VideoOutput*>(module.get());
    }
    return nullptr;
}

void VideoOutputGUI::drawOutputInfo() {
    VideoOutput* videoOutput = getVideoOutput();
    if (!videoOutput) return;

    float fps = ofGetFrameRate();
    int width = videoOutput->getViewportWidth();
    int height = videoOutput->getViewportHeight();
    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    
    // Create child window for output info table
    if (ImGui::BeginChild("OutputInfoChild", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None)) {
        // Create table for output information
        if (ImGui::BeginTable("OutputInfoTable", 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Output Info", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            
            // Row 1: FPS
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            
            // Determine color based on fps value
            ImVec4 color;
            if (fps < 30.0f) {
                color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // Red
            } else if (fps < 55.0f) {
                color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f); // Yellow
            } else {
                color = ImVec4(0.3f, 1.0f, 0.3f, 1.0f); // Green
            }
            
            ImGui::Text("FPS: ");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::Text("%.1f", fps);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::Text("(%.1f ms/frame)", 1000.0f / fps);
            
            // Row 2: Resolution
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Resolution: %d x %d", width, height);
            
            // Row 3: Aspect Ratio
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Aspect Ratio: %.2f:1", aspectRatio);
            
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

void VideoOutputGUI::drawMasterControls() {
    VideoOutput* videoOutput = getVideoOutput();
    if (!videoOutput) return;
    
    // Create child window for master controls table
    if (ImGui::BeginChild("MasterControlsChild", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None)) {
        // Create table for master controls
        if (ImGui::BeginTable("MasterControlsTable", 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Master Controls", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            
            // Row 1: Master opacity
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            float masterOpacity = videoOutput->getMasterOpacity();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##masterOpacity", &masterOpacity, 0.0f, 1.0f, "Opacity: %.2f")) {
                videoOutput->setMasterOpacity(masterOpacity);
                ParameterRouter* router = getParameterRouter();
                if (router) {
                    router->notifyParameterChange(videoOutput, "masterOpacity", masterOpacity);
                }
            }
            
            // Row 2: Auto-normalize toggle
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool autoNormalize = videoOutput->getAutoNormalize();
            if (ImGui::Checkbox("Auto Normalize", &autoNormalize)) {
                videoOutput->setAutoNormalize(autoNormalize);
                ParameterRouter* router = getParameterRouter();
                if (router) {
                    router->notifyParameterChange(videoOutput, "autoNormalize", autoNormalize ? 1.0f : 0.0f);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Automatically normalize opacity for ADD mode to prevent white-out");
            }
            
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

void VideoOutputGUI::drawConnections() {
    VideoOutput* videoOutput = getVideoOutput();
    if (!videoOutput) return;
    
    size_t numConnections = videoOutput->getNumConnections();
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
        
        // Build table header - enhanced with Blend Mode column for future per-source support
        // For now, blend mode column shows global mode (grayed out) as a preview
        // Use proportional widths with resizable columns, ensuring all columns stay visible
        if (ImGui::BeginTable("connections", 3, 
                              ImGuiTableFlags_Borders | 
                              ImGuiTableFlags_RowBg | 
                              ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_SizingStretchProp)) {
            // Source column - takes most space (weight 3), cannot be hidden to keep names visible
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 3.0f);
            // Blend Mode column - proportional (weight 1)
            ImGui::TableSetupColumn("Blend Mode", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            // Opacity column - proportional (weight 2)
            ImGui::TableSetupColumn("Opacity", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableHeadersRow();
            
            // OPTIMIZATION: Display in reverse order (top row = highest index = top layer)
            // This matches visual layer semantics: top row renders on top
            for (size_t displayIdx = 0; displayIdx < numConnections; displayIdx++) {
                // Convert display index to storage index (reverse mapping)
                size_t storageIdx = numConnections - 1 - displayIdx;
                
                ImGui::PushID(static_cast<int>(storageIdx)); // Use storage index for ID
                ImGui::TableNextRow();
                
                // Get human-readable name first (needed for drag preview)
                std::string displayName = "Connection " + std::to_string(storageIdx);
                if (reg) {
                    auto module = videoOutput->getSourceModule(storageIdx);
                    if (module) {
                        std::string humanName = reg->getName(module);
                        if (!humanName.empty()) {
                            displayName = humanName;
                        }
                    }
                }
                
                // Source column - make draggable and show name
                ImGui::TableSetColumnIndex(0);
                
                // Use Selectable styled as text to make it draggable
                // Style it to look like regular text (no selection highlight)
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0)); // Transparent when selected
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f)); // Slight highlight on hover
                ImGui::Selectable(displayName.c_str(), false, 0);
                ImGui::PopStyleColor(2);
                
                // Make the selectable draggable
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    // Store storage index in payload
                    ImGui::SetDragDropPayload("VIDEO_SOURCE_REORDER", &storageIdx, sizeof(size_t));
                    ImGui::Text("Moving: %s", displayName.c_str());
                    ImGui::EndDragDropSource();
                }
                
                // Make the row a drop target
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VIDEO_SOURCE_REORDER")) {
                        IM_ASSERT(payload->DataSize == sizeof(size_t));
                        size_t draggedStorageIdx = *(const size_t*)payload->Data;
                        size_t targetStorageIdx = storageIdx;
                        
                        // Only reorder if different
                        if (draggedStorageIdx != targetStorageIdx) {
                            videoOutput->reorderSource(draggedStorageIdx, targetStorageIdx);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                
                // Blend Mode column - interactive combo box per row
                ImGui::TableSetColumnIndex(1);
                // Set combo width to match column width
                ImGui::SetNextItemWidth(-1); // -1 means use remaining width (full column width)
                
                ofBlendMode sourceMode = videoOutput->getSourceBlendMode(storageIdx);
                int currentModeIndex = 0;
                if (sourceMode == OF_BLENDMODE_ADD) currentModeIndex = 0;
                else if (sourceMode == OF_BLENDMODE_MULTIPLY) currentModeIndex = 1;
                else if (sourceMode == OF_BLENDMODE_ALPHA) currentModeIndex = 2;
                
                const char* blendModes[] = { "Add", "Multiply", "Alpha" };
                std::string comboId = "##blendMode_" + std::to_string(storageIdx);
                
                if (ImGui::Combo(comboId.c_str(), &currentModeIndex, blendModes, IM_ARRAYSIZE(blendModes))) {
                    ofBlendMode newMode = OF_BLENDMODE_ADD;
                    if (currentModeIndex == 0) newMode = OF_BLENDMODE_ADD;
                    else if (currentModeIndex == 1) newMode = OF_BLENDMODE_MULTIPLY;
                    else if (currentModeIndex == 2) newMode = OF_BLENDMODE_ALPHA;
                    
                    videoOutput->setSourceBlendMode(storageIdx, newMode);
                    ParameterRouter* router = getParameterRouter();
                    if (router) {
                        std::string paramName = "connectionBlendMode_" + std::to_string(storageIdx);
                        router->notifyParameterChange(videoOutput, paramName, static_cast<float>(currentModeIndex));
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Per-source blend mode for this connection");
                }
                
                // Opacity column - draggable visualization (like AudioOutputGUI)
                ImGui::TableSetColumnIndex(2);
                float opacity = videoOutput->getSourceOpacity(storageIdx);
                drawDraggableOpacityViz(storageIdx, opacity);
                
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
// Draggable Opacity Visualization
//--------------------------------------------------------------

void VideoOutputGUI::drawDraggableOpacityViz(size_t sourceIndex, float opacity) {
    // Ensure we have state for this source
    if (sourceIndex >= opacityVizStates_.size()) {
        opacityVizStates_.resize(sourceIndex + 1);
    }
    auto& vizState = opacityVizStates_[sourceIndex];
    vizState.sourceIndex = sourceIndex;
    
    // Configure for source rows (compact, similar to AudioOutputGUI)
    OpacityVizConfig config;
    config.canvasSize = ImVec2(ImGui::GetContentRegionAvail().x, 22.0f);
    config.bgColor = IM_COL32(20, 20, 20, 255);
    config.borderColor = IM_COL32(100, 100, 100, 255);
    config.opacityFillColor = IM_COL32(150, 150, 150, 200);
    
    // Use unified visualization function
    drawDraggableOpacityVizInternal(
        "##opacityViz_" + std::to_string(sourceIndex),
        opacity,
        config,
        vizState,
        [this, sourceIndex](float newOpacity) {
            VideoOutput* videoOutput = getVideoOutput();
            if (videoOutput) {
                videoOutput->setSourceOpacity(sourceIndex, newOpacity);
                ParameterRouter* router = getParameterRouter();
                if (router) {
                    std::string paramName = "connectionOpacity_" + std::to_string(sourceIndex);
                    router->notifyParameterChange(videoOutput, paramName, newOpacity);
                }
            }
        }
    );
}

void VideoOutputGUI::drawDraggableOpacityVizInternal(
    const std::string& id,
    float opacity,
    const OpacityVizConfig& config,
    DraggableOpacityViz& vizState,
    std::function<void(float newOpacity)> onOpacityChanged) {
    
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
    
    // Opacity indicator (fill from right, inverted - like volume)
    // Higher opacity = less gray fill (more transparent gray overlay)
    float opacityFillWidth = canvasSize.x * (1.0f - opacity);
    ImVec2 opacityFillMin = ImVec2(canvasPos.x + canvasSize.x - opacityFillWidth, canvasPos.y);
    ImVec2 opacityFillMax = ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
    drawList->AddRectFilled(opacityFillMin, opacityFillMax, config.opacityFillColor);
    
    // Border
    drawList->AddRect(
        canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        config.borderColor
    );
    
    // Handle drag
    if (isActive && ImGui::IsMouseDragging(0)) {
        if (!vizState.isDragging) {
            vizState.startDrag(io.MousePos.y, opacity, 0);
        } else {
            float newOpacity = opacity;
            vizState.updateDrag(io.MousePos.y, newOpacity);
            
            // Clamp to valid range
            newOpacity = std::max(0.0f, std::min(1.0f, newOpacity));
            
            // Apply change
            onOpacityChanged(newOpacity);
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
    
    // Opacity text overlay (percentage) - right aligned
    std::string opacityText = formatOpacityText(opacity);
    ImVec2 textSize = ImGui::CalcTextSize(opacityText.c_str());
    ImVec2 textPos = ImVec2(
        canvasPos.x + canvasSize.x - textSize.x - 4.0f,
        canvasPos.y + (canvasSize.y - textSize.y) * 0.5f
    );
    drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), opacityText.c_str());
    
    // Advance cursor
    ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, canvasPos.y + canvasSize.y));
}

void VideoOutputGUI::DraggableOpacityViz::startDrag(float startY, float startValue, size_t index) {
    isDragging = true;
    dragStartY = startY;
    dragStartValue = startValue;
    sourceIndex = index;
}

void VideoOutputGUI::DraggableOpacityViz::updateDrag(float currentY, float& valueOut) {
    if (!isDragging) return;
    
    // Calculate drag delta (up = increase, down = decrease)
    float dragDelta = dragStartY - currentY; // Positive when dragging up
    
    // Convert pixel movement to opacity change (linear 0-1 range)
    float opacityDelta = dragDelta * VideoOutputGUI::DRAG_SENSITIVITY;
    
    valueOut = dragStartValue + opacityDelta;
}

void VideoOutputGUI::DraggableOpacityViz::endDrag() {
    isDragging = false;
    dragStartY = 0.0f;
    dragStartValue = 0.0f;
}

std::string VideoOutputGUI::formatOpacityText(float opacity) {
    char text[32];
    snprintf(text, sizeof(text), "%.0f%%", opacity * 100.0f);
    return std::string(text);
}

//--------------------------------------------------------------
// GUI Factory Registration
//--------------------------------------------------------------
namespace {
    struct VideoOutputGUIRegistrar {
        VideoOutputGUIRegistrar() {
            GUIManager::registerGUIType("VideoOutput", 
                []() -> std::unique_ptr<ModuleGUI> {
                    return std::make_unique<VideoOutputGUI>();
                });
        }
    };
    static VideoOutputGUIRegistrar g_videoOutputGUIRegistrar;
}
