		AAlicePawn* GetLocalPawn()
		{
			AAlicePlayerController* controller = g_State.AlicePlayerController;
			if (controller && controller->MyAlicePawn)
				return controller->MyAlicePawn;
			return g_State.AlicePawn;
		}

		void RefreshMapName(AWorldInfo* world)
		{
			const auto now = Clock::now();
			if (world != g_currentWorld)
			{
				if (!g_currentMap.empty())
					g_mapBeforeWorldChange = g_currentMap;
				if (g_currentWorld)
				{
					RetireTrackedRemoteNativeGlideTrails(
						g_currentWorld, nullptr, "world-change");
					ForgetTornDownWorldObjects();
				}
				ResetSharedEnemyPose(false);
				ClearRemoteProjectileVisuals(g_currentWorld != nullptr);
				g_localClockBomb = {};
				g_recentLocalPepperProjectiles.clear();
				{
					std::lock_guard lock(g_stateMutex);
					g_inboundProjectileEvents.clear();
					g_outboundProjectileEvents.clear();
					g_inboundSharedWorldEvents.clear();
					g_outboundSharedWorldEvents.clear();
				}
				g_pendingEnemyDamage.clear();
				g_seenSharedWorldEvents.clear();
				g_seenSharedWorldEventOrder.clear();
				g_deferredSharedDamageRequests.clear();
				g_pendingAuthoritativeEnemyStates.clear();
				g_localInteractionWindowUntil = {};
				g_interactionCutsceneBypassUntil = {};
				g_localInteractionKeysThisAttempt.clear();
				g_localTriggerUseCandidates.clear();
				g_sequenceOpUseSnapshot.clear();
				g_sequenceOpUseLogged.clear();
				g_pendingSequenceOpUseTrace = false;
				g_contextActorUseSnapshot.clear();
				g_pendingContextActorUseDetection = false;
				g_remoteContextInteraction = {};
				g_consumedContextUiActor = nullptr;
				g_consumedContextUiSawCinematic = false;
				g_consumedContextUiFallbackAt = {};
				g_interactionCandidateTraceCount = 0;
				g_usedEventSnapshot.clear();
				g_pendingUsedEventDetection = false;
				g_lastAuthoritativeEnemyStateSerial.clear();
				g_nextDeferredSharedWorldApply = {};
				g_currentWorld = world;
				++g_worldEpoch;
				g_interpolationTraceCount = 0;
				g_currentMap.clear();
				g_cutsceneTraceInitialized = false;
				g_waitingCutsceneAction = nullptr;
				g_waitingCutsceneKey = 0;
				g_waitingCutsceneOriginalPlayRate = 1.0f;
				g_waitingCutscenePlayRateOverridden = false;
				g_waitingCutsceneCanDeferActivation = false;
				g_waitingCutsceneActivationDeferred = false;
				g_cutsceneBarrierAdvertiseKey = 0;
				g_recentReleasedCutsceneKey = 0;
				g_cutsceneBarrierStartedAt = {};
				g_cutsceneBarrierAdvertiseUntil = {};
				g_recentReleasedCutsceneUntil = {};
				g_emergencyCutsceneAdvertiseKey = 0;
				g_emergencyCutsceneAdvertiseUntil = {};
				g_lastEmergencyCutsceneKey = 0;
				g_lastEmergencyCutsceneUntil = {};
				g_emergencyForcedCutsceneAction = nullptr;
				g_emergencyForcedCutsceneRequestedAt = {};
				g_emergencyForcedCutsceneStarted = false;
				AchievementOverlay::SetCoopWaitingForPeer(false);
				g_nextMapRefresh = now;
				g_nextSoloMinigameDetection = {};
				// Give UE3/PhysX time to finish tearing down and constructing the
				// level before another pawn with cloth and hair is requested.
				g_nextSpawnAttempt = now + std::chrono::seconds(3);
				if (g_remoteWorld != world)
				{
					g_remotePawn = nullptr;
					g_remoteController = nullptr;
					g_remoteWorld = nullptr;
				}
				TraceWorldResetInvariants();
				Log("World changed; visual proxy will be recreated.");
			}
			if (world && now >= g_nextMapRefresh)
			{
				std::string refreshedMap = world->GetMapName(false).ToString();
				if (refreshedMap.empty())
				{
					if (UObject* package = world->GetPackageObj())
						refreshedMap = package->GetName();
				}
				if (refreshedMap != g_currentMap)
				{
					g_currentMap = refreshedMap;
					const bool enteredMainMenu =
						_stricmp(g_currentMap.c_str(),
							"AliceEntry") == 0
						&& !g_mapBeforeWorldChange.empty()
						&& _stricmp(
							g_mapBeforeWorldChange.c_str(),
							"AliceEntry") != 0;
					if (enteredMainMenu
						&& !g_localReturnToMenuInitiated
						&& !g_applyingNetworkReturnToMenu)
					{
						g_localReturnToMenuInitiated = true;
						if (g_config.role == Role::Host)
							RequestHostReturnToMenu(
								"host entered AliceEntry");
						else if (g_config.role == Role::Client)
							RequestClientReturnToMenu(
								"client entered AliceEntry");
					}
					if (!g_currentMap.empty()
						&& _stricmp(g_currentMap.c_str(),
							"AliceEntry") != 0)
					{
						g_localReturnToMenuInitiated = false;
						g_clientReturnToMenuRequestPending = false;
						g_hostReturnToMenuIssued = false;
						if (g_config.role == Role::Host
							&& (g_hostCommandFlags
								& HostCommandReturnToMenu) != 0)
						{
							g_hostCommandFlags = 0;
						}
					}
					g_mapBeforeWorldChange.clear();
					Log("Active map=" + (g_currentMap.empty() ? std::string("<empty>") : g_currentMap) + '.');
				}
				g_nextMapRefresh = now + std::chrono::seconds(1);
			}
		}

		bool IsSoloMinigameLevelName(const std::string& name)
		{
			auto startsWithInsensitive =
				[&name](const char* prefix)
				{
					const std::size_t length =
						std::strlen(prefix);
					return name.size() >= length
						&& _strnicmp(
							name.c_str(), prefix, length) == 0;
				};
			return startsWithInsensitive(
					"Chapter2_W1_Ship")
				|| startsWithInsensitive(
					"Chapter2_W1_Ride_01")
				|| startsWithInsensitive("CC_2dBoat");
		}

		bool DetectSoloMinigameObject(
			const UObject* object, std::string& activeLevel)
		{
			activeLevel.clear();
			if (!object)
				return false;
			const std::string name = ObjectName(object);
			static constexpr const char* LevelTokens[] = {
				"Chapter2_W1_Ship",
				"Chapter2_W1_Ride_01",
				"CC_2dBoat",
			};
			for (const char* token : LevelTokens)
			{
				if (ContainsCaseInsensitive(name, token))
				{
					activeLevel = token;
					return true;
				}
			}
			return false;
		}

		bool DetectSoloMinigame(
			AWorldInfo* world, std::string& activeLevel)
		{
			activeLevel.clear();
			if (!world)
				return false;
			if (IsSoloMinigameLevelName(g_currentMap))
			{
				activeLevel = g_currentMap;
				return true;
			}
			for (ULevelStreaming* level :
				world->StreamingLevels)
			{
				if (!level || !level->bIsVisible)
				{
					continue;
				}
				const std::string package =
					level->PackageName.ToString();
				if (IsSoloMinigameLevelName(package))
				{
					activeLevel = package;
					return true;
				}
			}
			return false;
		}

		void SetSoloMinigameActive(
			bool active, const std::string& level)
		{
			if (active == g_soloMinigameActive
				&& (!active
					|| level == g_soloMinigameLevel))
			{
				return;
			}
			if (!active)
			{
				Log("SOLOMINIGAME passthrough disabled; "
					"normal co-op resumed.");
				g_soloMinigameActive = false;
				g_soloMinigameLevel.clear();
				AchievementOverlay::
					HideSoloMinigameWarning();
				return;
			}

			g_soloMinigameActive = true;
			g_soloMinigameLevel = level;
			AchievementOverlay::ShowSoloMinigameWarning(7000);
			AchievementOverlay::SetPeerWatchingCutscene(false);
			AchievementOverlay::SetCoopWaitingForPeer(false);
			if (g_waitingCutsceneAction)
				ReleaseCutsceneBarrier("solo-minigame");
			if (g_remotePawn)
				DestroyRemotePawn();
			ResetSharedEnemyPose(true);
			ClearRemoteProjectileVisuals(true);
			g_activeRemoteAnimationGraph.reset();
			{
				std::lock_guard lock(g_stateMutex);
				g_outboundAnimationGraph.reset();
				g_inboundAnimationGraph.reset();
				g_outboundProjectileEvents.clear();
				g_inboundProjectileEvents.clear();
				g_outboundSharedWorldEvents.clear();
				g_inboundSharedWorldEvents.clear();
			}
			Log("SOLOMINIGAME passthrough enabled for "
				+ (level.empty()
					? std::string("<unknown>")
					: level)
				+ "; gameplay replication is isolated.");
		}

		constexpr int AliceAbilityCount =
			static_cast<int>(EAliceAbilityControl::EAAC_END);
		constexpr std::uint32_t AliceAbilityBits =
			(1u << AliceAbilityCount) - 1u;

		std::uint32_t ScanAbilityMask(AAlicePlayerController* controller,
			bool deriveWeaponAbilities)
		{
			if (!controller || !IsLiveUObject(controller))
				return 0;

			std::uint32_t mask = 0;
			UAlicePersistentDataManager* persistent =
				controller->persistentDataManager;
			if (persistent && IsLiveUObject(persistent))
			{
				const int32_t ownedCount = (std::clamp)(
					controller->AbilityGot.size(), 0, 128);
				for (int abilityIndex = 0;
					abilityIndex < AliceAbilityCount; ++abilityIndex)
				{
					const FString expected = persistent->getAbilityName(
						static_cast<EAliceAbilityControl>(abilityIndex));
					if (expected.empty() || !expected.c_str())
						continue;
					for (int32_t ownedIndex = 0;
						ownedIndex < ownedCount; ++ownedIndex)
					{
						const FString& owned =
							controller->AbilityGot.at(ownedIndex);
						if (!owned.empty() && owned.c_str()
							&& _wcsicmp(owned.c_str(), expected.c_str()) == 0)
						{
							mask |= 1u << abilityIndex;
							break;
						}
					}
				}
			}

			// Persistent arrays can lag one frame behind a newly granted weapon on
			// the host. Derive flags only while publishing the authoritative state.
			// Never use this fallback while examining the client: its copied numeric
			// levels are exactly the situation where Aiming may still be missing.
			if (deriveWeaponAbilities && controller->WeaponLevel[0] > 0)
				mask |= 1u << static_cast<int>(
					EAliceAbilityControl::EAAC_VorpalBlade);
			if (deriveWeaponAbilities && controller->WeaponLevel[1] > 0)
				mask |= 1u << static_cast<int>(
					EAliceAbilityControl::EAAC_HobbyHorse);
			if (deriveWeaponAbilities && controller->WeaponLevel[2] > 0)
			{
				mask |= 1u << static_cast<int>(
					EAliceAbilityControl::EAAC_PepperGrinder);
				mask |= 1u << static_cast<int>(
					EAliceAbilityControl::EAAC_Aiming);
			}
			if (deriveWeaponAbilities && controller->WeaponLevel[3] > 0)
			{
				mask |= 1u << static_cast<int>(
					EAliceAbilityControl::EAAC_TeapotCannon);
				mask |= 1u << static_cast<int>(
					EAliceAbilityControl::EAAC_Aiming);
			}
			return mask & AliceAbilityBits;
		}

		std::uint32_t CaptureAbilityMask(
			AAlicePlayerController* controller)
		{
			if (!controller || !IsLiveUObject(controller))
				return 0;
			const auto now = Clock::now();
			const int32_t abilityCount = (std::clamp)(
				controller->AbilityGot.size(), 0, 128);
			if (controller != g_cachedAbilityController
				|| abilityCount != g_cachedAbilityCount
				|| now >= g_nextAbilityMaskScan)
			{
				g_cachedAbilityController = controller;
				g_cachedAbilityCount = abilityCount;
				g_cachedAbilityMask = ScanAbilityMask(controller, true);
				g_nextAbilityMaskScan =
					now + std::chrono::seconds(1);
			}
			return g_cachedAbilityMask;
		}

		PlayerStatePayload CaptureState(AAlicePawn* pawn, std::uint32_t actorId,
			bool authoritative, PlayerAction action, std::uint32_t actionSerial)
		{
			PlayerStatePayload state{};
			strncpy_s(state.mapName, g_currentMap.c_str(), _TRUNCATE);
			state.actorId = actorId;
			state.location[0] = pawn->Location.X;
			state.location[1] = pawn->Location.Y;
			state.location[2] = pawn->Location.Z;
			state.rotation[0] = pawn->Rotation.Pitch;
			state.rotation[1] = pawn->Rotation.Yaw;
			state.rotation[2] = pawn->Rotation.Roll;
			state.velocity[0] = pawn->Velocity.X;
			state.velocity[1] = pawn->Velocity.Y;
			state.velocity[2] = pawn->Velocity.Z;
			state.drawScale = pawn->DrawScale;
			state.drawScale3D[0] = pawn->DrawScale3D.X;
			state.drawScale3D[1] = pawn->DrawScale3D.Y;
			state.drawScale3D[2] = pawn->DrawScale3D.Z;
			if (pawn->Mesh)
			{
				state.meshTranslation[0] = pawn->Mesh->Translation.X;
				state.meshTranslation[1] = pawn->Mesh->Translation.Y;
				state.meshTranslation[2] = pawn->Mesh->Translation.Z;
				state.meshScale = pawn->Mesh->Scale;
				state.meshScale3D[0] = pawn->Mesh->Scale3D.X;
				state.meshScale3D[1] = pawn->Mesh->Scale3D.Y;
				state.meshScale3D[2] = pawn->Mesh->Scale3D.Z;
			}
			if (pawn->CylinderComponent)
			{
				state.collisionRadius = pawn->CylinderComponent->CollisionRadius;
				state.collisionHeight = pawn->CylinderComponent->CollisionHeight;
			}
			state.health = pawn->Health;
			state.healthMax = pawn->HealthMax;
			state.physics = static_cast<std::uint8_t>(pawn->Physics);
			state.movementState = static_cast<std::uint8_t>(pawn->BasicMovementState);
			state.specialMove = static_cast<std::uint8_t>(pawn->SpecialMove);
			state.jumpStatus = static_cast<std::uint8_t>(pawn->CurrentJumpStatus);
			state.dodgeStatus = static_cast<std::uint8_t>(
				pawn->CurrentDodgeStatus);
			state.action = action;
			state.actionSerial = actionSerial;
			const bool emergencyCutsceneAdvertised =
				g_emergencyCutsceneAdvertiseKey != 0
				&& Clock::now()
					< g_emergencyCutsceneAdvertiseUntil;
			state.cutsceneBarrierKey =
				emergencyCutsceneAdvertised
					? g_emergencyCutsceneAdvertiseKey
					: g_cutsceneBarrierAdvertiseKey;
			state.archetype = static_cast<std::uint8_t>(pawn->ArcheTypeID);
			const AAlicePlayerController* controller =
				g_State.AlicePlayerController;
			if (controller && IsLiveUObject(
					const_cast<AAlicePlayerController*>(controller))
				&& controller->MyAlicePawn == pawn)
			{
				for (std::size_t index = 0; index < 4; ++index)
				{
					state.weaponLevels[index] =
						static_cast<std::uint8_t>((std::clamp)(
							controller->WeaponLevel[index], 0, 4));
				}
				state.abilityMask = CaptureAbilityMask(
					const_cast<AAlicePlayerController*>(controller));
			}
			if (pawn->Weapon
				&& pawn->Weapon->IsA(AWeaponForAlice::StaticClass()))
			{
				auto* weapon =
					reinterpret_cast<AWeaponForAlice*>(pawn->Weapon);
				state.weaponType = static_cast<std::uint8_t>(
					weapon->WeaponTypeEnum);
			}
			state.flags = pawn->bHidden ? 0 : StateVisible;
			if (controller && controller->MyAlicePawn == pawn)
				state.flags |= StateProgressionValid;
			if (pawn->Mesh && pawn->Mesh->HiddenGame)
				state.flags |= StateBodyHidden;
			if (pawn->UpperBodyComponent
				&& pawn->UpperBodyComponent->HiddenGame)
				state.flags |= StateUpperBodyHidden;
			if (pawn->HairComponent && pawn->HairComponent->HiddenGame)
				state.flags |= StateHairHidden;
			if (pawn->SkirtComponent && pawn->SkirtComponent->HiddenGame)
				state.flags |= StateSkirtHidden;
			if (pawn->BowComponent && pawn->BowComponent->HiddenGame)
				state.flags |= StateBowHidden;
			if (pawn->RibbonComponent && pawn->RibbonComponent->HiddenGame)
				state.flags |= StateRibbonHidden;
			if (pawn->EarComponent && pawn->EarComponent->HiddenGame)
				state.flags |= StateEarHidden;
			if (pawn->Weapon
				&& (pawn->Weapon->bHidden
					|| (pawn->Weapon->Mesh
						&& pawn->Weapon->Mesh->HiddenGame)))
			{
				state.flags |= StateWeaponHidden;
			}
			if (pawn->bIsWalking)
				state.flags |= StateWalking;
			if (pawn->bIsCrouched)
				state.flags |= StateCrouched;
			if (pawn->bShrinkingModeActive)
				state.flags |= StateShrunk;
			if (pawn->bAliceStartCombatCam || pawn->bInLockOnMode)
				state.flags |= StateInCombat;
			if (pawn->SpecialMove != ESpecialMove::SM_None)
				state.flags |= StateActionActive;
			if (authoritative)
				state.flags |= StateAuthoritative;
			if (emergencyCutsceneAdvertised)
				state.flags |= StateForceCutscene;
			if (controller && controller->MyAlicePawn == pawn
				&& (controller->bCinematicMode
					|| (controller->bCinemaDisableInputMove
						&& controller->bCinemaDisableInputLook)))
			{
				state.flags |= StateCinematic;
			}
			state.clientTimeMs = ElapsedMilliseconds();
			return state;
		}

		void ApplyHostWeaponProgression(
			const PlayerStatePayload& hostState)
		{
			if (g_config.role != Role::Client
				|| g_config.localMirror
				|| (hostState.flags & StateProgressionValid) == 0
				|| _stricmp(hostState.mapName, g_currentMap.c_str()) != 0
				|| g_currentMap.empty()
				|| _stricmp(g_currentMap.c_str(), "AliceEntry") == 0)
			{
				return;
			}
			AAlicePlayerController* controller =
				g_State.AlicePlayerController;
			if (!controller || !IsLiveUObject(controller)
				|| !controller->MyAlicePawn)
				return;

			std::array<int32_t, 4> desired{};
			bool levelsChanged = false;
			bool allZero = true;
			for (std::size_t index = 0; index < desired.size(); ++index)
			{
				desired[index] = (std::clamp)(
					static_cast<int32_t>(
						hostState.weaponLevels[index]), 0, 4);
				levelsChanged = levelsChanged
					|| controller->WeaponLevel[index]
						!= desired[index];
				allZero = allZero && desired[index] == 0;
			}
			// During streaming transitions MyAlicePawn can already be assigned
			// while WeaponLevel is still zero-initialized. Chapter 2+ can never
			// legitimately have no weapons at all, so do not transiently erase the
			// client's inventory before persistent host data arrives.
			if (allZero
				&& _strnicmp(g_currentMap.c_str(), "Chapter1", 8) != 0)
			{
				if (g_loggedInvalidZeroProgressionMap != g_currentMap)
				{
					g_loggedInvalidZeroProgressionMap = g_currentMap;
					Log("PROGRESSION ignored transient zero host levels on map="
						+ g_currentMap + '.');
				}
				return;
			}
			g_loggedInvalidZeroProgressionMap.clear();

			const std::uint32_t desiredAbilityMask =
				hostState.abilityMask & AliceAbilityBits;
			const std::uint32_t localAbilityMask =
				ScanAbilityMask(controller, false);
			const std::uint32_t missingAbilityMask =
				desiredAbilityMask & ~localAbilityMask;

			// WeaponLevel is only the upgrade value. A clean client profile does
			// not yet own the corresponding Inventory actors, so copying levels
			// alone leaves RMB aiming and the Weapons menu locked. Treat every
			// non-zero authoritative level as an unlocked weapon for this session.
			std::uint8_t inventoryMask = 0;
			AInventory* inventory =
				controller->MyAlicePawn->InvManager
					&& IsLiveUObject(
						controller->MyAlicePawn->InvManager)
				? controller->MyAlicePawn->InvManager->InventoryChain
				: nullptr;
			for (int inventoryIndex = 0;
				inventory && inventoryIndex < 64;
				++inventoryIndex)
			{
				if (!IsLiveUObject(inventory))
					break;
				AInventory* nextInventory = inventory->Inventory;
				if (inventory->IsA(AWeaponForAlice::StaticClass()))
				{
					const int type = static_cast<int>(
						reinterpret_cast<AWeaponForAlice*>(inventory)
							->WeaponTypeEnum);
					if (type >= 1 && type <= 4)
						inventoryMask |= static_cast<std::uint8_t>(
							1u << (type - 1));
				}
				inventory = nextInventory;
			}
			std::uint8_t desiredInventoryMask = 0;
			for (std::size_t index = 0; index < desired.size(); ++index)
			{
				if (desired[index] > 0)
					desiredInventoryMask |= static_cast<std::uint8_t>(
						1u << index);
			}
			const std::uint8_t missingInventoryMask =
				desiredInventoryMask
				& static_cast<std::uint8_t>(~inventoryMask);
			const bool shouldHaveRangeWeapon =
				desired[2] > 0 || desired[3] > 0;
			const bool rangeRuntimeRefreshNeeded =
				shouldHaveRangeWeapon
				&& g_rangeProgressionPawn
					!= controller->MyAlicePawn;
			if ((!levelsChanged && missingInventoryMask == 0
					&& missingAbilityMask == 0
					&& !rangeRuntimeRefreshNeeded)
				|| Clock::now() < g_nextHostProgressionApply)
				return;

			g_nextHostProgressionApply =
				Clock::now() + std::chrono::milliseconds(750);
			g_applyingHostProgression = true;
			for (int abilityIndex = 0;
				abilityIndex < AliceAbilityCount; ++abilityIndex)
			{
				const std::uint32_t bit = 1u << abilityIndex;
				if ((missingAbilityMask & bit) == 0)
					continue;
				// Use the normal gameplay path. It updates AbilityGot and all
				// dependent runtime state; the game's later checkpoint/menu save
				// persists it into AliceCoop/client-saves.
				controller->eventSetAbility(
					static_cast<EAliceAbilityControl>(abilityIndex));
			}
			for (std::size_t index = 0; index < desired.size(); ++index)
				controller->WeaponLevel[index] = desired[index];

			for (int type = 1; type <= 4; ++type)
			{
				const std::uint8_t bit = static_cast<std::uint8_t>(
					1u << (type - 1));
				if ((missingInventoryMask & bit) == 0)
					continue;
				UClass* weaponClass = nullptr;
				switch (type)
				{
				case 1:
					weaponClass = AVorpalBlade::StaticClass();
					break;
				case 2:
					weaponClass = AHobbyHorse::StaticClass();
					break;
				case 3:
					weaponClass = AEyeStaff::StaticClass();
					break;
				case 4:
					weaponClass = ATeapotCannon::StaticClass();
					break;
				default:
					break;
				}
				if (weaponClass)
				{
					controller->AddNewAliceWeapon(
						weaponClass, desired[type - 1]);
				}
			}

			// This is the same script path used after persistent data is
			// applied. It refreshes already-instantiated inventory weapons
			// without copying the host's profile, collectibles or statistics.
			controller->ResetWeaponCurrentWeaponLevel();
			for (std::size_t index = 0; index < desired.size(); ++index)
				controller->WeaponLevel[index] = desired[index];
			const bool targetingModesCompletelyMissing =
				!controller->TMode_CombatLockOn
				&& !controller->TMode_POI
				&& !controller->TMode_BreakableActor
				&& !controller->TMode_SkeletalMeshActor
				&& !controller->TargetMergeManager
				&& !controller->PreTargetMergeManager;
			if (targetingModesCompletelyMissing)
				controller->InitTargetingModes();
			// This transient state is normally built when a ranged weapon is
			// acquired during story play. A clean profile joined directly into a
			// later checkpoint skips that path even though AbilityGot and inventory
			// are now correct, leaving RMB fire active but without FPS aiming/UI.
			controller->ResetRangeWeapons();
			controller->UpdateRangeWeaponUI();
			controller->forceUpdateRangeWeaponUI();

			// ResetWeaponCurrentWeaponLevel only guarantees a refresh for the
			// equipped weapon. Refresh every instantiated inventory weapon too,
			// otherwise the client can keep its old upgrade after switching.
			inventory =
				controller->MyAlicePawn->InvManager
					&& IsLiveUObject(
						controller->MyAlicePawn->InvManager)
					? controller->MyAlicePawn->InvManager->InventoryChain
					: nullptr;
			for (int inventoryIndex = 0;
				inventory && inventoryIndex < 64;
				++inventoryIndex)
			{
				if (!IsLiveUObject(inventory))
					break;
				AInventory* nextInventory = inventory->Inventory;
				if (!inventory->IsA(AWeaponForAlice::StaticClass()))
				{
					inventory = nextInventory;
					continue;
				}
				auto* weapon =
					reinterpret_cast<AWeaponForAlice*>(inventory);
				const int type =
					static_cast<int>(weapon->WeaponTypeEnum);
				if (type >= 1 && type <= 4
					&& desired[type - 1] > 0)
				{
					weapon->WeaponLevel = desired[type - 1];
					weapon->SaveWeaponLevel = desired[type - 1];
					weapon->ChangeWeaponLevelData(
						desired[type - 1]);
				}
				inventory = nextInventory;
			}
			g_applyingHostProgression = false;
			const std::uint32_t appliedAbilityMask =
				ScanAbilityMask(controller, false);
			g_cachedAbilityController = controller;
			g_cachedAbilityCount = (std::clamp)(
				controller->AbilityGot.size(), 0, 128);
			g_cachedAbilityMask = appliedAbilityMask;
			if (shouldHaveRangeWeapon)
				g_rangeProgressionPawn = controller->MyAlicePawn;
			g_nextAbilityMaskScan =
				Clock::now() + std::chrono::seconds(1);
			Log("PROGRESSION applied host weapon levels=["
				+ std::to_string(desired[0]) + ','
				+ std::to_string(desired[1]) + ','
				+ std::to_string(desired[2]) + ','
				+ std::to_string(desired[3])
				+ "], addedMask=0x" + [&]()
				{
					std::ostringstream stream;
					stream << std::hex
						<< static_cast<int>(missingInventoryMask);
					return stream.str();
				}() + ", hostAbilities=0x" + [&]()
				{
					std::ostringstream stream;
					stream << std::hex << desiredAbilityMask;
					return stream.str();
				}() + ", missingAbilities=0x" + [&]()
				{
					std::ostringstream stream;
					stream << std::hex << missingAbilityMask;
					return stream.str();
				}() + ", appliedAbilities=0x" + [&]()
				{
					std::ostringstream stream;
					stream << std::hex << appliedAbilityMask;
					return stream.str();
				}() + ", rangeAvailable="
				+ (controller->IsAnyAvailableRangeWeapon() ? "yes" : "no")
				+ ", pepper="
				+ (controller->IsPepperGrinderAvailable() ? "yes" : "no")
				+ ", teapot="
				+ (controller->IsTeapotCannonAvailable() ? "yes" : "no")
				+ ", canFPS="
				+ (controller->CanFirstPersonView() ? "yes" : "no")
				+ ", fpsActive="
				+ (controller->IsFirstPersonViewActivated() ? "yes" : "no")
				+ ", canSwitchRange="
				+ (controller->CanSwitchRangeWeapon() ? "yes" : "no")
				+ ", pendingRange="
				+ std::to_string(controller->PendingRangeWeaponType)
				+ ", latestRange="
				+ std::to_string(controller->LatestRangeWeaponType)
				+ ", reticleFlag="
				+ (controller->bShowFPS_Reticule ? "yes" : "no")
				+ ", targetingInit="
				+ (targetingModesCompletelyMissing ? "yes" : "already")
				+ " to client session.");
		}

		ClientCommandPayload CaptureClientCommand(const PlayerStatePayload& desiredState)
		{
			ClientCommandPayload command{};
			command.desiredState = desiredState;
			command.commandNumber = ++g_commandNumber;
			if (g_clientRestartRequestPending)
				command.buttons |= ClientCommandRequestRestart;
			if (g_clientReturnToMenuRequestPending)
				command.buttons |= ClientCommandRequestReturnToMenu;
			const AAlicePlayerController* controller = g_State.AlicePlayerController;
			if (controller && controller->PlayerInput)
			{
				command.input[0] = controller->PlayerInput->aForward;
				command.input[1] = controller->PlayerInput->aStrafe;
				command.input[2] = controller->PlayerInput->aTurn;
				command.input[3] = controller->PlayerInput->aLookUp;
			}
			for (const auto& [key, binding] :
				g_clientSharedEnemyBindings)
			{
				if (command.enemyCount >= MaxSharedEnemies
					|| !binding.authorized
					|| !binding.clientAuthority
					|| !binding.enemy
					|| binding.enemy->Health <= 0
					|| binding.enemy->bDeleteMe
					|| binding.enemy->bPendingDelete)
				{
					continue;
				}
				const AAliceGameKynapsePawn* enemy =
					binding.enemy;
				SharedEnemyPosePayload& pose =
					command.enemies[command.enemyCount++];
				pose.entityKey = key;
				pose.location[0] = enemy->Location.X;
				pose.location[1] = enemy->Location.Y;
				pose.location[2] = enemy->Location.Z;
				pose.rotation[0] = enemy->Rotation.Pitch;
				pose.rotation[1] = enemy->Rotation.Yaw;
				pose.rotation[2] = enemy->Rotation.Roll;
				pose.velocity[0] = enemy->Velocity.X;
				pose.velocity[1] = enemy->Velocity.Y;
				pose.velocity[2] = enemy->Velocity.Z;
				pose.health = enemy->Health;
				pose.flags = SharedEnemyPoseClientAuthority
					| (enemy->bHidden
						? SharedEnemyPoseHidden : 0)
					| (enemy->bIsFighting
						? SharedEnemyPoseFighting : 0);
				pose.flags |= static_cast<std::uint32_t>(
					SharedEnemyClassSignature(enemy))
					<< static_cast<unsigned>(SharedEnemyPoseClassShift);
				pose.physics =
					static_cast<std::uint8_t>(enemy->Physics);
				pose.npcState =
					static_cast<std::uint8_t>(enemy->NPCState);
				pose.healthState =
					static_cast<std::uint8_t>(
						enemy->HealthState);
				pose.aiState =
					static_cast<std::uint8_t>(enemy->AIState);
			}
			return command;
		}

		void PublishSoloMinigamePresence()
		{
			PlayerStatePayload state{};
			strncpy_s(
				state.mapName, g_currentMap.c_str(), _TRUNCATE);
			state.actorId =
				g_config.role == Role::Host ? 1u : 2u;
			state.health = 1;
			state.healthMax = 1;
			state.flags = 0;
			state.clientTimeMs = ElapsedMilliseconds();

			std::lock_guard lock(g_stateMutex);
			g_outboundAnimationGraph.reset();
			if (g_config.role == Role::Client)
			{
				ClientCommandPayload command{};
				command.desiredState = state;
				command.commandNumber = ++g_commandNumber;
				g_outboundClientCommand = command;
				return;
			}
			HostSnapshotPayload snapshot{};
			snapshot.hostState = state;
			snapshot.snapshotNumber = ++g_snapshotNumber;
			snapshot.worldEpoch = g_worldEpoch;
			snapshot.hostTimeMs = ElapsedMilliseconds();
			g_outboundHostSnapshot = snapshot;
		}

		std::optional<ClientCommandSnapshot> ReadFreshClientCommand()
		{
			std::lock_guard lock(g_stateMutex);
			if (!g_inboundClientCommand)
				return std::nullopt;
			if (Clock::now() - g_inboundClientCommand->receivedAt
				> std::chrono::milliseconds(g_config.peerTimeoutMs))
				return std::nullopt;
			return g_inboundClientCommand;
		}

		std::optional<HostWorldSnapshot> ReadFreshHostSnapshot()
		{
			std::lock_guard lock(g_stateMutex);
			if (!g_inboundHostSnapshot)
				return std::nullopt;
			if (Clock::now() - g_inboundHostSnapshot->receivedAt
				> std::chrono::milliseconds(g_config.peerTimeoutMs))
				return std::nullopt;
			return g_inboundHostSnapshot;
		}

		std::uint8_t CurrentCheckpointWireValue(
			AWorldInfo* world)
		{
			static std::uint8_t lastLoggedValue = 0xFF;
			static std::string lastLoggedSource;
			auto finish = [&](int checkpoint,
				const char* source,
				const std::string& baseLevel)
			{
				const std::uint8_t value =
					checkpoint >= 0 && checkpoint < 70
						? static_cast<std::uint8_t>(
							checkpoint + 1)
						: 0;
				const std::string sourceText =
					source ? source : "<unknown>";
				if (value != lastLoggedValue
					|| sourceText != lastLoggedSource)
				{
					lastLoggedValue = value;
					lastLoggedSource = sourceText;
					Log("SESSIONJOIN host checkpoint source="
						+ sourceText + ", chapter="
						+ std::to_string(checkpoint)
						+ ", baseLevel="
						+ (baseLevel.empty()
							? std::string("<empty>")
							: baseLevel)
						+ ", activeMap=" + g_currentMap + '.');
				}
				return value;
			};

			ACheckPointManager* manager =
				world && world->Game
					? world->Game->MyCheckPointManager : nullptr;

			// Prefer the live checkpoint objects. LoadChapter and
			// GetLastLoadedChapter commonly retain the beginning of a biome,
			// which made JOIN load a valid map but the wrong checkpoint.
			if (manager && IsLiveUObject(manager)
				&& !g_currentMap.empty()
				&& _stricmp(g_currentMap.c_str(), "AliceEntry") != 0)
			{
				auto matchesActiveMap = [&](int chapter,
					const std::string& baseLevel)
				{
					if (chapter < 0 || chapter >= 70)
						return false;
					const std::string chapterMap =
						manager->AliceMapName[chapter].ToString();
					return (baseLevel.empty()
							|| ContainsCaseInsensitive(
								baseLevel, g_currentMap.c_str())
							|| ContainsCaseInsensitive(
								g_currentMap, baseLevel.c_str()))
						&& (chapterMap.empty()
							|| ContainsCaseInsensitive(
								chapterMap, g_currentMap.c_str())
							|| ContainsCaseInsensitive(
								g_currentMap, chapterMap.c_str()));
				};
				UAliceCheckpoint* liveCheckpoint =
					g_State.AliceEngine
						&& IsLiveUObject(g_State.AliceEngine)
						? g_State.AliceEngine->CurrentCheckpoint
						: nullptr;
				if (liveCheckpoint && IsLiveUObject(liveCheckpoint))
				{
					const int chapter =
						static_cast<int>(liveCheckpoint->Chapter);
					const std::string baseLevel =
						liveCheckpoint->BaseLevelName.ToString();
					if (matchesActiveMap(chapter, baseLevel))
						return finish(chapter, "engine-current", baseLevel);
				}
				const int managerCheckpoint =
					static_cast<int>(manager->LastCheckPoint);
				const std::string managerLevel =
					managerCheckpoint >= 0 && managerCheckpoint < 70
						? manager->AliceMapName[managerCheckpoint].ToString()
						: std::string();
				if (matchesActiveMap(managerCheckpoint, managerLevel))
				{
					return finish(managerCheckpoint,
						"manager-current", managerLevel);
				}
			}
			if (manager && IsLiveUObject(manager)
				&& !g_currentMap.empty()
				&& _stricmp(
					g_currentMap.c_str(), "AliceEntry") != 0)
			{
				auto chapterMatchesMap =
					[&](int chapter)
					{
						if (chapter < 0 || chapter >= 70)
							return false;
						const std::string candidateMap =
							manager->AliceMapName[chapter].ToString();
						return candidateMap.empty()
							|| ContainsCaseInsensitive(
								candidateMap, g_currentMap.c_str())
							|| ContainsCaseInsensitive(
								g_currentMap, candidateMap.c_str());
					};

				if (chapterMatchesMap(g_hostRequestedChapter))
				{
					return finish(
						g_hostRequestedChapter,
						"captured-LoadChapter",
						manager->AliceMapName[
							g_hostRequestedChapter].ToString());
				}

				if (g_lastLoadedChapterProbeMap != g_currentMap)
				{
					g_lastLoadedChapterProbeMap = g_currentMap;
					g_lastLoadedChapterProbe = -1;
					std::uint8_t nativeChapter = 0;
					if (NativeGetLastLoadedChapter(
							manager, nativeChapter)
						&& nativeChapter < 70)
					{
						g_lastLoadedChapterProbe =
							static_cast<int>(nativeChapter);
						Log("SESSIONJOIN native GetLastLoadedChapter="
							+ std::to_string(
								g_lastLoadedChapterProbe)
							+ ", activeMap=" + g_currentMap + '.');
					}
				}
				if (chapterMatchesMap(g_lastLoadedChapterProbe))
				{
					return finish(
						g_lastLoadedChapterProbe,
						"native-last-loaded",
						manager->AliceMapName[
							g_lastLoadedChapterProbe].ToString());
				}
			}

			UAliceCheckpoint* current =
				g_State.AliceEngine
					&& IsLiveUObject(g_State.AliceEngine)
					? g_State.AliceEngine->CurrentCheckpoint
					: nullptr;
			if (current && IsLiveUObject(current))
			{
				const int checkpoint =
					static_cast<int>(current->Chapter);
				const std::string baseLevel =
					current->BaseLevelName.ToString();
				const std::string chapterMap =
					manager && IsLiveUObject(manager)
						&& checkpoint >= 0 && checkpoint < 70
						? manager->AliceMapName[
							checkpoint].ToString()
						: std::string();
				const bool baseMapMatches =
					g_currentMap.empty()
					|| baseLevel.empty()
					|| ContainsCaseInsensitive(
						baseLevel, g_currentMap.c_str())
					|| ContainsCaseInsensitive(
						g_currentMap, baseLevel.c_str());
				const bool chapterMapMatches =
					g_currentMap.empty()
					|| chapterMap.empty()
					|| ContainsCaseInsensitive(
						chapterMap, g_currentMap.c_str())
					|| ContainsCaseInsensitive(
						g_currentMap, chapterMap.c_str());
				if (checkpoint >= 0 && checkpoint < 70
					&& baseMapMatches && chapterMapMatches)
				{
					return finish(
						checkpoint,
						"engine-current",
						baseLevel);
				}
			}

			if (!manager || !IsLiveUObject(manager))
				return finish(-1, "unavailable", {});
			const int checkpoint =
				static_cast<int>(manager->LastCheckPoint);
			const std::string baseLevel =
				checkpoint >= 0 && checkpoint < 70
					? manager->AliceMapName[checkpoint].ToString()
					: std::string();
			if (!g_currentMap.empty()
				&& _stricmp(
					g_currentMap.c_str(), "AliceEntry") != 0
				&& !baseLevel.empty()
				&& !ContainsCaseInsensitive(
					baseLevel, g_currentMap.c_str())
				&& !ContainsCaseInsensitive(
					g_currentMap, baseLevel.c_str()))
			{
				return finish(
					-1, "manager-map-mismatch", baseLevel);
			}
			return finish(
				checkpoint, "manager-fallback", baseLevel);
		}

		void SetJoinHostStatus(const std::string& status)
		{
			if (g_joinHostStatus == status)
				return;
			g_joinHostStatus = status;
			Log("SESSIONJOIN " + status + '.');
		}

		void BeginJoinHostAttempts()
		{
			const auto now = Clock::now();
			AchievementOverlay::HideCoopJoinTeleportHint();
			g_joinHostPending = false;
			g_joinHostRetryUntil = now + std::chrono::seconds(15);
			g_nextJoinHostRetry = now;
			g_joinHostLoadInvokedAt = {};
			g_joinHostAttemptCount = 0;
			SetJoinHostStatus("JOIN REQUESTED");
		}

		void FinishJoinHostAttempts()
		{
			g_joinHostRetryUntil = {};
			g_nextJoinHostRetry = {};
			g_joinHostLoadInvokedAt = {};
			g_joinHostAttemptCount = 0;
		}

		bool TryJoinHostCheckpoint(AWorldInfo* world)
		{
			if (g_config.role != Role::Client
				|| !world || !world->Game)
			{
				SetJoinHostStatus("JOIN UNAVAILABLE");
				return false;
			}
			const auto host = ReadFreshHostSnapshot();
			if (!host)
			{
				SetJoinHostStatus("HOST IS OFFLINE");
				return false;
			}
			const std::string hostMap =
				host->snapshot.hostState.mapName;
			if (hostMap.empty()
				|| _stricmp(hostMap.c_str(), "AliceEntry") == 0)
			{
				SetJoinHostStatus("HOST IS IN MAIN MENU");
				return false;
			}
			if ((host->snapshot.hostState.flags
					& StateCinematic) != 0
				|| host->snapshot.hostState.cutsceneBarrierKey != 0)
			{
				SetJoinHostStatus("HOST IS IN CUTSCENE");
				return false;
			}
			if (g_joinHostEligibilityMap != hostMap
				|| g_joinHostEligibilityEpoch
					!= host->snapshot.worldEpoch
				|| g_joinHostEligibleSince
					== Clock::time_point{}
				|| Clock::now() - g_joinHostEligibleSince
					< std::chrono::milliseconds(2500))
			{
				SetJoinHostStatus("HOST IS LOADING...");
				return false;
			}
			ACheckPointManager* manager =
				world->Game->MyCheckPointManager;
			if (!manager || !IsLiveUObject(manager))
			{
				SetJoinHostStatus("CHECKPOINT MANAGER NOT READY");
				return false;
			}

			int checkpoint = -1;
			const std::uint8_t hostCheckpoint =
				host->snapshot.reserved[0];
			if (hostCheckpoint >= 1 && hostCheckpoint <= 70)
			{
				const int candidate =
					static_cast<int>(hostCheckpoint) - 1;
				const std::string candidateMap =
					manager->AliceMapName[candidate].ToString();
				if (candidateMap.empty()
					|| ContainsCaseInsensitive(
						candidateMap, hostMap.c_str())
					|| ContainsCaseInsensitive(
						hostMap, candidateMap.c_str()))
				{
					checkpoint = candidate;
				}
			}
			if (checkpoint < 0)
			{
				int matchingCheckpoint = -1;
				int matchingCheckpointCount = 0;
				for (int index = 0; index < 70; ++index)
				{
					const std::string candidateMap =
						manager->AliceMapName[index].ToString();
					if (!candidateMap.empty()
							&& (ContainsCaseInsensitive(
								candidateMap, hostMap.c_str())
							|| ContainsCaseInsensitive(
								hostMap, candidateMap.c_str())))
					{
						if (matchingCheckpoint < 0)
							matchingCheckpoint = index;
						++matchingCheckpointCount;
					}
				}
				if (matchingCheckpointCount == 1)
					checkpoint = matchingCheckpoint;
				else if (matchingCheckpointCount > 1)
				{
					// A deterministic biome-start fallback is preferable to a dead
					// JOIN button. Once loaded, P can safely close the remaining gap.
					checkpoint = matchingCheckpoint;
					Log("SESSIONJOIN ambiguous map-only fallback hostMap="
						+ hostMap + ", matches="
						+ std::to_string(
							matchingCheckpointCount)
						+ ", selected="
						+ std::to_string(checkpoint) + '.');
				}
			}
			if (checkpoint < 0)
			{
				SetJoinHostStatus("HOST CHECKPOINT UNKNOWN");
				Log("SESSIONJOIN map lookup failed hostMap="
					+ hostMap + ", wireCheckpoint="
					+ std::to_string(hostCheckpoint) + '.');
				return false;
			}

			g_joinHostPending = true;
			g_joinHostLoadInvokedAt = Clock::now();
			++g_joinHostAttemptCount;
			SetJoinHostStatus("LOADING HOST CHECKPOINT...");
			const bool invoked = NativeLoadChapter(
				manager,
				static_cast<EChapterNameList>(checkpoint));
			Log("SESSIONJOIN LoadChapter hostMap=" + hostMap
				+ ", checkpoint=" + std::to_string(checkpoint)
				+ ", attempt="
				+ std::to_string(g_joinHostAttemptCount)
				+ ", native="
				+ (invoked ? std::string("yes.")
					: std::string("no.")));
			if (!invoked)
			{
				g_joinHostPending = false;
				g_joinHostLoadInvokedAt = {};
				SetJoinHostStatus("LOAD CHAPTER FAILED");
			}
			return invoked;
		}

		void UpdateCoopMainMenuPanel(AWorldInfo* world)
		{
			const bool visible =
				!g_config.localMirror
				&& !g_config.actionTrace
				&& _stricmp(
					g_currentMap.c_str(), "AliceEntry") == 0;
			bool peerConnected = false;
			bool peerCinematic = false;
			std::string peerMap;
			std::uint32_t peerWorldEpoch = 0;
			if (g_config.role == Role::Host)
			{
				const auto peer = ReadFreshClientCommand();
				if (peer)
				{
					peerConnected = true;
					peerMap =
						peer->command.desiredState.mapName;
				}
			}
			else
			{
				const auto peer = ReadFreshHostSnapshot();
				if (peer)
				{
					peerConnected = true;
					peerMap =
						peer->snapshot.hostState.mapName;
					peerWorldEpoch =
						peer->snapshot.worldEpoch;
					peerCinematic =
						(peer->snapshot.hostState.flags
							& StateCinematic) != 0
						|| peer->snapshot.hostState
							.cutsceneBarrierKey != 0;
				}
			}

			const bool hostInGameplay =
				g_config.role == Role::Client
				&& peerConnected
				&& !peerMap.empty()
				&& _stricmp(
					peerMap.c_str(), "AliceEntry") != 0;
			const bool joinCandidate =
				hostInGameplay && !peerCinematic;
			const auto now = Clock::now();
			if (!joinCandidate)
			{
				g_joinHostEligibilityMap.clear();
				g_joinHostEligibilityEpoch = 0;
				g_joinHostEligibleSince = {};
			}
			else if (g_joinHostEligibilityMap != peerMap
				|| g_joinHostEligibilityEpoch != peerWorldEpoch)
			{
				g_joinHostEligibilityMap = peerMap;
				g_joinHostEligibilityEpoch = peerWorldEpoch;
				g_joinHostEligibleSince = now;
			}
			const bool hostStateSettled =
				joinCandidate
				&& g_joinHostEligibleSince
					!= Clock::time_point{}
				&& now - g_joinHostEligibleSince
					>= std::chrono::milliseconds(2500);
			std::string status;
			if (!g_connected.load())
				status = "CONNECTING TO RELAY...";
			else if (!peerConnected)
				status = "WAITING FOR PEER...";
			else if (g_config.role == Role::Client
				&& !hostInGameplay)
				status = "HOST IS IN MAIN MENU";
			else if (g_config.role == Role::Client
				&& peerCinematic)
				status = "HOST IS IN CUTSCENE";
			else if (g_config.role == Role::Client
				&& hostInGameplay && !hostStateSettled)
				status = "HOST IS LOADING...";
			else if (g_joinHostPending)
				status = g_joinHostStatus;
			else
				status = "CONNECTED";

			AchievementOverlay::SetCoopMainMenuState(
				visible,
				g_config.role == Role::Host,
				g_connected.load(),
				peerConnected,
				peerMap,
				visible && hostStateSettled
					&& !peerCinematic
					&& !g_joinHostPending,
				status);
			if (visible && world
				&& AchievementOverlay::
					ConsumeCoopJoinHostRequest())
			{
				BeginJoinHostAttempts();
			}

			if (visible && world
				&& g_config.role == Role::Client
				&& g_joinHostRetryUntil != Clock::time_point{})
			{
				// A native LoadChapter request can be accepted before the title
				// menu's checkpoint manager is fully ready and then silently leave
				// us in AliceEntry. One L/click therefore owns a bounded retry
				// window instead of making the player press the button repeatedly.
				if (g_joinHostPending
					&& g_joinHostLoadInvokedAt != Clock::time_point{}
					&& now - g_joinHostLoadInvokedAt
						>= std::chrono::seconds(5))
				{
					g_joinHostPending = false;
					g_joinHostLoadInvokedAt = {};
					g_nextJoinHostRetry = now
						+ std::chrono::milliseconds(250);
					SetJoinHostStatus("LOAD DID NOT START; RETRYING");
				}

				if (!g_joinHostPending
					&& now >= g_nextJoinHostRetry
					&& now < g_joinHostRetryUntil)
				{
					g_nextJoinHostRetry = now
						+ std::chrono::milliseconds(900);
					TryJoinHostCheckpoint(world);
				}
				else if (!g_joinHostPending
					&& now >= g_joinHostRetryUntil)
				{
					FinishJoinHostAttempts();
					SetJoinHostStatus("JOIN FAILED - PRESS L TO RETRY");
				}
			}
		}

		std::optional<ReceivedAnimationGraph> ReadFreshAnimationGraph()
		{
			std::lock_guard lock(g_stateMutex);
			if (!g_inboundAnimationGraph)
				return std::nullopt;
			if (Clock::now() - g_inboundAnimationGraph->receivedAt
				> std::chrono::milliseconds(g_config.peerTimeoutMs))
			{
				return std::nullopt;
			}
			return g_inboundAnimationGraph;
		}

		void MirrorMaterials(UMeshComponent* destination, const UMeshComponent* source)
		{
			if (!destination || !source)
				return;
			for (int32_t index = 0; index < source->Materials.size(); ++index)
				destination->SetMaterial(index, source->Materials.at(index));
		}

		void MirrorSkeletalComponent(USkeletalMeshComponent* destination,
			const USkeletalMeshComponent* source, USkeletalMeshComponent* remoteRoot)
		{
			if (!destination || !source)
				return;
			destination->SetSkeletalMesh(source->SkeletalMesh, false, false);
			destination->SetAnimTreeTemplate(source->AnimTreeTemplate);
			destination->HiddenGame = source->HiddenGame;
			destination->bOwnerNoSee = false;
			destination->bOnlyOwnerSee = false;
			if (destination != remoteRoot)
				destination->AttachedToSkelComponent = remoteRoot;
			MirrorMaterials(destination, source);
		}

		void MirrorClothComponent(UClothComponent* destination,
			const UClothComponent* source, USkeletalMeshComponent* remoteRoot)
		{
			if (!destination || !source)
				return;
			MirrorSkeletalComponent(destination, source, remoteRoot);
			destination->ClothPhysicsAsset = source->ClothPhysicsAsset;
			destination->FixedBone = source->FixedBone;
			destination->FixedTargetClothName = source->FixedTargetClothName;
			destination->FixedTargetBone = source->FixedTargetBone;
			destination->ClothName = source->ClothName;
			destination->bPendingReset = true;
			destination->Reset();
		}

		void MirrorCosmetics(AAlicePawn* remote, const AAlicePawn* local)
		{
			if (!remote || !local)
				return;

			remote->ArcheTypeID = local->ArcheTypeID;
			remote->bArchetypeWonderLand = local->bArchetypeWonderLand;
			remote->CurWonderlandDress = local->CurWonderlandDress;
			remote->AliceCurrentDress = local->AliceCurrentDress;
			MirrorSkeletalComponent(remote->Mesh, local->Mesh, remote->Mesh);
			MirrorSkeletalComponent(remote->UpperBodyComponent, local->UpperBodyComponent, remote->Mesh);

			if (remote->HairComponent && local->HairComponent)
			{
				remote->Hair = local->Hair;
				remote->HairComponent->Template = local->HairComponent->Template;
				remote->HairComponent->PhysicsAsset = local->HairComponent->PhysicsAsset;
				remote->HairComponent->Material = local->HairComponent->Material;
				remote->HairComponent->OverrideMesh = remote->Mesh;
				remote->HairComponent->HiddenGame = local->HairComponent->HiddenGame;
				remote->HairComponent->bOwnerNoSee = false;
				remote->HairComponent->bOnlyOwnerSee = false;
				remote->HairComponent->bPendingReset = true;
				remote->HairComponent->Reset();
			}

			MirrorClothComponent(remote->SkirtComponent, local->SkirtComponent, remote->Mesh);
			MirrorClothComponent(remote->BowComponent, local->BowComponent, remote->Mesh);
			MirrorClothComponent(remote->RibbonComponent, local->RibbonComponent, remote->Mesh);
			MirrorClothComponent(remote->EarComponent, local->EarComponent, remote->Mesh);
			remote->eventResetClothHair(true, true);
			remote->AliceHairAir();
		}

