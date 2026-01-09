#include "CodeShell.h"
#include "core/Engine.h"
#include "ofLog.h"
#include "ofGraphics.h"
#include "ofAppRunner.h"
#include "TextEditor.h"
#include <imgui.h>
#include <sstream>
#include <regex>
#include <cmath>
#include <fstream>
#include <chrono>
#include <vector>
#include <algorithm>
#include <csignal>
#include <execinfo.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <thread>
#include <unistd.h>  // For STDERR_FILENO on macOS

namespace vt {
namespace shell {

// CRITICAL: Signal handler for crash debugging
static void crashHandler(int sig) {
    void *array[50];
    size_t size = backtrace(array, 50);
    
    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/crash.log", std::ios::app);
    if (logFile.is_open()) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        logFile << "=== CRASH DETECTED (signal " << sig << ") at " << now << " ===\n";
        logFile << "Stack trace:\n";
        
        char** messages = backtrace_symbols(array, size);
        for (size_t i = 0; i < size; i++) {
            logFile << "  [" << i << "] " << (messages[i] ? messages[i] : "???") << "\n";
        }
        free(messages);
        logFile << "=== END CRASH LOG ===\n\n";
        logFile.flush();
        logFile.close();
    }
    
    // Also write to stderr
    fprintf(stderr, "CRASH: signal %d\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    
    // Re-raise signal to get default behavior
    signal(sig, SIG_DFL);
    raise(sig);
}

CodeShell::CodeShell(Engine* engine)
    : Shell(engine)
    , codeEditor_(std::make_unique<TextEditor>())
{
    // Install crash handlers for debugging
    signal(SIGSEGV, crashHandler);
    signal(SIGABRT, crashHandler);
    signal(SIGBUS, crashHandler);
}

CodeShell::~CodeShell() = default;

void CodeShell::setup() {
    // Call parent setup() first to subscribe to state changes
    Shell::setup();
    
    if (observerId_ > 0) {
        ofLogNotice("CodeShell") << "Subscribed to state changes (ID: " << observerId_ << ")";
    }
    
    if (!codeEditor_) {
        ofLogError("CodeShell") << "Code editor not initialized in setup()";
        return;
    }
    
    // Initialize code editor with Lua language definition
    codeEditor_->SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
    
    // Create custom palette with transparent background for aesthetic transparency
    auto darkPalette = TextEditor::GetDarkPalette();
    // Make background fully transparent (alpha = 0)
    darkPalette[(int)TextEditor::PaletteIndex::Background] = IM_COL32(0, 0, 0, 0);
    codeEditor_->SetPalette(darkPalette);
    
    codeEditor_->SetShowWhitespaces(false);
    codeEditor_->SetTabSize(4);
    
    // Enable input handling
    codeEditor_->SetHandleKeyboardInputs(true);
    codeEditor_->SetHandleMouseInputs(true);
    
    // Register with ScriptManager for auto-sync (using safe API)
    if (engine_) {
        // Use safe API method instead of direct ScriptManager access
        engine_->setScriptUpdateCallback([this](const std::string& script) {
            // CRITICAL: Log immediately on callback entry
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"CodeShell.cpp:callback\",\"message\":\"CALLBACK ENTRY\",\"data\":{\"scriptLength\":" << script.length() << ",\"hasManualEdits\":" << (hasManualEdits_ ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            
            // CRITICAL FIX: Defer script updates to prevent crashes
            // Don't call SetText() directly from callback - it might be called during
            // script execution, ImGui rendering, or from unsafe contexts
            // Instead, store the script and apply it in update() when safe
            
            // Check if we should update (user hasn't manually edited)
            if (hasManualEdits_) {
                ofLogVerbose("CodeShell") << "Script update deferred - user has manual edits";
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"CodeShell.cpp:callback\",\"message\":\"CALLBACK EXIT - manual edits\",\"data\":{},\"timestamp\":" << now << "}\n";
                        logFile.flush();
                        logFile.close();
                    }
                }
                return;  // Don't update if user has edited
            }
            
            // Defer the update - will be applied in update() when safe
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"CodeShell.cpp:callback\",\"message\":\"BEFORE setting pendingScriptUpdate_\",\"data\":{},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            
            pendingScriptUpdate_ = script;
            hasPendingScriptUpdate_ = true;
            
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"CRASH\",\"location\":\"CodeShell.cpp:callback\",\"message\":\"CALLBACK EXIT - SUCCESS\",\"data\":{},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
        });
        
        // CRITICAL FIX: Don't try to load script from state here
        // The callback will be called immediately by ScriptManager::setScriptUpdateCallback()
        // if a script already exists, or later when the state observer fires
        // This ensures we always get the script through the callback mechanism
        // 
        // If no script is available yet, the editor will remain empty until
        // ScriptManager generates one (which happens in setup() after session load)
        // The callback will then populate the editor automatically
        ofLogNotice("CodeShell") << "Callback registered - script will be populated via callback";
    } else {
        // Fallback if engine not available
        std::string defaultScript = "-- videoTracker Lua Script\n";
        defaultScript += "-- Press Ctrl+Enter to execute selection, Ctrl+R to execute all\n";
        defaultScript += "-- Press Ctrl+Shift+A to toggle auto-evaluation\n\n";
        defaultScript += "local clock = engine:getClock()\n";
        defaultScript += "clock:setBPM(120)\n";
        defaultScript += "clock:play()\n";
        codeEditor_->SetText(defaultScript);
    }
    
    // Verify initialization
    if (codeEditor_->GetTotalLines() > 0) {
        editorInitialized_ = true;
        ofLogNotice("CodeShell") << "Code editor initialized with " << codeEditor_->GetTotalLines() << " lines";
    } else {
        ofLogError("CodeShell") << "Failed to initialize code editor text";
    }
    
