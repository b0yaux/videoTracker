#pragma once

#include "BaseCell.h"
#include <vector>
#include <string>
#include <functional>

// MenuCell - Dropdown/button cell for enum parameters
// Inherits from BaseCell for unified cell system
// Replaces drawSpecialColumn logic for enum parameters
class MenuCell : public BaseCell {
public:
    MenuCell();
    
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
    
    // Configuration
    void setEnumOptions(const std::vector<std::string>& options);
    void setCurrentIndex(int index);
    int getCurrentIndex() const { return currentIndex_; }
    
    // Enum-specific callbacks
    std::function<int()> getIndex;  // Get current enum index
    std::function<void(const std::string&, int)> onValueAppliedEnum;  // Called when value is applied (index version)
    
private:
    std::vector<std::string> enumOptions_;
    int currentIndex_ = 0;
    bool focused_ = false;
    
    // Helper to get current option text
    std::string getCurrentOptionText() const;
};

