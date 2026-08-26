#include "Common.hpp"
#include "Features.hpp"

namespace BlockCameraInMenu
{
	static bool s_applied = false;
	static void* s_pc = nullptr;

	void Tick()
	{
		AAlicePlayerController* pc = g_State.AlicePlayerController;
		if (pc)
		{
			pc->bIgnoreLookInput = AchievementOverlay::IsVisible();
		}
	}
}
