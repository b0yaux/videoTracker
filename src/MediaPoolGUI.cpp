#include "MediaPoolGUI.h"
#include "MediaPool.h"
#include "MediaPlayer.h"
#include "ParameterCell.h"
#include "Module.h"

MediaPoolGUI::MediaPoolGUI() 
    : mediaPool(nullptr), waveformHeight(100.0f), parentWidgetId(0), 
      isParentWidgetFocused(false), requestFocusMoveToParentWidget(false),
      editingColumnIndex(-1), shouldFocusFirstCell(false), shouldRefocusCurrentCell(false),
      anyCellFocusedThisFrame(false) {
    // Initialize search buffer
    memset(searchBuffer, 0, sizeof(searchBuffer));
    searchFilter = "";
}

void MediaPoolGUI::setMediaPool(MediaPool& pool) {
    mediaPool = &pool;
}

void MediaPoolGUI::draw() {



    // Draw parameter editing section
    drawParameters();
    ImGui::Separator();
     
    // Draw waveform at bottom
    drawWaveform(); 
    
    
    // Reserve space for waveform at bottom, then draw media list in remaining space
    float availableHeight = ImGui::GetContentRegionAvail().y;
    int waveformSpace = waveformHeight + 10; // Extra space for waveform controls
    
    // Draw media list in scrollable area wita reserved space for waveform
    ImGui::BeginChild("MediaList", ImVec2(0, availableHeight - waveformSpace), true);
    drawMediaList();
    ImGui::EndChild();
    
    drawSearchBar();
    drawDirectoryControls();

    ImGui::Separator();
    // Preview Mode Controls - TEMPORARY : Should be moved to a separate function
    ImGui::Text("Preview Mode :");
    PreviewMode currentMode = mediaPool->getPreviewMode();
    ImGui::SameLine();
    if (ImGui::RadioButton("Once", currentMode == PreviewMode::STOP_AT_END)) {
        mediaPool->setPreviewMode(PreviewMode::STOP_AT_END);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Loop", currentMode == PreviewMode::LOOP)) {
        mediaPool->setPreviewMode(PreviewMode::LOOP);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Play Next", currentMode == PreviewMode::PLAY_NEXT)) {
        mediaPool->setPreviewMode(PreviewMode::PLAY_NEXT);
    }
}

void MediaPoolGUI::drawDirectoryControls() {
    if (ImGui::Button("Browse Directory")) {
        mediaPool->browseForDirectory();
    }
    ImGui::SameLine();
    std::string displayPath = mediaPool->getDataDirectory();
    if (displayPath.length() > 50) {
        displayPath = "..." + displayPath.substr(displayPath.length() - 47);
    }
    ImGui::Text("%s", displayPath.c_str());
    ImGui::Separator();
}

void MediaPoolGUI::drawSearchBar() {
    ImGui::Text("Search:");
    ImGui::SameLine();
    if (ImGui::InputText("##search", searchBuffer, sizeof(searchBuffer))) {
        searchFilter = std::string(searchBuffer);
    }
    ImGui::Separator();
}

