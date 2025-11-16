#include "MediaPoolGUI.h"
#include "MediaPool.h"  // Includes PlayStyle enum
#include "MediaPlayer.h"
#include "ParameterCell.h"
#include "Module.h"
#include "core/ModuleRegistry.h"
#include "gui/GUIConstants.h"
#include "gui/MediaPreview.h"

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
    // Legacy method: set direct pointer (for backward compatibility)
    mediaPool = &pool;
}

MediaPool* MediaPoolGUI::getMediaPool() const {
    // If instance-aware (has registry and instanceName), use that
    if (getRegistry() && !getInstanceName().empty()) {
        auto module = getRegistry()->getModule(getInstanceName());
        if (!module) return nullptr;
        return dynamic_cast<MediaPool*>(module.get());
    }
    
    // Fallback to legacy direct pointer (for backward compatibility)
    return mediaPool;
}

std::string MediaPoolGUI::truncateTextToWidth(const std::string& text, float maxWidth, bool showEnd, const std::string& ellipsis) {
    if (maxWidth <= 0.0f) return text;
    
    ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    if (textSize.x <= maxWidth) return text;
    
    float ellipsisWidth = ImGui::CalcTextSize(ellipsis.c_str()).x;
    float maxTextWidth = maxWidth - ellipsisWidth;
    
    if (showEnd) {
        // Truncate from start: show end of text with ellipsis prefix
        std::string result = text;
        while (result.length() > 0) {
            ImVec2 testSize = ImGui::CalcTextSize(result.c_str());
            if (testSize.x <= maxTextWidth) break;
            result = result.substr(1); // Remove first character
        }
        return ellipsis + result;
    } else {
        // Truncate from end: show start of text with ellipsis suffix
        // Quick estimate to reduce iterations for very long strings
        float avgCharWidth = textSize.x / text.length();
        int estimatedChars = (int)(maxTextWidth / avgCharWidth);
        std::string result = text.substr(0, std::max(0, estimatedChars - 1));
        
        // Refine by checking actual width (usually only 1-2 iterations needed)
        while (result.length() > 0) {
            ImVec2 testSize = ImGui::CalcTextSize(result.c_str());
            if (testSize.x <= maxTextWidth) break;
            result.pop_back();
        }
        
        return result + ellipsis;
    }
}

void MediaPoolGUI::draw() {
    // Call base class draw (handles visibility, title bar, enabled state)
    ModuleGUI::draw();
}

// Helper function to draw waveform preview in tooltip
// Now uses shared MediaPreview
void MediaPoolGUI::drawWaveformPreview(MediaPlayer* player, float width, float height) {
    MediaPreview::drawWaveformPreview(player, width, height);
}

void MediaPoolGUI::drawContent() {
    // Get current MediaPool instance (handles null case)
    MediaPool* pool = getMediaPool();
    if (!pool) {
        ImGui::Text("Instance '%s' not found", getInstanceName().empty() ? "unknown" : getInstanceName().c_str());
        return;
    }
    
    // Draw parameter editing section
    drawParameters();

    // Draw waveform on top
    drawWaveform(); 
    
    // Calculate space needed for bottom controls (search bar + directory controls + separators)
    // Estimate: search bar (~ImGui::GetTextLineHeight() + padding), directory controls (~button height + padding), separators
    float bottomControlsHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y * 2 + 
                                 ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y + 
                                 ImGui::GetStyle().ItemSpacing.y * 2; // Separators and spacing
    
    // Get remaining space after waveform (already accounts for waveform height)
    float availableHeight = ImGui::GetContentRegionAvail().y;
    
    // Draw media list in scrollable area, reserving space for bottom controls
    // Use availableHeight minus bottom controls height, but ensure minimum size
    float mediaListHeight = availableHeight - bottomControlsHeight;
    float minMediaListHeight = ImGui::GetFrameHeight(); // Minimum height is one media line
    if (mediaListHeight < minMediaListHeight) {
        mediaListHeight = minMediaListHeight;
    }
    
    ImGui::BeginChild("MediaList", ImVec2(0, mediaListHeight), true);
    drawMediaList();
    ImGui::EndChild();
    
    drawSearchBar();
    drawDirectoryControls();
}

void MediaPoolGUI::drawDirectoryControls() {
    MediaPool* pool = getMediaPool();
    if (!pool) return;
    
    // Browse Directory button - opens native directory browser
    if (ImGui::Button("Browse Directory")) {
        pool->browseForDirectory();
    }
    
    ImGui::SameLine();
    std::string displayPath = pool->getDataDirectory();
    
    // Calculate available width after button (account for spacing)
    float availableWidth = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x;
    if (availableWidth > 0.0f) {
        // For directory paths, show the end (directory name) rather than the beginning
        displayPath = truncateTextToWidth(displayPath, availableWidth, true);
    }
    
    ImGui::Text("%s", displayPath.c_str());
    ImGui::Separator();
}

