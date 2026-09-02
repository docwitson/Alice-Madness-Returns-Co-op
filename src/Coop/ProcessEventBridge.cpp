#include "Common.hpp"
#include "Coop/CoopClient.hpp"
#include "Coop/Detail/ProcessEventCallbacks.hpp"
#include "Coop/ProcessEventBridge.hpp"

#include <cmath>

namespace AliceCoop::ProcessEventBridge
{
	namespace
	{
		UFunction* g_tickAlicePawn = nullptr;
		UFunction* g_interpStarted = nullptr;
		UFunction* g_aliceHudPostRender = nullptr;
		UFunction* g_viewportPostRender = nullptr;

		UFunction* g_meleeAttack = nullptr;
		UFunction* g_weaponAttack = nullptr;
		UFunction* g_switchWeaponGroup = nullptr;
		UFunction* g_rightClickAttack = nullptr;
		UFunction* g_startFire = nullptr;
		UFunction* g_stopWeaponFire = nullptr;
		UFunction* g_dodge = nullptr;
		UFunction* g_changeShrinkingMode = nullptr;
		UFunction* g_switchVorpal = nullptr;
		UFunction* g_switchHobby = nullptr;
		UFunction* g_switchEyeStaff = nullptr;
		UFunction* g_projectileInit = nullptr;
		UFunction* g_projectilePostBeginPlay = nullptr;
		UFunction* g_rangeProjectileFire = nullptr;
		UFunction* g_clockBombSetupStart = nullptr;
		UFunction* g_clockBombDetonate = nullptr;
		UFunction* g_clockBombDestroyed = nullptr;
		UFunction* g_pcSetupClockBomb = nullptr;
		UFunction* g_pcDetonateClockBomb = nullptr;
		UFunction* g_coopRestartAlice = nullptr;
		UFunction* g_coopShowDeathConfirm = nullptr;
		UFunction* g_coopContinueToGame = nullptr;
		UFunction* g_coopLoadCheckpoint = nullptr;
		UFunction* g_coopRespawnAlice = nullptr;
		UFunction* g_coopClientRestart = nullptr;
		UFunction* g_coopDeathRestartGame = nullptr;
		UFunction* g_coopMovieRestart = nullptr;
		UFunction* g_coopInGameMenuQuit = nullptr;
		UFunction* g_coopSetInMainMenu = nullptr;
		UFunction* g_coopOnSetVentState = nullptr;
		UFunction* g_coopSetCinematicMode = nullptr;
		UFunction* g_coopSequenceEventCheckActivate = nullptr;
		UFunction* g_coopSequenceOpActivated = nullptr;
		UFunction* g_coopSequenceOpDeactivated = nullptr;
		UFunction* g_coopPlayerUse = nullptr;
		UFunction* g_coopActorUsedBy = nullptr;
		UFunction* g_coopOnInteractInLondon = nullptr;
		UFunction* g_coopInteractInLondonX = nullptr;
		UFunction* g_coopExitInteractState = nullptr;
		UFunction* g_coopTriggerInteracted = nullptr;
		UFunction* g_coopNotifyInputKey = nullptr;
		UFunction* g_coopClientTravel = nullptr;
		UFunction* g_coopBackToTitle = nullptr;
		UFunction* g_coopLoadChapter = nullptr;
		UFunction* g_coopTitleMenuLoadCheckpoint = nullptr;
		UFunction* g_coopInGameMenuLoadCheckpoint = nullptr;
		UFunction* g_coopInGameMenuOpen = nullptr;
		UFunction* g_coopInGameMenuContinue = nullptr;
		UFunction* g_coopInGameMenuOnClose = nullptr;
		UFunction* g_coopTryUnlockAbilityTrophy = nullptr;

