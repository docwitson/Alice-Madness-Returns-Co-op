#include "Common.hpp"
#include "Features.hpp"

static safetyhook::MidHook SetWindowedMid{};

static void OnUseWindowed(safetyhook::Context& ctx)
{
	MemoryHelper::WriteMemory<int>(GetAddress(Addr::Fullscreen), !UseWindowed, false);
}

void ApplyUseWindowed()
{
	SetWindowedMid = safetyhook::create_mid(GetAddress(Addr::WindowedMode), OnUseWindowed);
}
