#include "Common.hpp"
#include "Features.hpp"

namespace HysteriaLockOnGuard
{
	static bool patchApplied = false;

	void Tick()
	{
		if (patchApplied) return;

		AAlicePlayerController* pc = g_State.AlicePlayerController;
		if (!pc || !pc->Pawn)
			return;

		AAlicePawn* pawn = static_cast<AAlicePawn*>(pc->Pawn);

		if (pawn->LockOnCameraRotSpeed != -1.0f)
			pawn->LockOnCameraRotSpeed = -1.0f;

		UObject* arch = pawn->ObjectArchetype;
		if (arch && static_cast<AAlicePawn*>(arch)->LockOnCameraRotSpeed != -1.0f)
		{
			static_cast<AAlicePawn*>(arch)->LockOnCameraRotSpeed = -1.0f;
			patchApplied = true;
		}
	}
}
