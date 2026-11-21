#include "ArtworkStore.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <utility>

static constexpr char MARKER[] = {'S', 'D', 'A', 'W'};
static constexpr uint16_t VERSION = 1;
static constexpr size_t HEADER_LENGTH = 16;

static constexpr char PICTURES_FOLDER[] = "/art";
static constexpr char TEMPORARY_FILE[] = "/art/write.tmp";

// CRC-32 as zlib and Ethernet compute it: the polynomial 0xEDB88320, reflected.
static constexpr auto CRC_TABLE = [] {
    std::array<uint32_t, 256> table{};

    for (uint32_t i = 0; i < table.size(); i++) {
        auto crc = i;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
        }
        table[i] = crc;
    }

    return table;
}();

static uint32_t compute_crc(const uint8_t* bytes, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < length; i++) {
        crc = (crc >> 8) ^ CRC_TABLE[(crc ^ bytes[i]) & 0xFF];
    }

    return crc ^ 0xFFFFFFFFu;
}

// FNV-1a, 64 bits wide.
static uint64_t hash_key(const std::string& key) {
    uint64_t hash = 0xCBF29CE484222325u;

    for (const auto c : key) {
        hash ^= uint8_t(c);
        hash *= 0x100000001B3u;
    }

    return hash;
}

static uint32_t read_u16(const uint8_t* bytes) { return bytes[0] | (bytes[1] << 8); }

static uint32_t read_u32(const uint8_t* bytes) { return read_u16(bytes) | (read_u16(bytes + 2) << 16); }

static void write_u16(uint8_t* bytes, uint32_t value) {
    bytes[0] = uint8_t(value);
    bytes[1] = uint8_t(value >> 8);
}

static void write_u32(uint8_t* bytes, uint32_t value) {
    write_u16(bytes, value);
    write_u16(bytes + 2, value >> 16);
}

// The files are read and written with POSIX's calls, which ESP-IDF's VFS hands to FAT as they are:
// newlib's stdio reads an unbuffered stream a byte at a time, and a buffered one a buffer's length
// at a time. A read or a write of a whole picture is then one call to FAT, which moves every whole
// sector of it straight between the card and the picture's bytes.

// Reads `length` bytes, or as many as the file has left. Returns how many it read, or -1 where the
// file did not answer, with errno set.
static ssize_t read_all(int file, void* bytes, size_t length) {
    size_t done = 0;

    while (done < length) {
        const auto count = read(file, (uint8_t*)bytes + done, length - done);
        if (count < 0) {
            return -1;
        }
        if (count == 0) {
            break;
        }
        done += size_t(count);
    }

    return ssize_t(done);
}

// Writes `length` bytes. Returns false where the file did not take them all, with errno set.
static bool write_all(int file, const void* bytes, size_t length) {
    size_t done = 0;

    while (done < length) {
        const auto count = write(file, (const uint8_t*)bytes + done, length - done);
        if (count <= 0) {
            if (count == 0) {
                errno = ENOSPC;
            }
            return false;
        }
        done += size_t(count);
    }

    return true;
}

// Reads a file's header and key, up to where the picture starts. Returns what makes the file not
// the one the store writes for `key`, or nullptr. A file that does not answer returns nullptr too,
// with `failed` set and errno saying why.
static const char* read_header(int file, uintmax_t file_length, const std::string& key, size_t& length, uint32_t& crc,
                               bool& failed) {
    uint8_t header[HEADER_LENGTH];
    const auto count = read_all(file, header, sizeof(header));
    if (count < 0) {
        failed = true;
        return nullptr;
    }
    if (size_t(count) != sizeof(header)) {
        return "the file is shorter than a header";
    }
    if (memcmp(header, MARKER, sizeof(MARKER)) != 0) {
        return "the marker is not the store's";
    }
    if (read_u16(header + 4) != VERSION) {
        return "the version is not one the store knows";
    }
    if (read_u16(header + 6) != key.size()) {
        return "the key is another picture's";
    }

    length = read_u32(header + 8);
    crc = read_u32(header + 12);

    if (file_length != HEADER_LENGTH + key.size() + length) {
        return "the file's length is not the header's, the key's and the picture's";
    }

    std::string stored(key.size(), '\0');
    const auto key_count = read_all(file, stored.data(), stored.size());
    if (key_count < 0) {
        failed = true;
        return nullptr;
    }
    if (size_t(key_count) != stored.size() || stored != key) {
        return "the key is another picture's";
    }

    return nullptr;
}

ArtworkStore::ArtworkStore(std::string folder, std::string format, Allocate allocate)
    : _folder(std::move(folder)), _format(std::move(format)), _allocate(std::move(allocate)) {}

void ArtworkStore::begin() {
    std::error_code error;
    std::filesystem::create_directory(_folder + PICTURES_FOLDER, error);
    if (error) {
        stop_writing("making the pictures' folder", error.value());
        return;
    }

    if (remove(get_temporary_path().c_str()) != 0 && errno != ENOENT) {
        stop(State::Off, "removing the temporary file", errno);
    }
}

