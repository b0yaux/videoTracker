#include "ViewManager.h"
#include "Clock.h"
#include "ClockGUI.h"
#include "MediaPool.h"
#include "MediaPoolGUI.h"
#include "TrackerSequencer.h"
#include "TrackerSequencerGUI.h"
#include "ofxSoundObjects.h"
#include "ofxImGui.h"
#include "ofSoundStream.h"
#include "ofLog.h"

ViewManager::ViewManager() {
}

void ViewManager::setup(
    Clock* clock_,
    ClockGUI* clockGUI_,
    ofxSoundOutput* audioOutput_,
    TrackerSequencer* tracker_,
    TrackerSequencerGUI* trackerGUI_,
    MediaPool* mediaPool_,
    MediaPoolGUI* mediaPoolGUI_,
    ofSoundStream* soundStream_
) {
    clock = clock_;
    clockGUI = clockGUI_;
    audioOutput = audioOutput_;
    tracker = tracker_;
    trackerGUI = trackerGUI_;
    mediaPool = mediaPool_;
    mediaPoolGUI = mediaPoolGUI_;
    soundStream = soundStream_;
    
    // Initialize audio devices
    if (soundStream) {
        audioDevices = soundStream->getDeviceList();
        
        // Find default output device
        for (size_t i = 0; i < audioDevices.size(); i++) {
            if (audioDevices[i].isDefaultOutput) {
                selectedAudioDevice = i;
                break;
            }
        }
        
        // Store listener for future device changes (will be set by ofApp after setup)
        // Initial setup will be done by ofApp after setting listener
    }

    ofLogNotice("ViewManager") << "Setup complete with " 
                               << PANEL_NAMES.size() << " panels";
}

// setAudioDeviceState() removed - audio state is now owned by ViewManager

void ViewManager::navigateToPanel(Panel panel) {
    if (panel < Panel::COUNT) {
        currentPanel = panel;
        ofLogNotice("ViewManager") << "Navigated to: " 
                                   << PANEL_NAMES[static_cast<int>(panel)];
    }
}

void ViewManager::nextPanel() {
    int next = (static_cast<int>(currentPanel) + 1) % static_cast<int>(Panel::COUNT);
    navigateToPanel(static_cast<Panel>(next));
}

void ViewManager::previousPanel() {
    int prev = (static_cast<int>(currentPanel) - 1 + static_cast<int>(Panel::COUNT)) 
             % static_cast<int>(Panel::COUNT);
    navigateToPanel(static_cast<Panel>(prev));
}

void ViewManager::handleMouseClick(int x, int y) {
    // This could be enhanced to detect which panel was clicked
    // For now, ImGui handles this automatically
}

const char* ViewManager::getCurrentPanelName() const {
    int idx = static_cast<int>(currentPanel);
    if (idx >= 0 && idx < PANEL_NAMES.size()) {
        return PANEL_NAMES[idx];
    }
    return "Unknown";
}

void ViewManager::draw() {
    // Draw panels - each panel will set focus if needed (before Begin())
    // We need to track panel changes, but update lastPanel AFTER drawing
    // so each panel can check if it should set focus
    Panel previousPanel = lastPanel;
    
    drawClockPanel(previousPanel);
    drawAudioOutputPanel(previousPanel);
    drawTrackerPanel(previousPanel);
    drawMediaPoolPanel(previousPanel);
    
    // Update lastPanel after drawing (so next frame can detect changes)
    lastPanel = currentPanel;
}

void ViewManager::setFocusIfChanged() {
    // This method is no longer needed - focus setting is handled in draw()
    // Keeping it for compatibility but it does nothing
}

void ViewManager::drawClockPanel(Panel previousPanel) {
    if (clockGUI && clock) {
        // Set focus only when panel changed and this is the current panel
        // Must be called BEFORE Begin() for it to work
        if (currentPanel == Panel::CLOCK && currentPanel != previousPanel) {
            ImGui::SetNextWindowFocus();
        }
        
        if (ImGui::Begin("Clock ")) {
            // Detect mouse click on panel background (not on widgets) to switch focus
            // Only switch if clicking on the window background, not on interactive widgets
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
                navigateToPanel(Panel::CLOCK);
            }
            
            // Clock controls
            clockGUI->draw(*clock);
            ImGui::End();
        }
    }
}

