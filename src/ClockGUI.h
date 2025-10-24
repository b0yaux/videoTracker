#pragma once
#include "Clock.h"

class ClockGUI {
public:
    ClockGUI();
    void draw(Clock& clock);
    
private:
    float bpmSlider = 120.0f;
    bool isDragging = false;
};
