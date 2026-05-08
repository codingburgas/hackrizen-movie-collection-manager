#include "theme.h"

#include <fstream>
#include <iostream>

#include <imgui.h>

namespace mcm::presentation::theme {

namespace {

constexpr const char* kBoldFontPath = "assets/fonts/Inter-Bold.ttf";

ImVec4 v4(float r, float g, float b, float a = 1.0f) {
    return ImVec4(r, g, b, a);
}

ImVec4 fromU32(ImU32 c) {
    return ImGui::ColorConvertU32ToFloat4(c);
}

Palette g_palette = {};

void applyCommonStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 10.0f;
    s.ChildRounding     = 12.0f;
    s.PopupRounding     = 8.0f;
    s.FrameRounding     = 8.0f;
    s.GrabRounding      = 6.0f;
    s.TabRounding       = 8.0f;
    s.ScrollbarRounding = 6.0f;
    s.WindowPadding     = ImVec2(14.0f, 12.0f);
    s.FramePadding      = ImVec2(10.0f, 7.0f);
    s.ItemSpacing       = ImVec2(10.0f, 8.0f);
    s.ItemInnerSpacing  = ImVec2(8.0f, 6.0f);
    s.IndentSpacing     = 18.0f;
    s.ScrollbarSize     = 12.0f;
    s.GrabMinSize       = 12.0f;
    s.WindowBorderSize  = 0.0f;
    s.ChildBorderSize   = 0.0f;
    s.FrameBorderSize   = 0.0f;
    s.PopupBorderSize   = 1.0f;
}

} // namespace

void loadFonts(float pixelSize) {
    ImGuiIO& io = ImGui::GetIO();

    if (!std::ifstream(kBoldFontPath, std::ios::binary).good()) {
        std::cerr << "[theme] Font file '" << kBoldFontPath
                  << "' not found. Using default ImGui font.\n";
        io.Fonts->AddFontDefault();
        return;
    }

    ImFont* bold = io.Fonts->AddFontFromFileTTF(kBoldFontPath, pixelSize);
    if (bold == nullptr) {
        std::cerr << "[theme] Failed to parse '" << kBoldFontPath
                  << "'. Using default ImGui font.\n";
        io.Fonts->AddFontDefault();
        return;
    }
    io.FontDefault = bold;
}

