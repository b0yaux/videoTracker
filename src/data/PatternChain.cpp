#include "PatternChain.h"
#include "ofLog.h"
#include "ofJson.h"

PatternChain::PatternChain() {
    // Default initialization - chains are enabled by default
    enabled = true;
}

void PatternChain::setCurrentIndex(int index) {
    if (index >= 0 && index < (int)chain.size()) {
        currentIndex = index;
        currentRepeat = 0;  // Reset repeat counter
    } else {
        ofLogWarning("PatternChain") << "Invalid chain index: " << index;
    }
}

void PatternChain::addEntry(const std::string& patternName) {
    int newIndex = (int)chain.size();
    chain.push_back(patternName);
    repeatCounts[newIndex] = 1;  // Default repeat count
    ofLogNotice("PatternChain") << "Added pattern '" << patternName << "' to chain";
}

void PatternChain::removeEntry(int chainIndex) {
    if (!isValidIndex(chainIndex)) {
        ofLogWarning("PatternChain") << "Invalid chain index for removal: " << chainIndex;
        return;
    }
    
    chain.erase(chain.begin() + chainIndex);
    
    // Remove repeat count and adjust indices
    repeatCounts.erase(chainIndex);
    std::map<int, int> newRepeatCounts;
    for (const auto& pair : repeatCounts) {
        if (pair.first < chainIndex) {
            newRepeatCounts[pair.first] = pair.second;
        } else if (pair.first > chainIndex) {
            newRepeatCounts[pair.first - 1] = pair.second;
        }
    }
    repeatCounts = newRepeatCounts;
    
    // Remove disabled state and adjust indices
    disabled.erase(chainIndex);
    std::map<int, bool> newDisabled;
    for (const auto& pair : disabled) {
        if (pair.first < chainIndex) {
            newDisabled[pair.first] = pair.second;
        } else if (pair.first > chainIndex) {
            newDisabled[pair.first - 1] = pair.second;
        }
    }
    disabled = newDisabled;
    
    // Adjust current chain index if necessary
    bool wasCurrentIndex = (currentIndex == chainIndex);
    if (currentIndex > chainIndex) {
        currentIndex--;
    }
    // If current index is out of bounds, clamp to last valid index
    if (currentIndex >= (int)chain.size()) {
        currentIndex = std::max(0, (int)chain.size() - 1);
    }
    if (wasCurrentIndex) {
        // If we removed the current index, reset repeat counter
        currentRepeat = 0;
    }
    
    ofLogNotice("PatternChain") << "Removed chain entry at index " << chainIndex;
}

void PatternChain::clear() {
    // CRITICAL: Preserve enabled state during playback
    // Only clear entries, don't reset enabled flag if chain is active
    // This prevents chain from being disabled during edits
    bool wasEnabled = enabled;
    
    chain.clear();
    repeatCounts.clear();
    disabled.clear();
    currentIndex = 0;
    currentRepeat = 0;
    
    // Restore enabled state (chain editing shouldn't disable chain)
    enabled = wasEnabled;
    // Note: currentIndex is reset to 0 since chain is empty, but enabled state is preserved
    // This prevents chain from being disabled during edits
    
    ofLogNotice("PatternChain") << "Pattern chain cleared (enabled state preserved: " << (enabled ? "true" : "false") << ")";
}

std::string PatternChain::getEntry(int chainIndex) const {
    if (isValidIndex(chainIndex)) {
        return chain[chainIndex];
    }
    return "";
}

void PatternChain::setEntry(int chainIndex, const std::string& patternName) {
    if (chainIndex < 0) {
        ofLogWarning("PatternChain") << "Invalid chain index: " << chainIndex;
        return;
    }
    
    // Resize pattern chain if necessary
    if (chainIndex >= (int)chain.size()) {
        chain.resize(chainIndex + 1, "");
        // Set default repeat count for new entries
        if (repeatCounts.find(chainIndex) == repeatCounts.end()) {
            repeatCounts[chainIndex] = 1;
        }
    }
    
    chain[chainIndex] = patternName;
    ofLogNotice("PatternChain") << "Set chain entry " << chainIndex << " to pattern '" << patternName << "'";
}

int PatternChain::getRepeatCount(int chainIndex) const {
    if (!isValidIndex(chainIndex)) {
        return 1;  // Default repeat count
    }
    auto it = repeatCounts.find(chainIndex);
    if (it != repeatCounts.end()) {
        return it->second;
    }
    return 1;  // Default repeat count
}

