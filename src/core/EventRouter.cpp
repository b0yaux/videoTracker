#include "EventRouter.h"
#include "ModuleRegistry.h"
#include "modules/Module.h"
#include "ofLog.h"
#include "ofEvents.h"

EventRouter::EventRouter(ModuleRegistry* registry)
    : registry_(registry) {
}

EventRouter::~EventRouter() {
    clear();
}

void EventRouter::clear() {
    // Unsubscribe all before clearing (convert UUIDs to names for unsubscribe)
    if (registry_) {
        auto subscriptionsCopy = subscriptions_;
        for (const auto& sub : subscriptionsCopy) {
            try {
                std::string sourceName = registry_->getName(sub.sourceUUID);
                std::string targetName = registry_->getName(sub.targetUUID);
                if (!sourceName.empty() && !targetName.empty()) {
                    unsubscribe(sourceName, sub.eventName, targetName, sub.handlerName);
                }
            } catch (...) {
                // Silently handle any unsubscription errors during cleanup
            }
        }
    }
    subscriptions_.clear();
    eventWrappers_.clear();
    
    ofLogNotice("EventRouter") << "Cleared all event subscriptions";
}

bool EventRouter::subscribe(const std::string& sourceModule, const std::string& eventName,
                           const std::string& targetModule, const std::string& handlerName) {
    if (!registry_) {
        ofLogError("EventRouter") << "Registry not set";
        return false;
    }
    
    // Validate inputs
    if (sourceModule.empty() || eventName.empty() || targetModule.empty() || handlerName.empty()) {
        ofLogError("EventRouter") << "Cannot subscribe: empty parameter(s)";
        return false;
    }
    
    if (sourceModule == targetModule) {
        ofLogError("EventRouter") << "Cannot subscribe module to itself: " << sourceModule;
        return false;
    }
    
    // Convert names to UUIDs for internal storage
    std::string sourceUUID = getNameToUUID(sourceModule);
    std::string targetUUID = getNameToUUID(targetModule);
    
    // Check if subscription already exists (using UUIDs)
    Subscription sub(sourceUUID, eventName, targetUUID, handlerName);
    if (hasSubscription(sourceModule, eventName, targetModule, handlerName)) {
        ofLogNotice("EventRouter") << "Event subscription already exists: " 
                                    << sourceModule << "." << eventName 
                                    << " -> " << targetModule << "." << handlerName;
        return true;
    }
    
    // Get modules (using names for actual module access)
    auto source = getModule(sourceModule);
    if (!source) {
        ofLogError("EventRouter") << "Source module not found: " << sourceModule;
        return false;
    }
    
    auto target = getModule(targetModule);
    if (!target) {
        ofLogError("EventRouter") << "Target module not found: " << targetModule;
        return false;
    }
    
    // Subscribe based on event type
    bool subscribed = false;
    
    if (eventName == "triggerEvent" && handlerName == "onTrigger") {
        // Get event generically from source module
        ofEvent<TriggerEvent>* event = source->getEvent(eventName);
        if (event) {
            // Use wrapper class approach to enable member function pointer subscription
            // This works with ofAddListener which requires member function pointers
            // Store weak_ptr to avoid keeping module alive unnecessarily
            std::weak_ptr<Module> weakTarget = target;
            
            // Create wrapper object that will handle the event
            auto wrapper = std::make_shared<ModuleEventWrapper>(weakTarget, targetModule);
            
            // Store wrapper to keep it alive
            eventWrappers_[sub] = wrapper;
            
            // Use ofAddListener with member function pointer - this is the standard way
            // The wrapper's handleTrigger method will call the virtual onTrigger on the target
            ofAddListener(*event, wrapper.get(), &ModuleEventWrapper::handleTrigger);
            subscribed = true;
            
            ofLogVerbose("EventRouter") << "Subscribed to event: " << sourceModule << "." << eventName 
                                        << " -> " << targetModule << "." << handlerName;
        } else {
            ofLogError("EventRouter") << "Source module '" << sourceModule 
                                      << "' does not have event '" << eventName 
                                      << "' (getEvent returned nullptr)";
            return false;
        }
    } else {
        // For other event types, we would need a more generic event discovery mechanism
        // This can be extended later with Module interface methods for event discovery
        ofLogWarning("EventRouter") << "Unknown event subscription pattern: " 
                                      << sourceModule << "." << eventName 
                                      << " -> " << targetModule << "." << handlerName
                                      << " (only 'triggerEvent' -> 'onTrigger' is currently supported)";
        return false;
    }
    
    if (subscribed) {
        // Track subscription
        subscriptions_.insert(sub);
        ofLogNotice("EventRouter") << "Subscribed to event: " 
                                    << sourceModule << "." << eventName 
                                    << " -> " << targetModule << "." << handlerName;
        return true;
    }
    
    return false;
}

