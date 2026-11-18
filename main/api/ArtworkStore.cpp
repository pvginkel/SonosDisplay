#include "ArtworkStore.h"

#include <algorithm>
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

// Reads a file's header and key, up to where the picture starts. Returns what makes the file not
// the one the store writes for `key`, or nullptr.
static const char* read_header(FILE* file, uintmax_t file_length, const std::string& key, size_t& length,
                               uint32_t& crc) {
    uint8_t header[HEADER_LENGTH];
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
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
    if (fread(stored.data(), 1, stored.size(), file) != stored.size() || stored != key) {
        return "the key is another picture's";
    }

    return nullptr;
}

namespace {

// Writes to a file through a buffer, a buffer's length at a time.
class Writer {
    FILE* _file;
    uint8_t* _buffer;
    size_t _buffer_length;
    size_t _used{};

public:
    Writer(FILE* file, uint8_t* buffer, size_t buffer_length)
        : _file(file), _buffer(buffer), _buffer_length(buffer_length) {}

    bool put(const void* bytes, size_t length) {
        auto from = (const uint8_t*)bytes;

        while (length > 0) {
            const auto count = std::min(length, _buffer_length - _used);
            memcpy(_buffer + _used, from, count);
            _used += count;
            from += count;
            length -= count;

            if (_used == _buffer_length && !flush()) {
                return false;
            }
        }

        return true;
    }

    bool flush() {
        const auto count = std::exchange(_used, 0);

        return fwrite(_buffer, 1, count, _file) == count;
    }
};

}  // namespace

ArtworkStore::ArtworkStore(std::string folder, std::string format, uint8_t* buffer, size_t buffer_length,
                           Allocate allocate)
    : _folder(std::move(folder)),
      _format(std::move(format)),
      _buffer(buffer),
      _buffer_length(buffer_length),
      _allocate(std::move(allocate)) {}

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

    std::error_code error;
    const auto file_length = std::filesystem::file_size(path, error);
    if (error == std::errc::no_such_file_or_directory) {
        return {};
    }
    if (error) {
        stop(State::Off, "looking for a picture", error.value());
        return {};
    }

    const auto file = fopen(path.c_str(), "rb");
    if (!file) {
        stop(State::Off, "opening a picture", errno);
        return {};
    }

    // newlib's fread() fills the stream's buffer, a buffer's length at a time, and copies out of
    // it. With no buffer, it reads a byte at a time.
    setvbuf(file, (char*)_buffer, _IOFBF, _buffer_length);

    Picture picture;
    uint32_t crc = 0;
    errno = 0;

    picture.problem = read_header(file, file_length, key, picture.length, crc);
    if (!picture.problem) {
        auto body = _allocate(picture.length);
        if (!body) {
            fclose(file);
            return Picture{.problem = "there is no memory for the picture"};
        }

        if (fread(body.get(), 1, picture.length, file) != picture.length) {
            picture.problem = "the file is shorter than its header says";
        } else if (compute_crc(body.get(), picture.length) != crc) {
            picture.problem = "the CRC is not the picture's";
        } else {
            picture.body = std::move(body);
        }
    }

    const auto failed = ferror(file) != 0;
    const auto read_error = errno;
    fclose(file);

    if (failed) {
        stop(State::Off, "reading a picture", read_error);
        return {};
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

    const auto file = fopen(temporary.c_str(), "wb");
    if (!file) {
        stop_writing("making the temporary file", errno);
        return false;
    }

    // newlib writes what an unbuffered stream is given from where it is, in one piece.
    setvbuf(file, nullptr, _IONBF, 0);

    // Does not check the removal: a temporary file that stays is begin()'s to remove.
    const auto give_up = [this, &temporary](const char* doing, int doing_error) {
        remove(temporary.c_str());
        stop_writing(doing, doing_error);

        return false;
    };

    uint8_t header[HEADER_LENGTH];
    memcpy(header, MARKER, sizeof(MARKER));
    write_u16(header + 4, VERSION);
    write_u16(header + 6, uint32_t(key.size()));
    write_u32(header + 8, uint32_t(length));
    write_u32(header + 12, compute_crc(body, length));

    Writer writer(file, _buffer, _buffer_length);
    errno = 0;

    const auto written = writer.put(header, sizeof(header)) && writer.put(key.data(), key.size()) &&
                         writer.put(body, length) && writer.flush();
    const auto write_error = errno;

    if (fclose(file) != 0 && written) {
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