		bool IsLifecycleFunction(UFunction* function)
		{
			return function == g_coopRestartAlice
				|| function == g_coopShowDeathConfirm
				|| function == g_coopContinueToGame
				|| function == g_coopLoadCheckpoint
				|| function == g_coopRespawnAlice
				|| function == g_coopClientRestart
				|| function == g_coopDeathRestartGame
				|| function == g_coopMovieRestart
				|| function == g_coopInGameMenuQuit
				|| function == g_coopSetInMainMenu
				|| function == g_coopOnSetVentState
				|| function == g_coopSetCinematicMode
				|| function == g_coopPlayerUse
				|| function == g_coopActorUsedBy
				|| function == g_coopOnInteractInLondon
				|| function == g_coopInteractInLondonX
				|| function == g_coopExitInteractState
				|| function == g_coopTriggerInteracted
				|| function == g_coopNotifyInputKey
				|| function == g_coopClientTravel
				|| function == g_coopBackToTitle;
		}
	}

	Invocation::Invocation(int object, UFunction* function, int params, int result)
		: object_(object), function_(function), params_(params), result_(result)
	{
	}

	Invocation::~Invocation() = default;

	void Invocation::SetDecision(Decision decision)
	{
		decision_ = decision;
		decisionSet_ = decision.disposition != Disposition::Continue;
	}

	int Invocation::RawObject() const
	{
		return object_;
	}

	UFunction* Invocation::Function() const
	{
		return function_;
	}

	int Invocation::RawParams() const
	{
		return params_;
	}

	int Invocation::RawResult() const
	{
		return result_;
	}

	UObject* Invocation::Object() const
	{
		return reinterpret_cast<UObject*>(object_);
	}

	const void* Invocation::Params() const
	{
		return reinterpret_cast<const void*>(params_);
	}