void MediaPoolGUI::drawMediaList() {
    // Create a focusable parent widget BEFORE the list for navigation
    // This widget can receive focus when exiting the list via Ctrl+Enter or UP key on first item
    ImGui::PushID("MediaListParent");
    
    // Following ImGui pattern: SetKeyboardFocusHere(0) BEFORE creating widget to request focus
    if (requestFocusMoveToParentWidget) {
        ImGui::SetKeyboardFocusHere(0); // Request focus for the upcoming widget
        // Set flag immediately so InputRouter can see it in the same frame
        // (SetKeyboardFocusHere takes effect next frame, but we want InputRouter to know now)
        isParentWidgetFocused = true;
    }
    
    // Create an invisible, focusable button that acts as the parent widget
    // This allows us to move focus here when exiting the list
    // Arrow keys will navigate to other widgets in the panel when this is focused
    ImGui::InvisibleButton("##MediaListParent", ImVec2(100, 20));
    parentWidgetId = ImGui::GetItemID(); // Store ID for potential future use
    
    // Following ImGui pattern: SetItemDefaultFocus() AFTER creating widget to mark as default
    if (requestFocusMoveToParentWidget) {
        ImGui::SetItemDefaultFocus(); // Mark this widget as the default focus
        requestFocusMoveToParentWidget = false; // Clear flag after using it
    }
    
    // Check if parent widget is focused right after creating it (ImGui pattern: IsItemFocused() works for last item)
    // This updates the state if focus has already moved (e.g., from previous frame's request)
    if (!isParentWidgetFocused) {
        // Only check if we didn't just set it above (to avoid overwriting)
        isParentWidgetFocused = ImGui::IsItemFocused();
    }
    
    // Label for the media list - make it non-navigable
    // This prevents clicking on the title from triggering list focus
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true); // Prevent navigation to this text
    ImGui::Text("Available Media:");
    ImGui::PopItemFlag(); // Restore navigation flag
    ImGui::PopID();
    
    // Track if any list item is focused (to update parent widget focus state)
    bool anyListItemFocused = false;
    
    // Get current index for auto-scrolling
    size_t currentIndex = mediaPool->getCurrentIndex();
    
    // Track if index changed to determine if we should sync scroll
    bool shouldSyncScroll = (currentIndex != previousMediaIndex);
    
    // Show indexed media list with actual file names
    if (mediaPool->getNumPlayers() > 0) {
        auto playerNames = mediaPool->getPlayerNames();
        auto playerFileNames = mediaPool->getPlayerFileNames();
        
        for (size_t i = 0; i < playerNames.size(); i++) {
            // Filter by search term
            if (!searchFilter.empty()) {
                std::string lowerName = playerNames[i];
                std::string lowerSearch = searchFilter;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(), ::tolower);
                
                // Also check file names if available
                bool nameMatches = (lowerName.find(lowerSearch) != std::string::npos);
                bool fileMatches = false;
                if (i < playerFileNames.size() && !playerFileNames[i].empty()) {
                    std::string lowerFileName = playerFileNames[i];
                    std::transform(lowerFileName.begin(), lowerFileName.end(), lowerFileName.begin(), ::tolower);
                    fileMatches = (lowerFileName.find(lowerSearch) != std::string::npos);
                }
                
                if (!nameMatches && !fileMatches) {
                    continue; // Skip non-matching items
                }
            }
            
            auto player = mediaPool->getMediaPlayer(i);
            if (player) {
                // Check if this is the currently active player
                bool isActive = (mediaPool->getActivePlayer() == player);
                bool isPlaying = (player->isPlaying());
                
                // Create clean display format: [01] [AV] Title
                std::string indexStr = "[" + std::to_string(i + 1); // Start at 01, not 00
                if (indexStr.length() < 3) {
                    indexStr = "[" + std::string(2 - (indexStr.length() - 1), '0') + std::to_string(i + 1);
                }
                indexStr += "]";
                
                // Get media type badge
                std::string mediaType = "";
                if (player->isAudioLoaded() && player->isVideoLoaded()) {
                    mediaType = "[AV]";
                } else if (player->isAudioLoaded()) {
                    mediaType = "[A]";
                } else if (player->isVideoLoaded()) {
                    mediaType = "[V]";
                } else {
                    mediaType = "--";
                }
                
                // Get clean title from file names
                std::string title = "";
                if (i < playerFileNames.size() && !playerFileNames[i].empty()) {
                    // Use the file name as title, remove extension
                    title = ofFilePath::getBaseName(playerFileNames[i]);
                } else {
                    // Fallback to player name if no file name
                    title = playerNames[i];
                }
                
                // Clean display name: [01] [AV] Title
                std::string displayName = indexStr + " " + mediaType + " " + title;
                
                // Visual styling for active and playing media
                if (isActive) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.1f, 0.1f, 0.9f, 0.6f)); // Blue or Orange background for active
                }
                if (isPlaying) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 1.0f, 0.7f, 1.0f)); // Green for playing
                }
                
                // Make items selectable and clickable
                if (ImGui::Selectable(displayName.c_str(), isActive)) {
                    // Use playMediaManual to set MANUAL_PREVIEW mode and play from start
                    bool success = mediaPool->playMediaManual(i, 0.0f);  // Always play from start (position 0.0)
                    if (!success) {
                        ofLogWarning("MediaPoolGUI") << "Failed to play media at index " << i;
                    }
                }
                
                // Auto-scroll to current playing media at top of list
                // Only sync scroll when media index changes (allows user scrolling otherwise)
                if (i == currentIndex && shouldSyncScroll) {
                    ImGui::SetScrollHereY(0.0f); // 0.0 = top of visible area
                }
                
                // Track if any list item is focused
                if (ImGui::IsItemFocused()) {
                    anyListItemFocused = true;
                }
                
                // Add hover tooltip with video frame preview
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    
                    // Show video frame if available
                    if (player->isVideoLoaded()) {
                        ofTexture videoTexture = player->getVideoPlayer().getVideoFile().getTexture();
                        if (videoTexture.isAllocated()) {
                            // Display video frame as tooltip
                            ImGui::Image((void*)(intptr_t)videoTexture.getTextureData().textureID, 
                                        ImVec2(160, 90), // Preview size
                                        ImVec2(0, 0), ImVec2(1, 1)); // Normal orientation (no flip)
                            
                            // Show basic info below the frame
                            ImGui::Text("Index: %zu", i);
                            ImGui::Text("Status: %s", player->isPlaying() ? "Playing" : "Stopped");
                        } else {
                            // Fallback to metadata if no video frame available
                            ImGui::Text("Index: %zu", i);
                            ImGui::Text("Video: %s", player->isVideoLoaded() ? "Loaded" : "Not loaded");
                            ImGui::Text("Status: %s", player->isPlaying() ? "Playing" : "Stopped");
                        }
                    } else {
                        // Show audio-only info
                        ImGui::Text("Index: %zu", i);
                        ImGui::Text("Audio: %s", player->isAudioLoaded() ? "Loaded" : "Not loaded");
                        ImGui::Text("Status: %s", player->isPlaying() ? "Playing" : "Stopped");
                    }
                    
                    ImGui::Text("Click to play/pause");
                    ImGui::EndTooltip();
                }
                
                // Add right-click context menu
                if (ImGui::BeginPopupContextItem(("MediaContext" + std::to_string(i)).c_str())) {
                    ImGui::Text("Media %zu", i);
                    ImGui::Separator();
                    
                    if (ImGui::MenuItem("Play/Preview", "Click")) {
                        // Stop all other players first
                        for (size_t j = 0; j < mediaPool->getNumPlayers(); j++) {
                            auto otherPlayer = mediaPool->getMediaPlayer(j);
                            if (otherPlayer && otherPlayer != player) {
                                otherPlayer->stop();
                            }
                        }
                        mediaPool->setActivePlayer(i);
                        player->play();
                    }
                    
                    if (ImGui::MenuItem("Stop", "")) {
                        player->stop();
                    }
                    
                    ImGui::Separator();
                    
                    if (ImGui::MenuItem("Set Position 0%")) {
                        player->setPosition(0.0f);
                    }
                    if (ImGui::MenuItem("Set Position 50%")) {
                        player->setPosition(0.5f);
                    }
                    
                    ImGui::EndPopup();
                }
                
                // Pop style colors if we pushed them
                if (isPlaying) {
                    ImGui::PopStyleColor();
                }
                if (isActive) {
                    ImGui::PopStyleColor();
                }
                
                // Status indicators are now included in the main display name
            }
        }
    ImGui::Separator();
    }
    
    // Update previous index after processing list (for scroll sync on next frame)
    previousMediaIndex = currentIndex;
    
    // CRITICAL: Update parent widget focus state AFTER the list ends
    // Following ImGui pattern: We can't check IsItemFocused() for a widget created earlier,
    // so we infer the state based on what we know:
    // - If any list item was focused, parent widget is definitely not focused
    // - If no list item is focused, we might be on parent widget
    // - The state checked right after creating the button is still valid if no items were focused
    if (anyListItemFocused) {
        // A list item is focused, so parent widget is definitely not focused
        isParentWidgetFocused = false;
    }
    // Otherwise, keep the state we checked right after creating the button
    // This follows ImGui's pattern: IsItemFocused() is only valid for the last item,
    // so we rely on the state we captured when the widget was created

}

