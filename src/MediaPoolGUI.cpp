#include "MediaPoolGUI.h"
#include "MediaPool.h"
#include "MediaPlayer.h"

MediaPoolGUI::MediaPoolGUI() 
    : mediaPool(nullptr), showAdvancedOptions(false), showFileDetails(true), 
      waveformHeight(100.0f) {
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
                    // Stop all other players first
                    for (size_t j = 0; j < mediaPool->getNumPlayers(); j++) {
                        auto otherPlayer = mediaPool->getMediaPlayer(j);
                        if (otherPlayer && otherPlayer != player) {
                            otherPlayer->stop();
                        }
                    }
                    
                    // Set as active player
                    mediaPool->setActivePlayer(i);
                    
                    // Handle play/pause logic
                    if (player) {
                        if (player->isPlaying()) {
                            // If already playing, pause it
                            player->pause();
                        } else {
                            // If paused or stopped, play it
                            // Enable both audio and video for playback
                            player->audioEnabled.set(true);
                            player->videoEnabled.set(true);
                            player->play();
                        }
                    }
                }
                
                // Add hover tooltip with metadata
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Index: %zu", i);
                    ImGui::Text("Player: %s", playerNames[i].c_str());
                    if (i < playerFileNames.size() && !playerFileNames[i].empty()) {
                        ImGui::Text("File: %s", playerFileNames[i].c_str());
                    }
                    ImGui::Text("Audio: %s", player->isAudioLoaded() ? "Loaded" : "Not loaded");
                    ImGui::Text("Video: %s", player->isVideoLoaded() ? "Loaded" : "Not loaded");
                    ImGui::Text("Status: %s", player->isPlaying() ? "Playing" : "Stopped");
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
}
void MediaPoolGUI::drawWaveform() {
    auto currentPlayer = mediaPool->getActivePlayer();
    if (currentPlayer && currentPlayer->isAudioLoaded()) {
        ImGui::Text("Current Media Waveform:");
        
        // Get audio buffer data
        ofSoundBuffer buffer = currentPlayer->getAudioPlayer().getBuffer();
        int numFrames = buffer.getNumFrames();
        int numChannels = buffer.getNumChannels();
        
        if (numFrames > 0 && numChannels > 0) {
            // Prepare data for ImPlot - much more efficient than manual drawing
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
            
            // Use ImPlot for high-performance waveform rendering
            if (ImPlot::BeginPlot("Waveform", ImVec2(-1, waveformHeight), 
                ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | 
                ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
                ImPlot::SetupAxes("", "", ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
                ImPlot::SetupAxisLimits(ImAxis_X1, 0, 1, ImPlotCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, -1, 1, ImPlotCond_Always);
                
                // Plot each channel with white color
                for (int ch = 0; ch < numChannels; ch++) {
                    ImPlot::SetNextLineStyle(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f); // White lines
                    ImPlot::PlotLine(("Channel " + std::to_string(ch)).c_str(), 
                                   timeData.data(), channelData[ch].data(), actualPoints);
                }
                
                // Calculate proper playhead position
                float playheadPosition = 0.0f;
                bool showPlayhead = false;
                
                if (currentPlayer->isPlaying()) {
                    // Get actual media playback position (0.0-1.0) - this updates in real-time
                    float mediaPosition = currentPlayer->getAudioPlayer().getPosition();
                    playheadPosition = mediaPosition;
                    showPlayhead = true;
                } else {
                    // Use tracker sequencer step position when not playing
                    float stepPosition = currentPlayer->position.get();
                    if (stepPosition > 0.0f) {
                        playheadPosition = stepPosition;
                        showPlayhead = true;
                    }
                }
                
                // Handle click-to-seek and drag functionality
                if (ImPlot::IsPlotHovered()) {
                    // Show cursor as hand when hovering over waveform
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    
                    // Check for mouse click or drag
                    if (ImGui::IsMouseClicked(0) || ImGui::IsMouseDragging(0)) {
                        ImPlotPoint mousePos = ImPlot::GetPlotMousePos();
                        if (mousePos.x >= 0.0 && mousePos.x <= 1.0) {
                            // Seek to the clicked/dragged position
                            currentPlayer->getAudioPlayer().setPosition(mousePos.x);
                            currentPlayer->position.set(mousePos.x);
                            
                            // If paused and clicked, start playing
                            if (!currentPlayer->isPlaying()) {
                                currentPlayer->play();
                            }
                        }
                    }
                }
                
                // Draw playhead with green color
                if (showPlayhead) {
                    // Create vertical line data for playhead - use local vectors to ensure updates
                    std::vector<float> playheadX = {playheadPosition, playheadPosition};
                    std::vector<float> playheadY = {-1.0f, 1.0f};
                    
                    ImPlot::SetNextLineStyle(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), 3.0f); // Green playhead, thicker
                    ImPlot::PlotLine("Playhead", playheadX.data(), playheadY.data(), 2);
                }
                
                ImPlot::EndPlot();
            }
        }
        
    } else {
        ImGui::Text("No active player with audio to display waveform.");
    }
}

void MediaPoolGUI::setShowAdvancedOptions(bool show) {
    showAdvancedOptions = show;
}

void MediaPoolGUI::setShowFileDetails(bool show) {
    showFileDetails = show;
}
