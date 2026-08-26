#pragma once
#define WIN32_LEAN_AND_MEAN
#include <cstdint>
#include <cstring>
#include <Windows.h>

namespace MFortress
{
#pragma pack(push, 1)
    struct PatchEntry
    {
        uint32_t address;
        uint8_t  offset;
        uint8_t  size;
    };
#pragma pack(pop)

    inline bool ApplyPatches(const PatchEntry* patches, size_t count, const uint8_t* bytes)
    {
        constexpr uintptr_t PAGE_MASK = ~uintptr_t(0xFFF);

        uintptr_t lowAddress = ~uintptr_t(0);
        uintptr_t highAddress = 0;

        for (size_t index = 0; index < count; index++)
        {
            uintptr_t entryStart = patches[index].address;
            uintptr_t entryEnd = entryStart + patches[index].size;
            if (entryStart < lowAddress)  lowAddress = entryStart;
            if (entryEnd > highAddress) highAddress = entryEnd;
        }

        uintptr_t regionStart = lowAddress & PAGE_MASK;
        uintptr_t regionEnd = (highAddress + 0xFFF) & PAGE_MASK;
        SIZE_T regionSize = static_cast<SIZE_T>(regionEnd - regionStart);

        DWORD oldProtect;
        if (!VirtualProtect(reinterpret_cast<LPVOID>(regionStart), regionSize, PAGE_EXECUTE_READWRITE, &oldProtect)) 
            return false;

        for (size_t index = 0; index < count; index++)
        {
            std::memcpy(reinterpret_cast<uint8_t*>(patches[index].address), bytes + patches[index].offset, patches[index].size);
        }

        VirtualProtect(reinterpret_cast<LPVOID>(regionStart), regionSize, oldProtect, &oldProtect);
        return true;
    }
}