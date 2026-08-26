#include "Common.hpp"
#include "Features.hpp"

namespace InputResponsiveness
{
	void Tick()
	{
		AAlicePlayerController* pc = g_State.AlicePlayerController;
		if (!pc) return;

		bool disable = g_State.isUsingGamepad ? DisableControllerAcceleration : DisableMouseAcceleration;
		if (disable)
		{
			pc->aTurnElapsedTime = 10.0f;
			pc->aLookUpElapsedTime = 10.0f;
		}

		if (pc->PlayerInput)
		{
			pc->PlayerInput->bEnableMouseSmoothing = !DisableMouseSmoothing;
		}
	}
}
