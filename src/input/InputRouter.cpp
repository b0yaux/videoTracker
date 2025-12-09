#include "InputRouter.h"
#include "Clock.h"
#include "gui/ViewManager.h"
#include "gui/Console.h"
#include "gui/CommandBar.h"
#include "core/ModuleRegistry.h"
#include "gui/GUIManager.h"
#include "gui/ModuleGUI.h"
#include "core/SessionManager.h"
#include "core/ProjectManager.h"
#include "gui/AddMenu.h"
#include "AssetLibrary.h"
#include "Module.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "ofLog.h"
#include "ofFileUtils.h"

InputRouter::InputRouter() {
}

void InputRouter::setup(
    Clock* clock_,
    ModuleRegistry* registry_,
    GUIManager* guiManager_,
    ViewManager* viewManager_,
    Console* console_,
    CommandBar* commandBar_
) {
    clock = clock_;
    registry = registry_;
    guiManager = guiManager_;
    viewManager = viewManager_;
    console = console_;
    commandBar = commandBar_;

    ofLogNotice("InputRouter") << "Setup complete (InputRouter refactoring)";
}

void InputRouter::setAddMenu(AddMenu* addMenu_) {
    addMenu = addMenu_;
}

// Focus-based routing helpers (generic - no module-specific types)
ModuleGUI* InputRouter::getFocusedGUI() const {
    if (!guiManager || !registry) return nullptr;

    // SAFE APPROACH: Use getAllInstanceNames() + getGUI() to avoid dangling pointers
    // This prevents crashes when GUIs are deleted between frames
    auto instanceNames = guiManager->getAllInstanceNames();
    for (const auto& instanceName : instanceNames) {
        auto* gui = guiManager->getGUI(instanceName);
        if (gui && gui->isKeyboardFocused()) {
            return gui;
        }
    }

    return nullptr;
}

Module* InputRouter::getModuleForGUI(ModuleGUI* gui) const {
    if (!gui || !registry) return nullptr;

    std::string instanceName = gui->getInstanceName();
    auto module = registry->getModule(instanceName);
    return module ? module.get() : nullptr;
}


void InputRouter::setSessionCallbacks(
    std::function<void()> onSaveSession_,
    std::function<void()> onLoadSession_
) {
    onSaveSession = onSaveSession_;
    onLoadSession = onLoadSession_;
}

void InputRouter::setFileMenuCallbacks(
    std::function<void()> onSave_,
    std::function<void()> onSaveAs_,
    std::function<void()> onOpen_,
    std::function<void()> onOpenRecent_
) {
    onSave = onSave_;
    onSaveAs = onSaveAs_;
    onOpen = onOpen_;
    onOpenRecent = onOpenRecent_;
}

// Note: setPlayState() removed - play state now comes directly from Clock reference
// Clock is the single source of truth for transport state

void InputRouter::setCurrentStep(int* currentStep_) {
    currentStep = currentStep_;
}

void InputRouter::setLastTriggeredStep(int* lastTriggeredStep_) {
    lastTriggeredStep = lastTriggeredStep_;
}

void InputRouter::setShowGUI(bool* showGUI_) {
    showGUI = showGUI_;
}

void InputRouter::update() {
    // OLD: Tab/Shift+Tab panel navigation (replaced with Cmd+Left/Right Arrow)
    // Tab navigation conflicted with ImGui defaults and was inconsistent
    // New system: Use Cmd+Left/Right Arrow keys for lightweight window cycling
    // Implementation: See handleKeyPress() for Cmd+Arrow navigation

    // No active logic needed here - window navigation now handled in handleKeyPress()
}

