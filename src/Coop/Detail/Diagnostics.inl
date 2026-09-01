		void ReportInvariantViolation(
			const char* event, const char* invariant,
			const std::string& details)
		{
			if (!g_config.invariantTrace)
				return;
			const std::string eventName = event ? event : "Unknown";
			const std::string invariantName = invariant ? invariant : "Unknown";
			const std::string signature = eventName + '|' + invariantName;
			{
				std::lock_guard<std::mutex> lock(g_invariantTraceMutex);
				if (!g_reportedInvariantViolations.insert(signature).second)
					return;
			}
			Log("[CoopInvariant] event=" + eventName
				+ " invariant=" + invariantName
				+ " details=" + details);
		}

		void TraceWorldResetInvariants()
		{
			if (!g_config.invariantTrace)
				return;
			if (g_remotePawn || g_remoteController || g_remoteWorld)
			{
				ReportInvariantViolation("WorldReset", "RemotePawnCleared",
					"remotePawn=" + std::to_string(g_remotePawn != nullptr)
					+ ",remoteController="
					+ std::to_string(g_remoteController != nullptr)
					+ ",remoteWorld=" + std::to_string(g_remoteWorld != nullptr));
			}
			if (!g_remotePepperVisuals.empty() || !g_remoteClockBombVisuals.empty())
			{
				ReportInvariantViolation("WorldReset", "ProjectilesCleared",
					"pepper=" + std::to_string(g_remotePepperVisuals.size())
					+ ",clockBomb=" + std::to_string(g_remoteClockBombVisuals.size()));
			}
			if (g_remoteContextInteraction.active
				|| !g_localInteractionKeysThisAttempt.empty()
				|| !g_sequenceOpUseSnapshot.empty()
				|| !g_usedEventSnapshot.empty())
			{
				ReportInvariantViolation("WorldReset", "InteractionStateCleared",
					"remoteActive=" + std::to_string(g_remoteContextInteraction.active)
					+ ",localKeys=" + std::to_string(g_localInteractionKeysThisAttempt.size())
					+ ",sequenceOps=" + std::to_string(g_sequenceOpUseSnapshot.size())
					+ ",usedEvents=" + std::to_string(g_usedEventSnapshot.size()));
			}
			if (!g_clientSharedEnemyBindings.empty()
				|| !g_sharedEnemyRegistry.empty()
				|| !g_hostEnemyAggro.empty())
			{
				ReportInvariantViolation("WorldReset", "EnemyBindingsCleared",
					"clientBindings=" + std::to_string(g_clientSharedEnemyBindings.size())
					+ ",registry=" + std::to_string(g_sharedEnemyRegistry.size())
					+ ",aggro=" + std::to_string(g_hostEnemyAggro.size()));
			}
		}

		void TracePeerTeardownInvariants()
		{
			if (!g_config.invariantTrace)
				return;
			if (g_remotePawn || g_remoteController || g_remoteWorld)
			{
				ReportInvariantViolation("PeerTeardown", "RemotePawnCleared",
					"remotePawn=" + std::to_string(g_remotePawn != nullptr)
					+ ",remoteController="
					+ std::to_string(g_remoteController != nullptr)
					+ ",remoteWorld=" + std::to_string(g_remoteWorld != nullptr));
			}
			if (g_remotePresentation.valid || g_remotePresentationWeapon
				|| g_remoteHairProxy || g_remoteGlideParticle)
			{
				ReportInvariantViolation("PeerTeardown", "PresentationCleared",
					"valid=" + std::to_string(g_remotePresentation.valid)
					+ ",weapon=" + std::to_string(g_remotePresentationWeapon != nullptr)
					+ ",hair=" + std::to_string(g_remoteHairProxy != nullptr)
					+ ",glide=" + std::to_string(g_remoteGlideParticle != nullptr));
			}
			if (g_activeRemoteAnimationGraph || !g_remoteGraphSequences.empty())
			{
				ReportInvariantViolation("PeerTeardown", "AnimationStateCleared",
					"graph=" + std::to_string(g_activeRemoteAnimationGraph.has_value())
					+ ",sequences=" + std::to_string(g_remoteGraphSequences.size()));
			}
			if (!g_remotePepperVisuals.empty() || !g_remoteClockBombVisuals.empty())
			{
				ReportInvariantViolation("PeerTeardown", "ProjectilesCleared",
					"pepper=" + std::to_string(g_remotePepperVisuals.size())
					+ ",clockBomb=" + std::to_string(g_remoteClockBombVisuals.size()));
			}
			if (g_remoteContextInteraction.active)
			{
				ReportInvariantViolation("PeerTeardown", "RemoteInteractionCleared",
					"remoteActive=1");
			}
			if (!g_clientSharedEnemyBindings.empty()
				|| !g_sharedEnemyRegistry.empty()
				|| !g_hostEnemyAggro.empty())
			{
				ReportInvariantViolation("PeerTeardown", "EnemyBindingsCleared",
					"clientBindings=" + std::to_string(g_clientSharedEnemyBindings.size())
					+ ",registry=" + std::to_string(g_sharedEnemyRegistry.size())
					+ ",aggro=" + std::to_string(g_hostEnemyAggro.size()));
			}
		}

		void TraceCutsceneReleaseInvariants()
		{
			if (!g_config.invariantTrace)
				return;
			if (g_waitingCutsceneAction || g_waitingCutsceneKey != 0)
			{
				ReportInvariantViolation("CutsceneBarrierReleased", "WaitingStateCleared",
					"action=" + std::to_string(g_waitingCutsceneAction != nullptr)
					+ ",key=" + std::to_string(g_waitingCutsceneKey));
			}
			if (g_waitingCutscenePlayRateOverridden
				|| g_waitingCutsceneCanDeferActivation
				|| g_waitingCutsceneActivationDeferred
				|| g_replayingDeferredCutsceneActivation)
			{
				ReportInvariantViolation("CutsceneBarrierReleased", "FlagsCleared",
					"playRateOverride="
					+ std::to_string(g_waitingCutscenePlayRateOverridden)
					+ ",canDefer="
					+ std::to_string(g_waitingCutsceneCanDeferActivation)
					+ ",activationDeferred="
					+ std::to_string(g_waitingCutsceneActivationDeferred)
					+ ",replaying="
					+ std::to_string(g_replayingDeferredCutsceneActivation));
			}
		}

		void TraceSaveSyncIdleInvariants(const char* event)
		{
			if (!g_config.invariantTrace)
				return;
			const char* eventName = event ? event : "SaveSyncFinished";
			if (g_hostSaveTransfer.active || g_clientSaveTransfer.active)
			{
				ReportInvariantViolation(eventName, "TransfersInactive",
					"hostActive=" + std::to_string(g_hostSaveTransfer.active)
					+ ",clientActive=" + std::to_string(g_clientSaveTransfer.active));
			}
			const std::uint32_t requested =
				g_requestedSaveTransferId.load(std::memory_order_acquire);
			if (requested != 0)
			{
				ReportInvariantViolation(eventName, "RequestCleared",
					"transferId=" + std::to_string(requested));
			}
			if (g_saveSyncInProgress.load(std::memory_order_acquire))
			{
				ReportInvariantViolation(eventName, "ProgressCleared",
					"inProgress=1");
			}
		}

		const char* PlayerActionName(PlayerAction action)
		{
			switch (action)
			{
			case PlayerAction::Jump: return "jump";
			case PlayerAction::MeleeAttack: return "melee-attack";
			case PlayerAction::WeaponAttack: return "weapon-attack";
			case PlayerAction::RightClickAttack: return "right-click-attack";
			case PlayerAction::StartFire: return "start-fire";
			case PlayerAction::StopFire: return "stop-fire";
			case PlayerAction::Dodge: return "dodge";
			case PlayerAction::ShrinkEnter: return "shrink-enter";
			case PlayerAction::ShrinkLeave: return "shrink-leave";
			case PlayerAction::WeaponSwitch: return "weapon-switch";
			default: return "none";
			}
		}

		void ExtendActionTraceWindow()
		{
			g_actionTraceWindowUntilMs = ElapsedMilliseconds()
				+ static_cast<std::uint64_t>(g_config.actionTraceWindowMs);
		}

		void ResetActionTraceState(AAlicePawn* pawn)
		{
			g_traceObjectLabelsReady = false;
			g_traceObjectLabels.clear();
			g_tracedPawn = pawn;
			g_tracedWeapon = nullptr;
			g_lastTracePawnState.clear();
			g_lastTraceWeaponState.clear();
			g_lastTraceBodyAnimations.clear();
			g_lastTraceUpperBodyAnimations.clear();
			g_lastTraceWeaponAnimations.clear();
			g_traceEventTimes.clear();
			g_actionTraceEventSerial = 0;
			g_nextTraceSample = {};
			ExtendActionTraceWindow();
			Log("TRACE SESSION pawn=" + ObjectName(pawn)
				+ ", map=" + g_currentMap + '.');
		}

		void RefreshTraceObjectLabels(AAlicePawn* pawn)
		{
			g_traceObjectLabelsReady = false;
			std::unordered_map<UObject*, std::string> labels;
			auto add = [&labels](UObject* object)
			{
				if (object)
					labels.emplace(object, ObjectName(object));
			};
			auto addSkeletalComponent = [&add](USkeletalMeshComponent* component)
			{
				if (!component)
					return;
				add(component);
				add(component->Animations);
				const int32_t nodeCount =
					std::min<int32_t>(component->AnimTickArray.size(), 1024);
				for (int32_t index = 0; index < nodeCount; ++index)
					add(component->AnimTickArray.at(index));
			};

			add(pawn);
			add(g_State.AlicePlayerController);
			add(pawn->HairComponent);
			add(pawn->SkirtComponent);
			add(pawn->BowComponent);
			add(pawn->RibbonComponent);
			add(pawn->EarComponent);
			addSkeletalComponent(pawn->Mesh);
			addSkeletalComponent(pawn->UpperBodyComponent);
			for (int32_t index = 0; index < pawn->SpecialMoves.size(); ++index)
				add(pawn->SpecialMoves.at(index));

			AWeapon* weapon = pawn->Weapon;
			add(weapon);
			if (weapon)
				addSkeletalComponent(weapon->Mesh);
			if (weapon && weapon->IsA(AAliceGameWeaponBase::StaticClass()))
			{
				auto* aliceWeapon =
					reinterpret_cast<AAliceGameWeaponBase*>(weapon);
				add(aliceWeapon->SlotNode);
			}

			g_traceObjectLabels.swap(labels);
			g_traceObjectLabelsReady = true;
		}

		std::string SpecialMoveObjectName(const AAlicePawn* pawn)
		{
			if (!pawn)
				return "<null>";
			const std::uint8_t index =
				static_cast<std::uint8_t>(pawn->SpecialMove);
			if (index >= pawn->SpecialMoves.size())
				return "<not-instanced>";
			return ObjectName(pawn->SpecialMoves.at(index));
		}

		std::string BuildTracePawnState(const AAlicePawn* pawn)
		{
			if (!pawn)
				return "<no-pawn>";

			const auto componentState = [](const UPrimitiveComponent* component)
			{
				if (!component)
					return std::string("<null>");
				return std::string(component->HiddenGame ? "hidden" : "visible")
					+ (component->bAttached ? ",attached" : ",detached");
			};
			const auto simulatorState = [](const FPointer& simulator)
			{
				return simulator.Dummy ? "sim" : "no-sim";
			};

			const auto quantize = [](float value, float stepsPerUnit)
			{
				return std::round(value * stepsPerUnit) / stepsPerUnit;
			};
			std::ostringstream stream;
			stream << std::fixed << std::setprecision(3)
				<< "special=" << static_cast<int>(pawn->SpecialMove)
				<< " (" << SpecialMoveObjectName(pawn) << ')'
				<< ", previous=" << static_cast<int>(pawn->PreviousSpecialMove)
				<< ", movement=" << static_cast<int>(pawn->BasicMovementState)
				<< ", physics=" << static_cast<int>(pawn->Physics)
				<< ", pendingPhysics=" << static_cast<int>(pawn->PendingPhysics)
				<< ", jump=" << static_cast<int>(pawn->CurrentJumpStatus)
				<< ", dodge=" << static_cast<int>(pawn->CurrentDodgeStatus)
				<< ", shrink=" << pawn->bShrinkingModeActive
				<< ", hidden=" << pawn->bHidden
				<< ", drawScale=" << quantize(pawn->DrawScale, 20.0f);
			if (pawn->Mesh)
			{
				stream << ", mesh=" << componentState(pawn->Mesh)
					<< ", meshScale="
					<< quantize(pawn->Mesh->Scale, 20.0f)
					<< ", meshTranslation=("
					<< quantize(pawn->Mesh->Translation.X, 2.0f) << ','
					<< quantize(pawn->Mesh->Translation.Y, 2.0f) << ','
					<< quantize(pawn->Mesh->Translation.Z, 2.0f) << ')';
			}
			if (pawn->HairComponent)
			{
				stream << ", hair=" << componentState(pawn->HairComponent)
					<< ',' << simulatorState(pawn->HairComponent->Simulator);
			}
			if (pawn->SkirtComponent)
			{
				stream << ", skirt=" << componentState(pawn->SkirtComponent)
					<< ',' << simulatorState(pawn->SkirtComponent->Simulator)
					<< ", clothEnabled="
					<< pawn->SkirtComponent->bEnableClothSimulation
					<< ", clothFrozen=" << pawn->SkirtComponent->bClothFrozen;
			}
			const AAlicePlayerController* controller = g_State.AlicePlayerController;
			stream << ", cinematic="
				<< (controller ? controller->bCinematicMode : false);
			return stream.str();
		}

		std::string BuildControlLifecycleState(const AAlicePawn* pawn)
		{
			if (!pawn)
				return "pawn=<null>";
			const AAlicePlayerController* controller =
				g_State.AlicePlayerController;
			std::ostringstream stream;
			stream << "physics=" << static_cast<int>(pawn->Physics)
				<< ", movement=" << static_cast<int>(pawn->BasicMovementState)
				<< ", special=" << static_cast<int>(pawn->SpecialMove)
				<< ", jumpStatus=" << static_cast<int>(pawn->CurrentJumpStatus)
				<< ", pawnCanJump=" << (pawn->bCanJump ? 1 : 0)
				<< ", jumpCapable=" << (pawn->bJumpCapable ? 1 : 0)
				<< ", aliceFrozen=" << (pawn->bIsFrozen ? 1 : 0)
				<< ", velocityZ=" << pawn->Velocity.Z
				<< ", floorZ=" << pawn->Floor.Z
				<< ", controller=" << ObjectName(controller);
			if (controller)
			{
				stream << ", pressedJump="
					<< (controller->bPressedJump ? 1 : 0)
					<< ", doubleJump="
					<< (controller->bDoubleJump ? 1 : 0)
					<< ", controllerFrozen="
					<< (controller->bFrozen ? 1 : 0)
					<< ", cinematic="
					<< (controller->bCinematicMode ? 1 : 0)
					<< ", cinemaMove="
					<< (controller->bCinemaDisableInputMove ? 1 : 0)
					<< ", cinemaLook="
					<< (controller->bCinemaDisableInputLook ? 1 : 0);
				if (controller->PlayerInput)
				{
					bool spaceInPressedKeys = false;
					for (int32_t index = 0;
						index < std::min<int32_t>(
							controller->PlayerInput->PressedKeys.size(), 128);
						++index)
					{
						if (_stricmp(controller->PlayerInput->PressedKeys
								.at(index).ToString().c_str(), "SpaceBar") == 0)
						{
							spaceInPressedKeys = true;
							break;
						}
					}
					stream << ", pressedKeys="
						<< controller->PlayerInput->PressedKeys.size()
						<< ", spaceTracked="
						<< (spaceInPressedKeys ? 1 : 0)
						<< ", spacePhysical="
						<< ((GetAsyncKeyState(VK_SPACE) & 0x8000) ? 1 : 0);
				}
			}
			return stream.str();
		}

		void BeginControlLifecycleTrace(const char* key, std::uint8_t event)
		{
			if (!g_config.controlLifecycleTrace)
				return;
			g_controlTraceUntil = Clock::now() + std::chrono::seconds(5);
			g_nextControlTraceSample = {};
			g_lastControlTraceSignature.clear();
			Log("CONTROLLIFE input key=" + std::string(key ? key : "<null>")
				+ ", event=" + std::to_string(event) + ", "
				+ BuildControlLifecycleState(GetLocalPawn()) + '.');
		}

		void TickControlLifecycleTrace()
		{
			if (!g_config.controlLifecycleTrace
				|| Clock::now() >= g_controlTraceUntil
				|| Clock::now() < g_nextControlTraceSample)
				return;
			g_nextControlTraceSample =
				Clock::now() + std::chrono::milliseconds(250);
			const std::string state =
				BuildControlLifecycleState(GetLocalPawn());
			const bool changed = state != g_lastControlTraceSignature;
			g_lastControlTraceSignature = state;
			Log("CONTROLLIFE sample changed="
				+ std::string(changed ? "yes, " : "no, ") + state + '.');
		}

		std::string BuildTraceWeaponState(AWeapon* weapon)
		{
			if (!weapon)
				return "weapon=<null>";

			std::ostringstream stream;
			stream << "weapon=" << ObjectName(weapon)
				<< ", fireMode=" << static_cast<int>(weapon->CurrentFireMode)
				<< ", hidden=" << weapon->bHidden
				<< ", putDown=" << weapon->bWeaponPutDown;
			if (weapon->IsA(AWeaponForAlice::StaticClass()))
			{
				const auto* aliceWeapon =
					reinterpret_cast<const AWeaponForAlice*>(weapon);
				stream << ", type="
					<< static_cast<int>(aliceWeapon->WeaponTypeEnum)
					<< ", inUse=" << aliceWeapon->bInUse
					<< ", fadeToHide=" << aliceWeapon->bFadeToHide;
			}
			if (weapon->IsA(AWeaponForAliceMelee::StaticClass()))
			{
				const auto* melee =
					reinterpret_cast<const AWeaponForAliceMelee*>(weapon);
				stream << ", combo="
					<< static_cast<int>(melee->CurrentComboState);
			}
			if (weapon->Mesh)
			{
				stream << ", mesh="
					<< (weapon->Mesh->HiddenGame ? "hidden" : "visible")
					<< (weapon->Mesh->bAttached ? ",attached" : ",detached");
			}
			if (weapon->IsA(AAliceGameWeaponBase::StaticClass()))
			{
				const auto* aliceBase =
					reinterpret_cast<const AAliceGameWeaponBase*>(weapon);
				if (aliceBase->SlotNode)
				{
					stream << ", slotActive="
						<< aliceBase->SlotNode->ActiveChildIndex
						<< ", slotCustom="
						<< aliceBase->SlotNode->bIsPlayingCustomAnim;
				}
			}
			return stream.str();
		}

		struct AnimationTraceSnapshot
		{
			std::string signature;
			std::string details;
		};

		AnimationTraceSnapshot BuildAnimationTrace(
			const USkeletalMeshComponent* component)
		{
			if (!component)
				return { "<null>", "<null>" };

			std::ostringstream signature;
			std::ostringstream details;
			details << std::fixed << std::setprecision(3);
			int activeCount = 0;
			const int32_t nodeCount =
				std::min<int32_t>(component->AnimTickArray.size(), 1024);
			for (int32_t index = 0; index < nodeCount; ++index)
			{
				UAnimNode* node = component->AnimTickArray.at(index);
				if (!node)
					continue;
				const bool relevant = node->bRelevant
					|| node->bJustBecameRelevant
					|| node->NodeTotalWeight > 0.001f;
				const std::string nodeName = node->NodeName.IsValid()
					? node->NodeName.ToString()
					: std::string("<unnamed>");

				if (node->IsA(UAnimNodeSequence::StaticClass()))
				{
					const auto* sequence =
						reinterpret_cast<const UAnimNodeSequence*>(node);
					if (!sequence->bPlaying || !relevant)
						continue;
					const std::string sequenceName =
						sequence->AnimSeqName.IsValid()
						? sequence->AnimSeqName.ToString()
						: std::string("<unnamed>");
					signature << "S" << index << ':' << nodeName
						<< '=' << sequenceName
						<< ":L" << sequence->bLooping
						<< ":R" << node->bRelevant << '|';
					details << "seq[" << index << "] " << nodeName
						<< '=' << sequenceName
						<< " time=" << sequence->CurrentTime
						<< " rate=" << sequence->Rate
						<< " weight=" << node->NodeTotalWeight
						<< " loop=" << sequence->bLooping << "; ";
					++activeCount;
					continue;
				}

				if (node->IsA(UAliceGameAnimNode_BlendList::StaticClass()))
				{
					const auto* blend =
						reinterpret_cast<const UAliceGameAnimNode_BlendList*>(node);
					if (!relevant && !blend->bIsPlayingCustomAnim)
						continue;
					signature << "A" << index << ':' << nodeName
						<< '=' << blend->ActiveChildIndex
						<< ":C" << blend->bIsPlayingCustomAnim << '|';
					details << "aliceBlend[" << index << "] " << nodeName
						<< " child=" << blend->ActiveChildIndex
						<< " weight=" << node->NodeTotalWeight
						<< " custom=" << blend->bIsPlayingCustomAnim << "; ";
					++activeCount;
					continue;
				}

				if (node->IsA(UAnimNodeBlendList::StaticClass()) && relevant)
				{
					const auto* blend =
						reinterpret_cast<const UAnimNodeBlendList*>(node);
					signature << "B" << index << ':' << nodeName
						<< '=' << blend->ActiveChildIndex << '|';
					details << "blend[" << index << "] " << nodeName
						<< " child=" << blend->ActiveChildIndex
						<< " weight=" << node->NodeTotalWeight << "; ";
					++activeCount;
				}
			}
			if (activeCount == 0)
				return { "<none>", "<none>" };
			return { signature.str(), details.str() };
		}

		void TraceAnimationComponent(const char* label,
			const USkeletalMeshComponent* component, std::string& previous)
		{
			const AnimationTraceSnapshot snapshot =
				BuildAnimationTrace(component);
			if (snapshot.signature == previous)
				return;
			previous = snapshot.signature;
			ExtendActionTraceWindow();
			Log(std::string("TRACE ANIM ") + label + ": " + snapshot.details);
		}

		void TickActionTrace(AAlicePawn* pawn, AWorldInfo* world)
		{
			if (!pawn || !world)
				return;
			if (pawn != g_tracedPawn)
				ResetActionTraceState(pawn);

			const auto now = Clock::now();
			if (now < g_nextTraceSample)
				return;
			g_nextTraceSample = now + std::chrono::milliseconds(50);
			RefreshTraceObjectLabels(pawn);

			const std::string pawnState = BuildTracePawnState(pawn);
			if (pawnState != g_lastTracePawnState)
			{
				g_lastTracePawnState = pawnState;
				ExtendActionTraceWindow();
				Log("TRACE STATE " + pawnState);
			}

			AWeapon* weapon = pawn->Weapon;
			if (weapon != g_tracedWeapon)
			{
				g_tracedWeapon = weapon;
				g_lastTraceWeaponState.clear();
				g_lastTraceWeaponAnimations.clear();
				ExtendActionTraceWindow();
			}
			const std::string weaponState = BuildTraceWeaponState(weapon);
			if (weaponState != g_lastTraceWeaponState)
			{
				g_lastTraceWeaponState = weaponState;
				ExtendActionTraceWindow();
				Log("TRACE WEAPON " + weaponState);
			}

			TraceAnimationComponent("body", pawn->Mesh,
				g_lastTraceBodyAnimations);
			TraceAnimationComponent("upper-body", pawn->UpperBodyComponent,
				g_lastTraceUpperBodyAnimations);
			TraceAnimationComponent("weapon", weapon ? weapon->Mesh : nullptr,
				g_lastTraceWeaponAnimations);

			if (g_config.showOnScreenStatus
				&& Clock::now() >= g_nextStatusDraw)
			{
				g_nextStatusDraw =
					Clock::now() + std::chrono::milliseconds(100);
				std::wostringstream message;
				message << L"AliceCoop ACTION TRACE | marker "
					<< g_actionTraceMarker << L" | recording";
				world->AddOnScreenDebugMessage(0x0A11CE, 0.15f,
					FColor(80, 255, 120), FString(message.str().c_str()));
			}
		}

		bool ContainsTraceKeyword(const std::string& name)
		{
			static constexpr std::array<const char*, 22> keywords{
				"SpecialMove", "Anim", "Weapon", "Fire", "Attack", "Dodge",
				"Shrink", "Jump", "Hover", "Float", "Butter", "Particle",
				"Effect", "Attach", "Detach", "Fade", "Hidden", "Visibility",
				"RootMotion", "Play", "Stop", "Slot"
			};
			for (const char* keyword : keywords)
			{
				if (name.find(keyword) != std::string::npos)
					return true;
			}
			return false;
		}

		bool IsHighSignalTraceFunction(const std::string& name)
		{
			static constexpr std::array<const char*, 17> signals{
				"DoSpecialMove", "SpecialMoveStarted", "SpecialMoveEnded",
				"SpecialMoveAssigned", "PlayWeapon", "PlayCustomAnim",
				"SetActiveChild", "SetBlendTarget", "MeleeAttack",
				"WeaponAttack", "RightClickAttack", "Dodge",
				"ChangeShrinkingMode", "StartFire", "StopWeaponFire",
				"AttachWeapon", "FadeOutWeapon"
			};
			for (const char* signal : signals)
			{
				if (name.find(signal) != std::string::npos)
					return true;
			}
			return false;
		}

		bool IsNoisyTraceFunction(const std::string& name)
		{
			if (name.find("UpdateCameraAnim") != std::string::npos
				|| name.find("FootStep") != std::string::npos
				|| name == "ServerUpdateLevelVisibility")
			{
				return true;
			}
			static constexpr std::array<const char*, 12> prefixes{
				"Tick", "PlayerTick", "Get", "Can", "Is", "Check",
				"Update", "Calc", "Should", "Verify", "Allow", "Display"
			};
			for (const char* prefix : prefixes)
			{
				if (name.rfind(prefix, 0) == 0)
					return true;
			}
			return false;
		}

		std::string TraceParameterBytes(const UFunction* function,
			const void* parameters)
		{
			if (!function || !parameters || function->ParmsSize == 0)
				return "-";
			const auto* bytes =
				static_cast<const std::uint8_t*>(parameters);
			const std::size_t count =
				std::min<std::size_t>(function->ParmsSize, 16);
			std::ostringstream stream;
			stream << std::hex << std::setfill('0');
			for (std::size_t index = 0; index < count; ++index)
			{
				if (index)
					stream << ' ';
				stream << std::setw(2)
					<< static_cast<unsigned int>(bytes[index]);
			}
			if (function->ParmsSize > count)
				stream << " ...";
			return stream.str();
		}

		void TraceLocalWeaponVfxEvent(UObject* object,
			UFunction* function, const void* parameters)
		{
			if (!g_config.enabled || !g_config.actionTrace
				|| !object || !function)
				return;
			const std::string name = function->GetName();
			if (name == "RefireCheckTimer"
				|| name == "GetMuzzleLoc"
				|| name == "MiniFireTimeOver")
			{
				return;
			}
			const bool relevant =
				ContainsCaseInsensitive(name, "muzzle")
				|| ContainsCaseInsensitive(name, "particle")
				|| ContainsCaseInsensitive(name, "projectile")
				|| ContainsCaseInsensitive(name, "effect")
				|| ContainsCaseInsensitive(name, "trace")
				|| ContainsCaseInsensitive(name, "fire")
				|| ContainsCaseInsensitive(name, "charge")
				|| ContainsCaseInsensitive(name, "loop")
				|| ContainsCaseInsensitive(name, "weaponsethidden");
			if (!relevant)
				return;
			// Map loading emits an enormous number of unrelated
			// ProcessEvents. Do not touch controller/pawn state until the
			// cheap function-name filter has identified a weapon VFX event.
			AAlicePawn* local = GetLocalPawn();
			if (!local || object != local->Weapon)
				return;
			const std::string key =
				"local-vfx|" + function->GetFullName();
			const std::uint64_t now = ElapsedMilliseconds();
			const auto previous = g_traceEventTimes.find(key);
			if (previous != g_traceEventTimes.end()
				&& now - previous->second < 10)
			{
				return;
			}
			g_traceEventTimes[key] = now;
			Log("LOCALVFX EVENT weapon=" + ObjectName(object)
				+ ", function=" + function->GetFullName()
				+ ", params="
				+ TraceParameterBytes(function, parameters) + '.');
		}

		void TraceProcessEventInternal(UObject* object, UFunction* function,
			const void* parameters)
		{
			TraceLocalWeaponVfxEvent(
				object, function, parameters);
			if (!g_config.actionTrace || !object || !function)
				return;
			if (!g_State.bRealGameplay
				|| !g_traceObjectLabelsReady.load())
				return;

			const auto objectLabel = g_traceObjectLabels.find(object);
			if (objectLabel == g_traceObjectLabels.end())
				return;
			const std::string functionName = function->GetName();
			const bool highSignal = IsHighSignalTraceFunction(functionName);
			const bool windowActive =
				ElapsedMilliseconds() <= g_actionTraceWindowUntilMs.load();
			if (!highSignal
				&& (!windowActive
					|| !ContainsTraceKeyword(functionName)
					|| IsNoisyTraceFunction(functionName)))
			{
				return;
			}

			const std::string eventKey = objectLabel->second
				+ '|' + function->GetFullName();
			const std::uint64_t now = ElapsedMilliseconds();
			const auto previous = g_traceEventTimes.find(eventKey);
			const std::uint64_t minimumInterval = highSignal ? 20 : 200;
			if (previous != g_traceEventTimes.end()
				&& now - previous->second < minimumInterval)
			{
				return;
			}
			g_traceEventTimes[eventKey] = now;

			Log("TRACE EVENT #" + std::to_string(++g_actionTraceEventSerial)
				+ " object=" + objectLabel->second
				+ ", function=" + function->GetFullName()
				+ ", params=" + TraceParameterBytes(function, parameters) + '.');
		}

		void LogCosmeticState(const char* label, const AAlicePawn* pawn)
		{
			if (!pawn)
			{
				Log(std::string("Cosmetics[") + label + "]: pawn=<null>");
				return;
			}

			std::ostringstream stream;
			stream << "Cosmetics[" << label << "]: archetype="
				<< static_cast<int>(pawn->ArcheTypeID)
				<< ", pawn=" << ObjectName(pawn)
				<< ", mesh=" << ObjectName(pawn->Mesh ? pawn->Mesh->SkeletalMesh : nullptr);
			Log(stream.str());

			if (pawn->HairComponent)
			{
				const UHairComponent* hair =
					pawn->HairComponent;
				stream.str({});
				stream.clear();
				stream << "Cosmetics[" << label << "].hair: component="
					<< ObjectName(hair)
					<< ", owner=" << ObjectName(hair->Owner)
					<< ", attached=" << hair->bAttached
					<< ", scene=" << (hair->Scene.Dummy ? "yes" : "no")
					<< ", sceneInfo="
					<< (hair->SceneInfo.Dummy ? "yes" : "no")
					<< ", hidden=" << hair->HiddenGame
					<< ", template=" << ObjectName(hair->Template)
					<< ", templateMesh=" << ObjectName(
						hair->Template
							? hair->Template->SkeletalMesh : nullptr)
					<< ", nodes="
					<< (hair->Template
						? hair->Template->Nodes.size() : 0)
					<< ", strands="
					<< (hair->Template
						? hair->Template->Strands.size() : 0)
					<< ", material=" << ObjectName(hair->Material)
					<< ", overrideMesh=" << ObjectName(hair->OverrideMesh)
					<< ", simulator="
					<< (hair->Simulator.Dummy ? "yes" : "no")
					<< ", bounds=(" << hair->Bounds.Origin.X << ','
					<< hair->Bounds.Origin.Y << ','
					<< hair->Bounds.Origin.Z << ";r="
					<< hair->Bounds.SphereRadius << ')'
					<< ", world=(" << hair->LocalToWorld.WPlane.X << ','
					<< hair->LocalToWorld.WPlane.Y << ','
					<< hair->LocalToWorld.WPlane.Z << ')'
					<< ", relative=(" << hair->Translation.X << ','
					<< hair->Translation.Y << ','
					<< hair->Translation.Z << ";rot="
					<< hair->Rotation.Pitch << ','
					<< hair->Rotation.Yaw << ','
					<< hair->Rotation.Roll << ";scale="
					<< hair->Scale << ')'
					<< ", lastRender=" << hair->LastRenderTime;
				Log(stream.str());
			}
			else
			{
				Log(std::string("Cosmetics[") + label + "].hair: component=<null>");
			}

			auto logCloth = [label](const char* componentLabel, const UClothComponent* cloth)
			{
				if (!cloth)
				{
					Log(std::string("Cosmetics[") + label + "]." + componentLabel
						+ ": component=<null>");
					return;
				}

				std::ostringstream clothStream;
				clothStream << "Cosmetics[" << label << "]." << componentLabel
					<< ": component=" << ObjectName(cloth)
					<< ", owner=" << ObjectName(cloth->Owner)
					<< ", attached=" << cloth->bAttached
					<< ", scene=" << (cloth->Scene.Dummy ? "yes" : "no")
					<< ", sceneInfo="
					<< (cloth->SceneInfo.Dummy ? "yes" : "no")
					<< ", hidden=" << cloth->HiddenGame
					<< ", mesh=" << ObjectName(cloth->SkeletalMesh)
					<< ", parentMesh=" << ObjectName(cloth->AttachedToSkelComponent)
					<< ", parentAnim=" << ObjectName(cloth->ParentAnimComponent)
					<< ", parentBones=" << cloth->ParentBoneMap.size()
					<< ", transformFromParent="
					<< cloth->bTransformFromAnimParent
					<< ", simulator=" << (cloth->Simulator.Dummy ? "yes" : "no")
					<< ", fixed=" << (cloth->FixedBone.IsValid()
						? cloth->FixedBone.ToString()
						: std::string("<invalid>"))
					<< ", fixedIndex=" << cloth->FixedNodeIndex
					<< ", boneNodes=" << cloth->boneNodeCount
					<< ", bounds=(" << cloth->Bounds.Origin.X << ','
					<< cloth->Bounds.Origin.Y << ','
					<< cloth->Bounds.Origin.Z << ";r="
					<< cloth->Bounds.SphereRadius << ')'
					<< ", world=("
					<< cloth->LocalToWorld.WPlane.X << ','
					<< cloth->LocalToWorld.WPlane.Y << ','
					<< cloth->LocalToWorld.WPlane.Z << ')'
					<< ", relative=(" << cloth->Translation.X << ','
					<< cloth->Translation.Y << ','
					<< cloth->Translation.Z << ";rot="
					<< cloth->Rotation.Pitch << ','
					<< cloth->Rotation.Yaw << ','
					<< cloth->Rotation.Roll << ";scale="
					<< cloth->Scale << ')'
					<< ", lastRender=" << cloth->LastRenderTime;
				Log(clothStream.str());
			};

			logCloth("skirt", pawn->SkirtComponent);
			logCloth("bow", pawn->BowComponent);
			logCloth("ribbon", pawn->RibbonComponent);
			logCloth("ear", pawn->EarComponent);
		}

		int32_t FindPresentationAnchorBone(
			USkeletalMeshComponent* body, bool headAnchor,
			FName& anchorName)
		{
			if (!body)
				return -1;

			int32_t bestIndex = -1;
			FName bestName;
			int bestPriority = 1000;
			std::size_t bestLength = 1000;
			const int32_t boneCount = std::min<int32_t>(
				body->SpaceBases.size(),
				body->SkeletalMesh
					? body->SkeletalMesh->RefSkeleton.size()
					: body->SpaceBases.size());
			for (int32_t index = 0; index < boneCount; ++index)
			{
				FName name;
				if (!NativeGetBoneName(body, index, name))
					continue;
				const std::string text = name.ToString();
				std::string lower = text;
				std::transform(lower.begin(), lower.end(),
					lower.begin(), [](unsigned char value)
					{
						return static_cast<char>(std::tolower(value));
					});

				int priority = 1000;
				if (headAnchor)
				{
					if (lower == "head" || lower == "bip01 head")
						priority = 0;
					else if (lower.find("head") != std::string::npos
						&& lower.find("end") == std::string::npos)
						priority = 1;
					else if (lower.find("neck") != std::string::npos)
						priority = 2;
					else if (lower.find("spine") != std::string::npos)
						priority = 3;
				}
				else
				{
					if (lower == "pelvis"
						|| lower == "bip01 pelvis")
						priority = 0;
					else if (lower.find("pelvis")
						!= std::string::npos)
						priority = 1;
					else if (lower.find("hip")
						!= std::string::npos)
						priority = 2;
					else if (lower.find("root")
						!= std::string::npos)
						priority = 3;
				}
				if (priority < bestPriority
					|| (priority == bestPriority
						&& text.size() < bestLength))
				{
					bestIndex = index;
					bestName = name;
					bestPriority = priority;
					bestLength = text.size();
				}
			}

			if (bestIndex < 0 && boneCount > 0)
			{
				bestIndex = 0;
				NativeGetBoneName(body, 0, bestName);
			}
			if (bestIndex >= 0)
			{
				memcpy_s(&anchorName, sizeof(anchorName),
					&bestName, sizeof(bestName));
			}
			return bestIndex;
		}

		FVector MatrixOrigin(const FMatrix& matrix)
		{
			return FVector(
				matrix.WPlane.X,
				matrix.WPlane.Y,
				matrix.WPlane.Z);
		}

		FVector TransformPoint(
			const FVector& point, const FMatrix& matrix)
		{
			return FVector(
				point.X * matrix.XPlane.X
					+ point.Y * matrix.YPlane.X
					+ point.Z * matrix.ZPlane.X
					+ matrix.WPlane.X,
				point.X * matrix.XPlane.Y
					+ point.Y * matrix.YPlane.Y
					+ point.Z * matrix.ZPlane.Y
					+ matrix.WPlane.Y,
				point.X * matrix.XPlane.Z
					+ point.Y * matrix.YPlane.Z
					+ point.Z * matrix.ZPlane.Z
					+ matrix.WPlane.Z);
		}

		std::string DescribeSpatialFrame(
			const FMatrix& matrix)
		{
			std::ostringstream stream;
			stream << std::fixed << std::setprecision(2)
				<< "O=(" << matrix.WPlane.X << ','
				<< matrix.WPlane.Y << ','
				<< matrix.WPlane.Z << ')'
				<< " X=(" << matrix.XPlane.X << ','
				<< matrix.XPlane.Y << ','
				<< matrix.XPlane.Z << ')'
				<< " Y=(" << matrix.YPlane.X << ','
				<< matrix.YPlane.Y << ','
				<< matrix.YPlane.Z << ')'
				<< " Z=(" << matrix.ZPlane.X << ','
				<< matrix.ZPlane.Y << ','
				<< matrix.ZPlane.Z << ')';
			return stream.str();
		}

		void LogCosmeticComponentSpatial(
			AAlicePawn* remote, const char* label,
			USkeletalMeshComponent* component,
			const FMatrix& targetBoneMatrix,
			const char* targetBoneLabel)
		{
			if (!remote || !remote->Mesh || !component
				|| !component->SkeletalMesh)
			{
				Log(std::string("SPATIAL ") + label
					+ ": unavailable.");
				return;
			}

			const FVector targetOrigin =
				MatrixOrigin(targetBoneMatrix);
			int32_t nearestIndex = -1;
			float nearestDistance =
				(std::numeric_limits<float>::max)();
			FMatrix nearestWorldMatrix{};
			const int32_t boneCount = std::min<int32_t>(
				component->SpaceBases.size(),
				component->SkeletalMesh->RefSkeleton.size());
			for (int32_t index = 0;
				index < boneCount; ++index)
			{
				const FMatrix worldMatrix = MultiplyMatrices(
					component->SpaceBases.at(index),
					component->LocalToWorld);
				const float distance = FVector::Dist(
					MatrixOrigin(worldMatrix), targetOrigin);
				if (distance < nearestDistance)
				{
					nearestDistance = distance;
					nearestIndex = index;
					nearestWorldMatrix = worldMatrix;
				}
			}
			FName nearestName;
			if (nearestIndex >= 0)
				NativeGetBoneName(
					component, nearestIndex, nearestName);

			const USkeletalMesh* asset =
				component->SkeletalMesh;
			const FVector assetBoundsWorld =
				TransformPoint(
					asset->Bounds.Origin,
					component->LocalToWorld);
			const FAttachment* attachment = nullptr;
			for (int32_t index = 0;
				index < remote->Mesh->Attachments.size(); ++index)
			{
				const FAttachment& candidate =
					remote->Mesh->Attachments.at(index);
				if (candidate.Component == component)
				{
					attachment = &candidate;
					break;
				}
			}

			std::ostringstream stream;
			stream << "SPATIAL " << label
				<< ": component{"
				<< DescribeSpatialFrame(
					component->LocalToWorld) << '}'
				<< ", assetBoundsLocal=("
				<< asset->Bounds.Origin.X << ','
				<< asset->Bounds.Origin.Y << ','
				<< asset->Bounds.Origin.Z << ";r="
				<< asset->Bounds.SphereRadius << ')'
				<< ", assetBoundsWorld=("
				<< assetBoundsWorld.X << ','
				<< assetBoundsWorld.Y << ','
				<< assetBoundsWorld.Z << ')'
				<< ", importOrigin=("
				<< asset->Origin.X << ','
				<< asset->Origin.Y << ','
				<< asset->Origin.Z << ";rot="
				<< asset->RotOrigin.Pitch << ','
				<< asset->RotOrigin.Yaw << ','
				<< asset->RotOrigin.Roll << ')'
				<< ", target=" << targetBoneLabel
				<< '{' << DescribeSpatialFrame(
					targetBoneMatrix) << '}'
				<< ", nearestBone="
				<< (nearestName.IsValid()
					? nearestName.ToString()
					: std::string("<none>"))
				<< '[' << nearestIndex << "]"
				<< ", nearestDistance=" << nearestDistance;
			if (nearestIndex >= 0)
			{
				stream << ", nearestFrame={"
					<< DescribeSpatialFrame(
						nearestWorldMatrix) << '}';
			}
			if (attachment)
			{
				stream << ", attachment=";
				stream << (attachment->BoneName.IsValid()
					? attachment->BoneName.ToString()
					: std::string("<invalid>"));
				stream << ", rel=("
					<< attachment->RelativeLocation.X << ','
					<< attachment->RelativeLocation.Y << ','
					<< attachment->RelativeLocation.Z
					<< ";rot="
					<< attachment->RelativeRotation.Pitch << ','
					<< attachment->RelativeRotation.Yaw << ','
					<< attachment->RelativeRotation.Roll
					<< ";scale="
					<< attachment->RelativeScale.X << ','
					<< attachment->RelativeScale.Y << ','
					<< attachment->RelativeScale.Z << ')';
			}
			else
			{
				stream << ", attachment=<none>";
			}
			stream << '.';
			Log(stream.str());
		}

		void LogRemoteCosmeticSpatialSample(
			AAlicePawn* remote)
		{
			if (!remote || !remote->Mesh)
				return;

			FName rootName;
			NativeGetBoneName(remote->Mesh, 0, rootName);
			FName pelvisName;
			FName headName;
			const int32_t pelvisIndex =
				FindPresentationAnchorBone(
					remote->Mesh, false, pelvisName);
			const int32_t headIndex =
				FindPresentationAnchorBone(
					remote->Mesh, true, headName);
			FMatrix rootMatrix{};
			FMatrix pelvisMatrix{};
			FMatrix headMatrix{};
			const bool hasRoot =
				NativeGetBoneMatrix(
					remote->Mesh, 0, rootMatrix);
			const bool hasPelvis = pelvisIndex >= 0
				&& NativeGetBoneMatrix(
					remote->Mesh, pelvisIndex,
					pelvisMatrix);
			const bool hasHead = headIndex >= 0
				&& NativeGetBoneMatrix(
					remote->Mesh, headIndex,
					headMatrix);
			if (!hasRoot || !hasPelvis || !hasHead)
			{
				Log("SPATIAL body bone matrices unavailable.");
				return;
			}

			const int sampleNumber =
				++g_cosmeticSpatialSampleNumber;
			std::ostringstream bodyStream;
			bodyStream << "SPATIAL sample=" << sampleNumber
				<< ", actor=(" << remote->Location.X << ','
				<< remote->Location.Y << ','
				<< remote->Location.Z << ";rot="
				<< remote->Rotation.Pitch << ','
				<< remote->Rotation.Yaw << ','
				<< remote->Rotation.Roll << ')'
				<< ", mesh={"
				<< DescribeSpatialFrame(
					remote->Mesh->LocalToWorld) << '}'
				<< ", root="
				<< (rootName.IsValid()
					? rootName.ToString()
					: std::string("<invalid>"))
				<< "[0]{" << DescribeSpatialFrame(
					rootMatrix) << '}'
				<< ", pelvis=" << pelvisName.ToString()
				<< '[' << pelvisIndex << "]{"
				<< DescribeSpatialFrame(pelvisMatrix) << '}'
				<< ", head=" << headName.ToString()
				<< '[' << headIndex << "]{"
				<< DescribeSpatialFrame(headMatrix) << "}.";
			Log(bodyStream.str());

			LogCosmeticComponentSpatial(
				remote, "skirt", remote->SkirtComponent,
				pelvisMatrix, "pelvis");
			if (g_remoteHairProxy
				&& g_remoteHairProxy->SkeletalMeshComponent)
			{
				LogCosmeticComponentSpatial(
					remote, "hair-skeletal",
					g_remoteHairProxy
						->SkeletalMeshComponent,
					headMatrix, "head");
			}
		}

