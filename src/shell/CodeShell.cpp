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
#include <thread>
#include <functional>

namespace vt {
namespace shell {

// Simple hash for script content (Phase 7.4)
static std::string hashScript(const std::string& script) {
    // Use std::hash for simplicity - doesn't need to be cryptographic
    std::hash<std::string> hasher;
    return std::to_string(hasher(script));
}

CodeShell::CodeShell(Engine* engine)
    : Shell(engine)
    , codeEditor_(std::make_unique<TextEditor>())
{
}

CodeShell::~CodeShell() = default;

void CodeShell::setup() {
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
        engine_->setScriptUpdateCallback([this](const std::string& script, uint64_t scriptVersion) {
            // GUARD: Check if shell is exiting - if so, skip processing
            // This prevents use-after-free if callback is invoked during exit()
            if (isExiting_.load()) {
                return;
            }
            
            // Defer script updates to prevent crashes
            // Don't call SetText() directly from callback - it might be called during
            // script execution, ImGui rendering, or from unsafe contexts
            // Instead, store the script and apply it in update() when safe
            
            // Check editor mode to determine update behavior
            if (editorMode_ == EditorMode::EDIT) {
                // EDIT mode: Store synced script for later, don't update editor
                // The script will be applied when user switches back to VIEW mode
                {
                    std::lock_guard<std::mutex> lock(pendingUpdateMutex_);
                    pendingScriptUpdate_ = script;
                    pendingScriptVersion_ = scriptVersion;
                }
                hasPendingScriptUpdate_.store(true);
                return;  // Don't update editor when user is editing
            }
            // VIEW mode: Continue to defer update (will be applied in update() when safe)

            // Defer the update - will be applied in update() when safe
            // Use mutex-protected storage for thread-safe access from any thread
            {
                std::lock_guard<std::mutex> lock(pendingUpdateMutex_);
                pendingScriptUpdate_ = script;
                pendingScriptVersion_ = scriptVersion;
            }
            hasPendingScriptUpdate_.store(true);
        });
        
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

void CodeShell::onStateChanged(const EngineState& state, uint64_t stateVersion) {
    // Call base class implementation to update lastStateVersion_
    Shell::onStateChanged(state, stateVersion);
    
    // Check if state version is newer than last seen (prevent stale state processing)
    if (stateVersion < lastStateVersion_) {
        ofLogVerbose("CodeShell") << "Ignoring stale state update (version: " << stateVersion 
                                   << ", last: " << lastStateVersion_ << ")";
        return;
    }
    
    // State version is current or newer - process update
    ofLogNotice("CodeShell") << "State update received (version: " << stateVersion << ")";
    
    // NOTE: Script updates are handled exclusively via the ScriptManager callback mechanism
    // (setScriptUpdateCallback in setup()). The callback provides fresh script data directly
    // from ScriptManager, avoiding stale state issues from state.script.currentScript.
    // 
    // onStateChanged() is kept for non-script state changes (module additions/removals,
    // connection changes, etc.) but intentionally does NOT handle script updates.
    
    // Log that we're receiving state changes but script will sync via callback
    if (editorMode_ == EditorMode::EDIT) {
        ofLogVerbose("CodeShell") << "State update received in EDIT mode - script will sync via callback when returning to VIEW mode";
    } else {
        ofLogVerbose("CodeShell") << "State update received in VIEW mode - script will update via callback";
    }
}

void CodeShell::update(float deltaTime) {
    if (!active_) return;
    
    // Update embedded REPL shell
    if (replShell_) {
        replShell_->update(deltaTime);
    }
    
    // Ensure TextEditor is initialized (but never in draw()!)
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
    
    // Detect user editing (switch to EDIT mode when user types)
    // Only detect in VIEW mode
    // CRITICAL: Only call GetText() after editor is initialized to prevent crashes
    if (codeEditor_ && editorInitialized_ && editorMode_ == EditorMode::VIEW) {
        try {
            std::string currentText = codeEditor_->GetText();
            // Compare with last known editor text to detect changes
            // Only detect if we have a previous text to compare (not on first update)
            if (!lastEditorText_.empty() && currentText != lastEditorText_) {
                // Text changed and we're in VIEW mode - user started editing
                editorMode_ = EditorMode::EDIT;
                userEditBuffer_ = currentText;  // Store current text as user edit
                ofLogNotice("CodeShell") << "Switched to EDIT mode - user started editing";
                if (engine_) {
                    engine_->setScriptAutoUpdate(false);  // Disable auto-update when editing
                }
            }
            // Update lastEditorText_ for next comparison (but only if we're still in VIEW mode)
            // If we switched to EDIT mode, lastEditorText_ will be updated in the auto-evaluation section
            if (editorMode_ == EditorMode::VIEW) {
                lastEditorText_ = currentText;
            }
        } catch (const std::exception& e) {
            ofLogError("CodeShell") << "Exception in mode detection: " << e.what();
            // Don't crash - just skip mode detection this frame
        } catch (...) {
            ofLogError("CodeShell") << "Unknown exception in mode detection";
            // Don't crash - just skip mode detection this frame
        }
    }
    
    // Apply deferred script updates when safe
    // This prevents crashes from calling SetText() during script execution or ImGui rendering
    // We need to be very careful - only apply when absolutely safe
    // NEVER call SetText() during draw() - it causes crashes!
    if (hasPendingScriptUpdate_.load() && codeEditor_ && editorMode_ == EditorMode::VIEW) {
        // Use state version comparison instead of unsafe state flags
        // State version increments AFTER commands/scripts complete, providing reliable safety signal
        bool isSafe = true;
        uint64_t currentVersion = 0;

        if (engine_) {
            currentVersion = engine_->getStateVersion();

            // Check if state has been updated since last apply
            if (currentVersion <= lastAppliedVersion_) {
                // State hasn't changed yet - wait for update to complete
                isSafe = false;
                if (lastDeferredVersionWarning_ != currentVersion) {
                    ofLogNotice("CodeShell") << "Deferred update blocked - waiting for state version "
                                              << (lastAppliedVersion_ + 1) << " (current: " << currentVersion << ")";
                    lastDeferredVersionWarning_ = currentVersion;
                }
            }
        }

        // Only apply if state has been updated (version increased)
        if (isSafe) {
            try {
                // ATOMIC load for flag, mutex-protected for string/version
                std::string pendingUpdate;
                uint64_t pendingVersion = 0;
                {
                    std::lock_guard<std::mutex> lock(pendingUpdateMutex_);
                    pendingUpdate = pendingScriptUpdate_;
                    pendingVersion = pendingScriptVersion_;
                }

                // Update lastEditorText_ BEFORE SetText to prevent mode detection from triggering EDIT mode
                // This ensures that when SetText() is called, GetText() will match lastEditorText_
                // and mode detection won't think the user edited
                lastEditorText_ = pendingUpdate;

                codeEditor_->SetText(pendingUpdate);

                editorInitialized_ = true;
                hasPendingScriptUpdate_.store(false);
                lastAppliedVersion_ = pendingVersion;
                ofLogVerbose("CodeShell") << "Applied deferred script update (state version: " << currentVersion
                                          << ", script version: " << pendingVersion << ")";
            } catch (const std::exception& e) {
                ofLogError("CodeShell") << "Exception applying deferred script update: " << e.what();
                hasPendingScriptUpdate_.store(false);  // Clear to prevent retry loop
            } catch (...) {
                ofLogError("CodeShell") << "Unknown exception applying deferred script update";
                hasPendingScriptUpdate_.store(false);  // Clear to prevent retry loop
            }
        } else {
            // Still waiting for state update - keep it pending
            // Updates persist until state version increases (no arbitrary timeout)
            // This prevents updates from being lost during long-running script execution
            ofLogVerbose("CodeShell") << "Deferred script update pending - waiting for state version "
                                     << (lastAppliedVersion_ + 1);
        }
    }
    
    // Disable auto-update when user is actively editing
    // This prevents ScriptManager from overwriting user's edits
    // Use safe API instead of direct ScriptManager access
    if (codeEditor_ && editorMode_ == EditorMode::EDIT && engine_) {
        engine_->setScriptAutoUpdate(false);
    } else if (codeEditor_ && editorMode_ == EditorMode::VIEW && engine_) {
        // VIEW mode: Enable auto-update to allow script updates
        // Use ATOMIC load for thread-safe access
        bool hasPending = hasPendingScriptUpdate_.load();
        bool enableAutoUpdate = !hasPending;
        engine_->setScriptAutoUpdate(enableAutoUpdate);
        if (!enableAutoUpdate) {
            ofLogWarning("CodeShell") << "VIEW mode but script auto-update held until pending script applies";
        }
    }
    
    if (!autoEvalEnabled_ && !autoEvalLoggedDisabled_) {
        ofLogNotice("CodeShell") << "Auto-evaluation disabled: " << autoEvalDisableReason_;
        autoEvalLoggedDisabled_ = true;
    }
    
    // Check for text changes and trigger auto-evaluation (only in EDIT mode)
    // CRITICAL: Only call GetText() after editor is initialized to prevent crashes
    if (codeEditor_ && editorInitialized_ && autoEvalEnabled_ && editorMode_ == EditorMode::EDIT) {
        std::string currentText;
        try {
            currentText = codeEditor_->GetText();
        } catch (const std::exception& e) {
            ofLogError("CodeShell") << "Exception getting editor text for auto-eval: " << e.what();
            return;  // Skip auto-evaluation this frame
        } catch (...) {
            ofLogError("CodeShell") << "Unknown exception getting editor text for auto-eval";
            return;  // Skip auto-evaluation this frame
        }
        
        // Detect text changes
        if (currentText != lastEditorText_) {
            lastEditTime_ = ofGetElapsedTimef();
            lastEditorText_ = currentText;
            
            // Phase 7.4: Reset failure tracking when script changes
            // (Only reset if the hash is different - not just whitespace changes)
            std::string newHash = hashScript(currentText);
            if (newHash != hashScript(lastExecutedScript_)) {
                executionTracker_.reset();
            }
            
            // Check for simple parameter changes (execute immediately, no debounce)
            checkAndExecuteSimpleChanges();
        }
        
        // Debounced auto-evaluation (execute after user stops typing)
        float currentTime = ofGetElapsedTimef();
        if (currentTime - lastEditTime_ > autoEvalDebounce_ && lastEditTime_ > 0.0f) {
            // User stopped typing, auto-evaluate script
            // Only if in EDIT mode (redundant check, but preserves existing logic)
            if (editorMode_ == EditorMode::EDIT) {
                // Safety checks before execution
                if (engine_ && editorInitialized_ && !engine_->isExecutingScript() && !engine_->commandsBeingProcessed()) {
                    std::string currentText;
                    try {
                        currentText = codeEditor_->GetText();
                    } catch (const std::exception& e) {
                        ofLogError("CodeShell") << "Exception getting editor text for auto-eval debounce: " << e.what();
                        lastEditTime_ = 0.0f;  // Reset to prevent retry
                        return;  // Skip auto-evaluation this frame
                    } catch (...) {
                        ofLogError("CodeShell") << "Unknown exception getting editor text for auto-eval debounce";
                        lastEditTime_ = 0.0f;  // Reset to prevent retry
                        return;  // Skip auto-evaluation this frame
                    }
                    
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
            // Never call SetText() during draw() - it causes crashes!
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
            
            // Use public Render() method - it creates its own child window
            // Pass the content size so it fills the available space
            // The line backgrounds drawn above will show through the transparent child background
            codeEditor_->Render("##CodeEditor", contentSize, false);
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
    // CRITICAL: Set exit flag FIRST, before any other cleanup
    // This prevents the callback from accessing 'this' if it's invoked during exit
    isExiting_.store(true);
    
    // Now unregister script update callback
    if (engine_) {
        engine_->clearScriptUpdateCallback();
        ofLogVerbose("CodeShell") << "Script update callback cleared immediately on exit - preventing use-after-free";
    }
    
    // Now safe to cleanup embedded REPL shell
    if (replShell_) {
        replShell_->exit();
        replShell_.reset();
    }
}

std::vector<std::string> CodeShell::getTextLinesCopy() const {
    if (!codeEditor_) return {};
    auto linesRef = codeEditor_->GetTextLines();
    // CRITICAL: Copy immediately to prevent use-after-free
    // GetTextLines() returns a reference that becomes invalid when SetText() is called
    return std::vector<std::string>(linesRef.begin(), linesRef.end());
}

void CodeShell::refreshScriptFromState() {
    if (!engine_ || !codeEditor_) return;
    
    // Get current script from state snapshot (instead of direct ScriptManager access)
    EngineState state = engine_->getState();
    std::string currentScript = state.script.currentScript;
    
    // Only update if in VIEW mode (user wants to see current state)
    if (editorMode_ == EditorMode::VIEW && !currentScript.empty()) {
        codeEditor_->SetText(currentScript);
        lastEditorText_ = currentScript;  // Update tracking
        editorInitialized_ = true;
        ofLogNotice("CodeShell") << "Refreshed script from state (" 
                                 << codeEditor_->GetTotalLines() << " lines)";
    }
}

void CodeShell::setActive(bool active) {
    if (active) {
        // Activating - just set active flag
        Shell::setActive(active);
    } else {
        // Deactivating - call exit() to cleanup callback registration
        // Use isExiting_ guard to prevent calling exit() multiple times
        if (!isExiting_.load() && active_) {
            exit();
        }
    }
    
    // Don't call refreshScriptFromState() here
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
        auto lines = getTextLinesCopy();
        if (!lines.empty()) {
            TextEditor::Coordinates start(0, 0);
            TextEditor::Coordinates end(static_cast<int>(lines.size() - 1), 
                                       static_cast<int>(lines.back().length()));
            codeEditor_->SetSelection(start, end);
            // Command keys don't switch to EDIT mode
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
                // Command keys don't switch to EDIT mode (cut is a command)
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
            // Paste IS user input (user chose to paste), so switch to EDIT mode
            editorMode_ = EditorMode::EDIT;
            // Store current text as user edit (only if editor is initialized)
            if (editorInitialized_) {
                try {
                    userEditBuffer_ = codeEditor_->GetText();
                } catch (const std::exception& e) {
                    ofLogError("CodeShell") << "Exception getting editor text after paste: " << e.what();
                } catch (...) {
                    ofLogError("CodeShell") << "Unknown exception getting editor text after paste";
                }
            }
            if (engine_) {
                engine_->setScriptAutoUpdate(false);
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
    
    // Switch to EDIT mode only for actual user input (not command keys)
    // This prevents auto-sync from overwriting user edits
    if (isUserInput(key)) {
        editorMode_ = EditorMode::EDIT;
        // Store current text as user edit (only if editor is initialized)
        if (editorInitialized_) {
            try {
                userEditBuffer_ = codeEditor_->GetText();
            } catch (const std::exception& e) {
                ofLogError("CodeShell") << "Exception getting editor text for user input: " << e.what();
            } catch (...) {
                ofLogError("CodeShell") << "Unknown exception getting editor text for user input";
            }
        }
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
    if (!codeEditor_ || !editorInitialized_) {
        ofLogWarning("CodeShell") << "Cannot execute selection - editor not initialized";
        return;
    }
    
    std::string text;
    
    try {
        if (codeEditor_->HasSelection()) {
            // Execute selected text
            text = codeEditor_->GetSelectedText();
        } else {
            // Execute current line
            auto cursorPos = codeEditor_->GetCursorPosition();
            auto lines = getTextLinesCopy();
            if (cursorPos.mLine >= 0 && cursorPos.mLine < (int)lines.size()) {
                text = lines[cursorPos.mLine];
            }
        }
    } catch (const std::exception& e) {
        ofLogError("CodeShell") << "Exception getting selection text: " << e.what();
        return;
    } catch (...) {
        ofLogError("CodeShell") << "Unknown exception getting selection text";
        return;
    }
    
    if (!text.empty()) {
        executeLuaScript(text);
    }
}

void CodeShell::executeAll() {
    if (!codeEditor_ || !editorInitialized_) {
        ofLogWarning("CodeShell") << "Cannot execute - editor not initialized";
        return;
    }
    
    std::string text;
    try {
        text = codeEditor_->GetText();
    } catch (const std::exception& e) {
        ofLogError("CodeShell") << "Exception getting editor text for execution: " << e.what();
        return;
    } catch (...) {
        ofLogError("CodeShell") << "Unknown exception getting editor text for execution";
        return;
    }
    
    if (!text.empty()) {
        std::string scriptHash = hashScript(text);
        uint64_t nowMs = static_cast<uint64_t>(ofGetElapsedTimeMillis());
        
        // Phase 7.4: Check if we should retry
        if (!executionTracker_.shouldRetry(scriptHash, nowMs)) {
            ofLogVerbose("CodeShell") << "Skipping execution - same failing script in cooldown";
            return;
        }
        
        // Store current auto-update state and disable during execution
        bool wasAutoUpdate = false;
        if (engine_) {
            wasAutoUpdate = engine_->isScriptAutoUpdateEnabled();
            engine_->setScriptAutoUpdate(false);
        }
        
        // Execute via Engine
        Engine::Result result = engine_->eval(text);
        
        // Re-enable auto-update
        if (engine_) {
            engine_->setScriptAutoUpdate(wasAutoUpdate);
        }
        
        // Phase 7.4: Track result
        if (result.success) {
            executionTracker_.recordSuccess();
            lastExecutedScript_ = text;  // Update after successful execution
        } else {
            executionTracker_.recordFailure(scriptHash, nowMs);
            ofLogWarning("CodeShell") << "Script execution failed (failure #" 
                                      << executionTracker_.consecutiveFailures << ")";
            // Don't update lastExecutedScript_ on failure - keep old version for change detection
        }
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
                        auto lines = getTextLinesCopy();
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
    auto lines = getTextLinesCopy();
    
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
    auto lines = getTextLinesCopy();
    
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

    // Clear previous errors
    clearErrors();

    // Redesign: Fire-and-forget - remove blocking wait
    // This prevents deadlock where main thread waits for notifications
    // but can't process them because it's blocked
    bool wasAutoUpdate = false;
    if (engine_) {
        wasAutoUpdate = engine_->isScriptAutoUpdateEnabled();
        engine_->setScriptAutoUpdate(false);  // Prevent overwrite during execution
    }

    // Execute via Engine (non-blocking)
    Engine::Result result = engine_->eval(script);

    // Re-enable auto-update immediately
    // Script will be regenerated when state changes via observer callback
    if (result.success) {
        editorMode_ = EditorMode::VIEW;
        userEditBuffer_.clear();
        if (engine_) {
            engine_->setScriptAutoUpdate(true);  // Re-enable for VIEW mode
        }
        ofLogNotice("CodeShell") << "Script executed - will update when state changes (fire-and-forget design)";
    } else {
        // Error handling - stay in EDIT mode on failure
        ofLogError("CodeShell") << "Script execution failed - staying in EDIT mode: " << result.error;
        // editorMode_ stays in EDIT mode
        // userEditBuffer_ preserved
    }

    // Display result in REPL
    if (replShell_) {
        if (result.success) {
            replShell_->appendOutput(result.message);
        } else {
            replShell_->appendError(result.error);

            // Mark error in editor
            int errorLine = parseErrorLine(result.error);
            if (errorLine > 0) {
                markErrorInEditor(errorLine - 1, result.error);  // Convert to 0-based
            }
        }
    }
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
    
    auto lines = getTextLinesCopy();
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
    if (!codeEditor_ || !editorInitialized_) return;
    
    // Get current script text
    std::string currentText;
    try {
        currentText = codeEditor_->GetText();
    } catch (const std::exception& e) {
        ofLogError("CodeShell") << "Exception getting editor text for simple changes: " << e.what();
        return;
    } catch (...) {
        ofLogError("CodeShell") << "Unknown exception getting editor text for simple changes";
        return;
    }
    
    // Phase 7.4: Check if we should retry this script
    std::string scriptHash = hashScript(currentText);
    uint64_t nowMs = static_cast<uint64_t>(ofGetElapsedTimeMillis());
    if (!executionTracker_.shouldRetry(scriptHash, nowMs)) {
        // Skip execution - same failing script, still in cooldown
        return;
    }
    
    // If we have a previous executed script, detect all changed lines
    if (!lastExecutedScript_.empty() && !currentText.empty()) {
        std::vector<int> changedLines = detectChangedLines(lastExecutedScript_, currentText);
        auto lines = getTextLinesCopy();
        
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
        auto lines = getTextLinesCopy();
        
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

} // namespace shell
} // namespace vt