void applyLightOrange() {
    ImGui::StyleColorsLight();

    Palette p;
    p.isDark            = false;
    p.bgGradTop         = IM_COL32(244, 244, 248, 255);
    p.bgGradBot         = IM_COL32(255, 255, 255, 255);
    p.cardFill          = IM_COL32(255, 255, 255, 240);
    p.cardHighlight     = IM_COL32(255, 255, 255, 220);
    p.cardShadow        = IM_COL32(20,  24,  40,  28);
    p.panelBorder       = IM_COL32(214, 218, 230, 255);
    p.accentA           = IM_COL32(255, 122, 26,  255);
    p.accentB           = IM_COL32(255, 157, 74,  255);
    p.statusWatchlist   = IM_COL32(217, 153, 13,  255);
    p.statusWatched     = IM_COL32(64,  140, 244, 255);
    p.statusOwned       = IM_COL32(38,  184, 77,  255);
    p.connectionOnline  = IM_COL32(38,  184, 77,  255);
    p.connectionOffline = IM_COL32(225, 65,  60,  255);
    p.connectionPending = IM_COL32(240, 165, 30,  255);
    p.textPrimary       = IM_COL32(22,  25,  34,  255);
    p.textDim           = IM_COL32(107, 112, 128, 255);
    p.trackFill         = IM_COL32(0,   0,   0,   24);
    g_palette = p;

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                  = fromU32(p.textPrimary);
    c[ImGuiCol_TextDisabled]          = fromU32(p.textDim);
    c[ImGuiCol_WindowBg]              = v4(0, 0, 0, 0); // transparent: bg is gradient
    c[ImGuiCol_ChildBg]               = v4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]               = fromU32(p.cardFill);
    c[ImGuiCol_MenuBarBg]             = fromU32(p.cardFill);
    c[ImGuiCol_Border]                = fromU32(p.panelBorder);
    c[ImGuiCol_BorderShadow]          = v4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = v4(0.96f, 0.96f, 0.97f, 1.0f);
    c[ImGuiCol_FrameBgHovered]        = v4(0.93f, 0.93f, 0.95f, 1.0f);
    c[ImGuiCol_FrameBgActive]         = v4(0.90f, 0.90f, 0.93f, 1.0f);
    c[ImGuiCol_TitleBg]               = fromU32(p.cardFill);
    c[ImGuiCol_TitleBgActive]         = fromU32(p.cardFill);
    c[ImGuiCol_TitleBgCollapsed]      = fromU32(p.cardFill);
    c[ImGuiCol_ScrollbarBg]           = v4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]         = v4(0.78f, 0.78f, 0.81f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered]  = fromU32(p.accentB);
    c[ImGuiCol_ScrollbarGrabActive]   = fromU32(p.accentA);
    c[ImGuiCol_CheckMark]             = fromU32(p.accentA);
    c[ImGuiCol_SliderGrab]            = fromU32(p.accentA);
    c[ImGuiCol_SliderGrabActive]      = fromU32(p.accentB);
    c[ImGuiCol_Button]                = v4(0.94f, 0.94f, 0.96f, 1.0f);
    c[ImGuiCol_ButtonHovered]         = v4(0.88f, 0.88f, 0.91f, 1.0f);
    c[ImGuiCol_ButtonActive]          = v4(0.82f, 0.82f, 0.85f, 1.0f);
    c[ImGuiCol_Header]                = v4(0.93f, 0.93f, 0.95f, 1.0f);
    c[ImGuiCol_HeaderHovered]         = v4(1.00f, 0.85f, 0.65f, 1.0f);
    c[ImGuiCol_HeaderActive]          = v4(1.00f, 0.75f, 0.45f, 1.0f);
    c[ImGuiCol_Separator]             = fromU32(p.panelBorder);
    c[ImGuiCol_SeparatorHovered]      = fromU32(p.accentB);
    c[ImGuiCol_SeparatorActive]       = fromU32(p.accentA);
    c[ImGuiCol_ResizeGrip]            = v4(0.85f, 0.85f, 0.88f, 0.6f);
    c[ImGuiCol_ResizeGripHovered]     = fromU32(p.accentB);
    c[ImGuiCol_ResizeGripActive]      = fromU32(p.accentA);
    c[ImGuiCol_Tab]                   = v4(0.93f, 0.93f, 0.95f, 1.0f);
    c[ImGuiCol_TabHovered]            = fromU32(p.accentB);
    c[ImGuiCol_TabActive]             = fromU32(p.accentA);
    c[ImGuiCol_TabUnfocused]          = v4(0.96f, 0.96f, 0.97f, 1.0f);
    c[ImGuiCol_TabUnfocusedActive]    = v4(0.93f, 0.93f, 0.95f, 1.0f);
    c[ImGuiCol_TableHeaderBg]         = v4(0.96f, 0.94f, 0.92f, 1.0f);
    c[ImGuiCol_TableBorderStrong]     = fromU32(p.panelBorder);
    c[ImGuiCol_TableBorderLight]      = v4(0.92f, 0.92f, 0.94f, 1.0f);
    c[ImGuiCol_TableRowBg]            = v4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]         = v4(0, 0, 0, 0.025f);
    c[ImGuiCol_TextSelectedBg]        = v4(1.00f, 0.85f, 0.65f, 1.0f);
    c[ImGuiCol_NavHighlight]          = fromU32(p.accentA);
    c[ImGuiCol_DragDropTarget]        = fromU32(p.accentA);

    applyCommonStyle();
}

