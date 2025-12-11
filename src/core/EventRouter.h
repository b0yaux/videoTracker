#pragma once

#include "modules/Module.h"
#include "ModuleRegistry.h"
#include <string>
#include <set>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include "ofJson.h"

// Wrapper class to enable member function pointer subscription
// This allows us to use ofAddListener which requires member function pointers
class ModuleEventWrapper {
public:
    ModuleEventWrapper(std::weak_ptr<Module> target, const std::string& targetName)
        : weakTarget_(target), targetName_(targetName) {}
    
    void handleTrigger(TriggerEvent& evt) {
        if (auto targetPtr = weakTarget_.lock()) {
            // Explicitly call virtual function - ensures proper virtual dispatch
            targetPtr->onTrigger(evt);
        } else {
            // Module was destroyed - log warning but don't spam
            static int warningCount = 0;
            if (++warningCount % 100 == 0) { // Log every 100th occurrence
                ofLogWarning("EventRouter") << "Target module '" << targetName_ 
                                             << "' was destroyed before event handler could be called";
            }
        }
    }
    
private:
    std::weak_ptr<Module> weakTarget_;
    std::string targetName_;
};

/**
 * EventRouter - Handles event subscriptions between modules
 * 
 * Extracted from ConnectionManager to provide focused event routing functionality.
 * Manages ofEvent subscriptions for trigger events and other module events.
 * 
 * Design Philosophy:
 * - Public APIs accept module names (user-friendly, backward compatible)
 * - Internal storage uses UUIDs (stable across renames, no renameModule needed)
 * - Serialization saves both UUIDs (primary) and names (readability)
 * - This separation ensures subscriptions persist when modules are renamed
 * 
 * Usage:
 *   EventRouter router(&registry);
 *   router.subscribe("tracker1", "triggerEvent", "pool1", "onTrigger");
 *   router.unsubscribe("tracker1", "triggerEvent", "pool1", "onTrigger");
 */
class EventRouter {
public:
    /**
     * Event subscription information
     * Stores UUIDs internally to avoid needing renameModule when modules are renamed
     */
    struct Subscription {
        std::string sourceUUID;  // UUID of source module
        std::string eventName;
        std::string targetUUID;  // UUID of target module
        std::string handlerName;
        
        Subscription() {}
        Subscription(const std::string& sourceUUID, const std::string& event,
                    const std::string& targetUUID, const std::string& handler)
            : sourceUUID(sourceUUID), eventName(event),
              targetUUID(targetUUID), handlerName(handler) {}
        
        bool operator<(const Subscription& other) const {
            if (sourceUUID != other.sourceUUID) return sourceUUID < other.sourceUUID;
            if (eventName != other.eventName) return eventName < other.eventName;
            if (targetUUID != other.targetUUID) return targetUUID < other.targetUUID;
            return handlerName < other.handlerName;
        }
    };
    
    EventRouter(ModuleRegistry* registry = nullptr);
    ~EventRouter();
    
    /**
     * Set module registry (can be called after construction)
     */
    void setRegistry(ModuleRegistry* registry) { registry_ = registry; }
    
    /**
     * Subscribe a module to another module's event
     * @param sourceModule Source module name (e.g., "tracker1")
     * @param eventName Event name (e.g., "triggerEvent")
     * @param targetModule Target module name (e.g., "pool1")
     * @param handlerName Handler method name (e.g., "onTrigger")
     * @return true if subscription succeeded, false otherwise
     */
    bool subscribe(const std::string& sourceModule, const std::string& eventName,
                   const std::string& targetModule, const std::string& handlerName);
    
    /**
     * Unsubscribe a module from another module's event
     * @param sourceModule Source module name
     * @param eventName Event name
     * @param targetModule Target module name
     * @param handlerName Handler method name
     * @return true if unsubscription succeeded
     */
    bool unsubscribe(const std::string& sourceModule, const std::string& eventName,
                     const std::string& targetModule, const std::string& handlerName);
    
    /**
     * Unsubscribe all events from/to a module
     * @param moduleName Module name
     * @return true if all unsubscriptions succeeded
     */
    bool unsubscribeAll(const std::string& moduleName);
    
    /**
     * Clear all event subscriptions
     */
    void clear();
    
    /**
     * Check if a subscription exists
     * @param sourceModule Source module name
     * @param eventName Event name
     * @param targetModule Target module name
     * @param handlerName Handler method name
     * @return true if subscription exists
     */
    bool hasSubscription(const std::string& sourceModule, const std::string& eventName,
                       const std::string& targetModule, const std::string& handlerName) const;
    
    /**
     * Get all subscriptions for a source module
     * @param sourceModule Source module name
     * @return Vector of subscriptions
     */
    std::vector<Subscription> getSubscriptionsFrom(const std::string& sourceModule) const;
    
    /**
     * Get all subscriptions for a target module
     * @param targetModule Target module name
     * @return Vector of subscriptions
     */
    std::vector<Subscription> getSubscriptionsTo(const std::string& targetModule) const;
    
    /**
     * Get total number of subscriptions
     */
    int getSubscriptionCount() const;
    
    /**
     * Serialize subscriptions to JSON
     */
    ofJson toJson() const;
    
    /**
     * Deserialize subscriptions from JSON
     */
    bool fromJson(const ofJson& json);
    
private:
    ModuleRegistry* registry_;
    
    // Subscription tracking
    std::set<Subscription> subscriptions_;
    
    // Store wrapper objects to keep them alive and enable member function pointer subscription
    // Map from subscription to the wrapper object
    std::map<Subscription, std::shared_ptr<ModuleEventWrapper>> eventWrappers_;
    
    // Helper methods
    std::shared_ptr<Module> getModule(const std::string& identifier) const;
    
    // Convert module name to UUID (returns UUID if identifier is already UUID)
    std::string getNameToUUID(const std::string& identifier) const;
};

