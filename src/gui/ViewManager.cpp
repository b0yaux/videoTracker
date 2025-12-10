#include "ViewManager.h"
#include "GUIConstants.h"
#include "GUIManager.h"
#include "utils/Clock.h"
#include "gui/ClockGUI.h"
#include "FileBrowser.h"
#include "Console.h"
#include "CommandBar.h"
#include "AssetLibraryGUI.h"
#include "ofxSoundObjects.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "ofMain.h"
#include "ofLog.h"
#include <map>
#include <vector>
#include <algorithm>
#include <iomanip>

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
    CommandBar* commandBar_,
    AssetLibraryGUI* assetLibraryGUI_
) {
    clock = clock_;
    clockGUI = clockGUI_;
    audioOutput = audioOutput_;
    guiManager = guiManager_;
    fileBrowser = fileBrowser_;
    console = console_;
    commandBar = commandBar_;
    assetLibraryGUI = assetLibraryGUI_;

    ofLogNotice("ViewManager") << "Setup complete with GUIManager";
}

// setAudioDeviceState() removed - audio state is now owned by ViewManager

void ViewManager::navigateToWindow(const std::string& windowName) {
    // Generic navigation - works for ANY window by name (ALL GUI panels)
    currentFocusedWindow = windowName;
    ImGui::SetWindowFocus(windowName.c_str());
    ofLogNotice("ViewManager") << "Navigated to window: " << windowName;
}



void ViewManager::nextWindow() {
    std::string next = findWindowInDirection(currentFocusedWindow, 0); // 0 = right
    navigateToWindow(next.empty() ? findAlignedCycleWindow(currentFocusedWindow, 0) : next);
    ofLogNotice("ViewManager") << "Next window: " << getCurrentFocusedWindow();
}

void ViewManager::previousWindow() {
    std::string prev = findWindowInDirection(currentFocusedWindow, 1); // 1 = left
    navigateToWindow(prev.empty() ? findAlignedCycleWindow(currentFocusedWindow, 1) : prev);
    ofLogNotice("ViewManager") << "Previous window: " << getCurrentFocusedWindow();
}

void ViewManager::upWindow() {
    std::string up = findWindowInDirection(currentFocusedWindow, 3); // 3 = up
    navigateToWindow(up.empty() ? findAlignedCycleWindow(currentFocusedWindow, 3) : up);
    ofLogNotice("ViewManager") << "Up window: " << getCurrentFocusedWindow();
}

void ViewManager::downWindow() {
    std::string down = findWindowInDirection(currentFocusedWindow, 2); // 2 = down
    navigateToWindow(down.empty() ? findAlignedCycleWindow(currentFocusedWindow, 2) : down);
    ofLogNotice("ViewManager") << "Down window: " << getCurrentFocusedWindow();
}

std::vector<std::string> ViewManager::getAvailableWindows() const {
    std::vector<std::string> windows;

    // Helper lambda to check if a window actually exists and is visible
    auto isWindowVisible = [](const std::string& windowName) -> bool {
        ImGuiWindow* window = ImGui::FindWindowByName(windowName.c_str());
        return window && window->Active && !window->Hidden && window->WasActive;
    };

    // Add core windows only if they actually exist and are visible
    // Respect master modules visibility setting
    if (masterModulesVisible_) {
        std::vector<std::string> coreWindows = {
            "Clock ", "masterAudioOut", "masterVideoOut", "masterOscilloscope", "masterSpectrogram"
        };

        for (const auto& windowName : coreWindows) {
            if (isWindowVisible(windowName)) {
                windows.push_back(windowName);
            }
        }
    }

    // Add utility windows only if visible and exist
    if (fileBrowserVisible_ && isWindowVisible("File Browser")) {
        windows.push_back("File Browser");
    }
    if (consoleVisible_ && isWindowVisible("Console")) {
        windows.push_back("Console");
    }
    if (assetLibraryVisible_ && isWindowVisible("Asset Library")) {
        windows.push_back("Asset Library");
    }

    // Add all visible module instances (uses actual instance names, not hardcoded types)
    if (guiManager) {
        auto instances = guiManager->getAllInstanceNames();
        for (const auto& name : instances) {
            // Skip master modules if they're hidden
            if (!masterModulesVisible_ && (name == "masterAudioOut" || name == "masterVideoOut" || 
                name == "masterOscilloscope" || name == "masterSpectrogram")) {
                continue;
            }
            
            if (isWindowVisible(name)) {
                windows.push_back(name);
            }
        }
    }

    return windows;
}

