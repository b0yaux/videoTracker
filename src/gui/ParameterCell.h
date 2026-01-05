#pragma once

#include "BaseCell.h"
#include "modules/Module.h"
#include "core/ParameterRouter.h"
#include "core/ParameterPath.h"
#include "ofMain.h"  // for ofToString
#include <functional>
#include <limits>
#include <vector>
#include <string>
#include <memory>

// Forward declarations for concrete cell types (only needed in .cpp for createCellForParameter)
class NumCell;
class BoolCell;
class MenuCell;

/**
 * ParameterCell - Module adapter with routing awareness
 * 
 * This class bridges the backend (Module) to the GUI (CellWidget) with
 * direct Module binding and routing awareness for future modulation features.
 * 
 * Key Features:
 * - Direct Module binding (no factory pattern)
 * - Routing awareness (ParameterRouter integration)
 * - Creates CellWidget with all editing features
 * - Supports custom getters/setters for special cases
 * 
 * Usage Examples:
 * 
 * // Simple: Direct Module binding
 * ParameterCell cell(&myModule, paramDesc, &parameterRouter);
 * CellWidget widget = cell.createCellWidget();
 * widget.draw(uniqueId, isFocused, ...);
 * 
 * // With custom getter/setter (e.g., MultiSampler needs activePlayer)
 * ParameterCell cell(&myModule, paramDesc, &parameterRouter);
 * CellWidget widget = cell.createCellWidget();
 * widget.getCurrentValue = [&]() { return myModule->getActivePlayer()->getValue(); };
 * widget.onValueApplied = [&](const std::string&, float v) { myModule->setValue(v); };
 * 
 * // Check routing connections (for future visualization)
 * if (cell.hasConnection()) {
 *     auto connections = cell.getConnections();
 *     // Draw connection indicators...
 * }
 */
class ParameterCell {
public:
    /**
     * Constructor: Direct Module binding
     * 
     * @param module Module instance (must outlive the ParameterCell)
     * @param desc Parameter descriptor (defines name, type, range, default)
     * @param router Optional ParameterRouter for routing awareness (can be nullptr)
     */
    ParameterCell(Module* module, 
                 const ParameterDescriptor& desc,
                 ParameterRouter* router = nullptr);
    
    /**
     * Get current parameter value
     * Uses custom getter if set, otherwise uses Module::getParameter
     * @return Parameter value, or NaN if not available
     */
    float getValue() const;
    
    /**
     * Set parameter value
     * Uses custom setter if set, otherwise uses Module::setParameter
     * @param value Value to set
     */
    void setValue(float value);
    
    /**
     * Create BaseCell with all editing features
     * 
     * Creates a fully configured cell (NumCell, BoolCell, or MenuCell) based on parameter type:
     * - NumCell: Drag editing, keyboard input, expression parsing, multi-precision
     * - BoolCell: Toggle button with ON/OFF labels
     * - MenuCell: Enum selection with cycling
     * 
     * @return Configured BaseCell ready to use (unique_ptr for polymorphism)
     */
    std::unique_ptr<BaseCell> createCell();
    
    /**
     * Static helper to create appropriate cell type based on ParameterDescriptor
     * 
     * This is a factory method that creates the correct cell type:
     * - FLOAT/INT -> NumCell
     * - BOOL -> BoolCell
     * - ENUM -> MenuCell
     * 
     * @param desc Parameter descriptor
     * @param router Optional ParameterRouter for routing awareness
     * @return Configured BaseCell ready to use
     */
    static std::unique_ptr<BaseCell> createCellForParameter(
        const ParameterDescriptor& desc,
        ParameterRouter* router = nullptr
    );
    
    /**
     * Set custom getter callback
     * Overrides Module::getParameter for special cases (e.g., MultiSampler activePlayer)
     * @param getter Callback that returns current value (return NaN if not available)
     */
    void setCustomGetter(std::function<float()> getter);
    
    /**
     * Set custom setter callback
     * Overrides Module::setParameter for special cases
     * @param setter Callback that sets the value
     */
    void setCustomSetter(std::function<void(float)> setter);
    
    /**
     * Set custom formatter callback
     * Overrides standard formatting (e.g., logarithmic mapping)
     * @param formatter Callback that formats value to string
     */
    void setCustomFormatter(std::function<std::string(float)> formatter);
    
    /**
     * Set custom parser callback
     * Overrides standard parsing (uses ExpressionParser by default)
     * @param parser Callback that parses string to float
     */
    void setCustomParser(std::function<float(const std::string&)> parser);
    
    /**
     * Set custom remover callback
     * Overrides default reset behavior (resets to defaultValue by default)
     * @param remover Callback that removes/resets the parameter
     */
    void setCustomRemover(std::function<void()> remover);
    
    /**
     * Set whether parameter is removable
     * @param removable true if parameter can be removed/deleted (default: true)
     */
    void setRemovable(bool removable);
    
    // Future: Routing awareness for modulation
    
    /**
     * Check if this parameter has any routing connections
     * @return true if parameter is connected to/from other parameters
     */
    bool hasConnection() const;
    
    /**
     * Get all routing connections for this parameter
     * @return Vector of connection paths (source, target pairs)
     */
    std::vector<std::pair<std::string, std::string>> getConnections() const;
    
    /**
     * Get parameter path for routing (e.g., "tracker1.position")
     * @return Parameter path string, or empty string if module is null
     */
    std::string getParameterPath() const;
    
    // Metadata access
    
    /**
     * Get parameter descriptor
     */
    const ParameterDescriptor& getDescriptor() const { return desc_; }
    
    /**
     * Get module instance
     */
    Module* getModule() const { return module_; }
    
    /**
     * Get parameter router (can be nullptr)
     */
    ParameterRouter* getRouter() const { return router_; }

private:
    Module* module_;
    ParameterDescriptor desc_;
    ParameterRouter* router_;
    
    // Custom callbacks (override default Module behavior)
    std::function<float()> customGetter_;
    std::function<void(float)> customSetter_;
    std::function<std::string(float)> customFormatter_;
    std::function<float(const std::string&)> customParser_;
    std::function<void()> customRemover_;
    bool isRemovable_ = true;
    
    // Type-specific configuration methods removed - now handled by BaseCell::configure()
    // This keeps ParameterCell decoupled from concrete cell types
};
