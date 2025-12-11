#include "Spectrogram.h"
#include "core/ModuleRegistry.h"
#include "core/ModuleFactory.h"
#include "ofLog.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

Spectrogram::Spectrogram() {
    // Initialize FFT
    setupFft();
    
    // Initialize 8 volume-based color stops (-120dB to 0dB)
    volumeColorStops_.resize(8);
    volumeColorStops_[0] = {-120.0f, ofColor(0, 0, 0)};      // Black (silence)
    volumeColorStops_[1] = {-90.0f, ofColor(0, 0, 100)};     // Dark blue
    volumeColorStops_[2] = {-72.0f, ofColor(0, 100, 150)};   // Cyan
    volumeColorStops_[3] = {-60.0f, ofColor(0, 200, 0)};     // Green
    volumeColorStops_[4] = {-48.0f, ofColor(255, 255, 0)};   // Yellow
    volumeColorStops_[5] = {-36.0f, ofColor(255, 150, 0)};  // Orange
    volumeColorStops_[6] = {-24.0f, ofColor(255, 0, 0)};     // Red
    volumeColorStops_[7] = {0.0f, ofColor(255, 255, 255)};   // White (loud)
    
    updateHistorySize();
    ensureOutputFbo();
    
    // Initialize quad VBO for fullscreen rendering (like Oscilloscope)
    // Triangle strip order: bottom-left, bottom-right, top-left, top-right
    quadVertices_ = {
        glm::vec2(-1.0f, -1.0f),  // Bottom-left
        glm::vec2(1.0f, -1.0f),   // Bottom-right
        glm::vec2(-1.0f, 1.0f),    // Top-left
        glm::vec2(1.0f, 1.0f)      // Top-right
    };
    quadTexCoords_ = {
        glm::vec2(0.0f, 0.0f),    // Bottom-left (OpenGL: 0,0 is bottom-left)
        glm::vec2(1.0f, 0.0f),    // Bottom-right
        glm::vec2(0.0f, 1.0f),    // Top-left
        glm::vec2(1.0f, 1.0f)     // Top-right
    };
    quadVbo_.setVertexData(quadVertices_.data(), 4, GL_STATIC_DRAW);
    quadVbo_.setTexCoordData(quadTexCoords_.data(), 4, GL_STATIC_DRAW);
}

Spectrogram::~Spectrogram() {
    // Cleanup handled by smart pointers
}

std::string Spectrogram::getName() const {
    return "Spectrogram";
}

ModuleType Spectrogram::getType() const {
    return ModuleType::UTILITY;
}

std::vector<ParameterDescriptor> Spectrogram::getParameters() const {
    std::vector<ParameterDescriptor> params;
    
    params.push_back(ParameterDescriptor(
        "enabled",
        ParameterType::BOOL,
        0.0f,
        1.0f,
        1.0f,
        "Enabled"
    ));
    
    params.push_back(ParameterDescriptor(
        "fftSize",
        ParameterType::INT,
        256.0f,
        8192.0f,
        2048.0f,
        "FFT Size"
    ));
    
    params.push_back(ParameterDescriptor(
        "windowType",
        ParameterType::INT,
        0.0f,
        4.0f,
        3.0f,  // HAMMING
        "Window Type"
    ));
    
    params.push_back(ParameterDescriptor(
        "speed",
        ParameterType::FLOAT,
        0.1f,
        5.0f,
        1.0f,
        "Speed"
    ));
    
    params.push_back(ParameterDescriptor(
        "fftScale",
        ParameterType::INT,
        0.0f,
        2.0f,
        1.0f,  // Default to Log
        "FFT Scale"
    ));
    
    return params;
}

void Spectrogram::onTrigger(TriggerEvent& event) {
    // Spectrogram doesn't respond to triggers
}

void Spectrogram::setParameter(const std::string& paramName, float value, bool notify) {
    if (paramName == "enabled") {
        setEnabled(value > 0.5f);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback("enabled", value);
        }
    } else if (paramName == "fftSize") {
        setFftSize(static_cast<int>(value));
        if (notify && parameterChangeCallback) {
            parameterChangeCallback("fftSize", value);
        }
    } else if (paramName == "windowType") {
        int typeIndex = static_cast<int>(value);
        fftWindowType type = OF_FFT_WINDOW_HAMMING;
        if (typeIndex == 0) type = OF_FFT_WINDOW_RECTANGULAR;
        else if (typeIndex == 1) type = OF_FFT_WINDOW_BARTLETT;
        else if (typeIndex == 2) type = OF_FFT_WINDOW_HANN;
        else if (typeIndex == 3) type = OF_FFT_WINDOW_HAMMING;
        else if (typeIndex == 4) type = OF_FFT_WINDOW_SINE;
        setWindowType(type);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback("windowType", value);
        }
    } else if (paramName == "speed") {
        setSpeed(value);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback("speed", value);
        }
    } else if (paramName == "fftScale") {
        int scaleIndex = static_cast<int>(value);
        FftScale scale = FFT_SCALE_LOG;
        if (scaleIndex == 0) scale = FFT_SCALE_LINEAR;
        else if (scaleIndex == 1) scale = FFT_SCALE_LOG;
        else if (scaleIndex == 2) scale = FFT_SCALE_MEL;
        setFftScale(scale);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback("fftScale", value);
        }
    }
}

