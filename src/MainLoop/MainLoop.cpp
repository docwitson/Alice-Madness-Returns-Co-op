#include "Common.hpp"
#include "Features.hpp"
#include "Coop/CoopClient.hpp"

static safetyhook::InlineHook MainLoop_Hook{};

static void* __fastcall Loop_Hook(int thisp, int)
{
	if (g_State.AlicePlayerController && g_State.AlicePlayerController->PlayerInput)
		g_State.isUsingGamepad = g_State.AlicePlayerController->PlayerInput->bUsingGamepad;

	if (HysteriaLockOnGuardEnabled) HysteriaLockOnGuard::Tick();
	if (FixStuckKeysEnabled) FixStuckKeys::Tick();
	if (FixMissingMusicEnabled) FixMissingMusic::Tick();
	if (FixMapPropertiesEnabled) FixMapProperties::Tick();
	if (FixHatterElevatorEnabled) FixHatterElevator::Tick();
	if (FixFadeToBlackEnabled) FixFadeToBlack::Tick();
	if (CutsceneFPSCapEnabled) CutsceneFPSCap::Tick();
	if (UnlockCompleteEditionDLC) RefreshDLCWeapon();
	InputResponsiveness::Tick();
	if (FixAspectRatio) BlackBarGuard::Tick();
	if (RestoreMenuMouseCursor) MenuMouseCursor::Tick();

	if (AchievementSupport)
	{
		UpdateAchievementProgress();
		BlockCameraInMenu::Tick();
	}

	if (AchievementSupport || AliceCoop::IsEnabled()) AchievementOverlay::Update();

	void* result = MainLoop_Hook.fastcall<void*>(thisp);
	// Apply the kinematic proxy after UE3 has ticked actors, preserving native
	// cloth, slot timing and network transform ownership from protocol40.
	AliceCoop::Tick();
	return result;
}

void ApplyMainLoopHooks()
{
	MainLoop_Hook = HookHelper::CreateHook((void*)GetAddress(Addr::EngineTick), &Loop_Hook);
}