std::string ViewManager::findWindowInDirection(const std::string& currentWindow, int direction) const {
    auto windows = getAvailableWindows();
    if (windows.empty()) return "";

    ImGuiWindow* current = ImGui::FindWindowByName(currentWindow.c_str());
    if (!current) return "";

    ImGuiWindow* closest = nullptr;
    float minDist = FLT_MAX;

    for (const auto& name : windows) {
        if (name == currentWindow) continue;
        ImGuiWindow* w = ImGui::FindWindowByName(name.c_str());
        if (!w || !w->Active) continue;

        float dx = w->Pos.x - current->Pos.x;
        float dy = w->Pos.y - current->Pos.y;

        // Calculate proximity to current window for better cycling behavior
        float proximity = sqrt(dx * dx + dy * dy);

        // Simple directional check
        bool correctDirection = (direction == 0 && dx > 0) ||  // right
                               (direction == 1 && dx < 0) ||  // left
                               (direction == 2 && dy > 0) ||  // down
                               (direction == 3 && dy < 0);    // up

        if (correctDirection) {
            // For up/down: prioritize same column (closest X), then distance
            // For left/right: prioritize same row (closest Y), then distance
            float alignmentDist = (direction <= 1) ? abs(dy) : abs(dx);  // row/column alignment
            float primaryDist = (direction <= 1) ? abs(dx) : abs(dy);    // primary movement distance

            float score = alignmentDist * 10 + primaryDist + proximity;  // alignment is 100x more important

            if (score < minDist) {
                minDist = score;
                closest = w;
            }
        }
    }

    return closest ? closest->Name : "";
}

std::string ViewManager::findAlignedCycleWindow(const std::string& currentWindow, int direction) const {
    auto windows = getAvailableWindows();
    if (windows.empty()) return "";

    ImGuiWindow* current = ImGui::FindWindowByName(currentWindow.c_str());
    if (!current) return "";

    ImGuiWindow* best = nullptr;
    float bestScore = FLT_MAX;

    for (const auto& name : windows) {
        if (name == currentWindow) continue;
        ImGuiWindow* w = ImGui::FindWindowByName(name.c_str());
        if (!w || !w->Active) continue;

        // Calculate alignment distance (how well aligned the windows are)
        float alignmentDist = (direction <= 1) ? abs(w->Pos.y - current->Pos.y) :  // horizontal: same row
                                                 abs(w->Pos.x - current->Pos.x);    // vertical: same column

        // Bias towards edge positions for cycling, but consider proximity
        float edgeScore = (direction == 0) ? w->Pos.x :      // right->leftmost
                         (direction == 1) ? -w->Pos.x :     // left->rightmost
                         (direction == 2) ? w->Pos.y :      // down->topmost
                                           -w->Pos.y;       // up->bottommost

        // Balanced scoring: prioritize alignment, then edge position, with proximity as tiebreaker
        float totalScore = alignmentDist * 20 + edgeScore * 40;

        if (totalScore < bestScore) {
            bestScore = totalScore;
            best = w;
        }
    }

    return best ? best->Name : "";
}

void ViewManager::handleMouseClick(int x, int y) {
    // This could be enhanced to detect which panel was clicked
    // For now, ImGui handles this automatically
}

