//
//  GUIConstants.h
//  Centralized constants management for the videoTracker GUI
//
//  All GUI colors and constants are defined here for easy editing and theme management.
//  Colors and constants are organized by semantic meaning (active states, backgrounds, etc.)
//

#pragma once

#include <imgui.h>

namespace GUIConstants {
    
    // ============================================================================
    // BASE THEME COLORS
    // ============================================================================
    
    // Background colors
    namespace Background {
        constexpr ImVec4 Window = ImVec4(0.15f, 0.15f, 0.15f, 0.4f);
        constexpr ImVec4 Child = ImVec4(0.01f, 0.01f, 0.01f, 0.6f);
        constexpr ImVec4 Popup = ImVec4(0.1f, 0.1f, 0.1f, 0.95f);
        constexpr ImVec4 MenuBar = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);
        constexpr ImVec4 Title = ImVec4(0.01f, 0.01f, 0.01f, 0.65f);
        constexpr ImVec4 TitleActive = ImVec4(0.01f, 0.01f, 0.01f, 0.65f);
        constexpr ImVec4 DockingEmpty = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        constexpr ImVec4 DockingPreview = ImVec4(0.9f, 0.1f, 0.1f, 0.4f); // Blue preview when docking, matches Button::Hovered theme
        constexpr ImVec4 ModalDim = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
        
        // Table/Grid backgrounds
        constexpr ImVec4 TableHeader = ImVec4(0.01f, 0.01f, 0.01f, 0.8f);
        constexpr ImVec4 TableRow = ImVec4(0.0f, 0.0f, 0.0f, 0.05f);
        constexpr ImVec4 TableRowAlt = ImVec4(0.0f, 0.0f, 0.0f, 0.1f);
        constexpr ImVec4 TableRowFilled = ImVec4(0.01f, 0.01f, 0.01f, 0.5f);
        constexpr ImVec4 TableRowEmpty = ImVec4(0.05f, 0.05f, 0.05f, 0.05f); // Reduced opacity for empty rows
        constexpr ImVec4 StepNumber = ImVec4(0.05f, 0.05f, 0.05f, 0.8f);
        
