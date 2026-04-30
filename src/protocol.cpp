/**
 * Protocol implementation - JSON <-> Message converters.
 */
#include "protocol.h"

#include <unordered_map>

#include <nlohmann/json.hpp>

namespace mcm::protocol {

namespace {

const std::unordered_map<MessageKind, std::string> KIND_TO_STRING = {
    {MessageKind::HELLO,          "hello"},
    {MessageKind::FULL_STATE,     "full_state"},
    {MessageKind::EVENT_ADDED,    "event_added"},
    {MessageKind::EVENT_UPDATED,  "event_updated"},
    {MessageKind::EVENT_REMOVED,  "event_removed"},
    {MessageKind::REQUEST_ADD,    "request_add"},
    {MessageKind::REQUEST_UPDATE, "request_update"},
    {MessageKind::REQUEST_REMOVE, "request_remove"},
    {MessageKind::REQUEST_SYNC,   "request_sync"},
    {MessageKind::ERROR_REPLY,    "error"},
};

// Reverse of KIND_TO_STRING — used by kindFromString for O(1) lookup.
const std::unordered_map<std::string, MessageKind> STRING_TO_KIND = [] {
    std::unordered_map<std::string, MessageKind> m;
    m.reserve(KIND_TO_STRING.size());
    for (const auto& [kind, str] : KIND_TO_STRING) {
        m[str] = kind;
    }
    return m;
}();

/**
 * kindFromString - O(1) reverse lookup for the "type" field. Internal.
 */
MessageKind kindFromString(const std::string& text) {
    const auto it = STRING_TO_KIND.find(text);
    return (it != STRING_TO_KIND.end()) ? it->second : MessageKind::UNKNOWN;
}

} // namespace

std::string encodeMessage(const Message& message) {
    nlohmann::json root;
    auto kindEntry = KIND_TO_STRING.find(message.kind);
    root["type"] = (kindEntry != KIND_TO_STRING.end()) ? kindEntry->second : "unknown";
    root["revision"] = message.revision;
    root["sessionId"] = message.sessionId;

    switch (message.kind) {
        case MessageKind::HELLO:
        case MessageKind::FULL_STATE: {
            nlohmann::json array = nlohmann::json::array();
            for (const data::Movie& movie : message.snapshot) {
                array.push_back(data::movieToJson(movie));
            }
            root["movies"] = std::move(array);
            break;
        }
        case MessageKind::EVENT_ADDED:
        case MessageKind::EVENT_UPDATED:
        case MessageKind::REQUEST_ADD:
        case MessageKind::REQUEST_UPDATE:
            root["movie"] = data::movieToJson(message.payload);
            break;
        case MessageKind::EVENT_REMOVED:
        case MessageKind::REQUEST_REMOVE:
            root["targetId"] = message.targetId;
            break;
        case MessageKind::ERROR_REPLY:
            root["error"] = message.errorText;
            break;
        case MessageKind::REQUEST_SYNC:
        case MessageKind::UNKNOWN:
        default:
            break;
    }
    return root.dump();
}

bool decodeMessage(const std::string& text, Message& out) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error&) {
        return false;
    }
    if (!root.is_object() || !root.contains("type") || !root["type"].is_string()) {
        return false;
    }

    out = Message{};
    out.kind = kindFromString(root["type"].get<std::string>());
    out.revision = root.value("revision", std::uint64_t{0});
    out.sessionId = root.value("sessionId", std::uint64_t{0});

    switch (out.kind) {
        case MessageKind::HELLO:
        case MessageKind::FULL_STATE: {
            if (root.contains("movies") && root["movies"].is_array()) {
                for (const auto& node : root["movies"]) {
                    out.snapshot.push_back(data::movieFromJson(node));
                }
            }
            break;
        }
        case MessageKind::EVENT_ADDED:
        case MessageKind::EVENT_UPDATED:
        case MessageKind::REQUEST_ADD:
        case MessageKind::REQUEST_UPDATE: {
            if (root.contains("movie") && root["movie"].is_object()) {
                out.payload = data::movieFromJson(root["movie"]);
            }
            break;
        }
        case MessageKind::EVENT_REMOVED:
        case MessageKind::REQUEST_REMOVE:
            out.targetId = root.value("targetId", std::uint64_t{0});
            break;
        case MessageKind::ERROR_REPLY:
            out.errorText = root.value("error", std::string{});
            break;
        case MessageKind::REQUEST_SYNC:
        case MessageKind::UNKNOWN:
        default:
            break;
    }
    return true;
}

} // namespace mcm::protocol
