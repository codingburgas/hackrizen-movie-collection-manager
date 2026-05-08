/**
 * Presentation Layer implementation - Dear ImGui front-end.
 *
 * Cinematic dashboard layout: app bar with connection chip and theme/settings,
 * hero metric strip with animated counters and sparklines, eased filter
 * drawer, chart row (donut + genre bars), polished movie table, and a glass
 * status strip. All rendering uses free functions on the UiState struct;
 * no classes are introduced.
 */
#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#endif

#include "presentation.h"
#include "theme.h"
#include "ui_anim.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

namespace mcm::presentation {

namespace {

constexpr int WINDOW_WIDTH  = 1400;
constexpr int WINDOW_HEIGHT = 860;

GLFWwindow* windowHandle = nullptr;

// ──────────────────────────────────────────────────────────────────────────────
//  Small helpers
// ──────────────────────────────────────────────────────────────────────────────
void copyString(char* dst, std::size_t cap, const std::string& src) {
    const std::size_t len = std::min(src.size(), cap - 1);
    std::memcpy(dst, src.data(), len);
    dst[len] = '\0';
}

data::Movie makeMovieFromForm(const UiState& state) {
    data::Movie movie;
    movie.id              = state.editingId;
    movie.title           = state.formTitle;
    movie.director        = state.formDirector;
    movie.genres          = state.formGenres;
    movie.notes           = state.formNotes;
    movie.year            = state.formYear;
    movie.rating          = state.formRating;
    movie.durationMinutes = state.formDuration;
    movie.status          = static_cast<data::Status>(state.formStatus);
    movie.favorite        = state.formFavorite;
    return movie;
}

void resetForm(UiState& state) {
    state.formTitle[0]    = '\0';
    state.formDirector[0] = '\0';
    state.formGenres[0]   = '\0';
    state.formNotes[0]    = '\0';
    state.formYear     = 2024;
    state.formRating   = 7.5f;
    state.formDuration = 120;
    state.formStatus   = 0;
    state.formFavorite = false;
    state.editingId    = 0;
}

bool sendRequest(network::Client& client, const protocol::Message& message) {
    return network::sendClientMessage(client, protocol::encodeMessage(message));
}

void refreshTotalDuration(UiState& state) {
    const std::vector<data::Movie> snapshot = logic::snapshotCollection(state.localCollection);
    std::vector<std::uint64_t> selected(state.selectedIds.begin(), state.selectedIds.end());
    state.totalSelectedMinutes = logic::totalDurationRecursive(snapshot, selected, 0);
}

ImU32 statusColor(data::Status status) {
    const auto& p = theme::currentPalette();
    switch (status) {
        case data::Status::WATCHLIST: return p.statusWatchlist;
        case data::Status::WATCHED:   return p.statusWatched;
        case data::Status::OWNED:     return p.statusOwned;
    }
    return p.textPrimary;
}

ImVec4 statusTextVec(data::Status status) {
    return ImGui::ColorConvertU32ToFloat4(statusColor(status));
}

// Export the current filtered+sorted snapshot to a CSV file.
void exportToCsv(const UiState& state) {
    const std::vector<data::Movie> snapshot = logic::snapshotCollection(state.localCollection);

    logic::FilterCriteria criteria;
    criteria.showWatchlist  = state.showWatchlist;
    criteria.showWatched    = state.showWatched;
    criteria.showOwned      = state.showOwned;
    criteria.favoritesOnly  = state.favoritesOnly;
    criteria.titleSubstring = state.searchBuffer;
    criteria.genreFilter    = state.genreFilter;
    criteria.directorFilter = state.directorFilter;
    criteria.minRating      = state.minRatingFilter;
    criteria.maxRating      = state.maxRatingFilter;
    criteria.minYear        = state.minYearFilter;
    criteria.maxYear        = state.maxYearFilter;

    std::vector<data::Movie> movies = logic::applyFilters(snapshot, criteria);
    logic::sortMovies(movies, state.sortKey, state.sortOrder);

    static const char* STATUS_LABELS[] = {"Watchlist", "Watched", "Owned"};

    std::ofstream file("movie_collection_export.csv");
    file << "ID,Title,Director,Genres,Year,Rating,Duration (min),Status,Favorite,Notes\n";
    for (const auto& m : movies) {
        const int si = static_cast<int>(m.status);
        file << m.id << ","
             << "\"" << m.title    << "\","
             << "\"" << m.director << "\","
             << "\"" << m.genres   << "\","
             << m.year << ","
             << m.rating << ","
             << m.durationMinutes << ","
             << STATUS_LABELS[si >= 0 && si <= 2 ? si : 0] << ","
             << (m.favorite ? "Yes" : "No") << ","
             << "\"" << m.notes << "\"\n";
        }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Per-frame animation update — recomputes targets from real data
// ──────────────────────────────────────────────────────────────────────────────
void tickAnimation(UiState& state, float dt,
                   const logic::StatusCounts& counts,
                   float avg, float best, long long totalMins,
                   const std::vector<data::Movie>& snap) {
    AnimState& a = state.anim;
    const int total = counts.watchlist + counts.watched + counts.owned;

    anim::setEased(a.totalCount,      static_cast<float>(total));
    anim::setEased(a.watchlistCount,  static_cast<float>(counts.watchlist));
    anim::setEased(a.watchedCount,    static_cast<float>(counts.watched));
    anim::setEased(a.ownedCount,      static_cast<float>(counts.owned));
    anim::setEased(a.avgRating,       avg);
    anim::setEased(a.bestRating,      best);
    anim::setEased(a.totalHours,      static_cast<float>(totalMins) / 60.0f);

    const float wlF  = total > 0 ? static_cast<float>(counts.watchlist) / total : 0.0f;
    const float wdF  = total > 0 ? static_cast<float>(counts.watched)   / total : 0.0f;
    const float ownF = total > 0 ? static_cast<float>(counts.owned)     / total : 0.0f;
    anim::setEased(a.watchlistFraction, wlF);
    anim::setEased(a.watchedFraction,   wdF);
    anim::setEased(a.ownedFraction,     ownF);

    anim::setEased(a.filterDrawerHeight, state.showAdvancedFilters ? 110.0f : 0.0f);
    anim::setEased(a.statsHeight,        state.showStatsPanel ? 168.0f : 0.0f);
    anim::setEased(a.themeFade,          state.darkMode ? 1.0f : 0.0f);

    if (a.firstFrame) {
        anim::snapEased(a.totalCount,        static_cast<float>(total));
        anim::snapEased(a.watchlistCount,    static_cast<float>(counts.watchlist));
        anim::snapEased(a.watchedCount,      static_cast<float>(counts.watched));
        anim::snapEased(a.ownedCount,        static_cast<float>(counts.owned));
        anim::snapEased(a.avgRating,         avg);
        anim::snapEased(a.bestRating,        best);
        anim::snapEased(a.totalHours,        static_cast<float>(totalMins) / 60.0f);
        anim::snapEased(a.watchlistFraction, wlF);
        anim::snapEased(a.watchedFraction,   wdF);
        anim::snapEased(a.ownedFraction,     ownF);
        anim::snapEased(a.filterDrawerHeight, state.showAdvancedFilters ? 110.0f : 0.0f);
        anim::snapEased(a.statsHeight,        state.showStatsPanel ? 168.0f : 0.0f);
        anim::snapEased(a.themeFade,          state.darkMode ? 1.0f : 0.0f);
        a.firstFrame = false;
    }

    anim::tickEased(a.totalCount,        dt, 0.32f);
    anim::tickEased(a.watchlistCount,    dt, 0.32f);
    anim::tickEased(a.watchedCount,      dt, 0.32f);
    anim::tickEased(a.ownedCount,        dt, 0.32f);
    anim::tickEased(a.avgRating,         dt, 0.30f);
    anim::tickEased(a.bestRating,        dt, 0.30f);
    anim::tickEased(a.totalHours,        dt, 0.30f);
    anim::tickEased(a.watchlistFraction, dt, 0.28f);
    anim::tickEased(a.watchedFraction,   dt, 0.28f);
    anim::tickEased(a.ownedFraction,     dt, 0.28f);
    anim::tickEased(a.filterDrawerHeight, dt, 0.12f);
    anim::tickEased(a.statsHeight,        dt, 0.18f);
    anim::tickEased(a.themeFade,          dt, 0.20f);

    a.spinPhase += dt * 0.18f;
    if (a.spinPhase > 6.2831853f) a.spinPhase -= 6.2831853f;

    // Favorite-pulse decay + transition detection.
    for (auto it = a.favoritePulse.begin(); it != a.favoritePulse.end(); ) {
        it->second -= dt;
        if (it->second <= 0.0f) {
            it = a.favoritePulse.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& m : snap) {
        auto& last = a.lastFavorite[m.id];
        if (m.favorite && !last) {
            a.favoritePulse[m.id] = 0.45f;
        }
        last = m.favorite;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Draw the full-viewport background gradient (under the root window)
// ──────────────────────────────────────────────────────────────────────────────
void drawBackgroundGradient(const UiState& state) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* bg = ImGui::GetBackgroundDrawList();

    const auto& p = theme::currentPalette();
    // Cross-fade light vs dark gradient endpoints during theme toggle so the
    // viewport background eases between palettes instead of snapping.
    const float fade = state.anim.themeFade.current;
    static const ImU32 lightTop = IM_COL32(244, 244, 248, 255);
    static const ImU32 lightBot = IM_COL32(255, 255, 255, 255);
    static const ImU32 darkTop  = IM_COL32(14,  16,  20,  255);
    static const ImU32 darkBot  = IM_COL32(22,  25,  34,  255);
    const ImU32 top = anim::lerpColor(lightTop, darkTop, fade);
    const ImU32 bot = anim::lerpColor(lightBot, darkBot, fade);

    bg->AddRectFilledMultiColor(vp->WorkPos,
                                ImVec2(vp->WorkPos.x + vp->WorkSize.x,
                                       vp->WorkPos.y + vp->WorkSize.y),
                                top, top, bot, bot);

    // Subtle accent glow in the upper-left for cinematic depth.
    const ImVec2 glowCenter(vp->WorkPos.x + 220.0f, vp->WorkPos.y + 60.0f);
    const ImU32 glowA = anim::withAlpha(p.accentA, p.isDark ? 0.10f : 0.05f);
    const ImU32 glowB = anim::withAlpha(p.accentA, 0.0f);
    const int rings = 24;
    const float maxR = 380.0f;
    for (int i = 0; i < rings; ++i) {
        const float t = static_cast<float>(i) / rings;
        const float r = maxR * (1.0f - t);
        const ImU32 c = anim::lerpColor(glowA, glowB, t);
        bg->AddCircleFilled(glowCenter, r, c, 64);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Settings popup (host / port / auto-reconnect) — opened from the app bar
// ──────────────────────────────────────────────────────────────────────────────
void renderSettingsPopup(UiState& state, network::Client& client) {
    if (state.showSettingsPopup) {
        ImGui::OpenPopup("##settings");
        state.showSettingsPopup = false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 24.0f,
                                   vp->WorkPos.y + 60.0f),
                            ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
    if (ImGui::BeginPopup("##settings")) {
        ImGui::TextDisabled("CONNECTION");
        ImGui::Separator();
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputText("Host", state.hostBuffer, sizeof(state.hostBuffer));
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputText("Port", state.portBuffer, sizeof(state.portBuffer));
        ImGui::Checkbox("Auto-reconnect", &state.autoReconnect);
        ImGui::Separator();
        const bool online = client.connected.load();
        if (online) {
            if (ImGui::Button("Disconnect", ImVec2(220.0f, 0.0f))) {
                network::disconnectClient(client);
                state.statusMessage = "Disconnected.";
            }
        } else {
            if (ImGui::Button("Connect", ImVec2(220.0f, 0.0f))) {
                state.lastReconnectAttemptTime = glfwGetTime();
                if (network::connectClient(client, state.hostBuffer, state.portBuffer)) {
                    state.statusMessage = "Connected.";
                    protocol::Message sync;
                    sync.kind = protocol::MessageKind::REQUEST_SYNC;
                    sendRequest(client, sync);
                } else {
                    state.statusMessage = "Connect failed: " + network::lastClientError(client);
                }
            }
        }
        if (!online && state.autoReconnect) {
            const double now  = glfwGetTime();
            const double wait = 5.0 - (now - state.lastReconnectAttemptTime);
            if (wait > 0.0) {
                ImGui::TextDisabled("Auto-retry in %.0fs", wait);
            }
        }
        ImGui::EndPopup();
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  App bar — title, connection chip, theme toggle, settings
// ──────────────────────────────────────────────────────────────────────────────
void renderAppBar(UiState& state, network::Client& client, float dt) {
    const auto& p = theme::currentPalette();
    const float h = 48.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx(mn.x + ImGui::GetContentRegionAvail().x, mn.y + h);

    anim::drawGlassPanel(dl, mn, mx, p.cardFill, p.cardHighlight, p.panelBorder, 14.0f);

    // Title with accent gradient bar to the left.
    const ImVec2 accentMn(mn.x + 14.0f, mn.y + 12.0f);
    const ImVec2 accentMx(accentMn.x + 4.0f, mx.y - 12.0f);
    anim::drawVerticalGradient(dl, accentMn, accentMx, p.accentA, p.accentB, 2.0f);

    ImGui::SetCursorScreenPos(ImVec2(mn.x + 28.0f, mn.y + 14.0f));
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.accentA), "MOVIE");
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textPrimary), "COLLECTION");
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextDisabled("//");
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textDim), "cinematic");

    // Right side: settings + theme + connection chip (right-anchored).
    const bool online = client.connected.load();
    const float chipW = 142.0f;
    const float btnW  = 36.0f;
    const float gap   = 8.0f;
    const float rightX = mx.x - 16.0f - chipW - gap - btnW - gap - btnW;

    ImGui::SetCursorScreenPos(ImVec2(rightX, mn.y + 8.0f));
    ImGui::PushID("appbar_settings");
    if (ImGui::Button("...", ImVec2(btnW, 32.0f))) {
        state.showSettingsPopup = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Connection settings");
    }
    ImGui::PopID();
    ImGui::SameLine(0.0f, gap);

    const char* themeIcon = state.darkMode ? "Sun" : "Moon";
    if (ImGui::Button(themeIcon, ImVec2(btnW, 32.0f))) {
        state.darkMode = !state.darkMode;
        if (state.darkMode) theme::applyDarkTheme();
        else                theme::applyLightOrange();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(state.darkMode ? "Switch to light theme" : "Switch to dark theme");
    }
    ImGui::SameLine(0.0f, gap);

    // Connection chip — drawn manually for the colored dot + pulse.
    const ImVec2 chipMn = ImGui::GetCursorScreenPos();
    const ImVec2 chipMx(chipMn.x + chipW, chipMn.y + 32.0f);
    const ImU32 chipFill = anim::withAlpha(online ? p.connectionOnline : p.connectionOffline,
                                           p.isDark ? 0.18f : 0.14f);
    dl->AddRectFilled(chipMn, chipMx, chipFill, 16.0f);
    dl->AddRect(chipMn, chipMx,
                online ? p.connectionOnline : p.connectionOffline, 16.0f, 0, 1.2f);

    // Pulsing dot.
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(glfwGetTime()) * 3.0f);
    const ImU32 dotCore = online ? p.connectionOnline : p.connectionOffline;
    const ImU32 dotGlow = anim::withAlpha(dotCore, online ? (0.15f + 0.30f * pulse) : 0.25f);
    const ImVec2 dotCenter(chipMn.x + 16.0f, chipMn.y + 16.0f);
    dl->AddCircleFilled(dotCenter, 9.0f, dotGlow, 16);
    dl->AddCircleFilled(dotCenter, 5.0f, dotCore, 16);

    // Label.
    const char* label = online ? "ONLINE" : "OFFLINE";
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(chipMn.x + 30.0f, chipMn.y + (32.0f - ts.y) * 0.5f),
                p.textPrimary, label);

