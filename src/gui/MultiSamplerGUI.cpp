#include "MultiSamplerGUI.h"
#include "NumCell.h"  // For dynamic_cast
#include "ParameterCell.h"  // For ParameterCell
#include "modules/MultiSampler.h"  // Includes PlayStyle enum
#include "modules/MediaPlayer.h"
#include "modules/Module.h"
#include "core/ModuleRegistry.h"
#include "core/Engine.h"  // For commandsBeingProcessed()
#include "gui/GUIConstants.h"
#include "gui/MediaPreview.h"
#include "gui/GUIManager.h"
#include "ofMain.h"
#include "ofLog.h"
#include <limits>
#include <iomanip>
#include <cmath>
#include <fstream>
#include <sstream>
#include <chrono>
#include <functional>
#include <algorithm>

MultiSamplerGUI::MultiSamplerGUI() 
    : multiSampler_(nullptr), waveformHeight(100.0f), parentWidgetId(0), 
      isParentWidgetFocused(false), requestFocusMoveToParentWidget(false),
      shouldFocusFirstCell(false) {
}

void MultiSamplerGUI::setMultiSampler(MultiSampler& sampler) {
    // Legacy method: set direct pointer (for backward compatibility)
    multiSampler_ = &sampler;
}

MultiSampler* MultiSamplerGUI::getMultiSampler() const {
    // If instance-aware (has registry and instanceName), use that
    ModuleRegistry* reg = ModuleGUI::getRegistry();
    std::string instanceName = ModuleGUI::getInstanceName();
    if (reg && !instanceName.empty()) {
        auto module = reg->getModule(instanceName);
        if (!module) return nullptr;
        return dynamic_cast<MultiSampler*>(module.get());
    }
    
    // Fallback to legacy direct pointer (for backward compatibility)
    return multiSampler_;
}

std::string MultiSamplerGUI::truncateTextToWidth(const std::string& text, float maxWidth, bool showEnd, const std::string& ellipsis) {
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

void MultiSamplerGUI::draw() {
    // Call base class draw (handles visibility, title bar, enabled state)
    ModuleGUI::draw();
}

// Helper function to draw waveform preview in tooltip
// Now uses shared MediaPreview
void MultiSamplerGUI::drawWaveformPreview(MediaPlayer* player, float width, float height) {
    MediaPreview::drawWaveformPreview(player, width, height);
}

void MultiSamplerGUI::drawContent() {
    // Skip drawing when window is collapsed to avoid accessing invalid window properties
    // This is a safety check in case drawContent() is called despite the ViewManager check
    if (ImGui::IsWindowCollapsed()) {
        return;
    }
    
    // Get current MultiSampler instance (handles null case)
    MultiSampler* sampler = getMultiSampler();
    if (!sampler) {
        std::string instanceName = ModuleGUI::getInstanceName();
        ImGui::Text("Instance '%s' not found", instanceName.empty() ? "unknown" : instanceName.c_str());
        // Still set up drag drop target even if sampler is null
        setupDragDropTarget();
        return;
    }
    
    // Sync selectedSampleIndex_ to currently playing sample and sync GUI state from voice
    // This ensures the GUI shows the waveform and parameters for the last played sample
    // GUI layer owns this sync logic (separation of concerns - backend doesn't know about GUI)
    // 
    // NOTE: Direct state reads (isPlaying, getSampleCount, isSamplePlaying) are used here for display logic.
    // These could be replaced with EngineState snapshots in the future, but would require parsing
    // JSON from typeSpecificData. Parameter reads already use commands (correct), so these direct
    // reads are acceptable for now as they're module-specific display state, not parameter state.
    if (sampler->isPlaying()) {
        size_t sampleCount = sampler->getSampleCount();
        // Find the first playing sample (in monophonic mode, there's only one)
        // In polyphonic mode, show the first one found (could be enhanced to show most recent)
        for (size_t i = 0; i < sampleCount; ++i) {
            if (sampler->isSamplePlaying(static_cast<int>(i))) {
                selectedSampleIndex_ = i;
                
                // Sync GUI state from voice (GUI's responsibility, not backend's)
                Voice* voice = sampler->getVoiceForSample(static_cast<int>(i));
                if (voice && (voice->state == Voice::PLAYING || voice->state == Voice::RELEASING)) {
                    SampleRef& sample = sampler->getSampleMutable(i);
                    // Sync GUI state from MediaPlayer (single source of truth for position)
                    sample.currentPlayheadPosition = voice->player.playheadPosition.get();
                    sample.currentSpeed = voice->player.speed.get();
                    sample.currentVolume = voice->player.volume.get();
                    sample.currentStartPosition = voice->player.startPosition.get();
                    sample.currentRegionStart = voice->player.regionStart.get();
                    sample.currentRegionEnd = voice->player.regionEnd.get();
                    sample.currentGrainSize = voice->player.loopSize.get();
                }
                break;  // In monophonic mode, there's only one playing sample
            }
        }
    }
    // If not playing, keep the current selectedSampleIndex_ (user's manual selection)
    
    // CRITICAL FIX (Phase 7.9.7.1): Cache PlayStyle at start of draw to avoid multiple lock acquisitions
    // Check if commands are processing before calling getPlayStyle() to prevent deadlock
    PlayStyle currentPlayStyle = PlayStyle::ONCE;  // Default fallback
    if (sampler) {
        // Check if commands are processing before calling getPlayStyle() to prevent deadlock
        bool commandsProcessing = false;
        if (engine_) {
            commandsProcessing = engine_->commandsBeingProcessed();
        }
        if (commandsProcessing) {
            // Commands processing - use cached value if available, otherwise use default
            if (hasCachedPlayStyle_) {
                currentPlayStyle = cachedPlayStyle_;
            } else {
                // No cache - use default to prevent deadlock
                ofLogVerbose("MultiSamplerGUI") << "getPlayStyle() - using default (commands processing, no cache)";
                currentPlayStyle = PlayStyle::ONCE;
            }
        } else {
            // Safe to call - update cache
            currentPlayStyle = sampler->getPlayStyle();
            cachedPlayStyle_ = currentPlayStyle;
            hasCachedPlayStyle_ = true;
        }
    }
    
    // Global Controls (Simple Button Bar - NOT in child window)
    drawGlobalControls(currentPlayStyle);
    ImGui::Spacing(); // Add spacing after global controls
    
    // ADSR Parameters (ONCE/LOOP modes) or Granular Controls (GRAIN mode)
    drawADSRParameters(currentPlayStyle);
    drawGranularControls(currentPlayStyle);
    ImGui::Spacing(); // Add spacing after ADSR/granular controls
    
    // Child 1: Parameter table (auto-size to fit content, no extra space)
    // Use a calculated height based on table structure: header + row + minimal padding
    // Account for: header height, row height, cell padding (2px top + 2px bottom)
    float tableHeaderHeight = ImGui::GetFrameHeight();
    float tableRowHeight = ImGui::GetFrameHeight();
    float cellVerticalPadding = 4.0f; // 2px top + 2px bottom (from CellGrid cellPadding ImVec2(2, 2))
    // Use a tighter calculation - borders are included in frame height
    float parameterTableHeight = tableHeaderHeight + tableRowHeight + cellVerticalPadding;
    
    ImGui::BeginChild("MediaPoolParameters", ImVec2(0, parameterTableHeight), false, ImGuiWindowFlags_NoScrollbar);
    float paramsStartTime = ofGetElapsedTimef();
    drawParameters();
    float paramsTime = (ofGetElapsedTimef() - paramsStartTime) * 1000.0f;
    if (paramsTime > 1.0f) {
        std::string instanceName = ModuleGUI::getInstanceName();
        ofLogNotice("MultiSamplerGUI") << "[PERF] '" << instanceName << "' drawParameters: " 
                                    << std::fixed << std::setprecision(2) << paramsTime << "ms";
    }
    ImGui::EndChild();
    
    // Child 2: Waveform (fixed height) - ADSR curve is overlaid on top
    ImGui::BeginChild("MediaPoolWaveform", ImVec2(0, waveformHeight), false, ImGuiWindowFlags_NoScrollbar);
    float waveformStartTime = ofGetElapsedTimef();
    drawWaveform();
    float waveformTime = (ofGetElapsedTimef() - waveformStartTime) * 1000.0f;
    if (waveformTime > 1.0f) {
        std::string instanceName = ModuleGUI::getInstanceName();
        ofLogNotice("MultiSamplerGUI") << "[PERF] '" << instanceName << "' drawWaveform: " 
                                    << std::fixed << std::setprecision(2) << waveformTime << "ms";
    }
    ImGui::EndChild();
    
    // Child 3: Media list (takes all remaining space)
    ImGui::BeginChild("MediaList", ImVec2(0, 0), true);
    float listStartTime = ofGetElapsedTimef();
    drawMediaList();
    float listTime = (ofGetElapsedTimef() - listStartTime) * 1000.0f;
    if (listTime > 1.0f) {
        std::string instanceName = ModuleGUI::getInstanceName();
        ofLogNotice("MultiSamplerGUI") << "[PERF] '" << instanceName << "' drawMediaList: " 
                                    << std::fixed << std::setprecision(2) << listTime << "ms";
    }
    ImGui::EndChild();
    
    // Set up drag & drop target on the main window (covers entire panel)
    // Must be called after all content is drawn, like AssetLibraryGUI does
    // This ensures the yellow highlight appears and drops work properly
    setupDragDropTarget();
}




/// MARK: - GLOBAL CONTROLS
/// @brief Draw simple button bar for global controls (PLAY, PLAY STYLE, POLYPHONY)
void MultiSamplerGUI::drawGlobalControls(PlayStyle currentPlayStyle) {
    MultiSampler* sampler = getMultiSampler();
    if (!sampler) return;
    
    // Simple horizontal button layout - divide available width equally
    // We have 4 buttons: PLAY, PLAY STYLE, POLYPHONY, and Waveform Overlay Mode
    float availableWidth = ImGui::GetContentRegionAvail().x;
    float buttonWidth = (availableWidth - 3 * ImGui::GetStyle().ItemSpacing.x) / 4.0f;
    
    // 1. PLAY Button (mediaIndex)
    size_t currentIndex = selectedSampleIndex_;
    size_t numPlayers = sampler->getSampleCount();
    bool isActive = sampler->isSamplePlaying(static_cast<int>(currentIndex));
    
    char indexBuf[8];
    if (numPlayers > 0) {
        snprintf(indexBuf, sizeof(indexBuf), "%02d", (int)(currentIndex + 1));
    } else {
        snprintf(indexBuf, sizeof(indexBuf), "--");
    }
    
    // Apply active state styling
    if (isActive) {
        ImGui::PushStyleColor(ImGuiCol_Button, GUIConstants::Active::StepButton);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GUIConstants::Active::StepButtonHover);
    }
    
    ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
    if (ImGui::Button(indexBuf, ImVec2(buttonWidth, 0))) {
        if (numPlayers == 0) return;
        if (isActive) {
            // Stop all voices playing this sample
            auto voices = sampler->getVoicesForSample(static_cast<int>(currentIndex));
            for (auto* voice : voices) {
                voice->release();
                voice->state = Voice::RELEASING;
            }
            // If no other samples are playing, transition to IDLE
            if (!sampler->isPlaying()) {
                sampler->setModeIdle();
            }
        } else {
            // Start manual playback - plays normally (no gate duration)
            sampler->playMediaManual(currentIndex);
        }
    }
    ImGui::PopItemFlag();
    
    if (isActive) {
        ImGui::PopStyleColor(2);
    }
    
    ImGui::SameLine();
    
    // 2. PLAY STYLE Button (enum - cycles ONCE/LOOP/GRAIN/NEXT)
    // Use cached PlayStyle passed as parameter (avoids lock acquisition during command processing)
    const char* styleLabel;
    switch (currentPlayStyle) {
        case PlayStyle::ONCE: styleLabel = "ONCE"; break;
        case PlayStyle::LOOP: styleLabel = "LOOP"; break;
        case PlayStyle::GRAIN: styleLabel = "GRAIN"; break;
        case PlayStyle::NEXT: styleLabel = "NEXT"; break;
        default: styleLabel = "ONCE"; break;
    }
    
    ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
    if (ImGui::Button(styleLabel, ImVec2(buttonWidth, 0))) {
        // Cycle play style: ONCE → LOOP → GRAIN → NEXT → ONCE
        PlayStyle nextStyle;
        switch (currentPlayStyle) {
            case PlayStyle::ONCE: nextStyle = PlayStyle::LOOP; break;
            case PlayStyle::LOOP: nextStyle = PlayStyle::GRAIN; break;
            case PlayStyle::GRAIN: nextStyle = PlayStyle::NEXT; break;
            case PlayStyle::NEXT: nextStyle = PlayStyle::ONCE; break;
        }
        sampler->setPlayStyle(nextStyle);
    }
    ImGui::PopItemFlag();
    
    if (ImGui::IsItemHovered()) {
        const char* tooltip;
        switch (currentPlayStyle) {
            case PlayStyle::ONCE:
                tooltip = "Play Style: ONCE\nClick to cycle: ONCE → LOOP → GRAIN → NEXT";
                break;
            case PlayStyle::LOOP:
                tooltip = "Play Style: LOOP\nClick to cycle: LOOP → GRAIN → NEXT → ONCE";
                break;
            case PlayStyle::GRAIN:
                tooltip = "Play Style: GRAIN\nClick to cycle: GRAIN → NEXT → ONCE → LOOP";
                break;
            case PlayStyle::NEXT:
                tooltip = "Play Style: NEXT\nClick to cycle: NEXT → ONCE → LOOP → GRAIN";
                break;
            default:
                tooltip = "Play Style: ONCE\nClick to cycle: ONCE → LOOP → GRAIN → NEXT";
                break;
        }
        ImGui::SetTooltip("%s", tooltip);
    }
    
    ImGui::SameLine();
    
    // 3. POLYPHONY Button (enum - toggles MONO/POLY)
    PolyphonyMode currentMode = sampler->getPolyphonyMode();
    const char* modeLabel = (currentMode == PolyphonyMode::POLYPHONIC) ? "POLY" : "MONO";
    const char* tooltipText = (currentMode == PolyphonyMode::POLYPHONIC) 
        ? "POLYPHONIC\nClick to switch to MONOPHONIC"
        : "MONOPHONIC\nClick to switch to POLYPHONIC";
    
    ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
    if (ImGui::Button(modeLabel, ImVec2(buttonWidth, 0))) {
        // Toggle polyphony mode
        float newValue = (currentMode == PolyphonyMode::MONOPHONIC) ? 1.0f : 0.0f;
        setParameterViaCommand("polyphonyMode", newValue);
    }
    ImGui::PopItemFlag();
    
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltipText);
    }
    
    ImGui::SameLine();
    
    // 4. Waveform Overlay Mode Selector (cycling button)
    const char* overlayModeLabel;
    switch (waveformOverlayMode_) {
        case WaveformOverlayMode::REGION: overlayModeLabel = "Region"; break;
        case WaveformOverlayMode::AUTOMATION: overlayModeLabel = "Automation"; break;
        default: overlayModeLabel = "Region"; break;
    }
    
    ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
    if (ImGui::Button(overlayModeLabel, ImVec2(buttonWidth, 0))) {
        // Cycle mode: REGION → AUTOMATION → REGION
        switch (waveformOverlayMode_) {
            case WaveformOverlayMode::REGION:
                waveformOverlayMode_ = WaveformOverlayMode::AUTOMATION;
                break;
            case WaveformOverlayMode::AUTOMATION:
                waveformOverlayMode_ = WaveformOverlayMode::REGION;
                break;
        }
        // Clear dragging states when switching modes
        draggingMarker = WaveformMarker::NONE;
        adsrEditorState_.isDragging = false;
        adsrEditorState_.draggedBreakpoint = -1;
    }
    ImGui::PopItemFlag();
    
    if (ImGui::IsItemHovered()) {
        const char* tooltip;
        switch (waveformOverlayMode_) {
            case WaveformOverlayMode::REGION:
                tooltip = "Waveform Mode: Region\nClick to cycle: Region → Automation";
                break;
            case WaveformOverlayMode::AUTOMATION:
                tooltip = "Waveform Mode: Automation\nClick to cycle: Automation → Region";
                break;
            default:
                tooltip = "Waveform Mode: Region\nClick to cycle: Region → Automation";
                break;
        }
        ImGui::SetTooltip("%s", tooltip);
    }
}

