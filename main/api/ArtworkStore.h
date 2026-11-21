#pragma once

// The pictures kept in a folder: the TF card's on the device, one of the host's in sim/. A picture
// is kept under its key, which is the format it was asked for in, its size and its artwork id, in
// <folder>/art/<2 hex>/<8 hex>.bin. The name is a hash of the key, since the ids differ in case and
// FAT's names do not. A file is a header and then the picture's bytes as they arrived:
//
//    0  4  "SDAW"
//    4  2  the version, 1
//    6  2  the key's length
//    8  4  the picture's length
//   12  4  the CRC-32 of the picture's bytes
//   16     the key, "<format>/<size>/<artwork id>", and then the picture
//
// with every number little-endian. The card outlives every build: the name and the header are the
// same in each, and on every platform.
//
// A picture is written to <folder>/art/write.tmp, which is closed and then renamed into place, so
// a power cut leaves no picture half written; begin() removes the temporary file it leaves. A
// picture the folder holds is not written again, and the store writes nothing else: no index and
// no log.
//
// It includes the C++ standard library, Artwork.h and POSIX's file calls only, which ESP-IDF's VFS
// and the host both have, so sim/ builds it too. One task makes every call.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "Artwork.h"

class ArtworkStore {
public:
    // On: pictures are read and written. Full: the folder has no room left, so what it holds is
    // read and nothing is written. Off: the folder failed, so nothing is read or written. A store
    // never goes back to On.
    enum class State { On, Full, Off };

    // What stopped the writes or turned the store off, for the log: what the store was doing, and
    // the errno that came of it.
    struct Fault {
        const char* doing;
        int error;
    };

    // Where the bytes of a picture of `length` bytes go, or nullptr where there is no memory for
    // them. LVGL asks for the address to be a multiple of 4.
    using Allocate = std::function<std::shared_ptr<uint8_t>(size_t length)>;

    // What a read brings.
    struct Picture {
        // The picture's bytes as they were written, or nullptr: the folder does not hold it.
        ArtworkBody body;
        size_t length{};
        // Why a file that is there brought no picture, for the log, or nullptr: it was not the
        // picture's or did not read, and is deleted, or there is no memory for the picture and it
        // stays.
        const char* problem{};
    };

private:
    std::string _folder;
    std::string _format;
    Allocate _allocate;
    State _state{State::On};
    Fault _fault{};

public:
    // Keeps the pictures asked for in `format` in `folder`. A picture read is put where `allocate`
    // says, and is read there in one piece, as a picture written is written from where it is: on
    // the device FAT then asks the card for the sectors of an allocation unit in one command.
    ArtworkStore(std::string folder, std::string format, Allocate allocate);
    ArtworkStore(const ArtworkStore&) = delete;
    ArtworkStore& operator=(const ArtworkStore&) = delete;

    // Makes the pictures' folder where there is none, and removes the temporary file a write that
    // was cut short left. Comes before the first read or write.
    void begin();

    // The picture, checked against its header. A file that is not there is a miss. So is one that
    // is not what the store writes for this key, by its marker, version, key, length or CRC, and
    // one that does not read, as a file a power cut damaged may not; it is deleted, and `problem`
    // says what was wrong. Without memory for the picture there is none either: `problem` says so,
    // and the file stays. Only a folder that does not answer turns the store off: at the open, or
    // at the delete.
    Picture read(const std::string& artwork_id, int32_t size);

    // Keeps the picture, and returns whether the folder holds it from here on. A folder with no
    // room, at whichever step of the write, stops the writes; any other failure turns the store
    // off. Neither leaves the temporary file behind.
    bool write(const std::string& artwork_id, int32_t size, const uint8_t* body, size_t length);

    // Deletes the picture's file, where there is one, so that a write keeps the picture again. A
    // folder that does not answer turns the store off.
    void erase(const std::string& artwork_id, int32_t size);

    State get_state() const { return _state; }

    // What stopped the writes or turned the store off, where get_state() is not On.
    const Fault& get_fault() const { return _fault; }

    // The file the picture is kept in.
    std::string get_path(const std::string& artwork_id, int32_t size) const;

    // The file a picture is written to before it is renamed into place.
    std::string get_temporary_path() const;

private:
    std::string make_key(const std::string& artwork_id, int32_t size) const;
    std::string make_path(const std::string& key) const;
    void stop(State state, const char* doing, int error);
    void stop_writing(const char* doing, int error);
};
