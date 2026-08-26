#pragma once

#include "Coop/Protocol.hpp"

#include <string>

namespace AliceCoop
{
	void Initialize();
	void Tick();
	void PumpLifecycleCommands();
	void OnAlicePawnTicked(AAlicePawn* pawn);
	void DrawTuningOverlay(UCanvas* canvas);
	void RecordLocalAction(AliceCoopProtocol::PlayerAction action);
	void OnLocalPepperProjectileSpawn(
		APepperGrinderPrimaryProjectile* projectile,
		const FVector& direction);
	void OnLocalClockBombSpawn(AAliceClonePawn* bomb);
	void OnLocalClockBombDetonate(AAliceClonePawn* bomb);
	void OnLocalClockBombDestroyed(AAliceClonePawn* bomb);
	void TraceProcessEvent(UObject* object, UFunction* function,
		const void* parameters);
	void TraceWorldProcessEvent(UObject* object, UFunction* function,
		const void* parameters, bool after);
	void HandleSharedCombatProcessEvent(UObject* object,
		UFunction* function, const void* parameters, bool after);
	bool ShouldSuppressSharedPlayerDamage(UObject* object,
		UFunction* function, const void* parameters);
	bool IsApplyingHostProgression();
	void TraceLifecycleProcessEvent(UObject* object,
		UFunction* function, const void* parameters, bool after);
	void RecordHostLoadChapter(std::uint8_t chapter);
	void ObservePauseMenuState(bool open);
	void HandleSharedInteractionProcessEvent(UObject* object,
		const void* parameters, bool after);
	void TraceSequenceOpProcessEvent(UObject* object,
		bool activated, bool after);
	bool ShouldDeferSequenceOpActivation(UObject* object);
	void TraceInterpolationStarted(AActor* actor,
		USeqAct_Interp* action);
	void RepairRemoteVisibilityAfterLocalDodge();
	bool IsEnabled();
	std::string GetOverlayStatusLine();
	std::string GetOverlayDebugDetails();
	void ExecuteDevCommand(int command);
	bool IsActionTraceEnabled();
	bool IsWorldTraceEnabled();
	bool ShouldUseUniqueMutex();
	void ObserveSavePath(const wchar_t* effectivePath);
	bool TryRedirectClientSave(const wchar_t* originalPath, std::wstring& redirectedPath);
}
