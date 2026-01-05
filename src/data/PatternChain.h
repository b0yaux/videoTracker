#pragma once

#include "ofJson.h"
#include <vector>
#include <map>
#include <string>

// PatternChain manages pattern sequencing and chaining logic
// Encapsulates pattern chain state and advancement logic
// Uses pattern names (stable references) instead of indices (unstable)
class PatternChain {
public:
    PatternChain();
    
    // Chain management
    int getSize() const { return (int)chain.size(); }
    int getCurrentIndex() const { return currentIndex; }
    void setCurrentIndex(int index);
    
    void addEntry(const std::string& patternName);
    void removeEntry(int chainIndex);
    void clear();
    std::string getEntry(int chainIndex) const;  // Returns pattern name
    void setEntry(int chainIndex, const std::string& patternName);
    const std::vector<std::string>& getChain() const { return chain; }
    
    // Repeat counts
    int getRepeatCount(int chainIndex) const;
    void setRepeatCount(int chainIndex, int repeatCount);
    
    // Enable/disable
    bool isEnabled() const { return enabled; }
    void setEnabled(bool use) { enabled = use; }
    
    bool isEntryDisabled(int chainIndex) const;
    void setEntryDisabled(int chainIndex, bool disabled);
    
    // Chain advancement logic
    // Called when a pattern finishes (wraps around)
    // Advances chain state and returns the next pattern name to use
    // Returns empty string if chain is disabled/empty or no valid pattern available
    std::string getNextPattern();
    
    // Peek at next pattern without modifying chain state (thread-safe read)
    // Returns what the next pattern would be if getNextPattern() was called
    std::string peekNextPattern() const;
    
    // Reset chain state (called on stop/reset)
    void reset();
    
    // Serialization
    void toJson(ofJson& json) const;
    void fromJson(const ofJson& json, const std::vector<std::string>& availablePatternNames);
    
private:
    std::vector<std::string> chain;            // Sequence of pattern names (stable references)
    std::map<int, int> repeatCounts;           // Repeat counts for each chain entry (default: 1)
    std::map<int, bool> disabled;              // Disabled state for each chain entry
    int currentIndex = 0;                      // Current position in chain
    int currentRepeat = 0;                     // Current repeat count for current chain entry
    bool enabled = true;                       // If true, use pattern chain; if false, use direct pattern name
    
    bool isValidIndex(int index) const;
    int findNextEnabledIndex(int startIndex) const;
};

