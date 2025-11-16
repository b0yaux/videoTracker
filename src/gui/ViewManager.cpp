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
#include "ofxImGui.h"
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

void ViewManager::drawFocusedWindowOutline(float thickness) {
    if (ImGui::IsWindowFocused()) {
        // Use foreground draw list to draw on top of everything (including scrollbars)
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        
        // Get window rect in screen space (includes titlebar and all decorations)
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 windowSize = ImGui::GetWindowSize();
        
        // Calculate the full window rectangle
        ImVec2 min = windowPos;
        ImVec2 max = ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y);
        
        // Draw rectangle outline around the entire window (including titlebar and scrollbars)
        // Using foreground draw list ensures it's drawn on top and not clipped
        drawList->AddRect(min, max, GUIConstants::toU32(GUIConstants::Outline::Focus), 0.0f, 0, thickness);
    }
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
            
            // Draw focus outline if this window is focused
            drawFocusedWindowOutline();
            
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
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, GUIConstants::Plot::Histogram);
        ImGui::ProgressBar(currentAudioLevel, ImVec2(-1, 0), "");
            ImGui::PopStyleColor();
        ImGui::Text("Level: %.3f", currentAudioLevel);
        
        // Draw focus outline if this window is focused
        drawFocusedWindowOutline();
        
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
                trackerGUI->clearCellFocus();
            }
            
            trackerGUI->draw(*tracker);
            
            // Draw focus outline if this window is focused
            drawFocusedWindowOutline();
            
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
            
            // Draw focus outline if this window is focused
            drawFocusedWindowOutline();
            
            ImGui::End();
        }
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
        
        // Disable scrolling on main window - only child regions should scroll
        // Use NoTitleBar so we can draw custom title bar with integrated ON/OFF toggle
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | 
                                      ImGuiWindowFlags_NoScrollWithMouse |
                                      ImGuiWindowFlags_NoTitleBar;
        
        if (ImGui::Begin(windowTitle.c_str(), nullptr, windowFlags)) {
            // Draw custom title bar with integrated ON/OFF toggle
            trackerGUI->drawCustomTitleBar();
            
            // Detect mouse click on panel background
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
                navigateToPanel(Panel::TRACKER);
                trackerGUI->clearCellFocus();
            }
            
            trackerGUI->draw();  // Calls ModuleGUI::draw() which draws content only
            
            // Draw focus outline if this window is focused
            drawFocusedWindowOutline();
            
            ImGui::End();
        }
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
        
        // Disable scrolling on main window - only child regions should scroll
        // Use NoTitleBar so we can draw custom title bar with integrated ON/OFF toggle
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | 
                                      ImGuiWindowFlags_NoScrollWithMouse |
                                      ImGuiWindowFlags_NoTitleBar;
        
        if (ImGui::Begin(windowTitle.c_str(), nullptr, windowFlags)) {
            // Draw custom title bar with integrated ON/OFF toggle
            mediaPoolGUI->drawCustomTitleBar();
            
            // Detect mouse click on panel background
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
                navigateToPanel(Panel::MEDIA_POOL);
            }
            
            mediaPoolGUI->draw();  // Calls ModuleGUI::draw() which draws content only
            
            // Draw focus outline if this window is focused
            drawFocusedWindowOutline();
            
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
    
    // Always call Begin() so ImGui can track window state even when collapsed
    if (ImGui::Begin("File Browser", nullptr, windowFlags)) {
        // Track last visibility state to only update collapse when it changes
        static bool lastFileBrowserVisible = false;
        if (fileBrowserVisible_ != lastFileBrowserVisible) {
            // Visibility state changed - update collapse state (after Begin())
            ImGui::SetWindowCollapsed(!fileBrowserVisible_, ImGuiCond_Always);
            lastFileBrowserVisible = fileBrowserVisible_;
        }
        
        // Sync visibility state with ImGui's window collapsed state
        // If user manually expands a collapsed window, update visibility to true
        bool isCollapsed = ImGui::IsWindowCollapsed();
        if (!isCollapsed && !fileBrowserVisible_) {
            // User manually expanded the window - sync our state
            fileBrowserVisible_ = true;
            lastFileBrowserVisible = true;
        }
        
        // Detect mouse click on panel background
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
            navigateToPanel(Panel::FILE_BROWSER);
        }
        
        // Draw panel content directly (FileBrowser is a utility panel, not a module)
        // Wrap in try-catch to ensure End() is always called even if draw() throws
        try {
            fileBrowser->draw();
        } catch (const std::exception& e) {
            ofLogError("ViewManager") << "Exception in fileBrowser->draw(): " << e.what();
        } catch (...) {
            ofLogError("ViewManager") << "Unknown exception in fileBrowser->draw()";
        }
        
        // Draw focus outline if this window is focused
        drawFocusedWindowOutline();
        
        ImGui::End();
    }
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
    
    // Standard window flags for utility panel (no special title bar needed)
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | 
                                  ImGuiWindowFlags_NoScrollWithMouse;
    
    // Always call Begin() so ImGui can track window state for docking
    // This allows the window to be docked and its layout to be user-customizable
    // We do NOT programmatically control collapse/expand - let user and ImGui handle it
    bool* pOpen = nullptr;  // Don't use close button - visibility controlled by ViewManager
    if (ImGui::Begin("Console", pOpen, windowFlags)) {
        // Sync Console's internal state with ViewManager (in case user manually expanded)
        // If user manually expands a collapsed window, consider it "visible"
        bool isCollapsed = ImGui::IsWindowCollapsed();
        if (!isCollapsed && !consoleVisible_) {
            // User manually expanded the window - sync our state
            consoleVisible_ = true;
            lastConsoleVisible = true;
            console->open();
        }
        
        // Detect mouse click on panel background
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
            navigateToPanel(Panel::CONSOLE);
        }
        
        // Only draw content if console is "visible" (colon key toggle)
        // Window is always drawn to maintain docking state
        if (consoleVisible_) {
            // Draw panel content (Console::drawContent() draws only the content, no Begin/End)
            // Wrap in try-catch to ensure End() is always called even if drawContent() throws
            try {
                console->drawContent();
            } catch (const std::exception& e) {
                ofLogError("ViewManager") << "Exception in console->drawContent(): " << e.what();
            } catch (...) {
                ofLogError("ViewManager") << "Unknown exception in console->drawContent()";
            }
        }
        // When hidden, window is collapsed but still tracked by ImGui for docking
        
        // Draw focus outline if this window is focused
        drawFocusedWindowOutline();
        
        ImGui::End();
    }
}

