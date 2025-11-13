#include "InputRouter.h"
#include "Clock.h"
#include "TrackerSequencer.h"
#include "TrackerSequencerGUI.h"
#include "gui/ViewManager.h"
#include "MediaPool.h"
#include "MediaPoolGUI.h"
#include "ofxImGui.h"
#include "ofLog.h"

InputRouter::InputRouter() {
}

void InputRouter::setup(
    Clock* clock_,
    TrackerSequencer* tracker_,
    ViewManager* viewManager_,
    MediaPool* mediaPool_,
    MediaPoolGUI* mediaPoolGUI_
) {
    clock = clock_;
    tracker = tracker_;
    viewManager = viewManager_;
    mediaPool = mediaPool_;
    mediaPoolGUI = mediaPoolGUI_;

    ofLogNotice("InputRouter") << "Setup complete";
}

void InputRouter::setPlayState(bool* isPlaying_) {
    isPlaying = isPlaying_;
}

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
    
    // LOG: Entry point for all key presses
    ofLogNotice("InputRouter") << "=== KEY PRESS: key=" << key 
        << " ('" << (char)key << "')";
    
    // Priority 1: Panel navigation (Ctrl+Tab / Ctrl+Shift+Tab) - check BEFORE ImGui processes
    // IMPORTANT: Use OF_KEY_CONTROL (not COMMAND) for Ctrl key
    // On Mac: OF_KEY_CONTROL = Control key, OF_KEY_COMMAND = Command key (⌘)
    if (key == OF_KEY_TAB) {
        bool ctrlPressed = keyEvent.hasModifier(OF_KEY_CONTROL);
        if (ctrlPressed) {
            if (handlePanelNavigation(keyEvent)) {
                return true;
            }
        }
        // Regular Tab (without Ctrl) - let ImGui handle it for native navigation
        // Don't return false here, let it fall through to ImGui processing
    }
    
    updateImGuiCaptureState();
    
    // Priority 2: Spacebar - ALWAYS works (global transport control)
    // Handle spacebar BEFORE other checks to ensure it always works
    if (key == ' ') {
        // Alt+Spacebar: Trigger current edit step
        if (keyEvent.hasModifier(OF_KEY_ALT)) {
            if (tracker) {
                int editStep = tracker->getEditStep();
                if (editStep >= 0) {
                    tracker->triggerStep(editStep);
                    logKeyPress(key, "Alt+Spacebar: Trigger step");
                    return true;
                }
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
    
    // DEBUG: Log MediaPool focus state
    ofLogVerbose("InputRouter") << "  MediaPool check: currentPanelIndex=" << currentPanelIndex
                                 << ", mediaPoolParameterFocused=" << (mediaPoolParameterFocused ? "YES" : "NO")
                                 << ", mediaPoolGUI=" << (mediaPoolGUI ? "YES" : "NO");
    
    // CRITICAL: Handle MediaPool parameter editing FIRST, regardless of panel index
    // This allows editing parameters even when ViewManager thinks a different panel is active
    // (e.g., MediaPool parameters in a docked window while tracker panel is "active")
    if (mediaPoolGUI && mediaPoolParameterFocused) {
        bool ctrlPressed = keyEvent.hasModifier(OF_KEY_CONTROL);
        bool shiftPressed = keyEvent.hasModifier(OF_KEY_SHIFT);
        
        ofLogNotice("InputRouter") << "  MediaPool parameter focused - routing key to MediaPoolGUI (panel=" << currentPanelIndex << ", key=" << key << ")";
        
        // Delegate to MediaPoolGUI for parameter editing
        bool handled = mediaPoolGUI->handleKeyPress(key, ctrlPressed, shiftPressed);
        if (handled) {
            ofLogNotice("InputRouter") << "  Key handled by MediaPoolGUI";
            return true;
        } else {
            ofLogNotice("InputRouter") << "  Key NOT handled by MediaPoolGUI - letting ImGui handle";
            // If MediaPoolGUI didn't handle it, let ImGui process it (for navigation)
            // This allows arrow keys to work for navigation even when cells are focused
            return false;
        }
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
    bool isParentWidgetFocused = false;
    
    // ALWAYS check tracker state if tracker exists, regardless of panel index
    // This is because the user might be interacting with tracker even if ViewManager
    // thinks a different panel is active (ImGui window focus vs ViewManager panel state)
    if (tracker) {
        int editStep = tracker->getEditStep();
        int editColumn = tracker->getEditColumn();
        // Check if a valid cell is focused (indicates user is interacting with tracker)
        trackerCellFocused = (editStep >= 0 && editStep < tracker->getStepCount() && editColumn >= 0);
        // Check if on header row (editStep == -1 means no cell focused, likely on header)
        onHeaderRow = (editStep == -1 && !tracker->getIsEditingCell());
    }
    
    bool isInTrackerPanel = (tracker && viewManager && currentPanelIndex == 2);
    
    ofLogNotice("InputRouter") << "  Panel check: tracker=" << (tracker ? "YES" : "NO")
        << ", viewManager=" << (viewManager ? "YES" : "NO")
        << ", currentPanelIndex=" << currentPanelIndex
        << ", trackerCellFocused=" << (trackerCellFocused ? "YES" : "NO")
        << ", onHeaderRow=" << (onHeaderRow ? "YES" : "NO");
    
    // Use either panel index check OR tracker cell focused check OR header row check
    // This handles both docked windows and regular panel navigation, including header row navigation
    bool inTrackerPanel = isInTrackerPanel || trackerCellFocused || onHeaderRow;
    
    // Handle tracker input - cells are directly navigable like other widgets
    if (tracker && inTrackerPanel) {
        // Check for modifier keys properly
        bool ctrlPressed = keyEvent.hasModifier(OF_KEY_CONTROL);
        bool shiftPressed = keyEvent.hasModifier(OF_KEY_SHIFT);
        bool cmdPressed = keyEvent.hasModifier(OF_KEY_COMMAND);
        
        // CRITICAL: In edit mode, block arrow keys from ImGui navigation
        // This prevents ImGui from moving focus when arrow keys adjust values
        bool inEditMode = tracker->getIsEditingCell();
        
        // CRITICAL: Route Enter and numeric keys even when ImGui wants keyboard
        // This ensures these keys work when cells are focused
        // CRITICAL: In edit mode, ALWAYS route arrow keys to tracker BEFORE ImGui can process them
        // This prevents ImGui from using arrow keys for navigation and changing focus
        if (inEditMode && (key == OF_KEY_UP || key == OF_KEY_DOWN || 
                           key == OF_KEY_LEFT || key == OF_KEY_RIGHT)) {
            // Arrow keys in edit mode: always route to tracker (adjust values)
            // Don't let ImGui use them for navigation - this locks focus to the editing cell
            // Sync state first to ensure tracker knows current cell
            syncEditStateFromImGuiFocus();
            if (tracker->handleKeyPress(key, ctrlPressed, shiftPressed)) {
                ofLogNotice("InputRouter") << "  Arrow key in edit mode: HANDLED by tracker (blocked from ImGui)";
                logKeyPress(key, "Tracker: Arrow key in edit mode (blocked from ImGui)");
                return true; // Consume the key to prevent ImGui from processing
            }
        }
        
        // CRITICAL: When NOT in edit mode, let ImGui handle arrow keys for navigation
        // Don't route arrow keys to tracker - this allows ImGui's native navigation to work
        // The tracker's arrow key handling (in handleKeyPress) should only be used in edit mode
        if (!inEditMode && (key == OF_KEY_UP || key == OF_KEY_DOWN || 
                            key == OF_KEY_LEFT || key == OF_KEY_RIGHT)) {
            // Not in edit mode: Let ImGui handle arrow keys for native navigation
            // This allows smooth navigation between parameter cells and step cells
            ofLogNotice("InputRouter") << "  Arrow key NOT in edit mode: letting ImGui handle navigation";
            return false; // Let ImGui process arrow keys for navigation
        }
        
        // Enter key: Check for Ctrl+Enter (go up a level) vs Shift+Enter (exit grid)
        if (key == OF_KEY_RETURN) {
            // When on header row, don't route Enter to tracker - let ImGui handle it
            if (onHeaderRow && !ctrlPressed && !shiftPressed) {
                ofLogNotice("InputRouter") << "  Enter key on header row: letting ImGui handle (not routing to tracker)";
                return false; // Let ImGui process the key
            }
            
            ofLogNotice("InputRouter") << "  Enter key detected in tracker panel";
            if (shiftPressed) {
                // Shift+Enter: Exit grid navigation (clear cell selection)
                ofLogNotice("InputRouter") << "  Shift+Enter: Exiting grid";
                if (tracker->handleKeyPress(key, false, true)) {
                    logKeyPress(key, "Tracker: Shift+Enter (exit grid)");
                    return true;
                }
            } else {
                // Regular Enter: Always try to handle it if we're in tracker panel
                // CRITICAL: Sync editStep/editColumn from ImGui focus BEFORE calling handleKeyPress
                // This ensures the tracker knows which cell is focused even if GUI sync hasn't happened yet
                ofLogNotice("InputRouter") << "  Regular Enter: Syncing edit state from ImGui focus";
                syncEditStateFromImGuiFocus();
                
                // DEBUG: Log frame count and state before handling
                int currentFrame = ImGui::GetFrameCount();
                int editStep = tracker->getEditStep();
                int editColumn = tracker->getEditColumn();
                bool isEditing = tracker->getIsEditingCell();
                ofLogNotice("InputRouter") << "  Enter key at frame=" << currentFrame
                    << ", editStep=" << editStep << ", editColumn=" << editColumn
                    << ", isEditingCell=" << (isEditing ? "YES" : "NO");
                
                bool handled = tracker->handleKeyPress(key, false, false);
                if (handled) {
                    if (currentStep) {
                        *currentStep = tracker->getCurrentStep();
                    }
                    ofLogNotice("InputRouter") << "  Enter key HANDLED by tracker";
                    logKeyPress(key, "Tracker: Enter (handled)");
                    return true;
                } else {
                    // Enter was pressed but tracker didn't handle it
                    // This might mean editStep/editColumn aren't set yet
                    ofLogWarning("InputRouter") << "  Enter key NOT handled by tracker. editStep=" << editStep 
                        << ", editColumn=" << editColumn << ", isEditingCell=" << (isEditing ? "YES" : "NO");
                    
                    // Still consume it to prevent ImGui from activating buttons
                    // The tracker should handle it next frame once GUI sync happens
                    return true; // Consume to prevent ImGui from processing
                }
            }
        }
        
        // Route numeric keys and edit mode keys to tracker
        // This allows typing numbers to auto-enter edit mode, and handles edit mode input
        ofLogNotice("InputRouter") << "  Checking numeric keys: inEditMode=" << (inEditMode ? "YES" : "NO");
        if (inEditMode) {
            // In edit mode: Arrow keys already handled above, now handle numeric input
            // Numeric keys (including numpad - openFrameworks converts numpad to regular '0'-'9')
            // Also handle decimal point, minus, backspace, delete for numeric input
            if ((key >= '0' && key <= '9') ||
                key == '.' || key == '-' || key == OF_KEY_BACKSPACE || key == OF_KEY_DEL) {
                ofLogNotice("InputRouter") << "  Numeric key '" << (char)key << "' in edit mode - routing to tracker";
                if (tracker->handleKeyPress(key, ctrlPressed, shiftPressed)) {
                    ofLogNotice("InputRouter") << "  Numeric key HANDLED by tracker (in edit mode)";
                    logKeyPress(key, "Tracker: Numeric key in edit mode");
                    return true;
                } else {
                    ofLogWarning("InputRouter") << "  Numeric key NOT handled by tracker (in edit mode)";
                }
            }
        } else {
            // Not in edit mode: Route numeric keys to tracker for direct typing
            // This allows typing numbers to auto-enter edit mode
            // BUT: Don't route if an ImGui input field is active (e.g., repeat count inputs)
            if ((key >= '0' && key <= '9') || key == '.' || key == '-') {
                // Check if any ImGui widget is active (including InputText fields like repeat count)
                // If so, let ImGui handle the key instead of routing to tracker
                // This prevents interference when editing pattern chain repeat counts
                if (ImGui::IsAnyItemActive()) {
                    ofLogNotice("InputRouter") << "  Numeric key '" << (char)key << "' - ImGui item active (e.g., repeat count InputText), letting ImGui handle it";
                    return false; // Let ImGui handle it
                }
                
                // Also check WantTextInput as a fallback (for cases where item might not be "active" yet)
                ImGuiIO& io = ImGui::GetIO();
                if (io.WantTextInput) {
                    ofLogNotice("InputRouter") << "  Numeric key '" << (char)key << "' - ImGui text input active, letting ImGui handle it";
                    return false; // Let ImGui handle it
                }
                
                ofLogNotice("InputRouter") << "  Numeric key '" << (char)key << "' detected (not in edit mode)";
                // CRITICAL: Sync editStep/editColumn from ImGui focus BEFORE calling handleKeyPress
                // This ensures the tracker knows which cell is focused even if GUI sync hasn't happened yet
                ofLogNotice("InputRouter") << "  Syncing edit state from ImGui focus";
                syncEditStateFromImGuiFocus();
                
                // DEBUG: Log frame count and state before handling
                int currentFrame = ImGui::GetFrameCount();
                int editStep = tracker->getEditStep();
                int editColumn = tracker->getEditColumn();
                ofLogNotice("InputRouter") << "  Numeric key '" << (char)key << "' at frame=" 
                    << currentFrame << ", editStep=" << editStep << ", editColumn=" << editColumn;
                
                bool handled = tracker->handleKeyPress(key, ctrlPressed, shiftPressed);
                
                if (handled) {
                    ofLogNotice("InputRouter") << "  Numeric key HANDLED by tracker (entered edit mode)";
                    logKeyPress(key, "Tracker: Numeric key (auto-enter edit mode)");
                    return true;
                } else {
                    // Numeric key not handled - no cell is focused
                    // Don't consume the key - let ImGui or other handlers process it
                    ofLogNotice("InputRouter") << "  Numeric key '" << (char)key << "' NOT handled by tracker (no cell focused). "
                        << "editStep=" << editStep << ", editColumn=" << editColumn;
                    return false; // Let other handlers process the key
                }
            }
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
    // Note: Parameter editing is handled earlier (Priority 4) before tracker checks
    bool inMediaPoolPanel = (mediaPool && viewManager && currentPanelIndex == 3);
    if (inMediaPoolPanel && mediaPoolGUI) {
        bool ctrlPressed = keyEvent.hasModifier(OF_KEY_CONTROL);
        bool shiftPressed = keyEvent.hasModifier(OF_KEY_SHIFT);
        
        // Ctrl+Enter: Go up a level (focus parent container)
        // Works when inside the media list (a Selectable is focused)
        if (key == OF_KEY_RETURN && ctrlPressed && !shiftPressed) {
            // Check if we're inside the media list (a Selectable is focused)
            // We can detect this by checking if parent widget is NOT focused
            // (if parent widget is focused, we're already at parent level)
            if (!mediaPoolGUI->getIsParentWidgetFocused()) {
                ofLogNotice("InputRouter") << "  Ctrl+Enter in MediaPool: requesting focus move to parent container";
                mediaPoolGUI->requestFocusMoveToParent();
                return true; // Consume the key
            }
        }
        
        // UP key on first list item: Move focus to parent widget
        // This allows exiting the list using default ImGui navigation
        // We detect this by checking if we're NOT on parent widget and UP is pressed
        // ImGui will handle the actual navigation, but we need to detect when we're at the first item
        // and explicitly move to parent widget
        if (key == OF_KEY_UP && !mediaPoolGUI->getIsParentWidgetFocused()) {
            // Check if we're at the first navigable item in the list
            // We can't directly check this, but we can let ImGui handle it first,
            // then check if focus moved to parent widget
            // For now, we'll let ImGui handle UP key navigation naturally
            // The parent widget pattern will work when ImGui tries to navigate up from first item
            ofLogNotice("InputRouter") << "  UP key in MediaPool list - letting ImGui handle navigation";
            // Don't consume - let ImGui handle it
        }
    }
    
    return false;
}

bool InputRouter::handleGlobalShortcuts(int key) {
    // Global shortcuts work even when ImGui has focus

    switch (key) {
        case ' ':  // SPACE - Play/Stop (always works, even when ImGui has focus)
            if (clock) {
                bool currentlyPlaying = (isPlaying && *isPlaying);
                if (currentlyPlaying) {
                    clock->stop();
                    if (isPlaying) *isPlaying = false;
                    logKeyPress(key, "Global: Stop");
                } else {
                    clock->start();
                    if (isPlaying) *isPlaying = true;
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

        case 'S':  // S - Save pattern (capital S to distinguish from speed)
            if (tracker) {
                tracker->saveState("pattern.json");
                logKeyPress(key, "Global: Save pattern");
                return true;
            }
            break;
    }

    return false;
}

bool InputRouter::handlePanelNavigation(ofKeyEventArgs& keyEvent) {
    if (!viewManager) return false;
    
    int key = keyEvent.key;
    if (key == OF_KEY_TAB) {
        bool shiftPressed = keyEvent.hasModifier(OF_KEY_SHIFT);
        if (shiftPressed) {
            viewManager->previousPanel();
        } else {
            viewManager->nextPanel();
        }
        logKeyPress(key, "Navigation: Ctrl+Tab");
        return true;
    }
    return false;
}

bool InputRouter::handleTrackerInput(ofKeyEventArgs& keyEvent) {
    if (!tracker) return false;
    
    int key = keyEvent.key;
    bool ctrlPressed = keyEvent.hasModifier(OF_KEY_CONTROL);
    bool shiftPressed = keyEvent.hasModifier(OF_KEY_SHIFT);
    
    // Delegate to TrackerSequencer with proper modifier flags
    if (tracker->handleKeyPress(key, ctrlPressed, shiftPressed)) {
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
    return tracker ? tracker->getIsEditingCell() : false;
}

void InputRouter::syncEditStateFromImGuiFocus() {
    // Sync edit state from ImGui focus before processing keys
    // This ensures editStep/editColumn are set even if GUI draw sync hasn't happened yet
    if (tracker) {
        TrackerSequencerGUI::syncEditStateFromImGuiFocus(*tracker);
    }
}

void InputRouter::logKeyPress(int key, const char* context) {
    ofLogVerbose("InputRouter") << context << " - Key: " << key;
}

