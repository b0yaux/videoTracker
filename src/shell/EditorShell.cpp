#include "EditorShell.h"
#include "core/Engine.h"
#include "gui/ViewManager.h"
#include "gui/GUIManager.h"
#include "gui/ModuleGUI.h"
#include "modules/Module.h"
#include "ofLog.h"
#include "ofJson.h"
#include <imgui.h>

namespace vt {
namespace shell {

EditorShell::EditorShell(Engine* engine)
    : Shell(engine)
{
}

EditorShell::~EditorShell() {
}

void EditorShell::setup() {
    // Call parent setup() first to subscribe to state changes
    Shell::setup();
    
    if (observerId_ > 0) {
        ofLogNotice("EditorShell") << "Subscribed to state changes (ID: " << observerId_ << ")";
    }
    
    // EditorShell is a thin wrapper around ofApp's existing GUI
    // Setup is handled by ofApp, this shell just provides the interface
    ofLogNotice("EditorShell") << "Editor shell setup complete";
}

void EditorShell::update(float deltaTime) {
    if (!active_) return;
    // Updates are handled by ofApp
}

void EditorShell::draw() {
    if (!active_) return;
    
    
    
    // Call ofApp's drawGUI callback
    if (drawGUICallback_) {
        
        
        drawGUICallback_();
        
        
    }
}

void EditorShell::exit() {
    if (observerId_ > 0) {
        ofLogNotice("EditorShell") << "Unsubscribing from state changes (ID: " << observerId_ << ")";
    }
    // Call parent exit() last to unsubscribe from state changes
    Shell::exit();
}

void EditorShell::onStateChanged(const EngineState& state, uint64_t stateVersion) {
    // Check if state version is newer than last seen (prevent stale state processing)
    if (stateVersion < lastStateVersion_) {
        ofLogVerbose("EditorShell") << "Ignoring stale state update (version: " << stateVersion 
                                     << ", last: " << lastStateVersion_ << ")";
        return;
    }
    
    // Call base class implementation to update lastStateVersion_
    Shell::onStateChanged(state, stateVersion);
    
    // Cache state snapshot for thread-safe access in draw() method
    cachedState_ = state;
    
    // Log state changes for debugging
    ofLogNotice("EditorShell") << "State changed (version: " << stateVersion << ")";
    
    // Note: UI updates should be deferred to draw() method
    // This callback just caches the state snapshot
}

bool EditorShell::handleKeyPress(int key) {
    if (!active_) return false;
    
    // F3 toggles editor (handled by ofApp shell switching)
    if (key == OF_KEY_F3) {
        return false;  // Let ofApp handle shell switching
    }
    
    // Delegate to ofApp's key handler (which calls InputRouter)
    // InputRouter already checks WantCaptureKeyboard internally for appropriate keys
    // We don't need to block keys here - let InputRouter make the decision
    if (handleKeyPressCallback_) {
        bool handled = handleKeyPressCallback_(key);
        // Return true if handled, false otherwise
        // This prevents ofApp from calling InputRouter again
        return handled;
    }
    
    return false;
}

bool EditorShell::handleMousePress(int x, int y, int button) {
    if (!active_) return false;
    // Mouse handling is typically done by ImGui
    return false;
}

bool EditorShell::handleWindowResize(int w, int h) {
    if (!active_) return false;
    // Window resize handling
    return false;
}

ofJson EditorShell::serializeUIState() const {
    ofJson json;
    json["gui"] = ofJson::object();
    
    // View state (panel visibility, current panel, etc.)
    if (viewManager_) {
        json["gui"]["viewState"] = ofJson::object();
        json["gui"]["viewState"]["fileBrowserVisible"] = viewManager_->isFileBrowserVisible();
        json["gui"]["viewState"]["consoleVisible"] = viewManager_->isConsoleVisible();
        json["gui"]["viewState"]["assetLibraryVisible"] = viewManager_->isAssetLibraryVisible();
        json["gui"]["viewState"]["currentFocusedWindow"] = viewManager_->getCurrentFocusedWindow();
        json["gui"]["viewState"]["masterModulesVisible"] = viewManager_->isMasterModulesVisible();
    }
    
    // Module instance visibility state
    if (guiManager_) {
        json["gui"]["visibleInstances"] = ofJson::object();
        json["gui"]["visibleInstances"]["mediaPool"] = ofJson::array();
        json["gui"]["visibleInstances"]["tracker"] = ofJson::array();
        json["gui"]["visibleInstances"]["audioOutput"] = ofJson::array();
        json["gui"]["visibleInstances"]["videoOutput"] = ofJson::array();
        json["gui"]["visibleInstances"]["audioMixer"] = ofJson::array();
        json["gui"]["visibleInstances"]["videoMixer"] = ofJson::array();
        
        auto visibleInstruments = guiManager_->getVisibleInstances(ModuleType::INSTRUMENT);
        for (const auto& name : visibleInstruments) {
            json["gui"]["visibleInstances"]["mediaPool"].push_back(name);
        }
        
        auto visibleTracker = guiManager_->getVisibleInstances(ModuleType::SEQUENCER);
        for (const auto& name : visibleTracker) {
            json["gui"]["visibleInstances"]["tracker"].push_back(name);
        }
        
        auto visibleUtility = guiManager_->getVisibleInstances(ModuleType::UTILITY);
        // Separate utility types by checking module type from state snapshot (Shell-safe API)
        if (engine_) {
            // Use state snapshot instead of direct ModuleRegistry access
            EngineState state = engine_->getState();
            for (const auto& name : visibleUtility) {
                // Check if module exists in state snapshot
                auto it = state.modules.find(name);
                if (it != state.modules.end()) {
                    std::string typeName = it->second.type;
                    if (typeName == "AudioOutput") {
                        json["gui"]["visibleInstances"]["audioOutput"].push_back(name);
                    } else if (typeName == "VideoOutput") {
                        json["gui"]["visibleInstances"]["videoOutput"].push_back(name);
                    } else if (typeName == "AudioMixer") {
                        json["gui"]["visibleInstances"]["audioMixer"].push_back(name);
                    } else if (typeName == "VideoMixer") {
                        json["gui"]["visibleInstances"]["videoMixer"].push_back(name);
                    }
                }
            }
        }
    }
    
    // Module layouts (from ModuleGUI)
    json["gui"]["moduleLayouts"] = ofJson::object();
    std::map<std::string, ImVec2> layouts = ModuleGUI::getAllDefaultLayouts();
    for (const auto& [typeName, size] : layouts) {
        json["gui"]["moduleLayouts"][typeName] = ofJson::object();
        json["gui"]["moduleLayouts"][typeName]["width"] = size.x;
        json["gui"]["moduleLayouts"][typeName]["height"] = size.y;
    }
    
    // ImGui window state (docking, positions, sizes)
    // Save unconditionally - ImGui will include docking state if it exists
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (ctx) {
        size_t iniSize = 0;
        const char* iniData = ImGui::SaveIniSettingsToMemory(&iniSize);
        if (iniData && iniSize > 0) {
            json["gui"]["imguiState"] = std::string(iniData, iniSize);
            ofLogNotice("EditorShell") << "✓ Saved ImGui window state (" << iniSize << " bytes)";
        }
    }
    
    return json;
}

bool EditorShell::loadUIState(const ofJson& json) {
    if (!json.is_object() || !json.contains("gui") || !json["gui"].is_object()) {
        ofLogWarning("EditorShell") << "Invalid UI state format";
        return false;
    }
    
    auto guiJson = json["gui"];
    
    // Load view state
    if (viewManager_ && guiJson.contains("viewState") && guiJson["viewState"].is_object()) {
        try {
            auto viewState = guiJson["viewState"];
            if (viewState.contains("fileBrowserVisible")) {
                viewManager_->setFileBrowserVisible(viewState["fileBrowserVisible"].get<bool>());
            }
            if (viewState.contains("consoleVisible")) {
                viewManager_->setConsoleVisible(viewState["consoleVisible"].get<bool>());
            }
            if (viewState.contains("assetLibraryVisible")) {
                viewManager_->setAssetLibraryVisible(viewState["assetLibraryVisible"].get<bool>());
            }
            if (viewState.contains("masterModulesVisible")) {
                viewManager_->setMasterModulesVisible(viewState["masterModulesVisible"].get<bool>());
            }
            if (viewState.contains("currentFocusedWindow")) {
                std::string windowName = viewState["currentFocusedWindow"].get<std::string>();
                if (!windowName.empty()) {
                    viewManager_->navigateToWindow(windowName);
                }
            }
            ofLogNotice("EditorShell") << "Loaded view state";
        } catch (const std::exception& e) {
            ofLogError("EditorShell") << "Failed to load view state: " << e.what();
        }
    }
    
    // Load module layouts
    if (guiJson.contains("moduleLayouts") && guiJson["moduleLayouts"].is_object()) {
        try {
            std::map<std::string, ImVec2> layouts;
            for (auto& [key, value] : guiJson["moduleLayouts"].items()) {
                if (value.is_object() && value.contains("width") && value.contains("height")) {
                    float width = value["width"].get<float>();
                    float height = value["height"].get<float>();
                    layouts[key] = ImVec2(width, height);
                }
            }
            ModuleGUI::setAllDefaultLayouts(layouts);
            ofLogNotice("EditorShell") << "Loaded " << layouts.size() << " module layout(s)";
        } catch (const std::exception& e) {
            ofLogError("EditorShell") << "Failed to load module layouts: " << e.what();
        }
    }
    
    // Load ImGui window state (or store for later if ImGui not initialized)
    // ImGui will apply docking state when dockspace exists
    if (guiJson.contains("imguiState") && guiJson["imguiState"].is_string()) {
        std::string imguiState = guiJson["imguiState"].get<std::string>();
        if (!imguiState.empty()) {
            ImGuiContext* ctx = ImGui::GetCurrentContext();
            if (ctx) {
                // Try to load immediately - ImGui will apply docking state when dockspace exists
                try {
                    ImGui::LoadIniSettingsFromMemory(imguiState.c_str(), imguiState.size());
                    ofLogNotice("EditorShell") << "✓ Loaded ImGui window state (" << imguiState.size() << " bytes)";
                    imguiStateLoaded_ = true;
                    pendingImGuiState_.clear();  // Clear after successful load
                } catch (const std::exception& e) {
                    ofLogError("EditorShell") << "Failed to load ImGui state: " << e.what();
                    // Store for retry
                    pendingImGuiState_ = imguiState;
                    imguiStateLoaded_ = false;
                }
            } else {
                // ImGui not initialized yet - store for later loading
                pendingImGuiState_ = imguiState;
                imguiStateLoaded_ = false;
                ofLogNotice("EditorShell") << "ImGui not initialized, stored layout for later loading (" << imguiState.size() << " bytes)";
            }
        }
    }
    
    // Load module instance visibility state
    if (guiManager_ && guiJson.contains("visibleInstances") && guiJson["visibleInstances"].is_object()) {
        try {
            auto visibleInstances = guiJson["visibleInstances"];
            if (engine_) {
                // Use state snapshot instead of direct ModuleRegistry access
                EngineState state = engine_->getState();
                
                // Make all modules visible by default first (iterate from state snapshot)
                for (const auto& [name, moduleState] : state.modules) {
                    if (name != "masterAudioOut" && name != "masterVideoOut") {
                        guiManager_->setInstanceVisible(name, true);
                    }
                }
                
                // Then restore saved visibility
                for (auto& [category, instances] : visibleInstances.items()) {
                    if (instances.is_array()) {
                        for (const auto& name : instances) {
                            if (name.is_string()) {
                                std::string instanceName = name.get<std::string>();
                                // Check if module exists in state snapshot
                                if (state.modules.find(instanceName) != state.modules.end()) {
                                    guiManager_->setInstanceVisible(instanceName, true);
                                    ofLogVerbose("EditorShell") << "Restored " << category << " visibility: " << instanceName;
                                }
                            }
                        }
                    }
                }
                ofLogNotice("EditorShell") << "Restored module instance visibility state";
            }
        } catch (const std::exception& e) {
            ofLogError("EditorShell") << "Failed to restore visibility state: " << e.what();
        }
    }
    
    return true;
}

bool EditorShell::loadPendingImGuiState() {
    // Only load if we have pending state and ImGui is initialized
    if (pendingImGuiState_.empty()) {
        return false;  // No pending state
    }
    
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) {
        return false;  // ImGui not initialized yet
    }
    
    // Load the state - ImGui will apply docking when dockspace exists
    try {
        ImGui::LoadIniSettingsFromMemory(pendingImGuiState_.c_str(), pendingImGuiState_.size());
        ofLogNotice("EditorShell") << "✓ Loaded pending ImGui window state (" << pendingImGuiState_.size() << " bytes)";
        imguiStateLoaded_ = true;
        pendingImGuiState_.clear();  // Clear after successful load
        return true;
    } catch (const std::exception& e) {
        ofLogError("EditorShell") << "Failed to load pending ImGui state: " << e.what();
        return false;
    }
}

} // namespace shell
} // namespace vt

