#include "CommandShell.h"
#include "core/Engine.h"
#include "ofLog.h"
#include "ofGraphics.h"
#include "ofAppRunner.h"
#include <algorithm>
#include <sstream>
#include <cstring>

namespace vt {
namespace shell {

CommandShell::CommandShell(Engine* engine)
    : Shell(engine)
    , currentInput_("")
    , cursorPosition_(0)
    , charWidth_(0.0f)
    , charHeight_(0.0f)
    , lineSpacing_(0.0f)
    , padding_(20.0f)
    , scrollY_(0.0f)
    , maxScrollY_(0.0f)
    , isSelecting_(false)
    , selectionStartLine_(-1)
    , selectionStartCol_(-1)
    , selectionEndLine_(-1)
    , selectionEndCol_(-1)
{
}

CommandShell::~CommandShell() {
}

void CommandShell::setup() {
    // Initialize output with welcome message
    outputLines_.clear();
    outputLines_.push_back("VideoTracker - Command Shell");
    outputLines_.push_back("Type 'help' for commands, F3 to switch to Editor");
    currentInput_.clear();
    cursorPosition_ = 0;
    
    // Setup font first
    setupFont();
    
    // Initialize FBO with a default size (will be resized in updateTerminalSize)
    if (outputFbo_.getWidth() == 0 || outputFbo_.getHeight() == 0) {
        outputFbo_.allocate(800, 600, GL_RGBA);  // Default size
        ofLogNotice("CommandShell") << "FBO initialized with default size";
    }
    
    // Update terminal size (will resize FBO if needed)
    updateTerminalSize();
    
    // Update input line position after initial output is set
    updateInputLinePosition();
    
    ofLogNotice("CommandShell") << "Command shell setup complete";
    ofLogNotice("CommandShell") << "Font loaded: " << (font_.isLoaded() ? "yes" : "no");
    ofLogNotice("CommandShell") << "FBO size: " << outputFbo_.getWidth() << "x" << outputFbo_.getHeight();
}

void CommandShell::update(float deltaTime) {
    if (!active_) return;
    
    // Update terminal size if window resized
    updateTerminalSize();
    
    // Auto-scroll to bottom if needed
    if (shouldScrollToBottom_) {
        scrollY_ = maxScrollY_;
        shouldScrollToBottom_ = false;
    }
}

void CommandShell::draw() {
    if (!active_) return;
    
    // Render terminal
    renderTerminal();
}

void CommandShell::exit() {
    // Cleanup if needed
}

bool CommandShell::handleKeyPress(int key) {
    if (!active_) return false;
    
    // Check for modifier keys (Cmd/Ctrl) - use ofGetKeyPressed for current state
    // Note: This checks the current state, which should work for modifier combinations
    bool cmdPressed = ofGetKeyPressed(OF_KEY_COMMAND);
    bool ctrlPressed = ofGetKeyPressed(OF_KEY_CONTROL);
    bool cmdOrCtrlPressed = cmdPressed || ctrlPressed;
    
    // Copy to clipboard (Cmd+C / Ctrl+C)
    if (cmdOrCtrlPressed && (key == 'c' || key == 'C')) {
        if (!selectedText_.empty()) {
            copyToClipboard(selectedText_);
            clearSelection();  // Clear selection after copy
            return true;
        }
        return false;
    }
    
    // Handle special keys
    if (key == OF_KEY_RETURN || key == '\r' || key == '\n') {
        handleEnter();
        return true;
    }
    
    if (key == OF_KEY_BACKSPACE) {
        handleBackspace();
        return true;
    }
    
    if (key == OF_KEY_DEL) {
        handleDelete();
        return true;
    }
    
    if (key == OF_KEY_LEFT || key == OF_KEY_RIGHT || key == OF_KEY_UP || key == OF_KEY_DOWN) {
        handleArrowKeys(key);
        return true;
    }
    
    if (key == '\t') {
        handleTab();
        return true;
    }
    
    // Handle character input (printable characters)
    if (key >= 32 && key <= 126) {  // Printable ASCII range
        handleCharacterInput(static_cast<char>(key));
        return true;
    }
    
    return false;
}

bool CommandShell::handleMousePress(int x, int y, int button) {
    if (!active_) return false;
    
    // Check if click is within terminal bounds
    if (x >= terminalX_ && x <= terminalX_ + terminalWidth_ &&
        y >= terminalY_ && y <= terminalY_ + terminalHeight_) {
        
        if (button == 0) {  // Left mouse button
            // Clear previous selection
            clearSelection();
            startSelection(x, y);
            return true;
        }
    } else {
        // Click outside terminal - clear selection
        clearSelection();
    }
    
    return false;
}

bool CommandShell::handleMouseDrag(int x, int y, int button) {
    if (!active_) return false;
    
    if (isSelecting_ && button == 0) {
        updateSelection(x, y);
        return true;
    }
    
    return false;
}

bool CommandShell::handleMouseRelease(int x, int y, int button) {
    if (!active_) return false;
    
    if (isSelecting_ && button == 0) {
        endSelection();
        return true;
    }
    
    return false;
}

bool CommandShell::handleWindowResize(int w, int h) {
    updateTerminalSize();
    return true;
}

//--------------------------------------------------------------
// Command Execution
//--------------------------------------------------------------

void CommandShell::executeCommand(const std::string& command) {
    if (command.empty()) return;
    
    // Trim whitespace
    std::string trimmed = command;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
    trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);
    
