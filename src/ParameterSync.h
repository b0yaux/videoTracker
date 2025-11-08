#pragma once

#include "Module.h"
#include <functional>
#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <cstddef>

// Forward declarations
class TrackerSequencer;
class MediaPool;

/**
 * ParameterSync - Modular parameter synchronization connector
 * 
 * Connects modules via named parameter bindings with bidirectional sync.
 * Prevents feedback loops and respects conditional sync rules.
 * 
 * Usage:
 *   ParameterSync sync;
 *   sync.connect(&trackerSequencer, "currentStepPosition", &mediaPool, "position",
 *                [this]() { return !clock.isPlaying(); });
 */
class ParameterSync {
public:
    ParameterSync();
    ~ParameterSync();
    
    /**
     * Connect two modules with bidirectional parameter binding
     * @param source Source module
     * @param sourceParam Parameter name in source module (e.g., "currentStepPosition")
     * @param target Target module
     * @param targetParam Parameter name in target module (e.g., "position")
     * @param condition Optional function that returns true when sync should be active
     */
    void connect(
        void* source,
        const std::string& sourceParam,
        void* target,
        const std::string& targetParam,
        std::function<bool()> condition = nullptr
    );
    
    /**
     * Disconnect a binding
     * @param source Source module
     * @param sourceParam Parameter name to disconnect
     */
    void disconnect(void* source, const std::string& sourceParam);
    
    /**
     * Update method - call from ofApp::update() to process sync
     */
    void update();
    
    /**
     * Register a parameter change from a module
     * Modules call this when their parameters change
     * @param module Module that changed
     * @param paramName Parameter name that changed
     * @param value New parameter value
     */
    void notifyParameterChange(void* module, const std::string& paramName, float value);
    
    /**
     * Get parameter value from a module (for sync system)
     */
    float getParameterValue(void* module, const std::string& paramName) const;
    
    /**
     * Set parameter value in a module (for sync system)
     */
    void setParameterValue(void* module, const std::string& paramName, float value);

private:
    struct Binding {
        void* source;
        std::string sourceParam;
        void* target;
        std::string targetParam;
        std::function<bool()> condition;
        std::atomic<bool> syncing; // Guard to prevent feedback loops
        
        Binding() : source(nullptr), target(nullptr), syncing(false) {}
        
        // Delete copy constructor and assignment (atomic is not copyable)
        Binding(const Binding&) = delete;
        Binding& operator=(const Binding&) = delete;
        
        // Allow move constructor and assignment
        Binding(Binding&& other) noexcept
            : source(other.source)
            , sourceParam(std::move(other.sourceParam))
            , target(other.target)
            , targetParam(std::move(other.targetParam))
            , condition(std::move(other.condition))
            , syncing(other.syncing.load(std::memory_order_acquire))
        {}
        
        Binding& operator=(Binding&& other) noexcept {
            if (this != &other) {
                source = other.source;
                sourceParam = std::move(other.sourceParam);
                target = other.target;
                targetParam = std::move(other.targetParam);
                condition = std::move(other.condition);
                syncing.store(other.syncing.load(std::memory_order_acquire), std::memory_order_release);
            }
            return *this;
        }
    };
    
    std::vector<Binding> bindings;
    
    // Helper to find bindings
    std::vector<size_t> findBindingsForSource(void* source, const std::string& paramName) const;
    std::vector<size_t> findBindingsForTarget(void* target, const std::string& paramName) const;
};

