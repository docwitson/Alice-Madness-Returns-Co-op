#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace AliceCoopNative
{
    // Read only side-effect-free UE3 object expressions. Do not advance FFrame
    // or evaluate functions: the original exec must consume its own arguments.
    template <typename ReadWord, typename PropertyOffset>
    bool ReadSoundArgument(std::span<const std::uint8_t> code,
        std::uint32_t object, std::uint32_t locals, ReadWord read,
        PropertyOffset offset, std::uint32_t& cue)
    {
        if (code.empty()) return false;
        if (code[0] == 0x25 || code[0] == 0x26) { cue = code[0] - 0x25; return true; }
        if (code.size() < 5) return false;
        if (code[0] == 0x10) // EX_DynArrayElement: index, then array expression
        {
            std::uint32_t index = 0;
            if (code[1] != 0 && code[1] != 1 && code[1] != 0x1d && code[1] != 0x25 && code[1] != 0x26) return false;
            std::size_t indexBytes = code[1] == 0x25 || code[1] == 0x26 ? 1 : 5;
            if (!ReadSoundArgument(code.subspan(1), object, locals, read, offset, index)
                || code.size() < 1 + indexBytes + 5) return false;
            auto array = code.subspan(1 + indexBytes);
            if (array[0] != 0 && array[0] != 1) return false;
            std::uint32_t property = 0; std::int32_t field = -1;
            std::memcpy(&property, array.data() + 1, 4);
            if (!offset(property, field) || field < 0 || field > 0x10000) return false;
            const auto base = array[0] == 0 ? locals : object;
            if (!base || base > UINT32_MAX - static_cast<std::uint32_t>(field) - 4) return false;
            std::uint32_t data = 0, count = 0;
            if (!read(base + field, data) || !read(base + field + 4, count)
                || !data || count > 1024 || index >= count || data > UINT32_MAX - index * 4) return false;
            return read(data + index * 4, cue);
        }
        std::uint32_t operand = 0;
        std::memcpy(&operand, code.data() + 1, 4);
        if (code[0] == 0x1d) { cue = operand; return true; } // EX_IntConst (array index)
        if (code[0] == 0x20) { cue = operand; return true; } // EX_ObjectConst
        if (code[0] != 0x00 && code[0] != 0x01) return false;
        std::int32_t propertyOffset = -1;
        if (!offset(operand, propertyOffset) || propertyOffset < 0
            || propertyOffset > 0x10000) return false;
        const auto base = code[0] == 0x00 ? locals : object;
        if (!base || base > UINT32_MAX - static_cast<std::uint32_t>(propertyOffset)) return false;
        return read(base + propertyOffset, cue);
    }

    inline bool IsFreshAudioEvent(std::uint64_t now, std::uint64_t received,
        bool persistent)
    {
        return now >= received && now - received <= (persistent ? 600u : 350u);
    }
}
