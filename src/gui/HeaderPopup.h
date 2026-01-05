#pragma once

#include "ofMain.h"
#include "gui/GUIConstants.h"
#include <imgui.h>
#include <string>
#include <vector>
#include <functional>

// Generic header popup utility for column headers
// Completely generic - can be used for any purpose (parameter swapping, column actions, etc.)
// Follows ImGui best practices: immediate mode, stateless, reusable
namespace HeaderPopup {
    // Item structure for popup items (completely generic)
    struct PopupItem {
        std::string id;           // Unique identifier (e.g., parameter name, action ID, etc.)
        std::string displayName;  // Display text shown in popup
        std::string tooltip;      // Optional tooltip text
        
        PopupItem() {}
        PopupItem(const std::string& id, const std::string& displayName, const std::string& tooltip = "")
            : id(id), displayName(displayName), tooltip(tooltip) {}
    };
    
    // Draw a generic header popup
    // This is the core method - completely generic and reusable
    // 
    // Parameters:
    //   popupId: Unique identifier for the popup (must match the ID used in OpenPopup)
    //   items: List of items to display in the popup
    //   columnWidth: Width of the column (popup will match this width)
    //   headerPos: Screen position of the header (popup will appear above it)
    //   onItemSelected: Callback when an item is selected (receives item ID)
    //   filter: Optional filter function to exclude items (returns true to include, false to exclude)
    //
    // Usage example:
    //   std::vector<HeaderPopup::PopupItem> items = {
    //       HeaderPopup::PopupItem("item1", "Item 1", "Tooltip for item 1"),
    //       HeaderPopup::PopupItem("item2", "Item 2", "Tooltip for item 2")
    //   };
    //   HeaderPopup::draw("MyPopup", items, 100.0f, ImVec2(x, y),
    //                    [](const std::string& id) { /* handle selection */ });
    void draw(const std::string& popupId,
              const std::vector<PopupItem>& items,
              float columnWidth,
              const ImVec2& headerPos,
              std::function<void(const std::string& itemId)> onItemSelected,
              std::function<bool(const PopupItem&)> filter = nullptr,
              std::function<void(const std::string& itemId)> onItemDeleted = nullptr);
}