    if (trimmed.empty()) {
        resetInput();
        return;
    }
    
    // Handle shell-specific commands first (before sending to Engine)
    std::string cmdLower = trimmed;
    std::transform(cmdLower.begin(), cmdLower.end(), cmdLower.begin(), ::tolower);
    
    if (cmdLower == "clear" || cmdLower == "cls") {
        // Clear command - handled locally by shell
        clearOutput();
        resetInput();
        return;
    }
    
    // Save to history
    saveToHistory(trimmed);
    
    // Add command to output (it was in the input area, now it becomes output)
    outputLines_.push_back("> " + trimmed);
    
    // Execute command via Engine
    Engine::Result result = engine_->executeCommand(trimmed);
    
    // Append result to output
    if (result.success) {
        if (!result.message.empty()) {
            appendOutput(result.message);
        }
    } else {
        appendError("ERROR: " + (result.error.empty() ? "Command failed" : result.error));
    }
    
    // Reset input for next command
    resetInput();
}

void CommandShell::appendOutput(const std::string& text) {
    // Split text into lines and add to outputLines_
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() || !outputLines_.empty()) {
            outputLines_.push_back(line);
        }
    }
    
    // Limit output history
    while (outputLines_.size() > 1000) {
        outputLines_.pop_front();
    }
    
    shouldScrollToBottom_ = true;
    updateTerminalSize();  // Recalculate scroll bounds
    updateInputLinePosition();  // Update input line position after new output
}

void CommandShell::appendError(const std::string& text) {
    appendOutput(text);
}

void CommandShell::resetInput() {
    // Clear current input
    currentInput_.clear();
    cursorPosition_ = 0;
    shouldScrollToBottom_ = true;
}

void CommandShell::clearOutput() {
    // Clear all output lines (but keep welcome message)
    outputLines_.clear();
    outputLines_.push_back("Command Shell - Interactive Terminal");
    outputLines_.push_back("Type 'help' for commands, F1 to toggle");
    
    // Reset scroll position
    scrollY_ = 0.0f;
    maxScrollY_ = 0.0f;
    
    // Clear selection
    clearSelection();
    
    // Update input line position
    updateInputLinePosition();
    updateTerminalSize();
}

//--------------------------------------------------------------
// History Navigation
//--------------------------------------------------------------

void CommandShell::navigateHistory(int direction) {
    if (history_.empty()) return;
    
    if (historyPos_ == -1) {
        // Starting history navigation - save current input
        std::string currentInput = currentInput_;
        if (!currentInput.empty() && (history_.empty() || history_.back() != currentInput)) {
            // Save current input as temporary history entry
            historyPos_ = static_cast<int>(history_.size());
        } else {
            historyPos_ = static_cast<int>(history_.size()) - 1;
        }
    } else {
        historyPos_ += direction;
    }
    
    // Clamp history position
    if (historyPos_ < 0) {
        historyPos_ = 0;
    } else if (historyPos_ >= static_cast<int>(history_.size())) {
        historyPos_ = static_cast<int>(history_.size()) - 1;
    }
    
    // Load history entry
    if (historyPos_ >= 0 && historyPos_ < static_cast<int>(history_.size())) {
        loadHistoryEntry(historyPos_);
    }
}