    // Create embedded REPL shell
    replShell_ = std::make_unique<CommandShell>(engine_);
    replShell_->setEmbeddedMode(true);
    replShell_->setup();
    
    ofLogNotice("CodeShell") << "Code shell setup complete";
}

void CodeShell::update(float deltaTime) {
    if (!active_) return;
    
    // Update embedded REPL shell
    if (replShell_) {
        replShell_->update(deltaTime);
    }
    
    // CRITICAL FIX: Ensure TextEditor is initialized (but never in draw()!)
    // If editor is empty, initialize it here (safe to do in update())
    if (codeEditor_ && !editorInitialized_ && codeEditor_->GetTotalLines() == 0) {
        std::string defaultScript = "-- videoTracker Lua Script\n";
        defaultScript += "-- Press Ctrl+Enter to execute\n";
        defaultScript += "-- Press Ctrl+R to execute all\n";
        defaultScript += "-- Press Ctrl+Shift+A to toggle auto-evaluation\n";
        defaultScript += "\n";
        defaultScript += "clock:setBPM(120)\n";
        defaultScript += "clock:play()\n";
        codeEditor_->SetText(defaultScript);
        editorInitialized_ = true;
    }
    
    // CRITICAL FIX: Apply deferred script updates when safe
    // This prevents crashes from calling SetText() during script execution or ImGui rendering
    // We need to be very careful - only apply when absolutely safe
    // NEVER call SetText() during draw() - it causes crashes!
    if (hasPendingScriptUpdate_ && codeEditor_ && !hasManualEdits_) {
        // Multiple safety checks - only apply when ALL conditions are safe
        bool isSafe = true;
        
        if (engine_) {
            // Check if script is executing
            if (engine_->isExecutingScript()) {
                isSafe = false;
                ofLogVerbose("CodeShell") << "Deferred update blocked - script executing";
            }
            
            // Check if commands are processing
            if (isSafe && engine_->commandsBeingProcessed()) {
                isSafe = false;
                ofLogVerbose("CodeShell") << "Deferred update blocked - commands processing";
            }
            
            // CRITICAL FIX: Check render guard to prevent updates during rendering
            // This prevents crashes from script updates during ImGui rendering
            if (isSafe && engine_->isRendering()) {
                isSafe = false;
                ofLogVerbose("CodeShell") << "Deferred update blocked - rendering in progress";
            }
        }
        
        // Only apply if all safety checks pass
        if (isSafe) {
            try {
                // #region agent log - Enhanced crash logging
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        bool isExecuting = engine_ ? engine_->isExecutingScript() : false;
                        bool commandsProcessing = engine_ ? engine_->commandsBeingProcessed() : false;
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"SETTEXT\",\"location\":\"CodeShell.cpp:update\",\"message\":\"BEFORE SetText()\",\"data\":{\"scriptLength\":" << pendingScriptUpdate_.length() << ",\"isExecutingScript\":" << (isExecuting ? "true" : "false") << ",\"commandsProcessing\":" << (commandsProcessing ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
                        logFile.flush();
                        logFile.close();
                    }
                }
                // #endregion
                
                // Update lastEditorText_ BEFORE SetText to prevent recursive updates
                lastEditorText_ = pendingScriptUpdate_;
                
                // CRITICAL: Log thread ID and ImGui context before SetText
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        void* imguiContext = ImGui::GetCurrentContext();
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"SETTEXT\",\"location\":\"CodeShell.cpp:update\",\"message\":\"IMMEDIATELY BEFORE SetText()\",\"data\":{\"threadId\":\"" << std::this_thread::get_id() << "\",\"imguiContext\":" << (imguiContext ? "valid" : "null") << ",\"scriptLength\":" << pendingScriptUpdate_.length() << "},\"timestamp\":" << now << "}\n";
                        logFile.flush();
                        logFile.close();
                    }
                }
                
                codeEditor_->SetText(pendingScriptUpdate_);
                
                // #region agent log - Success
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"SETTEXT\",\"location\":\"CodeShell.cpp:update\",\"message\":\"AFTER SetText() - SUCCESS\",\"data\":{},\"timestamp\":" << now << "}\n";
                        logFile.flush();
                        logFile.close();
                    }
                }
                // #endregion
                
                editorInitialized_ = true;
                hasPendingScriptUpdate_ = false;
                ofLogVerbose("CodeShell") << "Applied deferred script update (" << pendingScriptUpdate_.length() << " chars)";
            } catch (const std::exception& e) {
                // #region agent log - Exception caught
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"SETTEXT\",\"location\":\"CodeShell.cpp:update\",\"message\":\"EXCEPTION in SetText() - CRASH POINT\",\"data\":{\"error\":\"" << e.what() << "\"},\"timestamp\":" << now << "}\n";
                        logFile.flush();
                        logFile.close();
                    }
                }
                // #endregion
                ofLogError("CodeShell") << "Exception applying deferred script update: " << e.what();
                hasPendingScriptUpdate_ = false;  // Clear to prevent retry loop
            } catch (...) {
                // #region agent log - Unknown exception
                {
                    std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                    if (logFile.is_open()) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"SETTEXT\",\"location\":\"CodeShell.cpp:update\",\"message\":\"UNKNOWN EXCEPTION in SetText() - CRASH POINT\",\"data\":{},\"timestamp\":" << now << "}\n";
                        logFile.flush();
                        logFile.close();
                    }
                }
                // #endregion
                ofLogError("CodeShell") << "Unknown exception applying deferred script update";
                hasPendingScriptUpdate_ = false;  // Clear to prevent retry loop
            }
        } else {
            // Still unsafe - keep it pending for next frame
            // But limit retries to prevent infinite pending state
            static int pendingFrames = 0;
            pendingFrames++;
            if (pendingFrames > 60) {  // After 60 frames (~1 second at 60fps), give up
                ofLogWarning("CodeShell") << "Deferred script update pending too long - clearing";
                hasPendingScriptUpdate_ = false;
                pendingFrames = 0;
            }
        }
    }
    
    // CRITICAL FIX: Disable auto-update when user is actively editing
    // This prevents ScriptManager from overwriting user's edits
    // Use safe API instead of direct ScriptManager access
    if (codeEditor_ && hasManualEdits_ && engine_) {
        engine_->setScriptAutoUpdate(false);
    }
    
    // Check for text changes and trigger auto-evaluation
    if (codeEditor_ && autoEvalEnabled_ && hasManualEdits_) {
        std::string currentText = codeEditor_->GetText();
        
        // Detect text changes
        if (currentText != lastEditorText_) {
            lastEditTime_ = ofGetElapsedTimef();
            lastEditorText_ = currentText;
            
            // Check for simple parameter changes (execute immediately, no debounce)
            checkAndExecuteSimpleChanges();
        }
        
        // Debounced auto-evaluation (execute after user stops typing)
        float currentTime = ofGetElapsedTimef();
        if (currentTime - lastEditTime_ > autoEvalDebounce_ && lastEditTime_ > 0.0f) {
            // User stopped typing, auto-evaluate script
            // Only if there were actual changes
            if (hasManualEdits_) {
                // Safety checks before execution
                if (engine_ && !engine_->isExecutingScript() && !engine_->commandsBeingProcessed()) {
                    std::string currentText = codeEditor_->GetText();
                    
                    // Use incremental execution if enabled and changes are small
                    if (incrementalEvalEnabled_ && !lastExecutedScript_.empty() && !currentText.empty()) {
                        // Detect changed lines
                        std::vector<int> changedLines = detectChangedLines(lastExecutedScript_, currentText);
                        
                        // Use incremental execution if only a few lines changed
                        if (changedLines.size() > 0 && changedLines.size() <= (size_t)maxIncrementalLines_) {
                            try {
                                executeChangedLines(changedLines);
                                lastExecutedScript_ = currentText;  // Update after successful execution
                            } catch (const std::exception& e) {
                                ofLogError("CodeShell") << "Error during incremental execution: " << e.what();
                                if (replShell_) {
                                    replShell_->appendError("Incremental execution error: " + std::string(e.what()));
                                }
                                // Fallback to full execution on error
                                executeAll();
                                lastExecutedScript_ = currentText;
                            } catch (...) {
                                ofLogError("CodeShell") << "Unknown error during incremental execution";
                                if (replShell_) {
                                    replShell_->appendError("Incremental execution error: unknown error");
                                }
                                // Fallback to full execution on error
                                executeAll();
                                lastExecutedScript_ = currentText;
                            }
                        } else {
                            // Too many changes or change detection failed - fallback to full execution
                            executeAll();
                            lastExecutedScript_ = currentText;  // Update after successful execution
                        }
                    } else {
                        // Incremental execution disabled or no previous script - use full execution
                        executeAll();
                        lastExecutedScript_ = currentText;  // Update after successful execution
                    }
                } else {
                    // State is unsafe - defer execution (will try again next frame)
                    ofLogVerbose("CodeShell") << "Deferring auto-evaluation - state is unsafe";
                }
                
                lastEditTime_ = 0.0f;  // Reset to prevent repeated execution
            }
        }
    }
}

