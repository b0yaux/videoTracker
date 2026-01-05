#include "ModuleGUI.h"
#include "core/ModuleRegistry.h"
#include "core/ConnectionManager.h"
#include "gui/GUIManager.h"
#include "ParameterCell.h"
#include "modules/Module.h"
#include "GUIConstants.h"
#include "ofLog.h"
#include "imgui_internal.h"
#include "ofJson.h"
#include "ofFileUtils.h"
#include "gui/CellGrid.h"
#include "ofConstants.h"  // For OF_KEY_* constants

// Static member initialization
std::map<std::string, ImVec2> ModuleGUI::defaultLayouts;
bool ModuleGUI::layoutsLoaded = false;
const std::string ModuleGUI::LAYOUTS_FILENAME = "module_layouts.json";

ModuleGUI::ModuleGUI() {
    // Load layouts on first construction if not already loaded
    // Note: If layouts are loaded from session, setAllDefaultLayouts()
    // will set layoutsLoaded = true, preventing file load
    if (!layoutsLoaded) {
        loadDefaultLayouts();
        layoutsLoaded = true;
    }
    
    // Initialize rename buffer with empty string
    renameBuffer_[0] = '\0';
}

void ModuleGUI::syncEnabledState() {
    if (registry && !instanceName.empty()) {
        auto module = registry->getModule(instanceName);
        if (module) {
            enabled_ = module->isEnabled();
        }
    }
}

void ModuleGUI::drawTitleBarToggle() {
    // Don't draw toggle if subclass says not to
    if (!shouldShowToggle()) return;
    
    // Draw ON/OFF toggle button directly in ImGui's native title bar
    // Uses foreground draw list to draw on top of title bar decorations
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window || window->SkipItems) return;
    
    // Skip drawing when window is collapsed to avoid interfering with ImGui's internal state
    if (ImGui::IsWindowCollapsed()) return;
    
    // Get title bar rectangle (works for both docked and undocked windows)
    ImRect titleBarRect = window->TitleBarRect();
    
    // If title bar is too small or invalid, skip drawing
    if (titleBarRect.GetHeight() < 1.0f) return;
    
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Calculate toggle control dimensions
    float checkboxSize = titleBarRect.GetHeight() * 0.6f;  // Slightly smaller checkbox
    std::string label = enabled_ ? "ON" : "OFF";
    ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
    float spacing = style.ItemSpacing.x;
    float padding = style.WindowPadding.x;
    float totalControlsWidth = labelSize.x + spacing + checkboxSize;  // Label before checkbox
    
    // Position toggle on the right side of title bar, near close button area
    // Leave a small gap from the right edge for window controls if they exist
    float rightPadding = padding;
    float toggleStartX = titleBarRect.Max.x - totalControlsWidth - rightPadding;
    
    // Position checkbox and label (checkbox on right, label to its left)
    ImVec2 checkboxPos = ImVec2(titleBarRect.Max.x - rightPadding - checkboxSize,
                                titleBarRect.Min.y + (titleBarRect.GetHeight() - checkboxSize) * 0.5f);
    ImVec2 labelPos = ImVec2(checkboxPos.x - spacing - labelSize.x,
                            titleBarRect.Min.y + (titleBarRect.GetHeight() - ImGui::GetTextLineHeight()) * 0.5f);
    
    // Handle click detection manually (don't create widgets that interfere with collapsed windows)
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    ImRect toggleRect(ImVec2(toggleStartX, checkboxPos.y), 
                     ImVec2(toggleStartX + totalControlsWidth, checkboxPos.y + checkboxSize));
    bool mouseInToggle = toggleRect.Contains(mousePos);
    bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool hovered = mouseInToggle;
    bool clicked = mouseInToggle && mouseClicked;
    
    // Only process click if window is not collapsed (to avoid interfering with collapse behavior)
    if (clicked && !ImGui::IsWindowCollapsed()) {
        enabled_ = !enabled_;
        
        // Update backend module enabled state
        if (registry && !instanceName.empty()) {
            auto module = registry->getModule(instanceName);
            if (module) {
                module->setEnabled(enabled_);
            }
        }
        
        ofLogVerbose("ModuleGUI") << "Module " << instanceName << " " << (enabled_ ? "enabled" : "disabled");
    }
    
    // Use foreground draw list to draw on top of title bar
    // This works even when window is collapsed
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    
    // Draw ON/OFF label (before checkbox) - use title bar text color
    ImU32 labelColor = enabled_ ? 
        ImGui::GetColorU32(ImGuiCol_Text) : 
        ImGui::GetColorU32(ImGuiCol_Text, 0.6f);
    drawList->AddText(labelPos, labelColor, label.c_str());
    
    // Draw checkbox visual
    ImRect checkboxRect(checkboxPos, ImVec2(checkboxPos.x + checkboxSize, checkboxPos.y + checkboxSize));
    ImU32 borderColor = hovered ? 
        ImGui::GetColorU32(ImGuiCol_Border) : 
        ImGui::GetColorU32(ImGuiCol_Border, 0.5f);
    
    drawList->AddRect(checkboxRect.Min, checkboxRect.Max, borderColor, 0.0f, 0, 1.5f);
    
    if (enabled_) {
        // Fill checkbox when enabled
        ImU32 fillColor = ImGui::GetColorU32(ImGuiCol_CheckMark, 0.3f);
        drawList->AddRectFilled(checkboxRect.Min, checkboxRect.Max, fillColor);
        
        // Draw checkmark
        float checkmarkThickness = 2.0f;
        ImVec2 center = checkboxRect.GetCenter();
        ImVec2 p1 = ImVec2(center.x - checkboxSize * 0.2f, center.y);
        ImVec2 p2 = ImVec2(center.x - checkboxSize * 0.05f, center.y + checkboxSize * 0.15f);
        ImVec2 p3 = ImVec2(center.x + checkboxSize * 0.25f, center.y - checkboxSize * 0.15f);
        ImU32 checkmarkColor = ImGui::GetColorU32(ImGuiCol_CheckMark);
        drawList->AddLine(p1, p2, checkmarkColor, checkmarkThickness);
        drawList->AddLine(p2, p3, checkmarkColor, checkmarkThickness);
    }
}