void MediaPoolGUI::drawSearchBar() {
    ImGui::Separator();
    ImGui::Text("Search:");
    ImGui::SameLine();
    // Add a thin grey outline to the search bar
    ImVec2 frameMin = ImGui::GetCursorScreenPos();
    ImVec2 frameMax = ImVec2(frameMin.x + ImGui::CalcItemWidth(), frameMin.y + ImGui::GetFrameHeight());
    bool edited = ImGui::InputText("##search", searchBuffer, sizeof(searchBuffer));
    ImGui::GetWindowDrawList()->AddRect(frameMin, frameMax, GUIConstants::toU32(GUIConstants::Border::SearchBar), 1.0f, 0, 1.0f);
    if (edited) {
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
    ImGui::InvisibleButton("##MediaListParent", ImVec2(100, 5));
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
    
    MediaPool* pool = getMediaPool();
    if (!pool) return;
    
    // Get current index for auto-scrolling
    size_t currentIndex = pool->getCurrentIndex();
    
    // Track if index changed to determine if we should sync scroll
    bool shouldSyncScroll = (currentIndex != previousMediaIndex);
    
    // Show indexed media list with actual file names
    if (pool->getNumPlayers() > 0) {
        auto playerNames = pool->getPlayerNames();
        auto playerFileNames = pool->getPlayerFileNames();
        
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
            
            auto player = pool->getMediaPlayer(i);
            if (player) {
                // Check if this is the currently active player
                bool isActive = (pool->getActivePlayer() == player);
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
                
                // Truncate title if too long for available width
                float availableWidth = ImGui::GetContentRegionAvail().x;
                if (availableWidth > 0.0f) {
                    // Calculate width of prefix: "[01] [AV] "
                    std::string prefix = indexStr + " " + mediaType + " ";
                    float prefixWidth = ImGui::CalcTextSize(prefix.c_str()).x;
                    float maxTitleWidth = availableWidth - prefixWidth - 20.0f; // Reserve padding
                    
                    if (maxTitleWidth > 0.0f) {
                        title = truncateTextToWidth(title, maxTitleWidth);
                    }
                }
                
                // Clean display name: [01] [AV] Title
                std::string displayName = indexStr + " " + mediaType + " " + title;
                
                // Visual styling for active and playing media
                if (isActive) {
                    ImGui::PushStyleColor(ImGuiCol_Header, GUIConstants::Active::MediaItem);
                }
                if (isPlaying) {
                    ImGui::PushStyleColor(ImGuiCol_Text, GUIConstants::Text::Playing);
                }
                
                // Make items selectable and clickable
                if (ImGui::Selectable(displayName.c_str(), isActive)) {
                    // Use playMediaManual to set MANUAL_PREVIEW mode and play from start
                    bool success = getMediaPool()->playMediaManual(i, 0.0f);  // Always play from start (position 0.0)
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
                
                // Add hover tooltip with video frame preview and/or audio waveform
                if (ImGui::IsItemHovered()) {
                    // Use shared preview utility
                    MediaPreview::drawMediaTooltip(player, static_cast<int>(i));
                    ImGui::Text("Click to play/pause");
                }
                
                // Add right-click context menu
                if (ImGui::BeginPopupContextItem(("MediaContext" + std::to_string(i)).c_str())) {
                    ImGui::Text("Media %zu", i);
                    ImGui::Separator();
                    
                    if (ImGui::MenuItem("Play/Preview", "Click")) {
                        // Stop all other players first
                        for (size_t j = 0; j < getMediaPool()->getNumPlayers(); j++) {
                            auto otherPlayer = getMediaPool()->getMediaPlayer(j);
                            if (otherPlayer && otherPlayer != player) {
                                otherPlayer->stop();
                            }
                        }
                        getMediaPool()->setActivePlayer(i);
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
    auto currentPlayer = getMediaPool()->getActivePlayer();
    if (!currentPlayer) {
        ImGui::Text("No active player to display waveform.");
        return;
    }
    
    // Get current media index for per-index zoom state
    size_t currentIndex = getMediaPool()->getCurrentIndex();
    auto zoomState = getWaveformZoomState(currentIndex);
    float waveformZoom = zoomState.first;
    float waveformOffset = zoomState.second;
    
    // Create invisible button for interaction area
    // CRITICAL: Ensure non-zero size for InvisibleButton (ImGui assertion requirement)
    // This can fail during initial window setup before layout is complete
    float safeHeight = std::max(waveformHeight, 1.0f);
    float availableWidth = ImGui::GetContentRegionAvail().x;
    
    // Ensure both dimensions are positive (required by ImGui)
    // Use fallback values if window isn't ready yet
    if (availableWidth <= 0.0f) {
        availableWidth = 100.0f; // Fallback minimum width
    }
    // safeHeight is already guaranteed to be >= 1.0f
    
    ImVec2 canvasSize = ImVec2(availableWidth, safeHeight);
    ImGui::InvisibleButton("waveform_canvas", canvasSize);
    
    // Get draw list for custom rendering
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetItemRectMin();
    ImVec2 canvasMax = ImGui::GetItemRectMax();
    float canvasWidth = canvasMax.x - canvasPos.x;
    float canvasHeight = canvasMax.y - canvasPos.y;
    float centerY = canvasPos.y + canvasHeight * 0.5f;
    
    // CRITICAL: Check if dragging a ParameterCell - prevents interference with waveform interactions
    bool isDraggingParameter = !draggingParameter.empty();
    
    // Handle zoom and pan interactions
    if (ImGui::IsItemHovered() && !isDraggingParameter) {
        // Mouse wheel for zoom (centered on mouse position)
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            // Get mouse position relative to canvas
            ImVec2 mousePos = ImGui::GetMousePos();
            float mouseX = mousePos.x - canvasPos.x;
            float mouseTime = mouseX / canvasWidth; // Time position under mouse (0-1)
            
            // Calculate visible time range
            float visibleRange = 1.0f / waveformZoom;
            float visibleStart = waveformOffset;
            float mouseTimeAbsolute = visibleStart + mouseTime * visibleRange;
            
            // Zoom factor (1.2x per scroll step)
            float zoomFactor = (wheel > 0.0f) ? 1.2f : 1.0f / 1.2f;
            float newZoom = waveformZoom * zoomFactor;
            newZoom = std::max(1.0f, std::min(100.0f, newZoom)); // Clamp zoom
            
            // Calculate new offset to keep mouse position fixed
            float newVisibleRange = 1.0f / newZoom;
            float newOffset = mouseTimeAbsolute - mouseTime * newVisibleRange;
            newOffset = std::max(0.0f, std::min(1.0f - newVisibleRange, newOffset));
            
            // Store updated zoom state for current index
            setWaveformZoomState(currentIndex, newZoom, newOffset);
            waveformZoom = newZoom;
            waveformOffset = newOffset;
        }
        
        // Handle panning with middle mouse or Shift+drag (only if not dragging a marker or ParameterCell)
        bool isPanning = false;
        if (draggingMarker == WaveformMarker::NONE) {
            isPanning = ImGui::IsMouseDown(2) || (ImGui::IsMouseDragging(0) && ImGui::GetIO().KeyShift);
        }
        if (isPanning) {
            ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGui::IsMouseDown(2) ? 2 : 0);
            if (dragDelta.x != 0.0f) {
                float visibleRange = 1.0f / waveformZoom;
                
                // Pan by drag distance (normalized to time range)
                float panDelta = -dragDelta.x / canvasWidth * visibleRange;
                float newOffset = waveformOffset + panDelta;
                newOffset = std::max(0.0f, std::min(1.0f - visibleRange, newOffset));
                
                // Store updated offset for current index
                setWaveformZoomState(currentIndex, waveformZoom, newOffset);
                waveformOffset = newOffset;
                
                ImGui::ResetMouseDragDelta(ImGui::IsMouseDown(2) ? 2 : 0);
            }
        }
        
        // Double-click to reset zoom
        if (ImGui::IsMouseDoubleClicked(0)) {
            setWaveformZoomState(currentIndex, 1.0f, 0.0f);
            waveformZoom = 1.0f;
            waveformOffset = 0.0f;
        }
    }
    
    // Calculate visible time range
    float visibleRange = 1.0f / waveformZoom;
    float visibleStart = waveformOffset;
    float visibleEnd = waveformOffset + visibleRange;
    
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
            
            // Calculate how many points we need for visible range
            // When zoomed out (visibleRange = 1.0): use base precision (MAX_WAVEFORM_POINTS)
            // When zoomed in (visibleRange < 1.0): increase precision proportionally to zoom level
            // This ensures better detail when zoomed in while maintaining performance when zoomed out
            // Formula: base points for visible range * (1 + zoom bonus)
            // The zoom bonus increases precision when zoomed in, allowing more points per visible range
            float zoomLevel = 1.0f / visibleRange; // Convert visibleRange back to zoom level (1.0 = no zoom, 10.0 = 10x zoom)
            float zoomPrecisionBonus = (zoomLevel - 1.0f) * ZOOM_PRECISION_MULTIPLIER; // Bonus precision when zoomed in
            float precisionMultiplier = 1.0f + zoomPrecisionBonus;
            // Base points for visible range, multiplied by precision to get more detail when zoomed
            int maxPoints = (int)(MAX_WAVEFORM_POINTS * visibleRange * precisionMultiplier);
            maxPoints = std::max(MIN_WAVEFORM_POINTS, std::min(MAX_WAVEFORM_POINTS, maxPoints));
            
            int stepSize = std::max(1, numFrames / maxPoints);
            actualPoints = std::min(maxPoints, numFrames / stepSize);
            
            timeData.resize(actualPoints);
            channelData.resize(numChannels);
            for (int ch = 0; ch < numChannels; ch++) {
                channelData[ch].resize(actualPoints);
            }
            
            // Downsample audio data (only for visible range if zoomed in)
            for (int i = 0; i < actualPoints; i++) {
                // Map point index to time position within visible range
                float timePos = (float)i / (float)actualPoints;
                float absoluteTime = visibleStart + timePos * visibleRange;
                
                // Clamp to valid range
                absoluteTime = std::max(0.0f, std::min(1.0f, absoluteTime));
                
                // Map to sample index
                int sampleIndex = (int)(absoluteTime * numFrames);
                sampleIndex = std::max(0, std::min(numFrames - 1, sampleIndex));
                
                timeData[i] = timePos; // Normalized time within visible range (0-1)
                
                for (int ch = 0; ch < numChannels; ch++) {
                    channelData[ch][i] = buffer.getSample(sampleIndex, ch);
                }
            }
        }
    }
    
    // Draw fallback transparent black rectangle for video-only players
    ImU32 bgColor = GUIConstants::toIM_COL32(GUIConstants::Background::Waveform);
    drawList->AddRectFilled(canvasPos, canvasMax, bgColor);
    
    if (hasAudioData) {
        // Draw actual waveform
        float amplitudeScale = canvasHeight * WAVEFORM_AMPLITUDE_SCALE;
        
        // Get volume to scale waveform amplitude proportionally
        float volume = currentPlayer->volume.get();
        
        // Draw each channel with white color
        for (int ch = 0; ch < numChannels; ch++) {
            ImU32 lineColor = GUIConstants::toU32(GUIConstants::Waveform::Line);
            
            for (int i = 0; i < actualPoints - 1; i++) {
                float x1 = canvasPos.x + timeData[i] * canvasWidth;
                float y1 = centerY - channelData[ch][i] * volume * amplitudeScale;
                float x2 = canvasPos.x + timeData[i + 1] * canvasWidth;
                float y2 = centerY - channelData[ch][i + 1] * volume * amplitudeScale;
                
                drawList->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), lineColor, 1.0f);
            }
        }
    }
    
    // Draw controls (markers) on top of waveform
    drawWaveformControls(canvasPos, canvasMax, canvasWidth, canvasHeight);
}