void CommandShell::saveToHistory(const std::string& command) {
    if (command.empty()) return;
    
    // Don't add duplicates (check last entry)
    if (history_.empty() || history_.back() != command) {
        history_.push_back(command);
    }
    
    // Limit history size
    if (history_.size() > 100) {
        history_.erase(history_.begin());
    }
    
    historyPos_ = -1;  // Reset history position
}

void CommandShell::loadHistoryEntry(int index) {
    if (index < 0 || index >= static_cast<int>(history_.size())) {
        return;
    }
    
    currentInput_ = history_[index];
    cursorPosition_ = static_cast<int>(currentInput_.length());
}

//--------------------------------------------------------------
// Tab Completion
//--------------------------------------------------------------

std::vector<std::string> CommandShell::getCompletions(const std::string& prefix) {
    std::vector<std::string> completions;
    
    // Basic command completions
    std::vector<std::string> commands = {
        "list", "add", "remove", "route", "unroute", "connections", 
        "help", "clear", "play", "stop", "bpm", "get", "set", "info", "import"
    };
    
    for (const auto& cmd : commands) {
        if (cmd.find(prefix) == 0) {
            completions.push_back(cmd);
        }
    }
    
    // TODO: Add module name completions from Engine
    // TODO: Add parameter path completions
    
    return completions;
}

void CommandShell::completeCommand() {
    std::string currentInput = currentInput_;
    if (currentInput.empty()) return;
    
    // Find the word to complete (last word)
    size_t start = currentInput.find_last_of(" \t");
    if (start == std::string::npos) {
        start = 0;
    } else {
        start++;  // Skip the space
    }
    
    std::string prefix = currentInput.substr(start);
    if (prefix.empty()) return;
    
    std::vector<std::string> completions = getCompletions(prefix);
    
    if (completions.empty()) {
        return;
    } else if (completions.size() == 1) {
        // Single completion - replace prefix
        std::string newInput = currentInput.substr(0, start) + completions[0];
        currentInput_ = newInput;
        cursorPosition_ = static_cast<int>(currentInput_.length());
    } else {
        // Multiple completions - show them
        appendOutput("\nCompletions:");
        for (const auto& comp : completions) {
            appendOutput("  " + comp);
        }
        resetInput();
    }
}

//--------------------------------------------------------------
// Rendering Helpers
//--------------------------------------------------------------

void CommandShell::setupFont() {
    // Try to load monospace font from common locations
    // Use ofToDataPath to ensure we're looking in the right place
    std::vector<std::string> fontPaths = {
        ofToDataPath("verdana.ttf", true),  // Try data directory first
        ofToDataPath("fonts/verdana.ttf", true),
        ofToDataPath("fonts/Inconsolata-Regular.ttf", true),
        "verdana.ttf",  // Try relative path
        "fonts/verdana.ttf"
    };
    
    bool fontLoaded = false;
    for (const auto& path : fontPaths) {
        font_.load(path, 14, true, true);
        if (font_.isLoaded()) {
            fontLoaded = true;
            ofLogNotice("CommandShell") << "Loaded font from: " << path;
            break;
        }
    }
    
    if (fontLoaded) {
        charWidth_ = font_.getStringBoundingBox("M", 0, 0).width;
        charHeight_ = font_.getStringBoundingBox("M", 0, 0).height;
        // Use font's line height which includes space for descenders
        // getLineHeight() returns the proper line spacing including descenders
        lineSpacing_ = font_.getLineHeight();
        if (lineSpacing_ <= 0) {
            // Fallback if getLineHeight() returns invalid value
            lineSpacing_ = charHeight_ * 1.2f;  // Add 20% for descenders
        }
    } else {
        // Fallback: Use ofDrawBitmapString dimensions
        // ofDrawBitmapString uses 8x14 pixels per character, but needs space for descenders
        charWidth_ = 8.0f;
        charHeight_ = 14.0f;
        lineSpacing_ = 16.0f;  // Add a bit more for descenders (14 + 2)
        ofLogWarning("CommandShell") << "Font loading failed, using ofDrawBitmapString fallback";
    }
}

