#pragma once

#include "Module.h"
#include "ofMain.h"
#include "ofxSoundObjects.h"
#include "ofxVisualObjects.h"
#include "ofxFft.h"
#include "ofShader.h"
#include "ofVbo.h"
#include <vector>
#include <memory>
#include <mutex>
#include <string>
#include <deque>

// Forward declarations
class ModuleRegistry;
class ConnectionManager;
class ParameterRouter;
class Clock;

/**
 * Spectrogram - Audio frequency spectrum visualization module
 * 
 * Implements Module interface and ofxVisualObject for video output.
 * Visualizes frequency-domain audio spectrum as a scrolling heatmap.
 * 
 * Architecture:
 * - Accepts audio input via ofxSoundObject
 * - Accumulates samples into FFT buffer
 * - Performs FFT analysis using ofxFft
 * - Stores frequency history in scrolling buffer
 * - Renders spectrogram (time vs frequency) to FBO
 * - Outputs as ofxVisualObject for routing to VideoOutput
 * 
 * Usage:
 * ```cpp
 * auto spectrogram = std::make_shared<Spectrogram>();
 * auto audioSource = std::make_shared<MultiSampler>();
 * 
 * // Connect audio source to spectrogram
 * spectrogram->connectAudioSource(audioSource->getAudioOutput());
 * 
 * // Connect spectrogram to video output
 * videoOutput->connectModule(spectrogram);
 * ```
 */
class Spectrogram : public Module, public ofxVisualObject, public ofxSoundObject {
public:
    Spectrogram();
    virtual ~Spectrogram();
    
    // Module interface implementation
    std::string getName() const override;
    ModuleType getType() const override;
    std::vector<ParameterDescriptor> getParametersImpl() const override;
    void onTrigger(TriggerEvent& event) override;
    void setParameterImpl(const std::string& paramName, float value, bool notify = true) override;
    float getParameterImpl(const std::string& paramName) const override;
    ModuleMetadata getMetadata() const override;
    
    // Routing interface - Spectrogram produces video output
    ofxVisualObject* getVideoOutput() const override { 
        return const_cast<ofxVisualObject*>(static_cast<const ofxVisualObject*>(this)); 
    }
    bool producesVideo() const override { return true; }
    
    // Audio input interface - Spectrogram accepts audio input
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
    
    void setFftSize(int fftSize);
    int getFftSize() const { return fftSize_; }
    
    void setWindowType(fftWindowType windowType);
    fftWindowType getWindowType() const { return windowType_; }
    
    // Volume-based color editing (8 stops from -120dB to 0dB)
    // stopIndex: 0-7 (8 volume stops)
    void setVolumeColor(int stopIndex, const ofColor& color);
    ofColor getVolumeColor(int stopIndex) const;
    float getVolumeStop(int stopIndex) const;  // Returns dB value for this stop
    
    void setSpeed(float speed);  // Scroll speed (replaces timeWindow)
    float getSpeed() const { return speed_; }
    
    // FFT scale type
    enum FftScale {
        FFT_SCALE_LINEAR = 0,  // Direct Hz mapping (1:1)
        FFT_SCALE_LOG = 1,     // Logarithmic scale
        FFT_SCALE_MEL = 2      // Mel scale (perceptual)
    };
    void setFftScale(FftScale scale);
    FftScale getFftScale() const { return fftScale_; }
    
    // Update method (called from main thread)
    void update() override;
    
private:
    // FFT configuration
    int fftSize_ = 2048;  // FFT buffer size (256-8192)
    fftWindowType windowType_ = OF_FFT_WINDOW_HAMMING;
    std::shared_ptr<ofxFft> fft_;
    
    // Audio buffer for FFT input
    std::vector<float> fftBuffer_;
    size_t fftBufferIndex_ = 0;
    
    // Thread safety for FFT processing
    mutable std::mutex fftMutex_;
    
    // Frequency history (scrolling buffer)
    // Each row is a time slice, each column is a frequency bin
    // New data is added on the right, old data scrolls left
    std::deque<std::vector<float>> frequencyHistory_;
    size_t maxHistorySize_;  // Maximum number of time slices
    
    // Audio sample rate (detected from input)
    float sampleRate_ = 44100.0f;
    
    // Parameters
    // Note: enabled_ is inherited from Module base class
    
    // Volume-based color stops (8 stops from -120dB to 0dB)
    struct VolumeColorStop {
        float volumeDb;  // Volume in dB (-120 to 0)
        ofColor color;
    };
    std::vector<VolumeColorStop> volumeColorStops_;  // 8 stops
    
    float speed_ = 1.0f;  // Scroll speed (multiplier for time window calculation)
    FftScale fftScale_ = FFT_SCALE_LOG;  // Default to Log scale
    
    // GPU rendering resources
    ofTexture frequencyTexture_;   // 2D texture: width=time slices, height=frequency bins
    ofShader spectrogramShader_;   // Shader for color mapping
    ofVbo quadVbo_;                // Fullscreen quad VBO (like Oscilloscope)
    std::vector<glm::vec2> quadVertices_;  // Quad vertex positions
    std::vector<glm::vec2> quadTexCoords_; // Quad texture coordinates
    bool shaderLoaded_ = false;
    
    // Texture data buffer (flat array for upload)
    std::vector<float> textureData_;
    int textureWidth_ = 0;  // Number of time slices
    int textureHeight_ = 0; // Number of frequency bins
    
    // Output FBO for visualization
    ofFbo outputFbo_;
    
    // FBO dimensions (default to reasonable size)
    int fboWidth_ = 1920;
    int fboHeight_ = 512;  // Spectrogram is typically taller
    
    // Performance optimization: dirty flag and incremental max tracking
    bool textureDirty_ = false;  // Set to true when new FFT data arrives
    float rollingMaxMagnitude_ = 1.0f;  // Incrementally tracked max magnitude
    
    // Helper methods
    void ensureOutputFbo(int width = 1920, int height = 512);
    void setupFft();
    void processFft();
    void updateHistorySize();
    void renderSpectrogram();
    void loadShaders();
    void updateTexture();
    
    // Override to initialize rendering snapshot
    void updateRenderingSnapshot() override;
    
    // FFT scale conversion helpers
    float frequencyToPosition(float freq, float minFreq, float maxFreq) const;
    float positionToFrequency(float pos, float minFreq, float maxFreq) const;
    float hzToMel(float hz) const;
    float melToHz(float mel) const;
    
    // Resampling helper - resamples FFT bins based on scale for equal precision
    std::vector<float> resampleBinsByScale(
        const std::vector<float>& fftBins,
        int targetWidth,
        float sampleRate,
        FftScale scale
    ) const;
};


