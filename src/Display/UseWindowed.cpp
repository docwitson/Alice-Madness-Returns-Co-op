#include "Common.hpp"
#include "Features.hpp"
#include "StartupDisplayMode.hpp"

static AliceCoopDisplay::StartupMode startupMode;

static safetyhook::MidHook SetWindowedMid{};

static void OnUseWindowed(safetyhook::Context& ctx)
{
	// All launch preferences, including the initial borderless resolution, apply
	// only at startup. Later resolution changes must reach every viewport layer.
	if (!startupMode.Consume()) return;

	// Start borderless at the monitor's resolution.
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

	// Apply the requested launch mode once. Later calls belong to the game
	// settings / Alt+Enter and must retain the player's choice.
	MemoryHelper::WriteMemory<int>(GetAddress(Addr::Fullscreen), !UseWindowed, false);
}

void ApplyUseWindowed()
{
	SetWindowedMid = safetyhook::create_mid(GetAddress(Addr::WindowedMode), OnUseWindowed);
}