void CommandShell::updateTerminalSize() {
    float screenWidth = ofGetWidth();
    float screenHeight = ofGetHeight();
    
    // Safety check: ensure we have valid screen dimensions
    if (screenWidth <= 0 || screenHeight <= 0) {
        ofLogWarning("CommandShell") << "Invalid screen dimensions, skipping size update";
        return;
    }
    
    terminalX_ = padding_;
    terminalY_ = padding_;
    terminalWidth_ = screenWidth - (padding_ * 2.0f);
    terminalHeight_ = screenHeight - (padding_ * 2.0f);
    
    // Ensure minimum sizes
    if (terminalWidth_ < 100.0f) terminalWidth_ = 100.0f;
    if (terminalHeight_ < 100.0f) terminalHeight_ = 100.0f;
    
    // Calculate output area height (full terminal height for scrolling)
    outputAreaHeight_ = terminalHeight_ - lineSpacing_;  // Reserve space for input line at bottom
    if (outputAreaHeight_ < 50.0f) outputAreaHeight_ = 50.0f;  // Minimum height
    
    // Calculate maximum scroll position
    float totalContentHeight = static_cast<float>(outputLines_.size()) * lineSpacing_;
    maxScrollY_ = std::max(0.0f, totalContentHeight - outputAreaHeight_);
    
    // Clamp scroll position
    if (scrollY_ > maxScrollY_) {
        scrollY_ = maxScrollY_;
    }
    if (scrollY_ < 0.0f) {
        scrollY_ = 0.0f;
    }
    
    // Update FBO size (only if dimensions changed and are valid)
    int fboWidth = static_cast<int>(terminalWidth_);
    int fboHeight = static_cast<int>(outputAreaHeight_);
    if (fboWidth > 0 && fboHeight > 0) {
        if (outputFbo_.getWidth() != fboWidth || outputFbo_.getHeight() != fboHeight) {
            outputFbo_.allocate(fboWidth, fboHeight, GL_RGBA);
            ofLogNotice("CommandShell") << "FBO allocated: " << fboWidth << "x" << fboHeight;
        }
    }
}

void CommandShell::updateInputLinePosition() {
    // Calculate input line Y position: right after last output line, clamped to bottom
    if (outputLines_.empty()) {
        // No output yet - position at top
        inputLineY_ = terminalY_;
    } else {
        // Calculate position of last output line (in screen coordinates)
        // The last line is at index (outputLines_.size() - 1)
        float lastOutputLineIndex = static_cast<float>(outputLines_.size() - 1);
        
        // Calculate Y position in FBO coordinates (accounting for scroll)
        float lastOutputLineYInFBO = lastOutputLineIndex * lineSpacing_ - scrollY_;
        
        // Convert to screen coordinates
        float lastOutputLineYInScreen = terminalY_ + lastOutputLineYInFBO;
        
        // Input line should be right after last output line
        float desiredInputY = lastOutputLineYInScreen + lineSpacing_;
        
        // Clamp to bottom of terminal (but allow it to be above if there's little output)
        float maxInputY = terminalY_ + terminalHeight_ - lineSpacing_;
        inputLineY_ = std::min(desiredInputY, maxInputY);
        
        // Ensure input line is never above the first output line
        if (!outputLines_.empty()) {
            float firstOutputLineY = terminalY_ - scrollY_;
            inputLineY_ = std::max(inputLineY_, firstOutputLineY + lineSpacing_);
        }
    }
}

