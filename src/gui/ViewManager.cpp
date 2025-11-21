#include "ViewManager.h"
#include "GUIConstants.h"
#include "GUIManager.h"
#include "Clock.h"
#include "ClockGUI.h"
#include "MediaPool.h"
#include "MediaPoolGUI.h"
#include "TrackerSequencer.h"
#include "TrackerSequencerGUI.h"
#include "FileBrowser.h"
#include "Console.h"
#include "ofxSoundObjects.h"
#include <imgui.h>
#include "ofSoundStream.h"
#include "ofLog.h"

ViewManager::ViewManager() {
}

// New instance-aware setup
void ViewManager::setup(
    Clock* clock_,
    ClockGUI* clockGUI_,
    ofxSoundOutput* audioOutput_,
    GUIManager* guiManager_,
    FileBrowser* fileBrowser_,
    Console* console_,
    ofSoundStream* soundStream_
) {
    clock = clock_;
    clockGUI = clockGUI_;
    audioOutput = audioOutput_;
    guiManager = guiManager_;
    fileBrowser = fileBrowser_;
    console = console_;
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
    }
    
    ofLogNotice("ViewManager") << "Setup complete with GUIManager";
}

// Legacy setup method (for backward compatibility)
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

/**
 * Main draw function - renders all panels
 * 
 * ViewManager's primary responsibility: coordinate panel rendering.
 * - Gets GUI objects from GUIManager (for module panels)
 * - Renders each panel based on current state
 * - Manages focus and visibility
 * 
 * Note: This is view-only. No business logic here.
 */
void ViewManager::draw() {
    // Draw panels - each panel will set focus if needed (before Begin())
    // We need to track panel changes, but update lastPanel AFTER drawing
    // so each panel can check if it should set focus
    Panel previousPanel = lastPanel;
    
    drawClockPanel(previousPanel);
    drawAudioOutputPanel(previousPanel);
    
    // Use new instance-aware methods if GUIManager is available
    if (guiManager) {
        drawTrackerPanels(previousPanel);
        drawMediaPoolPanels(previousPanel);
    } else {
        // Fallback to legacy single-instance methods
        drawTrackerPanel(previousPanel);
        drawMediaPoolPanel(previousPanel);
    }
    
    // Always draw FileBrowser (even when collapsed) so ImGui can save its layout state
    drawFileBrowserPanel(previousPanel);
    
    // Always draw Console (even when collapsed) so ImGui can save its layout state
    drawConsolePanel(previousPanel);
    
    // Update lastPanel after drawing (so next frame can detect changes)
    lastPanel = currentPanel;
}

void ViewManager::setFocusIfChanged() {
    // This method is no longer needed - focus setting is handled in draw()
    // Keeping it for compatibility but it does nothing
}

void ViewManager::drawWindowOutline() {
    // Skip drawing outline when window is collapsed to avoid accessing invalid window properties
    if (ImGui::IsWindowCollapsed()) {
        return;
    }
    
    // Use foreground draw list to draw on top of everything (including scrollbars)
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    
    // Get window rect in screen space (includes titlebar and all decorations)
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();
    
    // Validate window size is valid (not zero or negative)
    if (windowSize.x <= 0 || windowSize.y <= 0) {
        return;
    }
    
    // Calculate the full window rectangle
    ImVec2 min = windowPos;
    ImVec2 max = ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y);
    
    // Draw outline based on focus state
    if (ImGui::IsWindowFocused()) {
        // Draw focused outline (brighter, thicker)
        drawList->AddRect(min, max, GUIConstants::toU32(GUIConstants::Outline::Focus), 0.0f, 0, GUIConstants::Outline::FocusThickness);
    } else {
        // Draw unfocused outline (dimmer, thinner)
        drawList->AddRect(min, max, GUIConstants::toU32(GUIConstants::Outline::Unfocused), 0.0f, 0, GUIConstants::Outline::UnfocusedThickness);
    }
}

