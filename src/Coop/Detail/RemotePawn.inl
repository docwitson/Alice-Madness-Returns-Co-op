		bool BindLocalMirrorPose(AAlicePawn* remote, AAlicePawn* source)
		{
			if (!remote || !source || !remote->Mesh || !source->Mesh)
				return false;

			if (remote->Mesh->SkeletalMesh != source->Mesh->SkeletalMesh)
			{
				if (!g_loggedLocalMirrorIncompatibility)
				{
					g_loggedLocalMirrorIncompatibility = true;
					Log("WARN: Local mirror body skeleton mismatch: source="
						+ ObjectName(source->Mesh->SkeletalMesh)
						+ ", mirror=" + ObjectName(remote->Mesh->SkeletalMesh) + '.');
				}
				return false;
			}

			if (remote->Mesh->ParentAnimComponent != source->Mesh)
				remote->Mesh->SetParentAnimComponent(source->Mesh);

			// Share only the evaluated skeleton. The duplicate must retain its own
			// actor/component transform so it remains beside the playable Alice.
			remote->Mesh->bTransformFromAnimParent = false;

			bool upperBodyBound = false;
			if (remote->UpperBodyComponent && source->UpperBodyComponent
				&& remote->UpperBodyComponent->SkeletalMesh
					== source->UpperBodyComponent->SkeletalMesh)
			{
				if (remote->UpperBodyComponent->ParentAnimComponent
					!= source->UpperBodyComponent)
				{
					remote->UpperBodyComponent->SetParentAnimComponent(
						source->UpperBodyComponent);
				}
				remote->UpperBodyComponent->bTransformFromAnimParent = false;
				upperBodyBound = remote->UpperBodyComponent->ParentAnimComponent
					== source->UpperBodyComponent;
			}

			const bool bodyBound = remote->Mesh->ParentAnimComponent == source->Mesh;
			if (bodyBound && !g_loggedLocalMirrorBinding)
			{
				g_loggedLocalMirrorBinding = true;
				Log("Local mirror pose parent bound: sourceBones="
					+ std::to_string(source->Mesh->SpaceBases.size())
					+ ", mirrorBones=" + std::to_string(remote->Mesh->SpaceBases.size())
					+ ", upperBody=" + (upperBodyBound ? "bound" : "not-bound") + '.');
			}
			return bodyBound;
		}

		void AttachRemoteController(AAlicePawn* remote)
		{
			if (!remote)
				return;

			UClass* controllerClass = AAliceGameCloneAliceAIController::StaticClass();
			if (!controllerClass)
			{
				Log("WARN: AliceGameCloneAliceAIController class was not found.");
				return;
			}

			AActor* spawnContext = remote;
			if (remote->WorldInfo && remote->WorldInfo->Game)
				spawnContext = remote->WorldInfo->Game;

			// Controllers are world services rather than children of their pawn. Using
			// the pawn as Owner makes UE3 reject AliceGameCloneAliceAIController here.
			// The generated Actor::Spawn wrapper rejects controller classes
			// in this build. Invoke the native Spawn event directly, as is
			// already done for the hair and projectile presentation actors.
			AActor* actor = NativeSpawn(
				spawnContext, controllerClass, nullptr, FName(),
				remote->Location, remote->Rotation, nullptr, true);
			bool usedGenericController = false;
			if (!actor)
			{
				UClass* fallbackClass = AAIController::StaticClass();
				if (fallbackClass)
				{
					actor = NativeSpawn(
						spawnContext, fallbackClass, nullptr, FName(),
						remote->Location, remote->Rotation,
						nullptr, true);
					usedGenericController = actor != nullptr;
				}
			}
			auto* controller = reinterpret_cast<AController*>(actor);
			if (!controller)
			{
				Log("WARN: Failed to spawn both the Alice clone controller and"
					" the generic AI controller.");
				return;
			}

			controller->eventPossess(remote, false);
			controller->Pawn = remote;
			remote->Controller = controller;
			// Sphinx treats damage instigators with non-player controllers as
			// NPC-on-NPC traffic and keeps Alice as its fight target. This
			// controller exists only for the authoritative remote Alice, so it
			// is safe and necessary to identify it as the second player.
			controller->bIsPlayer =
				g_config.role == Role::Host;
			g_remoteController = controller;

			std::ostringstream stream;
			stream << "Remote service controller attached ["
				<< (usedGenericController ? "generic AI fallback" : "Alice clone")
				<< "] ("
				<< (g_config.role == Role::Host ? "host authority" : "client presentation")
				<< "): object=" << ObjectName(controller)
				<< ", specialMoveClasses="
				<< remote->SpecialMoveClasses.size()
				<< ", specialMoves=" << remote->SpecialMoves.size()
				<< ", weapon=" << ObjectName(remote->Weapon) << '.';
			Log(stream.str());
		}

		AAlicePawn* SpawnRemotePawn(AAlicePawn* localPawn, AWorldInfo* world,
			const PlayerStatePayload& state)
		{
			if (!world->Game || Clock::now() < g_nextSpawnAttempt)
				return nullptr;
			g_nextSpawnAttempt = Clock::now() + std::chrono::seconds(2);

			auto* game = reinterpret_cast<AAliceGameInfo*>(world->Game);
			AAlicePawn* actorTemplate = game->AliceArcheType;
			const int archetype = static_cast<int>(localPawn->ArcheTypeID);
			if (archetype >= 0 && archetype < 9 && game->AliceCachedArcheType[archetype])
				actorTemplate = game->AliceCachedArcheType[archetype];
			if (!actorTemplate)
				actorTemplate = localPawn;
			const EAliceArcheType localArchetypeBeforeFactory = localPawn->ArcheTypeID;
			const EAliceArcheType gameArchetypeBeforeFactory = game->AliceArcheTypeID;
			LogCosmeticState("local-before-factory", localPawn);

			const FVector location(state.location[0], state.location[1], state.location[2]);
			const FRotator rotation(state.rotation[0], state.rotation[1], state.rotation[2]);
			UClass* spawnClass = actorTemplate && actorTemplate->Class
				? actorTemplate->Class
				: localPawn->Class;
			AActor* spawned = nullptr;
			AAlicePlayerController* controller = g_State.AlicePlayerController;
			if (controller && controller->StartSpot)
			{
				APawn* const originalControllerPawn = controller->Pawn;
				APawn* const originalAcknowledgedPawn = controller->AcknowledgedPawn;
				AAlicePawn* const originalMyAlicePawn = controller->MyAlicePawn;
				AActor* const originalViewTarget = controller->ViewTarget;
				const std::uint8_t originalIgnoreMoveInput = controller->bIgnoreMoveInput;
				const std::uint8_t originalIgnoreLookInput = controller->bIgnoreLookInput;
				const bool originalCinemaDisableInputMove = controller->bCinemaDisableInputMove;
				const bool originalCinemaDisableInputLook = controller->bCinemaDisableInputLook;
				ANavigationPoint* const startSpot = controller->StartSpot;
				const FVector originalStartLocation = startSpot->Location;
				const FRotator originalStartRotation = startSpot->Rotation;

				// Spawn the factory pawn at its final network position. Hair and cloth
				// simulators bind during construction and do not reliably survive the
				// large PlayerStart -> peer teleport performed by the old proxy path.
				startSpot->Location = location;
				startSpot->Rotation = rotation;
				APawn* defaultPawn = game->SpawnDefaultPawnFor(controller, startSpot);
				startSpot->Location = originalStartLocation;
				startSpot->Rotation = originalStartRotation;
				if (defaultPawn && defaultPawn != localPawn)
				{
					spawned = defaultPawn;
					Log("Visual proxy created through AliceGameInfo.SpawnDefaultPawnFor"
						" with archetype=" + std::to_string(static_cast<int>(localPawn->ArcheTypeID)) + '.');
				}
				if (localPawn->ArcheTypeID != localArchetypeBeforeFactory
					|| game->AliceArcheTypeID != gameArchetypeBeforeFactory)
				{
					Log("Factory archetype side effect: local "
						+ std::to_string(static_cast<int>(localArchetypeBeforeFactory)) + " -> "
						+ std::to_string(static_cast<int>(localPawn->ArcheTypeID))
						+ ", game "
						+ std::to_string(static_cast<int>(gameArchetypeBeforeFactory)) + " -> "
						+ std::to_string(static_cast<int>(game->AliceArcheTypeID)) + '.');
				}

				// AliceGame's factory mutates the supplied controller while constructing the pawn.
				// Restore every gameplay-facing link so input remains bound to the original Alice.
				controller->Pawn = originalControllerPawn ? originalControllerPawn : localPawn;
				controller->AcknowledgedPawn = originalAcknowledgedPawn
					? originalAcknowledgedPawn
					: localPawn;
				controller->MyAlicePawn = originalMyAlicePawn ? originalMyAlicePawn : localPawn;
				controller->ViewTarget = originalViewTarget ? originalViewTarget : localPawn;
				controller->bIgnoreMoveInput = originalIgnoreMoveInput;
				controller->bIgnoreLookInput = originalIgnoreLookInput;
				controller->bCinemaDisableInputMove = originalCinemaDisableInputMove;
				controller->bCinemaDisableInputLook = originalCinemaDisableInputLook;
				localPawn->Controller = controller;
				g_State.AlicePawn = localPawn;
			}

			if (!spawned)
			{
				spawned = localPawn->Spawn(spawnClass, nullptr,
					FName(), location, rotation, actorTemplate, true);
			}
			if (!spawned && localPawn->Class)
			{
				Log("Template spawn failed; retrying from the local pawn class default.");
				spawned = localPawn->Spawn(localPawn->Class, nullptr,
					FName(), location, rotation, nullptr, true);
			}
			auto* remote = reinterpret_cast<AAlicePawn*>(spawned);
			if (!remote)
			{
				const std::string className = spawnClass ? spawnClass->GetName() : "<null>";
				const std::string templateName = actorTemplate ? actorTemplate->GetFullName() : "<null>";
				Log("Visual proxy spawn failed; class=" + className
					+ ", template=" + templateName + "; will retry.");
				return nullptr;
			}

			// The factory normally duplicates the requested archetype completely. Retain the
			// component-level fallback for builds that silently substitute the default archetype.
			if (remote->ArcheTypeID != localPawn->ArcheTypeID)
			{
				Log("Factory substituted a different archetype; applying cosmetic fallback.");
				MirrorCosmetics(remote, localPawn);
			}
			// The client copy remains presentation-only. On the host the
			// proxy must be a valid damage target, otherwise Sphinx rejects
			// it before considering any of the controller target fields.
			remote->bCanBeDamaged =
				g_config.role == Role::Host
				&& g_config.sharedEnemyTransforms;
			remote->Instigator = remote;
			remote->Controller = nullptr;
			remote->PlayerReplicationInfo = nullptr;
			remote->Physics = EPhysics::PHYS_None;
			remote->SpecialMove = ESpecialMove::SM_None;
			remote->PreviousSpecialMove = ESpecialMove::SM_None;
			remote->CurrentJumpStatus = EJumpStatus::EMT_None;
			remote->RemoteRole = ENetRole::ROLE_None;
			remote->Role = ENetRole::ROLE_Authority;
			remote->bHidden = false;
			remote->bOnlyOwnerSee = false;
			NativeSetActorHidden(remote, false);
			if (remote->Mesh)
			{
				remote->Mesh->bOwnerNoSee = false;
				remote->Mesh->bOnlyOwnerSee = false;
			}
			if (g_config.disableProxyCollision)
			{
				remote->bCollideActors = false;
				remote->bCollideWorld = false;
				remote->bBlockActors = false;
				remote->SetCollision(false, false, true);
				remote->SetCollisionType(ECollisionType::COLLIDE_NoCollision);
			}
			remote->SetLocationNoCheck(location);
			remote->SetRotation(rotation);
			remote->Location = location;
			remote->Rotation = rotation;
			NativeForceUpdateComponents(remote, false, false);
			LogCosmeticState("local-before-reattach", localPawn);
			LogCosmeticState("remote-before-reattach", remote);
			ReattachProxyCosmetics(remote);
			SpawnRemoteHairProxy(remote);
			EnableRemoteAnimationTick(remote);
			LogCosmeticState("remote-after-reattach", remote);
			g_remotePawn = remote;
			g_remoteWorld = world;
			ResetRemoteWeaponPresentationState();
			g_remoteDodgeVisualHidden = false;
			g_remoteGlideVfxActive = false;
			g_remoteAirResetPending = false;
			g_remoteGlideInactiveSince = {};
			g_remoteNativeGlideCurrent = nullptr;
			g_remoteNativeGlideCurrentSince = {};
			g_nextRemoteNativeGlideSweep = {};
			if (g_remoteGlideParticle)
				RetirePersistentPresentationParticle(
					g_remoteGlideParticle);
			g_remoteGlideParticle = nullptr;
			g_remoteClockBombAnimationActive = false;
			g_devForceProxyHidden = false;
			g_devDodgeHideUntil = {};
			g_devMuzzleUntil = {};
			g_devGlideUntil = {};
			g_remoteDodgeParticleTemplate = nullptr;
			g_remoteDodgeParticleUntil = {};
			g_nextRemoteDodgeParticle = {};
			g_remoteFullBodyNotifyCursor = {};
			g_recentRemoteVisualNotifies.clear();
			g_lastRemoteVfx = "none";
			g_remoteBaseDrawScale = remote->DrawScale;
			g_remoteShrinkApplied = false;
			g_remoteHidden = false;
			g_remoteRenderDetached = false;
			g_loggedPostTickPresentation = false;
			g_loggedPostEngineSkeletonRefresh = false;
			g_loggedPresentedCosmetics = false;
			g_loggedDelayedCosmetics = false;
			g_remoteHairOwnerFallbackApplied = false;
			g_remoteCosmeticDiagnosticAt =
				Clock::now() + std::chrono::seconds(2);
			g_nextCosmeticSpatialSample =
				Clock::now() + std::chrono::seconds(1);
			// The read-only spatial trace has served its purpose. Keep the
			// instrumentation available for future diagnostics without
			// producing twenty large samples during every normal connection.
			g_cosmeticSpatialSamplesRemaining = 0;
			g_cosmeticSpatialSampleNumber = 0;
			g_remotePresentation = {};
			g_remoteGraphSequences.clear();
			g_lastAppliedAnimationGraphFrame = 0;
			g_loggedRemoteAnimationGraph = false;
			g_lastRemoteAnimationGraphSignature.clear();
			g_remoteAppliedFullBodyNode = nullptr;
			g_remoteAppliedFullBodySlot = nullptr;
			g_remoteAppliedFullBodyName.clear();
			ResetPresentationAnimChannel(g_remoteFullBodyChannel);
			ResetPresentationAnimChannel(g_remoteUpperAdditiveChannel);
			g_loggedAnimationStageFailures.clear();
			g_loggedAnimationStageSuccesses.clear();
			g_loggedConfigAnimationStages.clear();
			g_animationStagePoseSamples.clear();
			g_loggedNotifyInventories.clear();
			g_loggedLocalMirrorBinding = false;
			g_loggedLocalMirrorIncompatibility = false;
			g_lastRemoteActionSerial = state.actionSerial;
			g_lastRemoteSpecialMove = static_cast<std::uint8_t>(ESpecialMove::SM_None);
			g_loggedMissingSpecialMoves = false;
			if (g_config.localMirror)
				BindLocalMirrorPose(remote, localPawn);
			Log(std::string(g_config.localMirror
				? "Local mirror Alice"
				: (g_config.role == Role::Host
					? "Host-authoritative remote Alice"
					: "Remote Alice presentation proxy"))
				+ " spawned on map " + g_currentMap + '.');
			return remote;
		}

		void HideAndDetachRetiredRemotePawn(AAlicePawn* pawn, bool park)
		{
			if (!pawn || !IsLiveUObject(pawn))
				return;
			auto hidePrimitive = [](UPrimitiveComponent* component)
			{
				if (!component)
					return;
				component->HiddenGame = true;
				NativeSetComponentHidden(component, true);
			};
			auto detach = [pawn](UActorComponent* component)
			{
				if (component && component->bAttached)
					NativeDetachActorComponent(pawn, component);
			};

			pawn->bHidden = true;
			NativeSetActorHidden(pawn, true);
			pawn->SetTickIsDisabled(true);
			pawn->SetCollision(false, false, true);
			hidePrimitive(pawn->Mesh);
			hidePrimitive(pawn->UpperBodyComponent);
			hidePrimitive(pawn->HairComponent);
			hidePrimitive(pawn->SkirtComponent);
			hidePrimitive(pawn->BowComponent);
			hidePrimitive(pawn->RibbonComponent);
			hidePrimitive(pawn->EarComponent);
			detach(pawn->HairComponent);
			detach(pawn->SkirtComponent);
			detach(pawn->BowComponent);
			detach(pawn->RibbonComponent);
			detach(pawn->EarComponent);
			detach(pawn->UpperBodyComponent);
			detach(pawn->Mesh);
			if (park)
			{
				const FVector parked(
					pawn->Location.X, pawn->Location.Y,
					pawn->Location.Z - 100000.0f);
				NativeSetActorLocationNoCheck(pawn, parked);
				pawn->Location = parked;
			}
		}

		void TickRetiredRemotePawns(AWorldInfo* world)
		{
			const Clock::time_point now = Clock::now();
			for (auto iterator = g_retiredRemotePawns.begin();
				iterator != g_retiredRemotePawns.end();)
			{
				RetiredRemotePawn& retired = *iterator;
				if (!retired.pawn || retired.world != world
					|| !IsLiveUObject(retired.pawn)
					|| retired.pawn->bDeleteMe)
				{
					iterator = g_retiredRemotePawns.erase(iterator);
					continue;
				}
				HideAndDetachRetiredRemotePawn(retired.pawn, false);
				if (now < retired.nextDestroyAttempt)
				{
					++iterator;
					continue;
				}
				retired.pawn->bNoDelete = false;
				retired.pawn->bStatic = false;
				++retired.attempts;
				const bool destroyed = NativeDestroyActor(retired.pawn);
				if (destroyed || retired.pawn->bDeleteMe)
				{
					Log("PEERLIFE retired Alice destroyed on retry "
						+ std::to_string(retired.attempts) + '.');
					iterator = g_retiredRemotePawns.erase(iterator);
					continue;
				}
				retired.nextDestroyAttempt =
					now + std::chrono::milliseconds(500);
				++iterator;
			}
		}

		bool IsRetiredRemotePawn(const AAlicePawn* pawn)
		{
			return pawn && std::any_of(g_retiredRemotePawns.begin(),
				g_retiredRemotePawns.end(),
				[pawn](const RetiredRemotePawn& retired)
				{
					return retired.pawn == pawn;
				});
		}

		void DestroyRemotePawn()
		{
			if (g_remoteWorld != g_currentWorld
				|| (g_remotePawn && !IsLiveUObject(g_remotePawn)))
			{
				Log("Visual Alice proxy references became stale; "
					"discarding them without UObject teardown.");
				ForgetTornDownWorldObjects();
				return;
			}
			// Make teardown visually atomic. NativeDestroyActor can be deferred or
			// refused for an archetype-cloned pawn, so detach its render tree before
			// asking UE3 to destroy it.
			RetireTrackedRemoteNativeGlideTrails(
				g_remoteWorld, nullptr, "proxy-destroy");
			if (g_remoteGlideParticle)
				HardStopPresentationParticle(g_remoteGlideParticle, true);
			g_remoteGlideParticle = nullptr;
			DestroyRemoteStaticLeafTrail();
			for (const TrackedPresentationParticle& tracked :
				g_remoteMovementTransientParticles)
			{
				if (IsLivePresentationParticle(tracked.component)
					&& tracked.component->Template == tracked.particleTemplate)
				{
					HardStopPresentationParticle(tracked.component, true);
				}
			}
			g_remoteMovementTransientParticles.clear();
			for (const PendingMovementParticlePreservation& pending :
				g_pendingMovementParticlePreservations)
			{
				if (IsLivePresentationParticle(pending.component)
					&& pending.component->Template == pending.particleTemplate)
				{
					HardStopPresentationParticle(pending.component, true);
				}
			}
			g_pendingMovementParticlePreservations.clear();
			g_remoteMovementParticleCapture = {};
			if (g_remotePawn)
			{
				HideAndDetachRetiredRemotePawn(g_remotePawn, false);
				if (g_remotePawn->DummyWeapon
					&& g_remotePawn->DummyWeapon->Mesh)
				{
					g_remotePawn->DummyWeapon->Mesh->HiddenGame = true;
					NativeSetComponentHidden(
						g_remotePawn->DummyWeapon->Mesh, true);
				}
			}
			DestroyRemotePresentationWeapon(true);
			DestroyRemoteHairProxy();
			if (g_remoteController && IsLiveUObject(g_remoteController)
				&& g_remoteWorld && g_remoteWorld == g_currentWorld)
			{
				g_remoteController->eventUnPossess();
				g_remoteController->bNoDelete = false;
				g_remoteController->bStatic = false;
				const bool controllerDestroyed =
					NativeDestroyActor(g_remoteController);
				if (!controllerDestroyed)
					Log("WARN: Remote controller destroy was deferred.");
			}
			if (g_remotePawn && IsLiveUObject(g_remotePawn)
				&& g_remoteWorld && g_remoteWorld == g_currentWorld)
			{
				g_remotePawn->bNoDelete = false;
				g_remotePawn->bStatic = false;
				const bool pawnDestroyed = NativeDestroyActor(g_remotePawn);
				if (!pawnDestroyed)
				{
					HideAndDetachRetiredRemotePawn(g_remotePawn, true);
					if (!IsRetiredRemotePawn(g_remotePawn))
					{
						g_retiredRemotePawns.push_back({
							g_remotePawn, g_remoteWorld,
							Clock::now() + std::chrono::milliseconds(250), 0 });
					}
					Log("WARN: Remote Alice destroy was deferred; render tree "
						"detached, pawn parked, retry scheduled.");
				}
			}
			if (g_remotePawn)
				Log("Visual Alice proxy removed.");
			g_remotePawn = nullptr;
			// The rotated UHair belongs to this cloned pawn. Keep the marker
			// while only its presentation proxy is rebuilt, but forget it once
			// the pawn itself is gone so a newly allocated template at the same
			// address is converted exactly once.
			g_remoteHairRotatedTemplate = nullptr;
			g_remoteHairTuningTemplate = nullptr;
			g_remoteHairBaseNodes.clear();
			g_remoteController = nullptr;
			g_remoteWorld = nullptr;
			ResetRemoteWeaponPresentationState();
			g_remoteDodgeVisualHidden = false;
			g_remoteGlideVfxActive = false;
			g_remoteAirResetPending = false;
			g_remoteGlideInactiveSince = {};
			if (g_remoteGlideParticle)
				RetirePersistentPresentationParticle(
					g_remoteGlideParticle);
			g_remoteGlideParticle = nullptr;
			g_remoteClockBombAnimationActive = false;
			g_devForceProxyHidden = false;
			g_devDodgeHideUntil = {};
			g_devMuzzleUntil = {};
			g_devGlideUntil = {};
			g_remoteDodgeParticleTemplate = nullptr;
			g_remoteDodgeParticleUntil = {};
			g_nextRemoteDodgeParticle = {};
			g_remoteFullBodyNotifyCursor = {};
			g_recentRemoteVisualNotifies.clear();
			g_lastRemoteVfx = "none";
			g_remoteBaseDrawScale = 1.0f;
			g_remoteShrinkApplied = false;
			g_remoteHidden = false;
			g_remoteRenderDetached = false;
			g_loggedPostTickPresentation = false;
			g_loggedPostEngineSkeletonRefresh = false;
			g_loggedPresentedCosmetics = false;
			g_loggedDelayedCosmetics = false;
			g_remoteHairOwnerFallbackApplied = false;
			g_remoteCosmeticDiagnosticAt = {};
			g_nextCosmeticSpatialSample = {};
			g_cosmeticSpatialSamplesRemaining = 0;
			g_cosmeticSpatialSampleNumber = 0;
			g_remotePresentation = {};
			g_remoteGraphSequences.clear();
			g_lastAppliedAnimationGraphFrame = 0;
			g_loggedRemoteAnimationGraph = false;
			g_lastRemoteAnimationGraphSignature.clear();
			g_remoteAppliedFullBodyNode = nullptr;
			g_remoteAppliedFullBodySlot = nullptr;
			g_remoteAppliedFullBodyName.clear();
			g_lastRemoteAnimationCompareSignature.clear();
			g_nextRemoteAnimationCompareSample = {};
			ResetPresentationAnimChannel(g_remoteFullBodyChannel);
			ResetPresentationAnimChannel(g_remoteUpperAdditiveChannel);
			g_loggedAnimationStageFailures.clear();
			g_loggedAnimationStageSuccesses.clear();
			g_loggedConfigAnimationStages.clear();
			g_animationStagePoseSamples.clear();
			g_loggedLocalMirrorBinding = false;
			g_loggedLocalMirrorIncompatibility = false;
			g_lastRemoteActionSerial = 0;
			g_lastRemoteSpecialMove = static_cast<std::uint8_t>(ESpecialMove::SM_None);
			g_loggedMissingSpecialMoves = false;
		}

		void TeardownDisconnectedPeerPresentation(const char* reason)
		{
			if (g_remotePawn || g_remotePresentationWeapon
				|| g_remoteHairProxy || g_remoteGlideParticle)
			{
				DestroyRemotePawn();
			}
			else
			{
				ResetRemoteWeaponPresentationState();
			}
			ClearRemoteProjectileVisuals(true);
			ResetSharedEnemyPose(true);
			if (g_remoteContextInteraction.active)
				ResetRemoteContextInteraction("peer-disconnected");
			g_activeRemoteAnimationGraph.reset();
			g_lastRemoteAnimationCompareSignature.clear();
			g_nextRemoteAnimationCompareSample = {};
			AchievementOverlay::SetPeerWatchingCutscene(false);
			AchievementOverlay::SetCoopWaitingForPeer(false);
			g_nextSpawnAttempt = Clock::now()
				+ std::chrono::milliseconds(300);
			Log(std::string("PEERLIFE presentation teardown completed, reason=")
				+ (reason ? reason : "unknown") + '.');
		}