void CommandShell::renderTerminal() {
    // Safety check: ensure FBO is allocated
    if (outputFbo_.getWidth() == 0 || outputFbo_.getHeight() == 0) {
        ofLogWarning("CommandShell") << "FBO not allocated yet, skipping render";
        return;
    }
    
    // Note: Font doesn't need to be loaded - we use ofDrawBitmapString as fallback
    
    // Save OpenGL state before rendering (important to not affect viewport)
    ofPushStyle();
    ofPushMatrix();
    ofPushView();
    
    // Draw output area with scrolling
    outputFbo_.begin();
    ofClear(0, 0, 0, 0);  // Transparent background
    
    float y = -scrollY_;  // Start at scroll position
    
    // Draw output lines with transparent backgrounds
    for (size_t i = 0; i < outputLines_.size(); ++i) {
        float lineY = y + (static_cast<float>(i) * lineSpacing_);
        
        // Only draw if line is visible
        if (lineY + lineSpacing_ >= 0 && lineY <= outputAreaHeight_) {
            const std::string& line = outputLines_[i];
            
            // Draw transparent background for line
            drawStringWithBackground(0.0f, lineY, line, ofColor::white, ofColor(0, 0, 0, 200));
        }
    }
    
    // Draw selection highlight (only if actively selecting or has valid selection)
    if (isSelecting_ || (!selectedText_.empty() && selectionStartLine_ >= 0)) {
        drawSelection();
    }
    
    outputFbo_.end();
    
    // Restore viewport to full window (critical for viewport rendering)
    ofViewport(0, 0, ofGetWidth(), ofGetHeight());
    
    // Draw FBO to screen
    ofSetColor(255, 255, 255, 255);
    outputFbo_.draw(terminalX_, terminalY_);
    
    // Draw input line (directly below output, continuous flow)
    float inputY = inputLineY_;
    
    // Draw prompt with transparent background
    std::string prompt = "> ";
    drawStringWithBackground(terminalX_, inputY, prompt, ofColor(100, 255, 100), ofColor(0, 50, 0, 200));
    
    // Draw input text with transparent background
    if (!currentInput_.empty()) {
        float promptWidth = font_.isLoaded() 
            ? font_.getStringBoundingBox(prompt, 0, 0).width 
            : prompt.length() * charWidth_;
        float inputX = terminalX_ + promptWidth;
        drawStringWithBackground(inputX, inputY, currentInput_, ofColor::white, ofColor(0, 0, 0, 200));
    }
    
    // Draw cursor (blinking)
    static float cursorTime = 0.0f;
    cursorTime += ofGetLastFrameTime();
    bool showCursor = (static_cast<int>(cursorTime * 2.0f) % 2) == 0;
    
    if (showCursor) {
        float promptWidth = font_.isLoaded() 
            ? font_.getStringBoundingBox(prompt, 0, 0).width 
            : prompt.length() * charWidth_;
        std::string inputBeforeCursor = currentInput_.substr(0, cursorPosition_);
        float inputWidth = font_.isLoaded()
            ? font_.getStringBoundingBox(inputBeforeCursor, 0, 0).width
            : inputBeforeCursor.length() * charWidth_;
        float cursorX = terminalX_ + promptWidth + inputWidth;
        float cursorY = inputY + charHeight_;
        
        ofSetColor(255, 255, 255, 255);
        ofDrawLine(cursorX, inputY, cursorX, cursorY);
    }
    
    // Restore OpenGL state (critical to not affect viewport rendering)
    ofPopView();
    ofPopMatrix();
    ofPopStyle();
}

void CommandShell::drawChar(float x, float y, char c, const ofColor& color) {
    ofSetColor(color);
    if (font_.isLoaded()) {
        font_.drawString(std::string(1, c), x, y + charHeight_);
    } else {
        // Fallback to ofDrawBitmapString
        ofDrawBitmapString(std::string(1, c), x, y + charHeight_);
    }
}

void CommandShell::drawString(float x, float y, const std::string& text, const ofColor& color) {
    ofSetColor(color);
    if (font_.isLoaded()) {
        font_.drawString(text, x, y + charHeight_);
    } else {
        // Fallback to ofDrawBitmapString
        ofDrawBitmapString(text, x, y + charHeight_);
    }
}