bool InputRouter::handleKeyPress(ofKeyEventArgs& keyEvent) {
    int key = keyEvent.key;
    int keycode = keyEvent.keycode;
    int scancode = keyEvent.scancode;

    // Extract modifiers once at the top
    bool ctrlPressed = keyEvent.hasModifier(OF_KEY_CONTROL);
    bool shiftPressed = keyEvent.hasModifier(OF_KEY_SHIFT);
    bool cmdPressed = keyEvent.hasModifier(OF_KEY_COMMAND);
    bool altPressed = keyEvent.hasModifier(OF_KEY_ALT);



    // Priority 0: File menu shortcuts (Cmd+S, Cmd+Shift+S, Cmd+O, Cmd+Shift+O)
    if (cmdPressed) {
        if (key == 's' || key == 'S') {
            if (shiftPressed && onSaveAs) {
                // Cmd+Shift+S: Save As
                onSaveAs();
                logKeyPress(key, "Global: Cmd+Shift+S Save As");
                return true;
            } else if (onSave) {
                // Cmd+S: Save
                onSave();
                logKeyPress(key, "Global: Cmd+S Save");
                return true;
            }
        }

        if (key == 'o' || key == 'O') {
            if (shiftPressed && onOpen) {
                // Cmd+Shift+O: Open dialog
                onOpen();
                logKeyPress(key, "Global: Cmd+Shift+O Open");
                return true;
            } else if (onOpenRecent) {
                // Cmd+O: Open Recent (could show popup, for now just log)
                // The menu will show recent sessions when clicked
                logKeyPress(key, "Global: Cmd+O Open Recent (use menu)");
                return true;
            }
        }
    }

    // Debug: Log Cmd+':' attempts (before check to see what we're receiving)
    if (cmdPressed && (key == ':' || key == 58 || keycode == 59)) {
        ofLogNotice("InputRouter") << "[COLON_DEBUG] Cmd+':' detected: key=" << key
                                    << ", keycode=" << keycode
                                    << ", scancode=" << scancode
                                    << ", shift=" << shiftPressed
                                    << ", isColonKey=" << (key == ':' || key == 58 || (keycode == 59 && shiftPressed));
    }

    // Priority 0.5: Cmd+':' - Toggle Console (global shortcut, works everywhere)
    // On macOS, ':' is Shift+';' (semicolon), so we need to check for semicolon keycode (59) with Shift
    // Also check direct ':' character (ASCII 58) and keycode 59 (semicolon)
    bool isColonKey = (key == ':' || key == 58 || (keycode == 59 && shiftPressed));
    if (isColonKey && cmdPressed && viewManager) {
        bool visible = viewManager->isConsoleVisible();
        viewManager->setConsoleVisible(!visible);

        // Sync Console's internal state and navigate to console window when showing
        if (!visible && console) {
            console->open();
            // Navigate to console window using window name
            viewManager->navigateToWindow("Console");
        } else if (visible && console) {
            console->close();
        }

        logKeyPress(key, "Global: Cmd+':' Toggle Console");
        return true; // Consume the key
    }

    // Priority 0.5: Cmd+L - Toggle Asset Library (global shortcut, works everywhere)
    if (cmdPressed && (key == 'l' || key == 'L') && viewManager) {
        bool visible = viewManager->isAssetLibraryVisible();
        viewManager->setAssetLibraryVisible(!visible);

        // Navigate to Asset Library window when showing
        if (!visible) {
            viewManager->navigateToWindow("Asset Library");
        }

        logKeyPress(key, "Global: Cmd+L Toggle Asset Library");
        return true; // Consume the key
    }

    // Priority 0.5: Cmd+B - Toggle File Browser (global shortcut, works everywhere)
    if (cmdPressed && (key == 'b' || key == 'B') && viewManager) {
        bool visible = viewManager->isFileBrowserVisible();
        viewManager->setFileBrowserVisible(!visible);

        // Navigate to File Browser window when showing
        if (!visible) {
            viewManager->navigateToWindow("File Browser");
        }

        logKeyPress(key, "Global: Cmd+B Toggle File Browser");
        return true; // Consume the key
    }

    // Priority 0.5: Alt+Shift+M - Toggle Master Modules (clock + master outputs) (global shortcut, works everywhere)
    // Priority 0.5: Alt+Shift+M - Toggle Master Modules (clock + master outputs) (global shortcut, works everywhere)
    // Use scancode 41 for M key position to work with AZERTY keyboards and modifiers
    if (altPressed && scancode == 41 && viewManager) {
        bool visible = viewManager->isMasterModulesVisible();
        viewManager->setMasterModulesVisible(!visible);

        // Navigate to Clock window when showing master modules
        if (!visible) {
            viewManager->navigateToWindow("Clock ");
        }

        logKeyPress(key, "Global: Alt+Shift+M Toggle Master Modules");
        return true; // Consume the key
    }

    // Priority 0.5: Cmd+'=' - Toggle Command Bar (global shortcut, works everywhere)
    if (cmdPressed && (key == '=' || key == '+')) {
        if (commandBar) {
            commandBar->toggle();
            logKeyPress(key, "Global: Cmd+'=' Toggle Command Bar");
            return true; // Consume the key
        }
    }

    // Priority 0.5: Shift+A - Open Add Menu (global shortcut, works everywhere)
    if (shiftPressed && (key == 'A' || key == 'a')) {
        if (addMenu) {
            // Get current mouse position for menu placement
            ImGuiIO& io = ImGui::GetIO();
            addMenu->open(io.MousePos.x, io.MousePos.y);
            logKeyPress(key, "Global: Shift+A Open Add Menu");
            return true; // Consume the key
        }
    }

    // Priority 1: Window Navigation - Ctrl+Arrow Keys or Cmd+Arrow Keys (spatial navigation)
    // Support both Ctrl (cross-platform) and Cmd (macOS) modifiers
    // Note: We use ImGui's AddKeyEvent with false to prevent ImGui from processing the same
    // arrow keys for its internal navigation, avoiding conflicts between window navigation
    // and ImGui's keyboard navigation within windows
    if ((ctrlPressed || cmdPressed) && viewManager) {
        if (key == OF_KEY_LEFT) {
            // Prevent ImGui from processing this arrow key
            ImGuiIO& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiKey_LeftArrow, false);

            viewManager->previousWindow();
            logKeyPress(key, "Navigation: Ctrl/Cmd+Left Arrow - Previous Window");
            return true;
        }
        if (key == OF_KEY_RIGHT) {
            // Prevent ImGui from processing this arrow key
            ImGuiIO& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiKey_RightArrow, false);

            viewManager->nextWindow();
            logKeyPress(key, "Navigation: Ctrl/Cmd+Right Arrow - Next Window");
            return true;
        }
        if (key == OF_KEY_UP) {
            // Prevent ImGui from processing this arrow key
            ImGuiIO& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiKey_UpArrow, false);

            viewManager->upWindow();
            logKeyPress(key, "Navigation: Ctrl/Cmd+Up Arrow - Up Window");
            return true;
        }
        if (key == OF_KEY_DOWN) {
            // Prevent ImGui from processing this arrow key
            ImGuiIO& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiKey_DownArrow, false);

            viewManager->downWindow();
            logKeyPress(key, "Navigation: Ctrl/Cmd+Down Arrow - Down Window");
            return true;
        }
    }

    // Priority 0.5: Console arrow keys for history navigation (before other handlers consume them)
    // Only handle if console is visible and input text is focused
    if (console && viewManager && viewManager->isConsoleVisible() && console->isConsoleOpen() &&
        (key == OF_KEY_UP || key == OF_KEY_DOWN)) {
        if (console->handleArrowKeys(key)) {
            logKeyPress(key, "Console: Arrow key history navigation");
            return true; // Console consumed the arrow key
        }
    }

    // Ctrl+Tab is now handled by ImGui natively for window/panel navigation
    // No custom handling needed - let ImGui process it

    updateImGuiCaptureState();

    // Priority 2: Spacebar - ALWAYS works (global transport control)
    // Handle spacebar BEFORE other checks to ensure it always works
    // EXCEPT when console input is focused (user is typing commands)
    if (key == ' ') {
        // Don't handle spacebar if console input is focused (let user type spaces)
        if (console && viewManager && viewManager->isConsoleVisible() && console->isConsoleOpen() &&
            console->isInputTextFocused()) {
            // Console input is focused - let ImGui handle spacebar for text input
            return false;  // Don't consume, let it pass through to ImGui
        }

        // Regular Spacebar: Play/Stop (always works, even when ImGui has focus)
        if (handleGlobalShortcuts(key)) {
            return true;
        }
    }

    // Priority 3: Other global shortcuts - only when ImGui isn't busy
    if (!ImGui::IsAnyItemActive() && !ImGui::GetIO().WantCaptureMouse) {
        if (handleGlobalShortcuts(key)) {
            return true;
        }
    }

    // Priority 4: Generic GUI input routing - route to focused GUI
    // Let modules handle their own shortcuts via handleKeyPress()
    auto focusedGUI = getFocusedGUI();
    if (focusedGUI && focusedGUI->isKeyboardFocused()) {
        // Delegate to GUI's generic handleKeyPress method
        // Modules handle their own shortcuts internally
        bool handled = focusedGUI->handleKeyPress(key, ctrlPressed, shiftPressed);
        if (handled) {
            return true;
        }
        // If GUI didn't handle it, let ImGui process it (for navigation)
        return false;
    }

    return false;
}

