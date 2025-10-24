#include "ClockGUI.h"
#include "ofxImGui.h"

ClockGUI::ClockGUI() {
}

void ClockGUI::draw(Clock& clock) {
    // Sync BPM slider with current BPM when not being dragged
    if (!isDragging) {
        bpmSlider = clock.getBPM();
    }
    
    // BPM control - apply changes immediately to prevent stopping issues
    if (ImGui::SliderFloat("BPM", &bpmSlider, clock.getMinBPM(), clock.getMaxBPM())) {
        isDragging = true;
        
        // Apply BPM changes immediately when slider moves
        if (abs(bpmSlider - clock.getBPM()) > 0.1f) { // Small threshold to avoid noise
            ofLogNotice("ClockGUI") << "BPM slider changed from " << clock.getBPM() << " to " << bpmSlider;
            clock.setBPM(bpmSlider);
            
            if (clock.isPlaying()) {
                ofLogNotice("ClockGUI") << "BPM changed during playback to: " << clock.getBPM();
            } else {
                ofLogNotice("ClockGUI") << "BPM slider changed to: " << clock.getBPM();
            }
        }
    } else if (isDragging && !ImGui::IsItemActive()) {
        // User finished dragging, ensure final value is applied
        isDragging = false;
        if (abs(bpmSlider - clock.getBPM()) > 0.1f) {
            ofLogNotice("ClockGUI") << "BPM drag finished, applying: " << bpmSlider;
            clock.setBPM(bpmSlider);
            ofLogNotice("ClockGUI") << "BPM drag finished at: " << clock.getBPM();
        }
    }
    
    // BPM Visualizer - simple pulsing circle
    ImGui::SameLine();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float size = 5.0f + (clock.getBeatPulse() * 5.0f);
    ImU32 color = clock.isPlaying() ? IM_COL32(clock.getBeatPulse() * 255, clock.getBeatPulse() * 255, clock.getBeatPulse() * 255, 255) : IM_COL32(0, 0, 0, 255);
    draw->AddCircleFilled(ImVec2(pos.x+9, pos.y+9), size, color);
    
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    
    // Transport controls
    if (ImGui::Button(clock.isPlaying() ? "Stop" : "Play")) {
        if (clock.isPlaying()) {
            clock.stop();
        } else {
            clock.start();
        }
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        clock.reset();
    }
}