const char* ViewManager::getCurrentWindowName() const {
    return currentFocusedWindow.c_str();
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
    // Track focus changes for this frame
    lastFocusedWindow = currentFocusedWindow;

    ofLogVerbose("ViewManager") << "draw() called";

    // Draw clock panel only if master modules are visible
    if (masterModulesVisible_) {
        drawClockPanel();
    }

    // Draw all visible module panels (generic - handles all module types)
    if (guiManager) {
        drawModulePanels();
    } else {
        ofLogWarning("ViewManager") << "drawModulePanels() skipped - guiManager is null";
    }

    // Draw utility panels only when visible (toggled ON in View menu)
    if (fileBrowserVisible_) {
        drawFileBrowserPanel();
    }

    if (consoleVisible_) {
        drawConsolePanel();
    }

    if (assetLibraryVisible_) {
        drawAssetLibraryPanel();
    }

    // Draw command bar (separate from console, triggered by Cmd+'=')
    if (commandBar && commandBar->isOpen()) {
        // Track visibility state changes to handle focus
        static bool lastCommandBarOpen = false;
        bool visibilityChanged = (commandBar->isOpen() != lastCommandBarOpen);

        if (visibilityChanged && commandBar->isOpen()) {
            // Command bar just opened - bring to front and ensure it's shown
            // Must be called BEFORE CommandPaletteWindow() which calls ImGui::Begin()
            ImGui::SetNextWindowFocus();
            ImGui::SetNextWindowCollapsed(false);
        }
        lastCommandBarOpen = commandBar->isOpen();

        commandBar->draw();
    }
}

void ViewManager::setFocusIfChanged() {
    // This method is no longer needed - focus setting is handled in draw()
    // Keeping it for compatibility but it does nothing
}



void ViewManager::drawModulePanels() {
    if (!guiManager) {
        ofLogWarning("ViewManager") << "drawModulePanels() called but guiManager is null";
        return;
    }

    // SAFE APPROACH: Get instance names instead of raw pointers
    // This prevents crashes from dangling pointers when GUIs are deleted
    auto instanceNames = guiManager->getAllInstanceNames();

    // Draw each visible GUI instance
    for (const auto& instanceName : instanceNames) {

        // SAFE: Look up GUI by name - returns nullptr if deleted
        auto* gui = guiManager->getGUI(instanceName);
        if (!gui) {
            continue;
        }

        // Skip if GUI doesn't have registry set (not fully initialized)
        if (!gui->getRegistry()) {
            continue;
        }

        // Only draw if instance is visible (visibility system handles all filtering)
        if (!guiManager->isInstanceVisible(instanceName)) {
            continue;
        }
        
        // Skip master modules if they are set to hidden
        if (!masterModulesVisible_ && (instanceName == "masterAudioOut" || instanceName == "masterVideoOut" || 
            instanceName == "masterOscilloscope" || instanceName == "masterSpectrogram")) {
            continue;
        }

        // Create window title with instance name
        std::string windowTitle = instanceName;

        // CRITICAL: Validate module still exists before accessing it
        // This prevents crashes when MediaPool modules with audio/video ports are deleted
        auto* reg = gui->getRegistry();
        if (!reg || !reg->hasModule(instanceName)) {
            // Module was deleted - skip this GUI
            continue;
        }

        // Setup window properties (applies default size if saved)
        // Note: setupWindow() may access registry, so ensure GUI is fully initialized
        try {
            gui->setupWindow();
        } catch (...) {
            // Skip this GUI if setup fails (not fully initialized)
            continue;
        }

        // Check if this window should be focused (by name match - works for ALL modules)
        bool shouldFocus = (windowTitle == currentFocusedWindow);
        bool focusChanged = (shouldFocus && windowTitle != lastFocusedWindow);

        // Set focus when window changes
        if (focusChanged) {
            ImGui::SetNextWindowFocus();
        }

        // Set border size and color based on focus state (native ImGui border system)
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);  // Enable border
        ImGui::PushStyleColor(ImGuiCol_Border,
            shouldFocus ? GUIConstants::Outline::Focus : GUIConstants::Outline::Unfocused);

        // Disable scrolling on main window
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse;

        // Prevent "hide tab bar" dropdown button in docked windows
        // This ensures tab bars always remain visible and serve as module title bars
        ImGuiWindowClass windowClass;
        windowClass.DockingAlwaysTabBar = true;
        ImGui::SetNextWindowClass(&windowClass);

        // ImGui::Begin() returns false when window is collapsed
        if (ImGui::Begin(windowTitle.c_str(), nullptr, windowFlags)) {
            if (!ImGui::IsWindowCollapsed()) {
                // Draw menu icon button in ImGui's native title bar
                try {
                    gui->drawTitleBarMenuIcon();
                } catch (...) {
                }
                
                // Draw ON/OFF toggle button in ImGui's native title bar
                try {
                    gui->drawTitleBarToggle();
                } catch (...) {
                }
                
                // Draw module popup menu (if open)
                try {
                    gui->drawModulePopup();
                } catch (...) {
                }

                // Handle navigation on click - navigate to this window (works for ALL modules)
                if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
                    navigateToWindow(windowTitle);
                    // Clear cell focus for modules that support it (e.g., TrackerSequencer)
                    try {
                        gui->clearCellFocus();
                    } catch (...) {
                    }
                }

                // Draw GUI content (may throw if not fully initialized)
                try {
                    float guiStartTime = ofGetElapsedTimef();
                    gui->draw();
                    float guiTime = (ofGetElapsedTimef() - guiStartTime) * 1000.0f;
                    // Log slow GUI windows (> 1ms)
                    if (guiTime > 1.0f) {
                        ofLogNotice("ViewManager") << "[PERF] Window '" << windowTitle << "' GUI: " 
                                                   << std::fixed << std::setprecision(2) << guiTime << "ms";
                    }
                } catch (...) {
                    // Continue to next GUI instead of crashing
                }

                // Draw outline for docked windows (native borders work for undocked)
                drawWindowOutline();

                // Save layout if window was resized
                ImVec2 currentSize = ImGui::GetWindowSize();
                static std::map<std::string, ImVec2> previousSizes;
                std::string windowId = windowTitle;
                auto it = previousSizes.find(windowId);
                if (it == previousSizes.end() || it->second.x != currentSize.x || it->second.y != currentSize.y) {
                    if (it != previousSizes.end()) {
                        try {
                            gui->saveDefaultLayout();
                        } catch (...) {
                        }
                    }
                    previousSizes[windowId] = currentSize;
                }
            }
        } else {
        }
        ImGui::End();  // Always call End() regardless of Begin() return value
        ImGui::PopStyleColor();  // Pop border color
        ImGui::PopStyleVar();    // Pop border size
    }

}