void ModuleGUI::drawTitleBarMenuIcon() {
    // Draw menu icon (☰) button in title bar, positioned to the left of ON/OFF toggle
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window || window->SkipItems) return;
    
    // Skip drawing when window is collapsed
    if (ImGui::IsWindowCollapsed()) return;
    
    // Get title bar rectangle
    ImRect titleBarRect = window->TitleBarRect();
    if (titleBarRect.GetHeight() < 1.0f) return;
    
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Calculate menu icon
    std::string menuIcon = "☰";  // Three horizontal lines (hamburger menu)
    ImVec2 iconSizeVec = ImGui::CalcTextSize(menuIcon.c_str());
    
    // Position menu icon to the left of the ON/OFF toggle
    // We need to calculate where the toggle starts, then position menu icon before it
    float checkboxSize = titleBarRect.GetHeight() * 0.6f;
    std::string toggleLabel = enabled_ ? "ON" : "OFF";
    ImVec2 labelSize = ImGui::CalcTextSize(toggleLabel.c_str());
    float spacing = style.ItemSpacing.x;
    float padding = style.WindowPadding.x;
    float toggleWidth = labelSize.x + spacing + checkboxSize;
    float toggleStartX = titleBarRect.Max.x - toggleWidth - padding;
    
    // Position menu icon to the left of toggle with some spacing
    float menuIconX = toggleStartX - spacing - iconSizeVec.x;
    ImVec2 menuIconPos = ImVec2(menuIconX,
                                titleBarRect.Min.y + (titleBarRect.GetHeight() - iconSizeVec.y) * 0.5f);
    
    // Handle click detection
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    ImRect menuIconRect(menuIconPos, ImVec2(menuIconPos.x + iconSizeVec.x, menuIconPos.y + iconSizeVec.y));
    bool mouseInIcon = menuIconRect.Contains(mousePos);
    bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool hovered = mouseInIcon;
    bool clicked = mouseInIcon && mouseClicked;
    
    // Open popup on click
    if (clicked && !ImGui::IsWindowCollapsed()) {
        // Initialize rename buffer with current instance name
        strncpy(renameBuffer_, instanceName.c_str(), sizeof(renameBuffer_) - 1);
        renameBuffer_[sizeof(renameBuffer_) - 1] = '\0';
        
        // Open popup
        std::string popupId = "ModuleMenu_" + instanceName;
        ImGui::OpenPopup(popupId.c_str());
    }
    
    // Use foreground draw list to draw on top of title bar
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    
    // Draw menu icon with hover color
    ImU32 iconColor = hovered ? 
        ImGui::GetColorU32(ImGuiCol_Text) : 
        ImGui::GetColorU32(ImGuiCol_Text, 0.7f);
    drawList->AddText(menuIconPos, iconColor, menuIcon.c_str());
}

