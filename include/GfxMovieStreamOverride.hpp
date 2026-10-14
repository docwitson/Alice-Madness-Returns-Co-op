#pragma once

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

// The GFx root loader has already cached its end position before it is hooked.
// Keep that bound consistent with a larger compiled movie for the entire load.
class GfxMovieStreamOverride
{
public:
    GfxMovieStreamOverride() = default;
    GfxMovieStreamOverride(const GfxMovieStreamOverride&) = delete;
    GfxMovieStreamOverride& operator=(const GfxMovieStreamOverride&) = delete;

    bool Apply(std::uint8_t*& data, std::uint32_t& size, std::uint32_t& end,
        const std::vector<std::uint8_t>& compiled)
    {
        if (dataSlot_ || !data || size < 8 || end != size
            || compiled.size() < 8 || compiled.size() > std::numeric_limits<std::uint32_t>::max())
            return false;
        std::uint32_t declaredSize = 0;
        std::memcpy(&declaredSize, compiled.data() + 4, sizeof(declaredSize));
        if (declaredSize != compiled.size()) return false;

        dataSlot_ = &data;
        sizeSlot_ = &size;
        endSlot_ = &end;
        originalData_ = data;
        originalSize_ = size;
        originalEnd_ = end;
        data = const_cast<std::uint8_t*>(compiled.data());
        size = declaredSize;
        end = declaredSize;
        return true;
    }

    ~GfxMovieStreamOverride()
    {
        if (!dataSlot_) return;
        *dataSlot_ = originalData_;
        *sizeSlot_ = originalSize_;
        *endSlot_ = originalEnd_;
    }

private:
    std::uint8_t** dataSlot_ = nullptr;
    std::uint32_t* sizeSlot_ = nullptr;
    std::uint32_t* endSlot_ = nullptr;
    std::uint8_t* originalData_ = nullptr;
    std::uint32_t originalSize_ = 0;
    std::uint32_t originalEnd_ = 0;
};
