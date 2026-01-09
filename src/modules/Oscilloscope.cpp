#include "Oscilloscope.h"
#include "core/ModuleRegistry.h"
#include "core/ModuleFactory.h"
#include "ofLog.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

// Static cached orthographic matrix (never changes, so cache it)
static ofMatrix4x4 cachedOrthoMatrix_;
static bool orthoMatrixInitialized_ = false;

Oscilloscope::Oscilloscope() 
    : maxBufferSize_(MAX_BUFFER_SIZE) {
    // Initialize with default parameters
    updateBufferSize();
    ensureOutputFbo();
    
    // Reserve memory for vertex buffer (GPU resources created lazily on first render)
    vertices_.reserve(MAX_BUFFER_SIZE);
    
    // Initialize cached color values
    updateNormalizedColor();
    
    // Initialize rendering snapshot
    updateRenderingSnapshot();
}

Oscilloscope::~Oscilloscope() {
    // Cleanup handled by smart pointers
}

std::string Oscilloscope::getName() const {
    return "Oscilloscope";
}

ModuleType Oscilloscope::getType() const {
    return ModuleType::UTILITY;
}

std::vector<ParameterDescriptor> Oscilloscope::getParametersImpl() const {
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
        "scale",
        ParameterType::FLOAT,
        0.1f,
        5.0f,
        0.5f,  // Default to 0.5 (half scale) for better visibility
        "Scale"
    ));
    
    params.push_back(ParameterDescriptor(
        "pointSize",
        ParameterType::FLOAT,
        0.5f,
        2.0f,
        1.0f,
        "Point Size"
    ));
    
    return params;
}

void Oscilloscope::onTrigger(TriggerEvent& event) {
    // Oscilloscope doesn't respond to triggers
}

void Oscilloscope::setParameterImpl(const std::string& paramName, float value, bool notify) {
    if (paramName == "enabled") {
        setEnabled(value > 0.5f);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback("enabled", value);
        }
    } else if (paramName == "scale") {
        setScale(value);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback("scale", value);
        }
    } else if (paramName == "pointSize") {
        setPointSize(value);
        if (notify && parameterChangeCallback) {
            parameterChangeCallback("pointSize", value);
        }
    }
    // Background color is handled via GUI color picker, not as a float parameter
}

float Oscilloscope::getParameterImpl(const std::string& paramName) const {
    if (paramName == "enabled") {
        return getEnabled() ? 1.0f : 0.0f;
    } else if (paramName == "scale") {
        return getScale();
    } else if (paramName == "pointSize") {
        return getPointSize();
    }
    // Unknown parameter - return default (base class default is 0.0f)
    // NOTE: Cannot call Module::getParameter() here as it would deadlock (lock already held)
    return 0.0f;
}

Module::ModuleMetadata Oscilloscope::getMetadata() const {
    Module::ModuleMetadata metadata;
    metadata.typeName = "Oscilloscope";
    metadata.eventNames = {};
    metadata.parameterNames = {"enabled", "scale", "pointSize"};
    metadata.parameterDisplayNames["enabled"] = "Enabled";
    metadata.parameterDisplayNames["scale"] = "Scale";
    metadata.parameterDisplayNames["pointSize"] = "Point Size";
    return metadata;
}