float Spectrogram::getParameter(const std::string& paramName) const {
    if (paramName == "enabled") {
        return getEnabled() ? 1.0f : 0.0f;
    } else if (paramName == "fftSize") {
        return static_cast<float>(getFftSize());
    } else if (paramName == "windowType") {
        fftWindowType type = getWindowType();
        if (type == OF_FFT_WINDOW_RECTANGULAR) return 0.0f;
        else if (type == OF_FFT_WINDOW_BARTLETT) return 1.0f;
        else if (type == OF_FFT_WINDOW_HANN) return 2.0f;
        else if (type == OF_FFT_WINDOW_HAMMING) return 3.0f;
        else if (type == OF_FFT_WINDOW_SINE) return 4.0f;
        return 3.0f;
    } else if (paramName == "speed") {
        return getSpeed();
    } else if (paramName == "fftScale") {
        return static_cast<float>(getFftScale());
    }
    return Module::getParameter(paramName);
}

Module::ModuleMetadata Spectrogram::getMetadata() const {
    Module::ModuleMetadata metadata;
    metadata.typeName = "Spectrogram";
    metadata.eventNames = {};
    metadata.parameterNames = {"enabled", "fftSize", "windowType", "speed", "fftScale"};
    metadata.parameterDisplayNames["enabled"] = "Enabled";
    metadata.parameterDisplayNames["fftSize"] = "FFT Size";
    metadata.parameterDisplayNames["windowType"] = "Window Type";
    metadata.parameterDisplayNames["speed"] = "Speed";
    metadata.parameterDisplayNames["fftScale"] = "FFT Scale";
    return metadata;
}

std::vector<Port> Spectrogram::getInputPorts() const {
    std::vector<Port> ports;
    ports.push_back(Port(
        "audio_in",
        PortType::AUDIO_IN,
        false,
        "Audio Input",
        const_cast<ofxSoundObject*>(static_cast<const ofxSoundObject*>(this))
    ));
    return ports;
}

std::vector<Port> Spectrogram::getOutputPorts() const {
    std::vector<Port> ports;
    ports.push_back(Port(
        "video_out",
        PortType::VIDEO_OUT,
        false,
        "Video Output",
        const_cast<ofxVisualObject*>(static_cast<const ofxVisualObject*>(this))
    ));
    return ports;
}

ofJson Spectrogram::toJson(class ModuleRegistry* registry) const {
    ofJson json;
    json["type"] = "Spectrogram";
    json["name"] = getName();
    json["enabled"] = isEnabled();
    json["fftSize"] = fftSize_;
    json["speed"] = speed_;
    json["fftScale"] = static_cast<int>(fftScale_);
    
    // Serialize volume color stops
    json["volumeColorStops"] = ofJson::array();
    for (const auto& stop : volumeColorStops_) {
        ofJson stopJson;
        stopJson["volumeDb"] = stop.volumeDb;
        stopJson["color"]["r"] = stop.color.r;
        stopJson["color"]["g"] = stop.color.g;
        stopJson["color"]["b"] = stop.color.b;
        stopJson["color"]["a"] = stop.color.a;
        json["volumeColorStops"].push_back(stopJson);
    }
    
    // Serialize window type
    int windowTypeIndex = 3;  // Default to HAMMING
    if (windowType_ == OF_FFT_WINDOW_RECTANGULAR) windowTypeIndex = 0;
    else if (windowType_ == OF_FFT_WINDOW_BARTLETT) windowTypeIndex = 1;
    else if (windowType_ == OF_FFT_WINDOW_HANN) windowTypeIndex = 2;
    else if (windowType_ == OF_FFT_WINDOW_HAMMING) windowTypeIndex = 3;
    else if (windowType_ == OF_FFT_WINDOW_SINE) windowTypeIndex = 4;
    json["windowType"] = windowTypeIndex;
    
    return json;
}

