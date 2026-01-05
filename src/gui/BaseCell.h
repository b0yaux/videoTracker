#pragma once

#include <string>
#include <functional>
#include <vector>
#include "core/ParameterDescriptor.h"  // Full definition needed for configure() method

// Forward declarations
struct ImVec2;

// Unified interaction result (works for all cell types)
struct CellInteraction {
    bool clicked = false;
    bool focusChanged = false;
    bool valueChanged = false;
    bool editModeChanged = false;
    bool shouldExitEarly = false;
    
    CellInteraction() = default;
};

// Base class for all cell types (NumCell, BoolCell, MenuCell, TextCell, etc.)
// Provides unified interface for parameter editing widgets
class BaseCell {
public:
    virtual ~BaseCell() = default;
    
    // Core drawing interface (unified across all cell types)
    virtual CellInteraction draw(int uniqueId,
                                 bool isFocused,
                                 bool shouldFocusFirst = false) = 0;
    
    // Edit mode management (common to all cells)
    virtual void enterEditMode() = 0;
    virtual void exitEditMode() = 0;
    virtual bool isEditingMode() const = 0;
    
    // Focus management
    virtual bool isFocused() const = 0;
    
    // Drag state (for cells that support dragging, like NumCell)
    // Returns false by default for cells that don't support dragging
    virtual bool isDragging() const { return false; }
    
    /**
     * Configure this cell with callbacks
     * Each cell type implements this to set up its type-specific callbacks
     * This allows ParameterCell to configure cells without knowing their concrete types
     * 
     * Cells are pure UI components - they only know about callbacks, not business logic (Module)
     * ParameterCell creates the callbacks that bridge to Module, keeping cells decoupled
     * 
     * @param desc Parameter descriptor (for metadata like name, default value, etc.)
     * @param getter Required getter callback (returns current value)
     * @param setter Required setter callback (sets new value)
     * @param remover Optional remover callback (resets/removes parameter)
     * @param formatter Optional formatter callback (for numeric cells)
     * @param parser Optional parser callback (for numeric cells)
     */
    virtual void configure(
        const ParameterDescriptor& desc,
        std::function<float()> getter,
        std::function<void(float)> setter,
        std::function<void()> remover = nullptr,
        std::function<std::string(float)> formatter = nullptr,
        std::function<float(const std::string&)> parser = nullptr
    ) = 0;
    
    // Common callbacks (standardized interface)
    // onValueApplied: paramName, value (as string for type-agnostic handling)
    std::function<void(const std::string&, const std::string&)> onValueApplied;
    std::function<void(const std::string&)> onValueRemoved;
    std::function<void(bool)> onEditModeChanged;
    
    // Configuration
    std::string parameterName;
    bool isRemovable = true;
    
protected:
    // Common state
    bool editing_ = false;
    bool focused_ = false;
};