        // Waveform
        constexpr ImVec4 Waveform = ImVec4(0.0f, 0.0f, 0.0f, 0.55f); // ~100/255 alpha
        constexpr ImVec4 WaveformTrimmed = ImVec4(0.4f, 0.4f, 0.4f, 0.36f); // Grey, semi-transparent
    }
    
    // Text colors
    namespace Text {
        constexpr ImVec4 Default = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
        constexpr ImVec4 Disabled = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        constexpr ImVec4 Playing = ImVec4(0.7f, 1.0f, 0.7f, 1.0f); // Light green
        constexpr ImVec4 Warning = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
        constexpr ImVec4 Info = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); // Grey
    }
    
    // Border colors
    namespace Border {
        constexpr ImVec4 Default = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);
        constexpr ImVec4 Strong = ImVec4(0.1f, 0.1f, 0.1f, 0.8f);
        constexpr ImVec4 Light = ImVec4(0.4f, 0.4f, 0.4f, 0.6f);
        constexpr ImVec4 SearchBar = ImVec4(0.59f, 0.59f, 0.59f, 1.0f); // ~150/255
        constexpr ImVec4 Shadow = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // No shadow
    }
    
    // ============================================================================
    // ACTIVE/PLAYBACK STATE COLORS
    // ============================================================================
    
    namespace Active {
        // Active step colors (green theme)
        constexpr ImVec4 StepBright = ImVec4(0.0f, 0.85f, 0.0f, 0.5f); // Bright green when playing
        constexpr ImVec4 StepDim = ImVec4(0.2f, 0.7f, 0.2f, 0.2f); // Dim green for inactive playback step
        constexpr ImVec4 StepButton = ImVec4(0.2f, 0.7f, 0.2f, 0.8f); // Green button when active
        constexpr ImVec4 StepButtonHover = ImVec4(0.25f, 0.75f, 0.25f, 0.9f); // Brighter green on hover
        
        // Active media/item colors (blue theme)
        constexpr ImVec4 MediaItem = ImVec4(0.1f, 0.1f, 0.9f, 0.8f); // Blue for active media
        constexpr ImVec4 Pattern = ImVec4(0.1f, 0.1f, 0.9f, 0.8f); // Blue for current pattern
        constexpr ImVec4 PatternPlaying = ImVec4(0.0f, 0.9f, 0.0f, 0.6f); // Bright green when playing
        
        // Chain entry colors
        constexpr ImVec4 ChainEntry = ImVec4(0.4f, 0.4f, 0.4f, 1.0f); // Grey for current chain entry
        constexpr ImVec4 ChainEntryBorder = ImVec4(0.8f, 0.8f, 0.8f, 1.0f); // Light grey border
        constexpr ImVec4 ChainEntryInactive = ImVec4(0.25f, 0.25f, 0.25f, 1.0f); // Dark for inactive chain entries
    }
    
    // ============================================================================
    // WAVEFORM MARKER COLORS
    // ============================================================================
    
    namespace Waveform {
        constexpr ImVec4 Line = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // White waveform lines
        constexpr ImVec4 Playhead = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green playhead
        constexpr ImVec4 RegionStart = ImVec4(0.71f, 0.71f, 0.71f, 1.0f); // Medium grey (~180/255)
        constexpr ImVec4 RegionEnd = ImVec4(0.71f, 0.71f, 0.71f, 1.0f); // Medium grey
        constexpr ImVec4 Position = ImVec4(0.47f, 0.47f, 0.47f, 1.0f); // Darker grey (~120/255)
        constexpr ImVec4 LoopRange = ImVec4(0.39f, 0.20f, 0.78f, 0.3f); // Purple overlay for loop range (~30% opacity)
        constexpr ImVec4 LoopRangeBorder = ImVec4(0.59f, 0.39f, 1.0f, 0.59f); // Brighter purple border for loop range
    }
    
    // ============================================================================
    // OUTLINE/INDICATOR COLORS AND CONSTANTS
    // ============================================================================
    
    namespace Outline {
        // Selection outlines
        constexpr ImVec4 Red = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red outline for selected
        constexpr ImVec4 Orange = ImVec4(1.0f, 0.5f, 0.0f, 1.0f); // Orange outline when editing
        constexpr ImVec4 RedDim = ImVec4(0.9f, 0.05f, 0.1f, 1.0f); // Slightly dimmer red
        
        // Focus outline for GUI panels
        constexpr ImVec4 Focus = ImVec4(0.4f, 0.4f, 0.4f, 1.0f); // Grey outline for focused panels
        constexpr float FocusThickness = 1.0f; // Thickness for focused window outline
        
        // Unfocused outline for GUI panels
        constexpr ImVec4 Unfocused = ImVec4(0.2f, 0.2f, 0.2f, 0.6f); // Dimmer grey outline for unfocused panels
        constexpr float UnfocusedThickness = 0.5f; // Thinner thickness for unfocused window outline
        
        // Disabled state
        constexpr ImVec4 Disabled = ImVec4(0.8f, 0.2f, 0.2f, 1.0f); // Red line for disabled
        constexpr ImVec4 DisabledBg = ImVec4(0.4f, 0.2f, 0.2f, 1.0f); // Red tint background
    }
    
    // ============================================================================
    // BUTTON COLORS
    // ============================================================================
    
    namespace Button {
        constexpr ImVec4 Default = ImVec4(0.3f, 0.3f, 0.3f, 0.8f);
        constexpr ImVec4 Hovered = ImVec4(0.1f, 0.1f, 0.9f, 0.9f); // Blue hover
        constexpr ImVec4 Active = ImVec4(0.04f, 0.04f, 0.04f, 1.0f);
        constexpr ImVec4 EditMode = ImVec4(0.0f, 0.0f, 0.0f, 0.8f); // Black in edit mode
        constexpr ImVec4 EditModeHover = ImVec4(0.05f, 0.05f, 0.05f, 0.8f);
        constexpr ImVec4 EditModeActive = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
        constexpr ImVec4 Transparent = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    
    // ============================================================================
    // SLIDER/FRAME COLORS
    // ============================================================================
    
    namespace Slider {
        constexpr ImVec4 Grab = ImVec4(0.5f, 0.5f, 0.5f, 0.8f);
        constexpr ImVec4 GrabActive = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    }
    
    namespace Frame {
        constexpr ImVec4 Bg = ImVec4(0.03f, 0.03f, 0.03f, 0.75f);
        constexpr ImVec4 BgHovered = ImVec4(0.2f, 0.2f, 0.8f, 0.8f); // Blue hover
        constexpr ImVec4 BgActive = ImVec4(0.15f, 0.15f, 0.15f, 0.9f);
        constexpr ImVec4 ChainEntry = ImVec4(0.4f, 0.4f, 0.4f, 1.0f); // For repeat count cells
    }
    
    // ============================================================================
    // SCROLLBAR COLORS
    // ============================================================================
    
    namespace Scrollbar {
        constexpr ImVec4 Bg = ImVec4(0.1f, 0.1f, 0.1f, 0.8f);
        constexpr ImVec4 Grab = ImVec4(0.3f, 0.3f, 0.3f, 0.8f);
        constexpr ImVec4 GrabHovered = ImVec4(0.4f, 0.4f, 0.4f, 0.9f);
        constexpr ImVec4 GrabActive = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        constexpr float Size = 5.0f; // Scrollbar width (default ImGui is 16.0f)
    }
    
    // ============================================================================
    // TAB COLORS
    // ============================================================================
    
    namespace Tab {
        constexpr ImVec4 Default = ImVec4(0.01f, 0.01f, 0.01f, 0.8f);
        constexpr ImVec4 Hovered = ImVec4(0.4f, 0.4f, 0.4f, 0.6f);
        constexpr ImVec4 Active = ImVec4(0.01f, 0.01f, 0.01f, 0.8f);
        constexpr ImVec4 Unfocused = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
        constexpr ImVec4 UnfocusedActive = ImVec4(0.01f, 0.01f, 0.01f, 0.5f);
    }
    
    // ============================================================================
    // SEPARATOR COLORS
    // ============================================================================
    
    namespace Separator {
        constexpr ImVec4 Default = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);
        constexpr ImVec4 Hovered = ImVec4(0.3f, 0.3f, 0.3f, 0.9f);
        constexpr ImVec4 Active = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    }
    
    // ============================================================================
    // DOCKING APPEARANCE
    // ============================================================================
    //
    // IMPORTANT: When windows are docked vs undocked, ImGui uses different rendering:
    //
    // UNDOCKED (Floating) Windows:
    //   - Use Title Bar: ImGuiCol_TitleBg, ImGuiCol_TitleBgActive
    //   - Show window borders and resize grips
    //   - Window background: ImGuiCol_WindowBg
    //
    // DOCKED Windows:
    //   - Use Tabs: ImGuiCol_Tab, ImGuiCol_TabActive, ImGuiCol_TabHovered, etc.
    //   - Tab bar replaces title bar when multiple windows are docked together
    //   - Window background: ImGuiCol_WindowBg (same as undocked)
    //   - Separators between docked panels use DockingSeparatorSize
    //
    // To make docked and undocked windows look similar:
    //   - Match Tab colors to TitleBg colors
    //   - Or use IsWindowDocked() to conditionally style
    //
    namespace Docking {
        // Thickness of separators between docked panels (default ImGui is 2.0f)
        // Smaller values = thinner separators
        constexpr float SeparatorSize = 0.75f;  // Thinner separators for cleaner look
    
        // Note: Docking::WindowBg actually doesn't affect the docked panel windowbg.
        // It is left here for documentation/reference only.
        constexpr ImVec4 WindowBg = ImVec4(0.15f, 0.15f, 0.15f, 0.0f);  // Same as Background::Window by default
        
    }
    
    // ============================================================================
    // HEADER COLORS
    // ============================================================================
    
    namespace Header {
        constexpr ImVec4 Default = ImVec4(0.1f, 0.1f, 0.1f, 0.8f);
    }
    
    // ============================================================================
    // FILE BROWSER COLORS
    // ============================================================================
    
    namespace FileBrowser {
        // Selection highlight colors - brighter and more visible
        constexpr ImVec4 Selected = ImVec4(0.2f, 0.4f, 0.8f, 0.6f); // Bright blue selection
        constexpr ImVec4 SelectedHovered = ImVec4(0.25f, 0.45f, 0.85f, 0.7f); // Slightly brighter on hover
    }
    
    // ============================================================================
    // RESIZE GRIP COLORS 
    // ============================================================================
    
    namespace ResizeGrip {
        constexpr ImVec4 Default = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);
    }
    
    // ============================================================================
    // PARAMETER CELL COLORS
    // ============================================================================
    
    namespace CellWidget {
        constexpr ImVec4 FillBar = ImVec4(0.5f, 0.5f, 0.5f, 0.25f); // Grey fill bar
    }
    
    // ============================================================================
    // PLOT/VISUALIZATION COLORS
    // ============================================================================
    
    namespace Plot {
        constexpr ImVec4 Histogram = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green for audio level
    }
    
    // ============================================================================
    // CLOCK VISUALIZER COLORS
    // ============================================================================
    
    namespace Clock {
        // Pulse color is dynamic based on beatPulse value, but base is white when playing
        constexpr ImVec4 PulseBase = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        constexpr ImVec4 PulseStopped = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    
    // ============================================================================
    // UTILITY FUNCTIONS
    // ============================================================================
    
    // Convert ImVec4 to ImU32 (for ImDrawList)
    inline ImU32 toU32(const ImVec4& color) {
        return ImGui::ColorConvertFloat4ToU32(color);
    }
    
    // Convert ImVec4 to IM_COL32 format (for direct use)
    inline ImU32 toIM_COL32(const ImVec4& color) {
        return IM_COL32(
            (int)(color.x * 255.0f),
            (int)(color.y * 255.0f),
            (int)(color.z * 255.0f),
            (int)(color.w * 255.0f)
        );
    }
    
    // Apply all ImGui style colors (call this in setupGUI)
    void applyImGuiStyle();
    
} // namespace GUIConstants

