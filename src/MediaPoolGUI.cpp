#include "MediaPoolGUI.h"
#include "MediaPool.h"
#include "MediaPlayer.h"

MediaPoolGUI::MediaPoolGUI() 
    : mediaPool(nullptr), waveformHeight(100.0f), parentWidgetId(0), 
      isParentWidgetFocused(false), requestFocusMoveToParentWidget(false) {
    // Initialize search buffer
    memset(searchBuffer, 0, sizeof(searchBuffer));
    searchFilter = "";
}

void MediaPoolGUI::setMediaPool(MediaPool& pool) {
    mediaPool = &pool;
}

void MediaPoolGUI::draw() {
    drawDirectoryControls();
    drawSearchBar();
    
    // Reserve space for waveform at bottom, then draw media list in remaining space
    float availableHeight = ImGui::GetContentRegionAvail().y;
    int waveformSpace = waveformHeight + 25; // Extra space for waveform controls
    
    // Draw media list in scrollable area wita reserved space for waveform
    ImGui::BeginChild("MediaList", ImVec2(0, availableHeight - waveformSpace), true);
    drawMediaList();
    ImGui::EndChild();
    
    // Draw waveform at bottom
    drawWaveform();
}

void MediaPoolGUI::drawDirectoryControls() {
    ImGui::Text("Directory:");
    ImGui::SameLine();
    std::string displayPath = mediaPool->getDataDirectory();
    if (displayPath.length() > 50) {
        displayPath = "..." + displayPath.substr(displayPath.length() - 47);
    }
    ImGui::Text("%s", displayPath.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Browse Directory")) {
        mediaPool->browseForDirectory();
    }
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
    // If we just requested focus move above, the flag is already set, but we verify here
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
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1.0f, 0.5f, 0.0f, 0.4f)); // Orange background for active
                }
                if (isPlaying) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.7f, 0.7f, 1.0f)); // Green for playing
                }
                
                // Make items selectable and clickable
                if (ImGui::Selectable(displayName.c_str(), isActive)) {
                    // Use playMediaManual to set MANUAL_PREVIEW mode and play from start
                    bool success = mediaPool->playMediaManual(i, 0.0f);  // Always play from start (position 0.0)
                    if (!success) {
                        ofLogWarning("MediaPoolGUI") << "Failed to play media at index " << i;
                    }
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
