	void DrawTuningOverlay(UCanvas* canvas)
	{
		if (!g_config.enabled || g_soloMinigameActive
			|| !canvas || !canvas->Canvas.Dummy)
		{
			AchievementOverlay::SetPeerWatchingCutscene(false);
			return;
		}

		const bool peerWatchingCutscene =
			g_remotePresentation.valid
			&& (g_remotePresentation.state.flags
				& StateCinematic) != 0;
		AchievementOverlay::SetPeerWatchingCutscene(
			peerWatchingCutscene);

		if (!g_config.hairTuningEnabled
			|| !g_tuningOverlayVisible)
		{
			return;
		}
		if (!g_loggedTuningOverlayRender)
		{
			g_loggedTuningOverlayRender = true;
			Log("TUNINGUI Canvas render path is active.");
		}

		const float scale = std::clamp(
			static_cast<float>(canvas->SizeY) / 1080.0f,
			0.72f, 1.15f);
		const float originX = 18.0f * scale;
		const float originY = 18.0f * scale;
		const float panelWidth = 660.0f * scale;
		const float panelHeight = 112.0f * scale;
		if (canvas->DefaultTexture)
		{
			canvas->SetPos(originX, originY, 0.0f);
			canvas->SetDrawColor(5, 10, 18, 190);
			canvas->DrawRect(
				panelWidth, panelHeight,
				canvas->DefaultTexture);
		}

		FFontRenderInfo renderInfo{};
		renderInfo.bClipText = false;
		renderInfo.bEnableShadow = true;
		canvas->SetDrawColor(130, 220, 255, 255);
		float lineY = originY + 8.0f * scale;
		const auto drawLine = [&](const std::wstring& text)
		{
			canvas->SetPos(
				originX + 10.0f * scale, lineY, 0.0f);
			canvas->DrawTextWin(
				FString(text.c_str()), false,
				scale, scale, renderInfo);
			lineY += 22.0f * scale;
		};

		std::wostringstream target;
		target << L"AliceCoop Tuning | Hair target "
			<< (static_cast<int>(g_hairRotationCandidate) + 1)
			<< L"/"
			<< static_cast<int>(HairRotationCandidate::Count)
			<< L" "
			<< HairRotationCandidateWideName(
				g_hairRotationCandidate);
		drawLine(target.str());

		std::wostringstream rotation;
		rotation << std::fixed << std::setprecision(0)
			<< L"Rotation  X " << g_config.hairRotationX
			<< L"  Y " << g_config.hairRotationY
			<< L"  Z " << g_config.hairRotationZ
			<< L"    O/P  L/;  ,/.";
		drawLine(rotation.str());

		std::wostringstream position;
		position << std::fixed << std::setprecision(1)
			<< L"Position  X " << g_config.hairOffsetX
			<< L"  Y " << g_config.hairOffsetY
			<< L"  Z " << g_config.hairOffsetZ
			<< L"    U/I  J/K  N/M";
		drawLine(position.str());
		drawLine(L"[ next target    ] hide/show");
	}

	void OnAlicePawnTicked(AAlicePawn* pawn)
	{
		if (IsRetiredRemotePawn(pawn))
		{
			HideAndDetachRetiredRemotePawn(pawn, false);
			return;
		}
		if (!g_config.enabled || g_soloMinigameActive
			|| pawn != g_remotePawn || !g_remotePresentation.valid)
			return;

		// AlicePawn.Tick rewrites scale, component visibility and some kinematic
		// fields. Reapply after that script tick, while still before world render.
		ApplyRemotePresentation(pawn, g_remotePresentation, false);
		if (g_config.localMirror)
		{
			BindLocalMirrorPose(pawn, GetLocalPawn());
		}
		else
		{
			if (g_activeRemoteAnimationGraph)
			{
				ApplyRemoteAnimationGraph(pawn,
					g_activeRemoteAnimationGraph->graph, false);
			}
		}
		// AlicePawn.Tick and its animation update can restore visibility and
		// cosmetic transforms after the regular coop update. Make this the
			// final presentation barrier for dodge hiding, skirt/scalp bindings
			// and independent hair repair.
			ForceRemoteCosmeticMasterPose(pawn);
			if (!g_loggedPostTickPresentation)
		{
			g_loggedPostTickPresentation = true;
			Log("Remote presentation hook is active after AlicePawn.Tick.");
		}
	}

	void RecordLocalAction(PlayerAction action)
	{
		if (!g_config.enabled || g_soloMinigameActive
			|| action == PlayerAction::None)
			return;
		g_localAction = static_cast<std::uint8_t>(action);
		const std::uint32_t serial = ++g_localActionSerial;
		if (g_config.actionTrace)
		{
			Log("Local action serial=" + std::to_string(serial)
				+ ", action="
				+ std::to_string(static_cast<int>(action)) + '.');
			ExtendActionTraceWindow();
			Log("TRACE MARK #" + std::to_string(++g_actionTraceMarker)
				+ " action=" + PlayerActionName(action)
				+ " (" + std::to_string(static_cast<int>(action)) + ").");
		}
	}

	void RecordHostLoadChapter(std::uint8_t chapter)
	{
		if (!g_config.enabled || g_config.localMirror
			|| g_config.role != Role::Host || chapter >= 70)
		{
			return;
		}
		g_hostRequestedChapter = static_cast<int>(chapter);
		Log("SESSIONJOIN captured host LoadChapter="
			+ std::to_string(g_hostRequestedChapter)
			+ ", activeMap="
			+ (g_currentMap.empty()
				? std::string("<none>") : g_currentMap)
			+ '.');
	}

	void ObservePauseMenuState(bool open)
	{
		if (!g_config.enabled)
			return;
		const bool previous = g_pauseMenuEventOpen.exchange(
			open, std::memory_order_acq_rel);
		g_pauseMenuEventSerial.fetch_add(1, std::memory_order_acq_rel);
		if (previous != open)
			Log(std::string("UI pause menu event=")
				+ (open ? "open." : "closed."));
	}

	void OnLocalPepperProjectileSpawn(
		APepperGrinderPrimaryProjectile* projectile,
		const FVector& direction)
	{
		if (!g_config.enabled || g_soloMinigameActive
			|| g_config.localMirror || !projectile
			|| projectile->IsDefaultObject())
		{
			return;
		}
		AAlicePawn* localPawn = GetLocalPawn();
		const bool ownedByLocal = localPawn
			&& (projectile->Instigator == localPawn
				|| (projectile->WeaponOwner
					&& projectile->WeaponOwner->Instigator
						== localPawn));
		if (!ownedByLocal)
			return;
		if (!PepperProjectileReplicationEnabled)
		{
			static bool loggedDisabled = false;
			if (!loggedDisabled)
			{
				loggedDisabled = true;
				Log("PROJECTILESTAGE pepper projectile replication "
					"disabled; remote muzzle presentation only.");
			}
			return;
		}

		const Clock::time_point now = Clock::now();
		for (auto iterator =
				g_recentLocalPepperProjectiles.begin();
			iterator != g_recentLocalPepperProjectiles.end();)
		{
			if (now - iterator->second > std::chrono::seconds(10))
				iterator =
					g_recentLocalPepperProjectiles.erase(iterator);
			else
				++iterator;
		}
		if (g_recentLocalPepperProjectiles.contains(projectile))
			return;
		g_recentLocalPepperProjectiles.emplace(projectile, now);

		FVector resolvedDirection = direction;
		auto normalizeDirection = [](FVector& value)
		{
			const float lengthSquared =
				value.X * value.X + value.Y * value.Y
				+ value.Z * value.Z;
			if (lengthSquared < 0.001f)
				return false;
			const float inverseLength =
				1.0f / std::sqrt(lengthSquared);
			value.X *= inverseLength;
			value.Y *= inverseLength;
			value.Z *= inverseLength;
			return true;
		};
		if (!normalizeDirection(resolvedDirection))
		{
			resolvedDirection = projectile->FlightFront;
			normalizeDirection(resolvedDirection);
		}
		if (!normalizeDirection(resolvedDirection))
		{
			resolvedDirection = projectile->Velocity;
			normalizeDirection(resolvedDirection);
		}
		if (!normalizeDirection(resolvedDirection))
		{
			constexpr float RotatorToRadians =
				6.28318530717958647692f / 65536.0f;
			const float pitch =
				static_cast<float>(projectile->Rotation.Pitch)
				* RotatorToRadians;
			const float yaw =
				static_cast<float>(projectile->Rotation.Yaw)
				* RotatorToRadians;
			const float cosPitch = std::cos(pitch);
			resolvedDirection = FVector(
				cosPitch * std::cos(yaw),
				cosPitch * std::sin(yaw),
				std::sin(pitch));
		}

		if (projectile->ProjFlightEffectTemplate)
			g_pepperFlightTemplate =
				projectile->ProjFlightEffectTemplate;
		else if (projectile->ProjFlightEffects
			&& projectile->ProjFlightEffects->Template)
		{
			g_pepperFlightTemplate =
				projectile->ProjFlightEffects->Template;
		}
		else if (projectile->ProjEffects
			&& projectile->ProjEffects->Template)
		{
			g_pepperFlightTemplate =
				projectile->ProjEffects->Template;
		}

		ProjectileEventPayload event{};
		event.kind = ProjectileEventKind::PepperSpawn;
		event.projectileId = g_localProjectileId++;
		if (event.projectileId == 0)
			event.projectileId = g_localProjectileId++;
		event.variant = static_cast<std::uint8_t>(
			std::clamp(projectile->WeaponLevel, 0, 255));
		UParticleSystemComponent* sourceParticle =
			projectile->ProjFlightEffects
				? projectile->ProjFlightEffects
				: projectile->ProjEffects;
		float particleScale = projectile->DrawScale;
		if (sourceParticle)
		{
			float componentScale3D =
				std::fabs(sourceParticle->Scale3D.X);
			componentScale3D = (std::max)(
				componentScale3D,
				std::fabs(sourceParticle->Scale3D.Y));
			componentScale3D = (std::max)(
				componentScale3D,
				std::fabs(sourceParticle->Scale3D.Z));
			componentScale3D = (std::max)(
				componentScale3D, 0.001f);
			particleScale *= sourceParticle->Scale
				* componentScale3D;
		}
		event.extra = particleScale;
		FillProjectileTransform(event, projectile);
		const float velocitySquared =
			event.velocity[0] * event.velocity[0]
			+ event.velocity[1] * event.velocity[1]
			+ event.velocity[2] * event.velocity[2];
		if (velocitySquared < 1.0f)
		{
			float fallbackSpeed = projectile->Speed;
			if (fallbackSpeed < 1.0f)
				fallbackSpeed = projectile->MaxSpeed;
			if (fallbackSpeed < 1.0f)
				fallbackSpeed = projectile->RefVel;
			if (fallbackSpeed < 1.0f)
				fallbackSpeed = 12000.0f;
			event.velocity[0] =
				resolvedDirection.X * fallbackSpeed;
			event.velocity[1] =
				resolvedDirection.Y * fallbackSpeed;
			event.velocity[2] =
				resolvedDirection.Z * fallbackSpeed;
		}
		QueueProjectileEvent(event);

		static std::uint32_t pepperLogCount = 0;
		++pepperLogCount;
		if (pepperLogCount <= 3 || pepperLogCount % 100 == 0)
		{
			Log("PROJECTILESTAGE local pepper projectile #"
				+ std::to_string(pepperLogCount)
				+ ", id=" + std::to_string(event.projectileId)
				+ ", speed=" + std::to_string(projectile->Speed)
				+ ", maxSpeed="
				+ std::to_string(projectile->MaxSpeed)
				+ ", refVel="
				+ std::to_string(projectile->RefVel)
				+ ", drawScale="
				+ std::to_string(projectile->DrawScale)
				+ ", particleScale="
				+ std::to_string(sourceParticle
					? sourceParticle->Scale : 0.0f)
				+ ", particleScale3D=("
				+ std::to_string(sourceParticle
					? sourceParticle->Scale3D.X : 0.0f)
				+ ','
				+ std::to_string(sourceParticle
					? sourceParticle->Scale3D.Y : 0.0f)
				+ ','
				+ std::to_string(sourceParticle
					? sourceParticle->Scale3D.Z : 0.0f)
				+ "), netScale="
				+ std::to_string(event.extra)
				+ ", direction=("
				+ std::to_string(resolvedDirection.X) + ','
				+ std::to_string(resolvedDirection.Y) + ','
				+ std::to_string(resolvedDirection.Z) + ')'
				+ ", velocity=("
				+ std::to_string(event.velocity[0]) + ','
				+ std::to_string(event.velocity[1]) + ','
				+ std::to_string(event.velocity[2])
				+ "), effect="
				+ ObjectName(g_pepperFlightTemplate) + '.');
		}
	}

	void OnLocalClockBombSpawn(AAliceClonePawn* bomb)
	{
		AAlicePawn* localPawn = GetLocalPawn();
		if (!g_config.enabled || g_soloMinigameActive
			|| g_config.localMirror || !bomb
			|| bomb->IsDefaultObject()
			|| !localPawn
			|| (bomb->MyAlicePawn != localPawn
				&& localPawn->MyClonePawn != bomb))
		{
			return;
		}
		if (g_localClockBomb.bomb == bomb
			&& g_localClockBomb.projectileId != 0)
		{
			return;
		}

		if (bomb->ExplosionParticle)
			g_clockBombExplosionTemplate =
				bomb->ExplosionParticle;
		g_localClockBomb = {};
		g_localClockBomb.bomb = bomb;
		g_localClockBomb.lastLocation = bomb->Location;
		g_localClockBomb.lastVelocity = bomb->Velocity;
		g_localClockBomb.lastRotation = bomb->Rotation;
		g_localClockBomb.cloneState =
			static_cast<std::uint8_t>(bomb->CloneState);
		g_localClockBomb.projectileId =
			g_localProjectileId++;
		if (g_localClockBomb.projectileId == 0)
			g_localClockBomb.projectileId =
				g_localProjectileId++;
		g_localClockBomb.nextUpdate =
			Clock::now() + std::chrono::milliseconds(50);

		ProjectileEventPayload event{};
		event.kind = ProjectileEventKind::ClockBombSpawn;
		event.projectileId =
			g_localClockBomb.projectileId;
		event.variant =
			static_cast<std::uint8_t>(bomb->CloneState);
		event.extra = bomb->CountdownTime;
		FillProjectileTransform(event, bomb);
		QueueProjectileEvent(event);
		Log("PROJECTILESTAGE local clock bomb spawned id="
			+ std::to_string(event.projectileId)
			+ ", mesh=" + ObjectName(
				bomb->Mesh ? bomb->Mesh->SkeletalMesh : nullptr)
			+ ", explosion="
			+ ObjectName(bomb->ExplosionParticle) + '.');
	}

	void OnLocalClockBombDetonate(AAliceClonePawn* bomb)
	{
		AAlicePawn* localPawn = GetLocalPawn();
		if (!g_config.enabled || g_soloMinigameActive
			|| g_config.localMirror || !bomb
			|| !localPawn
			|| (bomb->MyAlicePawn != localPawn
				&& localPawn->MyClonePawn != bomb
				&& g_localClockBomb.bomb != bomb))
		{
			return;
		}
		if (g_localClockBomb.bomb != bomb
			|| g_localClockBomb.projectileId == 0)
		{
			OnLocalClockBombSpawn(bomb);
		}
		if (g_localClockBomb.bomb != bomb
			|| g_localClockBomb.projectileId == 0
			|| g_localClockBomb.exploded)
		{
			return;
		}
		g_localClockBomb.exploded = true;
		ProjectileEventPayload event{};
		event.kind = ProjectileEventKind::ClockBombExplode;
		event.projectileId =
			g_localClockBomb.projectileId;
		event.variant =
			static_cast<std::uint8_t>(bomb->CloneState);
		FillProjectileTransform(event, bomb);
		QueueProjectileEvent(event);
		Log("PROJECTILESTAGE local clock bomb detonated id="
			+ std::to_string(event.projectileId) + '.');
	}

	void OnLocalClockBombDestroyed(AAliceClonePawn* bomb)
	{
		if (g_soloMinigameActive
			|| !bomb || g_localClockBomb.bomb != bomb)
			return;
		if (!g_localClockBomb.exploded
			&& g_localClockBomb.projectileId != 0)
		{
			if (bomb->CloneState
				== EClonnPawnState::
					e_ClonePawnState_Destory)
			{
				OnLocalClockBombDetonate(bomb);
			}
			else
			{
				ProjectileEventPayload event{};
				event.kind =
					ProjectileEventKind::ClockBombRemove;
				event.projectileId =
					g_localClockBomb.projectileId;
				event.variant =
					static_cast<std::uint8_t>(bomb->CloneState);
				FillProjectileTransform(event, bomb);
				QueueProjectileEvent(event);
			}
		}
		g_localClockBomb = {};
	}

	void TraceLifecycleProcessEvent(UObject* object,
		UFunction* function, const void* parameters, bool after)
	{
		if (!g_config.enabled || g_soloMinigameActive
			|| !function)
			return;
		const std::string functionName = function->GetName();
		const bool localController = object
			&& g_State.AlicePlayerController
			&& object == g_State.AlicePlayerController;
		if (!after && functionName == "Restart"
			&& object
			&& object->IsA(
				UAliceGfxMovie_inGameMenu::StaticClass()))
		{
			// The old world is still valid here. Retire pooled presentation
			// effects before the checkpoint teardown invalidates their owners;
			// the previous code merely forgot these pointers after the load.
			CleanupRemoteWeaponTransients(true);
			StopRemoteWeaponLoopParticles();
			if (g_remoteGlideParticle)
				HardStopPresentationParticle(
					g_remoteGlideParticle, true);
			g_remoteGlideParticle = nullptr;
			DestroyRemoteStaticLeafTrail();
			if (g_remoteAttackTrailParticle)
				HardStopPresentationParticle(
					g_remoteAttackTrailParticle, true);
			g_remoteAttackTrailParticle = nullptr;
			if (g_remoteMuzzleParticle)
				HardStopPresentationParticle(
					g_remoteMuzzleParticle, true);
			g_remoteMuzzleParticle = nullptr;
			Log("VFXSTAGE retired proxy effects before checkpoint restart.");
		}
		if (!after && functionName == "UsedBy"
			&& parameters && object
			&& object->IsA(ATrigger::StaticClass()))
		{
			const auto* value = reinterpret_cast<const
				AActor_execUsedBy_Params*>(parameters);
			const bool localUser =
				value->User && value->User == GetLocalPawn();
			Log("TRIGGERINTERACT UsedBy trigger="
				+ ObjectName(object)
				+ ", user=" + ObjectName(value->User)
				+ ", localUser="
				+ (localUser ? std::string("yes")
					: std::string("no"))
				+ ", applyingNetwork="
				+ (g_applyingSharedTriggerInteraction
					? std::string("yes.")
					: std::string("no.")));
			if (!g_config.localMirror
				&& localUser
				&& !g_applyingSharedTriggerInteraction)
			{
				QueueTriggerInteraction(
					reinterpret_cast<ATrigger*>(object),
					"Actor.UsedBy");
			}
		}
		if (localController && !after
			&& functionName == "Use"
			&& !g_applyingSharedInteraction)
		{
			++g_localInteractionAttemptSerial;
			g_localInteractionKeysThisAttempt.clear();
			CaptureContextActorUseSnapshot();
			g_localInteractionWindowUntil =
				Clock::now() + std::chrono::seconds(10);
			Log("SHAREDINTERACT local Use attempt="
				+ std::to_string(g_localInteractionAttemptSerial)
				+ ", map="
				+ (g_currentMap.empty()
					? std::string("<none>") : g_currentMap)
				+ '.');
		}
		if (localController && after
			&& functionName == "Use"
			&& !g_applyingSharedInteraction)
		{
			DetectStartedContextActor(false);
		}
		if (localController && functionName == "notifyInputKey")
		{
			if (after || !parameters)
				return;
			const auto* value = reinterpret_cast<const
				AAlicePlayerController_execnotifyInputKey_Params*>(
					parameters);
			const std::string key = value->Key.ToString();
			if (g_config.controlLifecycleTrace
				&& (_stricmp(key.c_str(), "SpaceBar") == 0
					|| _stricmp(key.c_str(), "Space") == 0
					|| _stricmp(key.c_str(), "Jump") == 0))
			{
				BeginControlLifecycleTrace(key.c_str(), value->Event);
			}
			if (value->Event
					== static_cast<std::uint8_t>(
						EInputEvent::IE_Pressed)
				&& _stricmp(key.c_str(), "P") == 0)
			{
				g_teleportInputRequested.store(
					true, std::memory_order_release);
				Log("TELEPORT P input received through "
					"PlayerController.notifyInputKey.");
				return;
			}
			if (value->Event
					== static_cast<std::uint8_t>(
						EInputEvent::IE_Pressed)
				&& _stricmp(key.c_str(), "O") == 0)
			{
				g_forceTeleportInputRequested.store(
					true, std::memory_order_release);
				Log("TELEPORT O input received through "
					"PlayerController.notifyInputKey.");
				return;
			}
			if (value->Event
					!= static_cast<std::uint8_t>(
						EInputEvent::IE_Pressed)
				|| _stricmp(key.c_str(), "C") != 0)
				return;
			++g_localInteractionAttemptSerial;
			g_localInteractionKeysThisAttempt.clear();
			g_localInteractionWindowUntil =
				Clock::now() + std::chrono::seconds(10);
			Log("SHAREDINTERACT local C press attempt="
				+ std::to_string(
					g_localInteractionAttemptSerial) + '.');
		}
		if (localController && after
			&& functionName == "TriggerInteracted" && parameters)
		{
			const auto* value = reinterpret_cast<const
				APlayerController_execTriggerInteracted_Params*>(
					parameters);
			if (value->ReturnValue)
			{
				g_localInteractionWindowUntil =
					Clock::now() + std::chrono::seconds(10);
			}
			Log("SHAREDINTERACT TriggerInteracted result="
				+ std::string(value->ReturnValue ? "yes." : "no."));
		}
		if (localController && !after
			&& functionName == "OnInteractInLondon"
			&& parameters)
		{
			const auto* value = reinterpret_cast<const
				AAlicePlayerController_execOnInteractInLondon_Params*>(
					parameters);
			USeqAct_InteractInLondon* action = value->inAction;
			Log("LONDONINTERACT OnInteractInLondon action="
				+ ObjectName(action)
				+ ", key="
				+ SharedWorldKeyText(
					SequenceActionStableKey(action))
				+ ", applyingNetwork="
				+ (g_applyingSharedLondonInteraction
					? std::string("yes")
					: std::string("no"))
				+ ", active="
				+ (action && action->bActive
					? std::string("yes")
					: std::string("no"))
				+ ", inputs="
				+ std::to_string(
					action ? action->InputLinks.size() : 0)
				+ ", outputs="
				+ std::to_string(
					action ? action->OutputLinks.size() : 0)
				+ '.');
		}
		if (localController && !after
			&& functionName == "interactInLondonX")
		{
			auto* controller = reinterpret_cast<
				AAlicePlayerController*>(object);
			USeqAct_InteractInLondon* action =
				controller ? controller->InteractLondonActor : nullptr;
			const std::uint64_t key =
				SequenceActionStableKey(action);
			Log("LONDONINTERACT interactInLondonX action="
				+ ObjectName(action)
				+ ", key=" + SharedWorldKeyText(key)
				+ ", applyingNetwork="
				+ (g_applyingSharedLondonInteraction
					? std::string("yes")
					: std::string("no"))
				+ ", active="
				+ (action && action->bActive
					? std::string("yes")
					: std::string("no"))
				+ '.');
			if (!g_config.localMirror
				&& !g_applyingSharedLondonInteraction
				&& key != 0
				&& g_localInteractionKeysThisAttempt.insert(
					key).second)
			{
				SharedWorldEventPayload event{};
				event.kind =
					SharedWorldEventKind::InteractionInLondon;
				event.entityKey = key;
				event.originActorId =
					g_config.role == Role::Host ? 1u : 2u;
				QueueSharedWorldEvent(event);
				Log("LONDONINTERACT TX action="
					+ ObjectName(action)
					+ ", key=" + SharedWorldKeyText(key) + '.');
			}
		}
		if (localController && !after
			&& functionName == "exitInteractState")
		{
			auto* controller = reinterpret_cast<
				AAlicePlayerController*>(object);
			Log("LONDONINTERACT exitInteractState action="
				+ ObjectName(controller
					? controller->InteractLondonActor : nullptr)
				+ ", applyingNetwork="
				+ (g_applyingSharedLondonInteraction
					? std::string("yes.")
					: std::string("no.")));
		}
		if (localController && !after
			&& functionName == "ClientTravel" && parameters)
		{
			const auto* value = reinterpret_cast<const
				APlayerController_eventClientTravel_Params*>(
					parameters);
			Log("SESSION ClientTravel trace URL="
				+ value->URL.ToString()
				+ ", type="
				+ std::to_string(value->TravelType)
				+ ", seamless="
				+ (value->bSeamless
					? std::string("yes.") : std::string("no.")));
		}
		if (!g_config.localMirror && !after
			&& functionName == "backtoTitle"
			&& localController
			&& !g_applyingNetworkReturnToMenu
			&& !g_localReturnToMenuInitiated)
		{
			g_localReturnToMenuInitiated = true;
			if (g_config.role == Role::Host)
				RequestHostReturnToMenu("host backtoTitle");
			else if (g_config.role == Role::Client)
				RequestClientReturnToMenu("client backtoTitle");
		}
		if (!g_config.localMirror && !after
			&& functionName == "QuitGame"
			&& object
			&& object->IsA(
				UAliceGfxMovie_inGameMenu::StaticClass())
			&& !g_applyingNetworkReturnToMenu
			&& !g_localReturnToMenuInitiated)
		{
			g_localReturnToMenuInitiated = true;
			if (g_config.role == Role::Host)
				RequestHostReturnToMenu("host menu");
			else if (g_config.role == Role::Client)
				RequestClientReturnToMenu("client menu");
		}
		if (!g_config.localMirror && !after
			&& functionName == "SetInMainMenu"
			&& parameters && !g_applyingNetworkReturnToMenu
			&& !g_localReturnToMenuInitiated
			&& !g_currentMap.empty()
			&& _stricmp(g_currentMap.c_str(), "AliceEntry") != 0)
		{
			const auto* value = reinterpret_cast<const
				AAlicePlayerController_execSetInMainMenu_Params*>(
					parameters);
			if (value->DesiredInMainMenu)
			{
				g_localReturnToMenuInitiated = true;
				if (g_config.role == Role::Host)
					RequestHostReturnToMenu(
						"host SetInMainMenu");
				else if (g_config.role == Role::Client)
					RequestClientReturnToMenu(
						"client SetInMainMenu");
			}
		}
		if (!g_config.localMirror && !after
			&& functionName == "deathRestartGame"
			&& !g_applyingNetworkRestart)
		{
			g_localRestartInitiated = true;
			if (g_config.role == Role::Host)
				RequestHostCheckpointRestart("host Continue");
			else if (g_config.role == Role::Client)
				RequestClientCheckpointRestart("client Continue");
		}
		if (!g_config.localMirror && !after
			&& functionName == "OnSetVentState"
			&& parameters && !g_applyingSharedVentState)
		{
			const auto* value = reinterpret_cast<const
				AAlicePlayerController_execOnSetVentState_Params*>(
					parameters);
			g_pendingLocalVentActionKey =
				SequenceActionStableKey(value->inAction);
			g_pendingLocalVentInputIndex = 0;
			if (value->inAction)
			{
				for (int32_t index = 0;
					index < value->inAction->InputLinks.size();
					++index)
				{
					const FSeqOpInputLink& input =
						value->inAction->InputLinks.at(index);
					if (input.bHasImpulse
						|| input.QueuedActivations > 0)
					{
						g_pendingLocalVentInputIndex =
							static_cast<std::uint8_t>(index);
						break;
					}
				}
				const std::string inputName =
					value->inAction->InputLinks.empty()
						? std::string("<none>")
						: value->inAction->InputLinks.at(
							std::min<int32_t>(
								g_pendingLocalVentInputIndex,
								value->inAction->InputLinks.size()
									- 1)).LinkDesc.ToString();
				Log("SHAREDWORLD captured local vent input="
					+ std::to_string(
						g_pendingLocalVentInputIndex)
					+ ", name=" + inputName
					+ ", key="
					+ SharedWorldKeyText(
						g_pendingLocalVentActionKey) + '.');
			}
		}
		if (!g_config.localMirror && after
			&& functionName == "OnSetVentState"
			&& parameters && !g_applyingSharedVentState)
		{
			const auto* value = reinterpret_cast<const
				AAlicePlayerController_execOnSetVentState_Params*>(
					parameters);
			const std::uint64_t key =
				SequenceActionStableKey(value->inAction);
			if (key != 0 && g_remoteContextInteraction.active)
			{
				g_remoteContextInteraction.localVentApplied = true;
				ReleaseRemoteContextActorPresentation(
					"local-vent-completed");
				Log("SHAREDWORLD suppressed replicated ContextActor "
					"vent echo key=" + SharedWorldKeyText(key)
					+ ", deferredHostState="
					+ (g_remoteContextInteraction.deferredVent
						.has_value()
						? std::string("yes.")
						: std::string("no.")));
				if (g_remoteContextInteraction.deferredVent.has_value())
				{
					g_remoteContextInteraction = {};
				}
				else
				{
					g_remoteContextInteraction.deadline =
						Clock::now() + std::chrono::seconds(5);
				}
			}
			else if (key != 0
				&& !(key == g_suppressedVentActionKey
					&& Clock::now()
						< g_suppressedVentActionUntil))
			{
				const std::uint8_t input =
					key == g_pendingLocalVentActionKey
						? g_pendingLocalVentInputIndex
						: 0;
				Log("SHAREDWORLD observed vent completion key="
					+ SharedWorldKeyText(key)
					+ ", input="
					+ std::to_string(input)
					+ ", action="
					+ ObjectName(value->inAction) + '.');
				SharedWorldEventPayload event{};
				event.kind =
					SharedWorldEventKind::VentStateApplied;
				event.entityKey = key;
				event.originActorId =
					g_config.role == Role::Host ? 1u : 2u;
				event.flags = input;
				QueueSharedWorldEvent(event);
				Log("SHAREDWORLD TX vent fallback key="
					+ SharedWorldKeyText(key)
					+ ", input=" + std::to_string(input) + '.');
			}
			else if (key != 0)
			{
				Log("SHAREDWORLD suppressed echoed vent state key="
					+ SharedWorldKeyText(key) + '.');
			}
		}
		std::ostringstream stream;
		stream << "COOPLIFECYCLE phase=" << (after ? "post" : "pre")
			<< ", function=" << functionName
			<< ", map="
			<< (g_currentMap.empty() ? "<none>" : g_currentMap);

		// A restart/load call may invalidate its own receiver before it returns,
		// so only inspect the object and parameters on the pre-call side.
		if (!after)
		{
			stream << ", object=" << ObjectName(object);
			if (functionName == "OnSetVentState" && parameters)
			{
				const auto* value = reinterpret_cast<const
					AAlicePlayerController_execOnSetVentState_Params*>(
						parameters);
				stream << ", action=" << ObjectName(value->inAction);
			}
			else if (functionName == "SetCinematicMode" && parameters)
			{
				const auto* value = reinterpret_cast<const
					AAlicePlayerController_execSetCinematicMode_Params*>(
						parameters);
				stream << ", enabled="
					<< (value->bInCinematicMode ? "yes" : "no")
					<< ", hidePlayer="
					<< (value->bHidePlayer ? "yes" : "no")
					<< ", blocksMovement="
					<< (value->bAffectsMovement ? "yes" : "no");
			}
		}
		stream << '.';
		const bool importantLifecycle =
			ContainsCaseInsensitive(functionName, "restart")
			|| ContainsCaseInsensitive(functionName, "loadchapter")
			|| functionName == "backtoTitle"
			|| functionName == "QuitGame"
			|| functionName == "SetInMainMenu";
		if (g_config.actionTrace || importantLifecycle)
			Log(stream.str());
	}

		bool InterpHasDirectorTrack(const USeqAct_Interp* action)
		{
			if (!action || !action->InterpData
				|| !IsLiveUObject(action->InterpData))
			{
				return false;
			}
			for (UInterpGroup* group :
				action->InterpData->InterpGroups)
			{
				if (!group || !IsLiveUObject(group))
					continue;
				if (group->IsA(
						UInterpGroupDirector::StaticClass()))
				{
					return true;
				}
				for (UInterpTrack* track : group->InterpTracks)
				{
					if (track && IsLiveUObject(track)
						&& track->IsA(
							UInterpTrackDirector::StaticClass()))
					{
						return true;
					}
				}
			}
			return false;
		}

		USeqAct_Interp* FindCutsceneAction(
			std::uint64_t entityKey)
		{
			if (entityKey == 0)
				return nullptr;
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return nullptr;
			for (int32_t index = 0;
				index < objects->size(); ++index)
			{
				UObject* object = objects->at(index);
				if (!object
					|| !object->IsA(
						USeqAct_Interp::StaticClass())
					|| !IsLiveUObject(object)
					|| SequenceActionStableKey(object)
						!= entityKey)
				{
					continue;
				}
				return reinterpret_cast<USeqAct_Interp*>(object);
			}
			return nullptr;
		}

		bool IsSpawnerStyleCinematic(const USeqAct_Interp* action)
		{
			if (!action || action->InputLinks.size() != 5
				|| action->OutputLinks.size() != 4)
			{
				return false;
			}
			const std::string name = ObjectName(action);
			return name.find(".Prefabs.") != std::string::npos
				&& (name.find("_Spawner_Seq_")
						!= std::string::npos
					|| name.find("_Spawner.")
						!= std::string::npos);
		}

		bool TryStartEarlyCutsceneBarrier(USeqAct_Interp* action)
		{
			if (!action || g_config.localMirror
				|| g_waitingCutsceneAction)
			{
				return false;
			}
			const auto now = Clock::now();
			const bool interactionMatinee =
				g_remoteContextInteraction.active
				|| now < g_localInteractionWindowUntil
				|| now < g_interactionCutsceneBypassUntil;
			const bool peerAvailable =
				g_remotePresentation.valid
				&& !g_currentMap.empty()
				&& _stricmp(
					g_remotePresentation.state.mapName,
					g_currentMap.c_str()) == 0;
			const std::uint64_t key =
				SequenceActionStableKey(action);
			if (!peerAvailable || interactionMatinee || key == 0
				|| (!InterpHasDirectorTrack(action)
					&& !IsSpawnerStyleCinematic(action))
				|| (key == g_recentReleasedCutsceneKey
					&& now < g_recentReleasedCutsceneUntil))
			{
				return false;
			}

			g_waitingCutsceneAction = action;
			g_waitingCutsceneKey = key;
			g_cutsceneBarrierAdvertiseKey = key;
			g_cutsceneBarrierStartedAt = now;
			g_cutsceneBarrierAdvertiseUntil = {};
			g_waitingCutsceneOriginalPlayRate = action->PlayRate;
			g_waitingCutscenePlayRateOverridden = true;
			g_waitingCutsceneCanDeferActivation = true;
			g_waitingCutsceneActivationDeferred = false;
			action->PlayRate = 0.0f;
			action->bPaused = true;
			AchievementOverlay::SetCoopWaitingForPeer(true);
			Log("CUTSCENEBARRIER armed before activation key="
				+ SharedWorldKeyText(key)
				+ ", action=" + ObjectName(action)
				+ ", originalPlayRate="
				+ std::to_string(
					g_waitingCutsceneOriginalPlayRate) + '.');
			return true;
		}

	bool ShouldDeferSequenceOpActivation(UObject* object)
	{
		if (!g_config.enabled || g_soloMinigameActive
			|| g_config.localMirror || !object
			|| g_replayingDeferredCutsceneActivation
			|| !g_waitingCutsceneCanDeferActivation
			|| g_waitingCutsceneActivationDeferred
			|| object != g_waitingCutsceneAction)
		{
			return false;
		}
		g_waitingCutsceneActivationDeferred = true;
		Log("CUTSCENEBARRIER deferred Activated() key="
			+ SharedWorldKeyText(g_waitingCutsceneKey)
			+ ", action="
			+ ObjectName(g_waitingCutsceneAction) + '.');
		return true;
	}

	void TraceSequenceOpProcessEvent(UObject* object,
		bool activated, bool after)
	{
		if (!g_config.enabled || !object)
			return;
		std::string soloLevel;
		if (DetectSoloMinigameObject(object, soloLevel))
		{
			SetSoloMinigameActive(true, soloLevel);
			return;
		}
		if (g_soloMinigameActive
			|| !object->IsA(USequenceOp::StaticClass())
			|| !activated)
		{
			return;
		}
		auto* op = reinterpret_cast<USequenceOp*>(object);
		if (op->IsA(USeqAct_Interp::StaticClass()))
		{
			auto* interp = reinterpret_cast<USeqAct_Interp*>(op);
			if (!after)
			{
				TryStartEarlyCutsceneBarrier(interp);
			}
			else if (g_waitingCutsceneAction == interp)
			{
				// Activated() clears these fields on some Matinees. Reapply
				// the zero-frame barrier before their first world tick.
				if (g_waitingCutscenePlayRateOverridden)
					interp->PlayRate = 0.0f;
				interp->bPaused = true;
			}
		}
		if (after)
			return;

		bool replicatedContextEvent = false;
		if (!g_config.localMirror
			&& op->IsA(
				USeqEvent_ContextActionActivated::StaticClass()))
		{
			auto* contextEvent = reinterpret_cast<
				USeqEvent_ContextActionActivated*>(op);
			replicatedContextEvent =
				g_remoteContextInteraction.active
				&& contextEvent->Originator
					== g_remoteContextInteraction.actor
				&& contextEvent->Instigator == GetLocalPawn();
			if (replicatedContextEvent)
			{
				g_remoteContextInteraction.localContextActivated = true;
				Log("CONTEXTACTOR local completion event reached key="
					+ SharedWorldKeyText(
						SequenceActionStableKey(contextEvent))
					+ ", actorKey="
					+ SharedWorldKeyText(
						g_remoteContextInteraction.actorKey)
					+ ", event=" + ObjectName(contextEvent) + '.');
			}
		}
		if (activated && !after
			&& !g_config.localMirror
			&& !g_applyingSharedContextAction
			&& !replicatedContextEvent
			&& op->IsA(
				USeqEvent_ContextActionActivated::StaticClass()))
		{
			auto* contextEvent = reinterpret_cast<
				USeqEvent_ContextActionActivated*>(op);
			if (contextEvent->Instigator == GetLocalPawn())
			{
				const std::uint64_t key =
					SequenceActionStableKey(contextEvent);
				if (key != 0
					&& g_localInteractionKeysThisAttempt.insert(
						key).second)
				{
					// Context interactions (valves, levers and memory
					// entrances) often chain character and camera Matinees.
					// The first Matinee consumes the short Use window, while
					// later stages must still bypass the generic two-player
					// trigger barrier because the interaction itself is
					// already replicated to both peers.
					g_interactionCutsceneBypassUntil =
						Clock::now() + std::chrono::seconds(30);
					SharedWorldEventPayload event{};
					event.kind = SharedWorldEventKind::
						InteractionContextActionActivated;
					event.entityKey = key;
					event.originActorId =
						g_config.role == Role::Host ? 1u : 2u;
					QueueSharedWorldEvent(event);
					Log("CONTEXTINTERACT TX key="
						+ SharedWorldKeyText(key)
						+ ", event=" + ObjectName(contextEvent)
						+ ", originator="
						+ ObjectName(contextEvent->Originator)
						+ ", instigator="
						+ ObjectName(contextEvent->Instigator)
						+ ", triggers="
						+ std::to_string(
							contextEvent->TriggerCount)
						+ ", enabled="
						+ (contextEvent->bEnabled
							? std::string("yes.")
							: std::string("no.")));
				}
			}
		}
		if (!g_pendingSequenceOpUseTrace
			|| Clock::now() >= g_sequenceOpUseTraceUntil)
		{
			return;
		}
		if (!g_sequenceOpUseLogged.insert(op).second)
			return;
		++g_sequenceOpUseTraceCount;
		std::string handler = "<none>";
		if (op->IsA(USequenceAction::StaticClass()))
		{
			handler = reinterpret_cast<USequenceAction*>(op)
				->HandlerName.ToString();
		}
		Log("SEQUENCEUSE event #"
			+ std::to_string(g_sequenceOpUseTraceCount)
			+ ", phase=" + (after ? std::string("post")
				: std::string("pre"))
			+ ", event=" + (activated
				? std::string("Activated")
				: std::string("Deactivated"))
			+ ", key="
			+ SharedWorldKeyText(
				SequenceActionStableKey(op))
			+ ", op=" + ObjectName(op)
			+ ", count=" + std::to_string(op->ActivateCount)
			+ ", active="
			+ (op->bActive ? std::string("yes")
				: std::string("no"))
			+ ", inputs=" + SequenceOpInputState(op)
			+ ", handler=" + handler
			+ ", parent=" + ObjectName(op->ParentSequence)
			+ '.');
	}

	void HandleSharedInteractionProcessEvent(UObject* object,
		const void* parameters, bool after)
	{
		if (!g_config.enabled || g_soloMinigameActive
			|| g_config.localMirror
			|| !after || !object || !parameters
			|| g_applyingSharedInteraction
			|| !object->IsA(USeqEvent_Used::StaticClass()))
		{
			return;
		}
		const auto* value = reinterpret_cast<const
			USequenceEvent_execCheckActivate_Params*>(parameters);
		AAlicePawn* localPawn = GetLocalPawn();
		if (!value->ReturnValue || value->bTest || !localPawn)
		{
			return;
		}
		const std::uint64_t key =
			SequenceActionStableKey(object);
		if (key == 0)
			return;
		const bool directlyOwned =
			value->InInstigator == localPawn
			|| value->InOriginator == localPawn
			|| value->InInstigator
				== g_State.AlicePlayerController
			|| value->InOriginator
				== g_State.AlicePlayerController;
		const bool insideLocalUseWindow =
			Clock::now() < g_localInteractionWindowUntil;
		if (g_interactionCandidateTraceCount < 100)
		{
			++g_interactionCandidateTraceCount;
			Log("SHAREDINTERACT candidate key="
				+ SharedWorldKeyText(key)
				+ ", event=" + ObjectName(object)
				+ ", originator="
				+ ObjectName(value->InOriginator)
				+ ", instigator="
				+ ObjectName(value->InInstigator)
				+ ", direct="
				+ (directlyOwned ? std::string("yes")
					: std::string("no"))
				+ ", useWindow="
				+ (insideLocalUseWindow ? std::string("yes.")
					: std::string("no.")));
		}
		if (!directlyOwned && !insideLocalUseWindow)
			return;
		if (!g_localInteractionKeysThisAttempt.insert(key).second)
			return;

		SharedWorldEventPayload event{};
		event.kind = SharedWorldEventKind::InteractionUsed;
		event.entityKey = key;
		event.originActorId =
			g_config.role == Role::Host ? 1u : 2u;
		if (!value->ActivateIndices.empty())
		{
			event.flags = static_cast<std::uint8_t>(
				std::clamp(value->ActivateIndices.at(0),
					0, 255));
		}
		QueueSharedWorldEvent(event);
		Log("SHAREDINTERACT TX key="
			+ SharedWorldKeyText(key)
			+ ", event=" + ObjectName(object)
			+ ", originator="
			+ ObjectName(value->InOriginator)
			+ ", instigator="
			+ ObjectName(value->InInstigator)
			+ ", index=" + std::to_string(event.flags)
			+ '.');
	}

		void ReleaseCutsceneBarrier(const char* reason)
		{
			USeqAct_Interp* action = g_waitingCutsceneAction;
			const std::uint64_t key = g_waitingCutsceneKey;
			const bool replayActivation =
				g_waitingCutsceneActivationDeferred;
			if (action && IsLiveUObject(action))
			{
				if (g_waitingCutscenePlayRateOverridden)
				{
					action->PlayRate =
						g_waitingCutsceneOriginalPlayRate;
				}
				action->bPaused = false;
			}
			g_waitingCutsceneAction = nullptr;
			g_waitingCutsceneKey = 0;
			g_waitingCutsceneOriginalPlayRate = 1.0f;
			g_waitingCutscenePlayRateOverridden = false;
			g_waitingCutsceneCanDeferActivation = false;
			g_waitingCutsceneActivationDeferred = false;
			g_recentReleasedCutsceneKey = key;
			g_recentReleasedCutsceneUntil =
				Clock::now() + std::chrono::seconds(20);
			g_cutsceneBarrierAdvertiseUntil =
				Clock::now() + std::chrono::seconds(2);
			AchievementOverlay::SetCoopWaitingForPeer(false);
			Log("CUTSCENEBARRIER released key="
				+ SharedWorldKeyText(key)
				+ ", action=" + ObjectName(action)
				+ ", reason="
				+ (reason ? std::string(reason)
					: std::string("<unknown>")) + '.');
			if (replayActivation && action
				&& IsLiveUObject(action))
			{
				g_replayingDeferredCutsceneActivation = true;
				action->eventActivated();
				g_replayingDeferredCutsceneActivation = false;
				Log("CUTSCENEBARRIER replayed Activated() key="
					+ SharedWorldKeyText(key)
					+ ", action=" + ObjectName(action) + '.');
			}
			TraceCutsceneReleaseInvariants();
		}

		bool NativeForceActivateSequenceInput(
			USequenceOp* action, int inputIndex)
		{
			if (!action || inputIndex < 0
				|| inputIndex >= action->InputLinks.size())
			{
				return false;
			}
			static UFunction* forceActivateInput = nullptr;
			if (!forceActivateInput)
			{
				forceActivateInput = UFunction::FindFunction(
					"Function Engine.SequenceOp.ForceActivateInput");
			}
			if (!forceActivateInput)
				return false;
			USequenceOp_execForceActivateInput_Params parameters{};
			parameters.InputIdx = inputIndex;
			action->ProcessEvent(
				forceActivateInput, &parameters, nullptr);
			return true;
		}

		void AdvertiseEmergencyCutscene(std::uint64_t key)
		{
			if (key == 0)
				return;
			g_emergencyCutsceneAdvertiseKey = key;
			g_emergencyCutsceneAdvertiseUntil =
				Clock::now() + std::chrono::seconds(3);
		}

		void LogIncomingCutsceneOps(USeqAct_Interp* target)
		{
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!target || !objects)
				return;
			int incomingCount = 0;
			for (int32_t objectIndex = 0;
				objectIndex < objects->size(); ++objectIndex)
			{
				UObject* object = objects->at(objectIndex);
				if (!object
					|| !object->IsA(USequenceOp::StaticClass())
					|| !IsLiveUObject(object))
				{
					continue;
				}
				auto* op = reinterpret_cast<USequenceOp*>(object);
				for (int32_t outputIndex = 0;
					outputIndex < op->OutputLinks.size();
					++outputIndex)
				{
					const FSeqOpOutputLink& output =
						op->OutputLinks.at(outputIndex);
					for (int32_t linkIndex = 0;
						linkIndex < output.Links.size();
						++linkIndex)
					{
						const FSeqOpOutputInputLink& link =
							output.Links.at(linkIndex);
						if (link.LinkedOp != target)
							continue;
						++incomingCount;
						Log("CUTSCENEEMERGENCY incoming op="
							+ ObjectName(op) + ", output="
							+ std::to_string(outputIndex)
							+ ", targetInput="
							+ std::to_string(
								link.InputLinkIdx) + '.');
					}
				}
			}
			Log("CUTSCENEEMERGENCY incoming op count="
				+ std::to_string(incomingCount)
				+ ", target=" + ObjectName(target) + '.');
		}

		bool ForceEmergencyCutscene(
			std::uint64_t key, const char* source)
		{
			if (key == 0)
				return false;
			const auto now = Clock::now();
			if (key == g_lastEmergencyCutsceneKey
				&& now < g_lastEmergencyCutsceneUntil)
			{
				return true;
			}
			g_lastEmergencyCutsceneKey = key;
			g_lastEmergencyCutsceneUntil =
				now + std::chrono::seconds(20);
			AdvertiseEmergencyCutscene(key);

			if (g_waitingCutsceneAction
				&& g_waitingCutsceneKey == key)
			{
				Log("CUTSCENEEMERGENCY releasing local barrier key="
					+ SharedWorldKeyText(key) + ", source="
					+ (source ? std::string(source)
						: std::string("<unknown>")) + '.');
				ReleaseCutsceneBarrier("emergency-force");
				return true;
			}

			USeqAct_Interp* action = FindCutsceneAction(key);
			if (!action)
			{
				Log("CUTSCENEEMERGENCY action not loaded key="
					+ SharedWorldKeyText(key) + ", source="
					+ (source ? std::string(source)
						: std::string("<unknown>")) + '.');
				// Permit another request once the streaming level has loaded.
				g_lastEmergencyCutsceneUntil =
					now + std::chrono::milliseconds(750);
				return false;
			}
			if (action->bActive || action->bIsPlaying)
			{
				Log("CUTSCENEEMERGENCY action already active key="
					+ SharedWorldKeyText(key) + ", action="
					+ ObjectName(action) + '.');
				return true;
			}

			// The generated SDK wrapper clears FUNC_Native and is inert in
			// this game build. Invoke the original native UFunction directly
			// so UE3 queues the Matinee through its real parent Sequence.
			if (action->PlayRate <= 0.001f)
				action->PlayRate = 1.0f;
			action->bPaused = false;
			g_recentReleasedCutsceneKey = key;
			g_recentReleasedCutsceneUntil =
				now + std::chrono::seconds(20);
			g_emergencyForcedCutsceneAction = action;
			g_emergencyForcedCutsceneRequestedAt = now;
			g_emergencyForcedCutsceneStarted = false;
			LogIncomingCutsceneOps(action);
			const bool invoked =
				NativeForceActivateSequenceInput(action, 0);
			if (!invoked)
			{
				g_emergencyForcedCutsceneAction = nullptr;
				g_emergencyForcedCutsceneRequestedAt = {};
			}
			AchievementOverlay::SetCoopWaitingForPeer(false);
			Log("CUTSCENEEMERGENCY activated key="
				+ SharedWorldKeyText(key) + ", action="
				+ ObjectName(action) + ", source="
				+ (source ? std::string(source)
					: std::string("<unknown>"))
				+ ", native="
				+ (invoked
					? std::string("yes") : std::string("no"))
				+ ", active="
				+ (action->bActive
					? std::string("yes") : std::string("no"))
				+ ", playing="
				+ (action->bIsPlaying
					? std::string("yes.") : std::string("no.")));
			return invoked;
		}

		void TickCutsceneBarrier(
			const PlayerStatePayload& peerState,
			bool peerOnSameMap)
		{
			const auto now = Clock::now();
			const bool peerEmergency =
				peerOnSameMap
				&& peerState.cutsceneBarrierKey != 0
				&& (peerState.flags & StateForceCutscene) != 0;
			const std::uint64_t availableEmergencyKey =
				g_waitingCutsceneKey != 0
					? g_waitingCutsceneKey
					: (peerOnSameMap
						? peerState.cutsceneBarrierKey : 0);
			if (AchievementOverlay::
					ConsumeCoopForceCutsceneRequest()
				&& availableEmergencyKey != 0)
			{
				Log("CUTSCENEEMERGENCY UI requested key="
					+ SharedWorldKeyText(
						availableEmergencyKey) + '.');
				ForceEmergencyCutscene(
					availableEmergencyKey, "local-ui");
			}
			if (peerEmergency)
			{
				ForceEmergencyCutscene(
					peerState.cutsceneBarrierKey, "peer-request");
			}
			if (g_emergencyCutsceneAdvertiseKey != 0
				&& now >= g_emergencyCutsceneAdvertiseUntil)
			{
				g_emergencyCutsceneAdvertiseKey = 0;
				g_emergencyCutsceneAdvertiseUntil = {};
			}
			// Host progression is authoritative when an encounter has ended
			// there but client-only enemies remain quarantined locally. Give
			// the client's Kismet a short chance to reach the same cutscene
			// naturally, then activate the exact matching Matinee.
			if (g_config.role == Role::Client
				&& !g_waitingCutsceneAction
				&& peerOnSameMap
				&& peerState.cutsceneBarrierKey != 0
				&& !g_lastLocalCinematic
				&& g_clientQuarantinedEnemyCount > 0
				&& g_clientOrphanedEncounterSince
					!= Clock::time_point{}
				&& now - g_clientOrphanedEncounterSince
					>= std::chrono::milliseconds(750)
				&& now >= g_nextHostCutsceneRecoveryAttempt
				&& g_lastAutomaticCutsceneRecoveryKey
					!= peerState.cutsceneBarrierKey)
			{
				g_nextHostCutsceneRecoveryAttempt =
					now + std::chrono::milliseconds(500);
				USeqAct_Interp* action = FindCutsceneAction(
					peerState.cutsceneBarrierKey);
				if (action && !action->bActive)
				{
					g_lastAutomaticCutsceneRecoveryKey =
						peerState.cutsceneBarrierKey;
					Log("CUTSCENERECOVERY adopting host progression "
						"key="
						+ SharedWorldKeyText(
							peerState.cutsceneBarrierKey)
						+ ", action=" + ObjectName(action)
						+ ", quarantined="
						+ std::to_string(
							g_clientQuarantinedEnemyCount)
						+ '.');
					AdvertiseEmergencyCutscene(
						peerState.cutsceneBarrierKey);
					NativeForceActivateSequenceInput(action, 0);
				}
				else if (!action
					&& g_lastMissingHostCutsceneKey
						!= peerState.cutsceneBarrierKey)
				{
					g_lastMissingHostCutsceneKey =
						peerState.cutsceneBarrierKey;
					Log("CUTSCENERECOVERY host progression action "
						"not loaded yet, key="
						+ SharedWorldKeyText(
							peerState.cutsceneBarrierKey) + '.');
				}
			}
			if (g_waitingCutsceneAction)
			{
				if (!IsLiveUObject(g_waitingCutsceneAction))
				{
					ReleaseCutsceneBarrier("action-invalid");
				}
				else if (peerOnSameMap
					&& peerState.cutsceneBarrierKey
						== g_waitingCutsceneKey)
				{
					ReleaseCutsceneBarrier("peer-ready");
				}
				else if (!peerOnSameMap
					&& now - g_cutsceneBarrierStartedAt
						>= std::chrono::seconds(30))
				{
					ReleaseCutsceneBarrier("safety-timeout");
				}
				else
				{
					if (g_waitingCutscenePlayRateOverridden)
					{
						g_waitingCutsceneAction->PlayRate = 0.0f;
					}
					g_waitingCutsceneAction->bPaused = true;
					AchievementOverlay::SetCoopWaitingForPeer(true);
				}
			}
			else if (g_cutsceneBarrierAdvertiseKey != 0
				&& g_cutsceneBarrierAdvertiseUntil
					!= Clock::time_point{}
				&& now >= g_cutsceneBarrierAdvertiseUntil)
			{
				g_cutsceneBarrierAdvertiseKey = 0;
				g_cutsceneBarrierAdvertiseUntil = {};
			}
		}

	void TraceInterpolationStarted(AActor* actor,
		USeqAct_Interp* action)
	{
		if (!g_config.enabled || !actor || !action)
			return;
		std::string soloLevel;
		if (DetectSoloMinigameObject(action, soloLevel))
		{
			SetSoloMinigameActive(true, soloLevel);
			return;
		}
		if (g_soloMinigameActive)
		{
			return;
		}
		if (action == g_emergencyForcedCutsceneAction)
		{
			g_emergencyForcedCutsceneStarted = true;
			if (actor->IsA(ASkeletalMeshActorMAT::StaticClass()))
			{
				auto* cinematicActor =
					reinterpret_cast<ASkeletalMeshActor*>(actor);
				USkeletalMeshComponent* component =
					cinematicActor->SkeletalMeshComponent;
				const bool actorWasHidden = actor->bHidden;
				const bool componentWasHidden =
					component && component->HiddenGame;
				// A forced Matinee bypasses the Kismet setup which normally
				// builds Alice's hair, cloth and facial presentation. That
				// leaves a stripped cinematic mannequin on top of the fully
				// configured gameplay pawn. Prefer the intact gameplay Alice
				// as the emergency fallback and expose only the other Matinee
				// participants.
				const bool suppressIncompleteCinematicAlice =
					actor->IsA(
						AAliceGameSkeletalMeshActorMAT::StaticClass())
					&& component
					&& ContainsCaseInsensitive(
						ObjectName(component->SkeletalMesh),
						"CH_Alice");
				actor->bHidden =
					suppressIncompleteCinematicAlice;
				NativeSetActorHidden(
					actor, suppressIncompleteCinematicAlice);
				if (component)
				{
					component->HiddenGame =
						suppressIncompleteCinematicAlice;
					component->SetHidden(
						suppressIncompleteCinematicAlice);
				}
				NativeForceUpdateComponents(
					actor, false, false);
				Log("CUTSCENEEMERGENCY "
					+ std::string(
						suppressIncompleteCinematicAlice
							? "suppressed incomplete Alice actor="
							: "prepared actor=")
					+ ObjectName(actor) + ", mesh="
					+ ObjectName(component
						? component->SkeletalMesh : nullptr)
					+ ", actorHidden="
					+ (actorWasHidden
						? std::string("yes")
						: std::string("no"))
					+ ", componentHidden="
					+ (componentWasHidden
						? std::string("yes.")
						: std::string("no.")));
			}
		}
		const std::uint64_t key =
			SequenceActionStableKey(action);
		const auto now = Clock::now();
			const bool interactionMatinee =
				g_remoteContextInteraction.active
				|| now < g_localInteractionWindowUntil
				|| now < g_interactionCutsceneBypassUntil;
		if (g_remoteContextInteraction.active
			&& !g_remoteContextInteraction.localMatineeStarted)
		{
			g_remoteContextInteraction.localMatineeStarted = true;
			Log("CONTEXTACTOR local Matinee reached key="
				+ SharedWorldKeyText(key)
				+ ", actorKey="
				+ SharedWorldKeyText(
					g_remoteContextInteraction.actorKey)
				+ ", action=" + ObjectName(action) + '.');
		}
		if (!g_config.localMirror
			&& !g_applyingSharedInteractionMatinee
			&& key != 0
			&& Clock::now() < g_localInteractionWindowUntil)
		{
			SharedWorldEventPayload event{};
			event.kind =
				SharedWorldEventKind::InteractionMatineeStarted;
			event.entityKey = key;
			event.originActorId =
				g_config.role == Role::Host ? 1u : 2u;
			QueueSharedWorldEvent(event);
			g_interactionCutsceneBypassUntil =
				Clock::now() + std::chrono::seconds(30);
			g_localInteractionWindowUntil = {};
			Log("SHAREDINTERACT TX Matinee key="
				+ SharedWorldKeyText(key)
				+ ", action=" + ObjectName(action)
				+ ", actor=" + ObjectName(actor) + '.');
		}
		const bool peerAvailable =
			g_remotePresentation.valid
			&& !g_currentMap.empty()
			&& _stricmp(
				g_remotePresentation.state.mapName,
				g_currentMap.c_str()) == 0;
		const AAlicePlayerController* localController =
			g_State.AlicePlayerController;
		const bool controllerCinematic =
			localController
			&& (localController->bCinematicMode
				|| (localController->bCinemaDisableInputMove
					&& localController->bCinemaDisableInputLook));
		const bool eligibleBarrier =
			!g_config.localMirror
			&& peerAvailable
			&& !interactionMatinee
			&& actor->IsA(ACameraActor::StaticClass())
			&& controllerCinematic
			&& key != 0;
		if (eligibleBarrier
			&& !(key == g_recentReleasedCutsceneKey
				&& now < g_recentReleasedCutsceneUntil))
		{
			if (!g_waitingCutsceneAction)
			{
				g_waitingCutsceneAction = action;
				g_waitingCutsceneKey = key;
				g_cutsceneBarrierAdvertiseKey = key;
				g_cutsceneBarrierStartedAt = now;
				g_cutsceneBarrierAdvertiseUntil = {};
				g_waitingCutsceneOriginalPlayRate = action->PlayRate;
				g_waitingCutscenePlayRateOverridden = true;
				g_waitingCutsceneCanDeferActivation = false;
				g_waitingCutsceneActivationDeferred = false;
				action->PlayRate = 0.0f;
				action->bPaused = true;
				AchievementOverlay::SetCoopWaitingForPeer(true);
				Log("CUTSCENEBARRIER waiting key="
					+ SharedWorldKeyText(key)
					+ ", action=" + ObjectName(action)
					+ ", actor=" + ObjectName(actor)
					+ ", position="
					+ std::to_string(action->Position) + '.');
			}
			else if (g_waitingCutsceneKey == key)
			{
				if (g_waitingCutscenePlayRateOverridden)
					action->PlayRate = 0.0f;
				action->bPaused = true;
			}
			else
			{
				Log("CUTSCENEBARRIER ignored nested key="
					+ SharedWorldKeyText(key)
					+ ", waitingFor="
					+ SharedWorldKeyText(
						g_waitingCutsceneKey) + '.');
			}
		}
		if (g_interpolationTraceCount >= 100)
			return;
		++g_interpolationTraceCount;
		Log("CUTSCENEINTERP start #" + std::to_string(
				g_interpolationTraceCount)
			+ ", key=" + SharedWorldKeyText(key)
			+ ", actor=" + ObjectName(actor)
			+ ", action=" + ObjectName(action)
			+ ", inputs="
			+ std::to_string(action->InputLinks.size())
			+ ", outputs="
			+ std::to_string(action->OutputLinks.size())
			+ ", aliceMatinee="
			+ (action->bAliceMatinee
				? std::string("yes")
				: std::string("no"))
			+ ", controllerCinematic="
			+ (controllerCinematic
				? std::string("yes.")
				: std::string("no.")));
	}

	void TraceProcessEvent(UObject* object, UFunction* function,
		const void* parameters)
	{
		if (g_soloMinigameActive)
			return;
		TraceProcessEventInternal(object, function, parameters);
	}

	void PumpLifecycleCommands()
	{
		if (g_soloMinigameActive)
			return;
		ApplyPendingLifecycleCommand();
	}

	bool IsEnabled()
	{
		return g_config.enabled;
	}

	bool IsApplyingHostProgression()
	{
		return g_config.enabled && g_applyingHostProgression;
	}

	void RepairRemoteVisibilityAfterLocalDodge()
	{
		if (g_soloMinigameActive
			|| !g_remotePawn || g_remoteHidden)
			return;
		g_remotePawn->EnableForceTranslucency(
			false, 1.0f, 0.0f, 0, false);
		const std::array<UPrimitiveComponent*, 7> components{
			g_remotePawn->Mesh,
			g_remotePawn->UpperBodyComponent,
			g_remotePawn->HairComponent,
			g_remotePawn->SkirtComponent,
			g_remotePawn->BowComponent,
			g_remotePawn->RibbonComponent,
			g_remotePawn->EarComponent
		};
		for (UPrimitiveComponent* component : components)
		{
			if (!component)
				continue;
			component->EnableForceTranslucency(
				false, 1.0f, 0.0f, 0, false);
			component->bForceTranslucency = false;
			component->ForceTranslucencyAlpha = 1.0f;
			component->ForceTranslucencyTargetAlpha = 1.0f;
		}
		if (g_remotePresentationWeapon)
		{
			g_remotePresentationWeapon->EnableForceTranslucency(
				false, 1.0f, 0.0f, 0, false);
			if (g_remotePresentationWeapon->Mesh)
			{
				g_remotePresentationWeapon->Mesh
					->EnableForceTranslucency(
						false, 1.0f, 0.0f, 0, false);
			}
		}
	}

	std::string GetOverlayStatusLine()
	{
		if (!g_config.enabled)
			return {};
		std::string status =
			g_config.role == Role::Host ? "HOST" : "CLIENT";
		status += g_connected.load()
			? " | RELAY ONLINE" : " | RELAY OFFLINE";
		if (g_soloMinigameActive)
			return status + " | SOLO MINIGAME";
		status += g_remotePawn
			? " | PEER VISIBLE" : " | WAITING FOR PEER";
		return status;
	}

	std::string GetOverlayDebugDetails()
	{
		if (!g_config.enabled)
			return {};
		std::ostringstream stream;
		stream << "Remote: "
			<< (g_remotePawn ? ObjectName(g_remotePawn) : "<none>")
			<< '\n';
		stream << "Enemy aggro: host="
			<< g_hostAggroHostTargets
			<< " client=" << g_hostAggroClientTargets
			<< " | authority="
			<< (g_config.role == Role::Host
				? "host"
				: (g_clientEnemyAuthorityActive
					? "host snapshot" : "local"))
			<< '\n';
		stream << "Animation: "
			<< (g_remoteFullBodyChannel.active
				? g_remoteFullBodyChannel.sequenceName : "<idle>")
			<< '\n';
		stream << "Action: #"
			<< g_lastRemoteActionSerial << ' '
			<< PlayerActionName(g_remotePresentation.valid
				? g_remotePresentation.state.action
				: PlayerAction::None)
			<< " | special="
			<< static_cast<int>(
				g_remotePresentation.valid
					? g_remotePresentation.state.specialMove : 0)
			<< '\n';
		stream << "Weapon: type="
			<< static_cast<int>(g_remotePresentationWeaponType)
			<< " actor="
			<< ObjectName(g_remotePresentationWeapon) << '\n';
		std::size_t outboundProjectileCount = 0;
		std::size_t inboundProjectileCount = 0;
		{
			std::lock_guard lock(g_stateMutex);
			outboundProjectileCount =
				g_outboundProjectileEvents.size();
			inboundProjectileCount =
				g_inboundProjectileEvents.size();
		}
		stream << "Projectiles: pepper="
			<< g_remotePepperVisuals.size()
			<< " bomb=" << g_remoteClockBombVisuals.size()
			<< " localBomb=" << g_localClockBomb.projectileId
			<< " queue=" << outboundProjectileCount
			<< '/' << inboundProjectileCount << '\n';
		stream << std::fixed << std::setprecision(1)
			<< "Pepper projectiles: disabled (muzzle only)"
			<< "\nBomb hat Z offset: "
			<< ClockBombZOffset << '\n';
		stream << "VFX anchor: "
			<< VfxAttachmentCandidateName(
				g_vfxAttachmentCandidate)
			<< " | shared="
			<< (g_sharedDevState ? "yes" : "no")
			<< " rev=" << g_sharedDevRevision
			<< '/' << g_sharedDevCommandRevision << '\n';
		if (g_remotePresentationWeapon)
		{
			AWeaponForAlice* weapon =
				g_remotePresentationWeapon;
			stream << "Sockets: trace="
				<< (weapon->TraceSocket.IsValid()
					? weapon->TraceSocket.ToString()
					: "<invalid>")
				<< " muzzle="
				<< (weapon->MuzzleFlashSocket.IsValid()
					? weapon->MuzzleFlashSocket.ToString()
					: "<invalid>")
				<< " range="
				<< (weapon->RangeAttackSocket.IsValid()
					? weapon->RangeAttackSocket.ToString()
					: "<invalid>")
				<< " first="
				<< (FirstWeaponSocket(weapon).IsValid()
					? FirstWeaponSocket(weapon).ToString()
					: "<invalid>") << '\n';
			const FName resolved =
				ResolveCandidateWeaponSocket(
					weapon, g_vfxAttachmentCandidate);
			stream << "Resolved anchor: "
				<< (resolved.IsValid()
					? resolved.ToString() : "<none>")
				<< " kind="
				<< (WeaponMeshHasSocket(weapon, resolved)
					? "socket"
					: (WeaponMeshHasBone(weapon, resolved)
						? "bone" : "missing"))
				<< '\n';
			stream << "Templates: trail="
				<< ObjectName(weapon->TracePSCTemplate)
				<< " muzzle="
				<< ObjectName(weapon->MuzzleFlashPSCTemplate)
				<< " effect0="
				<< ObjectName(
					weapon->WeaponEffectPSCTemplate.size() > 0
						? weapon->WeaponEffectPSCTemplate.at(0)
						: nullptr)
				<< '\n';
		}
		stream << "Dodge hidden="
			<< (g_remoteDodgeVisualHidden ? "yes" : "no")
			<< " | glide="
			<< (g_remoteGlideVfxActive ? "yes" : "no")
			<< " | bombAnim="
			<< (g_remoteClockBombAnimationActive ? "yes" : "no")
			<< " | trail="
			<< (g_remoteAttackTrailActive ? "yes" : "no")
			<< " | muzzle="
			<< (g_remoteMuzzleFlashActive ? "yes" : "no")
			<< '\n';
		if (g_remotePawn)
		{
			stream << std::fixed << std::setprecision(3)
				<< "Scale: body=" << g_remotePawn->DrawScale
				<< " hairActor="
				<< (g_remoteHairProxy
					? g_remoteHairProxy->DrawScale : -1.0f)
				<< " hairComp="
				<< (g_remoteIndependentHair
					? g_remoteIndependentHair->Scale : -1.0f)
				<< " hairLength="
				<< (g_remoteIndependentHair
					? g_remoteIndependentHair->LengthScale
					: -1.0f)
				<< '\n';
		}
		stream << "Last VFX: " << g_lastRemoteVfx
			<< "\nButtons are presentation-only; they do no damage.";
		return stream.str();
	}

	void ExecuteDevCommand(int command)
	{
		if (!g_config.enabled || g_soloMinigameActive)
			return;

		const bool sharedCommand =
			(command >= 1 && command <= 5)
			|| command == 9
			|| (command >= 10 && command <= 16);
		if (sharedCommand && !g_applyingSharedDevCommand)
			PublishSharedDevCommand(command);
		const bool configurationCommand =
			command == 7 || command == 8
			|| (command >= 10 && command <= 16);
		if (!g_remotePawn && !configurationCommand)
			return;

		const Clock::time_point now = Clock::now();
		switch (command)
		{
		case 1: // TestDodge
			if (!g_remoteDodgeParticleTemplate)
				g_remoteDodgeParticleTemplate =
					FindRemoteDodgeParticleTemplate();
			g_devDodgeHideUntil =
				now + std::chrono::milliseconds(850);
			g_remoteDodgeParticleUntil =
				now + std::chrono::milliseconds(850);
			g_nextRemoteDodgeParticle = now;
			g_lastRemoteVfx = "DEV dodge: "
				+ ObjectName(g_remoteDodgeParticleTemplate);
			Log("VFXSTAGE DEV command=dodge, template="
				+ ObjectName(g_remoteDodgeParticleTemplate) + '.');
			break;
		case 2: // TestMeleeTrail
			SetRemoteAttackTrail(false);
			SetRemoteAttackTrail(true);
			g_lastRemoteVfx = "DEV melee trail";
			Log("VFXSTAGE DEV command=melee-trail.");
			break;
		case 3: // TestMuzzle
			SetRemoteMuzzleFlash(
				g_remotePresentationWeapon, false);
			SetRemoteMuzzleFlash(
				g_remotePresentationWeapon, true);
			g_devMuzzleUntil =
				now + std::chrono::milliseconds(800);
			g_lastRemoteVfx = "DEV pepper muzzle";
			Log("VFXSTAGE DEV command=muzzle.");
			break;
		case 4: // TestShrink
		{
			UParticleSystemComponent* spawned =
				SpawnRemoteCosmeticParticle(
					g_remotePawn,
					g_remotePawn->StartShrink, false);
			g_lastRemoteVfx = std::string("DEV shrink: ")
				+ (spawned ? "spawned" : "failed");
			Log(std::string("VFXSTAGE DEV command=shrink, spawned=")
				+ (spawned ? "yes." : "no."));
			break;
		}
		case 5: // TestGlide
		{
			if (g_remoteGlideParticle)
				RetirePersistentPresentationParticle(
					g_remoteGlideParticle);
			UParticleSystem* glide = FindLoadedParticleSystem(
				{ "Glide" }, { "Alice" });
			if (!glide)
				glide = FindLoadedParticleSystem(
					{ "Float" }, { "Alice" });
			g_remoteGlideParticle =
				SpawnRemoteCosmeticParticle(
					g_remotePawn, glide, true);
			MakePresentationParticleVisible(
				g_remoteGlideParticle);
			g_devGlideUntil =
				now + std::chrono::milliseconds(1500);
			g_lastRemoteVfx = "DEV isolated glide";
			Log("VFXSTAGE DEV command=glide, template="
				+ ObjectName(glide) + ", spawned="
				+ (g_remoteGlideParticle ? "yes." : "no."));
			break;
		}
		case 6: // ToggleProxyHidden
			g_devForceProxyHidden = !g_devForceProxyHidden;
			g_lastRemoteVfx = std::string("DEV force hidden: ")
				+ (g_devForceProxyHidden ? "on" : "off");
			Log(std::string("VFXSTAGE DEV command=proxy-hidden, value=")
				+ (g_devForceProxyHidden ? "on." : "off."));
			break;
		case 7: // PreviousVfxAnchor
		{
			const int count = static_cast<int>(
				VfxAttachmentCandidate::Count);
			const int current = static_cast<int>(
				g_vfxAttachmentCandidate);
			g_vfxAttachmentCandidate =
				static_cast<VfxAttachmentCandidate>(
					(current + count - 1) % count);
			PublishSharedVfxAttachmentCandidate();
			g_lastRemoteVfx = std::string("anchor selected: ")
				+ VfxAttachmentCandidateName(
					g_vfxAttachmentCandidate);
			Log("VFXSTAGE DEV attachment candidate="
				+ std::string(VfxAttachmentCandidateName(
					g_vfxAttachmentCandidate)) + '.');
			break;
		}
		case 8: // NextVfxAnchor
		{
			const int count = static_cast<int>(
				VfxAttachmentCandidate::Count);
			const int current = static_cast<int>(
				g_vfxAttachmentCandidate);
			g_vfxAttachmentCandidate =
				static_cast<VfxAttachmentCandidate>(
					(current + 1) % count);
			PublishSharedVfxAttachmentCandidate();
			g_lastRemoteVfx = std::string("anchor selected: ")
				+ VfxAttachmentCandidateName(
					g_vfxAttachmentCandidate);
			Log("VFXSTAGE DEV attachment candidate="
				+ std::string(VfxAttachmentCandidateName(
					g_vfxAttachmentCandidate)) + '.');
			break;
		}
		case 9: // TestWeaponEffect
		{
			AWeaponForAlice* weapon =
				g_remotePresentationWeapon;
			if (!weapon)
			{
				g_lastRemoteVfx =
					"DEV weapon effect: no proxy weapon";
				break;
			}
			UParticleSystem* particle =
				weapon->WeaponEffectPSCTemplate.size() > 0
					? weapon->WeaponEffectPSCTemplate.at(0)
					: nullptr;
			if (!particle)
				particle = weapon->TracePSCTemplate;
			if (!particle)
				particle = weapon->MuzzleFlashPSCTemplate;
			if (!particle)
			{
				g_lastRemoteVfx =
					"DEV weapon effect: no template";
				break;
			}
			if (g_vfxAttachmentCandidate
				== VfxAttachmentCandidate::NativeWeapon)
			{
				SetRemoteAttackTrail(false);
				SetRemoteAttackTrail(true);
				g_lastRemoteVfx =
					"DEV weapon effect: native invoked";
			}
			else
			{
				if (g_remoteAttackTrailParticle)
					HardStopPresentationParticle(
						g_remoteAttackTrailParticle, true);
				g_remoteAttackTrailParticle =
					SpawnWeaponParticleCandidate(
						weapon, particle,
						g_vfxAttachmentCandidate);
				MakePresentationParticleVisible(
					g_remoteAttackTrailParticle);
				g_remoteAttackTrailActive = true;
				g_remoteAttackTrailUntil =
					now + std::chrono::milliseconds(900);
				g_lastRemoteVfx =
					"DEV weapon effect: "
					+ ObjectName(particle) + " @ "
					+ VfxAttachmentCandidateName(
						g_vfxAttachmentCandidate)
					+ (g_remoteAttackTrailParticle
						? " [spawned]" : " [failed]");
			}
			Log("VFXSTAGE DEV command=weapon-effect, template="
				+ ObjectName(particle)
				+ ", candidate="
				+ VfxAttachmentCandidateName(
					g_vfxAttachmentCandidate)
				+ ", spawned="
				+ (g_vfxAttachmentCandidate
					== VfxAttachmentCandidate::NativeWeapon
					? "native."
					: (g_remoteAttackTrailParticle
						? "yes." : "no.")));
			break;
		}
		case 10: // PepperHypothesis1
		case 11: // PepperHypothesis2
		case 12: // PepperHypothesis3
			g_pepperProjectileHypothesis = command - 9;
			ClearRemoteProjectileVisuals(true);
			g_lastRemoteVfx = "Pepper hypothesis "
				+ std::to_string(g_pepperProjectileHypothesis);
			Log("PROJECTILESTAGE DEV pepper hypothesis="
				+ std::to_string(
					g_pepperProjectileHypothesis) + '.');
			break;
		case 13: // BombHypothesis1
		case 14: // BombHypothesis2
		case 15: // BombHypothesis3
			g_clockBombHypothesis = command - 12;
			ClearRemoteProjectileVisuals(true);
			g_lastRemoteVfx = "Bomb hypothesis "
				+ std::to_string(g_clockBombHypothesis);
			Log("PROJECTILESTAGE DEV clock bomb hypothesis="
				+ std::to_string(g_clockBombHypothesis)
				+ ", zOffset="
				+ std::to_string(ClockBombHypothesisZOffset(
					static_cast<std::uint8_t>(
						g_clockBombHypothesis))) + '.');
			break;
		case 16: // ClearProjectileTests
			ClearRemoteProjectileVisuals(true);
			g_lastRemoteVfx =
				"Projectile test objects cleared";
			Log("PROJECTILESTAGE DEV cleared remote projectiles.");
			break;
		default:
			break;
		}
	}

	bool IsActionTraceEnabled()
	{
		return g_config.enabled && !g_soloMinigameActive
			&& g_config.actionTrace;
	}

	bool ShouldSuppressSharedPlayerDamage(
		UObject* object, UFunction* function,
		const void* parameters)
	{
		if (!g_config.enabled || g_soloMinigameActive
			|| g_config.localMirror
			|| !g_config.sharedEnemyHealth
			|| !g_config.sharedEnemyTransforms
			|| g_applyingSharedPlayerDamage
			|| !object || !function || !parameters
			|| function->GetName() != "TakeDamage")
		{
			return false;
		}
		const auto* value = reinterpret_cast<const
			AAlicePawn_eventTakeDamage_Params*>(parameters);
		if (value->Damage <= 0 || value->Damage > 100000)
			return false;
		APawn* instigatorPawn =
			value->InstigatedBy
				? value->InstigatedBy->Pawn : nullptr;
		if (!instigatorPawn && value->DamageCauser)
			instigatorPawn = value->DamageCauser->Instigator;
		if (!instigatorPawn
			|| !instigatorPawn->IsA(
				AAliceGameKynapsePawn::StaticClass()))
		{
			return false;
		}
		auto* enemy = reinterpret_cast<
			AAliceGameKynapsePawn*>(instigatorPawn);
		if (!enemy->WorldInfo
			|| enemy->WorldInfo != g_currentWorld)
		{
			return false;
		}

		if (g_config.role == Role::Host
			&& object == g_remotePawn)
		{
			const std::uint64_t entityKey =
				SharedEnemyStableKey(enemy);
			if (entityKey)
			{
				SharedWorldEventPayload event{};
				event.kind =
					SharedWorldEventKind::PlayerDamageRequest;
				event.originActorId = 1;
				event.entityKey = entityKey;
				event.damage = value->Damage;
				event.hitLocation[0] = value->HitLocation.X;
				event.hitLocation[1] = value->HitLocation.Y;
				event.hitLocation[2] = value->HitLocation.Z;
				event.momentum[0] = value->Momentum.X;
				event.momentum[1] = value->Momentum.Y;
				event.momentum[2] = value->Momentum.Z;
				const std::string damageType =
					ObjectName(value->DamageType);
				strncpy_s(event.damageType,
					damageType.c_str(), _TRUNCATE);
				QueueSharedWorldEvent(event);
				Log("PLAYERDAMAGE TX target=client, key="
					+ SharedWorldKeyText(entityKey)
					+ ", enemy=" + ObjectName(enemy)
					+ ", damage="
					+ std::to_string(value->Damage) + '.');
			}
			else
			{
				Log("PLAYERDAMAGE suppressed proxy hit from "
					"unmatched enemy=" + ObjectName(enemy)
					+ '.');
			}
			// The proxy is presentation-only. Never let its local health,
			// death state or global Alice UI mutate.
			return true;
		}

		AAlicePawn* localPawn = GetLocalPawn();
		if (object == localPawn
			&& ShouldGuardBackgroundPlayerDamage())
		{
			LogBackgroundDamageGuard("local-ai", value->Damage);
			return true;
		}
		if (g_config.role == Role::Client
			&& object == localPawn
			&& g_clientEnemyAuthorityActive)
		{
			const std::uint64_t localKey =
				SharedEnemyStableKey(enemy);
			const auto alias =
				g_clientEnemyHostKeyByActor.find(enemy);
			const std::uint64_t entityKey =
				alias != g_clientEnemyHostKeyByActor.end()
					? alias->second : localKey;
			const auto binding =
				g_clientSharedEnemyBindings.find(entityKey);
			if (entityKey
				&& binding
					!= g_clientSharedEnemyBindings.end()
				&& binding->second.authorized
				&& binding->second.clientAuthority
				&& binding->second.enemy == enemy)
			{
				Log("PLAYERDAMAGE accepted delegated client AI hit, "
					"key=" + SharedWorldKeyText(entityKey)
					+ ", enemy=" + ObjectName(enemy)
					+ ", damage="
					+ std::to_string(value->Damage) + '.');
				return false;
			}
			// Client-side Kynapse continues running around the host pose and
			// may occasionally produce a duplicate touch/melee hit. The host
			// is authoritative for non-delegated NPC targeting and sends the
			// real hit above.
			const auto now = Clock::now();
			if (now >= g_nextSuppressedClientDamageLog)
			{
				g_nextSuppressedClientDamageLog =
					now + std::chrono::seconds(2);
				Log("PLAYERDAMAGE suppressed client-local AI hit, "
					"enemy=" + ObjectName(enemy)
					+ ", damage="
					+ std::to_string(value->Damage) + '.');
			}
			return true;
		}
		return false;
	}

	bool IsWorldTraceEnabled()
	{
		return g_config.enabled && !g_soloMinigameActive
			&& g_config.worldTrace;
	}

	void TraceWorldProcessEvent(UObject* object, UFunction* function,
		const void* parameters, bool after)
	{
		if (!g_config.enabled || g_soloMinigameActive
			|| !g_config.worldTrace
			|| !object || !function || !g_currentWorld
			|| !g_State.bRealGameplay)
		{
			return;
		}
		if (!IsPotentialWorldTraceFunction(function))
			return;
		const WorldTraceKind kind =
			IdentifyWorldTraceObject(object);
		if (kind == WorldTraceKind::None)
			return;
		auto* actor = reinterpret_cast<AActor*>(object);
		AAlicePawn* localPawn = GetLocalPawn();
		if (actor->WorldInfo != g_currentWorld
			|| !IsWithinWorldTraceRadius(actor, localPawn))
		{
			return;
		}
		const std::string functionName = function->GetName();
		if (!IsWorldTraceFunction(kind, functionName))
			return;

		std::string key = WorldTraceStableKey(object, kind);
		const auto record = g_worldTraceRecords.find(object);
		if (record != g_worldTraceRecords.end())
			key = record->second.key;
		Log("WORLDTRACE EVENT #"
			+ std::to_string(++g_worldTraceEventSerial)
			+ ", phase=" + (after ? std::string("post")
				: std::string("pre"))
			+ ", key=" + key
			+ ", kind=" + WorldTraceKindName(kind)
			+ ", object=" + ObjectName(object)
			+ ", function=" + function->GetFullName()
			+ ", params=" + (after
				? std::string("-")
				: WorldTraceEventParameters(
					kind, functionName, parameters))
			+ ", state={" + WorldTraceState(object, kind) + "}.");
	}

	void HandleSharedCombatProcessEvent(UObject* object,
		UFunction* function, const void* parameters, bool after)
	{
		if (g_soloMinigameActive)
			return;
		if (g_config.enabled
			&& g_applyingSharedEnemyDamage
			&& g_config.role == Role::Host
			&& object && function && parameters && !after
			&& object->IsA(
				AAliceGameKynapseAIController::StaticClass()))
		{
			const std::string callback = function->GetName();
			if (callback == "NotifyTakeHit"
				|| callback == "KynapseTakeDamager")
			{
				AController* instigator =
					*reinterpret_cast<AController* const*>(
						parameters);
				Log("AGGRO native callback=" + callback
					+ ", receiver=" + ObjectName(object)
					+ ", instigator="
					+ ObjectName(instigator)
					+ ", pawn="
					+ ObjectName(
						instigator ? instigator->Pawn : nullptr)
					+ ", isPlayer="
					+ (instigator && instigator->bIsPlayer
						? std::string("yes.")
						: std::string("no.")));
			}
		}
		if (!g_config.enabled || !g_config.sharedEnemyHealth
			|| !object || !function || !g_currentWorld
			|| !g_State.bRealGameplay
			|| g_applyingSharedEnemyDamage)
		{
			return;
		}
		const WorldTraceKind traceKind =
			IdentifyWorldTraceObject(object);
		if (traceKind == WorldTraceKind::Breakable)
		{
			if (!after || g_applyingSharedBreakableDestroy
				|| function->GetName() != "TakeDamage")
			{
				return;
			}
			auto* breakable =
				reinterpret_cast<AGameBreakableActor*>(object);
			if (breakable->WorldInfo != g_currentWorld
				|| !breakable->bDestoryed)
			{
				return;
			}
			const std::uint64_t entityKey =
				WorldTraceStableKeyValue(
					object, WorldTraceKind::Breakable);
			if (!entityKey
				|| !g_sentSharedBreakableKeys.insert(
					entityKey).second)
			{
				return;
			}
			SharedWorldEventPayload event{};
			event.kind =
				SharedWorldEventKind::BreakableDestroyed;
			event.originActorId =
				g_config.role == Role::Host ? 1u : 2u;
			event.entityKey = entityKey;
			event.flags =
				(breakable->CanSpawnHealth ? 1u : 0u)
				| (breakable->CanSpawnXP ? 2u : 0u);
			event.hitLocation[0] = breakable->Location.X;
			event.hitLocation[1] = breakable->Location.Y;
			event.hitLocation[2] = breakable->Location.Z;
			QueueSharedWorldEvent(event);
			Log("SHAREDBREAKABLE TX key="
				+ SharedWorldKeyText(entityKey)
				+ ", object=" + ObjectName(breakable)
				+ ", role="
				+ (g_config.role == Role::Host
					? std::string("host.")
					: std::string("client.")));
			return;
		}
		const auto cached =
			g_sharedDamageFunctionCache.find(function);
		bool isTakeDamage = false;
		if (cached != g_sharedDamageFunctionCache.end())
			isTakeDamage = cached->second;
		else
		{
			isTakeDamage = function->GetName() == "TakeDamage";
			g_sharedDamageFunctionCache.emplace(
				function, isTakeDamage);
		}
		if (!isTakeDamage
			|| traceKind != WorldTraceKind::Enemy)
		{
			return;
		}
		auto* enemy =
			reinterpret_cast<AAliceGameKynapsePawn*>(object);
		if (enemy->WorldInfo != g_currentWorld)
			return;
		const std::uint64_t localEntityKey =
			SharedEnemyStableKey(enemy);
		const auto enemyAlias =
			g_clientEnemyHostKeyByActor.find(enemy);
		const std::uint64_t entityKey =
			g_config.role == Role::Client
				&& enemyAlias != g_clientEnemyHostKeyByActor.end()
				? enemyAlias->second : localEntityKey;
		if (g_config.role == Role::Client
			&& g_config.sharedEnemyTransforms
			&& g_hostAuthorizedEnemyKeys.find(localEntityKey)
				== g_hostAuthorizedEnemyKeys.end())
		{
			if (g_loggedUnauthorizedEnemyDamage.insert(
					entityKey).second)
			{
				Log("SHAREDWORLD blocked client-only enemy damage key="
					+ SharedWorldKeyText(entityKey)
					+ "; host has not advertised this enemy.");
			}
			return;
		}

		if (!after)
		{
			if (!parameters)
				return;
			const auto* value = reinterpret_cast<const
				AAliceGameKynapsePawn_eventTakeDamage_Params*>(
					parameters);
			if (value->DamageAmount <= 0
				|| value->DamageAmount > 100000)
			{
				return;
			}
			PendingEnemyDamage pending{};
			pending.damage = value->DamageAmount;
			pending.hitLocation = value->HitLocation;
			pending.momentum = value->Momentum;
			pending.damageType = ObjectName(value->DamageType);
			g_pendingEnemyDamage[object] = std::move(pending);
			return;
		}

		const auto pending = g_pendingEnemyDamage.find(object);
		if (pending == g_pendingEnemyDamage.end())
			return;
		const PendingEnemyDamage damage = pending->second;
		g_pendingEnemyDamage.erase(pending);

		SharedWorldEventPayload event{};
		event.kind = g_config.role == Role::Host
			? SharedWorldEventKind::EnemyAuthoritativeState
			: SharedWorldEventKind::EnemyDamageRequest;
		event.originActorId =
			g_config.role == Role::Host ? 1u : 2u;
		event.entityKey = entityKey;
		event.damage = damage.damage;
		event.health = enemy->Health;
		event.healthMax = enemy->HealthMax;
		event.flags = enemy->Health <= 0
			|| enemy->bDeleteMe
			|| enemy->bPendingDelete
			? SharedWorldEnemyDead : 0;
		event.hitLocation[0] = damage.hitLocation.X;
		event.hitLocation[1] = damage.hitLocation.Y;
		event.hitLocation[2] = damage.hitLocation.Z;
		event.momentum[0] = damage.momentum.X;
		event.momentum[1] = damage.momentum.Y;
		event.momentum[2] = damage.momentum.Z;
		strncpy_s(event.damageType,
			damage.damageType.c_str(), _TRUNCATE);

		if (g_config.role == Role::Host)
		{
			RegisterHostEnemyAggroDamage(
				event.entityKey, 1, event.damage);
			QueueAuthoritativeEnemyState(
				enemy, event.entityKey, event, 1);
		}
		else
		{
			QueueSharedWorldEvent(event);
			Log("SHAREDWORLD TX damage key="
				+ SharedWorldKeyText(event.entityKey)
				+ ", damage=" + std::to_string(event.damage)
				+ ", localHealth="
				+ std::to_string(event.health)
				+ '/' + std::to_string(event.healthMax) + '.');
		}
	}

	bool ShouldUseUniqueMutex()
	{
		return g_config.enabled;
	}

	void ObserveSavePath(const wchar_t* effectivePath)
	{
		if (!g_config.enabled || !effectivePath || !*effectivePath)
			return;
		const std::filesystem::path path(effectivePath);
		const std::wstring name = path.filename().wstring();
		SaveSyncFileKind kind{};
		if (_wcsicmp(name.c_str(),
				SaveFileName(SaveSyncFileKind::PersistentData)) == 0)
			kind = SaveSyncFileKind::PersistentData;
		else if (_wcsicmp(name.c_str(),
				SaveFileName(SaveSyncFileKind::Checkpoint)) == 0)
			kind = SaveSyncFileKind::Checkpoint;
		else
			return;
		std::error_code error;
		const std::filesystem::path absolute =
			std::filesystem::absolute(path, error);
		std::lock_guard<std::mutex> lock(g_observedSavePathMutex);
		g_observedSavePaths[SaveFileIndex(kind)] =
			error ? path : absolute;
	}

	bool TryRedirectClientSave(const wchar_t* originalPath, std::wstring& redirectedPath)
	{
		if (!g_config.enabled || g_config.role != Role::Client || !originalPath || !*originalPath)
			return false;
		RememberOriginalClientSavePath(originalPath);

		const std::filesystem::path sandboxDirectory =
			std::filesystem::path(SystemHelper::GetModulePath()) / L"AliceCoop" / L"client-saves";
		std::error_code error;
		std::filesystem::create_directories(sandboxDirectory, error);
		const std::filesystem::path source(originalPath);
		redirectedPath = (sandboxDirectory / source.filename()).wstring();
		return true;
	}