std::vector<Port> Oscilloscope::getInputPorts() const {
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

std::vector<Port> Oscilloscope::getOutputPorts() const {
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

ofJson Oscilloscope::toJson(class ModuleRegistry* registry) const {
    ofJson json;
    json["type"] = "Oscilloscope";
    json["name"] = getName();
    json["enabled"] = isEnabled();
    json["scale"] = scale_;
    json["pointSize"] = pointSize_;
    
    // Serialize colors
    json["color"]["r"] = color_.r;
    json["color"]["g"] = color_.g;
    json["color"]["b"] = color_.b;
    json["color"]["a"] = color_.a;
    
    json["backgroundColor"]["r"] = backgroundColor_.r;
    json["backgroundColor"]["g"] = backgroundColor_.g;
    json["backgroundColor"]["b"] = backgroundColor_.b;
    json["backgroundColor"]["a"] = backgroundColor_.a;
    
    return json;
}

void Oscilloscope::fromJson(const ofJson& json) {
    if (json.contains("enabled")) {
        setEnabled(json["enabled"].get<bool>());
    }
    if (json.contains("scale")) {
        setScale(json["scale"].get<float>());
    }
    if (json.contains("thickness")) {
        // Legacy support - map to pointSize
        setPointSize(json["thickness"].get<float>());
    }
    if (json.contains("pointSize")) {
        setPointSize(json["pointSize"].get<float>());
    }
    
    // Load colors (setColor will update the cache)
    if (json.contains("color")) {
        ofColor c;
        c.r = json["color"]["r"].get<int>();
        c.g = json["color"]["g"].get<int>();
        c.b = json["color"]["b"].get<int>();
        c.a = json["color"]["a"].get<int>();
        setColor(c);  // This will call updateNormalizedColor()
    }
    
    if (json.contains("backgroundColor")) {
        ofColor bg;
        bg.r = json["backgroundColor"]["r"].get<int>();
        bg.g = json["backgroundColor"]["g"].get<int>();
        bg.b = json["backgroundColor"]["b"].get<int>();
        bg.a = json["backgroundColor"]["a"].get<int>();
        setBackgroundColor(bg);
    }
}

void Oscilloscope::process(ofSoundBuffer& input, ofSoundBuffer& output) {
    // Pass audio through unchanged (we're just monitoring)
    input.copyTo(output);
    
    if (!isEnabled()) {
        return;
    }
    
    // Update sample rate from input
    if (input.getSampleRate() > 0) {
        sampleRate_ = input.getSampleRate();
        updateBufferSize();
    }
    
    size_t numFrames = input.getNumFrames();
    size_t numChannels = input.getNumChannels();
    
    std::lock_guard<std::mutex> lock(audioMutex_);
    
    // Extract stereo X-Y pairs for Lissajous visualization
    // Left channel = X, Right channel = Y
    for (size_t i = 0; i < numFrames; i++) {
        float x = 0.0f;
        float y = 0.0f;
        
        if (numChannels >= 2) {
            // Stereo: use left and right channels
            x = input.getSample(i, 0);  // Left channel = X
            y = input.getSample(i, 1);  // Right channel = Y
        } else if (numChannels == 1) {
            // Mono input: duplicate to both channels (creates diagonal line)
            x = input.getSample(i, 0);
            y = x;
        } else {
            // Multi-channel: use first two channels
            x = input.getSample(i, 0);
            y = (numChannels > 1) ? input.getSample(i, 1) : x;
        }
        
        // Add X-Y pair to circular buffer
        audioBufferXY_.push_back(std::make_pair(x, y));
        
        // Maintain circular buffer size
        if (audioBufferXY_.size() > maxBufferSize_) {
            audioBufferXY_.pop_front();
        }
    }
}

void Oscilloscope::process(ofFbo& input, ofFbo& output) {
    int inputWidth = input.isAllocated() ? input.getWidth() : ofGetWidth();
    int inputHeight = input.isAllocated() ? input.getHeight() : ofGetHeight();
    
    int size = std::min(inputWidth, inputHeight);
    if (size <= 0) {
        size = 512;
    }
    
    ensureOutputFbo(size, size);
    renderLissajous();
    
    if (outputFbo_.isAllocated()) {
        if (!output.isAllocated() || output.getWidth() != inputWidth || output.getHeight() != inputHeight) {
            ofFbo::Settings settings;
            settings.width = inputWidth;
            settings.height = inputHeight;
            settings.internalformat = GL_RGBA;
            settings.useDepth = false;
            settings.useStencil = false;
            output.allocate(settings);
        }
        output.begin();
        ofClear(backgroundColor_);
        
        float scaleX = static_cast<float>(inputWidth) / static_cast<float>(size);
        float scaleY = static_cast<float>(inputHeight) / static_cast<float>(size);
        float scale = std::min(scaleX, scaleY);
        float offsetX = (inputWidth - size * scale) * 0.5f;
        float offsetY = (inputHeight - size * scale) * 0.5f;
        
        ofPushMatrix();
        ofTranslate(offsetX, offsetY);
        ofScale(scale, scale);
        outputFbo_.draw(0, 0, size, size);
        ofPopMatrix();
        
        output.end();
    } else {
        if (output.isAllocated()) {
            output.begin();
            ofClear(backgroundColor_);
            output.end();
        }
    }
}

void Oscilloscope::update() {
    // Called from main thread - safe to update VBO here
    if (isEnabled()) {
        // Update buffer size based on current framerate
        updateBufferSize();
        
        // Update VBO with latest audio data
        updateVbo();
    }
}

// setEnabled() is inherited from Module base class

void Oscilloscope::setScale(float scale) {
    // Allow scale > 1.0 for larger visualizations (can exceed NDC bounds)
    scale_ = std::max(0.1f, scale);  // Minimum 0.1, no maximum
}


void Oscilloscope::setColor(const ofColor& color) {
    color_ = color;
    colorDirty_ = true;
    updateNormalizedColor();
}

void Oscilloscope::setBackgroundColor(const ofColor& color) {
    backgroundColor_ = color;
}

void Oscilloscope::setThickness(float thickness) {
    // Legacy method - maps to pointSize for backward compatibility
    pointSize_ = std::max(0.5f, std::min(2.0f, thickness));
}

void Oscilloscope::setPointSize(float pointSize) {
    pointSize_ = std::max(0.5f, std::min(2.0f, pointSize));
}

float Oscilloscope::getPointSize() const {
    return pointSize_;
}


void Oscilloscope::updateBufferSize() {
    float frameRate = ofGetFrameRate();
    if (frameRate <= 0.0f) {
        frameRate = 60.0f;
    }
    
    float frameDuration = 1.0f / frameRate;
    maxBufferSize_ = static_cast<size_t>(frameDuration * sampleRate_);
    maxBufferSize_ = std::min(maxBufferSize_, MAX_BUFFER_SIZE);
    
    std::lock_guard<std::mutex> lock(audioMutex_);
    while (audioBufferXY_.size() > maxBufferSize_) {
        audioBufferXY_.pop_front();
    }
}

void Oscilloscope::ensureOutputFbo(int width, int height) {
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
        
        // VBO may need to be reallocated if size changed significantly
        // (will be handled in updateVbo())
    }
}

void Oscilloscope::updateNormalizedColor() {
    if (colorDirty_) {
        normalizedColorR_ = (color_.r > 0 || color_.g > 0 || color_.b > 0) ? color_.r / 255.0f : 1.0f;
        normalizedColorG_ = (color_.r > 0 || color_.g > 0 || color_.b > 0) ? color_.g / 255.0f : 1.0f;
        normalizedColorB_ = (color_.r > 0 || color_.g > 0 || color_.b > 0) ? color_.b / 255.0f : 1.0f;
        colorDirty_ = false;
    }
}

void Oscilloscope::loadShaders() {
    if (shaderLoaded_) {
        return;
    }
    
    const std::string vertexShaderSource = R"(
#version 120
attribute vec3 position;
uniform float scale;
varying vec4 vColor;
void main() {
    vec2 scaledPos = position.xy * scale;
    gl_Position = vec4(scaledPos, 0.0, 1.0);
    vColor = vec4(1.0, 1.0, 1.0, 1.0);
}
)";

    const std::string fragmentShaderSource = R"(
#version 120
varying vec4 vColor;
uniform vec4 drawColor;
void main() {
    gl_FragColor = drawColor;
}
)";
    
    if (shader_.setupShaderFromSource(GL_VERTEX_SHADER, vertexShaderSource) &&
        shader_.setupShaderFromSource(GL_FRAGMENT_SHADER, fragmentShaderSource)) {
        shader_.bindAttribute(0, "position");
        
        if (shader_.linkProgram()) {
            shaderLoaded_ = true;
        } else {
            ofLogError("Oscilloscope") << "Failed to link shaders";
            shaderLoaded_ = false;
        }
    } else {
        ofLogError("Oscilloscope") << "Failed to compile shaders";
        shaderLoaded_ = false;
    }
}

