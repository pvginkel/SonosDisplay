#pragma once

// A folder of the host's that stands in for the display's TF card: new and empty, and removed with
// all it holds once the scene lets go of it, so no scene meets what another one kept.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

class CardFolder {
    std::string _path;

public:
    CardFolder() : _path((std::filesystem::temp_directory_path() / "sonos-display-card-XXXXXX").string()) {
        if (!mkdtemp(_path.data())) {
            fprintf(stderr, "render: cannot make the folder %s\n", _path.c_str());
            exit(1);
        }
    }

    CardFolder(const CardFolder&) = delete;
    CardFolder& operator=(const CardFolder&) = delete;

    ~CardFolder() {
        std::error_code error;
        std::filesystem::remove_all(_path, error);
        if (error) {
            fprintf(stderr, "render: cannot remove the folder %s\n", _path.c_str());
            exit(1);
        }
    }

    const std::string& get_path() const { return _path; }
};
