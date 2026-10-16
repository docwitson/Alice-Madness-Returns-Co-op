#include "Common.hpp"
#include "Features.hpp"

static safetyhook::MidHook ForceBorderlessMid{};
static safetyhook::MidHook RestoreWindowedMid{};

static void OnForceBorderless(safetyhook::Context& ctx)
{
	MemoryHelper::WriteMemory<DWORD>(ctx.ebp + 0x14, WS_POPUP | WS_SYSMENU, false);
	MemoryHelper::WriteMemory<DWORD>(ctx.ebp + 0x10, 1, false);
}

static void OnRestoreWindowed(safetyhook::Context& ctx)
{
	MemoryHelper::WriteMemory<DWORD>(ctx.ebp + 0x10, 0, false);
}

void ApplyBorderlessFullscreen()
{
	if (!BorderlessFullscreenEnabled) return;

	ForceBorderlessMid = safetyhook::create_mid(GetAddress(Addr::ViewportForceBorderless), OnForceBorderless);
	RestoreWindowedMid = safetyhook::create_mid(GetAddress(Addr::ViewportLayoutDone), OnRestoreWindowed);
}
