/**
 * Logic Layer implementation.
 *
 * Quicksort, linear search, binary search and a recursive aggregation.
 * Presentation calls these functions; they call the data layer.
 */
#include "logic.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace mcm::logic {

namespace {

/**
 * toLower - ASCII-only lowercasing helper. Internal.
 */
std::string toLower(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (char ch : input) {
        output.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return output;
}

/**
 * compareLess - key-based ascending less-than between two movies. Internal.
 */
bool compareLess(const data::Movie& left, const data::Movie& right, SortKey key) {
    switch (key) {
        case SortKey::TITLE:      return toLower(left.title) < toLower(right.title);
        case SortKey::YEAR:       return left.year < right.year;
        case SortKey::RATING:     return left.rating < right.rating;
        case SortKey::DURATION:   return left.durationMinutes < right.durationMinutes;
        case SortKey::DATE_ADDED: return left.dateAdded < right.dateAdded;
    }
    return false;
}

/**
 * compareForOrder - honours the requested direction on top of compareLess. Internal.
 */
bool compareForOrder(const data::Movie& left,
                     const data::Movie& right,
                     SortKey key,
                     SortOrder order) {
    const bool less = compareLess(left, right, key);
    if (order == SortOrder::ASCENDING) {
        return less;
    }
    // Descending: strict greater-than (false on equality to preserve stability
    // semantics for quicksort's partition predicate).
    return compareLess(right, left, key);
}

/**
 * partition - Lomuto partition around the rightmost pivot. Internal.
 *
 * @return Final index of the pivot element.
 */
std::ptrdiff_t partition(std::vector<data::Movie>& movies,
                         std::ptrdiff_t low,
                         std::ptrdiff_t high,
                         SortKey key,
                         SortOrder order) {
    const data::Movie pivot = movies[static_cast<std::size_t>(high)];
    std::ptrdiff_t storeIndex = low - 1;
    for (std::ptrdiff_t cursor = low; cursor < high; ++cursor) {
        if (compareForOrder(movies[static_cast<std::size_t>(cursor)], pivot, key, order)) {
            ++storeIndex;
            std::swap(movies[static_cast<std::size_t>(storeIndex)],
                      movies[static_cast<std::size_t>(cursor)]);
        }
    }
    std::swap(movies[static_cast<std::size_t>(storeIndex + 1)],
              movies[static_cast<std::size_t>(high)]);
    return storeIndex + 1;
}

/**
 * sumDurationsImpl - helper for totalDurationRecursive that walks selectedIds
 * with O(1) per-step lookups into a pre-built id→duration map.
 */
long long sumDurationsImpl(
    const std::unordered_map<std::uint64_t, int>& durationMap,
    const std::vector<std::uint64_t>& selectedIds,
    std::size_t cursor) {
    if (cursor >= selectedIds.size()) {
        return 0;
    }
    const auto it = durationMap.find(selectedIds[cursor]);
    const long long current = (it != durationMap.end()) ? it->second : 0LL;
    return current + sumDurationsImpl(durationMap, selectedIds, cursor + 1);
}

/**
 * quickSortRecursive - classic recursive quicksort driver. Internal.
 */
void quickSortRecursive(std::vector<data::Movie>& movies,
                        std::ptrdiff_t low,
                        std::ptrdiff_t high,
                        SortKey key,
                        SortOrder order) {
    if (low >= high) {
        return;
    }
    const std::ptrdiff_t pivotIndex = partition(movies, low, high, key, order);
    quickSortRecursive(movies, low, pivotIndex - 1, key, order);
    quickSortRecursive(movies, pivotIndex + 1, high, key, order);
}

} // namespace

void sortMovies(std::vector<data::Movie>& movies, SortKey key, SortOrder order) {
    if (movies.size() < 2) {
        return;
    }
    quickSortRecursive(
        movies,
        0,
        static_cast<std::ptrdiff_t>(movies.size()) - 1,
        key,
        order);
}

std::vector<std::size_t> linearSearchByTitle(
    const std::vector<data::Movie>& movies,
    const std::string& needle) {
    std::vector<std::size_t> matches;
    if (needle.empty()) {
        return matches;
    }
    const std::string loweredNeedle = toLower(needle);
    for (std::size_t index = 0; index < movies.size(); ++index) {
        const std::string haystack = toLower(movies[index].title);
        if (haystack.find(loweredNeedle) != std::string::npos) {
            matches.push_back(index);
        }
    }
    return matches;
}

std::size_t binarySearchExactTitle(
    const std::vector<data::Movie>& sortedMovies,
    const std::string& title) {
    std::ptrdiff_t low = 0;
    std::ptrdiff_t high = static_cast<std::ptrdiff_t>(sortedMovies.size()) - 1;
    const std::string loweredTarget = toLower(title);
    while (low <= high) {
        const std::ptrdiff_t mid = low + (high - low) / 2;
        const std::string loweredMid = toLower(sortedMovies[static_cast<std::size_t>(mid)].title);
        if (loweredMid == loweredTarget) {
            return static_cast<std::size_t>(mid);
        }
        if (loweredMid < loweredTarget) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

long long totalDurationRecursive(
    const std::vector<data::Movie>& movies,
    const std::vector<std::uint64_t>& selectedIds,
    std::size_t cursor) {
    // Build an O(1) id→duration map on first entry, then delegate to the
    // recursive helper to keep per-step lookups cheap.
    if (cursor == 0) {
        std::unordered_map<std::uint64_t, int> durationMap;
        durationMap.reserve(movies.size());
        for (const data::Movie& movie : movies) {
            durationMap[movie.id] = movie.durationMinutes;
        }
        return sumDurationsImpl(durationMap, selectedIds, 0);
    }
    // cursor != 0 path retained for callers that enter mid-sequence, though
    // no such call sites exist in the current codebase.
    if (cursor >= selectedIds.size()) {
        return 0;
    }
    const std::size_t index = data::findIndexById(movies, selectedIds[cursor]);
    const long long current = (index == std::numeric_limits<std::size_t>::max())
        ? 0
        : static_cast<long long>(movies[index].durationMinutes);
    return current + totalDurationRecursive(movies, selectedIds, cursor + 1);
}

float averageRating(const std::vector<data::Movie>& movies) {
    if (movies.empty()) {
        return 0.0f;
    }
    double accumulator = 0.0;
    for (const data::Movie& movie : movies) {
        accumulator += movie.rating;
    }
    return static_cast<float>(accumulator / static_cast<double>(movies.size()));
}

bool validateMovie(const data::Movie& movie, std::string& errorMessage) {
    if (movie.title.empty()) {
        errorMessage = "Title cannot be empty.";
        return false;
    }
    if (movie.title.size() > data::MAX_TITLE_LENGTH) {
        std::ostringstream stream;
        stream << "Title must be at most " << data::MAX_TITLE_LENGTH << " characters.";
        errorMessage = stream.str();
        return false;
    }
    if (movie.year < data::MIN_YEAR || movie.year > data::MAX_YEAR) {
        std::ostringstream stream;
        stream << "Year must be between " << data::MIN_YEAR
               << " and " << data::MAX_YEAR << ".";
        errorMessage = stream.str();
        return false;
    }
    if (movie.rating < data::MIN_RATING || movie.rating > data::MAX_RATING) {
        errorMessage = "Rating must be between 0 and 10.";
        return false;
    }
    if (movie.durationMinutes <= 0) {
        errorMessage = "Duration must be positive.";
        return false;
    }
    errorMessage.clear();
    return true;
}

std::vector<data::Movie> filterByStatus(
    const std::vector<data::Movie>& movies,
    data::Status status) {
    std::vector<data::Movie> result;
    result.reserve(movies.size());
    for (const data::Movie& movie : movies) {
        if (movie.status == status) {
            result.push_back(movie);
        }
    }
    return result;
}

std::vector<data::Movie> applyFilters(
    const std::vector<data::Movie>& movies,
    const FilterCriteria& criteria) {
    std::vector<data::Movie> result;
    result.reserve(movies.size());
    const std::string loweredNeedle   = toLower(criteria.titleSubstring);
    const std::string loweredGenre    = toLower(criteria.genreFilter);
    const std::string loweredDirector = toLower(criteria.directorFilter);
    for (const data::Movie& movie : movies) {
        if (criteria.favoritesOnly && !movie.favorite) {
            continue;
        }
        switch (movie.status) {
            case data::Status::WATCHLIST: if (!criteria.showWatchlist) continue; break;
            case data::Status::WATCHED:   if (!criteria.showWatched)   continue; break;
            case data::Status::OWNED:     if (!criteria.showOwned)     continue; break;
        }
        if (movie.rating < criteria.minRating || movie.rating > criteria.maxRating) {
            continue;
        }
        if (movie.year < criteria.minYear || movie.year > criteria.maxYear) {
            continue;
        }
        if (!loweredNeedle.empty()) {
            if (toLower(movie.title).find(loweredNeedle) == std::string::npos) {
                continue;
            }
        }
        if (!loweredGenre.empty()) {
            if (toLower(movie.genres).find(loweredGenre) == std::string::npos) {
                continue;
            }
        }
        if (!loweredDirector.empty()) {
            if (toLower(movie.director).find(loweredDirector) == std::string::npos) {
                continue;
            }
        }
        result.push_back(movie);
    }
    return result;
}

StatusCounts countByStatus(const std::vector<data::Movie>& movies) {
    StatusCounts counts;
    for (const data::Movie& movie : movies) {
        switch (movie.status) {
            case data::Status::WATCHLIST: ++counts.watchlist; break;
            case data::Status::WATCHED:   ++counts.watched;   break;
            case data::Status::OWNED:     ++counts.owned;     break;
        }
    }
    return counts;
}

std::vector<std::pair<std::string, int>> genreStats(
    const std::vector<data::Movie>& movies) {
    std::unordered_map<std::string, int> counts;
    for (const data::Movie& movie : movies) {
        if (movie.genres.empty()) {
            continue;
        }
        std::istringstream stream(movie.genres);
        std::string token;
        while (std::getline(stream, token, ',')) {
            const std::size_t start = token.find_first_not_of(" \t");
            const std::size_t end   = token.find_last_not_of(" \t");
            if (start == std::string::npos) {
                continue;
            }
            std::string trimmed = token.substr(start, end - start + 1);
            if (trimmed.empty()) {
                continue;
            }
            trimmed[0] = static_cast<char>(
                std::toupper(static_cast<unsigned char>(trimmed[0])));
            ++counts[trimmed];
        }
    }
    std::vector<std::pair<std::string, int>> result(counts.begin(), counts.end());
    std::sort(result.begin(), result.end(),
              [](const std::pair<std::string, int>& a,
                 const std::pair<std::string, int>& b) {
                  return a.second > b.second;
              });
    return result;
}

float highestRating(const std::vector<data::Movie>& movies) {
    float best = 0.0f;
    for (const data::Movie& movie : movies) {
        if (movie.rating > best) {
            best = movie.rating;
        }
    }
    return best;
}

long long totalDurationAll(const std::vector<data::Movie>& movies) {
    long long total = 0;
    for (const data::Movie& movie : movies) {
        total += static_cast<long long>(movie.durationMinutes);
    }
    return total;
}

std::vector<data::Movie> snapshotCollection(const data::Collection& collection) {
    return data::snapshotMovies(collection);
}

void replaceMirror(data::Collection& collection,
                   const std::vector<data::Movie>& movies,
                   std::uint64_t revision) {
    data::replaceAll(collection, movies, revision);
}

bool applyEventToMirror(data::Collection& collection,
                        const protocol::Message& message) {
    switch (message.kind) {
        case protocol::MessageKind::EVENT_ADDED:
            return data::addMovieWithId(collection, message.payload);
        case protocol::MessageKind::EVENT_UPDATED:
            return data::updateMovie(collection, message.payload);
        case protocol::MessageKind::EVENT_REMOVED:
            return data::removeMovie(collection, message.targetId);
        default:
            return false;
    }
}

} // namespace mcm::logic
