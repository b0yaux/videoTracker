#include "ParameterCell.h"
#include "CellWidget.h"
#include "Module.h"
#include "core/ParameterRouter.h"
#include "core/ParameterPath.h"
#include "ExpressionParser.h"
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

CellWidget ParameterCell::createCellWidget() {
    CellWidget cell;
    
    // Basic configuration
    cell.parameterName = desc_.name;
    cell.isInteger = (desc_.type == ParameterType::INT);
    cell.isRemovable = isRemovable_;
    cell.setValueRange(desc_.minValue, desc_.maxValue, desc_.defaultValue);
    cell.calculateStepIncrement();
    
    // Set up getter callback
    if (customGetter_) {
        cell.getCurrentValue = customGetter_;
    } else {
        // Use direct Module binding
        Module* module = module_;  // Capture for lambda
        std::string paramName = desc_.name;  // Capture by value
        cell.getCurrentValue = [module, paramName]() -> float {
            if (!module) {
                return std::numeric_limits<float>::quiet_NaN();
            }
            return module->getParameter(paramName);
        };
    }
    
    // Set up setter callback
    if (customSetter_) {
        auto setter = customSetter_;  // Capture for lambda
        cell.onValueApplied = [setter](const std::string&, float value) {
            setter(value);
        };
    } else {
        // Use direct Module binding
        Module* module = module_;  // Capture for lambda
        std::string paramName = desc_.name;  // Capture by value
        cell.onValueApplied = [module, paramName](const std::string&, float value) {
            if (module) {
                module->setParameter(paramName, value, true);
            }
        };
    }
    
    // Set up remover callback (reset to default)
    if (customRemover_) {
        auto remover = customRemover_;  // Capture for lambda
        cell.onValueRemoved = [remover](const std::string&) {
            remover();
        };
    } else {
        // Default: Reset to defaultValue via Module::setParameter
        Module* module = module_;  // Capture for lambda
        std::string paramName = desc_.name;  // Capture by value
        float defaultValue = desc_.defaultValue;  // Capture by value
        cell.onValueRemoved = [module, paramName, defaultValue](const std::string&) {
            if (module) {
                module->setParameter(paramName, defaultValue, true);
            }
        };
    }
    
    // Set up formatting
    if (customFormatter_) {
        cell.formatValue = customFormatter_;
    } else {
        setupStandardFormatting(cell);
    }
    
    // Set up parser (optional - uses default if not provided)
    if (customParser_) {
        cell.parseValue = customParser_;
    }
    // Otherwise, CellWidget uses default ExpressionParser
    
    return cell;
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

void ParameterCell::setupStandardFormatting(CellWidget& cell) const {
    if (desc_.type == ParameterType::INT) {
        // Integer parameters: no decimal places
        cell.formatValue = [](float value) -> std::string {
            return ofToString((int)std::round(value));
        };
    } else {
        // Float parameters: 3 decimal places (0.001 precision) - unified for all float params
        cell.formatValue = [](float value) -> std::string {
            return ofToString(value, 3);
        };
    }
}
