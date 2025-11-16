#include "InputRouter.h"
#include "Clock.h"
#include "TrackerSequencer.h"
#include "TrackerSequencerGUI.h"
#include "gui/ViewManager.h"
#include "MediaPool.h"
#include "MediaPoolGUI.h"
#include "gui/Console.h"
#include "ofxImGui.h"
#include "ofLog.h"

InputRouter::InputRouter() {
}

void InputRouter::setup(
    Clock* clock_,
    TrackerSequencer* tracker_,
    TrackerSequencerGUI* trackerGUI_,
    ViewManager* viewManager_,
    MediaPool* mediaPool_,
    MediaPoolGUI* mediaPoolGUI_,
    Console* console_
) {
    clock = clock_;
    tracker = tracker_;
    trackerGUI = trackerGUI_;
    viewManager = viewManager_;
    mediaPool = mediaPool_;
    mediaPoolGUI = mediaPoolGUI_;
    console = console_;

    ofLogNotice("InputRouter") << "Setup complete";
}

void InputRouter::setSessionCallbacks(
    std::function<void()> onSaveSession_,
    std::function<void()> onLoadSession_
) {
    onSaveSession = onSaveSession_;
    onLoadSession = onLoadSession_;
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

bool InputRouter::handleKeyPress(ofKeyEventArgs& keyEvent) {
    int key = keyEvent.key;
    int keycode = keyEvent.keycode;
    int scancode = keyEvent.scancode;
    
    // Extract modifiers once at the top
    bool ctrlPressed = keyEvent.hasModifier(OF_KEY_CONTROL);
    bool shiftPressed = keyEvent.hasModifier(OF_KEY_SHIFT);
    bool cmdPressed = keyEvent.hasModifier(OF_KEY_COMMAND);
    
    // Debug: Log Cmd+':' attempts (before check to see what we're receiving)
    if (cmdPressed && (key == ':' || key == 58 || keycode == 59)) {
        ofLogNotice("InputRouter") << "[COLON_DEBUG] Cmd+':' detected: key=" << key 
                                    << ", keycode=" << keycode 
                                    << ", scancode=" << scancode 
                                    << ", shift=" << shiftPressed
                                    << ", isColonKey=" << (key == ':' || key == 58 || (keycode == 59 && shiftPressed));
    }
    
    // Priority 0: Cmd+':' - Toggle Console (global shortcut, works everywhere)
    // On macOS, ':' is Shift+';' (semicolon), so we need to check for semicolon keycode (59) with Shift
    // Also check direct ':' character (ASCII 58) and keycode 59 (semicolon)
    bool isColonKey = (key == ':' || key == 58 || (keycode == 59 && shiftPressed));
    if (isColonKey && cmdPressed && viewManager) {
        bool visible = viewManager->isConsoleVisible();
        viewManager->setConsoleVisible(!visible);
        
        // Sync Console's internal state and navigate to console panel when showing
        if (!visible && console) {
            console->open();
            // Navigate to console panel - Panel enum is defined in ViewManager.h (global namespace)
            // We can access it directly since it's in the global namespace
            viewManager->navigateToPanel(Panel::CONSOLE);
        } else if (visible && console) {
            console->close();
        }
        
        logKeyPress(key, "Global: Cmd+':' Toggle Console");
        return true; // Consume the key
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
    
    // Priority 1: Panel navigation (Ctrl+Tab / Ctrl+Shift+Tab) - check BEFORE ImGui processes
    // On macOS, Ctrl+Tab may be transformed by the system. Detect Tab by checking:
    // 1. Standard Tab detection (key/keycode)
    // 2. macOS-specific: When Control is pressed, check scancode (48 = Tab on macOS)
    const int GLFW_KEY_TAB = 258;
    const int MACOS_SCANCODE_TAB = 48;  // Tab scancode on macOS (from logs)
    
    bool isTabKey = (key == OF_KEY_TAB) || (keycode == GLFW_KEY_TAB);
    
    // macOS workaround: When Control is pressed, also check scancode for Tab
    // This handles cases where macOS transforms Ctrl+Tab events
    if (!isTabKey && ctrlPressed && scancode == MACOS_SCANCODE_TAB) {
        isTabKey = true;
    }
    
    // Debug: Log Tab key detection attempts
    if (ctrlPressed && (key == OF_KEY_TAB || keycode == GLFW_KEY_TAB || scancode == MACOS_SCANCODE_TAB)) {
        ofLogNotice("InputRouter") << "[TAB_DEBUG] Ctrl+Tab detected: key=" << key 
                                    << ", keycode=" << keycode 
                                    << ", scancode=" << scancode 
                                    << ", isTabKey=" << isTabKey;
    }
    
    if (isTabKey && ctrlPressed) {
        // Ctrl+Tab or Ctrl+Shift+Tab - handle panel navigation
        ofLogNotice("InputRouter") << "[TAB_DEBUG] Handling Ctrl+Tab navigation";
        if (handlePanelNavigation(keyEvent)) {
            ofLogNotice("InputRouter") << "[TAB_DEBUG] Panel navigation handled successfully";
            return true; // Consume the key to prevent ImGui from processing
        } else {
            ofLogWarning("InputRouter") << "[TAB_DEBUG] Panel navigation handler returned false";
        }
    }
    
    updateImGuiCaptureState();
    
    // Priority 2: Spacebar - ALWAYS works (global transport control)
    // Handle spacebar BEFORE other checks to ensure it always works
    if (key == ' ') {
        // Alt+Spacebar: Trigger current edit step
        if (tracker && trackerGUI) {
            int editStep = trackerGUI->getEditStep();
            if (editStep >= 0) {
                tracker->triggerStep(editStep);
                logKeyPress(key, "Alt+Spacebar: Trigger step");
                return true;
            }
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
    
    // Priority 4: MediaPool parameter editing - check BEFORE tracker to avoid conflicts
    // CRITICAL: Sync focus state from ImGui before checking if parameter is focused
    // This ensures editingColumnIndex and editingParameter are set even if GUI draw sync hasn't happened yet
    int currentPanelIndex = viewManager ? viewManager->getCurrentPanelIndex() : -1;
    
    if (mediaPoolGUI) {
        MediaPoolGUI::syncEditStateFromImGuiFocus(*mediaPoolGUI);
    }
    
    bool mediaPoolParameterFocused = (mediaPoolGUI && mediaPoolGUI->isKeyboardFocused());
    
    // Handle MediaPool parameter editing FIRST, regardless of panel index
    // This allows editing parameters even when ViewManager thinks a different panel is active
    if (mediaPoolGUI && mediaPoolParameterFocused) {
        // Delegate to MediaPoolGUI for parameter editing
        bool handled = mediaPoolGUI->handleKeyPress(key, ctrlPressed, shiftPressed);
        if (handled) {
            return true;
        }
        // If MediaPoolGUI didn't handle it, let ImGui process it (for navigation)
        return false;
    }
    
    // Priority 5: Tracker input - only when in tracker panel
    // CRITICAL: Route tracker input BEFORE ImGui can consume it, even if ImGui wants keyboard
    // This ensures Enter and numeric keys work when cells are focused
    // 
    // IMPORTANT: We check multiple conditions to determine if we're in the tracker panel:
    // 1. Panel index == 2 (official tracker panel)
    // 2. OR a tracker cell is focused (editStep/editColumn are valid)
    // This handles both docked windows and regular panel navigation
    bool trackerCellFocused = false;
    bool onHeaderRow = false;
    
    // ALWAYS check tracker state if tracker exists, regardless of panel index
    // This is because the user might be interacting with tracker even if ViewManager
    // thinks a different panel is active (ImGui window focus vs ViewManager panel state)
    if (tracker && trackerGUI) {
        int editStep = trackerGUI->getEditStep();
        int editColumn = trackerGUI->getEditColumn();
        // Check if a valid cell is focused (indicates user is interacting with tracker)
        trackerCellFocused = (editStep >= 0 && editStep < tracker->getStepCount() && editColumn >= 0);
        // Check if on header row (editStep == -1 means no cell focused, likely on header)
        onHeaderRow = (editStep == -1 && !trackerGUI->getIsEditingCell());
    }
    
    bool isInTrackerPanel = (tracker && viewManager && currentPanelIndex == 2);
    bool inTrackerPanel = isInTrackerPanel || trackerCellFocused || onHeaderRow;
    
    // Handle tracker input - cells are directly navigable like other widgets
    if (tracker && inTrackerPanel) {
        
        // PHASE 1: Arrow keys in edit mode are handled directly in ParameterCell::draw()
        // InputRouter should skip them to prevent double-processing
        bool inEditMode = trackerGUI ? trackerGUI->getIsEditingCell() : false;
        
        if (inEditMode && (key == OF_KEY_UP || key == OF_KEY_DOWN || 
                           key == OF_KEY_LEFT || key == OF_KEY_RIGHT)) {
            // Arrow keys in edit mode: Let Phase 1 handle them
            ofLogNotice("InputRouter") << "  Arrow key in edit mode - letting Phase 1 handle it in ParameterCell::draw()";
            return false; // Let ImGui process it so Phase 1 can handle it
        }
        
        // When NOT in edit mode, let ImGui handle arrow keys for navigation
        if (!inEditMode && (key == OF_KEY_UP || key == OF_KEY_DOWN || 
                            key == OF_KEY_LEFT || key == OF_KEY_RIGHT)) {
            // Not in edit mode: Let ImGui handle arrow keys for native navigation
            ofLogNotice("InputRouter") << "  Arrow key NOT in edit mode: letting ImGui handle navigation";
            return false; // Let ImGui process arrow keys for navigation
        }
        
        // Enter key: PHASE 1 handles regular Enter in edit mode
        // InputRouter only handles special cases (Shift+Enter, Ctrl+Enter)
        if (key == OF_KEY_RETURN) {
            // When on header row, don't route Enter to tracker - let ImGui handle it
            if (onHeaderRow && !ctrlPressed && !shiftPressed) {
                return false; // Let ImGui process the key
            }
            
            // PHASE 1: If in edit mode, let Phase 1 handle Enter (validates and exits edit mode)
            if (inEditMode && !ctrlPressed && !shiftPressed) {
                ofLogNotice("InputRouter") << "  Enter key - in edit mode, letting Phase 1 handle it in ParameterCell::draw()";
                return false; // Let ImGui process it so Phase 1 can handle it
            }
            
            // PHASE 1: If a tracker cell is focused (but not in edit mode), let Phase 1 handle Enter
            if (trackerCellFocused && !ctrlPressed && !shiftPressed) {
                ofLogNotice("InputRouter") << "  Enter key - tracker cell focused, letting Phase 1 handle it in ParameterCell::draw()";
                return false; // Let ImGui process it so Phase 1 can handle it
            }
            
            // Special cases: Shift+Enter and Ctrl+Enter are still handled by InputRouter
            if (!trackerGUI) {
                return false;
            }
            
            // Create GUI state struct from trackerGUI
            TrackerSequencer::GUIState guiState;
            guiState.editStep = trackerGUI->getEditStep();
            guiState.editColumn = trackerGUI->getEditColumn();
            guiState.isEditingCell = trackerGUI->getIsEditingCell();
            guiState.editBufferCache = trackerGUI->getEditBufferCache();
            guiState.editBufferInitializedCache = trackerGUI->getEditBufferInitializedCache();
            guiState.shouldRefocusCurrentCell = trackerGUI->getShouldRefocusCurrentCell();
            
            if (shiftPressed) {
                // Shift+Enter: Exit grid navigation (clear cell selection)
                syncEditStateFromImGuiFocus();
                guiState.editStep = trackerGUI->getEditStep();
                guiState.editColumn = trackerGUI->getEditColumn();
                if (tracker->handleKeyPress(key, false, true, guiState)) {
                    trackerGUI->setEditCell(guiState.editStep, guiState.editColumn);
                    trackerGUI->setInEditMode(guiState.isEditingCell);
                    trackerGUI->getEditBufferCache() = guiState.editBufferCache;
                    trackerGUI->setEditBufferInitializedCache(guiState.editBufferInitializedCache);
                    trackerGUI->setShouldRefocusCurrentCell(guiState.shouldRefocusCurrentCell);
                    logKeyPress(key, "Tracker: Shift+Enter (exit grid)");
                    return true;
                }
            } else if (ctrlPressed) {
                // Ctrl+Enter: Special action (if any)
                // For now, let it fall through to other handlers
                return false;
            }
            // Regular Enter when not in edit mode and no cell focused - let other handlers process it
            return false;
        }
        
        // PHASE 1: Numeric keys are handled directly in ParameterCell::draw()
        // If we're in the tracker panel, InputRouter should NEVER process numeric keys
        // This prevents double-processing and timing issues with GUI state sync
        if (inTrackerPanel && ((key >= '0' && key <= '9') || key == '.' || key == '-' || 
                               key == OF_KEY_BACKSPACE || key == OF_KEY_DEL)) {
            // Check if any ImGui widget is active (like InputText fields for repeat count)
            // If so, let ImGui handle it
            if (ImGui::IsAnyItemActive()) {
                ofLogNotice("InputRouter") << "  Numeric key '" << (char)key << "' - ImGui item active, letting ImGui handle it";
                return false;
            }
            
            // Also check WantTextInput as a fallback
            ImGuiIO& io = ImGui::GetIO();
            if (io.WantTextInput) {
                ofLogNotice("InputRouter") << "  Numeric key '" << (char)key << "' - ImGui text input active, letting ImGui handle it";
                return false;
            }
            
            // In tracker panel: Let Phase 1 handle ALL numeric keys
            // Phase 1 will check if cell is focused and handle accordingly
            ofLogNotice("InputRouter") << "  Numeric key '" << (char)key << "' - in tracker panel, letting Phase 1 handle it in ParameterCell::draw()";
            return false; // Let ImGui process it so Phase 1 can handle it
        }
        
        ofLogNotice("InputRouter") << "  Key not matched for tracker input, checking other handlers...";
        
        // Handle other tracker input with proper modifiers
        if (handleTrackerInput(keyEvent)) {
            ofLogNotice("InputRouter") << "  Key handled by handleTrackerInput";
            return true;
        }
        
        ofLogNotice("InputRouter") << "  Key not handled in tracker panel section";
    } else {
        ofLogNotice("InputRouter") << "  NOT in tracker panel (currentPanelIndex=" << currentPanelIndex 
            << ", trackerCellFocused=" << (trackerCellFocused ? "YES" : "NO") << ")";
    }
    
    // Handle MediaPool panel-specific navigation (only when in MediaPool panel)
    bool inMediaPoolPanel = (mediaPool && viewManager && currentPanelIndex == 3);
    if (inMediaPoolPanel && mediaPoolGUI) {
        // Ctrl+Enter: Go up a level (focus parent container)
        if (key == OF_KEY_RETURN && ctrlPressed && !shiftPressed) {
            if (!mediaPoolGUI->getIsParentWidgetFocused()) {
                mediaPoolGUI->requestFocusMoveToParent();
                return true; // Consume the key
            }
        }
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

        case 'r':
        case 'R':  // R - Reset
            if (clock) {
                clock->reset();
                if (tracker) tracker->reset();
                if (currentStep) *currentStep = 0;
                if (lastTriggeredStep) *lastTriggeredStep = 0;
                logKeyPress(key, "Global: Reset");
                return true;
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

        case 'n':
        case 'N':  // N - Next media
            if (mediaPool) {
                mediaPool->nextPlayer();
                logKeyPress(key, "Global: Next media");
                return true;
            }
            break;

        case 'm':
        case 'M':  // M - Previous media
            if (mediaPool) {
                mediaPool->previousPlayer();
                logKeyPress(key, "Global: Previous media");
                return true;
            }
            break;

        case 'S':  // S - Save session (capital S to distinguish from speed)
            if (onSaveSession) {
                onSaveSession();
                logKeyPress(key, "Global: Save session");
                return true;
            } else if (tracker) {
                // Fallback to old behavior if callback not set
                tracker->saveState("pattern.json");
                logKeyPress(key, "Global: Save pattern (fallback)");
                return true;
            }
            break;
    }

    return false;
}

bool InputRouter::handlePanelNavigation(ofKeyEventArgs& keyEvent) {
    if (!viewManager) return false;
    
    bool shiftPressed = keyEvent.hasModifier(OF_KEY_SHIFT);
    
    if (shiftPressed) {
        viewManager->previousPanel();
        logKeyPress(OF_KEY_TAB, "Navigation: Ctrl+Shift+Tab");
    } else {
        viewManager->nextPanel();
        logKeyPress(OF_KEY_TAB, "Navigation: Ctrl+Tab");
    }
    return true;
}

bool InputRouter::handleTrackerInput(ofKeyEventArgs& keyEvent) {
    if (!tracker || !trackerGUI) return false;
    
    int key = keyEvent.key;
    bool ctrlPressed = keyEvent.hasModifier(OF_KEY_CONTROL);
    bool shiftPressed = keyEvent.hasModifier(OF_KEY_SHIFT);
    
    // Create GUI state struct from trackerGUI
    TrackerSequencer::GUIState guiState;
    guiState.editStep = trackerGUI->getEditStep();
    guiState.editColumn = trackerGUI->getEditColumn();
    guiState.isEditingCell = trackerGUI->getIsEditingCell();
    guiState.editBufferCache = trackerGUI->getEditBufferCache();
    guiState.editBufferInitializedCache = trackerGUI->getEditBufferInitializedCache();
    
    // Delegate to TrackerSequencer with proper modifier flags
    if (tracker->handleKeyPress(key, ctrlPressed, shiftPressed, guiState)) {
        // Update GUI state back from the modified guiState
        trackerGUI->setEditCell(guiState.editStep, guiState.editColumn);
        trackerGUI->setInEditMode(guiState.isEditingCell);
        trackerGUI->getEditBufferCache() = guiState.editBufferCache;
        trackerGUI->setEditBufferInitializedCache(guiState.editBufferInitializedCache);
        if (currentStep) {
            *currentStep = tracker->getCurrentStep();
        }
        logKeyPress(key, "Tracker input");
        return true;
    }
    return false;
}

void InputRouter::updateImGuiCaptureState() {
    ImGuiIO& io = ImGui::GetIO();
    imGuiCapturingKeyboard = io.WantCaptureKeyboard;
}

bool InputRouter::isImGuiCapturingKeyboard() const {
    return imGuiCapturingKeyboard;
}

bool InputRouter::isSequencerInEditMode() const {
    return trackerGUI ? trackerGUI->getIsEditingCell() : false;
}

void InputRouter::syncEditStateFromImGuiFocus() {
    // Sync edit state from ImGui focus before processing keys
    // This ensures editStep/editColumn are set even if GUI draw sync hasn't happened yet
    if (trackerGUI) {
        trackerGUI->syncEditStateFromImGuiFocus();
    }
}

void InputRouter::logKeyPress(int key, const char* context) {
    ofLogVerbose("InputRouter") << context << " - Key: " << key;
}