void CommandShell::drawStringWithBackground(float x, float y, const std::string& text, 
                                              const ofColor& textColor,
                                              const ofColor& bgColor) {
    if (text.empty()) return;
    
    // Calculate text width
    float textWidth;
    if (font_.isLoaded()) {
        // Get bounding box at the text drawing position (y + charHeight_)
        ofRectangle textBounds = font_.getStringBoundingBox(text, x, y + charHeight_);
        textWidth = textBounds.width;
    } else {
        // Estimate for ofDrawBitmapString (8 pixels per character)
        textWidth = text.length() * charWidth_;
    }
    
    // Draw background - cover full line height including descenders
    // Background starts at y (line start) and covers full lineSpacing_ height
    ofSetColor(bgColor);
    ofDrawRectangle(x, y, textWidth, lineSpacing_);
    
    // Draw text - text is drawn at y + charHeight_ (baseline position)
    drawString(x, y, text, textColor);
}

void CommandShell::drawSelection() {
    if (selectionStartLine_ < 0 || selectionEndLine_ < 0) return;
    
    // Normalize selection (start before end)
    int startLine = std::min(selectionStartLine_, selectionEndLine_);
    int endLine = std::max(selectionStartLine_, selectionEndLine_);
    int startCol = (startLine == selectionStartLine_) ? selectionStartCol_ : selectionEndCol_;
    int endCol = (endLine == selectionEndLine_) ? selectionEndCol_ : selectionStartCol_;
    
    // Draw selection highlight
    ofSetColor(50, 150, 255, 150);  // Semi-transparent blue
    
    for (int line = startLine; line <= endLine; ++line) {
        if (line < 0 || line >= static_cast<int>(outputLines_.size())) continue;
        
        const std::string& lineText = outputLines_[line];
        int lineStartCol = (line == startLine) ? startCol : 0;
        int lineEndCol = (line == endLine) ? endCol : static_cast<int>(lineText.length());
        
        if (lineStartCol >= lineEndCol) continue;
        
        // Calculate selection rectangle (in FBO coordinates)
        std::string beforeStart = lineText.substr(0, lineStartCol);
        std::string selected = lineText.substr(lineStartCol, lineEndCol - lineStartCol);
        
        float x = font_.isLoaded()
            ? font_.getStringBoundingBox(beforeStart, 0, 0).width
            : beforeStart.length() * charWidth_;
        float y = static_cast<float>(line) * lineSpacing_ - scrollY_;  // Account for scroll
        float width = font_.isLoaded()
            ? font_.getStringBoundingBox(selected, 0, 0).width
            : selected.length() * charWidth_;
        float height = lineSpacing_;
        
        // Only draw if visible in FBO
        if (y + height >= 0 && y <= outputAreaHeight_) {
            ofDrawRectangle(x, y, width, height);
        }
    }
}

//--------------------------------------------------------------
// Text Selection Helpers
//--------------------------------------------------------------

void CommandShell::startSelection(int x, int y) {
    // Convert screen coordinates to terminal coordinates
    int line, col;
    screenToTerminalPos(x, y, line, col);
    
    if (line >= 0 && line < static_cast<int>(outputLines_.size())) {
        isSelecting_ = true;
        selectionStartLine_ = line;
        selectionStartCol_ = col;
        selectionEndLine_ = line;
        selectionEndCol_ = col;
        selectedText_.clear();
    }
}

void CommandShell::updateSelection(int x, int y) {
    if (!isSelecting_) return;
    
    // Convert screen coordinates to terminal coordinates
    int line, col;
    screenToTerminalPos(x, y, line, col);
    
    if (line >= 0 && line < static_cast<int>(outputLines_.size())) {
        selectionEndLine_ = line;
        selectionEndCol_ = col;
        
        // Update selected text
        selectedText_ = getSelectedText();
    }
}

void CommandShell::endSelection() {
    isSelecting_ = false;
    
    // Finalize selected text
    selectedText_ = getSelectedText();
}

void CommandShell::clearSelection() {
    isSelecting_ = false;
    selectionStartLine_ = -1;
    selectionStartCol_ = -1;
    selectionEndLine_ = -1;
    selectionEndCol_ = -1;
    selectedText_.clear();
}

