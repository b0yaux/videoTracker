#pragma once
#include <functional>
#include <string>
#include <vector>

/**
 * AddMenu - Simple Add Menu for module selection
 */
class AddMenu {
public:
    AddMenu();
    ~AddMenu() = default;

    // Module type information
    struct ModuleInfo {
        std::string typeName;
        std::string displayName;
        std::string description;
        std::string category;
        std::string shortcut;
    };

    // Setup with available modules and callback
    void setup(
        const std::vector<ModuleInfo>& availableModules,
        std::function<void(const std::string& moduleType)> onAddModule
    );

    // Menu state management
    void open(float mouseX = -1, float mouseY = -1);
    void close();
    bool isOpen() const { return isMenuOpen; }

    // Main draw function - call every frame
    void draw();

    // Handle direct keyboard input for filtering
    void handleCharInput(unsigned int character);

    // Clear current filter and reset selection
    void reset();

private:
    // Available modules data
    std::vector<ModuleInfo> availableModules;
    
    // Callback for adding module
    std::function<void(const std::string& moduleType)> onAddModule;
    
    // Menu state
    bool isMenuOpen = false;
    bool shouldOpenMenu = false;
    float menuPosX = 0.0f;
    float menuPosY = 0.0f;
    
    // Filter state
    std::string filterText = "";
    int selectedIndex = 0;
    float lastInputTime = 0.0f;
    
    // UI helper methods
    void drawMenuContent();
    std::vector<int> getFilteredIndices() const;
    bool matchesFilter(const ModuleInfo& module) const;
    void selectModule(int index);
    void setMenuPosition(float x, float y);
    std::string toLowerCase(const std::string& str) const;
    float getCurrentTime() const;
    void logAction(const std::string& action) const;
};