	void Initialize()
	{
		g_tickAlicePawn = UFunction::FindFunction(
			"Function AliceGame.AlicePawn.Tick");
		g_interpStarted = UFunction::FindFunction(
			"Function Engine.Actor.InterpolationStarted");
		g_aliceHudPostRender = UFunction::FindFunction(
			"Function AliceGame.AliceHud.PostRender");
		g_viewportPostRender = UFunction::FindFunction(
			"Function Engine.GameViewportClient.PostRender");

		g_meleeAttack = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.MeleeAttack");
		g_weaponAttack = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.WeaponAttack");
		g_switchWeaponGroup = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.SwitchWeaponGroup");
		g_rightClickAttack = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.RightClickAttack");
		g_startFire = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.StartFire");
		g_stopWeaponFire = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.StopWeaponFire");
		g_dodge = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.Dodge");
		g_changeShrinkingMode = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.ChangeShrinkingMode");
		g_switchVorpal = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.SwitchToVorpalBlade");
		g_switchHobby = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.SwitchToHobbyHorse");
		g_switchEyeStaff = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.SwitchToEyeStaff");
		g_projectileInit = UFunction::FindFunction(
			"Function AliceGame.AliceGameProjectile.Init");
		g_projectilePostBeginPlay = UFunction::FindFunction(
			"Function AliceGame.AliceGameProjectile.PostBeginPlay");
		g_rangeProjectileFire = UFunction::FindFunction(
			"Function AliceGame.WeaponForAliceRange.ProjectileFire");
		g_clockBombSetupStart = UFunction::FindFunction(
			"Function AliceGame.AliceClonePawn.SetupStart");
		g_clockBombDetonate = UFunction::FindFunction(
			"Function AliceGame.AliceClonePawn.Detonate");
		g_clockBombDestroyed = UFunction::FindFunction(
			"Function AliceGame.AliceClonePawn.Destroyed");
		g_pcSetupClockBomb = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.SetupClockBomb");
		g_pcDetonateClockBomb = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.DetonateClockBomb");
		g_coopRestartAlice = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.RestartAlice");
		g_coopShowDeathConfirm = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.ShowDeathConfirmDialog");
		g_coopContinueToGame = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.ContinueToGame");
		g_coopLoadCheckpoint = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.LoadCheckpoint");
		g_coopRespawnAlice = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.RespawnAlice");
		g_coopClientRestart = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.ClientRestart");
		g_coopDeathRestartGame = UFunction::FindFunction(
			"Function AliceGame.AliceGFxMovie_HUD.deathRestartGame");
		g_coopMovieRestart = UFunction::FindFunction(
			"Function AliceGame.AliceGFXMovie.Restart");
		g_coopInGameMenuQuit = UFunction::FindFunction(
			"Function AliceGame.AliceGfxMovie_inGameMenu.QuitGame");
		g_coopSetInMainMenu = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.SetInMainMenu");
		g_coopOnSetVentState = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.OnSetVentState");
		g_coopSetCinematicMode = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.SetCinematicMode");
		g_coopSequenceEventCheckActivate = UFunction::FindFunction(
			"Function Engine.SequenceEvent.CheckActivate");
		g_coopSequenceOpActivated = UFunction::FindFunction(
			"Function Engine.SequenceOp.Activated");
		g_coopSequenceOpDeactivated = UFunction::FindFunction(
			"Function Engine.SequenceOp.Deactivated");
		g_coopPlayerUse = UFunction::FindFunction(
			"Function Engine.PlayerController.Use");
		g_coopActorUsedBy = UFunction::FindFunction(
			"Function Engine.Actor.UsedBy");
		g_coopOnInteractInLondon = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.OnInteractInLondon");
		g_coopInteractInLondonX = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.interactInLondonX");
		g_coopExitInteractState = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.exitInteractState");
		g_coopTriggerInteracted = UFunction::FindFunction(
			"Function Engine.PlayerController.TriggerInteracted");
		g_coopNotifyInputKey = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.notifyInputKey");
		g_coopClientTravel = UFunction::FindFunction(
			"Function Engine.PlayerController.ClientTravel");
		g_coopBackToTitle = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.backtoTitle");
		g_coopLoadChapter = UFunction::FindFunction(
			"Function Engine.CheckPointManager.LoadChapter");
		g_coopTitleMenuLoadCheckpoint = UFunction::FindFunction(
			"Function AliceGame.AliceGfxMovie_titlePlayerMenu.LoadCheckpoint");
		g_coopInGameMenuLoadCheckpoint = UFunction::FindFunction(
			"Function AliceGame.AliceGfxMovie_inGameMenu.LoadCheckpoint");
		g_coopInGameMenuOpen = UFunction::FindFunction(
			"Function AliceGame.AliceGfxMovie_inGameMenu.OpenMenu");
		g_coopInGameMenuContinue = UFunction::FindFunction(
			"Function AliceGame.AliceGfxMovie_inGameMenu.continueGame");
		g_coopInGameMenuOnClose = UFunction::FindFunction(
			"Function AliceGame.AliceGfxMovie_inGameMenu.OnClose");
		g_coopTryUnlockAbilityTrophy = UFunction::FindFunction(
			"Function AliceGame.AlicePlayerController.tryUnlockAbilityTrophy");
	}

