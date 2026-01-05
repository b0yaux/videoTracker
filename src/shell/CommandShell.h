#pragma once

#include "Shell.h"
#include <string>
#include <vector>
#include <deque>
#include "ofTrueTypeFont.h"
#include "ofFbo.h"

namespace vt {
namespace shell {

/**
 * CommandShell - Custom-rendered terminal interface (Hydra/Strudel style)
 * 
 * This shell provides a terminal-like REPL interface with full control:
 * - Hydra-style transparent overlay (F1 to toggle)
 * - Custom rendering with ofTrueTypeFont for precise control
 * - Always-active input (keyboard always routes to input line)
 * - Smooth text selection with mouse drag
 * - Transparent text backgrounds for readability
 * - Command history with arrow keys
 * - Tab completion for commands/modules
 * - Real-time output from Engine
 * 
 * Implementation: Custom rendering with:
 * - ofFbo for scrollable output area
 * - ofTrueTypeFont for text rendering
 * - Mouse events for text selection
 * - Keyboard input always active for input line
 * 
 * Toggle: F1
 */
class CommandShell : public Shell {
public:
    CommandShell(Engine* engine);
    ~CommandShell() override;
    
    void setup() override;
    void update(float deltaTime) override;
    void draw() override;
    void exit() override;
    
    bool handleKeyPress(int key) override;
    bool handleMousePress(int x, int y, int button) override;
    bool handleMouseDrag(int x, int y, int button) override;
    bool handleMouseRelease(int x, int y, int button) override;
    bool handleWindowResize(int w, int h) override;
    
    std::string getName() const override { return "Command"; }
    std::string getDescription() const override { return "Interactive command terminal for quick commands"; }
    
private:
    // Terminal state
    std::string currentInput_;              // Current input text
    std::vector<std::string> history_;      // Command history
    int historyPos_ = -1;                    // Current position in history
    int cursorPosition_ = 0;                 // Cursor position in input (character index)
    
    // Output history
    std::deque<std::string> outputLines_;    // Output lines
    
    // Rendering
    ofTrueTypeFont font_;                    // Monospace font for terminal
    ofFbo outputFbo_;                        // FBO for scrollable output area
    float charWidth_ = 0.0f;                 // Character width (monospace)
    float charHeight_ = 0.0f;                // Character height
    float lineSpacing_ = 0.0f;               // Line spacing (charHeight + extra)
    float padding_ = 20.0f;                  // Padding around terminal
    
    // Scrolling
    float scrollY_ = 0.0f;                   // Vertical scroll position
    float maxScrollY_ = 0.0f;                // Maximum scroll position
    bool shouldScrollToBottom_ = false;      // Flag to auto-scroll after output
    
    // Text selection
    bool isSelecting_ = false;               // Currently selecting text
    int selectionStartLine_ = -1;            // Selection start line index
    int selectionStartCol_ = -1;             // Selection start column
    int selectionEndLine_ = -1;              // Selection end line index
    int selectionEndCol_ = -1;               // Selection end column
    std::string selectedText_;               // Currently selected text
    
    // Terminal layout
    float terminalX_ = 0.0f;                  // Terminal X position
    float terminalY_ = 0.0f;                 // Terminal Y position
    float terminalWidth_ = 0.0f;              // Terminal width
    float terminalHeight_ = 0.0f;             // Terminal height
    float outputAreaHeight_ = 0.0f;          // Output area height (above input line)
    float inputLineY_ = 0.0f;                // Input line Y position
    
    // Command execution
    void executeCommand(const std::string& command);
    void appendOutput(const std::string& text);
    void appendError(const std::string& text);
    void resetInput();
    void clearOutput();  // Clear all output lines
    
    // History navigation
    void navigateHistory(int direction);
    void saveToHistory(const std::string& command);
    void loadHistoryEntry(int index);
    
    // Tab completion
    std::vector<std::string> getCompletions(const std::string& prefix);
    void completeCommand();
    
    // Rendering helpers
    void setupFont();
    void updateTerminalSize();
    void updateInputLinePosition();
    void renderTerminal();
    void drawChar(float x, float y, char c, const ofColor& color = ofColor::white);
    void drawString(float x, float y, const std::string& text, const ofColor& color = ofColor::white);
    void drawStringWithBackground(float x, float y, const std::string& text, 
                                   const ofColor& textColor = ofColor::white,
                                   const ofColor& bgColor = ofColor(0, 0, 0, 200));
    void drawSelection();
    
    // Text selection helpers
    void startSelection(int x, int y);
    void updateSelection(int x, int y);
    void endSelection();
    void clearSelection();
    std::string getSelectedText();
    void copyToClipboard(const std::string& text);
    void screenToTerminalPos(int screenX, int screenY, int& line, int& col);
    
    // Input handling
    void handleCharacterInput(char c);
    void handleBackspace();
    void handleDelete();
    void handleArrowKeys(int key);
    void handleEnter();
    void handleTab();
};

} // namespace shell
} // namespace vt

