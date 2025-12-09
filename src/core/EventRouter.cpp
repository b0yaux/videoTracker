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
    // Unsubscribe all before clearing
    auto subscriptionsCopy = subscriptions_;
    for (const auto& sub : subscriptionsCopy) {
        try {
            unsubscribe(sub.sourceModule, sub.eventName, sub.targetModule, sub.handlerName);
        } catch (...) {
            // Silently handle any unsubscription errors during cleanup
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
    
    // Check if subscription already exists
    Subscription sub(sourceModule, eventName, targetModule, handlerName);
    if (hasSubscription(sourceModule, eventName, targetModule, handlerName)) {
        ofLogNotice("EventRouter") << "Event subscription already exists: " 
                                    << sourceModule << "." << eventName 
                                    << " -> " << targetModule << "." << handlerName;
        return true;
    }
    
    // Get modules
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
    
    // Check if subscription exists
    if (!hasSubscription(sourceModule, eventName, targetModule, handlerName)) {
        return false;
    }
    
    // Get modules
    auto source = getModule(sourceModule);
    auto target = getModule(targetModule);
    
    if (source && target) {
        // Unsubscribe based on event type
        if (eventName == "triggerEvent" && handlerName == "onTrigger") {
            ofEvent<TriggerEvent>* event = source->getEvent(eventName);
            if (event) {
                // Find and remove the stored wrapper
                Subscription sub(sourceModule, eventName, targetModule, handlerName);
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
    
    // Remove from tracking
    Subscription sub(sourceModule, eventName, targetModule, handlerName);
    subscriptions_.erase(sub);
    
    ofLogVerbose("EventRouter") << "Unsubscribed from event: " 
                                 << sourceModule << "." << eventName 
                                 << " -> " << targetModule << "." << handlerName;
    
    return true;
}

bool EventRouter::unsubscribeAll(const std::string& moduleName) {
    if (moduleName.empty()) {
        ofLogWarning("EventRouter") << "Cannot unsubscribeAll with empty module name";
        return false;
    }
    
    bool unsubscribed = false;
    
    // Find all subscriptions involving this module (as source or target)
    std::vector<Subscription> toRemove;
    for (const auto& sub : subscriptions_) {
        if (sub.sourceModule == moduleName || sub.targetModule == moduleName) {
            toRemove.push_back(sub);
        }
    }
    
    // Unsubscribe them
    for (const auto& sub : toRemove) {
        if (unsubscribe(sub.sourceModule, sub.eventName, sub.targetModule, sub.handlerName)) {
            unsubscribed = true;
        }
    }
    
    return unsubscribed;
}

bool EventRouter::hasSubscription(const std::string& sourceModule, const std::string& eventName,
                                 const std::string& targetModule, const std::string& handlerName) const {
    Subscription sub(sourceModule, eventName, targetModule, handlerName);
    return subscriptions_.find(sub) != subscriptions_.end();
}

std::vector<EventRouter::Subscription> EventRouter::getSubscriptionsFrom(const std::string& sourceModule) const {
    std::vector<Subscription> result;
    for (const auto& sub : subscriptions_) {
        if (sub.sourceModule == sourceModule) {
            result.push_back(sub);
        }
    }
    return result;
}

std::vector<EventRouter::Subscription> EventRouter::getSubscriptionsTo(const std::string& targetModule) const {
    std::vector<Subscription> result;
    for (const auto& sub : subscriptions_) {
        if (sub.targetModule == targetModule) {
            result.push_back(sub);
        }
    }
    return result;
}

int EventRouter::getSubscriptionCount() const {
    return subscriptions_.size();
}

ofJson EventRouter::toJson() const {
    ofJson json = ofJson::array();
    for (const auto& sub : subscriptions_) {
        ofJson subJson;
        subJson["sourceModule"] = sub.sourceModule;
        subJson["eventName"] = sub.eventName;
        subJson["targetModule"] = sub.targetModule;
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
        if (subJson.contains("sourceModule") && subJson.contains("eventName") &&
            subJson.contains("targetModule") && subJson.contains("handlerName") &&
            subJson.contains("type") && subJson["type"] == "event") {
            std::string source = subJson["sourceModule"].get<std::string>();
            std::string event = subJson["eventName"].get<std::string>();
            std::string target = subJson["targetModule"].get<std::string>();
            std::string handler = subJson["handlerName"].get<std::string>();
            subscribe(source, event, target, handler);
        }
    }
    
    return true;
}

std::shared_ptr<Module> EventRouter::getModule(const std::string& moduleName) const {
    if (!registry_) {
        return nullptr;
    }
    return registry_->getModule(moduleName);
}

