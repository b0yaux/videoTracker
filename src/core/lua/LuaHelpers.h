#pragma once

#include "core/Engine.h"
#include <string>
#include <map>

namespace vt {
namespace lua {

/**
 * LuaHelpers - High-level declarative functions for live-coding
 * 
 * Provides clean, declarative syntax for common operations:
 * - sampler(name, config) - Create and configure MultiSampler (IDEMPOTENT)
 * - sequencer(name, config) - Create and configure TrackerSequencer (IDEMPOTENT)
 * - connect(source, target, type) - Create connections (IDEMPOTENT)
 * 
 * These functions wrap the low-level command execution with
 * declarative, functional-style APIs inspired by Tidal/Strudel/Hydra.
 * 
 * All functions are IDEMPOTENT - they can be called multiple times safely.
 * This enables live-coding where scripts can be executed repeatedly without errors.
 * - Module creation functions update parameters if module exists, create if not
 * - Connection functions skip if connection exists, create if not
 * - Pattern functions update if pattern exists, create if not
 */
class LuaHelpers {
public:
    LuaHelpers(Engine* engine);
    
    /**
     * Create a MultiSampler module with declarative configuration (IDEMPOTENT)
     * If module exists, updates parameters. If not, creates module.
     * @param name Module instance name
     * @param config Lua table with parameters (volume, speed, etc.)
     * @return Module instance name if successful, empty string on failure
     */
    std::string createSampler(const std::string& name, const std::map<std::string, std::string>& config = {});
    
    /**
     * Create a TrackerSequencer module with declarative configuration (IDEMPOTENT)
     * If module exists, updates parameters. If not, creates module.
     * @param name Module instance name
     * @param config Lua table with parameters
     * @return Module instance name if successful, empty string on failure
     */
    std::string createSequencer(const std::string& name, const std::map<std::string, std::string>& config = {});
    
    /**
     * Create or configure a system module (AudioOutput, VideoOutput, etc.)
     * @param moduleType Module type ("AudioOutput", "VideoOutput", "Oscilloscope", "Spectrogram")
     * @param name Module instance name (must be system module name like "masterAudioOut")
     * @param config Lua table with parameters
     * @return Module instance name if successful, empty string on failure
     */
    std::string createSystemModule(const std::string& moduleType, const std::string& name, const std::map<std::string, std::string>& config = {});
    
    /**
     * Create a connection between modules (IDEMPOTENT)
     * If connection exists, skips (no-op). If not, creates connection.
     * @param source Source module name
     * @param target Target module name
     * @param type Connection type ("audio", "video", "event", "parameter")
     * @return true if successful, false otherwise
     */
    bool connect(const std::string& source, const std::string& target, const std::string& type = "audio");
    
    /**
     * Set a module parameter directly (bypasses executeCommand)
     * @param moduleName Module instance name
     * @param paramName Parameter name
     * @param value Parameter value (as string, will be parsed)
     * @return true if successful, false otherwise
     */
    bool setParameter(const std::string& moduleName, const std::string& paramName, const std::string& value);
    
    /**
     * Get a module parameter value
     * @param moduleName Module instance name
     * @param paramName Parameter name
     * @return Parameter value as string, empty if not found
     */
    std::string getParameter(const std::string& moduleName, const std::string& paramName);
    
private:
    Engine* engine_;
    
    // Helper to parse config values
    float parseFloat(const std::string& value, float defaultValue = 0.0f);
    int parseInt(const std::string& value, int defaultValue = 0);
    bool parseBool(const std::string& value, bool defaultValue = false);
};

} // namespace lua
} // namespace vt