void Oscilloscope::updateVbo() {
    std::vector<std::pair<float, float>> samplesXY;
    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        samplesXY.assign(audioBufferXY_.begin(), audioBufferXY_.end());
    }
    
    if (samplesXY.empty() || samplesXY.size() < 2) {
        vertices_.clear();
        return;
    }
    
    vertices_.clear();
    vertices_.reserve((samplesXY.size() - 1) * 6);
    
    float safeScale = std::max(scale_, 0.1f);
    float halfWidth = (pointSize_ / static_cast<float>(fboWidth_)) * 2.0f / safeScale;
    
    for (size_t i = 0; i < samplesXY.size() - 1; i++) {
        glm::vec2 p0(samplesXY[i].first, samplesXY[i].second);
        glm::vec2 p1(samplesXY[i + 1].first, samplesXY[i + 1].second);
        
        glm::vec2 dir = p1 - p0;
        float len = glm::length(dir);
        if (len < 0.0001f) continue;
        
        dir = dir / len;
        glm::vec2 perp(-dir.y, dir.x);
        glm::vec2 offset = perp * halfWidth;
        
        vertices_.push_back(glm::vec3(p0 - offset, 0.0f));
        vertices_.push_back(glm::vec3(p0 + offset, 0.0f));
        vertices_.push_back(glm::vec3(p1 - offset, 0.0f));
        
        vertices_.push_back(glm::vec3(p0 + offset, 0.0f));
        vertices_.push_back(glm::vec3(p1 - offset, 0.0f));
        vertices_.push_back(glm::vec3(p1 + offset, 0.0f));
    }
    
    if (!vertices_.empty()) {
        int newVertexCount = static_cast<int>(vertices_.size());
        if (!vbo_.getIsAllocated() || vboVertexCount_ != newVertexCount) {
            vbo_.setVertexData(vertices_.data(), newVertexCount, GL_DYNAMIC_DRAW);
            vboVertexCount_ = newVertexCount;
        } else {
            vbo_.updateVertexData(vertices_.data(), newVertexCount);
        }
    } else {
        vboVertexCount_ = 0;
    }
}