	Decision EarlyBefore(Invocation& invocation)
	{
		const int This = invocation.RawObject();
		UFunction* Function = invocation.Function();
		const int uParams = invocation.RawParams();
		Decision pendingDecision{};

		// Session-only host progression must not award story unlock trophies on a
		// clean client profile. SetAbility calls this nested function normally; the
		// ability itself is still applied, only its unrelated achievement side
		// effect is suppressed during the short co-op synchronization scope.
		if (Function == g_coopTryUnlockAbilityTrophy
			&& AliceCoop::IsApplyingHostProgression())
		{
			return { Disposition::Suppress,
				DecisionReason::HostProgressionTrophy };
		}

		if (AliceCoop::IsEnabled()
			&& g_State.AlicePlayerController
			&& This == reinterpret_cast<int>(g_State.AlicePlayerController))
		{
			using AliceCoopProtocol::PlayerAction;
			if (Function == g_meleeAttack)
				AliceCoop::RecordLocalAction(PlayerAction::MeleeAttack);
			else if (Function == g_weaponAttack)
				AliceCoop::RecordLocalAction(PlayerAction::WeaponAttack);
			else if (Function == g_rightClickAttack)
				AliceCoop::RecordLocalAction(PlayerAction::RightClickAttack);
			else if (Function == g_startFire)
				AliceCoop::RecordLocalAction(PlayerAction::StartFire);
			else if (Function == g_stopWeaponFire)
				AliceCoop::RecordLocalAction(PlayerAction::StopFire);
			else if (Function == g_dodge)
				AliceCoop::RecordLocalAction(PlayerAction::Dodge);
			else if (Function == g_changeShrinkingMode)
			{
				const AAlicePawn* pawn =
					g_State.AlicePlayerController->MyAlicePawn;
				AliceCoop::RecordLocalAction(
					pawn && pawn->bShrinkingModeActive
					? PlayerAction::ShrinkLeave
					: PlayerAction::ShrinkEnter);
			}
			else if (Function == g_switchWeaponGroup
				|| Function == g_switchVorpal
				|| Function == g_switchHobby
				|| Function == g_switchEyeStaff)
			{
				AliceCoop::RecordLocalAction(PlayerAction::WeaponSwitch);
			}
		}

		if (AliceCoop::IsEnabled())
		{
			if (Function == g_coopInGameMenuOpen)
				AliceCoop::ObservePauseMenuState(true);
			else if (Function == g_coopInGameMenuContinue
				|| Function == g_coopInGameMenuOnClose
				|| Function == g_coopInGameMenuQuit)
			{
				AliceCoop::ObservePauseMenuState(false);
			}
			if (Function == g_coopLoadChapter && uParams)
			{
				const auto* parameters = reinterpret_cast<const
					ACheckPointManager_execLoadChapter_Params*>(uParams);
				AliceCoop::RecordHostLoadChapter(
					parameters->beLoadedCharpter);
			}
			else if (Function == g_coopTitleMenuLoadCheckpoint && uParams)
			{
				const auto* parameters = reinterpret_cast<const
					UAliceGfxMovie_titlePlayerMenu_execLoadCheckpoint_Params*>(
						uParams);
				if (parameters->cid >= 0 && parameters->cid < 70)
				{
					AliceCoop::RecordHostLoadChapter(
						static_cast<std::uint8_t>(parameters->cid));
				}
			}
			else if (Function == g_coopInGameMenuLoadCheckpoint && uParams)
			{
				const auto* parameters = reinterpret_cast<const
					UAliceGfxMovie_inGameMenu_execLoadCheckpoint_Params*>(
						uParams);
				if (parameters->cid >= 0 && parameters->cid < 70)
				{
					AliceCoop::RecordHostLoadChapter(
						static_cast<std::uint8_t>(parameters->cid));
				}
			}
			AliceCoop::HandleSharedCombatProcessEvent(
				reinterpret_cast<UObject*>(This), Function,
				reinterpret_cast<const void*>(uParams), false);
			if (AliceCoop::ShouldSuppressSharedPlayerDamage(
					reinterpret_cast<UObject*>(This), Function,
					reinterpret_cast<const void*>(uParams)))
			{
				return { Disposition::Suppress,
					DecisionReason::SharedPlayerDamage };
			}
			if (Function == g_coopSequenceOpActivated
				|| Function == g_coopSequenceOpDeactivated)
			{
				AliceCoop::TraceSequenceOpProcessEvent(
					reinterpret_cast<UObject*>(This),
					Function == g_coopSequenceOpActivated, false);
				if (Function == g_coopSequenceOpActivated
					&& AliceCoop::ShouldDeferSequenceOpActivation(
						reinterpret_cast<UObject*>(This)))
				{
					pendingDecision = { Disposition::Defer,
						DecisionReason::SequenceActivationBarrier };
				}
			}
			if (IsLifecycleFunction(Function))
			{
				AliceCoop::TraceLifecycleProcessEvent(
					reinterpret_cast<UObject*>(This), Function,
					reinterpret_cast<const void*>(uParams), false);
			}
		}
		if (AliceCoop::IsActionTraceEnabled())
		{
			AliceCoop::TraceProcessEvent(
				reinterpret_cast<UObject*>(This), Function,
				reinterpret_cast<const void*>(uParams));
		}
		if (AliceCoop::IsWorldTraceEnabled())
		{
			AliceCoop::TraceWorldProcessEvent(
				reinterpret_cast<UObject*>(This), Function,
				reinterpret_cast<const void*>(uParams), false);
		}

		return pendingDecision;
	}

