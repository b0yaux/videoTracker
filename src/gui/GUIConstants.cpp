//
//  GUIConstants.cpp
//  Implementation of centralized constants management
//

#include "GUIConstants.h"

namespace GUIConstants {
    
    void applyImGuiStyle() {
        ImGuiStyle& style = ImGui::GetStyle();
        
        // Base backgrounds
        style.Colors[ImGuiCol_DockingEmptyBg] = Background::DockingEmpty;
        style.Colors[ImGuiCol_WindowBg] = Background::Window;
        style.Colors[ImGuiCol_ChildBg] = Background::Child;
        style.Colors[ImGuiCol_PopupBg] = Background::Popup;
        style.Colors[ImGuiCol_ModalWindowDimBg] = Background::ModalDim;
        style.Colors[ImGuiCol_MenuBarBg] = Background::MenuBar;
        style.Colors[ImGuiCol_TitleBg] = Background::Title;
        style.Colors[ImGuiCol_TitleBgActive] = Background::TitleActive;
        
        // Scrollbar
        style.Colors[ImGuiCol_ScrollbarBg] = Scrollbar::Bg;
        style.Colors[ImGuiCol_ScrollbarGrab] = Scrollbar::Grab;
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = Scrollbar::GrabHovered;
        style.Colors[ImGuiCol_ScrollbarGrabActive] = Scrollbar::GrabActive;
        
        // Resize grip
        style.Colors[ImGuiCol_ResizeGrip] = ResizeGrip::Default;
        
        // Tabs
        style.Colors[ImGuiCol_Tab] = Tab::Default;
        style.Colors[ImGuiCol_TabHovered] = Tab::Hovered;
        style.Colors[ImGuiCol_TabActive] = Tab::Active;
        style.Colors[ImGuiCol_TabUnfocused] = Tab::Unfocused;
        style.Colors[ImGuiCol_TabUnfocusedActive] = Tab::UnfocusedActive;
        
        // Separators
        style.Colors[ImGuiCol_Separator] = Separator::Default;
        style.Colors[ImGuiCol_SeparatorHovered] = Separator::Hovered;
        style.Colors[ImGuiCol_SeparatorActive] = Separator::Active;
        
        // Table/Grid
        style.Colors[ImGuiCol_TableHeaderBg] = Background::TableHeader;
        style.Colors[ImGuiCol_TableBorderStrong] = Border::Strong;
        style.Colors[ImGuiCol_TableBorderLight] = Border::Light;
        style.Colors[ImGuiCol_TableRowBg] = Background::TableRow;
        style.Colors[ImGuiCol_TableRowBgAlt] = Background::TableRowAlt;
        
        // Headers
        style.Colors[ImGuiCol_Header] = Header::Default;
        
        // Buttons
        style.Colors[ImGuiCol_Button] = Button::Default;
        style.Colors[ImGuiCol_ButtonHovered] = Button::Hovered;
        style.Colors[ImGuiCol_ButtonActive] = Button::Active;
        
        // Sliders
        style.Colors[ImGuiCol_SliderGrab] = Slider::Grab;
        style.Colors[ImGuiCol_SliderGrabActive] = Slider::GrabActive;
        
        // Frames
        style.Colors[ImGuiCol_FrameBg] = Frame::Bg;
        style.Colors[ImGuiCol_FrameBgHovered] = Frame::BgHovered;
        style.Colors[ImGuiCol_FrameBgActive] = Frame::BgActive;
        
        // Text
        style.Colors[ImGuiCol_Text] = Text::Default;
        style.Colors[ImGuiCol_TextDisabled] = Text::Disabled;
        
        // Borders
        style.Colors[ImGuiCol_Border] = Border::Default;
        style.Colors[ImGuiCol_BorderShadow] = Border::Shadow;
    }
    
} // namespace GUIConstants

