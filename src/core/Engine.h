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
#include "ScriptManager.h"
#include "Command.h"
#include "modules/AudioOutput.h"
#include "modules/VideoOutput.h"
#include "ofxSoundObjects.h"
#include "ofxLua.h"
#include "readerwriterqueue.h"
#include "../../libs/concurrentqueue/blockingconcurrentqueue.h"  // From moodycamel (Phase 7.3)
#include <memory>
#include <shared_mutex>
#include <mutex>
#include <atomic>
#include <vector>
#include <functional>
#include <cstdint>
#include <thread>
#include <chrono>
#include <cassert>

// Forward declarations
class GUIManager;
class ViewManager;

namespace vt {
namespace shell {
    class Shell;  // Forward declaration for shell registry
}
}

namespace vt {

// ═══════════════════════════════════════════════════════════
// THREAD SAFETY ASSERTIONS (Phase 7.9 Plan 5)
// ═══════════════════════════════════════════════════════════

// Thread safety assertion macros
// These verify thread safety contracts at runtime to catch violations during development

#ifdef NDEBUG
    // Release mode: assertions disabled (no performance overhead)
    #define ASSERT_MAIN_THREAD() ((void)0)
    #define ASSERT_AUDIO_THREAD() ((void)0)
    #define ASSERT_THREAD_SAFE() ((void)0)
#else
    // Debug mode: assertions enabled
    // Note: Thread ID comparison is fast (single atomic read + comparison)
    #define ASSERT_MAIN_THREAD() \
        do { \
            if (!Engine::isMainThread()) { \
                ofLogError("Engine") << "ASSERT_MAIN_THREAD failed: called from thread " \
                    << std::this_thread::get_id() << ", expected main thread " \
                    << Engine::getMainThreadId(); \
                assert(false && "Must be called from main thread"); \
            } \
        } while(0)
    
    #define ASSERT_AUDIO_THREAD() \
        do { \
            if (!Engine::isAudioThread()) { \
                ofLogError("Engine") << "ASSERT_AUDIO_THREAD failed: called from thread " \
                    << std::this_thread::get_id() << ", expected audio thread " \
                    << Engine::getAudioThreadId(); \
                assert(false && "Must be called from audio thread"); \
            } \
        } while(0)
    
    #define ASSERT_THREAD_SAFE() \
        do { \
            // Thread-safe operations can be called from any thread
            // This assertion is a placeholder for future thread-safe verification
            // Currently, thread-safe operations are verified by design (lock-free)
        } while(0)
#endif


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
    
    /**
     * Execute a command string (e.g., "start", "bpm 120").
     * 
     * THREAD-SAFE: Can be called from any thread (main thread or script execution thread).
     * State updates are enqueued to main thread via notification queue to ensure thread safety.
     * 
     * @param command Command string to execute
     * @return Result indicating success/failure
     */
    Result executeCommand(const std::string& command);
    
    /**
     * Execute Lua script synchronously (blocks until completion).
     * 
     * Use this for:
     * - Internal engine operations that need immediate results
     * - Scripts that must complete before continuing
     * 
     * @param script Lua script to execute
     * @return Result indicating success/failure
     */
    Result eval(const std::string& script);
    Result evalFile(const std::string& path);
    
    /**
     * Execute script with sync contract (Script → Engine synchronization).
     * 
     * This method executes script and waits for state to be updated before calling callback.
     * Guarantees script changes are reflected in engine state before callback fires.
     * 
     * Uses:
     * - Command queue synchronization (waits for commands to be processed)
     * - State versioning (waits for state version to be updated)
     * 
     * @param script Script to execute
     * @param callback Callback function (executed when sync is complete)
     *                  Receives success/failure status
     */
    void syncScriptToEngine(const std::string& script, std::function<void(bool success)> callback);
    