void Spectrogram::fromJson(const ofJson& json) {
    if (json.contains("enabled")) {
        setEnabled(json["enabled"].get<bool>());
    }
    if (json.contains("fftSize")) {
        setFftSize(json["fftSize"].get<int>());
    }
    if (json.contains("windowType")) {
        int typeIndex = json["windowType"].get<int>();
        fftWindowType type = OF_FFT_WINDOW_HAMMING;
        if (typeIndex == 0) type = OF_FFT_WINDOW_RECTANGULAR;
        else if (typeIndex == 1) type = OF_FFT_WINDOW_BARTLETT;
        else if (typeIndex == 2) type = OF_FFT_WINDOW_HANN;
        else if (typeIndex == 3) type = OF_FFT_WINDOW_HAMMING;
        else if (typeIndex == 4) type = OF_FFT_WINDOW_SINE;
        setWindowType(type);
    }
    if (json.contains("speed")) {
        setSpeed(json["speed"].get<float>());
    }
    if (json.contains("fftScale")) {
        int scaleIndex = json["fftScale"].get<int>();
        FftScale scale = FFT_SCALE_LOG;
        if (scaleIndex == 0) scale = FFT_SCALE_LINEAR;
        else if (scaleIndex == 1) scale = FFT_SCALE_LOG;
        else if (scaleIndex == 2) scale = FFT_SCALE_MEL;
        setFftScale(scale);
    }
    // Backward compatibility: migrate old frequencyScale to fftScale
    else if (json.contains("frequencyScale")) {
        int scaleIndex = json["frequencyScale"].get<int>();
        FftScale scale = FFT_SCALE_LOG;
        if (scaleIndex == 0) scale = FFT_SCALE_LINEAR;
        else if (scaleIndex == 1) scale = FFT_SCALE_LOG;
        else if (scaleIndex == 2) scale = FFT_SCALE_MEL;
        setFftScale(scale);
    }
    
    // Load volume color stops
    if (json.contains("volumeColorStops") && json["volumeColorStops"].is_array()) {
        volumeColorStops_.clear();
        for (const auto& stopJson : json["volumeColorStops"]) {
            VolumeColorStop stop;
            stop.volumeDb = stopJson["volumeDb"].get<float>();
            stop.color.r = stopJson["color"]["r"].get<int>();
            stop.color.g = stopJson["color"]["g"].get<int>();
            stop.color.b = stopJson["color"]["b"].get<int>();
            stop.color.a = stopJson.contains("color") && stopJson["color"].contains("a") 
                          ? stopJson["color"]["a"].get<int>() : 255;
            volumeColorStops_.push_back(stop);
        }
        // Ensure we have 8 stops
        while (volumeColorStops_.size() < 8) {
            volumeColorStops_.push_back({0.0f, ofColor::white});
        }
        volumeColorStops_.resize(8);
    }
    
    // Backward compatibility: migrate old low/mid/high colors to volume stops
    if (json.contains("lowBandColor") || json.contains("midBandColor") || json.contains("highBandColor")) {
        volumeColorStops_.clear();
        volumeColorStops_.resize(8);
        // Distribute old colors across stops
        if (json.contains("lowBandColor")) {
            ofColor c;
            c.r = json["lowBandColor"]["r"].get<int>();
            c.g = json["lowBandColor"]["g"].get<int>();
            c.b = json["lowBandColor"]["b"].get<int>();
            volumeColorStops_[0] = {-120.0f, c};
            volumeColorStops_[1] = {-90.0f, c};
        }
        if (json.contains("midBandColor")) {
            ofColor c;
            c.r = json["midBandColor"]["r"].get<int>();
            c.g = json["midBandColor"]["g"].get<int>();
            c.b = json["midBandColor"]["b"].get<int>();
            volumeColorStops_[3] = {-60.0f, c};
            volumeColorStops_[4] = {-48.0f, c};
        }
        if (json.contains("highBandColor")) {
            ofColor c;
            c.r = json["highBandColor"]["r"].get<int>();
            c.g = json["highBandColor"]["g"].get<int>();
            c.b = json["highBandColor"]["b"].get<int>();
            volumeColorStops_[6] = {-24.0f, c};
            volumeColorStops_[7] = {0.0f, c};
        }
    }
}