void MediaPoolGUI::drawWaveform() {
    
    auto currentPlayer = mediaPool->getActivePlayer();
    if (currentPlayer) {
        
        // Create invisible button for interaction area
        ImVec2 canvasSize = ImVec2(-1, waveformHeight);
        ImGui::InvisibleButton("waveform_canvas", canvasSize);
        
        // Get draw list for custom rendering
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 canvasPos = ImGui::GetItemRectMin();
        ImVec2 canvasMax = ImGui::GetItemRectMax();
        
        // Calculate proper playhead position
        float playheadPosition = 0.0f;
        bool showPlayhead = false;
        
        // Always use the position parameter as the single source of truth
        // This ensures consistency between playing and non-playing states
        float mediaPosition = currentPlayer->position.get();
            if (mediaPosition > 0.0f || currentPlayer->isPlaying()) {
                playheadPosition = mediaPosition;
                showPlayhead = true;
            }
            
            // Handle click-to-seek and drag functionality
            // IMPORTANT: Check IsItemHovered OR IsItemActive to support dragging
            // IsItemActive ensures dragging continues even if mouse moves outside hover area
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                // Show cursor as hand when hovering over waveform
                if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                }
                
                // Check for mouse click or drag
                if (ImGui::IsMouseClicked(0)) {
                    // Click: seek to position and handle playback appropriately
                    ImVec2 mousePos = ImGui::GetMousePos();
                    float relativeX = (mousePos.x - canvasPos.x) / (canvasMax.x - canvasPos.x);
                    
                    if (relativeX >= 0.0f && relativeX <= 1.0f) {
                        auto player = mediaPool->getActivePlayer();
                        if (player) {
                            // When scrubbing, update both startPosition and position
                            // This allows editing the start position while paused
                            player->startPosition.set(relativeX);
                            // Only update position if not playing (to avoid triggering onPositionChanged feedback loop)
                            if (!player->isPlaying()) {
                            player->position.set(relativeX);
                            }
                            // Use setParameter to trigger synchronization notifications
                            mediaPool->setParameter("position", relativeX, true);
                            ofLogNotice("MediaPoolGUI") << "Seeking to position " << relativeX;
                            
                            // Debug: Check position before play
                            ofLogNotice("MediaPoolGUI") << "Position before play: " << player->position.get();
                            
                            // Check if MediaPool is in IDLE mode and start manual preview if needed
                            if (mediaPool->isIdle()) {
                                ofLogNotice("MediaPoolGUI") << "MediaPool is IDLE, starting manual preview from position " << relativeX;
                                mediaPool->playMediaManual(mediaPool->getCurrentIndex(), relativeX);
                            } else {
                                // MediaPool is already active, just play from the new position
                                player->play();
                                ofLogNotice("MediaPoolGUI") << "Resumed playback from position " << relativeX;
                            }
                            
                            // Debug: Check position after play
                            ofLogNotice("MediaPoolGUI") << "Position after play: " << player->position.get();
                        }
                    }
                } else if (ImGui::IsMouseDragging(0)) {
                    // Drag: scrub - seek to position during playback
                    ImVec2 mousePos = ImGui::GetMousePos();
                    float relativeX = (mousePos.x - canvasPos.x) / (canvasMax.x - canvasPos.x);
                    
                    // Clamp to valid range
                    relativeX = std::max(0.0f, std::min(1.0f, relativeX));
                    
                    if (relativeX >= 0.0f && relativeX <= 1.0f) {
                        auto player = mediaPool->getActivePlayer();
                        if (player) {
                            // Update startPosition for future triggers
                            player->startPosition.set(relativeX);
                            
                            // IMPORTANT: For scrubbing during playback, we need to seek directly
                            // onPositionChanged() prevents seeking during playback to avoid feedback loops,
                            // but scrubbing is an explicit user action, so we bypass it
                            if (player->isAudioLoaded()) {
                                player->getAudioPlayer().setPosition(relativeX);
                            }
                            if (player->isVideoLoaded()) {
                                player->getVideoPlayer().getVideoFile().setPosition(relativeX);
                                // Update video after seeking (needed for HAP codec)
                                player->getVideoPlayer().getVideoFile().update();
                            }
                            
                            // Update position parameter for UI display (without triggering expensive seek)
                            // We already set the actual playback position above
                            player->position.set(relativeX);
                            
                            // Use setParameter to trigger synchronization notifications
                            mediaPool->setParameter("position", relativeX, true);
                        }
                    }
                }
            }
            
            // Draw waveform area
            float canvasWidth = canvasMax.x - canvasPos.x;
            float canvasHeight = canvasMax.y - canvasPos.y;
            float centerY = canvasPos.y + canvasHeight * 0.5f;
            
            // Check if we have audio data to draw waveform
            bool hasAudioData = false;
            int numChannels = 0;
            int actualPoints = 0;
            static std::vector<float> timeData;
            static std::vector<std::vector<float>> channelData;
            
            if (currentPlayer->isAudioLoaded()) {
                // Get audio buffer data
                ofSoundBuffer buffer = currentPlayer->getAudioPlayer().getBuffer();
                int numFrames = buffer.getNumFrames();
                numChannels = buffer.getNumChannels();
                
                if (numFrames > 0 && numChannels > 0) {
                    hasAudioData = true;
                    
                    // Prepare data for ImDrawList - much more efficient than manual drawing
                    // Resize vectors if needed
                    int maxPoints = 2000; // Reasonable number of points for smooth waveform
                    int stepSize = std::max(1, numFrames / maxPoints);
                    actualPoints = std::min(maxPoints, numFrames / stepSize);
                    
                    timeData.resize(actualPoints);
                    channelData.resize(numChannels);
                    for (int ch = 0; ch < numChannels; ch++) {
                        channelData[ch].resize(actualPoints);
                    }
                    
                    // Downsample audio data
                    for (int i = 0; i < actualPoints; i++) {
                        int sampleIndex = i * stepSize;
                        if (sampleIndex < numFrames) {
                            timeData[i] = (float)i / (float)actualPoints; // Normalized time 0-1
                            
                            for (int ch = 0; ch < numChannels; ch++) {
                                channelData[ch][i] = buffer.getSample(sampleIndex, ch);
                            }
                        }
                    }
                }
            }
            

            // Draw fallback transparent black rectangle for video-only players
            ImU32 bgColor = IM_COL32(0, 0, 0, 100); // Semi-transparent black
            drawList->AddRectFilled(canvasPos, canvasMax, bgColor);
            
            if (hasAudioData) {
                // Draw actual waveform
                float amplitudeScale = canvasHeight * 0.4f; // Use 80% of height for amplitude
                
                // Draw each channel with white color
                for (int ch = 0; ch < numChannels; ch++) {
                    ImU32 lineColor = IM_COL32(255, 255, 255, 255); // White
                    
                    for (int i = 0; i < actualPoints - 1; i++) {
                        float x1 = canvasPos.x + timeData[i] * canvasWidth;
                        float y1 = centerY - channelData[ch][i] * amplitudeScale;
                        float x2 = canvasPos.x + timeData[i + 1] * canvasWidth;
                        float y2 = centerY - channelData[ch][i + 1] * amplitudeScale;
                        
                        drawList->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), lineColor, 1.0f);
                    }
                }
            }
            
            // Draw playhead with green color (always show if we have a position)
            if (showPlayhead) {
                float playheadX = canvasPos.x + playheadPosition * canvasWidth;
                ImU32 playheadColor = IM_COL32(0, 255, 0, 255); // Green
                
                drawList->AddLine(
                    ImVec2(playheadX, canvasPos.y),
                    ImVec2(playheadX, canvasMax.y),
                    playheadColor, 3.0f
                );
            }
        
    } else {
        ImGui::Text("No active player to display waveform.");
    }
}