    // ═══════════════════════════════════════════════════════════
    // SHELL-SAFE API (Shells should ONLY use these methods)
    // ═══════════════════════════════════════════════════════════
    // 
    // Shells (CodeShell, EditorShell, CommandShell, CLIShell) should
    // ONLY use the methods in this section. All other methods are for
    // internal use, Lua bindings, or GUI components.
    // 
    // Shell-Safe Methods:
    // - getState() - Read state via immutable snapshots
    // - executeCommand() - Change state via commands
    // - enqueueCommand() - Queue commands for audio thread
    // - subscribe() / unsubscribe() - Subscribe to state changes
    // - setScriptUpdateCallback() - Register script update callback
    // - setScriptAutoUpdate() - Control script auto-update
    // - isScriptAutoUpdateEnabled() - Check auto-update status
    // 
    // See docs/SHELL_ABSTRACTION.md for complete pattern documentation.
    
    // ═══════════════════════════════════════════════════════════
    // STATE OBSERVATION (Read-Only Snapshots)
    // ═══════════════════════════════════════════════════════════
    
    // Get complete engine state (immutable snapshot)
    EngineState getState() const;
    
    // Lock-free read (atomic pointer load via mutex for C++17 compatibility)
    // Returns immutable state snapshot that is never modified after creation
    // MEMORY SAFETY: Returns shared_ptr to const EngineState (immutable)
    // Thread-safe: Can be called from any thread (lock-free read via mutex)
    std::shared_ptr<const EngineState> getImmutableStateSnapshot() const;
    
    // Update snapshot from runtime state (only during safe periods)
    // CRITICAL: Only called during safe periods (not during script execution or command processing)
    // Creates new immutable snapshot from current state and atomically swaps pointer
    // Thread-safe: Must be called from main thread during safe periods
    void updateImmutableStateSnapshot();
    
    // Get specific module state
    EngineState::ModuleState getModuleState(const std::string& name) const;
    
    // ═══════════════════════════════════════════════════════════
    // STATE SNAPSHOT (Lock-Free Serialization)
    // ═══════════════════════════════════════════════════════════
    
    /**
     * Get current engine state snapshot (lock-free read).
     * Returns immutable JSON snapshot that can be serialized without locks.
     * 
     * THREAD-SAFE: Lock-free read (atomic pointer load).
     * Can be called from any thread without locks.
     * 
     * @return Shared pointer to immutable JSON snapshot, or nullptr if snapshot not yet created
     */
    std::shared_ptr<const ofJson> getStateSnapshot() const {
        // Memory barrier ensures we see the latest snapshot update
        std::atomic_thread_fence(std::memory_order_acquire);
        std::lock_guard<std::mutex> lock(snapshotJsonMutex_);
        return snapshotJson_;  // Fast read with mutex (C++17 compatible)
    }
    
    /**
     * Get current state version number.
     * Used for snapshot staleness detection.
     * 
     * THREAD-SAFE: Atomic read.
     * 
     * @return Current state version number
     */
    uint64_t getStateVersion() const {
        // Use acquire semantics to ensure we see latest state version
        return stateVersion_.load(std::memory_order_acquire);
    }
    
    /**
     * @deprecated DEPRECATED in Phase 1 - Do not use.
     * 
     * This function is inherently broken when called from the main thread because:
     *   1. It blocks the main thread waiting for state version to increment
     *   2. State version only increments in updateStateSnapshot()
     *   3. updateStateSnapshot() is called from processNotificationQueue()
     *   4. processNotificationQueue() runs in update() on the main thread
     *   5. Main thread is blocked → deadlock
     * 
     * Kept for backward compatibility but logs a warning if called.
     * Use the asynchronous observer pattern instead.
     * 
     * @param targetVersion Target version number to wait for
     * @param timeoutMs Timeout in milliseconds (ignored - returns immediately)
     * @return Always returns false
     */
    [[deprecated("Use asynchronous observer pattern instead - this function causes deadlocks")]]
    bool waitForStateVersion(uint64_t targetVersion, uint64_t timeoutMs = 2000);
    
    /**
     * Sync engine state to editor shells with completion guarantee (Engine → Editor Shell synchronization).
     * 
     * This method ensures editor shells receive state updates with completion guarantee.
     * Uses event-driven notification queue for proper ordering.
     * 
     * @param callback Callback function (executed when sync is complete)
     *                  Receives state snapshot when sync is complete
     */
    void syncEngineToEditor(std::function<void(const EngineState&)> callback);
    
