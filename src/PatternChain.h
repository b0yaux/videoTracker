#pragma once

#include "ofJson.h"
#include <vector>
#include <map>

// PatternChain manages pattern sequencing and chaining logic
// Encapsulates pattern chain state and advancement logic
class PatternChain {
public:
    PatternChain();
    
    // Chain management
    int getSize() const { return (int)chain.size(); }
    int getCurrentIndex() const { return currentIndex; }
    void setCurrentIndex(int index);
    
    void addEntry(int patternIndex);
    void removeEntry(int chainIndex);
    void clear();
    int getEntry(int chainIndex) const;
    void setEntry(int chainIndex, int patternIndex);
    const std::vector<int>& getChain() const { return chain; }
    
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
    // Returns the next pattern index to use, or -1 if chain is disabled/empty
    // Updates internal state (currentIndex, currentRepeat)
    int advanceOnPatternFinish(int numPatterns);
    
    // Reset chain state (called on stop/reset)
    void reset();
    
    // Serialization
    void toJson(ofJson& json) const;
    void fromJson(const ofJson& json, int numPatterns);
    
private:
    std::vector<int> chain;                    // Sequence of pattern indices
    std::map<int, int> repeatCounts;           // Repeat counts for each chain entry (default: 1)
    std::map<int, bool> disabled;              // Disabled state for each chain entry
    int currentIndex = 0;                      // Current position in chain
    int currentRepeat = 0;                     // Current repeat count for current chain entry
    bool enabled = true;                       // If true, use pattern chain; if false, use direct pattern index
    
    bool isValidIndex(int index) const;
    int findNextEnabledIndex(int startIndex) const;
};