void ViewManager::drawWindowOutline() {
    // Hybrid approach: Native borders for undocked windows, manual drawing for docked windows
    // Skip drawing outline when window is collapsed
    if (ImGui::IsWindowCollapsed()) {
        return;
    }

    // Check if window is docked - use multiple methods for reliability
    bool isDocked = ImGui::IsWindowDocked();
    ImGuiID dockId = ImGui::GetWindowDockID();

    // Alternative check: if dockId is non-zero, window is docked
    if (!isDocked && dockId == 0) {
        // Window is not docked - native borders are handled by PushStyleVar/PushStyleColor
        return;
    }

    // DOCKED WINDOWS: Draw borders manually using foreground draw list
    // Use foreground draw list to ensure border is visible above all content
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (!drawList) {
        // Fallback to window draw list if foreground is not available
        drawList = ImGui::GetWindowDrawList();
        if (!drawList) {
            return;
        }
    }

    // Get window rectangle in screen space (full window including title bar and borders)
    // This ensures we draw on the actual window outline, not the content area
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();

    // Validate window size
    if (windowSize.x <= 0 || windowSize.y <= 0) {
        return;
    }

    // Calculate the full window rectangle (outer edge)
    ImVec2 min = windowPos;
    ImVec2 max = ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y);

    // Check focus state - we're inside the window context, so IsWindowFocused works correctly
    bool isFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow);

    // Draw border based on focus state - match native border appearance exactly
    if (isFocused) {
        // Draw focused outline (brighter, thicker) - matches native border
        drawList->AddRect(min, max, GUIConstants::toU32(GUIConstants::Outline::Focus),
                         0.0f, 0, GUIConstants::Outline::FocusThickness);
    } else {
        // Draw unfocused outline (dimmer, thinner) - matches native border
        drawList->AddRect(min, max, GUIConstants::toU32(GUIConstants::Outline::Unfocused),
                         0.0f, 0, GUIConstants::Outline::UnfocusedThickness);
    }
}

