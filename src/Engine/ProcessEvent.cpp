#include "Common.hpp"
#include "Coop/CoopClient.hpp"
#include "Features.hpp"

#include <cmath>

safetyhook::InlineHook ProcessEvent;

static UFunction* g_tickAlice = nullptr;
static UFunction* g_tickAlicePawn = nullptr;
static UFunction* g_dlcStatusMovie = nullptr;
static UFunction* g_dlcStatusPC = nullptr;
static UFunction* g_calloutMovie = nullptr;
static UFunction* g_calloutPC = nullptr;
static UFunction* g_updateCam = nullptr;
static UFunction* g_getPlatform = nullptr;
static UFunction* g_finishFirstUpgrade = nullptr;
static UFunction* g_interpStarted = nullptr;
static UFunction* g_interpFinished = nullptr;
static UFunction* g_aliceHudPostRender = nullptr;
static UFunction* g_viewportPostRender = nullptr;

static UFunction* g_meleeAttack = nullptr;
static UFunction* g_weaponAttack = nullptr;
static UFunction* g_switchWeaponGroup = nullptr;
static UFunction* g_rightClickAttack = nullptr;
static UFunction* g_startFire = nullptr;
static UFunction* g_stopWeaponFire = nullptr;
static UFunction* g_dodge = nullptr;
static UFunction* g_changeShrinkingMode = nullptr;
static UFunction* g_switchVorpal = nullptr;
static UFunction* g_switchHobby = nullptr;
static UFunction* g_switchEyeStaff = nullptr;
static UFunction* g_pcCanFire = nullptr;
static UFunction* g_pawnFadeOutWeapon = nullptr;
static UFunction* g_pawnClearDelayAttach = nullptr;
static UFunction* g_projectileInit = nullptr;
static UFunction* g_projectilePostBeginPlay = nullptr;
static UFunction* g_rangeProjectileFire = nullptr;
static UFunction* g_clockBombSetupStart = nullptr;
static UFunction* g_clockBombDetonate = nullptr;
static UFunction* g_clockBombDestroyed = nullptr;
static UFunction* g_pcSetupClockBomb = nullptr;
static UFunction* g_pcDetonateClockBomb = nullptr;
static UFunction* g_coopRestartAlice = nullptr;
static UFunction* g_coopShowDeathConfirm = nullptr;
static UFunction* g_coopContinueToGame = nullptr;
static UFunction* g_coopLoadCheckpoint = nullptr;
static UFunction* g_coopRespawnAlice = nullptr;
static UFunction* g_coopClientRestart = nullptr;
static UFunction* g_coopDeathRestartGame = nullptr;
static UFunction* g_coopMovieRestart = nullptr;
static UFunction* g_coopInGameMenuQuit = nullptr;
static UFunction* g_coopSetInMainMenu = nullptr;
static UFunction* g_coopOnSetVentState = nullptr;
static UFunction* g_coopSetCinematicMode = nullptr;
static UFunction* g_coopSequenceEventCheckActivate = nullptr;
static UFunction* g_coopSequenceOpActivated = nullptr;
static UFunction* g_coopSequenceOpDeactivated = nullptr;
static UFunction* g_coopPlayerUse = nullptr;
static UFunction* g_coopActorUsedBy = nullptr;
static UFunction* g_coopOnInteractInLondon = nullptr;
static UFunction* g_coopInteractInLondonX = nullptr;
static UFunction* g_coopExitInteractState = nullptr;
static UFunction* g_coopTriggerInteracted = nullptr;
static UFunction* g_coopNotifyInputKey = nullptr;
static UFunction* g_coopClientTravel = nullptr;
static UFunction* g_coopBackToTitle = nullptr;
static UFunction* g_coopLoadChapter = nullptr;
static UFunction* g_coopTitleMenuLoadCheckpoint = nullptr;
static UFunction* g_coopInGameMenuLoadCheckpoint = nullptr;
static UFunction* g_coopInGameMenuOpen = nullptr;
static UFunction* g_coopInGameMenuContinue = nullptr;
static UFunction* g_coopInGameMenuOnClose = nullptr;
static UFunction* g_coopTryUnlockAbilityTrophy = nullptr;

