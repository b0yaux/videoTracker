#pragma once
#include "utils/Clock.h"
#include "core/EngineState.h"

// Forward declaration
namespace vt {
    class Engine;
}

class ClockGUI {
public:
    ClockGUI();
    ~ClockGUI();
    void draw(Clock& clock);
    void setEngine(vt::Engine* engine);
    
private:
    float bpmSlider = 120.0f;
    bool isDragging = false;
    vt::Engine* engine_ = nullptr;
    
    // State subscription
    size_t observerId_ = 0;  // Subscription ID for state change notifications
    vt::EngineState cachedState_;  // Cached state for thread-safe access
    bool stateNeedsUpdate_ = false;  // Dirty flag for UI updates
    
    // Cleanup method
    void unsubscribe();
};