void ViewManager::drawClockPanel(Panel previousPanel) {
    if (clockGUI && clock) {
        // Set focus only when panel changed and this is the current panel
        // Must be called BEFORE Begin() for it to work
        if (currentPanel == Panel::CLOCK && currentPanel != previousPanel) {
            ImGui::SetNextWindowFocus();
        }
        
        // ImGui::Begin() returns false when window is collapsed
        // IMPORTANT: Always call End() even if Begin() returns false
        if (ImGui::Begin("Clock ")) {
            // Only draw content if window is not collapsed (to avoid accessing invalid window properties)
            if (!ImGui::IsWindowCollapsed()) {
                if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
                    navigateToPanel(Panel::CLOCK);
                }
                
                clockGUI->draw(*clock);
                drawWindowOutline();
            }
        }
        ImGui::End();  // Always call End() regardless of Begin() return value
    }
}

void ViewManager::drawAudioOutputPanel(Panel previousPanel) {
    // Set focus only when panel changed and this is the current panel
    // Must be called BEFORE Begin() for it to work
    if (currentPanel == Panel::AUDIO_OUTPUT && currentPanel != previousPanel) {
        ImGui::SetNextWindowFocus();
    }
    
    // ImGui::Begin() returns false when window is collapsed
    // IMPORTANT: Always call End() even if Begin() returns false
    if (ImGui::Begin("Audio Output")) {
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
                setupAudioStream();
                audioDeviceChanged = false;
            }
        }
        
        ImGui::SliderFloat("Volume", &globalVolume, 0.0f, 1.0f, "%.2f");
        
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, GUIConstants::Plot::Histogram);
        ImGui::ProgressBar(currentAudioLevel, ImVec2(-1, 0), "");
        ImGui::PopStyleColor();
        ImGui::Text("Level: %.3f", currentAudioLevel);
        
        drawWindowOutline();
    }
    ImGui::End();  // Always call End() regardless of Begin() return value
}

void ViewManager::drawTrackerPanel(Panel previousPanel) {
    if (trackerGUI && tracker) {
        // Set focus only when panel changed and this is the current panel
        // Must be called BEFORE Begin() for it to work
        if (currentPanel == Panel::TRACKER && currentPanel != previousPanel) {
            ImGui::SetNextWindowFocus();
        }
        
        // ImGui::Begin() returns false when window is collapsed
        // IMPORTANT: Always call End() even if Begin() returns false
        if (ImGui::Begin("Tracker Sequencer")) {
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
                navigateToPanel(Panel::TRACKER);
                trackerGUI->clearCellFocus();
            }
            
            trackerGUI->draw(*tracker);
            drawWindowOutline();
        }
        ImGui::End();  // Always call End() regardless of Begin() return value
    }
}

void ViewManager::drawMediaPoolPanel(Panel previousPanel) {
    if (mediaPoolGUI && mediaPool) {
        // Set focus only when panel changed and this is the current panel
        // Must be called BEFORE Begin() for it to work
        if (currentPanel == Panel::MEDIA_POOL && currentPanel != previousPanel) {
            ImGui::SetNextWindowFocus();
        }
        
        // ImGui::Begin() returns false when window is collapsed
        // IMPORTANT: Always call End() even if Begin() returns false
        if (ImGui::Begin("Media Pool")) {
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
                navigateToPanel(Panel::MEDIA_POOL);
            }
            
            mediaPoolGUI->draw();
            drawWindowOutline();
        }
        ImGui::End();  // Always call End() regardless of Begin() return value
    }
}

