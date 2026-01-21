#include "ClockGUI.h"
#include <imgui.h>
#include "gui/GUIConstants.h"
#include "core/Engine.h"
#include "core/Command.h"

ClockGUI::ClockGUI() : observerId_(0), stateNeedsUpdate_(false) {
}

void ClockGUI::draw(Clock& clock) {
    // CRITICAL FIX (Phase 7.9 Plan 6 Task 4): Use cached state with version verification
    // Verify state version matches engine version before using cached state
    // If stale, fallback to polling to ensure fresh state
    vt::EngineState state;
    if (stateNeedsUpdate_ && engine_) {
        // Use cached state (updated via subscription)
        state = cachedState_;
        
        // CRITICAL FIX: Verify cached state version matches engine version
        // If stale, fallback to polling to ensure fresh state
        uint64_t currentEngineVersion = engine_->getStateVersion();
        uint64_t cachedStateVersion = state.version;
        
        if (cachedStateVersion < currentEngineVersion) {
            // Cached state is stale - fallback to polling
            ofLogVerbose("ClockGUI") << "Cached state is stale (version: " << cachedStateVersion 
                                     << ", engine: " << currentEngineVersion 
                                     << ") - polling fresh state";
            state = engine_->getState();
        } else {
            // Cached state is current - use it
            stateNeedsUpdate_ = false;
        }
        
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                uint64_t currentVersion = engine_->getStateVersion();
                uint64_t stateVersion = state.version;
                int64_t versionDiff = (int64_t)stateVersion - (int64_t)currentVersion;
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"SYNC_DEBUG\",\"hypothesisId\":\"D\",\"location\":\"ClockGUI.cpp:draw\",\"message\":\"Using cached state\",\"data\":{\"stateVersion\":" << stateVersion << ",\"currentVersion\":" << currentVersion << ",\"versionDiff\":" << versionDiff << ",\"bpm\":" << state.transport.bpm << ",\"isPlaying\":" << (state.transport.isPlaying ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
    } else if (engine_) {
        // Fallback to polling if subscription not available (backward compatibility)
        state = engine_->getState();
        
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                uint64_t currentVersion = engine_->getStateVersion();
                uint64_t stateVersion = state.version;
                int64_t versionDiff = (int64_t)stateVersion - (int64_t)currentVersion;
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"SYNC_DEBUG\",\"hypothesisId\":\"D\",\"location\":\"ClockGUI.cpp:draw\",\"message\":\"Polling state\",\"data\":{\"stateVersion\":" << stateVersion << ",\"currentVersion\":" << currentVersion << ",\"versionDiff\":" << versionDiff << ",\"bpm\":" << state.transport.bpm << ",\"isPlaying\":" << (state.transport.isPlaying ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
    } else {
        // Fallback to direct clock reads if engine not available (shouldn't happen in normal operation)
        ofLogWarning("ClockGUI") << "Engine not available, using direct clock reads";
    }
    
    // Sync BPM slider with current BPM when not being dragged
    if (!isDragging) {
        if (engine_) {
            bpmSlider = state.transport.bpm;
        } else {
            bpmSlider = clock.getBPM();
        }
    }
    
    // BPM control - apply changes immediately to prevent stopping issues
    if (ImGui::SliderFloat("BPM", &bpmSlider, clock.getMinBPM(), clock.getMaxBPM())) {
        isDragging = true;
        
        // Apply BPM changes immediately when slider moves
        float currentBPM = engine_ ? state.transport.bpm : clock.getBPM();
        if (abs(bpmSlider - currentBPM) > 0.1f) { // Small threshold to avoid noise
            ofLogNotice("ClockGUI") << "BPM slider changed from " << currentBPM << " to " << bpmSlider;
            
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    uint64_t currentVersion = engine_ ? engine_->getStateVersion() : 0;
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"SYNC_DEBUG\",\"hypothesisId\":\"B,D\",\"location\":\"ClockGUI.cpp:draw\",\"message\":\"Enqueueing SetBPMCommand\",\"data\":{\"stateVersion\":" << currentVersion << ",\"bpmBefore\":" << currentBPM << ",\"bpmAfter\":" << bpmSlider << "},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            
            // CRITICAL FIX: Route BPM changes through command queue for thread safety and script sync
            if (engine_) {
                auto cmd = std::make_unique<vt::SetBPMCommand>(bpmSlider);
                if (!engine_->enqueueCommand(std::move(cmd))) {
                    ofLogWarning("ClockGUI") << "Failed to enqueue SetBPMCommand, falling back to direct call";
                    clock.setBPM(bpmSlider);
                }
            } else {
                // Fallback to direct call if engine not available
                clock.setBPM(bpmSlider);
            }
            
            if (engine_ && state.transport.isPlaying) {
                ofLogNotice("ClockGUI") << "BPM changed during playback to: " << state.transport.bpm;
            } else {
                ofLogNotice("ClockGUI") << "BPM slider changed to: " << (engine_ ? state.transport.bpm : clock.getBPM());
            }
        }
    } else if (isDragging && !ImGui::IsItemActive()) {
        // User finished dragging, ensure final value is applied
        isDragging = false;
        float currentBPM = engine_ ? state.transport.bpm : clock.getBPM();
        if (abs(bpmSlider - currentBPM) > 0.1f) {
            ofLogNotice("ClockGUI") << "BPM drag finished, applying: " << bpmSlider;
            // CRITICAL FIX: Route BPM changes through command queue for thread safety and script sync
            if (engine_) {
                auto cmd = std::make_unique<vt::SetBPMCommand>(bpmSlider);
                if (!engine_->enqueueCommand(std::move(cmd))) {
                    ofLogWarning("ClockGUI") << "Failed to enqueue SetBPMCommand, falling back to direct call";
                    clock.setBPM(bpmSlider);
                }
            } else {
                // Fallback to direct call if engine not available
                clock.setBPM(bpmSlider);
            }
            float finalBPM = engine_ ? state.transport.bpm : clock.getBPM();
            ofLogNotice("ClockGUI") << "BPM drag finished at: " << finalBPM;
        }
    }
    
    // BPM Visualizer - simple pulsing circle
    ImGui::SameLine();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float size = 5.0f + (clock.getBeatPulse() * 5.0f);
    ImU32 color;
    bool isPlaying = engine_ ? state.transport.isPlaying : clock.isPlaying();
    if (isPlaying) {
        ImVec4 pulseColor = GUIConstants::Clock::PulseBase;
        pulseColor.x *= clock.getBeatPulse();
        pulseColor.y *= clock.getBeatPulse();
        pulseColor.z *= clock.getBeatPulse();
        color = GUIConstants::toIM_COL32(pulseColor);
    } else {
        color = GUIConstants::toU32(GUIConstants::Clock::PulseStopped);
    }
    draw->AddCircleFilled(ImVec2(pos.x+9, pos.y+9), size, color);
    
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    
    // Transport controls
    // CRITICAL FIX: Route play/stop through command queue for thread safety and script sync
    if (ImGui::Button(isPlaying ? "Stop" : "Play")) {
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                uint64_t currentVersion = engine_ ? engine_->getStateVersion() : 0;
                std::string cmdType = isPlaying ? "StopTransportCommand" : "StartTransportCommand";
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"SYNC_DEBUG\",\"hypothesisId\":\"B,D\",\"location\":\"ClockGUI.cpp:draw\",\"message\":\"Play/Stop button clicked\",\"data\":{\"stateVersion\":" << currentVersion << ",\"command\":\"" << cmdType << "\",\"isPlayingBefore\":" << (isPlaying ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        
        if (engine_) {
            if (isPlaying) {
                auto cmd = std::make_unique<vt::StopTransportCommand>();
                if (!engine_->enqueueCommand(std::move(cmd))) {
                    // Fallback: execute immediately if queue is full (ensures state notifications)
                    ofLogWarning("ClockGUI") << "Command queue full, executing StopTransportCommand immediately";
                    auto fallbackCmd = std::make_unique<vt::StopTransportCommand>();
                    engine_->executeCommandImmediate(std::move(fallbackCmd));
                }
            } else {
                auto cmd = std::make_unique<vt::StartTransportCommand>();
                if (!engine_->enqueueCommand(std::move(cmd))) {
                    // Fallback: execute immediately if queue is full (ensures state notifications)
                    ofLogWarning("ClockGUI") << "Command queue full, executing StartTransportCommand immediately";
                    auto fallbackCmd = std::make_unique<vt::StartTransportCommand>();
                    engine_->executeCommandImmediate(std::move(fallbackCmd));
                }
            }
        } else {
            // Fallback to direct call if engine not available (shouldn't happen in normal operation)
            ofLogWarning("ClockGUI") << "Engine not available, using direct clock call";
            if (clock.isPlaying()) {
                clock.stop();
            } else {
                clock.start();
            }
        }
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        // CRITICAL FIX: Route reset through command queue for thread safety and script sync
        if (engine_) {
            auto cmd = std::make_unique<vt::ResetTransportCommand>();
            if (!engine_->enqueueCommand(std::move(cmd))) {
                ofLogWarning("ClockGUI") << "Failed to enqueue ResetTransportCommand, falling back to direct call";
                clock.reset();
            }
        } else {
            // Fallback to direct call if engine not available (shouldn't happen in normal operation)
            ofLogWarning("ClockGUI") << "Engine not available, using direct clock call";
            clock.reset();
        }
    }
}

ClockGUI::~ClockGUI() {
    unsubscribe();
}

void ClockGUI::setEngine(vt::Engine* engine) {
    // Unsubscribe from previous engine if needed
    if (observerId_ > 0 && engine_) {
        unsubscribe();
    }
    
    engine_ = engine;
    
    // Subscribe to state changes if engine is available
    if (engine_ && observerId_ == 0) {
        observerId_ = engine_->subscribe([this](const vt::EngineState& state) {
            // #region agent log
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    uint64_t currentVersion = engine_ ? engine_->getStateVersion() : 0;
                    uint64_t stateVersion = state.version;
                    int64_t versionDiff = (int64_t)stateVersion - (int64_t)currentVersion;
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"SYNC_DEBUG\",\"hypothesisId\":\"D\",\"location\":\"ClockGUI.cpp:observer\",\"message\":\"State cached in observer\",\"data\":{\"stateVersion\":" << stateVersion << ",\"currentVersion\":" << currentVersion << ",\"versionDiff\":" << versionDiff << ",\"bpm\":" << state.transport.bpm << ",\"isPlaying\":" << (state.transport.isPlaying ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            // #endregion
            
            // Update cached state and mark UI for update
            this->cachedState_ = state;
            this->stateNeedsUpdate_ = true;
            
            // Log state changes for debugging
            ofLogNotice("ClockGUI") << "State changed (BPM: " << state.transport.bpm 
                                   << ", Playing: " << (state.transport.isPlaying ? "true" : "false") << ")";
        });
        
        ofLogNotice("ClockGUI") << "Subscribed to state changes (ID: " << observerId_ << ")";
    }
}

void ClockGUI::unsubscribe() {
    if (observerId_ > 0 && engine_) {
        engine_->unsubscribe(observerId_);
        observerId_ = 0;
        ofLogNotice("ClockGUI") << "Unsubscribed from state changes";
    }
}