    // ═══════════════════════════════════════════════════════════
    // SHELL-SAFE API (for ScriptManager operations)
    // ═══════════════════════════════════════════════════════════
    
    /**
     * Set script update callback (Shell-safe API)
     * Wraps ScriptManager::setScriptUpdateCallback()
     * @param callback Function called when script updates
     */
    void setScriptUpdateCallback(std::function<void(const std::string&, uint64_t)> callback);
    
    /**
     * Clear script update callback (Shell-safe API)
     * Wraps ScriptManager::clearScriptUpdateCallback()
     * IMPORTANT: Call this in Shell::exit() to prevent use-after-free crashes
     * @return Current auto-update enabled status
     */
    void clearScriptUpdateCallback();
    
    /**
     * Set script auto-update enabled/disabled (Shell-safe API)
     * Wraps ScriptManager::setAutoUpdate()
     * @param enabled Whether to enable auto-updates
     */
    void setScriptAutoUpdate(bool enabled);
    
    /**
     * Check if script auto-update is enabled (Shell-safe API)
     * Wraps ScriptManager::isAutoUpdateEnabled()
     * @return true if auto-update is enabled
     */
    bool isScriptAutoUpdateEnabled() const;
    
    // ═══════════════════════════════════════════════════════════
    // COMMAND QUEUE (Unified command processing)
    // ═══════════════════════════════════════════════════════════
    
    /**
     * Enqueue a command for processing in the audio thread
     * @param cmd Command to enqueue (takes ownership)
     * @return true if enqueued successfully, false if queue is full
     */
    bool enqueueCommand(std::unique_ptr<Command> cmd);
    
    /**
     * Process all queued commands (called from audio thread)
     * @return Number of commands processed
     */
    int processCommands();
    
    /**
     * Execute a command immediately (synchronous, for non-audio operations).
     * 
     * THREAD-SAFE: Can be called from any thread (main thread via ClockGUI, or script execution thread via SWIG).
     * State updates are enqueued to main thread via notification queue to ensure thread safety.
     * 
     * @param cmd Command to execute (takes ownership)
     */
    void executeCommandImmediate(std::unique_ptr<Command> cmd);
    
    // Subscribe to state changes
    // 
    // Observer Safety Requirements:
    // - Observers MUST be read-only: only read state, never modify it
    // - Observers MUST NOT call enqueueCommand() or executeCommand() during notification
    // - Observers MUST NOT call notifyStateChange() (would cause recursive notifications)
    // - Observers MUST handle exceptions internally (exceptions are caught and logged, observer removed)
    // - Observers receive state via getState() which handles unsafe periods by returning cached state
    // - Observers may be called during command processing or script execution (state is cached)
    // 
    // Violations will cause the observer to be removed automatically.
    using StateObserver = std::function<void(const EngineState&)>;
    size_t subscribe(StateObserver callback);
    void unsubscribe(size_t id);
    
    // ═══════════════════════════════════════════════════════════
    // MULTI-SHELL COORDINATION (Phase 7.9.2 Plan 3)
    // ═══════════════════════════════════════════════════════════
    
    /**
     * Register a shell for coordinated state updates.
     * Shells are notified in registration order (FIFO) to ensure consistent state.
     * 
     * @param shell Shell to register (must not be null)
     */
    void registerShell(vt::shell::Shell* shell);
    
    /**
     * Unregister a shell from coordinated state updates.
     * 
     * @param shell Shell to unregister
     */
    void unregisterShell(vt::shell::Shell* shell);
    
    /**
     * Notify all registered shells of state update.
     * Ensures all shells receive state updates in registration order (FIFO).
     * 
     * @param state Current engine state
     * @param stateVersion State version number
     */
    void notifyAllShells(const EngineState& state, uint64_t stateVersion);
    
    // ═══════════════════════════════════════════════════════════
    // AUDIO/VIDEO CALLBACKS (for integration with host app)
    // ═══════════════════════════════════════════════════════════
    