/// MARK: - PARAMETERS
/// @brief create a BaseCell for a given ParameterDescriptor
/// @param paramDesc 
/// @return std::unique_ptr<BaseCell>
std::unique_ptr<BaseCell> MultiSamplerGUI::createCellWidgetForParameter(const ParameterDescriptor& paramDesc) {
    MultiSampler* sampler = getMultiSampler();
    if (!sampler) {
        return nullptr;  // Return nullptr if no sampler
    }
    
    // Use ModuleGUI helper to create BaseCell with routing awareness
    // This centralizes the common pattern of getting module + router
    // Set up custom getter - capture mediaPool to get active player dynamically
    // This ensures we always get the current active player, not a stale reference
    auto customGetter = [this, paramDesc]() -> float {
        MultiSampler* sampler = getMultiSampler();
        if (!sampler) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        
        const SampleRef* displaySample = (sampler && selectedSampleIndex_ < sampler->getSampleCount()) 
            ? &sampler->getSample(selectedSampleIndex_) : nullptr;
        if (!displaySample) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        
        // Read from parameter state cache (synced from voice during playback, editable when idle)
        if (paramDesc.name == "position") {
            return displaySample->currentStartPosition;
        } else if (paramDesc.name == "speed") {
            return displaySample->currentSpeed;
        } else if (paramDesc.name == "volume") {
            return displaySample->currentVolume;
        } else if (paramDesc.name == "regionStart") {
            return displaySample->currentRegionStart;
        } else if (paramDesc.name == "regionEnd") {
            return displaySample->currentRegionEnd;
        } else if (paramDesc.name == "grainSize" || paramDesc.name == "loopSize") {
            return displaySample->currentGrainSize;
        }
        
        return std::numeric_limits<float>::quiet_NaN();
    };
    
    // Set up custom setter
    auto customSetter = [this, paramDesc](float value) {
        setParameterViaCommand(paramDesc.name, value);
    };
    
    // Set up custom remover: reset to default value (double-click to reset)
    auto customRemover = [this, paramDesc]() {
        setParameterViaCommand(paramDesc.name, paramDesc.defaultValue);
    };
    
    // Special handling for grainSize: logarithmic mapping for better precision at low values (1-100ms granular range)
    if (paramDesc.name == "grainSize" || paramDesc.name == "loopSize") {
        // Logarithmic mapping: slider value (0.0-1.0) maps to loopSize (0.001s to 10s)
        // This provides better precision at low values (1-100ms = 0.001-0.1s)
        const float MIN_LOOP_SIZE = 0.001f;  // 1ms minimum
        const float MAX_LOOP_SIZE = 10.0f;   // 10s maximum
        
        // Calculate default slider value from actual grainSize value (not hardcoded 1.0s)
        // This ensures the slider shows the correct value even when grainSize is 0
        MultiSampler* sampler = getMultiSampler();
        float defaultSeconds = 0.0f;
        if (sampler) {
            const SampleRef* displaySample = (sampler && selectedSampleIndex_ < sampler->getSampleCount()) 
            ? &sampler->getSample(selectedSampleIndex_) : nullptr;
            if (displaySample) {
                defaultSeconds = displaySample->currentGrainSize;
            }
        }
        // If still 0, use a reasonable default (0.1s = 100ms) for initial display
        if (defaultSeconds <= 0.0f) {
            defaultSeconds = 0.1f;  // 100ms default - good for granular synthesis
        }
        
        float defaultSliderValue = 0.0f;
        if (defaultSeconds > MIN_LOOP_SIZE && defaultSeconds < MAX_LOOP_SIZE) {
            defaultSliderValue = std::log(defaultSeconds / MIN_LOOP_SIZE) / std::log(MAX_LOOP_SIZE / MIN_LOOP_SIZE);
        } else if (defaultSeconds >= MAX_LOOP_SIZE) {
            defaultSliderValue = 1.0f;
        } else if (defaultSeconds <= MIN_LOOP_SIZE) {
            // For values <= MIN_LOOP_SIZE, use a small non-zero slider value to allow editing
            // This prevents the slider from being stuck at 0.0
            defaultSliderValue = 0.01f;  // 1% of slider range - allows dragging up
        }
        
        // Create modified parameter descriptor with slider range (0.0-1.0)
        ParameterDescriptor loopSizeParam(paramDesc.name, paramDesc.type, 0.0f, 1.0f, defaultSliderValue, paramDesc.displayName);
        
        // Override getter: Map from actual seconds to logarithmic slider value (0.0-1.0)
        auto loopSizeGetter = [this, MIN_LOOP_SIZE, MAX_LOOP_SIZE]() -> float {
            MultiSampler* sampler = getMultiSampler();
            if (!sampler) return 0.0f;
            
            const SampleRef* displaySample = (sampler && selectedSampleIndex_ < sampler->getSampleCount()) 
            ? &sampler->getSample(selectedSampleIndex_) : nullptr;
            if (!displaySample) return 0.0f;
            
            // Get actual grainSize value in seconds from GUI state
            float actualValue = displaySample->currentGrainSize;
            
            // Map from linear seconds to logarithmic slider value (0.0-1.0)
            // Inverse of: value = MIN * pow(MAX/MIN, sliderValue)
            // CRITICAL FIX: When actualValue is 0, return a small non-zero slider value (0.01)
            // This allows the user to drag the slider up from 0. If we return 0.0, the slider
            // might be stuck at minimum and uneditable. The setter will handle 0.01 correctly.
            float sliderValue = 0.0f;
            if (actualValue <= 0.0f) {
                // Return small non-zero value to allow editing from 0
                sliderValue = 0.01f;  // 1% of slider range - allows dragging up
            } else if (actualValue <= MIN_LOOP_SIZE) {
                // For values between 0 and MIN_LOOP_SIZE, map proportionally
                // This provides smooth transition from 0 to MIN_LOOP_SIZE
                sliderValue = (actualValue / MIN_LOOP_SIZE) * 0.01f + 0.01f;  // Map to 0.01-0.02 range
            } else if (actualValue >= MAX_LOOP_SIZE) {
                sliderValue = 1.0f;
            } else {
                sliderValue = std::log(actualValue / MIN_LOOP_SIZE) / std::log(MAX_LOOP_SIZE / MIN_LOOP_SIZE);
                // Ensure slider value is at least 0.01 to allow editing
                sliderValue = std::max(0.01f, sliderValue);
            }
            
            return sliderValue;
        };
        
        // Override setter: Map from slider value to actual seconds
        auto loopSizeSetter = [this, paramDesc, MIN_LOOP_SIZE, MAX_LOOP_SIZE](float sliderValue) {
            MultiSampler* sampler = getMultiSampler();
            if (!sampler) {
                ofLogWarning("MultiSamplerGUI") << "[CRASH PREVENTION] MultiSampler is null in setValue callback for parameter: " << paramDesc.name;
                return;
            }
            
            // Clamp slider value to [0.0, 1.0]
            sliderValue = std::max(0.0f, std::min(1.0f, sliderValue));
            
            // CRITICAL FIX: Handle slider value 0.0-0.01 range specially
            // When slider is at 0.0-0.01, user wants to set grainSize to 0 (use full region)
            // For values > 0.01, use logarithmic mapping
            float actualValue = 0.0f;
            if (sliderValue <= 0.01f) {
                // Slider at minimum (0.0-0.01) means grainSize = 0 (use full region)
                actualValue = 0.0f;
            } else {
                // Map from logarithmic slider value to linear seconds
                // value = MIN * pow(MAX/MIN, sliderValue)
                // Adjust sliderValue to account for the 0.01 offset
                float adjustedSliderValue = (sliderValue - 0.01f) / 0.99f;  // Map 0.01-1.0 to 0.0-1.0
                actualValue = MIN_LOOP_SIZE * std::pow(MAX_LOOP_SIZE / MIN_LOOP_SIZE, adjustedSliderValue);
            }
            
            // Clamp to actual duration if available
            if (selectedSampleIndex_ < sampler->getSampleCount()) {
                const SampleRef& sample = sampler->getSample(selectedSampleIndex_);
                if (sample.duration > 0.001f) {
                    actualValue = std::min(actualValue, sample.duration);
                }
            }
            
            setParameterViaCommand(paramDesc.name, actualValue);
        };
        
        // Override formatter: Show actual seconds with appropriate precision
        // NOTE: No "s" suffix - keeps parsing simple and standard (no custom parseValue needed)
        auto loopSizeFormatter = [MIN_LOOP_SIZE, MAX_LOOP_SIZE](float sliderValue) -> std::string {
            // Map slider value to actual seconds for display
            sliderValue = std::max(0.0f, std::min(1.0f, sliderValue));
            float actualValue = MIN_LOOP_SIZE * std::pow(MAX_LOOP_SIZE / MIN_LOOP_SIZE, sliderValue);
            
            // Use appropriate precision based on value magnitude:
            // - 5 decimals for values < 0.01s (10ms) - granular synthesis range
            // - 4 decimals for values < 0.1s (100ms)
            // - 3 decimals for values >= 0.1s
            if (actualValue < 0.01f) {
                return ofToString(actualValue, 5);
            } else if (actualValue < 0.1f) {
                return ofToString(actualValue, 4);
            } else {
                return ofToString(actualValue, 3);
            }
        };
        
        // Create and return CellWidget with custom callbacks
        return createCellWidget(loopSizeParam, loopSizeGetter, loopSizeSetter, nullptr, loopSizeFormatter);
    }
    
    // Special handling for ADSR parameters with unbounded max values
    // These parameters can accept very large values, but for drag sensitivity we need a reasonable max
    // to prevent astronomical drag increments
    if (paramDesc.name == "attackMs" || paramDesc.name == "decayMs" || paramDesc.name == "releaseMs") {
        // Use a reasonable max value (10 seconds = 10000ms) for drag sensitivity calculation
        // The actual parameter can still accept higher values through direct input
        const float REASONABLE_MAX_MS = 10000.0f;  // 10 seconds - reasonable for most use cases
        
        // Create modified parameter descriptor with reasonable max for drag sensitivity
        ParameterDescriptor adsrParam(paramDesc.name, paramDesc.type, 
                                     paramDesc.minValue, 
                                     REASONABLE_MAX_MS,  // Use reasonable max for drag sensitivity
                                     paramDesc.defaultValue, 
                                     paramDesc.displayName);
        
        // Override setter to allow values beyond REASONABLE_MAX_MS (for direct input)
        auto adsrSetter = [this, paramDesc](float value) {
            // Allow values beyond REASONABLE_MAX_MS (no clamping in setter)
            // The parameter descriptor's maxValue is only used for drag sensitivity
            setParameterViaCommand(paramDesc.name, value);
        };
        
        // Use standard getter (no special mapping needed)
        return createCellWidget(adsrParam, customGetter, adsrSetter, customRemover);
    }
    
    // For all other parameters: use standard createCellWidget with custom callbacks
    return createCellWidget(paramDesc, customGetter, customSetter, customRemover);
}



/// MARK: - P Descritpor
// ParameterDescriptor : Returns a vector of ParameterDescriptor objects representing editable parameters.
// Parameters named "note" are excluded from the returned vector.
std::vector<ParameterDescriptor> MultiSamplerGUI::getEditableParameters() const {
    MultiSampler* sampler = getMultiSampler();
    if (!sampler) {
        ofLogWarning("MultiSamplerGUI") << "[CRASH PREVENTION] MultiSampler is null in getEditableParameters()";
        return std::vector<ParameterDescriptor>(); // Return empty vector
    }
    auto params = sampler->getParameters();
    std::vector<ParameterDescriptor> editableParams;
    for (const auto& param : params) {
        if (param.name != "note") {
            editableParams.push_back(param);
        }
    }
    return editableParams;
}