    // Reserve space for the bar.
    ImGui::SetCursorScreenPos(ImVec2(mn.x, mx.y + 8.0f));
    (void)dt;
}

// ──────────────────────────────────────────────────────────────────────────────
//  Hero metric strip — 4 animated cards
// ──────────────────────────────────────────────────────────────────────────────
struct MetricCard {
    const char* label;
    float       displayValue;
    int         decimals;
    const char* suffix;
    ImU32       accent;
    bool        useGradient;
    float       sparkSeed; // small variation so each sparkline differs
};

void renderHeroCard(UiState& state, ImVec2 mn, ImVec2 mx,
                    const MetricCard& mc, float dt) {
    const auto& p = theme::currentPalette();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Hover detection via an invisible button covering the card.
    ImGui::SetCursorScreenPos(mn);
    const ImVec2 size(mx.x - mn.x, mx.y - mn.y);
    char id[32];
    std::snprintf(id, sizeof(id), "##hero_%s", mc.label);
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const float hover01 = anim::tickHover(state.anim.hover, ImGui::GetID(id), hovered, dt);

    // Card geometry (with hover scale).
    const float scale = 1.0f + 0.018f * hover01;
    const ImVec2 cardCenter((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    const ImVec2 halfSz((mx.x - mn.x) * 0.5f * scale, (mx.y - mn.y) * 0.5f * scale);
    const ImVec2 cmn(cardCenter.x - halfSz.x, cardCenter.y - halfSz.y);
    const ImVec2 cmx(cardCenter.x + halfSz.x, cardCenter.y + halfSz.y);

    anim::drawCard(dl, cmn, cmx,
                   p.cardFill, p.cardHighlight, p.cardShadow, mc.accent,
                   12.0f, hover01);

    // Top strip / accent underline.
    const ImVec2 stripMn(cmn.x + 14.0f, cmx.y - 6.0f);
    const ImVec2 stripMx(cmx.x - 14.0f, cmx.y - 3.0f);
    if (mc.useGradient) {
        anim::drawHorizontalGradient(dl, stripMn, stripMx, p.accentA, p.accentB, 2.0f);
    } else {
        dl->AddRectFilled(stripMn, stripMx, mc.accent, 2.0f);
    }

    // Label (top-left).
    dl->AddText(ImVec2(cmn.x + 16.0f, cmn.y + 12.0f), p.textDim, mc.label);

    // Big value.
    char value[32];
    if (mc.decimals == 0) {
        std::snprintf(value, sizeof(value), "%d%s",
                      static_cast<int>(std::round(mc.displayValue)), mc.suffix);
    } else {
        std::snprintf(value, sizeof(value), "%.*f%s",
                      mc.decimals, static_cast<double>(mc.displayValue), mc.suffix);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, p.textPrimary);
    // Render value at ~1.7x scale by directly drawing larger via ImGui::SetWindowFontScale
    // is heavy-handed — use AddText with default font and a manual offset for hierarchy.
    {
        const ImVec2 ts = ImGui::CalcTextSize(value);
        // Draw a faint shadow behind for cinematic feel.
        const ImU32 shadow = anim::withAlpha(mc.accent, 0.20f);
        dl->AddText(ImVec2(cmn.x + 17.0f, cmn.y + 33.0f), shadow, value);
        dl->AddText(ImVec2(cmn.x + 16.0f, cmn.y + 32.0f), p.textPrimary, value);
        (void)ts;
    }
    ImGui::PopStyleColor();

    // Mini sparkline (bottom-left half).
    const int N = 14;
    float spark[N];
    const float baseT = static_cast<float>(glfwGetTime()) * 0.3f;
    for (int i = 0; i < N; ++i) {
        spark[i] = 0.5f + 0.5f * std::sin(baseT + mc.sparkSeed + i * 0.7f)
                       + 0.25f * std::sin(baseT * 1.7f + mc.sparkSeed + i * 0.3f);
    }
    const ImVec2 sparkMn(cmn.x + 14.0f, cmx.y - 38.0f);
    const ImVec2 sparkMx(cmx.x - 14.0f, cmx.y - 14.0f);
    const ImU32 line = mc.accent;
    const ImU32 fill = anim::withAlpha(mc.accent, 0.18f);
    anim::drawSparkline(dl, sparkMn, sparkMx, spark, N, line, fill);
}

void renderHeroStrip(UiState& state, float dt) {
    const auto& p = theme::currentPalette();
    const float availW = ImGui::GetContentRegionAvail().x;
    const float gap = 12.0f;
    const float cardW = (availW - gap * 3.0f) / 4.0f;
    const float cardH = 110.0f;

    const ImVec2 base = ImGui::GetCursorScreenPos();

    MetricCard cards[4] = {
        { "TOTAL MOVIES", state.anim.totalCount.current, 0, "",
          p.accentA, true, 0.0f },
        { "WATCHLIST",    state.anim.watchlistCount.current, 0, "",
          p.statusWatchlist, false, 1.7f },
        { "WATCHED",      state.anim.watchedCount.current, 0, "",
          p.statusWatched, false, 3.4f },
        { "OWNED",        state.anim.ownedCount.current, 0, "",
          p.statusOwned, false, 5.1f },
    };

    for (int i = 0; i < 4; ++i) {
        const ImVec2 mn(base.x + (cardW + gap) * i, base.y);
        const ImVec2 mx(mn.x + cardW, mn.y + cardH);
        renderHeroCard(state, mn, mx, cards[i], dt);
    }

    ImGui::SetCursorScreenPos(ImVec2(base.x, base.y + cardH + 8.0f));
}

// ──────────────────────────────────────────────────────────────────────────────
//  Filter row — search + status pills + buttons
// ──────────────────────────────────────────────────────────────────────────────
bool pill(const char* label, bool active, ImU32 accent, float dt,
          UiState& state, float width = 0.0f) {
    const auto& p = theme::currentPalette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 ts = ImGui::CalcTextSize(label);
    const float padX = 14.0f;
    const float h = 30.0f;
    const float w = width > 0.0f ? width : (ts.x + padX * 2.0f);

    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx(mn.x + w, mn.y + h);
    ImGui::InvisibleButton(label, ImVec2(w, h));
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    const float hover01 = anim::tickHover(state.anim.hover, ImGui::GetID(label), hovered, dt);

    if (active) {
        anim::drawHorizontalGradient(dl, mn, mx,
                                     anim::withAlpha(accent, 0.85f),
                                     anim::withAlpha(accent, 1.0f),
                                     h * 0.5f);
        const ImU32 inner = p.isDark ? IM_COL32(0, 0, 0, 60) : IM_COL32(255, 255, 255, 80);
        dl->AddRect(mn, mx, inner, h * 0.5f, 0, 1.0f);
    } else {
        const ImU32 fill = anim::withAlpha(accent, 0.10f + 0.10f * hover01);
        dl->AddRectFilled(mn, mx, fill, h * 0.5f);
        dl->AddRect(mn, mx, anim::withAlpha(accent, 0.55f + 0.30f * hover01),
                    h * 0.5f, 0, 1.0f);
    }

    const ImU32 textCol = active ? IM_COL32(255, 255, 255, 255) : p.textPrimary;
    dl->AddText(ImVec2(mn.x + (w - ts.x) * 0.5f, mn.y + (h - ts.y) * 0.5f),
                textCol, label);
    return clicked;
}

void renderFilterRow(UiState& state, network::Client& client, float dt,
                     const logic::StatusCounts& counts) {
    const auto& p = theme::currentPalette();
    const float h = 48.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx(mn.x + ImGui::GetContentRegionAvail().x, mn.y + h);
    anim::drawGlassPanel(dl, mn, mx, p.cardFill, p.cardHighlight, p.panelBorder, 12.0f);

    ImGui::SetCursorScreenPos(ImVec2(mn.x + 14.0f, mn.y + 9.0f));

    // Search box.
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##search", "Search title...",
                             state.searchBuffer, sizeof(state.searchBuffer));
    ImGui::SameLine(0.0f, 8.0f);

    char wlLabel[32], wdLabel[32], ownLabel[32];
    std::snprintf(wlLabel,  sizeof(wlLabel),  "Watchlist %d", counts.watchlist);
    std::snprintf(wdLabel,  sizeof(wdLabel),  "Watched %d",   counts.watched);
    std::snprintf(ownLabel, sizeof(ownLabel), "Owned %d",     counts.owned);

    if (pill(wlLabel,  state.showWatchlist, p.statusWatchlist, dt, state)) {
        state.showWatchlist = !state.showWatchlist;
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (pill(wdLabel,  state.showWatched,   p.statusWatched,   dt, state)) {
        state.showWatched = !state.showWatched;
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (pill(ownLabel, state.showOwned,     p.statusOwned,     dt, state)) {
        state.showOwned = !state.showOwned;
    }
    ImGui::SameLine(0.0f, 12.0f);
    if (pill(state.favoritesOnly ? "<3 Faves" : "Faves",
             state.favoritesOnly, p.connectionOffline, dt, state)) {
        state.favoritesOnly = !state.favoritesOnly;
    }

    // Right side: filter / stats / export / sync.
    ImGui::SameLine();
    const float rightStart = mx.x - 14.0f - (96.0f + 100.0f + 96.0f + 70.0f + 24.0f);
    ImGui::SetCursorScreenPos(ImVec2(rightStart, mn.y + 9.0f));

    if (pill(state.showAdvancedFilters ? "Filters -" : "Filters +",
             state.showAdvancedFilters, p.accentA, dt, state, 96.0f)) {
        state.showAdvancedFilters = !state.showAdvancedFilters;
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (pill(state.showStatsPanel ? "Stats -" : "Stats +",
             state.showStatsPanel, p.accentB, dt, state, 100.0f)) {
        state.showStatsPanel = !state.showStatsPanel;
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (pill("Export", false, p.statusOwned, dt, state, 96.0f)) {
        exportToCsv(state);
        state.statusMessage = "Exported to movie_collection_export.csv";
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (pill("Sync", false, p.statusWatched, dt, state, 70.0f)) {
        if (client.connected.load()) {
            protocol::Message sync;
            sync.kind = protocol::MessageKind::REQUEST_SYNC;
            sendRequest(client, sync);
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(mn.x, mx.y + 8.0f));
}

// ──────────────────────────────────────────────────────────────────────────────
//  Filter drawer — eased height
// ──────────────────────────────────────────────────────────────────────────────
void renderFilterDrawer(UiState& state) {
    const float h = state.anim.filterDrawerHeight.current;
    if (h < 1.0f) {
        return;
    }
    const auto& p = theme::currentPalette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx(mn.x + ImGui::GetContentRegionAvail().x, mn.y + h);
    anim::drawGlassPanel(dl, mn, mx, p.cardFill, p.cardHighlight, p.panelBorder, 12.0f);

    // Clip contents so they appear to slide in.
    ImGui::PushClipRect(mn, mx, true);
    ImGui::SetCursorScreenPos(ImVec2(mn.x + 14.0f, mn.y + 12.0f));

    if (ImGui::BeginChild("##drawer_inner",
                          ImVec2(mx.x - mn.x - 28.0f, mx.y - mn.y - 16.0f),
                          false,
                          ImGuiWindowFlags_NoScrollbar)) {
        ImGui::TextDisabled("ADVANCED FILTERS");
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(20.0f, 0.0f));
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset")) {
            state.genreFilter[0]    = '\0';
            state.directorFilter[0] = '\0';
            state.minRatingFilter   = 0.0f;
            state.maxRatingFilter   = 10.0f;
            state.minYearFilter     = 1880;
            state.maxYearFilter     = 2200;
        }

        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##genre", "Genre filter...",
                                 state.genreFilter, sizeof(state.genreFilter));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##director", "Director filter...",
                                 state.directorFilter, sizeof(state.directorFilter));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        ImGui::DragFloatRange2("Rating", &state.minRatingFilter, &state.maxRatingFilter,
                               0.1f, 0.0f, 10.0f, "%.1f", "%.1f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        ImGui::DragIntRange2("Year", &state.minYearFilter, &state.maxYearFilter,
                             1.0f, 1880, 2200);
    }
    ImGui::EndChild();
    ImGui::PopClipRect();

    ImGui::SetCursorScreenPos(ImVec2(mn.x, mx.y + 8.0f));
}

// ──────────────────────────────────────────────────────────────────────────────
//  Charts panel — donut + genre bars
// ──────────────────────────────────────────────────────────────────────────────
void renderChartsPanel(UiState& state, float dt,
                       const std::vector<data::Movie>& snap,
                       const logic::StatusCounts& counts) {
    const float h = state.anim.statsHeight.current;
    if (h < 1.0f) {
        return;
    }
    const auto& p = theme::currentPalette();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const float availW = ImGui::GetContentRegionAvail().x;
    const float donutW = 280.0f;
    const float gap = 12.0f;
    const float barsW = availW - donutW - gap;

    // ── Genre bars card (left) ────────────────────────────────────────────
    const ImVec2 barsMn = mn;
    const ImVec2 barsMx(mn.x + barsW, mn.y + h - 6.0f);
    anim::drawGlassPanel(dl, barsMn, barsMx, p.cardFill, p.cardHighlight, p.panelBorder, 12.0f);

    ImGui::PushClipRect(barsMn, barsMx, true);
    dl->AddText(ImVec2(barsMn.x + 14.0f, barsMn.y + 12.0f), p.textDim, "TOP GENRES");

    auto genres = logic::genreStats(snap);
    constexpr std::size_t MAX_G = 6;
    if (genres.size() > MAX_G) genres.resize(MAX_G);

    int maxCount = 1;
    for (const auto& g : genres) maxCount = std::max(maxCount, g.second);

    const float topY = barsMn.y + 36.0f;
    const float rowH = 18.0f;
    const float labelW = 130.0f;
    const float countW = 40.0f;

    for (std::size_t i = 0; i < genres.size(); ++i) {
        const float y = topY + static_cast<float>(i) * (rowH + 4.0f);
        if (y + rowH > barsMx.y - 8.0f) break;

        // Genre name (truncated to labelW).
        char name[64];
        std::snprintf(name, sizeof(name), "%s", genres[i].first.c_str());
        dl->AddText(ImVec2(barsMn.x + 14.0f, y + 1.0f), p.textPrimary, name);

        // Bar.
        const float barX0 = barsMn.x + 14.0f + labelW;
        const float barX1 = barsMx.x - 14.0f - countW;
        const float ratio = static_cast<float>(genres[i].second) / static_cast<float>(maxCount);
        auto& eased = state.anim.genreEased[genres[i].first];
        anim::setEased(eased, ratio);
        anim::tickEased(eased, dt, 0.30f);

        anim::drawHBar(dl,
                       ImVec2(barX0, y + 2.0f),
                       ImVec2(barX1, y + rowH - 2.0f),
                       eased.current,
                       p.trackFill, p.accentA, p.accentB,
                       (rowH - 4.0f) * 0.5f);

        // Count.
        char cnt[16];
        std::snprintf(cnt, sizeof(cnt), "%d", genres[i].second);
        dl->AddText(ImVec2(barX1 + 8.0f, y + 1.0f), p.textPrimary, cnt);
    }
    ImGui::PopClipRect();

    // ── Donut card (right) ────────────────────────────────────────────────
    const ImVec2 donutMn(mn.x + barsW + gap, mn.y);
    const ImVec2 donutMx(donutMn.x + donutW, mn.y + h - 6.0f);
    anim::drawGlassPanel(dl, donutMn, donutMx,
                         p.cardFill, p.cardHighlight, p.panelBorder, 12.0f);
    dl->AddText(ImVec2(donutMn.x + 14.0f, donutMn.y + 12.0f), p.textDim, "STATUS DISTRIBUTION");

    const ImVec2 dCenter((donutMn.x + donutMx.x) * 0.5f - 50.0f,
                         (donutMn.y + donutMx.y) * 0.5f + 6.0f);
    const float radius = std::min((donutMx.y - donutMn.y) * 0.34f, 56.0f);
    const float thickness = 14.0f;

    // Hover detection per segment via invisible buttons in the donut bbox.
    // Cheap approximation: use a single invisible button over the donut rect.
    ImGui::SetCursorScreenPos(ImVec2(dCenter.x - radius - 6.0f, dCenter.y - radius - 6.0f));
    ImGui::InvisibleButton("##donut", ImVec2(radius * 2.0f + 12.0f, radius * 2.0f + 12.0f));
    const bool donutHover = ImGui::IsItemHovered();
    const float donutHover01 = anim::tickHover(state.anim.hover,
                                               ImGui::GetID("##donut"),
                                               donutHover, dt);

    const float fractions[3] = {
        state.anim.watchlistFraction.current,
        state.anim.watchedFraction.current,
        state.anim.ownedFraction.current,
    };
    const ImU32 colors[3] = {
        p.statusWatchlist, p.statusWatched, p.statusOwned,
    };
    const float dim01[3] = {
        1.0f, 1.0f, 1.0f,
    };
    (void)donutHover01;

    anim::drawDonut(dl, dCenter, radius, thickness, fractions, colors, dim01, 3,
                    state.anim.spinPhase);

    // Center label: total count.
    char totalBuf[16];
    std::snprintf(totalBuf, sizeof(totalBuf), "%d",
                  static_cast<int>(std::round(state.anim.totalCount.current)));
    const ImVec2 ts = ImGui::CalcTextSize(totalBuf);
    dl->AddText(ImVec2(dCenter.x - ts.x * 0.5f, dCenter.y - ts.y * 0.5f - 2.0f),
                p.textPrimary, totalBuf);
    const ImVec2 lts = ImGui::CalcTextSize("TOTAL");
    dl->AddText(ImVec2(dCenter.x - lts.x * 0.5f, dCenter.y + ts.y * 0.5f),
                p.textDim, "TOTAL");

    // Legend on the right of the donut.
    const float legendX = dCenter.x + radius + 24.0f;
    const float legendY = dCenter.y - 32.0f;
    const char* labels[3] = { "Watchlist", "Watched", "Owned" };
    const int   c3[3]     = { counts.watchlist, counts.watched, counts.owned };
    for (int i = 0; i < 3; ++i) {
        const float y = legendY + static_cast<float>(i) * 22.0f;
        dl->AddCircleFilled(ImVec2(legendX, y + 7.0f), 5.0f, colors[i], 12);
        dl->AddText(ImVec2(legendX + 14.0f, y), p.textPrimary, labels[i]);
        char cnt[16];
        std::snprintf(cnt, sizeof(cnt), "%d", c3[i]);
        dl->AddText(ImVec2(legendX + 80.0f, y), p.textDim, cnt);
    }

    ImGui::SetCursorScreenPos(ImVec2(mn.x, mn.y + h));
}

// ──────────────────────────────────────────────────────────────────────────────
//  Movie table
// ──────────────────────────────────────────────────────────────────────────────
void renderMovieTable(UiState& state, float dt) {
    const auto& p = theme::currentPalette();
    std::vector<data::Movie> snapshot = logic::snapshotCollection(state.localCollection);

    logic::FilterCriteria criteria;
    criteria.showWatchlist  = state.showWatchlist;
    criteria.showWatched    = state.showWatched;
    criteria.showOwned      = state.showOwned;
    criteria.favoritesOnly  = state.favoritesOnly;
    criteria.titleSubstring = state.searchBuffer;
    criteria.genreFilter    = state.genreFilter;
    criteria.directorFilter = state.directorFilter;
    criteria.minRating      = state.minRatingFilter;
    criteria.maxRating      = state.maxRatingFilter;
    criteria.minYear        = state.minYearFilter;
    criteria.maxYear        = state.maxYearFilter;

    std::vector<data::Movie> movies = logic::applyFilters(snapshot, criteria);
    logic::sortMovies(movies, state.sortKey, state.sortOrder);

    // Wrap the table in a glass card.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 cardMn = ImGui::GetCursorScreenPos();
    const ImVec2 cardMx(cardMn.x + ImGui::GetContentRegionAvail().x,
                        cardMn.y + ImGui::GetContentRegionAvail().y);
    anim::drawGlassPanel(dl, cardMn, cardMx, p.cardFill, p.cardHighlight, p.panelBorder, 14.0f);

    ImGui::SetCursorScreenPos(ImVec2(cardMn.x + 10.0f, cardMn.y + 10.0f));
    if (!ImGui::BeginChild("##table_inner",
                           ImVec2(cardMx.x - cardMn.x - 20.0f,
                                  cardMx.y - cardMn.y - 20.0f),
                           false,
                           ImGuiWindowFlags_NoScrollbar)) {
        ImGui::EndChild();
        return;
    }

    const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg
                                     | ImGuiTableFlags_Resizable
                                     | ImGuiTableFlags_ScrollY
                                     | ImGuiTableFlags_Sortable
                                     | ImGuiTableFlags_BordersInnerH;

    if (ImGui::BeginTable("movies", 10, tableFlags, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Sel",      ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableSetupColumn("Fav",      ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("Title",    ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Director", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Genres",   ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Year",     ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("Rating",   ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("Status",   ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 84.0f);
        ImGui::TableSetupColumn("Actions",  ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
            if (specs->SpecsDirty && specs->SpecsCount > 0) {
                const ImGuiTableColumnSortSpecs& s = specs->Specs[0];
                switch (s.ColumnIndex) {
                    case 2: state.sortKey = logic::SortKey::TITLE;    break;
                    case 5: state.sortKey = logic::SortKey::YEAR;     break;
                    case 6: state.sortKey = logic::SortKey::RATING;   break;
                    case 7: state.sortKey = logic::SortKey::DURATION; break;
                    default: break;
                }
                state.sortOrder = (s.SortDirection == ImGuiSortDirection_Ascending)
                    ? logic::SortOrder::ASCENDING
                    : logic::SortOrder::DESCENDING;
                specs->SpecsDirty = false;
            }
        }

        for (const data::Movie& movie : movies) {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 30.0f);
            ImGui::PushID(static_cast<int>(movie.id));

            // Hover-driven row overlay.
            const ImGuiID rowId = ImGui::GetID("##row");
            const ImVec2 rowMn = ImGui::GetCursorScreenPos();
            const float rowH = ImGui::GetTextLineHeight() + 12.0f;

            // Col 0: selection checkbox.
            ImGui::TableSetColumnIndex(0);
            bool selected = state.selectedIds.count(movie.id) > 0;
            if (ImGui::Checkbox("##sel", &selected)) {
                if (selected) state.selectedIds.insert(movie.id);
                else          state.selectedIds.erase(movie.id);
                refreshTotalDuration(state);
            }

            // Hover detection: use the row's first cell rect approximation.
            const ImVec2 rowAfterSel = ImGui::GetCursorScreenPos();
            (void)rowAfterSel;
            const bool rowHovered =
                ImGui::IsMouseHoveringRect(ImVec2(rowMn.x - 14.0f, rowMn.y - 4.0f),
                                           ImVec2(cardMx.x - 12.0f, rowMn.y + rowH - 4.0f));
            const float rowHover01 = anim::tickHover(state.anim.hover, rowId, rowHovered, dt);

            // Animated left accent border, drawn on the table's draw list.
            ImDrawList* tdl = ImGui::GetWindowDrawList();
            const ImU32 accentRow = statusColor(movie.status);
            const float borderW = 2.5f + 2.5f * rowHover01;
            const ImVec2 borderMn(cardMn.x + 10.0f,
                                  rowMn.y - 2.0f);
            const ImVec2 borderMx(borderMn.x + borderW,
                                  rowMn.y + rowH - 6.0f);
            tdl->AddRectFilled(borderMn, borderMx,
                               anim::withAlpha(accentRow, 0.85f), 1.5f);
            // Subtle row hover overlay.
            if (rowHover01 > 0.001f) {
                const ImU32 ov = anim::withAlpha(p.isDark
                    ? IM_COL32(255, 255, 255, 255)
                    : IM_COL32(0, 0, 0, 255),
                    rowHover01 * (p.isDark ? 0.05f : 0.04f));
                tdl->AddRectFilled(ImVec2(cardMn.x + 14.0f, rowMn.y - 2.0f),
                                   ImVec2(cardMx.x - 12.0f, rowMn.y + rowH - 6.0f),
                                   ov, 4.0f);
            }

            // Col 1: favorite heart with pulse animation.
            ImGui::TableSetColumnIndex(1);
            if (movie.favorite) {
                float pulseMul = 1.0f;
                auto it = state.anim.favoritePulse.find(movie.id);
                if (it != state.anim.favoritePulse.end()) {
                    const float t = 1.0f - (it->second / 0.45f);
                    pulseMul = 1.0f + 0.4f * std::sin(t * 3.14159f);
                }
                const ImVec2 heartCenter = ImGui::GetCursorScreenPos();
                const float baseR = 5.5f;
                const float r = baseR * pulseMul;
                const ImU32 heart = p.connectionOffline;
                tdl->AddCircleFilled(ImVec2(heartCenter.x + r,        heartCenter.y + r + 3.0f),
                                     r * 0.65f, heart, 12);
                tdl->AddCircleFilled(ImVec2(heartCenter.x + r * 2.4f, heartCenter.y + r + 3.0f),
                                     r * 0.65f, heart, 12);
                tdl->AddTriangleFilled(
                    ImVec2(heartCenter.x + r * 0.5f, heartCenter.y + r + 5.0f),
                    ImVec2(heartCenter.x + r * 2.9f, heartCenter.y + r + 5.0f),
                    ImVec2(heartCenter.x + r * 1.7f, heartCenter.y + r * 2.5f + 5.0f),
                    heart);
                ImGui::Dummy(ImVec2(20.0f, 0.0f));
            }

            // Col 2: title with tooltip.
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(movie.title.c_str());
            if (!movie.notes.empty() && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextDisabled("Notes:");
                ImGui::TextWrapped("%s", movie.notes.c_str());
                ImGui::EndTooltip();
            }

            // Col 3: director.
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(movie.director.c_str());

            // Col 4: genres.
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(movie.genres.c_str());

            // Col 5: year.
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%d", movie.year);

            // Col 6: rating bar (gradient horizontal bar).
            ImGui::TableSetColumnIndex(6);
            {
                const ImVec2 barMn = ImGui::GetCursorScreenPos();
                const float barW = ImGui::GetContentRegionAvail().x - 4.0f;
                const float barH = 16.0f;
                const ImVec2 barMx(barMn.x + barW, barMn.y + barH);
                ImU32 colA, colB;
                if (movie.rating >= 7.5f) {
                    colA = IM_COL32( 38, 184,  77, 255);
                    colB = IM_COL32( 80, 220, 120, 255);
                } else if (movie.rating >= 5.0f) {
                    colA = IM_COL32(241, 184,  61, 255);
                    colB = IM_COL32(255, 207, 105, 255);
                } else {
                    colA = IM_COL32(231,  78,  72, 255);
                    colB = IM_COL32(245, 120, 112, 255);
                }
                anim::drawHBar(tdl, barMn, barMx, movie.rating / 10.0f,
                               p.trackFill, colA, colB, barH * 0.5f);
                char ratingBuf[16];
                std::snprintf(ratingBuf, sizeof(ratingBuf), "%.1f", static_cast<double>(movie.rating));
                const ImVec2 rts = ImGui::CalcTextSize(ratingBuf);
                tdl->AddText(ImVec2(barMn.x + (barW - rts.x) * 0.5f,
                                    barMn.y + (barH - rts.y) * 0.5f),
                             IM_COL32(255, 255, 255, 230), ratingBuf);
                ImGui::Dummy(ImVec2(barW, barH));
            }

            // Col 7: duration.
            ImGui::TableSetColumnIndex(7);
            if (movie.durationMinutes >= 60) {
                ImGui::Text("%dh%02dm", movie.durationMinutes / 60, movie.durationMinutes % 60);
            } else {
                ImGui::Text("%dmin", movie.durationMinutes);
            }

            // Col 8: status badge.
            ImGui::TableSetColumnIndex(8);
            {
                static const char* STATUS_LABELS[] = {"Watchlist", "Watched", "Owned"};
                const int si = static_cast<int>(movie.status);
                const int si_clamped = (si >= 0 && si <= 2) ? si : 0;
                ImGui::PushStyleColor(ImGuiCol_Text, statusTextVec(movie.status));
                ImGui::TextUnformatted(STATUS_LABELS[si_clamped]);
                ImGui::PopStyleColor();
            }

            // Col 9: actions.
            ImGui::TableSetColumnIndex(9);
            if (ImGui::SmallButton("Edit")) {
                state.editingId = movie.id;
                copyString(state.formTitle,    sizeof(state.formTitle),    movie.title);
                copyString(state.formDirector, sizeof(state.formDirector), movie.director);
                copyString(state.formGenres,   sizeof(state.formGenres),   movie.genres);
                copyString(state.formNotes,    sizeof(state.formNotes),    movie.notes);
                state.formYear     = movie.year;
                state.formRating   = movie.rating;
                state.formDuration = movie.durationMinutes;
                state.formStatus   = static_cast<int>(movie.status);
                state.formFavorite = movie.favorite;
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(p.connectionOffline));
            if (ImGui::SmallButton("Del")) {
                state.pendingDeleteId = movie.id;
            }
            ImGui::PopStyleColor();

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

// ──────────────────────────────────────────────────────────────────────────────
//  Delete confirmation modal
// ──────────────────────────────────────────────────────────────────────────────
void renderDeleteModal(UiState& state, network::Client& client) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm Delete", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete movie id %llu?",
                    static_cast<unsigned long long>(state.pendingDeleteId));
        ImGui::TextDisabled("This cannot be undone.");
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.75f, 0.12f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.18f, 0.15f, 1.0f));
        if (ImGui::Button("Delete", ImVec2(100, 0))) {
            protocol::Message request;
            request.kind     = protocol::MessageKind::REQUEST_REMOVE;
            request.targetId = state.pendingDeleteId;
            if (network::sendClientMessage(client, protocol::encodeMessage(request))) {
                state.statusMessage = "Delete request sent.";
            }
            if (state.editingId == state.pendingDeleteId) {
                resetForm(state);
            }
            state.selectedIds.erase(state.pendingDeleteId);
            state.pendingDeleteId = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            state.pendingDeleteId = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Form panel (right side) — wrapped as a glass card with gradient submit
// ──────────────────────────────────────────────────────────────────────────────
void renderForm(UiState& state, network::Client& client, float dt) {
    const auto& p = theme::currentPalette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 cardMn = ImGui::GetCursorScreenPos();
    const ImVec2 cardMx(cardMn.x + ImGui::GetContentRegionAvail().x,
                        cardMn.y + ImGui::GetContentRegionAvail().y);
    anim::drawGlassPanel(dl, cardMn, cardMx,
                         p.cardFill, p.cardHighlight, p.panelBorder, 14.0f);

    ImGui::SetCursorScreenPos(ImVec2(cardMn.x + 14.0f, cardMn.y + 14.0f));
    if (!ImGui::BeginChild("##form_inner",
                           ImVec2(cardMx.x - cardMn.x - 28.0f,
                                  cardMx.y - cardMn.y - 28.0f),
                           false)) {
        ImGui::EndChild();
        return;
    }

    if (state.editingId == 0) {
        ImGui::TextDisabled("ADD NEW MOVIE");
    } else {
        ImGui::TextDisabled("EDIT MOVIE");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, p.textPrimary);
        ImGui::Text("(%llu)", static_cast<unsigned long long>(state.editingId));
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();

    const float fieldW = ImGui::GetContentRegionAvail().x;

    if (state.focusTitleNextFrame) {
        ImGui::SetKeyboardFocusHere();
        state.focusTitleNextFrame = false;
    }
    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputTextWithHint("##title", "Title *", state.formTitle, sizeof(state.formTitle));

    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputTextWithHint("##director", "Director", state.formDirector, sizeof(state.formDirector));

    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputTextWithHint("##genres", "Genres (comma-separated)",
                             state.formGenres, sizeof(state.formGenres));

    ImGui::SetNextItemWidth((fieldW - 8.0f) * 0.40f);
    ImGui::InputInt("##year", &state.formYear);
    ImGui::SameLine();
    ImGui::SetNextItemWidth((fieldW - 8.0f) * 0.60f);
    ImGui::SliderFloat("##rating", &state.formRating, 0.0f, 10.0f, "Rating: %.1f");

    ImGui::SetNextItemWidth((fieldW - 8.0f) * 0.40f);
    ImGui::InputInt("##dur", &state.formDuration);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Duration in minutes");
    }
    ImGui::SameLine();
    {
        static const char* STATUS_LABELS[] = {"Watchlist", "Watched", "Owned"};
        ImGui::SetNextItemWidth((fieldW - 8.0f) * 0.60f);
        ImGui::Combo("##status", &state.formStatus, STATUS_LABELS, IM_ARRAYSIZE(STATUS_LABELS));
    }

    ImGui::Checkbox("Favorite", &state.formFavorite);

    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputTextMultiline("##notes", state.formNotes, sizeof(state.formNotes),
                              ImVec2(fieldW, 90.0f));

    ImGui::Spacing();

    const bool online = client.connected.load();
    if (!online) ImGui::BeginDisabled();

    // Gradient submit button drawn manually.
    const char* submitLabel = state.editingId == 0 ? "Create Movie" : "Save Changes";
    const float btnH = 38.0f;
    const ImVec2 btnMn = ImGui::GetCursorScreenPos();
    const ImVec2 btnMx(btnMn.x + fieldW, btnMn.y + btnH);
    ImGui::InvisibleButton("##submit", ImVec2(fieldW, btnH));
    const bool sClicked = ImGui::IsItemClicked();
    const bool sHover   = ImGui::IsItemHovered();
    const float sH01    = anim::tickHover(state.anim.hover, ImGui::GetID("##submit"), sHover, dt);

    ImDrawList* fdl = ImGui::GetWindowDrawList();
    const float scale = 1.0f + 0.012f * sH01;
    const ImVec2 ctr((btnMn.x + btnMx.x) * 0.5f, (btnMn.y + btnMx.y) * 0.5f);
    const ImVec2 hsz((btnMx.x - btnMn.x) * 0.5f * scale,
                     (btnMx.y - btnMn.y) * 0.5f * scale);
    const ImVec2 sMn(ctr.x - hsz.x, ctr.y - hsz.y);
    const ImVec2 sMx(ctr.x + hsz.x, ctr.y + hsz.y);
    anim::drawHorizontalGradient(fdl, sMn, sMx, p.accentA, p.accentB, btnH * 0.5f);
    if (sH01 > 0.001f) {
        fdl->AddRect(sMn, sMx, anim::withAlpha(p.accentB, sH01 * 0.9f),
                     btnH * 0.5f, 0, 1.5f);
    }
    const ImVec2 ts = ImGui::CalcTextSize(submitLabel);
    fdl->AddText(ImVec2(ctr.x - ts.x * 0.5f, ctr.y - ts.y * 0.5f),
                 IM_COL32(255, 255, 255, 250), submitLabel);

    if (sClicked) {
        data::Movie candidate = makeMovieFromForm(state);
        std::string validationError;
        if (!logic::validateMovie(candidate, validationError)) {
            state.statusMessage = "Invalid: " + validationError;
        } else {
            protocol::Message request;
            request.payload = candidate;
            request.kind = state.editingId == 0
                ? protocol::MessageKind::REQUEST_ADD
                : protocol::MessageKind::REQUEST_UPDATE;
            if (sendRequest(client, request)) {
                state.statusMessage = state.editingId == 0
                    ? "Movie added."
                    : "Movie updated.";
                resetForm(state);
            } else {
                state.statusMessage = "Send failed.";
            }
        }
    }

    if (state.editingId != 0) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImGui::ColorConvertU32ToFloat4(
                                  anim::withAlpha(p.connectionOffline, 0.85f)));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(0.90f, 0.18f, 0.15f, 1.0f));
        if (ImGui::Button("Delete This Movie", ImVec2((fieldW - 8.0f) * 0.55f, 0.0f))) {
            state.pendingDeleteId = state.editingId;
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(-1.0f, 0.0f))) {
            resetForm(state);
        }
    }

    if (!online) {
        ImGui::EndDisabled();
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.connectionOffline),
                           "Connect to a server to make changes.");
    }

    ImGui::EndChild();
}

