#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include "ofJson.h"
#include "core/ParameterDescriptor.h"  // For ParameterDescriptor and ParameterType

// Forward declarations for routing interface
class ofxSoundObject;
class ofxVisualObject;

// Forward declaration for ConnectionManager (full definition needed for ConnectionType enum)
class ConnectionManager;

// Module type enumeration - UI/organization categories only, NOT functional constraints
// Functionality is determined by capabilities (see ModuleCapability below)
// A module can have any combination of capabilities regardless of its type
enum class ModuleType {
    SEQUENCER,    // UI category: Pattern-based sequencers (e.g., TrackerSequencer)
    INSTRUMENT,   // UI category: Sound/video sources (e.g., MultiSampler, MIDIOutput)
    EFFECT,       // UI category: Audio/video processors (future: filters, delays, etc.)
    UTILITY       // UI category: Routing, mixing, utilities (e.g., AudioMixer, VideoMixer)
};

// Module capability enumeration - describes what a module can DO (functional behavior)
// Used for capability-based queries instead of type-specific checks
// Modules can have multiple capabilities (e.g., a drum machine could both emit and accept triggers)
enum class ModuleCapability {
    ACCEPTS_FILE_DROP,          // Module can accept file drops (e.g., MultiSampler)
    EMITS_TRIGGER_EVENTS,       // Module emits trigger events (any type can have this, not just SEQUENCER)
    ACCEPTS_TRIGGER_EVENTS      // Module accepts trigger events (any type can have this, not just INSTRUMENT)
};

// Event data for trigger events (discrete step triggers)
struct TriggerEvent {
    // Map of parameter names to values
    // TrackerSequencer sends: {"note": 60, "position": 0.5, "speed": 1.0, "volume": 1.0}
    // Modules map these to their own parameters (e.g., note → mediaIndex)
    std::map<std::string, float> parameters;
    
    // Optional: duration in seconds (for step-based triggers)
    float duration = 0.0f;
    
    // Step number from sequencer (-1 for non-sequencer triggers like manual preview)
    int step = -1;
    
    // Pattern name for event routing (used by PatternRuntime to identify source pattern)
    // TrackerSequencer filters events by patternName when forwarding PatternRuntime events
    std::string patternName = "";
};

// Port-based routing system (Phase 1: Unified port system)
// Ports provide explicit input/output declarations for modules
enum class PortType {
    AUDIO_IN,      // Audio input port
    AUDIO_OUT,     // Audio output port
    VIDEO_IN,      // Video input port
    VIDEO_OUT,     // Video output port
    PARAMETER_IN,  // Parameter input port (for modulation)
    PARAMETER_OUT, // Parameter output port
    EVENT_IN,      // Event input port (triggers)
    EVENT_OUT      // Event output port
};

/**
 * Port - Describes an input or output port on a module
 * 
 * Ports provide explicit declarations of what a module can accept or produce.
 * This replaces the hybrid capabilities + output methods approach with a unified system.
 * 
 * Example:
 *   MultiSampler has: audio_out (AUDIO_OUT), video_out (VIDEO_OUT), trigger_in (EVENT_IN)
 *   AudioMixer has: audio_in_0, audio_in_1, ... (AUDIO_IN, multiConnect=true), audio_out (AUDIO_OUT)
 */
struct Port {
    std::string name;           // Unique port name within module (e.g., "audio_out", "trigger_in")
    PortType type;              // Port type (AUDIO_IN, AUDIO_OUT, etc.)
    bool isMultiConnect;        // Can multiple sources connect? (true for mixers, false for single connections)
    std::string displayName;    // User-friendly name (e.g., "Audio Output", "Trigger Input")
    void* dataPtr;              // Pointer to underlying data object (ofxSoundObject*, ofxVisualObject*, etc.)
                                // nullptr if port doesn't have direct data access
    
    Port()
        : name(""), type(PortType::AUDIO_IN), isMultiConnect(false), displayName(""), dataPtr(nullptr) {}
    
    Port(const std::string& n, PortType t, bool multiConnect, const std::string& display, void* data = nullptr)
        : name(n), type(t), isMultiConnect(multiConnect), displayName(display), dataPtr(data) {}
    