bool InputRouter::handleGlobalShortcuts(int key) {
    // Global shortcuts work even when ImGui has focus

    switch (key) {
        case ' ':  // SPACE - Play/Stop (always works, even when ImGui has focus)
            if (clock) {
                // Use Clock as single source of truth for transport state
                bool currentlyPlaying = clock->isPlaying();
                if (currentlyPlaying) {
                    clock->stop();
                    logKeyPress(key, "Global: Stop");
                } else {
                    clock->start();
                    logKeyPress(key, "Global: Start");
                }
                return true;  // Always return true to prevent ImGui from processing spacebar
            }
            break;

        case 'g':
        case 'G':  // G - Toggle GUI
            if (showGUI) {
                *showGUI = !*showGUI;
                logKeyPress(key, "Global: Toggle GUI");
                return true;
            }
            break;

        case 'S':  // S - Save session (capital S to distinguish from speed)
            if (onSaveSession) {
                onSaveSession();
                logKeyPress(key, "Global: Save session");
                return true;
            }
            break;
    }

    return false;
}

// Removed handlePanelNavigation - Ctrl+Tab is now handled by ImGui natively
// Removed handleTrackerInput - modules handle their own input via handleKeyPress()

void InputRouter::updateImGuiCaptureState() {
    ImGuiIO& io = ImGui::GetIO();
    imGuiCapturingKeyboard = io.WantCaptureKeyboard;
}

