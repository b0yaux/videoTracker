#include "GUIManager.h"
#include "TrackerSequencer.h"
#include "MediaPool.h"
#include "core/ModuleRegistry.h"
#include "ofLog.h"
#include <algorithm>

GUIManager::GUIManager() {
}

GUIManager::~GUIManager() {
    // unique_ptr will automatically clean up
}

void GUIManager::setRegistry(ModuleRegistry* registry_) {
    registry = registry_;
}

void GUIManager::syncWithRegistry() {
    if (!registry) {
        ofLogWarning("GUIManager") << "Cannot sync: registry is null";
        return;
    }
    
    syncMediaPoolGUIs();
    syncTrackerGUIs();
}

void GUIManager::syncMediaPoolGUIs() {
    if (!registry) return;
    
    // Get all INSTRUMENT type modules (MediaPool is INSTRUMENT)
    auto instruments = registry->getModulesByType(ModuleType::INSTRUMENT);
    
    // Build set of current MediaPool instance names
    std::set<std::string> currentInstances;
    for (const auto& module : instruments) {
        // Check if it's actually a MediaPool (not just any instrument)
        auto mediaPool = std::dynamic_pointer_cast<MediaPool>(module);
        if (mediaPool) {
            std::string name = getInstanceNameForModule(module);
            if (!name.empty()) {
                currentInstances.insert(name);
            }
        }
    }
    
    // Remove GUIs for deleted instances
    auto it = mediaPoolGUIs.begin();
    while (it != mediaPoolGUIs.end()) {
        if (currentInstances.find(it->first) == currentInstances.end()) {
            std::string instanceName = it->first;  // Save name before erasing
            ofLogNotice("GUIManager") << "Removing MediaPool GUI for deleted instance: " << instanceName;
            visibleMediaPoolInstances.erase(instanceName);
            it = mediaPoolGUIs.erase(it);
        } else {
            ++it;
        }
    }
    
    // Create GUIs for new instances
    for (const auto& name : currentInstances) {
        if (mediaPoolGUIs.find(name) == mediaPoolGUIs.end()) {
            ofLogNotice("GUIManager") << "Creating MediaPool GUI for instance: " << name;
            auto gui = std::make_unique<MediaPoolGUI>();
            gui->setRegistry(registry);
            gui->setInstanceName(name);
            mediaPoolGUIs[name] = std::move(gui);
            
            // Default: make first instance visible
            if (visibleMediaPoolInstances.empty()) {
                visibleMediaPoolInstances.insert(name);
            }
        }
    }
}

void GUIManager::syncTrackerGUIs() {
    if (!registry) return;
    
    // Get all SEQUENCER type modules (TrackerSequencer is SEQUENCER)
    auto sequencers = registry->getModulesByType(ModuleType::SEQUENCER);
    
    // Build set of current TrackerSequencer instance names
    std::set<std::string> currentInstances;
    for (const auto& module : sequencers) {
        // Check if it's actually a TrackerSequencer (not just any sequencer)
        auto tracker = std::dynamic_pointer_cast<TrackerSequencer>(module);
        if (tracker) {
            std::string name = getInstanceNameForModule(module);
            if (!name.empty()) {
                currentInstances.insert(name);
            }
        }
    }
    
    // Remove GUIs for deleted instances
    auto it = trackerGUIs.begin();
    while (it != trackerGUIs.end()) {
        if (currentInstances.find(it->first) == currentInstances.end()) {
            std::string instanceName = it->first;  // Save name before erasing
            ofLogNotice("GUIManager") << "Removing TrackerSequencer GUI for deleted instance: " << instanceName;
            visibleTrackerInstances.erase(instanceName);
            it = trackerGUIs.erase(it);
        } else {
            ++it;
        }
    }
    
    // Create GUIs for new instances
    for (const auto& name : currentInstances) {
        if (trackerGUIs.find(name) == trackerGUIs.end()) {
            ofLogNotice("GUIManager") << "Creating TrackerSequencer GUI for instance: " << name;
            auto gui = std::make_unique<TrackerSequencerGUI>();
            gui->setRegistry(registry);
            gui->setInstanceName(name);
            trackerGUIs[name] = std::move(gui);
            
            // Default: make first instance visible
            if (visibleTrackerInstances.empty()) {
                visibleTrackerInstances.insert(name);
            }
        }
    }
}

std::string GUIManager::getInstanceNameForModule(std::shared_ptr<Module> module) const {
    if (!registry || !module) return "";
    
    // Iterate through all modules to find the one matching this pointer
    std::vector<std::string> allUUIDs = registry->getAllUUIDs();
    for (const auto& uuid : allUUIDs) {
        auto regModule = registry->getModule(uuid);
        if (regModule == module) {
            return registry->getName(uuid);
        }
    }
    
    return "";
}

MediaPoolGUI* GUIManager::getMediaPoolGUI(const std::string& instanceName) {
    auto it = mediaPoolGUIs.find(instanceName);
    if (it != mediaPoolGUIs.end()) {
        return it->second.get();
    }
    return nullptr;
}

TrackerSequencerGUI* GUIManager::getTrackerGUI(const std::string& instanceName) {
    auto it = trackerGUIs.find(instanceName);
    if (it != trackerGUIs.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::vector<MediaPoolGUI*> GUIManager::getAllMediaPoolGUIs() {
    std::vector<MediaPoolGUI*> result;
    result.reserve(mediaPoolGUIs.size());
    for (auto& pair : mediaPoolGUIs) {
        result.push_back(pair.second.get());
    }
    return result;
}

std::vector<TrackerSequencerGUI*> GUIManager::getAllTrackerGUIs() {
    std::vector<TrackerSequencerGUI*> result;
    result.reserve(trackerGUIs.size());
    for (auto& pair : trackerGUIs) {
        result.push_back(pair.second.get());
    }
    return result;
}

void GUIManager::setInstanceVisible(const std::string& instanceName, bool visible) {
    // Check if it's a MediaPool instance
    if (mediaPoolGUIs.find(instanceName) != mediaPoolGUIs.end()) {
        if (visible) {
            visibleMediaPoolInstances.insert(instanceName);
        } else {
            visibleMediaPoolInstances.erase(instanceName);
        }
        return;
    }
    
    // Check if it's a TrackerSequencer instance
    if (trackerGUIs.find(instanceName) != trackerGUIs.end()) {
        if (visible) {
            visibleTrackerInstances.insert(instanceName);
        } else {
            visibleTrackerInstances.erase(instanceName);
        }
        return;
    }
    
    ofLogWarning("GUIManager") << "Instance not found: " << instanceName;
}

bool GUIManager::isInstanceVisible(const std::string& instanceName) const {
    if (visibleMediaPoolInstances.find(instanceName) != visibleMediaPoolInstances.end()) {
        return true;
    }
    if (visibleTrackerInstances.find(instanceName) != visibleTrackerInstances.end()) {
        return true;
    }
    return false;
}

std::set<std::string> GUIManager::getVisibleInstances(ModuleType type) const {
    if (type == ModuleType::INSTRUMENT) {
        return visibleMediaPoolInstances;
    } else if (type == ModuleType::SEQUENCER) {
        return visibleTrackerInstances;
    }
    return std::set<std::string>();
}

