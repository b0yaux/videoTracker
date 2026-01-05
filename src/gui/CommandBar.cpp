#include "CommandBar.h"
#include "core/CommandExecutor.h"
#include "gui/ViewManager.h"
#include "gui/GUIManager.h"
#include "core/ModuleRegistry.h"
#include "ofLog.h"

CommandBar::CommandBar() {
    // Initialize command palette context
    ImCmd::CreateContext();
}

CommandBar::~CommandBar() {
    // Restore navigation if command bar was open
    if (navigationStateSaved_) {
        restoreImGuiNavigation();
    }
    // Clean up command palette context
    ImCmd::DestroyContext();
}

void CommandBar::setup(CommandExecutor* executor, ViewManager* viewManager_, GUIManager* guiManager_) {
    commandExecutor = executor;
    viewManager = viewManager_;
    guiManager = guiManager_;
}

void CommandBar::toggle() {
    bool wasOpen = isOpen_;
    isOpen_ = !isOpen_;
    if (isOpen_) {
        registerCommands();
    } else if (wasOpen && navigationStateSaved_) {
        // Command bar was closed, restore navigation
        restoreImGuiNavigation();
        navigationStateSaved_ = false;
    }
}

void CommandBar::open() {
    isOpen_ = true;
    registerCommands();
}

void CommandBar::close() {
    if (isOpen_) {
        isOpen_ = false;
        if (navigationStateSaved_) {
            restoreImGuiNavigation();
            navigationStateSaved_ = false;
        }
    }
}

void CommandBar::draw() {
    if (!isOpen_) {
        // If command bar was just closed, restore navigation
        if (navigationStateSaved_) {
            restoreImGuiNavigation();
            navigationStateSaved_ = false;
        }
        return;
    }
    
    // Disable ImGui navigation when command bar is open (only save state once)
    // This ensures arrow keys and typing only control the command bar
    if (!navigationStateSaved_) {
        disableImGuiNavigation();
        navigationStateSaved_ = true;
    } else {
        // Keep navigation disabled (but don't overwrite saved state)
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    }
    
    // Draw command bar window
    // Note: SetNextWindowAffixedTop() is called inside CommandPaletteWindow, so we don't need to call it here
    // The window will close automatically when an item is selected or when it loses focus
    ImCmd::CommandPaletteWindow("Command Bar", &isOpen_);
}

void CommandBar::registerCommands() {
    if (commandsRegistered || !commandExecutor) {
        return; // Already registered or executor not set
    }
    
    // Clear any existing commands first
    ImCmd::RemoveAllCaches();
    
    // ========================================================================
    // DIRECT ACTIONS - Execute immediately without console output
    // ========================================================================
    
    // Register "Add Module" command with subcommand
    ImCmd::Command addCmd;
    addCmd.Name = "Add Module";
    addCmd.InitialCallback = [this]() {
        ImCmd::Prompt(std::vector<std::string>{"pool", "tracker"});
    };
    addCmd.SubsequentCallback = [this](int selected_option) {
        if (commandExecutor) {
            std::string moduleType = (selected_option == 0) ? "MultiSampler" : "TrackerSequencer";
            // Execute directly without console output
            commandExecutor->cmdAdd(moduleType);
            isOpen_ = false;
        }
    };
    ImCmd::AddCommand(addCmd);
    
    // Register "Remove Module" commands - one for each existing module
    auto moduleNames = commandExecutor->getAllModuleNames();
    for (const auto& moduleName : moduleNames) {
        ImCmd::Command removeCmd;
        removeCmd.Name = "Remove " + moduleName;
        removeCmd.InitialCallback = [this, moduleName]() {
            if (commandExecutor) {
                // Execute directly without console output
                commandExecutor->cmdRemove(moduleName);
                isOpen_ = false;
            }
        };
        ImCmd::AddCommand(removeCmd);
    }
    
    // Register "Route Module" command with subcommand
    ImCmd::Command routeCmd;
    routeCmd.Name = "Route Module";
    routeCmd.InitialCallback = [this]() {
        // Build list of module names for routing
        std::vector<std::string> options;
        if (commandExecutor) {
            auto moduleNames = commandExecutor->getAllModuleNames();
            for (const auto& name : moduleNames) {
                options.push_back(name);
            }
        }
        if (options.empty()) {
            options.push_back("(No modules available)");
        }
        ImCmd::Prompt(options);
    };
    routeCmd.SubsequentCallback = [this](int selected_option) {
        if (commandExecutor) {
            auto moduleNames = commandExecutor->getAllModuleNames();
            if (selected_option >= 0 && selected_option < static_cast<int>(moduleNames.size())) {
                std::string sourceName = moduleNames[selected_option];
                // Try to route to master outputs
                std::string routeArgs = sourceName + " masterAudioOut";
                // Execute directly without console output
                commandExecutor->cmdRoute(routeArgs);
                isOpen_ = false;
            }
        }
    };
    ImCmd::AddCommand(routeCmd);
    
    // ========================================================================
    // NAVIGATION - Focus on module GUI panels and Clock
    // ========================================================================
    
    // Register Clock navigation
    ImCmd::Command clockNavCmd;
    clockNavCmd.Name = "Clock";
    clockNavCmd.InitialCallback = [this]() {
        if (viewManager) {
            // Use generic window navigation - works for all panels
            viewManager->navigateToWindow("Clock ");
        }
        isOpen_ = false;
    };
    ImCmd::AddCommand(clockNavCmd);
    
    // Register navigation commands - one for each module (just type module name to focus)
    // Generic navigation works for ALL modules including masterVideoOut, masterAudioOut, etc.
    for (const auto& moduleName : moduleNames) {
        ImCmd::Command navCmd;
        navCmd.Name = moduleName;  // Just the module name for instant navigation
        navCmd.InitialCallback = [this, moduleName]() {
            if (viewManager) {
                // Generic navigation - works for ALL modules by window name
                viewManager->navigateToWindow(moduleName);
            }
            isOpen_ = false;
        };
        ImCmd::AddCommand(navCmd);
    }
    
    commandsRegistered = true;
    ofLogNotice("CommandBar") << "Commands registered: " << (3 + moduleNames.size() * 2) << " commands (includes Clock navigation)";
}

void CommandBar::unregisterCommands() {
    ImCmd::RemoveAllCaches();
    commandsRegistered = false;
}

void CommandBar::refreshCommands() {
    // If commands are registered, re-register them (useful when modules change)
    if (commandsRegistered) {
        commandsRegistered = false;  // Reset flag to force re-registration
        registerCommands();
    }
}



void CommandBar::disableImGuiNavigation() {
    ImGuiIO& io = ImGui::GetIO();
    
    // Save current navigation state
    previousNavKeyboardState_ = (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
    previousNavGamepadState_ = (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0;
    
    // Disable keyboard and gamepad navigation
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
}

void CommandBar::restoreImGuiNavigation() {
    ImGuiIO& io = ImGui::GetIO();
    
    // Restore previous navigation state
    if (previousNavKeyboardState_) {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }
    if (previousNavGamepadState_) {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    }
}

