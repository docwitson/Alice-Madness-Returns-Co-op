#pragma once

#include <cstdint>
#include <cstring>
#include <span>

namespace AliceCoopNative
{
    // Recognize the persistent loader's archive-cache reference, its matching
    // constructor store and 0x88 byte-buffer layout. No absolute EXE addresses.
    inline std::uint32_t FindPersistentArchiveCacheSlot(std::span<const std::uint8_t> code)
    {
        std::uint32_t result = 0;
        for (std::size_t i = 0; i + 22 <= code.size(); ++i)
        {
            if (code[i] != 0x8b || code[i + 1] != 0x35
                || code[i + 6] != 0x3b || code[i + 7] != 0xf3
                || code[i + 8] != 0x75 || code[i + 10] != 0x6a
                || code[i + 11] != 8 || code[i + 12] != 0x68
                || code[i + 13] != 0x94 || code[i + 14] || code[i + 15]
                || code[i + 16] || code[i + 17] != 0xe8) continue;
            std::uint32_t slot = 0;
            std::memcpy(&slot, code.data() + i + 2, sizeof(slot));
            if (!slot || (slot & 3)) continue;
            bool matched = false;
            for (std::size_t j = i + 22; j + 16 <= code.size() && j < i + 80; ++j)
            {
                if (code[j] != 0x89 || code[j + 1] != 0x35
                    || std::memcmp(code.data() + j + 2, &slot, sizeof(slot))) continue;
                const std::uint8_t bufferLea[] = {0x8d, 0xbe, 0x88, 0, 0, 0};
                if (std::memcmp(code.data() + j + 10, bufferLea, sizeof(bufferLea))) continue;
                matched = true;
                break;
            }
            if (!matched) continue;
            if (result) return 0; // Ambiguous/unsupported code must not be modified.
            result = slot;
        }
        return result;
    }

    struct ArchiveByteBuffer32
    {
        std::uint32_t data;
        std::int32_t count;
        std::int32_t capacity;
    };

    template <typename SeekToStart>
    bool InvalidatePersistentArchiveBuffer(ArchiveByteBuffer32& buffer, SeekToStart seek)
    {
        if (buffer.count < 0 || buffer.capacity < buffer.count
            || buffer.capacity > 64 * 1024 * 1024
            || (buffer.count && !buffer.data)) return false;
        if (!buffer.count) return true;
        if (!seek()) return false;
        // Match the native profile-reset path: retain engine-owned allocation,
        // rewind the archive, clear used length so the next load opens the file.
        buffer.count = 0;
        return true;
    }
}
