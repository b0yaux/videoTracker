#include "MediaPoolGUI.h"
#include "MediaPool.h"
#include "MediaPlayer.h"

MediaPoolGUI::MediaPoolGUI() 
    : mediaPool(nullptr), waveformHeight(100.0f) {
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
    drawMediaList();
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
    // Show indexed media list with actual file names
    if (mediaPool->getNumPlayers() > 0) {
        ImGui::Text("Available Media:");
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
    
    // Preview Mode Controls
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
}

void MediaPoolGUI::drawWaveform() {
    auto currentPlayer = mediaPool->getActivePlayer();
    if (currentPlayer && currentPlayer->isAudioLoaded()) {
        
        // Get audio buffer data
        ofSoundBuffer buffer = currentPlayer->getAudioPlayer().getBuffer();
        int numFrames = buffer.getNumFrames();
        int numChannels = buffer.getNumChannels();
        
        if (numFrames > 0 && numChannels > 0) {
            // Prepare data for ImDrawList - much more efficient than manual drawing
            static std::vector<float> timeData;
            static std::vector<std::vector<float>> channelData;
            
            // Resize vectors if needed
            int maxPoints = 2000; // Reasonable number of points for smooth waveform
            int stepSize = std::max(1, numFrames / maxPoints);
            int actualPoints = std::min(maxPoints, numFrames / stepSize);
            
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
            if (ImGui::IsItemHovered()) {
                // Show cursor as hand when hovering over waveform
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                
                // Check for mouse click or drag
                if (ImGui::IsMouseClicked(0)) {
                    // Click: seek to position and handle playback appropriately
                    ImVec2 mousePos = ImGui::GetMousePos();
                    float relativeX = (mousePos.x - canvasPos.x) / (canvasMax.x - canvasPos.x);
                    
                    if (relativeX >= 0.0f && relativeX <= 1.0f) {
                        auto player = mediaPool->getActivePlayer();
                        if (player) {
                            // Always seek to the clicked position first
                            player->position.set(relativeX);
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
                    // Drag: scrub without restarting playback
                    ImVec2 mousePos = ImGui::GetMousePos();
                    float relativeX = (mousePos.x - canvasPos.x) / (canvasMax.x - canvasPos.x);
                    
                    if (relativeX >= 0.0f && relativeX <= 1.0f) {
                        auto player = mediaPool->getActivePlayer();
                        if (player) {
                            player->position.set(relativeX);
                        }
                    }
                }
            }
            
            // Draw waveform using ImDrawList
            float canvasWidth = canvasMax.x - canvasPos.x;
            float canvasHeight = canvasMax.y - canvasPos.y;
            float centerY = canvasPos.y + canvasHeight * 0.5f;
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
            
            // Draw playhead with green color
            if (showPlayhead) {
                float playheadX = canvasPos.x + playheadPosition * canvasWidth;
                ImU32 playheadColor = IM_COL32(0, 255, 0, 255); // Green
                
                drawList->AddLine(
                    ImVec2(playheadX, canvasPos.y),
                    ImVec2(playheadX, canvasMax.y),
                    playheadColor, 3.0f
                );
            }
        }
        
    } else {
        ImGui::Text("No active player with audio to display waveform.");
    }
}

