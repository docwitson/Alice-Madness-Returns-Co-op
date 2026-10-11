#pragma once

#include "Coop/Protocol.hpp"

#include <string>

class AAliceClonePawn;
class AAlicePawn;
class AActor;
class AEmitter;
struct ADecalManager_execSpawnDecal_Params;
class AAliceGameProjectile;
class UCanvas;
class UParticleSystem;
class UParticleSystemComponent;
struct FRotator;
class UFunction;
class UObject;
class USeqAct_Interp;
struct FVector;

namespace AliceCoop
{
	void PumpLifecycleCommands();
	void OnAlicePawnTicked(AAlicePawn* pawn);
	void DrawTuningOverlay(UCanvas* canvas);
	void RecordLocalAction(AliceCoopProtocol::PlayerAction action);
	void OnLocalNativeEmitterSpawn(UObject* source, UParticleSystemComponent* component,
		UParticleSystem* particle, const FVector& location, const FRotator& rotation);
	void OnLocalNativeActorEmitterSpawn(UObject* source, AEmitter* emitter);
	void OnLocalNativeDecalSpawn(UObject* source, const ADecalManager_execSpawnDecal_Params& params);
	void OnLocalWeaponProjectileSpawn(AAliceGameProjectile* projectile,
		const FVector& direction);
	void OnLocalWeaponProjectilePending(AAliceGameProjectile* projectile);
	void OnLocalWeaponProjectileEnd(AAliceGameProjectile* projectile,
		const FVector* hitLocation, const FVector* hitNormal);
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
	std::string GetOverlayStatusLine();
	std::string GetOverlayDebugDetails();
	void ExecuteDevCommand(int command);
	bool IsActionTraceEnabled();
	bool IsWorldTraceEnabled();
	bool IsProcessEventBridgeTraceEnabled();
	void LogProcessEventBridgeDiagnostic(const std::string& message);
}