static AWeapon* volatile g_weaponToRefresh = nullptr;
static AWeapon* s_lastSeenWeapon = nullptr;

static inline int DesiredControllerIconSet()
{
	if (UsePS3ControllerIcons == 1) return 2; // Xbox 360 icons
	if (UsePS3ControllerIcons == 2) return 3; // PS3 icons
	return (ControllerHelper::GetGamepadStyle() == ControllerHelper::GamepadStyle::PlayStation) ? 3 : 2;
}

static void __fastcall ProcessEvent_Hook(int This, void*, UFunction* Function, int uParams, int uResult)
{
	// Session-only host progression must not award story unlock trophies on a
	// clean client profile. SetAbility calls this nested function normally; the
	// ability itself is still applied, only its unrelated achievement side
	// effect is suppressed during the short co-op synchronization scope.
	if (Function == g_coopTryUnlockAbilityTrophy
		&& AliceCoop::IsApplyingHostProgression())
	{
		return;
	}

	bool deferCoopSequenceActivation = false;
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
			const AAlicePawn* pawn = g_State.AlicePlayerController->MyAlicePawn;
			AliceCoop::RecordLocalAction(pawn && pawn->bShrinkingModeActive
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
			return;
		}
		if (Function == g_coopSequenceOpActivated
			|| Function == g_coopSequenceOpDeactivated)
		{
			AliceCoop::TraceSequenceOpProcessEvent(
				reinterpret_cast<UObject*>(This),
				Function == g_coopSequenceOpActivated, false);
			if (Function == g_coopSequenceOpActivated)
			{
				deferCoopSequenceActivation =
					AliceCoop::ShouldDeferSequenceOpActivation(
						reinterpret_cast<UObject*>(This));
			}
		}
		if (Function == g_coopRestartAlice
			|| Function == g_coopShowDeathConfirm
			|| Function == g_coopContinueToGame
			|| Function == g_coopLoadCheckpoint
			|| Function == g_coopRespawnAlice
			|| Function == g_coopClientRestart
			|| Function == g_coopDeathRestartGame
			|| Function == g_coopMovieRestart
			|| Function == g_coopInGameMenuQuit
			|| Function == g_coopSetInMainMenu
			|| Function == g_coopOnSetVentState
			|| Function == g_coopSetCinematicMode
			|| Function == g_coopPlayerUse
			|| Function == g_coopActorUsedBy
			|| Function == g_coopOnInteractInLondon
			|| Function == g_coopInteractInLondonX
			|| Function == g_coopExitInteractState
			|| Function == g_coopTriggerInteracted
			|| Function == g_coopNotifyInputKey
			|| Function == g_coopClientTravel
			|| Function == g_coopBackToTitle)
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
	if (deferCoopSequenceActivation)
		return;

	bool isDlcStatus = (Function == g_dlcStatusMovie || Function == g_dlcStatusPC);
	bool isCallout = (Function == g_calloutMovie || Function == g_calloutPC);

	bool isWeaponSwitch = FixWeaponSwitchFadeInEnabled && g_meleeAttack && (Function == g_meleeAttack || Function == g_weaponAttack || Function == g_switchWeaponGroup || Function == g_rightClickAttack || Function == g_switchVorpal || Function == g_switchHobby || Function == g_switchEyeStaff);

	bool switchWeaponWasHidden = false;
	if (isWeaponSwitch)
	{
		AAlicePlayerController* wpc = g_State.AlicePlayerController;
		AWeaponForAlice* w = (wpc && wpc->MyAlicePawn) ? reinterpret_cast<AWeaponForAlice*>(wpc->MyAlicePawn->Weapon) : nullptr;
		switchWeaponWasHidden = (w == nullptr) || w->bFadeToHide;
	}

	if (UnlockCompleteEditionDLC && Function == g_tickAlice)
	{
		AAlicePlayerController* pc = g_State.AlicePlayerController;
		if (pc && pc->WorldInfo && pc->WorldInfo->Game && pc->Pawn)
		{
			AAliceGameInfo* gi = (AAliceGameInfo*)pc->WorldInfo->Game;
			gi->DLC_VB_UnLock = 1;
			gi->DLC_ES_UnLock = 1;
			gi->DLC_HH_UnLock = 1;
			gi->DLC_TC_UnLock = 1;

			AWeapon* w = pc->Pawn->Weapon;
			if (w && w != s_lastSeenWeapon)
			{
				s_lastSeenWeapon = w;
				g_weaponToRefresh = w;
			}
		}
		else
		{
			s_lastSeenWeapon = nullptr;
			g_weaponToRefresh = nullptr;
		}
	}

	if (CutsceneFPSCapEnabled && uParams)
	{
		if (Function == g_interpStarted)
		{
			CutsceneFPSCap::OnInterpStarted(*reinterpret_cast<USeqAct_Interp**>(uParams));
		}
		else if (Function == g_interpFinished)
		{
			CutsceneFPSCap::OnInterpFinished(*reinterpret_cast<USeqAct_Interp**>(uParams));
		}
	}
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

	ProcessEvent.thiscall<void>(This, Function, uParams, uResult);

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
		if (Function == g_coopRestartAlice
			|| Function == g_coopShowDeathConfirm
			|| Function == g_coopContinueToGame
			|| Function == g_coopLoadCheckpoint
			|| Function == g_coopRespawnAlice
			|| Function == g_coopClientRestart
			|| Function == g_coopDeathRestartGame
			|| Function == g_coopMovieRestart
			|| Function == g_coopInGameMenuQuit
			|| Function == g_coopSetInMainMenu
			|| Function == g_coopOnSetVentState
			|| Function == g_coopSetCinematicMode
			|| Function == g_coopPlayerUse
			|| Function == g_coopActorUsedBy
			|| Function == g_coopOnInteractInLondon
			|| Function == g_coopInteractInLondonX
			|| Function == g_coopExitInteractState
			|| Function == g_coopTriggerInteracted
			|| Function == g_coopNotifyInputKey
			|| Function == g_coopClientTravel
			|| Function == g_coopBackToTitle)
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
		auto* parameters =
			reinterpret_cast<
				UGameViewportClient_eventPostRender_Params*>(
					uParams);
		AliceCoop::DrawTuningOverlay(parameters->Canvas);
		AliceCoop::PumpLifecycleCommands();
	}

	if (AliceCoop::IsEnabled() && Function == g_tickAlicePawn)
		AliceCoop::OnAlicePawnTicked(reinterpret_cast<AAlicePawn*>(This));

	if (isWeaponSwitch && switchWeaponWasHidden && g_pcCanFire && g_pawnFadeOutWeapon)
	{
		AAlicePlayerController* wpc = g_State.AlicePlayerController;
		AAlicePawn* pawn = wpc ? wpc->MyAlicePawn : nullptr;
		AWeaponForAlice* w = pawn ? reinterpret_cast<AWeaponForAlice*>(pawn->Weapon) : nullptr;

		if (w && !w->bInUse)
		{
			struct { uint32_t ReturnValue; } canFire{};
			wpc->ProcessEvent(g_pcCanFire, &canFire, nullptr);
			if (!canFire.ReturnValue)
			{
				if (g_pawnClearDelayAttach)
				{
					int noParams = 0;
					pawn->ProcessEvent(g_pawnClearDelayAttach, &noParams, nullptr);
				}

				AAlicePawn_execFadeOutWeapon_Params fade{};
				fade.bDetachFromPawn = 1;
				pawn->ProcessEvent(g_pawnFadeOutWeapon, &fade, nullptr);
			}
		}
	}

	if (UnlockCompleteEditionDLC && isDlcStatus && uParams)
	{
		*(int32_t*)((uint8_t*)uParams + 0x04) = 2;
	}

	// In-game prompts
	if (isCallout && uParams)
	{
		if (g_State.isUsingGamepad && *(int32_t*)((uint8_t*)uParams + 0x00) == 2 && DesiredControllerIconSet() == 3)
		{
			*(int32_t*)((uint8_t*)uParams + 0x00) = 3;
		}
	}

	// Menu prompts
	if (EnableControllerIcons && Function == g_getPlatform && uParams && ControllerHelper::IsConnected())
	{
		*(int32_t*)((uint8_t*)uParams + 0x00) = DesiredControllerIconSet();
	}

	if (FixUpgradeCursorLeak && Function == g_finishFirstUpgrade)
	{
		if (UGFxMovie* mv = (UGFxMovie*)UObject::FindFirstOf<UAliceGFxMovie_HUD>())
		{
			static UFunction* asVoid = UFunction::FindFunction("Function GFxUI.GFxMovie.ActionScriptVoid");
			if (asVoid)
			{
				struct { FString Path; } p;
				p.Path = FString(L"_root.cmc.removeMovieClip");
				mv->ProcessEvent(asVoid, &p, nullptr);
			}
		}
	}

	if (FixAspectRatio && g_State.isWideScreen && Function == g_updateCam && This && g_State.bRealGameplay)
	{
		AAlicePlayerController* pc = g_State.AlicePlayerController;
		AAlicePawn* pawn = pc ? (AAlicePawn*)pc->Pawn : nullptr;

		if (pawn)
		{
			float base = pawn->AliceCameraFOV;
			float want = ScaleFOV(base, ASPECT_RATIO_16_9, g_State.currentAspectRatio);
			ACamera* cam = (ACamera*)This;

			static float eased = 0.0f;
			if (eased <= 1.0f)
			{
				eased = want;
			}

			eased += (want - eased) * 0.05f;
			cam->CameraCache.POV.FOV = eased;
		}
	}
}