bool EventRouter::unsubscribe(const std::string& sourceModule, const std::string& eventName,
                              const std::string& targetModule, const std::string& handlerName) {
    if (!registry_) {
        return false;
    }
    
    // Convert names to UUIDs for lookup (subscriptions are stored by UUID)
    std::string sourceUUID = getNameToUUID(sourceModule);
    std::string targetUUID = getNameToUUID(targetModule);
    Subscription sub(sourceUUID, eventName, targetUUID, handlerName);
    
    // Check if subscription exists
    if (subscriptions_.find(sub) == subscriptions_.end()) {
        return false;
    }
    
    // Get modules (using names for actual module access)
    auto source = getModule(sourceModule);
    auto target = getModule(targetModule);
    
    if (source && target) {
        // Unsubscribe based on event type
        if (eventName == "triggerEvent" && handlerName == "onTrigger") {
            ofEvent<TriggerEvent>* event = source->getEvent(eventName);
            if (event) {
                // Find and remove the stored wrapper (using UUID-based subscription)
                auto it = eventWrappers_.find(sub);
                if (it != eventWrappers_.end()) {
                    // Remove using member function pointer
                    ofRemoveListener(*event, it->second.get(), &ModuleEventWrapper::handleTrigger);
                    eventWrappers_.erase(it);
                } else {
                    ofLogWarning("EventRouter") << "Wrapper not found for subscription - "
                                                 << "may have already been removed";
                }
            }
        }
    }
    
    // Remove from tracking (using UUID-based subscription)
    subscriptions_.erase(sub);
    
    ofLogVerbose("EventRouter") << "Unsubscribed from event: " 
                                 << sourceModule << "." << eventName 
                                 << " -> " << targetModule << "." << handlerName;
    
    return true;
}

bool EventRouter::unsubscribeAll(const std::string& moduleName) {
    if (moduleName.empty() || !registry_) {
        ofLogWarning("EventRouter") << "Cannot unsubscribeAll with empty module name or no registry";
        return false;
    }
    
    // Convert name to UUID (subscriptions are stored by UUID)
    std::string moduleUUID = getNameToUUID(moduleName);
    
    bool unsubscribed = false;
    
    // Find all subscriptions involving this module (as source or target, by UUID)
    std::vector<Subscription> toRemove;
    for (const auto& sub : subscriptions_) {
        if (sub.sourceUUID == moduleUUID || sub.targetUUID == moduleUUID) {
            toRemove.push_back(sub);
        }
    }
    
    // Unsubscribe them (convert UUIDs back to names for unsubscribe)
    for (const auto& sub : toRemove) {
        std::string sourceName = registry_->getName(sub.sourceUUID);
        std::string targetName = registry_->getName(sub.targetUUID);
        if (!sourceName.empty() && !targetName.empty()) {
            if (unsubscribe(sourceName, sub.eventName, targetName, sub.handlerName)) {
                unsubscribed = true;
            }
        }
    }
    
    return unsubscribed;
}

// renameModule removed - subscriptions are now UUID-based, so renaming doesn't affect them

bool EventRouter::hasSubscription(const std::string& sourceModule, const std::string& eventName,
                                 const std::string& targetModule, const std::string& handlerName) const {
    if (!registry_) {
        return false;
    }
    
    // Convert names to UUIDs (subscriptions are stored by UUID)
    std::string sourceUUID = getNameToUUID(sourceModule);
    std::string targetUUID = getNameToUUID(targetModule);
    Subscription sub(sourceUUID, eventName, targetUUID, handlerName);
    return subscriptions_.find(sub) != subscriptions_.end();
}

