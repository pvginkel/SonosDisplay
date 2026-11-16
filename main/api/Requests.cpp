#include "Requests.h"

#include <memory>

#include "cJSON.h"

std::string write_volume_change(const VolumeChange& change) {
    auto body = "{\"delta\":" + std::to_string(change.step);
    if (change.unmutes) {
        body += ",\"muted\":false";
    }

    return body + "}";
}

static const char* kind_name(ItemKind kind) {
    switch (kind) {
        case ItemKind::Artist:
            return "artist";
        case ItemKind::Album:
            return "album";
        case ItemKind::Track:
            return "track";
        case ItemKind::Playlist:
            return "playlist";
    }

    return "";
}

static const char* action_name(QueueAction action) {
    switch (action) {
        case QueueAction::Play:
            return "play";
        case QueueAction::Shuffle:
            return "shuffle";
        case QueueAction::Add:
            return "add";
    }

    return "";
}

// The id is the backend's own text, so cJSON writes it as a JSON string.
std::optional<std::string> write_queue_request(const QueueRequest& request) {
    const auto root = std::unique_ptr<cJSON, void (*)(cJSON*)>(cJSON_CreateObject(), cJSON_Delete);

    // The API ignores the order for every other item and action.
    const auto ordered = request.kind == ItemKind::Artist && request.action != QueueAction::Shuffle;

    if (!root || !cJSON_AddStringToObject(root.get(), "kind", kind_name(request.kind)) ||
        !cJSON_AddStringToObject(root.get(), "item_id", request.item_id.c_str()) ||
        !cJSON_AddStringToObject(root.get(), "action", action_name(request.action)) ||
        (ordered && !cJSON_AddStringToObject(root.get(), "order", "newest_first"))) {
        return std::nullopt;
    }

    const auto text = cJSON_PrintUnformatted(root.get());
    if (!text) {
        return std::nullopt;
    }

    std::string body(text);
    cJSON_free(text);

    return body;
}