void Spectrogram::process(ofSoundBuffer& input, ofSoundBuffer& output) {
    // Pass audio through unchanged (we're just monitoring)
    input.copyTo(output);
    
    if (!isEnabled()) {
        return;
    }
    
    // Update sample rate from input
    if (input.getSampleRate() > 0) {
        sampleRate_ = input.getSampleRate();
    }
    
    // Extract mono signal and accumulate into FFT buffer
    size_t numFrames = input.getNumFrames();
    size_t numChannels = input.getNumChannels();
    
    std::lock_guard<std::mutex> lock(fftMutex_);
    
    for (size_t i = 0; i < numFrames; i++) {
        // Average all channels to get mono signal
        float sample = 0.0f;
        for (size_t ch = 0; ch < numChannels; ch++) {
            sample += input.getSample(i, ch);
        }
        sample /= numChannels;
        
        // Add to FFT buffer
        if (fftBufferIndex_ < fftBuffer_.size()) {
            fftBuffer_[fftBufferIndex_] = sample;
            fftBufferIndex_++;
            
            // When buffer is full, process FFT
            if (fftBufferIndex_ >= fftBuffer_.size()) {
                processFft();
                fftBufferIndex_ = 0;
            }
        }
    }
}

void Spectrogram::process(ofFbo& input, ofFbo& output) {
    // Spectrogram generates its own visualization, so input is ignored
    // Use input FBO dimensions for output (or default if input is not allocated)
    int width = input.isAllocated() ? input.getWidth() : 1920;
    int height = input.isAllocated() ? input.getHeight() : 1080;
    
    // Ensure output FBO matches input dimensions
    ensureOutputFbo(width, height);
    
    // Render spectrogram to output FBO
    renderSpectrogram();
    
    // Copy to output
    if (outputFbo_.isAllocated()) {
        if (!output.isAllocated() || output.getWidth() != width || output.getHeight() != height) {
            ofFbo::Settings settings;
            settings.width = width;
            settings.height = height;
            settings.internalformat = GL_RGBA;
            settings.useDepth = false;
            settings.useStencil = false;
            output.allocate(settings);
        }
        output.begin();
        ofClear(0, 0, 0, 0);
        outputFbo_.draw(0, 0, width, height);
        output.end();
    } else {
        // Fallback: clear output if FBO not ready
        if (output.isAllocated()) {
            output.begin();
            ofClear(0, 0, 0, 0);
            output.end();
        }
    }
}

void Spectrogram::update() {
    // Called from main thread - texture update now handled in renderSpectrogram()
    // when dirty flag is set, avoiding unnecessary updates every frame
}

// setEnabled() is inherited from Module base class

void Spectrogram::setFftSize(int fftSize) {
    // Ensure fftSize is power of 2 and within range
    int newSize = std::max(256, std::min(8192, fftSize));
    // Round to nearest power of 2
    int power = 1;
    while (power < newSize) power <<= 1;
    if (power > newSize) power >>= 1;
    
    if (power != fftSize_) {
        fftSize_ = power;
        setupFft();
        textureDirty_ = true;  // FFT size change requires texture regeneration
    }
}

void Spectrogram::setWindowType(fftWindowType windowType) {
    if (windowType_ != windowType) {
        windowType_ = windowType;
        setupFft();
        textureDirty_ = true;  // Window type change requires texture regeneration
    }
}

void Spectrogram::setVolumeColor(int stopIndex, const ofColor& color) {
    if (stopIndex >= 0 && stopIndex < static_cast<int>(volumeColorStops_.size())) {
        volumeColorStops_[stopIndex].color = color;
    }
}

ofColor Spectrogram::getVolumeColor(int stopIndex) const {
    if (stopIndex >= 0 && stopIndex < static_cast<int>(volumeColorStops_.size())) {
        return volumeColorStops_[stopIndex].color;
    }
    return ofColor::white;  // Default fallback
}

float Spectrogram::getVolumeStop(int stopIndex) const {
    if (stopIndex >= 0 && stopIndex < static_cast<int>(volumeColorStops_.size())) {
        return volumeColorStops_[stopIndex].volumeDb;
    }
    return -120.0f;  // Default fallback
}

void Spectrogram::setSpeed(float speed) {
    speed_ = std::max(0.1f, std::min(5.0f, speed));
    updateHistorySize();
    textureDirty_ = true;  // Speed change affects history size and texture dimensions
}

void Spectrogram::setFftScale(FftScale scale) {
    if (fftScale_ != scale) {
        fftScale_ = scale;
        textureDirty_ = true;  // Scale change requires texture regeneration
    }
}

void Spectrogram::setupFft() {
    fft_ = std::shared_ptr<ofxFft>(ofxFft::create(fftSize_, windowType_));
    fftBuffer_.resize(fftSize_);
    fftBufferIndex_ = 0;
}

