#include "ParameterPath.h"
#include <regex>
#include <sstream>
#include <algorithm>
#include <cctype>

ParameterPath::ParameterPath() 
    : valid(false) {
}

ParameterPath::ParameterPath(const std::string& path) 
    : valid(false) {
    parse(path);
}

bool ParameterPath::parse(const std::string& path) {
    valid = false;
    instanceName.clear();
    parameterName.clear();
    index.reset();
    
    if (path.empty()) {
        return false;
    }
    
    // Pattern: <instanceName>.<parameterName>[<index>]
    // Examples:
    //   - "tracker1.position"
    //   - "tracker1.step[4].position"
    //   - "multisampler2.volume"
    
    // Find the last dot (separates instance from parameter)
    size_t lastDot = path.find_last_of('.');
    if (lastDot == std::string::npos || lastDot == 0 || lastDot == path.length() - 1) {
        // No dot or dot at start/end - invalid
        return false;
    }
    
    // Extract instance name (everything before last dot)
    instanceName = path.substr(0, lastDot);
    if (!isValidIdentifier(instanceName)) {
        return false;
    }
    
    // Extract parameter part (everything after last dot)
    std::string paramPart = path.substr(lastDot + 1);
    
    // Check for index: parameterName[index]
    size_t bracketOpen = paramPart.find('[');
    size_t bracketClose = paramPart.find(']');
    
    if (bracketOpen != std::string::npos && bracketClose != std::string::npos) {
        // Has index
        if (bracketOpen == 0 || bracketClose <= bracketOpen + 1 || bracketClose != paramPart.length() - 1) {
            // Invalid bracket positions
            return false;
        }
        
        // Extract parameter name (before bracket)
        parameterName = paramPart.substr(0, bracketOpen);
        if (!isValidIdentifier(parameterName)) {
            return false;
        }
        
        // Extract index (between brackets)
        std::string indexStr = paramPart.substr(bracketOpen + 1, bracketClose - bracketOpen - 1);
        try {
            int idx = std::stoi(indexStr);
            if (idx < 0) {
                // Negative indices not allowed
                return false;
            }
            index = idx;
        } catch (const std::exception&) {
            // Invalid integer
            return false;
        }
    } else if (bracketOpen == std::string::npos && bracketClose == std::string::npos) {
        // No index - simple parameter name
        parameterName = paramPart;
        if (!isValidIdentifier(parameterName)) {
            return false;
        }
    } else {
        // Mismatched brackets - invalid
        return false;
    }
    
    valid = true;
    return true;
}

std::string ParameterPath::toString() const {
    if (!valid) {
        return "";
    }
    
    std::stringstream ss;
    ss << instanceName << "." << parameterName;
    
    if (index.has_value()) {
        ss << "[" << index.value() << "]";
    }
    
    return ss.str();
}

std::string ParameterPath::build(const std::string& instanceName, 
                                 const std::string& parameterName,
                                 std::optional<int> index) {
    std::stringstream ss;
    ss << instanceName << "." << parameterName;
    
    if (index.has_value()) {
        ss << "[" << index.value() << "]";
    }
    
    return ss.str();
}

bool ParameterPath::isValidFormat(const std::string& path) {
    ParameterPath testPath;
    return testPath.parse(path);
}

bool ParameterPath::isValidIdentifier(const std::string& identifier) {
    if (identifier.empty()) {
        return false;
    }
    
    // Must start with letter or underscore
    if (!std::isalpha(identifier[0]) && identifier[0] != '_') {
        return false;
    }
    
    // Rest can be alphanumeric, underscore, or hyphen
    for (size_t i = 1; i < identifier.length(); ++i) {
        char c = identifier[i];
        if (!std::isalnum(c) && c != '_' && c != '-') {
            return false;
        }
    }
    
    return true;
}

bool ParameterPath::operator==(const ParameterPath& other) const {
    return valid == other.valid &&
           instanceName == other.instanceName &&
           parameterName == other.parameterName &&
           index == other.index;
}

bool ParameterPath::operator!=(const ParameterPath& other) const {
    return !(*this == other);
}