	void BeforeOriginal(Invocation& invocation)
	{
		const int This = invocation.RawObject();
		UFunction* Function = invocation.Function();
		const int uParams = invocation.RawParams();

		if (AliceCoop::IsEnabled() && uParams
			&& Function == g_interpStarted)
		{
			AliceCoop::TraceInterpolationStarted(
				reinterpret_cast<AActor*>(This),
				*reinterpret_cast<USeqAct_Interp**>(uParams));
		}

		const bool isLocalClockBombDetonate =
			AliceCoop::IsEnabled() && This
			&& Function == g_clockBombDetonate;
		const bool isLocalClockBombDestroyed =
			AliceCoop::IsEnabled() && This
			&& Function == g_clockBombDestroyed;
		const bool isPlayerClockBombDetonate =
			AliceCoop::IsEnabled() && This
			&& Function == g_pcDetonateClockBomb;
		if (isLocalClockBombDetonate)
		{
			AliceCoop::OnLocalClockBombDetonate(
				reinterpret_cast<AAliceClonePawn*>(This));
		}
		if (isLocalClockBombDestroyed)
		{
			AliceCoop::OnLocalClockBombDestroyed(
				reinterpret_cast<AAliceClonePawn*>(This));
		}
		if (isPlayerClockBombDetonate
			&& g_State.AlicePlayerController
			&& This == reinterpret_cast<int>(
				g_State.AlicePlayerController))
		{
			AAlicePawn* pawn =
				g_State.AlicePlayerController->MyAlicePawn;
			if (pawn && pawn->MyClonePawn
				&& pawn->MyClonePawn->IsA(
					AAliceClonePawn::StaticClass()))
			{
				AliceCoop::OnLocalClockBombDetonate(
					reinterpret_cast<AAliceClonePawn*>(
						pawn->MyClonePawn));
			}
		}
	}

