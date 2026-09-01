		template <typename T>
		T* FindClassTemplate(UClass* objectClass)
		{
			if (!objectClass)
				return nullptr;
			T* archetype = nullptr;
			T* fallback = nullptr;
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return nullptr;
			for (int32_t index = 0; index < objects->size(); ++index)
			{
				UObject* object = objects->at(index);
				if (!object || object->Class != objectClass)
					continue;
				auto* typed = reinterpret_cast<T*>(object);
				if (object->IsDefaultObject())
					return typed;
				if (!archetype
					&& (object->ObjectFlags & RF_ArchetypeObject) != 0)
				{
					archetype = typed;
				}
				if (!fallback)
					fallback = typed;
			}
			return archetype ? archetype : fallback;
		}

		void FillProjectileTransform(ProjectileEventPayload& event,
			const AActor* actor)
		{
			if (!actor)
				return;
			event.location[0] = actor->Location.X;
			event.location[1] = actor->Location.Y;
			event.location[2] = actor->Location.Z;
			event.rotation[0] = actor->Rotation.Pitch;
			event.rotation[1] = actor->Rotation.Yaw;
			event.rotation[2] = actor->Rotation.Roll;
			event.velocity[0] = actor->Velocity.X;
			event.velocity[1] = actor->Velocity.Y;
			event.velocity[2] = actor->Velocity.Z;
		}

		void QueueProjectileEvent(ProjectileEventPayload event)
		{
			if (!g_config.enabled || g_config.localMirror
				|| g_config.role == Role::Unknown)
			{
				return;
			}
			event.eventSerial = g_localProjectileSerial++;
			event.mapHash = HashMapName(g_currentMap);
			event.actorId =
				g_config.role == Role::Host ? 1u : 2u;
			event.clientTimeMs = ElapsedMilliseconds();
			std::lock_guard lock(g_stateMutex);
			if (g_outboundProjectileEvents.size() >= 256)
				g_outboundProjectileEvents.pop_front();
			g_outboundProjectileEvents.push_back(event);
		}

		std::deque<ReceivedProjectileEvent>
			DrainInboundProjectileEvents()
		{
			std::deque<ReceivedProjectileEvent> events;
			std::lock_guard lock(g_stateMutex);
			events.swap(g_inboundProjectileEvents);
			return events;
		}

		UParticleSystem* ResolvePepperFlightTemplate()
		{
			if (g_pepperFlightTemplate)
				return g_pepperFlightTemplate;
			auto* projectileDefault = FindClassTemplate<
				APepperGrinderPrimaryProjectile>(
					APepperGrinderPrimaryProjectile::StaticClass());
			if (projectileDefault)
			{
				g_pepperFlightTemplate =
					projectileDefault->ProjFlightEffectTemplate;
				if (!g_pepperFlightTemplate
					&& projectileDefault->ProjFlightEffects)
				{
					g_pepperFlightTemplate =
						projectileDefault->ProjFlightEffects->Template;
				}
				if (!g_pepperFlightTemplate
					&& projectileDefault->ProjEffects)
				{
					g_pepperFlightTemplate =
						projectileDefault->ProjEffects->Template;
				}
			}
			if (!g_pepperFlightTemplate)
			{
				g_pepperFlightTemplate = FindLoadedParticleSystem(
					{ "Pepper" }, { "Projectile", "Flight", "Bullet" });
			}
			Log("PROJECTILESTAGE pepper flight template="
				+ ObjectName(g_pepperFlightTemplate) + '.');
			return g_pepperFlightTemplate;
		}

		UStaticMesh* ResolvePepperMarkerMesh()
		{
			if (g_pepperMarkerMesh)
				return g_pepperMarkerMesh;
			UParticleSystem* particle =
				ResolvePepperFlightTemplate();
			if (particle)
			{
				const int32_t emitterCount = std::min<int32_t>(
					particle->Emitters.size(), 64);
				for (int32_t emitterIndex = 0;
					emitterIndex < emitterCount
					&& !g_pepperMarkerMesh; ++emitterIndex)
				{
					UParticleEmitter* emitter =
						particle->Emitters.at(emitterIndex);
					if (!emitter)
						continue;
					const int32_t lodCount = std::min<int32_t>(
						emitter->LODLevels.size(), 16);
					for (int32_t lodIndex = 0;
						lodIndex < lodCount; ++lodIndex)
					{
						UParticleLODLevel* lod =
							emitter->LODLevels.at(lodIndex);
						UParticleModule* typeData = lod
							? lod->TypeDataModule : nullptr;
						if (!typeData || !typeData->IsA(
							UParticleModuleTypeDataMesh::StaticClass()))
						{
							continue;
						}
						g_pepperMarkerMesh = reinterpret_cast<
							UParticleModuleTypeDataMesh*>(
								typeData)->Mesh;
						if (g_pepperMarkerMesh)
							break;
					}
				}
			}
			Log("PROJECTILESTAGE pepper marker mesh="
				+ ObjectName(g_pepperMarkerMesh) + '.');
			return g_pepperMarkerMesh;
		}

		USkeletalMeshComponent* ResolveClockBombMeshTemplate()
		{
			if (g_clockBombMeshTemplate
				&& g_clockBombMeshTemplate->SkeletalMesh)
			{
				return g_clockBombMeshTemplate;
			}
			if (AAlicePawn* localPawn = GetLocalPawn();
				localPawn && localPawn->CloneArcheType
				&& localPawn->CloneArcheType->Mesh
				&& localPawn->CloneArcheType->Mesh->SkeletalMesh)
			{
				g_clockBombMeshTemplate =
					localPawn->CloneArcheType->Mesh;
			}
			if (g_clockBombMeshTemplate)
				return g_clockBombMeshTemplate;
			USkeletalMeshComponent* fallback = nullptr;
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return nullptr;
			for (int32_t index = 0; index < objects->size(); ++index)
			{
				UObject* object = objects->at(index);
				if (!object
					|| !object->IsA(
						AAliceClonePawn::StaticClass()))
				{
					continue;
				}
				auto* bomb = reinterpret_cast<AAliceClonePawn*>(object);
				if (!bomb->Mesh || !bomb->Mesh->SkeletalMesh)
					continue;
				if (!fallback)
					fallback = bomb->Mesh;
				if (object->IsDefaultObject()
					|| (object->ObjectFlags
						& RF_ArchetypeObject) != 0)
				{
					g_clockBombMeshTemplate = bomb->Mesh;
					break;
				}
			}
			if (!g_clockBombMeshTemplate)
				g_clockBombMeshTemplate = fallback;
			Log("PROJECTILESTAGE clock bomb mesh template="
				+ ObjectName(g_clockBombMeshTemplate)
				+ ", mesh="
				+ ObjectName(g_clockBombMeshTemplate
					? g_clockBombMeshTemplate->SkeletalMesh
					: nullptr)
				+ '.');
			return g_clockBombMeshTemplate;
		}

		UParticleSystem* ResolveClockBombExplosionTemplate()
		{
			if (g_clockBombExplosionTemplate)
				return g_clockBombExplosionTemplate;
			if (AAlicePawn* localPawn = GetLocalPawn();
				localPawn && localPawn->CloneArcheType
				&& localPawn->CloneArcheType->ExplosionParticle)
			{
				g_clockBombExplosionTemplate =
					localPawn->CloneArcheType->ExplosionParticle;
				return g_clockBombExplosionTemplate;
			}
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (objects)
			{
				for (int32_t index = 0; index < objects->size(); ++index)
				{
					UObject* object = objects->at(index);
					if (!object
						|| !object->IsA(
							AAliceClonePawn::StaticClass()))
					{
						continue;
					}
					auto* bomb =
						reinterpret_cast<AAliceClonePawn*>(object);
					if (!bomb->ExplosionParticle)
						continue;
					g_clockBombExplosionTemplate =
						bomb->ExplosionParticle;
					if (object->IsDefaultObject()
						|| (object->ObjectFlags
							& RF_ArchetypeObject) != 0)
					{
						break;
					}
				}
			}
			if (!g_clockBombExplosionTemplate)
			{
				g_clockBombExplosionTemplate =
					FindLoadedParticleSystem(
						{ "ClockBomb" }, { "Explode", "Explosion" });
			}
			if (!g_clockBombExplosionTemplate)
			{
				g_clockBombExplosionTemplate =
					FindLoadedParticleSystem(
						{ "Bomb" }, { "Explode", "Explosion" });
			}
			Log("PROJECTILESTAGE clock bomb explosion template="
				+ ObjectName(g_clockBombExplosionTemplate) + '.');
			return g_clockBombExplosionTemplate;
		}

		float ResolveClockBombCollisionHeight()
		{
			AAlicePawn* localPawn = GetLocalPawn();
			AAliceClonePawn* archetype = localPawn
				? localPawn->CloneArcheType : nullptr;
			if (archetype && archetype->CylinderComponent
				&& archetype->CylinderComponent->CollisionHeight > 1.0f)
			{
				return archetype->CylinderComponent->CollisionHeight;
			}
			return 44.0f;
		}

		float ClockBombHypothesisZOffset(std::uint8_t hypothesis)
		{
			(void)hypothesis;
			return ClockBombZOffset;
		}

		FVector ApplyClockBombHypothesisLocation(
			const FVector& location, std::uint8_t hypothesis)
		{
			FVector result = location;
			result.Z += ClockBombHypothesisZOffset(hypothesis);
			return result;
		}

		void ForceClockBombCloneState(RemoteClockBombVisual& visual)
		{
			if (visual.hypothesis != 3 || !visual.actor
				|| !visual.actor->SkeletalMeshComponent)
			{
				return;
			}
			USkeletalMeshComponent* component =
				visual.actor->SkeletalMeshComponent;
			std::vector<UAnimNode*> pending;
			std::unordered_set<UAnimNode*> visited;
			if (component->Animations)
				pending.push_back(component->Animations);
			const int32_t tickNodeCount = std::min<int32_t>(
				component->AnimTickArray.size(), 1024);
			for (int32_t index = 0; index < tickNodeCount; ++index)
				if (component->AnimTickArray.at(index))
					pending.push_back(
						component->AnimTickArray.at(index));

			std::uint8_t desiredState = visual.cloneState;
			const auto age = Clock::now() - visual.spawnedAt;
			if (desiredState <= static_cast<std::uint8_t>(
					EClonnPawnState::e_ClonePawnState_Landing)
				&& age > std::chrono::milliseconds(450))
			{
				// Some levels leave CloneState at Falling even after the hat
				// has landed. The countdown branch is the one that exposes
				// and animates the rabbit.
				desiredState = static_cast<std::uint8_t>(
					EClonnPawnState::e_ClonePawnState_Countdown);
			}

			std::uint16_t traversed = 0;
			std::uint16_t cloneBlendCount = 0;
			while (!pending.empty() && traversed < 1024)
			{
				UAnimNode* node = pending.back();
				pending.pop_back();
				if (!node || !visited.insert(node).second)
					continue;
				++traversed;
				if (node->IsA(
					UAliceGameAnimNode_BlendByCloneState::StaticClass()))
				{
					++cloneBlendCount;
					auto* blend = reinterpret_cast<
						UAliceGameAnimNode_BlendByCloneState*>(node);
					if (blend->Children.size() > 0)
					{
						const int32_t child = std::clamp<int32_t>(
							static_cast<int32_t>(desiredState),
							0, blend->Children.size() - 1);
						blend->bPlayActiveChild = true;
						if (blend->ActiveChildIndex != child)
							NativeSetActiveChild(
								blend, child, 0.04f);
					}
				}
				TArray<FAnimBlendChild>* children = nullptr;
				if (node->IsA(UAnimNodeBlendBase::StaticClass()))
				{
					children = &reinterpret_cast<
						UAnimNodeBlendBase*>(node)->Children;
				}
				else if (node->IsA(
					UAliceGameAnimNode_BlendBase::StaticClass()))
				{
					children = &reinterpret_cast<
						UAliceGameAnimNode_BlendBase*>(node)->Children;
				}
				if (!children)
					continue;
				const int32_t childCount = std::min<int32_t>(
					children->size(), 128);
				for (int32_t childIndex = 0;
					childIndex < childCount; ++childIndex)
					if (children->at(childIndex).Anim)
						pending.push_back(
							children->at(childIndex).Anim);
			}
			visual.animationNodeCount = traversed;
			if (visual.lastForcedCloneState != desiredState)
			{
				visual.lastForcedCloneState = desiredState;
				Log("PROJECTILESTAGE clock bomb anim id="
					+ std::to_string(visual.projectileId)
					+ ", networkState="
					+ std::to_string(visual.cloneState)
					+ ", forcedState="
					+ std::to_string(desiredState)
					+ ", nodes="
					+ std::to_string(traversed)
					+ ", cloneBlends="
					+ std::to_string(cloneBlendCount) + '.');
			}
		}

		RemoteClockBombVisual* SpawnRemoteClockBomb(
			AWorldInfo* world, const ProjectileEventPayload& event)
		{
			if (!world)
				return nullptr;
			auto existing =
				g_remoteClockBombVisuals.find(event.projectileId);
			if (existing != g_remoteClockBombVisuals.end())
				return &existing->second;

			USkeletalMeshComponent* source =
				ResolveClockBombMeshTemplate();
			AActor* context = g_remotePawn
				? reinterpret_cast<AActor*>(g_remotePawn)
				: reinterpret_cast<AActor*>(GetLocalPawn());
			if (!context || !source || !source->SkeletalMesh)
			{
				Log("PROJECTILESTAGE clock bomb proxy unavailable.");
				return nullptr;
			}
			const FVector rawLocation(event.location[0],
				event.location[1], event.location[2]);
			const std::uint8_t hypothesis = static_cast<std::uint8_t>(
				std::clamp(g_clockBombHypothesis, 1, 3));
			const FVector location =
				ApplyClockBombHypothesisLocation(
					rawLocation, hypothesis);
			const FRotator rotation(event.rotation[0],
				event.rotation[1], event.rotation[2]);
			AActor* spawned = NativeSpawn(context,
				ASkeletalMeshActorSpawnable::StaticClass(),
				g_remotePawn, FName(), location, rotation,
				nullptr, true);
			if (!spawned
				|| !spawned->IsA(
					ASkeletalMeshActorSpawnable::StaticClass()))
			{
				if (spawned)
					NativeDestroyActor(spawned);
				Log("PROJECTILESTAGE clock bomb proxy spawn failed.");
				return nullptr;
			}

			auto* proxy = reinterpret_cast<
				ASkeletalMeshActorSpawnable*>(spawned);
			proxy->bNoDelete = false;
			proxy->bStatic = false;
			proxy->bMovable = true;
			proxy->bCanBeDamaged = false;
			proxy->bCollideActors = false;
			proxy->bCollideWorld = false;
			proxy->bBlockActors = false;
			proxy->Physics = EPhysics::PHYS_None;
			proxy->SetCollision(false, false, true);
			proxy->SetCollisionType(ECollisionType::COLLIDE_NoCollision);
			proxy->SetLocationNoCheck(location);
			proxy->SetRotation(rotation);
			proxy->Location = location;
			proxy->Rotation = rotation;
			if (proxy->SkeletalMeshComponent)
			{
				USkeletalMeshComponent* destination =
					proxy->SkeletalMeshComponent;
				NativeSetSkeletalMesh(destination,
					source->SkeletalMesh, false);
				MirrorMaterials(destination, source);
				destination->SetTranslation(source->Translation);
				destination->SetRotation(source->Rotation);
				destination->SetScale(source->Scale);
				destination->SetScale3D(source->Scale3D);
				destination->HiddenGame = false;
				destination->bOwnerNoSee = false;
				destination->bOnlyOwnerSee = false;
				NativeSetComponentHidden(destination, false);
			}
			NativeSetActorHidden(proxy, false);
			NativeForceUpdateComponents(proxy, false, false);

			RemoteClockBombVisual visual{};
			visual.projectileId = event.projectileId;
			visual.actor = proxy;
			visual.targetLocation = location;
			visual.velocity = FVector(event.velocity[0],
				event.velocity[1], event.velocity[2]);
			visual.targetRotation = rotation;
			visual.cloneState = event.variant;
			visual.hypothesis = hypothesis;
			visual.lastTick = Clock::now();
			visual.lastUpdate = visual.lastTick;
			visual.spawnedAt = visual.lastTick;
			auto inserted = g_remoteClockBombVisuals.emplace(
				event.projectileId, visual);
			Log("PROJECTILESTAGE remote clock bomb spawned id="
				+ std::to_string(event.projectileId)
				+ ", actor=" + ObjectName(proxy)
				+ ", hypothesis="
				+ std::to_string(hypothesis)
				+ ", zOffset="
				+ std::to_string(
					ClockBombHypothesisZOffset(hypothesis))
				+ ", cloneState="
				+ std::to_string(event.variant)
				+ ", animNodes="
				+ std::to_string(proxy->SkeletalMeshComponent
					? proxy->SkeletalMeshComponent
						->AnimTickArray.size() : 0)
				+ '.');
			return &inserted.first->second;
		}

		void DestroyRemoteClockBomb(std::uint32_t projectileId)
		{
			auto found =
				g_remoteClockBombVisuals.find(projectileId);
			if (found == g_remoteClockBombVisuals.end())
				return;
			AActor* actor = found->second.actor;
			if (actor && actor->WorldInfo == g_currentWorld)
			{
				NativeSetActorHidden(actor, true);
				actor->bNoDelete = false;
				NativeDestroyActor(actor);
			}
			g_remoteClockBombVisuals.erase(found);
		}

		void ClearRemoteProjectileVisuals(bool destroyActors)
		{
			for (RemotePepperVisual& visual :
				g_remotePepperVisuals)
			{
				if (visual.particle)
					HardStopPresentationParticle(
						visual.particle, true);
				if (visual.marker
					&& visual.marker->WorldInfo == g_currentWorld)
				{
					NativeSetActorHidden(
						visual.marker, true);
					visual.marker->bNoDelete = false;
					NativeDestroyActor(visual.marker);
				}
			}
			g_remotePepperVisuals.clear();
			if (destroyActors)
			{
				for (auto& item : g_remoteClockBombVisuals)
				{
					AActor* actor = item.second.actor;
					if (actor && actor->WorldInfo == g_currentWorld)
					{
						NativeSetActorHidden(actor, true);
						actor->bNoDelete = false;
						NativeDestroyActor(actor);
					}
				}
			}
			g_remoteClockBombVisuals.clear();
		}

		void ApplyInboundProjectileEvents(AWorldInfo* world)
		{
			if (!world || g_config.localMirror)
				return;
			auto events = DrainInboundProjectileEvents();
			const std::uint32_t mapHash =
				HashMapName(g_currentMap);
			const Clock::time_point now = Clock::now();
			for (const ReceivedProjectileEvent& received : events)
			{
				const ProjectileEventPayload& event =
					received.event;
				if (event.mapHash != mapHash
					|| now - received.receivedAt
						> std::chrono::seconds(2))
				{
					continue;
				}
				const FVector location(event.location[0],
					event.location[1], event.location[2]);
				const FVector velocity(event.velocity[0],
					event.velocity[1], event.velocity[2]);
				const FRotator rotation(event.rotation[0],
					event.rotation[1], event.rotation[2]);
				switch (event.kind)
				{
				case ProjectileEventKind::PepperSpawn:
				{
					if (!PepperProjectileReplicationEnabled)
						break;
					const std::uint8_t hypothesis =
						static_cast<std::uint8_t>(std::clamp(
							g_pepperProjectileHypothesis, 1, 3));
					const float markerScale =
						hypothesis == 1 ? 0.010f
						: (hypothesis == 2 ? 0.018f : 0.030f);
					ADynamicSMActor_Spawnable* marker = nullptr;
					UStaticMesh* markerMesh =
						ResolvePepperMarkerMesh();
					AActor* context = g_remotePawn
						? reinterpret_cast<AActor*>(g_remotePawn)
						: reinterpret_cast<AActor*>(GetLocalPawn());
					if (context && markerMesh)
					{
						AActor* spawned = NativeSpawn(context,
							ADynamicSMActor_Spawnable::StaticClass(),
							g_remotePawn, FName(), location,
							rotation, nullptr, true);
						if (spawned && spawned->IsA(
							ADynamicSMActor_Spawnable::StaticClass()))
						{
							marker = reinterpret_cast<
								ADynamicSMActor_Spawnable*>(spawned);
							marker->bNoDelete = false;
							marker->bStatic = false;
							marker->bMovable = true;
							marker->bCanBeDamaged = false;
							marker->bCollideActors = false;
							marker->bCollideWorld = false;
							marker->bBlockActors = false;
							marker->Physics = EPhysics::PHYS_None;
							marker->SetCollision(
								false, false, true);
							marker->SetCollisionType(
								ECollisionType::COLLIDE_NoCollision);
							marker->SetStaticMesh(markerMesh,
								FVector(0.0f, 0.0f, 0.0f),
								FRotator(0, 0, 0),
								FVector(markerScale,
									markerScale, markerScale));
							marker->SetLocationNoCheck(location);
							marker->SetRotation(rotation);
							marker->Location = location;
							marker->Rotation = rotation;
							NativeSetActorHidden(marker, false);
							NativeForceUpdateComponents(
								marker, false, true);
						}
						else if (spawned)
						{
							NativeDestroyActor(spawned);
						}
					}
					RemotePepperVisual visual{};
					visual.projectileId = event.projectileId;
					visual.marker = marker;
					visual.location = location;
					visual.velocity = velocity;
					visual.rotation = rotation;
					visual.scale = markerScale;
					visual.hypothesis = hypothesis;
					visual.lastTick = now;
					visual.expiresAt =
						now + std::chrono::milliseconds(260);
					g_remotePepperVisuals.push_back(visual);
					Log("PROJECTILESTAGE remote pepper spawned id="
						+ std::to_string(event.projectileId)
						+ ", hypothesis="
						+ std::to_string(hypothesis)
						+ ", marker="
						+ ObjectName(marker)
						+ ", mesh="
						+ ObjectName(markerMesh)
						+ ", velocity=("
						+ std::to_string(velocity.X) + ','
						+ std::to_string(velocity.Y) + ','
						+ std::to_string(velocity.Z) + ").");
					break;
				}
				case ProjectileEventKind::ClockBombSpawn:
				case ProjectileEventKind::ClockBombUpdate:
				{
					RemoteClockBombVisual* visual =
						SpawnRemoteClockBomb(world, event);
					if (visual)
					{
						visual->hypothesis =
							static_cast<std::uint8_t>(std::clamp(
								g_clockBombHypothesis, 1, 3));
						visual->targetLocation =
							ApplyClockBombHypothesisLocation(
								location, visual->hypothesis);
						visual->velocity = velocity;
						visual->targetRotation = rotation;
						visual->cloneState = event.variant;
						visual->lastUpdate = now;
					}
					break;
				}
				case ProjectileEventKind::ClockBombExplode:
				{
					UParticleSystem* explosion =
						ResolveClockBombExplosionTemplate();
					if (explosion && world->MyEmitterPool)
					{
						UParticleSystemComponent* spawned =
							world->MyEmitterPool->SpawnEmitter(
								explosion, location, rotation,
								nullptr, false);
						MakePresentationParticleVisible(spawned);
					}
					DestroyRemoteClockBomb(event.projectileId);
					Log("PROJECTILESTAGE remote clock bomb exploded id="
						+ std::to_string(event.projectileId) + '.');
					break;
				}
				case ProjectileEventKind::ClockBombRemove:
					DestroyRemoteClockBomb(event.projectileId);
					break;
				default:
					break;
				}
			}
		}

		void TickRemoteProjectileVisuals(AWorldInfo* world)
		{
			if (!world)
				return;
			const Clock::time_point now = Clock::now();
			auto pepperNext = g_remotePepperVisuals.begin();
			for (auto current = g_remotePepperVisuals.begin();
				current != g_remotePepperVisuals.end(); ++current)
			{
				if (now >= current->expiresAt)
				{
					if (current->particle)
						HardStopPresentationParticle(
							current->particle, true);
					if (current->marker
						&& current->marker->WorldInfo == world)
					{
						NativeSetActorHidden(
							current->marker, true);
						current->marker->bNoDelete = false;
						NativeDestroyActor(current->marker);
					}
					continue;
				}
				const float delta = std::clamp(
					std::chrono::duration<float>(
						now - current->lastTick).count(),
					0.0f, 0.1f);
				current->lastTick = now;
				const FVector previousLocation = current->location;
				current->location.X +=
					current->velocity.X * delta;
				current->location.Y +=
					current->velocity.Y * delta;
				current->location.Z +=
					current->velocity.Z * delta;
				if (current->particle)
				{
					current->particle->SetTranslation(
						current->location);
					current->particle->SetRotation(
						current->rotation);
					current->particle->SetScale(
						current->scale);
					current->particle->SetScale3D(FVector(
						1.0f, 1.0f, 1.0f));
					NativeForceComponentUpdate(
						current->particle, true);
				}
				if (current->marker
					&& current->marker->WorldInfo == world)
				{
					current->marker->SetLocationNoCheck(
						current->location);
					current->marker->SetRotation(
						current->rotation);
					current->marker->Location =
						current->location;
					current->marker->Rotation =
						current->rotation;
					NativeForceUpdateComponents(
						current->marker, false, true);
				}
				else if (!current->marker)
				{
					FVector lineStart = previousLocation;
					FVector lineEnd = current->location;
					const float lengthSquared =
						current->velocity.X * current->velocity.X
						+ current->velocity.Y * current->velocity.Y
						+ current->velocity.Z * current->velocity.Z;
					if (lengthSquared > 0.001f)
					{
						const float inverseLength =
							1.0f / std::sqrt(lengthSquared);
						lineEnd.X += current->velocity.X
							* inverseLength * 18.0f;
						lineEnd.Y += current->velocity.Y
							* inverseLength * 18.0f;
						lineEnd.Z += current->velocity.Z
							* inverseLength * 18.0f;
					}
					AActor::DrawDebugLine(
						lineStart, lineEnd, 255, 210, 32, false);
				}
				if (pepperNext != current)
					*pepperNext = *current;
				++pepperNext;
			}
			g_remotePepperVisuals.erase(
				pepperNext, g_remotePepperVisuals.end());

			for (auto iterator =
					g_remoteClockBombVisuals.begin();
				iterator != g_remoteClockBombVisuals.end();)
			{
				RemoteClockBombVisual& visual =
					iterator->second;
				if (!visual.actor
					|| visual.actor->WorldInfo != world
					|| now - visual.lastUpdate
						> std::chrono::seconds(8))
				{
					if (visual.actor
						&& visual.actor->WorldInfo == world)
					{
						NativeSetActorHidden(
							visual.actor, true);
						NativeDestroyActor(visual.actor);
					}
					iterator =
						g_remoteClockBombVisuals.erase(iterator);
					continue;
				}
				const float delta = std::clamp(
					std::chrono::duration<float>(
						now - visual.lastTick).count(),
					0.0f, 0.1f);
				visual.lastTick = now;
				const float age = std::clamp(
					std::chrono::duration<float>(
						now - visual.lastUpdate).count(),
					0.0f, 0.12f);
				FVector predicted = visual.targetLocation;
				predicted.X += visual.velocity.X * age;
				predicted.Y += visual.velocity.Y * age;
				predicted.Z += visual.velocity.Z * age;
				const float alpha =
					1.0f - std::exp(-18.0f * delta);
				FVector smoothed = visual.actor->Location;
				smoothed.X +=
					(predicted.X - smoothed.X) * alpha;
				smoothed.Y +=
					(predicted.Y - smoothed.Y) * alpha;
				smoothed.Z +=
					(predicted.Z - smoothed.Z) * alpha;
				visual.actor->SetLocationNoCheck(smoothed);
				visual.actor->SetRotation(visual.targetRotation);
				visual.actor->Location = smoothed;
				visual.actor->Rotation = visual.targetRotation;
				NativeForceUpdateComponents(
					visual.actor, false, true);
				++iterator;
			}
		}

		void TickLocalClockBombReplication()
		{
			AAlicePawn* localPawn = GetLocalPawn();
			AAliceClonePawn* activeBomb = nullptr;
			if (localPawn && localPawn->MyClonePawn
				&& localPawn->MyClonePawn->IsA(
					AAliceClonePawn::StaticClass()))
			{
				activeBomb = reinterpret_cast<AAliceClonePawn*>(
					localPawn->MyClonePawn);
			}
			if (activeBomb
				&& activeBomb != g_localClockBomb.bomb)
			{
				OnLocalClockBombSpawn(activeBomb);
			}
			if (!activeBomb)
			{
				if (g_localClockBomb.bomb
					&& g_localClockBomb.projectileId != 0
					&& !g_localClockBomb.exploded)
				{
					ProjectileEventPayload event{};
					event.kind =
						ProjectileEventKind::ClockBombExplode;
					event.projectileId =
						g_localClockBomb.projectileId;
					event.variant =
						g_localClockBomb.cloneState;
					event.location[0] =
						g_localClockBomb.lastLocation.X;
					event.location[1] =
						g_localClockBomb.lastLocation.Y;
					event.location[2] =
						g_localClockBomb.lastLocation.Z;
					event.rotation[0] =
						g_localClockBomb.lastRotation.Pitch;
					event.rotation[1] =
						g_localClockBomb.lastRotation.Yaw;
					event.rotation[2] =
						g_localClockBomb.lastRotation.Roll;
					event.velocity[0] =
						g_localClockBomb.lastVelocity.X;
					event.velocity[1] =
						g_localClockBomb.lastVelocity.Y;
					event.velocity[2] =
						g_localClockBomb.lastVelocity.Z;
					QueueProjectileEvent(event);
					Log("PROJECTILESTAGE clock bomb disappearance "
						"treated as explosion id="
						+ std::to_string(event.projectileId)
						+ '.');
				}
				g_localClockBomb = {};
				return;
			}
			g_localClockBomb.lastLocation =
				activeBomb->Location;
			g_localClockBomb.lastVelocity =
				activeBomb->Velocity;
			g_localClockBomb.lastRotation =
				activeBomb->Rotation;
			g_localClockBomb.cloneState =
				static_cast<std::uint8_t>(
					activeBomb->CloneState);
			if (activeBomb->CloneState
				== EClonnPawnState::
					e_ClonePawnState_Destory
				&& !g_localClockBomb.exploded)
			{
				OnLocalClockBombDetonate(activeBomb);
			}
			if (g_localClockBomb.exploded)
			{
				return;
			}
			const Clock::time_point now = Clock::now();
			if (now < g_localClockBomb.nextUpdate)
				return;
			ProjectileEventPayload event{};
			event.kind = ProjectileEventKind::ClockBombUpdate;
			event.projectileId =
				g_localClockBomb.projectileId;
			event.variant = static_cast<std::uint8_t>(
				activeBomb->CloneState);
			event.extra =
				activeBomb->CountdownTime;
			FillProjectileTransform(event, activeBomb);
			QueueProjectileEvent(event);
			g_localClockBomb.nextUpdate =
				now + std::chrono::milliseconds(50);
		}

