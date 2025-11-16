#include "MediaPreview.h"
#include "MediaPlayer.h"
#include "ofxSoundObjects.h"
#include "ofxVisualObjects.h"

namespace MediaPreview {
    
void drawWaveformPreview(MediaPlayer* player, float width, float height) {
    if (!player || !player->isAudioLoaded()) return;
    
    // Get audio buffer data
    ofSoundBuffer buffer = player->getAudioPlayer().getBuffer();
    int numFrames = buffer.getNumFrames();
    int numChannels = buffer.getNumChannels();
    
    if (numFrames <= 0 || numChannels <= 0) return;
    
    // Get ImDrawList for drawing
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasMax = ImVec2(canvasPos.x + width, canvasPos.y + height);
    float canvasWidth = width;
    float canvasHeight = height;
    float centerY = canvasPos.y + canvasHeight * 0.5f;
    
    // Draw background
    ImU32 bgColor = GUIConstants::toIM_COL32(GUIConstants::Background::Waveform);
    drawList->AddRectFilled(canvasPos, canvasMax, bgColor);
    
    // Downsample audio data for preview
    const int maxPreviewPoints = MAX_TOOLTIP_WAVEFORM_POINTS;
    int stepSize = std::max(1, numFrames / maxPreviewPoints);
    int actualPoints = std::min(maxPreviewPoints, numFrames / stepSize);
    
    if (actualPoints < MIN_WAVEFORM_POINTS_FOR_DRAW) return;
    
    // Extract sample data
    std::vector<std::vector<float>> channelData(numChannels);
    for (int ch = 0; ch < numChannels; ch++) {
        channelData[ch].resize(actualPoints);
        for (int i = 0; i < actualPoints; i++) {
            int sampleIndex = i * stepSize;
            sampleIndex = std::max(0, std::min(numFrames - 1, sampleIndex));
            channelData[ch][i] = buffer.getSample(sampleIndex, ch);
        }
    }
    
    // Draw waveform
    float amplitudeScale = canvasHeight * WAVEFORM_AMPLITUDE_SCALE;
    ImU32 lineColor = GUIConstants::toU32(GUIConstants::Waveform::Line);
    
    for (int ch = 0; ch < numChannels; ch++) {
        for (int i = 0; i < actualPoints - 1; i++) {
            float x1 = canvasPos.x + ((float)i / (float)(actualPoints - 1)) * canvasWidth;
            float y1 = centerY - channelData[ch][i] * amplitudeScale;
            float x2 = canvasPos.x + ((float)(i + 1) / (float)(actualPoints - 1)) * canvasWidth;
            float y2 = centerY - channelData[ch][i + 1] * amplitudeScale;
            
            drawList->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), lineColor, 1.0f);
        }
    }
    
    // Advance cursor to account for the drawn waveform
    ImGui::Dummy(ImVec2(width, height));
}

float drawVideoThumbnail(MediaPlayer* player, float width) {
    if (!player || !player->isVideoLoaded()) return 0.0f;
    
    // Get video texture
    ofTexture& tex = player->getVideoPlayer().getVideoFile().getTexture();
    if (!tex.isAllocated()) return 0.0f;
    
    // Calculate aspect ratio from texture
    float aspectRatio = (float)tex.getHeight() / (float)tex.getWidth();
    if (aspectRatio <= 0.0f) aspectRatio = 9.0f / 16.0f; // Default 16:9
    
    float height = width * aspectRatio;
    
    // Display video frame
    ImVec2 uv0(0, 1);
    ImVec2 uv1(1, 0);
    ImGui::Image((void*)(intptr_t)tex.getTextureData().textureID, 
                ImVec2(width, height), uv0, uv1);
    
    return height;
}

void drawMediaTooltip(MediaPlayer* player, int index) {
    if (!player) return;
    
    ImGui::BeginTooltip();
    
    // Show video frame if available
    if (player->isVideoLoaded()) {
        float thumbnailHeight = drawVideoThumbnail(player, 160.0f);
        
        // Show audio waveform below video if audio is also loaded
        if (player->isAudioLoaded()) {
            ImGui::Spacing();
            drawWaveformPreview(player, 160.0f, 40.0f); // Compact waveform below video
        }
        
        // Show basic info
        if (index >= 0) {
            ImGui::Text("Index: %d", index);
        }
        ImGui::Text("Status: %s", player->isPlaying() ? "Playing" : "Stopped");
    } else if (player->isAudioLoaded()) {
        // Audio-only: show waveform preview
        drawWaveformPreview(player, 160.0f, 60.0f);
        
        // Show basic info
        if (index >= 0) {
            ImGui::Text("Index: %d", index);
        }
        ImGui::Text("Status: %s", player->isPlaying() ? "Playing" : "Stopped");
    } else {
        // No media loaded
        if (index >= 0) {
            ImGui::Text("Index: %d", index);
        }
        ImGui::Text("No media loaded");
    }
    
    ImGui::EndTooltip();
}

} // namespace MediaPreview

