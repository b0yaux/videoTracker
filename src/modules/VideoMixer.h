#pragma once

#include "Module.h"
#include "ofMain.h"  // For ofPixelFormat
#include "ofxVisualObjects.h"
#include <vector>
#include <memory>
#include <mutex>
#include <string>

// Forward declarations
class Module;

/**
 * VideoMixer - Master video mixer module for combining multiple video sources
 * 
 * Implements Module interface and wraps ofxVideoMixer functionality.
 * Provides per-connection opacity control, master opacity control, and blend modes.
 * 
 * Architecture:
 * - Any video-producing module can connect to VideoMixer
 * - VideoMixer composites all connected sources using blend modes
 * - VideoMixer can connect to VideoOutput for final output (legacy support)
 * - VideoOutput now has internal mixer, so sources connect directly to VideoOutput
 * 
 * Usage:
 * ```cpp
 * auto mixer = std::make_shared<VideoMixer>();
 * auto multiSampler = std::make_shared<MultiSampler>();
 * 
 * // Connect mediaPool to mixer
 * mixer->connectModule(mediaPool);
 * 
 * // Set per-connection opacity
 * mixer->setConnectionOpacity(0, 0.8f);
 * 
 * // Set blend mode
 * mixer->setBlendMode(OF_BLENDMODE_ADD);
 * 
 * // Set master opacity
 * mixer->setMasterOpacity(1.0f);
 * ```
 */
class VideoMixer : public Module, public ofxVisualObject {
public:
    VideoMixer();
    virtual ~VideoMixer();
    
    // Module interface implementation
    std::string getName() const override;
    ModuleType getType() const override;
    std::vector<ParameterDescriptor> getParametersImpl() const override;
    void onTrigger(TriggerEvent& event) override; // Mixers don't receive triggers
    void setParameterImpl(const std::string& paramName, float value, bool notify = true) override;
    float getParameterImpl(const std::string& paramName) const override;
    ModuleMetadata getMetadata() const override;
    
    // Serialization
    ofJson toJson(class ModuleRegistry* registry = nullptr) const override;
    void fromJson(const ofJson& json) override;
    
    // Video processing (from ofxVisualObject)
    void process(ofFbo& input, ofFbo& output) override;
    
    // Connection management
    /**
     * Disconnect module at source index
     * @param sourceIndex Index of source to remove
     */
    void disconnectModule(size_t sourceIndex);
    
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
    
    // Per-connection opacity control
    /**
     * Set opacity for a specific source
     * @param sourceIndex Index of source
     * @param opacity Opacity (0.0 to 1.0)
     */
    void setConnectionOpacity(size_t sourceIndex, float opacity);
    
    /**
     * Get opacity for a specific source
     * @param sourceIndex Index of source
     * @return Opacity value
     */
    float getConnectionOpacity(size_t sourceIndex) const;
    
    // Master opacity control
    /**
     * Set master opacity for all connections
     * @param opacity Master opacity (0.0 to 1.0)
     */
    void setMasterOpacity(float opacity);
    
    /**
     * Get master opacity
     * @return Master opacity value
     */
    float getMasterOpacity() const;
    
    // Blend mode control
    /**
     * Set blend mode for compositing
     * @param mode OpenFrameworks blend mode (OF_BLENDMODE_ADD, OF_BLENDMODE_MULTIPLY, OF_BLENDMODE_ALPHA)
     */
    void setBlendMode(ofBlendMode mode);
    
    /**
     * Get current blend mode
     * @return Current blend mode
     */
    ofBlendMode getBlendMode() const;
    
    // Auto-normalization (for ADD mode to prevent white-out)
    /**
     * Enable/disable auto-normalization for ADD mode
     * @param enabled True to enable auto-normalization
     */
    void setAutoNormalize(bool enabled);
    
    /**
     * Get auto-normalization state
     * @return True if auto-normalization is enabled
     */
    bool getAutoNormalize() const;
    
    // Direct access to underlying ofxVideoMixer (for advanced use)
    ofxVideoMixer& getVideoMixer() { return videoMixer_; }
    const ofxVideoMixer& getVideoMixer() const { return videoMixer_; }
    
    // Routing interface - VideoMixer produces video output (inherits from ofxVisualObject)
    ofxVisualObject* getVideoOutput() const override { return const_cast<ofxVisualObject*>(static_cast<const ofxVisualObject*>(this)); }
    bool producesVideo() const override { return true; }
    
    // Port-based routing interface (Phase 1)
    std::vector<Port> getInputPorts() const override;
    std::vector<Port> getOutputPorts() const override;
    
    // Connection management interface (from Module base class)
    int connectModule(std::shared_ptr<Module> module) override;
    void disconnectModule(std::shared_ptr<Module> module) override;
    
    // Connection restoration (for session loading)
    /**
     * Restore connections from JSON (called after all modules are loaded)
     * @param connectionsJson Array of connection info with moduleName and opacity
     * @param registry ModuleRegistry to look up modules by name
     */
    void restoreConnections(const ofJson& connectionsJson, class ModuleRegistry* registry) override;
    
    // ofxVisualObject interface
    ofFbo& getOutputBuffer() { return outputFbo_; }
    
private:
    // Underlying video mixer
    ofxVideoMixer videoMixer_;
    
    // Output FBO for this mixer
    ofFbo outputFbo_;
    
    // Connected modules (stored as weak_ptr to avoid circular dependencies)
    std::vector<std::weak_ptr<Module>> connectedModules_;
    
    // Per-source opacity (parallel to connectedModules_)
    std::vector<float> sourceOpacities_;
    
    // Thread safety
    mutable std::mutex connectionMutex_;
    
    // Master opacity cache (since ofxVideoMixer::getMasterOpacity() is not const)
    float masterOpacity_ = 1.0f;
    
    
    // Ensure output FBO is allocated
    void ensureOutputFbo(int width = 1920, int height = 1080);
};

