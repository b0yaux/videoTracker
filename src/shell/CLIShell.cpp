#include "CLIShell.h"
#include "core/Engine.h"
#include "ofLog.h"
#include <sstream>
#include <algorithm>

namespace vt {
namespace shell {

CLIShell::CLIShell(Engine* engine, const std::string& commandOrFile)
    : Shell(engine)
    , commandOrFile_(commandOrFile)
{
    // CLI shell is always active when created (runs once and exits)
    setActive(true);
}

CLIShell::~CLIShell() {
}

void CLIShell::setup() {
    // Execute commands immediately on setup
    if (!commandOrFile_.empty()) {
        // Check if it's a file path or a command
        std::ifstream file(commandOrFile_);
        if (file.good()) {
            // It's a file
            file.close();
            executeFromFile(commandOrFile_);
        } else {
            // It's a command string
            executeCommand(commandOrFile_);
        }
        executed_ = true;
        shouldExit_ = true;
    } else {
        // No command/file provided - read from stdin
        executeFromStdin();
        executed_ = true;
        shouldExit_ = true;
    }
}

void CLIShell::update(float deltaTime) {
    if (!active_) return;
    
    // CLI shell doesn't need continuous updates
    // Commands are executed once in setup()
}

void CLIShell::draw() {
    // CLI shell doesn't render anything
    // All output goes to stdout/stderr
}

void CLIShell::exit() {
    // Cleanup if needed
}

bool CLIShell::handleKeyPress(int key) {
    // CLI shell doesn't handle interactive key presses
    return false;
}

void CLIShell::executeCommand(const std::string& command) {
    if (command.empty()) return;
    
    // Trim whitespace
    std::string trimmed = command;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
    trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);
    
    if (trimmed.empty()) return;
    
    // Execute command via Engine
    Engine::Result result = engine_->executeCommand(trimmed);
    printResult(result);
}

void CLIShell::executeFromStdin() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        
        // Skip comments
        if (line[0] == '#' || (line.length() > 1 && line[0] == '/' && line[1] == '/')) {
            continue;
        }
        
        executeCommand(line);
        
        // Exit on error if desired (can be made configurable)
        // For now, continue executing all commands
    }
}

void CLIShell::executeFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        printError("Failed to open file: " + filePath);
        return;
    }
    
    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        lineNumber++;
        
        if (line.empty()) continue;
        
        // Skip comments
        if (line[0] == '#' || (line.length() > 1 && line[0] == '/' && line[1] == '/')) {
            continue;
        }
        
        executeCommand(line);
    }
    
    file.close();
}

void CLIShell::printResult(const Engine::Result& result) {
    if (result.success) {
        if (!result.message.empty()) {
            printSuccess(result.message);
        }
    } else {
        printError(result.error.empty() ? result.message : result.error);
    }
}

void CLIShell::printError(const std::string& error) {
    std::cerr << "ERROR: " << error << std::endl;
}

void CLIShell::printSuccess(const std::string& message) {
    std::cout << message << std::endl;
}

} // namespace shell
} // namespace vt

