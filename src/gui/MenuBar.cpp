#include "MenuBar.h"
#include "ViewManager.h"
#include "core/SessionManager.h"
#include "core/ProjectManager.h"
#include "AssetLibrary.h"
#include "FileBrowser.h"
#include "ofLog.h"
#include "GUIConstants.h"
#include "ofFileUtils.h"
#include "ofJson.h"
#include <cstring>
#include <set>

MenuBar::MenuBar() {
    initializeAvailableModules();
    // Set recent sessions path
    recentSessionsPath_ = ofFilePath::join(ofFilePath::getUserHomeDir(), "videoTracker_recent_sessions.json");
    loadRecentSessions();
}

void MenuBar::setup(
    std::function<void()> onSavePattern_fn,
    std::function<void()> onLoadPattern_fn,
    std::function<void()> onSaveLayout_fn,
    std::function<void()> onLoadLayout_fn,
    std::function<void(const std::string& moduleType)> onAddModule_fn,
    std::function<void()> onToggleFileBrowser_fn,
    std::function<void()> onToggleConsole_fn,
    std::function<void()> onToggleAssetLibrary_fn,
    std::function<void()> onToggleDemoWindow_fn,
    std::function<void()> onSaveSession_fn,
    std::function<void()> onSaveSessionAs_fn,
    std::function<void()> onOpenSession_fn,
    std::function<void(const std::string& sessionPath)> onOpenRecentSession_fn,
    std::function<void()> onNewSession_fn,
    std::function<std::string()> getCurrentSessionName_fn,
    std::function<void()> onOpenProject_fn,
    std::function<void()> onNewProject_fn,
    std::function<void()> onCloseProject_fn,
    std::function<std::string()> getCurrentProjectName_fn,
    std::function<std::vector<std::string>()> getProjectSessions_fn,
    std::function<void(const std::string& sessionName)> onOpenProjectSession_fn,
    std::function<void()> onImportFile_fn,
    std::function<void()> onImportFolder_fn
) {
    onSavePattern = onSavePattern_fn;
    onLoadPattern = onLoadPattern_fn;
    onSaveLayout = onSaveLayout_fn;
    onLoadLayout = onLoadLayout_fn;
    onAddModule = onAddModule_fn;
    onToggleFileBrowser = onToggleFileBrowser_fn;
    onToggleConsole = onToggleConsole_fn;
    onToggleAssetLibrary = onToggleAssetLibrary_fn;
    onToggleDemoWindow = onToggleDemoWindow_fn;
    
    // Session menu callbacks
    onSaveSession = onSaveSession_fn;
    onSaveSessionAs = onSaveSessionAs_fn;
    onOpenSession = onOpenSession_fn;
    onOpenRecentSession = onOpenRecentSession_fn;
    onNewSession = onNewSession_fn;
    getCurrentSessionName = getCurrentSessionName_fn;
    
    // Project menu callbacks
    onOpenProject = onOpenProject_fn;
    onNewProject = onNewProject_fn;
    onCloseProject = onCloseProject_fn;
    getCurrentProjectName = getCurrentProjectName_fn;
    getProjectSessions = getProjectSessions_fn;
    onOpenProjectSession = onOpenProjectSession_fn;
    
    // File menu callbacks (imports)
    onImportFile = onImportFile_fn;
    onImportFolder = onImportFolder_fn;

    ofLogNotice("MenuBar") << "Setup complete";
}

void MenuBar::initializeAvailableModules() {
    availableModules.clear();
    availableModules.push_back({"MediaPool", "Media Pool", "Video/audio media pool"});
    availableModules.push_back({"TrackerSequencer", "Tracker Sequencer", "Step sequencer for patterns"});
}

