		float NativePlaybackRate(UAnimNodeSequence* node, float effectiveRate)
		{
			return NormalizeAnimationRate(effectiveRate,
				node && node->SkelComponent ? node->SkelComponent->GlobalAnimRateScale : 1.0f,
				node && node->AnimSeq ? node->AnimSeq->RateScale : 1.0f);
		}

		float RemoteAnimationSampleAge()
		{
			return g_activeRemoteAnimationGraph ? std::clamp(std::chrono::duration<float>(
				Clock::now() - g_activeRemoteAnimationGraph->receivedAt).count(), 0.0f, 0.10f) : 0.0f;
		}

		bool HasInstancedSpecialMove(AAlicePawn* remote, std::uint8_t moveIndex)
		{
			return remote
				&& moveIndex > static_cast<std::uint8_t>(ESpecialMove::SM_None)
				&& moveIndex < static_cast<std::uint8_t>(ESpecialMove::SM_END)
				&& moveIndex < remote->SpecialMoves.size()
				&& remote->SpecialMoves.at(moveIndex) != nullptr;
		}

		bool EnsureInstancedSpecialMove(AAlicePawn* remote, std::uint8_t moveIndex)
		{
			if (HasInstancedSpecialMove(remote, moveIndex))
				return true;
			if (!remote
				|| moveIndex <= static_cast<std::uint8_t>(ESpecialMove::SM_None)
				|| moveIndex >= static_cast<std::uint8_t>(ESpecialMove::SM_END)
				|| moveIndex >= remote->SpecialMoveClasses.size()
				|| !remote->SpecialMoveClasses.at(moveIndex))
			{
				return false;
			}

			const int32_t countBefore = remote->SpecialMoves.size();
			const auto move = static_cast<ESpecialMove>(moveIndex);
			const bool verified = remote->VerifySMHasBeenInstanced(move);
			const bool usable = HasInstancedSpecialMove(remote, moveIndex);
			Log("Remote special move lazy init: move=" + std::to_string(moveIndex)
				+ ", verify=" + (verified ? "true" : "false")
				+ ", instances=" + std::to_string(countBefore) + " -> "
				+ std::to_string(remote->SpecialMoves.size())
				+ ", usable=" + (usable ? "true." : "false."));
			return usable;
		}

		void ApplyRemoteAction(AAlicePawn* remote, const PlayerStatePayload& state)
		{
			if (state.actionSerial != 0 && state.actionSerial != g_lastRemoteActionSerial)
			{
				Log("Remote action serial=" + std::to_string(state.actionSerial)
					+ ", action=" + std::to_string(static_cast<int>(state.action))
					+ ", specialMove=" + std::to_string(state.specialMove) + '.');
				g_lastRemoteActionSerial = state.actionSerial;
			}

			if (state.specialMove == g_lastRemoteSpecialMove)
				return;

			const std::uint8_t previousMove = g_lastRemoteSpecialMove;
			g_lastRemoteSpecialMove = state.specialMove;
			if (state.specialMove == static_cast<std::uint8_t>(ESpecialMove::SM_None))
			{
				if (previousMove != static_cast<std::uint8_t>(ESpecialMove::SM_None)
					&& remote->SpecialMove != ESpecialMove::SM_None)
				{
					remote->EndSpecialMove(remote->SpecialMove);
				}
				return;
			}

			if (!EnsureInstancedSpecialMove(remote, state.specialMove))
			{
				if (!g_loggedMissingSpecialMoves)
				{
					Log("WARN: Remote action animation is unavailable: requested move="
						+ std::to_string(state.specialMove)
						+ ", specialMoveClasses=" + std::to_string(remote->SpecialMoveClasses.size())
						+ ", specialMoves=" + std::to_string(remote->SpecialMoves.size()) + '.');
					g_loggedMissingSpecialMoves = true;
				}
				return;
			}

			const auto move = static_cast<ESpecialMove>(state.specialMove);
			if (remote->SpecialMove != ESpecialMove::SM_None
				&& remote->SpecialMove != move)
			{
				remote->EndSpecialMove(remote->SpecialMove);
			}
			const bool started = remote->eventDoSpecialMove(move, true, nullptr, 0);
			Log("Remote special move " + std::to_string(state.specialMove)
				+ (started ? " started." : " was rejected."));
		}

		USkeletalMeshComponent* ResolveRemoteAnimationComponent(
			AAlicePawn* remote, AnimationComponent component)
		{
			if (!remote)
				return nullptr;
			switch (component)
			{
			case AnimationComponent::Body:
				return remote->Mesh;
			case AnimationComponent::UpperBody:
				return remote->UpperBodyComponent;
			case AnimationComponent::Weapon:
				if (remote->Weapon && remote->Weapon->Mesh)
					return remote->Weapon->Mesh;
				if (remote->DummyWeapon && remote->DummyWeapon->Mesh)
					return remote->DummyWeapon->Mesh;
				return nullptr;
			default:
				return nullptr;
			}
		}

		std::uint32_t AnimationNodeKey(
			AnimationComponent component, std::uint16_t nodeIndex)
		{
			return (static_cast<std::uint32_t>(component) << 16)
				| nodeIndex;
		}

		bool IsSafeFullBodySequence(
			const AnimationSequencePayload& state)
		{
			if (state.component != AnimationComponent::Body
				|| !state.sequenceName[0])
			{
				return false;
			}

			// The body slot can only consume regular Alice sequences. Additive
			// poses and weapon-only sequences need their own base pose, bone
			// mask, attachment and AnimSet; feeding them to the full-body slot
			// produces ref poses or severely stretched limbs.
			return NativeAnimationSlot(state.flags) <= 0
				&& _strnicmp(state.sequenceName, "Alice", 5) == 0;
		}

		bool RejectGroundedAirSequence(
			const AnimationSequencePayload& state)
		{
			if (!g_remotePresentation.valid || !state.sequenceName[0])
				return false;
			const PlayerStatePayload& source = g_remotePresentation.state;
			const EPhysics physics = static_cast<EPhysics>(source.physics);
			const EJumpStatus jump = static_cast<EJumpStatus>(source.jumpStatus);
			const bool airbornePhysics = physics == EPhysics::PHYS_Falling
				|| physics == EPhysics::PHYS_Flying
				|| physics == EPhysics::PHYS_Float
				|| physics == EPhysics::PHYS_JumpPad
				|| physics == EPhysics::PHYS_SteamVent;
			const bool groundedPhysics = physics == EPhysics::PHYS_Walking
				|| physics == EPhysics::PHYS_NavMeshWalking
				|| physics == EPhysics::PHYS_Spider
				|| physics == EPhysics::PHYS_Ladder
				|| physics == EPhysics::PHYS_Slide
				|| physics == EPhysics::PHYS_TrackSlide;
			// CurrentJumpStatus can remain Fall/Rise for several seconds after
			// Physics has already returned to Walking. In that state Alice's own
			// AnimTree fades the air branch out, while the proxy used to restart
			// its full-body fall clip at full weight. Explicit grounded physics is
			// authoritative over that stale status.
			const bool airborne = airbornePhysics
				|| (!groundedPhysics
					&& (jump == EJumpStatus::EMT_Jump
				|| jump == EJumpStatus::EMT_Rise
				|| jump == EJumpStatus::EMT_Fall));
			if (airborne)
				return false;
			const std::string name(state.sequenceName);
			const bool airPose = ContainsCaseInsensitive(name, "float")
				|| ContainsCaseInsensitive(name, "glide")
				|| ContainsCaseInsensitive(name, "fly")
				|| ContainsCaseInsensitive(name, "jump")
				|| ContainsCaseInsensitive(name, "fall");
			if (airPose)
				return true;
			const float horizontalSpeedSquared =
				source.velocity[0] * source.velocity[0]
				+ source.velocity[1] * source.velocity[1];
			return horizontalSpeedSquared > 25.0f
				&& ContainsCaseInsensitive(name, "land");
		}

		bool IsSafeStandalonePresentationSequence(
			const AnimationSequencePayload& state)
		{
			if (!IsSafeFullBodySequence(state))
				return false;
			const std::string name(state.sequenceName);
			return ContainsCaseInsensitive(name, "float")
				|| ContainsCaseInsensitive(name, "glide")
				|| ContainsCaseInsensitive(name, "shrink")
				|| ContainsCaseInsensitive(name, "fly");
		}

		bool IsSafeUpperAdditiveSequence(const AnimationSequencePayload& state)
		{
			return state.component == AnimationComponent::Body && NativeAnimationSlot(state.flags) == 3
				&& state.sequenceName[0];
		}

		bool IsSafeHalfBodySequence(const AnimationSequencePayload& state)
		{
			return state.component == AnimationComponent::Body && NativeAnimationSlot(state.flags) == 1
				&& _strnicmp(state.sequenceName, "Alice", 5) == 0;
		}

		int32_t FindAnimationNodeIndex(
			const USkeletalMeshComponent* component, const UAnimNode* wanted);

		UAliceGameAnimNode_BlendBySlot* ResolveRemoteConfigSlot(
			AAliceGamePawn* remote, EAnimBlendNodeIndex configIndex,
			int32_t* outNodeIndex = nullptr)
		{
			if (outNodeIndex)
				*outNodeIndex = -1;
			if (!remote || !remote->Mesh)
				return nullptr;
			const int32_t index = static_cast<int32_t>(configIndex);
			if (index < 0 || index >= remote->AnimBlendNodes.size())
				return nullptr;
			UAnimNode* node = remote->AnimBlendNodes.at(index);
			if (!node
				|| !node->IsA(
					UAliceGameAnimNode_BlendBySlot::StaticClass()))
			{
				return nullptr;
			}
			if (outNodeIndex)
				*outNodeIndex = FindAnimationNodeIndex(
					remote->Mesh, node);
			return reinterpret_cast<
				UAliceGameAnimNode_BlendBySlot*>(node);
		}

		UAliceGameAnimNode_BlendBySlot* ResolveRemoteFullBodySlot(
			AAlicePawn* remote, int32_t* outNodeIndex = nullptr)
		{
			if (outNodeIndex)
				*outNodeIndex = -1;
			if (!remote || !remote->Mesh)
				return nullptr;

			USkeletalMeshComponent* component = remote->Mesh;
			const int32_t nodeCount = std::min<int32_t>(
				component->AnimTickArray.size(), 65535);
			for (int32_t index = 0; index < nodeCount; ++index)
			{
				UAnimNode* node = component->AnimTickArray.at(index);
				if (!node
					|| !node->IsA(
						UAliceGameAnimNode_BlendBySlot::StaticClass()))
				{
					continue;
				}
				const std::string nodeName = node->NodeName.IsValid()
					? node->NodeName.ToString() : std::string();
				if (!ContainsCaseInsensitive(nodeName, "fullbody"))
					continue;
				if (outNodeIndex)
					*outNodeIndex = index;
				return reinterpret_cast<
					UAliceGameAnimNode_BlendBySlot*>(node);
			}
			return nullptr;
		}

		int32_t FindAnimationNodeIndex(
			const USkeletalMeshComponent* component, const UAnimNode* wanted)
		{
			if (!component || !wanted)
				return -1;
			const int32_t nodeCount = std::min<int32_t>(
				component->AnimTickArray.size(), 65535);
			for (int32_t index = 0; index < nodeCount; ++index)
				if (component->AnimTickArray.at(index) == wanted)
					return index;
			return -1;
		}

		const AnimationSequencePayload* FindSafeSequenceForSlot(
			const AnimationGraphPayload& graph, const USkeletalMeshComponent*,
			const UAliceGameAnimNode_BlendBySlot*, int32_t)
		{
			for (std::size_t index = 0; index < (std::min<std::size_t>)(graph.sequenceCount, MaxAnimationSequences); ++index)
			{
				const auto& state = graph.sequences[index];
				if (NativeAnimationSlot(state.flags) == 0 && (state.flags & AnimationSequenceSlotActive)
					&& IsSafeFullBodySequence(state) && !RejectGroundedAirSequence(state)) return &state;
			}
			return nullptr;
		}

		const AnimationSequencePayload* FindUpperSequenceForSlot(
			const AnimationGraphPayload& graph, EAnimBlendNodeIndex configIndex)
		{
			for (std::size_t index = 0; index < (std::min<std::size_t>)(graph.sequenceCount, MaxAnimationSequences); ++index)
			{
				const auto& state = graph.sequences[index];
				if (state.component == AnimationComponent::Body
					&& NativeAnimationSlot(state.flags) == static_cast<int>(configIndex)
					&& (state.flags & AnimationSequenceSlotActive)) return &state;
			}
			return nullptr;
		}

		const AnimationSequencePayload* FindStandalonePresentationSequence(
			const AnimationGraphPayload& graph,
			const std::string& preferredSequence)
		{
			const AnimationSequencePayload* best = nullptr;
			const AnimationSequencePayload* preferred = nullptr;
			const std::size_t sequenceCount = std::min<std::size_t>(
				graph.sequenceCount, MaxAnimationSequences);
			for (std::size_t index = 0; index < sequenceCount; ++index)
			{
				const AnimationSequencePayload& state =
					graph.sequences[index];
				if (!IsSafeStandalonePresentationSequence(state))
					continue;
				const std::string name(state.sequenceName);
				const bool airborneSequence =
					ContainsCaseInsensitive(name, "float")
					|| ContainsCaseInsensitive(name, "glide")
					|| ContainsCaseInsensitive(name, "fly");
				if (airborneSequence && g_remotePresentation.valid)
				{
					const EPhysics physics = static_cast<EPhysics>(
						g_remotePresentation.state.physics);
					const bool sourceAirborne =
						physics == EPhysics::PHYS_Falling
						|| physics == EPhysics::PHYS_Flying
						|| physics == EPhysics::PHYS_Float
						|| physics == EPhysics::PHYS_JumpPad
						|| physics == EPhysics::PHYS_SteamVent;
					if (!sourceAirborne)
						continue;
				}
				const bool matchesPreferred = !preferredSequence.empty()
					&& _stricmp(state.sequenceName,
						preferredSequence.c_str()) == 0;
				// A fading Float_Idle can briefly survive the landing slot with a
				// tiny weight. Starting it as a fresh standalone animation after the
				// landing is what produced the one-leg hover while walking.
				const float minimumWeight = matchesPreferred ? 0.025f : 0.10f;
				if (!std::isfinite(state.nodeWeight)
					|| state.nodeWeight < minimumWeight)
				{
					continue;
				}
				if (!best || state.nodeWeight > best->nodeWeight)
					best = &state;
				if (matchesPreferred)
				{
					preferred = &state;
				}
			}
			// Directional float nodes often trade a few hundredths of weight
			// every packet. Keep the currently presented direction until a
			// replacement is clearly stronger instead of restarting the
			// full-body slot at the network cadence.
			if (preferred && best
				&& preferred->nodeWeight + 0.20f >= best->nodeWeight)
			{
				return preferred;
			}
			return best;
		}

		float PresentationBlendTime(const char* sequenceName)
		{
			const std::string name = sequenceName
				? sequenceName : std::string();
			if (ContainsCaseInsensitive(name, "landlow"))
				return 0.025f;
			if (ContainsCaseInsensitive(name, "land"))
				return 0.040f;
			if (ContainsCaseInsensitive(name, "dodge"))
				return 0.030f;
			if (ContainsCaseInsensitive(name, "attack")
				|| ContainsCaseInsensitive(name, "mele"))
			{
				return 0.060f;
			}
			if (ContainsCaseInsensitive(name, "float")
				|| ContainsCaseInsensitive(name, "glide")
				|| ContainsCaseInsensitive(name, "fly"))
			{
				return 0.100f;
			}
			return 0.080f;
		}

		float PresentationBlendTime(
			const AnimationSequencePayload& state)
		{
			const float base = PresentationBlendTime(state.sequenceName);
			const std::string name = state.sequenceName;
			if (!ContainsCaseInsensitive(name, "attack")
				&& !ContainsCaseInsensitive(name, "mele"))
			{
				return base;
			}
			// Combo clips overlap on the owner: a new segment can become the
			// active slot child while its source weight is still only 0.05-0.20.
			// A fixed 60 ms blend made that authored cross-fade look blocky.
			const float weight = std::isfinite(state.nodeWeight)
				? std::clamp(state.nodeWeight, 0.0f, 1.0f) : 1.0f;
			if (weight < 0.20f)
				return 0.140f;
			if (weight < 0.50f)
				return 0.100f;
			return base;
		}

		void ApplyPresentationRootOptions(UAnimNodeSequence* sequence, std::uint16_t options)
		{
			if (!sequence) return;
			for (int axis = 0; axis < 3; ++axis)
			{
				sequence->RootBoneOption[axis] = static_cast<ERootBoneAxis>(AnimationRootAxisOption(options, axis, false));
				sequence->RootRotationOption[axis] = static_cast<ERootRotationOption>(AnimationRootAxisOption(options, axis, true));
			}
			sequence->bZeroRootTranslation = (options & 0x1000) != 0;
			sequence->bZeroRootRotation = (options & 0x2000) != 0;
		}

		bool StartPresentationConfigAnimation(AAliceGamePawn* remote,
			PresentationAnimChannel& channel, int32_t blendNodeIndex,
			const AnimationSequencePayload& state, bool standalone)
		{
			if (!remote || !state.sequenceName[0])
				return false;
			FName sequenceName;
			if (!ResolveExistingAnimationName(remote->Mesh,
				state.sequenceName, sequenceName))
				return false;
			if (!channel.configInitialized)
			{
				channel.config.AnimationNames.push_back(sequenceName);
				channel.configInitialized = true;
			}
			else if (channel.config.AnimationNames.size() > 0)
			{
				channel.config.AnimationNames.at(0) = sequenceName;
			}
			else
			{
				channel.config.AnimationNames.push_back(sequenceName);
			}

			const bool looping =
				(state.flags & AnimationSequenceLooping) != 0;
			const float rate = std::isfinite(state.rate) ? state.rate : 1.0f;
			float blendTime = PresentationBlendTime(state);
			float blendOutTime = blendTime;
			if (g_activeRemoteAnimationGraph)
			{
				const auto& graph = g_activeRemoteAnimationGraph->graph;
				float wireIn = 0.0f, wireOut = 0.0f;
				if (blendNodeIndex == 0) { wireIn = graph.fullBodyBlendTicks * 0.002f; wireOut = graph.fullBodyBlendOutTicks * 0.002f; }
				if (blendNodeIndex == 1) { wireIn = graph.halfBodyBlendTicks * 0.002f; wireOut = graph.halfBodyBlendOutTicks * 0.002f; }
				if (blendNodeIndex == 3) { wireIn = graph.upperBlendTicks * 0.002f; wireOut = graph.upperBlendOutTicks * 0.002f; }
				const bool authored = (graph.authoredBlendMask & (1u << blendNodeIndex)) != 0;
				bool nativeBlendOut = false;
				if (blendNodeIndex == 0)
					for (std::size_t i = 0; i < (std::min<std::size_t>)(graph.blendCount, MaxAnimationBlends); ++i)
						if (graph.blends[i].component == AnimationComponent::Body
							&& NativeAnimationSlot(graph.blends[i].flags) == 0
							&& (graph.blends[i].flags & AnimationBlendNativeSlotTiming))
							{
								nativeBlendOut = true;
								wireOut = NativePendingBlendOut(graph.blends[i].flags, graph.fullBodyBlendOutTicks);
								break;
							}
				// A native zero blend-in is valid; it must not pick the melee fallback.
				if (nativeBlendOut || authored || wireIn >= 0.04f) blendTime = wireIn;
				if (nativeBlendOut || authored || wireOut >= 0.04f) blendOutTime = wireOut;
			}
			const bool captureDoubleJumpParticle =
				ContainsCaseInsensitive(state.sequenceName, "jumpd_")
				&& (ContainsCaseInsensitive(state.sequenceName, "_rise")
					|| ContainsCaseInsensitive(state.sequenceName, "_start"));
			if (captureDoubleJumpParticle)
				BeginRemoteMovementParticleCapture(remote, state.sequenceName);
			channel.config.BlendNodeIndex =
				static_cast<std::uint8_t>(blendNodeIndex);
			channel.config.AnimType =
				static_cast<int32_t>(EAnimConfigType::EACT_Arbitrary);
			channel.config.BlendInTime = blendTime;
			channel.config.BlendOutTime = blendOutTime;
			const AnimationAssetLookup asset = FindAnimationAsset(remote->Mesh, state.sequenceName);
			channel.config.PlayRate = NormalizeAnimationRate(rate, remote->Mesh->GlobalAnimRateScale,
				asset.sequence ? asset.sequence->RateScale : 1.0f);
			channel.config.bLoop = looping;
			channel.config.bCauseActorAnimEnd = false;
			channel.config.bTriggerFakeRootMotion = false;
			channel.config.bNotExtendAnimTimeForFakeRootMotion = true;
			channel.config.AnimPlayType = static_cast<std::uint8_t>(
				EConfigAnimPlayType::ECAPT_OneByOne);
			channel.config.FakeRootMotionMode = static_cast<std::uint8_t>(ERootMotionMode::RMM_Ignore);
			for (int axis = 0; axis < 3; ++axis)
			{
				channel.config.RootBoneTransitionOption[axis] = AnimationRootAxisOption(state.rootBoneOptions, axis, false);
				channel.config.RootBoneRotationOption[axis] = AnimationRootAxisOption(state.rootBoneOptions, axis, true);
			}

			const bool invoked = NativePlayConfigAnim(remote,
				blendNodeIndex,
				static_cast<int32_t>(EAnimConfigType::EACT_Arbitrary),
				channel.config);
			if (!invoked)
				return false;

			channel.active = true;
			channel.standalone = standalone;
			channel.blendNodeIndex = blendNodeIndex;
			channel.sequenceName = state.sequenceName;
			channel.lastSourceSeen = Clock::now();
			if (g_config.animationLifecycleTrace)
			{
				Log("ANIMLIFE start sequence="
					+ std::string(state.sequenceName)
					+ ", channel=" + std::to_string(blendNodeIndex)
					+ ", standalone="
					+ (standalone ? "yes." : "no."));
			}
			return true;
		}

		bool StopPresentationConfigAnimation(AAliceGamePawn* remote,
			PresentationAnimChannel& channel, float blendOutTime)
		{
			if (!channel.active)
				return false;
			const std::string stoppedSequence = channel.sequenceName;
			const bool invoked = remote && channel.configInitialized
				? NativeStopConfigAnim(remote, blendOutTime, channel.config)
				: false;
			ResetPresentationAnimChannel(channel);
			if (g_config.animationLifecycleTrace)
			{
				Log("ANIMLIFE stop sequence=" + stoppedSequence
					+ ", native=" + (invoked ? "yes." : "no; local reset."));
			}
			return invoked;
		}

		enum class PresentationSequenceKind
		{
			FullBody,
			UpperAdditive,
			UpperAbsolute,
		};

		bool ApplySafeSlotSequence(AAlicePawn* remote,
			UAliceGameAnimNode_BlendBySlot* slot, int32_t targetChild,
			UAnimNodeSequence* sequence, const AnimationSequencePayload& state,
			bool synchronizePosition, bool forcePosition,
			PresentationSequenceKind kind)
		{
			const bool safe = kind == PresentationSequenceKind::FullBody
				? IsSafeFullBodySequence(state)
				: (kind == PresentationSequenceKind::UpperAdditive
					? IsSafeUpperAdditiveSequence(state) : IsSafeHalfBodySequence(state));
			if (!sequence || !safe)
				return false;

			const std::string oldSequenceName =
				sequence->AnimSeqName.IsValid()
				? sequence->AnimSeqName.ToString() : "<invalid>";
			UAnimSequence* const oldAnimSequence = sequence->AnimSeq;
			auto logFailure = [&](const char* reason)
			{
				const char* stage =
					kind == PresentationSequenceKind::FullBody
					? "ANIMSTAGE" : "UPPERSTAGE";
				const std::string key = std::string(stage) + ':'
					+ reason + ':' + state.sequenceName;
				if (!g_loggedAnimationStageFailures.insert(key).second)
					return;
				AAlicePawn* local = GetLocalPawn();
				std::ostringstream stream;
				stream << stage << " reject reason=" << reason
					<< ", requested=" << state.sequenceName
					<< ", rootOptions=" << state.rootBoneOptions
					<< ", targetChild=" << targetChild
					<< ", slotActive="
					<< (slot ? slot->ActiveChildIndex : -1)
					<< ", targetNode=" << ObjectName(sequence)
					<< ", nodeSkel=" << ObjectName(sequence->SkelComponent)
					<< ", expectedSkel="
					<< ObjectName(remote ? remote->Mesh : nullptr)
					<< ", oldName=" << oldSequenceName
					<< ", oldAnimSeq=" << ObjectName(oldAnimSequence)
					<< ", newName="
					<< (sequence->AnimSeqName.IsValid()
						? sequence->AnimSeqName.ToString() : "<invalid>")
					<< ", newAnimSeq=" << ObjectName(sequence->AnimSeq)
					<< "; remote{" << DescribeAnimationAssets(
						remote ? remote->Mesh : nullptr,
						state.sequenceName)
					<< "}; local{" << DescribeAnimationAssets(
						local ? local->Mesh : nullptr,
						state.sequenceName) << "}.";
				Log(stream.str());
			};

			FName sequenceName;
			if (!ResolveExistingAnimationName(
				remote ? remote->Mesh : nullptr,
				state.sequenceName, sequenceName))
			{
				logFailure("invalid-fname");
				return false;
			}
			const bool nameMatches = sequence->AnimSeqName.IsValid()
				&& _stricmp(sequence->AnimSeqName.ToString().c_str(),
					state.sequenceName) == 0;
			const bool looping =
				(state.flags & AnimationSequenceLooping) != 0;
			const float rate = std::isfinite(state.rate) ? state.rate : 1.0f;
			const float sourcePosition = std::isfinite(state.position)
				&& state.position >= 0.0f
				? state.position : 0.0f;

			sequence->bNoNotifies = true;
			sequence->bCauseActorAnimEnd = false;
			sequence->bCauseActorAnimPlay = false;
			sequence->bForceRefposeWhenNotPlaying = false;
			ApplyPresentationRootOptions(sequence, state.rootBoneOptions);
			if (!nameMatches)
				NativeSetAnim(sequence, sequenceName);
			if (!sequence->AnimSeq)
			{
				logFailure("animseq-null-after-setanim");
				return false;
			}
			if (kind == PresentationSequenceKind::UpperAdditive && !sequence->AnimSeq->bIsAdditive)
			{
				logFailure("absolute-asset-in-additive-slot");
				return false;
			}
			if (kind == PresentationSequenceKind::UpperAbsolute && sequence->AnimSeq->bIsAdditive)
			{
				logFailure("additive-asset-in-absolute-slot");
				return false;
			}
			const float sequenceLength = sequence->AnimSeq->SequenceLength;
			float position = sourcePosition + RemoteAnimationSampleAge() * rate;
			if (looping && sequenceLength > 0.001f)
				position = std::fmod(position, sequenceLength);
			const bool endpoint = !looping && sequenceLength > 0.001f
				&& position >= sequenceLength - 0.001f;
			if (!looping && sequenceLength > 0.001f)
				position = std::clamp(position, 0.0f, sequenceLength - 0.001f);
			const bool sourceStopped = (state.rootBoneOptions & AnimationPoseStopped) != 0;
			const auto correction = CorrectAnimationPhase(sequence->CurrentTime, position,
				sequenceLength, looping, endpoint ? 0.0f : rate);
			const float presentationRate = NativePlaybackRate(sequence, correction.rate);
			if (!sourceStopped && (!nameMatches || (!sequence->bPlaying && synchronizePosition)))
				NativePlayAnim(sequence, looping, presentationRate, position);
			sequence->Rate = presentationRate;
			sequence->bLooping = looping;
			sequence->bPlaying = !sourceStopped;
			if (sourceStopped || forcePosition || !nameMatches || endpoint || (synchronizePosition && correction.snap))
			{
				NativeSetPosition(sequence, position);
				sequence->CurrentTime = position;
				sequence->PreviousTime = position;
			}

			if (kind == PresentationSequenceKind::FullBody)
			{
				g_remoteAppliedFullBodyNode = sequence;
				g_remoteAppliedFullBodySlot = slot;
				g_remoteAppliedFullBodyName = state.sequenceName;
			}
			if (kind == PresentationSequenceKind::UpperAdditive) g_remoteAppliedUpperBodyNode = sequence;
			if (kind == PresentationSequenceKind::UpperAbsolute) g_remoteAppliedHalfBodyNode = sequence;
			const char* stage =
				kind == PresentationSequenceKind::FullBody
				? "ANIMSTAGE" : "UPPERSTAGE";
			const std::string successKey = std::string(stage) + ':'
				+ state.sequenceName;
			if (g_loggedAnimationStageSuccesses.insert(
				successKey).second)
			{
				std::ostringstream stream;
				stream << stage << " accept requested="
					<< state.sequenceName
					<< ", rootOptions=" << state.rootBoneOptions
					<< ", targetChild=" << targetChild
					<< ", targetNode=" << ObjectName(sequence)
					<< ", nodeSkel=" << ObjectName(sequence->SkelComponent)
					<< ", animSeq=" << ObjectName(sequence->AnimSeq)
					<< ", time=" << sequence->CurrentTime
					<< ", length="
					<< (sequence->AnimSeq
						? sequence->AnimSeq->SequenceLength : -1.0f)
					<< ", rate=" << sequence->Rate
					<< ", looping=" << sequence->bLooping << '.';
				Log(stream.str());
			}
			return true;
		}

		void ApplyRemotePawnStance(AAlicePawn* remote, const PlayerStatePayload& state)
		{
			const auto stance = static_cast<EBasePawnStance>(UnpackPawnStance(state.flags));
			if (remote->PawnStance == stance) return;
			remote->PrevPawnStance = remote->PawnStance;
			remote->PawnStance = stance;
			// BlendByStance's native TickAnim reads PawnStance and applies its
			// authored BlendTimes. Calling SetPawnStance would run gameplay script.
			if (g_loggedConfigAnimationStages.insert("pawn-stance:" + std::to_string(static_cast<int>(stance))).second)
				Log("ANIMSTAGE native source PawnStance=" + std::to_string(static_cast<int>(stance)) + '.');
		}

		void EnableRemoteAnimationTick(AAlicePawn* remote)
		{
			if (!remote)
				return;
			auto enable = [remote](USkeletalMeshComponent* component)
			{
				if (!component || !component->Animations)
					return;
				// Extract the same root pose as the owner, but network coordinates
				// already contain its movement. Native Ignore suppresses actor motion.
				component->RootMotionMode = ERootMotionMode::RMM_Ignore;
				component->PreviousRMM = ERootMotionMode::RMM_Ignore;
				component->PendingRMM = ERootMotionMode::RMM_Ignore;
				component->OldPendingRMM = ERootMotionMode::RMM_Ignore;
				component->FakeRootMotionMode = ERootMotionMode::RMM_Ignore;
				component->RootMotionRotationMode = ERootMotionRotationMode::RMRM_Ignore;
				component->bPauseAnims = false;
				component->bNoSkeletonUpdate = false;
				component->bUpdateSkelWhenNotRendered = true;
				component->bTickAnimNodesWhenNotRendered = true;
				const int32_t nodeCount = std::min<int32_t>(
					component->AnimTickArray.size(), 65535);
				for (int32_t index = 0;
					index < nodeCount; ++index)
				{
					UAnimNode* node =
						component->AnimTickArray.at(index);
					if (node && node->SkelComponent == component
						&& node->IsA(UAliceAnimNode_AimOffset::StaticClass()))
					{
						auto* aim = reinterpret_cast<UAliceAnimNode_AimOffset*>(node);
						if (aim->AlicePawnOwner != remote)
						{
							aim->AlicePawnOwner = remote;
							if (g_loggedConfigAnimationStages.insert("native-aim-owner").second)
								Log("UPPERSTAGE native AimOffset owner bound to visual proxy.");
						}
					}
					if (!node
						|| !node->IsA(
							UAnimNodeSequence::StaticClass()))
					{
						continue;
					}
					auto* sequence =
						reinterpret_cast<UAnimNodeSequence*>(node);
					sequence->bNoNotifies = true;
					sequence->bCauseActorAnimEnd = false;
					sequence->bCauseActorAnimPlay = false;
				}
			};
			enable(remote->Mesh);
			enable(remote->UpperBodyComponent);
			if (remote->SkirtComponent)
				enable(remote->SkirtComponent);
			if (remote->BowComponent)
				enable(remote->BowComponent);
			if (remote->RibbonComponent)
				enable(remote->RibbonComponent);
			if (remote->EarComponent)
				enable(remote->EarComponent);
			if (remote->Weapon)
				enable(remote->Weapon->Mesh);
			else if (remote->DummyWeapon)
				enable(remote->DummyWeapon->Mesh);
		}

		void ApplyRemoteWeaponTypeBlends(AAlicePawn* remote,
			const AnimationGraphPayload& graph)
		{
			if (!remote || !remote->Mesh)
				return;
			(void)graph;
			const int32_t fallbackWeaponType =
				remote->Weapon
					&& remote->Weapon->IsA(
						AWeaponForAlice::StaticClass())
				? static_cast<int32_t>(
					reinterpret_cast<AWeaponForAlice*>(
						remote->Weapon)->WeaponTypeEnum)
				: g_remotePresentation.valid
				? static_cast<int32_t>(
					g_remotePresentation.state.weaponType)
				: 0;
			int appliedCount = 0;
			const int32_t nodeCount = std::min<int32_t>(
				remote->Mesh->AnimTickArray.size(), 65535);
			for (int32_t nodeIndex = 0;
				nodeIndex < nodeCount; ++nodeIndex)
			{
				UAnimNode* node =
					remote->Mesh->AnimTickArray.at(nodeIndex);
				if (!node
					|| !node->IsA(
						UAliceGameAnimNode_BlendByAliceWeaponType::
							StaticClass()))
				{
					continue;
				}
				auto* weaponBlend = reinterpret_cast<
					UAliceGameAnimNode_BlendByAliceWeaponType*>(node);
				// AnimTickArray indices are local to a skeletal component. Dynamic
				// full-body slot children make the owner's index unsuitable for the
				// cloned pawn, especially after an air transition. The replicated
				// weapon type is the authoritative selector for every such branch.
				int32_t targetChild = fallbackWeaponType;
				if (targetChild < 0
					|| targetChild >= weaponBlend->Children.size())
				{
					continue;
				}
				if (weaponBlend->ActiveChildIndex != targetChild)
					NativeSetActiveChild(weaponBlend, targetChild, 0.08f);
				++appliedCount;
			}
			if (appliedCount > 0)
			{
				const std::string key = "weapon-type:"
					+ std::to_string(fallbackWeaponType);
				if (g_loggedConfigAnimationStages.insert(key).second)
				{
					Log("UPPERSTAGE weapon-type branches="
						+ std::to_string(appliedCount)
						+ ", fallbackType="
						+ std::to_string(fallbackWeaponType) + '.');
				}
			}
		}

		void ApplyNativeUpperLowerBlend(AAlicePawn* remote, const AnimationGraphPayload& graph)
		{
			const int index = static_cast<int>(EAnimBlendNodeIndex::EABLIdx_PerBone_BlendUpperLower_Main);
			if (!(graph.authoredBlendMask & 0x80) || index >= remote->AnimBlendNodes.size()) return;
			auto* node = remote->AnimBlendNodes.at(index);
			if (!node || !node->IsA(UAliceGameAnimNode_BlendPerBone::StaticClass())) return;
			auto* blend = reinterpret_cast<UAliceGameAnimNode_BlendPerBone*>(node);
			const float weight = graph.upperLowerWeight / 65535.0f;
			if (std::fabs(blend->Child2WeightTarget - weight) <= 0.001f) return;
			UAliceGameAnimNode_BlendTwo_execSetBlendTarget_Params params{};
			params.BlendTarget = weight;
			params.BlendTime = (std::max)(0.0f, graph.upperLowerBlendTicks * 0.002f - RemoteAnimationSampleAge());
			InvokeNativeFunction(blend, UFunction::FindFunction("Function AliceGame.AliceGameAnimNode_BlendTwo.SetBlendTarget"), params);
		}

		bool ApplyRemoteUpperGraph(AAlicePawn* remote,
			const AnimationGraphPayload& graph, bool synchronizePosition,
			EAnimBlendNodeIndex nativeSlotIndex, PresentationAnimChannel& channel)
		{
			int32_t slotNodeIndex = -1;
			UAliceGameAnimNode_BlendBySlot* slot =
				ResolveRemoteConfigSlot(remote,
					nativeSlotIndex,
					&slotNodeIndex);
			if (!slot)
			{
				if (g_loggedConfigAnimationStages.insert(
					"upper-slot-missing").second)
				{
					Log("UPPERSTAGE combat upper additive slot is missing.");
				}
				return false;
			}

			const std::size_t blendCount = std::min<std::size_t>(
				graph.blendCount, MaxAnimationBlends);
			const AnimationBlendPayload* sourceSlot = nullptr;
			for (std::size_t index = 0; index < blendCount; ++index)
			{
				const AnimationBlendPayload& state = graph.blends[index];
				if (state.component == AnimationComponent::Body
					&& NativeAnimationSlot(state.flags) == static_cast<int>(nativeSlotIndex))
				{
					sourceSlot = &state;
					break;
				}
			}

			int32_t sourceChild = 0;
			const AnimationSequencePayload* sourceSequence = nullptr;
			if (sourceSlot
				&& (sourceSlot->flags & AnimationBlendCustom) != 0
				&& sourceSlot->activeChildIndex > 0
				&& sourceSlot->activeChildIndex
					< slot->Children.size())
			{
				sourceChild = sourceSlot->activeChildIndex;
				sourceSequence =
					FindUpperSequenceForSlot(graph, nativeSlotIndex);
			}

			bool applied = false;
			bool configStarted = false;
			if (sourceSequence)
			{
				if (synchronizePosition)
				channel.lastSourceSeen =
					Clock::now();
				const int32_t configIndex = static_cast<int32_t>(nativeSlotIndex);
				const bool selectionChanged =
					!channel.active
					|| channel.blendNodeIndex
						!= configIndex
					|| _stricmp(
						channel.sequenceName.c_str(),
						sourceSequence->sequenceName) != 0
					|| channel.targetChild <= 0
					|| channel.sourceChild != sourceChild
					|| (synchronizePosition && !(sourceSequence->flags & AnimationSequenceLooping)
						&& sourceSequence->position + 0.05f < channel.lastSourcePosition);
				int32_t targetChild =
					channel.targetChild;
				if (selectionChanged)
				{
					configStarted = StartPresentationConfigAnimation(
						remote, channel,
						configIndex, *sourceSequence, false);
					targetChild = configStarted
						? slot->ActiveChildIndex : sourceChild;
				}
				if (targetChild <= 0
					|| targetChild >= slot->Children.size())
				{
					targetChild = sourceChild;
				}
				UAnimNode* childNode =
					slot->Children.at(targetChild).Anim;
				if (childNode
					&& childNode->IsA(
						UAnimNodeSequence::StaticClass()))
				{
					auto* targetSequence =
						reinterpret_cast<UAnimNodeSequence*>(childNode);
					if (selectionChanged && slot->ActiveChildIndex != targetChild)
					{
						// SetActiveChild can reset the dynamic sequence clock. Select
						// the child first, then restore the authoritative source time.
						NativeSetActiveChild(slot, targetChild,
							configStarted
								? channel.config.BlendInTime
								: PresentationBlendTime(
									sourceSequence->sequenceName));
					}
					applied = ApplySafeSlotSequence(remote, slot,
						targetChild, targetSequence, *sourceSequence,
						synchronizePosition, configStarted,
						nativeSlotIndex == EAnimBlendNodeIndex::EABLIdx_Slot_Combat_Upper_Additive
							? PresentationSequenceKind::UpperAdditive : PresentationSequenceKind::UpperAbsolute);
					if (applied)
					{
						if (slot->ActiveChildIndex == targetChild)
						{
							slot->bIsPlayingCustomAnim = true;
							slot->bPlayActiveChild = true;
						}
						channel.active = true;
						channel.blendNodeIndex =
							configIndex;
						channel.targetChild =
							targetChild;
						channel.sourceChild =
							sourceChild;
						channel.sequenceName =
							sourceSequence->sequenceName;
						channel.lastSourcePosition =
							sourceSequence->position;
						channel.lastSourceRate =
							sourceSequence->rate;
						if (synchronizePosition)
						{
							channel.lastSourceSeen =
								Clock::now();
						}
						if (configStarted)
						{
							const std::string key =
								std::string("upper:")
								+ sourceSequence->sequenceName;
							if (g_loggedConfigAnimationStages.insert(
								key).second)
							{
								std::ostringstream stream;
								stream
									<< "CONFIGANIM upper native requested="
									<< sourceSequence->sequenceName
									<< ", slotNode=" << slotNodeIndex
									<< ", sourceChild=" << sourceChild
									<< ", targetChild=" << targetChild
									<< ", dynamicIndex="
									<< slot->CurrentDynamicAnimIndex
									<< '.';
								Log(stream.str());
							}
						}
					}
				}
			}

			if (!applied && !sourceSequence
				&& (!sourceSlot || sourceSlot->activeChildIndex > 0)
				&& slot->ActiveChildIndex == channel.targetChild
				&& channel.active
				&& channel.targetChild > 0
				&& channel.targetChild
					< slot->Children.size()
				&& channel.lastSourceSeen
					!= Clock::time_point{}
				&& Clock::now()
					- channel.lastSourceSeen
					< std::chrono::milliseconds(140))
			{
				slot->bIsPlayingCustomAnim = true;
				slot->bPlayActiveChild = true;
				applied = true;
			}
			if (!applied)
			{
				StopPresentationConfigAnimation(
					remote, channel, channel.config.BlendOutTime);
				slot->bIsPlayingCustomAnim = false;
				slot->bPlayActiveChild = false;
				if (slot->ActiveChildIndex != 0)
					NativeSetActiveChild(slot, 0, 0.08f);
			}
			return applied;
		}

		const AnimationSequencePayload* FindRemoteWeaponSequence(
			const AnimationGraphPayload& graph)
		{
			const AnimationSequencePayload* best = nullptr;
			float bestScore = -1.0f;
			const std::size_t sequenceCount = std::min<std::size_t>(
				graph.sequenceCount, MaxAnimationSequences);
			for (std::size_t index = 0; index < sequenceCount; ++index)
			{
				const AnimationSequencePayload& state =
					graph.sequences[index];
				if (state.component != AnimationComponent::Weapon
					|| !state.sequenceName[0])
				{
					continue;
				}
				const float score =
					((state.flags & AnimationSequenceCustom) != 0
						? 4.0f : 0.0f)
					+ ((state.flags & AnimationSequenceAction) != 0
						? 2.0f : 0.0f)
					+ (std::isfinite(state.nodeWeight)
						? state.nodeWeight : 0.0f);
				if (!best || score > bestScore)
				{
					best = &state;
					bestScore = score;
				}
			}
			return best;
		}

		void SetRemoteMuzzleFlash(AWeaponForAlice* weapon, bool active)
		{
			if (!weapon)
				return;
			const Clock::time_point now = Clock::now();
			if (active)
			{
				g_remoteMuzzleLastActiveRequest = now;
			}
			else if (g_remoteMuzzleFlashActive
				&& g_remoteMuzzleLastActiveRequest
					!= Clock::time_point{}
				&& now - g_remoteMuzzleLastActiveRequest
					< std::chrono::milliseconds(300))
			{
				// Packet jitter can briefly expose the release/idle branch
				// between two fire graph samples. Keep one muzzle component
				// alive across that gap instead of spawning hundreds of pooled
				// PSCs during a long network session.
				return;
			}
			if (active == g_remoteMuzzleFlashActive)
				return;
			g_remoteMuzzleFlashActive = active;
			if (active)
			{
				if (g_remoteMuzzleParticle)
					RetirePersistentPresentationParticle(
						g_remoteMuzzleParticle);
				g_remoteMuzzleParticle = nullptr;
				UParticleSystem* muzzle =
					weapon->MuzzleFlashPSCTemplate;
				if (!muzzle && weapon->WeaponLevelData.size() > 0)
				{
					const int32_t level = std::clamp<int32_t>(
						weapon->WeaponLevel - 1, 0,
						weapon->WeaponLevelData.size() - 1);
					muzzle = weapon->WeaponLevelData.at(level)
						.WLDP_Particle_Muzzle;
				}
				if (!muzzle
					&& static_cast<std::uint8_t>(
						weapon->WeaponTypeEnum)
						== static_cast<std::uint8_t>(
							EAliceWeaponType::EAWT_EyeStaff))
				{
					const bool dlcMesh = weapon->Mesh
						&& ContainsCaseInsensitive(
							ObjectName(
								weapon->Mesh->SkeletalMesh),
							"dlc");
					muzzle = FindLoadedParticleSystem(
						{ "PepperGrinder", "Muzzle" },
						{ dlcMesh ? "DLC" : "Lv01" });
				}
				if (muzzle && weapon->Mesh && weapon->WorldInfo
					&& weapon->WorldInfo->MyEmitterPool)
				{
					if (g_remoteMuzzleParticle)
						RetirePersistentPresentationParticle(
							g_remoteMuzzleParticle);
					constexpr VfxAttachmentCandidate
						AuthoredMuzzleCandidate =
							VfxAttachmentCandidate::
								WeaponMuzzleSocket;
					g_remoteMuzzleParticle =
						SpawnWeaponParticleCandidate(
							weapon, muzzle,
							AuthoredMuzzleCandidate);
					MakePresentationParticleVisible(
						g_remoteMuzzleParticle);
					g_lastRemoteVfx = "muzzle: "
						+ ObjectName(muzzle)
						+ " @ "
						+ VfxAttachmentCandidateName(
							AuthoredMuzzleCandidate)
						+ (g_remoteMuzzleParticle
							? " [spawned]" : " [failed]");
					Log("VFXSTAGE muzzle direct template="
						+ ObjectName(muzzle)
						+ ", candidate="
						+ VfxAttachmentCandidateName(
							AuthoredMuzzleCandidate)
						+ ", muzzleSocket="
						+ (weapon->MuzzleFlashSocket.IsValid()
							? weapon->MuzzleFlashSocket.ToString()
							: std::string("<invalid>"))
						+ ", rangeSocket="
						+ (weapon->RangeAttackSocket.IsValid()
							? weapon->RangeAttackSocket.ToString()
							: std::string("<invalid>"))
						+ ", spawned="
						+ (g_remoteMuzzleParticle
							? "yes." : "no."));
				}
			}
			else
			{
				weapon->eventStopMuzzleFlash();
				if (weapon->MuzzleFlashLight)
					weapon->MuzzleFlashLight->SetEnabled(false);
				if (weapon->MuzzleFlashPSC)
					HardStopPresentationParticle(
						weapon->MuzzleFlashPSC, false);
				if (weapon->IsA(
					AWeaponForAliceRange::StaticClass()))
				{
					auto* range = reinterpret_cast<
						AWeaponForAliceRange*>(weapon);
					if (range->NormalMuzzleParticle)
						HardStopPresentationParticle(
							range->NormalMuzzleParticle,
							false);
				}
				if (g_remoteMuzzleParticle)
					RetirePersistentPresentationParticle(
						g_remoteMuzzleParticle);
				g_remoteMuzzleParticle = nullptr;
				if (weapon->IsA(AEyeStaff::StaticClass()))
				{
					auto* pepper =
						reinterpret_cast<AEyeStaff*>(weapon);
					if (pepper->MuzzleParticleActor)
					{
						AEmitter* emitter =
							pepper->MuzzleParticleActor;
						HardStopPresentationParticle(
							emitter->ParticleSystemComponent,
							false);
						emitter->bHidden = true;
						NativeSetActorHidden(emitter, true);
						emitter->SetTickIsDisabled(true);
						const FVector parked(
							emitter->Location.X,
							emitter->Location.Y,
							emitter->Location.Z
								- 100000.0f);
						emitter->SetLocationNoCheck(parked);
						emitter->Location = parked;
						pepper->MuzzleParticleActor = nullptr;
					}
				}
			}
			Log(std::string("VFXSTAGE remote muzzle=")
				+ (active ? "on." : "off."));
		}

		bool ApplyRemoteWeaponGraph(AAlicePawn* remote,
			const AnimationGraphPayload& graph, bool synchronizePosition)
		{
			if (!remote || !g_remotePresentationWeapon
				|| remote->Weapon != g_remotePresentationWeapon)
			{
				g_remoteAppliedWeaponNode = nullptr;
				return false;
			}
			AWeaponForAlice* weapon = g_remotePresentationWeapon;
			if (weapon->Mesh && remote->Mesh
				&& weapon->Mesh->AttachedToSkelComponent
					!= remote->Mesh)
			{
				const bool socketInvoked =
					NativeAttachComponentToSocket(
						remote->Mesh, weapon->Mesh,
						g_remotePresentationWeaponSocket);
				NativeForceComponentUpdate(weapon->Mesh, false);
				const std::string key =
					"weapon-socket-repair:"
					+ std::to_string(
						g_remotePresentationWeaponType);
				if (g_loggedConfigAnimationStages.insert(key).second)
				{
					Log("WEAPONSTAGE repaired binding: socketCall="
						+ std::string(socketInvoked
							? "invoked" : "failed")
						+ ", parent="
						+ ObjectName(
							weapon->Mesh
								->AttachedToSkelComponent)
						+ '.');
				}
			}
			const AnimationSequencePayload* source =
				FindRemoteWeaponSequence(graph);
			if (!source)
			{
				if (!g_remoteAppliedWeaponAnimation.empty()
					&& g_remoteWeaponAnimationLastSeen
						!= Clock::time_point{}
					&& Clock::now() - g_remoteWeaponAnimationLastSeen
						< std::chrono::milliseconds(180))
				{
					return true;
				}
				if (!g_remoteAppliedWeaponAnimation.empty())
				{
					NativeStopWeaponSlotAnim(weapon, 0.06f);
					g_remoteAppliedWeaponAnimation.clear();
					g_remoteAppliedWeaponNode = nullptr;
				}
				SetRemoteMuzzleFlash(weapon, false);
				return false;
			}

			if (synchronizePosition)
				g_remoteWeaponAnimationLastSeen = Clock::now();
			const bool changed =
				g_remoteAppliedWeaponAnimation.empty()
				|| _stricmp(
					g_remoteAppliedWeaponAnimation.c_str(),
					source->sequenceName) != 0;
			FName sequenceName;
			if (!ResolveExistingAnimationName(weapon->Mesh,
				source->sequenceName, sequenceName))
			{
				const std::string key =
					std::string("weapon-invalid:")
					+ source->sequenceName;
				if (g_loggedConfigAnimationStages.insert(key).second)
				{
					Log("WEAPONSTAGE missing animation asset="
						+ std::string(source->sequenceName)
						+ ", mesh="
						+ ObjectName(weapon->Mesh
							? weapon->Mesh->SkeletalMesh : nullptr)
						+ '.');
				}
				return false;
			}

			const bool looping =
				(source->flags & AnimationSequenceLooping) != 0;
			const float rate = std::isfinite(source->rate) ? source->rate : 1.0f;
			if (changed
				&& !NativePlayWeaponSlotAnim(weapon, sequenceName,
					rate, looping, 0.04f, 0.06f))
			{
				Log("WEAPONSTAGE native play failed for "
					+ std::string(source->sequenceName) + '.');
				return false;
			}

			bool matchedNode = false;
			if (weapon->Mesh)
			{
				const int32_t nodeCount = std::min<int32_t>(
					weapon->Mesh->AnimTickArray.size(), 1024);
				for (int32_t index = 0; index < nodeCount; ++index)
				{
					UAnimNode* node =
						weapon->Mesh->AnimTickArray.at(index);
					if (!node
						|| !node->IsA(
							UAnimNodeSequence::StaticClass()))
					{
						continue;
					}
					auto* sequence =
						reinterpret_cast<UAnimNodeSequence*>(node);
					// The proxy weapon is cosmetic. Suppress collision/damage
					// animation notifies and drive safe VFX explicitly below.
					sequence->bNoNotifies = true;
					sequence->bCauseActorAnimEnd = false;
					sequence->bCauseActorAnimPlay = false;
					if (!sequence->AnimSeqName.IsValid()
						|| _stricmp(
							sequence->AnimSeqName.ToString().c_str(),
							source->sequenceName) != 0)
					{
						continue;
					}
					matchedNode = true;
					ApplyPresentationRootOptions(sequence, source->rootBoneOptions);
					g_remoteAppliedWeaponNode = sequence;
					sequence->Rate = NativePlaybackRate(sequence, rate);
					sequence->bLooping = looping;
					float position = std::isfinite(source->position)
						? (std::max)(0.0f, source->position + RemoteAnimationSampleAge() * rate) : 0.0f;
					const float length = sequence->AnimSeq ? sequence->AnimSeq->SequenceLength : 0.0f;
					if (looping && length > 0.001f) position = std::fmod(position, length);
					if (!looping && length > 0.001f) position = (std::min)(position, length - 0.001f);
					const auto correction = CorrectAnimationPhase(sequence->CurrentTime, position, length, looping, rate);
					sequence->Rate = NativePlaybackRate(sequence, correction.rate);
					if (changed || (synchronizePosition && correction.snap)) NativeSetPosition(sequence, position);
				}
				NativeForceSkelUpdate(weapon->Mesh);
			}

			g_remoteAppliedWeaponAnimation = source->sequenceName;
			const std::string animationName(source->sequenceName);
			const bool firing =
				ContainsCaseInsensitive(animationName, "fire")
				&& !ContainsCaseInsensitive(
					animationName, "release");
			SetRemoteMuzzleFlash(weapon, firing);
			const std::string key =
				std::string("weapon:") + source->sequenceName;
			if (g_loggedConfigAnimationStages.insert(key).second)
			{
				Log("WEAPONSTAGE native requested="
					+ std::string(source->sequenceName)
					+ ", looping=" + (looping ? "true" : "false")
					+ ", node=" + (matchedNode ? "matched" : "pending")
					+ ", slot=" + ObjectName(weapon->SlotNode) + '.');
			}
			return true;
		}

		void ApplyRemoteFullBodyFadePoses(UAliceGameAnimNode_BlendBySlot* slot, const AnimationGraphPayload& graph, bool synchronizePosition)
		{
			// Update only children already owned and weighted by the native slot.
			// Starting another config here would re-enter its blend; weights/targets
			// and the native automatic blend-out remain entirely engine-owned.
			for (int child = 1; child < slot->Children.size(); ++child)
			{
				const auto& blendChild = slot->Children.at(child);
				if (child == slot->ActiveChildIndex || blendChild.Weight <= 0.001f || !blendChild.Anim
					|| !blendChild.Anim->IsA(UAnimNodeSequence::StaticClass())) continue;
				auto* sequence = reinterpret_cast<UAnimNodeSequence*>(blendChild.Anim);
				if (!sequence->AnimSeq || !sequence->AnimSeqName.IsValid()) continue;
				const auto name = sequence->AnimSeqName.ToString();
				const AnimationSequencePayload* source = nullptr;
				float closest = (std::numeric_limits<float>::max)();
				for (std::size_t i = 0; i < (std::min<std::size_t>)(graph.sequenceCount, MaxAnimationSequences); ++i)
				{
					const auto& candidate = graph.sequences[i];
					if (!IsSafeFullBodySequence(candidate) || NativeAnimationSlot(candidate.flags) != 0
						|| (candidate.flags & AnimationSequenceSlotActive)
						|| _stricmp(candidate.sequenceName, name.c_str()) != 0
						|| !std::isfinite(candidate.position) || !std::isfinite(candidate.rate)) continue;
					const float distance = std::fabs(candidate.position - sequence->CurrentTime);
					if (distance < closest) { source = &candidate; closest = distance; }
				}
				if (!source) continue;
				const bool stopped = (source->rootBoneOptions & AnimationPoseStopped) != 0;
				const bool looping = (source->flags & AnimationSequenceLooping) != 0;
				const float length = sequence->AnimSeq->SequenceLength;
				float position = (std::max)(0.0f, source->position + RemoteAnimationSampleAge() * source->rate);
				if (length > 0.001f)
					position = looping ? std::fmod(position, length) : std::clamp(position, 0.0f, length - 0.001f);
				const auto correction = CorrectAnimationPhase(sequence->CurrentTime, position, length, looping, source->rate);
				ApplyPresentationRootOptions(sequence, source->rootBoneOptions);
				sequence->bForceRefposeWhenNotPlaying = false;
				sequence->bNoNotifies = true;
				sequence->bCauseActorAnimEnd = false;
				sequence->bCauseActorAnimPlay = false;
				sequence->bLooping = looping;
				sequence->Rate = stopped ? 0.0f : NativePlaybackRate(sequence, correction.rate);
				sequence->bPlaying = !stopped;
				if (stopped || (synchronizePosition && correction.snap))
				{
					NativeSetPosition(sequence, position);
					sequence->CurrentTime = position;
					sequence->PreviousTime = position;
				}
			}
		}

		bool ApplyRemoteAnimationGraph(AAlicePawn* remote,
			const AnimationGraphPayload& graph, bool synchronizePosition)
		{
			g_remoteAppliedFullBodyNode = nullptr;
			g_remoteAppliedUpperBodyNode = nullptr;
			g_remoteAppliedHalfBodyNode = nullptr;
			g_remoteAppliedFullBodySlot = nullptr;
			g_remoteAppliedFullBodyName.clear();
			if (!remote || !remote->Mesh)
				return false;

			if (g_remotePresentation.valid) ApplyRemotePawnStance(remote, g_remotePresentation.state);
			EnableRemoteAnimationTick(remote);
			int32_t slotNodeIndex = -1;
			UAliceGameAnimNode_BlendBySlot* slot =
				ResolveRemoteFullBodySlot(remote, &slotNodeIndex);
			if (!slot)
				return false;
			if (synchronizePosition) TraceNativeFullBodyTransition(remote, graph, false);

			const std::size_t blendCount = std::min<std::size_t>(
				graph.blendCount, MaxAnimationBlends);
			const std::size_t sequenceCount = std::min<std::size_t>(
				graph.sequenceCount, MaxAnimationSequences);
			ApplyRemoteWeaponTypeBlends(remote, graph);
			ApplyRemoteUpperGraph(remote, graph, synchronizePosition,
				EAnimBlendNodeIndex::EABLIdx_Slot_HalfBody_Upper_Main, g_remoteHalfBodyChannel);
			ApplyRemoteUpperGraph(remote, graph, synchronizePosition,
				EAnimBlendNodeIndex::EABLIdx_Slot_Combat_Upper_Additive, g_remoteUpperAdditiveChannel);
			ApplyNativeUpperLowerBlend(remote, graph);
			ApplyRemoteWeaponGraph(remote, graph,
				synchronizePosition);
			const AnimationBlendPayload* sourceSlot = nullptr;
			for (std::size_t index = 0; index < blendCount; ++index)
			{
				const AnimationBlendPayload& state = graph.blends[index];
				if (state.component == AnimationComponent::Body
					&& NativeAnimationSlot(state.flags) == 0)
				{
					sourceSlot = &state;
					break;
				}
			}

			int32_t sourceChild = 0;
			const AnimationSequencePayload* sourceSequence = nullptr;
			const bool sourceCustomSlot = sourceSlot
				&& (sourceSlot->flags & (AnimationBlendCustom | AnimationBlendNativeSlotTiming)) != 0
				&& sourceSlot->activeChildIndex > 0
				&& sourceSlot->activeChildIndex < slot->Children.size();
			if (sourceCustomSlot)
			{
				sourceChild = sourceSlot->activeChildIndex;
				sourceSequence = FindSafeSequenceForSlot(graph,
					remote->Mesh, slot, sourceChild);
			}
			bool standaloneSelection = false;
			if (!sourceSequence)
			{
				const std::string preferred =
					g_remoteFullBodyChannel.standalone
					? g_remoteFullBodyChannel.sequenceName
					: std::string();
				if (!sourceCustomSlot)
				{
					sourceSequence =
						FindStandalonePresentationSequence(
							graph, preferred);
				}
				if (sourceSequence && slot->Children.size() > 1)
				{
					sourceChild = 1;
					standaloneSelection = true;
				}
			}

			bool applied = false;
			bool configStarted = false;
			int32_t targetChild = 0;
			if (sourceSequence && sourceChild > 0)
			{
				if (synchronizePosition)
					g_remoteFullBodyChannel.lastSourceSeen = Clock::now();
				// A non-looping source can remain relevant for a few frames after the
				// proxy slot naturally returns to child zero. Re-running PlayConfigAnim
				// for the same clip in that window restarted its last frames every tick
				// and produced the visible blocky melee/dodge transitions. Reuse the
				// existing dynamic child and correct only its source position. A real
				// sequence-name change still starts a fresh native config animation.
				const bool selectionChanged =
					!g_remoteFullBodyChannel.active
					|| g_remoteFullBodyChannel.blendNodeIndex
						!= static_cast<int32_t>(
							EAnimBlendNodeIndex::
								EABLIdx_Slot_FullBody_Main)
					|| _stricmp(
						g_remoteFullBodyChannel.sequenceName.c_str(),
						sourceSequence->sequenceName) != 0
					|| g_remoteFullBodyChannel.targetChild <= 0
					|| (!standaloneSelection && g_remoteFullBodyChannel.sourceChild != sourceChild)
					|| (synchronizePosition && !(sourceSequence->flags & AnimationSequenceLooping)
						&& sourceSequence->position + 0.05f < g_remoteFullBodyChannel.lastSourcePosition);
				if (selectionChanged)
				{
					configStarted = StartPresentationConfigAnimation(
						remote, g_remoteFullBodyChannel,
						static_cast<int32_t>(
							EAnimBlendNodeIndex::
								EABLIdx_Slot_FullBody_Main),
						*sourceSequence, standaloneSelection);
					if (configStarted
						&& slot->ActiveChildIndex > 0
						&& slot->ActiveChildIndex
							< slot->Children.size())
					{
						targetChild = slot->ActiveChildIndex;
					}
					else
					{
						targetChild = sourceChild;
					}
				}
				else
				{
					targetChild = g_remoteFullBodyChannel.targetChild;
				}

				if (targetChild <= 0
					|| targetChild >= slot->Children.size())
				{
					targetChild = sourceChild;
				}
				UAnimNode* childNode = slot->Children.at(targetChild).Anim;
				if (childNode
					&& childNode->IsA(UAnimNodeSequence::StaticClass()))
				{
					auto* targetSequence =
						reinterpret_cast<UAnimNodeSequence*>(childNode);
					// A fresh source selection can still own this child after the
					// proxy's autonomous tick has released it. Restore that selection
					// without starting another config. Repeated application of an old
					// graph must not reopen the native release on every pawn tick.
					const bool reselectChild = slot->ActiveChildIndex != targetChild
						&& (selectionChanged || synchronizePosition);
					if (reselectChild)
					{
						float blendTime = configStarted ? g_remoteFullBodyChannel.config.BlendInTime
							: PresentationBlendTime(sourceSequence->sequenceName);
						if (!selectionChanged && sourceSlot && (sourceSlot->flags & AnimationBlendNativeSlotTiming))
						{
							const float remaining = (std::max)(0.0f, sourceSlot->nodeIndex * 0.002f - RemoteAnimationSampleAge());
							blendTime = NativeBlendTransitionTime(remaining, slot->Children.at(targetChild).Weight);
						}
						Log("ANIMBLEND reconcile frame=" + std::to_string(graph.frameNumber)
							+ ", sequence=" + sourceSequence->sequenceName
							+ ", nativeChild=" + std::to_string(slot->ActiveChildIndex)
							+ ", sourceChild=" + std::to_string(sourceChild)
							+ ", targetChild=" + std::to_string(targetChild)
							+ ", time=" + std::to_string(sourceSequence->position) + '.');
						NativeSetActiveChild(slot, targetChild, blendTime);
					}
					applied = ApplySafeSlotSequence(remote, slot,
						targetChild, targetSequence, *sourceSequence,
						synchronizePosition, configStarted || reselectChild,
						PresentationSequenceKind::FullBody);
					if (applied)
					{
						if (slot->ActiveChildIndex == targetChild)
						{
							slot->bIsPlayingCustomAnim = standaloneSelection
								|| (sourceSlot && (sourceSlot->flags & AnimationBlendCustom));
							slot->bPlayActiveChild = (sourceSequence->rootBoneOptions & AnimationPoseStopped) == 0;
							if (sourceSlot && (sourceSlot->flags & AnimationBlendNativeSlotTiming))
								slot->CurrentPendingBlendOutTime = NativePendingBlendOut(sourceSlot->flags, graph.fullBodyBlendOutTicks);
						}
						g_remoteFullBodyChannel.active = true;
						g_remoteFullBodyChannel.standalone =
							standaloneSelection;
						g_remoteFullBodyChannel.blendNodeIndex =
							static_cast<int32_t>(
								EAnimBlendNodeIndex::
									EABLIdx_Slot_FullBody_Main);
						g_remoteFullBodyChannel.targetChild =
							targetChild;
						g_remoteFullBodyChannel.sourceChild =
							sourceChild;
						g_remoteFullBodyChannel.sequenceName =
							sourceSequence->sequenceName;
						g_remoteFullBodyChannel.lastSourcePosition =
							sourceSequence->position;
						g_remoteFullBodyChannel.lastSourceRate =
							sourceSequence->rate;
						if (synchronizePosition)
						{
							g_remoteFullBodyChannel.lastSourceSeen =
								Clock::now();
						}
						if (configStarted)
						{
							Log("ANIMBLEND native frame=" + std::to_string(graph.frameNumber)
								+ ", sequence=" + std::string(sourceSequence->sequenceName)
								+ ", in=" + std::to_string(g_remoteFullBodyChannel.config.BlendInTime)
								+ ", out=" + std::to_string(g_remoteFullBodyChannel.config.BlendOutTime)
								+ ", stopped=" + ((sourceSequence->rootBoneOptions & AnimationPoseStopped) ? "yes." : "no."));
							const std::string key =
								std::string("fullbody:")
								+ sourceSequence->sequenceName;
							if (g_loggedConfigAnimationStages.insert(
								key).second)
							{
								std::ostringstream stream;
								stream
									<< "CONFIGANIM full-body native requested="
									<< sourceSequence->sequenceName
									<< ", sourceChild=" << sourceChild
									<< ", targetChild=" << targetChild
									<< ", dynamicIndex="
									<< slot->CurrentDynamicAnimIndex
									<< ", blend="
									<< g_remoteFullBodyChannel.config.BlendInTime
									<< '.';
								Log(stream.str());
							}
						}
					}
				}
			}
			// Capture can briefly expose a custom slot before its new sequence
			// (or retain the old sequence after the slot packet). Preserve the
			// last native clip across that one-packet gap instead of blending
			// to locomotion and immediately back again.
			if (!applied && !sourceSequence
				&& (!sourceSlot || sourceSlot->activeChildIndex > 0)
				&& slot->ActiveChildIndex == g_remoteFullBodyChannel.targetChild
				&& g_remoteFullBodyChannel.active
				&& g_remoteFullBodyChannel.targetChild > 0
				&& g_remoteFullBodyChannel.targetChild
					< slot->Children.size()
				&& g_remoteFullBodyChannel.lastSourceSeen
					!= Clock::time_point{}
				&& Clock::now()
					- g_remoteFullBodyChannel.lastSourceSeen
					< std::chrono::milliseconds(110))
			{
				UAnimNode* heldNode = slot->Children.at(
					g_remoteFullBodyChannel.targetChild).Anim;
				if (heldNode
					&& heldNode->IsA(UAnimNodeSequence::StaticClass()))
				{
					auto* heldSequence =
						reinterpret_cast<UAnimNodeSequence*>(heldNode);
					if (slot->ActiveChildIndex
						!= g_remoteFullBodyChannel.targetChild)
					{
						NativeSetActiveChild(slot,
							g_remoteFullBodyChannel.targetChild, 0.04f);
					}
					// Do not let a non-looping dynamic slot wrap to frame zero in
					// the one-packet gap before the next combo segment arrives.
					// The owner holds/blends out its last sampled pose here.
					float heldPosition = (std::max)(0.0f,
						g_remoteFullBodyChannel.lastSourcePosition
						+ std::clamp(std::chrono::duration<float>(Clock::now() - g_remoteFullBodyChannel.lastSourceSeen).count(),
							0.0f, 0.11f) * g_remoteFullBodyChannel.lastSourceRate);
					if (heldSequence->AnimSeq
						&& heldSequence->AnimSeq->SequenceLength > 0.001f)
					{
						heldPosition = (std::min)(heldPosition,
							heldSequence->AnimSeq->SequenceLength - 0.001f);
					}
					NativeSetPosition(heldSequence, heldPosition);
					heldSequence->CurrentTime = heldPosition;
					heldSequence->PreviousTime = heldPosition;
					heldSequence->Rate = 0.0f;
					heldSequence->bPlaying = true;
					g_remoteAppliedFullBodyNode = heldSequence;
					g_remoteAppliedFullBodySlot = slot;
					g_remoteAppliedFullBodyName =
						g_remoteFullBodyChannel.sequenceName;
					slot->bIsPlayingCustomAnim = true;
					slot->bPlayActiveChild = true;
					applied = true;
				}
			}
			if (!applied)
			{
				float releaseTime = g_remoteFullBodyChannel.config.BlendOutTime;
				const bool nativeRelease = sourceSlot && sourceSlot->activeChildIndex == 0
					&& (sourceSlot->flags & AnimationBlendNativeSlotTiming);
				if (nativeRelease)
				{
					const float remaining = (std::max)(0.0f, sourceSlot->nodeIndex * 0.002f - RemoteAnimationSampleAge());
					releaseTime = NativeBlendTransitionTime(remaining, slot->Children.size() > 0
						? slot->Children.at(0).Weight : 0.0f);
				}
				if (g_remoteFullBodyChannel.active)
					Log("ANIMBLEND release sequence=" + g_remoteFullBodyChannel.sequenceName
						+ ", nativeTiming=" + (nativeRelease ? "yes" : "no")
						+ ", requested=" + std::to_string(releaseTime) + '.');
				StopPresentationConfigAnimation(remote, g_remoteFullBodyChannel, releaseTime);
				slot->bIsPlayingCustomAnim = false;
				slot->bPlayActiveChild = false;
				if (slot->ActiveChildIndex != 0)
					NativeSetActiveChild(slot, 0, nativeRelease ? releaseTime : 0.10f);
			}

			ApplyRemoteFullBodyFadePoses(slot, graph, synchronizePosition);


			if (synchronizePosition)
			{
				g_lastAppliedAnimationGraphFrame = graph.frameNumber;
				std::ostringstream signature;
				for (std::size_t index = 0;
					index < sequenceCount; ++index)
				{
					const AnimationSequencePayload& sequence =
						graph.sequences[index];
					if ((sequence.flags & (AnimationSequenceAction
						| AnimationSequenceCustom)) == 0)
					{
						continue;
					}
					signature << 'S'
						<< static_cast<int>(sequence.component) << ':'
						<< sequence.rootBoneOptions << '='
						<< sequence.sequenceName << '|';
				}
				for (std::size_t index = 0; index < blendCount; ++index)
				{
					const AnimationBlendPayload& blend =
						graph.blends[index];
					if ((blend.flags & AnimationBlendCustom) == 0)
						continue;
					signature << 'B'
						<< static_cast<int>(blend.component) << ':'
						<< blend.nodeIndex << '='
						<< blend.activeChildIndex << '|';
				}
				const std::string newSignature = signature.str();
				if (newSignature
					!= g_lastRemoteAnimationGraphSignature)
				{
					g_lastRemoteAnimationGraphSignature = newSignature;
					if (g_config.actionTrace
						|| g_config.animationLifecycleTrace)
					{
						Log("Remote AnimTree actions: "
							+ (newSignature.empty()
								? std::string("<none>") : newSignature) + '.');
					}
				}
				if (!g_loggedRemoteAnimationGraph)
				{
					g_loggedRemoteAnimationGraph = true;
					Log("Native config full-body replication v8 is active: slot="
						+ std::to_string(slotNodeIndex)
						+ ", sequences=" + std::to_string(sequenceCount)
						+ ", blends=" + std::to_string(blendCount) + '.');
				}
			}
			return applied;
		}