// ──────────────────────────────────────────────────────────────────────────────
//  Status bar — glass strip
// ──────────────────────────────────────────────────────────────────────────────
void renderStatusBar(const UiState& state, network::Client& client,
                     const logic::StatusCounts& counts, float avg) {
    const auto& p = theme::currentPalette();
    const float h = 44.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx(mn.x + ImGui::GetContentRegionAvail().x, mn.y + h);
    anim::drawGlassPanel(dl, mn, mx, p.cardFill, p.cardHighlight, p.panelBorder, 12.0f);

    ImGui::SetCursorScreenPos(ImVec2(mn.x + 14.0f, mn.y + 12.0f));

    const std::vector<data::Movie> snap = logic::snapshotCollection(state.localCollection);
    ImGui::Text("Movies: %zu", snap.size());
    ImGui::SameLine(0.0f, 12.0f); ImGui::TextDisabled("|"); ImGui::SameLine(0.0f, 12.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, statusTextVec(data::Status::WATCHLIST));
    ImGui::Text("Watchlist %d", counts.watchlist); ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, statusTextVec(data::Status::WATCHED));
    ImGui::Text("Watched %d", counts.watched); ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, statusTextVec(data::Status::OWNED));
    ImGui::Text("Owned %d", counts.owned); ImGui::PopStyleColor();

    ImGui::SameLine(0.0f, 12.0f); ImGui::TextDisabled("|"); ImGui::SameLine(0.0f, 12.0f);
    ImGui::Text("Avg %.2f", static_cast<double>(avg));

    if (!state.selectedIds.empty()) {
        ImGui::SameLine(0.0f, 12.0f); ImGui::TextDisabled("|"); ImGui::SameLine(0.0f, 12.0f);
        ImGui::Text("Sel %zu (%lld min, %.1fh)",
                    state.selectedIds.size(),
                    state.totalSelectedMinutes,
                    static_cast<double>(state.totalSelectedMinutes) / 60.0);
    }

    if (!state.statusMessage.empty()) {
        ImGui::SameLine(0.0f, 12.0f); ImGui::TextDisabled("|"); ImGui::SameLine(0.0f, 12.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(p.accentA));
        ImGui::TextUnformatted(state.statusMessage.c_str());
        ImGui::PopStyleColor();
    }

    // Right-anchored: revision + connection state.
    const bool online = client.connected.load();
    char rightBuf[64];
    std::snprintf(rightBuf, sizeof(rightBuf), "rev %llu",
                  static_cast<unsigned long long>(state.lastKnownRevision));
    const ImVec2 rts = ImGui::CalcTextSize(rightBuf);
    dl->AddText(ImVec2(mx.x - 14.0f - rts.x, mn.y + 14.0f),
                p.textDim, rightBuf);

    (void)online;
    ImGui::SetCursorScreenPos(ImVec2(mn.x, mx.y));
}