void MenuBar::draw() {
    if (ImGui::BeginMainMenuBar()) {
        drawProjectMenu();
        drawSessionMenu();
        drawFileMenu();
        drawAddMenu();
        drawViewMenu();
        drawLayoutMenu();
        drawHelpMenu();
        ImGui::EndMainMenuBar();
    }

    // Help popup - draw every frame if open
    // Note: OpenPopup must be called before BeginPopupModal in the same frame
    // So we check if the flag was set and open it, then show the modal
    if (showControlsHelp && ImGui::BeginPopupModal("Controls Help", &showControlsHelp, 
                                    ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(GUIConstants::Text::Warning, "Controls");
        ImGui::Text("SPACE: Play/Stop");
        ImGui::Text("R: Reset");
        ImGui::Text("G: Toggle GUI");
        ImGui::Text("N: Next media");
        ImGui::Text("M: Previous media");
        ImGui::Text("S: Save session");
        ImGui::Text("MAJ+A: Add Module");
        ImGui::Separator();
        ImGui::TextColored(GUIConstants::Text::Info, "Pattern Editing");
        ImGui::Text("Click cells to edit");
        ImGui::Text("Drag to set values");
        ImGui::Text("Right-click for options");
        ImGui::Separator();
        if (ImGui::Button("Close")) {
            showControlsHelp = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    
    // Add Module popup
    drawAddModulePopup();
}

bool MenuBar::handleKeyPress(int key, bool shiftPressed) {
    // MAJ+a (Shift+A) opens Add Module popup
    if (shiftPressed && (key == 'A' || key == 'a')) {
        showAddModulePopup = true;
        // Reset filter and selection when opening
        memset(addModuleFilter, 0, sizeof(addModuleFilter));
        selectedModuleIndex = 0;
        return true;
    }
    return false;
}

void MenuBar::drawFileMenu() {
    if (ImGui::BeginMenu("File")) {
        // Import File
        if (ImGui::MenuItem("Import File...")) {
            if (onImportFile) onImportFile();
        }
        
        // Import Folder
        if (ImGui::MenuItem("Import Folder...")) {
            if (onImportFolder) onImportFolder();
        }
        
        ImGui::EndMenu();
    }
}

//--------------------------------------------------------------
void MenuBar::drawSessionMenu() {
    if (ImGui::BeginMenu("Session")) {
        // Save Session
        if (ImGui::MenuItem("Save", "Cmd+S")) {
            if (onSaveSession) onSaveSession();
        }
        
        // Save Session As
        if (ImGui::MenuItem("Save As...", "Cmd+Shift+S")) {
            if (onSaveSessionAs) onSaveSessionAs();
        }
        
        ImGui::Separator();
        
        // Open Session
        if (ImGui::MenuItem("Open...", "Cmd+Shift+O")) {
            if (onOpenSession) onOpenSession();
        }
        
        // Open Recent submenu
        if (ImGui::BeginMenu("Open Recent", "Cmd+O")) {
            if (recentSessions_.empty()) {
                ImGui::TextDisabled("No recent sessions");
            } else {
                for (const auto& sessionPath : recentSessions_) {
                    std::string displayName = ofFilePath::getFileName(sessionPath);
                    if (ImGui::MenuItem(displayName.c_str())) {
                        if (onOpenRecentSession) onOpenRecentSession(sessionPath);
                    }
                }
            }
            ImGui::EndMenu();
        }
        
        ImGui::Separator();
        
        // New Session
        if (ImGui::MenuItem("New Session...")) {
            if (onNewSession) onNewSession();
        }
        
        ImGui::Separator();
        
        // Current Session indicator (non-clickable)
        if (getCurrentSessionName) {
            std::string currentSession = getCurrentSessionName();
            if (!currentSession.empty()) {
                std::string sessionLabel = "Current: " + currentSession;
                ImGui::TextDisabled(sessionLabel.c_str());
            } else {
                ImGui::TextDisabled("Current: [unsaved session]");
            }
        } else {
            ImGui::TextDisabled("Current: [unknown]");
        }
        
        ImGui::EndMenu();
    }
}

//--------------------------------------------------------------
void MenuBar::drawProjectMenu() {
    if (ImGui::BeginMenu("Project")) {
        // Open Project
        if (ImGui::MenuItem("Open Project...")) {
            if (onOpenProject) onOpenProject();
        }
        
        ImGui::Separator();
        
        // Current Project indicator and sessions submenu
        if (getCurrentProjectName) {
            std::string projectName = getCurrentProjectName();
            if (!projectName.empty()) {
                // Show current project name (non-clickable)
                std::string projectLabel = "Current: " + projectName;
                ImGui::TextDisabled(projectLabel.c_str());
                
                ImGui::Separator();
                
                // Sessions submenu
                if (ImGui::BeginMenu("Sessions")) {
                    if (getProjectSessions) {
                        auto sessions = getProjectSessions();
                        if (sessions.empty()) {
                            ImGui::TextDisabled("No sessions in project");
                        } else {
                            // Show checkmark for current session
                            std::string currentSession;
                            if (getCurrentSessionName) {
                                currentSession = getCurrentSessionName();
                            }
                            
                            for (const auto& sessionName : sessions) {
                                bool isCurrent = (sessionName == currentSession);
                                if (ImGui::MenuItem(sessionName.c_str(), nullptr, isCurrent, !isCurrent)) {
                                    if (onOpenProjectSession) onOpenProjectSession(sessionName);
                                }
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
            } else {
                ImGui::TextDisabled("No project open");
            }
        }
        
        ImGui::Separator();
        
        // New Project
        if (ImGui::MenuItem("New Project...")) {
            if (onNewProject) onNewProject();
        }
        
        // Close Project (only if project is open)
        if (getCurrentProjectName) {
            std::string projectName = getCurrentProjectName();
            if (!projectName.empty()) {
                if (ImGui::MenuItem("Close Project")) {
                    if (onCloseProject) onCloseProject();
                }
            }
        }
        
        ImGui::EndMenu();
    }
}

void MenuBar::drawViewMenu() {
    if (ImGui::BeginMenu("View")) {
        // Get current visibility state from ViewManager if available
        bool fileBrowserVisible = false;
        bool consoleVisible = false;
        bool assetLibraryVisible = false;
        if (viewManager_) {
            fileBrowserVisible = viewManager_->isFileBrowserVisible();
            consoleVisible = viewManager_->isConsoleVisible();
            assetLibraryVisible = viewManager_->isAssetLibraryVisible();
        }
        if (ImGui::MenuItem("Console", "Cmd+:", consoleVisible)) {
            if (onToggleConsole) onToggleConsole();
        }
        if (ImGui::MenuItem("Asset Library", "Cmd+L", assetLibraryVisible)) {
            if (onToggleAssetLibrary) onToggleAssetLibrary();
        }
        if (ImGui::MenuItem("File Browser", "Cmd+B", fileBrowserVisible)) {
            if (onToggleFileBrowser) onToggleFileBrowser();
        }

        ImGui::Separator();
        if (ImGui::MenuItem("ImGui Demo", "Ctrl+D")) {
            if (onToggleDemoWindow) onToggleDemoWindow();
        }
        ImGui::EndMenu();
    }
}

void MenuBar::drawAddMenu() {
    if (ImGui::BeginMenu("Add")) {
        if (ImGui::MenuItem("Add Module...", "MAJ+A")) {
            showAddModulePopup = true;
            // Reset filter and selection when opening
            memset(addModuleFilter, 0, sizeof(addModuleFilter));
            selectedModuleIndex = 0;
        }
        ImGui::EndMenu();
    }
}

void MenuBar::drawLayoutMenu() {
    if (ImGui::BeginMenu("Layout")) {
        if (ImGui::MenuItem("Save Layout as Default")) {
            if (onSaveLayout) onSaveLayout();
        }
        if (ImGui::MenuItem("Load Default Layout")) {
            if (onLoadLayout) onLoadLayout();
        }
        ImGui::EndMenu();
    }
}

void MenuBar::drawHelpMenu() {
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Controls")) {
            showControlsHelp = true;
            // OpenPopup must be called in the same frame as BeginPopupModal
            // We'll open it immediately here, then BeginPopupModal will be called in draw()
            ImGui::OpenPopup("Controls Help");
        }
        ImGui::EndMenu();
    }
}

void MenuBar::drawAddModulePopup() {
    if (!showAddModulePopup) return;
    
    // Open popup on first frame
    if (showAddModulePopup) {
        ImGui::OpenPopup("Add Module");
    }
    
    // Center popup on screen
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), 
                           ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
    if (ImGui::BeginPopupModal("Add Module", &showAddModulePopup, 
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        // Filter input (auto-focus on first frame)
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::InputText("##filter", addModuleFilter, sizeof(addModuleFilter));
        
        // Build filtered list
        std::vector<int> filteredIndices;
        std::string filterLower = addModuleFilter;
        std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
        
        for (size_t i = 0; i < availableModules.size(); i++) {
            if (filterLower.empty()) {
                filteredIndices.push_back(i);
            } else {
                std::string nameLower = availableModules[i].displayName;
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                
                if (nameLower.find(filterLower) != std::string::npos) {
                    filteredIndices.push_back(i);
                }
            }
        }
        
        // Clamp selected index to valid range
        if (selectedModuleIndex >= static_cast<int>(filteredIndices.size())) {
            selectedModuleIndex = 0;
        }
        if (selectedModuleIndex < 0 && !filteredIndices.empty()) {
            selectedModuleIndex = 0;
        }
        
        // Draw filtered list
        for (size_t listIdx = 0; listIdx < filteredIndices.size(); listIdx++) {
            int moduleIdx = filteredIndices[listIdx];
            const auto& module = availableModules[moduleIdx];
            
            bool isSelected = (static_cast<int>(listIdx) == selectedModuleIndex);
            
            if (ImGui::Selectable(module.displayName.c_str(), isSelected)) {
                selectedModuleIndex = static_cast<int>(listIdx);
            }
        }
        
        // Handle keyboard input
        if (ImGui::IsWindowFocused()) {
            // Arrow key navigation
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && selectedModuleIndex > 0) {
                selectedModuleIndex--;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && 
                selectedModuleIndex < static_cast<int>(filteredIndices.size()) - 1) {
                selectedModuleIndex++;
            }
            
            // Enter to add, Escape to cancel
            bool canAdd = !filteredIndices.empty() && selectedModuleIndex >= 0 && 
                          selectedModuleIndex < static_cast<int>(filteredIndices.size());
            
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) && canAdd) {
                if (onAddModule) {
                    int moduleIdx = filteredIndices[selectedModuleIndex];
                    onAddModule(availableModules[moduleIdx].typeName);
                    showAddModulePopup = false;
                    memset(addModuleFilter, 0, sizeof(addModuleFilter));
                    selectedModuleIndex = 0;
                }
            }
            
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                showAddModulePopup = false;
                memset(addModuleFilter, 0, sizeof(addModuleFilter));
                selectedModuleIndex = 0;
            }
        }
        
        ImGui::EndPopup();
    }
}

//--------------------------------------------------------------
void MenuBar::addToRecentSessions(const std::string& sessionPath) {
    if (sessionPath.empty()) return;
    
    // Normalize path to absolute path for consistent comparison
    std::string normalizedPath = ofFilePath::getAbsolutePath(sessionPath);
    
    // Remove if already exists (compare normalized paths)
    recentSessions_.erase(
        std::remove_if(recentSessions_.begin(), recentSessions_.end(),
            [&normalizedPath](const std::string& existingPath) {
                std::string normalizedExisting = ofFilePath::getAbsolutePath(existingPath);
                return normalizedExisting == normalizedPath;
            }),
        recentSessions_.end()
    );
    
    // Add normalized path to front
    recentSessions_.insert(recentSessions_.begin(), normalizedPath);
    
    // Limit to MAX_RECENT_SESSIONS
    if (recentSessions_.size() > MAX_RECENT_SESSIONS) {
        recentSessions_.resize(MAX_RECENT_SESSIONS);
    }
    
    saveRecentSessions();
}

//--------------------------------------------------------------
void MenuBar::loadRecentSessions() {
    if (!ofFile(recentSessionsPath_).exists()) {
        return;
    }
    
    ofFile file(recentSessionsPath_, ofFile::ReadOnly);
    if (!file.is_open()) {
        return;
    }
    
    try {
        ofJson json = ofJson::parse(file.readToBuffer().getText());
        if (json.contains("recentSessions") && json["recentSessions"].is_array()) {
            recentSessions_.clear();
            std::set<std::string> seenPaths;  // Track normalized paths to prevent duplicates
            
            for (const auto& item : json["recentSessions"]) {
                if (item.is_string()) {
                    std::string path = item.get<std::string>();
                    // Normalize to absolute path
                    std::string normalizedPath = ofFilePath::getAbsolutePath(path);
                    
                    // Only add if file still exists and we haven't seen this normalized path
                    if (ofFile(normalizedPath).exists() && seenPaths.find(normalizedPath) == seenPaths.end()) {
                        recentSessions_.push_back(normalizedPath);
                        seenPaths.insert(normalizedPath);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        ofLogWarning("MenuBar") << "Failed to load recent sessions: " << e.what();
    }
}

//--------------------------------------------------------------
void MenuBar::saveRecentSessions() {
    try {
        ofJson json;
        json["recentSessions"] = recentSessions_;
        
        ofFile file(recentSessionsPath_, ofFile::WriteOnly);
        if (file.is_open()) {
            file << json.dump(2);
        }
    } catch (const std::exception& e) {
        ofLogWarning("MenuBar") << "Failed to save recent sessions: " << e.what();
    }
}

//--------------------------------------------------------------
void MenuBar::setupWithDependencies(
    SessionManager* sessionManager,
    ProjectManager* projectManager,
    AssetLibrary* assetLibrary,
    ViewManager* viewManager,
    FileBrowser* fileBrowser,
    std::function<void(const std::string&)> onAddModule,
    std::function<void()> onSaveLayout,
    std::function<void()> onLoadLayout,
    std::function<void()> onUpdateWindowTitle,
    bool* showDemoWindowPtr
) {
    if (!sessionManager || !projectManager || !assetLibrary || !viewManager || !fileBrowser) {
        ofLogError("MenuBar") << "Cannot setup with dependencies: null pointer(s)";
        return;
    }
    
    // Store ViewManager reference
    viewManager_ = viewManager;
    
    // Setup with internally created callbacks
    setup(
        []() { /* legacy save pattern - not used */ },
        []() { /* legacy load pattern - not used */ },
        onSaveLayout,
        onLoadLayout,
        onAddModule,
        [viewManager]() { 
            bool visible = viewManager->isFileBrowserVisible();
            viewManager->setFileBrowserVisible(!visible);
        },
        [viewManager]() { 
            bool visible = viewManager->isConsoleVisible();
            viewManager->setConsoleVisible(!visible);
        },
        [viewManager]() { 
            bool visible = viewManager->isAssetLibraryVisible();
            viewManager->setAssetLibraryVisible(!visible);
        },
        [showDemoWindowPtr]() { 
            if (showDemoWindowPtr) {
                *showDemoWindowPtr = !(*showDemoWindowPtr);
                ofLogNotice("MenuBar") << "[IMGUI] Toggled Demo Window: " << (*showDemoWindowPtr ? "Visible" : "Hidden");
            }
        },
        // Session menu callbacks
        [sessionManager, projectManager, this, onUpdateWindowTitle]() { 
            // Save Session
            std::string sessionName = sessionManager->getCurrentSessionName();
            if (sessionName.empty()) {
                if (projectManager->isProjectOpen()) {
                    sessionName = projectManager->generateDefaultSessionName();
                } else {
                    sessionName = "session.json";
                }
            }
            if (sessionManager->saveSession(sessionName)) {
                onUpdateWindowTitle();
                std::string sessionPath = projectManager->isProjectOpen() 
                    ? projectManager->getSessionPath(sessionName)
                    : sessionName;
                if (!sessionPath.empty()) {
                    addToRecentSessions(sessionPath);
                }
            }
        },
        [sessionManager, projectManager, this, onUpdateWindowTitle]() { 
            // Save Session As - show dialog
            std::string defaultName = sessionManager->getCurrentSessionName();
            if (defaultName.empty()) {
                defaultName = projectManager->isProjectOpen() 
                    ? projectManager->generateDefaultSessionName()
                    : "session.json";
            }
            auto result = ofSystemSaveDialog(defaultName, "Save Session As");
            if (result.bSuccess) {
                std::string sessionName = ofFilePath::getFileName(result.filePath);
                if (projectManager->isProjectOpen()) {
                    sessionManager->saveSession(sessionName);
                    onUpdateWindowTitle();
                    addToRecentSessions(projectManager->getSessionPath(sessionName));
                } else {
                    sessionManager->saveSessionToPath(result.filePath);
                    onUpdateWindowTitle();
                    addToRecentSessions(result.filePath);
                }
            }
        },
        [sessionManager, this, onUpdateWindowTitle]() { 
            // Open Session - show dialog
            auto result = ofSystemLoadDialog("Open Session", false);
            if (result.bSuccess) {
                if (sessionManager->loadSessionFromPath(result.filePath)) {
                    onUpdateWindowTitle();
                    addToRecentSessions(result.filePath);
                }
            }
        },
        [sessionManager, this, onUpdateWindowTitle](const std::string& sessionPath) {
            // Open Recent Session
            if (sessionManager->loadSessionFromPath(sessionPath)) {
                onUpdateWindowTitle();
                addToRecentSessions(sessionPath);
            }
        },
        [sessionManager, projectManager, this, onUpdateWindowTitle]() { 
            // New Session - create in current project
            if (!projectManager->isProjectOpen()) {
                ofLogWarning("MenuBar") << "Cannot create session: no project open";
                return;
            }
            std::string newSessionName = projectManager->generateDefaultSessionName();
            if (projectManager->createSessionFile(newSessionName)) {
                if (sessionManager->loadSession(newSessionName)) {
                    onUpdateWindowTitle();
                    std::string sessionPath = projectManager->getSessionPath(newSessionName);
                    if (!sessionPath.empty()) {
                        addToRecentSessions(sessionPath);
                    }
                }
            }
        },
        [sessionManager]() { 
            // Get Current Session Name
            return sessionManager->getCurrentSessionName();
        },
        // Project menu callbacks
        [projectManager, sessionManager, assetLibrary, fileBrowser, onUpdateWindowTitle]() { 
            // Open Project - show dialog
            auto result = ofSystemLoadDialog("Open Project", true);
            if (result.bSuccess) {
                std::string projectPath = result.filePath;
                if (projectManager->openProject(projectPath)) {
                    // Set FileBrowser to project directory
                    fileBrowser->setProjectDirectory(projectManager->getProjectRoot());
                    // Initialize AssetLibrary with project assets
                    assetLibrary->initialize();
                    onUpdateWindowTitle();
                    // Try to load default session
                    auto sessions = projectManager->listSessions();
                    if (!sessions.empty()) {
                        sessionManager->loadSession(sessions[0]);
                        onUpdateWindowTitle();
                    }
                }
            }
        },
        [projectManager, sessionManager, assetLibrary, fileBrowser, this, onUpdateWindowTitle]() { 
            // New Project - show dialog
            auto result = ofSystemSaveDialog("MyProject", "Create New Project");
            if (result.bSuccess) {
                std::string projectPath = ofFilePath::getEnclosingDirectory(result.filePath);
                std::string projectName = ofFilePath::getFileName(result.filePath);
                if (projectManager->createProject(projectPath, projectName)) {
                    // Set FileBrowser to project directory
                    fileBrowser->setProjectDirectory(projectManager->getProjectRoot());
                    // Initialize AssetLibrary with project assets
                    assetLibrary->initialize();
                    onUpdateWindowTitle();
                    // Create default session
                    std::string defaultSession = projectManager->generateDefaultSessionName();
                    if (projectManager->createSessionFile(defaultSession)) {
                        sessionManager->loadSession(defaultSession);
                        onUpdateWindowTitle();
                        std::string sessionPath = projectManager->getSessionPath(defaultSession);
                        if (!sessionPath.empty()) {
                            addToRecentSessions(sessionPath);
                        }
                    }
                }
            }
        },
        [projectManager, fileBrowser, onUpdateWindowTitle]() { 
            // Close Project
            // Reset FileBrowser to user home directory
            fileBrowser->setProjectDirectory(ofFilePath::getUserHomeDir());
            projectManager->closeProject();
            onUpdateWindowTitle();
        },
        [projectManager]() { 
            // Get Current Project Name
            return projectManager->isProjectOpen() ? projectManager->getProjectName() : std::string();
        },
        [projectManager]() {
            // Get Project Sessions
            if (projectManager->isProjectOpen()) {
                return projectManager->listSessions();
            }
            return std::vector<std::string>();
        },
        [sessionManager, projectManager, this, onUpdateWindowTitle](const std::string& sessionName) {
            // Open Project Session
            if (sessionManager->loadSession(sessionName)) {
                onUpdateWindowTitle();
                std::string sessionPath = projectManager->getSessionPath(sessionName);
                if (!sessionPath.empty()) {
                    addToRecentSessions(sessionPath);
                }
            }
        },
        // File menu callbacks (imports)
        [assetLibrary]() { 
            // Import File
            auto result = ofSystemLoadDialog("Select media file to import", false);
            if (result.bSuccess) {
                std::vector<std::string> files;
                files.push_back(result.filePath);
                assetLibrary->importFiles(files, "");
                ofLogNotice("MenuBar") << "Imported file to AssetLibrary: " << result.filePath;
            }
        },
        [assetLibrary]() { 
            // Import Folder
            auto result = ofSystemLoadDialog("Select folder to import", true);
            if (result.bSuccess) {
                std::string folderName = ofFilePath::getFileName(result.filePath);
                if (folderName.empty()) {
                    folderName = ofFilePath::getBaseName(result.filePath);
                }
                assetLibrary->importFolder(result.filePath, folderName);
                ofLogNotice("MenuBar") << "Imported folder to AssetLibrary: " << result.filePath;
            }
        }
    );
    
    ofLogNotice("MenuBar") << "Setup with dependencies complete";
}

