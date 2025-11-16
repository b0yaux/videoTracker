#include "ParameterSync.h"
#include "TrackerSequencer.h"
#include "MediaPool.h"
#include "MediaPlayer.h"
#include "Module.h"
#include "ofLog.h"
#include <cmath>

ParameterSync::ParameterSync() {
}

ParameterSync::~ParameterSync() {
    bindings.clear();
}

void ParameterSync::connect(
    Module* source,
    const std::string& sourceParam,
    Module* target,
    const std::string& targetParam,
    std::function<bool()> condition
) {
    if (!source || !target) {
        ofLogError("ParameterSync") << "Cannot connect: source or target is null";
        return;
    }
    
    // Use emplace_back to construct Binding in place (can't copy due to atomic member)
    bindings.emplace_back();
    Binding& binding = bindings.back();
    binding.source = source;
    binding.sourceParam = sourceParam;
    binding.target = target;
    binding.targetParam = targetParam;
    binding.condition = condition ? condition : []() { return true; };
    binding.syncing.store(false, std::memory_order_relaxed);
    
    ofLogNotice("ParameterSync") << "Connected: " << sourceParam << " -> " << targetParam;
}

void ParameterSync::disconnect(Module* source, const std::string& sourceParam) {
    auto it = bindings.begin();
    while (it != bindings.end()) {
        if (it->source == source && it->sourceParam == sourceParam) {
            it = bindings.erase(it);
            ofLogNotice("ParameterSync") << "Disconnected: " << sourceParam;
        } else {
            ++it;
        }
    }
}

void ParameterSync::update() {
    // Periodic update can be used for polling-based sync if needed
    // For now, we rely on parameter change notifications
}

void ParameterSync::notifyParameterChange(Module* module, const std::string& paramName, float value) {
    // Find all bindings where this module is the source
    auto bindingIndices = findBindingsForSource(module, paramName);
    
    for (size_t idx : bindingIndices) {
        if (idx >= bindings.size()) continue;
        
        Binding& binding = bindings[idx];
        
        // Check if sync should be active
        if (!binding.condition()) {
            continue;
        }
        
        // Prevent feedback loop
        if (binding.syncing.load(std::memory_order_acquire)) {
            continue;
        }
        
        // Set syncing flag
        binding.syncing.store(true, std::memory_order_release);
        
        // Get current target value to check if update is needed
        float currentTargetValue = getParameterValue(binding.target, binding.targetParam);
        
        // Only update if value actually changed (avoid unnecessary updates)
        // Also, for position sync, don't sync 0 if current value is non-zero (preserve position)
        // This prevents resetting position when sync system returns 0 incorrectly
        bool shouldUpdate = std::abs(currentTargetValue - value) > 0.0001f;
        if (binding.targetParam == "position" && value == 0.0f && currentTargetValue > 0.001f) {
            // Don't sync 0 if we have a valid position - this prevents unwanted resets
            shouldUpdate = false;
        }
        
        if (shouldUpdate) {
            setParameterValue(binding.target, binding.targetParam, value);
        }
        
        // Clear syncing flag
        binding.syncing.store(false, std::memory_order_release);
    }
}

float ParameterSync::getParameterValue(Module* module, const std::string& paramName) const {
    if (!module) {
        return 0.0f;
    }
    
    // Try to use Module interface first (for modules that properly implement it)
    // Check if parameter exists in module's parameter list
    auto params = module->getParameters();
    for (const auto& param : params) {
        if (param.name == paramName) {
            // Use Module's setParameter/getParameter if available
            // For now, we still need special cases for TrackerSequencer and MediaPool
            // until they fully implement the Module interface
            break;
        }
    }
    
    // Special case: TrackerSequencer (check by parameter name)
    // TODO: Remove this once TrackerSequencer fully implements Module interface
    if (paramName == "currentStepPosition") {
        TrackerSequencer* ts = dynamic_cast<TrackerSequencer*>(module);
        if (ts) {
            return ts->getCurrentStepPosition();
        }
    }
    
    // Special case: MediaPool (check by parameter name)
    // For position sync, we want to get startPosition (not playhead position)
    if (paramName == "position") {
        MediaPool* mp = dynamic_cast<MediaPool*>(module);
        if (mp) {
            auto* player = mp->getActivePlayer();
            if (player) {
                // Return startPosition for sync (this is what we sync with tracker)
                return player->startPosition.get();
            }
        }
    }
    
    return 0.0f;
}

void ParameterSync::setParameterValue(Module* module, const std::string& paramName, float value) {
    if (!module) {
        return;
    }
    
    // Special case: TrackerSequencer (check by parameter name)
    // TODO: Remove this once TrackerSequencer fully implements Module interface
    if (paramName == "currentStepPosition") {
        TrackerSequencer* ts = dynamic_cast<TrackerSequencer*>(module);
        if (ts) {
            ts->setCurrentStepPosition(value);
            return;
        }
    }
    
    // For MediaPool and other modules, use standard Module interface
    // MediaPool already implements Module::setParameter()
    module->setParameter(paramName, value, false);
}

std::vector<size_t> ParameterSync::findBindingsForSource(Module* source, const std::string& paramName) const {
    std::vector<size_t> indices;
    for (size_t i = 0; i < bindings.size(); ++i) {
        if (bindings[i].source == source && bindings[i].sourceParam == paramName) {
            indices.push_back(i);
        }
    }
    return indices;
}

std::vector<size_t> ParameterSync::findBindingsForTarget(Module* target, const std::string& paramName) const {
    std::vector<size_t> indices;
    for (size_t i = 0; i < bindings.size(); ++i) {
        if (bindings[i].target == target && bindings[i].targetParam == paramName) {
            indices.push_back(i);
        }
    }
    return indices;
}

