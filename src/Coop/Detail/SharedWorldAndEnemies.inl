		std::uint64_t WorldTraceStableKeyValue(UObject* object,
			WorldTraceKind kind)
		{
			const FVector origin = WorldTraceOrigin(object, kind);
			const auto quantize = [](float value)
			{
				return static_cast<int>(std::lround(value / 10.0f) * 10);
			};
			std::ostringstream identity;
			identity << g_currentMap << '|'
				<< WorldTraceKindName(kind) << '|'
				<< (object->Class ? object->Class->GetName() : "<class>")
				<< '|' << quantize(origin.X)
				<< ',' << quantize(origin.Y)
				<< ',' << quantize(origin.Z);
			return WorldTraceHash(identity.str());
		}

		std::uint64_t SharedEnemyStableKey(
			AAliceGameKynapsePawn* enemy)
		{
			if (!enemy)
				return 0;
			const auto found = g_sharedEnemyStableKeys.find(enemy);
			if (found != g_sharedEnemyStableKeys.end())
				return found->second;
			const std::uint64_t key = WorldTraceStableKeyValue(
				enemy, WorldTraceKind::Enemy);
			if (key)
				g_sharedEnemyStableKeys.emplace(enemy, key);
			return key;
		}

		std::uint16_t SharedEnemyClassSignature(
			const AAliceGameKynapsePawn* enemy)
		{
			if (!enemy || !enemy->Class)
				return 0;
			return static_cast<std::uint16_t>(
				WorldTraceHash(enemy->Class->GetName()) & 0xFFFFu);
		}

		std::string WorldTraceStableKey(UObject* object,
			WorldTraceKind kind)
		{
			std::ostringstream key;
			key << std::hex << std::setfill('0') << std::setw(16)
				<< WorldTraceStableKeyValue(object, kind);
			return key.str();
		}

		std::string WorldTraceState(UObject* object,
			WorldTraceKind kind)
		{
			auto* actor = reinterpret_cast<AActor*>(object);
			std::ostringstream state;
			state << "loc=" << FormatWorldTraceVector(actor->Location)
				<< ", hidden=" << (actor->bHidden ? 1 : 0)
				<< ", delete=" << (actor->bDeleteMe ? 1 : 0)
				<< ", pendingDelete="
				<< (actor->bPendingDelete ? 1 : 0)
				<< ", physics=" << static_cast<int>(actor->Physics);
			switch (kind)
			{
			case WorldTraceKind::Enemy:
			{
				auto* enemy =
					reinterpret_cast<AAliceGameKynapsePawn*>(object);
				state << ", health=" << enemy->Health
					<< '/' << enemy->HealthMax
					<< ", npcState="
					<< static_cast<int>(enemy->NPCState)
					<< ", healthState="
					<< static_cast<int>(enemy->HealthState)
					<< ", aiState="
					<< static_cast<int>(enemy->AIState)
					<< ", fighting="
					<< (enemy->bIsFighting ? 1 : 0)
					<< ", level=" << enemy->EnemyLevel
					<< ", rank=" << enemy->EnemyClassRank
					<< ", dropsHP="
					<< (enemy->CanSpawnHealth ? 1 : 0)
					<< ", dropsXP="
					<< (enemy->CanSpawnXP ? 1 : 0);
				break;
			}
			case WorldTraceKind::Breakable:
			{
				auto* breakable =
					reinterpret_cast<AGameBreakableActor*>(object);
				state << ", destroyed="
					<< (breakable->bDestoryed ? 1 : 0)
					<< ", pendingSelf="
					<< (breakable->bPendingDestroySelf ? 1 : 0)
					<< ", step=" << breakable->CurrentBreakableStep
					<< '/' << breakable->BreakableSteps.size()
					<< ", dropsHP="
					<< (breakable->CanSpawnHealth ? 1 : 0)
					<< ", dropsXP="
					<< (breakable->CanSpawnXP ? 1 : 0);
				break;
			}
			case WorldTraceKind::Pickup:
			{
				auto* pickup =
					reinterpret_cast<AAlicePickupFactory*>(object);
				state << ", disabled="
					<< (pickup->bIsDisabled ? 1 : 0)
					<< ", respawning="
					<< (pickup->bIsRespawning ? 1 : 0);
				if (object->IsA(
					AAliceXPPickupFactory::StaticClass()))
				{
					auto* xp = reinterpret_cast<
						AAliceXPPickupFactory*>(object);
					state << ", xp=" << xp->XPValue
						<< ", picked="
						<< (xp->bPicked ? 1 : 0);
				}
				break;
			}
			case WorldTraceKind::DroppedPickup:
			{
				auto* pickup =
					reinterpret_cast<AAliceDroppedPickup*>(object);
				state << ", pickupable="
					<< (pickup->bPickupable ? 1 : 0)
					<< ", rotating="
					<< (pickup->bRotatingPickup ? 1 : 0);
				break;
			}
			default:
				break;
			}
			return state.str();
		}

		std::string WorldTraceEventParameters(WorldTraceKind kind,
			const std::string& functionName, const void* parameters)
		{
			if (!parameters)
				return "-";
			if (functionName == "TakeDamage")
			{
				if (kind == WorldTraceKind::Enemy)
				{
					const auto* value = reinterpret_cast<const
						AAliceGameKynapsePawn_eventTakeDamage_Params*>(
							parameters);
					return "damage="
						+ std::to_string(value->DamageAmount)
						+ ", instigator="
						+ ObjectName(value->EventInstigator)
						+ ", causer="
						+ ObjectName(value->DamageCauser)
						+ ", hit="
						+ FormatWorldTraceVector(value->HitLocation);
				}
				if (kind == WorldTraceKind::Breakable)
				{
					const auto* value = reinterpret_cast<const
						AGameBreakableActor_eventTakeDamage_Params*>(
							parameters);
					return "damage=" + std::to_string(value->Damage)
						+ ", instigator="
						+ ObjectName(value->EventInstigator)
						+ ", causer="
						+ ObjectName(value->DamageCauser)
						+ ", hit="
						+ FormatWorldTraceVector(value->HitLocation);
				}
			}
			if (functionName == "TriggerStepDestroyedEvent"
				&& kind == WorldTraceKind::Breakable)
			{
				const auto* value = reinterpret_cast<const
					AGameBreakableActor_eventTriggerStepDestroyedEvent_Params*>(
						parameters);
				return "step=" + std::to_string(value->iBrokenStep)
					+ ", instigator="
					+ ObjectName(value->EventInstigator);
			}
			if (functionName == "PickedUpBy"
				&& (kind == WorldTraceKind::Pickup
					|| kind == WorldTraceKind::DroppedPickup))
			{
				APawn* pawn = *reinterpret_cast<APawn* const*>(parameters);
				return "pawn=" + ObjectName(pawn);
			}
			return "-";
		}

		bool IsWorldTraceFunction(WorldTraceKind kind,
			const std::string& functionName)
		{
			switch (kind)
			{
			case WorldTraceKind::Enemy:
				return functionName == "TakeDamage"
					|| functionName == "Died"
					|| functionName == "NotifyDied"
					|| functionName == "KilledBy"
					|| functionName == "DropPickups"
					|| functionName == "PostBeginPlay"
					|| functionName == "Destroyed";
			case WorldTraceKind::Breakable:
				return functionName == "TakeDamage"
					|| functionName == "TakeStepDamage"
					|| functionName == "TakeLastDamage"
					|| functionName == "BreakStepApart"
					|| functionName == "BreakLastApart"
					|| functionName == "TriggerStepDestroyedEvent"
					|| functionName == "DropPickups"
					|| functionName == "HideAndDestroy"
					|| functionName == "PostBeginPlay"
					|| functionName == "Destroyed";
			case WorldTraceKind::Pickup:
			case WorldTraceKind::DroppedPickup:
				return functionName == "PickedUpBy"
					|| functionName == "GiveTo"
					|| functionName == "SpawnCopyFor"
					|| functionName == "SetPickupHidden"
					|| functionName == "DisablePickup"
					|| functionName == "PostBeginPlay"
					|| functionName == "Destroyed";
			default:
				return false;
			}
		}

		bool IsPotentialWorldTraceFunction(UFunction* function)
		{
			const auto cached =
				g_worldTraceFunctionCache.find(function);
			if (cached != g_worldTraceFunctionCache.end())
				return cached->second;
			const std::string name = function->GetName();
			const bool relevant =
				name == "TakeDamage"
				|| name == "Died"
				|| name == "NotifyDied"
				|| name == "KilledBy"
				|| name == "TakeStepDamage"
				|| name == "TakeLastDamage"
				|| name == "BreakStepApart"
				|| name == "BreakLastApart"
				|| name == "TriggerStepDestroyedEvent"
				|| name == "DropPickups"
				|| name == "HideAndDestroy"
				|| name == "PickedUpBy"
				|| name == "GiveTo"
				|| name == "SpawnCopyFor"
				|| name == "SetPickupHidden"
				|| name == "DisablePickup"
				|| name == "PostBeginPlay"
				|| name == "Destroyed";
			g_worldTraceFunctionCache.emplace(function, relevant);
			return relevant;
		}

		std::string SharedWorldKeyText(std::uint64_t key)
		{
			std::ostringstream stream;
			stream << std::hex << std::setfill('0')
				<< std::setw(16) << key;
			return stream.str();
		}

		void QueueSharedWorldEvent(SharedWorldEventPayload event)
		{
			if (!g_config.sharedEnemyHealth || !g_connected
				|| g_currentMap.empty())
			{
				return;
			}
			event.eventSerial = ++g_sharedWorldEventSerial;
			if (event.eventSerial == 0)
				event.eventSerial = ++g_sharedWorldEventSerial;
			event.mapHash = HashMapName(g_currentMap);
			event.actorId =
				g_config.role == Role::Host ? 1u : 2u;
			event.clientTimeMs = ElapsedMilliseconds();
			std::lock_guard lock(g_stateMutex);
			if (g_outboundSharedWorldEvents.size() >= 256)
				g_outboundSharedWorldEvents.pop_front();
			g_outboundSharedWorldEvents.push_back(event);
		}

		AAliceGameKynapsePawn* FindSharedEnemy(
			std::uint64_t entityKey, AWorldInfo* world)
		{
			if (!entityKey || !world)
				return nullptr;
			const auto bound =
				g_clientSharedEnemyBindings.find(entityKey);
			if (bound != g_clientSharedEnemyBindings.end()
				&& bound->second.enemy
				&& bound->second.enemy->WorldInfo == world
				&& !bound->second.enemy->bDeleteMe
				&& !bound->second.enemy->bPendingDelete)
			{
				return bound->second.enemy;
			}
			const auto registered = g_sharedEnemyRegistry.find(entityKey);
			if (registered != g_sharedEnemyRegistry.end()
				&& registered->second
				&& registered->second->WorldInfo == world)
			{
				return registered->second;
			}
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return nullptr;
			const int32_t count = objects->size();
			for (int32_t index = 0; index < count; ++index)
			{
				UObject* object = objects->at(index);
				if (!object || object->IsDefaultObject()
					|| !object->IsA(
						AAliceGameKynapsePawn::StaticClass()))
				{
					continue;
				}
				auto* enemy =
					reinterpret_cast<AAliceGameKynapsePawn*>(object);
				if (enemy->WorldInfo == world
					&& SharedEnemyStableKey(enemy) == entityKey)
				{
					return enemy;
				}
			}
			return nullptr;
		}

		AGameBreakableActor* FindSharedBreakable(
			std::uint64_t entityKey, AWorldInfo* world)
		{
			if (!entityKey || !world)
				return nullptr;
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return nullptr;
			const int32_t count = objects->size();
			for (int32_t index = 0; index < count; ++index)
			{
				UObject* object = objects->at(index);
				if (!object || object->IsDefaultObject()
					|| !object->IsA(
						AGameBreakableActor::StaticClass()))
				{
					continue;
				}
				auto* breakable =
					reinterpret_cast<AGameBreakableActor*>(object);
				if (breakable->WorldInfo == world
					&& !breakable->bDeleteMe
					&& !breakable->bPendingDelete
					&& WorldTraceStableKeyValue(
						object, WorldTraceKind::Breakable)
						== entityKey)
				{
					return breakable;
				}
			}
			return nullptr;
		}

		void ApplySharedBreakableDestroy(
			AGameBreakableActor* breakable,
			const SharedWorldEventPayload& event,
			AAlicePawn* localPawn)
		{
			if (!breakable || breakable->bDestoryed)
				return;

			UClass* damageType = UDamageType::StaticClass();
			if (!breakable->DamageTypes.empty()
				&& breakable->DamageTypes.at(0))
			{
				damageType = breakable->DamageTypes.at(0);
			}
			FTraceHitInfo hitInfo{};
			hitInfo.Item = -1;
			hitInfo.LevelIndex = -1;
			hitInfo.BoneName = FName("None");
			const FVector zero(0.0f, 0.0f, 0.0f);
			g_applyingSharedBreakableDestroy = true;
			breakable->eventTakeDamage(
				1000000,
				g_State.AlicePlayerController,
				breakable->Location,
				zero,
				damageType,
				hitInfo,
				localPawn);
			g_applyingSharedBreakableDestroy = false;
			Log("SHAREDBREAKABLE APPLY key="
				+ SharedWorldKeyText(event.entityKey)
				+ ", object=" + ObjectName(breakable)
				+ ", destroyed="
				+ (breakable->bDestoryed
					? std::string("yes")
					: std::string("no"))
				+ ", hidden="
				+ (breakable->bHidden
					? std::string("yes.")
					: std::string("no.")));
		}

		void ResetSharedEnemyPose(bool restoreClientAi)
		{
			for (const auto& [controller, originalPause] :
				g_hostCutsceneControllerPause)
			{
				if (controller && IsLiveUObject(controller)
					&& !controller->bDeleteMe
					&& !controller->bPendingDelete)
				{
					controller->bPauseTick = originalPause;
				}
			}
			g_hostCutsceneControllerPause.clear();
			g_hostCutsceneEnemyAnchors.clear();
			if (g_hostCutsceneBlindWorld
				&& IsLiveUObject(g_hostCutsceneBlindWorld))
			{
				g_hostCutsceneBlindWorld->bNPCBlindOn =
					g_hostCutsceneOriginalNpcBlind;
			}
			g_hostCutsceneBlindWorld = nullptr;
			g_hostCutsceneOriginalNpcBlind = false;
			if (restoreClientAi)
			{
				for (const auto& [key, binding] :
					g_clientSharedEnemyBindings)
				{
					(void)key;
					AAliceGameKynapsePawn* enemy = binding.enemy;
					if (enemy && enemy->WorldInfo
							== g_sharedEnemyPoseWorld
						&& !enemy->bDeleteMe
						&& !enemy->bPendingDelete)
					{
						enemy->bPauseTick =
							binding.originalPauseTick;
						enemy->bHidden =
							binding.originalHidden;
						NativeSetActorHidden(
							enemy, binding.originalHidden);
					}
				}
			}
			g_clientSharedEnemyBindings.clear();
			g_clientEnemyHostKeyByActor.clear();
			g_hostAuthorizedEnemyKeys.clear();
			g_loggedUnauthorizedEnemyDamage.clear();
			for (const auto& [key, aggro] : g_hostEnemyAggro)
			{
				const auto found = g_sharedEnemyRegistry.find(key);
				if (aggro.controllerPauseCaptured
					&& found != g_sharedEnemyRegistry.end()
					&& found->second && found->second->Controller
					&& IsLiveUObject(found->second->Controller)
					&& found->second->Controller->IsA(
						AAliceGameKynapseAIController::
							StaticClass()))
				{
					reinterpret_cast<
						AAliceGameKynapseAIController*>(
							found->second->Controller)
						->bPauseTick =
							aggro.originalControllerPause;
				}
			}
			g_hostEnemyAggro.clear();
			g_nextHostEnemyAggroUpdate = {};
			g_clientEnemyAuthorityActive = false;
			g_clientQuarantinedEnemyCount = 0;
			g_clientOrphanedEncounterSince = {};
			g_nextHostCutsceneRecoveryAttempt = {};
			g_lastMissingHostCutsceneKey = 0;
			g_lastAutomaticCutsceneRecoveryKey = 0;
			g_hostAggroHostTargets = 0;
			g_hostAggroClientTargets = 0;
			g_sharedEnemyRegistry.clear();
			g_sharedEnemyStableKeys.clear();
			g_sharedEnemyPoseWorld = nullptr;
			g_sharedEnemyPoseMap.clear();
			g_nextSharedEnemyRegistryRefresh = {};
			g_lastSharedEnemyRegistryCount = -1;
			g_lastSharedEnemySnapshotCount = -1;
			g_nextSharedEnemyPoseSummary = {};
			g_sharedEnemyPoseMaxCorrection = 0.0f;
			g_sharedEnemyNativeMoves = 0;
			g_sharedEnemyNativeMoveFailures = 0;
		}

		bool IsSharedEnemyPoseEligible(
			AAliceGameKynapsePawn* enemy)
		{
			if (!enemy)
				return false;

			// The Dollmaker root pawn is a phase/Kismet coordinator rather
			// than the damageable hand currently being fought. Both games
			// create it locally at the same fixed origin for every phase.
			// Pausing its tick or assigning network aggro freezes only part of
			// the authored boss state and produces visual phase desync.
			return !enemy->IsA(
				AAliceGameDollMakerBossPawn::StaticClass());
		}

		void RefreshSharedEnemyRegistry(AWorldInfo* world)
		{
			if (!world)
				return;
			if (world != g_sharedEnemyPoseWorld
				|| g_currentMap != g_sharedEnemyPoseMap)
			{
				ResetSharedEnemyPose(false);
				g_sharedEnemyPoseWorld = world;
				g_sharedEnemyPoseMap = g_currentMap;
			}
			const auto now = Clock::now();
			if (now < g_nextSharedEnemyRegistryRefresh)
				return;
			g_nextSharedEnemyRegistryRefresh =
				now + std::chrono::milliseconds(250);

			std::unordered_map<std::uint64_t,
				AAliceGameKynapsePawn*> refreshed;
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return;
			const int32_t count = objects->size();
			for (int32_t index = 0; index < count; ++index)
			{
				UObject* object = objects->at(index);
				if (!object || object->IsDefaultObject()
					|| !object->IsA(
						AAliceGameKynapsePawn::StaticClass()))
				{
					continue;
				}
				auto* enemy =
					reinterpret_cast<AAliceGameKynapsePawn*>(object);
				if (enemy->WorldInfo != world
					|| enemy->bDeleteMe
					|| enemy->bPendingDelete
					|| !IsSharedEnemyPoseEligible(enemy))
				{
					continue;
				}
				const std::uint64_t key =
					SharedEnemyStableKey(enemy);
				if (key)
					refreshed.emplace(key, enemy);
			}
			g_sharedEnemyRegistry.swap(refreshed);
			const int registryCount =
				static_cast<int>(g_sharedEnemyRegistry.size());
			if (registryCount != g_lastSharedEnemyRegistryCount)
			{
				g_lastSharedEnemyRegistryCount = registryCount;
				Log("SHAREDPOSE registry role="
					+ std::string(g_config.role == Role::Host
						? "host" : "client")
					+ ", map=" + g_currentMap
					+ ", enemies="
					+ std::to_string(registryCount) + '.');
			}
		}

		void RegisterHostEnemyAggroDamage(
			std::uint64_t entityKey,
			std::uint32_t attackerActorId,
			std::int32_t damage)
		{
			if (g_config.role != Role::Host || !entityKey
				|| damage <= 0)
			{
				return;
			}
			const auto now = Clock::now();
			HostEnemyAggroState& aggro =
				g_hostEnemyAggro[entityKey];
			const float threat = 1.0f
				+ static_cast<float>(
					std::clamp(damage, 0, 100)) * 0.08f;
			if (attackerActorId == 2)
			{
				if (aggro.lastClientHit == Clock::time_point{}
					|| now - aggro.lastClientHit
						> std::chrono::milliseconds(2800))
				{
					aggro.clientHitCombo = 0;
				}
				++aggro.clientHitCombo;
				aggro.clientThreat += threat;
				aggro.lastClientHit = now;
				// A real player hit is stronger evidence than proxy LOS
				// (the presentation pawn intentionally has no collision).
				// One hit therefore acquires the attacker immediately.
				aggro.forceClientUntil =
					now + std::chrono::seconds(10);
			}
			else
			{
				if (aggro.lastHostHit == Clock::time_point{}
					|| now - aggro.lastHostHit
						> std::chrono::milliseconds(2800))
				{
					aggro.hostHitCombo = 0;
				}
				++aggro.hostHitCombo;
				aggro.hostThreat += threat;
				aggro.lastHostHit = now;
				aggro.forceHostUntil =
					now + std::chrono::seconds(10);
			}
			Log("AGGRO threat key=" + SharedWorldKeyText(entityKey)
				+ ", attacker="
				+ (attackerActorId == 2
					? std::string("client")
					: std::string("host"))
				+ ", damage=" + std::to_string(damage)
				+ ", combo="
				+ std::to_string(attackerActorId == 2
					? aggro.clientHitCombo
					: aggro.hostHitCombo)
				+ ", score="
				+ std::to_string(attackerActorId == 2
					? aggro.clientThreat
					: aggro.hostThreat) + '.');
		}

		bool NativeRetargetSphinxFollow(
			AAliceGameKynapseAIController* controller,
			AActor* target)
		{
			if (!controller || !target)
				return false;
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function AliceGame."
					"AliceGameKynapseAIController."
					"KynapseSphinxFollow");
			}
			if (!function
				|| (function->FunctionFlags & 0x400) == 0
				|| function->iNative == 0)
			{
				return false;
			}
			AAliceGameKynapseAIController_execKynapseSphinxFollow_Params
				parameters{};
			parameters.inActor = target;
			// Do not use the generated SDK wrapper: it clears FUNC_Native
			// and iNative in this build, turning this intrinsic into a no-op.
			controller->ProcessEvent(function, &parameters, nullptr);
			return true;
		}

		bool NativeStartKynapseFollow(
			AAliceGameKynapseAIController* controller,
			AActor* target, float duration, bool& accepted)
		{
			accepted = false;
			if (!controller || !target)
				return false;
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function Kynapse."
					"KynapseAIController.KynapseFollow");
			}
			if (!function
				|| (function->FunctionFlags & 0x400) == 0
				|| function->iNative == 0)
			{
				return false;
			}
			AKynapseAIController_execKynapseFollow_Params
				parameters{};
			parameters.TargetEntity = target;
			parameters.Duration = duration;
			controller->ProcessEvent(
				function, &parameters, nullptr);
			accepted = parameters.ReturnValue;
			return true;
		}

		void ApplyHostEnemyAggroTarget(
			AAliceGameKynapseAIController* controller,
			HostEnemyAggroState& aggro,
			AAlicePawn* hostPawn,
			AAlicePawn* clientProxy,
			bool targetClient)
		{
			if (!controller || !hostPawn)
				return;
			AAlicePawn* oldClientTarget = aggro.clientTarget;
			AActor* target = targetClient
				? static_cast<AActor*>(clientProxy)
				: static_cast<AActor*>(hostPawn);
			if (!target)
				return;
			auto isAliceTarget =
				[hostPawn, clientProxy, oldClientTarget](
					const AActor* actor)
				{
					return !actor || actor == hostPawn
						|| actor == clientProxy
						|| actor == oldClientTarget;
				};

			// Preserve temporary non-Alice goals such as Clock Bombs and
			// scripted navigation points. Every Alice-facing pointer is
			// redirected so both melee movement and ranged aiming get the
			// same stable target.
			if (isAliceTarget(controller->Enemy))
				controller->Enemy =
					reinterpret_cast<APawn*>(target);
			if (isAliceTarget(controller->Focus))
				controller->Focus = target;
			if (isAliceTarget(controller->MoveTarget))
				controller->MoveTarget = target;
			if (isAliceTarget(controller->ShotTarget))
				controller->ShotTarget =
					reinterpret_cast<APawn*>(target);
			if (isAliceTarget(controller->ScriptedFocus))
				controller->ScriptedFocus = target;
			if (isAliceTarget(controller->shootTarget))
				controller->shootTarget = target;
			if (isAliceTarget(controller->hideAndShootTarget))
				controller->hideAndShootTarget = target;
			if (isAliceTarget(controller->followTarget))
				controller->followTarget = target;
			if (isAliceTarget(controller->latentTarget))
				controller->latentTarget = target;
			aggro.clientTarget = clientProxy;
		}

		void TickHostEnemyAggro(
			AAlicePawn* hostPawn,
			AAlicePawn* clientProxy,
			AWorldInfo* world,
			bool peerOnSameMap)
		{
			if (g_config.role != Role::Host
				|| !g_config.sharedEnemyTransforms
				|| !hostPawn || !world)
			{
				return;
			}
			const auto now = Clock::now();
			if (now < g_nextHostEnemyAggroUpdate)
				return;
			g_nextHostEnemyAggroUpdate =
				now + std::chrono::milliseconds(200);
			RefreshSharedEnemyRegistry(world);

			const bool clientGenerallyEligible =
				peerOnSameMap && clientProxy
				&& IsLiveUObject(clientProxy)
				&& clientProxy->Health > 0
				&& !clientProxy->bDeleteMe
				&& !clientProxy->bPendingDelete
				&& g_remotePresentation.valid
				&& (g_remotePresentation.state.flags
					& StateVisible) != 0
				&& (g_remotePresentation.state.flags
					& StateCinematic) == 0
				&& g_waitingCutsceneAction == nullptr;
			if (clientProxy && g_config.sharedEnemyHealth)
			{
				// Damage is intercepted before AlicePawn.TakeDamage mutates
				// this presentation pawn and is forwarded to the real client.
				clientProxy->bCanBeDamaged =
					clientGenerallyEligible;
			}

			std::unordered_set<std::uint64_t> liveKeys;
			int hostTargets = 0;
			int clientTargets = 0;
			constexpr float AwarenessRadius = 3600.0f;
			constexpr float AwarenessRadiusSquared =
				AwarenessRadius * AwarenessRadius;
			constexpr float InitialDistributionRadius = 2400.0f;
			constexpr float InitialDistributionRadiusSquared =
				InitialDistributionRadius
					* InitialDistributionRadius;
			constexpr float ProvokedLeashRadius = 6000.0f;
			constexpr float ProvokedLeashRadiusSquared =
				ProvokedLeashRadius * ProvokedLeashRadius;
			for (const auto& [key, enemy] :
				g_sharedEnemyRegistry)
			{
				if (!enemy || enemy->Health <= 0
					|| enemy->bDeleteMe
					|| enemy->bPendingDelete
					|| !enemy->Controller
					|| !enemy->Controller->IsA(
						AAliceGameKynapseAIController::
							StaticClass()))
				{
					continue;
				}
				liveKeys.insert(key);
				auto* controller = reinterpret_cast<
					AAliceGameKynapseAIController*>(
						enemy->Controller);
				HostEnemyAggroState& aggro =
					g_hostEnemyAggro[key];
				if (aggro.lastThreatDecay
					== Clock::time_point{})
				{
					aggro.lastThreatDecay = now;
				}
				else if (now - aggro.lastThreatDecay
					>= std::chrono::seconds(1))
				{
					aggro.hostThreat *= 0.88f;
					aggro.clientThreat *= 0.88f;
					aggro.lastThreatDecay = now;
				}

				const float hostDistanceSquared =
					(enemy->Location - hostPawn->Location)
						.SizeSquared();
				float clientDistanceSquared =
					(std::numeric_limits<float>::max)();
				bool clientVisible = false;
				if (clientGenerallyEligible)
				{
					clientDistanceSquared =
						(enemy->Location
							- clientProxy->Location)
							.SizeSquared();
					if (clientDistanceSquared
						<= AwarenessRadiusSquared)
					{
						const bool controllerSight =
							controller->LineOfSightTo(
								clientProxy,
								FVector(0.0f, 0.0f, 0.0f),
								true);
						// Controller.LineOfSightTo requires the target to
						// participate in collision queries. The presentation
						// proxy intentionally does not, so also trace the
						// unobstructed world segment between the enemy and the
						// replicated player position.
						const bool clearWorldSegment =
							enemy->FastTrace(
								clientProxy->Location,
								enemy->Location,
								FVector(0.0f, 0.0f, 0.0f),
								false);
						clientVisible =
							controllerSight
								|| clearWorldSegment;
						if (clientVisible)
							aggro.lastClientVisible = now;
					}
				}
				const bool clientHitProvesAwareness =
					clientGenerallyEligible
					&& aggro.lastClientHit
						!= Clock::time_point{}
					&& now - aggro.lastClientHit
						<= std::chrono::seconds(10)
					&& clientDistanceSquared
						<= ProvokedLeashRadiusSquared;
				const bool clientVisibleWithGrace =
					clientGenerallyEligible
					&& ((clientDistanceSquared
							<= AwarenessRadiusSquared
						&& (clientVisible
						|| (aggro.lastClientVisible
								!= Clock::time_point{}
							&& now
								- aggro.lastClientVisible
								<= std::chrono::
									milliseconds(1500))))
						|| clientHitProvesAwareness);
				const bool clientWithinLeash =
					clientGenerallyEligible
					&& clientDistanceSquared
						<= ProvokedLeashRadiusSquared;
				const bool clientEligibleForInitialSplit =
					clientGenerallyEligible
					&& clientDistanceSquared
						<= InitialDistributionRadiusSquared;

				bool desiredClient = aggro.targetClient;
				const bool forcedClient =
					clientVisibleWithGrace
					&& now < aggro.forceClientUntil;
				const bool forcedHost =
					now < aggro.forceHostUntil;
				if (!clientWithinLeash)
				{
					desiredClient = false;
				}
				else if (forcedClient || forcedHost)
				{
					if (forcedClient && forcedHost)
					{
						desiredClient =
							aggro.lastClientHit
								> aggro.lastHostHit;
					}
					else
						desiredClient = forcedClient;
				}
				else if (!aggro.initialized)
				{
					// Stable two-in-five distribution prevents every enemy
					// in a wave from choosing the same Alice. It deliberately
					// uses proximity instead of proxy LOS: the visual proxy
					// has collision disabled, which makes native LOS
					// unreliable even on an open arena.
					std::uint64_t mixedKey = key;
					mixedKey ^= mixedKey >> 33;
					mixedKey *= 0xff51afd7ed558ccdull;
					mixedKey ^= mixedKey >> 33;
					const bool distributedClient =
						clientEligibleForInitialSplit
						&& (mixedKey % 5u) < 2u;
					const bool clearlyCloser =
						clientEligibleForInitialSplit
						&& clientDistanceSquared
							+ 700.0f * 700.0f
								< hostDistanceSquared;
					desiredClient =
						distributedClient || clearlyCloser;
				}
				else if (now >= aggro.lockedUntil)
				{
					const bool clientClearlyCloser =
						clientDistanceSquared + 900.0f * 900.0f
							< hostDistanceSquared;
					const bool hostClearlyCloser =
						hostDistanceSquared + 900.0f * 900.0f
							< clientDistanceSquared;
					if (clientClearlyCloser)
						desiredClient = true;
					else if (hostClearlyCloser
						&& now - aggro.lastClientHit
							> std::chrono::seconds(10))
						desiredClient = false;
				}

				const bool hadInitializedTarget =
					aggro.initialized;
				const bool targetChanged =
					!aggro.initialized
					|| desiredClient != aggro.targetClient;
				if (targetChanged)
				{
					aggro.initialized = true;
					aggro.targetClient = desiredClient;
					aggro.lockedUntil =
						now + std::chrono::seconds(4);
					Log("AGGRO target key="
						+ SharedWorldKeyText(key)
						+ ", enemy=" + ObjectName(enemy)
						+ ", target="
						+ (desiredClient
							? std::string("client")
							: std::string("host"))
						+ ", hostDist="
						+ std::to_string(static_cast<int>(
							std::sqrt(hostDistanceSquared)))
						+ ", clientDist="
						+ (clientGenerallyEligible
							? std::to_string(static_cast<int>(
								std::sqrt(
									clientDistanceSquared)))
							: std::string("-1"))
						+ ", los="
						+ (clientVisible
							? std::string("yes.")
							: std::string("no.")));
				}
				if (desiredClient)
				{
					if (!aggro.controllerPauseCaptured)
					{
						aggro.originalControllerPause =
							controller->bPauseTick != 0;
						aggro.controllerPauseCaptured = true;
					}
					controller->bPauseTick = true;
					aggro.clientTarget = clientProxy;
				}
				else
				{
					if (aggro.controllerPauseCaptured)
					{
						controller->bPauseTick =
							aggro.originalControllerPause;
						aggro.controllerPauseCaptured = false;
					}
					ApplyHostEnemyAggroTarget(
						controller, aggro, hostPawn,
						clientProxy, false);
					if (targetChanged && hadInitializedTarget)
					{
						NativeRetargetSphinxFollow(
							controller, hostPawn);
					}
				}
				if (desiredClient)
					++clientTargets;
				else
					++hostTargets;
			}
			for (auto it = g_hostEnemyAggro.begin();
				it != g_hostEnemyAggro.end();)
			{
				if (liveKeys.find(it->first) == liveKeys.end())
				{
					const auto enemy =
						g_sharedEnemyRegistry.find(it->first);
					if (it->second.controllerPauseCaptured
						&& enemy != g_sharedEnemyRegistry.end()
						&& enemy->second
						&& enemy->second->Controller
						&& IsLiveUObject(
							enemy->second->Controller)
						&& enemy->second->Controller->IsA(
							AAliceGameKynapseAIController::
								StaticClass()))
					{
						reinterpret_cast<
							AAliceGameKynapseAIController*>(
								enemy->second->Controller)
							->bPauseTick =
								it->second
									.originalControllerPause;
					}
					it = g_hostEnemyAggro.erase(it);
				}
				else
					++it;
			}
			g_hostAggroHostTargets = hostTargets;
			g_hostAggroClientTargets = clientTargets;
		}

		void ApplyClientEnemyAuthorityPoses(
			const ClientCommandPayload* command,
			AWorldInfo* world)
		{
			if (g_config.role != Role::Host
				|| !g_config.sharedEnemyTransforms
				|| !command || !world)
			{
				return;
			}
			const std::size_t poseCount =
				std::min<std::size_t>(
					command->enemyCount, MaxSharedEnemies);
			std::size_t applied = 0;
			for (std::size_t index = 0;
				index < poseCount; ++index)
			{
				const SharedEnemyPosePayload& pose =
					command->enemies[index];
				if ((pose.flags
						& SharedEnemyPoseClientAuthority) == 0)
				{
					continue;
				}
				const auto aggro =
					g_hostEnemyAggro.find(pose.entityKey);
				if (aggro == g_hostEnemyAggro.end()
					|| !aggro->second.initialized
					|| !aggro->second.targetClient)
				{
					continue;
				}
				const auto found =
					g_sharedEnemyRegistry.find(pose.entityKey);
				if (found == g_sharedEnemyRegistry.end())
					continue;
				AAliceGameKynapsePawn* enemy = found->second;
				if (!enemy || enemy->Health <= 0
					|| enemy->bDeleteMe
					|| enemy->bPendingDelete)
				{
					continue;
				}

				const FVector target(
					pose.location[0],
					pose.location[1],
					pose.location[2]);
				const FVector delta = target - enemy->Location;
				FVector next = target;
				if (delta.SizeSquared() < 2000.0f * 2000.0f)
				{
					const float alpha = std::clamp(
						world->DeltaSeconds * 18.0f,
						0.0f, 1.0f);
					next = enemy->Location + delta * alpha;
				}
				const FRotator rotation(
					pose.rotation[0],
					pose.rotation[1],
					pose.rotation[2]);
				NativeSetActorLocationNoCheck(enemy, next);
				NativeSetActorRotation(enemy, rotation);
				enemy->Location = next;
				enemy->Rotation = rotation;
				enemy->Velocity = FVector(
					pose.velocity[0],
					pose.velocity[1],
					pose.velocity[2]);
				enemy->Physics =
					static_cast<EPhysics>(pose.physics);
				enemy->NPCState =
					static_cast<ENPCState>(pose.npcState);
				enemy->HealthState =
					static_cast<EHealthState>(
						pose.healthState);
				enemy->AIState =
					static_cast<EAIState>(pose.aiState);
				enemy->bIsFighting =
					(pose.flags
						& SharedEnemyPoseFighting) != 0;
				++applied;
			}

			static Clock::time_point nextSummary{};
			if (Clock::now() >= nextSummary)
			{
				nextSummary =
					Clock::now() + std::chrono::seconds(2);
				Log("AGGRO delegated client poses received="
					+ std::to_string(poseCount)
					+ ", applied="
					+ std::to_string(applied) + '.');
			}
		}

		void UpdateHostCutsceneNpcBlindness(
			AWorldInfo* world, bool pause)
		{
			if (g_config.role != Role::Host
				|| !g_config.sharedEnemyTransforms || !world)
			{
				return;
			}
			if (pause)
			{
				if (g_hostCutsceneBlindWorld != world)
				{
					if (g_hostCutsceneBlindWorld
						&& IsLiveUObject(
							g_hostCutsceneBlindWorld))
					{
						g_hostCutsceneBlindWorld->bNPCBlindOn =
							g_hostCutsceneOriginalNpcBlind;
					}
					g_hostCutsceneBlindWorld = world;
					g_hostCutsceneOriginalNpcBlind =
						world->bNPCBlindOn != 0;
					Log("CUTSCENEBARRIER host NPC blindness enabled, "
						"original="
						+ std::string(
							g_hostCutsceneOriginalNpcBlind
								? "yes." : "no."));
				}
				world->bNPCBlindOn = true;
				// Pause only the decision-making controller. The pawn keeps
				// ticking, so Matinee movement, landing notifications and
				// encounter registration can still finish normally.
				g_nextSharedEnemyRegistryRefresh = {};
				RefreshSharedEnemyRegistry(world);
				for (const auto& [key, enemy] :
					g_sharedEnemyRegistry)
				{
					(void)key;
					if (!enemy || enemy->Health <= 0
						|| enemy->bDeleteMe
						|| enemy->bPendingDelete)
					{
						continue;
					}

					// Kynapse can keep executing a native movement task after
					// its controller has been blinded and paused. Hold the
					// pawn at the first position observed during the barrier,
					// but keep Pawn.Tick alive so encounter registration,
					// Matinee bindings and landing notifications are not
					// interrupted. This deliberately produces the same
					// harmless "running in place" presentation as the game's
					// own tutorial-pause path.
					const auto [anchorIt, inserted] =
						g_hostCutsceneEnemyAnchors.try_emplace(
							enemy, enemy->Location);
					if (inserted)
					{
						Log("CUTSCENEBARRIER anchored host enemy "
							+ ObjectName(enemy) + '.');
					}
					NativeSetActorLocationNoCheck(
						enemy, anchorIt->second);
					enemy->Location = anchorIt->second;
					enemy->Velocity =
						FVector(0.0f, 0.0f, 0.0f);

					if (!enemy->Controller
						|| !enemy->Controller->IsA(
							AAliceGameKynapseAIController::
								StaticClass()))
					{
						continue;
					}
					auto* controller = reinterpret_cast<
						AAliceGameKynapseAIController*>(
							enemy->Controller);
					g_hostCutsceneControllerPause.try_emplace(
						controller,
						controller->bPauseTick != 0);
					controller->bPauseTick = true;
				}
				return;
			}
			for (const auto& [controller, originalPause] :
				g_hostCutsceneControllerPause)
			{
				if (controller && IsLiveUObject(controller)
					&& !controller->bDeleteMe
					&& !controller->bPendingDelete)
				{
					controller->bPauseTick = originalPause;
				}
			}
			if (!g_hostCutsceneControllerPause.empty())
			{
				Log("CUTSCENEBARRIER restored host AI controllers, "
					"count="
					+ std::to_string(
						g_hostCutsceneControllerPause.size()) + '.');
			}
			g_hostCutsceneControllerPause.clear();
			if (!g_hostCutsceneEnemyAnchors.empty())
			{
				Log("CUTSCENEBARRIER released host enemy anchors, "
					"count="
					+ std::to_string(
						g_hostCutsceneEnemyAnchors.size()) + '.');
			}
			g_hostCutsceneEnemyAnchors.clear();
			if (g_hostCutsceneBlindWorld
				&& IsLiveUObject(g_hostCutsceneBlindWorld))
			{
				g_hostCutsceneBlindWorld->bNPCBlindOn =
					g_hostCutsceneOriginalNpcBlind;
				Log("CUTSCENEBARRIER host NPC blindness restored.");
			}
			g_hostCutsceneBlindWorld = nullptr;
			g_hostCutsceneOriginalNpcBlind = false;
		}

		void CaptureSharedEnemyPoses(
			AAlicePawn* localPawn, AWorldInfo* world,
			HostSnapshotPayload& snapshot)
		{
			if (!g_config.sharedEnemyTransforms
				|| g_config.role != Role::Host
				|| !localPawn || !world)
			{
				return;
			}
			RefreshSharedEnemyRegistry(world);
			struct Candidate
			{
				std::uint64_t key = 0;
				AAliceGameKynapsePawn* enemy = nullptr;
				float distanceSquared = 0.0f;
			};
			std::vector<Candidate> candidates;
			candidates.reserve(g_sharedEnemyRegistry.size());
			const float radiusSquared =
				g_config.sharedEnemyRadius
				* g_config.sharedEnemyRadius;
			for (const auto& [key, enemy] : g_sharedEnemyRegistry)
			{
				if (!enemy || enemy->Health <= 0
					|| enemy->bHidden
					|| enemy->bDeleteMe
					|| enemy->bPendingDelete)
				{
					continue;
				}
				const FVector delta =
					enemy->Location - localPawn->Location;
				const float distanceSquared = delta.SizeSquared();
				if (distanceSquared <= radiusSquared)
				{
					candidates.push_back(
						{ key, enemy, distanceSquared });
				}
			}
			std::sort(candidates.begin(), candidates.end(),
				[](const Candidate& left, const Candidate& right)
				{
					return left.distanceSquared
						< right.distanceSquared;
				});
			const std::size_t poseCount = (std::min)(
				candidates.size(), MaxSharedEnemies);
			snapshot.enemyCount =
				static_cast<std::uint8_t>(poseCount);
			for (std::size_t index = 0; index < poseCount; ++index)
			{
				const Candidate& candidate = candidates[index];
				const AAliceGameKynapsePawn* enemy =
					candidate.enemy;
				SharedEnemyPosePayload& pose =
					snapshot.enemies[index];
				pose.entityKey = candidate.key;
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
				pose.flags =
					(enemy->bHidden
						? SharedEnemyPoseHidden : 0)
					| (enemy->bIsFighting
						? SharedEnemyPoseFighting : 0);
				pose.flags |= static_cast<std::uint32_t>(
					SharedEnemyClassSignature(enemy))
					<< static_cast<unsigned>(SharedEnemyPoseClassShift);
				const auto aggro =
					g_hostEnemyAggro.find(candidate.key);
				if (aggro != g_hostEnemyAggro.end()
					&& aggro->second.initialized
					&& aggro->second.targetClient)
				{
					pose.flags |=
						SharedEnemyPoseClientAuthority;
				}
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
			if (static_cast<int>(poseCount)
				!= g_lastSharedEnemySnapshotCount)
			{
				g_lastSharedEnemySnapshotCount =
					static_cast<int>(poseCount);
				Log("SHAREDPOSE host snapshot enemies="
					+ std::to_string(poseCount)
					+ ", candidates="
					+ std::to_string(candidates.size()) + '.');
			}
		}

		void ApplySharedEnemyPoses(
			const HostSnapshotPayload* snapshot,
			AWorldInfo* world)
		{
			if (!g_config.sharedEnemyTransforms
				|| g_config.role != Role::Client
				|| !world)
			{
				g_clientEnemyAuthorityActive = false;
				if (!g_clientSharedEnemyBindings.empty())
					ResetSharedEnemyPose(true);
				return;
			}
			g_clientEnemyAuthorityActive = snapshot != nullptr;
			RefreshSharedEnemyRegistry(world);
			const auto now = Clock::now();
			const std::size_t poseCount = snapshot
				? std::min<std::size_t>(
					snapshot->enemyCount, MaxSharedEnemies)
				: 0;
			g_hostAuthorizedEnemyKeys.clear();
			g_clientEnemyHostKeyByActor.clear();
			for (std::size_t index = 0; index < poseCount; ++index)
			{
				const SharedEnemyPosePayload& pose =
					snapshot->enemies[index];
				g_hostAuthorizedEnemyKeys.insert(pose.entityKey);
				const std::uint16_t classSignature =
					static_cast<std::uint16_t>((pose.flags
						& SharedEnemyPoseClassMask)
						>> SharedEnemyPoseClassShift);
				AAliceGameKynapsePawn* enemy = nullptr;
				std::uint64_t localKey = 0;
				const auto priorBinding =
					g_clientSharedEnemyBindings.find(pose.entityKey);
				if (priorBinding != g_clientSharedEnemyBindings.end()
					&& priorBinding->second.enemy
					&& priorBinding->second.enemy->WorldInfo == world
					&& !priorBinding->second.enemy->bDeleteMe
					&& !priorBinding->second.enemy->bPendingDelete
					&& g_clientEnemyHostKeyByActor.find(
						priorBinding->second.enemy)
						== g_clientEnemyHostKeyByActor.end()
					&& (classSignature == 0
						|| SharedEnemyClassSignature(
							priorBinding->second.enemy)
							== classSignature))
				{
					enemy = priorBinding->second.enemy;
				}
				const auto found =
					g_sharedEnemyRegistry.find(pose.entityKey);
				if (!enemy && found != g_sharedEnemyRegistry.end()
					&& found->second
					&& g_clientEnemyHostKeyByActor.find(found->second)
						== g_clientEnemyHostKeyByActor.end())
				{
					enemy = found->second;
					localKey = found->first;
				}
				if (!enemy && classSignature != 0)
				{
					const FVector hostPosition(
						pose.location[0], pose.location[1],
						pose.location[2]);
					constexpr float MaximumFallbackDistance = 6000.0f;
					float bestDistanceSquared = MaximumFallbackDistance
						* MaximumFallbackDistance;
					for (const auto& [candidateKey, candidate] :
						g_sharedEnemyRegistry)
					{
						if (!candidate || candidate->Health <= 0
							|| candidate->bDeleteMe
							|| candidate->bPendingDelete
							|| g_clientEnemyHostKeyByActor.find(candidate)
								!= g_clientEnemyHostKeyByActor.end()
							|| SharedEnemyClassSignature(candidate)
								!= classSignature)
						{
							continue;
						}
						const float distanceSquared =
							(candidate->Location - hostPosition)
								.SizeSquared();
						if (distanceSquared < bestDistanceSquared)
						{
							bestDistanceSquared = distanceSquared;
							enemy = candidate;
							localKey = candidateKey;
						}
					}
				}
				if (!enemy || enemy->Health <= 0
					|| enemy->bDeleteMe
					|| enemy->bPendingDelete)
				{
					continue;
				}
				if (localKey == 0)
				{
					for (const auto& [candidateKey, candidate] :
						g_sharedEnemyRegistry)
					{
						if (candidate == enemy)
						{
							localKey = candidateKey;
							break;
						}
					}
				}
				if (localKey)
					g_hostAuthorizedEnemyKeys.insert(localKey);
				g_clientEnemyHostKeyByActor[enemy] = pose.entityKey;

				auto [bindingIt, inserted] =
					g_clientSharedEnemyBindings.try_emplace(
						pose.entityKey);
				SharedEnemyPoseBinding& binding =
					bindingIt->second;
				if (inserted || binding.enemy != enemy)
				{
					binding.enemy = enemy;
					binding.originalPauseTick =
						enemy->bPauseTick != 0;
					binding.originalHidden =
						enemy->bHidden != 0;
					Log("SHAREDPOSE client bind key="
						+ SharedWorldKeyText(pose.entityKey)
						+ (localKey != 0
							&& localKey != pose.entityKey
							? ", fallbackLocalKey="
								+ SharedWorldKeyText(localKey)
							: std::string())
						+ ", object=" + ObjectName(enemy)
						+ ", originalPause="
						+ (binding.originalPauseTick
							? "yes." : "no."));
				}
				binding.lastSeen = now;
				binding.authorized = true;
				binding.clientAuthority =
					(pose.flags
						& SharedEnemyPoseClientAuthority) != 0;
				if (binding.clientAuthority)
				{
					// On this machine the real local Alice is a valid
					// Kynapse target. Let the game's own AI navigate and
					// attack it, then return the resulting pawn pose to the
					// host in the next ClientCommand packet.
					enemy->bPauseTick =
						binding.originalPauseTick;
					enemy->bHidden = false;
					NativeSetActorHidden(enemy, false);
					continue;
				}
				enemy->bPauseTick = true;
				enemy->bHidden = false;
				NativeSetActorHidden(enemy, false);

				const FVector target(
					pose.location[0],
					pose.location[1],
					pose.location[2]);
				const FVector delta = target - enemy->Location;
				const float correction = std::sqrt(
					delta.SizeSquared());
				g_sharedEnemyPoseMaxCorrection = (std::max)(
					g_sharedEnemyPoseMaxCorrection,
					correction);
				FVector next = target;
				if (delta.SizeSquared()
					< g_config.sharedEnemySnapDistance
						* g_config.sharedEnemySnapDistance)
				{
					const float alpha = std::clamp(
						world->DeltaSeconds
							* g_config.sharedEnemyCorrectionSpeed,
						0.0f, 1.0f);
					next = enemy->Location + delta * alpha;
				}
				const FRotator rotation(
					pose.rotation[0],
					pose.rotation[1],
					pose.rotation[2]);
				if (NativeSetActorLocationNoCheck(enemy, next))
					++g_sharedEnemyNativeMoves;
				else
					++g_sharedEnemyNativeMoveFailures;
				NativeSetActorRotation(enemy, rotation);
				// Keep a direct fallback as well: unlike presentation
				// actors, NPCs can have native movement rejected by their
				// navigation/collision state.
				enemy->Location = next;
				enemy->Rotation = rotation;
				if (g_sharedEnemyNativeMoveFailures != 0)
					NativeForceUpdateComponents(
						enemy, false, true);
				enemy->Velocity = FVector(
					pose.velocity[0],
					pose.velocity[1],
					pose.velocity[2]);
				enemy->Physics =
					static_cast<EPhysics>(pose.physics);
				enemy->NPCState =
					static_cast<ENPCState>(pose.npcState);
				enemy->HealthState =
					static_cast<EHealthState>(
						pose.healthState);
				enemy->AIState =
					static_cast<EAIState>(pose.aiState);
				enemy->bIsFighting =
					(pose.flags
						& SharedEnemyPoseFighting) != 0;
			}

			// A client can fire the same encounter trigger before the host.
			// Quarantine such premature local enemies instead of letting them
			// run a second fight and queue damage for actors the host has not
			// spawned yet. They are restored as soon as the host advertises
			// the same stable key.
			std::size_t quarantinedCount = 0;
			if (snapshot)
			{
				const FVector hostLocation(
					snapshot->hostState.location[0],
					snapshot->hostState.location[1],
					snapshot->hostState.location[2]);
				const float radiusSquared =
					g_config.sharedEnemyRadius
						* g_config.sharedEnemyRadius;
				for (const auto& [key, enemy] :
					g_sharedEnemyRegistry)
				{
					if (!enemy || enemy->Health <= 0
						|| enemy->bDeleteMe
						|| enemy->bPendingDelete
						|| g_hostAuthorizedEnemyKeys.find(key)
							!= g_hostAuthorizedEnemyKeys.end())
					{
						continue;
					}
					const FVector delta =
						enemy->Location - hostLocation;
					if (delta.SizeSquared() > radiusSquared)
						continue;
					auto bindingFound =
						g_clientSharedEnemyBindings.find(key);
					if (bindingFound
						== g_clientSharedEnemyBindings.end())
					{
						SharedEnemyPoseBinding binding{};
						binding.enemy = enemy;
						binding.originalPauseTick =
							enemy->bPauseTick != 0;
						binding.originalHidden =
							enemy->bHidden != 0;
						bindingFound =
							g_clientSharedEnemyBindings.emplace(
								key, binding).first;
						Log("SHAREDPOSE client quarantine key="
							+ SharedWorldKeyText(key)
							+ ", object="
							+ ObjectName(enemy) + '.');
					}
					SharedEnemyPoseBinding& binding =
						bindingFound->second;
					if (binding.enemy != enemy)
					{
						binding.enemy = enemy;
						binding.originalPauseTick =
							enemy->bPauseTick != 0;
						binding.originalHidden =
							enemy->bHidden != 0;
					}
					binding.authorized = false;
					binding.clientAuthority = false;
					binding.lastSeen = now;
					enemy->bPauseTick = true;
					enemy->bHidden = true;
					NativeSetActorHidden(enemy, true);
					++quarantinedCount;
				}
			}

			for (auto it = g_clientSharedEnemyBindings.begin();
				it != g_clientSharedEnemyBindings.end();)
			{
				if (now - it->second.lastSeen
					<= std::chrono::milliseconds(750))
				{
					++it;
					continue;
				}
				AAliceGameKynapsePawn* released = it->second.enemy;
				if (released && IsLiveUObject(released)
					&& released->WorldInfo == world
					&& !released->bDeleteMe
					&& !released->bPendingDelete)
				{
					released->bPauseTick =
						it->second.originalPauseTick;
					released->bHidden =
						it->second.originalHidden;
					NativeSetActorHidden(
						released,
						it->second.originalHidden);
				}
				Log("SHAREDPOSE client release key="
					+ SharedWorldKeyText(it->first) + '.');
				it = g_clientSharedEnemyBindings.erase(it);
			}

			g_clientQuarantinedEnemyCount = quarantinedCount;
			if (snapshot && poseCount == 0
				&& quarantinedCount > 0)
			{
				if (g_clientOrphanedEncounterSince
					== Clock::time_point{})
				{
					g_clientOrphanedEncounterSince = now;
					Log("SHAREDPOSE client orphaned encounter "
						"detected; waiting for host progression "
						"cutscene.");
				}
			}
			else
			{
				g_clientOrphanedEncounterSince = {};
				g_lastMissingHostCutsceneKey = 0;
				g_lastAutomaticCutsceneRecoveryKey = 0;
			}

			if (now >= g_nextSharedEnemyPoseSummary)
			{
				g_nextSharedEnemyPoseSummary =
					now + std::chrono::seconds(2);
				Log("SHAREDPOSE client active="
					+ std::to_string(
						g_clientSharedEnemyBindings.size())
					+ ", received="
					+ std::to_string(poseCount)
					+ ", quarantined="
					+ std::to_string(quarantinedCount)
					+ ", maxCorrection="
					+ std::to_string(
						static_cast<int>(
							std::lround(
								g_sharedEnemyPoseMaxCorrection)))
					+ ", nativeMoves="
					+ std::to_string(g_sharedEnemyNativeMoves)
					+ ", nativeMoveFailures="
					+ std::to_string(
						g_sharedEnemyNativeMoveFailures)
					+ '.');
				g_sharedEnemyPoseMaxCorrection = 0.0f;
				g_sharedEnemyNativeMoves = 0;
				g_sharedEnemyNativeMoveFailures = 0;
			}
		}

		UClass* ResolveSharedDamageType(
			const SharedWorldEventPayload& event)
		{
			const std::string name(event.damageType,
				strnlen_s(event.damageType,
					sizeof(event.damageType)));
			UClass* resolved =
				name.empty() || name == "<null>"
					? nullptr
					: UObject::FindClass(name);
			if (!resolved)
			{
				resolved = UDamageType::StaticClass();
				Log("SHAREDWORLD damage type fallback source="
					+ (name.empty() ? std::string("<empty>") : name)
					+ ", resolved="
					+ ObjectName(resolved) + '.');
			}
			return resolved;
		}

		bool IsCurrentProcessForeground()
		{
			const HWND foregroundWindow = GetForegroundWindow();
			if (!foregroundWindow)
				return true;
			DWORD foregroundProcessId = 0;
			GetWindowThreadProcessId(
				foregroundWindow, &foregroundProcessId);
			return foregroundProcessId == GetCurrentProcessId();
		}

		bool ShouldGuardBackgroundPlayerDamage()
		{
			return g_config.backgroundWindowDamageGuard
				&& !IsCurrentProcessForeground();
		}

		void LogBackgroundDamageGuard(
			const char* source, int damage)
		{
			const auto now = Clock::now();
			if (now < g_nextBackgroundDamageGuardLog)
				return;
			g_nextBackgroundDamageGuardLog =
				now + std::chrono::seconds(2);
			Log("PLAYERDAMAGE background-window guard source="
				+ std::string(source)
				+ ", suppressed=" + std::to_string(damage) + '.');
		}

		void ApplySharedPlayerDamage(
			AAlicePawn* localPawn,
			AWorldInfo* world,
			const SharedWorldEventPayload& event)
		{
			if (g_config.role != Role::Client || !localPawn
				|| !world || event.damage <= 0
				|| localPawn->Health <= 0)
			{
				return;
			}
			if (ShouldGuardBackgroundPlayerDamage())
			{
				LogBackgroundDamageGuard("network", event.damage);
				return;
			}
			AAliceGameKynapsePawn* enemy =
				FindSharedEnemy(event.entityKey, world);
			AController* instigator =
				enemy ? enemy->Controller : nullptr;
			const FVector hitLocation = localPawn->Location;
			const FVector momentum(
				event.momentum[0],
				event.momentum[1],
				event.momentum[2]);
			FTraceHitInfo hitInfo{};
			hitInfo.Item = -1;
			hitInfo.LevelIndex = -1;
			hitInfo.BoneName = FName("None");
			hitInfo.HitComponent = localPawn->Mesh;
			g_applyingSharedPlayerDamage = true;
			localPawn->eventTakeDamage(
				event.damage,
				instigator,
				hitLocation,
				momentum,
				ResolveSharedDamageType(event),
				hitInfo,
				enemy);
			g_applyingSharedPlayerDamage = false;
			Log("PLAYERDAMAGE APPLY target=client, key="
				+ SharedWorldKeyText(event.entityKey)
				+ ", enemy=" + ObjectName(enemy)
				+ ", damage=" + std::to_string(event.damage)
				+ ", health=" + std::to_string(localPawn->Health)
				+ '/' + std::to_string(localPawn->HealthMax)
				+ '.');
		}

		void ApplySharedEnemyDamage(
			AAliceGameKynapsePawn* enemy,
			const SharedWorldEventPayload& event,
			AAlicePawn* localPawn)
		{
			if (!enemy || event.damage <= 0)
				return;
			// The two local AI simulations may place the matched enemy at
			// different coordinates. Apply hit reactions at the receiving
			// instance instead of replaying a foreign world-space point.
			const FVector hitLocation = enemy->Location;
			const FVector momentum(
				event.momentum[0],
				event.momentum[1],
				event.momentum[2]);
			FTraceHitInfo hitInfo{};
			hitInfo.Item = -1;
			hitInfo.LevelIndex = -1;
			// SDK FName() uses -1, while UE3 expects the valid NAME_None
			// table entry. A damage branch stringifies BoneName and crashes
			// in the global name table when the default -1 leaks through.
			hitInfo.BoneName = FName("None");
			hitInfo.PhysMaterial = enemy->PhysMaterial;
			hitInfo.HitComponent = enemy->Mesh;
			AController* eventInstigator =
				g_State.AlicePlayerController;
			AActor* damageCauser = localPawn;
			const bool remoteClientHitOnHost =
				g_config.role == Role::Host
				&& event.originActorId == 2
				&& g_remoteController
				&& g_remotePawn
				&& IsLiveUObject(g_remoteController)
				&& IsLiveUObject(g_remotePawn)
				&& g_remoteController->Pawn == g_remotePawn
				&& g_remotePawn->WorldInfo == enemy->WorldInfo;
			if (remoteClientHitOnHost)
			{
				// Preserve the real attacker identity when replaying a
				// client hit on the authoritative host. Passing the host
				// player controller here makes NotifyTakeHit /
				// KynapseTakeDamager refresh the native Sphinx fight task
				// with the host pawn, undoing every proxy target pointer we
				// set immediately before it.
				eventInstigator = g_remoteController;
				damageCauser = g_remotePawn;
			}
			g_applyingSharedEnemyDamage = true;
			enemy->eventTakeDamage(
				event.damage,
				eventInstigator,
				hitLocation,
				momentum,
				ResolveSharedDamageType(event),
				hitInfo,
				damageCauser);
			g_applyingSharedEnemyDamage = false;
			if (remoteClientHitOnHost)
			{
				Log("AGGRO native damage source key="
					+ SharedWorldKeyText(event.entityKey)
					+ ", enemy=" + ObjectName(enemy)
					+ ", instigator="
					+ ObjectName(eventInstigator)
					+ ", instigatorPawn="
					+ ObjectName(eventInstigator->Pawn)
					+ ", damageCauser="
					+ ObjectName(damageCauser)
					+ ", isPlayer="
					+ (eventInstigator->bIsPlayer
						? std::string("yes.")
						: std::string("no.")));
			}
		}

		void QueueAuthoritativeEnemyState(
			AAliceGameKynapsePawn* enemy,
			std::uint64_t entityKey,
			const SharedWorldEventPayload& source,
			std::uint32_t originActorId)
		{
			if (!enemy || g_config.role != Role::Host)
				return;
			SharedWorldEventPayload state = source;
			state.kind =
				SharedWorldEventKind::EnemyAuthoritativeState;
			state.entityKey = entityKey;
			state.originActorId = originActorId;
			state.health = enemy->Health;
			state.healthMax = enemy->HealthMax;
			state.flags = enemy->Health <= 0
				|| enemy->bDeleteMe
				|| enemy->bPendingDelete
				? SharedWorldEnemyDead : 0;
			QueueSharedWorldEvent(state);
			Log("SHAREDWORLD TX state key="
				+ SharedWorldKeyText(entityKey)
				+ ", origin="
				+ std::to_string(originActorId)
				+ ", damage=" + std::to_string(state.damage)
				+ ", health=" + std::to_string(state.health)
				+ '/' + std::to_string(state.healthMax)
				+ ", dead="
				+ ((state.flags & SharedWorldEnemyDead)
					? "yes." : "no."));
		}

		bool RememberSharedWorldEvent(
			const SharedWorldEventPayload& event)
		{
			const std::uint64_t identity =
				(static_cast<std::uint64_t>(event.actorId) << 32)
				| event.eventSerial;
			if (!g_seenSharedWorldEvents.insert(identity).second)
				return false;
			g_seenSharedWorldEventOrder.push_back(identity);
			while (g_seenSharedWorldEventOrder.size() > 512)
			{
				g_seenSharedWorldEvents.erase(
					g_seenSharedWorldEventOrder.front());
				g_seenSharedWorldEventOrder.pop_front();
			}
			return true;
		}

		void ReconcileAuthoritativeEnemyState(
			AAliceGameKynapsePawn* enemy,
			const SharedWorldEventPayload& event,
			AAlicePawn* localPawn)
		{
			if (!enemy)
				return;
			const bool authoritativeDead =
				(event.flags & SharedWorldEnemyDead) != 0
				|| event.health <= 0;
			if (event.originActorId == 1
				&& !enemy->bDeleteMe
				&& enemy->Health > 0)
			{
				ApplySharedEnemyDamage(enemy, event, localPawn);
			}
			if (authoritativeDead)
			{
				if (!enemy->bDeleteMe && enemy->Health > 0)
				{
					SharedWorldEventPayload lethal = event;
					lethal.damage =
						enemy->Health + enemy->HealthMax + 1;
					ApplySharedEnemyDamage(
						enemy, lethal, localPawn);
				}
			}
			else if (!enemy->bDeleteMe)
			{
				enemy->Health = std::clamp(
					event.health, 1, enemy->HealthMax);
			}
			Log("SHAREDWORLD APPLY state key="
				+ SharedWorldKeyText(event.entityKey)
				+ ", origin="
				+ std::to_string(event.originActorId)
				+ ", authoritativeHealth="
				+ std::to_string(event.health)
				+ '/' + std::to_string(event.healthMax)
				+ ", localHealth="
				+ std::to_string(enemy->Health)
				+ ", dead="
				+ (authoritativeDead ? "yes." : "no."));
		}

		void ProcessSharedDamageRequest(
			AAliceGameKynapsePawn* enemy,
			const SharedWorldEventPayload& event,
			AAlicePawn* localPawn)
		{
			if (!enemy)
				return;
			RegisterHostEnemyAggroDamage(
				event.entityKey, 2, event.damage);
			if (!enemy->bDeleteMe && enemy->Health > 0)
				ApplySharedEnemyDamage(enemy, event, localPawn);
			QueueAuthoritativeEnemyState(
				enemy, event.entityKey, event, 2);
		}

		std::deque<ReceivedSharedWorldEvent>
			DrainInboundSharedWorldEvents()
		{
			std::deque<ReceivedSharedWorldEvent> events;
			std::lock_guard lock(g_stateMutex);
			events.swap(g_inboundSharedWorldEvents);
			return events;
		}

		void ApplyDeferredSharedWorldEvents(
			AAlicePawn* localPawn, AWorldInfo* world)
		{
			const auto now = Clock::now();
			if (now < g_nextDeferredSharedWorldApply)
				return;
			g_nextDeferredSharedWorldApply =
				now + std::chrono::milliseconds(250);
			if (g_config.role == Role::Host)
			{
				for (auto it =
						g_deferredSharedDamageRequests.begin();
					it != g_deferredSharedDamageRequests.end();)
				{
					AAliceGameKynapsePawn* enemy =
						FindSharedEnemy(it->first, world);
					if (!enemy)
					{
						++it;
						continue;
					}
					for (const auto& event : it->second)
					{
						ProcessSharedDamageRequest(
							enemy, event, localPawn);
					}
					Log("SHAREDWORLD resolved deferred damage key="
						+ SharedWorldKeyText(it->first)
						+ ", count="
						+ std::to_string(it->second.size()) + '.');
					it = g_deferredSharedDamageRequests.erase(it);
				}
			}
			else
			{
				for (auto it =
						g_pendingAuthoritativeEnemyStates.begin();
					it != g_pendingAuthoritativeEnemyStates.end();)
				{
					AAliceGameKynapsePawn* enemy =
						FindSharedEnemy(it->first, world);
					if (!enemy)
					{
						++it;
						continue;
					}
					ReconcileAuthoritativeEnemyState(
						enemy, it->second, localPawn);
					Log("SHAREDWORLD resolved deferred state key="
						+ SharedWorldKeyText(it->first) + '.');
					it =
						g_pendingAuthoritativeEnemyStates.erase(it);
				}
			}
		}

		void ApplyInboundSharedWorldEvents(
			AAlicePawn* localPawn, AWorldInfo* world)
		{
			if (!g_config.sharedEnemyHealth
				|| !localPawn || !world)
			{
				return;
			}
			for (const ReceivedSharedWorldEvent& received :
				DrainInboundSharedWorldEvents())
			{
				const SharedWorldEventPayload& event =
					received.event;
				if (event.mapHash != HashMapName(g_currentMap)
					|| !RememberSharedWorldEvent(event))
				{
					continue;
				}
				if (event.kind
					== SharedWorldEventKind::
						InteractionContextActorStarted)
				{
					g_interactionCutsceneBypassUntil =
						Clock::now() + std::chrono::seconds(30);
					AContextActor* contextActor =
						FindContextActor(event.entityKey);
					AAlicePlayerController* controller =
						g_State.AlicePlayerController;
					if (contextActor && controller)
					{
						if (g_remoteContextInteraction.active)
						{
							ResetRemoteContextInteraction(
								"superseded");
						}
						AAlicePawn* aliceBefore =
							contextActor->Alice;
						AAlicePlayerController* controllerBefore =
							contextActor->APC;
						const bool startedBefore =
							contextActor->bContextActionStarted;
						const bool inAreaBefore =
							contextActor->bInTriggerArea;
						g_remoteContextInteraction.actor =
							contextActor;
						g_remoteContextInteraction.actorKey =
							event.entityKey;
						g_remoteContextInteraction.active = true;
						g_remoteContextInteraction.deadline =
							Clock::now()
							+ std::chrono::seconds(30);
						Log("CONTEXTACTOR queued cinematic handoff key="
							+ SharedWorldKeyText(event.entityKey)
							+ ", actor="
							+ ObjectName(contextActor)
							+ ", alice="
							+ ObjectName(aliceBefore)
							+ "->"
							+ ObjectName(contextActor->Alice)
							+ ", controller="
							+ ObjectName(controllerBefore)
							+ "->"
							+ ObjectName(contextActor->APC)
							+ ", started="
							+ (startedBefore
								? std::string("yes")
								: std::string("no"))
							+ "->"
							+ (contextActor->bContextActionStarted
								? std::string("yes")
								: std::string("no"))
							+ ", inArea="
							+ (inAreaBefore
								? std::string("yes")
								: std::string("no"))
							+ "->"
							+ (contextActor->bInTriggerArea
								? std::string("yes.")
								: std::string("no.")));
					}
					else
					{
						Log("CONTEXTACTOR unresolved key="
							+ SharedWorldKeyText(event.entityKey)
							+ ", controller="
							+ ObjectName(controller) + '.');
					}
					continue;
				}
				if (event.kind
					== SharedWorldEventKind::
						InteractionContextActionActivated)
				{
					g_interactionCutsceneBypassUntil =
						Clock::now() + std::chrono::seconds(30);
					USeqEvent_ContextActionActivated* contextEvent =
						FindContextActionEvent(event.entityKey);
					if (contextEvent)
					{
						const int triggerCountBefore =
							contextEvent->TriggerCount;
						const bool enabledBefore =
							contextEvent->bEnabled;
						const bool registeredBefore =
							contextEvent->bRegistered;
						const int outputCount =
							contextEvent->OutputLinks.size();
						TArray<int32_t> activateIndices;
						AActor* originator =
							g_remoteContextInteraction.active
								&& g_remoteContextInteraction.actor
								? g_remoteContextInteraction.actor
								: contextEvent->Originator
								? contextEvent->Originator
								: localPawn;
						g_applyingSharedContextAction = true;
						contextEvent->bEnabled = true;
						const bool activated =
							NativeCheckSequenceEvent(
								contextEvent,
								originator,
								localPawn,
								false,
								true,
								activateIndices);
						bool outputActivated = false;
						if (!activated && outputCount > 0)
						{
							contextEvent->Originator = originator;
							contextEvent->Instigator = localPawn;
							outputActivated =
								NativeActivateOutputLink(
									contextEvent, 0);
						}
						g_applyingSharedContextAction = false;
						if (g_remoteContextInteraction.active
							&& (activated || outputActivated))
						{
							g_remoteContextInteraction
								.localContextActivated = true;
							g_remoteContextInteraction
								.disableActorAfterActivation =
								outputActivated
								|| (g_remoteContextInteraction.actor
									&& g_remoteContextInteraction.actor
										->MaxTriggerTimers > 0)
								|| (contextEvent->MaxTriggerCount > 0
									&& contextEvent->TriggerCount
										>= contextEvent
											->MaxTriggerCount);
							ConsumeRemoteContextActor();
						}
						Log("CONTEXTINTERACT replayed key="
							+ SharedWorldKeyText(event.entityKey)
							+ ", event="
							+ ObjectName(contextEvent)
							+ ", originator="
							+ ObjectName(originator)
							+ ", instigator="
							+ ObjectName(localPawn)
							+ ", activated="
							+ (activated
								? std::string("yes")
								: std::string("no"))
							+ ", outputActivated="
							+ (outputActivated
								? std::string("yes")
								: std::string("no"))
							+ ", triggers="
							+ std::to_string(triggerCountBefore)
							+ "->"
							+ std::to_string(
								contextEvent->TriggerCount)
							+ ", active="
							+ (contextEvent->bActive
								? std::string("yes")
								: std::string("no"))
							+ ", enabled="
							+ (enabledBefore
								? std::string("yes")
								: std::string("no"))
							+ "->"
							+ (contextEvent->bEnabled
								? std::string("yes")
								: std::string("no"))
							+ ", registered="
							+ (registeredBefore
								? std::string("yes")
								: std::string("no"))
							+ ", outputs="
							+ std::to_string(outputCount)
							+ ", output0Links="
							+ std::to_string(outputCount > 0
								? contextEvent->OutputLinks
									.at(0).Links.size()
								: 0)
							+ ", indices="
							+ std::to_string(
								activateIndices.size()) + '.');
					}
					else
					{
						Log("CONTEXTINTERACT unresolved key="
							+ SharedWorldKeyText(event.entityKey)
							+ '.');
					}
					continue;
				}
				if (event.kind
					== SharedWorldEventKind::
						InteractionTriggerUsed)
				{
					ATrigger* trigger =
						FindInteractionTrigger(event.entityKey);
					if (trigger)
					{
						const int timesBefore =
							trigger->TriggerTimes;
						const bool recentBefore =
							trigger->bRecentlyTriggered;
						const bool enabledBefore =
							trigger->bEnabled;
						g_applyingSharedTriggerInteraction = true;
						const bool activated =
							trigger->UsedBy(localPawn);
						g_applyingSharedTriggerInteraction = false;
						Log("TRIGGERINTERACT replayed key="
							+ SharedWorldKeyText(event.entityKey)
							+ ", trigger=" + ObjectName(trigger)
							+ ", activated="
							+ (activated
								? std::string("yes")
								: std::string("no"))
							+ ", times="
							+ std::to_string(timesBefore)
							+ "->"
							+ std::to_string(
								trigger->TriggerTimes)
							+ ", recent="
							+ (recentBefore
								? std::string("yes")
								: std::string("no"))
							+ "->"
							+ (trigger->bRecentlyTriggered
								? std::string("yes")
								: std::string("no"))
							+ ", enabled="
							+ (enabledBefore
								? std::string("yes")
								: std::string("no"))
							+ "->"
							+ (trigger->bEnabled
								? std::string("yes.")
								: std::string("no.")));
					}
					else
					{
						Log("TRIGGERINTERACT unresolved key="
							+ SharedWorldKeyText(event.entityKey)
							+ '.');
					}
					continue;
				}
				if (event.kind
					== SharedWorldEventKind::InteractionInLondon)
				{
					USeqAct_InteractInLondon* action =
						FindLondonInteractionAction(
							event.entityKey);
					AAlicePlayerController* controller =
						g_State.AlicePlayerController;
					if (action && controller)
					{
						USeqAct_InteractInLondon* previous =
							controller->InteractLondonActor;
						const bool wasActive = action->bActive;
						g_applyingSharedLondonInteraction = true;
						controller->OnInteractInLondon(action);
						const bool assignedByHandler =
							controller->InteractLondonActor == action;
						if (!assignedByHandler)
							controller->InteractLondonActor = action;
						controller->interactInLondonX();
						g_applyingSharedLondonInteraction = false;
						Log("LONDONINTERACT replayed key="
							+ SharedWorldKeyText(event.entityKey)
							+ ", action=" + ObjectName(action)
							+ ", previous="
							+ ObjectName(previous)
							+ ", wasActive="
							+ (wasActive
								? std::string("yes")
								: std::string("no"))
							+ ", assignedByHandler="
							+ (assignedByHandler
								? std::string("yes")
								: std::string("no"))
							+ ", current="
							+ ObjectName(
								controller->InteractLondonActor)
							+ '.');
					}
					else
					{
						Log("LONDONINTERACT unresolved key="
							+ SharedWorldKeyText(event.entityKey)
							+ ", controller="
							+ ObjectName(controller) + '.');
					}
					continue;
				}
				if (event.kind
					== SharedWorldEventKind::InteractionUsed)
				{
					USeqEvent_Used* usedEvent =
						FindUsedInteractionEvent(
							event.entityKey);
					if (usedEvent)
					{
						TArray<int32_t> activateIndices;
						activateIndices.push_back(
							static_cast<int32_t>(
								event.flags));
						AActor* originator =
							usedEvent->Originator
								? usedEvent->Originator
								: localPawn;
						g_applyingSharedInteraction = true;
						const bool activated =
							usedEvent->CheckActivate(
								originator,
								localPawn,
								false,
								true,
								activateIndices);
						g_applyingSharedInteraction = false;
						Log("SHAREDINTERACT replay key="
							+ SharedWorldKeyText(
								event.entityKey)
							+ ", event="
							+ ObjectName(usedEvent)
							+ ", originator="
							+ ObjectName(originator)
							+ ", index="
							+ std::to_string(event.flags)
							+ ", activated="
							+ (activated
								? std::string("yes.")
								: std::string("no.")));
					}
					else
					{
						Log("SHAREDINTERACT unresolved key="
							+ SharedWorldKeyText(
								event.entityKey) + '.');
					}
					continue;
				}
				if (event.kind
					== SharedWorldEventKind::
						InteractionMatineeStarted)
				{
					g_interactionCutsceneBypassUntil =
						Clock::now() + std::chrono::seconds(30);
					if (g_remoteContextInteraction.active)
					{
						Log("SHAREDINTERACT ignored duplicate Matinee "
							"while cinematic handoff is active, key="
							+ SharedWorldKeyText(event.entityKey)
							+ '.');
						continue;
					}
					USeqAct_Interp* action =
						FindInteractionMatinee(event.entityKey);
					if (action)
					{
						const bool wasPlaying = action->bIsPlaying;
						int upstreamDepth = -1;
						std::string upstreamPath;
						USeqEvent_Used* usedEvent =
							FindUpstreamUsedEvent(
								action, upstreamDepth,
								upstreamPath);
						bool activated = false;
						bool outputActivated = false;
						int triggerCountBefore = -1;
						int triggerCountAfter = -1;
						g_applyingSharedInteractionMatinee = true;
						if (usedEvent)
						{
							triggerCountBefore =
								usedEvent->TriggerCount;
							TArray<int32_t> activateIndices;
							AActor* originator =
								usedEvent->Originator
									? usedEvent->Originator
									: localPawn;
							g_applyingSharedInteraction = true;
							activated = usedEvent->CheckActivate(
								originator,
								localPawn,
								false,
								true,
								activateIndices);
							g_applyingSharedInteraction = false;
							triggerCountAfter =
								usedEvent->TriggerCount;
							if (!activated
								&& !usedEvent->OutputLinks.empty())
							{
								outputActivated =
									usedEvent->ActivateOutputLink(0);
							}
						}
						g_applyingSharedInteractionMatinee = false;
						Log("SHAREDINTERACT simulated upstream Use key="
							+ SharedWorldKeyText(event.entityKey)
							+ ", action=" + ObjectName(action)
							+ ", wasPlaying="
							+ (wasPlaying ? std::string("yes")
								: std::string("no"))
							+ ", usedEvent="
							+ ObjectName(usedEvent)
							+ ", depth="
							+ std::to_string(upstreamDepth)
							+ ", enabled="
							+ (usedEvent && usedEvent->bEnabled
								? std::string("yes")
								: std::string("no"))
							+ ", triggers="
							+ std::to_string(triggerCountBefore)
							+ "->"
							+ std::to_string(triggerCountAfter)
							+ ", activated="
							+ (activated ? std::string("yes")
								: std::string("no"))
							+ ", outputFallback="
							+ (outputActivated
								? std::string("yes")
								: std::string("no"))
							+ ", parent="
							+ ObjectName(action->ParentSequence)
							+ ", path=" + upstreamPath + '.');
					}
					else
					{
						Log("SHAREDINTERACT unresolved Matinee key="
							+ SharedWorldKeyText(
								event.entityKey) + '.');
					}
					continue;
				}
				if (event.kind
					== SharedWorldEventKind::VentStateApplied)
				{
					if (g_remoteContextInteraction.active)
					{
						if (g_remoteContextInteraction.localVentApplied)
						{
							Log("SHAREDWORLD discarded duplicate host "
								"vent state after local replicated "
								"interaction completed, key="
								+ SharedWorldKeyText(event.entityKey)
								+ '.');
							ResetRemoteContextInteraction(
								"host-state-acknowledged");
						}
						else
						{
							g_remoteContextInteraction.deferredVent =
								event;
							Log("SHAREDWORLD deferred early host vent "
								"state until local ContextActor "
								"finishes, key="
								+ SharedWorldKeyText(event.entityKey)
								+ '.');
						}
						continue;
					}
					if (!ApplyReplicatedVentState(event))
					{
						Log("SHAREDWORLD could not resolve vent state key="
							+ SharedWorldKeyText(event.entityKey) + '.');
					}
					continue;
				}
				if (event.kind
					== SharedWorldEventKind::BreakableDestroyed)
				{
					AGameBreakableActor* breakable =
						FindSharedBreakable(
							event.entityKey, world);
					if (breakable)
					{
						ApplySharedBreakableDestroy(
							breakable, event, localPawn);
					}
					else
					{
						Log("SHAREDBREAKABLE unresolved key="
							+ SharedWorldKeyText(
								event.entityKey) + '.');
					}
					continue;
				}
				if (event.kind
					== SharedWorldEventKind::
						PlayerDamageRequest)
				{
					ApplySharedPlayerDamage(
						localPawn, world, event);
					continue;
				}
				AAliceGameKynapsePawn* enemy =
					FindSharedEnemy(event.entityKey, world);
				if (g_config.role == Role::Host
					&& event.kind == SharedWorldEventKind::
						EnemyDamageRequest)
				{
					if (enemy)
					{
						ProcessSharedDamageRequest(
							enemy, event, localPawn);
					}
					else
					{
						if (g_config.sharedEnemyTransforms)
						{
							Log("SHAREDWORLD reject unmatched damage key="
								+ SharedWorldKeyText(
									event.entityKey)
								+ ", damage="
								+ std::to_string(
									event.damage)
								+ "; enemy is absent on host.");
						}
						else
						{
							auto& deferred =
								g_deferredSharedDamageRequests[
									event.entityKey];
							if (deferred.size() < 64)
								deferred.push_back(event);
							Log("SHAREDWORLD defer damage key="
								+ SharedWorldKeyText(
									event.entityKey)
								+ ", damage="
								+ std::to_string(
									event.damage) + '.');
						}
					}
				}
				else if (g_config.role == Role::Client
					&& event.kind == SharedWorldEventKind::
						EnemyAuthoritativeState)
				{
					std::uint32_t& lastSerial =
						g_lastAuthoritativeEnemyStateSerial[
							event.entityKey];
					if (lastSerial != 0
						&& !IsNewerSequence(event.eventSerial, lastSerial))
					{
						continue;
					}
					lastSerial = event.eventSerial;
					if (enemy)
					{
						ReconcileAuthoritativeEnemyState(
							enemy, event, localPawn);
					}
					else
					{
						g_pendingAuthoritativeEnemyStates[
							event.entityKey] = event;
						Log("SHAREDWORLD defer state key="
							+ SharedWorldKeyText(event.entityKey)
							+ ", health="
							+ std::to_string(event.health) + '.');
					}
				}
			}
			ApplyDeferredSharedWorldEvents(localPawn, world);
		}

		void ResetWorldTrace(AWorldInfo* world)
		{
			g_worldTraceWorld = world;
			g_worldTraceMap = g_currentMap;
			g_worldTraceRecords.clear();
			g_sentSharedBreakableKeys.clear();
			g_worldTraceLastCounts = { -1, -1, -1, -1 };
			g_worldTraceEventSerial = 0;
			g_nextWorldTraceScan = {};
			if (world && !g_currentMap.empty())
			{
				Log("WORLDTRACE SESSION role="
					+ std::string(g_config.role == Role::Host
						? "host" : "client")
					+ ", map=" + g_currentMap
					+ ", epoch=" + std::to_string(g_worldEpoch)
					+ ", radius="
					+ std::to_string(
						static_cast<int>(g_config.worldTraceRadius))
					+ '.');
			}
		}

		void TickWorldTrace(AAlicePawn* localPawn, AWorldInfo* world)
		{
			if (!g_config.worldTrace || !localPawn || !world)
				return;
			if (world != g_worldTraceWorld
				|| g_currentMap != g_worldTraceMap)
			{
				ResetWorldTrace(world);
			}
			if (!g_State.bRealGameplay || g_currentMap.empty()
				|| _stricmp(g_currentMap.c_str(), "AliceEntry") == 0)
			{
				return;
			}
			const auto now = Clock::now();
			if (now < g_nextWorldTraceScan)
				return;
			g_nextWorldTraceScan =
				now + std::chrono::milliseconds(1500);

			std::unordered_set<const UObject*> seen;
			std::array<int, 4> counts{};
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return;
			const int32_t count = objects->size();
			for (int32_t index = 0; index < count; ++index)
			{
				UObject* object = objects->at(index);
				const WorldTraceKind kind =
					IdentifyWorldTraceObject(object);
				if (kind == WorldTraceKind::None)
					continue;
				auto* actor = reinterpret_cast<AActor*>(object);
				if (actor->WorldInfo != world
					|| !IsWithinWorldTraceRadius(actor, localPawn))
				{
					continue;
				}
				// Static decorative breakables without drops or Alice damage
				// are not useful for the shared-world experiment.
				if (kind == WorldTraceKind::Breakable)
				{
					auto* breakable =
						reinterpret_cast<AGameBreakableActor*>(object);
					if (!breakable->bCanBreakByAlice
						&& !breakable->CanSpawnHealth
						&& !breakable->CanSpawnXP)
					{
						continue;
					}
				}
				const int kindIndex =
					static_cast<int>(kind) - 1;
				if (kindIndex >= 0
					&& kindIndex < static_cast<int>(counts.size()))
					++counts[kindIndex];
				seen.insert(object);
				const std::string state =
					WorldTraceState(object, kind);
				const std::size_t signatureOffset =
					state.find(", hidden=");
				const std::string signature =
					signatureOffset == std::string::npos
						? state
						: state.substr(signatureOffset + 2);
				auto existing = g_worldTraceRecords.find(object);
				if (existing == g_worldTraceRecords.end())
				{
					WorldTraceRecord record{};
					record.key = WorldTraceStableKey(object, kind);
					record.kind = WorldTraceKindName(kind);
					record.objectName = ObjectName(object);
					record.signature = signature;
					Log("WORLDTRACE REGISTER key=" + record.key
						+ ", kind=" + record.kind
						+ ", object=" + record.objectName
						+ ", class=" + ObjectName(object->Class)
						+ ", origin="
						+ FormatWorldTraceVector(
							WorldTraceOrigin(object, kind))
						+ ", " + state + '.');
					g_worldTraceRecords.emplace(
						object, std::move(record));
				}
				else if (existing->second.signature != signature)
				{
					Log("WORLDTRACE STATE key="
						+ existing->second.key
						+ ", kind=" + existing->second.kind
						+ ", object="
						+ existing->second.objectName
						+ ", " + state + '.');
					existing->second.signature = signature;
				}
			}

			for (auto it = g_worldTraceRecords.begin();
				it != g_worldTraceRecords.end();)
			{
				if (seen.find(it->first) == seen.end())
				{
					Log("WORLDTRACE LEAVE key="
						+ it->second.key
						+ ", kind=" + it->second.kind
						+ ", object="
						+ it->second.objectName + '.');
					it = g_worldTraceRecords.erase(it);
				}
				else
					++it;
			}
			if (counts != g_worldTraceLastCounts)
			{
				g_worldTraceLastCounts = counts;
				Log("WORLDTRACE SUMMARY enemies="
					+ std::to_string(counts[0])
					+ ", breakables="
					+ std::to_string(counts[1])
					+ ", pickups="
					+ std::to_string(counts[2])
					+ ", dropped="
					+ std::to_string(counts[3]) + '.');
			}
		}

