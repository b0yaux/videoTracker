#pragma once

#include "MediaPlayer.h"
#include "gui/GUIConstants.h"
#include "ofxImGui.h"

/**
 * MediaPreview - Shared utilities for media preview (waveform, video thumbnail)
 * 
 * Extracted from MediaPoolGUI for reuse in FileBrowser and other components
 */
namespace MediaPreview {
    
    // Waveform preview constants (matching MediaPoolGUI)
    static constexpr int MAX_TOOLTIP_WAVEFORM_POINTS = 600;
    static constexpr int MIN_WAVEFORM_POINTS_FOR_DRAW = 2;
    static constexpr float WAVEFORM_AMPLITUDE_SCALE = 0.4f;
    
    /**
     * Draw waveform preview for audio file
     * @param player MediaPlayer with loaded audio
     * @param width Width of waveform preview
     * @param height Height of waveform preview
     */
    void drawWaveformPreview(MediaPlayer* player, float width, float height);
    
    /**
     * Draw video thumbnail preview
     * @param player MediaPlayer with loaded video
     * @param width Width of thumbnail (height calculated from aspect ratio)
     * @return Height of thumbnail
     */
    float drawVideoThumbnail(MediaPlayer* player, float width);
    
    /**
     * Draw hover tooltip with media preview (video thumbnail + waveform)
     * Similar to MediaPoolGUI's tooltip preview
     * @param player MediaPlayer to preview
     * @param index Optional index to display
     */
    void drawMediaTooltip(MediaPlayer* player, int index = -1);
}

