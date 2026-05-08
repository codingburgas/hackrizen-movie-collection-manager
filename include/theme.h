#pragma once

#include <imgui.h>

namespace mcm::presentation::theme {

/**
 * Palette - cinematic color set used by the custom-drawn cards/charts/glass
 * panels. Lives alongside the ImGui style; switching themes also swaps
 * the active palette so subsequent draws pick up matching colors.
 */
struct Palette {
    ImU32 bgGradTop;
    ImU32 bgGradBot;
    ImU32 cardFill;
    ImU32 cardHighlight;
    ImU32 cardShadow;
    ImU32 panelBorder;
    ImU32 accentA;            // gradient start (warmer)
    ImU32 accentB;            // gradient end   (lighter)
    ImU32 statusWatchlist;
    ImU32 statusWatched;
    ImU32 statusOwned;
    ImU32 connectionOnline;
    ImU32 connectionOffline;
    ImU32 connectionPending;
    ImU32 textPrimary;
    ImU32 textDim;
    ImU32 trackFill;          // unfilled portion of bars/sliders
    bool  isDark;
};

void loadFonts(float pixelSize = 17.0f);

void applyLightOrange();

void applyDarkTheme();

const Palette& currentPalette();

} // namespace mcm::presentation::theme