void MediaPoolGUI::drawWaveformControls(const ImVec2& canvasPos, const ImVec2& canvasMax, float canvasWidth, float canvasHeight) {
    MediaPool* pool = getMediaPool();
    if (!pool) return;
    
    auto currentPlayer = pool->getActivePlayer();
    if (!currentPlayer) return;
    
    // CRITICAL: Check if dragging a ParameterCell - prevents interference with waveform interactions
    bool isDraggingParameter = !draggingParameter.empty();
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Get current media index for per-index zoom state
    size_t currentIndex = getMediaPool()->getCurrentIndex();
    auto zoomState = getWaveformZoomState(currentIndex);
    float waveformZoom = zoomState.first;
    float waveformOffset = zoomState.second;
    
    // Get current parameter values
    float playheadPos = currentPlayer->playheadPosition.get(); // Absolute position
    float startPosRelative = currentPlayer->startPosition.get(); // Relative position (0.0-1.0 within region)
    float regionStart = currentPlayer->regionStart.get();
    float regionEnd = currentPlayer->regionEnd.get();
    
    // Ensure region bounds are valid (start <= end)
    if (regionStart > regionEnd) {
        std::swap(regionStart, regionEnd);
    }
    
    // Map relative startPosition to absolute position for display
    float regionSize = regionEnd - regionStart;
    float startPosAbsolute = 0.0f;
    if (regionSize > 0.001f) {
        startPosAbsolute = regionStart + startPosRelative * regionSize;
    } else {
        startPosAbsolute = std::max(0.0f, std::min(1.0f, startPosRelative));
    }
    
    // Calculate visible time range (accounting for zoom)
    float visibleRange = 1.0f / waveformZoom;
    float visibleStart = waveformOffset;
    
    // Helper lambda to map absolute time position to screen X coordinate
    auto mapToScreenX = [&](float absolutePos) -> float {
        if (absolutePos < visibleStart || absolutePos > visibleStart + visibleRange) {
            return -1000.0f; // Off-screen (negative value to indicate off-screen)
        }
        float relativePos = (absolutePos - visibleStart) / visibleRange;
        return canvasPos.x + relativePos * canvasWidth;
    };
    
    // Calculate marker positions in screen space (accounting for zoom)
    float playheadX = mapToScreenX(playheadPos);
    float positionX = mapToScreenX(startPosAbsolute);
    float regionStartX = mapToScreenX(regionStart);
    float regionEndX = mapToScreenX(regionEnd);
    
    // Marker hit detection threshold (pixels)
    const float MARKER_HIT_THRESHOLD = 8.0f;
    
    // Check if waveform canvas is hovered/active for interaction
    bool isCanvasHovered = ImGui::IsItemHovered();
    bool isCanvasActive = ImGui::IsItemActive();
    ImVec2 mousePos = ImGui::GetMousePos();
    float mouseX = mousePos.x;
    
    // Map screen X to absolute time (accounting for zoom/pan)
    float relativeX = (mouseX - canvasPos.x) / canvasWidth;
    relativeX = visibleStart + relativeX * visibleRange; // Convert to absolute time
    relativeX = std::max(0.0f, std::min(1.0f, relativeX));
    
    // Detect which marker is closest to mouse (for dragging)
    // Only check markers that are on-screen
    WaveformMarker hoveredMarker = WaveformMarker::NONE;
    if (isCanvasHovered || isCanvasActive) {
        float minDist = MARKER_HIT_THRESHOLD;
        
        // Check region start (only if on-screen)
        if (regionStartX >= 0.0f) {
            float dist = std::abs(mouseX - regionStartX);
            if (dist < minDist) {
                minDist = dist;
                hoveredMarker = WaveformMarker::REGION_START;
            }
        }
        
        // Check region end (only if on-screen)
        if (regionEndX >= 0.0f) {
            float dist = std::abs(mouseX - regionEndX);
            if (dist < minDist) {
                minDist = dist;
                hoveredMarker = WaveformMarker::REGION_END;
            }
        }
        
        // Check position marker (only if on-screen)
        if (positionX >= 0.0f) {
            float dist = std::abs(mouseX - positionX);
            if (dist < minDist) {
                minDist = dist;
                hoveredMarker = WaveformMarker::POSITION;
            }
        }
        
        // Playhead is not draggable, but we can still seek by clicking
    }
    
    // Handle mouse interaction
    // CRITICAL: Don't process waveform mouse interactions when dragging a ParameterCell
    // This prevents interference between parameter dragging and waveform interactions
    // (isDraggingParameter is already defined above)
    if ((isCanvasHovered || isCanvasActive) && !isDraggingParameter) {
        // Update cursor based on hovered marker
        if (hoveredMarker != WaveformMarker::NONE) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        } else {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        
        // Start dragging
        if (ImGui::IsMouseClicked(0)) {
            if (hoveredMarker != WaveformMarker::NONE) {
                draggingMarker = hoveredMarker;
                waveformDragStartX = mouseX;
            } else {
                // Click on empty area: seek playhead to position
                // CRITICAL: Only update playheadPosition, not startPosition (decoupled)
                // startPosition should only change when explicitly set (e.g., dragging position marker)
                auto player = getMediaPool()->getActivePlayer();
                if (player) {
                    if (player->isPlaying()) {
                        // During playback: seek playhead only (scrubbing)
                        if (player->isAudioLoaded()) {
                            player->getAudioPlayer().setPosition(relativeX);
                        }
                        if (player->isVideoLoaded()) {
                            player->getVideoPlayer().getVideoFile().setPosition(relativeX);
                            player->getVideoPlayer().getVideoFile().update();
                        }
                        player->playheadPosition.set(relativeX);
                    } else {
                        // Not playing: only update playheadPosition for visual feedback (scrubbing)
                        // Do NOT update startPosition or trigger playback - just move the playhead indicator
                        player->playheadPosition.set(relativeX);
                    }
                }
            }
        }
        
        // Continue dragging
        if (draggingMarker != WaveformMarker::NONE && ImGui::IsMouseDragging(0)) {
            auto player = getMediaPool()->getActivePlayer();
            if (player) {
                switch (draggingMarker) {
                    case WaveformMarker::REGION_START: {
                        float newStart = relativeX;
                        // Clamp to [0, regionEnd]
                        newStart = std::max(0.0f, std::min(regionEnd, newStart));
                        player->regionStart.set(newStart);
                        getMediaPool()->setParameter("regionStart", newStart, true);
                        break;
                    }
                    case WaveformMarker::REGION_END: {
                        float newEnd = relativeX;
                        // Clamp to [regionStart, 1]
                        newEnd = std::max(regionStart, std::min(1.0f, newEnd));
                        player->regionEnd.set(newEnd);
                        getMediaPool()->setParameter("regionEnd", newEnd, true);
                        break;
                    }
                    case WaveformMarker::POSITION: {
                        // Update startPosition (position marker) - map absolute to relative within region
                        float regionStartVal = player->regionStart.get();
                        float regionEndVal = player->regionEnd.get();
                        float regionSize = regionEndVal - regionStartVal;
                        
                        float relativePos = 0.0f;
                        if (regionSize > 0.001f) {
                            // Clamp to region bounds, then map to relative
                            float clampedAbsolute = std::max(regionStartVal, std::min(regionEndVal, relativeX));
                            relativePos = (clampedAbsolute - regionStartVal) / regionSize;
                            relativePos = std::max(0.0f, std::min(1.0f, relativePos));
                        } else {
                            relativePos = std::max(0.0f, std::min(1.0f, relativeX));
                        }
                        
                        player->startPosition.set(relativePos);
                        if (!player->isPlaying()) {
                            // Update playheadPosition to show absolute position
                            float absolutePos = (regionSize > 0.001f) ? 
                                (regionStartVal + relativePos * regionSize) : relativePos;
                            player->playheadPosition.set(absolutePos);
                        }
                        getMediaPool()->setParameter("position", relativePos, true);
                        break;
                    }
                    default:
                        break;
                }
            }
        }
        
        // Stop dragging
        if (ImGui::IsMouseReleased(0)) {
            draggingMarker = WaveformMarker::NONE;
        }
        
        // Handle scrubbing during playback (when dragging playhead area)
        // CRITICAL: Scrubbing only affects playheadPosition, not startPosition (decoupled)
        // startPosition should remain unchanged during scrubbing
        // Also skip if dragging a ParameterCell to prevent interference
        if (draggingMarker == WaveformMarker::NONE && ImGui::IsMouseDragging(0) && !isDraggingParameter) {
            auto player = getMediaPool()->getActivePlayer();
            if (player && player->isPlaying()) {
                // Scrubbing: only update playheadPosition (absolute position)
                // Don't change startPosition - it should remain as set
                if (player->isAudioLoaded()) {
                    player->getAudioPlayer().setPosition(relativeX);
                }
                if (player->isVideoLoaded()) {
                    player->getVideoPlayer().getVideoFile().setPosition(relativeX);
                    player->getVideoPlayer().getVideoFile().update();
                }
                
                player->playheadPosition.set(relativeX);
                // NOTE: Don't call setParameter("position") here - that would change startPosition
                // Scrubbing should only affect playheadPosition
            }
        }
    }
    
    // Draw grey background on trimmed parts (outside the range)
    // The range itself keeps the black waveform background
    // Only draw if markers are on-screen
    ImU32 trimmedColor = GUIConstants::toIM_COL32(GUIConstants::Background::WaveformTrimmed);
    if (regionStart > 0.0f && regionStartX >= 0.0f) {
        // Draw grey background for left trimmed part (before region start)
        float trimStartX = canvasPos.x;
        float trimEndX = std::min(regionStartX, canvasMax.x);
        if (trimEndX > trimStartX) {
            drawList->AddRectFilled(
                ImVec2(trimStartX, canvasPos.y),
                ImVec2(trimEndX, canvasMax.y),
                trimmedColor
            );
        }
    }
    if (regionEnd < 1.0f && regionEndX >= 0.0f) {
        // Draw grey background for right trimmed part (after region end)
        float trimStartX = std::max(regionEndX, canvasPos.x);
        float trimEndX = canvasMax.x;
        if (trimEndX > trimStartX) {
            drawList->AddRectFilled(
                ImVec2(trimStartX, canvasPos.y),
                ImVec2(trimEndX, canvasMax.y),
                trimmedColor
            );
        }
    }
    
    // Marker dimensions
    const float markerLineWidth = 1.5f;
    const float markerHandleWidth = 8.0f;
    const float markerHandleHeight = 6.0f;
    const float markerLineTopOffset = markerHandleHeight;
    
    // Draw region start marker (grey) - only if on-screen
    if (regionStartX >= 0.0f) {
        ImU32 regionStartColor = GUIConstants::toU32(GUIConstants::Waveform::RegionStart);
        drawList->AddLine(
            ImVec2(regionStartX, canvasPos.y + markerLineTopOffset),
            ImVec2(regionStartX, canvasMax.y),
            regionStartColor, markerLineWidth
        );
        // Draw marker handle (small horizontal bar at top)
        drawList->AddRectFilled(
            ImVec2(regionStartX - markerHandleWidth * 0.5f, canvasPos.y),
            ImVec2(regionStartX + markerHandleWidth * 0.5f, canvasPos.y + markerHandleHeight),
            regionStartColor
        );
    }
    
    // Draw region end marker (grey) - only if on-screen
    if (regionEndX >= 0.0f) {
        ImU32 regionEndColor = GUIConstants::toU32(GUIConstants::Waveform::RegionEnd);
        drawList->AddLine(
            ImVec2(regionEndX, canvasPos.y + markerLineTopOffset),
            ImVec2(regionEndX, canvasMax.y),
            regionEndColor, markerLineWidth
        );
        // Draw marker handle (small horizontal bar at top)
        drawList->AddRectFilled(
            ImVec2(regionEndX - markerHandleWidth * 0.5f, canvasPos.y),
            ImVec2(regionEndX + markerHandleWidth * 0.5f, canvasPos.y + markerHandleHeight),
            regionEndColor
        );
    }
    
    // Draw position marker (darker grey) - shows where playback will start - only if on-screen
    if (positionX >= 0.0f) {
        ImU32 positionColor = GUIConstants::toU32(GUIConstants::Waveform::Position);
        drawList->AddLine(
            ImVec2(positionX, canvasPos.y + markerLineTopOffset),
            ImVec2(positionX, canvasMax.y),
            positionColor, markerLineWidth
        );
        // Draw marker handle (small horizontal bar at top, slightly wider)
        const float positionHandleWidth = 10.0f;
        drawList->AddRectFilled(
            ImVec2(positionX - positionHandleWidth * 0.5f, canvasPos.y),
            ImVec2(positionX + positionHandleWidth * 0.5f, canvasPos.y + markerHandleHeight),
            positionColor
        );
    }
    
    // Draw playhead (green) - shows current playback position (can move freely, even outside region) - only if on-screen
    bool showPlayhead = (playheadPos > 0.0f || currentPlayer->isPlaying());
    if (showPlayhead && playheadX >= 0.0f) {
        ImU32 playheadColor = GUIConstants::toU32(GUIConstants::Waveform::Playhead);
        drawList->AddLine(
            ImVec2(playheadX, canvasPos.y),
            ImVec2(playheadX, canvasMax.y),
            playheadColor, 2.0f
        );
    }
    
    // Draw loop range visualization (when in LOOP play style with loopSize > 0)
    PlayStyle currentPlayStyle = getMediaPool()->getPlayStyle();
    if (currentPlayStyle == PlayStyle::LOOP) {
        float loopSizeSeconds = currentPlayer->loopSize.get();
        if (loopSizeSeconds > 0.001f) {
            float duration = currentPlayer->getDuration();
            if (duration > 0.001f) {
                // Convert loopSize (seconds) to normalized position (0-1)
                float loopSizeNormalized = loopSizeSeconds / duration;
                
                // Calculate loop start position (absolute) - same logic as in MediaPool::update()
                float relativeStartPos = currentPlayer->startPosition.get();
                float regionSize = regionEnd - regionStart;
                float loopStartAbsolute = 0.0f;
                
                if (regionSize > 0.001f) {
                    loopStartAbsolute = regionStart + relativeStartPos * regionSize;
                } else {
                    loopStartAbsolute = std::max(0.0f, std::min(1.0f, relativeStartPos));
                }
                
                // Calculate loop end (clamped to region end and media duration)
                float calculatedLoopEnd = loopStartAbsolute + loopSizeNormalized;
                float loopEndAbsolute = std::min(regionEnd, std::min(1.0f, calculatedLoopEnd));
                
                // Map to screen coordinates
                float loopStartX = mapToScreenX(loopStartAbsolute);
                float loopEndX = mapToScreenX(loopEndAbsolute);
                
                // Draw loop range overlay (semi-transparent blue/purple)
                if (loopStartX >= 0.0f || loopEndX >= 0.0f) {
                    // Clamp to visible area
                    float drawStartX = std::max(canvasPos.x, loopStartX >= 0.0f ? loopStartX : canvasPos.x);
                    float drawEndX = std::min(canvasMax.x, loopEndX >= 0.0f ? loopEndX : canvasMax.x);
                    
                    if (drawEndX > drawStartX) {
                        // Semi-transparent overlay for loop range
                        ImU32 loopRangeColor = GUIConstants::toIM_COL32(GUIConstants::Waveform::LoopRange);
                        drawList->AddRectFilled(
                            ImVec2(drawStartX, canvasPos.y),
                            ImVec2(drawEndX, canvasMax.y),
                            loopRangeColor
                        );
                        
                        // Draw border lines for clarity
                        ImU32 loopBorderColor = GUIConstants::toIM_COL32(GUIConstants::Waveform::LoopRangeBorder);
                        if (loopStartX >= 0.0f) {
                            drawList->AddLine(
                                ImVec2(loopStartX, canvasPos.y),
                                ImVec2(loopStartX, canvasMax.y),
                                loopBorderColor, 1.0f
                            );
                        }
                        if (loopEndX >= 0.0f) {
                            drawList->AddLine(
                                ImVec2(loopEndX, canvasPos.y),
                                ImVec2(loopEndX, canvasMax.y),
                                loopBorderColor, 1.0f
                            );
                        }
                    }
                }
            }
        }
    }
}