std::string CommandShell::getSelectedText() {
    if (selectionStartLine_ < 0 || selectionEndLine_ < 0) return "";
    
    // Normalize selection
    int startLine = std::min(selectionStartLine_, selectionEndLine_);
    int endLine = std::max(selectionStartLine_, selectionEndLine_);
    int startCol = (startLine == selectionStartLine_) ? selectionStartCol_ : selectionEndCol_;
    int endCol = (endLine == selectionEndLine_) ? selectionEndCol_ : selectionStartCol_;
    
    std::string result;
    
    for (int line = startLine; line <= endLine; ++line) {
        if (line < 0 || line >= static_cast<int>(outputLines_.size())) continue;
        
        const std::string& lineText = outputLines_[line];
        int lineStartCol = (line == startLine) ? startCol : 0;
        int lineEndCol = (line == endLine) ? endCol : static_cast<int>(lineText.length());
        
        if (lineStartCol < lineEndCol && lineStartCol < static_cast<int>(lineText.length())) {
            int actualEndCol = std::min(lineEndCol, static_cast<int>(lineText.length()));
            result += lineText.substr(lineStartCol, actualEndCol - lineStartCol);
        }
        
        if (line < endLine) {
            result += "\n";
        }
    }
    
    return result;
}

void CommandShell::copyToClipboard(const std::string& text) {
    // Use openFrameworks clipboard functionality
    if (ofGetWindowPtr()) {
        ofGetWindowPtr()->setClipboardString(text);
        ofLogNotice("CommandShell") << "Copied to clipboard: " << text.substr(0, 50) << "...";
    }
}

void CommandShell::screenToTerminalPos(int screenX, int screenY, int& line, int& col) {
    
    // Convert screen coordinates to terminal coordinates
    float localX = screenX - terminalX_;
    float localY = screenY - terminalY_;
    
    // Check if within output area
    if (localY < 0 || localY > outputAreaHeight_) {
        line = -1;
        col = -1;
        return;
    }
    
    // Calculate line (accounting for scroll)
    float scrolledY = localY + scrollY_;
    line = static_cast<int>(scrolledY / lineSpacing_);
    
    // Clamp line to valid range
    if (line < 0 || line >= static_cast<int>(outputLines_.size())) {
        line = -1;
        col = -1;
        return;
    }
    
    // Calculate column from X position
    // Find which character the X position corresponds to
    const std::string& lineText = outputLines_[line];
    float x = 0.0f;
    col = 0;
    
    for (size_t i = 0; i < lineText.length(); ++i) {
        float charWidth = font_.isLoaded()
            ? font_.getStringBoundingBox(std::string(1, lineText[i]), 0, 0).width
            : charWidth_;  // Use fixed width for bitmap font
        if (localX < x + charWidth / 2.0f) {
            break;
        }
        x += charWidth;
        col++;
    }
    
    // Clamp column
    col = std::max(0, std::min(col, static_cast<int>(lineText.length())));
}

//--------------------------------------------------------------
// Input Handling
//--------------------------------------------------------------

void CommandShell::handleCharacterInput(char c) {
    // Insert character at cursor position
    currentInput_.insert(cursorPosition_, 1, c);
    cursorPosition_++;
}

void CommandShell::handleBackspace() {
    if (cursorPosition_ > 0) {
        currentInput_.erase(cursorPosition_ - 1, 1);
        cursorPosition_--;
    }
}

void CommandShell::handleDelete() {
    if (cursorPosition_ < static_cast<int>(currentInput_.length())) {
        currentInput_.erase(cursorPosition_, 1);
    }
}

void CommandShell::handleArrowKeys(int key) {
    if (key == OF_KEY_LEFT) {
        if (cursorPosition_ > 0) {
            cursorPosition_--;
        }
    } else if (key == OF_KEY_RIGHT) {
        if (cursorPosition_ < static_cast<int>(currentInput_.length())) {
            cursorPosition_++;
        }
    } else if (key == OF_KEY_UP) {
        navigateHistory(-1);
    } else if (key == OF_KEY_DOWN) {
        navigateHistory(1);
    }
}

void CommandShell::handleEnter() {
    if (!currentInput_.empty()) {
        executeCommand(currentInput_);
    }
}

void CommandShell::handleTab() {
    completeCommand();
}

} // namespace shell
} // namespace vt