    void audioOut(ofSoundBuffer& buffer);
    void update(float deltaTime);

    // Process all pending notifications immediately
    // Redesign: Made public for shell transitions to ensure state consistency
    void processNotificationQueue();

    // ═══════════════════════════════════════════════════════════
    // SESSION MANAGEMENT
    // ═══════════════════════════════════════════════════════════
    
    bool loadSession(const std::string& path);
    bool saveSession(const std::string& path);
    std::string serializeState() const;  // JSON/YAML
    bool deserializeState(const std::string& data);
    
    // ═══════════════════════════════════════════════════════════
    // DIRECT ACCESS (NOT for Shells - Internal/Lua/GUI use only)
    // ═══════════════════════════════════════════════════════════
    // 
    // WARNING: Shells should NOT use these methods!
    // These are for:
    // - Lua bindings (SWIG code generation)
    // - GUI components (GUIManager, ModuleGUI, etc.)
    // - Engine internal code
    // 
    // Shells should use Shell-safe API methods instead (see above).
    // 
    // If you're implementing a Shell and need access to these, you're
    // doing something wrong. Use getState() and executeCommand() instead.
    
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
    ScriptManager& getScriptManager() { return scriptManager_; }
    
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
    
    // UI operation callbacks (optional, registered by Shells)
    void setOnModuleAdded(std::function<void(const std::string&)> callback) { onModuleAdded_ = callback; }
    void setOnModuleRemoved(std::function<void(const std::string&)> callback) { onModuleRemoved_ = callback; }
    
    
    // Unsafe state detection (consolidated from multiple flags)
    // Purpose: Single source of truth for unsafe state detection
    // Uses bitmask to track multiple unsafe conditions simultaneously
    // Bit 0: Script executing
    // Bit 1: Commands being processed
    // Bit 2+: Reserved for future use
    enum class UnsafeState : uint8_t {
        NONE = 0,
        SCRIPT_EXECUTING = 1 << 0,
        COMMANDS_PROCESSING = 1 << 1
    };
    
    // Check if script is currently executing (for components that need to defer operations)
    bool isExecutingScript() const { return hasUnsafeState(UnsafeState::SCRIPT_EXECUTING); }
    
    /**
     * Check if we're currently building a state snapshot (thread-local check)
     * Used to prevent recursive calls during snapshot building
     * @return true if buildStateSnapshot() is currently executing on this thread
     */
    static bool isBuildingSnapshot() { return isBuildingSnapshot_; }
    
    // ═══════════════════════════════════════════════════════════
    // THREAD ID TRACKING (Phase 7.9 Plan 5)
    // ═══════════════════════════════════════════════════════════
    
    /**
     * Set main thread ID (called during Engine::setup())
     * @param threadId Main thread ID
     */
    static void setMainThreadId(std::thread::id threadId) { mainThreadId_ = threadId; }
    
    /**
     * Set audio thread ID (called during Engine::setupAudio())
     * @param threadId Audio thread ID
     */
    static void setAudioThreadId(std::thread::id threadId) { audioThreadId_ = threadId; }
    
    /**
     * Get main thread ID
     * @return Main thread ID
     */
    static std::thread::id getMainThreadId() { return mainThreadId_; }
    
    /**
     * Get audio thread ID
     * @return Audio thread ID
     */
    static std::thread::id getAudioThreadId() { return audioThreadId_; }
    
    /**
     * Check if current thread is main thread
     * @return true if current thread is main thread
     */
    static bool isMainThread() {
        return std::this_thread::get_id() == mainThreadId_;
    }
    
    /**
     * Check if current thread is audio thread
     * @return true if current thread is audio thread
     */
    static bool isAudioThread() {
        return std::this_thread::get_id() == audioThreadId_;
    }
    bool commandsBeingProcessed() const { return hasUnsafeState(UnsafeState::COMMANDS_PROCESSING); }
    bool hasPendingCommands() const { return commandQueue_.size_approx() > 0; }
    bool isInUnsafeState() const { 
        // Use acquire semantics to ensure we see latest unsafe state flags
        // Phase 2 Simplification: Removed parametersBeingModified_ check
        // All state modifications now go through command queue, so SCRIPT_EXECUTING and COMMANDS_PROCESSING flags are sufficient
        std::atomic_thread_fence(std::memory_order_acquire);
        return unsafeStateFlags_.load(std::memory_order_acquire) != 0; 
    }
    
