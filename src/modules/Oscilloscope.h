#pragma once

#include "Module.h"
#include "ofMain.h"
#include "ofxSoundObjects.h"
#include "ofxVisualObjects.h"
#include "ofVbo.h"
#include "ofShader.h"
#include <vector>
#include <memory>
#include <mutex>
#include <string>
#include <deque>
#include <utility>  // for std::pair

// Forward declarations
class ModuleRegistry;
class ConnectionManager;
class ParameterRouter;
class Clock;

/**
 * Oscilloscope - Audio waveform visualization module
 * 
 * Implements Module interface and ofxVisualObject for video output.
 * Visualizes time-domain audio waveform as a video signal.
 * 
 * Architecture:
 * - Accepts audio input via ofxSoundObject
 * - Stores audio samples in circular buffer
 * - Renders waveform to FBO
 * - Outputs as ofxVisualObject for routing to VideoOutput
 * 
 * Usage:
 * ```cpp
 * auto oscilloscope = std::make_shared<Oscilloscope>();
 * auto audioSource = std::make_shared<MultiSampler>();
 * 
 * // Connect audio source to oscilloscope
 * oscilloscope->connectAudioSource(audioSource->getAudioOutput());
 * 
 * // Connect oscilloscope to video output
 * videoOutput->connectModule(oscilloscope);
 * ```
 */
class Oscilloscope : public Module, public ofxVisualObject, public ofxSoundObject {
public:
    Oscilloscope();
    virtual ~Oscilloscope();
    
    // Module interface implementation
    std::string getName() const override;
    ModuleType getType() const override;
    std::vector<ParameterDescriptor> getParametersImpl() const override;
    void onTrigger(TriggerEvent& event) override;
    void setParameterImpl(const std::string& paramName, float value, bool notify = true) override;
    float getParameterImpl(const std::string& paramName) const override;
    ModuleMetadata getMetadata() const override;
    
    // Routing interface - Oscilloscope produces video output
    ofxVisualObject* getVideoOutput() const override { 
        return const_cast<ofxVisualObject*>(static_cast<const ofxVisualObject*>(this)); 
    }
    bool producesVideo() const override { return true; }
    
    // Audio input interface - Oscilloscope accepts audio input
    ofxSoundObject* getAudioOutput() const override { 
        return const_cast<ofxSoundObject*>(static_cast<const ofxSoundObject*>(this)); 
    }
    bool producesAudio() const override { return false; }  // Pass-through, not a source
    
    // Port-based routing interface
    std::vector<Port> getInputPorts() const override;
    std::vector<Port> getOutputPorts() const override;
    
    // Serialization
    ofJson toJson(class ModuleRegistry* registry = nullptr) const override;
    void fromJson(const ofJson& json) override;
    
    // Audio processing (from ofxSoundObject)
    void process(ofSoundBuffer& input, ofSoundBuffer& output) override;
    
    // Video processing (from ofxVisualObject)
    void process(ofFbo& input, ofFbo& output) override;
    
    // ofxVisualObject interface
    ofFbo& getOutputBuffer() { return outputFbo_; }
    
    // Parameter controls
    // Note: setEnabled() is inherited from Module base class
    bool getEnabled() const { return isEnabled(); }
    
    void setScale(float scale);
    float getScale() const { return scale_; }
    
    void setColor(const ofColor& color);
    ofColor getColor() const { return color_; }
    
    void setBackgroundColor(const ofColor& color);
    ofColor getBackgroundColor() const { return backgroundColor_; }
    
    void setThickness(float thickness);  // Legacy - maps to pointSize
    float getThickness() const { return pointSize_; }  // Legacy
    
    void setPointSize(float pointSize);
    float getPointSize() const;
    
    // Update method (called from main thread)
    void update() override;
    
private:
    // Audio buffer management
    static constexpr size_t MAX_BUFFER_SIZE = 44100 * 2;  // 2 seconds at 44.1kHz
    
    // Circular buffer for audio samples (stereo X-Y pairs for Lissajous)
    // Each pair represents {left/X, right/Y} channel values
    std::deque<std::pair<float, float>> audioBufferXY_;
    size_t maxBufferSize_;
    
    // Thread safety for audio buffer
    mutable std::mutex audioMutex_;
    
    // Audio sample rate (detected from input)
    float sampleRate_ = 44100.0f;
    
    // Parameters
    // Note: enabled_ is inherited from Module base class
    float scale_ = 0.5f;           // Scale factor (can exceed 1.0 for larger visualizations)
    ofColor color_ = ofColor::white;
    ofColor backgroundColor_ = ofColor::black;  // Opaque background for proper compositing
    float pointSize_ = 1.0f;      // Line thickness in pixels (0.5 to 2.0)
    
    // GPU rendering resources
    ofVbo vbo_;
    ofShader shader_;
    std::vector<glm::vec3> vertices_;  // X, Y, Z (Z unused, for future use)
    bool shaderLoaded_ = false;
    
    // Output FBO for visualization
    ofFbo outputFbo_;
    
    // FBO dimensions (default to reasonable size)
    int fboWidth_ = 1920;
    int fboHeight_ = 200;  // Oscilloscope is typically horizontal
    
    // Performance optimizations: cached values
    float normalizedColorR_ = 1.0f;
    float normalizedColorG_ = 1.0f;
    float normalizedColorB_ = 1.0f;
    bool colorDirty_ = true;
    int vboVertexCount_ = 0;  // Track VBO size locally to avoid GPU queries
    
    // Helper methods
    void ensureOutputFbo(int width = 1920, int height = 200);
    void renderLissajous();
    void updateBufferSize();
    void updateVbo();
    void loadShaders();
    void updateNormalizedColor();  // Update cached color values
    
    // Extended rendering snapshot for Oscilloscope-specific parameters
    struct OscilloscopeRenderingSnapshot : public Module::RenderingSnapshot {
        const ofColor color;
        const ofColor backgroundColor;
        
        OscilloscopeRenderingSnapshot(bool enabled, float s, float ps, const ofColor& c, const ofColor& bg)
            : RenderingSnapshot(enabled, s, ps), color(c), backgroundColor(bg) {}
    };
    
    // Override to include Oscilloscope-specific parameters
    void updateRenderingSnapshot() override;
};