void MultiSamplerGUI::drawParameters() {
    MultiSampler* sampler = getMultiSampler();
    if (!sampler) return;
    
    // Start at the very top of the child window (no padding)
    ImGui::SetCursorPosY(0);
    
    // Get available parameters from MultiSampler
    auto allEditableParams = getEditableParameters();
    
    // Filter to only numeric parameters (FLOAT/INT) - enum/bool are in global controls, ADSR are in envelope controls
    std::vector<ParameterDescriptor> editableParams;
    for (const auto& param : allEditableParams) {
        // Skip enum/bool parameters (they're in global controls now)
        if (param.name == "playStyle" || param.name == "polyphonyMode" || 
            param.name == "index" || param.name == "mediaIndex") {
            continue;
        }
        // Skip ADSR envelope parameters (they're in ADSR controls now)
        if (param.name == "attackMs" || param.name == "decayMs" || 
            param.name == "sustain" || param.name == "releaseMs") {
            continue;
        }
        // Skip granular parameters (they're in granular controls now)
        if (param.name == "grainEnvelope") {
            continue;
        }
        // Note: grainSize can appear in both main table and granular controls
        // Only include numeric parameters
        if (param.type == ParameterType::FLOAT || param.type == ParameterType::INT) {
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
    // Position at top-left with minimum size (1x1 pixels)
    ImGui::SetCursorPos(ImVec2(0, 0));
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
    
    // Reset cursor to top after InvisibleButton to ensure table starts at the top
    ImGui::SetCursorPosY(0);
    
    // Note: Table styling is handled by CellGrid (CellPadding, ItemSpacing)
    
    // Reset focus tracking at start of frame
    callbacksState.resetFrame();
    
    // Use versioned table ID to reset column order if needed (change version number to force reset)
    static int tableVersion = 4; // v4: Removed enum/bool columns (moved to global controls)
    std::string tableId = "MediaPoolParameters_v" + std::to_string(tableVersion);
    
    // Configure CellGrid using unified helper
    CellGridConfig gridConfig;
    gridConfig.tableId = tableId;
    gridConfig.tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                            ImGuiTableFlags_SizingStretchProp;
    configureCellGrid(cellGrid, gridConfig);
        
    // Convert editableParams to CellGrid column configuration (numeric parameters only)
    std::vector<CellGridColumnConfig> tableColumnConfig;
    for (const auto& paramDesc : editableParams) {
        tableColumnConfig.push_back(CellGridColumnConfig(
            paramDesc.name, paramDesc.displayName, true, 0));  // isRemovable = true for all parameters
    }
    
    // Update column configuration using unified helper (only updates if changed)
    updateColumnConfigIfChanged(cellGrid, tableColumnConfig, lastColumnConfig);
    
    // Clear special column widget cache when column configuration changes
    if (tableColumnConfig != lastColumnConfig) {
        specialColumnWidgetCache.clear();
    }
    
    cellGrid.setAvailableParameters(editableParams);
    
    // Setup callbacks for CellGrid
    CellGridCallbacks callbacks;
    
    // Setup standard callbacks (focus tracking, edit mode, state sync)
    setupStandardCellGridCallbacks(callbacks, cellFocusState, callbacksState, cellGrid, true); // true = single row
    
    // MultiSampler-specific: Update isParentWidgetFocused in callbacks
    // Wrap the standard callbacks to also update isParentWidgetFocused
    auto originalOnCellFocusChanged = callbacks.onCellFocusChanged;
    callbacks.onCellFocusChanged = [this, originalOnCellFocusChanged](int row, int col) {
        if (originalOnCellFocusChanged) {
            originalOnCellFocusChanged(row, col);
        }
        isParentWidgetFocused = false;
    };
    
    auto originalOnCellClicked = callbacks.onCellClicked;
    callbacks.onCellClicked = [this, originalOnCellClicked](int row, int col) {
        if (originalOnCellClicked) {
            originalOnCellClicked(row, col);
        }
        isParentWidgetFocused = false;
    };
    
    callbacks.createCell = [this, sampler](int row, int col, const CellGridColumnConfig& colConfig) -> std::unique_ptr<BaseCell> {
        // Use parameter name directly from colConfig - no need to search through all parameters
        const std::string& paramName = colConfig.parameterName;
        
        // Skip "note" parameter (not editable in GUI, only used internally)
        if (paramName == "note") {
            return nullptr;
        }
        
        // Look up parameter descriptor by name
        // TODO: Could optimize with a parameter name -> ParameterDescriptor map for O(1) lookup
        auto editableParams = getEditableParameters();
        for (const auto& paramDesc : editableParams) {
            if (paramDesc.name == paramName) {
                return createCellWidgetForParameter(paramDesc);
            }
        }
        return nullptr; // Return nullptr if not found
    };
    callbacks.drawSpecialColumn = nullptr; // Will be set after all other callbacks are defined
    callbacks.getCellValue = [this, sampler](int row, int col, const CellGridColumnConfig& colConfig) -> float {
        // Use parameter name directly from colConfig - no index conversion needed
        const std::string& paramName = colConfig.parameterName;
        
        const SampleRef* displaySample = (sampler && selectedSampleIndex_ < sampler->getSampleCount()) 
            ? &sampler->getSample(selectedSampleIndex_) : nullptr;
        if (!displaySample) {
            // Fallback: look up default value from available parameters
            auto editableParams = getEditableParameters();
            for (const auto& paramDesc : editableParams) {
                if (paramDesc.name == paramName) {
                    return paramDesc.defaultValue;
                }
            }
            return 0.0f;
        }
        
        // Read from parameter state cache (synced from voice during playback, editable when idle)
        if (paramName == "position") {
            return displaySample->currentStartPosition;
        } else if (paramName == "speed") {
            return displaySample->currentSpeed;
        } else if (paramName == "volume") {
            return displaySample->currentVolume;
        } else if (paramName == "regionStart") {
            return displaySample->currentRegionStart;
        } else if (paramName == "regionEnd") {
            return displaySample->currentRegionEnd;
        } else if (paramName == "grainSize" || paramName == "loopSize") {
            return displaySample->currentGrainSize;
        }
        
        // Fallback: look up default value from available parameters
        auto editableParams = getEditableParameters();
        for (const auto& paramDesc : editableParams) {
            if (paramDesc.name == paramName) {
                return paramDesc.defaultValue;
            }
        }
        return 0.0f;
    };
    callbacks.setCellValue = [this](int row, int col, float value, const CellGridColumnConfig& colConfig) {
        // Use parameter name directly from colConfig - no index conversion needed
        const std::string& paramName = colConfig.parameterName;
        
        // Route through command queue for thread-safe parameter changes
        setParameterViaCommand(paramName, value);
    };
    callbacks.onRowStart = [](int row, bool isPlaybackRow, bool isEditRow) {
        // Set row background color
        ImU32 rowBgColor = GUIConstants::toU32(GUIConstants::Background::TableRowFilled);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowBgColor);
    };
    // State sync callbacks removed - BaseCell manages its own state internally
    // MediaPoolGUI only tracks which column is focused for UI purposes
    // Track if header was clicked to clear button cell focus
    // Setup header click callback to detect when headers are clicked
    callbacks.onHeaderClicked = [this](int col) {
        callbacksState.headerClickedThisFrame = true;
    };
    
    // Setup custom header rendering callback for Position parameter
    callbacks.drawCustomHeader = [this](int col, const CellGridColumnConfig& colConfig, ImVec2 cellStartPos, float columnWidth, float cellMinY) -> bool {
        if (colConfig.parameterName == "position") {
            // Draw column name first (standard header)
            ImGui::TableHeader(colConfig.displayName.c_str());
            
            // Check if header was clicked
            if (ImGui::IsItemClicked(0)) {
                callbacksState.headerClickedThisFrame = true;
            }
            
            return true; // Header was drawn by custom callback
        } else {
            // Default header for other parameters (use CellGrid's default rendering)
            // Header click detection happens in CellGrid via onHeaderClicked callback
            return false; // Let CellGrid handle default rendering
        }
        };
    
    // Now set drawSpecialColumn after all other callbacks are defined
    // Capture callbacks by value to avoid dangling references
    auto getCellValueCallback = callbacks.getCellValue;
    auto setCellValueCallback = callbacks.setCellValue;
    auto createCellCallback = callbacks.createCell;
    auto isCellFocusedCallback = callbacks.isCellFocused;
    auto onCellFocusChangedCallback = callbacks.onCellFocusChanged;
    auto onCellClickedCallback = callbacks.onCellClicked;
    
    callbacks.drawSpecialColumn = [this, sampler, getCellValueCallback, setCellValueCallback, 
                                    createCellCallback, isCellFocusedCallback,
                                    onCellFocusChangedCallback, onCellClickedCallback]
                                    (int row, int col, const CellGridColumnConfig& colConfig) {
        const std::string& paramName = colConfig.parameterName;
        
        // All columns are now numeric parameters - manually render BaseCell (replicating CellGrid's default behavior)
        // Button/enum columns (mediaIndex, playStyle, polyphonyMode) are now in global controls
        {
            // Get focus state
            bool isFocused = ModuleGUI::isCellFocused(cellFocusState, row, col);
            if (!isFocused && isCellFocusedCallback) {
                isFocused = isCellFocusedCallback(row, col);
            }
            
            // Get or create cell widget from cache
            auto key = std::make_pair(row, col);
            auto it = specialColumnWidgetCache.find(key);
            if (it == specialColumnWidgetCache.end()) {
                // Create new widget using callback
                if (createCellCallback) {
                    auto newCell = createCellCallback(row, col, colConfig);
                    if (newCell) {
                        specialColumnWidgetCache[key] = std::move(newCell);
                        it = specialColumnWidgetCache.find(key);
                    } else {
                        // Callback returned nullptr - skip rendering
                        return;
                    }
                } else {
                    // No callback - skip rendering
                    return;
                }
            }
            BaseCell& cell = *(it->second);
            
            // Set up callbacks on first creation (for NumCell specifically)
            NumCell* numCell = dynamic_cast<NumCell*>(it->second.get());
            if (numCell && !numCell->getCurrentValue && getCellValueCallback) {
                int rowCapture = row;
                int colCapture = col;
                const CellGridColumnConfig colConfigCapture = colConfig;
                numCell->getCurrentValue = [rowCapture, colCapture, colConfigCapture, getCellValueCallback]() -> float {
                    return getCellValueCallback(rowCapture, colCapture, colConfigCapture);
                };
            }
            
            if (!cell.onValueApplied && setCellValueCallback) {
                int rowCapture = row;
                int colCapture = col;
                const CellGridColumnConfig colConfigCapture = colConfig;
                // For NumCell, also set float callback for efficiency
                if (numCell) {
                    numCell->onValueAppliedFloat = [rowCapture, colCapture, colConfigCapture, setCellValueCallback](const std::string&, float value) {
                        setCellValueCallback(rowCapture, colCapture, value, colConfigCapture);
                    };
                }
                // Set BaseCell string callback for unified interface
                cell.onValueApplied = [rowCapture, colCapture, colConfigCapture, setCellValueCallback](const std::string&, const std::string& valueStr) {
                    try {
                        float value = std::stof(valueStr);
                        setCellValueCallback(rowCapture, colCapture, value, colConfigCapture);
                    } catch (...) {
                        // Ignore parse errors
                    }
                };
            }
            
            // BaseCell manages its own state (editing, buffer, drag, selection)
            // We only track which cell is focused for UI coordination
            // onEditModeChanged callback is set up by CellGrid automatically
            
            // Draw cell - BaseCell handles all state internally
            int uniqueId = row * 1000 + col;
            
            CellInteraction interaction = cell.draw(uniqueId, isFocused, false);
            
            // Handle interactions
            bool actuallyFocused = ImGui::IsItemFocused();
            
            if (interaction.focusChanged) {
                if (actuallyFocused) {
                    setCellFocus(cellFocusState, row, col, paramName);
                    callbacksState.anyCellFocusedThisFrame = true;
                } else if (cellFocusState.column == col) {
                    ModuleGUI::clearCellFocus(cellFocusState);
                }
                
                if (onCellFocusChangedCallback) {
                    onCellFocusChangedCallback(row, col);
                }
            }
            
            if (interaction.clicked) {
                setCellFocus(cellFocusState, row, col, paramName);
                if (onCellClickedCallback) {
                    onCellClickedCallback(row, col);
                }
            }
            
            isFocused = actuallyFocused;
            
            // Track editing state for UI purposes (BaseCell manages its own edit buffer)
            if (cell.isEditingMode() && isFocused) {
                cellFocusState.isEditing = true;
                callbacksState.anyCellFocusedThisFrame = true;
            } else if (cellFocusState.isEditing && isFocused && !cell.isEditingMode()) {
                cellFocusState.isEditing = false;
                // Note: Refocus is now handled automatically by BaseCell when exiting edit mode
            }
            
            return; // Done rendering BaseCell for this column
        }
    };
    
    cellGrid.setCallbacks(callbacks);
    
    // Begin table (single row, no fixed columns)
    cellGrid.beginTable(1, 0); // 1 row, 0 fixed columns
    
    // Draw headers (handled by CellGrid automatically)
    // Header click detection happens in the custom header callback above
    cellGrid.drawHeaders(0, nullptr);
    
    // Draw single row (handled by CellGrid automatically)
    cellGrid.drawRow(0, 0, false, false, nullptr);
        
        // Clear shouldFocusFirstCell flag after drawing
        if (shouldFocusFirstCell) {
            shouldFocusFirstCell = false;
        }
        
        // Clear focus if:
        // Clear focus using unified helper if conditions are met
        // Conditions: header clicked OR (cell focused but no cell focused this frame AND not editing)
        ModuleGUI::handleFocusClearing(cellFocusState, callbacksState);
        
    // End table
    cellGrid.endTable();
    
    // Check for clicks outside the grid (after table ends)
    // This handles clicks on empty space within the window
    if (cellFocusState.hasFocus() && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
        ModuleGUI::clearCellFocus(cellFocusState);
    }
}

void MultiSamplerGUI::drawADSRParameters(PlayStyle currentPlayStyle) {
    MultiSampler* sampler = getMultiSampler();
    if (!sampler) return;
    
    // Only show ADSR in ONCE and LOOP modes
    // Use cached PlayStyle passed as parameter (avoids lock acquisition during command processing)
    if (currentPlayStyle != PlayStyle::ONCE && currentPlayStyle != PlayStyle::LOOP) {
        return;
    }
    
    // Get ADSR parameters
    std::vector<ParameterDescriptor> adsrParams;
    auto allParams = sampler->getParameters();
    for (const auto& param : allParams) {
        if (param.name == "attackMs" || param.name == "decayMs" || 
            param.name == "sustain" || param.name == "releaseMs") {
            adsrParams.push_back(param);
        }
    }
    
    if (adsrParams.empty()) return;
    
    // Calculate table height (similar to main parameter table)
    float tableHeaderHeight = ImGui::GetFrameHeight();
    float tableRowHeight = ImGui::GetFrameHeight();
    float cellVerticalPadding = 4.0f;
    float adsrTableHeight = tableHeaderHeight + tableRowHeight + cellVerticalPadding;
    
    ImGui::BeginChild("ADSRParameters", ImVec2(0, adsrTableHeight), false, ImGuiWindowFlags_NoScrollbar);
    
    // Configure CellGrid for ADSR
    static int adsrTableVersion = 1;
    std::string tableId = "ADSRParameters_v" + std::to_string(adsrTableVersion);
    
    CellGridConfig gridConfig;
    gridConfig.tableId = tableId;
    gridConfig.tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                            ImGuiTableFlags_SizingStretchProp;
    configureCellGrid(adsrCellGrid, gridConfig);
    
    // Convert ADSR parameters to column configuration
    std::vector<CellGridColumnConfig> adsrColumnConfig;
    for (const auto& paramDesc : adsrParams) {
        adsrColumnConfig.push_back(CellGridColumnConfig(
            paramDesc.name, paramDesc.displayName, true, 0));
    }
    
    // Update column configuration
    updateColumnConfigIfChanged(adsrCellGrid, adsrColumnConfig, lastADSRColumnConfig);
    
    adsrCellGrid.setAvailableParameters(adsrParams);
    
    // Setup callbacks for ADSR CellGrid
    CellGridCallbacks callbacks;
    CellFocusState adsrFocusState;
    CellGridCallbacksState adsrCallbacksState;
    
    setupStandardCellGridCallbacks(callbacks, adsrFocusState, adsrCallbacksState, adsrCellGrid, true);
    
    callbacks.createCell = [this, sampler](int row, int col, const CellGridColumnConfig& colConfig) -> std::unique_ptr<BaseCell> {
        const std::string& paramName = colConfig.parameterName;
        auto allParams = sampler->getParameters();
        for (const auto& paramDesc : allParams) {
            if (paramDesc.name == paramName) {
                return createCellWidgetForParameter(paramDesc);
            }
        }
        return nullptr;
    };
    
    callbacks.getCellValue = [this, sampler](int row, int col, const CellGridColumnConfig& colConfig) -> float {
        const std::string& paramName = colConfig.parameterName;
        return sampler->getParameter(paramName);
    };
    
    callbacks.setCellValue = [this](int row, int col, float value, const CellGridColumnConfig& colConfig) {
        const std::string& paramName = colConfig.parameterName;
        setParameterViaCommand(paramName, value);
    };
    
    callbacks.onRowStart = [](int row, bool isPlaybackRow, bool isEditRow) {
        ImU32 rowBgColor = GUIConstants::toU32(GUIConstants::Background::TableRowFilled);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowBgColor);
    };
    
    adsrCellGrid.setCallbacks(callbacks);
    
    // Draw ADSR table
    adsrCellGrid.beginTable(1, 0); // 1 row, 0 fixed columns
    adsrCellGrid.drawHeaders(0, nullptr);
    adsrCellGrid.drawRow(0, 0, false, false, nullptr);
    adsrCellGrid.endTable();
    
    ImGui::EndChild();
}