    // Render guard (prevents state updates during rendering)
    bool isRendering() const { return isRendering_.load(std::memory_order_acquire); }
    void setRendering(bool rendering) { 
        isRendering_.store(rendering, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }
    
    // Public method for ParameterRouter to notify state changes
    void notifyParameterChanged();  // Debounced parameter change notification for script sync
    
    // Event handlers
    void onBPMChanged(float& newBpm);  // Called when Clock BPM changes
    
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
    
    // Script management (generates Lua from state)
    ScriptManager scriptManager_;
    
    // Lua scripting
    std::unique_ptr<ofxLua> lua_;
    
    // Configuration
    EngineConfig config_;
    bool isSetup_ = false;
    
    // Frame counter for deferring notification processing during startup
    // Skip notification processing for first few frames to allow window to appear
    mutable std::atomic<size_t> updateFrameCount_{0};
    
    // State observation
    // Purpose: Protects observers_ vector during registration/unregistration
    // When used: subscribe() and unsubscribe() methods (called by Shells)
    // Still needed: Yes - Shells register/unregister observers dynamically
    // Can be simplified: No - shared_mutex allows concurrent reads, exclusive writes
    mutable std::shared_mutex stateMutex_;
    std::vector<std::pair<size_t, StateObserver>> observers_;
    // Purpose: Thread-safe observer ID generation
    // When used: subscribe() method to assign unique IDs
    // Still needed: Yes - Multiple threads may subscribe concurrently
    // Can be simplified: No - Atomic counter is optimal
    std::atomic<size_t> nextObserverId_{0};
    
    // Multi-shell coordination (Phase 7.9.2 Plan 3)
    // Shells registered for coordinated state updates (FIFO order)
    // Protected by stateMutex_ (same mutex as observers_)
    std::vector<vt::shell::Shell*> registeredShells_;
    
    // Unsafe state flags (implementation detail - enum is public for inline method access)
    std::atomic<uint8_t> unsafeStateFlags_{0};
    
    // Helper methods for unsafe state management
    void setUnsafeState(UnsafeState state, bool active);
    bool hasUnsafeState(UnsafeState state) const;
    
    // Render guard (prevents state updates during rendering)
    // Purpose: Flag indicating rendering is in progress (ImGui draw() method)
    // When used: notifyStateChange() checks this to defer notifications during rendering
    // Still needed: Yes - Prevents crashes from state updates during ImGui rendering
    // Can be simplified: No - Critical for preventing crashes during rendering
    std::atomic<bool> isRendering_{false};
    
    // Unified command queue (lock-free, processed in audio thread)
    // Producer: GUI thread (enqueueCommand)
    // Consumer: Audio thread (processCommands)
    // Capacity: 1024 commands (should be more than enough)
    moodycamel::ReaderWriterQueue<std::unique_ptr<Command>> commandQueue_{1024};
    
    // Event-driven notification queue (unified notification mechanism)
    // Purpose: Queue stores all state notification callbacks that are processed in update()
    // When used: All state changes enqueue notifications here, update() processes queue on main thread
    // Simplification: Removed stateNeedsNotification_ flag - queue is single source of truth
    // Still needed: Yes - Ensures notifications happen on main thread event loop, not during rendering or from audio thread
    moodycamel::BlockingConcurrentQueue<std::function<void()>> notificationQueue_;
    
    // Recursive notification guard (prevents observers from triggering recursive notifications)
    // Purpose: Prevents infinite recursion if observer calls notifyStateChange()
    // When used: notifyStateChange() checks this before notifying, sets during notification
    // Still needed: Yes - Prevents recursive notification crashes
    // Can be simplified: No - Guard pattern is necessary
    std::atomic<bool> notifyingObservers_{false};
    
    // Command statistics (for debugging)
    struct CommandStats {
        uint64_t commandsProcessed = 0;
        uint64_t commandsDropped = 0;
        uint64_t queueOverflows = 0;
    };
    CommandStats commandStats_;
    
    // Queue monitoring statistics (Phase 7.9 Plan 8.2)
    struct QueueMonitorStats {
        // Notification queue
        size_t notificationQueueCurrent = 0;
        size_t notificationQueueMax = 0;
        size_t notificationQueueTotalProcessed = 0;
        size_t notificationQueueTotalEnqueued = 0;
        uint64_t notificationQueueLastLogTime = 0;
        
        // Command queue
        size_t commandQueueCurrent = 0;
        size_t commandQueueMax = 0;
        size_t commandQueueTotalProcessed = 0;
        size_t commandQueueTotalEnqueued = 0;
        uint64_t commandQueueLastLogTime = 0;
        
        // State version
        uint64_t stateVersionIncrements = 0;
        uint64_t stateVersionLastValue = 0;
        uint64_t stateVersionLastLogTime = 0;
        uint64_t stateVersionMaxGap = 0;
        uint64_t stateVersionSyncTimeouts = 0;
    };
    QueueMonitorStats queueMonitorStats_;
    
    // Monitoring thresholds (Phase 7.9 Plan 8.2)
    static constexpr size_t NOTIFICATION_QUEUE_WARNING_THRESHOLD = 100;
    static constexpr size_t NOTIFICATION_QUEUE_ERROR_THRESHOLD = 500;
    static constexpr size_t COMMAND_QUEUE_WARNING_THRESHOLD = 500;
    static constexpr size_t COMMAND_QUEUE_ERROR_THRESHOLD = 900;
    static constexpr uint64_t STATE_VERSION_RATE_WARNING_THRESHOLD = 100;  // increments/sec
    static constexpr uint64_t STATE_VERSION_RATE_ERROR_THRESHOLD = 1000;    // increments/sec
    static constexpr uint64_t STATE_VERSION_GAP_WARNING_THRESHOLD = 10;
    static constexpr uint64_t MONITORING_LOG_INTERVAL_MS = 5000;  // Log stats every 5 seconds
    
    // State snapshot throttling (prevents excessive expensive snapshot building)
    // Purpose: Limits buildStateSnapshot() frequency to prevent performance issues
    // When used: buildStateSnapshot() checks this before building (throttles to 100ms)
    // Still needed: Yes - Prevents excessive snapshot building (expensive operation)
    // Can be simplified: No - Throttling is necessary for performance
    std::atomic<uint64_t> lastStateSnapshotTime_{0};
    static constexpr uint64_t STATE_SNAPSHOT_THROTTLE_MS = 100;  // Max once per 100ms
    
    // Guard to prevent concurrent snapshot building
    // Purpose: Prevents multiple threads from building snapshots simultaneously
    // When used: buildStateSnapshot() acquires snapshotMutex_ for exclusive access
    // Still needed: Yes - buildStateSnapshot() is still used for building immutable snapshots
    // Note: Serialization no longer uses this (uses getStateSnapshot() instead - lock-free)
    mutable std::mutex snapshotMutex_;
    
    // Thread-local flag to detect recursive snapshot building
    // Purpose: Prevents getCurrentScript() from calling getState() during buildStateSnapshot()
    // When used: buildStateSnapshot() sets this flag, getCurrentScript() checks it
    // Still needed: Yes - Prevents deadlock when buildStateSnapshot() calls getCurrentScript()
    static thread_local bool isBuildingSnapshot_;
    
    // Thread ID tracking (Phase 7.9 Plan 5)
    // Purpose: Track main and audio thread IDs for thread safety assertions
    // When used: ASSERT_MAIN_THREAD() and ASSERT_AUDIO_THREAD() macros
    // Still needed: Yes - Enables runtime thread safety verification
    static std::thread::id mainThreadId_;
    static std::thread::id audioThreadId_;
    
    // Immutable JSON snapshot for lock-free serialization
    // Purpose: Lock-free JSON snapshot for serialization (from Phase 7.2)
    // When used: getStateSnapshot() reads this, updateStateSnapshot() updates this
    // Still needed: Yes - Core of lock-free serialization system
    // Can be simplified: No - This is the new lock-free pattern
    // Note: Stored as mutex-protected shared_ptr (C++17 compatible - std::atomic<shared_ptr> requires C++20)
    //       Mutex is only used for pointer updates, reads are fast (single mutex lock)
    //       Consistent with Phase 7.1's Module snapshot pattern
    mutable std::mutex snapshotJsonMutex_;
    mutable std::shared_ptr<const ofJson> snapshotJson_;
    
    // State version number (incremented on each snapshot update)
    // Purpose: Version tracking for snapshot staleness detection (from Phase 7.2)
    // When used: SessionManager checks version before serializing to detect stale snapshots
    // Still needed: Yes - Critical for async serialization staleness detection
    // Can be simplified: No - Version tracking is necessary
    std::atomic<uint64_t> stateVersion_{0};
    
    // Immutable state snapshot (Phase 7.9-9.2)
    // Purpose: Lock-free immutable state snapshots to prevent memory corruption during script execution
    // When used: getState() and notifyStateChange() use immutable snapshots instead of building during unsafe periods
    // Still needed: Yes - Core of immutable state pattern to prevent race conditions
    // Can be simplified: No - This is the new immutable state pattern
    // Note: Stored as mutex-protected shared_ptr (C++17 compatible - std::atomic<shared_ptr> requires C++20)
    //       Mutex is only used for pointer updates, reads are fast (single mutex lock)
    //       Consistent with Phase 7.2's snapshotJson_ pattern
    mutable std::mutex immutableStateSnapshotMutex_;
    mutable std::shared_ptr<const EngineState> immutableStateSnapshot_;
    
    // Helper to get current timestamp
    uint64_t getCurrentTimestamp() const;
    
    // Callbacks
    std::function<void()> onProjectOpened_;
    std::function<void()> onProjectClosed_;
    std::function<void()> onUpdateWindowTitle_;
    
    // UI operation callbacks (optional, registered by Shells)
    std::function<void(const std::string&)> onModuleAdded_;
    std::function<void(const std::string&)> onModuleRemoved_;
    
    // Internal methods
    void notifyStateChange();
    void notifyObserversWithState();  // Helper to actually call observers with current state
    
    // Simplified notification: enqueue notification to be processed on main thread
    // Replaces stateNeedsNotification_ flag pattern - queue is single source of truth
    void enqueueStateNotification();
    EngineState buildStateSnapshot() const;
    
    // Queue monitoring (Phase 7.9 Plan 8.2)
    void updateNotificationQueueMonitoring();
    void updateCommandQueueMonitoring();
    void updateStateVersionMonitoring();
    void logQueueStatistics();
    
    // Setup helpers
    void setupCoreSystems();
    void setupMasterOutputs();
    void setupCommandExecutor();
    void setupLua();
    void initializeProjectAndSession();
    
    // State building helpers
    // Returns true if completed successfully, false if aborted due to unsafe period
    bool buildModuleStates(EngineState& state) const;
    void buildConnectionStates(EngineState& state) const;
    void buildTransportState(EngineState& state) const;
    
    /**
     * Update engine state snapshot (called during safe periods).
     * Aggregates module snapshots (from Phase 7.1) and creates new immutable JSON snapshot.
     * 
     * THREAD-SAFE: Must be called during safe periods (after commands/scripts complete).
     * Called from main thread (update()) or audio thread (processCommands()).
     * 
     * Implementation:
     * 1. Get module snapshots (from Module::getSnapshot() - lock-free)
     * 2. Get transport state (with lock)
     * 3. Get connections (with lock)
     * 4. Get script state (from ScriptManager)
     * 5. Aggregate into JSON (similar to buildStateSnapshot() but uses module snapshots)
     * 6. Include version number in JSON
     * 7. Atomically update snapshotJson_ pointer
     * 8. Increment state version
     */
    void updateStateSnapshot();
};

} // namespace vt

