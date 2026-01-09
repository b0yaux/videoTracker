#pragma once

#include "Module.h"
#include "ofxSoundObjects.h"
#include "ofSoundStream.h"
#include <vector>
#include <string>
#include <memory>
#include <mutex>

// Forward declarations
class Module;
class ModuleRegistry;
class ConnectionManager;
class ParameterRouter;
class Clock;

/**
 * AudioOutput - Audio output module with integrated mixer functionality
 * 
 * Implements Module interface and combines AudioMixer and AudioOutput functionality.
 * Provides device selection, volume visualization, mixing, and final audio output.
 * 
 * Architecture:
 * - Audio-producing modules connect directly to AudioOutput
 * - AudioOutput internally mixes all connections
 * - AudioOutput provides final audio output to sound card
 * 
 * Usage:
 * ```cpp
 * auto output = std::make_shared<AudioOutput>();
 * auto multiSampler = std::make_shared<MultiSampler>();
 * 
 * // Connect mediaPool directly to output (mixing happens internally)
 * output->connectModule(mediaPool);
 * 
 * // Set per-connection volume
 * output->setConnectionVolume(0, 0.8f);
 * 
 * // Set master volume
 * output->setMasterVolume(1.0f);
 * 
 * // Setup audio stream
 * output->setupAudioStream();
 * ```
 */
class AudioOutput : public Module {
public:
    AudioOutput();
    virtual ~AudioOutput() noexcept;
    
    // Module interface implementation
    std::string getName() const override;
    ModuleType getType() const override;
    std::vector<ParameterDescriptor> getParametersImpl() const override;
    void onTrigger(TriggerEvent& event) override; // Outputs don't receive triggers
    void setParameterImpl(const std::string& paramName, float value, bool notify = true) override;
    float getParameterImpl(const std::string& paramName) const override;
    ModuleMetadata getMetadata() const override;
    
    // Indexed parameter support for connection-based parameters
    bool supportsIndexedParameters() const override { return true; }
    std::vector<std::pair<std::string, int>> getIndexedParameterRanges() const override;
    float getIndexedParameter(const std::string& baseName, int index) const override;
    void setIndexedParameter(const std::string& baseName, int index, float value, bool notify = true) override;
    
    // Routing interface - AudioOutput accepts audio input (it's a sink, not a source)
    // But internally it uses a mixer, so sources connect to the mixer
    // The mixer output can be monitored for visualization (oscilloscope, spectrogram, etc.)
    ofxSoundObject* getAudioOutput() const override { return const_cast<ofxSoundObject*>(static_cast<const ofxSoundObject*>(&soundMixer_)); }
    bool producesAudio() const override { return true; }  // Output can be monitored for visualization
    
    // Port-based routing interface (Phase 1)
    std::vector<Port> getInputPorts() const override;
    std::vector<Port> getOutputPorts() const override;
    
    // Connection management interface (from Module base class)
    int connectModule(std::shared_ptr<Module> module) override;
    void disconnectModule(std::shared_ptr<Module> module) override;
    
    // Serialization
    ofJson toJson(class ModuleRegistry* registry = nullptr) const override;
    void fromJson(const ofJson& json) override;
    
    // Audio processing
    void audioOut(ofSoundBuffer& buffer);
    
    /**
     * Disconnect module at connection index
     * @param connectionIndex Index of connection to remove
     */
    void disconnectModule(size_t connectionIndex);
    
    /**
     * Get number of connected modules
     * @return Number of connections
     */
    size_t getNumConnections() const;
    
    /**
     * Check if a module is connected
     * @param module Module to check
     * @return True if connected
     */
    bool isConnectedTo(std::shared_ptr<Module> module) const;
    
    /**
     * Get connection index for a module
     * @param module Module to find
     * @return Connection index, or -1 if not found
     */
    int getConnectionIndex(std::shared_ptr<Module> module) const;
    
    /**
     * Get module name for a connection index (for GUI display)
     * @param connectionIndex Index of connection
     * @return Module name, or empty string if not found or expired
     */
    std::string getConnectionModuleName(size_t connectionIndex) const;
    
    /**
     * Get module pointer for a connection index
     * @param connectionIndex Index of connection
     * @return shared_ptr to module, or nullptr if not found or expired
     */
    std::shared_ptr<Module> getConnectionModule(size_t connectionIndex) const;
    
    // Per-connection volume control
    /**
     * Set volume for a specific connection
     * @param connectionIndex Index of connection
     * @param volume Volume (0.0 to 1.0)
     */
    void setConnectionVolume(size_t connectionIndex, float volume);
    
