#pragma once

#include <string>
#include <optional>

/**
 * ParameterPath - Hierarchical parameter addressing system
 * 
 * Supports TouchDesigner-style paths: "instanceName.parameterName[index]"
 * 
 * Examples:
 *   - "tracker1.position"           - Simple parameter
 *   - "tracker1.step[4].position"   - Indexed parameter (step 4)
 *   - "multisampler2.volume"        - Another instance
 * 
 * Path format: <instanceName>.<parameterName>[<index>]
 * - instanceName: Human-readable name or UUID of module instance
 * - parameterName: Parameter name (e.g., "position", "speed", "volume")
 * - index: Optional integer index in brackets (e.g., "[4]")
 */
class ParameterPath {
public:
    ParameterPath();
    ParameterPath(const std::string& path);
    
    /**
     * Parse a path string into components
     * @param path Path string (e.g., "tracker1.step[4].position")
     * @return true if parsing succeeded, false otherwise
     */
    bool parse(const std::string& path);
    
    /**
     * Build a path string from components
     * @return Path string
     */
    std::string toString() const;
    
    /**
     * Check if path is valid
     */
    bool isValid() const { return valid; }
    
    /**
     * Get instance name (e.g., "tracker1")
     */
    const std::string& getInstanceName() const { return instanceName; }
    
    /**
     * Get parameter name (e.g., "position")
     */
    const std::string& getParameterName() const { return parameterName; }
    
    /**
     * Check if path has an index
     */
    bool hasIndex() const { return index.has_value(); }
    
    /**
     * Get index value (only valid if hasIndex() returns true)
     */
    int getIndex() const { return index.value_or(-1); }
    
    /**
     * Set instance name
     */
    void setInstanceName(const std::string& name) { instanceName = name; }
    
    /**
     * Set parameter name
     */
    void setParameterName(const std::string& name) { parameterName = name; }
    
    /**
     * Set index (optional)
     */
    void setIndex(int idx) { index = idx; }
    void clearIndex() { index.reset(); }
    
    /**
     * Validate path format
     */
    static bool isValidFormat(const std::string& path);
    
    /**
     * Build a path from components
     */
    static std::string build(const std::string& instanceName, 
                            const std::string& parameterName,
                            std::optional<int> index = std::nullopt);
    
    // Comparison operators
    bool operator==(const ParameterPath& other) const;
    bool operator!=(const ParameterPath& other) const;
    
private:
    std::string instanceName;
    std::string parameterName;
    std::optional<int> index;
    bool valid;
    
    /**
     * Validate component names (alphanumeric, underscore, hyphen)
     */
    static bool isValidIdentifier(const std::string& identifier);
};

