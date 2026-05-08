/**
 * Data Layer implementation.
 *
 * Uses nlohmann::json for persistence and std::mutex for thread safety.
 * All functions are free functions operating on the Collection struct.
 */
#include "data.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

namespace mcm::data {

namespace {

std::string clampString(const std::string& s, std::size_t maxLen) {
    return (s.size() <= maxLen) ? s : s.substr(0, maxLen);
}

std::string clampTitle(const std::string& title) {
    return clampString(title, MAX_TITLE_LENGTH);
}

bool isValidStatus(int v) {
    return v >= 0 && v <= 2;
}

// Returns false for records that should be rejected on load (corrupt file data).
bool isValidForLoad(const Movie& movie) {
    return !movie.title.empty()
        && movie.year >= MIN_YEAR && movie.year <= MAX_YEAR
        && movie.rating >= MIN_RATING && movie.rating <= MAX_RATING
        && movie.durationMinutes > 0;
}

} // namespace

nlohmann::json movieToJson(const Movie& movie) {
    return nlohmann::json{
        {"id",              movie.id},
        {"title",           movie.title},
        {"year",            movie.year},
        {"rating",          movie.rating},
        {"durationMinutes", movie.durationMinutes},
        {"status",          static_cast<int>(movie.status)},
        {"favorite",        movie.favorite},
        {"director",        movie.director},
        {"genres",          movie.genres},
        {"notes",           movie.notes},
        {"dateAdded",       movie.dateAdded},
    };
}

Movie movieFromJson(const nlohmann::json& node) {
    Movie movie;
    movie.id              = node.value("id",              std::uint64_t{0});
    movie.title           = node.value("title",           std::string{});
    movie.year            = node.value("year",            0);
    movie.rating          = node.value("rating",          0.0f);
    movie.durationMinutes = node.value("durationMinutes", 0);
    const int statusRaw   = node.value("status", 0);
    movie.status          = static_cast<Status>(isValidStatus(statusRaw) ? statusRaw : 0);
    movie.favorite        = node.value("favorite",  false);
    movie.director        = clampString(node.value("director", std::string{}), MAX_DIRECTOR_LENGTH);
    movie.genres          = clampString(node.value("genres",   std::string{}), MAX_GENRES_LENGTH);
    movie.notes           = clampString(node.value("notes",    std::string{}), MAX_NOTES_LENGTH);
    movie.dateAdded       = node.value("dateAdded", std::uint64_t{0});
    return movie;
}

namespace {
void clampMovieStrings(Movie& m) {
    m.title    = clampTitle(m.title);
    m.director = clampString(m.director, MAX_DIRECTOR_LENGTH);
    m.genres   = clampString(m.genres,   MAX_GENRES_LENGTH);
    m.notes    = clampString(m.notes,    MAX_NOTES_LENGTH);
}
} // namespace
cmake --build build --config Debug - j

std::uint64_t addMovie(Collection& collection, const Movie& prototype) {
    std::lock_guard<std::mutex> lock(collection.mutex);
    Movie stored = prototype;
    stored.id = collection.nextId++;
    if (stored.dateAdded == 0) {
        stored.dateAdded = static_cast<std::uint64_t>(std::time(nullptr));
    }
    clampMovieStrings(stored);
    collection.movies.push_back(std::move(stored));
    ++collection.revision;
    return collection.movies.back().id;
}

bool addMovieWithId(Collection& collection, const Movie& movie) {
    if (movie.id == 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(collection.mutex);
    for (const Movie& existing : collection.movies) {
        if (existing.id == movie.id) {
            return false;
        }
    }
    Movie stored = movie;
    clampMovieStrings(stored);
    collection.movies.push_back(std::move(stored));
    if (collection.nextId <= movie.id) {
        collection.nextId = movie.id + 1;
    }
    ++collection.revision;
    return true;
}

bool updateMovie(Collection& collection, const Movie& movie) {
    std::lock_guard<std::mutex> lock(collection.mutex);
    for (Movie& existing : collection.movies) {
        if (existing.id == movie.id) {
            existing.title    = clampTitle(movie.title);
            existing.year     = movie.year;
            existing.rating   = movie.rating;
            existing.durationMinutes = movie.durationMinutes;
            existing.status   = movie.status;
            existing.favorite = movie.favorite;
            existing.director = clampString(movie.director, MAX_DIRECTOR_LENGTH);
            existing.genres   = clampString(movie.genres,   MAX_GENRES_LENGTH);
            existing.notes    = clampString(movie.notes,    MAX_NOTES_LENGTH);
            // Preserve original dateAdded; do not overwrite on update.
            if (movie.dateAdded != 0) {
                existing.dateAdded = movie.dateAdded;
            }
            ++collection.revision;
            return true;
        }
    }
    return false;
}

bool removeMovie(Collection& collection, std::uint64_t id) {
    std::lock_guard<std::mutex> lock(collection.mutex);
    auto iterator = std::find_if(
        collection.movies.begin(),
        collection.movies.end(),
        [id](const Movie& movie) { return movie.id == id; });
    if (iterator == collection.movies.end()) {
        return false;
    }
    collection.movies.erase(iterator);
    ++collection.revision;
    return true;
}

std::vector<Movie> snapshotMovies(const Collection& collection) {
    std::lock_guard<std::mutex> lock(collection.mutex);
    return collection.movies;
}

std::uint64_t currentRevision(const Collection& collection) {
    std::lock_guard<std::mutex> lock(collection.mutex);
    return collection.revision;
}

std::size_t findIndexById(const std::vector<Movie>& movies, std::uint64_t id) {
    for (std::size_t index = 0; index < movies.size(); ++index) {
        if (movies[index].id == id) {
            return index;
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

bool loadFromFile(Collection& collection, const std::string& path) {
    std::ifstream stream(path);
    if (!stream.is_open()) {
        return false;
    }
    nlohmann::json root;
    try {
        stream >> root;
    } catch (const nlohmann::json::parse_error&) {
        return false;
    }

    std::lock_guard<std::mutex> lock(collection.mutex);
    collection.movies.clear();
    collection.nextId = 1;
    if (root.contains("movies") && root["movies"].is_array()) {
        for (const auto& node : root["movies"]) {
            Movie movie = movieFromJson(node);
            if (movie.id == 0) {
                movie.id = collection.nextId;
            }
            movie.title = clampTitle(movie.title);
            if (!isValidForLoad(movie)) {
                std::clog << "[data] skipping invalid record (id=" << movie.id
                          << ") during load\n";
                continue;
            }
            if (collection.nextId <= movie.id) {
                collection.nextId = movie.id + 1;
            }
            collection.movies.push_back(std::move(movie));
        }
    }
    ++collection.revision;
    return true;
}

bool saveToFile(const Collection& collection, const std::string& path) {
    std::vector<Movie> snapshot;
    std::uint64_t revision = 0;
    {
        std::lock_guard<std::mutex> lock(collection.mutex);
        snapshot = collection.movies;
        revision = collection.revision;
    }

    nlohmann::json root;
    root["version"]  = SCHEMA_VERSION;
    root["revision"] = revision;
    root["movies"] = nlohmann::json::array();
    for (const Movie& movie : snapshot) {
        root["movies"].push_back(movieToJson(movie));
    }

    // Write to a temporary file first so a crash mid-write cannot corrupt
    // the live persistence file.
    const std::string tmpPath = path + ".tmp";
    {
        std::ofstream stream(tmpPath, std::ios::trunc);
        if (!stream.is_open()) {
            return false;
        }
        stream << root.dump(2);
        if (!stream.good()) {
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        std::clog << "[data] atomic rename failed: " << ec.message() << "\n";
        std::filesystem::remove(tmpPath);
        return false;
    }
    return true;
}

void replaceAll(Collection& collection,
                const std::vector<Movie>& movies,
                std::uint64_t revision) {
    std::lock_guard<std::mutex> lock(collection.mutex);
    collection.movies = movies;
    collection.revision = revision;
    std::uint64_t maxId = 0;
    for (const Movie& movie : movies) {
        if (movie.id > maxId) {
            maxId = movie.id;
        }
    }
    collection.nextId = maxId + 1;
}

} // namespace mcm::data