void MultiSamplerGUI::drawGranularControls(PlayStyle currentPlayStyle) {
    MultiSampler* sampler = getMultiSampler();
    if (!sampler) return;
    
    // Only show granular controls in GRAIN mode
    // Use cached PlayStyle passed as parameter (avoids lock acquisition during command processing)
    if (currentPlayStyle != PlayStyle::GRAIN) {
        return;
    }
    
    // Get granular parameters
    std::vector<ParameterDescriptor> granularParams;
    auto allParams = sampler->getParameters();
    for (const auto& param : allParams) {
        if (param.name == "grainSize" || param.name == "grainEnvelope") {
            granularParams.push_back(param);
        }
    }
    
    if (granularParams.empty()) return;
    
    // Calculate table height
    float tableHeaderHeight = ImGui::GetFrameHeight();
    float tableRowHeight = ImGui::GetFrameHeight();
    float cellVerticalPadding = 4.0f;
    float granularTableHeight = tableHeaderHeight + tableRowHeight + cellVerticalPadding;
    
    ImGui::BeginChild("GranularControls", ImVec2(0, granularTableHeight), false, ImGuiWindowFlags_NoScrollbar);
    
    // Configure CellGrid for granular controls
    static int granularTableVersion = 1;
    std::string tableId = "GranularControls_v" + std::to_string(granularTableVersion);
    
    CellGridConfig gridConfig;
    gridConfig.tableId = tableId;
    gridConfig.tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                            ImGuiTableFlags_SizingStretchProp;
    configureCellGrid(granularCellGrid, gridConfig);
    
    // Convert granular parameters to column configuration
    std::vector<CellGridColumnConfig> granularColumnConfig;
    for (const auto& paramDesc : granularParams) {
        granularColumnConfig.push_back(CellGridColumnConfig(
            paramDesc.name, paramDesc.displayName, true, 0));
    }
    
    // Update column configuration
    updateColumnConfigIfChanged(granularCellGrid, granularColumnConfig, lastGranularColumnConfig);
    
    granularCellGrid.setAvailableParameters(granularParams);
    
    // Setup callbacks for granular CellGrid
    CellGridCallbacks callbacks;
    CellFocusState granularFocusState;
    CellGridCallbacksState granularCallbacksState;
    
    setupStandardCellGridCallbacks(callbacks, granularFocusState, granularCallbacksState, granularCellGrid, true);
    
    callbacks.createCell = [this, sampler](int row, int col, const CellGridColumnConfig& colConfig) -> std::unique_ptr<BaseCell> {
        const std::string& paramName = colConfig.parameterName;
        auto allParams = sampler->getParameters();
        for (const auto& paramDesc : allParams) {
            if (paramDesc.name == paramName) {
                return createCellWidgetForParameter(paramDesc);
            }
        }
        return nullptr;
    };
    
    callbacks.getCellValue = [this, sampler](int row, int col, const CellGridColumnConfig& colConfig) -> float {
        const std::string& paramName = colConfig.parameterName;
        const SampleRef* displaySample = (sampler && selectedSampleIndex_ < sampler->getSampleCount()) 
            ? &sampler->getSample(selectedSampleIndex_) : nullptr;
        if (!displaySample) {
            return sampler->getParameter(paramName);
        }
        
        // Read from parameter state cache (synced from voice during playback, editable when idle)
        if (paramName == "grainSize" || paramName == "loopSize") {
            return displaySample->currentGrainSize;
        } else if (paramName == "grainEnvelope") {
            // For now, return parameter value (grainEnvelope not yet stored in SampleRef)
            return sampler->getParameter(paramName);
        }
        
        return sampler->getParameter(paramName);
    };
    
    callbacks.setCellValue = [this](int row, int col, float value, const CellGridColumnConfig& colConfig) {
        const std::string& paramName = colConfig.parameterName;
        setParameterViaCommand(paramName, value);
    };
    
    callbacks.onRowStart = [](int row, bool isPlaybackRow, bool isEditRow) {
        ImU32 rowBgColor = GUIConstants::toU32(GUIConstants::Background::TableRowFilled);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowBgColor);
    };
    
    granularCellGrid.setCallbacks(callbacks);
    
    // Draw granular table
    granularCellGrid.beginTable(1, 0); // 1 row, 0 fixed columns
    granularCellGrid.drawHeaders(0, nullptr);
    granularCellGrid.drawRow(0, 0, false, false, nullptr);
    granularCellGrid.endTable();
    
    ImGui::EndChild();
}

void MultiSamplerGUI::clearCellFocus() {
    ModuleGUI::clearCellFocus(cellFocusState);
}

// Sync edit state from ImGui focus - called from InputRouter when keys are pressed
void MultiSamplerGUI::syncEditStateFromImGuiFocus(MediaPoolGUI& gui) {
    // Check if editingColumnIndex is already valid (GUI sync already happened)
    if (gui.cellFocusState.column >= 0) {
        // If editingParameter is empty but column is set, look it up from column config
        if (gui.cellFocusState.editingParameter.empty() && gui.multiSampler_) {
            // Get column configuration from CellGrid
            auto columnConfig = gui.cellGrid.getColumnConfiguration();
            if (gui.cellFocusState.column >= 0 && (size_t)gui.cellFocusState.column < columnConfig.size()) {
                gui.cellFocusState.editingParameter = columnConfig[gui.cellFocusState.column].parameterName;
            }
        }
        return; // Already synced
    }
    
    // GUI draw sync should handle this every frame
    // If not set, handleKeyPress will default gracefully
}



// Button widget creation functions removed - buttons are drawn directly via drawSpecialColumn
// using ImGui::Button() for better performance (see drawSpecialColumn callback)

