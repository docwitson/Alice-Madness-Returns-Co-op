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

		std::uint32_t ClockBombMaterialHash(UMaterialInterface* material)
		{
			if (!material) return 0;
			for (int depth = 0; depth < 8 && material->IsA(UMaterialInstance::StaticClass()); ++depth)
			{
				auto* instance = reinterpret_cast<UMaterialInstance*>(material);
				if (instance->bDuplicate)
				{
					// Native DuplicateInstance keeps the package/outer and adds
					// _Dup<number> to the authored name. Its counter is process-local.
					std::string name = material->GetFullName();
					const auto suffix = name.rfind("_Dup");
					if (suffix != std::string::npos && suffix + 4 < name.size()
						&& std::all_of(name.begin() + suffix + 4, name.end(),
							[](char value) { return value >= '0' && value <= '9'; }))
						name.resize(suffix);
					return HashMapName(name);
				}
				if ((material->ObjectFlags & RF_Transient) == 0
					&& !ContainsCaseInsensitive(material->GetFullName(), " Transient.")) break;
				if (!instance->Parent || instance->Parent == material) break;
				material = instance->Parent;
			}
			return HashMapName(material->GetFullName());
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
			if (const_cast<AActor*>(actor)->IsA(AAliceClonePawn::StaticClass()))
			{
				auto* clone = reinterpret_cast<const AAliceClonePawn*>(actor);
				if (clone->Mesh)
				{
					for (std::size_t index = 0; index < ClockBombMaterialSlots; ++index)
					{
						UMaterialInterface* material = index < static_cast<std::size_t>(clone->Mesh->Materials.size())
							? clone->Mesh->Materials.at(static_cast<int>(index)) : nullptr;
						if (!material && clone->Mesh->SkeletalMesh
							&& index < static_cast<std::size_t>(clone->Mesh->SkeletalMesh->Materials.size()))
							material = clone->Mesh->SkeletalMesh->Materials.at(static_cast<int>(index));
						event.clockBombMaterialHashes[index] = ClockBombMaterialHash(material);
					}
					event.reserved |= 0x8000;
					event.effectParameters[0] = clone->Mesh->Translation.X;
					event.effectParameters[1] = clone->Mesh->Translation.Y;
					event.effectParameters[2] = clone->Mesh->Translation.Z;
					event.effectParameters[3] = clone->Mesh->Scale;
					std::vector<AnimationSequenceCandidate> sequences;
					std::vector<AnimationBlendCandidate> blends;
					CaptureAnimationComponent(clone->Mesh, AnimationComponent::Body, sequences, blends);
					if (!sequences.empty())
					{
						const auto& sequence = std::max_element(sequences.begin(), sequences.end(),
							[](const auto& a, const auto& b) { return a.priority < b.priority; })->payload;
						strncpy_s(event.effectParameterName, sequence.sequenceName, _TRUNCATE);
						event.extra = sequence.position;
						memcpy_s(&event.particleTemplateHash, sizeof(event.particleTemplateHash), &sequence.rate, sizeof(sequence.rate));
						event.reserved |= sequence.rootBoneOptions & 0x3fff;
						if (sequence.flags & AnimationSequenceLooping) event.reserved |= 0x4000;
					}
				}
			}
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

		UParticleSystem* ResolveProjectileParticle(std::uint32_t hash)
		{
			if (!hash) return nullptr;
			const auto found = g_projectileParticleTemplates.find(hash);
			if (found != g_projectileParticleTemplates.end()) return found->second;
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects) return nullptr;
			for (int32_t index = 0; index < objects->size(); ++index)
			{
				UObject* object = objects->at(index);
				if (object && object->IsA(UParticleSystem::StaticClass())
					&& HashMapName(object->GetFullName()) == hash)
				{
					auto* particle = reinterpret_cast<UParticleSystem*>(object);
					g_projectileParticleTemplates.emplace(hash, particle);
					return particle;
				}
			}
			return nullptr;
		}

		UMaterialInterface* ResolveProjectileMaterial(std::uint32_t hash)
		{
			auto* objects = UObject::GObjObjects();
			if (!objects || !hash) return nullptr;
			for (int index = 0; index < objects->size(); ++index)
			{
				auto* object = objects->at(index);
				if (object && object->IsA(UMaterialInterface::StaticClass())
					&& HashMapName(object->GetFullName()) == hash)
					return reinterpret_cast<UMaterialInterface*>(object);
			}
			return nullptr;
		}

		void SpawnRemoteNativeDecal(AWorldInfo* world, const ProjectileEventPayload& event)
		{
			if (!world || !world->MyDecalManager) return;
			UMaterialInterface* material = ResolveProjectileMaterial(event.particleTemplateHash);
			if (!material)
			{
				Log("NATIVEFX RX missing decal asset=" + std::to_string(event.particleTemplateHash) + '.');
				return;
			}
			const std::string assetName = ObjectName(material);
			unsigned curvesStarted = 0;
			if (material->IsA(UMaterialInstanceTimeVarying::StaticClass()))
			{
				// Native projectile scripts create a child MITV whose own scalar
				// array can be empty. An absent transmitted name must not make us
				// reuse the package material and its already-running render clocks.
				std::vector<FName> curveNames;
				std::vector<FName> seenNames;
				std::unordered_set<UMaterialInterface*> parents;
				for (auto* current = material; current && parents.size() < 8
					&& parents.insert(current).second
					&& current->IsA(UMaterialInstance::StaticClass());
					current = reinterpret_cast<UMaterialInstance*>(current)->Parent)
				{
					if (!current->IsA(UMaterialInstanceTimeVarying::StaticClass())) break;
					auto* varying = reinterpret_cast<UMaterialInstanceTimeVarying*>(current);
					for (int i = 0; i < varying->ScalarParameterValues.size(); ++i)
					{
						const auto& parameter = varying->ScalarParameterValues.at(i);
						if (!parameter.ParameterName.IsValid()
							|| std::find(seenNames.begin(), seenNames.end(), parameter.ParameterName) != seenNames.end()) continue;
						seenNames.push_back(parameter.ParameterName);
						if (parameter.ParameterValueCurve.Points.size() > 0)
							curveNames.push_back(parameter.ParameterName);
					}
				}
				if (event.effectParameterName[0] && memchr(event.effectParameterName, 0, sizeof(event.effectParameterName)))
				{
					const FName explicitName(event.effectParameterName);
					if (explicitName.IsValid() && std::find(seenNames.begin(), seenNames.end(), explicitName) == seenNames.end())
						curveNames.push_back(explicitName);
				}
				UMaterialInstance_execDuplicateInstance_Params duplicate{};
				InvokeNativeFunction(material, UFunction::FindFunction("Function Engine.MaterialInstance.DuplicateInstance"), duplicate);
				if (!duplicate.ReturnValue || duplicate.ReturnValue == material)
				{
					Log("NATIVEFX RX could not isolate decal=" + assetName + '.');
					return;
				}
				material = duplicate.ReturnValue;
				auto* startFunction = UFunction::FindFunction("Function Engine.MaterialInstanceTimeVarying.SetScalarStartTime");
				// Collect names before native calls: inherited parameters may be
				// appended, reallocating the duplicate's scalar array. The native
				// setter copies inherited curves and refreshes render-thread maps.
				for (const FName& name : curveNames)
				{
					UMaterialInstanceTimeVarying_execSetScalarStartTime_Params start{};
					start.ParameterName = name;
					start.Value = 0.0f;
					if (InvokeNativeFunction(material, startFunction, start)) ++curvesStarted;
				}
			}
			ADecalManager_execSpawnDecal_Params params{};
			params.DecalMaterial = material;
			params.DecalLocation = FVector(event.location[0], event.location[1], event.location[2]);
			params.DecalOrientation = FRotator(event.rotation[0], event.rotation[1], event.rotation[2]);
			params.Width = event.velocity[0]; params.Height = event.velocity[1]; params.Thickness = event.velocity[2];
			params.bNoClip = (event.reserved & 1) != 0;
			params.bProjectOnTerrain = (event.reserved & 2) != 0;
			// Resolve surfaces in the receiving world; source component pointers
			// and skeletal bone/node indices cannot be used across processes.
			params.bProjectOnSkeletalMeshes = (event.reserved & 4) != 0;
			params.DecalRotation = event.effectParameters[0];
			params.InDecalLifeSpan = event.extra;
			params.InFracturedStaticMeshComponentIndex = -1;
			params.HitNodeIndex = -1; params.HitLevelIndex = -1;
			params.InDepthBias = event.effectParameters[1];
			params.InBlendRange.X = event.effectParameters[2]; params.InBlendRange.Y = event.effectParameters[3];
			auto* function = UFunction::FindFunction("Function Engine.DecalManager.SpawnDecal");
			if (!function) return;
			world->MyDecalManager->ProcessEvent(function, &params, nullptr);
			static std::unordered_map<std::uint32_t, unsigned> counts;
			const unsigned count = ++counts[event.particleTemplateHash];
			if (count <= 8 || count % 100 == 0)
				Log("NATIVEFX RX decal=" + assetName + ", instance=" + ObjectName(material)
					+ ", curves=" + std::to_string(curvesStarted) + ", spawned=" + (params.ReturnValue ? "yes." : "no."));
		}

		void DestroyRemoteWeaponProjectile(std::uint32_t id)
		{
			for (auto it = g_remotePepperVisuals.begin(); it != g_remotePepperVisuals.end();)
			{
				if (it->projectileId != id) { ++it; continue; }
				if (IsLivePresentationParticle(it->particle) && !it->particle->bIsCachedInPool
					&& it->particle->Template == it->particleTemplate)
					HardStopPresentationParticle(it->particle, true);
				it = g_remotePepperVisuals.erase(it);
			}
		}

		void TickLocalWeaponProjectileReplication()
		{
			if (g_config.localMirror) return;
			const auto now = Clock::now();
			TArray<UObject*>* objects = UObject::GObjObjects();
			for (const auto& [projectile, index] : g_pendingWeaponProjectiles)
			{
				if (objects && index >= 0 && index < objects->size()
					&& objects->at(index) == projectile && !projectile->bDeleteMe)
					OnLocalWeaponProjectileSpawn(projectile, projectile->FlightFront);
			}
			g_pendingWeaponProjectiles.clear();
			for (auto it = g_localWeaponProjectiles.begin(); it != g_localWeaponProjectiles.end();)
			{
				AAliceGameProjectile* projectile = it->first;
				LocalWeaponProjectileTrack& track = it->second;
				const bool valid = objects && track.objectIndex >= 0 && track.objectIndex < objects->size()
					&& objects->at(track.objectIndex) == projectile;
				if (valid && projectile->WorldInfo == g_currentWorld
					&& (projectile->bHidden || projectile->bShuttingDown || projectile->bDeleteMe))
				{
					// Retire flight after native shutdown. Impact visual hooks
					// independently relay actual source emitters and decals.
					++it;
					OnLocalWeaponProjectileEnd(projectile, nullptr, nullptr);
					continue;
				}
				if (!valid || projectile->WorldInfo != g_currentWorld || now >= track.expiresAt)
				{
					ProjectileEventPayload event = track.spawn;
					event.kind = ProjectileEventKind::WeaponProjectileRemove;
					QueueProjectileEvent(event);
					it = g_localWeaponProjectiles.erase(it);
					continue;
				}
				if (now >= track.nextUpdate)
				{
					ProjectileEventPayload event = track.spawn;
					event.kind = ProjectileEventKind::WeaponProjectileUpdate;
					FillProjectileTransform(event, projectile);
					QueueProjectileEvent(event);
					track.nextUpdate = now + std::chrono::milliseconds(50);
				}
				++it;
			}
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

		bool CopyClockBombAnimationSets(USkeletalMeshComponent* destination,
			const USkeletalMeshComponent* source)
		{
			if (!destination || !source || source->AnimSets.empty()
				|| !destination->TemporarySavedAnimSets.empty()) return false;
			// Native Save/RestoreAnimSets deep-copy with the engine allocator.
			// Only borrow the source view during Save; never give it to Restore,
			// which frees the destination array before restoring its owned copy.
			const auto original = destination->AnimSets;
			destination->AnimSets = source->AnimSets;
			USkeletalMeshComponent_execSaveAnimSets_Params save{};
			const bool saved = InvokeNativeFunction(destination,
				UFunction::FindFunction("Function Engine.SkeletalMeshComponent.SaveAnimSets"), save);
			destination->AnimSets = original;
			if (!saved) return false;
			USkeletalMeshComponent_execRestoreSavedAnimSets_Params restore{};
			return InvokeNativeFunction(destination,
				UFunction::FindFunction("Function Engine.SkeletalMeshComponent.RestoreSavedAnimSets"), restore)
				&& destination->AnimSets.size() == source->AnimSets.size();
		}

		void ApplyRemoteClockBombMaterials(RemoteClockBombVisual& visual,
			const ProjectileEventPayload& event)
		{
			if (!visual.actor || !visual.actor->SkeletalMeshComponent) return;
			auto* mesh = visual.actor->SkeletalMeshComponent;
			auto* function = UFunction::FindFunction("Function Engine.MeshComponent.SetMaterial");
			for (std::size_t index = 0; index < ClockBombMaterialSlots; ++index)
			{
				const auto hash = event.clockBombMaterialHashes[index];
				if (!hash || hash == visual.materialHashes[index]) continue;
				auto* material = ResolveProjectileMaterial(hash);
				if (!material) continue;
				UMeshComponent_execSetMaterial_Params params{};
				params.ElementIndex = static_cast<int>(index);
				params.Material = material;
				if (!InvokeNativeFunction(mesh, function, params)) continue;
				visual.materialHashes[index] = hash;
				Log("PROJECTILESTAGE clock bomb material id=" + std::to_string(event.projectileId)
					+ ", slot=" + std::to_string(index) + ", material=" + ObjectName(material) + '.');
			}
		}

		void ApplyRemoteClockBombAnimation(RemoteClockBombVisual& visual,
			const ProjectileEventPayload& event)
		{
			if (!visual.actor || !visual.actor->SkeletalMeshComponent
				|| !event.effectParameterName[0]
				|| !memchr(event.effectParameterName, 0, sizeof(event.effectParameterName))) return;
			auto* mesh = visual.actor->SkeletalMeshComponent;
			if (!mesh->Animations || !mesh->Animations->IsA(UAnimNodeSequence::StaticClass())) return;
			auto* node = reinterpret_cast<UAnimNodeSequence*>(mesh->Animations);
			const auto asset = FindAnimationAsset(mesh, event.effectParameterName);
			if (!asset.sequence) return;
			const bool changed = node->AnimSeq != asset.sequence;
			if (changed) NativeSetAnim(node, asset.sequence->SequenceName);
			if (node->AnimSeq != asset.sequence) return;
			float rate = 0.0f;
			memcpy_s(&rate, sizeof(rate), &event.particleTemplateHash, sizeof(rate));
			if (!std::isfinite(rate) || !std::isfinite(event.extra)) return;
			const bool looping = (event.reserved & 0x4000) != 0;
			const float length = asset.sequence->SequenceLength;
			if (!(length > 0.001f)) return;
			float position = (std::max)(0.0f, event.extra);
			position = looping ? std::fmod(position, length) : (std::min)(position, length - 0.001f);
			ApplyPresentationRootOptions(node, event.reserved & 0x3fff);
			node->bNoNotifies = true;
			node->bCauseActorAnimEnd = false;
			node->bCauseActorAnimPlay = false;
			node->bForceRefposeWhenNotPlaying = false;
			const bool endpoint = !looping && position >= length - 0.001f;
			const auto correction = CorrectAnimationPhase(node->CurrentTime, position, length, looping, endpoint ? 0.0f : rate);
			const float nativeRate = NativePlaybackRate(node, correction.rate);
			if (changed || !node->bPlaying) NativePlayAnim(node, looping, nativeRate, position);
			node->Rate = nativeRate;
			node->bLooping = looping;
			if (changed || correction.snap || endpoint) NativeSetPosition(node, position);
			if (changed)
				Log("PROJECTILESTAGE clock bomb animation id=" + std::to_string(event.projectileId)
					+ ", sequence=" + std::string(event.effectParameterName) + '.');
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
			NativeSetActorRotation(proxy, rotation);
			proxy->Location = location;
			proxy->Rotation = rotation;
			if (proxy->SkeletalMeshComponent)
			{
				USkeletalMeshComponent* destination =
					proxy->SkeletalMeshComponent;
				NativeSetSkeletalMesh(destination,
					source->SkeletalMesh, false);
				MirrorMaterials(destination, source);
				if (!CopyClockBombAnimationSets(destination, source))
					Log("PROJECTILESTAGE clock bomb AnimSets copy failed.");
				destination->bPauseAnims = false;
				destination->bNoSkeletonUpdate = false;
				destination->bUpdateSkelWhenNotRendered = true;
				destination->bTickAnimNodesWhenNotRendered = true;
				NativeSetTranslation(destination, source->Translation);
				NativeSetRotation(destination, source->Rotation);
				NativeSetScale(destination, source->Scale);
				NativeSetScale3D(destination, source->Scale3D);
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
			RemoteParticleReplayScope replayScope;
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
				bool finite = true;
				for (int axis = 0; axis < 3; ++axis)
					finite = finite && std::isfinite(event.location[axis]) && std::isfinite(event.velocity[axis]);
				if (!finite) continue;
				switch (event.kind)
				{
				case ProjectileEventKind::PepperSpawn:
				case ProjectileEventKind::TeapotSpawn:
				case ProjectileEventKind::WeaponProjectileUpdate:
				{
					if (g_completedRemoteProjectiles.contains(event.projectileId)) break;
					auto found = std::find_if(g_remotePepperVisuals.begin(), g_remotePepperVisuals.end(),
						[&](const RemotePepperVisual& visual) { return visual.projectileId == event.projectileId; });
					if (found != g_remotePepperVisuals.end()
						&& !IsNewerSequence(event.eventSerial, found->lastEventSerial)) break;
					if (found == g_remotePepperVisuals.end())
					{
						UParticleSystem* particle = ResolveProjectileParticle(event.particleTemplateHash);
						if (!particle || !world->MyEmitterPool) break;
						RemotePepperVisual visual{};
						visual.projectileId = event.projectileId;
						visual.particleTemplate = particle;
						visual.particle = world->MyEmitterPool->SpawnEmitter(particle, location, rotation, nullptr, false);
						if (!visual.particle) break;
						visual.scale = std::isfinite(event.extra) && event.extra > 0.0f
							? std::clamp(event.extra, 0.001f, 100.0f) : 1.0f;
						NativeSetScale(visual.particle, visual.scale);
						MakePresentationParticleVisible(visual.particle);
						g_remotePepperVisuals.push_back(visual);
						found = g_remotePepperVisuals.end() - 1;
						static std::uint32_t count = 0;
						if (++count <= 8 || count % 100 == 0)
							Log("PROJECTILESTAGE remote flight id=" + std::to_string(event.projectileId)
								+ ", template=" + ObjectName(particle) + '.');
					}
					found->location = location;
					found->lastEventSerial = event.eventSerial;
					found->velocity = velocity;
					found->rotation = rotation;
					found->lastTick = now;
					found->expiresAt = now + std::chrono::seconds(2);
					break;
				}
				case ProjectileEventKind::WeaponProjectileImpact:
				case ProjectileEventKind::WeaponProjectileRemove:
				{
					if (g_completedRemoteProjectiles.contains(event.projectileId)) break;
					DestroyRemoteWeaponProjectile(event.projectileId);
					g_completedRemoteProjectiles[event.projectileId] = now;
					if (event.kind == ProjectileEventKind::WeaponProjectileImpact && world->MyEmitterPool)
					{
						UParticleSystem* particle = ResolveProjectileParticle(event.particleTemplateHash);
						if (particle)
						{
							UParticleSystemComponent* spawned = world->MyEmitterPool->SpawnEmitter(particle, location, rotation, nullptr, false);
							MakePresentationParticleVisible(spawned);
							TrackRemoteWeaponTransient(spawned, particle, std::chrono::milliseconds(3500));
						}
					}
					break;
				}
				case ProjectileEventKind::NativeWorldDecal:
				{
					if (g_completedRemoteProjectiles.contains(event.projectileId)) break;
					g_completedRemoteProjectiles[event.projectileId] = now;
					SpawnRemoteNativeDecal(world, event);
					break;
				}
				case ProjectileEventKind::NativeWorldEffect:
				{
					if (g_completedRemoteProjectiles.contains(event.projectileId)) break;
					g_completedRemoteProjectiles[event.projectileId] = now;
					UParticleSystem* particle = ResolveProjectileParticle(event.particleTemplateHash);
					if (!particle || !world->MyEmitterPool)
					{
						Log("NATIVEFX RX missing asset hash=" + std::to_string(event.particleTemplateHash) + '.');
						break;
					}
					auto* spawned = world->MyEmitterPool->SpawnEmitter(particle, location, rotation, nullptr, false);
					if (!spawned) break;
					NativeSetScale(spawned, std::clamp(event.extra, 0.001f, 100.0f));
					NativeSetScale3D(spawned, velocity); // Native world-effect scale, not projectile velocity.
					MakePresentationParticleVisible(spawned);
					TrackRemoteWeaponTransient(spawned, particle, std::chrono::milliseconds(30000));
					static std::uint32_t count = 0;
					if (++count <= 16 || count % 100 == 0)
						Log("NATIVEFX RX template=" + ObjectName(particle) + '.');
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
						if ((event.reserved & 0x8000) && visual->actor->SkeletalMeshComponent)
						{
							auto* mesh = visual->actor->SkeletalMeshComponent;
							NativeSetTranslation(mesh, FVector(event.effectParameters[0],
								event.effectParameters[1], event.effectParameters[2]));
							NativeSetScale(mesh, event.effectParameters[3]);
						}
						ApplyRemoteClockBombMaterials(*visual, event);
						ApplyRemoteClockBombAnimation(*visual, event);
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
			for (auto it = g_completedRemoteProjectiles.begin(); it != g_completedRemoteProjectiles.end();)
			{
				if (now - it->second > std::chrono::seconds(10)) it = g_completedRemoteProjectiles.erase(it);
				else ++it;
			}
			auto pepperNext = g_remotePepperVisuals.begin();
			for (auto current = g_remotePepperVisuals.begin();
				current != g_remotePepperVisuals.end(); ++current)
			{
				// A completed PSC can be reused by native Alice before the next
				// network event. Never move or retire a different pool allocation.
				if (!IsLivePresentationParticle(current->particle)
					|| current->particle->bIsCachedInPool
					|| current->particle->Template != current->particleTemplate)
					continue;
				if (now >= current->expiresAt)
				{
					if (current->particle)
						HardStopPresentationParticle(
							current->particle, true);

					continue;
				}
				const float delta = std::clamp(
					std::chrono::duration<float>(
						now - current->lastTick).count(),
					0.0f, 0.1f);
				current->lastTick = now;
				current->location.X +=
					current->velocity.X * delta;
				current->location.Y +=
					current->velocity.Y * delta;
				current->location.Z +=
					current->velocity.Z * delta;
				if (current->particle)
				{
					NativeSetTranslation(current->particle,
						current->location);
					NativeSetRotation(current->particle,
						current->rotation);
					NativeSetScale(current->particle,
						current->scale);
					NativeSetScale3D(current->particle, FVector(
						1.0f, 1.0f, 1.0f));
					NativeForceComponentUpdate(
						current->particle, true);
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
				NativeSetActorRotation(visual.actor, visual.targetRotation);
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