    // Helper to check if two ports are compatible for connection
    static bool areCompatible(const Port& source, const Port& target) {
        // Source must be output, target must be input
        if (source.type == PortType::AUDIO_OUT && target.type == PortType::AUDIO_IN) return true;
        if (source.type == PortType::VIDEO_OUT && target.type == PortType::VIDEO_IN) return true;
        if (source.type == PortType::PARAMETER_OUT && target.type == PortType::PARAMETER_IN) return true;
        if (source.type == PortType::EVENT_OUT && target.type == PortType::EVENT_IN) return true;
        return false;
    }
};

// Unified base class for instruments and effects (SunVox-style)
// TrackerSequencer stays separate - it connects to Modules but doesn't inherit from Module
// This allows for future BespokeSynth-style evolution where TrackerSequencer becomes a Module too
class Module {
public:
    virtual ~Module() = default;
    
    // Identity
    virtual std::string getName() const = 0;
    
    // GUI-only: Module type for UI organization (not used for backend logic)
    // Backend should use capabilities, producesAudio(), producesVideo() instead
    // This is kept in base class for backward compatibility and GUI convenience
    virtual ModuleType getType() const = 0;
    
    // Get all available parameters that this module can accept
    // TrackerSequencer will query this to discover what parameters can be mapped to columns
    virtual std::vector<ParameterDescriptor> getParameters() const = 0;
    
    // Discrete trigger event (called when a step triggers)
    // This is separate from continuous parameter modulation
    // Parameters map contains all values for this trigger (e.g., note, position, speed, volume)
    // Note: non-const reference to match ofEvent signature requirements
    virtual void onTrigger(TriggerEvent& event) = 0;
    
    // Continuous parameter modulation (for modulators, envelopes, etc.)
    // This is for continuous updates, not discrete triggers
    // paramName: The parameter name (e.g., "position", "speed", "volume")
    // value: The value to set (interpreted based on parameter type)
    // notify: If true, notify parameter change callback (default: true)
    virtual void setParameter(const std::string& paramName, float value, bool notify = true) = 0;
    
    /**
     * Get a parameter value by name
     * @param paramName Parameter name (must match a name in getMetadata().parameterNames)
     * @return Parameter value, or 0.0f if parameter doesn't exist
     * 
     * This complements setParameter() to provide full parameter access.
     * Modules must implement this for all parameters listed in metadata.parameterNames.
     * This enables generic parameter routing without module-specific knowledge.
     */
    virtual float getParameter(const std::string& paramName) const {
        // Default implementation: return 0.0f
        // Modules override to return actual parameter values
        return 0.0f;
    }
    
    /**
     * Optional: Check if this module supports indexed parameters
     * @return True if module supports indexed parameter access (e.g., step[4].position)
     * 
     * Modules that support indexed parameters (like TrackerSequencer with step indices)
     * should override this to return true and implement getIndexedParameter/setIndexedParameter.
     */
    virtual bool supportsIndexedParameters() const { return false; }
    
    /**
     * Optional: Get an indexed parameter value
     * @param paramName Parameter name (e.g., "position", "speed", "volume")
     * @param index Index for the parameter (e.g., step index for TrackerSequencer)
     * @return Parameter value, or 0.0f if not supported or invalid index
     * 
     * Modules that support indexed parameters should override this.
     * Default implementation returns 0.0f (not supported).
     */
    virtual float getIndexedParameter(const std::string& paramName, int index) const {
        return 0.0f;
    }
    
    /**
     * Optional: Set an indexed parameter value
     * @param paramName Parameter name (e.g., "position", "speed", "volume")
     * @param index Index for the parameter (e.g., step index for TrackerSequencer)
     * @param value Value to set
     * @param notify If true, notify parameter change callback (default: true)
     * 
     * Modules that support indexed parameters should override this.
     * Default implementation does nothing (not supported).
     */
    virtual void setIndexedParameter(const std::string& paramName, int index, float value, bool notify = true) {
        // Default: no-op - modules that support indexing override this
    }
    
    // Parameter change callback (modules can notify external systems like ParameterRouter)
    void setParameterChangeCallback(std::function<void(const std::string&, float)> callback) {
        parameterChangeCallback = callback;
    }
    
    // Optional: update loop (for modules that need continuous updates)
    virtual void update() {}
    
    // Optional: transport state change notification (for modules that need transport events)
    // Called when Clock transport starts/stops
    virtual void onTransportChanged(bool isPlaying) {}
    
    // Optional: draw GUI (for modules that need visual representation)
    virtual void draw() {}
    