ParameterCell MediaPoolGUI::createParameterCellForParameter(const ParameterDescriptor& paramDesc) {
    ParameterCell cell;
    cell.parameterName = paramDesc.name;
    cell.isInteger = (paramDesc.type == ParameterType::INT);
    cell.setValueRange(paramDesc.minValue, paramDesc.maxValue, paramDesc.defaultValue);
    cell.calculateStepIncrement();
    
    auto activePlayer = mediaPool->getActivePlayer();
    
    // Set up getCurrentValue callback using MediaPlayer helper method
    cell.getCurrentValue = [paramDesc, activePlayer]() -> float {
        if (!activePlayer) return paramDesc.defaultValue;
        
        // Use MediaPlayer's helper method for cleaner parameter access
        const auto* param = activePlayer->getFloatParameter(paramDesc.name);
        if (param) {
            return param->get();
        }
        return paramDesc.defaultValue;
    };
    
    // Set up onValueApplied callback
    cell.onValueApplied = [this, paramDesc](const std::string&, float value) {
        if (mediaPool) {
            mediaPool->setParameter(paramDesc.name, value, true);
        }
    };
    
    // Set up formatValue callback (use openFrameworks ofToString for modern C++ string formatting)
    cell.formatValue = [paramDesc](float value) -> std::string {
        if (paramDesc.type == ParameterType::INT) {
            return ofToString((int)std::round(value));
        } else {
            return ofToString(value, 2); // 2 decimal places
        }
    };
    
    return cell;
}

bool MediaPoolGUI::handleParameterCellKeyPress(const ParameterDescriptor& paramDesc, int key, bool ctrlPressed, bool shiftPressed) {
    // Create ParameterCell for the parameter
    ParameterCell cell = createParameterCellForParameter(paramDesc);
    
    // Sync state from MediaPoolGUI to ParameterCell
    bool isSelected = (editingParameter == paramDesc.name);
    cell.isSelected = isSelected;
    
    // If we're entering edit mode via direct typing, enter edit mode now
    // Otherwise, use the current edit mode state
    if (isEditingParameter && !cell.isEditingMode()) {
        cell.enterEditMode();
    } else {
        cell.setEditing(isEditingParameter && isSelected);
    }
    
    if (isEditingParameter && isSelected) {
        cell.setEditBuffer(editBufferCache);
    }
    
    // Delegate keyboard handling to ParameterCell
    bool handled = cell.handleKeyPress(key, ctrlPressed, shiftPressed);
    
    if (handled) {
        // Sync edit mode state back from ParameterCell to MediaPoolGUI
        isEditingParameter = cell.isEditingMode();
        if (isEditingParameter) {
            editBufferCache = cell.getEditBuffer();
            editBufferInitializedCache = cell.isEditBufferInitialized();
        } else {
            editBufferCache.clear();
            editBufferInitializedCache = false;
        }
        
        // Disable/enable ImGui keyboard navigation based on edit mode
        ImGuiIO& io = ImGui::GetIO();
        if (isEditingParameter) {
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        } else {
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        }
    }
    
    return handled;
}