/// MARK: - MEDIA LIST
/// @brief draw the media list
/// @return void
void MultiSamplerGUI::drawMediaList() {
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
    

    ImGui::PopID();
    
    // Track if any list item is focused (to update parent widget focus state)
    bool anyListItemFocused = false;
    
    MultiSampler* sampler = getMultiSampler();
    if (!sampler) return;
    
    // Get current index for auto-scrolling
    size_t currentIndex = selectedSampleIndex_;
    
    // Track if index changed to determine if we should sync scroll
    bool shouldSyncScroll = (currentIndex != previousMediaIndex);
    
    // Get polyphony mode for display logic
    PolyphonyMode polyMode = sampler->getPolyphonyMode();
    bool isPolyMode = (polyMode == PolyphonyMode::POLYPHONIC);
    
    // SAMPLER-INSPIRED: Show indexed sample list from sample bank
    size_t numSamples = sampler->getSampleCount();
    if (numSamples > 0) {
        // Log list iteration for troubleshooting
        ofLogVerbose("MediaPoolGUI") << "[drawMediaList] Iterating " << numSamples << " samples";
        
        for (size_t i = 0; i < numSamples; i++) {
            const SampleRef& sample = sampler->getSample(i);
            
            // PROFESSIONAL VOICE STATE TRACKING
            // Check if this sample is the currently displayed one (for waveform sync)
            bool isDisplayed = (i == currentIndex);
            
            // Check if sample is actively playing (any voice)
            bool isPlaying = sampler->isSamplePlaying(static_cast<int>(i));
            
            // Get voice count for polyphonic mode display
            int voiceCount = 0;
            if (isPolyMode) {
                voiceCount = sampler->getVoiceCountForSample(static_cast<int>(i));
            }
            
            // Create clean display format: [01] [AV] Title
            std::string indexStr = "[" + std::to_string(i + 1); // Start at 01, not 00
            if (indexStr.length() < 3) {
                indexStr = "[" + std::string(2 - (indexStr.length() - 1), '0') + std::to_string(i + 1);
            }
            indexStr += "]";
            
            // Get media type badge from sample paths
            std::string mediaType = "";
            bool hasAudio = !sample.audioPath.empty();
            bool hasVideo = !sample.videoPath.empty();
            if (hasAudio && hasVideo) {
                mediaType = "[AV]";
            } else if (hasAudio) {
                mediaType = "[A]";
            } else if (hasVideo) {
                mediaType = "[V]";
            } else {
                mediaType = "--";
            }
            
            // Get clean title from display name
            std::string title = sample.displayName;
            if (title.empty()) {
                title = "Empty";
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
            
            // Final display name: [01] [AV] Title
            std::string displayName = indexStr + " " + mediaType + " " + title;
            
            // PROFESSIONAL COLOR CODING
            // Priority: Playing > Displayed > Default
            // Playing samples get green text (highest priority visual feedback)
            // Displayed sample gets white background (for waveform context)
            if (isDisplayed) {
                ImGui::PushStyleColor(ImGuiCol_Header, GUIConstants::Active::MediaItem);
            }
            if (isPlaying) {
                ImGui::PushStyleColor(ImGuiCol_Text, GUIConstants::Text::Playing);
            }
            
            // Make items selectable and clickable
            if (ImGui::Selectable(displayName.c_str(), isDisplayed)) {
                // CLICK-TO-PLAY: Trigger sample to play normally (no gate duration)
                MultiSampler* clickedPool = getMultiSampler();
                if (!clickedPool) {
                    ofLogError("MultiSamplerGUI") << "[CRASH PREVENTION] MultiSampler became null when clicking sample at index " << i;
                } else if (i >= clickedPool->getSampleCount()) {
                    ofLogError("MultiSamplerGUI") << "[CRASH PREVENTION] Index " << i << " out of bounds when clicking sample";
                } else {
                    // Set as displayed sample (for waveform sync)
                    selectedSampleIndex_ = i;  // GUI manages selection
                    
                    // Play sample normally (no gate duration - plays until stopped)
                    // In MONO mode: stops previous sample automatically
                    // In POLY mode: adds new voice (up to MAX_VOICES limit)
                    clickedPool->playMediaManual(i);
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
                // Use preview player if scrubbing, otherwise show basic info
                MediaPlayer* tooltipPlayer = sample.isScrubbing ? sample.previewPlayer.get() : nullptr;
                
                if (tooltipPlayer) {
                    MediaPreview::drawMediaTooltip(tooltipPlayer, static_cast<int>(i));
                } else {
                    // Show simple tooltip with sample info + voice state
                    ImGui::BeginTooltip();
                    ImGui::Text("Sample %zu: %s", i, sample.displayName.c_str());
                    if (!sample.audioPath.empty()) ImGui::Text("Audio: %s", ofFilePath::getFileName(sample.audioPath).c_str());
                    if (!sample.videoPath.empty()) ImGui::Text("Video: %s", ofFilePath::getFileName(sample.videoPath).c_str());
                    if (sample.duration > 0.0f) {
                        ImGui::Text("Duration: %.2fs", sample.duration);
                    }
                    
                    // Voice state info
                    if (isPolyMode && voiceCount > 0) {
                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), 
                                         "Playing: %d voice%s", 
                                         voiceCount, voiceCount > 1 ? "s" : "");
                    } else if (isPlaying) {
                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Playing");
                    }
                    
                    if (sample.isReadyForPlayback()) {
                        ImGui::TextDisabled("(Click to preview)");
                    } else {
                        ImGui::TextDisabled("(Not loaded)");
                    }
                    ImGui::EndTooltip();
                }
            }
            
            // Add right-click context menu
            if (ImGui::BeginPopupContextItem(("MediaContext" + std::to_string(i)).c_str())) {
                ImGui::Text("Sample %zu: %s", i, sample.displayName.c_str());
                ImGui::Separator();
                
                // Add "Stop All Voices" option in poly mode when sample is playing
                if (isPolyMode && voiceCount > 0) {
                    if (ImGui::MenuItem("Stop All Voices")) {
                        // Release all voices playing this sample
                        auto voices = sampler->getVoicesForSample(static_cast<int>(i));
                        for (auto* voice : voices) {
                            voice->release();
                            voice->state = Voice::RELEASING;
                        }
                    }
                    ImGui::Separator();
                }
                
                // Check if sample is loaded (has shared audio or display player)
                bool isLoaded = sample.isReadyForPlayback();
                
                if (ImGui::MenuItem("Unload from Memory", nullptr, false, isLoaded)) {
                    // Unload the sample's shared audio and display player
                    SampleRef& mutableSample = sampler->getSampleMutable(i);
                    mutableSample.unloadSharedAudio();
                    
                    // Release all voices playing this sample
                    auto voices = sampler->getVoicesForSample(static_cast<int>(i));
                    for (auto* voice : voices) {
                        voice->stop();
                        voice->state = Voice::FREE;
                    }
                }
                
                ImGui::Separator();
                
                if (ImGui::MenuItem("Remove from List")) {
                    sampler->removeSample(i);
                }
                
                ImGui::EndPopup();
            }
            
            // Pop style colors if we pushed them
            if (isPlaying) {
                ImGui::PopStyleColor();
            }
            if (isDisplayed) {
                ImGui::PopStyleColor();
            }
        }
    } else {
        // Show message when no media files are loaded
        ImGui::TextDisabled("No media files loaded");
        ImGui::TextDisabled("Drag files here or use 'Browse Directory' to add media");
    }
    ImGui::Separator();
    
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

/// MARK: - WAVEFORM
/// @brief draw waveform for the active player
void MultiSamplerGUI::drawWaveform() {
    MultiSampler* sampler = getMultiSampler();
    if (!sampler) return;
    
    // Get current media index for per-index zoom state
    size_t currentIndex = selectedSampleIndex_;
        const SampleRef* displaySample = (sampler && selectedSampleIndex_ < sampler->getSampleCount()) 
            ? &sampler->getSample(selectedSampleIndex_) : nullptr;
    
    auto zoomState = getWaveformZoomState(currentIndex);
    float waveformZoom = zoomState.first;
    float waveformOffset = zoomState.second;
    
    // Create invisible button for interaction area (ensure non-zero size for ImGui)
    float safeHeight = std::max(waveformHeight, 1.0f);
    float availableWidth = std::max(ImGui::GetContentRegionAvail().x, 100.0f); // Fallback if window not ready
    
    ImVec2 canvasSize = ImVec2(availableWidth, safeHeight);
    ImGui::InvisibleButton("waveform_canvas", canvasSize);
    
    // Get draw list and canvas dimensions
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetItemRectMin();
    ImVec2 canvasMax = ImGui::GetItemRectMax();
    float canvasWidth = canvasMax.x - canvasPos.x;
    float canvasHeight = canvasMax.y - canvasPos.y;
    float centerY = canvasPos.y + canvasHeight * 0.5f;
    
    // Draw background
    ImU32 bgColor = GUIConstants::toIM_COL32(GUIConstants::Background::Waveform);
    drawList->AddRectFilled(canvasPos, canvasMax, bgColor);
    
    // If no sample, show message and return early
    if (!displaySample || !displaySample->isReadyForPlayback()) {
        // Draw message centered in the waveform rectangle
        const char* message = "No sample loaded to display waveform.";
        ImVec2 textSize = ImGui::CalcTextSize(message);
        ImVec2 textPos = ImVec2(
            canvasPos.x + (canvasWidth - textSize.x) * 0.5f,
            canvasPos.y + (canvasHeight - textSize.y) * 0.5f
        );
        drawList->AddText(textPos, GUIConstants::toIM_COL32(GUIConstants::Text::Disabled), message);
        return;
    }
    
    // CRITICAL: Check if dragging a BaseCell (NumCell) - prevents interference with waveform interactions
    // Check cached widgets to see if any are currently dragging
    bool isDraggingParameter = false;
    for (const auto& [key, cell] : specialColumnWidgetCache) {
        if (cell && cell->isDragging()) {
            isDraggingParameter = true;
            break;
        }
    }
    
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
            newZoom = std::max(1.0f, std::min(10000.0f, newZoom)); // Clamp zoom (10000x for extreme precision)
            
            // Calculate new offset to keep mouse position fixed
            float newVisibleRange = 1.0f / newZoom;
            float newOffset = mouseTimeAbsolute - mouseTime * newVisibleRange;
            newOffset = std::max(0.0f, std::min(1.0f - newVisibleRange, newOffset));
            
            // Store updated zoom state for current index
            setWaveformZoomState(currentIndex, newZoom, newOffset);
            waveformZoom = newZoom;
            waveformOffset = newOffset;
            // Invalidate waveform cache when zoom changes
            waveformCacheValid_ = false;
        }
        
        // Handle panning with middle mouse or Shift+drag (only if not dragging a marker or BaseCell)
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
                // Invalidate waveform cache when pan changes
                waveformCacheValid_ = false;
                
                ImGui::ResetMouseDragDelta(ImGui::IsMouseDown(2) ? 2 : 0);
            }
        }
        
        // Double-click to reset zoom
        if (ImGui::IsMouseDoubleClicked(0)) {
            setWaveformZoomState(currentIndex, 1.0f, 0.0f);
            waveformZoom = 1.0f;
            waveformOffset = 0.0f;
            // Invalidate waveform cache when zoom resets
            waveformCacheValid_ = false;
        }
    }
    
    // Calculate visible time range
    float visibleRange = 1.0f / waveformZoom;
    float visibleStart = waveformOffset;
    
    // Waveform data for rendering (min/max pairs for industry-standard visualization)
    bool hasAudioData = false;
    int numChannels = 0;
    int actualPoints = 0;
    std::vector<float> waveformTimeData;
    std::vector<std::vector<float>> waveformChannelMinData;
    std::vector<std::vector<float>> waveformChannelMaxData;
    
    // Read audio buffer from shared audio file (not from player)
    if (displaySample->sharedAudioFile && displaySample->sharedAudioFile->isLoaded()) {
        // Cache audio buffer (getBuffer() is expensive ~10ms)
        std::string currentAudioPath = displaySample->audioPath;
        bool bufferNeedsRefresh = !audioBufferCacheValid_ || (cachedAudioFilePath_ != currentAudioPath);
        
        if (bufferNeedsRefresh) {
            cachedAudioBuffer_ = displaySample->getAudioBuffer();
            cachedAudioFilePath_ = currentAudioPath;
            audioBufferCacheValid_ = true;
            waveformCacheValid_ = false; // Invalidate waveform cache
        }
        const ofSoundBuffer& buffer = cachedAudioBuffer_;
        
        int numFrames = buffer.getNumFrames();
        numChannels = buffer.getNumChannels();
        
        if (numFrames > 0 && numChannels > 0) {
            hasAudioData = true;
            
            // Check if waveform cache is valid
            bool cacheValid = waveformCacheValid_ &&
                             (cachedMediaIndex_ == currentIndex) &&
                             (cachedNumFrames_ == numFrames) &&
                             (cachedNumChannels_ == numChannels) &&
                             (std::abs(cachedVisibleStart_ - visibleStart) < 0.0001f) &&
                             (std::abs(cachedVisibleRange_ - visibleRange) < 0.0001f) &&
                             (std::abs(cachedCanvasWidth_ - canvasWidth) < 1.0f);
            
            if (cacheValid && !cachedWaveformTimeData_.empty()) {
                // Use cached waveform data
                waveformTimeData = cachedWaveformTimeData_;
                waveformChannelMinData = cachedWaveformMinData_;
                waveformChannelMaxData = cachedWaveformMaxData_;
                actualPoints = static_cast<int>(waveformTimeData.size());
            } else {
                // Recalculate waveform data with adaptive quality
                // Calculate points based on canvas width (pixels) and zoom level
                // Base: 2.0 points per pixel for better unzoomed precision (increased from 1.5)
                float pointsPerPixel = 2.0f;
                
                // Adaptive precision scaling for deep zooming
                // Uses logarithmic scaling to provide more precision at higher zoom levels
                if (visibleRange < 1.0f) {
                    float zoomLevel = 1.0f / visibleRange; // 1.0 = no zoom, 10000.0 = 10000x zoom
                    
                    // Logarithmic scaling: provides smooth precision increase from 1x to 10000x zoom
                    // At 1x zoom: multiplier = 1.0
                    // At 10x zoom: multiplier ≈ 1.5
                    // At 100x zoom: multiplier ≈ 2.0
                    // At 1000x zoom: multiplier ≈ 2.5
                    // At 10000x zoom: multiplier ≈ 3.0
                    // This ensures we get more detail as we zoom deeper without excessive points at low zoom
                    float logZoom = std::log10(std::max(1.0f, zoomLevel));
                    float zoomDetailMultiplier = 1.0f + logZoom * 0.5f; // Logarithmic scaling factor
                    
                    // Cap at 10.0x for extremely deep zoom (10000x+) to prevent excessive point counts
                    // This allows up to 20 points per pixel at maximum zoom
                    pointsPerPixel *= std::min(zoomDetailMultiplier, 10.0f);
                }
                
                // Calculate max points based on canvas width and adaptive precision
                int maxPoints = (int)(canvasWidth * pointsPerPixel);
                // Clamp to reasonable bounds (supports up to 64000 points for extreme zoom)
                maxPoints = std::max(MIN_WAVEFORM_POINTS, std::min(MAX_WAVEFORM_POINTS, maxPoints));
                
                int stepSize = std::max(1, numFrames / maxPoints);
                actualPoints = std::min(maxPoints, numFrames / stepSize);
                
                waveformTimeData.resize(actualPoints);
                waveformChannelMinData.resize(numChannels);
                waveformChannelMaxData.resize(numChannels);
                for (int ch = 0; ch < numChannels; ch++) {
                    waveformChannelMinData[ch].resize(actualPoints);
                    waveformChannelMaxData[ch].resize(actualPoints);
                }
                
                // Downsample audio using min/max peak detection (industry standard)
                for (int i = 0; i < actualPoints; i++) {
                    // Map point index to time position within visible range
                    float timePos = (float)i / (float)actualPoints;
                    float absoluteTime = visibleStart + timePos * visibleRange;
                    
                    // Clamp to valid range
                    absoluteTime = std::max(0.0f, std::min(1.0f, absoluteTime));
                    
                    // Calculate sample range for this display point
                    // Each point represents a range of samples to avoid aliasing
                    float nextTimePos = (float)(i + 1) / (float)actualPoints;
                    float nextAbsoluteTime = visibleStart + nextTimePos * visibleRange;
                    nextAbsoluteTime = std::max(0.0f, std::min(1.0f, nextAbsoluteTime));
                    
                    // Convert time range to sample indices (use float precision)
                    float startSample = absoluteTime * numFrames;
                    float endSample = nextAbsoluteTime * numFrames;
                    
                    // Ensure we have at least one sample
                    int startIdx = std::max(0, std::min(numFrames - 1, (int)std::floor(startSample)));
                    int endIdx = std::max(0, std::min(numFrames - 1, (int)std::floor(endSample)));
                    
                    // Use at least one sample, but prefer range for smoothing
                    if (endIdx <= startIdx) {
                        endIdx = std::min(numFrames - 1, startIdx + 1);
                    }
                    
                    waveformTimeData[i] = timePos; // Normalized time within visible range (0-1)
                    
                    // Find min/max across sample range for each channel
                    for (int ch = 0; ch < numChannels; ch++) {
                        float minVal = buffer.getSample(startIdx, ch);
                        float maxVal = minVal;
                        
                        for (int s = startIdx; s <= endIdx && s < numFrames; s++) {
                            float sample = buffer.getSample(s, ch);
                            minVal = std::min(minVal, sample);
                            maxVal = std::max(maxVal, sample);
                        }
                        
                        // Store both min and max (preserves full dynamic range)
                        waveformChannelMinData[ch][i] = minVal;
                        waveformChannelMaxData[ch][i] = maxVal;
                    }
                }
                
                // Cache the calculated waveform data
                cachedWaveformTimeData_ = waveformTimeData;
                cachedWaveformMinData_ = waveformChannelMinData;
                cachedWaveformMaxData_ = waveformChannelMaxData;
                cachedVisibleStart_ = visibleStart;
                cachedVisibleRange_ = visibleRange;
                cachedCanvasWidth_ = canvasWidth;
                cachedNumFrames_ = numFrames;
                cachedNumChannels_ = numChannels;
                cachedMediaIndex_ = currentIndex;
                waveformCacheValid_ = true;
            }
        }
    } else {
        // No audio - invalidate all caches
        audioBufferCacheValid_ = false;
        waveformCacheValid_ = false;
    }
    
    // Draw waveform using industry-standard min/max vertical lines
    if (hasAudioData) {
        float amplitudeScale = canvasHeight * WAVEFORM_AMPLITUDE_SCALE;
        float volume = displaySample->currentVolume;
        ImU32 lineColor = GUIConstants::toU32(GUIConstants::Waveform::Line);
        
        // Draw each channel as vertical lines from min to max
        for (int ch = 0; ch < numChannels; ch++) {
            for (int i = 0; i < actualPoints; i++) {
                float x = canvasPos.x + waveformTimeData[i] * canvasWidth;
                float yMin = centerY - waveformChannelMinData[ch][i] * volume * amplitudeScale;
                float yMax = centerY - waveformChannelMaxData[ch][i] * volume * amplitudeScale;
                
                // Vertical line from min to max (filled waveform appearance)
                drawList->AddLine(ImVec2(x, yMin), ImVec2(x, yMax), lineColor, 1.0f);
            }
        }
    }
    
    // Draw controls (markers) on top of waveform
    drawWaveformControls(canvasPos, canvasMax, canvasWidth, canvasHeight);
}

