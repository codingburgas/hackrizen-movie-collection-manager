/**
 * Presentation Layer - Movie Collection Manager
 *
 * Renders the GUI with Dear ImGui (GLFW + OpenGL3 backend) and drives
 * user interaction. Strictly calls into the logic layer and the network
 * client; NEVER talks to the data layer directly.
 */
#ifndef MCM_PRESENTATION_H
#define MCM_PRESENTATION_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "data.h"
#include "logic.h"
#include "network.h"
#include "ui_anim.h"

namespace mcm::presentation {

/**
 * AnimState - per-frame eased values, hover ledger, and cached frame time
 * powering the cinematic UI. Lives inside UiState.
 */
struct AnimState {
    anim::Eased totalCount;
    anim::Eased watchlistCount;
    anim::Eased watchedCount;
    anim::Eased ownedCount;
    anim::Eased avgRating;
    anim::Eased bestRating;
    anim::Eased totalHours;

    anim::Eased watchlistFraction;
    anim::Eased watchedFraction;
    anim::Eased ownedFraction;

    anim::Eased filterDrawerHeight;     // 0 = closed
    anim::Eased statsHeight;            // 0 = stats panel hidden
    anim::Eased themeFade;              // 0 = light, 1 = dark
    anim::Eased connectionPulse;        // glow opacity for online dot

    anim::HoverLedger hover;

    // Per-id favorite-pulse timer: seconds remaining (>0 = animating).
    std::unordered_map<std::uint64_t, float> favoritePulse;

    // Snapshot of last-known favorite flag, used to detect transitions.
    std::unordered_map<std::uint64_t, bool> lastFavorite;

    // Per-genre eased fraction so genre bars slide in smoothly.
    std::unordered_map<std::string, anim::Eased> genreEased;

    double lastFrameTime = 0.0;
    float  spinPhase     = 0.0f;
    bool   firstFrame    = true;
};

/**
 * UiState - all mutable UI-side data. Passed by reference to render.
 * No statics, no globals.
 */
struct UiState {
    // Local replica of the server-side collection.
    data::Collection localCollection;

    // Sorting / filtering controls.
    logic::SortKey sortKey = logic::SortKey::TITLE;
    logic::SortOrder sortOrder = logic::SortOrder::ASCENDING;
    char searchBuffer[256] = "";

    // Status filter: which statuses are currently visible.
    bool showWatchlist = true;
    bool showWatched   = true;
    bool showOwned     = true;
    bool favoritesOnly = false;

    // New-movie form fields.
    char formTitle[256]    = "";
    char formDirector[256] = "";
    char formGenres[512]   = "";
    char formNotes[2048]   = "";
    int formYear     = 2024;
    float formRating = 7.5f;
    int formDuration = 120;
    int formStatus   = 0;    // cast of data::Status
    bool formFavorite = false;

    // Edit target (0 = creating a new entry).
    std::uint64_t editingId = 0;

    // Id pending confirmation before deletion (0 = none).
    std::uint64_t pendingDeleteId = 0;

    // Selection set for the recursive duration calculation.
    std::unordered_set<std::uint64_t> selectedIds;

    // Connection form.
    char hostBuffer[128] = "127.0.0.1";
    char portBuffer[16] = "9275";

    // Last error / info line shown in the status bar.
    std::string statusMessage;

    // Cached total duration for the current selection.
    long long totalSelectedMinutes = 0;

    // Monotonic revision tracked so we know when the local mirror changed.
    std::uint64_t lastKnownRevision = 0;

    // Advanced filter controls (extended FilterCriteria).
    bool showAdvancedFilters = false;
    char genreFilter[64]     = "";
    char directorFilter[64]  = "";
    float minRatingFilter    = 0.0f;
    float maxRatingFilter    = 10.0f;
    int minYearFilter        = 1880;
    int maxYearFilter        = 2200;

    // Statistics panel visibility.
    bool showStatsPanel = false;

    // Theme / appearance.
    bool darkMode = false;

    // Auto-reconnect state.
    bool autoReconnect              = true;
    double lastReconnectAttemptTime = -99.0;

    // Signals to renderForm that the Title field should grab keyboard focus.
    bool focusTitleNextFrame = false;

    // App-bar settings popup (host/port + auto-reconnect).
    bool showSettingsPopup = false;

    // Per-frame animation state — never persisted.
    AnimState anim;
};

/**
 * initialisePresentation - creates the GLFW window and Dear ImGui context.
 *
 * @param windowTitle Title shown in the OS title bar.
 * @return true on success; inspect stderr for diagnostics on failure.
 */
bool initialisePresentation(const std::string& windowTitle);

/**
 * shutdownPresentation - tears down ImGui and GLFW in reverse order.
 */
void shutdownPresentation();

/**
 * runMainLoop - pumps the render loop until the user closes the window
 * or disconnects. Returns when the window has been requested to close.
 *
 * @param state  UI state (mutated across frames).
 * @param client Network client used to communicate with the server.
 */
void runMainLoop(UiState& state, network::Client& client);

/**
 * applyServerEvent - integrates a single decoded protocol message into
 * the local mirror. Exposed for testing / for the main loop.
 *
 * @param state   UI state to update.
 * @param message Decoded message from the server.
 */
void applyServerEvent(UiState& state, const protocol::Message& message);

} // namespace mcm::presentation

#endif // MCM_PRESENTATION_H