void MediaPoolGUI::drawParameters() {
    if (!mediaPool) return;
    

    // Get available parameters from MediaPool
    auto params = mediaPool->getParameters();
    auto activePlayer = mediaPool->getActivePlayer();
    
    if (!activePlayer || params.empty()) {
        ImGui::Text("No parameters available");
        return;
    }
    
    // Filter out "note" parameter (it's not editable in the GUI, only used internally)
    // Keep parameters in their natural order from MediaPool::getParameters()
    std::vector<ParameterDescriptor> editableParams;
    for (const auto& param : params) {
        if (param.name != "note") {
            editableParams.push_back(param);
        }
    }
    
    if (editableParams.empty()) {
        ImGui::Text("No editable parameters available");
        return;
    }
    
    // Create a focusable parent widget BEFORE the table for navigation (similar to TrackerSequencer)
    ImGui::PushID("MediaPoolParametersParent");
    
    // Handle parent widget focus requests
    if (requestFocusMoveToParentWidget) {
        ImGui::SetKeyboardFocusHere(0); // Request focus for the upcoming widget
        isParentWidgetFocused = true;
        clearCellFocus();
        requestFocusMoveToParentWidget = false;
    }
    
    // Create an invisible button as the parent widget (similar to TrackerSequencer)
    ImGui::InvisibleButton("##MediaPoolParamsParent", ImVec2(0, 0));
    
    // Handle clicks on parent widget - clear cell focus when clicked
    if (ImGui::IsItemClicked(0)) {
        clearCellFocus();
        isParentWidgetFocused = true;
    }
    
    // Check if parent widget is focused
    if (ImGui::IsItemFocused()) {
        isParentWidgetFocused = true;
    } else if (isParentWidgetFocused && !ImGui::IsAnyItemFocused()) {
        // Parent widget lost focus - update state
        isParentWidgetFocused = false;
    }
    
    parentWidgetId = ImGui::GetItemID();
    ImGui::PopID();
    
    // Tracker-style table styling
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1, 1));
    
    // Table flags (responsive sizing - columns stretch to fill available width)
    // Allow column reordering with memory - columns can be moved by dragging headers
    ImGuiTableFlags flags = ImGuiTableFlags_Borders | 
                           ImGuiTableFlags_RowBg | 
                           ImGuiTableFlags_Resizable |
                           ImGuiTableFlags_Reorderable | // Allow column reordering (saved in ImGui settings)
                           ImGuiTableFlags_SizingStretchProp; // Responsive: columns stretch proportionally
    
    int totalColumns = 1 + (int)editableParams.size(); // Media index column + parameter columns
    
    // Reset focus tracking at start of frame
    anyCellFocusedThisFrame = false;
    
    // Use versioned table ID to reset column order if needed (change version number to force reset)
    // This allows us to reset saved column positions while keeping reorderability
    static int tableVersion = 1; // Increment this to reset all saved column settings
    std::string tableId = "MediaPoolParameters_v" + std::to_string(tableVersion);
    
    if (ImGui::BeginTable(tableId.c_str(), totalColumns, flags)) {
        // Setup index column: Fixed width, reorderable (user can move it, but we set it first)
        ImGui::TableSetupColumn("IDX", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        
        // Setup parameter columns (stretch to fill remaining width, equal weight, reorderable)
        for (const auto& paramDesc : editableParams) {
            ImGui::TableSetupColumn(paramDesc.displayName.c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        }
        
        ImGui::TableSetupScrollFreeze(0, 1);
        
        // Header row
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        
        // Media index column header
        ImGui::TableSetColumnIndex(0);
        ImGui::TableHeader("IDX");
        
        // Parameter column headers
        for (size_t i = 0; i < editableParams.size(); i++) {
            ImGui::TableSetColumnIndex((int)i + 1);
            ImGui::TableHeader(editableParams[i].displayName.c_str());
        }
        
        // Single data row (no steps dimension)
        ImGui::TableNextRow();
        
        // Draw media index button (first column, column index 0) - similar to step number button in TrackerSequencer
        ImGui::TableSetColumnIndex(0);
        drawMediaIndexButton(0, editableParams.size());
        
        // Draw parameter cells (remaining columns, column indices 1+)
        for (size_t i = 0; i < editableParams.size(); i++) {
            ImGui::TableSetColumnIndex((int)i + 1);
            const auto& paramDesc = editableParams[i];
            int columnIndex = (int)i + 1; // Column index (1-based for parameters)
            
            ImGui::PushID((int)(i + 2000)); // Unique ID for parameter cells
            
            // Create ParameterCell for this parameter using helper method
            ParameterCell paramCell = createParameterCellForParameter(paramDesc);
            
            // Sync state from MediaPoolGUI to ParameterCell
            bool isSelected = (editingColumnIndex == columnIndex);
            paramCell.isSelected = isSelected;
            paramCell.setEditing(isEditingParameter && isSelected);
            if (isEditingParameter && isSelected) {
                paramCell.setEditBuffer(editBufferCache);
            }
            
            // Restore drag state if this parameter is being dragged
            bool isDraggingThis = (draggingParameter == paramDesc.name);
            if (isDraggingThis) {
                paramCell.setDragState(true, dragStartY, dragStartX, lastDragValue);
            }
            
            // Determine focus state (similar to TrackerSequencer)
            bool isFocused = (editingColumnIndex == columnIndex);
            bool shouldFocusFirst = (columnIndex == 1 && shouldFocusFirstCell);
            bool shouldRefocus = (editingColumnIndex == columnIndex && shouldRefocusCurrentCell);
            
            // Draw ParameterCell (use ImGui's ID system for unique identification)
            int uniqueId = ImGui::GetID(paramDesc.name.c_str());
            
            ParameterCellInteraction interaction = paramCell.draw(uniqueId, isFocused, shouldFocusFirst, shouldRefocus);
            
            // Sync state back from ParameterCell to MediaPoolGUI (similar to TrackerSequencer)
            if (interaction.focusChanged) {
                int previousColumn = editingColumnIndex;
                editingParameter = paramDesc.name;
                editingColumnIndex = columnIndex;
                anyCellFocusedThisFrame = true;
                isParentWidgetFocused = false;
                
                // Exit edit mode if navigating to a different column
                if (previousColumn != columnIndex && isEditingParameter) {
                    isEditingParameter = false;
                    editBufferCache.clear();
                    editBufferInitializedCache = false;
                }
            }
            
            if (interaction.clicked) {
                // Cell was clicked - focus it but don't enter edit mode yet
                editingParameter = paramDesc.name;
                editingColumnIndex = columnIndex;
                isEditingParameter = false;
                anyCellFocusedThisFrame = true;
                isParentWidgetFocused = false;
            }
            
            // Sync drag state (maintain focus during drag)
            // CRITICAL: Always check drag state if we're dragging this parameter, even if mouse is outside cell
            // This ensures drag continues across entire window (Blender-style)
            if (interaction.dragStarted || interaction.dragEnded || paramCell.getIsDragging() || isDraggingThis) {
                if (paramCell.getIsDragging()) {
                    draggingParameter = paramDesc.name;
                    dragStartY = paramCell.getDragStartY();
                    dragStartX = paramCell.getDragStartX();
                    lastDragValue = paramCell.getLastDragValue();
                    // Maintain focus during drag
                    editingColumnIndex = columnIndex;
                    editingParameter = paramDesc.name;
                } else if (isDraggingThis && !paramCell.getIsDragging()) {
                    // Drag ended - clear drag state
                    draggingParameter.clear();
                }
            }
            
            // Sync edit mode state
            if (paramCell.isEditingMode()) {
                isEditingParameter = true;
                editBufferCache = paramCell.getEditBuffer();
                editBufferInitializedCache = paramCell.isEditBufferInitialized();
            } else if (isEditingParameter && isSelected && !paramCell.isEditingMode()) {
                isEditingParameter = false;
                editBufferCache.clear();
                editBufferInitializedCache = false;
            }
            
            // Clear refocus flag after using it
            if (shouldRefocus) {
                shouldRefocusCurrentCell = false;
            }
            
            ImGui::PopID();
        }
        
        // Clear shouldFocusFirstCell flag after drawing
        if (shouldFocusFirstCell) {
            shouldFocusFirstCell = false;
        }
        
        // After drawing all cells, if column is set but no cell was focused, clear focus
        if (editingColumnIndex >= 0 && !anyCellFocusedThisFrame && !isEditingParameter && !shouldRefocusCurrentCell) {
            clearCellFocus();
        }
        
        ImGui::EndTable();
    }
    
    ImGui::PopStyleVar(2);
}

void MediaPoolGUI::clearCellFocus() {
    editingColumnIndex = -1;
    editingParameter.clear();
    isEditingParameter = false;
    editBufferCache.clear();
    editBufferInitializedCache = false;
    draggingParameter.clear();
}

// Sync edit state from ImGui focus - called from InputRouter when keys are pressed
void MediaPoolGUI::syncEditStateFromImGuiFocus(MediaPoolGUI& gui) {
    // Check if editingColumnIndex is already valid (GUI sync already happened)
    if (gui.editingColumnIndex >= 0) {
        // If editingParameter is empty but editingColumnIndex is set, look it up
        if (gui.editingParameter.empty() && gui.mediaPool) {
            auto params = gui.mediaPool->getParameters();
            std::vector<ParameterDescriptor> editableParams;
            for (const auto& param : params) {
                if (param.name != "note") {
                    editableParams.push_back(param);
                }
            }
            // editingColumnIndex is 1-based for parameters (0 = media index button)
            if (gui.editingColumnIndex > 0 && (size_t)(gui.editingColumnIndex - 1) < editableParams.size()) {
                gui.editingParameter = editableParams[gui.editingColumnIndex - 1].name;
            }
        }
        return; // Already synced
    }
    
    // GUI draw sync should handle this every frame
    // If not set, handleKeyPress will default gracefully
}

void MediaPoolGUI::drawMediaIndexButton(int columnIndex, size_t numParamColumns) {
    if (!mediaPool) return;
    
    size_t currentIndex = mediaPool->getCurrentIndex();
    size_t numPlayers = mediaPool->getNumPlayers();
    auto activePlayer = mediaPool->getActivePlayer();
    
    // Check if current index's player is the active player and is playing
    // This ensures the button only shows green when THIS media is playing
    // IMPORTANT: When sequencer triggers media, it updates currentIndex via setActivePlayer(),
    // so currentIndex should always match the active player's index
    // CRITICAL FIX: Check both isPlaying() AND mode to determine button state
    // When manually pausing, mode transitions to IDLE immediately, but isPlaying() might lag
    // When empty step triggers, both mode and isPlaying() are updated correctly
    bool isCurrentMediaPlaying = false;
    if (activePlayer != nullptr && currentIndex < numPlayers) {
        // Verify that the active player matches the current index
        auto currentPlayer = mediaPool->getMediaPlayer(currentIndex);
        if (currentPlayer == activePlayer) {
            // Check if player is actually playing
            bool playerIsPlaying = activePlayer->isPlaying();
            
            // CRITICAL FIX: Also check mode - if mode is IDLE, button should be grey
            // This ensures button turns grey immediately when manually pausing
            // (mode transitions to IDLE immediately, even if isPlaying() hasn't updated yet)
            bool isIdleMode = mediaPool->isIdle();
            bool isManualPreviewMode = mediaPool->isManualPreview();
            
            // Button is green only if:
            // 1. Player is actually playing, AND
            // 2. Mode is not IDLE (either MANUAL_PREVIEW or SEQUENCER_ACTIVE)
            // This ensures button turns grey immediately when mode becomes IDLE (manual pause)
            isCurrentMediaPlaying = playerIsPlaying && !isIdleMode;
            
            // DEBUG: Log state changes for sequencer-triggered media
            // Use a per-index static map to track state changes for each media item
            static std::map<size_t, bool> lastPlayingStateByIndex;
            bool lastState = lastPlayingStateByIndex[currentIndex];
            if (isCurrentMediaPlaying != lastState) {
                PlaybackMode currentMode = isIdleMode ? PlaybackMode::IDLE : 
                                          (isManualPreviewMode ? PlaybackMode::MANUAL_PREVIEW : PlaybackMode::SEQUENCER_ACTIVE);
                ofLogNotice("MediaPoolGUI") << "[BUTTON_STATE] Index " << currentIndex 
                                             << " playing state changed: " << lastState 
                                             << " -> " << isCurrentMediaPlaying 
                                             << " (playerIsPlaying: " << playerIsPlaying
                                             << ", mode: " << (currentMode == PlaybackMode::IDLE ? "IDLE" : 
                                                               (currentMode == PlaybackMode::MANUAL_PREVIEW ? "MANUAL_PREVIEW" : "SEQUENCER_ACTIVE")) << ")";
                lastPlayingStateByIndex[currentIndex] = isCurrentMediaPlaying;
            }
        }
    }
    
    // Format button text: show current index (1-based) or "--" if no media
    std::string indexText;
    if (numPlayers > 0) {
        int displayIndex = (int)(currentIndex + 1); // 1-based display
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d", displayIndex); // Zero-padded to 2 digits
        indexText = buf;
    } else {
        indexText = "--";
    }
    
    // Determine focus state (similar to TrackerSequencer step button)
    bool isFocused = (editingColumnIndex == columnIndex);
    bool shouldRefocus = (editingColumnIndex == columnIndex && shouldRefocusCurrentCell);
    
    // Request focus if needed (before creating button)
    if (shouldRefocus) {
        ImGui::SetKeyboardFocusHere(0);
    }
    
    // Apply button styling for active playback (similar to TrackerSequencer step button)
    if (isCurrentMediaPlaying) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImVec4(0.2f, 0.7f, 0.2f, 0.8f))); // Green active state
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImVec4(0.25f, 0.75f, 0.25f, 0.9f))); // Brighter green on hover
    }
    
    // Prevent ImGui from auto-focusing when clicking empty space
    ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
    
    // Draw button
    bool buttonClicked = ImGui::Button(indexText.c_str(), ImVec2(-1, 0));
    
    // Pop the flag after creating the button
    ImGui::PopItemFlag();
    
    // Refocus after drawing (if needed)
    if (shouldRefocus) {
        ImGui::SetKeyboardFocusHere(-1);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }
    
    // Pop button styling if we pushed it
    if (isCurrentMediaPlaying) {
        ImGui::PopStyleColor(2);
    }
    
    // Prevent spacebar from triggering button clicks (spacebar should only pause/play globally)
    bool spacebarPressed = ImGui::IsKeyPressed(ImGuiKey_Space, false);
    
    // Handle button click: toggle play/pause for current media
    // Works with both manual preview and external triggers (sequencer)
    // CRITICAL FIX: Use MediaPool mode as primary check, not isPlaying()
    // isPlaying() can lag after stop(), causing double-click issue
    // Mode is updated immediately and is more reliable for toggle logic
    // CRITICAL: Only use buttonClicked (not IsItemClicked) for proper toggle behavior
    // buttonClicked is only true on the frame when button is released (single click)
    // IsItemClicked can be true for multiple frames, causing continuous triggering
    if (buttonClicked && !spacebarPressed && numPlayers > 0) {
        // Check MediaPool mode first (more reliable than isPlaying() which can lag)
        bool isManualPreviewMode = mediaPool->isManualPreview();
        bool isSequencerActiveMode = mediaPool->isSequencerActive();
        
        // RE-CHECK playing state as secondary check (for sequencer-triggered playback)
        bool currentlyPlaying = false;
        if (activePlayer != nullptr && currentIndex < numPlayers) {
            auto currentPlayer = mediaPool->getMediaPlayer(currentIndex);
            if (currentPlayer == activePlayer) {
                currentlyPlaying = activePlayer->isPlaying();
            }
        }
        
        ofLogNotice("MediaPoolGUI") << "[BUTTON_DEBUG] Button clicked - isCurrentMediaPlaying: " << isCurrentMediaPlaying 
                                     << ", currentlyPlaying (rechecked): " << currentlyPlaying
                                     << ", isManualPreviewMode: " << isManualPreviewMode
                                     << ", isSequencerActiveMode: " << isSequencerActiveMode
                                     << ", currentIndex: " << currentIndex 
                                     << ", activePlayer: " << (activePlayer ? "exists" : "null");
        
        // Toggle logic: Use mode as primary check (more reliable than isPlaying())
        // If in MANUAL_PREVIEW mode, stop. Otherwise, play.
        if (isManualPreviewMode && currentlyPlaying) {
            // Manual preview mode and playing - stop it
            if (activePlayer && currentIndex < numPlayers) {
                auto currentPlayer = mediaPool->getMediaPlayer(currentIndex);
                if (currentPlayer == activePlayer) {
                    mediaPool->stopManualPreview();
                }
            }
        } else if (isSequencerActiveMode && currentlyPlaying) {
            // Sequencer active and playing - just stop player (sequencer controls mode)
            if (activePlayer && currentIndex < numPlayers) {
                auto currentPlayer = mediaPool->getMediaPlayer(currentIndex);
                if (currentPlayer == activePlayer) {
                    currentPlayer->stop();
                }
            }
        } else {
            // Not in manual preview mode or not playing - start playback
            // This works regardless of current mode (IDLE, SEQUENCER_ACTIVE, etc.)
            // playMediaManual() will transition to MANUAL_PREVIEW mode and stop any other playback
            auto currentPlayer = mediaPool->getMediaPlayer(currentIndex);
            if (currentPlayer) {
                // Get current position if media was previously playing, otherwise start from beginning
                float startPosition = 0.0f;
                if (currentPlayer == activePlayer) {
                    // Use current position if this is the active player
                    startPosition = currentPlayer->position.get();
                }
                // Always call playMediaManual - it handles stopping other players and mode transitions
                // This works regardless of current mode (IDLE, SEQUENCER_ACTIVE, etc.)
                ofLogNotice("MediaPoolGUI") << "[BUTTON_DEBUG] Calling playMediaManual for index " << currentIndex 
                                             << " at position " << startPosition;
                bool success = mediaPool->playMediaManual(currentIndex, startPosition);
                if (!success) {
                    ofLogWarning("MediaPoolGUI") << "[BUTTON_DEBUG] Failed to start manual playback for index " << currentIndex;
                } else {
                    // CRITICAL FIX: Re-check playing state immediately after playMediaManual
                    // This ensures button state updates correctly even if there's a timing issue
                    // Also fixes the double-click issue where playMediaManual succeeds but player isn't playing yet
                    auto playerAfterPlay = mediaPool->getMediaPlayer(currentIndex);
                    bool isPlayingAfterCall = (playerAfterPlay && playerAfterPlay->isPlaying());
                    ofLogNotice("MediaPoolGUI") << "[BUTTON_DEBUG] Button clicked: Started manual playback for index " << currentIndex 
                                                 << " - isPlaying() after call: " << isPlayingAfterCall
                                                 << " - player: " << (playerAfterPlay ? "exists" : "null");
                    
                    // If playMediaManual succeeded but player isn't playing, log a warning
                    // This helps debug the double-click issue
                    if (!isPlayingAfterCall) {
                        ofLogWarning("MediaPoolGUI") << "[BUTTON_DEBUG] WARNING: playMediaManual returned success but player is not playing! "
                                                      << "This may cause the double-click issue. "
                                                      << "activePlayer: " << (mediaPool->getActivePlayer() ? "exists" : "null")
                                                      << ", currentIndex: " << currentIndex
                                                      << ", startPosition: " << startPosition;
                    }
                }
            } else {
                ofLogWarning("MediaPoolGUI") << "No player found for index " << currentIndex;
            }
        }
    }
    
    // ONE-WAY SYNC: ImGui focus → MediaPoolGUI state (similar to TrackerSequencer)
    bool actuallyFocused = ImGui::IsItemFocused();
    if (actuallyFocused) {
        bool itemWasClicked = ImGui::IsItemClicked(0);
        bool keyboardNavActive = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
        
        // Only sync if this is an intentional focus (click, keyboard nav, or refocus)
        if (itemWasClicked || keyboardNavActive || shouldRefocus) {
            anyCellFocusedThisFrame = true;
            bool columnChanged = (editingColumnIndex != columnIndex);
            
            // When in edit mode, prevent focus from changing to a different column
            if (isEditingParameter && columnChanged) {
                return; // Exit early - keep focus locked to editing column
            }
            
            // Sync state
            editingColumnIndex = columnIndex;
            editingParameter.clear(); // Media index button doesn't have a parameter name
            isParentWidgetFocused = false;
            
            // Clear refocus flag after successful sync
            if (shouldRefocus) {
                shouldRefocusCurrentCell = false;
            }
            
            // Exit edit mode if navigating to a different column
            if (columnChanged && isEditingParameter) {
                isEditingParameter = false;
                editBufferCache.clear();
                editBufferInitializedCache = false;
            }
        }
    }
}