void Spectrogram::processFft() {
    if (!fft_ || fftBuffer_.empty()) {
        return;
    }
    
    // Set signal and compute FFT
    fft_->setSignal(fftBuffer_.data());
    float* amplitudes = fft_->getAmplitude();
    int binSize = fft_->getBinSize();
    
    // Copy frequency bins to history
    std::vector<float> frequencyBins(binSize);
    float sliceMax = 0.0f;
    for (int i = 0; i < binSize; i++) {
        frequencyBins[i] = amplitudes[i];
        sliceMax = std::max(sliceMax, amplitudes[i]);
    }
    
    // Update rolling max incrementally (fast rise, slow decay)
    if (sliceMax > rollingMaxMagnitude_) {
        rollingMaxMagnitude_ = sliceMax;
    } else {
        rollingMaxMagnitude_ = rollingMaxMagnitude_ * 0.995f + sliceMax * 0.005f;
    }
    if (rollingMaxMagnitude_ < 0.0001f) rollingMaxMagnitude_ = 1.0f;
    
    // Add to history (new data on the right)
    frequencyHistory_.push_back(frequencyBins);
    
    // Trim history if too large
    while (frequencyHistory_.size() > maxHistorySize_) {
        frequencyHistory_.pop_front();
    }
    
    // Mark texture as dirty - new data arrived
    textureDirty_ = true;
}

void Spectrogram::updateHistorySize() {
    // Calculate max history size based on speed
    // Base time window: 5 seconds, adjusted by speed (higher speed = shorter window)
    float baseTimeWindow = 5.0f / speed_;  // Faster speed = shorter window
    float fftDuration = static_cast<float>(fftSize_) / sampleRate_;
    size_t newMaxSize = static_cast<size_t>(baseTimeWindow / fftDuration);
    newMaxSize = std::max(static_cast<size_t>(100), newMaxSize);  // Minimum 100 slices
    
    std::lock_guard<std::mutex> lock(fftMutex_);
    
    // If size changed, resize history to maintain fixed size (prevents startup stretching)
    if (newMaxSize != maxHistorySize_) {
        maxHistorySize_ = newMaxSize;
        
        // Resize history to fixed size, padding with zeros if needed
        int binSize = fft_ ? fft_->getBinSize() : 0;
        if (binSize > 0) {
            while (frequencyHistory_.size() < maxHistorySize_) {
                frequencyHistory_.push_back(std::vector<float>(binSize, 0.0f));
            }
            while (frequencyHistory_.size() > maxHistorySize_) {
                frequencyHistory_.pop_front();
            }
        }
        
        textureDirty_ = true;  // History size change affects texture dimensions
    }
}

void Spectrogram::ensureOutputFbo(int width, int height) {
    if (outputFbo_.getWidth() != width || outputFbo_.getHeight() != height) {
        ofFbo::Settings settings;
        settings.width = width;
        settings.height = height;
        settings.internalformat = GL_RGBA;
        settings.useDepth = false;
        settings.useStencil = false;
        outputFbo_.allocate(settings);
        fboWidth_ = width;
        fboHeight_ = height;
        textureDirty_ = true;  // FBO dimension change affects texture width
    }
}

void Spectrogram::loadShaders() {
    if (shaderLoaded_) {
        return;  // Already loaded
    }
    
    // Embedded shader source code (GLSL 120 for macOS compatibility)
    const std::string vertexShaderSource = R"(
#version 120

attribute vec2 position;
attribute vec2 texCoord;

varying vec2 vTexCoord;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    vTexCoord = texCoord;
}
)";

    const std::string fragmentShaderSource = R"(
#version 120

uniform sampler2D frequencyTexture;
uniform vec3 colorStops[8];      // 8 color stops (RGB)
uniform float stopVolumes[8];    // 8 volume thresholds in dB (-120 to 0)
uniform float opacity;

varying vec2 vTexCoord;