void ModuleGUI::drawModulePopup() {
    std::string popupId = "ModuleMenu_" + instanceName;
    ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing);
    
    if (ImGui::BeginPopup(popupId.c_str())) {
        // Instance name editor
        ImGui::Text("Instance name:");
        ImGui::SetNextItemWidth(200.0f);
        bool enterPressed = ImGui::InputText("##rename", renameBuffer_, sizeof(renameBuffer_), 
                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::SameLine();
        bool applyPressed = ImGui::Button("Apply");
        
        // Apply rename
        if ((enterPressed || applyPressed) && registry) {
            std::string newName(renameBuffer_);
            if (!newName.empty() && newName != instanceName) {
                if (registry->renameModule(instanceName, newName)) {
                    if (guiManager) {
                        guiManager->renameInstance(instanceName, newName);
                    }
                    instanceName = newName;
                } else {
                    strncpy(renameBuffer_, instanceName.c_str(), sizeof(renameBuffer_) - 1);
                    renameBuffer_[sizeof(renameBuffer_) - 1] = '\0';
                }
            }
        }
        
        ImGui::Separator();
        
        // Connections
        if (connectionManager && registry) {
            // Helper to get connection type string
            auto getTypeString = [](ConnectionManager::ConnectionType type) -> const char* {
                switch (type) {
                    case ConnectionManager::ConnectionType::AUDIO: return "AUDIO";
                    case ConnectionManager::ConnectionType::VIDEO: return "VIDEO";
                    case ConnectionManager::ConnectionType::PARAMETER: return "PARAMETER";
                    case ConnectionManager::ConnectionType::EVENT: return "EVENT";
                    default: return "UNKNOWN";
                }
            };
            
            // Helper to disconnect
            auto disconnectConnection = [this](const ConnectionManager::Connection& conn, bool isInput) {
                switch (conn.type) {
                    case ConnectionManager::ConnectionType::AUDIO:
                        connectionManager->disconnectAudio(
                            isInput ? conn.sourceModule : instanceName,
                            isInput ? instanceName : conn.targetModule);
                        break;
                    case ConnectionManager::ConnectionType::VIDEO:
                        connectionManager->disconnectVideo(
                            isInput ? conn.sourceModule : instanceName,
                            isInput ? instanceName : conn.targetModule);
                        break;
                    case ConnectionManager::ConnectionType::PARAMETER:
                        if (!conn.sourcePath.empty()) {
                            connectionManager->disconnectParameter(conn.sourcePath);
                        }
                        break;
                    case ConnectionManager::ConnectionType::EVENT:
                        connectionManager->unsubscribeEvent(
                            isInput ? conn.sourceModule : instanceName,
                            conn.eventName,
                            isInput ? instanceName : conn.targetModule,
                            conn.handlerName);
                        break;
                }
            };
            
            // Inputs section
            auto inputs = connectionManager->getConnectionsTo(instanceName);
            if (!inputs.empty()) {
                ImGui::Text("Inputs:");
                for (const auto& conn : inputs) {
                    ImGui::BulletText("%s (%s)", conn.sourceModule.c_str(), getTypeString(conn.type));
                    ImGui::SameLine();
                    if (ImGui::SmallButton(("X##input_" + conn.sourceModule + "_" + std::to_string(static_cast<int>(conn.type))).c_str())) {
                        disconnectConnection(conn, true);
                    }
                }
            }
            
            // Outputs section
            auto outputs = connectionManager->getConnectionsFrom(instanceName);
            if (!outputs.empty()) {
                ImGui::Text("Outputs:");
                for (const auto& conn : outputs) {
                    ImGui::BulletText("%s (%s)", conn.targetModule.c_str(), getTypeString(conn.type));
                    ImGui::SameLine();
                    if (ImGui::SmallButton(("X##output_" + conn.targetModule + "_" + std::to_string(static_cast<int>(conn.type))).c_str())) {
                        disconnectConnection(conn, false);
                    }
                }
            }
            
            // Add new connection
            ImGui::Separator();
            ImGui::Text("Add connection:");
            
            // Check which connection types have compatible modules
            ConnectionManager::ConnectionType allTypes[] = {
                ConnectionManager::ConnectionType::AUDIO,
                ConnectionManager::ConnectionType::VIDEO,
                ConnectionManager::ConnectionType::EVENT,
                ConnectionManager::ConnectionType::PARAMETER
            };
            const char* allTypeNames[] = { "AUDIO", "VIDEO", "EVENT", "PARAMETER" };
            
            // Build list of available connection types (only those with compatible modules)
            std::vector<int> availableTypeIndices;
            std::vector<const char*> availableTypeNames;
            std::vector<ConnectionManager::ConnectionType> availableTypes;
            
            for (int i = 0; i < 4; i++) {
                auto compatible = connectionManager->findCompatibleModules(instanceName, allTypes[i]);
                if (!compatible.empty()) {
                    availableTypeIndices.push_back(i);
                    availableTypeNames.push_back(allTypeNames[i]);
                    availableTypes.push_back(allTypes[i]);
                }
            }
            
            // Use per-instance storage to avoid conflicts between different module popups
            static std::map<std::string, int> selectedConnectionTypeMap; // instanceName -> selectedType (index in availableTypes)
            static std::map<std::string, int> selectedModuleIndexMap;     // instanceName -> selectedIndex
            
            // Initialize or get selected connection type for this instance
            int& selectedConnectionType = selectedConnectionTypeMap[instanceName];
            int& selectedModuleIndex = selectedModuleIndexMap[instanceName];
            
            // On popup open, select first available connection type
            if (ImGui::IsWindowAppearing()) {
                selectedConnectionType = 0;
                selectedModuleIndex = 0;
            }
            
            // Ensure selected index is valid
            if (selectedConnectionType >= static_cast<int>(availableTypeIndices.size())) {
                selectedConnectionType = 0;
            }
            
            if (!availableTypeNames.empty()) {
                ImGui::SetNextItemWidth(120.0f);
                bool typeChanged = ImGui::Combo("##connection_type", &selectedConnectionType, 
                                               availableTypeNames.data(), static_cast<int>(availableTypeNames.size()));
                
                // Get compatible modules based on selected connection type
                ConnectionManager::ConnectionType connType = availableTypes[selectedConnectionType];
                std::vector<std::string> compatibleModules = connectionManager->findCompatibleModules(instanceName, connType);
                
                // Reset module selection when connection type changes or if invalid
                if (typeChanged || selectedModuleIndex >= static_cast<int>(compatibleModules.size())) {
                    selectedModuleIndex = 0;
                }
                
                if (!compatibleModules.empty()) {
                    ImGui::SameLine();
                    
                    // Target module dropdown (only shows compatible modules)
                    if (ImGui::BeginCombo("##target_module", 
                                         selectedModuleIndex < static_cast<int>(compatibleModules.size()) ? 
                                         compatibleModules[selectedModuleIndex].c_str() : "")) {
                        for (int i = 0; i < static_cast<int>(compatibleModules.size()); i++) {
                            bool isSelected = (selectedModuleIndex == i);
                            if (ImGui::Selectable(compatibleModules[i].c_str(), isSelected)) {
                                selectedModuleIndex = i;
                            }
                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    
                    ImGui::SameLine();
                    
                    // Connect button
                    if (ImGui::Button("Connect")) {
                        std::string targetModule = compatibleModules[selectedModuleIndex];
                        bool success = false;
                        
                        // Get source module for metadata
                        auto sourceModule = registry->getModule(instanceName);
                        auto targetModulePtr = registry->getModule(targetModule);
                        
                        if (!sourceModule || !targetModulePtr) {
                            ofLogError("ModuleGUI") << "Failed to get module for connection";
                        } else {
                            // Get the actual connection type index from available types
                            int actualTypeIndex = availableTypeIndices[selectedConnectionType];
                            
                            switch (actualTypeIndex) {
                                case 0: // AUDIO
                                    success = connectionManager->connectAudio(instanceName, targetModule);
                                    break;
                                case 1: // VIDEO
                                    success = connectionManager->connectVideo(instanceName, targetModule);
                                    break;
                                case 2: { // EVENT
                                    auto sourceMetadata = sourceModule->getMetadata();
                                    auto targetMetadata = targetModulePtr->getMetadata();
                                    if (!sourceMetadata.eventNames.empty() && !targetMetadata.eventNames.empty()) {
                                        std::string eventName = sourceMetadata.eventNames[0];
                                        std::string handlerName = targetMetadata.eventNames[0];
                                        success = connectionManager->subscribeEvent(instanceName, eventName, targetModule, handlerName);
                                    } else {
                                        ofLogWarning("ModuleGUI") << "Missing event names for EVENT connection";
                                    }
                                    break;
                                }
                                case 3: { // PARAMETER
                                    auto sourceMetadata = sourceModule->getMetadata();
                                    auto targetMetadata = targetModulePtr->getMetadata();
                                    if (!sourceMetadata.parameterNames.empty() && !targetMetadata.parameterNames.empty()) {
                                        std::string sourceParam = sourceMetadata.parameterNames[0];
                                        std::string targetParam = targetMetadata.parameterNames[0];
                                        success = connectionManager->connectParameterDirect(instanceName, sourceParam, targetModule, targetParam);
                                    } else {
                                        ofLogWarning("ModuleGUI") << "Missing parameter names for PARAMETER connection";
                                    }
                                    break;
                                }
                            }
                            
                            if (success) {
                                ofLogNotice("ModuleGUI") << "Connected " << instanceName << " -> " << targetModule 
                                                         << " (" << availableTypeNames[selectedConnectionType] << ")";
                            }
                        }
                    }
                } else {
                    ImGui::SameLine();
                    ImGui::TextDisabled("No compatible modules for %s", availableTypeNames[selectedConnectionType]);
                }
            } else {
                ImGui::TextDisabled("No connection types available");
            }
            
            if (inputs.empty() && outputs.empty()) {
                ImGui::TextDisabled("No connections");
            }
        } else {
            ImGui::TextDisabled("ConnectionManager not available");
        }
        
        ImGui::EndPopup();
    }
}

void ModuleGUI::draw() {
    // Title bar toggle and popup menu are drawn by ViewManager
    // Just draw content here
    
    if (enabled_) {
        drawContent();
    } else {
        // Make background fully transparent to indicate disabled state
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##disabled_bg", ImGui::GetContentRegionAvail(), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
}

std::string ModuleGUI::getModuleTypeName() const {
    if (!registry || instanceName.empty()) {
        return "";
    }
    
    auto module = registry->getModule(instanceName);
    if (!module) {
        return "";
    }
    
    return module->getTypeName();
}

void ModuleGUI::setupWindow() {
    // Apply default size if saved for this module type
    ImVec2 defaultSize = getDefaultSize();
    if (defaultSize.x > 0 && defaultSize.y > 0) {
        ImGui::SetNextWindowSize(defaultSize, ImGuiCond_FirstUseEver);
    }
}

void ModuleGUI::saveDefaultLayout() {
    std::string typeName = getModuleTypeName();
    if (typeName.empty()) {
        ofLogWarning("ModuleGUI") << "Cannot save layout: module type name is empty";
        return;
    }
    
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window) {
        ofLogWarning("ModuleGUI") << "Cannot save layout: no current window";
        return;
    }
    
    ImVec2 size = window->Size;
    saveDefaultLayoutForType(typeName, size);
    ofLogNotice("ModuleGUI") << "Saved default layout for " << typeName 
                             << ": " << size.x << "x" << size.y;
}

ImVec2 ModuleGUI::getDefaultSize() const {
    std::string typeName = getModuleTypeName();
    if (typeName.empty()) {
        return ImVec2(0, 0);
    }
    
    return getDefaultSizeForType(typeName);
}

void ModuleGUI::saveDefaultLayoutForType(const std::string& moduleTypeName, const ImVec2& size) {
    if (moduleTypeName.empty()) {
        ofLogWarning("ModuleGUI") << "Cannot save layout: module type name is empty";
        return;
    }
    
    defaultLayouts[moduleTypeName] = size;
    saveDefaultLayouts();  // Persist to disk immediately
}

ImVec2 ModuleGUI::getDefaultSizeForType(const std::string& moduleTypeName) {
    auto it = defaultLayouts.find(moduleTypeName);
    if (it != defaultLayouts.end()) {
        return it->second;
    }
    return ImVec2(0, 0);  // No default saved
}

void ModuleGUI::loadDefaultLayouts() {
    std::string filePath = ofToDataPath(LAYOUTS_FILENAME, true);
    
    if (!ofFile::doesFileExist(filePath)) {
        ofLogNotice("ModuleGUI") << "No saved module layouts found at " << filePath;
        return;
    }
    
    try {
        ofFile file(filePath, ofFile::ReadOnly);
        if (!file.is_open()) {
            ofLogError("ModuleGUI") << "Failed to open layouts file: " << filePath;
            return;
        }
        
        std::string jsonString = file.readToBuffer().getText();
        file.close();
        
        ofJson json = ofJson::parse(jsonString);
        
        if (json.contains("layouts") && json["layouts"].is_object()) {
            defaultLayouts.clear();
            for (auto& [key, value] : json["layouts"].items()) {
                if (value.is_object() && value.contains("width") && value.contains("height")) {
                    float width = value["width"].get<float>();
                    float height = value["height"].get<float>();
                    defaultLayouts[key] = ImVec2(width, height);
                    ofLogVerbose("ModuleGUI") << "Loaded layout for " << key 
                                              << ": " << width << "x" << height;
                }
            }
            ofLogNotice("ModuleGUI") << "Loaded " << defaultLayouts.size() 
                                     << " module layout(s) from " << filePath;
        }
    } catch (const std::exception& e) {
        ofLogError("ModuleGUI") << "Exception loading layouts: " << e.what();
    }
}

void ModuleGUI::saveDefaultLayouts() {
    std::string filePath = ofToDataPath(LAYOUTS_FILENAME, true);
    
    try {
        ofJson json;
        json["layouts"] = ofJson::object();
        
        for (const auto& [typeName, size] : defaultLayouts) {
            json["layouts"][typeName] = ofJson::object();
            json["layouts"][typeName]["width"] = size.x;
            json["layouts"][typeName]["height"] = size.y;
        }
        
        ofFile file(filePath, ofFile::WriteOnly);
        if (!file.is_open()) {
            ofLogError("ModuleGUI") << "Failed to open layouts file for writing: " << filePath;
            return;
        }
        
        file << json.dump(4);  // Pretty print with 4 spaces
        file.close();
        
        ofLogVerbose("ModuleGUI") << "Saved " << defaultLayouts.size() 
                                  << " module layout(s) to " << filePath;
    } catch (const std::exception& e) {
        ofLogError("ModuleGUI") << "Exception saving layouts: " << e.what();
    }
}

std::map<std::string, ImVec2> ModuleGUI::getAllDefaultLayouts() {
    return defaultLayouts;
}

void ModuleGUI::setAllDefaultLayouts(const std::map<std::string, ImVec2>& layouts) {
    defaultLayouts = layouts;
    layoutsLoaded = true;  // Mark as loaded so we don't overwrite from file
}

void ModuleGUI::setupDragDropTarget() {
    if (ImGui::BeginDragDropTarget()) {
        // Check if there's any drag drop payload active (for debugging)
        const ImGuiPayload* activePayload = ImGui::GetDragDropPayload();
        if (activePayload) {
            ofLogVerbose("ModuleGUI") << "Active drag drop payload type: " << activePayload->DataType 
                                      << ", size: " << activePayload->DataSize;
        }
        
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATHS")) {
            ofLogNotice("ModuleGUI") << "Received FILE_PATHS payload, size: " << payload->DataSize;
            
            // Extract file paths from payload
            // Payload data is a serialized string: each path is null-terminated, final path is double-null-terminated
            if (payload->DataSize > 0) {
                const char* data = static_cast<const char*>(payload->Data);
                std::vector<std::string> filePaths;
                
                // Deserialize: read paths until we hit double null
                const char* current = data;
                while (current < data + payload->DataSize) {
                    if (*current == '\0') {
                        // Check if this is the end marker (double null)
                        if (current + 1 < data + payload->DataSize && *(current + 1) == '\0') {
                            break; // End of paths
                        }
                        // Single null - end of current path, start next
                        current++;
                        continue;
                    }
                    
                    // Find the end of this path (next null)
                    const char* pathStart = current;
                    while (current < data + payload->DataSize && *current != '\0') {
                        current++;
                    }
                    
                    // Extract path
                    if (current > pathStart) {
                        std::string path(pathStart, current - pathStart);
                        if (!path.empty()) {
                            filePaths.push_back(path);
                            ofLogVerbose("ModuleGUI") << "Extracted file path: " << path;
                        }
                    }
                }
                
                // Call virtual method (modules override this)
                if (!filePaths.empty() && handleFileDrop(filePaths)) {
                    ofLogNotice("ModuleGUI") << "Accepted " << filePaths.size() << " file(s) via drag & drop";
                } else if (filePaths.empty()) {
                    ofLogWarning("ModuleGUI") << "No file paths extracted from payload";
                }
            } else {
                ofLogWarning("ModuleGUI") << "Drag drop payload is empty";
            }
        } else if (activePayload) {
            ofLogVerbose("ModuleGUI") << "Drag drop payload type mismatch. Expected FILE_PATHS, got: " << activePayload->DataType;
        }
        ImGui::EndDragDropTarget();
    }
}

bool ModuleGUI::hasWindowState() const {
    if (instanceName.empty() || !registry) {
        return false;
    }
    
    // Find window by UUID-based window ID (format: "DisplayName###UUID")
    std::string uuid = registry->getUUID(instanceName);
    std::string windowID = !uuid.empty() ? (instanceName + "###" + uuid) : instanceName;
    ImGuiWindow* window = ImGui::FindWindowByName(windowID.c_str());
    return window != nullptr;
}

ImVec2 ModuleGUI::getWindowPosition() const {
    if (instanceName.empty() || !registry) {
        return ImVec2(0, 0);
    }
    
    // Find window by UUID-based window ID (format: "DisplayName###UUID")
    std::string uuid = registry->getUUID(instanceName);
    std::string windowID = !uuid.empty() ? (instanceName + "###" + uuid) : instanceName;
    ImGuiWindow* window = ImGui::FindWindowByName(windowID.c_str());
    if (window) {
        return window->Pos;
    }
    
    return ImVec2(0, 0);
}

ImVec2 ModuleGUI::getWindowSize() const {
    if (instanceName.empty() || !registry) {
        return ImVec2(0, 0);
    }
    
    // Find window by UUID-based window ID (format: "DisplayName###UUID")
    std::string uuid = registry->getUUID(instanceName);
    std::string windowID = !uuid.empty() ? (instanceName + "###" + uuid) : instanceName;
    ImGuiWindow* window = ImGui::FindWindowByName(windowID.c_str());
    if (window) {
        return window->Size;
    }
    
    return ImVec2(0, 0);
}

bool ModuleGUI::isWindowCollapsed() const {
    if (instanceName.empty() || !registry) {
        return false;
    }
    
    // Find window by UUID-based window ID (format: "DisplayName###UUID")
    std::string uuid = registry->getUUID(instanceName);
    std::string windowID = !uuid.empty() ? (instanceName + "###" + uuid) : instanceName;
    ImGuiWindow* window = ImGui::FindWindowByName(windowID.c_str());
    if (window) {
        return window->Collapsed;
    }
    
    return false;
}

std::shared_ptr<Module> ModuleGUI::getModule() const {
    if (!registry || instanceName.empty()) {
        return nullptr;
    }
    return registry->getModule(instanceName);
}

std::unique_ptr<BaseCell> ModuleGUI::createCellWidget(
    const ParameterDescriptor& paramDesc,
    std::function<float()> customGetter,
    std::function<void(float)> customSetter,
    std::function<void()> customRemover,
    std::function<std::string(float)> customFormatter,
    std::function<float(const std::string&)> customParser
) const {
    auto module = getModule();
    if (!module) {
        // Return nullptr if module not found
        return nullptr;
    }
    
    // Use ParameterCell internally as implementation detail
    ParameterCell cell(module.get(), paramDesc, parameterRouter);
    
    // Apply custom callbacks if provided
    if (customGetter) {
        cell.setCustomGetter(customGetter);
    }
    if (customSetter) {
        cell.setCustomSetter(customSetter);
    }
    if (customRemover) {
        cell.setCustomRemover(customRemover);
    }
    if (customFormatter) {
        cell.setCustomFormatter(customFormatter);
    }
    if (customParser) {
        cell.setCustomParser(customParser);
    }
    
    // Return configured BaseCell
    return cell.createCell();
}

// ============================================================================
// Unified CellGrid State Management - Helper Implementations
// ============================================================================

void ModuleGUI::setCellFocus(CellFocusState& state, int row, int column, const std::string& paramName) {
    state.row = row;
    state.column = column;
    if (!paramName.empty()) {
        state.editingParameter = paramName;
    }
}

void ModuleGUI::clearCellFocus(CellFocusState& state) {
    state.clear();
}

bool ModuleGUI::isCellFocused(const CellFocusState& state, int row, int column) const {
    return state.matches(row, column);
}

int ModuleGUI::getFocusedRow(const CellFocusState& state) const {
    return state.row;
}

void ModuleGUI::restoreImGuiKeyboardNavigation() {
    ImGuiIO& io = ImGui::GetIO();
    bool wasEnabled = (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    bool nowEnabled = (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
    if (!wasEnabled && nowEnabled) {
        ofLogNotice("ModuleGUI") << "[NAV_RESTORE] Restored ImGui keyboard navigation (was disabled, now enabled)";
    } else if (wasEnabled) {
        ofLogVerbose("ModuleGUI") << "[NAV_RESTORE] Navigation already enabled";
    }
}

// ============================================================================
// Unified CellGrid Configuration - Helper Implementations
// ============================================================================

void ModuleGUI::configureCellGrid(CellGrid& grid, const CellGridConfig& config) {
    grid.setTableId(config.tableId);
    grid.setTableFlags(config.tableFlags);
    grid.setCellPadding(config.cellPadding);
    grid.setItemSpacing(config.itemSpacing);
    grid.enableReordering(config.enableReordering);
    if (config.enableScrolling) {
        grid.enableScrolling(true, config.scrollHeight);
        grid.setScrollbarSize(config.scrollbarSize);
    } else {
        grid.enableScrolling(false);
    }
}

void ModuleGUI::updateColumnConfigIfChanged(CellGrid& grid, 
                                             const std::vector<CellGridColumnConfig>& newConfig,
                                             std::vector<CellGridColumnConfig>& lastConfig) {
    // CRITICAL: Only call setColumnConfiguration() when config actually changes
    // This prevents clearing the widget cache every frame, which destroys drag/edit state
    if (newConfig != lastConfig) {
        grid.setColumnConfiguration(newConfig);
        lastConfig = newConfig;
    }
}

// ============================================================================
// Unified CellGrid Callback Setup - Helper Implementation
// ============================================================================

void ModuleGUI::setupStandardCellGridCallbacks(CellGridCallbacks& callbacks,
                                                 CellFocusState& cellFocusState,
                                                 CellGridCallbacksState& callbacksState,
                                                 CellGrid& cellGrid,
                                                 bool isSingleRow) {
    // Standard focus row callback
    callbacks.getFocusedRow = [&cellFocusState]() -> int {
        return cellFocusState.row; // Return focused row (-1 if none)
    };
    
    // Standard cell focus check callback
    callbacks.isCellFocused = [&cellFocusState](int row, int col) -> bool {
        // This is optional - CellGrid will use actual ImGui focus if not provided
        return cellFocusState.row == row && cellFocusState.column == col;
    };
    
    // Standard edit mode changed callback
    callbacks.onEditModeChanged = [&cellFocusState](int row, int col, bool editing) {
        // Track editing state for UI coordination (CellWidget manages editing state internally)
        ImGuiIO& io = ImGui::GetIO();
        bool navWasEnabled = (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
        
        // Update focus state if this is the focused cell
        if (cellFocusState.row == row && cellFocusState.column == col) {
            bool wasEditing = cellFocusState.isEditing;
            cellFocusState.isEditing = editing;
        }
        
        // CRITICAL: Always manage navigation based on edit mode, regardless of focus state
        // This ensures navigation is restored even if focus was lost during edit mode
        if (editing) {
            // Disable ImGui keyboard navigation when entering edit mode
            // Arrow keys should only adjust values, not navigate
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
            ofLogNotice("ModuleGUI") << "[EDIT_MODE] Entering edit mode (row=" << row << ", col=" << col 
                                     << ") - Navigation " << (navWasEnabled ? "was ENABLED, disabled" : "already disabled");
        } else {
            // Restore ImGui keyboard navigation when exiting edit mode
            // CRITICAL: Always restore navigation when exiting edit mode, even if cell doesn't match focus state
            // This handles cases where focus was lost but edit mode is still active
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            bool navNowEnabled = (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
            bool isFocusedCell = (cellFocusState.row == row && cellFocusState.column == col);
            ofLogNotice("ModuleGUI") << "[EDIT_MODE] Exiting edit mode (row=" << row << ", col=" << col 
                                     << ", isFocused=" << isFocusedCell
                                     << ") - Navigation " << (navWasEnabled ? "was already enabled" : "restored")
                                     << ", now " << (navNowEnabled ? "ENABLED" : "DISABLED");
        }
        // Note: Refocus is now handled automatically by CellWidget when exiting edit mode
    };
    
    // Standard cell focus changed callback
    callbacks.onCellFocusChanged = [this, &cellFocusState, &callbacksState, &cellGrid, isSingleRow](int row, int col) {
        // CRITICAL: Ignore focus callbacks if focus was cleared in the same frame
        // This prevents infinite loops where clearing focus triggers a callback that restores focus
        // Frame-based guard works regardless of execution order (drawRow vs handleFocusClearing)
        int currentFrame = ImGui::GetFrameCount();
        if (callbacksState.lastClearedFrame == currentFrame) {
            ofLogVerbose("ModuleGUI") << "[FOCUS_SKIP] Ignoring onCellFocusChanged (row=" << row 
                                      << ", col=" << col << ") - focus was cleared in frame " << currentFrame;
            return;
        }
        
        // Always update state - this ensures focus is maintained after Enter validates edit
        int actualRow = isSingleRow ? 0 : row;
        
        int oldRow = cellFocusState.row;
        int oldCol = cellFocusState.column;
        
        // Try to get parameter name from column config if available
        if (col >= 0 && col < (int)cellGrid.getColumnConfiguration().size()) {
            const auto& colConfig = cellGrid.getColumnConfiguration()[col];
            this->setCellFocus(cellFocusState, actualRow, col, colConfig.parameterName);
        } else {
            this->setCellFocus(cellFocusState, actualRow, col);
        }
        callbacksState.anyCellFocusedThisFrame = true;
        
        ofLogNotice("ModuleGUI") << "[FOCUS_CHANGED] Cell focus changed from (" << oldRow << "," << oldCol 
                                 << ") to (" << actualRow << "," << col 
                                 << "), anyCellFocusedThisFrame=" << callbacksState.anyCellFocusedThisFrame
                                 << ", isEditing=" << cellFocusState.isEditing;
    };
    
    // Standard cell clicked callback
    callbacks.onCellClicked = [this, &cellFocusState, &callbacksState, &cellGrid, isSingleRow](int row, int col) {
        // Track which cell is clicked for UI purposes
        int actualRow = isSingleRow ? 0 : row;
        
        // Try to get parameter name from column config if available
        if (col >= 0 && col < (int)cellGrid.getColumnConfiguration().size()) {
            const auto& colConfig = cellGrid.getColumnConfiguration()[col];
            this->setCellFocus(cellFocusState, actualRow, col, colConfig.parameterName);
        } else {
            this->setCellFocus(cellFocusState, actualRow, col);
        }
        callbacksState.anyCellFocusedThisFrame = true;
        // Note: Don't clear edit mode - let CellWidget handle it
    };
}

// ============================================================================
// Unified Input Handling - Helper Implementations
// ============================================================================

bool ModuleGUI::isTypingKey(int key) {
    // Typing keys: numeric digits, decimal point, operators
    return (key >= '0' && key <= '9') || 
           key == '.' || 
           key == '-' || 
           key == '+' || 
           key == '*' || 
           key == '/';
}

bool ModuleGUI::shouldDelegateToCellWidget(int key, bool isEditing) {
    // Always delegate these keys to CellWidget when in edit mode
    if (isEditing) {
        // Enter: validates edit
        if (key == OF_KEY_RETURN) return true;
        // Escape: cancels edit
        if (key == OF_KEY_ESC) return true;
        // Arrow keys: adjust value or navigate within cell
        if (key == OF_KEY_UP || key == OF_KEY_DOWN || 
            key == OF_KEY_LEFT || key == OF_KEY_RIGHT) return true;
        // Backspace/Delete: edit buffer manipulation
        if (key == OF_KEY_BACKSPACE || key == OF_KEY_DEL) return true;
    }
    
    // Typing keys should always be delegated (CellWidget will auto-enter edit mode)
    if (isTypingKey(key)) return true;
    
    return false;
}

bool ModuleGUI::handleCellInputKey(int key, bool isEditing, bool ctrlPressed, bool shiftPressed) {
    // For typing keys: auto-enter edit mode if not already editing
    // Return false to let the key pass through to ImGui so CellWidget can process it during draw()
    if (isTypingKey(key)) {
        // The caller should set isEditing = true if needed
        // We just return false to delegate to CellWidget
        return false;
    }
    
    // For keys that should be delegated to CellWidget, return false
    if (shouldDelegateToCellWidget(key, isEditing)) {
        return false;
    }
    
    // Special case: Ctrl+Enter or Shift+Enter to exit grid navigation
    if (key == OF_KEY_RETURN && (ctrlPressed || shiftPressed)) {
        // This should be handled by the caller (exit edit mode, clear focus)
        // Return true to indicate it was handled (caller should process it)
        return true;
    }
    
    // All other keys: let them pass through (return false)
    return false;
}

// ============================================================================
// Unified Focus Clearing - Helper Implementations
// ============================================================================

bool ModuleGUI::shouldClearCellFocus(const CellFocusState& cellFocusState,
                                     const CellGridCallbacksState& callbacksState,
                                     std::function<bool()> additionalCondition) {
    // Always clear if header was clicked
    if (callbacksState.headerClickedThisFrame) {
        ofLogVerbose("ModuleGUI") << "[SHOULD_CLEAR] Header clicked - clearing focus";
        return true;
    }
    
    // CRITICAL: Do NOT clear if a cell was focused this frame
    // This prevents clearing focus immediately after it was just gained
    if (callbacksState.anyCellFocusedThisFrame) {
        ofLogVerbose("ModuleGUI") << "[SHOULD_CLEAR] Skipping - cell was focused this frame (anyCellFocusedThisFrame=true)";
        return false;
    }
    
    // Clear if cell was focused but no cell was focused this frame AND not editing
    // This handles stale focus state from previous frames
    if (cellFocusState.hasFocus() && !cellFocusState.isEditing) {
        ofLogVerbose("ModuleGUI") << "[SHOULD_CLEAR] Stale focus detected - cell has focus but no cell focused this frame and not editing";
        // Check additional condition if provided (e.g., not dragging)
        if (additionalCondition) {
            bool shouldClear = additionalCondition();
            ofLogVerbose("ModuleGUI") << "[SHOULD_CLEAR] Additional condition returned: " << shouldClear;
            return shouldClear;
        }
        return true;
    }
    
    return false;
}

bool ModuleGUI::handleFocusClearing(CellFocusState& cellFocusState,
                                     CellGridCallbacksState& callbacksState,
                                     std::function<bool()> additionalCondition) {
    if (shouldClearCellFocus(cellFocusState, callbacksState, additionalCondition)) {
        // Set frame-based guard BEFORE clearing to prevent focus callbacks in the same frame
        // This works regardless of execution order (drawRow vs handleFocusClearing)
        int currentFrame = ImGui::GetFrameCount();
        callbacksState.lastClearedFrame = currentFrame;
        
        // Clear focus state directly (same as clearCellFocus but static)
        int oldRow = cellFocusState.row;
        int oldCol = cellFocusState.column;
        bool wasEditing = cellFocusState.isEditing;
        
        ofLogNotice("ModuleGUI") << "[CLEAR_FOCUS] Clearing cell focus (row=" << oldRow 
                                 << ", col=" << oldCol << ", wasEditing=" << wasEditing 
                                 << ", frame=" << currentFrame << ")";
        
        // Restore navigation if we were editing
        if (wasEditing) {
            restoreImGuiKeyboardNavigation();
        }
        
        // Clear focus state
        cellFocusState.clear();
        
        // Note: lastClearedFrame persists and is compared against current frame number
        // This prevents focus callbacks from being triggered in the same frame as clearing
        
        return true;
    }
    return false;
}