bool MediaPoolGUI::handleKeyPress(int key, bool ctrlPressed, bool shiftPressed) {
    // CRITICAL FIX: If editingColumnIndex is set but editingParameter is empty,
    // look up the parameter name from the column index
    // This handles cases where focus was synced from ImGui but editingParameter wasn't set yet
    if (editingColumnIndex > 0 && editingParameter.empty() && mediaPool) {
        auto params = mediaPool->getParameters();
        // Filter out "note" parameter (it's not editable in the GUI)
        std::vector<ParameterDescriptor> editableParams;
        for (const auto& param : params) {
            if (param.name != "note") {
                editableParams.push_back(param);
            }
        }
        // editingColumnIndex is 1-based for parameters (0 = media index button)
        if (editingColumnIndex > 0 && (size_t)(editingColumnIndex - 1) < editableParams.size()) {
            editingParameter = editableParams[editingColumnIndex - 1].name;
        }
    }
    
    // Handle direct typing (numeric keys, decimal point, operators) - auto-enter edit mode
    // This matches TrackerSequencer behavior: typing directly enters edit mode
    if ((key >= '0' && key <= '9') || key == '.' || key == '-' || key == '+' || key == '*' || key == '/') {
        // Check if we have a valid parameter column focused (not media index button)
        if (!isEditingParameter && editingColumnIndex > 0) {
            // Find the parameter descriptor
            auto params = mediaPool->getParameters();
            const ParameterDescriptor* paramDesc = nullptr;
            
            // If editingParameter is set, use it; otherwise look up by column index
            if (!editingParameter.empty()) {
                for (const auto& param : params) {
                    if (param.name == editingParameter) {
                        paramDesc = &param;
                        break;
                    }
                }
            } else {
                // Look up by column index
                std::vector<ParameterDescriptor> editableParams;
                for (const auto& param : params) {
                    if (param.name != "note") {
                        editableParams.push_back(param);
                    }
                }
                if (editingColumnIndex > 0 && (size_t)(editingColumnIndex - 1) < editableParams.size()) {
                    paramDesc = &editableParams[editingColumnIndex - 1];
                    editingParameter = paramDesc->name; // Set it for future use
                }
            }
            
            if (paramDesc) {
                // Enter edit mode first (similar to TrackerSequencer)
                isEditingParameter = true;
                
                // Disable ImGui keyboard navigation when entering edit mode
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
                
                // Now handle the key via helper method (which will create cell, enter edit mode, and handle key)
                // The helper method will sync state back
                return handleParameterCellKeyPress(*paramDesc, key, ctrlPressed, shiftPressed);
            }
        }
    }
    
    // If a parameter is selected and in edit mode, delegate to ParameterCell
    if (!editingParameter.empty() && isEditingParameter) {
        // Find the parameter descriptor
        auto params = mediaPool->getParameters();
        const ParameterDescriptor* paramDesc = nullptr;
        for (const auto& param : params) {
            if (param.name == editingParameter) {
                paramDesc = &param;
                break;
            }
        }
        
        if (!paramDesc) return false;
        
        // Use helper method to handle key press (reduces duplication)
        return handleParameterCellKeyPress(*paramDesc, key, ctrlPressed, shiftPressed);
    }
    
    // Handle arrow key navigation (similar to TrackerSequencer)
    // CRITICAL: In edit mode, arrow keys adjust values (don't navigate)
    if (isEditingParameter && editingColumnIndex >= 0) {
        // In edit mode: Arrow keys adjust values
        if (key == OF_KEY_UP || key == OF_KEY_DOWN || key == OF_KEY_LEFT || key == OF_KEY_RIGHT) {
            // Ensure we have the parameter descriptor
            if (!editingParameter.empty()) {
                auto params = mediaPool->getParameters();
                const ParameterDescriptor* paramDesc = nullptr;
                for (const auto& param : params) {
                    if (param.name == editingParameter) {
                        paramDesc = &param;
                        break;
                    }
                }
                if (paramDesc) {
                    return handleParameterCellKeyPress(*paramDesc, key, ctrlPressed, shiftPressed);
                }
            }
        }
    }
    
    // CRITICAL: When NOT in edit mode, let ImGui handle arrow keys for native navigation
    // We'll sync our state from ImGui focus after it processes the keys
    if (!isEditingParameter && editingColumnIndex >= 0) {
        // Not in edit mode: Let ImGui handle arrow keys for native navigation
        // This allows smooth navigation between parameter cells
        // Our state will be synced from ImGui focus in the next frame during draw()
        if (key == OF_KEY_LEFT || key == OF_KEY_RIGHT || key == OF_KEY_UP || key == OF_KEY_DOWN) {
            // Ensure ImGui navigation is enabled so it can handle the keys
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            
            // Don't consume the key - let ImGui handle navigation
            // Our state will be synced from ImGui focus in drawParameters()
            return false;
        }
    }
    
    // Handle keyboard shortcuts for parameter navigation
    switch (key) {
        case OF_KEY_RETURN:
            if (ctrlPressed || shiftPressed) {
                // Ctrl+Enter or Shift+Enter: Exit parameter editing
                if (isEditingParameter) {
                    isEditingParameter = false;
                    editBufferCache.clear();
                    editBufferInitializedCache = false;
                    ImGuiIO& io = ImGui::GetIO();
                    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                    shouldRefocusCurrentCell = true; // Refocus cell after exiting edit mode
                    return true;
                }
            }
            if (editingColumnIndex > 0 && !isEditingParameter) {
                // Enter on selected parameter column: Enter edit mode
                // (Column 0 is media index button, which doesn't support editing)
                
                // Ensure editingParameter is set
                if (editingParameter.empty() && mediaPool) {
                    auto params = mediaPool->getParameters();
                    std::vector<ParameterDescriptor> editableParams;
                    for (const auto& param : params) {
                        if (param.name != "note") {
                            editableParams.push_back(param);
                        }
                    }
                    if (editingColumnIndex > 0 && (size_t)(editingColumnIndex - 1) < editableParams.size()) {
                        editingParameter = editableParams[editingColumnIndex - 1].name;
                    }
                }
                
                if (!editingParameter.empty()) {
                    isEditingParameter = true;
                    ImGuiIO& io = ImGui::GetIO();
                    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
                    return true;
                }
            }
            break;
            
        case OF_KEY_ESC:
            if (isEditingParameter) {
                isEditingParameter = false;
                editBufferCache.clear();
                editBufferInitializedCache = false;
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                shouldRefocusCurrentCell = true; // Refocus cell after exiting edit mode
                return true;
            }
            break;
    }
    
    return false;
}