    /**
     * Get volume for a specific connection
     * @param connectionIndex Index of connection
     * @return Volume value
     */
    float getConnectionVolume(size_t connectionIndex) const;
    
    // Master volume control
    /**
     * Set master volume for all connections
     * @param volume Master volume (0.0 to 1.0)
     */
    void setMasterVolume(float volume);
    
    /**
     * Get master volume
     * @return Master volume value
     */
    float getMasterVolume() const;
    
    // Audio device management
    /**
     * Setup audio stream with current device selection
     * @param listener Audio listener (typically ofApp)
     */
    void setupAudioStream(ofBaseApp* listener = nullptr);
    
    /**
     * Get list of available audio devices
     * @return Vector of audio device info
     */
    std::vector<ofSoundDevice> getAudioDevices() const;
    
    /**
     * Set selected audio device by index
     * @param deviceIndex Index in device list
     */
    void setAudioDevice(int deviceIndex);
    
    /**
     * Get selected audio device index
     * @return Device index
     */
    int getAudioDevice() const;
    
    /**
     * Get current audio level for visualization
     * @return Peak audio level (0.0 to 1.0)
     */
    float getCurrentAudioLevel() const;
    
    /**
     * Get current audio level for a specific connection
     * @param connectionIndex Index of connection
     * @return Peak audio level (0.0 to 1.0)
     */
    float getConnectionAudioLevel(size_t connectionIndex) const;
    
    // Direct access to underlying objects (for advanced use)
    ofxSoundMixer& getSoundMixer() { return soundMixer_; }
    const ofxSoundMixer& getSoundMixer() const { return soundMixer_; }
    
    ofxSoundOutput& getSoundOutput() { return soundOutput_; }
    const ofxSoundOutput& getSoundOutput() const { return soundOutput_; }
    ofSoundStream& getSoundStream() { return soundStream_; }
    const ofSoundStream& getSoundStream() const { return soundStream_; }
    
    // Connection restoration (for session loading)
    /**
     * Restore connections from JSON (called after all modules are loaded)
     * @param connectionsJson Array of connection info with moduleName and volume
     * @param registry ModuleRegistry to look up modules by name
     */
    void restoreConnections(const ofJson& connectionsJson, ModuleRegistry* registry) override;
    
    /**
     * Clear all connections (for session loading)
     * Called by AudioRouter when clearing connections during session load
     */
    void clearConnections();
    
    /**
     * Add a monitoring connection (for modules that monitor the mixed audio output)
     * Called by AudioRouter when connecting to AudioOutput's output port
     * @param monitorModule Module that should receive the mixed audio for visualization
     * @return true if added successfully
     */
    bool addMonitoringConnection(std::shared_ptr<Module> monitorModule);
    
    /**
     * Remove a monitoring connection
     * @param monitorModule Module to remove from monitoring
     */
    void removeMonitoringConnection(std::shared_ptr<Module> monitorModule);
    
private:
    // Internal sound mixer (mixes all connected sources)
    ofxSoundMixer soundMixer_;
    
    // Underlying sound output (connects mixer to sound card)
    ofxSoundOutput soundOutput_;
    
    // Connected modules (stored as weak_ptr to avoid circular dependencies)
    std::vector<std::weak_ptr<Module>> connectedModules_;
    
    // Per-connection volume (parallel to connectedModules_)
    std::vector<float> connectionVolumes_;
    
    // Monitoring connections (modules that monitor the mixer output for visualization)
    // These are separate from input connections - they receive the mixed audio output
    std::vector<ofxSoundObject*> monitoringConnections_;
    
    // Thread safety
    mutable std::mutex connectionMutex_;
    
    // Audio device management
    std::vector<ofSoundDevice> audioDevices_;
    int selectedAudioDevice_ = -1;  // -1 means "not set, use default"
    bool audioDeviceChanged_ = false;
    ofSoundStream soundStream_;
    
    // Audio level visualization
    float currentAudioLevel_ = 0.0f;
    std::vector<float> connectionAudioLevels_;  // Per-connection audio levels
    
    // Audio listener (for device changes)
    ofBaseApp* audioListener_ = nullptr;
    
    // Helper methods
    void calculateAudioLevel(const ofSoundBuffer& buffer);
    void refreshAudioDevices();
    void cleanupExpiredConnections();  // Clean up expired weak_ptrs from connection vectors
};