void applyDarkTheme() {
    ImGui::StyleColorsDark();

    Palette p;
    p.isDark            = true;
    p.bgGradTop         = IM_COL32(14,  16,  20,  255);
    p.bgGradBot         = IM_COL32(22,  25,  34,  255);
    p.cardFill          = IM_COL32(27,  31,  42,  235);
    p.cardHighlight     = IM_COL32(255, 255, 255, 14);
    p.cardShadow        = IM_COL32(0,   0,   0,   90);
    p.panelBorder       = IM_COL32(48,  53,  68,  255);
    p.accentA           = IM_COL32(255, 138, 31,  255);
    p.accentB           = IM_COL32(255, 184, 107, 255);
    p.statusWatchlist   = IM_COL32(241, 184, 61,  255);
    p.statusWatched     = IM_COL32(99,  165, 255, 255);
    p.statusOwned       = IM_COL32(58,  211, 110, 255);
    p.connectionOnline  = IM_COL32(58,  211, 110, 255);
    p.connectionOffline = IM_COL32(231, 78,  72,  255);
    p.connectionPending = IM_COL32(245, 178, 60,  255);
    p.textPrimary       = IM_COL32(236, 238, 245, 255);
    p.textDim           = IM_COL32(124, 129, 148, 255);
    p.trackFill         = IM_COL32(255, 255, 255, 22);
    g_palette = p;

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                  = fromU32(p.textPrimary);
    c[ImGuiCol_TextDisabled]          = fromU32(p.textDim);
    c[ImGuiCol_WindowBg]              = v4(0, 0, 0, 0);
    c[ImGuiCol_ChildBg]               = v4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]               = fromU32(p.cardFill);
    c[ImGuiCol_MenuBarBg]             = fromU32(p.cardFill);
    c[ImGuiCol_Border]                = fromU32(p.panelBorder);
    c[ImGuiCol_BorderShadow]          = v4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = v4(0.16f, 0.18f, 0.23f, 1.0f);
    c[ImGuiCol_FrameBgHovered]        = v4(0.22f, 0.24f, 0.31f, 1.0f);
    c[ImGuiCol_FrameBgActive]         = v4(0.28f, 0.30f, 0.39f, 1.0f);
    c[ImGuiCol_TitleBg]               = fromU32(p.cardFill);
    c[ImGuiCol_TitleBgActive]         = fromU32(p.cardFill);
    c[ImGuiCol_TitleBgCollapsed]      = fromU32(p.cardFill);
    c[ImGuiCol_ScrollbarBg]           = v4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]         = v4(0.30f, 0.32f, 0.40f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered]  = fromU32(p.accentB);
    c[ImGuiCol_ScrollbarGrabActive]   = fromU32(p.accentA);
    c[ImGuiCol_CheckMark]             = fromU32(p.accentA);
    c[ImGuiCol_SliderGrab]            = fromU32(p.accentA);
    c[ImGuiCol_SliderGrabActive]      = fromU32(p.accentB);
    c[ImGuiCol_Button]                = v4(0.20f, 0.22f, 0.28f, 1.0f);
    c[ImGuiCol_ButtonHovered]         = v4(0.27f, 0.29f, 0.38f, 1.0f);
    c[ImGuiCol_ButtonActive]          = v4(0.33f, 0.36f, 0.46f, 1.0f);
    c[ImGuiCol_Header]                = v4(0.22f, 0.24f, 0.31f, 1.0f);
    c[ImGuiCol_HeaderHovered]         = v4(0.55f, 0.30f, 0.05f, 0.55f);
    c[ImGuiCol_HeaderActive]          = v4(0.65f, 0.35f, 0.05f, 0.80f);
    c[ImGuiCol_Separator]             = fromU32(p.panelBorder);
    c[ImGuiCol_SeparatorHovered]      = fromU32(p.accentB);
    c[ImGuiCol_SeparatorActive]       = fromU32(p.accentA);
    c[ImGuiCol_ResizeGrip]            = v4(0.30f, 0.32f, 0.40f, 0.6f);
    c[ImGuiCol_ResizeGripHovered]     = fromU32(p.accentB);
    c[ImGuiCol_ResizeGripActive]      = fromU32(p.accentA);
    c[ImGuiCol_Tab]                   = v4(0.18f, 0.20f, 0.26f, 1.0f);
    c[ImGuiCol_TabHovered]            = fromU32(p.accentB);
    c[ImGuiCol_TabActive]             = fromU32(p.accentA);
    c[ImGuiCol_TabUnfocused]          = v4(0.14f, 0.16f, 0.21f, 1.0f);
    c[ImGuiCol_TabUnfocusedActive]    = v4(0.18f, 0.20f, 0.26f, 1.0f);
    c[ImGuiCol_TableHeaderBg]         = v4(0.16f, 0.18f, 0.23f, 1.0f);
    c[ImGuiCol_TableBorderStrong]     = fromU32(p.panelBorder);
    c[ImGuiCol_TableBorderLight]      = v4(0.20f, 0.22f, 0.28f, 1.0f);
    c[ImGuiCol_TableRowBg]            = v4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]         = v4(1.0f, 1.0f, 1.0f, 0.018f);
    c[ImGuiCol_TextSelectedBg]        = v4(0.55f, 0.30f, 0.05f, 0.55f);
    c[ImGuiCol_NavHighlight]          = fromU32(p.accentA);
    c[ImGuiCol_DragDropTarget]        = fromU32(p.accentA);

    applyCommonStyle();
}

const Palette& currentPalette() {
    return g_palette;
}

} // namespace mcm::presentation::theme
