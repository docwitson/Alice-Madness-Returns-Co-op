#include "Common.hpp"
#include "Features.hpp"

safetyhook::InlineHook UpdateViewportRHI;

static void __fastcall UpdateViewportRHI_Hook(int thisp, int, int bDestroyed, int NewSizeX, int NewSizeY, bool bNewIsFullscreen)
{
	if (BorderlessFullscreenEnabled && !bDestroyed)
	{
		int width = 0, height = 0;
		if (SystemHelper::GetTargetSize(width, height))
		{
			NewSizeX = width;
			NewSizeY = height;
		}

		bNewIsFullscreen = false;
	}

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
		ReapplyMemoryPosition();
	}

	UpdateViewportRHI.thiscall<void>(thisp, bDestroyed, NewSizeX, NewSizeY, bNewIsFullscreen);
}

void ApplyResolutionHook()
{
	if (!FontScaling && !FixAspectRatio && !BorderlessFullscreenEnabled) return;

	UpdateViewportRHI = HookHelper::CreateHook((void*)GetAddress(Addr::UpdateViewportRHI), &UpdateViewportRHI_Hook);
}
