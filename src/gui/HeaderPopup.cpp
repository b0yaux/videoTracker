#include "HeaderPopup.h"

namespace HeaderPopup {
    void draw(const std::string& popupId,
              const std::vector<PopupItem>& items,
              float columnWidth,
              const ImVec2& headerPos,
              std::function<void(const std::string& itemId)> onItemSelected,
              std::function<bool(const PopupItem&)> filter) {
        // Filter items if filter function is provided
        std::vector<PopupItem> filteredItems = items;
        if (filter) {
            filteredItems.clear();
            for (const auto& item : items) {
                if (filter(item)) {
                    filteredItems.push_back(item);
                }
            }
        }
        
        // Set popup background to match table header color exactly
        ImGui::PushStyleColor(ImGuiCol_PopupBg, GUIConstants::Background::TableHeader);
        
        // No window padding - make it feel like an extension of the header
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        
        // Calculate popup height based on number of filtered items (no padding)
        float itemHeight = ImGui::GetFrameHeight();
        float popupHeight = itemHeight * filteredItems.size();
        
        // Position popup directly above header (flush with header top edge)
        ImVec2 popupPos = ImVec2(headerPos.x, headerPos.y - popupHeight);
        ImVec2 popupSize = ImVec2(columnWidth, popupHeight);
        
        ImGui::SetNextWindowPos(popupPos, ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(popupSize, ImGuiCond_Appearing);
        
        if (ImGui::BeginPopup(popupId.c_str(), ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
            // Match header text color
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            
            // No item spacing - items should be flush
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            
            // Frame padding to match header cell padding
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImGui::GetStyle().CellPadding);
            
            // Show filtered items
            for (const auto& item : filteredItems) {
                // Selectable fills full width
                if (ImGui::Selectable(item.displayName.c_str(), false, 0, ImVec2(columnWidth, itemHeight))) {
                    // Call callback with item ID
                    if (onItemSelected) {
                        onItemSelected(item.id);
                    }
                    ImGui::CloseCurrentPopup();
                }
                
                // Show tooltip if available
                if (ImGui::IsItemHovered() && !item.tooltip.empty()) {
                    ImGui::SetTooltip("%s", item.tooltip.c_str());
                }
            }
            
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
            
            if (filteredItems.empty()) {
                ImGui::Text("No items available");
            }
            
            ImGui::EndPopup();
        }
        
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
}