void ViewManager::drawTrackerPanels(Panel previousPanel) {
    if (!guiManager) return;
    
    // Get all visible TrackerSequencer instances
    auto visibleInstances = guiManager->getVisibleInstances(ModuleType::SEQUENCER);
    auto allTrackerGUIs = guiManager->getAllTrackerGUIs();
    
    // Draw each visible instance in its own window
    for (auto* trackerGUI : allTrackerGUIs) {
        if (!trackerGUI) continue;
        
        std::string instanceName = trackerGUI->getInstanceName();
        if (visibleInstances.find(instanceName) == visibleInstances.end()) {
            continue;  // Skip non-visible instances
        }
        
        // Create window title with instance name
        std::string windowTitle = instanceName;  // Use instance name as window title
        
        // Set focus only when panel changed and this is the current panel
        if (currentPanel == Panel::TRACKER && currentPanel != previousPanel) {
            // Focus first visible instance
            if (trackerGUI == allTrackerGUIs.front()) {
                ImGui::SetNextWindowFocus();
            }
        }
        
        // Setup window properties (applies default size if saved)
        trackerGUI->setupWindow();
        
        // Disable scrolling on main window - only child regions should scroll
        // Use native ImGui title bar - toggle button will be drawn on top of it
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | 
                                      ImGuiWindowFlags_NoScrollWithMouse;
        
        // ImGui::Begin() returns false when window is collapsed
        // IMPORTANT: Always call End() even if Begin() returns false
        if (ImGui::Begin(windowTitle.c_str(), nullptr, windowFlags)) {
            // Only draw content if window is not collapsed (to avoid accessing invalid window properties)
            if (!ImGui::IsWindowCollapsed()) {
                // Draw ON/OFF toggle button in ImGui's native title bar
                trackerGUI->drawTitleBarToggle();
                
                if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
                    navigateToPanel(Panel::TRACKER);
                    trackerGUI->clearCellFocus();
                }
                
                trackerGUI->draw();
                
                // Save layout if window was resized (only save size, not position)
                ImVec2 currentSize = ImGui::GetWindowSize();
                static std::map<std::string, ImVec2> previousSizes;
                std::string windowId = windowTitle;
                auto it = previousSizes.find(windowId);
                if (it == previousSizes.end() || it->second.x != currentSize.x || it->second.y != currentSize.y) {
                    if (it != previousSizes.end()) {
                        trackerGUI->saveDefaultLayout();
                    }
                    previousSizes[windowId] = currentSize;
                }
                
                drawWindowOutline();
            }
        }
        ImGui::End();  // Always call End() regardless of Begin() return value
    }
}

void ViewManager::drawMediaPoolPanels(Panel previousPanel) {
    if (!guiManager) return;
    
    // Get all visible MediaPool instances
    auto visibleInstances = guiManager->getVisibleInstances(ModuleType::INSTRUMENT);
    auto allMediaPoolGUIs = guiManager->getAllMediaPoolGUIs();
    
    // Draw each visible instance in its own window
    for (auto* mediaPoolGUI : allMediaPoolGUIs) {
        if (!mediaPoolGUI) continue;
        
        std::string instanceName = mediaPoolGUI->getInstanceName();
        if (visibleInstances.find(instanceName) == visibleInstances.end()) {
            continue;  // Skip non-visible instances
        }
        
        // Create window title with instance name
        std::string windowTitle = instanceName;  // Use instance name as window title
        
        // Set focus only when panel changed and this is the current panel
        if (currentPanel == Panel::MEDIA_POOL && currentPanel != previousPanel) {
            // Focus first visible instance
            if (mediaPoolGUI == allMediaPoolGUIs.front()) {
                ImGui::SetNextWindowFocus();
            }
        }
        
        // Setup window properties (applies default size if saved)
        mediaPoolGUI->setupWindow();
        
        // Disable scrolling on main window - only child regions should scroll
        // Use native ImGui title bar - toggle button will be drawn on top of it
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | 
                                      ImGuiWindowFlags_NoScrollWithMouse;
        
        // ImGui::Begin() returns false when window is collapsed
        // IMPORTANT: Always call End() even if Begin() returns false
        if (ImGui::Begin(windowTitle.c_str(), nullptr, windowFlags)) {
            // Only draw content if window is not collapsed (to avoid accessing invalid window properties)
            if (!ImGui::IsWindowCollapsed()) {
                // Draw ON/OFF toggle button in ImGui's native title bar
                mediaPoolGUI->drawTitleBarToggle();
                
                if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
                    navigateToPanel(Panel::MEDIA_POOL);
                }
                
                mediaPoolGUI->draw();
                
                // Save layout if window was resized (only save size, not position)
                ImVec2 currentSize = ImGui::GetWindowSize();
                static std::map<std::string, ImVec2> previousSizes;
                std::string windowId = windowTitle;
                auto it = previousSizes.find(windowId);
                if (it == previousSizes.end() || it->second.x != currentSize.x || it->second.y != currentSize.y) {
                    if (it != previousSizes.end()) {
                        mediaPoolGUI->saveDefaultLayout();
                    }
                    previousSizes[windowId] = currentSize;
                }
                
                drawWindowOutline();
            }
        }
        ImGui::End();  // Always call End() regardless of Begin() return value
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

void ViewManager::drawFileBrowserPanel(Panel previousPanel) {
    if (!fileBrowser) return;
    
    // Set focus only when panel changed and this is the current panel
    // Must be called BEFORE Begin() for it to work
    if (currentPanel == Panel::FILE_BROWSER && currentPanel != previousPanel) {
        ImGui::SetNextWindowFocus();
    }
    
    // Standard window flags for utility panel (no special title bar needed)
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | 
                                  ImGuiWindowFlags_NoScrollWithMouse;
    
    // ImGui::Begin() returns false when window is collapsed
    // IMPORTANT: Always call End() even if Begin() returns false
    if (ImGui::Begin("File Browser", nullptr, windowFlags)) {
        // Window is open - safe to use window functions
        bool isCollapsed = ImGui::IsWindowCollapsed();
        
        // Sync visibility state
        if (!isCollapsed && !fileBrowserVisible_) {
            fileBrowserVisible_ = true;
        }
        
        // Only draw content when not collapsed
        if (!isCollapsed) {
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
                navigateToPanel(Panel::FILE_BROWSER);
            }
            
            fileBrowser->draw();
            drawWindowOutline();
        }
    }
    ImGui::End();  // Always call End() regardless of Begin() return value
}

