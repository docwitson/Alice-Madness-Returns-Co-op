#include "Common.hpp"
#include "Features.hpp"

static safetyhook::MidHook SetWindowedMid{};

static void OnUseWindowed(safetyhook::Context& ctx)
{
	// Borderless keeps the device windowed at the monitor's resolution
	if (BorderlessFullscreenEnabled)
	{
		MemoryHelper::WriteMemory<int>(GetAddress(Addr::Fullscreen), 0, false);

		int width = 0, height = 0;
		if (SystemHelper::GetTargetSize(width, height))
		{
			MemoryHelper::WriteMemory<int>(GetAddress(Addr::Width), width, false);
			MemoryHelper::WriteMemory<int>(GetAddress(Addr::Height), height, false);
		}

		return;
	}

	MemoryHelper::WriteMemory<int>(GetAddress(Addr::Fullscreen), !UseWindowed, false);
}

void ApplyUseWindowed()
{
	SetWindowedMid = safetyhook::create_mid(GetAddress(Addr::WindowedMode), OnUseWindowed);
}