ParameterCell MediaPoolGUI::createParameterCellForParameter(const ParameterDescriptor& paramDesc) {
    ParameterCell cell;
    cell.parameterName = paramDesc.name;
    cell.isInteger = (paramDesc.type == ParameterType::INT);
    cell.setValueRange(paramDesc.minValue, paramDesc.maxValue, paramDesc.defaultValue);
    cell.calculateStepIncrement();
    
    // Set up getCurrentValue callback - capture mediaPool to get active player dynamically
    // This ensures we always get the current active player, not a stale reference
    cell.getCurrentValue = [this, paramDesc]() -> float {
        MediaPool* pool = getMediaPool();
        if (!pool) return paramDesc.defaultValue;
        
        auto activePlayer = pool->getActivePlayer();
        if (!activePlayer) return paramDesc.defaultValue;
        
        // Special handling for "position" parameter: show startPosition instead of playheadPosition
        // (playheadPosition is already visually displayed as the green playhead in the waveform)
        if (paramDesc.name == "position") {
            return activePlayer->startPosition.get();
        }
        
        // Use MediaPlayer's helper method for cleaner parameter access
        const auto* param = activePlayer->getFloatParameter(paramDesc.name);
        if (param) {
            return param->get();
        }
        return paramDesc.defaultValue;
    };
    
    // Set up onValueApplied callback
    cell.onValueApplied = [this, paramDesc](const std::string&, float value) {
        MediaPool* pool = getMediaPool();
        if (pool) {
            pool->setParameter(paramDesc.name, value, true);
        }
    };
    
    // Set up formatValue callback (use openFrameworks ofToString for modern C++ string formatting)
    // Unified precision: 0.001 (3 decimal places) for all float parameters
    // Special handling for loopSize: logarithmic mapping for better precision at low values (1-100ms granular range)
    if (paramDesc.name == "loopSize") {
        // Logarithmic mapping: slider value (0.0-1.0) maps to loopSize (0.001s to 10s)
        // This provides better precision at low values (1-100ms = 0.001-0.1s)
        const float MIN_LOOP_SIZE = 0.001f;  // 1ms minimum
        const float MAX_LOOP_SIZE = 10.0f;   // 10s maximum
        
        // Set cell value range to slider range (0.0-1.0) for logarithmic mapping
        // Calculate default slider value from default seconds value (1.0s)
        float defaultSeconds = 1.0f;
        float defaultSliderValue = 0.0f;
        if (defaultSeconds > MIN_LOOP_SIZE && defaultSeconds < MAX_LOOP_SIZE) {
            defaultSliderValue = std::log(defaultSeconds / MIN_LOOP_SIZE) / std::log(MAX_LOOP_SIZE / MIN_LOOP_SIZE);
        } else if (defaultSeconds >= MAX_LOOP_SIZE) {
            defaultSliderValue = 1.0f;
        }
        cell.setValueRange(0.0f, 1.0f, defaultSliderValue);
        cell.calculateStepIncrement();
        
        // Map slider value to logarithmic range
        cell.getCurrentValue = [this, paramDesc, MIN_LOOP_SIZE, MAX_LOOP_SIZE]() -> float {
            MediaPool* pool = getMediaPool();
            if (!pool) return paramDesc.defaultValue;
            
            auto activePlayer = pool->getActivePlayer();
            if (!activePlayer) return paramDesc.defaultValue;
            
            // Get actual loopSize value in seconds
            float actualValue = activePlayer->loopSize.get();
            
            // Map from linear seconds to logarithmic slider value (0.0-1.0)
            // Inverse of: value = MIN * pow(MAX/MIN, sliderValue)
            if (actualValue <= MIN_LOOP_SIZE) return 0.0f;
            if (actualValue >= MAX_LOOP_SIZE) return 1.0f;
            float sliderValue = std::log(actualValue / MIN_LOOP_SIZE) / std::log(MAX_LOOP_SIZE / MIN_LOOP_SIZE);
            return sliderValue;
        };
        
        // Map slider value back to actual seconds
        cell.onValueApplied = [this, paramDesc, MIN_LOOP_SIZE, MAX_LOOP_SIZE](const std::string&, float sliderValue) {
            MediaPool* pool = getMediaPool();
            if (!pool) return;
            
            // Clamp slider value to [0.0, 1.0]
            sliderValue = std::max(0.0f, std::min(1.0f, sliderValue));
            
            // Map from logarithmic slider value to linear seconds
            // value = MIN * pow(MAX/MIN, sliderValue)
            float actualValue = MIN_LOOP_SIZE * std::pow(MAX_LOOP_SIZE / MIN_LOOP_SIZE, sliderValue);
            
            // Clamp to actual duration if available
            auto activePlayer = getMediaPool()->getActivePlayer();
            if (activePlayer) {
                float duration = activePlayer->getDuration();
                if (duration > 0.001f) {
                    actualValue = std::min(actualValue, duration);
                }
            }
            
            getMediaPool()->setParameter(paramDesc.name, actualValue, true);
        };
        
        // Format display value: show actual seconds with appropriate precision
        cell.formatValue = [MIN_LOOP_SIZE, MAX_LOOP_SIZE](float sliderValue) -> std::string {
            // Map slider value to actual seconds for display
            sliderValue = std::max(0.0f, std::min(1.0f, sliderValue));
            float actualValue = MIN_LOOP_SIZE * std::pow(MAX_LOOP_SIZE / MIN_LOOP_SIZE, sliderValue);
            
            // Use appropriate precision based on value magnitude:
            // - 5 decimals for values < 0.01s (10ms) - granular synthesis range
            // - 4 decimals for values < 0.1s (100ms)
            // - 3 decimals for values >= 0.1s
            if (actualValue < 0.01f) {
                return ofToString(actualValue, 5) + "s";
            } else if (actualValue < 0.1f) {
                return ofToString(actualValue, 4) + "s";
            } else {
                return ofToString(actualValue, 3) + "s";
            }
        };
    } else {
        // Standard linear mapping for other parameters
        cell.formatValue = [paramDesc](float value) -> std::string {
            if (paramDesc.type == ParameterType::INT) {
                return ofToString((int)std::round(value));
            } else {
                return ofToString(value, 3); // 3 decimal places (0.001 precision) for all float parameters
            }
        };
    }
    
    return cell;
}