/// MARK: - WF ctrls
/// @brief draw controls for the waveform
/// @param canvasPos 
/// @param canvasMax 
/// @param canvasWidth 
/// @param canvasHeight 
/// @return void
void MultiSamplerGUI::drawWaveformControls(const ImVec2& canvasPos, const ImVec2& canvasMax, float canvasWidth, float canvasHeight) {
    MultiSampler* sampler = getMultiSampler();
    if (!sampler) return;
    
    const SampleRef* displaySample = (sampler && selectedSampleIndex_ < sampler->getSampleCount()) 
        ? &sampler->getSample(selectedSampleIndex_) : nullptr;
    if (!displaySample || !displaySample->isReadyForPlayback()) return;
    
    // CRITICAL: Check if dragging a BaseCell (NumCell) - prevents interference with waveform interactions
    // Check cached widgets to see if any are currently dragging
    bool isDraggingParameter = false;
    for (const auto& [key, cell] : specialColumnWidgetCache) {
        if (cell && cell->isDragging()) {
            isDraggingParameter = true;
            break;
        }
    }
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Get current media index for per-index zoom state
    size_t currentIndex = selectedSampleIndex_;
    auto zoomState = getWaveformZoomState(currentIndex);
    float waveformZoom = zoomState.first;
    float waveformOffset = zoomState.second;
    
    // Get current parameter values from GUI state
    float playheadPos = displaySample->currentPlayheadPosition; // Absolute position
    float startPosRelative = displaySample->currentStartPosition; // Relative position (0.0-1.0 within region)
    float regionStart = displaySample->currentRegionStart;
    float regionEnd = displaySample->currentRegionEnd;
    
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
    
    // Only process region-related input if REGION mode is active
    // In AUTOMATION mode, skip position marker hit detection to avoid conflicts with automation breakpoints
    // Detect which marker is closest to mouse (for dragging)
    // Only check markers that are on-screen
    WaveformMarker hoveredMarker = WaveformMarker::NONE;
    if (waveformOverlayMode_ == WaveformOverlayMode::REGION && (isCanvasHovered || isCanvasActive)) {
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
    
    // Handle mouse interaction (only in REGION mode)
    // CRITICAL: Don't process waveform mouse interactions when dragging a BaseCell
    // This prevents interference between parameter dragging and waveform interactions
    // (isDraggingParameter is already defined above)
    if (waveformOverlayMode_ == WaveformOverlayMode::REGION && (isCanvasHovered || isCanvasActive) && !isDraggingParameter) {
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
                // Click on empty area: update GUI state
                MultiSampler* sampler = getMultiSampler();
                SampleRef* displaySample = (sampler && selectedSampleIndex_ < sampler->getSampleCount()) 
                    ? &sampler->getSampleMutable(selectedSampleIndex_) : nullptr;
                if (displaySample && sampler) {
                    if (sampler->isPlaying()) {
                        // PLAYING mode (sequencer active): Update startPosition for next trigger
                        // Do NOT seek playhead - playback controls the playhead (synced from voice)
                        float regionStartVal = displaySample->currentRegionStart;
                        float regionEndVal = displaySample->currentRegionEnd;
                        float regionSize = regionEndVal - regionStartVal;
                        
                        float relativePos = 0.0f;
                        if (regionSize > 0.001f) {
                            float clampedAbsolute = std::max(regionStartVal, std::min(regionEndVal, relativeX));
                            relativePos = (clampedAbsolute - regionStartVal) / regionSize;
                            relativePos = std::max(0.0f, std::min(1.0f, relativePos));
                        } else {
                            relativePos = std::max(0.0f, std::min(1.0f, relativeX));
                        }
                        
                        displaySample->currentStartPosition = relativePos;
                        setParameterViaCommand("position", relativePos);
                    } else {
                        // IDLE mode: Set playhead position for visual feedback only
                        displaySample->currentPlayheadPosition = relativeX;
                    }
                }
            }
        }
        
        // Continue dragging
        if (draggingMarker != WaveformMarker::NONE && ImGui::IsMouseDragging(0)) {
            MultiSampler* sampler = getMultiSampler();
            SampleRef* displaySample = (sampler && selectedSampleIndex_ < sampler->getSampleCount()) 
                ? &sampler->getSampleMutable(selectedSampleIndex_) : nullptr;
            if (displaySample && sampler) {
                switch (draggingMarker) {
                    case WaveformMarker::REGION_START: {
                        float newStart = relativeX;
                        // Clamp to [0, regionEnd]
                        newStart = std::max(0.0f, std::min(regionEnd, newStart));
                        displaySample->currentRegionStart = newStart;
                        setParameterViaCommand("regionStart", newStart);
                        break;
                    }
                    case WaveformMarker::REGION_END: {
                        float newEnd = relativeX;
                        // Clamp to [regionStart, 1]
                        newEnd = std::max(regionStart, std::min(1.0f, newEnd));
                        displaySample->currentRegionEnd = newEnd;
                        setParameterViaCommand("regionEnd", newEnd);
                        break;
                    }
                    case WaveformMarker::POSITION: {
                        // Update startPosition (position marker) - map absolute to relative within region
                        float regionStartVal = displaySample->currentRegionStart;
                        float regionEndVal = displaySample->currentRegionEnd;
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
                        
                        displaySample->currentStartPosition = relativePos;
                        if (!sampler->isPlaying()) {
                            // Update playheadPosition to show absolute position
                            float absolutePos = (regionSize > 0.001f) ? 
                                (regionStartVal + relativePos * regionSize) : relativePos;
                            displaySample->currentPlayheadPosition = absolutePos;
                        }
                        setParameterViaCommand("position", relativePos);
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
    }
    
    // Handle scrubbing (dragging without marker) - works in ALL modes
    // CRITICAL: Scrubbing behavior depends on MultiSampler playback mode
    // Only allow scrubbing if not dragging automation curves or region markers
    bool isDraggingAutomation = false; // Will be set by automation editor if active
    if (draggingMarker == WaveformMarker::NONE && ImGui::IsMouseDragging(0) && !isDraggingParameter && !isDraggingAutomation && (isCanvasHovered || isCanvasActive)) {
        MultiSampler* sampler = getMultiSampler();
        SampleRef* displaySample = (sampler && selectedSampleIndex_ < sampler->getSampleCount()) 
            ? &sampler->getSampleMutable(selectedSampleIndex_) : nullptr;
        if (!displaySample || !sampler) return;
        
        // Use existing relativeX (already calculated above with zoom/pan)
        
        if (sampler->isPlaying()) {
            // Only set scrubbing flag when actually scrubbing (while playing)
            isScrubbing = true;
            // PLAYING mode: Update position continuously during drag
            // Map absolute position to relative position within region
            float regionStartVal = displaySample->currentRegionStart;
            float regionEndVal = displaySample->currentRegionEnd;
            float regionSize = regionEndVal - regionStartVal;
            
            float relativePos = 0.0f;
            if (regionSize > 0.001f) {
                float clampedAbsolute = std::max(regionStartVal, std::min(regionEndVal, relativeX));
                relativePos = (clampedAbsolute - regionStartVal) / regionSize;
                relativePos = std::max(0.0f, std::min(1.0f, relativePos));
            } else {
                relativePos = std::max(0.0f, std::min(1.0f, relativeX));
            }
            
            // Update GUI state immediately for smooth visual feedback
            displaySample->currentStartPosition = relativePos;
            // Note: currentPlayheadPosition shows current playback (synced from voice), not scrubbed position
            
            // CRITICAL: Scrubbing while playing only updates startPosition (for next trigger)
            // It does NOT affect playheadPosition (current playback position)
            // Directly update ALL active voices for this sample for continuous updates
            size_t currentIndex = selectedSampleIndex_;
            auto activeVoices = sampler->getVoicesForSample(static_cast<int>(currentIndex));
            
            // Update startPosition on all active voices (affects next trigger, not current playback)
            for (auto* voice : activeVoices) {
                if (voice && voice->state == Voice::PLAYING) {
                    voice->startPosition.set(relativePos);
                    // DO NOT call voice->player.setPosition() - that would seek current playback
                }
            }
            
            // Also update via command queue for GUI state consistency
            setParameterViaCommand("position", relativePos);
        }
        // IDLE mode scrubbing removed - no audio feedback when paused
    }
    
    // Stop scrubbing on mouse release (only needed for IDLE mode, which is now removed)
    if (ImGui::IsMouseReleased(0)) {
        if (isScrubbing) {
            isScrubbing = false;
        }
    }
    
    // Draw greyed-out background for trimmed parts (outside region) - ALWAYS VISIBLE
    // This provides visual context for where the region boundaries are, even when markers are hidden
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
        
    // Draw region start/end markers - ONLY VISIBLE in REGION mode
    if (waveformOverlayMode_ == WaveformOverlayMode::REGION) {
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
    }
    
    // Draw position marker (darker grey) - shows where playback will start - ALWAYS VISIBLE in all modes
    if (positionX >= 0.0f) {
        const float markerLineWidth = 1.5f;
        const float markerHandleHeight = 6.0f;
        const float markerLineTopOffset = markerHandleHeight;
        const float positionHandleWidth = 10.0f;
        
        ImU32 positionColor = GUIConstants::toU32(GUIConstants::Waveform::Position);
        drawList->AddLine(
            ImVec2(positionX, canvasPos.y + markerLineTopOffset),
            ImVec2(positionX, canvasMax.y),
            positionColor, markerLineWidth
        );
        // Draw marker handle (small horizontal bar at top, slightly wider)
        drawList->AddRectFilled(
            ImVec2(positionX - positionHandleWidth * 0.5f, canvasPos.y),
            ImVec2(positionX + positionHandleWidth * 0.5f, canvasPos.y + markerHandleHeight),
            positionColor
        );
    }
    
    // Draw playhead (green) - shows current playback position (always visible, regardless of mode)
    // Check if preview player is playing for scrubbing feedback
    bool isPreviewPlaying = displaySample->isScrubbing && displaySample->previewPlayer && displaySample->previewPlayer->isPlaying();
    bool showPlayhead = (playheadPos > 0.0f || isPreviewPlaying);
    if (showPlayhead && playheadX >= 0.0f) {
        ImU32 playheadColor = GUIConstants::toU32(GUIConstants::Waveform::Playhead);
        drawList->AddLine(
            ImVec2(playheadX, canvasPos.y),
            ImVec2(playheadX, canvasMax.y),
            playheadColor, 2.0f
        );
    }
    
    // Draw loop range visualization (when in LOOP play style with loopSize > 0)
    // CRITICAL FIX (Phase 7.9.7.1): Use cached PlayStyle to avoid deadlock
    // Note: sampler is already declared at the start of drawWaveform()
    PlayStyle currentPlayStyle = PlayStyle::ONCE;  // Default fallback
    if (sampler) {
        if (engine_ && engine_->commandsBeingProcessed()) {
            // Commands processing - use cached value if available
            if (hasCachedPlayStyle_) {
                currentPlayStyle = cachedPlayStyle_;
            } else {
                // No cache - use default to prevent deadlock
                currentPlayStyle = PlayStyle::ONCE;
            }
        } else {
            // Safe to call - update cache
            currentPlayStyle = sampler->getPlayStyle();
            cachedPlayStyle_ = currentPlayStyle;
            hasCachedPlayStyle_ = true;
        }
    }
    if (currentPlayStyle == PlayStyle::GRAIN) {
        float grainSizeSeconds = displaySample->currentGrainSize;
        if (grainSizeSeconds > 0.001f) {
            float duration = displaySample->duration;
            if (duration > 0.001f) {
                // Calculate loop start position (absolute) - same logic as in MultiSampler::update()
                float relativeStartPos = displaySample->currentStartPosition;
                float regionSize = regionEnd - regionStart;
                float loopStartAbsolute = 0.0f;
                
                if (regionSize > 0.001f) {
                    loopStartAbsolute = regionStart + relativeStartPos * regionSize;
                } else {
                    loopStartAbsolute = std::max(0.0f, std::min(1.0f, relativeStartPos));
                }
                
                // CRITICAL FIX: Work in absolute time (seconds) first to preserve precision
                // Converting small time values to normalized positions loses precision for long samples
                // Convert normalized positions to absolute time
                float loopStartSeconds = loopStartAbsolute * duration;
                float regionEndSeconds = regionEnd * duration;
                
                // Calculate loop end in absolute time
                float calculatedLoopEndSeconds = loopStartSeconds + grainSizeSeconds;
                
                // Clamp to region end and media duration
                float clampedLoopEndSeconds = std::min(regionEndSeconds, std::min(duration, calculatedLoopEndSeconds));
                
                // Convert back to normalized position (0-1)
                float loopEndAbsolute = clampedLoopEndSeconds / duration;
                
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
    
    // NOTE: ADSR envelope curve is drawn above (in the input processing section)
    // Draw automation curves in AUTOMATION mode (position-based parameter automation)
    if (waveformOverlayMode_ == WaveformOverlayMode::AUTOMATION) {
        // TODO: Implement automation curve editor for volume, speed, etc.
        // This will be position-based (0.0-1.0 maps to sample positions)
        // Different from ADSR which is time-based (milliseconds)
    }
}

/// MARK: - ENVELOPE CURVE SYSTEM
/// ========================================================================
/// SAMPLE-ACCURATE ENVELOPE CURVE DRAWING
/// ========================================================================
/// This system provides pixel-perfect envelope curve visualization that aligns
/// precisely with the waveform display. All positions use normalized sample
/// coordinates (0.0-1.0) and the same coordinate mapping as the waveform.
///
/// KEY DESIGN PRINCIPLES:
/// 1. Envelope always starts at the trigger position (startPosition within region)
/// 2. Attack/Decay times are mapped to actual sample duration
/// 3. Sustain extends to regionEnd (maximum possible sustain)
/// 4. Release preview shows what happens when note-off occurs
/// 5. All drawing uses the same mapToScreenX function as the waveform

/// @brief Calculate envelope points from ADSR parameters
/// Creates a vector of sample-position/level pairs representing the envelope curve
std::vector<MultiSamplerGUI::EnvelopePoint> MultiSamplerGUI::calculateEnvelopePoints(
    const EnvelopeCurveParams& params
) const {
    std::vector<EnvelopePoint> points;
    
    // Validate inputs
    if (params.sampleDurationSeconds <= 0.0f) {
        return points;
    }
    
    float regionStart = params.regionStart;
    float regionEnd = params.regionEnd;
    if (regionEnd <= regionStart) {
        return points;
    }
    
    float regionSize = regionEnd - regionStart;
    float regionDurationMs = regionSize * params.sampleDurationSeconds * 1000.0f;
    
    // Calculate trigger position (absolute sample position where envelope starts)
    // startPosition is relative (0.0-1.0 within region), convert to absolute
    float triggerPosAbsolute = regionStart + params.startPosition * regionSize;
    triggerPosAbsolute = std::max(regionStart, std::min(regionEnd, triggerPosAbsolute));
    
    // Time available for envelope after trigger position (in ms)
    float remainingRegionMs = (regionEnd - triggerPosAbsolute) * params.sampleDurationSeconds * 1000.0f;
    
    // Convert ADSR times to sample positions (starting from trigger position)
    // All positions are clamped to remain within the region
    
    // Calculate cumulative times and positions
    float attackEndMs = std::min(params.attackMs, remainingRegionMs);
    float decayStartMs = attackEndMs;
    float decayEndMs = std::min(decayStartMs + params.decayMs, remainingRegionMs);
    
    // Convert ms to normalized positions relative to full sample
    // NOTE: Envelope times are in real-time milliseconds and should always be drawn
    // the same way relative to the waveform, regardless of playback speed.
    // Speed only affects how fast the playhead moves through the envelope, not how it's drawn.
    float msToNormalized = 1.0f / (params.sampleDurationSeconds * 1000.0f);
    
    float attackEndPos = triggerPosAbsolute + attackEndMs * msToNormalized;
    float decayEndPos = triggerPosAbsolute + decayEndMs * msToNormalized;
    
    // Calculate release duration in normalized time
    float releaseMs = params.releaseMs;
    float releaseDurationNormalized = releaseMs * msToNormalized;
    
    // Auto-release behavior: If release would extend beyond sample end (1.0),
    // start release earlier so it can complete within the available sample.
    // This prevents clicks when regionEnd = 1.0 (modern sampler practice).
    float sustainEndPos = regionEnd;
    if (params.showReleasePreview && params.releasePreviewPos >= 0.0f) {
        sustainEndPos = std::min(regionEnd, params.releasePreviewPos);
    } else {
        // Check if release would extend beyond sample end
        float potentialReleaseEnd = regionEnd + releaseDurationNormalized;
        if (potentialReleaseEnd > 1.0f && releaseMs > 0.001f) {
            // Auto-release: start release phase before sample ends
            sustainEndPos = 1.0f - releaseDurationNormalized;
            sustainEndPos = std::max(decayEndPos, sustainEndPos); // Don't go before decay ends
        }
    }
    
    // Release phase (preview - shows what happens after note-off or auto-release)
    float releaseStartPos = sustainEndPos;
    float releaseEndPos = releaseStartPos + releaseDurationNormalized;
    releaseEndPos = std::min(1.0f, releaseEndPos); // Clamp to sample end
    
    // Clamp all positions
    attackEndPos = std::max(regionStart, std::min(regionEnd, attackEndPos));
    decayEndPos = std::max(regionStart, std::min(regionEnd, decayEndPos));
    sustainEndPos = std::max(regionStart, std::min(regionEnd, sustainEndPos));
    
    // Generate envelope points
    const int POINTS_PER_SEGMENT = 20; // Smooth curves
    
    // Point 0: Trigger start (level 0.0)
    points.push_back({triggerPosAbsolute, 0.0f});
    
    // Attack phase: 0.0 → 1.0
    if (attackEndPos > triggerPosAbsolute && params.attackMs > 0.001f) {
        for (int i = 1; i <= POINTS_PER_SEGMENT; i++) {
            float t = (float)i / (float)POINTS_PER_SEGMENT;
            float pos = triggerPosAbsolute + t * (attackEndPos - triggerPosAbsolute);
            float level = t; // Linear attack (could add curve types later)
            points.push_back({pos, level});
        }
    } else {
        // Instant attack (0ms)
        points.push_back({triggerPosAbsolute, 1.0f});
    }
    
    // Decay phase: 1.0 → sustain
    if (decayEndPos > attackEndPos && params.decayMs > 0.001f) {
        for (int i = 1; i <= POINTS_PER_SEGMENT; i++) {
            float t = (float)i / (float)POINTS_PER_SEGMENT;
            float pos = attackEndPos + t * (decayEndPos - attackEndPos);
            float level = 1.0f + t * (params.sustain - 1.0f); // Linear decay
            points.push_back({pos, level});
        }
    } else if (decayEndPos <= attackEndPos) {
        // Instant decay to sustain level
        points.push_back({attackEndPos, params.sustain});
    }
    
    // Sustain phase: horizontal line at sustain level
    if (sustainEndPos > decayEndPos) {
        // Start of sustain (might be same as decay end)
        if (points.empty() || std::abs(points.back().samplePos - decayEndPos) > 0.0001f) {
            points.push_back({decayEndPos, params.sustain});
        }
        // End of sustain
        points.push_back({sustainEndPos, params.sustain});
    }
    
    // Release phase (preview): sustain → 0.0
    if (params.showReleasePreview && releaseEndPos > releaseStartPos && params.releaseMs > 0.001f) {
        for (int i = 1; i <= POINTS_PER_SEGMENT; i++) {
            float t = (float)i / (float)POINTS_PER_SEGMENT;
            float pos = releaseStartPos + t * (releaseEndPos - releaseStartPos);
            float level = params.sustain * (1.0f - t); // Linear release
            points.push_back({pos, level});
        }
    }
    
    return points;
}

/// @brief Draw envelope curve using pre-calculated points
void MultiSamplerGUI::drawEnvelopeCurve(
    const std::vector<EnvelopePoint>& points,
    const ImVec2& canvasPos,
    const ImVec2& canvasSize,
    std::function<float(float)> mapToScreenX,
    ImU32 curveColor,
    ImU32 fillColor
) {
    if (points.size() < 2) return;
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Convert envelope points to screen coordinates
    std::vector<ImVec2> screenPoints;
    screenPoints.reserve(points.size());
    
    float canvasBottom = canvasPos.y + canvasSize.y;
    float canvasTop = canvasPos.y;
    float canvasLeft = canvasPos.x;
    float canvasRight = canvasPos.x + canvasSize.x;
    
    for (const auto& pt : points) {
        float x = mapToScreenX(pt.samplePos);
        
        // Skip off-screen points (but keep edge points for proper clipping)
        if (x < canvasLeft - 50.0f || x > canvasRight + 50.0f) {
            continue;
        }
        
        // Map level (0.0-1.0) to Y coordinate (bottom to top)
        float y = canvasBottom - pt.level * canvasSize.y;
        y = std::max(canvasTop, std::min(canvasBottom, y));
        
        screenPoints.push_back(ImVec2(x, y));
    }
    
    if (screenPoints.size() < 2) return;
    
    // Draw filled area under curve (if fill color has alpha)
    if ((fillColor & 0xFF000000) != 0) {
        std::vector<ImVec2> fillPoints = screenPoints;
        // Close the polygon at the bottom
        fillPoints.push_back(ImVec2(screenPoints.back().x, canvasBottom));
        fillPoints.push_back(ImVec2(screenPoints.front().x, canvasBottom));
        
        // Use non-convex polygon fill for complex shapes
        if (fillPoints.size() >= 3) {
            drawList->AddConvexPolyFilled(fillPoints.data(), static_cast<int>(fillPoints.size()), fillColor);
        }
    }
    
    // Draw curve line
    drawList->AddPolyline(screenPoints.data(), static_cast<int>(screenPoints.size()), 
                         curveColor, ImDrawFlags_None, 2.0f);
}

/// MARK: - ENVELOPE EDITOR
/// @brief Reusable envelope curve editor (for ADSR and future modulation envelopes)
/// Uses the modular EnvelopeCurve system for sample-accurate visualization
bool MultiSamplerGUI::drawEnvelopeCurveEditor(
    const ImVec2& canvasPos,
    const ImVec2& canvasSize,
    const EnvelopeCurveParams& curveParams,
    std::function<float(float)> mapToScreenX,
    float visibleStart,
    float visibleRange,
    std::function<void(const std::string& param, float value)> onParameterChanged,
    EnvelopeEditorState& editorState,
    bool drawOnly
) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    
    bool isHovered = false;
    if (!drawOnly) {
    // Create invisible button for interaction (overlaid on waveform)
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("##ADSR_Editor", canvasSize);
        isHovered = ImGui::IsItemHovered() || editorState.isDragging;
    }
    
    // Extract parameters for readability
    float attackMs = curveParams.attackMs;
    float decayMs = curveParams.decayMs;
    float sustain = curveParams.sustain;
    float releaseMs = curveParams.releaseMs;
    float sampleDurationSeconds = curveParams.sampleDurationSeconds;
    float regionStart = curveParams.regionStart;
    float regionEnd = curveParams.regionEnd;
    float startPosition = curveParams.startPosition;
    
    // Validate inputs
    if (sampleDurationSeconds <= 0.0f || regionEnd <= regionStart) {
        return false;
    }
    
    float regionSize = regionEnd - regionStart;
    
    // Calculate trigger position (where envelope starts)
    // CRITICAL: This must match exactly what calculateEnvelopePoints uses
    float triggerPosAbsolute = regionStart + startPosition * regionSize;
    triggerPosAbsolute = std::max(regionStart, std::min(regionEnd, triggerPosAbsolute));
    
    // ========================================================================
    // UNIFIED POSITION CALCULATION
    // ========================================================================
    // These calculations MUST match calculateEnvelopePoints exactly for alignment
    // Convert ms to normalized positions relative to full sample
    // NOTE: Envelope times are in real-time milliseconds and should always be drawn
    // the same way relative to the waveform, regardless of playback speed.
    // Speed only affects how fast the playhead moves through the envelope, not how it's drawn.
    float msToNormalized = 1.0f / (sampleDurationSeconds * 1000.0f);
    
    // Time available for envelope after trigger position (in ms)
    float remainingRegionMs = (regionEnd - triggerPosAbsolute) * sampleDurationSeconds * 1000.0f;
    
    // Calculate cumulative times (clamped to available time)
    float attackEndMs = std::min(attackMs, remainingRegionMs);
    float decayStartMs = attackEndMs;
    float decayEndMs = std::min(decayStartMs + decayMs, remainingRegionMs);
    
    // Convert to absolute sample positions
    float attackEndPos = triggerPosAbsolute + attackEndMs * msToNormalized;
    float decayEndPos = triggerPosAbsolute + decayEndMs * msToNormalized;
    
    // Calculate release duration in normalized time
    float releaseDurationNormalized = releaseMs * msToNormalized;
    
    // Auto-release behavior: If release would extend beyond sample end (1.0),
    // start release earlier so it can complete within the available sample.
    // This matches the logic in calculateEnvelopePoints for consistent visualization.
    float sustainEndPos = regionEnd;
    float potentialReleaseEnd = regionEnd + releaseDurationNormalized;
    if (potentialReleaseEnd > 1.0f && releaseMs > 0.001f) {
        // Auto-release: start release phase before sample ends
        sustainEndPos = 1.0f - releaseDurationNormalized;
        sustainEndPos = std::max(decayEndPos, sustainEndPos); // Don't go before decay ends
    }
    
    float releaseEndPos = sustainEndPos + releaseDurationNormalized;
    
    // Clamp all positions to region bounds
    attackEndPos = std::max(regionStart, std::min(regionEnd, attackEndPos));
    decayEndPos = std::max(regionStart, std::min(regionEnd, decayEndPos));
    sustainEndPos = std::max(regionStart, std::min(1.0f, sustainEndPos)); // Can be before regionEnd due to auto-release
    // Release end is clamped to sample end (1.0)
    releaseEndPos = std::min(1.0f, releaseEndPos);
    
    // Calculate envelope points using the modular system
    std::vector<EnvelopePoint> envelopePoints = calculateEnvelopePoints(curveParams);
    
    // Draw the envelope curve
    ImU32 curveColor = GUIConstants::toIM_COL32(ImVec4(0.4f, 0.6f, 0.9f, 0.9f));
    ImU32 fillColor = GUIConstants::toIM_COL32(ImVec4(0.4f, 0.6f, 0.9f, 0.15f));
    drawEnvelopeCurve(envelopePoints, canvasPos, canvasSize, mapToScreenX, curveColor, fillColor);
    
    float canvasBottom = canvasPos.y + canvasSize.y;
    float canvasTop = canvasPos.y;
    
    // Draw subtle horizontal line at sustain level
    float sustainY = canvasPos.y + canvasSize.y * (1.0f - sustain);
    ImU32 gridColor = GUIConstants::toIM_COL32(ImVec4(0.5f, 0.5f, 0.5f, 0.3f));
        drawList->AddLine(
            ImVec2(canvasPos.x, sustainY),
            ImVec2(canvasPos.x + canvasSize.x, sustainY),
            gridColor, 1.0f
        );
    
    // Draw trigger point indicator (where envelope starts - aligns with position marker)
    float triggerX = mapToScreenX(triggerPosAbsolute);
    if (triggerX >= canvasPos.x - 10.0f && triggerX <= canvasPos.x + canvasSize.x + 10.0f) {
        // Dashed vertical line at trigger point (envelope start)
        ImU32 triggerColor = GUIConstants::toIM_COL32(ImVec4(0.4f, 0.6f, 0.9f, 0.5f));
        for (float y = canvasTop; y < canvasBottom; y += 8.0f) {
        drawList->AddLine(
                ImVec2(triggerX, y),
                ImVec2(triggerX, std::min(y + 4.0f, canvasBottom)),
                triggerColor, 1.0f
            );
        }
        
        // Small triangle marker at bottom showing trigger point
        drawList->AddTriangleFilled(
            ImVec2(triggerX, canvasBottom - 6.0f),
            ImVec2(triggerX - 4.0f, canvasBottom),
            ImVec2(triggerX + 4.0f, canvasBottom),
            curveColor
        );
    }
    
    // ========================================================================
    // BREAKPOINT HANDLES (for dragging ADSR parameters)
    // ========================================================================
    const float BREAKPOINT_SIZE = 6.0f;
    const float BREAKPOINT_HIT_THRESHOLD = 18.0f;
    
    // canvasBottom/canvasTop already declared above
    
    // Map breakpoint positions to screen coordinates
    // (triggerX already calculated above for the trigger indicator)
    float attackX = mapToScreenX(attackEndPos);
    float decayX = mapToScreenX(decayEndPos);
    float sustainEndX = mapToScreenX(sustainEndPos);
    float releaseEndX = mapToScreenX(releaseEndPos);
    
    // Breakpoint Y positions
    float peakY = canvasTop; // Level 1.0
    float releaseEndY = canvasBottom; // Level 0.0
    
    struct Breakpoint {
        ImVec2 pos;
        int id; // 0=Attack, 1=Decay, 2=Sustain, 3=Release
        const char* label;
        bool visible;
    };
    
    std::vector<Breakpoint> breakpoints = {
        {ImVec2(attackX, peakY), 0, "A", attackX >= canvasPos.x - 20.0f && attackX <= canvasPos.x + canvasSize.x + 20.0f},
        {ImVec2(decayX, sustainY), 1, "D", decayX >= canvasPos.x - 20.0f && decayX <= canvasPos.x + canvasSize.x + 20.0f},
        {ImVec2(sustainEndX, sustainY), 2, "S", sustainEndX >= canvasPos.x - 20.0f && sustainEndX <= canvasPos.x + canvasSize.x + 20.0f},
        {ImVec2(releaseEndX, releaseEndY), 3, "R", releaseEndX >= canvasPos.x - 20.0f && releaseEndX <= canvasPos.x + canvasSize.x + 20.0f}
    };
    
    // Input handling (breakpoint dragging) - skip in draw-only mode
    int hoveredBreakpoint = -1; // Declare outside if block for use in drawing
    if (!drawOnly) {
        // Calculate drag scaling (ms per pixel)
        float screenToSampleScale = visibleRange / canvasSize.x;
        float sampleToMsScale = sampleDurationSeconds * 1000.0f;
        float timeScale = screenToSampleScale * sampleToMsScale;
        float levelScale = 1.0f / canvasSize.y;
        
        // Check which breakpoint is hovered
    ImVec2 mousePos = io.MousePos;
    
        for (const auto& bp : breakpoints) {
            if (!bp.visible) continue;
        float dist = std::sqrt(
                std::pow(mousePos.x - bp.pos.x, 2) + 
                std::pow(mousePos.y - bp.pos.y, 2)
        );
        if (dist < BREAKPOINT_HIT_THRESHOLD) {
                hoveredBreakpoint = bp.id;
            break;
        }
    }
    
        // Handle dragging - all breakpoints support bidirectional (X and Y) movement
    if (ImGui::IsMouseClicked(0) && hoveredBreakpoint >= 0) {
        editorState.isDragging = true;
        editorState.draggedBreakpoint = hoveredBreakpoint;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    
        // Pixel-perfect dragging: convert mouse position directly to parameter values
        // This ensures the breakpoint follows the mouse cursor exactly, pixel by pixel
    if (editorState.isDragging && ImGui::IsMouseDragging(0)) {
            // Convert mouse position directly to normalized sample position and envelope level
            float mouseX = io.MousePos.x;
            float mouseY = io.MousePos.y;
            
            // Clamp mouse position to canvas bounds
            mouseX = std::max(canvasPos.x, std::min(canvasPos.x + canvasSize.x, mouseX));
            mouseY = std::max(canvasPos.y, std::min(canvasPos.y + canvasSize.y, mouseY));
            
            // Convert screen X to normalized sample position (0.0-1.0)
            float normalizedX = visibleStart + ((mouseX - canvasPos.x) / canvasSize.x) * visibleRange;
            normalizedX = std::max(0.0f, std::min(1.0f, normalizedX));
            
            // Convert screen Y to envelope level (0.0-1.0, inverted: top = 1.0, bottom = 0.0)
            float normalizedY = 1.0f - ((mouseY - canvasPos.y) / canvasSize.y);
            normalizedY = std::max(0.0f, std::min(1.0f, normalizedY));
            
            // Convert normalized positions to parameter values based on breakpoint type
        switch (editorState.draggedBreakpoint) {
                case 0: { // Attack - X = attack time, Y = peak level (future: could add peak level parameter)
                    // Convert normalized X position to attack time (relative to trigger position)
                    float attackEndPosAbsolute = normalizedX;
                    float attackDurationNormalized = attackEndPosAbsolute - triggerPosAbsolute;
                    float newAttack = attackDurationNormalized * sampleDurationSeconds * 1000.0f;
                    newAttack = std::max(0.0f, newAttack); // Only clamp to minimum (no upper limit)
                onParameterChanged("attackMs", newAttack);
                    // Y-axis: Currently attack always goes to 1.0, but could add peak level parameter in future
                    // For now, Y movement is ignored for Attack breakpoint
                break;
            }
                case 1: { // Decay - X = decay time, Y = sustain level
                    // Convert normalized X position to decay time (relative to attack end)
                    // First, calculate current attack end position
                    float currentAttackEndPos = triggerPosAbsolute + (attackMs * msToNormalized);
                    float decayEndPosAbsolute = normalizedX;
                    float decayDurationNormalized = decayEndPosAbsolute - currentAttackEndPos;
                    float newDecay = decayDurationNormalized * sampleDurationSeconds * 1000.0f;
                    newDecay = std::max(0.0f, newDecay); // Only clamp to minimum (no upper limit)
                onParameterChanged("decayMs", newDecay);
                    
                    // Y-axis: Direct level mapping
                    onParameterChanged("sustain", normalizedY);
                break;
            }
                case 2: { // Sustain - X = release time, Y = sustain level
                    // X-axis: Convert normalized position to release time
                    // The Sustain breakpoint represents where release starts (sustain end position).
                    // With auto-release, this can be before regionEnd.
                    // Calculate releaseMs such that release completes at sample end (1.0)
                    float sustainEndPosAbsolute = normalizedX;
                    float releaseEndPosAbsolute = 1.0f; // Release always ends at sample end with auto-release
                    float releaseDurationNormalized = releaseEndPosAbsolute - sustainEndPosAbsolute;
                    float newRelease = releaseDurationNormalized * sampleDurationSeconds * 1000.0f;
                    newRelease = std::max(5.0f, std::min(5000.0f, newRelease));
                    onParameterChanged("releaseMs", newRelease);
                    
                    // Y-axis: Direct level mapping
                    onParameterChanged("sustain", normalizedY);
                break;
            }
                case 3: { // Release - X = release time, Y = end level (future: could add end level parameter)
                    // X-axis: Convert normalized position to release time
                    // The Release breakpoint represents where release ends.
                    // Calculate releaseMs such that release ends at the dragged position (clamped to 1.0).
                    // With auto-release, the actual release end is clamped to sample end (1.0).
                    float releaseEndPosAbsolute = normalizedX;
                    releaseEndPosAbsolute = std::min(1.0f, releaseEndPosAbsolute); // Clamp to sample end
                    
                    // Calculate releaseMs based on distance from regionEnd to release end
                    // This sets the release duration parameter, which auto-release will use
                    float releaseDurationNormalized = releaseEndPosAbsolute - regionEnd;
                    float newRelease = releaseDurationNormalized * sampleDurationSeconds * 1000.0f;
                newRelease = std::max(5.0f, std::min(5000.0f, newRelease));
                onParameterChanged("releaseMs", newRelease);
                    // Y-axis: Currently release always goes to 0.0, but could add end level parameter in future
                    // For now, Y movement is ignored for Release breakpoint
                break;
            }
        }
    }
    
    if (ImGui::IsMouseReleased(0)) {
        editorState.isDragging = false;
        editorState.draggedBreakpoint = -1;
    }
    }
    
    // Draw breakpoints (hoveredBreakpoint already calculated above in input handling)
    for (const auto& bp : breakpoints) {
        if (!bp.visible) continue;
        
        bool bpHovered = (!drawOnly && hoveredBreakpoint == bp.id);
        bool bpDragging = (!drawOnly && editorState.draggedBreakpoint == bp.id);
        
        ImU32 color = bpDragging ? 
            GUIConstants::toIM_COL32(ImVec4(0.6f, 0.8f, 1.0f, 1.0f)) :
            (bpHovered ? 
                GUIConstants::toIM_COL32(ImVec4(0.5f, 0.7f, 0.95f, 1.0f)) :
                GUIConstants::toIM_COL32(ImVec4(0.4f, 0.6f, 0.9f, 1.0f)));
        
        drawList->AddCircleFilled(bp.pos, BREAKPOINT_SIZE, color, 16);
        drawList->AddCircle(bp.pos, BREAKPOINT_SIZE, GUIConstants::toIM_COL32(ImVec4(0, 0, 0, 1)), 16, 1.5f);
        
        ImVec2 labelPos = ImVec2(bp.pos.x + BREAKPOINT_SIZE + 2, bp.pos.y - 6);
        drawList->AddText(labelPos, GUIConstants::toIM_COL32(ImVec4(1, 1, 1, 1)), bp.label);
    }
    
    // Update cursor (only in input mode)
    if (!drawOnly && (hoveredBreakpoint >= 0 || editorState.isDragging)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    
    return editorState.isDragging;
}

/// MARK: - WF zoom
/// @brief get the zoom state for a given index
/// @param index 
/// @return std::pair<float, float>
std::pair<float, float> MultiSamplerGUI::getWaveformZoomState(size_t index) const {
    auto it = waveformZoomState.find(index);
    if (it != waveformZoomState.end()) {
        return it->second;  // Return stored {zoom, offset}
    }
    // Default values: no zoom (1.0), no offset (0.0)
    return std::make_pair(1.0f, 0.0f);
}

/// @brief set the zoom state for a given index
void MultiSamplerGUI::setWaveformZoomState(size_t index, float zoom, float offset) {
    waveformZoomState[index] = std::make_pair(zoom, offset);
}


/// MARK: - KEY PRESS
bool MultiSamplerGUI::handleKeyPress(int key, bool ctrlPressed, bool shiftPressed) {
    // PHASE 1: SINGLE INPUT PATH - BaseCell is sole input processor for cells
    // If any cell has focus, let BaseCell handle ALL input
    if (cellFocusState.hasFocus()) {
        return false; // BaseCell will handle in processInputInDraw()
    }
    
    // Only handle global shortcuts when no cell is focused
    // Currently MediaPoolGUI doesn't have global shortcuts like play/pause
    // (those are handled by TrackerSequencerGUI)
    // So for now, just let all keys pass through to ImGui when no cell is focused
    return false;
}

bool MultiSamplerGUI::handleFileDrop(const std::vector<std::string>& filePaths) {
    MultiSampler* sampler = getMultiSampler();
    if (!sampler || filePaths.empty()) {
        return false;
    }
    
    // Add files to MultiSampler
    sampler->addMediaFiles(filePaths);
    return true;
}

// Note: setupDragDropTarget() is inherited from ModuleGUI base class
// It handles FILE_PATHS payload (unified for all sources: FileBrowser, AssetLibrary, OS)
// and calls handleFileDrop() which adds files to MultiSampler

/// MARK: - AUTOMATION CURVE SYSTEM
/// ========================================================================
/// POSITION-BASED AUTOMATION CURVES
/// ========================================================================
/// This system provides position-based automation curves that map directly to sample positions.
/// Unlike ADSR (time-based, milliseconds), automation is position-based (0.0-1.0 maps to sample positions).

/// @brief Draw automation curve using pre-calculated points
void MultiSamplerGUI::drawAutomationCurve(
    const std::vector<AutomationPoint>& points,
    const ImVec2& canvasPos,
    const ImVec2& canvasSize,
    std::function<float(float)> mapToScreenX,
    float minValue,
    float maxValue,
    ImU32 curveColor,
    ImU32 fillColor
) {
    if (points.size() < 2) return;
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Convert automation points to screen coordinates
    std::vector<ImVec2> screenPoints;
    screenPoints.reserve(points.size());
    
    float canvasBottom = canvasPos.y + canvasSize.y;
    float canvasTop = canvasPos.y;
    float canvasLeft = canvasPos.x;
    float canvasRight = canvasPos.x + canvasSize.x;
    float valueRange = maxValue - minValue;
    
    for (const auto& pt : points) {
        float x = mapToScreenX(pt.position);
        
        // Skip off-screen points (but keep edge points for proper clipping)
        if (x < canvasLeft - 50.0f || x > canvasRight + 50.0f) {
            continue;
        }
        
        // Map value (minValue-maxValue) to Y coordinate (bottom to top)
        // Normalize value to 0.0-1.0 range
        float normalizedValue = valueRange > 0.001f ? (pt.value - minValue) / valueRange : 0.5f;
        normalizedValue = std::max(0.0f, std::min(1.0f, normalizedValue));
        
        float y = canvasBottom - normalizedValue * canvasSize.y;
        y = std::max(canvasTop, std::min(canvasBottom, y));
        
        screenPoints.push_back(ImVec2(x, y));
    }
    
    if (screenPoints.size() < 2) return;
    
    // Draw filled area under curve (if fill color has alpha)
    if ((fillColor & 0xFF000000) != 0) {
        std::vector<ImVec2> fillPoints = screenPoints;
        // Close the polygon at the bottom
        fillPoints.push_back(ImVec2(screenPoints.back().x, canvasBottom));
        fillPoints.push_back(ImVec2(screenPoints.front().x, canvasBottom));
        
        // Use non-convex polygon fill for complex shapes
        if (fillPoints.size() >= 3) {
            drawList->AddConvexPolyFilled(fillPoints.data(), static_cast<int>(fillPoints.size()), fillColor);
        }
    }
    
    // Draw curve line
    drawList->AddPolyline(screenPoints.data(), static_cast<int>(screenPoints.size()), 
                         curveColor, ImDrawFlags_None, 2.0f);
}

/// @brief Automation curve editor (position-based parameter automation)
bool MultiSamplerGUI::drawAutomationCurveEditor(
    AutomationParameter param,
    const ImVec2& canvasPos,
    const ImVec2& canvasSize,
    std::function<float(float)> mapToScreenX,
    float visibleStart,
    float visibleRange,
    std::function<void(float position, float value)> onPointChanged,
    AutomationEditorState& editorState,
    bool drawOnly
) {
    // TODO: Implement automation curve editor
    // For now, this is a placeholder that will be implemented in the next phase
    // The structure is in place - we need to:
    // 1. Store automation points per sample (in SampleRef or separate structure)
    // 2. Allow adding/removing/dragging points
    // 3. Interpolate between points for smooth curves
    // 4. Apply automation during playback
    
    return false;
}

//--------------------------------------------------------------
// GUI Factory Registration
//--------------------------------------------------------------
// Auto-register MultiSamplerGUI with GUIManager on static initialization
// This enables true modularity - no hardcoded dependencies in GUIManager
namespace {
    struct MultiSamplerGUIRegistrar {
        MultiSamplerGUIRegistrar() {
            // Register with new name
            GUIManager::registerGUIType("MultiSampler", 
                []() -> std::unique_ptr<ModuleGUI> {
                    return std::make_unique<MultiSamplerGUI>();
                });
            // Also register with legacy name for backward compatibility
            GUIManager::registerGUIType("MediaPool", 
                []() -> std::unique_ptr<ModuleGUI> {
                    return std::make_unique<MultiSamplerGUI>();
                });
        }
    };
    static MultiSamplerGUIRegistrar g_multiSamplerGUIRegistrar;
}


