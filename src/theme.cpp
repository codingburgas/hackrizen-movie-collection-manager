#include "theme.h"

#include <fstream>
#include <iostream>

#include <imgui.h>

namespace mcm::presentation::theme {

namespace {

constexpr const char* kBoldFontPath = "assets/fonts/Inter-Bold.ttf";

ImVec4 rgba(float r, float g, float b, float a = 1.0f) {
    return ImVec4(r, g, b, a);
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

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    const ImVec4 white         = rgba(1.00f, 1.00f, 1.00f);
    const ImVec4 nearWhite     = rgba(0.97f, 0.97f, 0.98f);
    const ImVec4 lightGrey     = rgba(0.92f, 0.92f, 0.94f);
    const ImVec4 frameGrey     = rgba(0.96f, 0.96f, 0.97f);
    const ImVec4 frameGreyHov  = rgba(0.92f, 0.92f, 0.94f);
    const ImVec4 frameGreyAct  = rgba(0.88f, 0.88f, 0.90f);
    const ImVec4 borderGrey    = rgba(0.82f, 0.82f, 0.85f);
    const ImVec4 separatorGrey = rgba(0.85f, 0.85f, 0.88f);
    const ImVec4 darkText      = rgba(0.10f, 0.10f, 0.12f);
    const ImVec4 disabledText  = rgba(0.55f, 0.55f, 0.58f);

    const ImVec4 orange        = rgba(1.00f, 0.55f, 0.10f);
    const ImVec4 orangeHover   = rgba(1.00f, 0.65f, 0.22f);
    const ImVec4 orangeActive  = rgba(0.85f, 0.42f, 0.05f);
    const ImVec4 orangeTintLo  = rgba(1.00f, 0.85f, 0.65f);
    const ImVec4 orangeTintMid = rgba(1.00f, 0.75f, 0.45f);
    const ImVec4 cream         = rgba(0.95f, 0.92f, 0.88f);

    c[ImGuiCol_Text]                  = darkText;
    c[ImGuiCol_TextDisabled]          = disabledText;

    c[ImGuiCol_WindowBg]              = white;
    c[ImGuiCol_ChildBg]               = white;
    c[ImGuiCol_PopupBg]               = white;
    c[ImGuiCol_MenuBarBg]             = white;

    c[ImGuiCol_Border]                = borderGrey;
    c[ImGuiCol_BorderShadow]          = rgba(0.0f, 0.0f, 0.0f, 0.0f);

    c[ImGuiCol_FrameBg]               = frameGrey;
    c[ImGuiCol_FrameBgHovered]        = frameGreyHov;
    c[ImGuiCol_FrameBgActive]         = frameGreyAct;

    c[ImGuiCol_TitleBg]               = nearWhite;
    c[ImGuiCol_TitleBgActive]         = lightGrey;
    c[ImGuiCol_TitleBgCollapsed]      = nearWhite;

    c[ImGuiCol_ScrollbarBg]           = nearWhite;
    c[ImGuiCol_ScrollbarGrab]         = rgba(0.78f, 0.78f, 0.81f);
    c[ImGuiCol_ScrollbarGrabHovered]  = orangeTintMid;
    c[ImGuiCol_ScrollbarGrabActive]   = orange;

    c[ImGuiCol_CheckMark]             = orangeActive;
    c[ImGuiCol_SliderGrab]            = orange;
    c[ImGuiCol_SliderGrabActive]      = orangeActive;

    c[ImGuiCol_Button]                = orange;
    c[ImGuiCol_ButtonHovered]         = orangeHover;
    c[ImGuiCol_ButtonActive]          = orangeActive;

    c[ImGuiCol_Header]                = lightGrey;
    c[ImGuiCol_HeaderHovered]         = orangeTintLo;
    c[ImGuiCol_HeaderActive]          = orangeTintMid;

    c[ImGuiCol_Separator]             = separatorGrey;
    c[ImGuiCol_SeparatorHovered]      = orangeHover;
    c[ImGuiCol_SeparatorActive]       = orange;

    c[ImGuiCol_ResizeGrip]            = orangeTintLo;
    c[ImGuiCol_ResizeGripHovered]     = orangeHover;
    c[ImGuiCol_ResizeGripActive]      = orangeActive;

    c[ImGuiCol_Tab]                   = lightGrey;
    c[ImGuiCol_TabHovered]            = orangeHover;
    c[ImGuiCol_TabActive]             = orange;
    c[ImGuiCol_TabUnfocused]          = nearWhite;
    c[ImGuiCol_TabUnfocusedActive]    = lightGrey;

    c[ImGuiCol_TableHeaderBg]         = cream;
    c[ImGuiCol_TableBorderStrong]     = borderGrey;
    c[ImGuiCol_TableBorderLight]      = separatorGrey;
    c[ImGuiCol_TableRowBg]            = white;
    c[ImGuiCol_TableRowBgAlt]         = nearWhite;

    c[ImGuiCol_TextSelectedBg]        = orangeTintLo;
    c[ImGuiCol_NavHighlight]          = orange;
    c[ImGuiCol_DragDropTarget]        = orangeActive;

    style.WindowRounding    = 6.0f;
    style.PopupRounding     = 4.0f;
    style.FrameRounding     = 4.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;
    style.ScrollbarRounding = 4.0f;

    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.FramePadding      = ImVec2(8.0f, 5.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);

    style.FrameBorderSize   = 1.0f;
    style.WindowBorderSize  = 1.0f;
}

} // namespace mcm::presentation::theme
