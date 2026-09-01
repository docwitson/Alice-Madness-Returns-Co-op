		bool IsLivePresentationParticle(
			UParticleSystemComponent* particle)
		{
			return IsLiveUObject(particle)
				&& particle->IsA(
					UParticleSystemComponent::StaticClass());
		}

		void TracePresentationParticleState(const char* stage,
			UParticleSystemComponent* particle)
		{
			if (!g_config.vfxLifecycleTrace)
				return;
			if (!IsLivePresentationParticle(particle))
			{
				Log("VFXLIFE stage=" + std::string(stage ? stage : "<null>")
					+ ", component=<invalid>.");
				return;
			}

			bool inActivePool = false;
			bool inCachedPool = false;
			int activePoolCount = -1;
			int cachedPoolCount = -1;
			AEmitterPool* pool = g_currentWorld
				&& IsLiveUObject(g_currentWorld)
				? g_currentWorld->MyEmitterPool : nullptr;
			if (pool && IsLiveUObject(pool))
			{
				activePoolCount = pool->ActiveComponents.size();
				cachedPoolCount = pool->PoolComponents.size();
				for (int32_t index = 0;
					index < std::min<int32_t>(activePoolCount, 2048); ++index)
				{
					if (pool->ActiveComponents.at(index) == particle)
						inActivePool = true;
				}
				for (int32_t index = 0;
					index < std::min<int32_t>(cachedPoolCount, 2048); ++index)
				{
					if (pool->PoolComponents.at(index) == particle)
						inCachedPool = true;
				}
			}

			int visibleMeshes = 0;
			for (int32_t index = 0;
				index < std::min<int32_t>(particle->SMComponents.size(), 64);
				++index)
			{
				UStaticMeshComponent* mesh = particle->SMComponents.at(index);
				if (IsLiveUObject(mesh) && !mesh->HiddenGame)
					++visibleMeshes;
			}

			std::ostringstream stream;
			stream << "VFXLIFE stage=" << (stage ? stage : "<null>")
				<< ", component=" << ObjectName(particle)
				<< ", template=" << ObjectName(particle->Template)
				<< ", active=" << (particle->bIsActive ? 1 : 0)
				<< ", completed=" << (particle->bWasCompleted ? 1 : 0)
				<< ", deactivated=" << (particle->bWasDeactivated ? 1 : 0)
				<< ", cachedFlag=" << (particle->bIsCachedInPool ? 1 : 0)
				<< ", hidden=" << (particle->HiddenGame ? 1 : 0)
				<< ", suppress=" << (particle->bSuppressSpawning ? 1 : 0)
				<< ", skipDynamic="
				<< (particle->bSkipUpdateDynamicDataDuringTick ? 1 : 0)
				<< ", emitters=" << particle->EmitterInstances.size()
				<< ", sm=" << particle->SMComponents.size()
				<< ", visibleSm=" << visibleMeshes
				<< ", poolActive=" << (inActivePool ? 1 : 0)
				<< ", poolCached=" << (inCachedPool ? 1 : 0)
				<< ", poolSizes=" << activePoolCount << '/' << cachedPoolCount
				<< '.';
			Log(stream.str());
		}

		void AuditPresentationParticle(UParticleSystemComponent* particle,
			bool retired)
		{
			if (!g_config.vfxLifecycleTrace
				|| !IsLivePresentationParticle(particle))
				return;
			const Clock::time_point now = Clock::now();
			VfxLifecycleAudit& audit = g_vfxLifecycleAudits[particle];
			audit.component = particle;
			audit.particleTemplate = particle->Template;
			audit.retired = retired;
			audit.retainUntil = now + std::chrono::seconds(retired ? 6 : 3);
			audit.nextSample = now + std::chrono::milliseconds(250);
			audit.sample = 0;
		}

		void TickVfxLifecycleTrace()
		{
			if (!g_config.vfxLifecycleTrace)
				return;
			const Clock::time_point now = Clock::now();
			for (auto it = g_vfxLifecycleAudits.begin();
				it != g_vfxLifecycleAudits.end();)
			{
				VfxLifecycleAudit& audit = it->second;
				if (now >= audit.retainUntil
					|| !IsLivePresentationParticle(audit.component)
					|| audit.component->Template != audit.particleTemplate)
				{
					it = g_vfxLifecycleAudits.erase(it);
					continue;
				}
				if (now >= audit.nextSample)
				{
					const std::string stage = std::string(audit.retired
						? "retired-sample-" : "active-sample-")
						+ std::to_string(++audit.sample);
					TracePresentationParticleState(stage.c_str(), audit.component);
					audit.nextSample = now + std::chrono::milliseconds(750);
				}
				++it;
			}
		}

		void SoftRetirePresentationParticle(
			UParticleSystemComponent* particle,
			bool reinforceRenderScene = true)
		{
			if (!IsLivePresentationParticle(particle))
				return;

			// Calling DeactivateSystem/KillParticlesForced on a component
			// whose emitter instance has already completed can enter UE3 with
			// stale native emitter storage. Presentation particles do not
			// affect gameplay, so retire them through their state flags and
			// let the emitter pool/weapon owner reclaim them naturally.
			// Keep dynamic render updates enabled until SetHidden/ReturnToPool has
			// reached the render thread. Freezing dynamic data here preserves the
			// last rendered sprite/mesh forever on some UE3 emitter templates.
			particle->bSkipUpdateDynamicDataDuringTick = false;
			particle->HiddenGame = true;
			if (reinforceRenderScene)
				NativeSetComponentHidden(particle, true);
			particle->bAutoActivate = false;
			particle->bSuppressSpawning = true;
			particle->bWasDeactivated = true;
			particle->bWasCompleted = true;
			particle->bIsActive = false;
			particle->bForcedInActive = true;
			for (int32_t index = 0;
				index < std::min<int32_t>(
					particle->SMComponents.size(), 64); ++index)
			{
				UStaticMeshComponent* mesh =
					particle->SMComponents.at(index);
				if (!IsLiveUObject(mesh)
					|| !mesh->IsA(
						UStaticMeshComponent::StaticClass()))
					continue;
				mesh->HiddenGame = true;
				if (reinforceRenderScene)
					NativeSetComponentHidden(mesh, true);
			}
		}

		bool TryReturnPresentationParticleToPool(
			UParticleSystemComponent* particle)
		{
			if (!IsLivePresentationParticle(particle)
				|| particle->bIsCachedInPool
				|| !g_currentWorld
				|| !IsLiveUObject(g_currentWorld)
				|| !g_currentWorld->MyEmitterPool
				|| !IsLiveUObject(g_currentWorld->MyEmitterPool))
			{
				return false;
			}
			AEmitterPool* pool = g_currentWorld->MyEmitterPool;
			bool activeInThisPool = false;
			for (int32_t index = 0;
				index < std::min<int32_t>(pool->ActiveComponents.size(), 2048);
				++index)
			{
				if (pool->ActiveComponents.at(index) == particle)
				{
					activeInThisPool = true;
					break;
				}
			}
			if (!activeInThisPool)
				return false;

			// These proxy particles were acquired from WorldInfo's emitter
			// pool. Returning them explicitly is the only path that also clears
			// stale UE3 render data; merely setting HiddenGame leaves frozen
			// sprites and mesh emitters alive on a remote/network session.
			pool->ReturnToPool(particle);
			if (g_config.actionTrace)
			{
				Log("VFXSTAGE returned presentation particle to pool template="
					+ ObjectName(particle->Template) + '.');
			}
			if (particle->bIsCachedInPool)
				return true;

			// ReturnToPool is a protected native helper in UE3. Calling it on a
			// component which has only just been force-retired does not remove the
			// PSC from ActiveComponents in Alice. The pool's public completion
			// callback performs the missing ActiveComponents/SMComponents cleanup.
			// Use it only after the PSC is already inactive so an authored live
			// effect is never truncated through this fallback.
			if (!particle->bIsActive || particle->bWasCompleted
				|| particle->bWasDeactivated)
			{
				pool->OnParticleSystemFinished(particle);
				if (particle->bIsCachedInPool)
					return true;
				for (int32_t index = 0;
					index < std::min<int32_t>(
						pool->ActiveComponents.size(), 2048); ++index)
				{
					if (pool->ActiveComponents.at(index) == particle)
						return false;
				}
				return true;
			}
			return false;
		}

		void EnforcePresentationParticleRetirement(
			UParticleSystemComponent* particle,
			std::chrono::milliseconds lifetime =
				std::chrono::milliseconds(10000))
		{
			if (!IsLivePresentationParticle(particle)
				|| particle->bIsCachedInPool)
			{
				return;
			}
			const Clock::time_point enforceUntil =
				Clock::now() + lifetime;
			for (RetiredPresentationParticle& retired :
				g_retiredPresentationParticles)
			{
				if (retired.component == particle)
				{
					if (enforceUntil > retired.enforceUntil)
						retired.enforceUntil = enforceUntil;
					retired.particleTemplate = particle->Template;
					retired.nextNativeHideAt = {};
					return;
				}
			}
			// A pathological action/notify loop must never turn this safety
			// registry into another unbounded presentation leak.
			if (g_retiredPresentationParticles.size() >= 384)
			{
				g_retiredPresentationParticles.erase(
					g_retiredPresentationParticles.begin());
			}
			g_retiredPresentationParticles.push_back({
				particle, particle->Template, enforceUntil, {} });
		}

		void HardStopPresentationParticle(
			UParticleSystemComponent* particle, bool)
		{
			if (!IsLivePresentationParticle(particle))
				return;
			TracePresentationParticleState("retire-before", particle);
			AuditPresentationParticle(particle, true);
			if (particle->bIsCachedInPool)
			{
				TracePresentationParticleState("retire-already-cached", particle);
				return;
			}
			// A preserved movement-trail snapshot disables simulation and freezes
			// its render data. Restore normal component updates before returning it
			// to UE3's emitter pool so OFF can reliably remove the frozen frame.
			particle->bUpdateComponentInTick = true;
			particle->CustomTimeDilation = 1.0f;
			particle->bSkipUpdateDynamicDataDuringTick = false;
			// Return a still-valid active PSC first. ReturnToPool owns the native
			// emitter-instance teardown; marking it completed beforehand can make
			// UE3 skip that render cleanup and leave a frozen final frame behind.
			if (TryReturnPresentationParticleToPool(particle))
			{
				// ReturnToPool normally hides the PSC, but explicitly push one final
				// visibility update while dynamic data is still allowed. This covers
				// mesh emitters whose cached child component retained its last frame.
				particle->bSkipUpdateDynamicDataDuringTick = false;
				particle->HiddenGame = true;
				NativeSetComponentHidden(particle, true);
				for (int32_t index = 0;
					index < std::min<int32_t>(particle->SMComponents.size(), 64);
					++index)
				{
					UStaticMeshComponent* mesh =
						particle->SMComponents.at(index);
					if (IsLiveUObject(mesh))
					{
						mesh->HiddenGame = true;
						NativeSetComponentHidden(mesh, true);
					}
				}
				TracePresentationParticleState("retire-pooled", particle);
				return;
			}
			SoftRetirePresentationParticle(particle);
			TracePresentationParticleState("retire-hidden-fallback", particle);
			EnforcePresentationParticleRetirement(particle);
		}

		void RetirePersistentPresentationParticle(
			UParticleSystemComponent* particle)
		{
			if (!IsLivePresentationParticle(particle)
				|| particle->bIsCachedInPool)
				return;
			const std::string particleName =
				ObjectName(particle->Template);
			const int32_t emitterCount = std::min<int32_t>(
				particle->EmitterInstances.size(), 64);
			HardStopPresentationParticle(particle, true);
			if (g_config.actionTrace)
			{
				Log("VFXSTAGE retired persistent particle="
					+ particleName + ", emitters="
					+ std::to_string(emitterCount) + '.');
			}
		}

		void TickRetiredPresentationParticles()
		{
			const Clock::time_point now = Clock::now();
			auto next = g_retiredPresentationParticles.begin();
			for (auto current =
					g_retiredPresentationParticles.begin();
				current != g_retiredPresentationParticles.end();
				++current)
			{
				if (now >= current->enforceUntil)
					continue;
				UParticleSystemComponent* particle =
					current->component;
				if (IsLivePresentationParticle(particle)
					&& !particle->bIsCachedInPool)
				{
					if (TryReturnPresentationParticleToPool(particle))
						continue;
					// UE3's pooled PSC can keep stale dynamic render data even
					// after all activity flags are cleared. Keep the cheap flags
					// enforced every frame and periodically push SetHidden into
					// the render scene until the pool reclaims this exact use.
					const bool sameUse =
						particle->Template == current->particleTemplate;
					if (!sameUse)
						continue;
					const bool nativeHide =
						current->nextNativeHideAt
							== Clock::time_point{}
						|| now >= current->nextNativeHideAt;
					SoftRetirePresentationParticle(
						particle, nativeHide);
					if (nativeHide)
					{
						current->nextNativeHideAt =
							now + std::chrono::milliseconds(200);
					}
				}
				if (next != current)
					*next = *current;
				++next;
			}
			g_retiredPresentationParticles.erase(
				next, g_retiredPresentationParticles.end());
		}

		AActor* PresentationParticleBase(
			AEmitterPool* pool, UParticleSystemComponent* particle)
		{
			if (!pool || !particle)
				return nullptr;
			for (int32_t index = 0; index < std::min<int32_t>(
				pool->RelativePSCs.size(), 2048); ++index)
			{
				const FEmitterBaseInfo& info = pool->RelativePSCs.at(index);
				if (info.PSC == particle)
					return info.Base;
			}
			return nullptr;
		}

		float PresentationDistanceSquared(
			const FVector& left, const FVector& right)
		{
			const float dx = left.X - right.X;
			const float dy = left.Y - right.Y;
			const float dz = left.Z - right.Z;
			return dx * dx + dy * dy + dz * dz;
		}

		void PreserveRemoteMovementParticle(
			UParticleSystemComponent* particle,
			UParticleSystem* particleTemplate)
		{
			if (!IsLivePresentationParticle(particle) || !particleTemplate
				|| particle->bIsCachedInPool)
			{
				return;
			}
			const auto alreadyTracked = std::find_if(
				g_remoteMovementTransientParticles.begin(),
				g_remoteMovementTransientParticles.end(),
				[&](const TrackedPresentationParticle& tracked)
				{
					return tracked.component == particle
						&& tracked.particleTemplate == particleTemplate;
				});
			if (alreadyTracked != g_remoteMovementTransientParticles.end())
				return;

			// Preserve the old playtest behaviour deliberately, but bound it per
			// template and globally so the optional history cannot grow forever.
			auto matchingCount = [&]()
			{
				return static_cast<std::size_t>(std::count_if(
					g_remoteMovementTransientParticles.begin(),
					g_remoteMovementTransientParticles.end(),
					[&](const TrackedPresentationParticle& tracked)
					{
						return tracked.particleTemplate == particleTemplate;
					}));
			};
			while (matchingCount() >= 24u
				|| g_remoteMovementTransientParticles.size() >= 96u)
			{
				auto oldest = matchingCount() >= 24u
					? std::find_if(
						g_remoteMovementTransientParticles.begin(),
						g_remoteMovementTransientParticles.end(),
						[&](const TrackedPresentationParticle& tracked)
						{
							return tracked.particleTemplate
								== particleTemplate;
						})
					: g_remoteMovementTransientParticles.begin();
				if (oldest == g_remoteMovementTransientParticles.end())
					break;
				if (IsLivePresentationParticle(oldest->component)
					&& oldest->component->Template == oldest->particleTemplate)
				{
					HardStopPresentationParticle(oldest->component, true);
				}
				g_remoteMovementTransientParticles.erase(oldest);
			}
			// Recreate the useful form of the original frozen-VFX bug: retain the
			// last render frame at its current world transform, but stop simulation
			// and detach the relative emitter base so it cannot follow Alice.
			const FVector worldPosition(
				particle->LocalToWorld.WPlane.X,
				particle->LocalToWorld.WPlane.Y,
				particle->LocalToWorld.WPlane.Z);
			const FRotator worldRotation = particle->GetRotation();
			if (g_currentWorld && g_currentWorld->MyEmitterPool)
			{
				AEmitterPool* pool = g_currentWorld->MyEmitterPool;
				for (int32_t index = 0; index < std::min<int32_t>(
					pool->RelativePSCs.size(), 2048); ++index)
				{
					FEmitterBaseInfo& info = pool->RelativePSCs.at(index);
					if (info.PSC != particle)
						continue;
					info.Base = nullptr;
					info.RelativeLocation = worldPosition;
					info.RelativeRotation = worldRotation;
					info.bInheritBaseScale = false;
					break;
				}
			}
			particle->SetAbsolute(true, true, true);
			particle->SetTranslation(worldPosition);
			particle->SetRotation(worldRotation);
			particle->ForceUpdate(true);
			particle->bResetOnDetach = false;
			particle->bResetOnFinish = false;
			particle->bUpdateComponentInTick = false;
			particle->CustomTimeDilation = 0.0f;
			particle->bSkipUpdateDynamicDataDuringTick = true;
			g_remoteMovementTransientParticles.push_back({
				particle, particleTemplate, (Clock::time_point::max)() });
		}

		void BeginRemoteMovementParticleCapture(
			AAliceGamePawn* remote, const char* sequenceName)
		{
			if (!remote || !remote->WorldInfo
				|| !remote->WorldInfo->MyEmitterPool || !sequenceName)
			{
				return;
			}
			AEmitterPool* pool = remote->WorldInfo->MyEmitterPool;
			g_remoteMovementParticleCapture = {};
			g_remoteMovementParticleCapture.active = true;
			g_remoteMovementParticleCapture.remote = remote;
			g_remoteMovementParticleCapture.sequenceName = sequenceName;
			g_remoteMovementParticleCapture.captureUntil =
				Clock::now() + std::chrono::milliseconds(650);
			for (int32_t index = 0; index < std::min<int32_t>(
				pool->ActiveComponents.size(), 2048); ++index)
			{
				UParticleSystemComponent* particle =
					pool->ActiveComponents.at(index);
				if (IsLivePresentationParticle(particle))
				{
					g_remoteMovementParticleCapture.baseline[particle] =
						particle->Template;
				}
			}
		}

		void ScheduleRemoteMovementParticlePreservation(
			UParticleSystemComponent* particle,
			UParticleSystem* particleTemplate,
			std::chrono::milliseconds settleTime)
		{
			if (!IsLivePresentationParticle(particle) || !particleTemplate
				|| particle->bIsCachedInPool)
			{
				return;
			}
			const bool tracked = std::any_of(
				g_remoteMovementTransientParticles.begin(),
				g_remoteMovementTransientParticles.end(),
				[&](const TrackedPresentationParticle& current)
				{
					return current.component == particle
						&& current.particleTemplate == particleTemplate;
				});
			const bool pending = std::any_of(
				g_pendingMovementParticlePreservations.begin(),
				g_pendingMovementParticlePreservations.end(),
				[&](const PendingMovementParticlePreservation& current)
				{
					return current.component == particle
						&& current.particleTemplate == particleTemplate;
				});
			if (tracked || pending)
				return;
			if (g_pendingMovementParticlePreservations.size() >= 64)
			{
				g_pendingMovementParticlePreservations.erase(
					g_pendingMovementParticlePreservations.begin());
			}
			g_pendingMovementParticlePreservations.push_back({
				particle, particleTemplate, Clock::now() + settleTime });
			if (g_config.vfxLifecycleTrace)
			{
				Log("VFXCAPTURE delayed preserve template="
					+ ObjectName(particleTemplate) + ", settleMs="
					+ std::to_string(settleTime.count()) + '.');
			}
		}

		void TickRemoteMovementParticleCapture()
		{
			const Clock::time_point now = Clock::now();
			auto pendingNext =
				g_pendingMovementParticlePreservations.begin();
			for (auto current =
					g_pendingMovementParticlePreservations.begin();
				current != g_pendingMovementParticlePreservations.end();
				++current)
			{
				UParticleSystemComponent* particle = current->component;
				const bool sameUse = IsLivePresentationParticle(particle)
					&& particle->Template == current->particleTemplate
					&& !particle->bIsCachedInPool;
				if (!sameUse)
					continue;
				if (!g_config.preserveMovementTrails)
				{
					HardStopPresentationParticle(particle, true);
					continue;
				}
				if (now >= current->preserveAt)
				{
					PreserveRemoteMovementParticle(
						particle, current->particleTemplate);
					if (g_config.vfxLifecycleTrace)
					{
						Log("VFXCAPTURE delayed preserve applied template="
							+ ObjectName(current->particleTemplate) + '.');
					}
					continue;
				}
				if (pendingNext != current)
					*pendingNext = *current;
				++pendingNext;
			}
			g_pendingMovementParticlePreservations.erase(
				pendingNext,
				g_pendingMovementParticlePreservations.end());

			auto next = g_remoteMovementTransientParticles.begin();
			for (auto current = g_remoteMovementTransientParticles.begin();
				current != g_remoteMovementTransientParticles.end(); ++current)
			{
				UParticleSystemComponent* particle = current->component;
				const bool sameUse = IsLivePresentationParticle(particle)
					&& particle->Template == current->particleTemplate;
				if (sameUse && !particle->bIsCachedInPool
					&& !g_config.preserveMovementTrails
					&& now >= current->expiresAt)
				{
					HardStopPresentationParticle(particle, true);
				}
				if (sameUse && !particle->bIsCachedInPool
					&& (g_config.preserveMovementTrails
						|| now < current->expiresAt))
				{
					if (next != current)
						*next = *current;
					++next;
				}
			}
			g_remoteMovementTransientParticles.erase(
				next, g_remoteMovementTransientParticles.end());

			RemoteMovementParticleCapture& capture =
				g_remoteMovementParticleCapture;
			if (!capture.active)
				return;
			if (now >= capture.captureUntil || !capture.remote
				|| !IsLiveUObject(capture.remote)
				|| !capture.remote->WorldInfo
				|| !capture.remote->WorldInfo->MyEmitterPool)
			{
				capture = {};
				return;
			}

			AEmitterPool* pool = capture.remote->WorldInfo->MyEmitterPool;
			AAlicePawn* local = GetLocalPawn();
			const FVector remoteLocation = capture.remote->Location;
			for (int32_t index = 0; index < std::min<int32_t>(
				pool->ActiveComponents.size(), 2048); ++index)
			{
				UParticleSystemComponent* particle =
					pool->ActiveComponents.at(index);
				if (!IsLivePresentationParticle(particle)
					|| capture.baseline.find(particle)
						!= capture.baseline.end())
				{
					continue;
				}
				capture.baseline[particle] = particle->Template;
				const std::string templateName = ObjectName(particle->Template);
				const bool dodge = ContainsCaseInsensitive(templateName, "dodge")
					|| ContainsCaseInsensitive(templateName, "butter");
				const bool movementName =
					ContainsCaseInsensitive(templateName, "jump")
					|| ContainsCaseInsensitive(templateName, "double")
					|| ContainsCaseInsensitive(templateName, "wind")
					|| ContainsCaseInsensitive(templateName, "air")
					|| ContainsCaseInsensitive(templateName, "leaf")
					|| ContainsCaseInsensitive(templateName, "float")
					|| ContainsCaseInsensitive(templateName, "glide");
				AActor* base = PresentationParticleBase(pool, particle);
				const FVector position(
					particle->LocalToWorld.WPlane.X,
					particle->LocalToWorld.WPlane.Y,
					particle->LocalToWorld.WPlane.Z);
				const float remoteDistance = PresentationDistanceSquared(
					position, remoteLocation);
				const float localDistance = local
					? PresentationDistanceSquared(position, local->Location)
					: (std::numeric_limits<float>::max)();
				const bool remoteOwned = base == capture.remote;
				const bool remoteNearest = remoteDistance <= 350.0f * 350.0f
					&& (!local || remoteDistance + 100.0f * 100.0f
						< localDistance);
				const bool captureParticle = !dodge
					&& (remoteOwned || (movementName && remoteNearest));
				if (g_config.vfxLifecycleTrace)
				{
					std::ostringstream stream;
					stream << "VFXCAPTURE sequence=" << capture.sequenceName
						<< ", template=" << templateName
						<< ", component=" << ObjectName(particle)
						<< ", base=" << ObjectName(base)
						<< ", remoteDist=" << std::sqrt(remoteDistance)
						<< ", localDist=" << std::sqrt(localDistance)
						<< ", selected=" << (captureParticle ? 1 : 0) << '.';
					Log(stream.str());
				}
				if (!captureParticle || particle == g_remoteGlideParticle)
					continue;
				const bool alreadyTracked = std::any_of(
					g_remoteMovementTransientParticles.begin(),
					g_remoteMovementTransientParticles.end(),
					[&](const TrackedPresentationParticle& tracked)
					{
						return tracked.component == particle
							&& tracked.particleTemplate == particle->Template;
					});
				const bool alreadyPending = std::any_of(
					g_pendingMovementParticlePreservations.begin(),
					g_pendingMovementParticlePreservations.end(),
					[&](const PendingMovementParticlePreservation& pending)
					{
						return pending.component == particle
							&& pending.particleTemplate == particle->Template;
					});
				if (!alreadyTracked && !alreadyPending)
				{
					if (g_config.preserveMovementTrails)
					{
						const bool sharedPhysxLeafTrail =
							ContainsCaseInsensitive(templateName,
								"Nv_FX_Alice.Particles.GlideTrail");
						if (sharedPhysxLeafTrail)
						{
							// Nv GlideTrail owns a shared PhysX particle budget.
							// Freezing even one PSC exhausts that budget and every
							// later glide creates an active but empty component. Let
							// the authored instance finish normally; the independent
							// static trail below preserves history without retaining
							// PhysX particles.
							if (g_config.vfxLifecycleTrace)
							{
								Log("VFXCAPTURE shared PhysX GlideTrail left live (not frozen).");
							}
							continue;
						}
						// GlideTrail is Alice's leaf cloud. Freezing it on the
						// discovery frame captures only the tiny initial burst.
						// Let its authored simulation expand first; double-jump
						// wind/vortex components retain the immediate behaviour.
						const bool settlingLeafTrail =
							ContainsCaseInsensitive(templateName, "leaf")
							|| ContainsCaseInsensitive(templateName, "glide")
							|| ContainsCaseInsensitive(templateName, "float");
						if (settlingLeafTrail)
						{
							ScheduleRemoteMovementParticlePreservation(
								particle, particle->Template,
								std::chrono::milliseconds(300));
						}
						else
						{
							PreserveRemoteMovementParticle(
								particle, particle->Template);
						}
					}
					else
						g_remoteMovementTransientParticles.push_back({
							particle, particle->Template,
							now + std::chrono::milliseconds(1400) });
				}
			}
		}

		void TrackRemoteWeaponTransient(
			UParticleSystemComponent* particle,
			UParticleSystem* particleTemplate,
			std::chrono::milliseconds lifetime)
		{
			if (!particle || !particleTemplate)
				return;
			const Clock::time_point now = Clock::now();
			const std::string templateName =
				ObjectName(particleTemplate);
			const bool movementEffect =
				ContainsCaseInsensitive(templateName, "dodge")
				|| ContainsCaseInsensitive(templateName, "butterfly")
				|| ContainsCaseInsensitive(templateName, "glide")
				|| ContainsCaseInsensitive(templateName, "float")
				|| ContainsCaseInsensitive(templateName, "jump")
				|| ContainsCaseInsensitive(templateName, "leaf")
				|| ContainsCaseInsensitive(templateName, "wind");
			const bool sharedPhysxLeafTrail =
				ContainsCaseInsensitive(
					templateName, "Nv_FX_Alice.Particles.GlideTrail");
			const bool optionalTrailEffect =
				!sharedPhysxLeafTrail
				&& !ContainsCaseInsensitive(templateName, "dodge")
				&& !ContainsCaseInsensitive(templateName, "butterfly")
				&& (ContainsCaseInsensitive(templateName, "jump")
					|| ContainsCaseInsensitive(templateName, "double")
					|| ContainsCaseInsensitive(templateName, "glide")
					|| ContainsCaseInsensitive(templateName, "float")
					|| ContainsCaseInsensitive(templateName, "leaf")
					|| ContainsCaseInsensitive(templateName, "wind"));
			const std::size_t perTemplateLimit =
				g_config.preserveMovementTrails && optionalTrailEffect
					? 24u : (movementEffect ? 1u : 6u);
			if (movementEffect
				&& !(g_config.preserveMovementTrails
					&& optionalTrailEffect))
			{
				lifetime = (std::min)(
					lifetime, std::chrono::milliseconds(900));
			}

			for (auto current =
					g_remoteWeaponTransientParticles.begin();
				current != g_remoteWeaponTransientParticles.end();)
			{
				if (current->component == particle)
				current = g_remoteWeaponTransientParticles.erase(current);
				else
					++current;
			}
			auto matchingCount = [&]()
			{
				return static_cast<std::size_t>(std::count_if(
					g_remoteWeaponTransientParticles.begin(),
					g_remoteWeaponTransientParticles.end(),
					[&](const TrackedPresentationParticle& tracked)
					{
						return tracked.particleTemplate
							== particleTemplate;
					}));
			};
			while (matchingCount() >= perTemplateLimit)
			{
				auto oldest = std::find_if(
					g_remoteWeaponTransientParticles.begin(),
					g_remoteWeaponTransientParticles.end(),
					[&](const TrackedPresentationParticle& tracked)
					{
						return tracked.particleTemplate
							== particleTemplate;
					});
				if (oldest == g_remoteWeaponTransientParticles.end())
					break;
				if (IsLivePresentationParticle(oldest->component)
					&& oldest->component->Template
						== oldest->particleTemplate)
				{
					HardStopPresentationParticle(
						oldest->component, true);
				}
				g_remoteWeaponTransientParticles.erase(oldest);
			}
			if (g_remoteWeaponTransientParticles.size() >= 64)
			{
				const TrackedPresentationParticle oldest =
					g_remoteWeaponTransientParticles.front();
				if (IsLivePresentationParticle(oldest.component)
					&& oldest.component->Template
						== oldest.particleTemplate)
				{
					HardStopPresentationParticle(
						oldest.component, true);
				}
				g_remoteWeaponTransientParticles.erase(
					g_remoteWeaponTransientParticles.begin());
			}
			g_remoteWeaponTransientParticles.push_back({
				particle, particleTemplate,
				g_config.preserveMovementTrails && optionalTrailEffect
					? (Clock::time_point::max)() : now + lifetime });
		}

		void CleanupRemoteWeaponTransients(bool force)
		{
			const Clock::time_point now = Clock::now();
			auto next = g_remoteWeaponTransientParticles.begin();
			for (auto current =
					g_remoteWeaponTransientParticles.begin();
				current != g_remoteWeaponTransientParticles.end();
				++current)
			{
				const bool expired =
					force || now >= current->expiresAt;
				if (!expired)
				{
					if (next != current)
						*next = *current;
					++next;
					continue;
				}
				UParticleSystemComponent* particle =
					current->component;
				if (IsLivePresentationParticle(particle)
					&& !particle->bIsCachedInPool
					&& particle->Template
						== current->particleTemplate)
				{
					HardStopPresentationParticle(
						particle, true);
				}
			}
			g_remoteWeaponTransientParticles.erase(
				next, g_remoteWeaponTransientParticles.end());
		}

		void StopRemoteWeaponLoopParticles()
		{
			for (UParticleSystemComponent* particle
				: g_remoteWeaponLoopParticles)
			{
				HardStopPresentationParticle(particle, true);
			}
			g_remoteWeaponLoopParticles.clear();
		}

		const FWeaponLevelDataPackage*
			SelectWeaponPresentationPackage(AWeaponForAlice* weapon)
		{
			if (!weapon)
				return nullptr;
			const bool dlcMesh = weapon->Mesh
				&& ContainsCaseInsensitive(
					ObjectName(weapon->Mesh->SkeletalMesh), "dlc");
			if (dlcMesh
				&& (weapon->DLCPackage.WLDP_Particle_Trail
					|| weapon->DLCPackage.WLDP_Particle_Muzzle
					|| weapon->DLCPackage.WLDP_WeaponEffect.size() > 0
					|| weapon->DLCPackage
						.AttachedLoopingParticleArray.size() > 0))
			{
				return &weapon->DLCPackage;
			}
			if (weapon->WeaponLevelData.size() <= 0)
				return nullptr;
			const int32_t level = std::clamp<int32_t>(
				weapon->WeaponLevel - 1, 0,
				weapon->WeaponLevelData.size() - 1);
			return &weapon->WeaponLevelData.at(level);
		}

		void ApplyRemoteWeaponPresentationPackage(
			AWeaponForAlice* weapon, std::uint8_t weaponLevel,
			std::uint8_t weaponVariant)
		{
			if (!weapon)
				return;
			const int32_t level = std::max<int32_t>(
				1, weaponLevel);
			weapon->WeaponLevel = level;
			const FWeaponLevelDataPackage* package = nullptr;
			const bool useDlc = weaponVariant == 2
				? weapon->Mesh
					&& ContainsCaseInsensitive(
						ObjectName(weapon->Mesh->SkeletalMesh),
						"dlc")
				: weaponVariant != 0;
			if (useDlc
				&& (weapon->DLCPackage.WLDP_SkeletalMesh
					|| weapon->DLCPackage.WLDP_Particle_Trail
					|| weapon->DLCPackage.WLDP_Particle_Muzzle))
			{
				package = &weapon->DLCPackage;
			}
			else if (weapon->WeaponLevelData.size() > 0)
			{
				const int32_t index = std::clamp<int32_t>(
					level - 1, 0,
					weapon->WeaponLevelData.size() - 1);
				package = &weapon->WeaponLevelData.at(index);
			}
			if (!package)
				return;
			if (weapon->Mesh && package->WLDP_SkeletalMesh
				&& weapon->Mesh->SkeletalMesh
					!= package->WLDP_SkeletalMesh)
			{
				weapon->Mesh->SetSkeletalMesh(
					package->WLDP_SkeletalMesh, false, false);
			}
			weapon->TracePSCTemplate =
				package->WLDP_Particle_Trail;
			weapon->MuzzleFlashPSCTemplate =
				package->WLDP_Particle_Muzzle;
			Log("WEAPONSTAGE package applied level="
				+ std::to_string(level)
				+ ", variant="
				+ std::to_string(weaponVariant)
				+ ", mesh="
				+ ObjectName(package->WLDP_SkeletalMesh)
				+ ", trail="
				+ ObjectName(package->WLDP_Particle_Trail)
				+ ", muzzle="
				+ ObjectName(package->WLDP_Particle_Muzzle)
				+ ", effects="
				+ std::to_string(
					package->WLDP_WeaponEffect.size()) + '.');
		}

		void LogWeaponPresentationHierarchy(
			AWeaponForAlice* weapon, const char* side)
		{
			if (!weapon)
				return;
			const std::string signature =
				std::string(side ? side : "<unknown>")
				+ '|' + ObjectName(weapon->Class)
				+ '|' + std::to_string(weapon->WeaponLevel)
				+ '|' + ObjectName(weapon->Mesh
					? weapon->Mesh->SkeletalMesh : nullptr);
			if (!g_loggedWeaponHierarchySignatures
				.insert(signature).second)
			{
				return;
			}
			std::ostringstream summary;
			summary << "WEAPONHIER " << side
				<< " actor=" << ObjectName(weapon)
				<< ", class=" << ObjectName(weapon->Class)
				<< ", level=" << weapon->WeaponLevel
				<< ", mesh="
				<< ObjectName(weapon->Mesh
					? weapon->Mesh->SkeletalMesh : nullptr)
				<< ", components=" << weapon->AllComponents.size()
				<< ", trace=" << ObjectName(weapon->TracePSCTemplate)
				<< ", muzzle="
				<< ObjectName(weapon->MuzzleFlashPSCTemplate)
				<< ", charge="
				<< ObjectName(weapon->ChargePSCTemplate)
				<< ", chargeFinish="
				<< ObjectName(weapon->ChargeFinishPSCTemplate)
				<< ", effects="
				<< weapon->WeaponEffectPSCTemplate.size()
				<< ", effectSockets=" << weapon->EffectSockets.size()
				<< '.';
			Log(summary.str());

			const int32_t levelCount = std::min<int32_t>(
				weapon->WeaponLevelData.size(), 8);
			auto logPackage =
				[side](const char* name, int32_t level,
					const FWeaponLevelDataPackage& package)
			{
				Log(std::string("WEAPONHIER ") + side + ' '
					+ name + '[' + std::to_string(level)
					+ "] trail="
					+ ObjectName(package.WLDP_Particle_Trail)
					+ ", muzzle="
					+ ObjectName(package.WLDP_Particle_Muzzle)
					+ ", effects="
					+ std::to_string(
						package.WLDP_WeaponEffect.size())
					+ ", loops="
					+ std::to_string(package
						.AttachedLoopingParticleArray.size())
					+ ", projectile="
					+ ObjectName(package.WLDP_Project1) + '.');
				const int32_t loopCount = std::min<int32_t>(
					package.AttachedLoopingParticleArray.size(), 16);
				for (int32_t loop = 0; loop < loopCount; ++loop)
				{
					const FAttachedLoopingParticleEffect& effect =
						package.AttachedLoopingParticleArray.at(loop);
					Log(std::string("WEAPONHIER ") + side
						+ " loop[" + std::to_string(loop)
						+ "] template="
						+ ObjectName(
							effect.WLDP_AttachedLoopingParticle)
						+ ", socket="
						+ (effect.AttachedLoopingParticleSocket
								.IsValid()
							? effect.AttachedLoopingParticleSocket
								.ToString()
							: std::string("<invalid>"))
						+ ", component="
						+ ObjectName(effect
							.AttachedLoopingParticleComponent)
						+ '.');
				}
			};
			for (int32_t level = 0; level < levelCount; ++level)
				logPackage("level", level,
					weapon->WeaponLevelData.at(level));
			logPackage("dlc", 0, weapon->DLCPackage);

			const int32_t componentCount = std::min<int32_t>(
				weapon->AllComponents.size(), 96);
			for (int32_t index = 0; index < componentCount; ++index)
			{
				UActorComponent* component =
					weapon->AllComponents.at(index);
				if (!component)
					continue;
				std::ostringstream item;
				item << "WEAPONHIER " << side
					<< " component[" << index << "]="
					<< ObjectName(component)
					<< ", class=" << ObjectName(component->Class);
				if (component->IsA(
					UParticleSystemComponent::StaticClass()))
				{
					auto* particle = reinterpret_cast<
						UParticleSystemComponent*>(component);
					item << ", PSC template="
						<< ObjectName(particle->Template)
						<< ", active=" << particle->bIsActive
						<< ", auto=" << particle->bAutoActivate
						<< ", cached=" << particle->bIsCachedInPool
						<< ", hidden=" << particle->HiddenGame;
				}
				else if (component->IsA(
					USkeletalMeshComponent::StaticClass()))
				{
					auto* mesh = reinterpret_cast<
						USkeletalMeshComponent*>(component);
					item << ", skeletal="
						<< ObjectName(mesh->SkeletalMesh);
				}
				item << '.';
				Log(item.str());
			}
			if (weapon->IsA(AEyeStaff::StaticClass()))
			{
				auto* pepper = reinterpret_cast<AEyeStaff*>(weapon);
				Log(std::string("WEAPONHIER ") + side
					+ " pepper muzzleActor="
					+ ObjectName(pepper->MuzzleParticleActor)
					+ ", normalMuzzle="
					+ ObjectName(pepper->NormalMuzzleParticle)
					+ ", muzzleLight="
					+ ObjectName(pepper->MuzzleFlashLight)
					+ ", fireSockets="
					+ std::to_string(
						pepper->FireSocketArray.size()) + '.');
			}
		}

		void ResetRemoteWeaponPresentationState()
		{
			StopRemoteWeaponLoopParticles();
			CleanupRemoteWeaponTransients(true);
			g_remotePresentationWeapon = nullptr;
			g_remotePresentationWeaponType =
				static_cast<std::uint8_t>(EAliceWeaponType::EAWT_None);
			g_remotePresentationWeaponLevel = 1;
			g_remotePresentationWeaponVariant = 0;
			g_remotePresentationWeaponSocket = FName();
			g_remoteWeaponBaseDrawScale = 1.0f;
			g_remoteAppliedWeaponAnimation.clear();
			g_remoteWeaponAnimationLastSeen = {};
			g_nextRemoteWeaponSpawnAttempt = {};
			g_remoteMuzzleFlashActive = false;
			g_remoteMuzzleLastActiveRequest = {};
			if (g_remoteMuzzleParticle)
				RetirePersistentPresentationParticle(
					g_remoteMuzzleParticle);
			g_remoteMuzzleParticle = nullptr;
			g_remoteAttackTrailActive = false;
			g_remoteAttackTrailUntil = {};
			if (g_remoteAttackTrailParticle)
				HardStopPresentationParticle(
					g_remoteAttackTrailParticle, true);
			g_remoteAttackTrailParticle = nullptr;
		}

		void PrepareRemoteWeaponParticleComponents(
			AWeaponForAlice* weapon)
		{
			if (!weapon)
				return;

			// Weapon archetypes keep idle and looping emitters as owned
			// components rather than notify templates. Rebuild that cosmetic
			// layer without entering a firing or damage state.
			weapon->SetLoopingParticle(weapon->WeaponLevel);
			int particleCount = 0;
			for (int32_t index = 0;
				index < weapon->AllComponents.size(); ++index)
			{
				UActorComponent* component =
					weapon->AllComponents.at(index);
				if (!component
					|| !component->IsA(
						UParticleSystemComponent::StaticClass()))
				{
					continue;
				}
				auto* particle =
					reinterpret_cast<UParticleSystemComponent*>(
						component);
				// Proxy-owned PSCs are often authored with auto-activate even
				// when the real weapon enables them only from its fire state.
				// Keep them dormant here; authored idle loops are spawned and
				// tracked explicitly below. Do not call KillParticlesForced on a
				// reused weapon component: its native emitter storage can already
				// be completed after a prior weapon switch.
				SoftRetirePresentationParticle(particle);
				Log("WEAPONSTAGE owned PSC["
					+ std::to_string(particleCount)
					+ "]=" + ObjectName(particle)
					+ ", template="
					+ ObjectName(particle->Template)
					+ ", auto="
					+ std::string(
						particle->bAutoActivate ? "yes" : "no")
					+ ", active="
					+ std::string(
						particle->bIsActive ? "yes." : "no."));
				++particleCount;
			}
			Log("WEAPONSTAGE owned particle components="
				+ std::to_string(particleCount) + '.');
		}

		void SpawnRemoteWeaponLoopParticles(
			AWeaponForAlice* weapon)
		{
			StopRemoteWeaponLoopParticles();
			const FWeaponLevelDataPackage* package =
				SelectWeaponPresentationPackage(weapon);
			if (!weapon || !weapon->Mesh || !package
				|| !weapon->WorldInfo
				|| !weapon->WorldInfo->MyEmitterPool)
			{
				return;
			}
			const int32_t count = std::min<int32_t>(
				package->AttachedLoopingParticleArray.size(), 16);
			for (int32_t index = 0; index < count; ++index)
			{
				const FAttachedLoopingParticleEffect& effect =
					package->AttachedLoopingParticleArray.at(index);
				if (!effect.WLDP_AttachedLoopingParticle)
					continue;
				FName attachPoint =
					effect.AttachedLoopingParticleSocket;
				if (!attachPoint.IsValid())
					attachPoint = FirstWeaponSocket(weapon);
				UParticleSystemComponent* particle =
					weapon->WorldInfo->MyEmitterPool
						->SpawnEmitterMeshAttachment(
							effect.WLDP_AttachedLoopingParticle,
							weapon->Mesh, attachPoint,
							WeaponMeshHasSocket(
								weapon, attachPoint),
							FVector(0.0f, 0.0f, 0.0f),
							FRotator(0, 0, 0));
				MakePresentationParticleVisible(particle);
				if (particle)
					g_remoteWeaponLoopParticles.push_back(particle);
				Log("WEAPONSTAGE explicit loop["
					+ std::to_string(index) + "] template="
					+ ObjectName(
						effect.WLDP_AttachedLoopingParticle)
					+ ", point="
					+ (attachPoint.IsValid()
						? attachPoint.ToString()
						: std::string("<invalid>"))
					+ ", kind="
					+ (WeaponMeshHasSocket(weapon, attachPoint)
						? "socket"
						: (WeaponMeshHasBone(weapon, attachPoint)
							? "bone" : "missing"))
					+ ", spawned="
					+ (particle ? "yes." : "no."));
			}
		}

		void HardStopRemoteWeaponComponents(
			AWeaponForAlice* weapon)
		{
			if (!weapon)
				return;
			weapon->eventStopMuzzleFlash();
			weapon->StopParticleTrail();
			if (weapon->IsA(
				AWeaponForAliceRange::StaticClass()))
			{
				reinterpret_cast<AWeaponForAliceRange*>(weapon)
					->StopAllParticlesAndSounds();
			}
			std::unordered_set<UParticleSystemComponent*> particles;
			auto add = [&particles](
				UParticleSystemComponent* particle)
			{
				if (particle)
					particles.insert(particle);
			};
			add(weapon->MuzzleFlashPSC);
			add(weapon->FlushParticleComponent);
			add(weapon->ChargeParticleComponent);
			if (weapon->IsA(
				AWeaponForAliceRange::StaticClass()))
			{
				add(reinterpret_cast<AWeaponForAliceRange*>(weapon)
					->NormalMuzzleParticle);
			}
			for (int32_t index = 0;
				index < weapon->AllComponents.size(); ++index)
			{
				UActorComponent* component =
					weapon->AllComponents.at(index);
				if (component && component->IsA(
					UParticleSystemComponent::StaticClass()))
				{
					add(reinterpret_cast<
						UParticleSystemComponent*>(component));
				}
			}
			for (UParticleSystemComponent* particle : particles)
				HardStopPresentationParticle(particle, false);
			if (weapon->MuzzleFlashLight)
				weapon->MuzzleFlashLight->SetEnabled(false);
			if (weapon->IsA(AEyeStaff::StaticClass()))
			{
				auto* pepper = reinterpret_cast<AEyeStaff*>(weapon);
				if (pepper->MuzzleParticleActor)
				{
					HardStopPresentationParticle(
						pepper->MuzzleParticleActor
							->ParticleSystemComponent,
						false);
					pepper->MuzzleParticleActor->bHidden = true;
					NativeSetActorHidden(
						pepper->MuzzleParticleActor, true);
				}
			}
		}

		void RetireRemoteWeaponOwnedParticles(
			AWeaponForAlice* weapon)
		{
			if (!weapon || !IsLiveUObject(weapon))
				return;
			std::unordered_set<UParticleSystemComponent*> retired;
			auto retire = [&](UParticleSystemComponent* particle)
			{
				if (!IsLivePresentationParticle(particle)
					|| !retired.insert(particle).second)
				{
					return;
				}
				particle->bIgnoreOwnerHidden = false;
				HardStopPresentationParticle(particle, true);
			};
			for (int32_t index = 0; index < std::min<int32_t>(
					weapon->AllComponents.size(), 128); ++index)
			{
				UActorComponent* component =
					weapon->AllComponents.at(index);
				if (component && component->IsA(
						UParticleSystemComponent::StaticClass()))
				{
					retire(reinterpret_cast<
						UParticleSystemComponent*>(component));
				}
			}
			retire(weapon->MuzzleFlashPSC);
			retire(weapon->FlushParticleComponent);
			if (weapon->IsA(AWeaponForAliceRange::StaticClass()))
			{
				auto* range =
					reinterpret_cast<AWeaponForAliceRange*>(weapon);
				retire(range->NormalMuzzleParticle);
			}
		}

		void DestroyRemotePresentationWeapon(bool finalTeardown = false)
		{
			AWeaponForAlice* weapon = g_remotePresentationWeapon;
			if (weapon && !IsLiveUObject(weapon))
			{
				Log("WEAPONSTAGE stale presentation weapon discarded "
					"without teardown.");
				ResetRemoteWeaponPresentationState();
				return;
			}
			if (g_remotePawn && !IsLiveUObject(g_remotePawn))
				g_remotePawn = nullptr;
			if (weapon && g_remoteMuzzleFlashActive)
				weapon->eventStopMuzzleFlash();
			if (weapon && g_remoteAttackTrailActive)
				weapon->eventNotifyMeleeAttackTraceParticleChange(false);
			if (g_remotePawn && g_remotePawn->Weapon == weapon)
				g_remotePawn->Weapon = nullptr;
			if (weapon && g_remoteWorld
				&& g_remoteWorld == g_currentWorld)
			{
				const std::string weaponName = ObjectName(weapon);
				const std::uint8_t weaponType =
					g_remotePresentationWeaponType;
				CleanupRemoteWeaponTransients(true);
				StopRemoteWeaponLoopParticles();
				RetireRemoteWeaponOwnedParticles(weapon);
				// Owned PSCs are reclaimed by DestroyActor. Explicitly
				// deactivating them here caused a use-after-destroy inside
				// UParticleSystemComponent::DeactivateSystem after combat
				// weapon switches.
				weapon->bNoDelete = false;
				weapon->bStatic = false;
				if (weapon->Mesh)
				{
					weapon->Mesh->HiddenGame = true;
					NativeSetComponentHidden(
						weapon->Mesh, true);
				}
				weapon->bHidden = true;
				NativeSetActorHidden(weapon, true);
				weapon->SetTickIsDisabled(true);
				if (weapon->Mesh && g_remotePawn
					&& g_remotePawn->Mesh
					&& weapon->Mesh->AttachedToSkelComponent
						== g_remotePawn->Mesh)
				{
					NativeDetachComponent(
						g_remotePawn->Mesh, weapon->Mesh);
				}
				const FVector retiredLocation(
					weapon->Location.X,
					weapon->Location.Y,
					weapon->Location.Z - 1000000.0f);
				weapon->SetLocationNoCheck(retiredLocation);
				weapon->Location = retiredLocation;
				if (!finalTeardown && weaponType >= 1
					&& weaponType < g_remoteWeaponCache.size())
				{
					g_remoteWeaponCache[weaponType] = weapon;
					Log("WEAPONSTAGE parked actor=" + weaponName
						+ ", type=" + std::to_string(weaponType)
						+ '.');
				}
				else
				{
					const bool destroyed = NativeDestroyActor(weapon);
					Log("WEAPONSTAGE destroy actor=" + weaponName
						+ ", result="
						+ (destroyed ? std::string("destroyed.")
							: std::string("hidden-only.")));
				}
			}
			ResetRemoteWeaponPresentationState();
			if (finalTeardown)
			{
				for (AWeaponForAlice*& cached : g_remoteWeaponCache)
				{
					if (!cached || cached == weapon)
					{
						cached = nullptr;
						continue;
					}
					if (IsLiveUObject(cached))
					{
						RetireRemoteWeaponOwnedParticles(cached);
						cached->bHidden = true;
						NativeSetActorHidden(cached, true);
						cached->SetTickIsDisabled(true);
						cached->bNoDelete = false;
						cached->bStatic = false;
						NativeDestroyActor(cached);
					}
					cached = nullptr;
				}
			}
		}

		const FWeaponPara* FindRemoteWeaponPara(AAlicePawn* remote,
			std::uint8_t weaponType)
		{
			if (!remote
				|| weaponType
					== static_cast<std::uint8_t>(
						EAliceWeaponType::EAWT_None))
			{
				return nullptr;
			}
			const int32_t count = std::min<int32_t>(
				remote->WeaponParas.size(), 64);
			for (int32_t index = 0; index < count; ++index)
			{
				const FWeaponPara& para = remote->WeaponParas.at(index);
				if (!para.WeaponClass || !para.WeaponArcheType
					|| !para.WeaponArcheType->IsA(
						AWeaponForAlice::StaticClass()))
				{
					continue;
				}
				auto* archetype =
					reinterpret_cast<AWeaponForAlice*>(
						para.WeaponArcheType);
				if (static_cast<std::uint8_t>(
					archetype->WeaponTypeEnum) == weaponType)
				{
					return &para;
				}
			}
			return nullptr;
		}

		AWeaponForAlice* EnsureRemotePresentationWeapon(
			AAlicePawn* remote, std::uint8_t weaponType,
			std::uint8_t weaponLevel, std::uint8_t weaponVariant)
		{
			if (!remote)
				return nullptr;
			if (weaponType
				== static_cast<std::uint8_t>(
					EAliceWeaponType::EAWT_None))
			{
				if (g_remotePresentationWeapon)
					DestroyRemotePresentationWeapon();
				return nullptr;
			}
			if (g_remotePresentationWeapon
				&& g_remotePresentationWeaponType == weaponType)
			{
				remote->Weapon = g_remotePresentationWeapon;
				if (g_remotePresentationWeaponLevel != weaponLevel
					|| g_remotePresentationWeaponVariant
						!= weaponVariant)
				{
					g_remotePresentationWeaponLevel = weaponLevel;
					g_remotePresentationWeaponVariant =
						weaponVariant;
					StopRemoteWeaponLoopParticles();
					ApplyRemoteWeaponPresentationPackage(
						g_remotePresentationWeapon,
						weaponLevel, weaponVariant);
					PrepareRemoteWeaponParticleComponents(
						g_remotePresentationWeapon);
					SpawnRemoteWeaponLoopParticles(
						g_remotePresentationWeapon);
				}
				return g_remotePresentationWeapon;
			}
			if (g_remotePresentationWeapon)
				DestroyRemotePresentationWeapon();
			if (Clock::now() < g_nextRemoteWeaponSpawnAttempt)
				return nullptr;
			g_nextRemoteWeaponSpawnAttempt =
				Clock::now() + std::chrono::seconds(2);

			const FWeaponPara* para =
				FindRemoteWeaponPara(remote, weaponType);
			if (!para)
			{
				Log("WEAPONSTAGE no proxy archetype for type="
					+ std::to_string(weaponType)
					+ ", paras="
					+ std::to_string(remote->WeaponParas.size()) + '.');
				return nullptr;
			}

			AActor* spawned = nullptr;
			bool reused = false;
			if (weaponType < g_remoteWeaponCache.size())
			{
				AWeaponForAlice* cached =
					g_remoteWeaponCache[weaponType];
				if (cached && IsLiveUObject(cached)
					&& cached->WorldInfo == remote->WorldInfo)
				{
					spawned = cached;
					reused = true;
				}
				g_remoteWeaponCache[weaponType] = nullptr;
			}
			if (!spawned)
			{
				spawned = NativeSpawn(remote, para->WeaponClass,
					remote, FName(), remote->Location, remote->Rotation,
					para->WeaponArcheType, true);
			}
			if (!spawned
				|| !spawned->IsA(AWeaponForAlice::StaticClass()))
			{
				if (spawned)
					NativeDestroyActor(spawned);
				Log("WEAPONSTAGE failed to spawn type="
					+ std::to_string(weaponType)
					+ ", archetype="
					+ ObjectName(para->WeaponArcheType) + '.');
				return nullptr;
			}

			auto* weapon = reinterpret_cast<AWeaponForAlice*>(spawned);
			weapon->bNoDelete = false;
			weapon->bStatic = false;
			NativeSetOwner(weapon, remote);
			weapon->Owner = remote;
			weapon->Instigator = remote;
			weapon->bCanBeDamaged = false;
			weapon->bCollideActors = false;
			weapon->bCollideWorld = false;
			weapon->bBlockActors = false;
			weapon->RemoteRole = ENetRole::ROLE_None;
			weapon->Role = ENetRole::ROLE_Authority;
			weapon->SetTickIsDisabled(false);
			weapon->SetLocationNoCheck(remote->Location);
			weapon->SetRotation(remote->Rotation);
			weapon->Location = remote->Location;
			weapon->Rotation = remote->Rotation;
			remote->Weapon = weapon;
			remote->SetWeaponParaInfo(weapon, *para);
			weapon->CacheAnimNodes();
			EnableRemoteAnimationTick(remote);
			const bool gameAttachInvoked = NativeAttachWeaponToSocket(
				weapon, para->DefaultAttachedSocketName);
			NativeForceSkelUpdate(remote->Mesh);
			const bool socketAttachInvoked =
				weapon->Mesh && NativeAttachComponentToSocket(
					remote->Mesh, weapon->Mesh,
					para->DefaultAttachedSocketName);
			const bool socketBound = weapon->Mesh
				&& weapon->Mesh->AttachedToSkelComponent == remote->Mesh;
			weapon->bHidden = false;
			if (weapon->Mesh)
			{
				weapon->Mesh->bOwnerNoSee = false;
				weapon->Mesh->bOnlyOwnerSee = false;
				weapon->Mesh->HiddenGame = false;
				NativeSetComponentHidden(weapon->Mesh, false);
				weapon->Mesh->bPauseAnims = false;
				weapon->Mesh->bNoSkeletonUpdate = false;
				weapon->Mesh->bUpdateSkelWhenNotRendered = true;
				weapon->Mesh->bTickAnimNodesWhenNotRendered = true;
				NativeForceSkelUpdate(weapon->Mesh);
				NativeForceComponentUpdate(weapon->Mesh, false);
			}
			NativeSetActorHidden(weapon, false);
			NativeForceUpdateComponents(weapon, false, false);
			NativeForceUpdateComponents(remote, false, true);

			g_remotePresentationWeapon = weapon;
			g_remotePresentationWeaponType = weaponType;
			g_remotePresentationWeaponLevel = weaponLevel;
			g_remotePresentationWeaponVariant = weaponVariant;
			g_remoteWeaponBaseDrawScale =
				weapon->DrawScale > 0.001f
					? weapon->DrawScale : 1.0f;
			g_remotePresentationWeaponSocket =
				para->DefaultAttachedSocketName;
			g_remoteAppliedWeaponAnimation.clear();
			g_remoteWeaponAnimationLastSeen = {};
			g_remoteMuzzleFlashActive = false;
			g_nextRemoteWeaponSpawnAttempt = {};
			ApplyRemoteWeaponPresentationPackage(
				weapon, weaponLevel, weaponVariant);
			PrepareRemoteWeaponParticleComponents(weapon);
			SpawnRemoteWeaponLoopParticles(weapon);
			LogWeaponPresentationHierarchy(weapon, "proxy");
			Log(std::string("WEAPONSTAGE ")
				+ (reused ? "reused" : "spawned") + " type="
				+ std::to_string(weaponType)
				+ ", actor=" + ObjectName(weapon)
				+ ", mesh=" + ObjectName(
					weapon->Mesh
						? weapon->Mesh->SkeletalMesh : nullptr)
				+ ", socket="
				+ (para->DefaultAttachedSocketName.IsValid()
					? para->DefaultAttachedSocketName.ToString()
					: std::string("<invalid>"))
				+ ", gameAttach="
				+ (gameAttachInvoked ? "invoked" : "failed")
				+ ", socketAttach="
				+ (socketAttachInvoked ? "invoked" : "failed")
				+ ", socketBound="
				+ (socketBound ? "yes" : "no")
				+ ", parent="
				+ ObjectName(weapon->Mesh
					? weapon->Mesh->AttachedToSkelComponent
					: nullptr)
				+ ", world=("
				+ std::to_string(weapon->Mesh
					? weapon->Mesh->LocalToWorld.WPlane.X : 0.0f)
				+ ','
				+ std::to_string(weapon->Mesh
					? weapon->Mesh->LocalToWorld.WPlane.Y : 0.0f)
				+ ','
				+ std::to_string(weapon->Mesh
					? weapon->Mesh->LocalToWorld.WPlane.Z : 0.0f)
				+ ')'
				+ ", slot=" + ObjectName(weapon->SlotNode) + '.');
			std::ostringstream vfx;
			vfx << "WEAPONSTAGE VFX templates: trace="
				<< ObjectName(weapon->TracePSCTemplate)
				<< ", muzzle="
				<< ObjectName(weapon->MuzzleFlashPSCTemplate)
				<< ", effects="
				<< weapon->WeaponEffectPSCTemplate.size()
				<< ", traceSocket="
				<< (weapon->TraceSocket.IsValid()
					? weapon->TraceSocket.ToString()
					: "<invalid>")
				<< ", muzzleSocket="
				<< (weapon->MuzzleFlashSocket.IsValid()
					? weapon->MuzzleFlashSocket.ToString()
					: "<invalid>")
				<< ", rangeSocket="
				<< (weapon->RangeAttackSocket.IsValid()
					? weapon->RangeAttackSocket.ToString()
					: "<invalid>")
				<< ", meshSockets=[";
			if (weapon->Mesh && weapon->Mesh->SkeletalMesh)
			{
				const int32_t socketCount = std::min<int32_t>(
					weapon->Mesh->SkeletalMesh->Sockets.size(),
					32);
				for (int32_t index = 0;
					index < socketCount; ++index)
				{
					USkeletalMeshSocket* socket =
						weapon->Mesh->SkeletalMesh
							->Sockets.at(index);
					if (!socket)
						continue;
					if (index > 0)
						vfx << ',';
					vfx << (socket->SocketName.IsValid()
						? socket->SocketName.ToString()
						: "<invalid>");
				}
			}
			vfx << "].";
			Log(vfx.str());
			return weapon;
		}

