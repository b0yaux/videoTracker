#include "ParameterCell.h"
#include "NumCell.h"  // For createCellForParameter
#include "BoolCell.h"  // For createCellForParameter
#include "MenuCell.h"  // For createCellForParameter
#include "modules/Module.h"
#include "core/ParameterRouter.h"
#include "core/ParameterPath.h"
#include "utils/ExpressionParser.h"
#include "ofMain.h"
#include <limits>
#include <cmath>

ParameterCell::ParameterCell(Module* module, 
                           const ParameterDescriptor& desc,
                           ParameterRouter* router)
    : module_(module)
    , desc_(desc)
    , router_(router)
    , isRemovable_(true)
{
}

float ParameterCell::getValue() const {
    if (customGetter_) {
        return customGetter_();
    }
    
    if (!module_) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    
    return module_->getParameter(desc_.name);
}

void ParameterCell::setValue(float value) {
    if (customSetter_) {
        customSetter_(value);
    } else if (module_) {
        module_->setParameter(desc_.name, value, true);
    }
}

std::unique_ptr<BaseCell> ParameterCell::createCell() {
    // Use static helper to create appropriate cell type
    auto cell = createCellForParameter(desc_, router_);
    
    if (!cell) {
        return nullptr;
    }
    
    // Create callbacks here (ParameterCell knows about Module, cells don't)
    // This keeps cells as pure UI components, decoupled from business logic
    
    std::function<float()> getter;
    if (customGetter_) {
        getter = customGetter_;
    } else {
        // Create default getter that calls Module
        Module* module = module_;
        std::string paramName = desc_.name;
        getter = [module, paramName]() -> float {
            if (!module) {
                return std::numeric_limits<float>::quiet_NaN();
            }
            return module->getParameter(paramName);
        };
    }
    
    std::function<void(float)> setter;
    if (customSetter_) {
        setter = customSetter_;
    } else {
        // Create default setter that calls Module
        Module* module = module_;
        std::string paramName = desc_.name;
        setter = [module, paramName](float value) {
            if (module) {
                module->setParameter(paramName, value, true);
            }
        };
    }
    
    std::function<void()> remover;
    if (customRemover_) {
        remover = customRemover_;
    } else {
        // Create default remover that resets to default value via Module
        Module* module = module_;
        std::string paramName = desc_.name;
        float defaultValue = desc_.defaultValue;
        remover = [module, paramName, defaultValue]() {
            if (module) {
                module->setParameter(paramName, defaultValue, true);
            }
        };
    }
    
    // Configure the cell with callbacks only (no Module*)
    // This keeps cells decoupled from business logic
    cell->configure(desc_, getter, setter, remover, customFormatter_, customParser_);
    cell->isRemovable = isRemovable_;
    
    return cell;
}

std::unique_ptr<BaseCell> ParameterCell::createCellForParameter(
    const ParameterDescriptor& desc,
    ParameterRouter* router
) {
    switch (desc.type) {
        case ParameterType::FLOAT:
        case ParameterType::INT: {
            auto cell = std::make_unique<NumCell>();
            cell->parameterName = desc.name;
            cell->isInteger = (desc.type == ParameterType::INT);
            cell->isRemovable = true;  // Default, can be overridden
            cell->setValueRange(desc.minValue, desc.maxValue, desc.defaultValue);
            cell->calculateStepIncrement();
            return cell;
        }
        case ParameterType::BOOL: {
            auto cell = std::make_unique<BoolCell>();
            cell->parameterName = desc.name;
            cell->isRemovable = true;  // Default, can be overridden
            return cell;
        }
        case ParameterType::ENUM: {
            auto cell = std::make_unique<MenuCell>();
            cell->parameterName = desc.name;
            cell->isRemovable = true;  // Default, can be overridden
            cell->setEnumOptions(desc.enumOptions);
            cell->setCurrentIndex(desc.defaultEnumIndex);
            return cell;
        }
        default:
            return nullptr;
    }
}

void ParameterCell::setCustomGetter(std::function<float()> getter) {
    customGetter_ = getter;
}

void ParameterCell::setCustomSetter(std::function<void(float)> setter) {
    customSetter_ = setter;
}

void ParameterCell::setCustomFormatter(std::function<std::string(float)> formatter) {
    customFormatter_ = formatter;
}

void ParameterCell::setCustomParser(std::function<float(const std::string&)> parser) {
    customParser_ = parser;
}

void ParameterCell::setCustomRemover(std::function<void()> remover) {
    customRemover_ = remover;
}

void ParameterCell::setRemovable(bool removable) {
    isRemovable_ = removable;
}

bool ParameterCell::hasConnection() const {
    if (!router_) {
        return false;
    }
    
    std::string path = getParameterPath();
    if (path.empty()) {
        return false;
    }
    
    auto connectionsFrom = router_->getConnectionsFrom(path);
    auto connectionsTo = router_->getConnectionsTo(path);
    
    return !connectionsFrom.empty() || !connectionsTo.empty();
}

std::vector<std::pair<std::string, std::string>> ParameterCell::getConnections() const {
    std::vector<std::pair<std::string, std::string>> allConnections;
    
    if (!router_) {
        return allConnections;
    }
    
    std::string path = getParameterPath();
    if (path.empty()) {
        return allConnections;
    }
    
    // Get connections from this parameter (as source)
    auto connectionsFrom = router_->getConnectionsFrom(path);
    allConnections.insert(allConnections.end(), connectionsFrom.begin(), connectionsFrom.end());
    
    // Get connections to this parameter (as target)
    auto connectionsTo = router_->getConnectionsTo(path);
    allConnections.insert(allConnections.end(), connectionsTo.begin(), connectionsTo.end());
    
    return allConnections;
}

std::string ParameterCell::getParameterPath() const {
    if (!module_) {
        return "";
    }
    
    return module_->getName() + "." + desc_.name;
}

// Type-specific configuration methods removed - now handled by BaseCell::configure()
// This keeps ParameterCell decoupled from concrete cell types
