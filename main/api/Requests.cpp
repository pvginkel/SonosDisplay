#include "Requests.h"

std::string write_volume_change(const VolumeChange& change) {
    auto body = "{\"delta\":" + std::to_string(change.step);
    if (change.unmutes) {
        body += ",\"muted\":false";
    }

    return body + "}";
}