void ViewManager::drawAudioOutputPanel(Panel previousPanel) {
    // Set focus only when panel changed and this is the current panel
    // Must be called BEFORE Begin() for it to work
    if (currentPanel == Panel::AUDIO_OUTPUT && currentPanel != previousPanel) {
        ImGui::SetNextWindowFocus();
    }
    
    if (ImGui::Begin("Audio Output")) {
        // Detect mouse click on panel background (not on widgets) to switch focus
        // Only switch if clicking on the window background, not on interactive widgets
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
            navigateToPanel(Panel::AUDIO_OUTPUT);
        }
        
        // Audio device selection
        if (!audioDevices.empty()) {
            if (ImGui::Combo("Device", &selectedAudioDevice, [](void* data, int idx, const char** out_text) {
                auto* devices = static_cast<std::vector<ofSoundDevice>*>(data);
                if (idx >= 0 && idx < devices->size()) {
                    *out_text = (*devices)[idx].name.c_str();
                    return true;
                }
                return false;
            }, &audioDevices, audioDevices.size())) {
                audioDeviceChanged = true;
            }
            
            if (audioDeviceChanged && soundStream) {
                // Re-setup audio stream with new device, preserving listener
                setupAudioStream();
                audioDeviceChanged = false;
            }
        }
        
        // Volume control
        ImGui::SliderFloat("Volume", &globalVolume, 0.0f, 1.0f, "%.2f");
        
        // Audio level visualization
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
        ImGui::ProgressBar(currentAudioLevel, ImVec2(-1, 0), "");
            ImGui::PopStyleColor();
        ImGui::Text("Level: %.3f", currentAudioLevel);
        ImGui::End();
    }
}

void ViewManager::drawTrackerPanel(Panel previousPanel) {
    if (trackerGUI && tracker) {
        // Set focus only when panel changed and this is the current panel
        // Must be called BEFORE Begin() for it to work
        if (currentPanel == Panel::TRACKER && currentPanel != previousPanel) {
            ImGui::SetNextWindowFocus();
        }
        
        if (ImGui::Begin("Tracker Sequencer")) {
            // Detect mouse click on panel background (not on widgets) to switch focus
            // Only switch if clicking on the window background, not on interactive widgets
            // This allows clicking on tracker cells to work regardless of which panel is "current"
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
                navigateToPanel(Panel::TRACKER);
                // CRITICAL: Clear cell focus when clicking empty space to prevent auto-focus loop
                // This prevents ImGui from auto-focusing the first cell when clicking empty space
                tracker->clearCellFocus();
            }
            
            trackerGUI->draw(*tracker);
            ImGui::End();
        }
    }
}

void ViewManager::drawMediaPoolPanel(Panel previousPanel) {
    if (mediaPoolGUI && mediaPool) {
        // Set focus only when panel changed and this is the current panel
        // Must be called BEFORE Begin() for it to work
        if (currentPanel == Panel::MEDIA_POOL && currentPanel != previousPanel) {
            ImGui::SetNextWindowFocus();
        }
        
        if (ImGui::Begin("Media Pool")) {
            // Detect mouse click on panel background (not on widgets) to switch focus
            // Only switch if clicking on the window background, not on interactive widgets
            // This allows clicking on media pool items to work regardless of which panel is "current"
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
                navigateToPanel(Panel::MEDIA_POOL);
            }
            
            mediaPoolGUI->draw();  // Delegate to separate GUI
            ImGui::End();
        }
    }
}

void ViewManager::setupAudioStream(ofBaseApp* listener) {
    if (!soundStream || audioDevices.empty()) {
        ofLogError("ViewManager") << "Cannot setup audio stream: no soundStream or devices";
        return;
    }
    
    // Use provided listener or stored listener
    ofBaseApp* listenerToUse = listener ? listener : audioListener;
    
    // Close existing stream if open
    soundStream->close();
    
    // Setup audio stream with selected device
    ofSoundStreamSettings settings;
    if (listenerToUse) {
        settings.setOutListener(listenerToUse);
    }
    settings.sampleRate = 44100;
    settings.numOutputChannels = 2;
    settings.numInputChannels = 0;
    settings.bufferSize = 512;
    
    if (selectedAudioDevice >= 0 && selectedAudioDevice < (int)audioDevices.size()) {
        settings.setOutDevice(audioDevices[selectedAudioDevice]);
    }
    
    soundStream->setup(settings);
    ofLogNotice("ViewManager") << "Audio stream setup with device: " 
                               << (selectedAudioDevice < (int)audioDevices.size() 
                                   ? audioDevices[selectedAudioDevice].name 
                                   : "default");
}

