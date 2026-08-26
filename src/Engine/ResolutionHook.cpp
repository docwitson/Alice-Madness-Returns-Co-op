#include "Common.hpp"
#include "Features.hpp"

safetyhook::InlineHook UpdateViewportRHI;

static void __fastcall UpdateViewportRHI_Hook(int thisp, int, int a2, int NewSizeX, int NewSizeY, bool bNewIsFullscreen)
{
	g_State.screenWidth = (float)NewSizeX;
	g_State.screenHeight = (float)NewSizeY;

	// baseHeight for subtitles = 768
	g_State.subtitlesScaleFactor = g_State.screenHeight / 768.0f;

	// Minimum 1.0x (768p and below)
	if (g_State.subtitlesScaleFactor < 1.0f) g_State.subtitlesScaleFactor = 1.0f;
	g_State.subtitlesScaleFactor *= FontScalingFactor;

	g_State.currentAspectRatio = g_State.screenWidth / g_State.screenHeight;

	if (FixAspectRatio)
	{
		g_State.AliceEngine->ConstrainedAspectRatio = g_State.currentAspectRatio;

		// Scale FOV for ultrawide
		g_State.isWideScreen = g_State.currentAspectRatio > ASPECT_RATIO_16_9;

		// Re-apply the menu letterbox geometry for the new resolution
		ReapplyMenuLetterbox();
	}

	UpdateViewportRHI.thiscall<void>(thisp, a2, NewSizeX, NewSizeY, bNewIsFullscreen);
}

void ApplyResolutionHook()
{
	if (!FontScaling && !FixAspectRatio) return;

	UpdateViewportRHI = HookHelper::CreateHook((void*)GetAddress(Addr::UpdateViewportRHI), &UpdateViewportRHI_Hook);
}