    // Optional: handle mouse clicks (for modules that need mouse interaction)
    // Default implementation does nothing - override in subclasses that need it
    virtual void handleMouseClick(int x, int y, int button) {}
    
    // Optional: accept file drops (for modules that can accept files)
    // Default implementation does nothing - override in subclasses that need it
    // @param filePaths Vector of file paths to add
    // @return True if files were accepted, false otherwise
    virtual bool acceptFileDrop(const std::vector<std::string>& filePaths) { return false; }
    
    // Connection compatibility interface (Phase 9.1)
    // Check if this module can connect to another module
    // @param other The module to check compatibility with
    // @param connectionType The type of connection (0=AUDIO, 1=VIDEO, 2=PARAMETER, 3=EVENT)
    // @return True if modules are compatible for this connection type
    virtual bool canConnectTo(const Module* other, int connectionType) const {
        if (!other) return false;
        
        // Port-based compatibility checks
        switch (connectionType) {
            case 0: { // AUDIO
                // Source must have AUDIO_OUT port, target must have AUDIO_IN port
                auto sourcePorts = getOutputPorts();
                auto targetPorts = other->getInputPorts();
                
                bool hasAudioOut = false;
                for (const auto& port : sourcePorts) {
                    if (port.type == PortType::AUDIO_OUT) {
                        hasAudioOut = true;
                        break;
                    }
                }
                
                bool hasAudioIn = false;
                for (const auto& port : targetPorts) {
                    if (port.type == PortType::AUDIO_IN) {
                        hasAudioIn = true;
                        break;
                    }
                }
                
                return hasAudioOut && hasAudioIn;
            }
            case 1: { // VIDEO
                // Source must have VIDEO_OUT port, target must have VIDEO_IN port
                auto sourcePorts = getOutputPorts();
                auto targetPorts = other->getInputPorts();
                
                bool hasVideoOut = false;
                for (const auto& port : sourcePorts) {
                    if (port.type == PortType::VIDEO_OUT) {
                        hasVideoOut = true;
                        break;
                    }
                }
                
                bool hasVideoIn = false;
                for (const auto& port : targetPorts) {
                    if (port.type == PortType::VIDEO_IN) {
                        hasVideoIn = true;
                        break;
                    }
                }
                
                return hasVideoOut && hasVideoIn;
            }
            case 2: { // PARAMETER
                // Both modules must have parameter ports or metadata parameters
                auto sourcePorts = getOutputPorts();
                auto targetPorts = other->getInputPorts();
                
                bool hasParamOut = false;
                for (const auto& port : sourcePorts) {
                    if (port.type == PortType::PARAMETER_OUT) {
                        hasParamOut = true;
                        break;
                    }
                }
                
                bool hasParamIn = false;
                for (const auto& port : targetPorts) {
                    if (port.type == PortType::PARAMETER_IN) {
                        hasParamIn = true;
                        break;
                    }
                }
                
                // Fallback to metadata check if ports not available (backward compatibility)
                if (!hasParamOut) hasParamOut = !getMetadata().parameterNames.empty();
                if (!hasParamIn) hasParamIn = !other->getMetadata().parameterNames.empty();
                
                return hasParamOut && hasParamIn;
            }
            case 3: { // EVENT
                // Source must have EVENT_OUT port, target must have EVENT_IN port
                auto sourcePorts = getOutputPorts();
                auto targetPorts = other->getInputPorts();
                
                bool hasEventOut = false;
                for (const auto& port : sourcePorts) {
                    if (port.type == PortType::EVENT_OUT) {
                        hasEventOut = true;
                        break;
                    }
                }
                
                bool hasEventIn = false;
                for (const auto& port : targetPorts) {
                    if (port.type == PortType::EVENT_IN) {
                        hasEventIn = true;
                        break;
                    }
                }
                
                // Fallback to capability check if ports not available (backward compatibility)
                if (!hasEventOut) hasEventOut = hasCapability(ModuleCapability::EMITS_TRIGGER_EVENTS);
                if (!hasEventIn) hasEventIn = other->hasCapability(ModuleCapability::ACCEPTS_TRIGGER_EVENTS);
                
                return hasEventOut && hasEventIn;
            }
            default:
                return false;
        }
    }
    
    // Get compatible module types for a connection type
    // @param connectionType The type of connection
    // @return Vector of module type names that are compatible (empty = all types compatible)
    virtual std::vector<std::string> getCompatibleModuleTypes(int connectionType) const { return {}; }
    
