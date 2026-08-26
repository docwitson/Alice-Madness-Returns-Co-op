#include "Common.hpp"
#include "Features.hpp"

static safetyhook::MidHook PhysXSdkDescOverride{};
static safetyhook::MidHook GetSystemVRAM{};

static int g_physXGpuHeapMB = 64;
static int g_physXMeshCacheMB = 8;

static void OnDetectVideoMemory(safetyhook::Context& ctx)
{
    const int m_dwVRAMQuantity = MemoryHelper::ReadMemory<int>(ctx.ebp - 0x7DC);
    const int m_dwDedicatedVRAM = MemoryHelper::ReadMemory<int>(ctx.ebp - 0x7D8);

    const int vramMB = std::max(m_dwVRAMQuantity, m_dwDedicatedVRAM);
    if (vramMB <= 0 || vramMB > 4096)
        return;

    int heap = 1;
    while (heap < vramMB / 16)
    {
        heap <<= 1;
    }
    heap = std::clamp(heap, 64, 256);

    g_physXGpuHeapMB = heap;
    g_physXMeshCacheMB = heap / 8;
}

static void OnPhysXCreateSDK(safetyhook::Context& ctx)
{
    const int gpuHeapOffset = (Addresses::GetBuild() == GameBuild::Current) ? -0x18 : -0x10;
    const int meshCacheOffset = (Addresses::GetBuild() == GameBuild::Current) ? -0x10 : -0x20;

    int* physXGpuHeapSize = reinterpret_cast<int*>(ctx.ebp + gpuHeapOffset);
    int* physXMeshCacheSize = reinterpret_cast<int*>(ctx.ebp + meshCacheOffset);

    *physXGpuHeapSize = g_physXGpuHeapMB;
    *physXMeshCacheSize = g_physXMeshCacheMB;
}

void ApplyAdaptivePhysXMemory()
{
	if (!AdaptivePhysXMemory) return;

	PhysXSdkDescOverride = safetyhook::create_mid(GetAddress(Addr::PhysXCreateSDK), OnPhysXCreateSDK);
	GetSystemVRAM = safetyhook::create_mid(GetAddress(Addr::GetSystemVRAM), OnDetectVideoMemory);
}
