#pragma once

#include "BaseCell.h"
#include <functional>

// BoolCell - Toggle button cell for boolean parameters
// Inherits from BaseCell for unified cell system
class BoolCell : public BaseCell {
public:
    BoolCell();
    
    // BaseCell interface implementation
    CellInteraction draw(int uniqueId, bool isFocused, bool shouldFocusFirst = false) override;
    void enterEditMode() override;
    void exitEditMode() override;
    bool isEditingMode() const override { return editing_; }
    bool isFocused() const override { return focused_; }
    void configure(const ParameterDescriptor& desc,
                   std::function<float()> getter,
                   std::function<void(float)> setter,
                   std::function<void()> remover = nullptr,
                   std::function<std::string(float)> formatter = nullptr,
                   std::function<float(const std::string&)> parser = nullptr) override;
    
    // Boolean-specific callbacks
    std::function<bool()> getCurrentValue;  // Get current boolean value
    std::function<void(const std::string&, bool)> onValueAppliedBool;  // Called when value is applied (bool version)
    
private:
    bool focused_ = false;
};