void ViewManager::drawClockPanel() {
    if (clockGUI && clock) {
        std::string windowName = "Clock ";
        bool shouldFocus = (windowName == currentFocusedWindow);
        bool focusChanged = (shouldFocus && windowName != lastFocusedWindow);

        // Set focus when window changes
        if (focusChanged) {
            ImGui::SetNextWindowFocus();
        }

        // Set border size and color based on focus state (native ImGui border system)
        bool isFocused = shouldFocus;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);  // Enable border
        ImGui::PushStyleColor(ImGuiCol_Border,
            isFocused ? GUIConstants::Outline::Focus : GUIConstants::Outline::Unfocused);

        // ImGui::Begin() returns false when window is collapsed
        // IMPORTANT: Always call End() even if Begin() returns false
        if (ImGui::Begin("Clock ")) {
            // Only draw content if window is not collapsed (to avoid accessing invalid window properties)
            if (!ImGui::IsWindowCollapsed()) {
                if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
                    navigateToWindow(windowName);
                }

                float clockStartTime = ofGetElapsedTimef();
                clockGUI->draw(*clock);
                float clockTime = (ofGetElapsedTimef() - clockStartTime) * 1000.0f;
                if (clockTime > 1.0f) {
                    ofLogNotice("ViewManager") << "[PERF] Window 'Clock' GUI: " 
                                              << std::fixed << std::setprecision(2) << clockTime << "ms";
                }

                // Draw outline for docked windows (native borders work for undocked)
                drawWindowOutline();
            }
        }
        ImGui::End();  // Always call End() regardless of Begin() return value
        ImGui::PopStyleColor();  // Pop border color
        ImGui::PopStyleVar();    // Pop border size
    }
}

// setupAudioStream() removed - audio device management is now handled by AudioOutputGUI

void ViewManager::drawAssetLibraryPanel() {
    if (!assetLibraryGUI) return;

    std::string windowName = "Asset Library";

    // This method is only called when assetLibraryVisible_ is true
    // Ensure window is shown (not collapsed) when visibility is true
    if (assetLibraryVisible_) {
        ImGui::SetNextWindowCollapsed(false);
    }

    bool shouldFocus = (windowName == currentFocusedWindow);
    bool focusChanged = (shouldFocus && windowName != lastFocusedWindow);

    // Set focus when window changes
    if (focusChanged) {
        ImGui::SetNextWindowFocus();
    }

    // Set border size and color based on focus state (native ImGui border system)
    bool isFocused = shouldFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);  // Enable border
    ImGui::PushStyleColor(ImGuiCol_Border,
        isFocused ? GUIConstants::Outline::Focus : GUIConstants::Outline::Unfocused);

    // Standard window flags for utility panel (no special title bar needed)
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse;

    // ImGui::Begin() returns false when window is collapsed
    // IMPORTANT: Always call End() even if Begin() returns false
    if (ImGui::Begin("Asset Library", nullptr, windowFlags)) {
        // Window is open - safe to use window functions
        bool isCollapsed = ImGui::IsWindowCollapsed();

        // Only draw content when not collapsed
        if (!isCollapsed) {
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
                navigateToWindow(windowName);
            }

            float assetStartTime = ofGetElapsedTimef();
            assetLibraryGUI->draw();
            float assetTime = (ofGetElapsedTimef() - assetStartTime) * 1000.0f;
            if (assetTime > 1.0f) {
                ofLogNotice("ViewManager") << "[PERF] Window 'Asset Library' GUI: " 
                                          << std::fixed << std::setprecision(2) << assetTime << "ms";
            }

            // Draw outline for docked windows (native borders work for undocked)
            drawWindowOutline();
        }
    }
    ImGui::End();  // Always call End() regardless of Begin() return value
    ImGui::PopStyleColor();  // Pop border color
    ImGui::PopStyleVar();    // Pop border size
}

