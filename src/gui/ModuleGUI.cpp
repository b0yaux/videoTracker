#include "ModuleGUI.h"
#include "core/ModuleRegistry.h"
#include "GUIConstants.h"
#include "ofLog.h"
#include "imgui_internal.h"
#include "ofJson.h"
#include "ofFileUtils.h"

// Static member initialization
std::map<std::string, ImVec2> ModuleGUI::defaultLayouts;
bool ModuleGUI::layoutsLoaded = false;
const std::string ModuleGUI::LAYOUTS_FILENAME = "module_layouts.json";

ModuleGUI::ModuleGUI() {
    // Load layouts on first construction if not already loaded
    if (!layoutsLoaded) {
        loadDefaultLayouts();
        layoutsLoaded = true;
    }
}

void ModuleGUI::drawTitleBarToggle() {
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

void ModuleGUI::draw() {
    // Title bar toggle is drawn by ViewManager using drawTitleBarToggle()
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

void ModuleGUI::setupDragDropTarget() {
    if (ImGui::BeginDragDropTarget()) {
        // Check if there's any drag drop payload active (for debugging)
        const ImGuiPayload* activePayload = ImGui::GetDragDropPayload();
        if (activePayload) {
            ofLogVerbose("ModuleGUI") << "Active drag drop payload type: " << activePayload->DataType 
                                      << ", size: " << activePayload->DataSize;
        }
        
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_BROWSER_FILES")) {
            ofLogNotice("ModuleGUI") << "Received FILE_BROWSER_FILES payload, size: " << payload->DataSize;
            
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
            ofLogVerbose("ModuleGUI") << "Drag drop payload type mismatch. Expected FILE_BROWSER_FILES, got: " << activePayload->DataType;
        }
        ImGui::EndDragDropTarget();
    }
}

