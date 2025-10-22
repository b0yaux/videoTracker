//
//  Clock.cpp
//
//  Clock wrapper for ofxTimeObjects - single source of truth for BPM
//

#include "Clock.h"
#include "ofxImGui.h"

//--------------------------------------------------------------
Clock::Clock() 
    : playing(false)
    , currentBpm(120.0f)
    , ticksPerBeat(4)
    , bpmSlider(120.0f)
    , lastBpmUpdate(0.0f)
    , bpmChangeThreshold(3.0f)
    , isDragging(false) {
}

//--------------------------------------------------------------
Clock::~Clock() {
    stop();
}

//--------------------------------------------------------------
void Clock::setup() {
    // Setup clock with default values
    clock.setBpm(currentBpm);
    clock.setTicksPerBeat(ticksPerBeat);
    
    // Setup internal tick listener that forwards to external listeners
    clock.addListener([this](const ofxTimeBuffer& tick) {
        if (tickListener) {
            tickListener(tick);
        }
    });
    
    ofLogNotice("Clock") << "Clock setup complete - BPM: " << currentBpm << ", Ticks per beat: " << ticksPerBeat;
}

//--------------------------------------------------------------
void Clock::setBPM(float bpm) {
    if (bpm != currentBpm && bpm > 0) {
        currentBpm = bpm;
        clock.setBpm(bpm);
        bpmSlider = bpm; // Sync GUI slider
        onBPMChanged();
    }
}

//--------------------------------------------------------------
float Clock::getBPM() const {
    return currentBpm;
}

//--------------------------------------------------------------
void Clock::setTicksPerBeat(int ticks) {
    if (ticks > 0 && ticks != ticksPerBeat) {
        ticksPerBeat = ticks;
        clock.setTicksPerBeat(ticks);
        ofLogNotice("Clock") << "Ticks per beat set to: " << ticks;
    }
}

//--------------------------------------------------------------
int Clock::getTicksPerBeat() const {
    return ticksPerBeat;
}

//--------------------------------------------------------------
void Clock::start() {
    if (!playing) {
        clock.start();
        playing = true;
        ofLogNotice("Clock") << "Clock started at BPM: " << currentBpm;
    }
}

//--------------------------------------------------------------
void Clock::stop() {
    if (playing) {
        clock.stop();
        playing = false;
        ofLogNotice("Clock") << "Clock stopped";
    }
}

//--------------------------------------------------------------
void Clock::pause() {
    if (playing) {
        clock.stop();
        playing = false;
        ofLogNotice("Clock") << "Clock paused";
    }
}

//--------------------------------------------------------------
void Clock::reset() {
    clock.reset();
    playing = false;
    ofLogNotice("Clock") << "Clock reset";
}

//--------------------------------------------------------------
bool Clock::isPlaying() const {
    return playing;
}

//--------------------------------------------------------------
void Clock::addTickListener(std::function<void(const ofxTimeBuffer&)> listener) {
    tickListener = listener;
}

//--------------------------------------------------------------
void Clock::removeTickListener() {
    tickListener = nullptr;
}

//--------------------------------------------------------------
void Clock::drawGUI() {
    // BPM control with proper debouncing
    if (ImGui::SliderFloat("BPM", &bpmSlider, 60.0f, 444.0f)) {
        isDragging = true;
        float currentTime = ofGetElapsedTimef();
        
        // Debouncing during playback to prevent timing disruption
        float debounceTime = playing ? 0.3f : 0.1f;
        
        if (currentTime - lastBpmUpdate > debounceTime && abs(bpmSlider - currentBpm) > bpmChangeThreshold) {
            setBPM(bpmSlider);
            lastBpmUpdate = currentTime;
            
            if (playing) {
                ofLogNotice("Clock") << "BPM changed during playback to: " << currentBpm;
            } else {
                ofLogNotice("Clock") << "BPM slider changed to: " << currentBpm;
            }
        }
    } else if (isDragging && !ImGui::IsItemActive()) {
        // User finished dragging, apply final value
        isDragging = false;
        setBPM(bpmSlider);
        ofLogNotice("Clock") << "BPM drag finished at: " << currentBpm;
    }
    
    ImGui::Separator();
    
    // Transport controls
    if (ImGui::Button(playing ? "Stop" : "Play")) {
        if (playing) {
            stop();
        } else {
            start();
        }
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        reset();
    }
    
    // Status display
    ImGui::Text("Status: %s", playing ? "Playing" : "Stopped");
    ImGui::Text("BPM: %.1f", currentBpm);
    ImGui::Text("Ticks per beat: %d", ticksPerBeat);
}

//--------------------------------------------------------------
void Clock::onBPMChanged() {
    // This method can be extended to notify other components
    // about BPM changes if needed in the future
}