// ──────────────────────────────────────────────────────────────────────────────
//  Server traffic processing + auto-reconnect
// ──────────────────────────────────────────────────────────────────────────────
void processServerTraffic(UiState& state, network::Client& client) {
    if (!client.connected.load() && state.autoReconnect) {
        const double now = glfwGetTime();
        if (now - state.lastReconnectAttemptTime >= 5.0) {
            state.lastReconnectAttemptTime = now;
            if (network::connectClient(client, state.hostBuffer, state.portBuffer)) {
                state.statusMessage = "Reconnected.";
                protocol::Message sync;
                sync.kind = protocol::MessageKind::REQUEST_SYNC;
                sendRequest(client, sync);
            }
        }
    }

    std::vector<std::string> frames = network::drainInbox(client);
    if (frames.empty()) {
        return;
    }
    for (const std::string& raw : frames) {
        protocol::Message message;
        if (!protocol::decodeMessage(raw, message)) {
            continue;
        }
        applyServerEvent(state, message);
    }
    refreshTotalDuration(state);
}

// ──────────────────────────────────────────────────────────────────────────────
//  Keyboard shortcut handling
// ──────────────────────────────────────────────────────────────────────────────
void handleKeyboardShortcuts(UiState& state) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        resetForm(state);
        state.focusTitleNextFrame = true;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        const auto all = logic::snapshotCollection(state.localCollection);
        logic::FilterCriteria fc;
        fc.showWatchlist  = state.showWatchlist;
        fc.showWatched    = state.showWatched;
        fc.showOwned      = state.showOwned;
        fc.favoritesOnly  = state.favoritesOnly;
        fc.titleSubstring = state.searchBuffer;
        fc.genreFilter    = state.genreFilter;
        fc.directorFilter = state.directorFilter;
        fc.minRating      = state.minRatingFilter;
        fc.maxRating      = state.maxRatingFilter;
        fc.minYear        = state.minYearFilter;
        fc.maxYear        = state.maxYearFilter;
        for (const auto& m : logic::applyFilters(all, fc)) {
            state.selectedIds.insert(m.id);
        }
        refreshTotalDuration(state);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E, false)) {
        exportToCsv(state);
        state.statusMessage = "Exported to movie_collection_export.csv";
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && state.editingId != 0) {
        resetForm(state);
    }
}

} // namespace

