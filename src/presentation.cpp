/**
 * Presentation Layer implementation - Dear ImGui front-end.
 *
 * Uses GLFW + OpenGL3 backend. All ImGui interaction is expressed with
 * free functions operating on the UiState struct; no classes defined.
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

#include <algorithm>
#include <chrono>
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

// Returns the color used to tint rows/badges for a given status.
ImU32 statusRowColor(data::Status status, bool dark) {
    if (dark) {
        switch (status) {
            case data::Status::WATCHLIST: return IM_COL32(180, 130, 20,  55);
            case data::Status::WATCHED:   return IM_COL32(30,  100, 200, 55);
            case data::Status::OWNED:     return IM_COL32(30,  160, 70,  55);
        }
    } else {
        switch (status) {
            case data::Status::WATCHLIST: return IM_COL32(255, 235, 140, 70);
            case data::Status::WATCHED:   return IM_COL32(160, 210, 255, 70);
            case data::Status::OWNED:     return IM_COL32(160, 240, 170, 70);
        }
    }
    return IM_COL32(200, 200, 200, 40);
}

ImVec4 statusTextColor(data::Status status) {
    switch (status) {
        case data::Status::WATCHLIST: return ImVec4(0.85f, 0.60f, 0.05f, 1.0f);
        case data::Status::WATCHED:   return ImVec4(0.25f, 0.55f, 0.95f, 1.0f);
        case data::Status::OWNED:     return ImVec4(0.15f, 0.72f, 0.30f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
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
//  Connection bar
// ──────────────────────────────────────────────────────────────────────────────
void renderConnectionBar(UiState& state, network::Client& client) {
    const bool online = client.connected.load();

    ImGui::Text("Server:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::InputText("##host", state.hostBuffer, sizeof(state.hostBuffer));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::InputText("##port", state.portBuffer, sizeof(state.portBuffer));
    ImGui::SameLine();

    if (online) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.15f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.20f, 0.15f, 1.0f));
        if (ImGui::Button("Disconnect")) {
            network::disconnectClient(client);
            state.statusMessage = "Disconnected.";
        }
        ImGui::PopStyleColor(2);
    } else {
        if (ImGui::Button("Connect")) {
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
    ImGui::SameLine();

    if (online) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f, 0.80f, 0.30f, 1.0f));
        ImGui::Text("  ONLINE");
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.25f, 0.20f, 1.0f));
        ImGui::Text("  OFFLINE");
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::Checkbox("Auto-reconnect", &state.autoReconnect);

    if (!online && state.autoReconnect) {
        const double now  = glfwGetTime();
        const double wait = 5.0 - (now - state.lastReconnectAttemptTime);
        if (wait > 0.0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(retry in %.0fs)", wait);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Toolbar
// ──────────────────────────────────────────────────────────────────────────────
void renderToolbar(UiState& state, network::Client& client) {
    const std::vector<data::Movie> snap = logic::snapshotCollection(state.localCollection);
    const logic::StatusCounts      counts = logic::countByStatus(snap);

    // Row 1: status filter checkboxes with live counts.
    char wlLabel[32], wdLabel[32], ownLabel[32];
    std::snprintf(wlLabel,  sizeof(wlLabel),  "Watchlist (%d)", counts.watchlist);
    std::snprintf(wdLabel,  sizeof(wdLabel),  "Watched (%d)",   counts.watched);
    std::snprintf(ownLabel, sizeof(ownLabel), "Owned (%d)",     counts.owned);

    ImGui::PushStyleColor(ImGuiCol_Text, statusTextColor(data::Status::WATCHLIST));
    ImGui::Checkbox(wlLabel, &state.showWatchlist);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, statusTextColor(data::Status::WATCHED));
    ImGui::Checkbox(wdLabel, &state.showWatched);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, statusTextColor(data::Status::OWNED));
    ImGui::Checkbox(ownLabel, &state.showOwned);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Checkbox("Favorites only", &state.favoritesOnly);

    // Row 2: search + actions.
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint("##search", "Search title...",
                             state.searchBuffer, sizeof(state.searchBuffer));
    ImGui::SameLine();
    if (ImGui::SmallButton("X##clrsearch")) {
        state.searchBuffer[0] = '\0';
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ImGui::SmallButton("Advanced Filters")) {
        state.showAdvancedFilters = !state.showAdvancedFilters;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Statistics")) {
        state.showStatsPanel = !state.showStatsPanel;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Export CSV")) {
        exportToCsv(state);
        state.statusMessage = "Exported to movie_collection_export.csv";
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Sync") && client.connected.load()) {
        protocol::Message sync;
        sync.kind = protocol::MessageKind::REQUEST_SYNC;
        sendRequest(client, sync);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    // Dark mode toggle.
    if (ImGui::SmallButton(state.darkMode ? "Light Mode" : "Dark Mode")) {
        state.darkMode = !state.darkMode;
        if (state.darkMode) {
            theme::applyDarkTheme();
        } else {
            theme::applyLightOrange();
        }
    }

    // Row 3: selection controls.
    if (ImGui::SmallButton("Select All")) {
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
    ImGui::SameLine();
    if (ImGui::SmallButton("Deselect All")) {
        state.selectedIds.clear();
        state.totalSelectedMinutes = 0;
    }
    if (!state.selectedIds.empty()) {
        ImGui::SameLine();
        char bulkLabel[48];
        std::snprintf(bulkLabel, sizeof(bulkLabel),
                      "Delete Selected (%zu)", state.selectedIds.size());
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.75f, 0.12f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.18f, 0.15f, 1.0f));
        if (ImGui::SmallButton(bulkLabel)) {
            if (client.connected.load()) {
                for (const std::uint64_t id : state.selectedIds) {
                    protocol::Message req;
                    req.kind     = protocol::MessageKind::REQUEST_REMOVE;
                    req.targetId = id;
                    sendRequest(client, req);
                }
                state.statusMessage = "Bulk delete requested.";
                if (state.selectedIds.count(state.editingId)) {
                    resetForm(state);
                }
                state.selectedIds.clear();
                state.totalSelectedMinutes = 0;
            }
        }
        ImGui::PopStyleColor(2);
    }

    // Keyboard shortcut hints.
    ImGui::SameLine();
    ImGui::TextDisabled("  Ctrl+N: New  Ctrl+A: Sel-All  Ctrl+E: Export  Esc: Cancel");
}

// ──────────────────────────────────────────────────────────────────────────────
//  Advanced filters (collapsible panel)
// ──────────────────────────────────────────────────────────────────────────────
void renderAdvancedFilters(UiState& state) {
    if (!state.showAdvancedFilters) {
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    if (ImGui::BeginChild("##advfilters", ImVec2(0.0f, 90.0f), true)) {
        ImGui::TextDisabled("Advanced Filters");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset Filters")) {
            state.genreFilter[0]    = '\0';
            state.directorFilter[0] = '\0';
            state.minRatingFilter   = 0.0f;
            state.maxRatingFilter   = 10.0f;
            state.minYearFilter     = 1880;
            state.maxYearFilter     = 2200;
        }

        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputTextWithHint("##genre", "Genre filter...",
                                 state.genreFilter, sizeof(state.genreFilter));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputTextWithHint("##director", "Director filter...",
                                 state.directorFilter, sizeof(state.directorFilter));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::DragFloatRange2("Rating", &state.minRatingFilter, &state.maxRatingFilter,
                               0.1f, 0.0f, 10.0f, "%.1f", "%.1f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragIntRange2("Year", &state.minYearFilter, &state.maxYearFilter,
                             1.0f, 1880, 2200);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ──────────────────────────────────────────────────────────────────────────────
//  Statistics panel
// ──────────────────────────────────────────────────────────────────────────────
void renderStatsPanel(const UiState& state) {
    if (!state.showStatsPanel) {
        return;
    }
    const std::vector<data::Movie> snap   = logic::snapshotCollection(state.localCollection);
    const logic::StatusCounts      counts = logic::countByStatus(snap);
    const float                    avg    = logic::averageRating(snap);
    const float                    best   = logic::highestRating(snap);
    const long long                total  = logic::totalDurationAll(snap);
    const auto                     genres = logic::genreStats(snap);
    const int                      total3 = counts.watchlist + counts.watched + counts.owned;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    if (ImGui::BeginChild("##stats", ImVec2(0.0f, 110.0f), true)) {
        ImGui::TextDisabled("Collection Statistics");
        ImGui::Separator();

        // Status distribution bars.
        const float barMaxWidth = 160.0f;
        ImGui::Text("Total: %d", total3);
        ImGui::SameLine(90.0f);

        const float wlFrac  = total3 > 0 ? static_cast<float>(counts.watchlist) / total3 : 0.0f;
        const float wdFrac  = total3 > 0 ? static_cast<float>(counts.watched)   / total3 : 0.0f;
        const float ownFrac = total3 > 0 ? static_cast<float>(counts.owned)     / total3 : 0.0f;

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, statusTextColor(data::Status::WATCHLIST));
        char wlBuf[24]; std::snprintf(wlBuf, sizeof(wlBuf), "WL %d", counts.watchlist);
        ImGui::ProgressBar(wlFrac, ImVec2(barMaxWidth, 14.0f), wlBuf);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, statusTextColor(data::Status::WATCHED));
        char wdBuf[24]; std::snprintf(wdBuf, sizeof(wdBuf), "Seen %d", counts.watched);
        ImGui::ProgressBar(wdFrac, ImVec2(barMaxWidth, 14.0f), wdBuf);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, statusTextColor(data::Status::OWNED));
        char ownBuf[24]; std::snprintf(ownBuf, sizeof(ownBuf), "Own %d", counts.owned);
        ImGui::ProgressBar(ownFrac, ImVec2(barMaxWidth, 14.0f), ownBuf);
        ImGui::PopStyleColor();

        // Rating + duration summary.
        const long long hours = total / 60;
        const long long mins  = total % 60;
        ImGui::Text("Avg rating: %.2f  |  Best: %.1f  |  Total watch time: %lldh %02lldm",
                    static_cast<double>(avg),
                    static_cast<double>(best),
                    hours, mins);

        // Top genres.
        if (!genres.empty()) {
            ImGui::TextDisabled("Top genres:");
            ImGui::SameLine();
            constexpr std::size_t MAX_SHOWN = 8;
            for (std::size_t i = 0; i < genres.size() && i < MAX_SHOWN; ++i) {
                if (i > 0) {
                    ImGui::SameLine();
                    ImGui::TextDisabled(" |");
                    ImGui::SameLine();
                }
                ImGui::Text("%s(%d)", genres[i].first.c_str(), genres[i].second);
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ──────────────────────────────────────────────────────────────────────────────
//  Movie table
// ──────────────────────────────────────────────────────────────────────────────
void renderMovieTable(UiState& state) {
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

    const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders
                                     | ImGuiTableFlags_RowBg
                                     | ImGuiTableFlags_Resizable
                                     | ImGuiTableFlags_ScrollY
                                     | ImGuiTableFlags_Sortable;

    if (ImGui::BeginTable("movies", 10, tableFlags, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Sel",      ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableSetupColumn("Fav",      ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 22.0f);
        ImGui::TableSetupColumn("Title",    ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Director", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Genres",   ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Year",     ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("Rating",   ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Status",   ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 76.0f);
        ImGui::TableSetupColumn("Actions",  ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        // Handle column sort specs.
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
            ImGui::TableNextRow();

            // Color-code the row by status.
            const ImU32 rowBg = statusRowColor(movie.status, state.darkMode);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowBg);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, rowBg);

            ImGui::PushID(static_cast<int>(movie.id));

            // Col 0: selection checkbox.
            ImGui::TableSetColumnIndex(0);
            bool selected = state.selectedIds.count(movie.id) > 0;
            if (ImGui::Checkbox("##sel", &selected)) {
                if (selected) {
                    state.selectedIds.insert(movie.id);
                } else {
                    state.selectedIds.erase(movie.id);
                }
                refreshTotalDuration(state);
            }

            // Col 1: favorite heart.
            ImGui::TableSetColumnIndex(1);
            if (movie.favorite) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.25f, 0.30f, 1.0f));
                ImGui::TextUnformatted("<3");
                ImGui::PopStyleColor();
            }

            // Col 2: title with tooltip showing notes.
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

            // Col 6: rating progress bar.
            ImGui::TableSetColumnIndex(6);
            {
                ImVec4 barColor;
                if (movie.rating >= 7.5f)      barColor = ImVec4(0.18f, 0.75f, 0.30f, 1.0f);
                else if (movie.rating >= 5.0f) barColor = ImVec4(0.95f, 0.70f, 0.05f, 1.0f);
                else                           barColor = ImVec4(0.90f, 0.25f, 0.18f, 1.0f);

                char ratingBuf[16];
                std::snprintf(ratingBuf, sizeof(ratingBuf), "%.1f", movie.rating);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
                ImGui::ProgressBar(movie.rating / 10.0f, ImVec2(-1.0f, 0.0f), ratingBuf);
                ImGui::PopStyleColor();
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
                ImGui::PushStyleColor(ImGuiCol_Text, statusTextColor(movie.status));
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
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.25f, 0.20f, 1.0f));
            if (ImGui::SmallButton("Del")) {
                state.pendingDeleteId = movie.id;
            }
            ImGui::PopStyleColor();

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
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
//  Movie create / edit form (right panel)
// ──────────────────────────────────────────────────────────────────────────────
void renderForm(UiState& state, network::Client& client) {
    if (state.editingId == 0) {
        ImGui::SeparatorText("Add New Movie");
    } else {
        char header[280];
        std::snprintf(header, sizeof(header), "Edit: %s", state.formTitle);
        ImGui::SeparatorText(header);
    }

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
    ImGui::InputTextWithHint("##genres", "Genres (comma-separated)", state.formGenres, sizeof(state.formGenres));

    // Year and Rating side by side.
    ImGui::SetNextItemWidth((fieldW - 8.0f) * 0.40f);
    ImGui::InputInt("##year", &state.formYear);
    ImGui::SameLine();
    ImGui::SetNextItemWidth((fieldW - 8.0f) * 0.60f);
    ImGui::SliderFloat("##rating", &state.formRating, 0.0f, 10.0f, "Rating: %.1f");

    // Duration and Status side by side.
    ImGui::SetNextItemWidth((fieldW - 8.0f) * 0.40f);
    ImGui::InputInt("##dur", &state.formDuration);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Duration in minutes");
        ImGui::EndTooltip();
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

    const bool online = client.connected.load();
    if (!online) {
        ImGui::BeginDisabled();
    }

    const char* submitLabel = state.editingId == 0 ? "Create Movie" : "Save Changes";
    if (ImGui::Button(submitLabel, ImVec2(fieldW, 0.0f))) {
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
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.75f, 0.12f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.18f, 0.15f, 1.0f));
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
        ImGui::TextColored(ImVec4(0.85f, 0.25f, 0.20f, 1.0f),
                           "Connect to a server to make changes.");
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Status bar
// ──────────────────────────────────────────────────────────────────────────────
void renderStatusBar(const UiState& state) {
    const std::vector<data::Movie> snap    = logic::snapshotCollection(state.localCollection);
    const logic::StatusCounts      counts  = logic::countByStatus(snap);
    const float                    avg     = logic::averageRating(snap);

    ImGui::Separator();
    ImGui::Text("Movies: %zu  |  ", snap.size());
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, statusTextColor(data::Status::WATCHLIST));
    ImGui::Text("Watchlist: %d", counts.watchlist);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, statusTextColor(data::Status::WATCHED));
    ImGui::Text("Watched: %d", counts.watched);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, statusTextColor(data::Status::OWNED));
    ImGui::Text("Owned: %d", counts.owned);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text("  |  Avg rating: %.2f", static_cast<double>(avg));

    if (!state.selectedIds.empty()) {
        ImGui::SameLine();
        ImGui::Text("  |  Selected: %zu  (%lld min = %.1fh)",
                    state.selectedIds.size(),
                    state.totalSelectedMinutes,
                    static_cast<double>(state.totalSelectedMinutes) / 60.0);
    }

    if (!state.statusMessage.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.42f, 0.05f, 1.0f));
        ImGui::TextUnformatted(state.statusMessage.c_str());
        ImGui::PopStyleColor();
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Server traffic processing + auto-reconnect
// ──────────────────────────────────────────────────────────────────────────────
void processServerTraffic(UiState& state, network::Client& client) {
    // Auto-reconnect logic.
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

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
                                           | ImGuiWindowFlags_NoMove
                                           | ImGuiWindowFlags_NoSavedSettings
                                           | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("Movie Collection Manager", nullptr, windowFlags);

        // Top bar.
        renderConnectionBar(state, client);
        ImGui::Separator();
        renderToolbar(state, client);
        renderAdvancedFilters(state);
        renderStatsPanel(state);
        ImGui::Separator();

        // Main content: table on the left, form panel on the right.
        const float formPanelWidth  = 310.0f;
        const float statusBarHeight = 80.0f;
        const float mainHeight      = ImGui::GetContentRegionAvail().y - statusBarHeight;
        const float tablePanelWidth = ImGui::GetContentRegionAvail().x - formPanelWidth - 8.0f;

        if (ImGui::BeginChild("##table_panel", ImVec2(tablePanelWidth, mainHeight), false,
                              ImGuiWindowFlags_NoScrollbar)) {
            renderMovieTable(state);
        }
        ImGui::EndChild();

        ImGui::SameLine(0.0f, 8.0f);

        if (ImGui::BeginChild("##form_panel", ImVec2(formPanelWidth, mainHeight), false)) {
            renderForm(state, client);
        }
        ImGui::EndChild();

        renderStatusBar(state);

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
        const ImVec4 clearColor = state.darkMode
            ? ImVec4(0.11f, 0.12f, 0.15f, 1.0f)
            : ImVec4(1.0f,  1.0f,  1.0f,  1.0f);
        glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(windowHandle);
    }
}

} // namespace mcm::presentation