void CodeShell::draw() {
    if (!active_) return;
    if (!codeEditor_) {
        ofLogError("CodeShell") << "Code editor not initialized";
        return;
    }
    
    // Ensure ImGui context is valid
    if (ImGui::GetCurrentContext() == nullptr) {
        ofLogWarning("CodeShell") << "ImGui context is null, skipping draw";
        return;
    }
    
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) {
        ofLogWarning("CodeShell") << "Invalid display size";
        return;
    }
    
    ImVec2 viewportSize = io.DisplaySize;
    
    // Calculate split view sizes
    float editorHeight = viewportSize.y * editorHeightRatio_;
    float replHeight = viewportSize.y - editorHeight - splitterHeight_;
    
    // Ensure minimum sizes
    if (editorHeight < 50.0f) editorHeight = 50.0f;
    if (replHeight < 50.0f) replHeight = 50.0f;
    
    // Draw code editor (top section)
    // TextEditor::Render() with parameters creates its own window/child
    // We'll create a simple parent window for it
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(viewportSize.x, editorHeight), ImGuiCond_Always);
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    
    // Use NoBackground flag for transparency (video shows through)
    // TextEditor's child window will have its own background
    ImGuiWindowFlags editorFlags = ImGuiWindowFlags_NoTitleBar | 
                                    ImGuiWindowFlags_NoResize | 
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoBackground |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    // Create parent window - TextEditor::Render() will create its own child inside
    if (ImGui::Begin("CodeEditorParent", nullptr, editorFlags)) {
        ImVec2 editorSize(viewportSize.x, editorHeight);
        // Get available content region - this is what the TextEditor child will use
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        if (contentSize.x <= 0 || contentSize.y <= 0) {
            contentSize = ImVec2(editorSize.x, editorSize.y);
        }
        
        // Ensure minimum content size
        if (contentSize.x < 10.0f) contentSize.x = 10.0f;
        if (contentSize.y < 10.0f) contentSize.y = 10.0f;
        
        // Always try to render if codeEditor_ exists
        if (codeEditor_) {
            // CRITICAL FIX: Never call SetText() during draw() - it causes crashes!
            // TextEditor must not be modified while it's being rendered
            // If text is empty, it will be initialized in update() or setup()
            int totalLines = codeEditor_->GetTotalLines();
            if (totalLines == 0) {
                // Don't call SetText() here - defer to update() or setup()
                // This prevents crashes from modifying TextEditor during ImGui rendering
                ofLogWarning("CodeShell") << "TextEditor has no lines during draw() - will initialize in update()";
                totalLines = 1;  // Use minimum to prevent rendering issues
            }
            
            // Debug: Log once per second
            static float lastLogTime = 0.0f;
            float currentTime = ofGetElapsedTimef();
            if (currentTime - lastLogTime > 1.0f) {
                ofLogNotice("CodeShell") << "Rendering TextEditor - Lines: " << totalLines 
                                         << ", Size: " << contentSize.x << "x" << contentSize.y;
                lastLogTime = currentTime;
            }
            
            // Draw semi-transparent backgrounds for each line
            // We'll draw them in the parent window, and they'll show through the transparent TextEditor child
            // Note: Since TextEditor child scrolls independently, we draw all lines so backgrounds
            // are always available as text scrolls into view
            auto drawList = ImGui::GetWindowDrawList();
            ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
            
            // Get font metrics for line positioning
            float lineHeight = ImGui::GetTextLineHeightWithSpacing();
            // totalLines already declared above
            
            // Draw semi-transparent black background for each line
            // These will appear behind the text in the TextEditor child window
            ImU32 lineBgColor = IM_COL32(0, 0, 0, 120); // Semi-transparent black for readability
            for (int line = 0; line < totalLines; ++line) {
                float lineY = cursorScreenPos.y + (line * lineHeight);
                ImVec2 lineStart(cursorScreenPos.x, lineY);
                ImVec2 lineEnd(cursorScreenPos.x + contentSize.x, lineY + lineHeight);
                drawList->AddRectFilled(lineStart, lineEnd, lineBgColor);
            }
            
            // CRITICAL: Log before Render() to catch crashes during rendering
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    void* imguiContext = ImGui::GetCurrentContext();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"RENDER\",\"location\":\"CodeShell.cpp:draw\",\"message\":\"BEFORE TextEditor::Render()\",\"data\":{\"threadId\":\"" << std::this_thread::get_id() << "\",\"imguiContext\":" << (imguiContext ? "valid" : "null") << ",\"totalLines\":" << totalLines << "},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
            
            // Use public Render() method - it creates its own child window
            // Pass the content size so it fills the available space
            // The line backgrounds drawn above will show through the transparent child background
            codeEditor_->Render("##CodeEditor", contentSize, false);
            
            // CRITICAL: Log after Render() to confirm it completed
            {
                std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
                if (logFile.is_open()) {
                    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"RENDER\",\"location\":\"CodeShell.cpp:draw\",\"message\":\"AFTER TextEditor::Render() - SUCCESS\",\"data\":{},\"timestamp\":" << now << "}\n";
                    logFile.flush();
                    logFile.close();
                }
            }
        } else {
            ImGui::Text("Code editor not created");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    
    // Draw splitter
    float splitterY = editorHeight;
    ImGui::SetNextWindowPos(ImVec2(0, splitterY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(viewportSize.x, splitterHeight_), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    
    ImGuiWindowFlags splitterFlags = ImGuiWindowFlags_NoTitleBar | 
                                      ImGuiWindowFlags_NoResize | 
                                      ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoCollapse |
                                      ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoBackground;
    
    if (ImGui::Begin("Splitter", nullptr, splitterFlags)) {
        ImGui::Button("##splitter", ImVec2(viewportSize.x, splitterHeight_));
        
        // Handle splitter drag
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            if (!isResizing_) {
                isResizing_ = true;
                resizeStartY_ = io.MousePos.y;
                resizeStartRatio_ = editorHeightRatio_;
            } else {
                float deltaY = io.MousePos.y - resizeStartY_;
                float newRatio = resizeStartRatio_ + (deltaY / viewportSize.y);
                editorHeightRatio_ = std::max(0.2f, std::min(0.8f, newRatio));
            }
        } else {
            isResizing_ = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    
    // Draw REPL output (bottom section) - use CommandShell's custom rendering
    if (replShell_) {
        float replY = splitterY + splitterHeight_;
        
        // Update embedded bounds for CommandShell
        replShell_->setEmbeddedBounds(0, replY, viewportSize.x, replHeight);
        
        // Render CommandShell (it will use embedded bounds)
        replShell_->draw();
    }
}

void CodeShell::exit() {
    if (observerId_ > 0) {
        ofLogNotice("CodeShell") << "Unsubscribing from state changes (ID: " << observerId_ << ")";
    }
    // Call parent exit() last to unsubscribe from state changes
    Shell::exit();
    // Cleanup if needed
}

void CodeShell::refreshScriptFromState() {
    if (!engine_ || !codeEditor_) return;
    
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"C\",\"location\":\"CodeShell.cpp:refreshScriptFromState\",\"message\":\"refreshScriptFromState ENTRY\",\"data\":{\"hasManualEdits\":" << (hasManualEdits_ ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
            logFile.flush();
            logFile.close();
        }
    }
    // #endregion
    
    // Get current script from state snapshot (instead of direct ScriptManager access)
    EngineState state = engine_->getState();
    std::string currentScript = state.script.currentScript;
    
    // Only update if user hasn't manually edited
    if (!hasManualEdits_ && !currentScript.empty()) {
        codeEditor_->SetText(currentScript);
        lastEditorText_ = currentScript;  // Update tracking
        editorInitialized_ = true;
        ofLogNotice("CodeShell") << "Refreshed script from state (" 
                                 << codeEditor_->GetTotalLines() << " lines)";
    }
}

void CodeShell::setActive(bool active) {
    Shell::setActive(active);
    
    // CRITICAL FIX: Don't call refreshScriptFromState() here
    // The callback mechanism (setScriptUpdateCallback) already handles script updates
    // refreshScriptFromState() reads from state.script.currentScript which may be stale
    // and overwrites the correct script that was provided by the callback
    // The callback will fire when state changes, keeping the editor in sync
    wasActive_ = active;
}

bool CodeShell::handleKeyPress(int key) {
    if (!active_) return false;
    if (!codeEditor_) return false;
    
    // Use ImGui key detection for reliable clipboard operations (works across platforms)
    ImGuiIO& io = ImGui::GetIO();
    bool cmdOrCtrlPressed = io.KeyCtrl || io.KeySuper; // Support both Ctrl and Cmd (Super)
    
    // Also check OF key states as fallback
    if (!cmdOrCtrlPressed) {
        bool cmdPressed = ofGetKeyPressed(OF_KEY_COMMAND);
        bool ctrlPressed = ofGetKeyPressed(OF_KEY_CONTROL);
        cmdOrCtrlPressed = cmdPressed || ctrlPressed;
    }
    
    // cmd+A / ctrl+A: Select all
    bool aKeyPressed = ImGui::IsKeyPressed(ImGuiKey_A, false) || (key == 'a' || key == 'A');
    if (cmdOrCtrlPressed && aKeyPressed) {
        // Select all text in editor
        auto lines = codeEditor_->GetTextLines();
        if (!lines.empty()) {
            TextEditor::Coordinates start(0, 0);
            TextEditor::Coordinates end(static_cast<int>(lines.size() - 1), 
                                       static_cast<int>(lines.back().length()));
            codeEditor_->SetSelection(start, end);
            // Don't set hasManualEdits_ for command keys
            return true;
        }
        return false;
    }
    
    // cmd+C / ctrl+C: Copy selected text
    bool cKeyPressed = ImGui::IsKeyPressed(ImGuiKey_C, false) || (key == 'c' || key == 'C');
    if (cmdOrCtrlPressed && cKeyPressed) {
        if (codeEditor_->HasSelection()) {
            std::string selectedText = codeEditor_->GetSelectedText();
            if (!selectedText.empty()) {
                // Use ImGui clipboard (works across platforms)
                ImGui::SetClipboardText(selectedText.c_str());
                ofLogVerbose("CodeShell") << "Copied " << selectedText.length() << " characters to clipboard";
                return true;
            }
        }
        return false;
    }
    
    // cmd+X / ctrl+X: Cut selected text
    bool xKeyPressed = ImGui::IsKeyPressed(ImGuiKey_X, false) || (key == 'x' || key == 'X');
    if (cmdOrCtrlPressed && xKeyPressed) {
        if (codeEditor_->HasSelection()) {
            std::string selectedText = codeEditor_->GetSelectedText();
            if (!selectedText.empty()) {
                // Copy to clipboard
                ImGui::SetClipboardText(selectedText.c_str());
                // Delete selected text
                codeEditor_->Delete();
                // Don't set hasManualEdits_ for command keys (cut is a command)
                ofLogVerbose("CodeShell") << "Cut " << selectedText.length() << " characters";
                return true;
            }
        }
        return false;
    }
    
    // cmd+V / ctrl+V: Paste from clipboard
    bool vKeyPressed = ImGui::IsKeyPressed(ImGuiKey_V, false) || (key == 'v' || key == 'V');
    if (cmdOrCtrlPressed && vKeyPressed) {
        // Get clipboard text from ImGui
        const char* clipboardText = ImGui::GetClipboardText();
        if (clipboardText && strlen(clipboardText) > 0) {
            // If there's a selection, replace it; otherwise insert at cursor
            if (codeEditor_->HasSelection()) {
                codeEditor_->Delete();
            }
            // Insert clipboard text
            codeEditor_->InsertText(clipboardText);
            // Paste IS user input (user chose to paste), so set hasManualEdits_
            hasManualEdits_ = true;
            if (engine_) {
                engine_->setScriptAutoUpdate(false);  // Use safe API
            }
            ofLogVerbose("CodeShell") << "Pasted " << strlen(clipboardText) << " characters";
            return true;
        }
        return false;
    }
    
    // Ctrl+Enter: Execute selection or current line
    if (cmdOrCtrlPressed && (key == OF_KEY_RETURN || key == '\r' || key == '\n')) {
        executeSelection();
        return true;
    }
    
    // Ctrl+R: Execute all (always full execution, bypasses incremental)
    if (cmdOrCtrlPressed && (key == 'r' || key == 'R')) {
        executeAll();
        return true;
    }
    
    // Ctrl+Shift+A: Toggle auto-evaluation
    bool shiftPressed = io.KeyShift || ofGetKeyPressed(OF_KEY_SHIFT);
    if (cmdOrCtrlPressed && shiftPressed && (key == 'a' || key == 'A')) {
        autoEvalEnabled_ = !autoEvalEnabled_;
        if (replShell_) {
            if (autoEvalEnabled_) {
                replShell_->appendOutput("Auto-evaluation: ENABLED (incremental execution)");
            } else {
                replShell_->appendOutput("Auto-evaluation: DISABLED");
            }
        }
        return true;
    }
    
    // Mark as manually edited only for actual user input (not command keys)
    // This prevents auto-sync from overwriting user edits
    if (isUserInput(key)) {
        hasManualEdits_ = true;
        // Disable auto-updates when user is editing
        if (engine_) {
            engine_->setScriptAutoUpdate(false);  // Use safe API
        }
    }
    
    // Let TextEditor handle other keys
    // Note: TextEditor handles its own input via ImGui
    return false;
}

bool CodeShell::handleMousePress(int x, int y, int button) {
    if (!active_) return false;
    // TextEditor handles mouse via ImGui
    return false;
}

bool CodeShell::handleMouseDrag(int x, int y, int button) {
    if (!active_) return false;
    // TextEditor handles mouse via ImGui
    return false;
}

bool CodeShell::handleMouseRelease(int x, int y, int button) {
    if (!active_) return false;
    // TextEditor handles mouse via ImGui
    return false;
}

bool CodeShell::handleWindowResize(int w, int h) {
    if (!active_) return false;
    // Window resize handled in draw()
    return false;
}

void CodeShell::executeSelection() {
    std::string text;
    
    if (codeEditor_->HasSelection()) {
        // Execute selected text
        text = codeEditor_->GetSelectedText();
    } else {
        // Execute current line
        auto cursorPos = codeEditor_->GetCursorPosition();
        auto lines = codeEditor_->GetTextLines();
        if (cursorPos.mLine >= 0 && cursorPos.mLine < (int)lines.size()) {
            text = lines[cursorPos.mLine];
        }
    }
    
    if (!text.empty()) {
        executeLuaScript(text);
    }
}

void CodeShell::executeAll() {
    std::string text = codeEditor_->GetText();
    if (!text.empty()) {
        executeLuaScript(text);
        lastExecutedScript_ = text;  // Update after execution
    }
}

void CodeShell::executeChangedLines(const std::vector<int>& changedLines) {
    if (!codeEditor_ || changedLines.empty()) {
        return;
    }
    
    // Get current script text for block detection
    std::string currentText = codeEditor_->GetText();
    std::string oldText = lastExecutedScript_.empty() ? "" : lastExecutedScript_;
    
    // Try to detect if changed lines form logical blocks
    if (!oldText.empty()) {
        std::vector<Block> changedBlocks = detectChangedBlocks(oldText, currentText);
        
        // If we found blocks, prefer block execution
        if (!changedBlocks.empty()) {
            for (const auto& block : changedBlocks) {
                if (block.startLine >= 0 && block.endLine >= block.startLine) {
                    // Verify block is complete (simple heuristic)
                    bool isComplete = true;
                    if (block.type == Block::FUNCTION) {
                        // Check if function has matching end (simple check)
                        auto lines = codeEditor_->GetTextLines();
                        if (block.endLine < (int)lines.size()) {
                            std::string endLine = lines[block.endLine];
                            if (endLine.find("end") == std::string::npos) {
                                isComplete = false;
                            }
                        }
                    }
                    
                    if (isComplete) {
                        // Execute entire block as single unit
                        executeBlock(block.startLine, block.endLine);
                        continue;  // Skip individual line execution for this block
                    } else {
                        ofLogWarning("CodeShell") << "Incomplete block detected, skipping execution";
                    }
                }
            }
            
            // If we executed blocks, we're done (don't execute individual lines)
            return;
        }
    }
    
    // No blocks detected or block execution failed - execute lines individually
    auto lines = codeEditor_->GetTextLines();
    
    // Execute each changed line individually
    // Preserves state from unchanged lines (they're not executed)
    for (int lineNum : changedLines) {
        if (lineNum >= 0 && lineNum < (int)lines.size()) {
            try {
                executeLine(lineNum);
            } catch (const std::exception& e) {
                ofLogError("CodeShell") << "Error executing line " << lineNum << ": " << e.what();
                // Continue with other lines even if one fails
            } catch (...) {
                ofLogError("CodeShell") << "Unknown error executing line " << lineNum;
                // Continue with other lines even if one fails
            }
        }
    }
}

void CodeShell::executeBlock(int startLine, int endLine) {
    if (!codeEditor_ || startLine < 0 || endLine < startLine) {
        return;
    }
    
    // Get all lines from editor
    auto lines = codeEditor_->GetTextLines();
    
    if (endLine >= (int)lines.size()) {
        ofLogWarning("CodeShell") << "Block end line out of range: " << endLine;
        return;
    }
    
    // Extract block content (lines startLine to endLine)
    std::string blockContent;
    for (int i = startLine; i <= endLine && i < (int)lines.size(); ++i) {
        blockContent += lines[i];
        if (i < endLine) {
            blockContent += "\n";
        }
    }
    
    if (!blockContent.empty()) {
        // Execute block as single script
        // This preserves block context (function definitions, etc.)
        try {
            executeLuaScript(blockContent);
        } catch (const std::exception& e) {
            ofLogError("CodeShell") << "Error executing block (lines " << startLine << "-" << endLine << "): " << e.what();
        } catch (...) {
            ofLogError("CodeShell") << "Unknown error executing block (lines " << startLine << "-" << endLine << ")";
        }
    }
}

void CodeShell::executeLuaScript(const std::string& script) {
    if (!engine_) return;
    
    // #region agent log
    {
        std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"A,C\",\"location\":\"CodeShell.cpp:executeLuaScript\",\"message\":\"executeLuaScript ENTRY\",\"data\":{\"scriptLength\":" << script.length() << "},\"timestamp\":" << now << "}\n";
            logFile.flush();
            logFile.close();
        }
    }
    // #endregion
    
    // Clear previous errors
    clearErrors();
    
    // CRITICAL FIX: Prevent feedback loop during script execution
    // Disable auto-update to prevent ScriptManager from overwriting editor
    // Don't reset hasManualEdits_ - keep user's edits protected
    bool wasAutoUpdate = false;
    if (engine_) {
        wasAutoUpdate = engine_->isScriptAutoUpdateEnabled();  // Use safe API
        
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"SYNC_DEBUG\",\"hypothesisId\":\"E\",\"location\":\"CodeShell.cpp:executeLuaScript\",\"message\":\"Disabling auto-update\",\"data\":{\"wasAutoUpdate\":" << (wasAutoUpdate ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        
        engine_->setScriptAutoUpdate(false);  // Prevent script overwrite - use safe API
    }
    
    // Execute via Engine with sync contract (Script → Engine synchronization)
    // Guarantees script changes are reflected in engine state before callback fires
    // Still non-blocking (async), but with completion guarantees
    std::string scriptCopy = script;
    engine_->syncScriptToEngine(scriptCopy, [this, wasAutoUpdate](bool success) {
        // Callback executed when sync is complete (from Engine::syncScriptToEngine())
        
        // #region agent log
        {
            std::ofstream logFile("/Users/jaufre/works/of_v0.12.1_osx_release/.cursor/debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                logFile << "{\"sessionId\":\"debug-session\",\"runId\":\"CRASH_DEBUG\",\"hypothesisId\":\"A,C\",\"location\":\"CodeShell.cpp:executeLuaScript\",\"message\":\"executeLuaScript - sync contract callback\",\"data\":{\"success\":" << (success ? "true" : "false") << "},\"timestamp\":" << now << "}\n";
                logFile.flush();
                logFile.close();
            }
        }
        // #endregion
        
        // Sync contract completed - script changes are now reflected in engine state
        // State version has been updated, commands have been processed
        
        // Restore auto-update setting
        if (engine_) {
            engine_->setScriptAutoUpdate(wasAutoUpdate);
        }
        
        // Handle result
        if (success) {
            clearErrors();
            // Script executed successfully and state is synchronized
        } else {
            // Script execution or sync failed
            // Error handling is done in sync contract callback
            ofLogWarning("CodeShell") << "Script → Engine sync failed";
        }
        
        // Re-enable auto-update only if it was enabled before AND execution succeeded
        // But defer it to prevent immediate script overwrite
        // The flag will remain false, protecting user edits
        // User can manually sync later if needed
    });
}

void CodeShell::markErrorInEditor(int line, const std::string& message) {
    TextEditor::ErrorMarkers markers;
    markers[line] = message;
    codeEditor_->SetErrorMarkers(markers);
}

void CodeShell::clearErrors() {
    TextEditor::ErrorMarkers markers;
    codeEditor_->SetErrorMarkers(markers);
}

int CodeShell::parseErrorLine(const std::string& errorMessage) {
    // Try to parse line number from Lua error message
    // Format: "filename:line: message" or "[string \"...\"]:line: message"
    std::regex lineRegex(R"((?:\[string[^\]]+\]|[\w/\.]+):(\d+):)");
    std::smatch match;
    
    if (std::regex_search(errorMessage, match, lineRegex)) {
        try {
            return std::stoi(match[1].str());
        } catch (...) {
            return 0;
        }
    }
    
    return 0;
}

void CodeShell::executeLine(int lineNumber) {
    if (!codeEditor_) return;
    
    auto lines = codeEditor_->GetTextLines();
    if (lineNumber >= 0 && lineNumber < (int)lines.size()) {
        std::string line = lines[lineNumber];
        if (!line.empty()) {
            // For clock operations, we need to ensure clock is available
            // Check if line uses clock and add context if needed
            std::regex clockPattern(R"(clock\s*:)");
            if (std::regex_search(line, clockPattern)) {
                // Execute with clock context
                std::string script = "local clock = engine:getClock()\n" + line;
                executeLuaScript(script);
            } else {
                // Execute line as-is
                executeLuaScript(line);
            }
        }
    }
}

bool CodeShell::isSimpleParameterChange(const std::string& line) {
    // Match patterns like: clock:setBPM(...), clock:start(), clock:stop(), setParam(...)
    std::regex bpmPattern(R"(clock\s*:\s*setBPM\s*\()");
    std::regex startPattern(R"(clock\s*:\s*start\s*\()");
    std::regex stopPattern(R"(clock\s*:\s*stop\s*\()");
    std::regex playPattern(R"(clock\s*:\s*play\s*\()");
    std::regex setParamPattern(R"(setParam\s*\()");
    std::regex setPattern(R"(\w+\s*:\s*set\w+\s*\()");
    
    return std::regex_search(line, bpmPattern) || 
           std::regex_search(line, startPattern) ||
           std::regex_search(line, stopPattern) ||
           std::regex_search(line, playPattern) ||
           std::regex_search(line, setParamPattern) ||
           std::regex_search(line, setPattern);
}

bool CodeShell::isBPMChange(const std::string& line) {
    // Match: clock:setBPM(...)
    std::regex bpmPattern(R"(clock\s*:\s*setBPM\s*\()");
    return std::regex_search(line, bpmPattern);
}

void CodeShell::checkAndExecuteSimpleChanges() {
    if (!codeEditor_) return;
    
    // Get current script text
    std::string currentText = codeEditor_->GetText();
    
    // If we have a previous executed script, detect all changed lines
    if (!lastExecutedScript_.empty() && !currentText.empty()) {
        std::vector<int> changedLines = detectChangedLines(lastExecutedScript_, currentText);
        auto lines = codeEditor_->GetTextLines();
        
        // Check each changed line for parameter changes and execute immediately
        for (int lineNum : changedLines) {
            if (lineNum >= 0 && lineNum < (int)lines.size()) {
                std::string currentLine = lines[lineNum];
                
                // Check if it's a simple parameter change (execute immediately)
                if (isSimpleParameterChange(currentLine)) {
                    // Execute just this line immediately (no debounce for simple changes)
                    executeLine(lineNum);
                }
            }
        }
    } else {
        // Fallback: If no previous script, check cursor line (original behavior)
        auto cursorPos = codeEditor_->GetCursorPosition();
        auto lines = codeEditor_->GetTextLines();
        
        if (cursorPos.mLine >= 0 && cursorPos.mLine < (int)lines.size()) {
            std::string currentLine = lines[cursorPos.mLine];
            
            // Check if it's a simple parameter change (execute immediately)
            if (isSimpleParameterChange(currentLine)) {
                // Execute just this line immediately (no debounce for simple changes)
                executeLine(cursorPos.mLine);
            }
        }
    }
}

// Change detection for incremental execution
std::vector<int> CodeShell::detectChangedLines(const std::string& oldScript, const std::string& newScript) {
    std::vector<int> changedLines;
    
    // Split both scripts into lines
    std::vector<std::string> oldLines;
    std::vector<std::string> newLines;
    
    std::istringstream oldStream(oldScript);
    std::istringstream newStream(newScript);
    std::string line;
    
    while (std::getline(oldStream, line)) {
        oldLines.push_back(line);
    }
    
    while (std::getline(newStream, line)) {
        newLines.push_back(line);
    }
    
    // Compare line-by-line
    size_t maxLines = std::max(oldLines.size(), newLines.size());
    for (size_t i = 0; i < maxLines; ++i) {
        if (i >= oldLines.size() || i >= newLines.size()) {
            // Line added or removed
            changedLines.push_back((int)i);
        } else if (oldLines[i] != newLines[i]) {
            // Line changed
            changedLines.push_back((int)i);
        }
    }
    
    return changedLines;
}

std::vector<CodeShell::Block> CodeShell::parseScriptBlocks(const std::string& script) {
    std::vector<Block> blocks;
    
    // Simple regex-based block detection (not full AST parsing)
    std::vector<std::string> lines;
    std::istringstream stream(script);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    
    // Simple heuristics: function definitions, pattern definitions, etc.
    int currentBlockStart = -1;
    Block::Type currentBlockType = Block::UNKNOWN;
    int functionDepth = 0;
    
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string trimmed = lines[i];
        // Remove leading/trailing whitespace
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
        
        // Detect function blocks (function ... end)
        if (trimmed.find("function") == 0) {
            currentBlockStart = (int)i;
            currentBlockType = Block::FUNCTION;
            functionDepth = 1;
        } else if (currentBlockType == Block::FUNCTION) {
            // Count nested functions
            if (trimmed.find("function") != std::string::npos) {
                functionDepth++;
            }
            if (trimmed.find("end") != std::string::npos) {
                functionDepth--;
                if (functionDepth == 0) {
                    // End of function block
                    Block block;
                    block.startLine = currentBlockStart;
                    block.endLine = (int)i;
                    block.type = Block::FUNCTION;
                    for (int j = currentBlockStart; j <= (int)i; ++j) {
                        block.content += lines[j] + "\n";
                    }
                    blocks.push_back(block);
                    currentBlockStart = -1;
                    currentBlockType = Block::UNKNOWN;
                }
            }
        }
        // Detect pattern definitions (pattern("...", ...))
        else if (trimmed.find("pattern(") != std::string::npos) {
            Block block;
            block.startLine = (int)i;
            block.endLine = (int)i;
            block.type = Block::PATTERN;
            block.content = lines[i];
            blocks.push_back(block);
        }
        // Detect variable assignments (simple heuristic)
        else if (trimmed.find("=") != std::string::npos && 
                 trimmed.find("local") == 0) {
            Block block;
            block.startLine = (int)i;
            block.endLine = (int)i;
            block.type = Block::VARIABLE;
            block.content = lines[i];
            blocks.push_back(block);
        }
    }
    
    return blocks;
}

std::vector<CodeShell::Block> CodeShell::detectChangedBlocks(const std::string& oldScript, const std::string& newScript) {
    std::vector<Block> changedBlocks;
    
    // Parse both scripts into blocks
    std::vector<Block> oldBlocks = parseScriptBlocks(oldScript);
    std::vector<Block> newBlocks = parseScriptBlocks(newScript);
    
    // Simple comparison: if block content changed, mark as changed
    // For now, use simple content comparison (can be improved later)
    for (const auto& newBlock : newBlocks) {
        bool found = false;
        for (const auto& oldBlock : oldBlocks) {
            if (oldBlock.startLine == newBlock.startLine && 
                oldBlock.type == newBlock.type) {
                if (oldBlock.content != newBlock.content) {
                    changedBlocks.push_back(newBlock);
                }
                found = true;
                break;
            }
        }
        if (!found) {
            // New block
            changedBlocks.push_back(newBlock);
        }
    }
    
    // Also check for removed blocks (blocks in old but not in new)
    for (const auto& oldBlock : oldBlocks) {
        bool found = false;
        for (const auto& newBlock : newBlocks) {
            if (oldBlock.startLine == newBlock.startLine && 
                oldBlock.type == newBlock.type) {
                found = true;
                break;
            }
        }
        if (!found) {
            // Block removed - mark as changed (will need special handling)
            changedBlocks.push_back(oldBlock);
        }
    }
    
    return changedBlocks;
}

bool CodeShell::isUserInput(int key) const {
    // Exclude command keys (Ctrl+Enter, Ctrl+R, etc.)
    ImGuiIO& io = ImGui::GetIO();
    bool cmdOrCtrlPressed = io.KeyCtrl || io.KeySuper;
    
    if (cmdOrCtrlPressed) {
        // Command keys don't count as manual edits
        return false;
    }
    
    // Only printable characters and editing keys count as user input
    return (key >= 32 && key <= 126) || 
           key == OF_KEY_BACKSPACE || key == OF_KEY_DEL ||
           key == OF_KEY_LEFT || key == OF_KEY_RIGHT || 
           key == OF_KEY_UP || key == OF_KEY_DOWN;
}

void CodeShell::onStateChanged(const EngineState& state) {
    // Cache state snapshot for thread-safe access in update() or draw()
    cachedState_ = state;
    
    // Log state changes for debugging
    ofLogNotice("CodeShell") << "State changed (BPM: " << state.transport.bpm 
                             << ", Playing: " << (state.transport.isPlaying ? "true" : "false") << ")";
    
    // Note: UI updates should be deferred to draw() or update() methods
    // This callback just caches the state snapshot
}

} // namespace shell
} // namespace vt