// ──────────────────────────────────────────────────────────────────────────────
//  Public API
// ──────────────────────────────────────────────────────────────────────────────

bool initialisePresentation(const std::string& windowTitle) {
    glfwSetErrorCallback([](int code, const char* desc) {
        std::cerr << "[glfw " << code << "] " << desc << "\n";
    });
    if (!glfwInit()) {
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    windowHandle = glfwCreateWindow(
        WINDOW_WIDTH, WINDOW_HEIGHT,
        windowTitle.c_str(), nullptr, nullptr);
    if (!windowHandle) {
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(windowHandle);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    theme::loadFonts();
    theme::applyLightOrange();
    ImGui_ImplGlfw_InitForOpenGL(windowHandle, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    return true;
}

void shutdownPresentation() {
    if (!windowHandle) {
        return;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(windowHandle);
    glfwTerminate();
    windowHandle = nullptr;
}

void applyServerEvent(UiState& state, const protocol::Message& message) {
    switch (message.kind) {
        case protocol::MessageKind::HELLO:
        case protocol::MessageKind::FULL_STATE: {
            logic::replaceMirror(state.localCollection, message.snapshot, message.revision);
            state.statusMessage = (message.kind == protocol::MessageKind::HELLO)
                ? "Session started (id=" + std::to_string(message.sessionId) + ")"
                : std::string("Full sync applied.");
            state.lastKnownRevision = message.revision;
            break;
        }
        case protocol::MessageKind::EVENT_ADDED:
        case protocol::MessageKind::EVENT_UPDATED: {
            logic::applyEventToMirror(state.localCollection, message);
            state.lastKnownRevision = message.revision;
            break;
        }
        case protocol::MessageKind::EVENT_REMOVED: {
            logic::applyEventToMirror(state.localCollection, message);
            state.selectedIds.erase(message.targetId);
            if (state.editingId == message.targetId) {
                state.editingId = 0;
            }
            state.lastKnownRevision = message.revision;
            break;
        }
        case protocol::MessageKind::ERROR_REPLY: {
            state.statusMessage = "Server error: " + message.errorText;
            break;
        }
        default:
            break;
    }
}

void runMainLoop(UiState& state, network::Client& client) {
    if (!windowHandle) {
        return;
    }
    while (!glfwWindowShouldClose(windowHandle)) {
        glfwPollEvents();
        processServerTraffic(state, client);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        handleKeyboardShortcuts(state);

        // Per-frame dt.
        const double now = glfwGetTime();
        float dt = state.anim.lastFrameTime > 0.0
            ? static_cast<float>(now - state.anim.lastFrameTime)
            : 1.0f / 60.0f;
        if (dt > 0.10f) dt = 0.10f;
        if (dt < 0.0f)  dt = 0.0f;
        state.anim.lastFrameTime = now;

        // Recompute snapshot + counts once per frame and share with all renderers.
        const std::vector<data::Movie> snap = logic::snapshotCollection(state.localCollection);
        const logic::StatusCounts counts = logic::countByStatus(snap);
        const float avg  = logic::averageRating(snap);
        const float best = logic::highestRating(snap);
        const long long totalMins = logic::totalDurationAll(snap);
        tickAnimation(state, dt, counts, avg, best, totalMins, snap);

        // Background gradient under everything.
        drawBackgroundGradient(state);

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowBgAlpha(0.0f);
        const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
                                           | ImGuiWindowFlags_NoMove
                                           | ImGuiWindowFlags_NoSavedSettings
                                           | ImGuiWindowFlags_NoBringToFrontOnFocus
                                           | ImGuiWindowFlags_NoBackground;
        ImGui::Begin("MovieCollectionRoot", nullptr, windowFlags);

        renderAppBar(state, client, dt);
        renderHeroStrip(state, dt);
        renderFilterRow(state, client, dt, counts);
        renderFilterDrawer(state);
        renderChartsPanel(state, dt, snap, counts);

        // Main content: table on the left, form panel on the right.
        const float formPanelWidth  = 330.0f;
        const float statusBarHeight = 56.0f;
        const float mainHeight      = ImGui::GetContentRegionAvail().y - statusBarHeight;
        const float tablePanelWidth = ImGui::GetContentRegionAvail().x - formPanelWidth - 12.0f;

        const ImVec2 mainCursor = ImGui::GetCursorScreenPos();
        if (ImGui::BeginChild("##table_panel",
                              ImVec2(tablePanelWidth, mainHeight),
                              false,
                              ImGuiWindowFlags_NoScrollbar)) {
            renderMovieTable(state, dt);
        }
        ImGui::EndChild();

        ImGui::SetCursorScreenPos(ImVec2(mainCursor.x + tablePanelWidth + 12.0f,
                                         mainCursor.y));
        if (ImGui::BeginChild("##form_panel",
                              ImVec2(formPanelWidth, mainHeight),
                              false)) {
            renderForm(state, client, dt);
        }
        ImGui::EndChild();

        ImGui::SetCursorScreenPos(ImVec2(mainCursor.x,
                                         mainCursor.y + mainHeight + 8.0f));
        renderStatusBar(state, client, counts, avg);

        renderSettingsPopup(state, client);

        // Delete confirmation modal.
        if (state.pendingDeleteId != 0) {
            ImGui::OpenPopup("Confirm Delete");
        }
        renderDeleteModal(state, client);

        ImGui::End();

        ImGui::Render();
        int fw = 0, fh = 0;
        glfwGetFramebufferSize(windowHandle, &fw, &fh);
        glViewport(0, 0, fw, fh);
        const auto& pal = theme::currentPalette();
        const ImVec4 clearColor = ImGui::ColorConvertU32ToFloat4(pal.bgGradTop);
        glClearColor(clearColor.x, clearColor.y, clearColor.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(windowHandle);
    }
}

} // namespace mcm::presentation