void main() {
    // Direct texture lookup - resampling already done on CPU
    // Texture X = frequency position (already resampled based on scale)
    // Texture Y = time (vTexCoord.y)
    float magnitude = texture2D(frequencyTexture, vTexCoord).r;
    
    // Convert normalized magnitude to dB
    float volumeDb = -120.0 + magnitude * 120.0;  // Map 0-1 to -120dB to 0dB
    
    // Find which two color stops to interpolate between based on volume
    vec3 color = vec3(0.0, 0.0, 0.0);
    
    // Find the stop index
    int stopIndex = 0;
    for (int i = 0; i < 7; i++) {
        if (volumeDb >= stopVolumes[i] && volumeDb <= stopVolumes[i + 1]) {
            stopIndex = i;
            break;
        }
    }
    
    // Clamp to valid range
    if (volumeDb <= stopVolumes[0]) {
        color = colorStops[0];
    } else if (volumeDb >= stopVolumes[7]) {
        color = colorStops[7];
    } else {
        // Interpolate between stops
        float t = (volumeDb - stopVolumes[stopIndex]) / 
                  (stopVolumes[stopIndex + 1] - stopVolumes[stopIndex]);
        color = mix(colorStops[stopIndex], colorStops[stopIndex + 1], t);
    }
    
    // Apply smooth brightness fade based on volume
    float brightness = 1.0;
    if (volumeDb < -60.0) {
        float fadeStart = -60.0;
        float fadeEnd = -120.0;
        float fadeRange = fadeStart - fadeEnd;
        float fadeAmount = (fadeStart - volumeDb) / fadeRange;
        fadeAmount = max(0.0, min(1.0, fadeAmount));
        fadeAmount = fadeAmount * fadeAmount * (3.0 - 2.0 * fadeAmount);  // smoothstep
        brightness = 1.0 - fadeAmount * 0.7;
    }
    brightness = max(0.3, min(1.0, brightness));
    
    color *= brightness;
    gl_FragColor = vec4(color, opacity);
}
)";
    
    // Load shaders from source strings
    if (spectrogramShader_.setupShaderFromSource(GL_VERTEX_SHADER, vertexShaderSource) &&
        spectrogramShader_.setupShaderFromSource(GL_FRAGMENT_SHADER, fragmentShaderSource)) {
        // ofMesh uses default attribute locations, but we bind explicitly for GLSL 120 compatibility
        spectrogramShader_.bindAttribute(0, "position");
        spectrogramShader_.bindAttribute(1, "texCoord");
        
        if (spectrogramShader_.linkProgram()) {
            shaderLoaded_ = true;
        } else {
            shaderLoaded_ = false;
        }
    } else {
        shaderLoaded_ = false;
    }
}

