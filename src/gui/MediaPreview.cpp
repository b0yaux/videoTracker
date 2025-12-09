#include "MediaPreview.h"
#include "modules/MediaPlayer.h"
#include "ofxSoundObjects.h"
#include "ofxVisualObjects.h"
#include "ofImage.h"
#include "ofFileUtils.h"
#include <cmath>
#include <map>

namespace MediaPreview {
    
void drawWaveformPreview(MediaPlayer* player, float width, float height) {
    if (!player || !player->isAudioLoaded()) return;
    
    try {
        // CRITICAL: Verify audio player is actually ready before accessing buffer
        auto& audioPlayer = player->getAudioPlayer();
        if (!audioPlayer.isLoaded()) return;
        
        // Get audio buffer data (with safety check)
        // CRITICAL: Copy buffer immediately to avoid thread safety issues
        ofSoundBuffer buffer;
        try {
            buffer = audioPlayer.getBuffer();
        } catch (...) {
            // Buffer access failed - likely thread safety issue
            return;
        }
        
        int numFrames = buffer.getNumFrames();
        int numChannels = buffer.getNumChannels();
        
        if (numFrames <= 0 || numChannels <= 0) return;
        
        // Get ImDrawList for drawing
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (!drawList) return;  // Safety check
        
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
        
        // CRITICAL: Ensure we have at least 2 points to avoid division by zero
        if (actualPoints < 2) return;
        
        // Extract sample data with bounds checking
        std::vector<std::vector<float>> channelData(numChannels);
        for (int ch = 0; ch < numChannels; ch++) {
            channelData[ch].resize(actualPoints);
            for (int i = 0; i < actualPoints; i++) {
                int sampleIndex = i * stepSize;
                sampleIndex = std::max(0, std::min(numFrames - 1, sampleIndex));
                
                // CRITICAL: Bounds check before accessing buffer
                if (sampleIndex >= 0 && sampleIndex < numFrames && ch >= 0 && ch < numChannels) {
                    try {
                        channelData[ch][i] = buffer.getSample(sampleIndex, ch);
                    } catch (...) {
                        // Sample access failed - use 0.0 as fallback
                        channelData[ch][i] = 0.0f;
                    }
                } else {
                    channelData[ch][i] = 0.0f;
                }
            }
        }
        
        // Draw waveform
        float amplitudeScale = canvasHeight * WAVEFORM_AMPLITUDE_SCALE;
        ImU32 lineColor = GUIConstants::toU32(GUIConstants::Waveform::Line);
        
        // CRITICAL: actualPoints - 1 is guaranteed to be >= 1 (we checked actualPoints >= 2)
        float divisor = (float)(actualPoints - 1);
        if (divisor <= 0.0f) return;  // Extra safety check
        
        for (int ch = 0; ch < numChannels; ch++) {
            for (int i = 0; i < actualPoints - 1; i++) {
                // CRITICAL: Safe division - divisor is guaranteed >= 1
                float x1 = canvasPos.x + ((float)i / divisor) * canvasWidth;
                float y1 = centerY - channelData[ch][i] * amplitudeScale;
                float x2 = canvasPos.x + ((float)(i + 1) / divisor) * canvasWidth;
                float y2 = centerY - channelData[ch][i + 1] * amplitudeScale;
                
                // Validate coordinates before drawing
                if (std::isfinite(x1) && std::isfinite(y1) && std::isfinite(x2) && std::isfinite(y2)) {
                    drawList->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), lineColor, 1.0f);
                }
            }
        }
        
        // Advance cursor to account for the drawn waveform
        ImGui::Dummy(ImVec2(width, height));
    } catch (...) {
        // Silently handle any errors - don't crash on tooltip
        return;
    }
}