	void AfterOriginal(Invocation& invocation)
	{
		const int This = invocation.RawObject();
		UFunction* Function = invocation.Function();
		const int uParams = invocation.RawParams();

		if (AliceCoop::IsEnabled())
		{
			if (Function == g_coopSequenceEventCheckActivate)
			{
				AliceCoop::HandleSharedInteractionProcessEvent(
					reinterpret_cast<UObject*>(This),
					reinterpret_cast<const void*>(uParams), true);
			}
			AliceCoop::HandleSharedCombatProcessEvent(
				reinterpret_cast<UObject*>(This), Function,
				reinterpret_cast<const void*>(uParams), true);
			if (Function == g_coopSequenceOpActivated
				|| Function == g_coopSequenceOpDeactivated)
			{
				AliceCoop::TraceSequenceOpProcessEvent(
					reinterpret_cast<UObject*>(This),
					Function == g_coopSequenceOpActivated, true);
			}
			if (IsLifecycleFunction(Function))
			{
				AliceCoop::TraceLifecycleProcessEvent(
					reinterpret_cast<UObject*>(This), Function,
					reinterpret_cast<const void*>(uParams), true);
			}
		}
		if (AliceCoop::IsWorldTraceEnabled())
		{
			AliceCoop::TraceWorldProcessEvent(
				reinterpret_cast<UObject*>(This), Function,
				reinterpret_cast<const void*>(uParams), true);
		}

		if (AliceCoop::IsEnabled() && This
			&& Function == g_projectilePostBeginPlay)
		{
			auto* object = reinterpret_cast<UObject*>(This);
			if (object->IsA(
				APepperGrinderPrimaryProjectile::StaticClass()))
			{
				auto* projectile = reinterpret_cast<
					APepperGrinderPrimaryProjectile*>(This);
				AliceCoop::OnLocalPepperProjectileSpawn(
					projectile, projectile->FlightFront);
			}
		}

		if (AliceCoop::IsEnabled() && This
			&& Function == g_rangeProjectileFire && uParams)
		{
			const auto* parameters = reinterpret_cast<
				const AWeaponForAliceRange_execProjectileFire_Params*>(
					uParams);
			AProjectile* result = parameters->ReturnValue;
			if (result && result->IsA(
				APepperGrinderPrimaryProjectile::StaticClass()))
			{
				auto* projectile = reinterpret_cast<
					APepperGrinderPrimaryProjectile*>(result);
				FVector direction = projectile->FlightFront;
				const float flightFrontSquared =
					direction.X * direction.X
					+ direction.Y * direction.Y
					+ direction.Z * direction.Z;
				if (flightFrontSquared < 0.001f)
				{
					direction = projectile->Velocity;
					const float velocitySquared =
						direction.X * direction.X
						+ direction.Y * direction.Y
						+ direction.Z * direction.Z;
					if (velocitySquared > 0.001f)
					{
						const float inverseLength =
							1.0f / std::sqrt(velocitySquared);
						direction.X *= inverseLength;
						direction.Y *= inverseLength;
						direction.Z *= inverseLength;
					}
				}
				AliceCoop::OnLocalPepperProjectileSpawn(
					projectile, direction);
			}
		}

		if (AliceCoop::IsEnabled() && This
			&& Function == g_projectileInit
			&& !g_rangeProjectileFire)
		{
			auto* object = reinterpret_cast<UObject*>(This);
			if (object->IsA(
				APepperGrinderPrimaryProjectile::StaticClass()))
			{
				const auto* parameters = reinterpret_cast<
					const AAliceGameProjectile_execInit_Params*>(
						uParams);
				const FVector direction = parameters
					? parameters->Direction
					: FVector(0.0f, 0.0f, 0.0f);
				AliceCoop::OnLocalPepperProjectileSpawn(
					reinterpret_cast<
						APepperGrinderPrimaryProjectile*>(This),
					direction);
			}
		}
		if (AliceCoop::IsEnabled() && This
			&& Function == g_clockBombSetupStart)
		{
			AliceCoop::OnLocalClockBombSpawn(
				reinterpret_cast<AAliceClonePawn*>(This));
		}
		if (AliceCoop::IsEnabled() && This
			&& Function == g_pcSetupClockBomb
			&& g_State.AlicePlayerController
			&& This == reinterpret_cast<int>(
				g_State.AlicePlayerController))
		{
			AAlicePawn* pawn =
				g_State.AlicePlayerController->MyAlicePawn;
			if (pawn && pawn->MyClonePawn
				&& pawn->MyClonePawn->IsA(
					AAliceClonePawn::StaticClass()))
			{
				AliceCoop::OnLocalClockBombSpawn(
					reinterpret_cast<AAliceClonePawn*>(
						pawn->MyClonePawn));
			}
		}

		if (AliceCoop::IsEnabled() && Function == g_dodge)
			AliceCoop::RepairRemoteVisibilityAfterLocalDodge();

		if (AliceCoop::IsEnabled()
			&& Function == g_aliceHudPostRender && This)
		{
			auto* hud = reinterpret_cast<AAliceHud*>(This);
			AliceCoop::DrawTuningOverlay(hud->Canvas);
			AliceCoop::PumpLifecycleCommands();
		}
		else if (AliceCoop::IsEnabled()
			&& Function == g_viewportPostRender && uParams)
		{
			auto* parameters = reinterpret_cast<
				UGameViewportClient_eventPostRender_Params*>(uParams);
			AliceCoop::DrawTuningOverlay(parameters->Canvas);
			AliceCoop::PumpLifecycleCommands();
		}

		if (AliceCoop::IsEnabled() && Function == g_tickAlicePawn)
			AliceCoop::OnAlicePawnTicked(
				reinterpret_cast<AAlicePawn*>(This));

		invocation.afterOriginalCompleted_ = true;
	}
}
