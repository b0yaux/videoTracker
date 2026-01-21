#pragma once

#include "Shell.h"
#include "CommandShell.h"
#include "core/EngineState.h"
#include <atomic>
#include <memory>
#include <string>

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
    
    // Script synchronization with EditorMode enum (simplified to 2 states)
    enum class EditorMode { VIEW, EDIT };
    EditorMode editorMode_ = EditorMode::VIEW;  // Current editor mode
    std::string userEditBuffer_;  // User's edits (not yet applied)
    bool wasActive_ = false;  // Track previous active state to detect activation
    
    // Deferred script update (to prevent crashes during script execution or ImGui rendering)
    // Thread-safe: protected by mutex for cross-thread access (callback writes, update() reads)
    std::string pendingScriptUpdate_;
    uint64_t pendingScriptVersion_ = 0;
    std::atomic<bool> hasPendingScriptUpdate_{false};
    std::mutex pendingUpdateMutex_;  // Protects pendingScriptUpdate_ and pendingScriptVersion_
    uint64_t lastAppliedVersion_ = 0;  // Track last applied state version for update safety
    uint64_t lastDeferredVersionWarning_ = 0;
    
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
    bool autoEvalEnabled_ = false;  // Disabled by default - user must opt-in
    bool autoEvalLoggedDisabled_ = false;
    std::string autoEvalDisableReason_ = "Auto-evaluation disabled by default (press Ctrl+Shift+A to enable)";
    std::string lastEditorText_;  // Track text changes (what user typed, may not be executed yet)
    std::string lastExecutedScript_;  // Track executed script (for diffing with current text)
    
    // Exit guard - prevents callback from accessing object during destruction
    std::atomic<bool> isExiting_{false};
    
    // Incremental execution configuration
    int maxIncrementalLines_ = 3;  // Max lines to execute incrementally (fallback to full execution if more)
    bool incrementalEvalEnabled_ = true;  // Enable incremental execution (use incremental for small changes)
    
    // Script execution tracking to prevent infinite retry loops (Phase 7.4)
    struct ScriptExecutionTracker {
        std::string lastFailedScriptHash;
        uint64_t lastFailureTimeMs{0};
        int consecutiveFailures{0};
        
        static constexpr int MAX_CONSECUTIVE_FAILURES = 3;
        static constexpr uint64_t FAILURE_COOLDOWN_MS = 2000;
        
        // Check if we should retry executing this script
        bool shouldRetry(const std::string& scriptHash, uint64_t nowMs) const {
            if (scriptHash != lastFailedScriptHash) {
                return true;  // Different script, try it
            }
            if (consecutiveFailures >= MAX_CONSECUTIVE_FAILURES) {
                return false;  // Too many failures, wait for script change
            }
            if (nowMs - lastFailureTimeMs < FAILURE_COOLDOWN_MS) {
                return false;  // Still in cooldown
            }
            return true;  // Retry allowed
        }
        
        void recordSuccess() {
            lastFailedScriptHash.clear();
            consecutiveFailures = 0;
            lastFailureTimeMs = 0;
        }
        
        void recordFailure(const std::string& scriptHash, uint64_t nowMs) {
            if (scriptHash == lastFailedScriptHash) {
                consecutiveFailures++;
            } else {
                lastFailedScriptHash = scriptHash;
                consecutiveFailures = 1;
            }
            lastFailureTimeMs = nowMs;
        }
        
        void reset() {
            lastFailedScriptHash.clear();
            consecutiveFailures = 0;
            lastFailureTimeMs = 0;
        }
    };
    ScriptExecutionTracker executionTracker_;
    
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
    
    // Copy-on-read helper to prevent use-after-free crashes
    // GetTextLines() returns a reference that becomes invalid when SetText() is called
    std::vector<std::string> getTextLinesCopy() const;
    
    // State change handler (override from Shell base class)
    void onStateChanged(const EngineState& state, uint64_t stateVersion) override;
};

} // namespace shell
} // namespace vt

