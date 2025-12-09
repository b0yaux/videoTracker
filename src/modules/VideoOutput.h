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
class ModuleRegistry;
class ConnectionManager;
class ParameterRouter;
class Clock;

/**
 * VideoOutput - Video output module with integrated mixer functionality
 * 
 * Implements Module interface and combines VideoMixer and VideoOutput functionality.
 * Provides mixing, compositing, and final video output to screen.
 * 
 * Architecture:
 * - Video-producing modules connect directly to VideoOutput
 * - VideoOutput internally mixes all connections
 * - VideoOutput provides final video output to screen
 * - Viewport automatically adjusts to window size
 * 
 * Usage:
 * ```cpp
 * auto output = std::make_shared<VideoOutput>();
 * auto mediaPool = std::make_shared<MediaPool>();
 * 
 * // Connect mediaPool directly to output (mixing happens internally)
 * output->connectModule(mediaPool);
 * 
 * // Set per-connection opacity
 * output->setSourceOpacity(0, 0.8f);
 * 
 * // Set blend mode
 * output->setBlendMode(OF_BLENDMODE_ADD);
 * 
 * // Set master opacity
 * output->setMasterOpacity(1.0f);
 * ```
 */
class VideoOutput : public Module, public ofxVisualObject {
public:
    VideoOutput();
    virtual ~VideoOutput();
    
    // Module interface implementation
    std::string getName() const override;
    ModuleType getType() const override;
    std::vector<ParameterDescriptor> getParameters() const override;
    void onTrigger(TriggerEvent& event) override; // Outputs don't receive triggers
    void setParameter(const std::string& paramName, float value, bool notify = true) override;
    float getParameter(const std::string& paramName) const override;
    ModuleMetadata getMetadata() const override;
    
    // Routing interface - VideoOutput accepts video input (it's a sink, not a source)
    // But internally it uses a mixer, so sources connect to the mixer
    ofxVisualObject* getVideoOutput() const override { return const_cast<ofxVisualObject*>(static_cast<const ofxVisualObject*>(this)); }
    bool producesVideo() const override { return false; }  // Output is a sink, not a source
    
    // Port-based routing interface (Phase 1)
    std::vector<Port> getInputPorts() const override;
    std::vector<Port> getOutputPorts() const override;
    
    // Connection management interface (from Module base class)
    int connectModule(std::shared_ptr<Module> module) override;
    void disconnectModule(std::shared_ptr<Module> module) override;
    
    // Serialization
    ofJson toJson() const override;
    void fromJson(const ofJson& json) override;
    
    // Video processing (from ofxVisualObject)
    void process(ofFbo& input, ofFbo& output) override;
    void draw() override;
    
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
    
    // Per-source opacity control
    /**
     * Set opacity for a specific source
     * @param sourceIndex Index of source
     * @param opacity Opacity (0.0 to 1.0)
     */
    void setSourceOpacity(size_t sourceIndex, float opacity);
    
    /**
     * Get opacity for a specific source
     * @param sourceIndex Index of source
     * @return Opacity value
     */
    float getSourceOpacity(size_t sourceIndex) const;
    
    /**
     * Get module pointer for a source index (for GUI display)
     * @param sourceIndex Index of source
     * @return shared_ptr to module, or nullptr if not found or expired
     */
    std::shared_ptr<Module> getSourceModule(size_t sourceIndex) const;
    
    // Per-source blend mode control
    /**
     * Set blend mode for a specific source
     * @param sourceIndex Index of source
     * @param mode Blend mode for this source
     */
    void setSourceBlendMode(size_t sourceIndex, ofBlendMode mode);
    
    /**
     * Get blend mode for a specific source
     * @param sourceIndex Index of source
     * @return Blend mode for this source, or global blend mode if not set
     */
    ofBlendMode getSourceBlendMode(size_t sourceIndex) const;
    
    // Source reordering
    /**
     * Reorder a source from one index to another (changes layer order)
     * @param fromIndex Source index to move
     * @param toIndex Destination index
     * @return True if reorder was successful
     */
    bool reorderSource(size_t fromIndex, size_t toIndex);
    
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
    
    // Viewport management (simplified - auto-adjusts to window size)
    /**
     * Handle window resize (viewport automatically adjusts)
     * @param width New width
     * @param height New height
     */
    void handleWindowResize(int width, int height);
    
    /**
     * Get current viewport width (from window size)
     * @return Viewport width
     */
    int getViewportWidth() const { return viewportWidth_; }
    
    /**
     * Get current viewport height (from window size)
     * @return Viewport height
     */
    int getViewportHeight() const { return viewportHeight_; }
    
    // Direct access to underlying objects (for advanced use)
    ofxVideoMixer& getVideoMixer() { return videoMixer_; }
    const ofxVideoMixer& getVideoMixer() const { return videoMixer_; }
    ofxVisualOutput& getVisualOutput() { return visualOutput_; }
    const ofxVisualOutput& getVisualOutput() const { return visualOutput_; }
    
    // ofxVisualObject interface
    ofFbo& getOutputBuffer() { return outputFbo_; }
    
    // Connection restoration (for session loading)
    /**
     * Restore connections from JSON (called after all modules are loaded)
     * @param connectionsJson Array of connection info with moduleName and opacity
     * @param registry ModuleRegistry to look up modules by name
     */
    void restoreConnections(const ofJson& connectionsJson, ModuleRegistry* registry) override;
    
private:
    // Internal video mixer (mixes all connected sources)
    ofxVideoMixer videoMixer_;
    
    // Underlying visual output (connects mixer to screen)
    ofxVisualOutput visualOutput_;
    
    // Output FBO for this output
    ofFbo outputFbo_;
    
    // Connected modules (stored as weak_ptr to avoid circular dependencies)
    std::vector<std::weak_ptr<Module>> connectedModules_;
    
    // Per-source opacity (parallel to connectedModules_)
    std::vector<float> sourceOpacities_;
    
    // Per-source blend mode (parallel to connectedModules_)
    std::vector<ofBlendMode> sourceBlendModes_;
    
    // Thread safety
    mutable std::mutex connectionMutex_;
    
    // Master opacity cache (since ofxVideoMixer::getMasterOpacity() is not const)
    float masterOpacity_ = 1.0f;
    
    // Viewport state (auto-adjusts to window size)
    int viewportWidth_ = 1920;
    int viewportHeight_ = 1080;
    
    // Persistent FBOs for video processing (avoid reallocation every frame)
    ofFbo inputFbo_;
    
    // Performance monitoring
    mutable float lastFrameTime_ = 0.0f;
    mutable float frameTimeAccumulator_ = 0.0f;
    mutable int frameCount_ = 0;
    mutable float lastFpsLogTime_ = 0.0f;
    static constexpr float FPS_LOG_INTERVAL = 5.0f; // Log FPS every 5 seconds
    
    // Helper methods
    void ensureOutputFbo(int width = 1920, int height = 1080);
};