void drawWaveformPreview(const std::vector<float>& waveformData, float width, float height) {
    if (waveformData.empty()) return;
    
    try {
        int actualPoints = static_cast<int>(waveformData.size());
        
        // CRITICAL: Ensure we have at least 2 points to avoid division by zero
        if (actualPoints < 2) return;
        
        // Get ImDrawList for drawing
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (!drawList) return;  // Safety check
        
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasMax = ImVec2(canvasPos.x + width, canvasPos.y + height);
        float canvasWidth = width;
        float canvasHeight = height;
        float centerY = canvasPos.y + canvasHeight * 0.5f;
        
        // Draw background
        ImU32 bgColor = GUIConstants::toIM_COL32(GUIConstants::Background::Waveform);
        drawList->AddRectFilled(canvasPos, canvasMax, bgColor);
        
        // Draw waveform
        float amplitudeScale = canvasHeight * WAVEFORM_AMPLITUDE_SCALE;
        ImU32 lineColor = GUIConstants::toU32(GUIConstants::Waveform::Line);
        
        // CRITICAL: actualPoints - 1 is guaranteed to be >= 1 (we checked actualPoints >= 2)
        float divisor = (float)(actualPoints - 1);
        if (divisor <= 0.0f) return;  // Extra safety check
        
        for (int i = 0; i < actualPoints - 1; i++) {
            // CRITICAL: Safe division - divisor is guaranteed >= 1
            float x1 = canvasPos.x + ((float)i / divisor) * canvasWidth;
            float y1 = centerY - waveformData[i] * amplitudeScale;
            float x2 = canvasPos.x + ((float)(i + 1) / divisor) * canvasWidth;
            float y2 = centerY - waveformData[i + 1] * amplitudeScale;
            
            // Validate coordinates before drawing
            if (std::isfinite(x1) && std::isfinite(y1) && std::isfinite(x2) && std::isfinite(y2)) {
                drawList->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), lineColor, 1.0f);
            }
        }
        
        // Advance cursor to account for the drawn waveform
        ImGui::Dummy(ImVec2(width, height));
    } catch (...) {
        // Silently handle any errors - don't crash on tooltip
        return;
    }
}

float drawVideoThumbnail(MediaPlayer* player, float width) {
    if (!player || !player->isVideoLoaded()) return 0.0f;
    
    try {
        // CRITICAL: Verify video is actually ready before accessing texture
        auto& videoFile = player->getVideoPlayer().getVideoFile();
        if (!videoFile.isLoaded()) return 0.0f;
        
        // Get video texture (with safety check)
        ofTexture& tex = videoFile.getTexture();
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
    } catch (...) {
        // Silently handle any errors - don't crash on tooltip
        return 0.0f;
    }
}

float drawCachedVideoThumbnail(const std::string& thumbnailPath, float width) {
    if (thumbnailPath.empty() || !ofFile::doesFileExist(thumbnailPath)) {
        return 0.0f;
    }
    
    try {
        // Static cache for loaded thumbnails (persists across frames)
        static std::map<std::string, ofImage> thumbnailCache;
        
        // Check if already loaded
        auto it = thumbnailCache.find(thumbnailPath);
        if (it == thumbnailCache.end()) {
            // Load thumbnail
            ofImage thumbImage;
            if (thumbImage.load(thumbnailPath)) {
                thumbnailCache[thumbnailPath] = thumbImage;
                it = thumbnailCache.find(thumbnailPath);
            } else {
                return 0.0f;
            }
        }
        
        if (it != thumbnailCache.end()) {
            ofImage& thumbImage = it->second;
            if (thumbImage.isAllocated()) {
                // Calculate aspect ratio
                float aspectRatio = (float)thumbImage.getHeight() / (float)thumbImage.getWidth();
                if (aspectRatio <= 0.0f) aspectRatio = 9.0f / 16.0f; // Default 16:9
                
                float height = width * aspectRatio;
                
                // Display image
                ImVec2 uv0(0, 1);
                ImVec2 uv1(1, 0);
                ImGui::Image((void*)(intptr_t)thumbImage.getTexture().getTextureData().textureID,
                            ImVec2(width, height), uv0, uv1);
                
                return height;
            }
        }
    } catch (...) {
        // Silently handle errors - don't crash on tooltip
        return 0.0f;
    }
    
    return 0.0f;
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

