#include "Common.hpp"
#include "Features.hpp"
#include "Coop/CoopClient.hpp"

static safetyhook::InlineHook MainLoop_Hook{};
static safetyhook::MidHook PresentWindowed{};
static safetyhook::MidHook PresentFullscreen{};
static safetyhook::MidHook ResetSitePre{};
static safetyhook::MidHook ResetSitePost{};

static bool NeedsD3DOverlay()
{
	return AchievementSupport || AliceCoop::IsEnabled();
}

static IDirect3DDevice9* GetD3D9Device()
{
	uintptr_t addr = GetAddress(Addr::D3D9DevicePtr);
	if (!addr) return nullptr;

	IDirect3DDevice9* dev = nullptr;
	__try
	{
		dev = *reinterpret_cast<IDirect3DDevice9**>(addr);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nullptr;
	}
	return dev;
}

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

	if (NeedsD3DOverlay())
	{
		// Keep diagnostics in AliceCoop logs. The in-game overlay is reserved
		// for player-facing co-op notifications.
		AchievementOverlay::SetCoopDevMode(false);
		if (AchievementSupport)
		{
			UpdateAchievementProgress();
			BlockCameraInMenu::Tick();
		}
		AchievementOverlay::Update(GetD3D9Device());
	}

	void* result = MainLoop_Hook.fastcall<void*>(thisp);

	// The remote avatar is kinematic. Apply its network transform, visibility,
	// and scale after UE3 has ticked all actors; otherwise the proxy's own
	// single-player pawn tick overwrites those values before the frame renders.
	AliceCoop::Tick();

	return result;
}

static void OnPresentWindowed(safetyhook::Context&)
{
	if (NeedsD3DOverlay())
		AchievementOverlay::OnPresent();
}

static void OnPresentFullscreen(safetyhook::Context&)
{
	if (NeedsD3DOverlay())
		AchievementOverlay::OnPresent();
}

static void OnResetSitePre(safetyhook::Context&)
{
	if (NeedsD3DOverlay())
		AchievementOverlay::OnDeviceLost();
}

static void OnResetSitePost(safetyhook::Context&)
{
	if (NeedsD3DOverlay())
		AchievementOverlay::OnDeviceReset();
}

void ApplyMainLoopHooks()
{
	MainLoop_Hook = HookHelper::CreateHook((void*)GetAddress(Addr::EngineTick), &Loop_Hook);

	if (NeedsD3DOverlay())
	{
		AchievementOverlay::Init(GetAddress(Addr::D3D9DevicePtr));
		PresentWindowed = safetyhook::create_mid(GetAddress(Addr::D3D9PresentWindowed), OnPresentWindowed);
		PresentFullscreen = safetyhook::create_mid(GetAddress(Addr::D3D9PresentFullscreen), OnPresentFullscreen);
		ResetSitePre = safetyhook::create_mid(GetAddress(Addr::D3D9ResetPre), OnResetSitePre);
		ResetSitePost = safetyhook::create_mid(GetAddress(Addr::D3D9ResetPost), OnResetSitePost);
	}
}
