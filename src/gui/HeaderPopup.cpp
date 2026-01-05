#include "HeaderPopup.h"

namespace HeaderPopup {
    void draw(const std::string& popupId,
              const std::vector<PopupItem>& items,
              float columnWidth,
              const ImVec2& headerPos,
              std::function<void(const std::string& itemId)> onItemSelected,
              std::function<bool(const PopupItem&)> filter,
              std::function<void(const std::string& itemId)> onItemDeleted) {
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
                // Create a row for this item (selectable + optional delete button)
                ImGui::PushID(item.id.c_str());
                
                // Calculate available width for selectable (leave space for delete button if delete callback exists)
                float deleteButtonWidth = 0.0f;
                float selectableWidth = columnWidth;
                if (onItemDeleted) {
                    deleteButtonWidth = ImGui::GetFrameHeight(); // Square button
                    selectableWidth = columnWidth - deleteButtonWidth - ImGui::GetStyle().ItemSpacing.x;
                }
                
                // Track hover state for the entire row using mouse position
                // This avoids using an invisible button that might intercept clicks
                ImVec2 rowStartPos = ImGui::GetCursorScreenPos();
                ImVec2 mousePos = ImGui::GetMousePos();
                bool isRowHovered = (mousePos.x >= rowStartPos.x && mousePos.x < rowStartPos.x + columnWidth &&
                                     mousePos.y >= rowStartPos.y && mousePos.y < rowStartPos.y + itemHeight);
                
                // Selectable for item selection (fills available width)
                if (ImGui::Selectable(item.displayName.c_str(), false, 0, ImVec2(selectableWidth, itemHeight))) {
                    // Call callback with item ID
                    if (onItemSelected) {
                        onItemSelected(item.id);
                    }
                    ImGui::CloseCurrentPopup();
                }
                
                // Check if selectable is hovered
                bool isSelectableHovered = ImGui::IsItemHovered();
                
                // Update row hover state after drawing selectable (in case mouse moved)
                if (!isRowHovered) {
                    mousePos = ImGui::GetMousePos();
                    isRowHovered = (mousePos.x >= rowStartPos.x && mousePos.x < rowStartPos.x + columnWidth &&
                                   mousePos.y >= rowStartPos.y && mousePos.y < rowStartPos.y + itemHeight);
                }
                
                // Show tooltip if available
                if (isSelectableHovered && !item.tooltip.empty()) {
                    ImGui::SetTooltip("%s", item.tooltip.c_str());
                }
                
                // Delete button (appears on hover, aligned to right)
                if (onItemDeleted) {
                    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x);
                    
                    // Show delete button when row is hovered (tracked by mouse position)
                    if (isRowHovered || isSelectableHovered) {
                        // Style delete button (red tint)
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.0f, 0.0f, 0.3f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.0f, 0.0f, 0.5f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.0f, 0.0f, 0.7f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                        
                        if (ImGui::Button("×", ImVec2(deleteButtonWidth, itemHeight))) {
                            // Call delete callback
                            if (onItemDeleted) {
                                onItemDeleted(item.id);
                            }
                            // Don't close popup - allow user to continue selecting/deleting
                        }
                        
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Delete %s", item.displayName.c_str());
                        }
                        
                        ImGui::PopStyleColor(4);
                    } else {
                        // Invisible button to maintain layout when not hovered
                        ImGui::InvisibleButton("##delete_spacer", ImVec2(deleteButtonWidth, itemHeight));
                    }
                }
                
                ImGui::PopID();
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