void ViewManager::drawConsolePanel(Panel previousPanel) {
    if (!console) return;
    
    // Sync Console's internal isOpen state with ViewManager's visibility BEFORE Begin()
    // Handle case where Console was toggled via Cmd+':' shortcut (bidirectional sync)
    if (console->isConsoleOpen() != consoleVisible_) {
        // ViewManager's state changed (e.g., via Cmd+':') - sync Console's internal state
        if (consoleVisible_) {
            console->open();
        } else {
            console->close();
        }
    }
    
    // Track visibility state changes to handle Cmd+':' toggle
    // When console becomes visible, bring it to front (but don't force expand/collapse)
    static bool lastConsoleVisible = false;
    bool visibilityChanged = (consoleVisible_ != lastConsoleVisible);
    
    if (visibilityChanged && consoleVisible_) {
        // Console just became visible - bring to front (user controls expand/collapse)
        ImGui::SetNextWindowFocus();
    }
    lastConsoleVisible = consoleVisible_;
    
    // Set focus only when panel changed and this is the current panel
    // Must be called BEFORE Begin() for it to work
    if (currentPanel == Panel::CONSOLE && currentPanel != previousPanel) {
        ImGui::SetNextWindowFocus();
    }
    
    // No special flags needed - Console handles its own styling and scrolling
    ImGuiWindowFlags windowFlags = 0;
    
    // ImGui::Begin() returns false when window is collapsed
    // IMPORTANT: Always call End() even if Begin() returns false
    bool* pOpen = nullptr;
    if (ImGui::Begin("Console", pOpen, windowFlags)) {
        // Window is open and not collapsed - safe to use window functions
        bool isCollapsed = ImGui::IsWindowCollapsed();
        
        // Sync visibility state
        if (!isCollapsed && !consoleVisible_) {
            consoleVisible_ = true;
            lastConsoleVisible = true;
            console->open();
        }
        
        // Only draw content when visible and not collapsed
        if (consoleVisible_ && !isCollapsed) {
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
                navigateToPanel(Panel::CONSOLE);
            }
            
            console->drawContent();
            drawWindowOutline();
        }
    }
    ImGui::End();  // Always call End() regardless of Begin() return value
}