void Oscilloscope::updateRenderingSnapshot() {
    // NOTE: Must be called with moduleMutex_ lock already held (shared or exclusive)
    // Called from setParameter() or updateSnapshot() which already hold the lock
    auto snapshot = std::make_shared<const OscilloscopeRenderingSnapshot>(
        enabled_.load(),  // Atomic read (safe even with lock held)
        scale_,
        pointSize_,
        color_,
        backgroundColor_
    );
    {
        std::lock_guard<std::mutex> snapshotLock(renderingSnapshotMutex_);
        renderingSnapshot_ = snapshot;
    }
}

void Oscilloscope::renderLissajous() {
    if (!outputFbo_.isAllocated()) {
        ensureOutputFbo(fboWidth_, fboHeight_);
    }
    
    // Get rendering snapshot (lock-free read)
    auto snapshot = std::dynamic_pointer_cast<const OscilloscopeRenderingSnapshot>(
        getRenderingSnapshot()
    );
    if (!snapshot || !snapshot->enabled) {
        outputFbo_.begin();
        ofClear(snapshot ? snapshot->backgroundColor : backgroundColor_);
        outputFbo_.end();
        return;
    }
    
    if (!shaderLoaded_) {
        loadShaders();
    }
    
    if (!shaderLoaded_ || vertices_.empty() || !vbo_.getIsAllocated()) {
        outputFbo_.begin();
        ofClear(backgroundColor_);
        outputFbo_.end();
        return;
    }
    
    outputFbo_.begin();
    
    ofPushMatrix();
    ofPushView();
    ofViewport(0, 0, fboWidth_, fboHeight_);
    
    // Use cached orthographic matrix (static, never changes)
    if (!orthoMatrixInitialized_) {
        cachedOrthoMatrix_.makeOrthoMatrix(-1, 1, -1, 1, -1, 1);
        orthoMatrixInitialized_ = true;
    }
    ofGetCurrentRenderer()->matrixMode(OF_MATRIX_PROJECTION);
    ofGetCurrentRenderer()->loadMatrix(cachedOrthoMatrix_);
    ofGetCurrentRenderer()->matrixMode(OF_MATRIX_MODELVIEW);
    ofGetCurrentRenderer()->loadIdentityMatrix();
    
    // Clear with opaque background color
    ofClear(snapshot->backgroundColor);
    
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    int numVertices = static_cast<int>(vertices_.size());
    
    shader_.begin();
    
    GLint positionLoc = shader_.getAttributeLocation("position");
    if (positionLoc < 0) {
        shader_.end();
        ofDisableBlendMode();
        ofPopView();
        ofPopMatrix();
        outputFbo_.end();
        return;
    }
    
    // Use cached normalized color values
    updateNormalizedColor();
    shader_.setUniform1f("scale", snapshot->scale);  // Use snapshot->scale instead of scale_
    shader_.setUniform4f("drawColor", normalizedColorR_, normalizedColorG_, normalizedColorB_, 1.0f);
    
    vbo_.bind();
    glEnableVertexAttribArray(positionLoc);
    glVertexAttribPointer(positionLoc, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    
    glDrawArrays(GL_TRIANGLES, 0, numVertices);
    
    glDisableVertexAttribArray(positionLoc);
    vbo_.unbind();
    
    shader_.end();
    
    ofDisableBlendMode();
    
    ofPopView();
    ofPopMatrix();
    outputFbo_.end();
}

//--------------------------------------------------------------
// Module Factory Registration
//--------------------------------------------------------------
namespace {
    struct OscilloscopeRegistrar {
        OscilloscopeRegistrar() {
            ModuleFactory::registerModuleType("Oscilloscope", 
                []() -> std::shared_ptr<Module> {
                    return std::make_shared<Oscilloscope>();
                });
        }
    };
    static OscilloscopeRegistrar g_oscilloscopeRegistrar;
}