    // Serialization interface
    // Each module implements its own serialization logic
    // registry parameter is optional - provided when serializing from ModuleRegistry for UUID/name lookup
    virtual ofJson toJson(class ModuleRegistry* registry = nullptr) const = 0;
    virtual void fromJson(const ofJson& json) = 0;
    
    // State snapshot for Engine (returns JSON to avoid Engine knowing about specific module types)
    // Default implementation uses toJson() - modules only override if they need to extract
    // specific runtime state (like current playback position, active voices, etc.)
    virtual ofJson getStateSnapshot() const { return toJson(); }
    
    /**
     * Unified initialization method - replaces postCreateSetup, configureSelf, and completeRestore
     * 
     * This single method handles all module initialization in one place, simplifying the lifecycle.
     * Called once after module creation (new or restored) and after connections are discovered.
     * 
     * @param clock Clock instance for transport subscriptions (may be nullptr)
     * @param registry ModuleRegistry for finding connected modules (may be nullptr)
     * @param connectionManager ConnectionManager for querying connections (may be nullptr)
     * @param parameterRouter ParameterRouter for setting up parameter callbacks (may be nullptr)
     * @param isRestored True if module is being restored from session, false if newly created
     * 
     * Default implementation does nothing - override in subclasses that need initialization.
     * 
     * Migration guide:
     * - Move postCreateSetup() logic here (Clock subscriptions, basic setup)
     * - Move configureSelf() logic here (connection-based configuration)
     * - Move completeRestore() logic here, gated by isRestored flag (deferred media loading, etc.)
     */
    virtual void initialize(
        class Clock* clock = nullptr,
        class ModuleRegistry* registry = nullptr,
        class ConnectionManager* connectionManager = nullptr,
        class ParameterRouter* parameterRouter = nullptr,
        class PatternRuntime* patternRuntime = nullptr,  // PatternRuntime for modules that need pattern access
        bool isRestored = false
    ) {}
    
    /**
     * Connection type enumeration (matches ConnectionManager::ConnectionType)
     * Defined here to avoid circular dependency
     */
    enum class ConnectionType {
        AUDIO,
        VIDEO,
        PARAMETER,
        EVENT
    };
    
    /**
     * Called by ConnectionManager after a successful connection is established
     * Allows modules to react to runtime connection changes (e.g., update callbacks, refresh state)
     * 
     * @param targetModuleName Name of the module that was connected to/from
     * @param connectionType Type of connection (AUDIO, VIDEO, PARAMETER, EVENT)
     * @param connectionManager ConnectionManager instance for querying connections
     * 
     * Default implementation does nothing - override in subclasses that need to react to connections.
     * 
     * This is called after connections are made at runtime (e.g., via Console), allowing modules
     * to update their internal state without requiring re-initialization.
     */
    virtual void onConnectionEstablished(
        const std::string& targetModuleName,
        ConnectionType connectionType,
        class ConnectionManager* connectionManager
    ) {}
    
    /**
     * Called by ConnectionManager when a connection is broken/disconnected
     * Allows modules to react to runtime disconnections (e.g., invalidate caches, refresh state)
     * 
     * @param targetModuleName Name of the module that was disconnected from
     * @param connectionType Type of connection that was broken (AUDIO, VIDEO, PARAMETER, EVENT)
     * @param connectionManager ConnectionManager instance for querying connections
     * 
     * Default implementation does nothing - override in subclasses that need to react to disconnections.
     * 
     * This is called when connections are removed at runtime (e.g., via Console), allowing modules
     * to clean up state and invalidate caches related to the disconnected module.
     */
    virtual void onConnectionBroken(
        const std::string& targetModuleName,
        ConnectionType connectionType,
        class ConnectionManager* connectionManager
    ) {}
    
    // Get module type name for serialization (e.g., "TrackerSequencer", "MultiSampler")
    // Default implementation returns getName() - override only if different
    virtual std::string getTypeName() const {
        return getName();
    }
    
    // Routing interface - modules that produce audio/video should implement these
    // Returns nullptr if module doesn't produce that type of output
    /**
     * Get audio output object for this module
     * @return Pointer to ofxSoundObject, or nullptr if module doesn't produce audio
     */
    virtual ofxSoundObject* getAudioOutput() const { return nullptr; }
    