bool InputRouter::isImGuiCapturingKeyboard() const {
    return imGuiCapturingKeyboard;
}

// Removed isSequencerInEditMode() - module-specific, not needed in generic router
// Removed syncEditStateFromImGuiFocus() - modules handle their own state sync

void InputRouter::logKeyPress(int key, const char* context) {
    ofLogVerbose("InputRouter") << context << " - Key: " << key;
}

//--------------------------------------------------------------
void InputRouter::handleDragEvent(ofDragInfo dragInfo, AssetLibrary* assetLibrary, ProjectManager* projectManager) {
    if (!registry || !guiManager || !assetLibrary || !projectManager) {
        ofLogError("InputRouter") << "Cannot handle drag event: missing dependencies";
        return;
    }

    if (dragInfo.files.empty()) {
        return;
    }

    // Get mouse position from drag info
    ofPoint mousePos = dragInfo.position;

    // Convert to ImGui coordinates (ImGui uses screen coordinates)
    ImVec2 imguiMousePos(mousePos.x, mousePos.y);

    // Filter valid media files
    std::vector<std::string> validFiles;
    for (const auto& filePath : dragInfo.files) {
        ofFile file(filePath);
        if (!file.exists()) {
            continue;
        }

        std::string ext = ofToLower(ofFilePath::getFileExt(filePath));
        bool isAudio = (ext == "wav" || ext == "mp3" || ext == "aiff" || ext == "aif" || ext == "m4a");
        bool isVideo = (ext == "mov" || ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "webm" || ext == "hap");

        if (isAudio || isVideo) {
            validFiles.push_back(filePath);
        }
    }

    if (validFiles.empty()) {
        ofLogNotice("InputRouter") << "No valid media files in drag-and-drop";
        return;
    }

    // Helper function to check if a file is within project Assets directory
    auto isFileInProjectAssets = [projectManager](const std::string& filePath) -> bool {
        if (!projectManager->isProjectOpen()) {
            return false;
        }
        std::string assetsDir = projectManager->getAssetsDirectory();
        if (assetsDir.empty()) {
            return false;
        }
        // Check if filePath starts with assetsDir
        std::string normalizedPath = ofFilePath::getAbsolutePath(filePath);
        std::string normalizedAssetsDir = ofFilePath::getAbsolutePath(assetsDir);
        return normalizedPath.find(normalizedAssetsDir) == 0;
    };

    // Check if drop is over AssetLibrary window first
    ImGuiWindow* assetLibraryWindow = ImGui::FindWindowByName("Asset Library");
    bool isOverAssetLibrary = false;
    if (assetLibraryWindow && assetLibraryWindow->Active) {
        ImVec2 windowPos = assetLibraryWindow->Pos;
        ImVec2 windowSize = assetLibraryWindow->Size;
        ImVec2 windowMax = ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y);

        if (imguiMousePos.x >= windowPos.x && imguiMousePos.x <= windowMax.x &&
            imguiMousePos.y >= windowPos.y && imguiMousePos.y <= windowMax.y) {
            isOverAssetLibrary = true;
        }
    }

    if (isOverAssetLibrary) {
        // Drop is over AssetLibrary - import files to AssetLibrary
        ofLogNotice("InputRouter") << "Dropping " << validFiles.size() << " file(s) to AssetLibrary";
        assetLibrary->handleDrop(validFiles);
        return;
    }

    // Find which module window received the drop - generic capability-based approach
    std::string targetInstanceName;

    // Check all modules that accept file drops to see if drop position is within their window
    registry->forEachModule([&targetInstanceName, &imguiMousePos](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
        if (!module || !module->hasCapability(ModuleCapability::ACCEPTS_FILE_DROP)) {
            return;
        }

        // Window title matches instance name
        std::string windowTitle = name;

        // Check if window exists and is visible
        ImGuiWindow* window = ImGui::FindWindowByName(windowTitle.c_str());
        if (window && window->Active) {
            // Check if mouse position is within window bounds
            ImVec2 windowPos = window->Pos;
            ImVec2 windowSize = window->Size;
            ImVec2 windowMax = ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y);

            if (imguiMousePos.x >= windowPos.x && imguiMousePos.x <= windowMax.x &&
                imguiMousePos.y >= windowPos.y && imguiMousePos.y <= windowMax.y) {
                targetInstanceName = name;
                return; // Stop iteration
            }
        }
    });

    // If no specific window was found, use the first visible module that accepts file drops
    if (targetInstanceName.empty()) {
        // Try visible instances first (by type)
        auto visibleInstances = guiManager->getVisibleInstances(ModuleType::INSTRUMENT);
        for (const auto& instanceName : visibleInstances) {
            auto module = registry->getModule(instanceName);
            if (module && module->hasCapability(ModuleCapability::ACCEPTS_FILE_DROP)) {
                targetInstanceName = instanceName;
                break;
            }
        }

        // Fallback: find any module with ACCEPTS_FILE_DROP capability
        if (targetInstanceName.empty()) {
            registry->forEachModule([&targetInstanceName](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
                if (module && module->hasCapability(ModuleCapability::ACCEPTS_FILE_DROP)) {
                    targetInstanceName = name;
                    return; // Stop after first match
                }
            });
        }
    }

    // Handle drop on module - generic capability-based handling
    if (!targetInstanceName.empty()) {
        auto targetModule = registry->getModule(targetInstanceName);

        if (targetModule && targetModule->hasCapability(ModuleCapability::ACCEPTS_FILE_DROP)) {
            // Check if files are from project Assets directory
            bool allFilesFromProject = true;
            for (const auto& filePath : validFiles) {
                if (!isFileInProjectAssets(filePath)) {
                    allFilesFromProject = false;
                    break;
                }
            }

            if (allFilesFromProject) {
                // Files are already in project - send directly to module
                ofLogNotice("InputRouter") << "Adding " << validFiles.size() << " file(s) from project to module: " << targetInstanceName;
                targetModule->acceptFileDrop(validFiles);
            } else {
                // Files are from OS - import to AssetLibrary first, then send to module
                ofLogNotice("InputRouter") << "Importing " << validFiles.size() << " file(s) to AssetLibrary, then sending to module: " << targetInstanceName;

                // Import files to AssetLibrary
                std::vector<std::string> importedAssetIds = assetLibrary->importFiles(validFiles);

                // Wait a bit for conversion to start (if needed), then get converted paths
                // For now, we'll send the original paths and let module handle it
                // In the future, we could wait for conversion and send converted paths
                std::vector<std::string> pathsToSend;
                for (const auto& assetId : importedAssetIds) {
                    std::string assetPath = assetLibrary->getAssetPath(assetId);
                    if (!assetPath.empty()) {
                        pathsToSend.push_back(assetPath);
                    } else {
                        // Fallback to original path if asset path not available
                        // Find original path from assetId
                        const AssetInfo* asset = assetLibrary->getAssetInfo(assetId);
                        if (asset && !asset->originalPath.empty()) {
                            pathsToSend.push_back(asset->originalPath);
                        }
                    }
                }

                if (!pathsToSend.empty()) {
                    targetModule->acceptFileDrop(pathsToSend);
                }
            }
        } else {
            ofLogWarning("InputRouter") << "Module instance not found or doesn't accept file drops: " << targetInstanceName;
        }
    } else {
        // Find first module that accepts file drops
        registry->forEachModule([&validFiles, &isFileInProjectAssets](const std::string& uuid, const std::string& name, std::shared_ptr<Module> module) {
            if (module && module->hasCapability(ModuleCapability::ACCEPTS_FILE_DROP)) {
                // Check if files are from project Assets directory
                bool allFilesFromProject = true;
                for (const auto& filePath : validFiles) {
                    if (!isFileInProjectAssets(filePath)) {
                        allFilesFromProject = false;
                        break;
                    }
                }

                if (allFilesFromProject) {
                    // Files are already in project - send directly to module
                    ofLogNotice("InputRouter") << "Adding " << validFiles.size() << " file(s) from project to module: " << name;
                    module->acceptFileDrop(validFiles);
                } else {
                    // Files are from OS - just log (AssetLibrary import should be done manually)
                    ofLogNotice("InputRouter") << "Files from OS - import to AssetLibrary first, then drag to module";
                }
                return; // Stop after first match
            }
        });
    }
}