void Spectrogram::updateTexture() {
    // Only update if texture is dirty (new FFT data arrived)
    if (!textureDirty_) {
        return;
    }
    
    // Copy frequency history (with lock)
    std::deque<std::vector<float>> history;
    {
        std::lock_guard<std::mutex> lock(fftMutex_);
        history = frequencyHistory_;
    }
    
    if (!fft_) {
        textureWidth_ = 0;
        textureHeight_ = 0;
        textureDirty_ = false;
        return;
    }
    
    int binSize = fft_->getBinSize();
    if (binSize == 0) {
        textureWidth_ = 0;
        textureHeight_ = 0;
        textureDirty_ = false;
        return;
    }
    
    // Fixed texture dimensions - prevents startup stretching
    // Use FBO width for 1:1 pixel mapping, or fixed 1024 for consistent quality
    int targetWidth = fboWidth_ > 0 ? fboWidth_ : 1024;
    int targetHeight = static_cast<int>(maxHistorySize_);
    
    // Use incrementally tracked max magnitude (updated in processFft())
    float maxMagnitude = rollingMaxMagnitude_;
    
    // Resize texture if dimensions changed
    if (textureWidth_ != targetWidth || textureHeight_ != targetHeight) {
        textureWidth_ = targetWidth;
        textureHeight_ = targetHeight;
        textureData_.resize(textureWidth_ * textureHeight_, 0.0f);
    }
    
    // Fill texture with resampled data based on scale
    // Each column represents equal portion of display scale (linear/log/mel)
    for (int y = 0; y < targetHeight; y++) {
        int sliceIndex = targetHeight - 1 - y;  // Newest at top (y=targetHeight-1)
        
        std::vector<float> resampledRow;
        if (sliceIndex >= 0 && sliceIndex < static_cast<int>(history.size())) {
            // Resample FFT bins based on scale for equal precision
            const auto& slice = history[sliceIndex];
            resampledRow = resampleBinsByScale(slice, targetWidth, sampleRate_, fftScale_);
        } else {
            // Pad with zeros when history is not yet full
            resampledRow.resize(targetWidth, 0.0f);
        }
        
        // Normalize and store
        for (int x = 0; x < targetWidth; x++) {
            float normalized = resampledRow[x] / maxMagnitude;
            normalized = std::max(0.0f, std::min(1.0f, normalized));
            textureData_[y * textureWidth_ + x] = normalized;
        }
    }
    
    // Upload to texture
    if (!frequencyTexture_.isAllocated() || 
        frequencyTexture_.getWidth() != textureWidth_ || 
        frequencyTexture_.getHeight() != textureHeight_) {
        frequencyTexture_.allocate(textureWidth_, textureHeight_, GL_LUMINANCE, false);
        frequencyTexture_.setTextureMinMagFilter(GL_LINEAR, GL_LINEAR);
        frequencyTexture_.setTextureWrap(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
    }
    
    frequencyTexture_.loadData(textureData_.data(), textureWidth_, textureHeight_, GL_LUMINANCE);
    
    // Clear dirty flag after successful update
    textureDirty_ = false;
}

void Spectrogram::renderSpectrogram() {
    // Use current FBO dimensions (set by process() or default)
    if (!outputFbo_.isAllocated()) {
        ensureOutputFbo(fboWidth_, fboHeight_);
    }
    
    if (!isEnabled()) {
        // Clear FBO if disabled
        outputFbo_.begin();
        ofClear(0, 0, 0, 0);
        outputFbo_.end();
        return;
    }
    
    // Load shaders if not already loaded
    if (!shaderLoaded_) {
        loadShaders();
    }
    
    // Update texture only if dirty (new FFT data arrived)
    if (textureDirty_) {
        updateTexture();
    }
    
    if (!shaderLoaded_ || textureWidth_ == 0 || textureHeight_ == 0 || !frequencyTexture_.isAllocated()) {
        // No data or shader failed - clear FBO
        outputFbo_.begin();
        ofClear(0, 0, 0, 0);
        outputFbo_.end();
        return;
    }
    
    // Render spectrogram using GPU
    outputFbo_.begin();
    ofClear(0, 0, 0, 0);
    
    // Set up orthographic projection
    ofPushMatrix();
    ofPushView();
    ofViewport(0, 0, fboWidth_, fboHeight_);
    
    ofMatrix4x4 ortho;
    ortho.makeOrthoMatrix(-1, 1, -1, 1, -1, 1);
    ofGetCurrentRenderer()->matrixMode(OF_MATRIX_PROJECTION);
    ofGetCurrentRenderer()->loadMatrix(ortho);
    ofGetCurrentRenderer()->matrixMode(OF_MATRIX_MODELVIEW);
    ofGetCurrentRenderer()->loadIdentityMatrix();
    
    // Enable alpha blending
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Bind shader and set uniforms
    spectrogramShader_.begin();
    
    // Get attribute locations (like Oscilloscope does)
    GLint positionLoc = spectrogramShader_.getAttributeLocation("position");
    GLint texCoordLoc = spectrogramShader_.getAttributeLocation("texCoord");
    
    if (positionLoc < 0 || texCoordLoc < 0) {
        frequencyTexture_.unbind();
        spectrogramShader_.end();
        ofDisableBlendMode();
        ofPopView();
        ofPopMatrix();
        outputFbo_.end();
        return;
    }
    
    // Set volume-based color stop uniforms
    float colorArray[8 * 3];  // 8 stops * 3 components (RGB)
    float volumeArray[8];     // 8 volume thresholds in dB
    
    for (int i = 0; i < 8; i++) {
        colorArray[i * 3 + 0] = volumeColorStops_[i].color.r / 255.0f;
        colorArray[i * 3 + 1] = volumeColorStops_[i].color.g / 255.0f;
        colorArray[i * 3 + 2] = volumeColorStops_[i].color.b / 255.0f;
        volumeArray[i] = volumeColorStops_[i].volumeDb;
    }
    
    spectrogramShader_.setUniform3fv("colorStops", colorArray, 8);
    spectrogramShader_.setUniform1fv("stopVolumes", volumeArray, 8);
    spectrogramShader_.setUniform1f("opacity", 1.0f);
    
    // Bind texture and set uniform
    frequencyTexture_.bind();
    spectrogramShader_.setUniformTexture("frequencyTexture", frequencyTexture_, 0);
    
    // Draw quad manually using client-side vertex arrays
    // Make sure no VBO is bound (unbind any existing VBO)
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    
    glEnableVertexAttribArray(positionLoc);
    glEnableVertexAttribArray(texCoordLoc);
    
    // Set up vertex attribute pointers using client-side arrays
    // Note: When using client-side arrays, the pointer is a direct memory address
    glVertexAttribPointer(positionLoc, 2, GL_FLOAT, GL_FALSE, 0, quadVertices_.data());
    glVertexAttribPointer(texCoordLoc, 2, GL_FLOAT, GL_FALSE, 0, quadTexCoords_.data());
    
    // Draw triangle strip (4 vertices = 2 triangles)
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    // Cleanup
    glDisableVertexAttribArray(positionLoc);
    glDisableVertexAttribArray(texCoordLoc);
    
    frequencyTexture_.unbind();
    spectrogramShader_.end();
    
    // Disable OpenGL states
    ofDisableBlendMode();
    
    ofPopView();
    ofPopMatrix();
    outputFbo_.end();
}

//--------------------------------------------------------------
// FFT Scale Conversion Helpers
//--------------------------------------------------------------

float Spectrogram::hzToMel(float hz) const {
    // Mel scale formula: m = 2595 * log10(1 + f/700)
    return 2595.0f * std::log10(1.0f + hz / 700.0f);
}

float Spectrogram::melToHz(float mel) const {
    // Inverse Mel scale: f = 700 * (10^(m/2595) - 1)
    return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}

float Spectrogram::frequencyToPosition(float freq, float minFreq, float maxFreq) const {
    switch (fftScale_) {
        case FFT_SCALE_LINEAR: {
            // Linear: direct mapping
            return (freq - minFreq) / (maxFreq - minFreq);
        }
        case FFT_SCALE_LOG: {
            // Logarithmic: equal ratios = equal distances
            if (freq <= minFreq) return 0.0f;
            if (freq >= maxFreq) return 1.0f;
            float logMin = std::log(minFreq);
            float logMax = std::log(maxFreq);
            float logFreq = std::log(freq);
            return (logFreq - logMin) / (logMax - logMin);
        }
        case FFT_SCALE_MEL: {
            // Mel: perceptual scale
            if (freq <= minFreq) return 0.0f;
            if (freq >= maxFreq) return 1.0f;
            float melMin = hzToMel(minFreq);
            float melMax = hzToMel(maxFreq);
            float melFreq = hzToMel(freq);
            return (melFreq - melMin) / (melMax - melMin);
        }
        default:
            return 0.0f;
    }
}

float Spectrogram::positionToFrequency(float pos, float minFreq, float maxFreq) const {
    pos = std::max(0.0f, std::min(1.0f, pos));  // Clamp to 0-1
    
    switch (fftScale_) {
        case FFT_SCALE_LINEAR: {
            // Linear: direct mapping
            return minFreq + pos * (maxFreq - minFreq);
        }
        case FFT_SCALE_LOG: {
            // Logarithmic: equal ratios = equal distances
            float logMin = std::log(minFreq);
            float logMax = std::log(maxFreq);
            float logFreq = logMin + pos * (logMax - logMin);
            return std::exp(logFreq);
        }
        case FFT_SCALE_MEL: {
            // Mel: perceptual scale
            float melMin = hzToMel(minFreq);
            float melMax = hzToMel(maxFreq);
            float melFreq = melMin + pos * (melMax - melMin);
            return melToHz(melFreq);
        }
        default:
            return minFreq;
    }
}

std::vector<float> Spectrogram::resampleBinsByScale(
    const std::vector<float>& fftBins,
    int targetWidth,
    float sampleRate,
    FftScale scale
) const {
    std::vector<float> resampled(targetWidth, 0.0f);
    
    if (fftBins.empty() || targetWidth <= 0) {
        return resampled;
    }
    
    int numBins = static_cast<int>(fftBins.size());
    float nyquist = sampleRate * 0.5f;
    float minFreq = 20.0f;
    float maxFreq = 20000.0f;
    
    // For each output texture column, calculate target frequency and sample from FFT bins
    for (int texCol = 0; texCol < targetWidth; texCol++) {
        // Calculate position in display (0.0 to 1.0)
        float pos = static_cast<float>(texCol) / static_cast<float>(targetWidth - 1);
        
        // Convert position to target frequency based on scale
        float targetFreq = positionToFrequency(pos, minFreq, maxFreq);
        
        // Convert frequency to FFT bin index
        float binIndexFloat = (targetFreq / nyquist) * numBins;
        
        // Clamp to valid bin range
        int binIndex = static_cast<int>(std::max(0.0f, std::min(static_cast<float>(numBins - 1), binIndexFloat)));
        int nextBinIndex = std::min(numBins - 1, binIndex + 1);
        
        // Linear interpolation between bins for smooth resampling
        float t = binIndexFloat - static_cast<float>(binIndex);
        resampled[texCol] = fftBins[binIndex] * (1.0f - t) + fftBins[nextBinIndex] * t;
    }
    
    return resampled;
}

//--------------------------------------------------------------
// Module Factory Registration
//--------------------------------------------------------------
namespace {
    struct SpectrogramRegistrar {
        SpectrogramRegistrar() {
            ModuleFactory::registerModuleType("Spectrogram", 
                []() -> std::shared_ptr<Module> {
                    return std::make_shared<Spectrogram>();
                });
        }
    };
    static SpectrogramRegistrar g_spectrogramRegistrar;
}










