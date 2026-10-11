		UParticleSystem* FindRemoteDodgeParticleTemplate()
		{
			auto fromSequence = [](UAnimSequence* sequence)
				-> UParticleSystem*
			{
				if (!sequence)
					return nullptr;
				const int32_t count = std::min<int32_t>(
					sequence->Notifies.size(), 256);
				for (int32_t index = 0; index < count; ++index)
				{
					UAnimNotify* notify =
						sequence->Notifies.at(index).Notify;
					if (!notify)
						continue;
					if (notify->IsA(
						UAliceAnimNotify_TriggerAliceDodgeParticle::
							StaticClass()))
					{
						auto* dodge = reinterpret_cast<
							UAliceAnimNotify_TriggerAliceDodgeParticle*>(
								notify);
						if (dodge->AliceDodgeEffectTemplate)
							return dodge->AliceDodgeEffectTemplate;
					}
					if (notify->IsA(
						UAnimNotify_PlayParticleEffect::StaticClass()))
					{
						auto* particle = reinterpret_cast<
							UAnimNotify_PlayParticleEffect*>(notify);
						const std::string name = ObjectName(notify);
						if (particle->PSTemplate
							&& (ContainsCaseInsensitive(name, "dodge")
								|| ContainsCaseInsensitive(
									name, "butter")))
						{
							return particle->PSTemplate;
						}
					}
				}
				return nullptr;
			};

			if (g_remoteAppliedFullBodyNode)
			{
				if (UParticleSystem* particle =
					fromSequence(g_remoteAppliedFullBodyNode->AnimSeq))
				{
					return particle;
				}
			}

			// Dodge starts before the first replicated animation frame can
			// arrive. The corresponding inline notify is nevertheless already
			// loaded with Alice's animation package, so use it as a read-only
			// recipe instead of hard-coding a package/object path.
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return nullptr;
			UParticleSystem* fallback = nullptr;
			const int32_t count = std::min<int32_t>(
				objects->size(), 250000);
			for (int32_t index = 0; index < count; ++index)
			{
				UObject* object = objects->at(index);
				if (!object)
					continue;
				if (object->IsA(
					UAliceAnimNotify_TriggerAliceDodgeParticle::
						StaticClass()))
				{
					auto* dodge = reinterpret_cast<
						UAliceAnimNotify_TriggerAliceDodgeParticle*>(
							object);
					if (!dodge->AliceDodgeEffectTemplate)
						continue;
					const std::string name = ObjectName(object);
					if (ContainsCaseInsensitive(name, "dodge"))
						return dodge->AliceDodgeEffectTemplate;
					fallback = dodge->AliceDodgeEffectTemplate;
				}
				else if (!fallback
					&& object->IsA(
						UAnimNotify_PlayParticleEffect::StaticClass()))
				{
					auto* particle = reinterpret_cast<
						UAnimNotify_PlayParticleEffect*>(object);
					if (!particle->PSTemplate)
						continue;
					const std::string name = ObjectName(object);
					if (ContainsCaseInsensitive(name, "dodge")
						|| ContainsCaseInsensitive(name, "butter"))
					{
						fallback = particle->PSTemplate;
					}
				}
			}
			return fallback;
		}

		UParticleSystemComponent* SpawnRemoteCosmeticParticle(
			AAlicePawn* remote, UParticleSystem* particle,
			bool attachToProxy)
		{
			if (!remote || !particle || !remote->WorldInfo
				|| !remote->WorldInfo->MyEmitterPool)
			{
				return nullptr;
			}
			return remote->WorldInfo->MyEmitterPool->SpawnEmitter(
				particle,
				(!attachToProxy && g_remotePresentation.valid)
					? g_remotePresentation.location
					: remote->Location,
				remote->Rotation,
				attachToProxy ? remote : nullptr, attachToProxy);
		}

		const char* VfxAttachmentCandidateName(
			VfxAttachmentCandidate candidate)
		{
			switch (candidate)
			{
			case VfxAttachmentCandidate::NativeWeapon:
				return "native weapon script";
			case VfxAttachmentCandidate::WeaponTraceSocket:
				return "weapon TraceSocket";
			case VfxAttachmentCandidate::WeaponMuzzleSocket:
				return "weapon muzzle/range socket";
			case VfxAttachmentCandidate::WeaponFirstSocket:
				return "weapon first authored socket";
			case VfxAttachmentCandidate::WeaponTraceWorld:
				return "world at weapon TraceSocket";
			case VfxAttachmentCandidate::AliceHolderSocket:
				return "Alice weapon-holder socket";
			case VfxAttachmentCandidate::AliceRootWorld:
				return "world at Alice root";
			default:
				return "unknown";
			}
		}

		UParticleSystem* FindLoadedParticleSystem(
			const std::vector<std::string>& requiredTokens,
			const std::vector<std::string>& preferredTokens = {})
		{
			UParticleSystem* fallback = nullptr;
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return nullptr;
			for (int32_t index = 0; index < objects->size(); ++index)
			{
				UObject* object = objects->at(index);
				if (!object
					|| !object->IsA(
						UParticleSystem::StaticClass()))
				{
					continue;
				}
				const std::string name = ObjectName(object);
				bool matches = true;
				for (const std::string& token : requiredTokens)
				{
					if (!ContainsCaseInsensitive(
						name, token.c_str()))
					{
						matches = false;
						break;
					}
				}
				if (!matches)
					continue;
				auto* particle =
					reinterpret_cast<UParticleSystem*>(object);
				if (!fallback)
					fallback = particle;
				for (const std::string& preferred : preferredTokens)
				{
					if (ContainsCaseInsensitive(
						name, preferred.c_str()))
						return particle;
				}
			}
			return fallback;
		}

		const std::vector<UStaticMesh*>& ResolveRemoteLeafTrailMeshes()
		{
			if (!g_remoteLeafTrailMeshes.empty())
				return g_remoteLeafTrailMeshes;
			UParticleSystem* leaves = FindLoadedParticleSystem(
				{ "GlideTrail" }, { "Nv_FX_Alice.Particles" });
			if (!leaves)
				return g_remoteLeafTrailMeshes;
			const int32_t emitterCount = std::min<int32_t>(
				leaves->Emitters.size(), 64);
			for (int32_t emitterIndex = 0;
				emitterIndex < emitterCount; ++emitterIndex)
			{
				UParticleEmitter* emitter = leaves->Emitters.at(emitterIndex);
				if (!emitter)
					continue;
				const int32_t lodCount = std::min<int32_t>(
					emitter->LODLevels.size(), 8);
				for (int32_t lodIndex = 0; lodIndex < lodCount; ++lodIndex)
				{
					UParticleLODLevel* lod = emitter->LODLevels.at(lodIndex);
					UParticleModule* typeData = lod ? lod->TypeDataModule : nullptr;
					if (!typeData || !typeData->IsA(
						UParticleModuleTypeDataMesh::StaticClass()))
					{
						continue;
					}
					UStaticMesh* mesh = reinterpret_cast<
						UParticleModuleTypeDataMesh*>(typeData)->Mesh;
					if (mesh && std::find(
						g_remoteLeafTrailMeshes.begin(),
						g_remoteLeafTrailMeshes.end(), mesh)
						== g_remoteLeafTrailMeshes.end())
					{
						g_remoteLeafTrailMeshes.push_back(mesh);
						Log("VFXCAPTURE static leaf mesh=" + ObjectName(mesh) + '.');
					}
					if (!mesh)
						continue;

					const std::string meshName = ObjectName(mesh);
					if (ContainsCaseInsensitive(meshName, "JumpFeather"))
					{
						UMaterialInterface* material = nullptr;
						auto* meshType = reinterpret_cast<
							UParticleModuleTypeDataMesh*>(typeData);
						if (meshType->bOverrideMaterial && lod->RequiredModule)
							material = lod->RequiredModule->Material;
						for (int32_t moduleIndex = 0;
							moduleIndex < lod->Modules.size(); ++moduleIndex)
						{
							UParticleModule* module = lod->Modules.at(moduleIndex);
							if (!module || !module->IsA(
								UParticleModuleMeshMaterial::StaticClass()))
							{
								continue;
							}
							auto* meshMaterial = reinterpret_cast<
								UParticleModuleMeshMaterial*>(module);
							if (meshMaterial->MeshMaterials.size() > 0
								&& meshMaterial->MeshMaterials.at(0))
							{
								material = meshMaterial->MeshMaterials.at(0);
								break;
							}
						}
						if (!material && mesh->LODInfo.size() > 0)
						{
							const FStaticMeshLODInfo& meshLod =
								mesh->LODInfo.at(0);
							if (meshLod.Elements.size() > 0)
								material = meshLod.Elements.at(0).Material;
						}
						const bool alreadyAdded = std::any_of(
							g_remoteFrozenLeafAssets.begin(),
							g_remoteFrozenLeafAssets.end(),
							[mesh](const RemoteFrozenLeafAsset& asset)
							{
								return asset.mesh == mesh;
							});
						if (!alreadyAdded)
						{
							g_remoteFrozenLeafAssets.push_back({ mesh, material });
							Log("VFXCAPTURE frozen leaf asset=" + meshName
								+ ", material=" + ObjectName(material) + '.');
						}
					}
					else if (!ContainsCaseInsensitive(meshName, "Tornado")
						&& std::none_of(
						g_remoteFrozenAccentAssets.begin(),
						g_remoteFrozenAccentAssets.end(),
						[mesh](const RemoteFrozenLeafAsset& asset)
						{
							return asset.mesh == mesh;
						}))
					{
						g_remoteFrozenAccentAssets.push_back({ mesh, nullptr, false });
					}
				}
			}
			Log("VFXCAPTURE static leaf meshes resolved="
				+ std::to_string(g_remoteLeafTrailMeshes.size())
				+ ", leaves=" + std::to_string(g_remoteFrozenLeafAssets.size())
				+ ", accents=" + std::to_string(g_remoteFrozenAccentAssets.size())
				+ '.');
			return g_remoteLeafTrailMeshes;
		}

		void DestroyRemoteStaticLeafTrail()
		{
			const auto destroyMarkers = [](auto& markers)
			{
				for (ADynamicSMActor_Spawnable* marker : markers)
				{
					if (!marker || !IsLiveUObject(marker)
						|| (g_currentWorld
							&& marker->WorldInfo != g_currentWorld))
					{
						continue;
					}
					NativeSetActorHidden(marker, true);
					marker->bNoDelete = false;
					NativeDestroyActor(marker);
				}
				markers.clear();
			};
			destroyMarkers(g_remoteLeafTrailMarkers);
			destroyMarkers(g_remoteAccentTrailMarkers);
			g_remoteLeafTrailMarkers.clear();
			g_nextRemoteLeafTrailMarker = {};
			g_remoteLeafTrailSample = 0;
		}

		float NextRemoteLeafTrailRandom()
		{
			g_remoteLeafTrailRandom =
				g_remoteLeafTrailRandom * 1664525u + 1013904223u;
			return static_cast<float>(
				(g_remoteLeafTrailRandom >> 8) & 0x00FFFFFFu)
				/ static_cast<float>(0x01000000u);
		}

		void CaptureRemoteNativeGlideMaterials(
			UParticleSystemComponent* particle)
		{
			if (!IsLivePresentationParticle(particle))
				return;
			ResolveRemoteLeafTrailMeshes();
			for (int32_t index = 0; index < std::min<int32_t>(
				particle->SMComponents.size(), 64); ++index)
			{
				UStaticMeshComponent* component =
					particle->SMComponents.at(index);
				if (!IsLiveUObject(component) || !component->StaticMesh
					|| !ContainsCaseInsensitive(
						ObjectName(component->StaticMesh), "JumpFeather"))
				{
					continue;
				}
				UMaterialInterface* material = component->GetMaterial(0);
				if (!material && component->Materials.size() > 0)
					material = component->Materials.at(0);
				if (!material)
					continue;
				const bool leafMesh = ContainsCaseInsensitive(
					ObjectName(component->StaticMesh), "JumpFeather");
				auto& assets = leafMesh
					? g_remoteFrozenLeafAssets
					: g_remoteFrozenAccentAssets;
				for (RemoteFrozenLeafAsset& asset : assets)
				{
					if (asset.mesh != component->StaticMesh
						|| asset.ownsMaterialCopy)
					{
						continue;
					}
					UMaterialInterface* frozenMaterial = material;
					if (material->IsA(UMaterialInstance::StaticClass()))
					{
						UMaterialInstance* duplicate = reinterpret_cast<
							UMaterialInstance*>(material)->DuplicateInstance();
						if (duplicate)
							frozenMaterial = duplicate;
					}
					asset.material = frozenMaterial;
					asset.ownsMaterialCopy = frozenMaterial != material;
					Log("VFXCAPTURE captured live trail material="
						+ ObjectName(material) + ", frozen="
						+ ObjectName(frozenMaterial) + ", mesh="
						+ ObjectName(asset.mesh) + '.');
				}
			}
		}

		bool IsRemoteNativeGlideTrail(
			UParticleSystemComponent* particle)
		{
			return IsLivePresentationParticle(particle)
				&& particle->Template
				&& ContainsCaseInsensitive(
					ObjectName(particle->Template),
					"Nv_FX_Alice.Particles.GlideTrail");
		}

		void TrackRemoteNativeGlideTrail(
			UParticleSystemComponent* particle, AWorldInfo* world)
		{
			if (!IsRemoteNativeGlideTrail(particle) || !world)
				return;
			for (RemoteNativeGlideTrail& tracked :
				g_remoteNativeGlideTrails)
			{
				if (tracked.component != particle)
					continue;
				tracked.particleTemplate = particle->Template;
				tracked.world = world;
				return;
			}
			g_remoteNativeGlideTrails.push_back({
				particle, particle->Template, world, Clock::now() });
		}

		bool IsPresentationParticleActiveInWorldPool(
			UParticleSystemComponent* particle, AWorldInfo* world)
		{
			if (!particle || !world || !IsLiveUObject(world)
				|| !world->MyEmitterPool
				|| !IsLiveUObject(world->MyEmitterPool))
			{
				return false;
			}
			AEmitterPool* pool = world->MyEmitterPool;
			for (int32_t index = 0; index < std::min<int32_t>(
				pool->ActiveComponents.size(), 2048); ++index)
			{
				if (pool->ActiveComponents.at(index) == particle)
					return true;
			}
			return false;
		}

		bool ForceRetireRemoteNativeGlideTrail(
			const RemoteNativeGlideTrail& tracked)
		{
			UParticleSystemComponent* particle = tracked.component;
			if (!IsRemoteNativeGlideTrail(particle)
				|| particle->Template != tracked.particleTemplate)
			{
				return false;
			}

			// Nv GlideTrail owns PhysX particles outside the ordinary PSC render
			// state. Returning a still-spawning component to AliceGameEmitterPool
			// can therefore leave those particles alive; a later pool reuse wakes
			// them at every old transform and produces the delayed "leaf shower".
			// Kill only a known-live, active proxy instance. Completed/stale UE3
			// emitter storage keeps the conservative flag-only retirement path.
			const bool forceKillSafe =
				IsPresentationParticleActiveInWorldPool(
					particle, tracked.world)
				&& !particle->bIsCachedInPool
				&& particle->bIsActive
				&& !particle->bWasCompleted
				&& particle->EmitterInstances.size() > 0;
			if (forceKillSafe)
			{
				particle->bSuppressSpawning = true;
				particle->KillParticlesForced();
				if (!particle->bWasDeactivated)
					particle->DeactivateSystem();
			}
			HardStopPresentationParticle(particle, true);
			return forceKillSafe;
		}

		void RetireTrackedRemoteNativeGlideTrails(
			AWorldInfo* world, UParticleSystemComponent* keep,
			const char* reason)
		{
			std::size_t killed = 0;
			std::size_t retired = 0;
			std::size_t forgotten = 0;
			auto next = g_remoteNativeGlideTrails.begin();
			for (auto current = g_remoteNativeGlideTrails.begin();
				current != g_remoteNativeGlideTrails.end(); ++current)
			{
				if (current->component == keep
					|| (world && current->world != world))
				{
					if (next != current)
						*next = *current;
					++next;
					continue;
				}
				if (IsRemoteNativeGlideTrail(current->component)
					&& current->component->Template
						== current->particleTemplate)
				{
					if (ForceRetireRemoteNativeGlideTrail(*current))
						++killed;
					++retired;
				}
				else
				{
					++forgotten;
				}
				if (g_remoteNativeGlideCurrent == current->component)
				{
					g_remoteNativeGlideCurrent = nullptr;
					g_remoteNativeGlideCurrentSince = {};
				}
			}
			g_remoteNativeGlideTrails.erase(
				next, g_remoteNativeGlideTrails.end());
			if (retired > 0 || forgotten > 0)
			{
				Log("VFXGUARD native GlideTrail cleanup reason="
					+ std::string(reason ? reason : "unknown")
					+ ", killed=" + std::to_string(killed)
					+ ", retired=" + std::to_string(retired)
					+ ", forgotten=" + std::to_string(forgotten)
					+ ", retained="
					+ std::to_string(g_remoteNativeGlideTrails.size())
					+ '.');
			}
		}

		void RetireRemoteNativeGlideTrails(
			AAlicePawn* remote, bool keepNewest)
		{
			if (!remote || !remote->WorldInfo
				|| !remote->WorldInfo->MyEmitterPool)
			{
				return;
			}
			AEmitterPool* pool = remote->WorldInfo->MyEmitterPool;
			std::vector<UParticleSystemComponent*> matches;
			for (int32_t index = 0; index < std::min<int32_t>(
				pool->ActiveComponents.size(), 2048); ++index)
			{
				UParticleSystemComponent* particle =
					pool->ActiveComponents.at(index);
				if (!IsLivePresentationParticle(particle)
					|| !particle->Template
					|| !ContainsCaseInsensitive(
						ObjectName(particle->Template),
						"Nv_FX_Alice.Particles.GlideTrail")
					|| PresentationParticleBase(pool, particle) != remote)
				{
					continue;
				}
				CaptureRemoteNativeGlideMaterials(particle);
				TrackRemoteNativeGlideTrail(particle, remote->WorldInfo);
				matches.push_back(particle);
			}
			UParticleSystemComponent* keep = nullptr;
			if (keepNewest && !matches.empty())
			{
				keep = matches.back();
				if (g_remoteNativeGlideCurrent != keep)
				{
					g_remoteNativeGlideCurrent = keep;
					g_remoteNativeGlideCurrentSince = Clock::now();
				}
			}
			RetireTrackedRemoteNativeGlideTrails(
				remote->WorldInfo, keep,
				keepNewest ? "glide-start" : "glide-end");
			if (!keepNewest)
			{
				g_remoteNativeGlideCurrent = nullptr;
				g_remoteNativeGlideCurrentSince = {};
			}
			if (!matches.empty() && g_config.vfxLifecycleTrace)
			{
				Log("VFXCAPTURE observed native GlideTrail instances="
					+ std::to_string(matches.size())
					+ (keep ? " (kept newest)." : "."));
			}
		}

		void TickRemoteNativeGlideTrailGuard(AAlicePawn* remote)
		{
			const Clock::time_point now = Clock::now();
			if (now < g_nextRemoteNativeGlideSweep)
				return;
			g_nextRemoteNativeGlideSweep =
				now + std::chrono::seconds(2);

			if (!remote || !remote->WorldInfo
				|| !remote->WorldInfo->MyEmitterPool)
			{
				RetireTrackedRemoteNativeGlideTrails(
					nullptr, nullptr, "missing-proxy");
				return;
			}

			AEmitterPool* pool = remote->WorldInfo->MyEmitterPool;
			std::vector<UParticleSystemComponent*> owned;
			for (int32_t index = 0; index < std::min<int32_t>(
				pool->ActiveComponents.size(), 2048); ++index)
			{
				UParticleSystemComponent* particle =
					pool->ActiveComponents.at(index);
				if (!IsRemoteNativeGlideTrail(particle)
					|| PresentationParticleBase(pool, particle) != remote)
				{
					continue;
				}
				TrackRemoteNativeGlideTrail(particle, remote->WorldInfo);
				owned.push_back(particle);
			}

			UParticleSystemComponent* keep = nullptr;
			if (g_remoteGlideVfxActive)
			{
				if (IsRemoteNativeGlideTrail(g_remoteNativeGlideCurrent)
					&& !g_remoteNativeGlideCurrent->bIsCachedInPool)
				{
					keep = g_remoteNativeGlideCurrent;
				}
				else if (!owned.empty())
				{
					keep = owned.back();
					g_remoteNativeGlideCurrent = keep;
					g_remoteNativeGlideCurrentSince = now;
				}

				// A normal Alice glide is short. Cap only the native PhysX leaf PSC;
				// the deterministic static trail remains available for an unusually
				// long fall and a fresh component is selected on the next glide.
				if (keep && g_remoteNativeGlideCurrentSince
						!= Clock::time_point{}
					&& now - g_remoteNativeGlideCurrentSince
						>= std::chrono::seconds(12))
				{
					Log("VFXGUARD native GlideTrail reached 12s safety cap.");
					keep = nullptr;
				}
			}

			RetireTrackedRemoteNativeGlideTrails(
				remote->WorldInfo, keep,
				keep ? "periodic-active-sweep"
					: "periodic-inactive-sweep");
		}

		void SpawnRemoteStaticLeafTrailSample(AAlicePawn* remote)
		{
			if (!remote || !remote->WorldInfo)
				return;
			const std::vector<UStaticMesh*>& meshes =
				ResolveRemoteLeafTrailMeshes();
			if (meshes.empty())
				return;

			constexpr std::size_t MaxStaticLeafMarkers = 256;
			constexpr std::size_t MaxStaticAccentMarkers = 96;
			constexpr int FrozenLeavesPerSample = 4;
			const bool spawnAccent = (g_remoteLeafTrailSample++ % 2u) == 0u;
			const int markerCount = FrozenLeavesPerSample
				+ (spawnAccent && !g_remoteFrozenAccentAssets.empty() ? 1 : 0);
			for (int markerIndex = 0; markerIndex < markerCount; ++markerIndex)
			{
				const bool isFrozenLeaf = markerIndex < FrozenLeavesPerSample
					&& !g_remoteFrozenLeafAssets.empty();
				auto& markerPool = isFrozenLeaf
					? g_remoteLeafTrailMarkers
					: g_remoteAccentTrailMarkers;
				const std::size_t markerLimit = isFrozenLeaf
					? MaxStaticLeafMarkers
					: MaxStaticAccentMarkers;
				while (markerPool.size() >= markerLimit)
				{
					ADynamicSMActor_Spawnable* oldest =
						markerPool.front();
					markerPool.pop_front();
					if (oldest && IsLiveUObject(oldest)
						&& oldest->WorldInfo == remote->WorldInfo)
					{
						NativeSetActorHidden(oldest, true);
						oldest->bNoDelete = false;
						NativeDestroyActor(oldest);
					}
				}

				const float offsetX =
					(NextRemoteLeafTrailRandom() - 0.5f) * 34.0f;
				const float offsetY =
					(NextRemoteLeafTrailRandom() - 0.5f) * 34.0f;
				const float offsetZ =
					(NextRemoteLeafTrailRandom() - 0.5f) * 28.0f;
				const FVector location(
					remote->Location.X + offsetX,
					remote->Location.Y + offsetY,
					remote->Location.Z + offsetZ);
				const FRotator rotation(
					static_cast<int32_t>(NextRemoteLeafTrailRandom() * 65535.0f),
					static_cast<int32_t>(NextRemoteLeafTrailRandom() * 65535.0f),
					static_cast<int32_t>(NextRemoteLeafTrailRandom() * 65535.0f));
				UStaticMesh* mesh = nullptr;
				UMaterialInterface* material = nullptr;
				float scale = 0.095f
					+ NextRemoteLeafTrailRandom() * 0.055f;
				if (isFrozenLeaf)
				{
					const RemoteFrozenLeafAsset& asset =
						g_remoteFrozenLeafAssets[
							g_remoteLeafTrailRandom
							% g_remoteFrozenLeafAssets.size()];
					mesh = asset.mesh;
					material = asset.material;
					// JumpFeather is authored much smaller than the symbolic
					// mesh emitters.  Keep it readable while the accents stay
					// deliberately subtle.
					scale = 0.86f
						+ NextRemoteLeafTrailRandom() * 0.38f;
				}
				else if (!g_remoteFrozenAccentAssets.empty())
				{
					const RemoteFrozenLeafAsset& asset =
						g_remoteFrozenAccentAssets[
						g_remoteLeafTrailRandom
						% g_remoteFrozenAccentAssets.size()];
					mesh = asset.mesh;
					material = asset.material;
					if (mesh && ContainsCaseInsensitive(
						ObjectName(mesh), "Tornado"))
					{
						scale = 0.22f
							+ NextRemoteLeafTrailRandom() * 0.12f;
					}
				}
				else
				{
					mesh = meshes[g_remoteLeafTrailRandom % meshes.size()];
				}
				if (!mesh)
					continue;
				AActor* spawned = NativeSpawn(remote,
					ADynamicSMActor_Spawnable::StaticClass(),
					remote, FName(), location, rotation, nullptr, true);
				if (!spawned || !spawned->IsA(
					ADynamicSMActor_Spawnable::StaticClass()))
				{
					if (spawned)
						NativeDestroyActor(spawned);
					continue;
				}
				auto* marker = reinterpret_cast<
					ADynamicSMActor_Spawnable*>(spawned);
				marker->bNoDelete = false;
				marker->bStatic = false;
				marker->bMovable = true;
				marker->bCanBeDamaged = false;
				marker->bCollideActors = false;
				marker->bCollideWorld = false;
				marker->bBlockActors = false;
				marker->Physics = EPhysics::PHYS_None;
				marker->SetCollision(false, false, true);
				marker->SetCollisionType(ECollisionType::COLLIDE_NoCollision);
				marker->SetStaticMesh(mesh,
					FVector(0.0f, 0.0f, 0.0f), FRotator(0, 0, 0),
					FVector(scale, scale, scale));
				if (material && marker->StaticMeshComponent)
					marker->StaticMeshComponent->SetMaterial(0, material);
				marker->SetLocationNoCheck(location);
				NativeSetActorRotation(marker, rotation);
				marker->Location = location;
				marker->Rotation = rotation;
				NativeSetActorHidden(marker, false);
				NativeForceUpdateComponents(marker, false, true);
				markerPool.push_back(marker);
			}
		}

		bool WeaponMeshHasSocket(AWeaponForAlice* weapon,
			const FName& name)
		{
			if (!weapon || !weapon->Mesh
				|| !weapon->Mesh->SkeletalMesh
				|| !name.IsValid())
			{
				return false;
			}
			USkeletalMesh* mesh = weapon->Mesh->SkeletalMesh;
			for (int32_t index = 0;
				index < mesh->Sockets.size(); ++index)
			{
				USkeletalMeshSocket* socket =
					mesh->Sockets.at(index);
				if (socket && socket->SocketName.IsValid()
					&& _stricmp(
						socket->SocketName.ToString().c_str(),
						name.ToString().c_str()) == 0)
				{
					return true;
				}
			}
			return false;
		}

		bool WeaponMeshHasBone(AWeaponForAlice* weapon,
			const FName& name)
		{
			return weapon && weapon->Mesh && name.IsValid()
				&& weapon->Mesh->MatchRefBone(name) >= 0;
		}

		bool WeaponMeshHasAttachPoint(AWeaponForAlice* weapon,
			const FName& name)
		{
			return WeaponMeshHasSocket(weapon, name)
				|| WeaponMeshHasBone(weapon, name);
		}

		FName FirstWeaponSocket(AWeaponForAlice* weapon)
		{
			if (!weapon || !weapon->Mesh
				|| !weapon->Mesh->SkeletalMesh)
			{
				return FName();
			}
			USkeletalMesh* mesh = weapon->Mesh->SkeletalMesh;
			for (int32_t index = 0;
				index < mesh->Sockets.size(); ++index)
			{
				USkeletalMeshSocket* socket =
					mesh->Sockets.at(index);
				if (socket && socket->SocketName.IsValid())
					return socket->SocketName;
			}
			return FName();
		}

		FName ResolveCandidateWeaponSocket(
			AWeaponForAlice* weapon,
			VfxAttachmentCandidate candidate)
		{
			if (!weapon)
				return FName();
			auto usable = [weapon](const FName& name)
			{
				return WeaponMeshHasAttachPoint(weapon, name);
			};
			switch (candidate)
			{
			case VfxAttachmentCandidate::WeaponTraceSocket:
			case VfxAttachmentCandidate::WeaponTraceWorld:
				if (weapon->IsA(AHobbyHorse::StaticClass()))
				{
					const FName radiusDamage("RadiusDamage");
					if (usable(radiusDamage))
						return radiusDamage;
				}
				if (usable(weapon->TraceSocket))
					return weapon->TraceSocket;
				break;
			case VfxAttachmentCandidate::WeaponMuzzleSocket:
				if (weapon->IsA(
					AWeaponForAliceRange::StaticClass()))
				{
					auto* range = reinterpret_cast<
						AWeaponForAliceRange*>(weapon);
					if (usable(range->WeaponMuzzleSocket))
						return range->WeaponMuzzleSocket;
				}
				if (usable(weapon->MuzzleFlashSocket))
					return weapon->MuzzleFlashSocket;
				if (usable(weapon->RangeAttackSocket))
					return weapon->RangeAttackSocket;
				for (int32_t index = 0;
					index < weapon->RangeAttackSocketArray.size();
					++index)
				{
					const FName socket =
						weapon->RangeAttackSocketArray.at(index);
					if (usable(socket))
						return socket;
				}
				if (weapon->IsA(AEyeStaff::StaticClass()))
				{
					auto* pepper =
						reinterpret_cast<AEyeStaff*>(weapon);
					for (int32_t index = 0;
						index < pepper->FireSocketArray.size();
						++index)
					{
						const FName socket =
							pepper->FireSocketArray.at(index);
						if (usable(socket))
							return socket;
					}
				}
				for (const char* name : {
					"MuzzleFX_Gen", "RangeRefSocket",
					"Weapon_Muzzle1", "Muzzle_Light" })
				{
					const FName socket(name);
					if (usable(socket))
						return socket;
				}
				break;
			case VfxAttachmentCandidate::WeaponFirstSocket:
				return FirstWeaponSocket(weapon);
			default:
				break;
			}
			return FirstWeaponSocket(weapon);
		}

		UParticleSystemComponent* SpawnWeaponParticleCandidate(
			AWeaponForAlice* weapon, UParticleSystem* particle,
			VfxAttachmentCandidate candidate)
		{
			if (!weapon || !particle || !weapon->WorldInfo
				|| !weapon->WorldInfo->MyEmitterPool)
			{
				return nullptr;
			}
			AEmitterPool* pool =
				weapon->WorldInfo->MyEmitterPool;
			if (candidate
				== VfxAttachmentCandidate::NativeWeapon)
			{
				return nullptr;
			}
			if (candidate
				== VfxAttachmentCandidate::AliceRootWorld)
			{
				return pool->SpawnEmitter(
					particle,
					g_remotePawn
						? g_remotePawn->Location : weapon->Location,
					g_remotePawn
						? g_remotePawn->Rotation : weapon->Rotation,
					nullptr, false);
			}
			if (candidate
				== VfxAttachmentCandidate::AliceHolderSocket
				&& g_remotePawn && g_remotePawn->Mesh)
			{
				return pool->SpawnEmitterMeshAttachment(
					particle, g_remotePawn->Mesh,
					g_remotePresentationWeaponSocket,
					g_remotePresentationWeaponSocket.IsValid(),
					FVector(0.0f, 0.0f, 0.0f),
					FRotator(0, 0, 0));
			}
			if (!weapon->Mesh)
				return nullptr;
			const FName socket =
				ResolveCandidateWeaponSocket(weapon, candidate);
			if (candidate
				== VfxAttachmentCandidate::WeaponTraceWorld)
			{
				FVector location = weapon->Location;
				FRotator rotation = weapon->Rotation;
				if (socket.IsValid())
				{
					if (WeaponMeshHasSocket(weapon, socket))
					{
						weapon->Mesh->
							GetSocketWorldLocationAndRotation(
								socket, 0, location, rotation);
					}
					else if (WeaponMeshHasBone(weapon, socket))
					{
						location = weapon->Mesh->
							GetBoneLocation(socket, 0);
						rotation = UObject::QuatToRotator(
							weapon->Mesh->
								GetBoneQuaternion(socket, 0));
					}
				}
				Log("VFXSTAGE weapon world particle="
					+ ObjectName(particle)
					+ ", socket="
					+ (socket.IsValid()
						? socket.ToString()
						: std::string("<invalid>"))
					+ ", location=("
					+ std::to_string(location.X) + ','
					+ std::to_string(location.Y) + ','
					+ std::to_string(location.Z) + ").");
				return pool->SpawnEmitter(
					particle, location, rotation,
					nullptr, false);
			}
			return pool->SpawnEmitterMeshAttachment(
				particle, weapon->Mesh, socket,
				WeaponMeshHasSocket(weapon, socket),
				FVector(0.0f, 0.0f, 0.0f),
				FRotator(0, 0, 0));
		}

		void MakePresentationParticleVisible(
			UParticleSystemComponent* component)
		{
			if (!IsLivePresentationParticle(component))
				return;
			const bool needsActivation = !component->bIsActive
				|| component->bWasCompleted
				|| component->bWasDeactivated
				|| component->bForcedInActive;
			g_retiredPresentationParticles.erase(
				std::remove_if(
					g_retiredPresentationParticles.begin(),
					g_retiredPresentationParticles.end(),
					[&](const RetiredPresentationParticle& retired)
					{
						return retired.component == component;
					}),
				g_retiredPresentationParticles.end());
			component->bOwnerNoSee = false;
			component->bOnlyOwnerSee = false;
			component->bIgnoreOwnerHidden = true;
			component->bForceTranslucency = false;
			component->ForceTranslucencyAlpha = 1.0f;
			component->ForceTranslucencyTargetAlpha = 1.0f;
			component->bSuppressSpawning = false;
			component->bWasDeactivated = false;
			component->bWasCompleted = false;
			component->bForcedInActive = false;
			component->bSkipUpdateDynamicDataDuringTick = false;
			NativeSetComponentHidden(component, false);
			component->HiddenGame = false;
			component->CustomTimeDilation = 1.0f;
			component->bUpdateComponentInTick = true;
			if (needsActivation)
				NativeActivateSystem(component, false);
			AuditPresentationParticle(component, false);
			TracePresentationParticleState("visible", component);
		}

		void SetRemoteAttackTrail(bool active)
		{
			if (!g_remotePresentationWeapon
				|| active == g_remoteAttackTrailActive)
			{
				return;
			}
			g_remoteAttackTrailActive = active;
			g_remotePresentationWeapon->
				eventNotifyMeleeAttackTraceParticleChange(active);
			if (g_remotePresentationWeapon->
				FlushParticleComponent)
			{
				if (active)
				MakePresentationParticleVisible(
					g_remotePresentationWeapon->
						FlushParticleComponent);
				else
					HardStopPresentationParticle(
						g_remotePresentationWeapon->
							FlushParticleComponent, true);
			}
			if (active)
			{
				UParticleSystem* trail =
					g_remotePresentationWeapon->
						eventGetWeaponLeveDateTrails();
				if (!trail)
					trail =
						g_remotePresentationWeapon->TracePSCTemplate;
				if (!trail
					&& g_remotePresentationWeaponType
						== static_cast<std::uint8_t>(
							EAliceWeaponType::EAWT_VorpalBlade))
				{
					const bool dlcMesh =
						g_remotePresentationWeapon->Mesh
						&& ContainsCaseInsensitive(
							ObjectName(
								g_remotePresentationWeapon
									->Mesh->SkeletalMesh),
							"dlc");
					trail = FindLoadedParticleSystem(
						{ "VorpalBlade", "BladeTrail" },
						{ dlcMesh ? "DLC" : "Lv01" });
				}
				bool authoredTrail = false;
				if (g_remoteAppliedFullBodyNode && g_remoteAppliedFullBodyNode->AnimSeq)
				{
					const auto& notifies = g_remoteAppliedFullBodyNode->AnimSeq->Notifies;
					for (int32_t index = 0; index < notifies.size(); ++index)
					{
						UAnimNotify* notify = notifies.at(index).Notify;
						authoredTrail = authoredTrail || (notify && notify->IsA(UAnimNotify_Trails::StaticClass()));
					}
				}
				if (!authoredTrail && trail && g_remotePresentationWeapon->Mesh
					&& g_remotePresentationWeapon->WorldInfo
					&& g_remotePresentationWeapon->WorldInfo
						->MyEmitterPool)
				{
					if (g_remoteAttackTrailParticle)
						HardStopPresentationParticle(
							g_remoteAttackTrailParticle, true);
					constexpr VfxAttachmentCandidate
						AuthoredTrailCandidate =
							VfxAttachmentCandidate::
								WeaponTraceSocket;
					g_remoteAttackTrailParticle =
						SpawnWeaponParticleCandidate(
							g_remotePresentationWeapon,
							trail, AuthoredTrailCandidate);
					MakePresentationParticleVisible(
						g_remoteAttackTrailParticle);
					g_lastRemoteVfx =
						"melee trail: "
						+ ObjectName(trail)
						+ " @ "
						+ VfxAttachmentCandidateName(
							AuthoredTrailCandidate)
						+ (g_remoteAttackTrailParticle
							? " [spawned]" : " [failed]");
					Log("VFXSTAGE melee direct template="
						+ ObjectName(trail)
						+ ", candidate="
						+ VfxAttachmentCandidateName(
							AuthoredTrailCandidate)
						+ ", traceSocket="
						+ (g_remotePresentationWeapon
							->TraceSocket.IsValid()
							? g_remotePresentationWeapon
								->TraceSocket.ToString()
							: std::string("<invalid>"))
						+ ", spawned="
						+ (g_remoteAttackTrailParticle
							? "yes." : "no."));
				}
				g_remoteAttackTrailUntil =
					Clock::now() + std::chrono::milliseconds(700);
			}
			else
			{
				if (g_remoteAttackTrailParticle)
					HardStopPresentationParticle(
						g_remoteAttackTrailParticle, true);
				g_remoteAttackTrailParticle = nullptr;
				g_remoteAttackTrailUntil = {};
			}
			Log(std::string("VFXSTAGE remote attack trail=")
				+ (active ? "on." : "off."));
		}

		void ApplyRemoteVisualAction(AAlicePawn* remote,
			const PlayerStatePayload& state)
		{
			if (!remote || state.actionSerial == 0
				|| state.actionSerial == g_lastRemoteActionSerial)
			{
				return;
			}

			g_lastRemoteActionSerial = state.actionSerial;
			const auto action = static_cast<PlayerAction>(state.action);
			if (g_config.actionTrace)
			{
				Log("VFXSTAGE action serial="
					+ std::to_string(state.actionSerial)
					+ ", action="
					+ std::to_string(static_cast<int>(state.action))
					+ ", specialMove="
					+ std::to_string(state.specialMove) + '.');
			}

			switch (action)
			{
			case PlayerAction::MeleeAttack:
			case PlayerAction::WeaponAttack:
				if (g_remotePresentationWeaponType
					== static_cast<std::uint8_t>(
						EAliceWeaponType::EAWT_VorpalBlade))
				{
					// Some replicated attack graphs enter after their
					// ToggleAttackTrace notify. The action edge is a reliable
					// fallback for the knife trail.
					SetRemoteAttackTrail(false);
					SetRemoteAttackTrail(true);
				}
				break;
			case PlayerAction::StartFire:
				SetRemoteMuzzleFlash(g_remotePresentationWeapon, true);
				break;
			case PlayerAction::StopFire:
				SetRemoteMuzzleFlash(g_remotePresentationWeapon, false);
				break;
			case PlayerAction::Dodge:
				// Authored dodge notify drives the native cosmetic emitter.
				break;
			case PlayerAction::ShrinkEnter:
			case PlayerAction::ShrinkLeave:
			{
				UParticleSystem* particle =
					action == PlayerAction::ShrinkEnter
						? remote->StartShrink : remote->EndShrink;
				UParticleSystemComponent* spawned =
					SpawnRemoteCosmeticParticle(
						remote, particle, true);
				Log(std::string("VFXSTAGE remote shrink ")
					+ (action == PlayerAction::ShrinkEnter
						? "enter" : "leave")
					+ " template=" + ObjectName(particle)
					+ ", spawned="
					+ (spawned ? "yes." : "no."));
				break;
			}
			default:
				break;
			}
		}

		std::unordered_map<UParticleSystemComponent*, UParticleSystem*> SnapshotNotifyParticles(AWorldInfo* world)
		{
			std::unordered_map<UParticleSystemComponent*, UParticleSystem*> result;
			if (world && world->MyEmitterPool)
			{
				const auto& components = world->MyEmitterPool->ActiveComponents;
				for (int32_t index = 0; index < components.size(); ++index)
				{
					auto* component = components.at(index);
					if (component) result.emplace(component, component->Template);
				}
			}
			auto collect = [&](AActor* actor)
			{
				if (!actor || !IsLiveUObject(actor)) return;
				for (int32_t index = 0; index < actor->AllComponents.size(); ++index)
				{
					UActorComponent* component = actor->AllComponents.at(index);
					if (component && component->IsA(UParticleSystemComponent::StaticClass()))
					{
						auto* particle = reinterpret_cast<UParticleSystemComponent*>(component);
						if (particle->bIsActive) result.emplace(particle, particle->Template);
					}
				}
			};
			collect(g_remotePawn); collect(g_remotePresentationWeapon);
			return result;
		}

		bool RetainNativeNotifyParticles(AWorldInfo* world,
			const std::unordered_map<UParticleSystemComponent*, UParticleSystem*>& before)
		{
			bool spawned = false;
			for (const auto& [component, particle] : SnapshotNotifyParticles(world))
			{
				const auto previous = before.find(component);
				if (previous != before.end() && previous->second == particle) continue;
				MakePresentationParticleVisible(component);
				TrackRemoteWeaponTransient(component, particle, std::chrono::milliseconds(3500));
				spawned = true;
			}
			return spawned;
		}

		bool ReplayNativeParticleNotify(AAlicePawn* remote, UAnimNodeSequence* node,
			UAnimNotify_PlayParticleEffect* notify, UParticleSystem* particle,
			USkeletalMeshComponent* attachMesh)
		{
			if (!remote || !node || !notify || !attachMesh) return false;
			const auto before = SnapshotNotifyParticles(remote->WorldInfo);
			UParticleSystem* original = notify->PSTemplate;
			USkeletalMeshComponent* originalMesh = node->SkelComponent;
			notify->PSTemplate = particle;
			node->SkelComponent = attachMesh;
			const bool called = InvokeNativeVisualNotify(notify, node, 0, 0.0f, 0.0f, 0.0f);
			node->SkelComponent = originalMesh;
			notify->PSTemplate = original;
			const bool spawned = RetainNativeNotifyParticles(remote->WorldInfo, before);
			if (g_config.actionTrace)
				Log("VFXSTAGE native particle notify=" + ObjectName(notify)
					+ ", template=" + ObjectName(particle) + ", spawned=" + (spawned ? "yes." : "no."));
			return called && spawned;
		}

		void ReplayNativeTrailNotify(UAnimNodeSequence* node, ActiveVisualTrail& trail, int phase, float time)
		{
			if (!node || !IsLiveUObject(node) || !node->SkelComponent || !trail.notify || !IsLiveUObject(trail.notify)) return;
			UAnimNotify_Trails* notify = trail.notify;
			const auto* package = SelectWeaponPresentationPackage(g_remotePresentationWeapon);
			RemoteParticleReplayScope replayScope;
			const auto before = SnapshotNotifyParticles(g_currentWorld);
			UParticleSystem* originalTemplate = notify->PSTemplate;
			const bool originalUseLevel = notify->UseAliceLevelDataParticle;
			const float originalStart = notify->LastStartTime, originalEnd = notify->EndTime;
			const float originalTime = notify->CurrentTime, originalStep = notify->TimeStep;
			UAnimNodeSequence* originalNode = notify->AnimNodeSeq;
			if (originalUseLevel && package && package->WLDP_Particle_Trail)
			{
				notify->PSTemplate = package->WLDP_Particle_Trail;
			}
			notify->LastStartTime = trail.startTime; notify->EndTime = trail.endTime;
			notify->CurrentTime = trail.previousTime;
			const bool called = InvokeNativeVisualNotify(notify, node, phase, time,
				(std::max)(0.0f, time - trail.previousTime), trail.endTime - trail.startTime);
			notify->PSTemplate = originalTemplate; notify->UseAliceLevelDataParticle = originalUseLevel;
			notify->LastStartTime = originalStart; notify->EndTime = originalEnd;
			notify->CurrentTime = originalTime; notify->TimeStep = originalStep;
			notify->AnimNodeSeq = originalNode;
			trail.previousTime = time;
			const bool spawned = RetainNativeNotifyParticles(g_currentWorld, before);
			if (phase == 0 && g_config.actionTrace)
				Log("VFXSTAGE native trail=" + ObjectName(notify) + ", called=" + (called ? "yes" : "no")
					+ ", spawned=" + (spawned ? "yes." : "no."));
		}

		void FinishRemoteVisualNotifyTrails()
		{
			for (auto& channel : g_remoteVisualNotifyChannels)
			{
				for (auto& trail : channel.trails)
					ReplayNativeTrailNotify(channel.cursor.node, trail, 2, trail.previousTime);
				channel = {};
			}
		}

		bool ReplayNativeDodgeNotify(AAlicePawn* remote, UAnimNodeSequence* node, UAnimNotify* notify)
		{
			if (!remote || !node || !remote->WorldInfo) return false;
			if (!IsLiveUObject(g_remoteNativeDodgeEmitter)
				|| g_remoteNativeDodgeEmitter->WorldInfo != remote->WorldInfo)
			{
				// Alice already creates a dedicated trace actor for this pawn.
				// Reuse it so the native flight lifecycle owns one emitter.
				auto* existing = remote->DodgeEmitter;
				auto* local = GetLocalPawn();
				if (IsLiveUObject(existing) && !existing->IsDefaultObject()
					&& !existing->bDeleteMe && existing->WorldInfo == remote->WorldInfo
					&& (!local || existing != local->DodgeEmitter))
				{
					g_remoteNativeDodgeEmitter = existing;
				}
				else
				{
					auto* actor = NativeSpawn(remote, AAliceDodgeParticleTrace::StaticClass(), remote,
						FName(), remote->Location, remote->Rotation, nullptr, true);
					g_remoteNativeDodgeEmitter = reinterpret_cast<AAliceDodgeParticleTrace*>(actor);
					if (!actor) return false;
					actor->Instigator = remote;
					actor->bCollideActors = false;
					actor->bBlockActors = false;
					actor->bCollideWorld = false;
				}
			}
			remote->DodgeEmitter = g_remoteNativeDodgeEmitter;
			RemoteParticleReplayScope replayScope;
			return InvokeNativeVisualNotify(notify, node, 0, 0.0f, 0.0f, 0.0f);
		}

		void DispatchRemoteVisualNotify(AAlicePawn* remote,
			USkeletalMeshComponent* sourceComponent,
			UAnimNotify* notify, UAnimNodeSequence* node, float notifyTime)
		{
			if (!remote || !sourceComponent || !notify)
				return;
			RemoteParticleReplayScope replayScope;

			const Clock::time_point now = Clock::now();

			if (g_remoteMaterialsIsolated && notify->IsA(UAliceAnimNotify_SetMatParam::StaticClass()))
			{
				// Authored dodge uses a timed material curve, not Actor.bHidden.
				// Only replay this cosmetic parameter; death/gameplay material changes
				// continue to belong to the authoritative pawn.
				auto* materialNotify = reinterpret_cast<UAliceAnimNotify_SetMatParam*>(notify);
				bool dodgeFade = materialNotify->MatParamInfos.size() > 0;
				for (int32_t i = 0; i < materialNotify->MatParamInfos.size(); ++i)
					dodgeFade = dodgeFade && materialNotify->MatParamInfos.at(i).ParamType == 0
						&& materialNotify->MatParamInfos.at(i).ParamName.ToString() == "FadeOutAlpha";
				if (dodgeFade)
				{
					InvokeNativeVisualNotify(notify, node, 0, 0.0f, 0.0f, 0.0f);
					// SetScalarStartTime stores world game time + Value. Seek the
					// native curve to the event's elapsed time instead of restarting
					// its visible first frames after network/animation sampling delay.
					const float rate = node->Rate * node->SkelComponent->GlobalAnimRateScale
						* (node->AnimSeq ? node->AnimSeq->RateScale : 1.0f);
					const float elapsed = rate > 0.001f
						? (std::max)(0.0f, node->CurrentTime - notifyTime) / rate : 0.0f;
					for (int32_t i = 0; i < materialNotify->MatParamInfos.size(); ++i)
					{
						const auto& info = materialNotify->MatParamInfos.at(i);
						if (!info.bActiveVaryingTime || info.MatID < 0
							|| info.MatID >= remote->Mesh->Materials.size()) continue;
						UMaterialInterface* material = remote->Mesh->Materials.at(info.MatID);
						if (!material || !material->IsA(UMaterialInstanceTimeVarying::StaticClass())) continue;
						UMaterialInstanceTimeVarying_execSetScalarStartTime_Params params{};
						params.ParameterName = info.ParamName;
						params.Value = -elapsed;
						InvokeNativeFunction(material, UFunction::FindFunction("Function Engine.MaterialInstanceTimeVarying.SetScalarStartTime"), params);
					}
					Log("MATERIALSTAGE native dodge fade started on proxy.");
				}
				return;
			}

			if (notify->IsA(
				UAliceAnimNotify_ToggleAttackTrace::StaticClass()))
			{
				auto* toggle = reinterpret_cast<
					UAliceAnimNotify_ToggleAttackTrace*>(notify);
				SetRemoteAttackTrail(toggle->Active);
				return;
			}
			if (notify->IsA(
				UAliceAnimNotify_TriggerAliceDodgeParticle::
					StaticClass()))
			{
				ReplayNativeDodgeNotify(remote, node, notify);
				return;
			}

			if (notify->IsA(
				UAnimNotify_PlayParticleEffect::StaticClass()))
			{
				auto* particle = reinterpret_cast<
					UAnimNotify_PlayParticleEffect*>(notify);
				UParticleSystem* particleTemplate =
					particle->PSTemplate;
				USkeletalMeshComponent* attachMesh =
					sourceComponent;
				const bool weaponSpecific = notify->IsA(
					UAliceAnimNotify_PlayParticleEffect_AliceWeapon::
						StaticClass())
					&& g_remotePresentationWeapon;
				if (weaponSpecific)
				{
					auto* weaponNotify = reinterpret_cast<
						UAliceAnimNotify_PlayParticleEffect_AliceWeapon*>(
							notify);
					UParticleSystem* weaponParticle = nullptr;
					const auto* package = SelectWeaponPresentationPackage(g_remotePresentationWeapon);
					if (package && weaponNotify->WeaponEffectIndex >= 0
						&& weaponNotify->WeaponEffectIndex < package->WLDP_WeaponEffect.size())
						weaponParticle = package->WLDP_WeaponEffect.at(weaponNotify->WeaponEffectIndex);
					if (weaponParticle)
						particleTemplate = weaponParticle;
					if (g_remotePresentationWeapon->Mesh)
						attachMesh =
							g_remotePresentationWeapon->Mesh;
				}
				if (!particleTemplate)
					return;
				if (weaponSpecific && !particle->bAttach
					&& g_remotePresentationWeapon->IsA(AHobbyHorse::StaticClass()))
					return; // Native ground-space emitter is relayed from its owner.
				if (ReplayNativeParticleNotify(remote, node, particle, particleTemplate, attachMesh))
					return;
				UParticleSystemComponent* spawned = nullptr;
				const bool dodgeParticle =
					ContainsCaseInsensitive(
						ObjectName(particleTemplate), "dodge")
					|| ContainsCaseInsensitive(
						ObjectName(particleTemplate), "butter");
				if (dodgeParticle)
				{
					g_remoteDodgeParticleTemplate =
						particleTemplate;
					g_remoteDodgeParticleUntil =
						now + std::chrono::milliseconds(750);
					g_nextRemoteDodgeParticle = now;
					spawned = SpawnRemoteCosmeticParticle(
						remote, particleTemplate, false);
				}
				else if (weaponSpecific
					&& !particle->bAttach)
				{
					// Authored HobbyHorse impact effects are world-space.
					// Actor.Location is the holder/pelvis, so sample the
					// animated weapon tip instead.
					spawned = SpawnWeaponParticleCandidate(
						g_remotePresentationWeapon,
						particleTemplate,
						VfxAttachmentCandidate::
							WeaponTraceWorld);
				}
				else if (weaponSpecific
					&& particle->bAttach
					&& !particle->SocketName.IsValid()
					&& !particle->BoneName.IsValid())
				{
					spawned = SpawnWeaponParticleCandidate(
						g_remotePresentationWeapon,
						particleTemplate,
						VfxAttachmentCandidate::
							WeaponTraceSocket);
				}
				else if (remote->WorldInfo
					&& remote->WorldInfo->MyEmitterPool
					&& attachMesh && particle->bAttach)
				{
					const FName attachPoint =
						particle->SocketName.IsValid()
							? particle->SocketName
							: particle->BoneName;
					spawned = remote->WorldInfo->MyEmitterPool
						->SpawnEmitterMeshAttachment(
							particleTemplate, attachMesh,
							attachPoint, attachPoint.IsValid(),
							FVector(0.0f, 0.0f, 0.0f),
							FRotator(0, 0, 0));
				}
				else
				{
					spawned = SpawnRemoteCosmeticParticle(
						remote, particleTemplate, false);
				}
				if (spawned && std::isfinite(particle->DrawScale) && particle->DrawScale > 0.0f)
					NativeSetScale(spawned, particle->DrawScale);
				MakePresentationParticleVisible(spawned);
				if (spawned)
				{
					TrackRemoteWeaponTransient(
						spawned, particleTemplate,
						weaponSpecific
							? std::chrono::milliseconds(2500)
							: (dodgeParticle
								? std::chrono::milliseconds(1500)
								: std::chrono::milliseconds(3500)));
				}
				g_lastRemoteVfx = "notify: "
					+ ObjectName(particleTemplate)
					+ (spawned ? " [spawned]" : " [failed]");
				if (g_config.actionTrace)
				{
					Log("VFXSTAGE particle notify="
						+ ObjectName(notify)
						+ ", template="
						+ ObjectName(particleTemplate)
						+ ", path=direct, spawned="
						+ (spawned ? "yes." : "no."));
				}
			}
		}

		void UpdateRemoteVisualNotifyChannel(AAlicePawn* remote,
			UAnimNodeSequence* node, VisualNotifyChannel& channel)
		{
			auto finishTrails = [&]()
			{
				for (auto& trail : channel.trails)
					ReplayNativeTrailNotify(channel.cursor.node, trail, 2, trail.previousTime);
				channel.trails.clear();
			};
			if (!remote || !node || !node->AnimSeq || !node->SkelComponent)
			{
				finishTrails(); channel = {}; return;
			}
			VisualNotifyCursor& cursor = channel.cursor;
			UAnimSequence* sequence = node->AnimSeq;
			const float current = (std::max)(0.0f, node->CurrentTime);
			float from = cursor.previousTime;
			const bool newSequence = !cursor.primed || cursor.sequence != sequence;
			const bool newAction = channel.actionSerial != g_lastRemoteActionSerial;
			const auto advance = ClassifyVisualNotifyAdvance(newSequence, newAction,
				node->bLooping, from, current, sequence->SequenceLength);
			auto dispatchInterval = [&](float begin, float end)
			{
				const int32_t count = std::min<int32_t>(sequence->Notifies.size(), 256);
				for (int32_t index = 0; index < count; ++index)
				{
					const FAnimNotifyEvent& event = sequence->Notifies.at(index);
					if (!event.Notify || event.Time <= begin || event.Time > end + 0.002f) continue;
					if (event.Notify->IsA(UAnimNotify_Trails::StaticClass()))
					{
						ActiveVisualTrail trail{ reinterpret_cast<UAnimNotify_Trails*>(event.Notify),
							event.Time, event.Time + (std::max)(event.Duration, 0.001f), event.Time };
						ReplayNativeTrailNotify(node, trail, 0, event.Time);
						channel.trails.push_back(trail);
					}
					else DispatchRemoteVisualNotify(remote, node->SkelComponent, event.Notify, node, event.Time);
				}
			};
			if (advance == VisualNotifyAdvance::LoopWrap)
			{
				dispatchInterval(from, sequence->SequenceLength);
				finishTrails(); from = -0.001f;
			}
			else if (advance == VisualNotifyAdvance::Restart)
			{
				finishTrails(); from = -0.001f;
			}
			else if (advance == VisualNotifyAdvance::Correction)
				return; // A position correction must not replay an already emitted event.
			cursor.node = node; cursor.sequence = sequence; cursor.primed = true;
			channel.actionSerial = g_lastRemoteActionSerial;
			dispatchInterval(from, current);
			for (auto it = channel.trails.begin(); it != channel.trails.end();)
			{
				const float time = (std::min)(current, it->endTime);
				if (time > it->previousTime)
					ReplayNativeTrailNotify(node, *it, 1, time);
				if (current >= it->endTime)
				{
					ReplayNativeTrailNotify(node, *it, 2, it->endTime);
					it = channel.trails.erase(it);
				}
				else ++it;
			}
			cursor.previousTime = (std::max)(from, current);
		}

		void UpdateRemoteVisualNotifies(AAlicePawn* remote)
		{
			UpdateRemoteVisualNotifyChannel(remote, g_remoteAppliedFullBodyNode, g_remoteVisualNotifyChannels[0]);
			UpdateRemoteVisualNotifyChannel(remote, g_remoteAppliedUpperBodyNode, g_remoteVisualNotifyChannels[1]);
			UpdateRemoteVisualNotifyChannel(remote, g_remoteAppliedWeaponNode, g_remoteVisualNotifyChannels[2]);
			UpdateRemoteVisualNotifyChannel(remote, g_remoteAppliedHalfBodyNode, g_remoteVisualNotifyChannels[3]);
		}

		void TickRemoteActionVfx(AAlicePawn* remote,
			bool dodgeVisualHidden)
		{
			const Clock::time_point now = Clock::now();
			if (g_devMuzzleUntil != Clock::time_point{}
				&& now >= g_devMuzzleUntil)
			{
				SetRemoteMuzzleFlash(
					g_remotePresentationWeapon, false);
				g_devMuzzleUntil = {};
			}
			if (g_devGlideUntil != Clock::time_point{}
				&& now >= g_devGlideUntil)
			{
				if (g_remoteGlideParticle)
					RetirePersistentPresentationParticle(
						g_remoteGlideParticle);
				g_remoteGlideParticle = nullptr;
				g_devGlideUntil = {};
			}
			if (g_remoteAttackTrailActive
				&& g_remoteAttackTrailUntil
					!= Clock::time_point{}
				&& now >= g_remoteAttackTrailUntil)
			{
				SetRemoteAttackTrail(false);
			}

		}

		void LogClockBombPresentationCandidates()
		{
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return;
			int logged = 0;
			for (int32_t index = 0;
				index < objects->size() && logged < 96;
				++index)
			{
				UObject* object = objects->at(index);
				if (!object)
					continue;
				const std::string name = ObjectName(object);
				if (!ContainsCaseInsensitive(name, "clockbomb"))
					continue;
				std::ostringstream stream;
				stream << "BOMBSTAGE candidate=" << name
					<< ", class=" << ObjectName(object->Class)
					<< ", default="
					<< (object->IsDefaultObject() ? "yes" : "no");
				if (object->IsA(AActor::StaticClass()))
				{
					auto* actor = reinterpret_cast<AActor*>(object);
					stream << ", actorLocation=("
						<< actor->Location.X << ','
						<< actor->Location.Y << ','
						<< actor->Location.Z << ')'
						<< ", hidden="
						<< (actor->bHidden ? "yes" : "no")
						<< ", delete="
						<< (actor->bDeleteMe ? "yes" : "no");
				}
				stream << '.';
				Log(stream.str());
				++logged;
			}
			Log("BOMBSTAGE candidates logged="
				+ std::to_string(logged) + '.');
		}

		void ApplyRemoteComponentVisibility(AAlicePawn* remote,
			const PlayerStatePayload& state, bool actorHidden)
		{
			if (!remote)
				return;

			auto setHidden = [](UPrimitiveComponent* component, bool value)
			{
				if (!component)
					return;
				component->bIgnoreOwnerHidden = false;
				NativeSetComponentHidden(component, value);
				component->HiddenGame = value;
			};

			setHidden(remote->Mesh, actorHidden
				|| (state.flags & StateBodyHidden) != 0);
			setHidden(remote->UpperBodyComponent, actorHidden
				|| (state.flags & StateUpperBodyHidden) != 0);
			if (g_remoteNativeHairActive && remote->HairComponent)
			{
				UHairComponent* hair = remote->HairComponent;
				const bool hidden = actorHidden || (state.flags & StateHairHidden) != 0;
				const bool wasHidden = hair->HiddenGame;
				hair->bIgnoreOwnerHidden = false;
				if (hidden != wasHidden)
					NativeSetComponentHidden(hair, hidden);
				hair->HiddenGame = hidden;
				if (wasHidden && !hidden)
					NativeResetHair(hair);
			}
			else
				setHidden(remote->HairComponent, true);
			if (g_remoteHairProxy
				&& g_remoteHairProxy->SkeletalMeshComponent)
			{
				setHidden(
					g_remoteHairProxy->SkeletalMeshComponent,
					true);
				// Hiding a UE3 HairComponent through its owner destroys the
				// custom simulator state. The final presentation barrier uses
				// scale instead, while this invisible scalp actor stays alive.
				g_remoteHairProxy->bHidden = false;
				NativeSetActorHidden(
					g_remoteHairProxy, false);
				if (g_remoteIndependentHair)
				{
					g_remoteIndependentHair->bOwnerNoSee = false;
					g_remoteIndependentHair->bOnlyOwnerSee = actorHidden;
					g_remoteIndependentHair->HiddenGame = false;
					NativeSetComponentHidden(
						g_remoteIndependentHair, false);
				}
			}
			auto setCosmeticHidden =
				[remote, actorHidden, &setHidden](
					UClothComponent* component,
					std::uint32_t sourceHiddenFlag)
			{
				const bool masterPose =
					component && component->SkeletalMesh
					&& (component->ParentAnimComponent
							== remote->Mesh
						|| component->AttachedToSkelComponent
							== remote->Mesh);
				setHidden(component, actorHidden
					|| (!masterPose
						&& (sourceHiddenFlag != 0)));
			};
			setCosmeticHidden(remote->SkirtComponent,
				state.flags & StateSkirtHidden);
			setCosmeticHidden(remote->BowComponent,
				state.flags & StateBowHidden);
			setCosmeticHidden(remote->RibbonComponent,
				state.flags & StateRibbonHidden);
			setCosmeticHidden(remote->EarComponent,
				state.flags & StateEarHidden);
			const bool presentationWeapon =
				remote->Weapon
				&& remote->Weapon == g_remotePresentationWeapon;
			const bool weaponHidden = actorHidden
				|| (!presentationWeapon
					&& (state.flags & StateWeaponHidden) != 0);
			if (remote->Weapon && remote->Weapon->Mesh)
				setHidden(remote->Weapon->Mesh, weaponHidden);
			if (remote->Weapon)
			{
				if (presentationWeapon)
				{
					const float weaponScale = weaponHidden
						? 0.001f
						: g_remoteWeaponBaseDrawScale;
					remote->Weapon->SetDrawScale(weaponScale);
					remote->Weapon->DrawScale = weaponScale;
				}
				remote->Weapon->bHidden = weaponHidden;
				NativeSetActorHidden(
					remote->Weapon, weaponHidden);
				if (presentationWeapon)
				{
					const std::string key =
						"weapon-visibility:"
						+ ObjectName(remote->Weapon);
					if (g_loggedConfigAnimationStages
						.insert(key).second)
					{
						Log("WEAPONSTAGE visibility actor="
							+ ObjectName(remote->Weapon)
							+ ", sourceHidden="
							+ std::string(
								(state.flags
									& StateWeaponHidden)
									!= 0
								? "yes" : "no")
							+ ", appliedHidden="
							+ std::string(weaponHidden
								? "yes" : "no")
							+ ", meshHidden="
							+ std::string(
								remote->Weapon->Mesh
									&& remote->Weapon->Mesh
										->HiddenGame
								? "yes." : "no."));
					}
				}
			}
			if (remote->DummyWeapon && remote->DummyWeapon->Mesh)
				setHidden(remote->DummyWeapon->Mesh, actorHidden
					|| (state.flags & StateWeaponHidden) != 0);
		}

		void ApplyRemotePresentation(AAlicePawn* remote,
			const RemotePresentation& presentation, bool forceComponents)
		{
			if (!remote || !presentation.valid)
				return;

			const PlayerStatePayload& state = presentation.state;
			if (g_remoteNativeHairActive && remote->HairComponent
				&& (presentation.location - remote->Location).SizeSquared() >= 1500.0f * 1500.0f)
				NativeResetHair(remote->HairComponent);
			remote->SetLocationNoCheck(presentation.location);
			NativeSetActorRotation(remote, presentation.rotation);
			remote->Location = presentation.location;
			remote->Rotation = presentation.rotation;
			remote->Velocity = FVector(state.velocity[0], state.velocity[1], state.velocity[2]);
			remote->AimOffsetPct.X = UnpackAimOffset(state.aimOffset, 0);
			remote->AimOffsetPct.Y = UnpackAimOffset(state.aimOffset, 1);
			ApplyRemotePawnStance(remote, state);
			remote->BasicMovementState = static_cast<EMovementState>(state.movementState);
			remote->bIsWalking = (state.flags & StateWalking) != 0;
			remote->bIsCrouched = (state.flags & StateCrouched) != 0;
			remote->Physics = EPhysics::PHYS_None;
			if (g_remotePresentationWeapon
				&& remote->Weapon
					== g_remotePresentationWeapon)
			{
				// The mesh is socket-bound independently. Keep the cosmetic
				// actor origin with its pawn as well so weapon effects that
				// query Actor.Location do not remain at the spawn point.
				g_remotePresentationWeapon->Location =
					presentation.location;
				g_remotePresentationWeapon->Rotation =
					presentation.rotation;
			}
			const bool shrunk = (state.flags & StateShrunk) != 0;
			remote->bShrinkingModeActive = shrunk;
			if (std::isfinite(state.drawScale) && state.drawScale > 0.01f)
			{
				remote->SetDrawScale(state.drawScale);
				remote->DrawScale = state.drawScale;
			}
			const FVector drawScale3D(
				state.drawScale3D[0], state.drawScale3D[1], state.drawScale3D[2]);
			if (std::isfinite(drawScale3D.X) && std::isfinite(drawScale3D.Y)
				&& std::isfinite(drawScale3D.Z))
			{
				remote->SetDrawScale3D(drawScale3D);
				remote->DrawScale3D = drawScale3D;
			}
			if (remote->Mesh)
			{
				const FVector meshTranslation(state.meshTranslation[0],
					state.meshTranslation[1], state.meshTranslation[2]);
				const FVector meshScale3D(state.meshScale3D[0],
					state.meshScale3D[1], state.meshScale3D[2]);
				if (std::isfinite(meshTranslation.X) && std::isfinite(meshTranslation.Y)
					&& std::isfinite(meshTranslation.Z))
				{
					NativeSetTranslation(remote->Mesh, meshTranslation);
					remote->Mesh->Translation = meshTranslation;
				}
				if (std::isfinite(state.meshScale) && state.meshScale > 0.01f)
				{
					NativeSetScale(remote->Mesh, state.meshScale);
					remote->Mesh->Scale = state.meshScale;
				}
				if (std::isfinite(meshScale3D.X) && std::isfinite(meshScale3D.Y)
					&& std::isfinite(meshScale3D.Z))
				{
					NativeSetScale3D(remote->Mesh, meshScale3D);
					remote->Mesh->Scale3D = meshScale3D;
				}
			}
			if (std::isfinite(state.collisionRadius) && state.collisionRadius > 0.01f
				&& std::isfinite(state.collisionHeight) && state.collisionHeight > 0.01f)
			{
				remote->SetCollisionSize(state.collisionRadius, state.collisionHeight);
			}

			remote->bHidden = presentation.hidden;
			remote->bOnlyOwnerSee = false;
			if (remote->Mesh)
			{
				remote->Mesh->bOwnerNoSee = false;
				remote->Mesh->bOnlyOwnerSee = false;
			}
			NativeSetActorHidden(remote, presentation.hidden);
			ApplyRemoteComponentVisibility(remote, state,
				presentation.hidden);
			if (forceComponents)
			{
				NativeForceUpdateComponents(remote, false, true);
				ForceRemoteCosmeticMasterPose(remote);
			}
		}

		void SetRemoteRenderDetached(
			AAlicePawn* remote, bool detached)
		{
			if (!remote
				|| detached == g_remoteRenderDetached)
			{
				return;
			}

			const std::array<UClothComponent*, 4> clothComponents{
				remote->SkirtComponent,
				remote->BowComponent,
				remote->RibbonComponent,
				remote->EarComponent
			};
			if (detached)
			{
				// UE3's custom Alice render paths can ignore HiddenGame after
				// a dodge translucency update. Removing the actual registered
				// skeletal/cloth components from the scene is the equivalent
				// of disabling a Unity SkinnedMeshRenderer.
				for (UClothComponent* cloth : clothComponents)
				{
					if (cloth && cloth->bAttached)
					{
						if (g_config.nativeClothSimulation)
							NativeSetComponentHidden(cloth, true);
						else
							NativeDetachActorComponent(remote, cloth);
					}
				}
				if (remote->UpperBodyComponent
					&& remote->UpperBodyComponent->bAttached)
				{
					NativeDetachActorComponent(
						remote, remote->UpperBodyComponent);
				}
				if (remote->Mesh && remote->Mesh->bAttached)
					NativeDetachActorComponent(
						remote, remote->Mesh);
			}
			else
			{
				if (remote->Mesh && !remote->Mesh->bAttached)
					NativeAttachComponent(remote, remote->Mesh);
				if (remote->UpperBodyComponent
					&& !remote->UpperBodyComponent->bAttached)
				{
					NativeAttachComponent(
						remote, remote->UpperBodyComponent);
				}
				for (UClothComponent* cloth : clothComponents)
				{
					if (!cloth || !cloth->SkeletalMesh)
						continue;
					if (g_config.nativeClothSimulation)
					{
						cloth->AttachedToSkelComponent = remote->Mesh;
						cloth->bTransformFromAnimParent = 1;
						if (!cloth->bAttached)
							NativeAttachComponent(remote, cloth);
						NativeSetComponentHidden(cloth, false);
						continue;
					}
					if (!cloth->bAttached)
						NativeAttachComponent(remote, cloth);
					if (remote->Mesh
						&& cloth->AttachedToSkelComponent
							!= remote->Mesh)
					{
						const char* label =
							cloth == remote->SkirtComponent
								? "skirt"
								: (cloth == remote->BowComponent
									? "bow"
									: (cloth
											== remote->RibbonComponent
										? "ribbon" : "ear"));
						AttachRigidPresentationCosmetic(
							remote->Mesh, cloth, false, label);
					}
				}
				NativeForceUpdateComponents(remote, false, false);
			}

			g_remoteRenderDetached = detached;
			Log(std::string("VISIBILITY proxy render components=")
				+ (detached ? "detached" : "reattached")
				+ ", bodyAttached="
				+ (remote->Mesh && remote->Mesh->bAttached
					? "yes" : "no")
				+ ", skirtAttached="
				+ (remote->SkirtComponent
					&& remote->SkirtComponent->bAttached
					? "yes." : "no."));
		}

		void ResetRemoteAirAnimTree(AAlicePawn* remote, bool logTransition)
		{
			if (!remote || !remote->Mesh)
				return;
			std::vector<UAnimNode*> pending;
			std::unordered_set<UAnimNode*> visited;
			if (remote->Mesh->Animations)
				pending.push_back(remote->Mesh->Animations);
			const int32_t nodeCount = std::min<int32_t>(
				remote->Mesh->AnimTickArray.size(), 2048);
			for (int32_t index = 0; index < nodeCount; ++index)
				if (remote->Mesh->AnimTickArray.at(index))
					pending.push_back(remote->Mesh->AnimTickArray.at(index));

			int floatNodes = 0;
			int fallNodes = 0;
			int legNodes = 0;
			int weaponNodes = 0;
			std::uint16_t traversed = 0;
			while (!pending.empty() && traversed < 2048)
			{
				UAnimNode* node = pending.back();
				pending.pop_back();
				if (!node || !visited.insert(node).second)
					continue;
				++traversed;
				if (node->IsA(
					UAliceGameAnimNode_BlendByFloat::StaticClass()))
				{
					auto* blend = reinterpret_cast<
						UAliceGameAnimNode_BlendByFloat*>(node);
					blend->curFloatState = 0;
					if (blend->ActiveChildIndex != 0) NativeSetActiveChild(blend, 0, 0.08f);
					++floatNodes;
				}
				else if (node->IsA(
					UAliceGameAnimNode_BlendByFall::StaticClass()))
				{
					auto* blend = reinterpret_cast<
						UAliceGameAnimNode_BlendByFall*>(node);
					blend->JumpStep = EBlendJumpSteps::BJS_None;
					blend->LastJumpStep = static_cast<int32_t>(
						EBlendJumpSteps::BJS_None);
					blend->PendingJumpStep = static_cast<int32_t>(
						EBlendJumpSteps::BJS_None);
					blend->PendingTimeToGo = 0.0f;
					if (blend->Children.size() > 4 && blend->ActiveChildIndex != 4)
						NativeSetActiveChild(blend, 4, 0.08f);
					++fallNodes;
				}
				else if (logTransition && node->IsA(
					UAliceGameAnimNode_BlendByLegState::StaticClass()))
				{
					// This transition helper can retain ELS_RightLeg after a
					// float landing on a kinematic proxy, producing the visible
					// one-leg skating pose over otherwise-correct Walk/Run input.
					auto* blend = reinterpret_cast<
						UAliceGameAnimNode_BlendByLegState*>(node);
					blend->LegState = ELegState::ELS_None;
					blend->oldLegState = ELegState::ELS_None;
					if (blend->Children.size() > 0)
						NativeSetActiveChild(blend, 0, 0.05f);
					++legNodes;
				}
				else if (logTransition && node->IsA(
					UAliceGameAnimNode_BlendByAliceWeaponType::StaticClass()))
				{
					// Re-entering a melee locomotion branch after Float can preserve
					// its last partial lower-body pose. Rebuild that branch from the
					// network-authoritative weapon type without resetting its base pose.
					auto* blend = reinterpret_cast<
						UAliceGameAnimNode_BlendByAliceWeaponType*>(node);
					const int32_t target = static_cast<int32_t>(
						g_remotePresentationWeaponType);
					if (target >= 0 && target < blend->Children.size())
					{
						if (blend->ActiveChildIndex != target)
							NativeSetActiveChild(blend, target, 0.08f);
						++weaponNodes;
					}
				}

				// AnimTickArray only contains the currently active branch. Traverse
				// the complete graph as well so an inactive float/leg transition
				// cannot retain its state and become active again after landing.
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
				{
					if (children->at(childIndex).Anim)
						pending.push_back(children->at(childIndex).Anim);
				}
			}
			remote->CurrentJumpStatus = EJumpStatus::EMT_None;
			remote->PendingPhysics = EPhysics::PHYS_None;
			remote->DoPendingPhysics = false;
			if (logTransition
				&& (g_config.animationLifecycleTrace
					|| g_config.animationComparisonTrace))
			{
				Log("ANIMLIFE grounded proxy air-tree reset traversed="
					+ std::to_string(traversed)
					+ " float=" + std::to_string(floatNodes)
					+ " fall=" + std::to_string(fallNodes)
					+ " leg=" + std::to_string(legNodes)
					+ " weapon=" + std::to_string(weaponNodes) + '.');
			}
		}

		void UpdateRemotePawn(AAlicePawn* remote, AWorldInfo* world,
			const PlayerStatePayload& state)
		{
			const FVector target(state.location[0], state.location[1], state.location[2]);
			const FVector delta = target - remote->Location;
			FVector next = target;
			if (!g_config.localMirror
				&& delta.SizeSquared() < 1500.0f * 1500.0f)
			{
				const float alpha = std::clamp(world->DeltaSeconds * g_config.interpolationSpeed, 0.0f, 1.0f);
				next = remote->Location + delta * alpha;
			}

			const FRotator rotation(state.rotation[0], state.rotation[1], state.rotation[2]);
			// Protocol v5 treats the duplicate strictly as a presentation actor.
			// Running SpecialMove script on it can mutate the real controller and
			// caused the old "both Alices dash" failure. AnimTree state arrives
			// separately and is applied without gameplay notifies.
			remote->SpecialMove = ESpecialMove::SM_None;
			remote->PreviousSpecialMove =
				static_cast<ESpecialMove>(state.specialMove);
			remote->CurrentJumpStatus =
				static_cast<EJumpStatus>(state.jumpStatus);
			remote->CurrentDodgeStatus =
				static_cast<EJumpStatus>(state.dodgeStatus);
			// Animation selection below must inspect this packet, not the previous
			// presentation packet. This matters on the exact landing frame.
			g_remotePresentation.valid = true;
			g_remotePresentation.state = state;
			if (!g_config.localMirror)
			{
				std::uint8_t presentationLevel = 1;
				if ((state.flags & StateProgressionValid) != 0
					&& state.weaponType >= 1
					&& state.weaponType <= 4)
				{
					presentationLevel = (std::max)(
						static_cast<std::uint8_t>(1),
						state.weaponLevels[state.weaponType - 1]);
				}
				EnsureRemotePresentationWeapon(
					remote, state.weaponType,
					presentationLevel, 2);
			}
			bool fullBodyPoseApplied = false;
			if (!g_config.localMirror && g_activeRemoteAnimationGraph)
			{
				const bool newGraph =
					g_activeRemoteAnimationGraph->graph.frameNumber
						!= g_lastAppliedAnimationGraphFrame;
				fullBodyPoseApplied = ApplyRemoteAnimationGraph(remote,
					g_activeRemoteAnimationGraph->graph, newGraph);
			}
			const EJumpStatus wireJump =
				static_cast<EJumpStatus>(state.jumpStatus);
			const EPhysics wirePhysics =
				static_cast<EPhysics>(state.physics);
			const bool groundedPhysics =
				wirePhysics == EPhysics::PHYS_Walking
				|| wirePhysics == EPhysics::PHYS_NavMeshWalking
				|| wirePhysics == EPhysics::PHYS_Spider
				|| wirePhysics == EPhysics::PHYS_Ladder
				|| wirePhysics == EPhysics::PHYS_Slide
				|| wirePhysics == EPhysics::PHYS_TrackSlide;
			const bool wireAirborne =
				wirePhysics == EPhysics::PHYS_Falling
				|| wirePhysics == EPhysics::PHYS_Flying
				|| wirePhysics == EPhysics::PHYS_Float
				|| wirePhysics == EPhysics::PHYS_JumpPad
				|| wirePhysics == EPhysics::PHYS_SteamVent
				|| (!groundedPhysics
					&& (wireJump == EJumpStatus::EMT_Jump
						|| wireJump == EJumpStatus::EMT_Rise
						|| wireJump == EJumpStatus::EMT_Fall));
			if (wireAirborne)
				g_remoteAirResetPending = true;
			const bool authoredAirTransition =
				g_remoteFullBodyChannel.active
				&& (ContainsCaseInsensitive(
						g_remoteFullBodyChannel.sequenceName, "jump")
					|| ContainsCaseInsensitive(
						g_remoteFullBodyChannel.sequenceName, "float")
					|| ContainsCaseInsensitive(
						g_remoteFullBodyChannel.sequenceName, "glide")
					|| ContainsCaseInsensitive(
						g_remoteFullBodyChannel.sequenceName, "fly")
					|| ContainsCaseInsensitive(
						g_remoteFullBodyChannel.sequenceName, "land"));
			const bool resetGroundedAirTree = !wireAirborne
				&& (groundedPhysics
					|| (g_remoteAirResetPending
						&& !authoredAirTransition));
			if (resetGroundedAirTree)
			{
				// UE3 ticks the kinematic proxy before this update. Its BlendByFloat
				// can therefore re-enter the last glide child one frame after the
				// landing transition. Pin the base air nodes to their grounded child
				// on every explicitly grounded packet; only log the actual edge.
				ResetRemoteAirAnimTree(remote, g_remoteAirResetPending);
				g_remoteAirResetPending = false;
			}
			const bool dodgeVisualHidden = !g_config.localMirror
				&& ((state.flags & StateDodgeInvisible) != 0 || Clock::now() < g_devDodgeHideUntil);
			g_remoteDodgeVisualHidden = dodgeVisualHidden;
			const bool rawGlideVfxActive =
				!g_config.localMirror
				&& g_remoteFullBodyChannel.active
				&& (ContainsCaseInsensitive(
						g_remoteFullBodyChannel.sequenceName,
						"float")
					|| ContainsCaseInsensitive(
						g_remoteFullBodyChannel.sequenceName,
						"glide"));
			bool glideVfxActive = rawGlideVfxActive;
			const Clock::time_point presentationNow = Clock::now();
			if (rawGlideVfxActive)
			{
				g_remoteGlideInactiveSince = {};
			}
			else if (g_remoteGlideVfxActive)
			{
				if (g_remoteGlideInactiveSince == Clock::time_point{})
					g_remoteGlideInactiveSince = presentationNow;
				const auto glideInactiveFor =
					presentationNow - g_remoteGlideInactiveSince;
				if (g_config.preserveMovementTrails
					&& g_remoteGlideParticle
					&& glideInactiveFor >= std::chrono::milliseconds(80))
				{
					// Capture the authored GFX end vortex while it is still
					// visible. Waiting for the full animation debounce captures
					// the already-faded final frame instead.
					PreserveRemoteMovementParticle(
						g_remoteGlideParticle,
						g_remoteGlideParticle->Template);
					Log("VFXCAPTURE preserved authored glide end vortex early.");
					g_remoteGlideParticle = nullptr;
				}
				if (glideInactiveFor
					< std::chrono::milliseconds(350))
				{
					glideVfxActive = true;
				}
			}
			if (glideVfxActive != g_remoteGlideVfxActive)
			{
				g_remoteGlideVfxActive = glideVfxActive;
				if (glideVfxActive)
				{
					// The proxy animation notify has already spawned the new
					// Nv PhysX leaf PSC by this point. Retire only stale uses
					// and let the newest one simulate normally for this glide.
					RetireRemoteNativeGlideTrails(remote, true);
					g_nextRemoteLeafTrailMarker = presentationNow;
					UParticleSystem* fallback =
						FindLoadedParticleSystem(
							{ "Glide" }, { "Alice" });
					if (!fallback)
						fallback = FindLoadedParticleSystem(
							{ "Float" }, { "Alice" });
					if (fallback)
					{
						if (g_remoteGlideParticle)
						{
							if (g_config.preserveMovementTrails)
								PreserveRemoteMovementParticle(
									g_remoteGlideParticle,
									g_remoteGlideParticle->Template);
							else
								RetirePersistentPresentationParticle(
									g_remoteGlideParticle);
						}
						g_remoteGlideParticle =
							SpawnRemoteCosmeticParticle(
								remote, fallback, true);
						MakePresentationParticleVisible(
							g_remoteGlideParticle);
					}
					g_lastRemoteVfx =
						"glide: "
						+ ObjectName(fallback)
						+ (g_remoteGlideParticle
							? " [isolated proxy component]"
							: " [missing]");
					Log("VFXSTAGE glide emitter="
						+ ObjectName(remote->GlideEmitter)
						+ ", native=disabled"
						+ ", template="
						+ ObjectName(fallback)
						+ ", direct="
						+ (g_remoteGlideParticle
							? "yes." : "no."));
				}
				else
				{
					g_remoteGlideInactiveSince = {};
					g_nextRemoteLeafTrailMarker = {};
					// Alice's real pawn normally stops GlideEmitter here. The
					// presentation proxy has no GlideEmitter reference, so do
					// the equivalent pool return explicitly. This is independent
					// from the optional frozen-trail history.
					RetireRemoteNativeGlideTrails(remote, false);
					if (g_remoteGlideParticle)
					{
						if (g_config.preserveMovementTrails)
							PreserveRemoteMovementParticle(
								g_remoteGlideParticle,
								g_remoteGlideParticle->Template);
						else
							RetirePersistentPresentationParticle(
								g_remoteGlideParticle);
					}
					g_remoteGlideParticle = nullptr;
					g_lastRemoteVfx =
						"glide native presentation: off";
				}
				Log(std::string("VFXSTAGE remote glide=")
					+ (glideVfxActive ? "on." : "off."));
			}
			if (glideVfxActive && g_config.preserveMovementTrails
				&& presentationNow >= g_nextRemoteLeafTrailMarker)
			{
				SpawnRemoteStaticLeafTrailSample(remote);
				g_nextRemoteLeafTrailMarker = presentationNow
					+ std::chrono::milliseconds(100);
			}
			const bool clockBombAnimationActive =
				g_remoteFullBodyChannel.active
				&& ContainsCaseInsensitive(
					g_remoteFullBodyChannel.sequenceName,
					"clockbomb");
			if (clockBombAnimationActive
				!= g_remoteClockBombAnimationActive)
			{
				g_remoteClockBombAnimationActive =
					clockBombAnimationActive;
				if (clockBombAnimationActive)
				{
					g_lastRemoteVfx =
						"clock bomb actor discovery";
					LogClockBombPresentationCandidates();
				}
			}

			const bool shrunk = (state.flags & StateShrunk) != 0;
			const bool shrinkChanged = shrunk != g_remoteShrinkApplied;
			if (shrinkChanged)
			{
				g_remoteShrinkApplied = shrunk;
				Log(std::string("Visual proxy shrink=") + (shrunk ? "on" : "off")
					+ ", actorScale=" + std::to_string(state.drawScale)
					+ ", meshScale=" + std::to_string(state.meshScale)
					+ ", meshZ=" + std::to_string(state.meshTranslation[2])
					+ ", collisionHeight=" + std::to_string(state.collisionHeight) + '.');
			}

			const AAlicePlayerController* controller = g_State.AlicePlayerController;
			const bool localCinematic = controller
				&& (controller->bCinematicMode
					|| (controller->bCinemaDisableInputMove
						&& controller->bCinemaDisableInputLook));
			// The proxy is local presentation only. Hide it as soon as this
			// window enters cinematic mode, including the synchronized
			// waiting barrier, so Matinee never sees two Alice actors.
			const bool hideForLocalCinematic =
				localCinematic || IsEmergencyForcedCutsceneActive();
			const bool peerDead = state.health <= 0;
			const bool suppressEffects = ((state.flags & StateVisible) == 0
					&& !dodgeVisualHidden)
				|| peerDead || hideForLocalCinematic
				|| g_devForceProxyHidden;
			const bool hidden = suppressEffects || dodgeVisualHidden;
			if (hidden != g_remoteHidden)
			{
				Log(std::string("Visual proxy ") + (hidden ? "hidden" : "shown")
					+ (hideForLocalCinematic
						? " for local cinematic."
						: (peerDead
							? " because peer is dead."
							: (dodgeVisualHidden
								? " for remote dodge."
								: "."))));
				g_remoteHidden = hidden;
			}
			// HiddenGame is not reliable for Alice's custom cloth render path.
			// Detaching only the proxy render tree is the renderer-equivalent
			// of disabling its SkinnedMeshRenderers and does not affect
			// networking, animation capture or the peer game.
			// Keep the skeleton registered during dodge so native cloth
			// retains its simulator and butterflies can outlive body hiding.
			SetRemoteRenderDetached(remote, suppressEffects);
			g_remotePresentation.location = next;
			g_remotePresentation.rotation = rotation;
			g_remotePresentation.hidden = hidden;
			ApplyRemotePresentation(remote, g_remotePresentation, true);
			ApplyRemoteVisualAction(remote, state);
			UpdateRemoteVisualNotifies(remote);
			TickRemoteActionVfx(remote, dodgeVisualHidden);
			TickRemoteNativeGlideTrailGuard(remote);
			if (suppressEffects)
			{
				if (IsLiveUObject(g_remoteNativeDodgeEmitter)
					&& g_remoteNativeDodgeEmitter->WorldInfo == world)
				{
					g_remoteNativeDodgeEmitter->eventFinishFlightEffects();
					g_remoteNativeDodgeEmitter->FlightTime = 0.0f;
				}

				// Animation notifies keep ticking while the proxy render tree
				// is detached for a cinematic. Retire anything they emitted
				// in that frame so invisible proxy effects cannot accumulate
				// behind the cutscene and survive into gameplay.
				CleanupRemoteWeaponTransients(true);
				StopRemoteWeaponLoopParticles();
				RetireTrackedRemoteNativeGlideTrails(
					remote->WorldInfo, nullptr, "proxy-hidden");
				g_remoteNativeGlideCurrent = nullptr;
				g_remoteNativeGlideCurrentSince = {};
				if (g_remoteGlideParticle)
				{
					RetirePersistentPresentationParticle(
						g_remoteGlideParticle);
					g_remoteGlideParticle = nullptr;
				}
				if (hideForLocalCinematic || peerDead)
					DestroyRemoteStaticLeafTrail();
				if (g_remoteMuzzleParticle)
				{
					RetirePersistentPresentationParticle(
						g_remoteMuzzleParticle);
					g_remoteMuzzleParticle = nullptr;
				}
				if (g_remoteAttackTrailParticle)
				{
					HardStopPresentationParticle(
						g_remoteAttackTrailParticle, true);
					g_remoteAttackTrailParticle = nullptr;
				}
				g_remoteDodgeParticleUntil = {};
				g_nextRemoteDodgeParticle = {};
			}
			const auto spatialNow = Clock::now();
			if (g_cosmeticSpatialSamplesRemaining > 0
				&& spatialNow >= g_nextCosmeticSpatialSample)
			{
				LogRemoteCosmeticSpatialSample(remote);
				--g_cosmeticSpatialSamplesRemaining;
				g_nextCosmeticSpatialSample =
					spatialNow + std::chrono::seconds(1);
			}
			if (!g_loggedPresentedCosmetics)
			{
				g_loggedPresentedCosmetics = true;
				LogCosmeticState("local-first-presented", GetLocalPawn());
				LogCosmeticState("remote-first-presented", remote);
			}
			if (!g_remoteHidden
				&& !g_loggedDelayedCosmetics
				&& g_remoteCosmeticDiagnosticAt
					!= Clock::time_point{}
				&& Clock::now() >= g_remoteCosmeticDiagnosticAt)
			{
				// OverrideMesh is required when Alice's custom hair renderer
				// cannot infer the proxy skeleton. Some archetypes instead
				// follow the original owner-based path. If the explicit path
				// has never submitted a render after two seconds, retry once
				// with the exact binding used by the playable Alice.
				if (!g_remoteNativeHairActive && !g_remoteHairProxy
					&& !g_remoteHairOwnerFallbackApplied
					&& remote->HairComponent
					&& remote->HairComponent->LastRenderTime < 0.0f)
				{
					LogCosmeticState(
						"remote-before-hair-owner-fallback", remote);
					NativeReattachComponent(
						remote, remote->HairComponent);
					remote->HairComponent->OverrideMesh = nullptr;
					remote->HairComponent->bOwnerNoSee = false;
					remote->HairComponent->bOnlyOwnerSee = false;
					remote->HairComponent->bJustAttached = true;
					remote->HairComponent->bPendingReset = true;
					NativeResetHair(remote->HairComponent);
					remote->AliceHairAir();
					NativeForceComponentUpdate(
						remote->HairComponent, false);
					NativeForceUpdateComponents(
						remote, false, false);
					g_remoteHairOwnerFallbackApplied = true;
					g_remoteCosmeticDiagnosticAt =
						Clock::now() + std::chrono::seconds(2);
					Log("COSMETICSTAGE hair had never rendered; "
						"retrying the playable-Alice owner binding.");
				}
				else
				{
					g_loggedDelayedCosmetics = true;
					LogCosmeticState(
						g_remoteHairOwnerFallbackApplied
							? "remote-after-hair-owner-fallback"
							: "remote-delayed",
						remote);
					if (remote->HairComponent)
					{
						std::ostringstream stream;
						stream
							<< "COSMETICSTAGE strand hair delayed:"
							<< " hidden="
							<< remote->HairComponent->HiddenGame
							<< ", lastRender="
							<< remote->HairComponent->LastRenderTime
							<< ", simulator="
							<< (remote->HairComponent->Simulator.Dummy
								? "yes" : "no")
							<< ", override="
							<< ObjectName(
								remote->HairComponent->OverrideMesh)
							<< '.';
						Log(stream.str());
					}
					if (g_remoteIndependentHair)
					{
						std::ostringstream stream;
						stream
							<< "COSMETICSTAGE independent hair delayed:"
							<< " hidden="
							<< g_remoteIndependentHair->HiddenGame
							<< ", lastRender="
							<< g_remoteIndependentHair->LastRenderTime
							<< ", simulator="
							<< (g_remoteIndependentHair
								->Simulator.Dummy
									? "yes" : "no")
							<< ", pendingReset="
							<< g_remoteIndependentHair->bPendingReset
							<< ", justAttached="
							<< g_remoteIndependentHair->bJustAttached
							<< ", template="
							<< ObjectName(
								g_remoteIndependentHair->Template)
							<< ", material="
							<< ObjectName(
								g_remoteIndependentHair->Material)
							<< ", override="
							<< ObjectName(
								g_remoteIndependentHair->OverrideMesh)
							<< ", world=("
							<< g_remoteIndependentHair
								->LocalToWorld.WPlane.X
							<< ','
							<< g_remoteIndependentHair
								->LocalToWorld.WPlane.Y
							<< ','
							<< g_remoteIndependentHair
								->LocalToWorld.WPlane.Z
							<< ')'
							<< ", scalpWorld=("
							<< (g_remoteHairProxy
								&& g_remoteHairProxy
									->SkeletalMeshComponent
								? g_remoteHairProxy
									->SkeletalMeshComponent
										->LocalToWorld.WPlane.X
								: 0.0f)
							<< ','
							<< (g_remoteHairProxy
								&& g_remoteHairProxy
									->SkeletalMeshComponent
								? g_remoteHairProxy
									->SkeletalMeshComponent
										->LocalToWorld.WPlane.Y
								: 0.0f)
							<< ','
							<< (g_remoteHairProxy
								&& g_remoteHairProxy
									->SkeletalMeshComponent
								? g_remoteHairProxy
									->SkeletalMeshComponent
										->LocalToWorld.WPlane.Z
								: 0.0f)
							<< ')'
							<< '.';
						Log(stream.str());
					}
					if (g_remoteHairProxy
						&& g_remoteHairProxy
							->SkeletalMeshComponent)
					{
						USkeletalMeshComponent* hairProxy =
							g_remoteHairProxy
								->SkeletalMeshComponent;
						std::ostringstream stream;
						stream
							<< "COSMETICSTAGE skeletal hair delayed:"
							<< " hidden=" << hairProxy->HiddenGame
							<< ", lastRender="
							<< hairProxy->LastRenderTime
							<< ", parentBones="
							<< hairProxy->ParentBoneMap.size()
							<< ", world=("
							<< hairProxy->LocalToWorld.WPlane.X
							<< ','
							<< hairProxy->LocalToWorld.WPlane.Y
							<< ','
							<< hairProxy->LocalToWorld.WPlane.Z
							<< ").";
						Log(stream.str());
					}
				}
			}
			if (shrinkChanged)
			{
				Log("Visual proxy applied scale: drawScale="
					+ std::to_string(remote->DrawScale)
					+ ", drawScale3D=("
					+ std::to_string(remote->DrawScale3D.X) + ','
					+ std::to_string(remote->DrawScale3D.Y) + ','
					+ std::to_string(remote->DrawScale3D.Z) + ')'
					+ ", meshScale="
					+ std::to_string(remote->Mesh
						? remote->Mesh->Scale : 0.0f) + '.');
			}
			if (!g_config.localMirror
				&& g_activeRemoteAnimationGraph)
			{
				EnableRemoteAnimationTick(remote);
				if (fullBodyPoseApplied && remote->Mesh)
				{
					// EngineTick has already evaluated this component using the
					// proxy pawn's ordinary locomotion tree. Re-evaluate only
					// after the safe full-body slot has been selected so the
					// action pose reaches this render frame. Unlike the old raw
					// graph driver, the tree stays unpaused and advances itself.
					const std::uint64_t poseBefore =
						PoseHash(remote->Mesh);
					NativeForceSkelUpdate(remote->Mesh);
					ForceRemoteCosmeticMasterPose(remote);
					const std::uint64_t poseAfter =
						PoseHash(remote->Mesh);
					int& poseSamples =
						g_animationStagePoseSamples[
							g_remoteAppliedFullBodyName];
					if (poseSamples < 3)
					{
						++poseSamples;
						std::ostringstream stream;
						stream << "ANIMSTAGE force sample=" << poseSamples
							<< ", requested="
							<< (g_remoteAppliedFullBodyName.empty()
								? "<none>"
								: g_remoteAppliedFullBodyName)
							<< ", slotActive="
							<< (g_remoteAppliedFullBodySlot
								? g_remoteAppliedFullBodySlot->ActiveChildIndex
								: -1)
							<< ", nodeName="
							<< (g_remoteAppliedFullBodyNode
								&& g_remoteAppliedFullBodyNode
									->AnimSeqName.IsValid()
								? g_remoteAppliedFullBodyNode
									->AnimSeqName.ToString()
								: "<invalid>")
							<< ", nodeTime="
							<< (g_remoteAppliedFullBodyNode
								? g_remoteAppliedFullBodyNode->CurrentTime
								: -1.0f)
							<< ", atoms=" << remote->Mesh->LocalAtoms.size()
							<< ", spaceBases="
							<< remote->Mesh->SpaceBases.size()
							<< ", poseHash=0x" << std::hex << poseBefore
							<< "->0x" << poseAfter << std::dec
							<< ", changed="
							<< (poseBefore != poseAfter ? "yes." : "no.");
						Log(stream.str());
					}
					if (!g_loggedPostEngineSkeletonRefresh)
					{
						g_loggedPostEngineSkeletonRefresh = true;
						Log("Post-EngineTick skeleton refresh v8 is active.");
					}
				}
			}
		}