static void PatchPinballCannonPrompt()
{
	UFunction* fPoi = UFunction::FindFunction("Function AliceGame.AlicePlayerController.ShowPOIUIHint");
	UFunction* fCtx = UFunction::FindFunction("Function AliceGame.AlicePlayerController.ShowContextActionUIHint");
	UFunction* fTouch = UFunction::FindFunction("Function AliceGame.PinballCannon.Touch");
	UFunction* fShoot = UFunction::FindFunction("Function AliceGame.PinballCannon.ShootOut");

	if (!fPoi || !fCtx || !fTouch || !fShoot)
		return;

	const FName poiName = fPoi->Name; // the FName an EX_VirtualFunction call to ShowPOIUIHint carries
	const FName ctxName = fCtx->Name;

	auto redirect = [&](UFunction* fn, bool fixHideArg)
	{
		TArray<uint8_t>& code = fn->Script;
		for (int32_t i = 0; i + 9 <= code.size(); i++)
		{
			if (code[i] != 0x1B) // EX_VirtualFunction
				continue;

			FName* callName = reinterpret_cast<FName*>(&code[i + 1]);
			if (*callName != poiName) // not the ShowPOIUIHint call
				continue;

			*callName = ctxName; // ShowPOIUIHint -> ShowContextActionUIHint

			if (fixHideArg)
			{
				const int32_t fc = i + 9;
				if (fc + 5 <= code.size() && code[fc] == 0x1E)
				{
					*reinterpret_cast<int32_t*>(&code[fc + 1]) = -2;
				}
			}

			return; // one ShowPOIUIHint call per function
		}
	};

	redirect(fTouch, false); // Touch  : show call, arg 0.0 -> int 0 (show) -- leave as-is
	redirect(fShoot, true); // ShootOut: hide call, -1.0f -> int -2 (clears the hint)
}