ArtworkStore::Picture ArtworkStore::read(const std::string& artwork_id, int32_t size) {
    if (_state == State::Off) {
        return {};
    }

    const auto key = make_key(artwork_id, size);
    const auto path = make_path(key);

    // The open finds the file, and the length comes from it: FAT looks the path up once.
    const auto file = open(path.c_str(), O_RDONLY);
    if (file < 0) {
        if (errno != ENOENT) {
            stop(State::Off, "opening a picture", errno);
        }
        return {};
    }

    struct stat status{};
    if (fstat(file, &status) != 0) {
        const auto stat_error = errno;
        close(file);
        stop(State::Off, "looking for a picture", stat_error);
        return {};
    }

    Picture picture;
    uint32_t crc = 0;
    auto failed = false;

    picture.problem = read_header(file, uintmax_t(status.st_size), key, picture.length, crc, failed);
    if (!picture.problem && !failed) {
        auto body = _allocate(picture.length);
        if (!body) {
            close(file);
            return Picture{.problem = "there is no memory for the picture"};
        }

        const auto count = read_all(file, body.get(), picture.length);
        if (count < 0) {
            failed = true;
        } else if (size_t(count) != picture.length) {
            picture.problem = "the file is shorter than its header says";
        } else if (compute_crc(body.get(), picture.length) != crc) {
            picture.problem = "the CRC is not the picture's";
        } else {
            picture.body = std::move(body);
        }
    }

    close(file);

    // A file that does not read is deleted as one that fails its checks is. A power cut can leave
    // a picture whose clusters run into a free one: FAT fails its read, and its delete stops at that
    // cluster. A folder that does not answer fails the delete too.
    if (failed) {
        picture.problem = "the file does not read";
    }

    if (picture.problem) {
        if (remove(path.c_str()) != 0) {
            stop(State::Off, "deleting a picture", errno);
        }
        return Picture{.problem = picture.problem};
    }

    return picture;
}

bool ArtworkStore::write(const std::string& artwork_id, int32_t size, const uint8_t* body, size_t length) {
    if (_state != State::On) {
        return false;
    }

    const auto key = make_key(artwork_id, size);
    const auto path = make_path(key);

    // FAT's rename fails on a name that exists, where the host's replaces the file.
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        return true;
    }
    if (error) {
        stop(State::Off, "looking for a picture", error.value());
        return false;
    }

    const auto temporary = get_temporary_path();

    const auto file = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file < 0) {
        stop_writing("making the temporary file", errno);
        return false;
    }

    // Does not check the removal: a temporary file that stays is begin()'s to remove.
    const auto give_up = [this, &temporary](const char* doing, int doing_error) {
        remove(temporary.c_str());
        stop_writing(doing, doing_error);

        return false;
    };

    // The header and the key in one write, which FAT keeps in the file's sector buffer, and then
    // the picture from where it is.
    std::string head(HEADER_LENGTH, '\0');
    const auto header = (uint8_t*)head.data();
    memcpy(header, MARKER, sizeof(MARKER));
    write_u16(header + 4, VERSION);
    write_u16(header + 6, uint32_t(key.size()));
    write_u32(header + 8, uint32_t(length));
    write_u32(header + 12, compute_crc(body, length));
    head += key;

    errno = 0;
    const auto written = write_all(file, head.data(), head.size()) && write_all(file, body, length);
    const auto write_error = errno;

    if (close(file) != 0 && written) {
        return give_up("closing the temporary file", errno);
    }
    if (!written) {
        return give_up("writing the temporary file", write_error);
    }

    if (rename(temporary.c_str(), path.c_str()) != 0) {
        if (errno != ENOENT) {
            return give_up("renaming the temporary file", errno);
        }

        std::filesystem::create_directory(std::filesystem::path(path).parent_path(), error);
        if (error) {
            return give_up("making a picture's folder", error.value());
        }
        if (rename(temporary.c_str(), path.c_str()) != 0) {
            return give_up("renaming the temporary file", errno);
        }
    }

    return true;
}

void ArtworkStore::erase(const std::string& artwork_id, int32_t size) {
    if (_state == State::Off) {
        return;
    }

    if (remove(get_path(artwork_id, size).c_str()) != 0 && errno != ENOENT) {
        stop(State::Off, "deleting a picture", errno);
    }
}

std::string ArtworkStore::get_path(const std::string& artwork_id, int32_t size) const {
    return make_path(make_key(artwork_id, size));
}

std::string ArtworkStore::get_temporary_path() const { return _folder + TEMPORARY_FILE; }

std::string ArtworkStore::make_key(const std::string& artwork_id, int32_t size) const {
    return _format + "/" + std::to_string(size) + "/" + artwork_id;
}

std::string ArtworkStore::make_path(const std::string& key) const {
    const auto hash = hash_key(key);

    char name[sizeof("/00/00000000.bin")];
    snprintf(name, sizeof(name), "/%02x/%08x.bin", unsigned(hash >> 56), unsigned(hash & 0xFFFFFFFFu));

    return _folder + PICTURES_FOLDER + name;
}

void ArtworkStore::stop(State state, const char* doing, int error) {
    _state = state;
    _fault = Fault{.doing = doing, .error = error};
}

// FAT answers a card with no room ENOSPC where a file is written, and EACCES where a file or a
// folder is made.
void ArtworkStore::stop_writing(const char* doing, int error) {
    stop(error == ENOSPC || error == EACCES ? State::Full : State::Off, doing, error);
}