    /**
     * Get video output object for this module
     * @return Pointer to ofxVisualObject, or nullptr if module doesn't produce video
     */
    virtual ofxVisualObject* getVideoOutput() const { return nullptr; }
    
    /**
     * Check if module produces audio
     * @return True if getAudioOutput() returns non-null
     */
    virtual bool producesAudio() const { return getAudioOutput() != nullptr; }
    
    /**
     * Check if module produces video
     * @return True if getVideoOutput() returns non-null
     */
    virtual bool producesVideo() const { return getVideoOutput() != nullptr; }
    
    // Event access interface - allows modules to expose events for generic subscription
    // Modules that emit events (like TrackerSequencer) should override this
    // @param eventName Name of the event (e.g., "triggerEvent")
    // @return Pointer to the event, or nullptr if module doesn't have this event
    virtual ofEvent<TriggerEvent>* getEvent(const std::string& eventName) { return nullptr; }
    
    // Enable/disable state
    virtual void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
    // Capability query interface - modules declare what they can do
    // Used instead of type-specific checks (dynamic_pointer_cast)
    /**
     * Check if module has a specific capability
     * @param capability The capability to check
     * @return True if module has this capability
     */
    virtual bool hasCapability(ModuleCapability capability) const { return false; }
    
    /**
     * Get all capabilities this module has
     * @return Vector of all capabilities
     */
    virtual std::vector<ModuleCapability> getCapabilities() const { return {}; }
    
    // REMOVED: getIndexRange() - use parameter system instead
    // Modules that provide index ranges should expose an "index" parameter
    // with dynamic min/max values in getParameters()
    
    /**
     * Restore connections from JSON (for modules that support connections, like mixers)
     * Default implementation does nothing - modules that need connection restoration override this
     * @param connectionsJson Array of connection info
     * @param registry ModuleRegistry to look up modules by name
     */
    virtual void restoreConnections(const ofJson& connectionsJson, class ModuleRegistry* registry) {
        // Default: no-op - modules that need connection restoration override this
    }
    
    // Module metadata interface - modules self-describe their features
    struct ModuleMetadata {
        std::string typeName;                    // "TrackerSequencer", "MultiSampler"
        std::vector<std::string> eventNames;     // ["triggerEvent"]
        std::vector<std::string> parameterNames; // ["currentStepPosition", "position"]
        std::map<std::string, std::string> parameterDisplayNames; // {"position": "Position"}
        
        ModuleMetadata() : typeName("") {}
    };
    
    /**
     * Get metadata about this module (events, parameters, etc.)
     * Used to avoid hardcoding parameter/event names in ofApp
     * @return Module metadata structure
     */
    virtual ModuleMetadata getMetadata() const = 0;
    
    // ========================================================================
    // Port-based routing interface (Phase 1: Unified port system)
    // ========================================================================
    // These methods provide explicit input/output port declarations
    // Modules should override these to declare their ports
    // Legacy methods (getAudioOutput, producesAudio, etc.) remain for backward compatibility
    
    /**
     * Get all input ports this module accepts
     * @return Vector of input ports (AUDIO_IN, VIDEO_IN, PARAMETER_IN, EVENT_IN)
     * 
     * Example for MultiSampler:
     *   return { Port("trigger_in", PortType::EVENT_IN, false, "Trigger Input") };
     * 
     * Example for AudioMixer:
     *   return { Port("audio_in_0", PortType::AUDIO_IN, true, "Audio Input 1"),
     *            Port("audio_in_1", PortType::AUDIO_IN, true, "Audio Input 2"), ... };
     */
    virtual std::vector<Port> getInputPorts() const {
        // Default: no input ports
        return {};
    }
    
    /**
     * Get all output ports this module produces
     * @return Vector of output ports (AUDIO_OUT, VIDEO_OUT, PARAMETER_OUT, EVENT_OUT)
     * 
     * Example for MultiSampler:
     *   return { Port("audio_out", PortType::AUDIO_OUT, false, "Audio Output", &internalAudioMixer_),
     *            Port("video_out", PortType::VIDEO_OUT, false, "Video Output", &internalVideoMixer_) };
     */
    virtual std::vector<Port> getOutputPorts() const {
        // Default: no output ports
        return {};
    }
    