//--------------------------------------------------------------
void InputRouter::setupWithCallbacks(
    Clock* clock_,
    ModuleRegistry* registry_,
    GUIManager* guiManager_,
    ViewManager* viewManager_,
    Console* console_,
    CommandBar* commandBar_,
    SessionManager* sessionManager,
    ProjectManager* projectManager,
    std::function<void()> onUpdateWindowTitle,
    int* currentStep_,
    int* lastTriggeredStep_,
    bool* showGUI_
) {
    // Setup system references
    setup(clock_, registry_, guiManager_, viewManager_, console_, commandBar_);

    // Set state references
    setCurrentStep(currentStep_);
    setLastTriggeredStep(lastTriggeredStep_);
    setShowGUI(showGUI_);

    // Set session save/load callbacks for keyboard shortcut (S key)
    setSessionCallbacks(
        [sessionManager, onUpdateWindowTitle]() {
            if (sessionManager && sessionManager->saveSession("session.json")) {
                onUpdateWindowTitle();
            }
        },
        [sessionManager, onUpdateWindowTitle]() {
            if (sessionManager && sessionManager->loadSession("session.json")) {
                onUpdateWindowTitle();
            }
        }
    );

    // Set File menu callbacks for keyboard shortcuts
    setFileMenuCallbacks(
        [sessionManager, projectManager, onUpdateWindowTitle]() {
            // Cmd+S: Save
            if (!sessionManager || !projectManager) return;
            std::string sessionName = sessionManager->getCurrentSessionName();
            if (sessionName.empty()) {
                if (projectManager->isProjectOpen()) {
                    sessionName = projectManager->generateDefaultSessionName();
                } else {
                    sessionName = "session.json";
                }
            }
            if (sessionManager->saveSession(sessionName)) {
                onUpdateWindowTitle();
                std::string sessionPath = projectManager->isProjectOpen()
                    ? projectManager->getSessionPath(sessionName)
                    : sessionName;
                // TODO: Add to recent sessions if needed
                // if (!sessionPath.empty()) {
                //     // Add to recent sessions
                // }
            }
        },
        [sessionManager, projectManager, onUpdateWindowTitle]() {
            // Cmd+Shift+S: Save As
            if (!sessionManager || !projectManager) return;
            std::string defaultName = sessionManager->getCurrentSessionName();
            if (defaultName.empty()) {
                defaultName = projectManager->isProjectOpen()
                    ? projectManager->generateDefaultSessionName()
                    : "session.json";
            }
            auto result = ofSystemSaveDialog(defaultName, "Save Session As");
            if (result.bSuccess) {
                std::string sessionName = ofFilePath::getFileName(result.filePath);
                if (projectManager->isProjectOpen()) {
                    sessionManager->saveSession(sessionName);
                    onUpdateWindowTitle();
                    // TODO: Add to recent sessions if needed
                    // addToRecentSessions(projectManager->getSessionPath(sessionName));
                } else {
                    sessionManager->saveSessionToPath(result.filePath);
                    onUpdateWindowTitle();
                    // TODO: Add to recent sessions if needed
                    // addToRecentSessions(result.filePath);
                }
            }
        },
        [sessionManager, onUpdateWindowTitle]() {
            // Cmd+Shift+O: Open
            if (!sessionManager) return;
            auto result = ofSystemLoadDialog("Open Session", false);
            if (result.bSuccess) {
                if (sessionManager->loadSessionFromPath(result.filePath)) {
                    onUpdateWindowTitle();
                    // TODO: Add to recent sessions if needed
                    // addToRecentSessions(result.filePath);
                }
            }
        },
        []() {
            // Cmd+O: Open Recent (for now, just log - menu shows recent sessions)
            // Could be enhanced to show a popup with recent sessions
        }
    );

    ofLogNotice("InputRouter") << "Setup with callbacks complete";
}
