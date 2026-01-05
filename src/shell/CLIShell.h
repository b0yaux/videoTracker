#pragma once

#include "Shell.h"
#include <iostream>
#include <string>
#include <fstream>
#include <vector>

namespace vt {
namespace shell {

/**
 * CLIShell - Batch CLI mode for non-interactive command execution
 * 
 * This shell provides command-line interface functionality:
 * - Reads commands from stdin (for piping/redirection)
 * - Executes commands from files (--cli flag with file path)
 * - Outputs results to stdout/stderr
 * - Exits after command execution
 * 
 * Usage:
 *   ./videoTracker --cli "list"
 *   ./videoTracker --cli commands.txt
 *   echo "list" | ./videoTracker --cli
 */
class CLIShell : public Shell {
public:
    CLIShell(Engine* engine, const std::string& commandOrFile = "");
    ~CLIShell() override;
    
    void setup() override;
    void update(float deltaTime) override;
    void draw() override;
    void exit() override;
    
    bool handleKeyPress(int key) override;
    
    std::string getName() const override { return "CLI"; }
    std::string getDescription() const override { return "Batch command-line interface"; }
    
    // Execute a single command and return result
    void executeCommand(const std::string& command);
    
    // Execute commands from stdin
    void executeFromStdin();
    
    // Execute commands from file
    void executeFromFile(const std::string& filePath);
    
    // Check if CLI shell should exit (after command execution)
    bool shouldExit() const { return shouldExit_; }
    
private:
    std::string commandOrFile_;  // Command string or file path
    bool executed_ = false;      // Whether commands have been executed
    bool shouldExit_ = false;     // Whether to exit after execution
    
    // Command execution helpers
    void printResult(const Engine::Result& result);
    void printError(const std::string& error);
    void printSuccess(const std::string& message);
};

} // namespace shell
} // namespace vt