void PatternChain::setRepeatCount(int chainIndex, int repeatCount) {
    if (!isValidIndex(chainIndex)) {
        ofLogWarning("PatternChain") << "Invalid chain index: " << chainIndex;
        return;
    }
    
    repeatCount = std::max(1, std::min(99, repeatCount));  // Clamp to 1-99
    repeatCounts[chainIndex] = repeatCount;
    ofLogNotice("PatternChain") << "Set chain entry " << chainIndex << " repeat count to " << repeatCount;
}

bool PatternChain::isEntryDisabled(int chainIndex) const {
    if (!isValidIndex(chainIndex)) {
        return false;
    }
    auto it = disabled.find(chainIndex);
    return (it != disabled.end() && it->second);
}

void PatternChain::setEntryDisabled(int chainIndex, bool disabledState) {
    if (!isValidIndex(chainIndex)) {
        ofLogWarning("PatternChain") << "Invalid chain index: " << chainIndex;
        return;
    }
    disabled[chainIndex] = disabledState;
    ofLogVerbose("PatternChain") << "Set chain entry " << chainIndex << " disabled: " << (disabledState ? "true" : "false");
}

std::string PatternChain::peekNextPattern() const {
    if (!enabled || chain.empty()) {
        return "";  // Chain disabled or empty
    }
    
    // Calculate what the next pattern would be without modifying state
    int peekRepeat = currentRepeat + 1;
    int peekIndex = currentIndex;
    
    // Get repeat count for current chain entry (default to 1 if not set)
    int repeatCount = getRepeatCount(peekIndex);
    
    // Check if we've finished all repeats for current chain entry
    if (peekRepeat >= repeatCount) {
        // Move to next chain entry (skip disabled entries)
        peekRepeat = 0;
        int startIndex = peekIndex;
        do {
            peekIndex = (peekIndex + 1) % (int)chain.size();
            // If we've looped back to start and all are disabled, break to avoid infinite loop
            if (peekIndex == startIndex) break;
        } while (isEntryDisabled(peekIndex) && peekIndex != startIndex);
    }
    
    // Return the next pattern name (only if not disabled)
    if (!isEntryDisabled(peekIndex)) {
        std::string nextPatternName = chain[peekIndex];
        if (!nextPatternName.empty()) {
            return nextPatternName;
        }
    }
    
    return "";  // No valid pattern to advance to
}

std::string PatternChain::getNextPattern() {
    
    
    if (!enabled || chain.empty()) {
        return "";  // Chain disabled or empty
    }
    
    // Increment repeat counter
    currentRepeat++;
    
    // Get repeat count for current chain entry (default to 1 if not set)
    int repeatCount = getRepeatCount(currentIndex);
    
    
    
    // Check if we've finished all repeats for current chain entry
    if (currentRepeat >= repeatCount) {
        
        
        // Move to next chain entry (skip disabled entries)
        currentRepeat = 0;
        int startIndex = currentIndex;
        do {
            currentIndex = (currentIndex + 1) % (int)chain.size();
            // If we've looped back to start and all are disabled, break to avoid infinite loop
            if (currentIndex == startIndex) break;
        } while (isEntryDisabled(currentIndex) && currentIndex != startIndex);
        
        
    }
    
    // Return the next pattern name (only if not disabled)
    if (!isEntryDisabled(currentIndex)) {
        std::string nextPatternName = chain[currentIndex];
        
        
        
        if (!nextPatternName.empty()) {
            ofLogVerbose("PatternChain") << "Pattern finished, advancing to pattern '" << nextPatternName 
                                         << "' (chain position " << currentIndex 
                                         << ", repeat " << (currentRepeat + 1) << "/" << repeatCount << ")";
            return nextPatternName;
        }
    }
    
    
    
    return "";  // No valid pattern to advance to
}

void PatternChain::reset() {
    currentIndex = 0;
    currentRepeat = 0;
}

void PatternChain::toJson(ofJson& json) const {
    ofJson chainArray = ofJson::array();
    for (size_t i = 0; i < chain.size(); i++) {
        ofJson entry;
        entry["patternName"] = chain[i];  // Save pattern name instead of index
        entry["repeatCount"] = getRepeatCount((int)i);
        chainArray.push_back(entry);
    }
    json["patternChain"] = chainArray;
    json["usePatternChain"] = enabled;
    json["currentChainIndex"] = currentIndex;
    json["currentChainRepeat"] = currentRepeat;
}

