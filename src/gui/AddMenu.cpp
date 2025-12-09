#include "AddMenu.h"
#include "ofMain.h"
#include "GUIConstants.h"

AddMenu::AddMenu() {
    reset();
}

void AddMenu::setup(
    const std::vector<ModuleInfo>& availableModules_,
    std::function<void(const std::string& moduleType)> onAddModule_
) {
    availableModules = availableModules_;
    onAddModule = onAddModule_;
    // Setup complete
}

void AddMenu::open(float mouseX, float mouseY) {
    if (isMenuOpen) return;
    
    reset();
    setMenuPosition(mouseX, mouseY);
    shouldOpenMenu = true;
    // Opening menu
}

void AddMenu::close() {
    if (isMenuOpen) {
        isMenuOpen = false;
        shouldOpenMenu = false;
        reset();
        // Menu closed
    }
}

void AddMenu::reset() {
    filterText.clear();
    selectedIndex = 0;
    lastInputTime = getCurrentTime();
}

void AddMenu::draw() {
    // Open menu popup on first frame if requested
    if (shouldOpenMenu) {
        ImGui::OpenPopup("Add Module");
        shouldOpenMenu = false;
        isMenuOpen = true;
    }
    
    if (!isMenuOpen) return;
    
    // Simple modal popup
    if (ImGui::BeginPopupModal("Add Module", &isMenuOpen)) {
        drawMenuContent();
        ImGui::EndPopup();
    }
}

void AddMenu::handleCharInput(unsigned int character) {
    if (!isMenuOpen) return;
    
    // Handle printable characters for direct typing
    if (character >= 32 && character < 127) {
        filterText += static_cast<char>(character);
        selectedIndex = 0;
        lastInputTime = getCurrentTime();
        // Filter updated
    }
}

void AddMenu::drawMenuContent() {
    // Show current filter
    if (!filterText.empty()) {
        ImGui::Text("Filter: %s", filterText.c_str());
    } else {
        ImGui::Text("Add Module (type to filter)");
    }
    ImGui::Separator();
    
    std::vector<int> filteredIndices = getFilteredIndices();
    
    if (filteredIndices.empty()) {
        ImGui::Text("No modules found");
        return;
    }
    
    // Simple list of modules
    for (size_t i = 0; i < filteredIndices.size() && i < 10; i++) {
        int moduleIndex = filteredIndices[i];
        const auto& module = availableModules[moduleIndex];
        
        bool isSelected = (static_cast<int>(i) == selectedIndex);
        if (ImGui::Selectable(module.displayName.c_str(), isSelected)) {
            selectModule(static_cast<int>(i));
        }
        
        if (ImGui::IsItemHovered() && !module.description.empty()) {
            ImGui::SetTooltip("%s", module.description.c_str());
        }
    }
    
    // Handle keyboard navigation
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && !filteredIndices.empty()) {
        selectModule(selectedIndex);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        close();
    }
}

std::vector<int> AddMenu::getFilteredIndices() const {
    std::vector<int> filteredIndices;
    
    for (size_t i = 0; i < availableModules.size(); i++) {
        if (matchesFilter(availableModules[i])) {
            filteredIndices.push_back(static_cast<int>(i));
        }
    }
    
    return filteredIndices;
}

bool AddMenu::matchesFilter(const ModuleInfo& module) const {
    if (filterText.empty()) return true;
    
    std::string filter = toLowerCase(filterText);
    std::string name = toLowerCase(module.displayName);
    
    return name.find(filter) != std::string::npos;
}

void AddMenu::selectModule(int index) {
    std::vector<int> filteredIndices = getFilteredIndices();
    
    if (index < 0 || index >= static_cast<int>(filteredIndices.size())) {
        return;
    }
    
    int moduleIndex = filteredIndices[index];
    const auto& module = availableModules[moduleIndex];
    
    if (onAddModule) {
        onAddModule(module.typeName);
        // Module selected
        close();
    }
}

void AddMenu::setMenuPosition(float x, float y) {
    menuPosX = x >= 0 ? x : 400;  // Simple fallback
    menuPosY = y >= 0 ? y : 300;
}

std::string AddMenu::toLowerCase(const std::string& str) const {
    std::string result = str;
    for (char& c : result) {
        c = std::tolower(c);
    }
    return result;
}

float AddMenu::getCurrentTime() const {
    static float time = 0.0f;
    time += 0.016f;
    return time;
}

void AddMenu::logAction(const std::string& action) const {
    // Log action
}