void ResolveProcessEventFunctions()
{
	g_tickAlice = UFunction::FindFunction("Function AliceGame.AlicePlayerController.PlayerTick");
	g_tickAlicePawn = UFunction::FindFunction("Function AliceGame.AlicePawn.Tick");
	g_dlcStatusMovie = UFunction::FindFunction("Function AliceGame.AliceGFXMovie.DLCGetStatus");
	g_dlcStatusPC = UFunction::FindFunction("Function AliceGame.AlicePlayerController.DLCGetStatus");
	g_calloutMovie = UFunction::FindFunction("Function AliceGame.AliceGFXMovie.GetCalloutPlatform");
	g_calloutPC = UFunction::FindFunction("Function AliceGame.AlicePlayerController.GetCalloutPlatform");
	g_updateCam = UFunction::FindFunction("Function AliceGame.AlicePlayerCamera.UpdateCamera");
	g_getPlatform = UFunction::FindFunction("Function AliceGame.AliceGFXMovie.GetPlatform");
	g_finishFirstUpgrade = UFunction::FindFunction("Function AliceGame.AliceGFXMovie.finishFirstWeaponUpgrade");
	g_interpStarted = UFunction::FindFunction("Function Engine.Actor.InterpolationStarted");
	g_interpFinished = UFunction::FindFunction("Function Engine.Actor.InterpolationFinished");
	g_aliceHudPostRender =
		UFunction::FindFunction("Function AliceGame.AliceHud.PostRender");
	g_viewportPostRender =
		UFunction::FindFunction(
			"Function Engine.GameViewportClient.PostRender");

	g_meleeAttack = UFunction::FindFunction("Function AliceGame.AlicePlayerController.MeleeAttack");
	g_weaponAttack = UFunction::FindFunction("Function AliceGame.AlicePlayerController.WeaponAttack");
	g_switchWeaponGroup = UFunction::FindFunction("Function AliceGame.AlicePlayerController.SwitchWeaponGroup");
	g_rightClickAttack = UFunction::FindFunction("Function AliceGame.AlicePlayerController.RightClickAttack");
	g_startFire = UFunction::FindFunction("Function AliceGame.AlicePlayerController.StartFire");
	g_stopWeaponFire = UFunction::FindFunction("Function AliceGame.AlicePlayerController.StopWeaponFire");
	g_dodge = UFunction::FindFunction("Function AliceGame.AlicePlayerController.Dodge");
	g_changeShrinkingMode = UFunction::FindFunction(
		"Function AliceGame.AlicePlayerController.ChangeShrinkingMode");
	g_switchVorpal = UFunction::FindFunction("Function AliceGame.AlicePlayerController.SwitchToVorpalBlade");
	g_switchHobby = UFunction::FindFunction("Function AliceGame.AlicePlayerController.SwitchToHobbyHorse");
	g_switchEyeStaff = UFunction::FindFunction("Function AliceGame.AlicePlayerController.SwitchToEyeStaff");
	g_pcCanFire = UFunction::FindFunction("Function AliceGame.AlicePlayerController.CanFire");
	g_pawnFadeOutWeapon = UFunction::FindFunction("Function AliceGame.AlicePawn.FadeOutWeapon");
	g_pawnClearDelayAttach = UFunction::FindFunction("Function AliceGame.AlicePawn.ClearDelayAttachWeapon");
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
	g_coopSequenceEventCheckActivate =
		UFunction::FindFunction(
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

	if (FixPinballCannonPrompt)
	{
		PatchPinballCannonPrompt();
	}
}

void RefreshDLCWeapon()
{
	AWeapon* w = g_weaponToRefresh;
	if (w)
	{
		g_weaponToRefresh = nullptr;
		((AWeaponForAlice*)w)->ChangeDLCData();
	}
}

int QueryCalloutPlatform()
{
	AAlicePlayerController* pc = g_State.AlicePlayerController;

	if (!pc || !g_calloutPC)
		return 0;

	int platform = 0;
	pc->ProcessEvent(g_calloutPC, &platform, nullptr);
	return platform;
}

void ApplyProcessEventHook()
{
	ProcessEvent = HookHelper::CreateHook((void*)GetAddress(Addr::ProcessEvent), &ProcessEvent_Hook);
}
