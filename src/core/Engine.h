#pragma once

#include "EngineState.h"
#include "Clock.h"
#include "ModuleRegistry.h"
#include "ModuleFactory.h"
#include "ParameterRouter.h"
#include "ConnectionManager.h"
#include "SessionManager.h"
#include "ProjectManager.h"
#include "CommandExecutor.h"
#include "MediaConverter.h"
#include "AssetLibrary.h"
#include "PatternRuntime.h"
#include "modules/AudioOutput.h"
#include "modules/VideoOutput.h"
#include "ofxSoundObjects.h"
#include <memory>
#include <shared_mutex>
#include <atomic>
#include <vector>
#include <functional>

// Forward declarations
class GUIManager;
class ViewManager;

namespace vt {

struct EngineConfig {
    std::string masterAudioOutName = "masterAudioOut";
    std::string masterVideoOutName = "masterVideoOut";
    bool enableAutoSave = true;
    float autoSaveInterval = 30.0f;
};

/**
 * Engine - The central headless core
 * 
 * Responsibilities:
 * - Own all modules and their connections
 * - Execute commands (from any UI)
 * - Provide state snapshots for rendering
 * - Handle audio/video I/O
 * 
 * Does NOT:
 * - Render UI (that's for shells)
 * - Handle input events directly (shells do that)
 * - Know about ImGui, windows, or GL contexts
 */
class Engine {
public:
    Engine();
    ~Engine();
    
    // ═══════════════════════════════════════════════════════════
    // INITIALIZATION
    // ═══════════════════════════════════════════════════════════
    
    void setup(const EngineConfig& config = {});
    void setupAudio(int sampleRate = 44100, int bufferSize = 512);
    
    // ═══════════════════════════════════════════════════════════
    // COMMAND INTERFACE (Primary API for all UIs)
    // ═══════════════════════════════════════════════════════════
    
    struct Result {
        bool success = false;
        std::string message;
        std::string error;
        
        Result() = default;
        Result(bool s, const std::string& msg) : success(s), message(msg) {}
        Result(bool s, const std::string& msg, const std::string& err) 
            : success(s), message(msg), error(err) {}
    };
    
    // Execute any command - returns result with success/error
    Result executeCommand(const std::string& command);
    
    // Script execution (for future Lua integration)
    Result eval(const std::string& script);
    Result evalFile(const std::string& path);
    
    // ═══════════════════════════════════════════════════════════
    // STATE OBSERVATION (Read-Only Snapshots)
    // ═══════════════════════════════════════════════════════════
    
    // Get complete engine state (immutable snapshot)
    EngineState getState() const;
    
    // Get specific module state
    EngineState::ModuleState getModuleState(const std::string& name) const;
    
    // Subscribe to state changes
    using StateObserver = std::function<void(const EngineState&)>;
    size_t subscribe(StateObserver callback);
    void unsubscribe(size_t id);
    
    // ═══════════════════════════════════════════════════════════
    // AUDIO/VIDEO CALLBACKS (for integration with host app)
    // ═══════════════════════════════════════════════════════════
    
    void audioOut(ofSoundBuffer& buffer);
    void update(float deltaTime);
    
    // ═══════════════════════════════════════════════════════════
    // SESSION MANAGEMENT
    // ═══════════════════════════════════════════════════════════
    
    bool loadSession(const std::string& path);
    bool saveSession(const std::string& path);
    std::string serializeState() const;  // JSON/YAML
    bool deserializeState(const std::string& data);
    
    // ═══════════════════════════════════════════════════════════
    // DIRECT ACCESS (for performance-critical operations)
    // ═══════════════════════════════════════════════════════════
    
    Clock& getClock() { return clock_; }
    ModuleRegistry& getModuleRegistry() { return moduleRegistry_; }
    ModuleFactory& getModuleFactory() { return moduleFactory_; }
    ConnectionManager& getConnectionManager() { return connectionManager_; }
    ParameterRouter& getParameterRouter() { return parameterRouter_; }
    SessionManager& getSessionManager() { return sessionManager_; }
    ProjectManager& getProjectManager() { return projectManager_; }
    AssetLibrary& getAssetLibrary() { return assetLibrary_; }
    CommandExecutor& getCommandExecutor() { return commandExecutor_; }
    PatternRuntime& getPatternRuntime() { return patternRuntime_; }
    const PatternRuntime& getPatternRuntime() const { return patternRuntime_; }
    
    // Get master outputs
    std::shared_ptr<AudioOutput> getMasterAudioOut() const { return masterAudioOut_; }
    std::shared_ptr<VideoOutput> getMasterVideoOut() const { return masterVideoOut_; }
    
    // ═══════════════════════════════════════════════════════════
    // CALLBACK SETUP (for GUI integration)
    // ═══════════════════════════════════════════════════════════
    
    // Set callbacks for GUI components that need engine events
    void setOnProjectOpened(std::function<void()> callback) { onProjectOpened_ = callback; }
    void setOnProjectClosed(std::function<void()> callback) { onProjectClosed_ = callback; }
    void setOnUpdateWindowTitle(std::function<void()> callback) { onUpdateWindowTitle_ = callback; }
    
    // Setup GUI managers (called from ofApp)
    void setupGUIManagers(GUIManager* guiManager, ViewManager* viewManager);
    
    // Get GUIManager pointer (for components that need it)
    GUIManager* getGUIManager() const { return guiManager_; }
    
private:
    // Core components (already exist, just moved from ofApp)
    Clock clock_;
    PatternRuntime patternRuntime_;  // Foundational system for pattern management
    ModuleRegistry moduleRegistry_;
    ModuleFactory moduleFactory_;
    ConnectionManager connectionManager_;
    ParameterRouter parameterRouter_;
    SessionManager sessionManager_;
    ProjectManager projectManager_;
    MediaConverter mediaConverter_;
    AssetLibrary assetLibrary_;
    
    // Master outputs
    std::shared_ptr<AudioOutput> masterAudioOut_;
    std::shared_ptr<VideoOutput> masterVideoOut_;
    
    // Command execution
    CommandExecutor commandExecutor_;
    
    // Configuration
    EngineConfig config_;
    bool isSetup_ = false;
    
    // State observation
    mutable std::shared_mutex stateMutex_;
    std::vector<std::pair<size_t, StateObserver>> observers_;
    std::atomic<size_t> nextObserverId_{0};
    
    // Callbacks
    std::function<void()> onProjectOpened_;
    std::function<void()> onProjectClosed_;
    std::function<void()> onUpdateWindowTitle_;
    
    // GUI manager reference (set during setupGUIManagers)
    GUIManager* guiManager_ = nullptr;
    
    // Internal methods
    void notifyStateChange();
    EngineState buildStateSnapshot() const;
    
    // Setup helpers
    void setupCoreSystems();
    void setupMasterOutputs();
    void setupCommandExecutor();
    void initializeProjectAndSession();
    
    // State building helpers
    void buildModuleStates(EngineState& state) const;
    void buildConnectionStates(EngineState& state) const;
    void buildTransportState(EngineState& state) const;
};

} // namespace vt