    /**
     * Get an input port by name
     * @param portName Port name (e.g., "trigger_in", "audio_in_0")
     * @return Pointer to port if found, nullptr otherwise
     * 
     * Note: Uses thread-local static storage to avoid dangling pointers.
     * The returned pointer is valid until the next call to getInputPort() or getOutputPort()
     * on any module in the same thread.
     */
    const Port* getInputPort(const std::string& portName) const {
        // Use thread-local static storage to cache ports and avoid dangling pointers
        // This is safe because ports are const and don't change during module lifetime
        thread_local static std::vector<Port> cachedPorts;
        thread_local static const Module* cachedModule = nullptr;
        
        // Refresh cache if this is a different module
        if (cachedModule != this) {
            cachedPorts = getInputPorts();
            cachedModule = this;
        }
        
        for (const auto& port : cachedPorts) {
            if (port.name == portName) {
                return &port;
            }
        }
        return nullptr;
    }
    
    /**
     * Get an output port by name
     * @param portName Port name (e.g., "audio_out", "video_out")
     * @return Pointer to port if found, nullptr otherwise
     * 
     * Note: Uses thread-local static storage to avoid dangling pointers.
     * The returned pointer is valid until the next call to getInputPort() or getOutputPort()
     * on any module in the same thread.
     */
    const Port* getOutputPort(const std::string& portName) const {
        // Use thread-local static storage to cache ports and avoid dangling pointers
        thread_local static std::vector<Port> cachedPorts;
        thread_local static const Module* cachedModule = nullptr;
        
        // Refresh cache if this is a different module
        if (cachedModule != this) {
            cachedPorts = getOutputPorts();
            cachedModule = this;
        }
        
        for (const auto& port : cachedPorts) {
            if (port.name == portName) {
                return &port;
            }
        }
        return nullptr;
    }
    
    /**
     * Check if this module has a specific input port
     * @param portName Port name to check
     * @return True if port exists
     */
    bool hasInput(const std::string& portName) const {
        return getInputPort(portName) != nullptr;
    }
    
    /**
     * Check if this module has a specific output port
     * @param portName Port name to check
     * @return True if port exists
     */
    bool hasOutput(const std::string& portName) const {
        return getOutputPort(portName) != nullptr;
    }
    
    /**
     * Check if this module has an output port of a specific type
     * @param type Port type to check (e.g., PortType::AUDIO_OUT, PortType::VIDEO_OUT)
     * @return True if module has at least one output port of this type
     */
    bool hasOutput(PortType type) const {
        auto ports = getOutputPorts();
        for (const auto& port : ports) {
            if (port.type == type) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * Check if this module has an input port of a specific type
     * @param type Port type to check (e.g., PortType::AUDIO_IN, PortType::VIDEO_IN)
     * @return True if module has at least one input port of this type
     */
    bool hasInput(PortType type) const {
        auto ports = getInputPorts();
        for (const auto& port : ports) {
            if (port.type == type) {
                return true;
            }
        }
        return false;
    }
    
    // ========================================================================
    // Connection Management Interface (Simplified: Direct connectModule support)
    // ========================================================================
    // Modules that manage their own connections (AudioOutput, VideoOutput, Mixers)
    // override connectModule() to provide connection tracking, volume/opacity control,
    // and thread-safe connection management.
    
    /**
     * Connect a source module to this module
     * @param sourceModule Module to connect from (stored as weak_ptr to avoid cycles)
     * @return Connection index (>=0) on success, -1 on failure or not supported
     * 
     * Default implementation returns -1 (not supported).
     * Modules that manage connections (AudioOutput, VideoOutput, AudioMixer, VideoMixer)
     * override this to handle connection establishment, tracking, and initialization.
     * 
     * Routers call this method directly - if it returns -1, they fall back to
     * direct port-based connection (connectTo() on underlying objects).
     */
    virtual int connectModule(std::shared_ptr<Module> sourceModule) {
        // Default: not supported
        return -1;
    }
    
    /**
     * Disconnect a source module from this module
     * @param sourceModule Module to disconnect
     * 
     * Default implementation does nothing (not supported).
     * Modules that manage connections override this to handle disconnection cleanup.
     */
    virtual void disconnectModule(std::shared_ptr<Module> sourceModule) {
        // Default: no-op - modules that manage connections override this
    }

protected:
    // Parameter change callback for synchronization systems
    std::function<void(const std::string&, float)> parameterChangeCallback;
    bool enabled_ = true;  // Module enabled state (default: enabled)
};
