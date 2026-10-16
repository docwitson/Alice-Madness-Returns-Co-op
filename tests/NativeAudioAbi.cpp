#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../include/SDK/SdkHeaders.hpp"
#include <array>
#include <cstdint>
#include <cstring>

// UE3 execIsPlaying writes a DWORD through its result pointer. Reproduce that
// write against the actual SDK type, with adjacent bytes standing in for the
// caller's stack locals. A one-byte bool bitfield corrupts those bytes.
bool TestNativeAudioResultStorage()
{
    static_assert(sizeof(UAudioComponent_execIsPlaying_Params) == 4);
    static_assert(alignof(UAudioComponent_execIsPlaying_Params) == 4);
    static_assert(sizeof(AActor_execCreateAudioComponent_Params) == 36);
    static_assert(offsetof(AActor_execCreateAudioComponent_Params, ReturnValue) == 32);
    static_assert(sizeof(UAudioComponent_execSetFloatParameter_Params) == 12);
    struct GuardedResult
    {
        UAudioComponent_execIsPlaying_Params params{};
        std::array<std::uint8_t, 8> adjacent;
    };
    for (const std::uint32_t nativeResult : {0u, 1u})
    {
        GuardedResult result{};
        result.adjacent.fill(0xa5);
        std::memcpy(static_cast<void*>(&result), &nativeResult, sizeof(nativeResult));
        if (static_cast<bool>(result.params.ReturnValue) != (nativeResult != 0)) return false;
        for (const auto byte : result.adjacent) if (byte != 0xa5) return false;
    }
    return true;
}
