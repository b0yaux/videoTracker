#pragma once

#include "Module.h"
#include "ofxSoundObjects.h"
#include <vector>
#include <memory>
#include <mutex>
#include <string>

// Forward declarations
class Module;

/**
 * AudioMixer - Master audio mixer module for combining multiple audio sources
 * 
 * Implements Module interface and wraps ofxSoundMixer functionality.
 * Provides per-connection volume control and master volume control.
 * 
 * Architecture:
 * - Any audio-producing module can connect to AudioMixer
 * - AudioMixer mixes all connected sources
 * - AudioMixer connects to AudioOutput for final output
 * 
 * Usage:
 * ```cpp
 * auto mixer = std::make_shared<AudioMixer>();
 * auto multiSampler = std::make_shared<MultiSampler>();
 * 
 * // Connect mediaPool to mixer
 * mixer->connectModule(mediaPool);
 * 
 * // Set per-connection volume
 * mixer->setConnectionVolume(0, 0.8f);
 * 
 * // Set master volume
 * mixer->setMasterVolume(1.0f);
 * ```
 */
class AudioMixer : public Module {
public:
    AudioMixer();
    virtual ~AudioMixer();
    
    // Module interface implementation
    std::string getName() const override;
    ModuleType getType() const override;
    std::vector<ParameterDescriptor> getParameters() const override;
    void onTrigger(TriggerEvent& event) override; // Mixers don't receive triggers
    void setParameter(const std::string& paramName, float value, bool notify = true) override;
    float getParameter(const std::string& paramName) const override;
    ModuleMetadata getMetadata() const override;
    
    // Serialization
    ofJson toJson(class ModuleRegistry* registry = nullptr) const override;
    void fromJson(const ofJson& json) override;
    
    // Audio processing
    void audioOut(ofSoundBuffer& output);
    
    // Connection management
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
    
    /**
     * Get current audio level for visualization
     * @return Peak audio level (0.0 to 1.0)
     */
    float getCurrentAudioLevel() const;
    
    /**
     * Method to update audio level from external source (when audioOut() isn't called)
     * This is needed because soundMixer_ is in the chain, not AudioMixer
     * @param buffer The processed audio buffer to calculate level from
     */
    void updateAudioLevelFromBuffer(const ofSoundBuffer& buffer);
    
    // Direct access to underlying ofxSoundMixer (for advanced use)
    ofxSoundMixer& getSoundMixer() { return soundMixer_; }
    const ofxSoundMixer& getSoundMixer() const { return soundMixer_; }
    
    // Routing interface - AudioMixer produces audio output
    ofxSoundObject* getAudioOutput() const override { return const_cast<ofxSoundObject*>(static_cast<const ofxSoundObject*>(&soundMixer_)); }
    bool producesAudio() const override { return true; }
    
    // Port-based routing interface (Phase 1)
    std::vector<Port> getInputPorts() const override;
    std::vector<Port> getOutputPorts() const override;
    
    // Connection management interface (from Module base class)
    int connectModule(std::shared_ptr<Module> module) override;
    void disconnectModule(std::shared_ptr<Module> module) override;
    
    // Connection restoration (for session loading)
    /**
     * Restore connections from JSON (called after all modules are loaded)
     * @param connectionsJson Array of connection info with moduleName and volume
     * @param registry ModuleRegistry to look up modules by name
     */
    void restoreConnections(const ofJson& connectionsJson, class ModuleRegistry* registry) override;
    
private:
    // Underlying sound mixer
    ofxSoundMixer soundMixer_;
    
    // Connected modules (stored as weak_ptr to avoid circular dependencies)
    std::vector<std::weak_ptr<Module>> connectedModules_;
    
    // Per-connection volume (parallel to connectedModules_)
    std::vector<float> connectionVolumes_;
    
    // Thread safety
    mutable std::mutex connectionMutex_;
    
    // Audio level visualization
    float currentAudioLevel_ = 0.0f;
    
    // Helper: Calculate audio level from buffer
    void calculateAudioLevel(const ofSoundBuffer& buffer);
};

