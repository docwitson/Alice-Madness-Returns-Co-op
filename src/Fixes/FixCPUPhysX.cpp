#include "Common.hpp"
#include "Features.hpp"

safetyhook::InlineHook PhysXLoad;

static safetyhook::MidHook physxCrashFix1{};
static safetyhook::MidHook physxCrashFix2{};

static uintptr_t physXCoreModuleAddress = 0;

static void OnPhysXNvidiaFix1(safetyhook::Context& ctx)
{
	uint32_t ebp = ctx.ebp;
	int ptr = MemoryHelper::ReadMemory<int>(ebp - 0x4);

	if (!MemoryHelper::IsReadable((void*)ptr, 4))
	{
		ctx.eip = physXCoreModuleAddress + 0x1DAB8;
	}
}

static void OnPhysXNvidiaFix2(safetyhook::Context& ctx)
{
	uint32_t ebp = ctx.ebp;
	int ptr = MemoryHelper::ReadMemory<int>(ebp - 0xC);

	if (!MemoryHelper::IsReadable((void*)ptr, 4))
	{
		ctx.eip = physXCoreModuleAddress + 0x12E49A;
	}
}

static void OnPhysXGameFix1(safetyhook::Context& ctx)
{
	int ptr = MemoryHelper::ReadMemory<int>(ctx.ebx);

	if (!MemoryHelper::IsReadable((void*)ptr, 4))
	{
		ctx.eip = physXCoreModuleAddress + 0x1C8EF;
	}
}

static void OnPhysXGameFix2(safetyhook::Context& ctx)
{
	uint32_t ebp = ctx.ebp;
	int ptr = MemoryHelper::ReadMemory<int>(ebp - 0xC);

	if (!MemoryHelper::IsReadable((void*)ptr, 4))
	{
		ctx.eip = physXCoreModuleAddress + 0x127DBA;
	}
}

static int __cdecl PhysXLoad_Hook()
{
	int result = PhysXLoad.ccall<int>();

	HMODULE hModule = GetModuleHandleW(L"PhysXCore.dll");
	if (hModule != NULL)
	{
		physXCoreModuleAddress = reinterpret_cast<uintptr_t>(hModule);
		IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)(physXCoreModuleAddress);
		IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(physXCoreModuleAddress + dos->e_lfanew);
		DWORD timestamp = nt->FileHeader.TimeDateStamp;

		if (timestamp == 0x60ED9FB3) // from NVIDIA drivers
		{
			physxCrashFix1 = safetyhook::create_mid(physXCoreModuleAddress + 0x1D9D8, OnPhysXNvidiaFix1);
			physxCrashFix2 = safetyhook::create_mid(physXCoreModuleAddress + 0x12E368, OnPhysXNvidiaFix2);
		}
		else if (timestamp == 0x4DC9B2B0) // from the game folder
		{
			physxCrashFix1 = safetyhook::create_mid(physXCoreModuleAddress + 0x1C806, OnPhysXGameFix1);
			physxCrashFix2 = safetyhook::create_mid(physXCoreModuleAddress + 0x127C8A, OnPhysXGameFix2);
		}
	}

	return result;
}

void ApplyFixCPUPhysX()
{
	if (!FixCPUPhysX) return;

	PhysXLoad = HookHelper::CreateHook((void*)GetAddress(Addr::PhysXLoad), &PhysXLoad_Hook);
}