void PatternChain::fromJson(const ofJson& json, const std::vector<std::string>& availablePatternNames) {
    chain.clear();
    repeatCounts.clear();
    disabled.clear();
    
    // Load pattern chain with repeat counts (support both new and legacy keys)
    ofJson chainArray;
    if (json.contains("patternChain") && json["patternChain"].is_array()) {
        chainArray = json["patternChain"];
    } else if (json.contains("orderList") && json["orderList"].is_array()) {
        // Legacy: support old "orderList" key
        chainArray = json["orderList"];
    }
    
    if (!chainArray.is_null() && chainArray.is_array()) {
        for (size_t i = 0; i < chainArray.size(); i++) {
            const auto& chainEntry = chainArray[i];
            std::string patternName;
            int repeatCount = 1;
            
            // Support both legacy format (int index) and new format (object with patternName)
            if (chainEntry.is_number()) {
                // Legacy format: pattern index - convert to name if possible
                int patternIdx = chainEntry;
                if (patternIdx >= 0 && patternIdx < (int)availablePatternNames.size()) {
                    patternName = availablePatternNames[patternIdx];
                } else {
                    ofLogWarning("PatternChain") << "Invalid pattern index in legacy format: " << patternIdx;
                    continue;
                }
            } else if (chainEntry.is_object()) {
                // New format: object with patternName (or patternIndex for backward compatibility)
                if (chainEntry.contains("patternName")) {
                    patternName = chainEntry["patternName"].get<std::string>();
                } else if (chainEntry.contains("patternIndex")) {
                    // Legacy: convert index to name
                    int patternIdx = chainEntry["patternIndex"];
                    if (patternIdx >= 0 && patternIdx < (int)availablePatternNames.size()) {
                        patternName = availablePatternNames[patternIdx];
                    } else {
                        ofLogWarning("PatternChain") << "Invalid pattern index: " << patternIdx;
                        continue;
                    }
                }
                if (chainEntry.contains("repeatCount")) {
                    repeatCount = chainEntry["repeatCount"];
                    repeatCount = std::max(1, std::min(99, repeatCount));
                }
            }
            
            // Only add if pattern name exists in available patterns
            if (!patternName.empty()) {
                // Check if pattern name exists (for validation)
                bool patternExists = false;
                for (const auto& name : availablePatternNames) {
                    if (name == patternName) {
                        patternExists = true;
                        break;
                    }
                }
                if (patternExists) {
                    chain.push_back(patternName);
                    repeatCounts[(int)i] = repeatCount;
                    disabled[(int)i] = false;  // Default to enabled when loading
                } else {
                    ofLogWarning("PatternChain") << "Pattern name not found in available patterns: '" << patternName << "', skipping";
                }
            }
        }
    }
    
    // Load pattern chain settings (support both new and legacy keys)
    if (json.contains("usePatternChain")) {
        enabled = json["usePatternChain"];
    } else if (json.contains("useOrderList")) {
        // Legacy: support old "useOrderList" key
        enabled = json["useOrderList"];
    } else {
        // Default to enabled for new files
        enabled = true;
    }
    
    if (json.contains("currentChainIndex")) {
        int loadedChainIndex = json["currentChainIndex"];
        if (loadedChainIndex >= 0 && loadedChainIndex < (int)chain.size()) {
            currentIndex = loadedChainIndex;
        } else {
            currentIndex = 0;
        }
    } else if (json.contains("currentOrderIndex")) {
        // Legacy: support old "currentOrderIndex" key
        int loadedChainIndex = json["currentOrderIndex"];
        if (loadedChainIndex >= 0 && loadedChainIndex < (int)chain.size()) {
            currentIndex = loadedChainIndex;
        } else {
            currentIndex = 0;
        }
    }
    
    if (json.contains("currentChainRepeat")) {
        currentRepeat = json["currentChainRepeat"];
    } else if (json.contains("currentOrderRepeat")) {
        // Legacy: support old "currentOrderRepeat" key
        currentRepeat = json["currentOrderRepeat"];
    } else {
        currentRepeat = 0;
    }
    
    // If pattern chain is empty but enabled, initialize with all available patterns
    if (enabled && chain.empty() && !availablePatternNames.empty()) {
        for (const auto& patternName : availablePatternNames) {
            chain.push_back(patternName);
            repeatCounts[(int)chain.size() - 1] = 1;
        }
        currentIndex = 0;
        currentRepeat = 0;
    }
}

bool PatternChain::isValidIndex(int index) const {
    return index >= 0 && index < (int)chain.size();
}

int PatternChain::findNextEnabledIndex(int startIndex) const {
    if (chain.empty()) return -1;
    
    int index = startIndex;
    int attempts = 0;
    while (attempts < (int)chain.size()) {
        if (!isEntryDisabled(index)) {
            return index;
        }
        index = (index + 1) % (int)chain.size();
        attempts++;
    }
    return -1;  // All entries disabled
}