bool MediaPoolGUI::handleParameterCellKeyPress(const ParameterDescriptor& paramDesc, int key, bool ctrlPressed, bool shiftPressed) {
    // Create ParameterCell for the parameter
    ParameterCell cell = createParameterCellForParameter(paramDesc);
    
    // Sync state from MediaPoolGUI to ParameterCell
    bool isSelected = (editingParameter == paramDesc.name);
    cell.isSelected = isSelected;
    
    // Track if we were in edit mode before handling the key
    bool wasInEditMode = isEditingParameter && isSelected;
    
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
        bool nowInEditMode = cell.isEditingMode();
        isEditingParameter = nowInEditMode;
        if (isEditingParameter) {
            editBufferCache = cell.getEditBuffer();
            editBufferInitializedCache = cell.isEditBufferInitialized();
        } else {
            editBufferCache.clear();
            editBufferInitializedCache = false;
        }
        
        // If we exited edit mode (especially via Enter key), request refocus to maintain cell focus
        if (wasInEditMode && !nowInEditMode && key == OF_KEY_RETURN) {
            shouldRefocusCurrentCell = true;
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
    MediaPool* pool = getMediaPool();
    if (!pool) return;
    
    ImGui::Separator();
    // Get available parameters from MediaPool
    auto params = getMediaPool()->getParameters();
    auto activePlayer = getMediaPool()->getActivePlayer();
    
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
    // CRITICAL: InvisibleButton requires non-zero size (ImGui assertion)
    // Use minimum size of 1x1 pixels to satisfy the requirement
    ImGui::InvisibleButton("##MediaPoolParamsParent", ImVec2(1, 1));
    
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
    
    int totalColumns = 2 + (int)editableParams.size(); // Media index column + PlayStyle column + parameter columns
    
    // Reset focus tracking at start of frame
    anyCellFocusedThisFrame = false;
    
    // Use versioned table ID to reset column order if needed (change version number to force reset)
    // This allows us to reset saved column positions while keeping reorderability
    static int tableVersion = 2; // Increment this to reset all saved column settings (v2: added STYLE column)
    std::string tableId = "MediaPoolParameters_v" + std::to_string(tableVersion);
    
    if (ImGui::BeginTable(tableId.c_str(), totalColumns, flags)) {
        // Setup index column: Fixed width, reorderable (user can move it, but we set it first)
        ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        
        // Setup PlayStyle column: Fixed width, reorderable
        ImGui::TableSetupColumn("Play style", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        
        // Setup parameter columns (stretch to fill remaining width, equal weight, reorderable)
        for (const auto& paramDesc : editableParams) {
            ImGui::TableSetupColumn(paramDesc.displayName.c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        }
        
        ImGui::TableSetupScrollFreeze(0, 1);
        
        // Header row
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        
        // Media index column header
        ImGui::TableSetColumnIndex(0);
        ImGui::TableHeader("Index");
        
        // PlayStyle column header
        ImGui::TableSetColumnIndex(1);
        ImGui::TableHeader("Play style");
        
        // Parameter column headers
        for (size_t i = 0; i < editableParams.size(); i++) {
            ImGui::TableSetColumnIndex((int)i + 2);
            const auto& paramDesc = editableParams[i];
            
            // Special handling for Position parameter (start position from sequencer): add scan mode selector
            if (paramDesc.name == "position") {
                // Get cell position and width before drawing header (similar to TrackerSequencerGUI)
                ImVec2 cellStartPos = ImGui::GetCursorScreenPos();
                float columnWidth = ImGui::GetColumnWidth();
                float cellMinY = cellStartPos.y;
                
                // Draw column name first (standard header)
                ImGui::TableHeader(paramDesc.displayName.c_str());
                
                // Draw scan mode selector button (right-aligned in header)
                if (pool) {
                    drawPositionScanModeButton(cellStartPos, columnWidth, cellMinY);
                }
            } else {
                // Standard header for other parameters
                ImGui::TableHeader(paramDesc.displayName.c_str());
            }
        }
        
        // Single data row (no steps dimension)
        ImGui::TableNextRow();
        
        // Set row background color (darker and more opaque)
        ImU32 rowBgColor = GUIConstants::toU32(GUIConstants::Background::TableRowFilled);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowBgColor);
        
        // Draw media index button (first column, column index 0) - similar to step number button in TrackerSequencer
        ImGui::TableSetColumnIndex(0);
        drawMediaIndexButton(0, editableParams.size());
        
        // Draw PlayStyle button (second column, column index 1)
        ImGui::TableSetColumnIndex(1);
        drawPlayStyleButton(1, editableParams.size());
        
        // Draw parameter cells (remaining columns, column indices 2+)
        for (size_t i = 0; i < editableParams.size(); i++) {
            ImGui::TableSetColumnIndex((int)i + 2);
            const auto& paramDesc = editableParams[i];
            int columnIndex = (int)i + 2; // Column index (2-based for parameters, 0=Index, 1=Play style)
            
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
            bool shouldFocusFirst = (columnIndex == 2 && shouldFocusFirstCell); // First parameter column is now at index 2 (0=Index, 1=Play style)
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
            // Helper lambda to sync drag state (matches TrackerSequencerGUI pattern exactly)
            auto syncDragState = [&](bool isDragging) {
                if (isDragging) {
                    draggingParameter = paramDesc.name;
                    dragStartY = paramCell.getDragStartY();
                    dragStartX = paramCell.getDragStartX();
                    lastDragValue = paramCell.getLastDragValue();
                    // Maintain focus during drag
                    editingColumnIndex = columnIndex;
                    editingParameter = paramDesc.name;
                    // CRITICAL: Set anyCellFocusedThisFrame to true during drag to prevent focus from being cleared
                    // This ensures smooth dragging even when mouse moves outside the cell
                    anyCellFocusedThisFrame = true;
                } else {
                    draggingParameter.clear();
                }
            };
            
            if (interaction.dragStarted || interaction.dragEnded || paramCell.getIsDragging() || isDraggingThis) {
                if (paramCell.getIsDragging()) {
                    // Drag is active - sync state (this handles both newly started and continuing drags)
                    // CRITICAL: Only check paramCell.getIsDragging(), not interaction.dragStarted
                    // This ensures we sync on every frame while dragging, not just when drag starts
                    syncDragState(true);
                } else if (isDraggingThis && !paramCell.getIsDragging()) {
                    // Drag ended - clear drag state
                    syncDragState(false);
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
        // CRITICAL: Don't clear focus if we're dragging - this prevents focus loss during smooth dragging
        if (editingColumnIndex >= 0 && !anyCellFocusedThisFrame && !isEditingParameter && !shouldRefocusCurrentCell && draggingParameter.empty()) {
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
            auto params = gui.getMediaPool()->getParameters();
            std::vector<ParameterDescriptor> editableParams;
            for (const auto& param : params) {
                if (param.name != "note") {
                    editableParams.push_back(param);
                }
            }
            // editingColumnIndex is 2-based for parameters (0 = Index button, 1 = Play style button)
            if (gui.editingColumnIndex > 1 && (size_t)(gui.editingColumnIndex - 2) < editableParams.size()) {
                gui.editingParameter = editableParams[gui.editingColumnIndex - 2].name;
            }
        }
        return; // Already synced
    }
    
    // GUI draw sync should handle this every frame
    // If not set, handleKeyPress will default gracefully
}

void MediaPoolGUI::drawPlayStyleButton(int columnIndex, size_t numParamColumns) {
    MediaPool* pool = getMediaPool();
    if (!pool) return;
    
    PlayStyle currentStyle = pool->getPlayStyle();
    
    // Get button label based on current style
    const char* styleLabel = "ONCE";
    const char* styleTooltip = "Play Style: ONCE\nClick to cycle: ONCE → LOOP → NEXT";
    switch (currentStyle) {
        case PlayStyle::ONCE:
            styleLabel = "ONCE";
            styleTooltip = "Play Style: ONCE\nClick to cycle: ONCE → LOOP → NEXT";
            break;
        case PlayStyle::LOOP:
            styleLabel = "LOOP";
            styleTooltip = "Play Style: LOOP\nClick to cycle: LOOP → NEXT → ONCE";
            break;
        case PlayStyle::NEXT:
            styleLabel = "NEXT";
            styleTooltip = "Play Style: NEXT\nClick to cycle: NEXT → ONCE → LOOP";
            break;
    }
    
    // Draw button
    if (ImGui::Button(styleLabel, ImVec2(-1, 0))) {
        // Cycle through styles: ONCE → LOOP → NEXT → ONCE
        PlayStyle nextStyle;
        switch (currentStyle) {
            case PlayStyle::ONCE:
                nextStyle = PlayStyle::LOOP;
                break;
            case PlayStyle::LOOP:
                nextStyle = PlayStyle::NEXT;
                break;
            case PlayStyle::NEXT:
                nextStyle = PlayStyle::ONCE;
                break;
        }
        getMediaPool()->setPlayStyle(nextStyle);
    }
    
    // Tooltip on hover
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", styleTooltip);
    }
}

void MediaPoolGUI::drawMediaIndexButton(int columnIndex, size_t numParamColumns) {
    MediaPool* pool = getMediaPool();
    if (!pool) return;
    
    size_t currentIndex = pool->getCurrentIndex();
    size_t numPlayers = pool->getNumPlayers();
    auto activePlayer = pool->getActivePlayer();
    
    // Format button text: show current index (1-based) or "--" if no media
    std::string indexText;
    if (numPlayers > 0) {
        int displayIndex = (int)(currentIndex + 1);
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d", displayIndex);
        indexText = buf;
    } else {
        indexText = "--";
    }
    
    // Determine focus state
    bool shouldRefocus = (editingColumnIndex == columnIndex && shouldRefocusCurrentCell);
    
    if (shouldRefocus) {
        ImGui::SetKeyboardFocusHere(0);
    }
    
    // SIMPLIFIED: Button state is based on mode - supports both manual and sequencer playback
    // Button is green if media is playing (either MANUAL_PREVIEW or SEQUENCER_ACTIVE)
    // Button is grey if IDLE (stopped)
    bool isCurrentMediaPlaying = false;
    if (activePlayer != nullptr && currentIndex < numPlayers) {
        auto currentPlayer = getMediaPool()->getMediaPlayer(currentIndex);
        if (currentPlayer == activePlayer) {
            // Button is green if mode is MANUAL_PREVIEW OR SEQUENCER_ACTIVE
            // This syncs with both manual clicks and external sequencer triggers
            isCurrentMediaPlaying = (getMediaPool()->isManualPreview() || getMediaPool()->isSequencerActive()) 
                        && currentPlayer->isPlaying();
        }
    }
    
    // Apply button styling for active playback
    if (isCurrentMediaPlaying) {
        ImGui::PushStyleColor(ImGuiCol_Button, GUIConstants::Active::StepButton);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GUIConstants::Active::StepButtonHover);
    }
    
    ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
    bool buttonClicked = ImGui::Button(indexText.c_str(), ImVec2(-1, 0));
    ImGui::PopItemFlag();
    
    if (shouldRefocus) {
        ImGui::SetKeyboardFocusHere(-1);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }
    
    if (isCurrentMediaPlaying) {
        ImGui::PopStyleColor(2);
    }
    
    // SIMPLIFIED: Handle button click - simple toggle logic
    bool spacebarPressed = ImGui::IsKeyPressed(ImGuiKey_Space, false);
    
    if (buttonClicked && !spacebarPressed && numPlayers > 0) {
        auto currentPlayer = getMediaPool()->getMediaPlayer(currentIndex);
        if (currentPlayer) {
            // Only toggle manual preview - don't interfere with sequencer playback
            // If sequencer is active, button click does nothing (sequencer controls playback)
            if (getMediaPool()->isManualPreview()) {
                // Currently in MANUAL_PREVIEW mode - stop it
                currentPlayer->stop();
                getMediaPool()->setModeIdle();  // Transition to IDLE immediately
            } else if (getMediaPool()->isIdle()) {
                // Not playing (IDLE) - start manual preview
                // Note: If SEQUENCER_ACTIVE, we don't do anything (sequencer is in control)
                // CRITICAL FIX: Convert relative startPosition to absolute position for playMediaManual
                // playMediaManual expects absolute position (0.0-1.0), but startPosition is relative within region
                float relativeStartPos = currentPlayer->startPosition.get();
                float regionStartVal = currentPlayer->regionStart.get();
                float regionEndVal = currentPlayer->regionEnd.get();
                float regionSize = regionEndVal - regionStartVal;
                
                float absoluteStartPosition;
                if (regionSize > 0.001f) {
                    // Convert relative position to absolute within region
                    absoluteStartPosition = regionStartVal + relativeStartPos * regionSize;
                } else {
                    // If region is invalid, use relative position directly
                    absoluteStartPosition = relativeStartPos;
                }
                
                // Clamp to valid range
                absoluteStartPosition = std::max(0.0f, std::min(1.0f, absoluteStartPosition));
                
                getMediaPool()->playMediaManual(currentIndex, absoluteStartPosition);
            }
            // If isSequencerActive(), do nothing - sequencer controls playback
        }
    }
    
    // ONE-WAY SYNC: ImGui focus → MediaPoolGUI state
    bool actuallyFocused = ImGui::IsItemFocused();
    if (actuallyFocused) {
        bool itemWasClicked = ImGui::IsItemClicked(0);
        bool keyboardNavActive = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
        
        if (itemWasClicked || keyboardNavActive || shouldRefocus) {
            anyCellFocusedThisFrame = true;
            bool columnChanged = (editingColumnIndex != columnIndex);
            
            if (isEditingParameter && columnChanged) {
                return;
            }
            
            editingColumnIndex = columnIndex;
            editingParameter.clear();
            isParentWidgetFocused = false;
            
            if (shouldRefocus) {
                shouldRefocusCurrentCell = false;
            }
            
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
    if (editingColumnIndex > 1 && editingParameter.empty() && mediaPool) {
        auto params = getMediaPool()->getParameters();
        // Filter out "note" parameter (it's not editable in the GUI)
        std::vector<ParameterDescriptor> editableParams;
        for (const auto& param : params) {
            if (param.name != "note") {
                editableParams.push_back(param);
            }
        }
        // editingColumnIndex is 2-based for parameters (0 = IDX button, 1 = STYLE button)
        if (editingColumnIndex > 1 && (size_t)(editingColumnIndex - 2) < editableParams.size()) {
            editingParameter = editableParams[editingColumnIndex - 2].name;
        }
    }
    
    // Handle direct typing (numeric keys, decimal point, operators) - auto-enter edit mode
    // This matches TrackerSequencer behavior: typing directly enters edit mode
    if ((key >= '0' && key <= '9') || key == '.' || key == '-' || key == '+' || key == '*' || key == '/') {
        // Check if we have a valid parameter column focused (not Index or Play style button)
        if (!isEditingParameter && editingColumnIndex > 1) {
            // Find the parameter descriptor
            auto params = getMediaPool()->getParameters();
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
                if (editingColumnIndex > 1 && (size_t)(editingColumnIndex - 2) < editableParams.size()) {
                    paramDesc = &editableParams[editingColumnIndex - 2];
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
        auto params = getMediaPool()->getParameters();
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
                auto params = getMediaPool()->getParameters();
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
            if (editingColumnIndex > 1 && !isEditingParameter) {
                // Enter on selected parameter column: Enter edit mode
                // (Column 0 is Index button, column 1 is Play style button, which don't support editing)
                
                // Ensure editingParameter is set
                if (editingParameter.empty() && mediaPool) {
                    auto params = getMediaPool()->getParameters();
                    std::vector<ParameterDescriptor> editableParams;
                    for (const auto& param : params) {
                        if (param.name != "note") {
                            editableParams.push_back(param);
                        }
                    }
                    if (editingColumnIndex > 1 && (size_t)(editingColumnIndex - 2) < editableParams.size()) {
                        editingParameter = editableParams[editingColumnIndex - 2].name;
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

void MediaPoolGUI::drawPositionScanModeButton(const ImVec2& cellStartPos, float columnWidth, float cellMinY) {
    MediaPool* pool = getMediaPool();
    if (!pool) return;
    
    // Mode cycling button with all 4 modes properly mapped
    static const char* const MODE_LABELS[] = { "N", "S", "M", "G" }; // None, Step, Media, Global
    static const char* const MODE_TOOLTIPS[] = { 
        "None: No scanning - always start from set position (or 0.0)",
        "Step: Each step remembers its scan position separately",
        "Media: Each media remembers its scan position across all steps", 
        "Global: All media share one scan position"
    };
    static constexpr int NUM_MODES = 4;
    
    // Helper functions to map between enum and GUI index
    auto modeToGuiIndex = [](ScanMode mode) -> int {
        switch (mode) {
            case ScanMode::NONE: return 0;
            case ScanMode::PER_STEP: return 1;
            case ScanMode::PER_MEDIA: return 2;
            case ScanMode::GLOBAL: return 3;
            default: return 2; // Default to PER_MEDIA
        }
    };
    
    auto guiIndexToMode = [](int guiIndex) -> ScanMode {
        switch (guiIndex) {
            case 0: return ScanMode::NONE;
            case 1: return ScanMode::PER_STEP;
            case 2: return ScanMode::PER_MEDIA;
            case 3: return ScanMode::GLOBAL;
            default: return ScanMode::PER_MEDIA;
        }
    };
    
    ScanMode currentMode = getMediaPool()->getScanMode();
    int currentModeIndex = modeToGuiIndex(currentMode);
    
    // Validate mode index
    if (currentModeIndex < 0 || currentModeIndex >= NUM_MODES) {
        currentModeIndex = 2; // Default to PER_MEDIA if invalid
    }
    
    ImGui::PushID("PositionScanMode");
    
    // Calculate button size and position (right-aligned in header)
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
    float buttonWidth = ImGui::CalcTextSize(MODE_LABELS[currentModeIndex]).x + 
                        ImGui::GetStyle().FramePadding.x * 2.0f;
    float padding = ImGui::GetStyle().CellPadding.x;
    
    // Position button to the right edge of the cell
    float cellMaxX = cellStartPos.x + columnWidth;
    float buttonStartX = cellMaxX - buttonWidth - padding;
    ImGui::SetCursorScreenPos(ImVec2(buttonStartX, cellMinY));
    
    // Single click cycles to next mode
    if (ImGui::SmallButton(MODE_LABELS[currentModeIndex])) {
        int nextModeIndex = (currentModeIndex + 1) % NUM_MODES;
        getMediaPool()->setScanMode(guiIndexToMode(nextModeIndex));
    }
    
    // Simple tooltip on hover
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", MODE_TOOLTIPS[currentModeIndex]);
    }
    
    ImGui::PopStyleVar();
    
    ImGui::PopID();
}

std::pair<float, float> MediaPoolGUI::getWaveformZoomState(size_t index) const {
    auto it = waveformZoomState.find(index);
    if (it != waveformZoomState.end()) {
        return it->second;  // Return stored {zoom, offset}
    }
    // Default values: no zoom (1.0), no offset (0.0)
    return std::make_pair(1.0f, 0.0f);
}

void MediaPoolGUI::setWaveformZoomState(size_t index, float zoom, float offset) {
    waveformZoomState[index] = std::make_pair(zoom, offset);
}
