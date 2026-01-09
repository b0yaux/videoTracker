#pragma once

#include "Shell.h"
#include "CommandShell.h"
#include <memory>

// Forward declaration - full definition in .cpp
class TextEditor;

namespace vt {
namespace shell {

/**
 * CodeShell - Live-coding shell with code editor and REPL (Strudel/Tidal/Hydra style)
 * 
 * This shell provides a live-coding environment:
 * - Code editor (ImGuiColorTextEdit) with Lua syntax highlighting
 * - Embedded REPL output (CommandShell) for results
 * - Split view (editor top, REPL bottom)
 * - Ctrl+Enter: Execute selection or current line
 * - Ctrl+R: Execute entire script (always full execution)
 * - Ctrl+Shift+A: Toggle auto-evaluation
 * - Auto-evaluation: Enabled by default, uses incremental execution for small changes
 * - Incremental execution: Only executes changed lines/blocks, preserves state from unchanged code
 * - Clipboard shortcuts: cmd+A (select all), cmd+C (copy), cmd+X (cut), cmd+V (paste)
 * - Error marking in editor
 * - Auto-sync with Engine state via ScriptManager
 * 
 * **Auto-Evaluation Safety:**
 * - Idempotent functions allow safe repeated execution
 * - Incremental execution only executes changed code (preserves state)
 * - Safety checks prevent execution during unsafe periods (script execution, command processing)
 * - Falls back to full execution for large changes (>3 lines)
 * 
 * **Current State**: Displays session reconstruction scripts (command-based).
 * **Future**: Will support declarative live-coding syntax once Pattern DSL compiler
 * and high-level Lua API are implemented.
 * 
 * Toggle: F2
 * 
 * See docs/SCRIPTING_ARCHITECTURE_ANALYSIS.md for current state and roadmap.
 */
class CodeShell : public Shell {
public:
    CodeShell(Engine* engine);
    ~CodeShell() override;
    
    void setup() override;
    void update(float deltaTime) override;
    void draw() override;
    void exit() override;
    
    // Override setActive to track activation/deactivation
    void setActive(bool active) override;
    
    bool handleKeyPress(int key) override;
    bool handleMousePress(int x, int y, int button) override;
    bool handleMouseDrag(int x, int y, int button) override;
    bool handleMouseRelease(int x, int y, int button) override;
    bool handleWindowResize(int w, int h) override;
    
    std::string getName() const override { return "Code"; }
    std::string getDescription() const override { return "Live-coding shell with Lua editor and REPL"; }
    
private:
    // Code editor (using pointer to avoid include in header)
    std::unique_ptr<TextEditor> codeEditor_;
    bool editorInitialized_ = false;
    
    // Embedded REPL shell
    std::unique_ptr<CommandShell> replShell_;
    
    // Split view
    float editorHeightRatio_ = 0.6f;  // 60% editor, 40% REPL
    float splitterHeight_ = 4.0f;
    bool isResizing_ = false;
    float resizeStartY_ = 0.0f;
    float resizeStartRatio_ = 0.0f;
    
    // Script synchronization (SIMPLIFIED)
    bool hasManualEdits_ = false;  // If true, user owns editor - no auto-sync
    bool wasActive_ = false;  // Track previous active state to detect activation
    bool isExecutingScript_ = false;  // Guard against concurrent access during script execution
    
    // Deferred script update (to prevent crashes during script execution or ImGui rendering)
    std::string pendingScriptUpdate_;
    bool hasPendingScriptUpdate_ = false;
    
    // Helper to refresh script from state (called only on activation)
    void refreshScriptFromState();
    
    // Auto-evaluation (debounced)
    // SAFE: Enabled by default with idempotent functions and incremental execution
    // - Idempotent functions allow safe repeated execution
    // - Incremental execution only executes changed lines/blocks (preserves state)
    // - Safety checks prevent execution during unsafe periods
    // Users can still execute manually with Ctrl+R (all) or Ctrl+Enter (selection/line)
    float lastEditTime_ = 0.0f;
    float autoEvalDebounce_ = 0.5f;  // 500ms debounce
    bool autoEvalEnabled_ = true;  // Enabled by default - now safe with idempotency + incremental execution
    std::string lastEditorText_;  // Track text changes (what user typed, may not be executed yet)
    std::string lastExecutedScript_;  // Track executed script (for diffing with current text)
    
    // Incremental execution configuration
    int maxIncrementalLines_ = 3;  // Max lines to execute incrementally (fallback to full execution if more)
    bool incrementalEvalEnabled_ = true;  // Enable incremental execution (use incremental for small changes)
    
    // Execution
    void executeSelection();
    void executeAll();
    void executeLuaScript(const std::string& script);
    void executeLine(int lineNumber);
    void executeChangedLines(const std::vector<int>& changedLines);
    void executeBlock(int startLine, int endLine);
    void markErrorInEditor(int line, const std::string& message);
    void clearErrors();
    
    // Helper to parse error line from Lua error message
    int parseErrorLine(const std::string& errorMessage);
    
    // Smart evaluation helpers
    bool isSimpleParameterChange(const std::string& line);
    bool isBPMChange(const std::string& line);
    void checkAndExecuteSimpleChanges();
    
    // Change detection for incremental execution
    struct Block {
        int startLine;
        int endLine;
        enum Type { FUNCTION, PATTERN, VARIABLE, COMMAND, UNKNOWN };
        Type type;
        std::string content;
    };
    
    std::vector<int> detectChangedLines(const std::string& oldScript, const std::string& newScript);
    std::vector<Block> detectChangedBlocks(const std::string& oldScript, const std::string& newScript);
    std::vector<Block> parseScriptBlocks(const std::string& script);
    
    // Helper to distinguish user input from command keys
    bool isUserInput(int key) const;
    
    // State change handler (override from Shell base class)
    void onStateChanged(const EngineState& state) override;
    
    // Cached state for thread-safe access
    EngineState cachedState_;
};

} // namespace shell
} // namespace vt

