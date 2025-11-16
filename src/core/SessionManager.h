#pragma once

#include <string>
#include "ofJson.h"

// Forward declarations
class Clock;
class ModuleRegistry;
class ModuleFactory;
class ParameterRouter;
class ViewManager;
class GUIManager;

/**
 * SessionManager - Central coordinator for saving/loading complete application sessions
 * 
 * Features:
 * - Saves/loads all modules, routing, clock, and GUI state
 * - Supports modular architecture (no hardcoded module types)
 * - Version-aware for future migration support
 * - Handles partial loading gracefully
 */
class SessionManager {
public:
    // Default constructor (for member initialization)
    SessionManager() : clock(nullptr), registry(nullptr), factory(nullptr), router(nullptr) {}
    
    // Constructor with all dependencies
    SessionManager(
        Clock* clock,
        ModuleRegistry* registry,
        ModuleFactory* factory,
        ParameterRouter* router
    );
    
    /**
     * Save complete session to file
     * @param filename Path to save session file
     * @return true if successful, false otherwise
     */
    bool saveSession(const std::string& filename);
    
    /**
     * Load complete session from file
     * @param filename Path to session file
     * @return true if successful, false otherwise
     */
    bool loadSession(const std::string& filename);
    
    /**
     * Get current session as JSON (for inspection/debugging)
     * @return JSON object representing current session
     */
    ofJson serializeAll() const;
    
    /**
     * Deserialize session from JSON (for testing/custom loading)
     * @param json JSON object representing session
     * @return true if successful, false otherwise
     */
    bool deserializeAll(const ofJson& json);
    
    /**
     * Get session version
     */
    static constexpr const char* SESSION_VERSION = "1.0";

private:
    Clock* clock;
    ModuleRegistry* registry;
    ModuleFactory* factory;
    ParameterRouter* router;
    
    /**
     * Migrate legacy format to new format
     */
    bool migrateLegacyFormat(const ofJson& json);
};