//--------------------------------------------------------------
void ViewManager::drawFileBrowserPanel() {
    if (!fileBrowser) return;

    std::string windowName = "File Browser";

    // This method is only called when fileBrowserVisible_ is true
    // Ensure window is shown (not collapsed) when visibility is true
    if (fileBrowserVisible_) {
        ImGui::SetNextWindowCollapsed(false);
    }

    bool shouldFocus = (windowName == currentFocusedWindow);
    bool focusChanged = (shouldFocus && windowName != lastFocusedWindow);

    // Set focus when window changes
    if (focusChanged) {
        ImGui::SetNextWindowFocus();
    }

    // Set border size and color based on focus state (native ImGui border system)
    bool isFocused = shouldFocus;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);  // Enable border
        ImGui::PushStyleColor(ImGuiCol_Border,
            isFocused ? GUIConstants::Outline::Focus : GUIConstants::Outline::Unfocused);

        // Standard window flags for utility panel (no special title bar needed)
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse;

        // ImGui::Begin() returns false when window is collapsed
        // IMPORTANT: Always call End() even if Begin() returns false
        if (ImGui::Begin("File Browser", nullptr, windowFlags)) {
        // Window is open - safe to use window functions
        bool isCollapsed = ImGui::IsWindowCollapsed();

        // Only draw content when not collapsed
        if (!isCollapsed) {
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
                navigateToWindow(windowName);
            }

            float fileBrowserStartTime = ofGetElapsedTimef();
            fileBrowser->draw();
            float fileBrowserTime = (ofGetElapsedTimef() - fileBrowserStartTime) * 1000.0f;
            if (fileBrowserTime > 1.0f) {
                ofLogNotice("ViewManager") << "[PERF] Window 'File Browser' GUI: " 
                                          << std::fixed << std::setprecision(2) << fileBrowserTime << "ms";
            }

            // Draw outline for docked windows (native borders work for undocked)
            drawWindowOutline();
        }
    }
    ImGui::End();  // Always call End() regardless of Begin() return value
    ImGui::PopStyleColor();  // Pop border color
    ImGui::PopStyleVar();    // Pop border size
}

void ViewManager::drawConsolePanel() {
    if (!console) return;

    std::string windowName = "Console";

    // This method is only called when consoleVisible_ is true
    // Sync Console's internal isOpen state with ViewManager's visibility
    // Handle case where Console was toggled via Cmd+':' shortcut (bidirectional sync)
    if (console->isConsoleOpen() != consoleVisible_) {
        // ViewManager's state changed (e.g., via Cmd+':') - sync Console's internal state
        if (consoleVisible_) {
            console->open();
        } else {
            console->close();
        }
    }

    // Ensure Console is open when visible
    if (consoleVisible_ && !console->isConsoleOpen()) {
        console->open();
    }

    // Track visibility state changes to handle Cmd+':' toggle
    // When console becomes visible, bring it to front
    static bool lastConsoleVisible = false;
    bool visibilityChanged = (consoleVisible_ != lastConsoleVisible);

    if (visibilityChanged && consoleVisible_) {
        // Console just became visible - bring to front and ensure it's shown
        ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowCollapsed(false);
    }
    lastConsoleVisible = consoleVisible_;

    bool shouldFocus = (windowName == currentFocusedWindow);
    bool focusChanged = (shouldFocus && windowName != lastFocusedWindow);

    // Set focus when window changes
    if (focusChanged) {
        ImGui::SetNextWindowFocus();
    }

    // Ensure window is shown when visibility is true
    if (consoleVisible_) {
        ImGui::SetNextWindowCollapsed(false);
    }

    // Set border size and color based on focus state (native ImGui border system)
    bool isFocused = shouldFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);  // Enable border
    ImGui::PushStyleColor(ImGuiCol_Border,
        isFocused ? GUIConstants::Outline::Focus : GUIConstants::Outline::Unfocused);

    // No special flags needed - Console handles its own styling and scrolling
    ImGuiWindowFlags windowFlags = 0;

    // ImGui::Begin() returns false when window is collapsed
    // IMPORTANT: Always call End() even if Begin() returns false
    bool* pOpen = nullptr;
    if (ImGui::Begin("Console", pOpen, windowFlags)) {
        // Window is open and not collapsed - safe to use window functions
        bool isCollapsed = ImGui::IsWindowCollapsed();

        // Only draw content when visible and not collapsed
        if (consoleVisible_ && !isCollapsed) {
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
                navigateToWindow(windowName);
            }

            float consoleStartTime = ofGetElapsedTimef();
            console->drawContent();
            float consoleTime = (ofGetElapsedTimef() - consoleStartTime) * 1000.0f;
            if (consoleTime > 1.0f) {
                ofLogNotice("ViewManager") << "[PERF] Window 'Console' GUI: " 
                                          << std::fixed << std::setprecision(2) << consoleTime << "ms";
            }

            // Draw outline for docked windows (native borders work for undocked)
            drawWindowOutline();
        }
    }
    ImGui::End();  // Always call End() regardless of Begin() return value
    ImGui::PopStyleColor();  // Pop border color
    ImGui::PopStyleVar();    // Pop border size
}