std::vector<EventRouter::Subscription> EventRouter::getSubscriptionsFrom(const std::string& sourceModule) const {
    std::vector<Subscription> result;
    if (!registry_) {
        return result;
    }
    
    // Convert name to UUID (subscriptions are stored by UUID)
    std::string sourceUUID = getNameToUUID(sourceModule);
    for (const auto& sub : subscriptions_) {
        if (sub.sourceUUID == sourceUUID) {
            // Return subscription with names for compatibility (create temporary Subscription with names)
            Subscription namedSub;
            namedSub.sourceUUID = sub.sourceUUID;
            namedSub.eventName = sub.eventName;
            namedSub.targetUUID = sub.targetUUID;
            namedSub.handlerName = sub.handlerName;
            result.push_back(namedSub);
        }
    }
    return result;
}

std::vector<EventRouter::Subscription> EventRouter::getSubscriptionsTo(const std::string& targetModule) const {
    std::vector<Subscription> result;
    if (!registry_) {
        return result;
    }
    
    // Convert name to UUID (subscriptions are stored by UUID)
    std::string targetUUID = getNameToUUID(targetModule);
    for (const auto& sub : subscriptions_) {
        if (sub.targetUUID == targetUUID) {
            // Return subscription with names for compatibility (create temporary Subscription with names)
            Subscription namedSub;
            namedSub.sourceUUID = sub.sourceUUID;
            namedSub.eventName = sub.eventName;
            namedSub.targetUUID = sub.targetUUID;
            namedSub.handlerName = sub.handlerName;
            result.push_back(namedSub);
        }
    }
    return result;
}

int EventRouter::getSubscriptionCount() const {
    return subscriptions_.size();
}

ofJson EventRouter::toJson() const {
    ofJson json = ofJson::array();
    if (!registry_) {
        return json;
    }
    
    for (const auto& sub : subscriptions_) {
        ofJson subJson;
        // Store UUIDs (for reliability) and names (for readability)
        subJson["sourceUUID"] = sub.sourceUUID;
        subJson["sourceModule"] = registry_->getName(sub.sourceUUID);
        subJson["eventName"] = sub.eventName;
        subJson["targetUUID"] = sub.targetUUID;
        subJson["targetModule"] = registry_->getName(sub.targetUUID);
        subJson["handlerName"] = sub.handlerName;
        subJson["type"] = "event";
        json.push_back(subJson);
    }
    return json;
}

bool EventRouter::fromJson(const ofJson& json) {
    if (!json.is_array()) {
        ofLogError("EventRouter") << "Invalid JSON format: expected array";
        return false;
    }
    
    clear();
    
    for (const auto& subJson : json) {
        if (subJson.contains("type") && subJson["type"] == "event") {
            // UUID-based format
            if (subJson.contains("sourceUUID") && subJson.contains("eventName") &&
                subJson.contains("targetUUID") && subJson.contains("handlerName")) {
                std::string sourceUUID = subJson["sourceUUID"].get<std::string>();
                std::string event = subJson["eventName"].get<std::string>();
                std::string targetUUID = subJson["targetUUID"].get<std::string>();
                std::string handler = subJson["handlerName"].get<std::string>();
                
                // Convert UUIDs to names for subscribe (public API accepts names for consistency)
                // Note: subscribe will convert back to UUIDs internally - this keeps the API clean
                if (registry_) {
                    std::string source = registry_->getName(sourceUUID);
                    std::string target = registry_->getName(targetUUID);
                    if (!source.empty() && !target.empty()) {
                        subscribe(source, event, target, handler);
                    }
                }
            }
        }
    }
    
    return true;
}

std::shared_ptr<Module> EventRouter::getModule(const std::string& identifier) const {
    if (!registry_) {
        return nullptr;
    }
    return registry_->getModule(identifier);
}

std::string EventRouter::getNameToUUID(const std::string& identifier) const {
    if (!registry_) {
        return identifier;
    }
    
    // Try to get UUID from name (if identifier is a name, this returns UUID)
    std::string uuid = registry_->getUUID(identifier);
    if (!uuid.empty()) {
        return uuid;
    }
    
    // If getUUID returned empty, identifier might already be a UUID
    // Check if module exists - if so, assume identifier is UUID
    if (registry_->hasModule(identifier)) {
        return identifier;
    }
    
    // Fallback: return as-is
    return identifier;
}

