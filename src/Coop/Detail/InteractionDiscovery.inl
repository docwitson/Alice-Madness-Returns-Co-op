		std::string ObjectName(const UObject* object)
		{
			return object ? const_cast<UObject*>(object)->GetFullName() : "<null>";
		}

		enum class WorldTraceKind : std::uint8_t
		{
			None,
			Enemy,
			Breakable,
			Pickup,
			DroppedPickup,
		};

		const char* WorldTraceKindName(WorldTraceKind kind)
		{
			switch (kind)
			{
			case WorldTraceKind::Enemy:
				return "enemy";
			case WorldTraceKind::Breakable:
				return "breakable";
			case WorldTraceKind::Pickup:
				return "pickup";
			case WorldTraceKind::DroppedPickup:
				return "dropped-pickup";
			default:
				return "unknown";
			}
		}

		WorldTraceKind IdentifyWorldTraceObject(UObject* object)
		{
			if (!object || object->IsDefaultObject())
				return WorldTraceKind::None;
			const auto cached =
				g_worldTraceClassCache.find(object->Class);
			if (cached != g_worldTraceClassCache.end())
			{
				return static_cast<WorldTraceKind>(
					cached->second);
			}
			WorldTraceKind result = WorldTraceKind::None;
			if (object->IsA(AAliceGameKynapsePawn::StaticClass()))
				result = WorldTraceKind::Enemy;
			else if (object->IsA(AGameBreakableActor::StaticClass()))
				result = WorldTraceKind::Breakable;
			else if (object->IsA(AAlicePickupFactory::StaticClass()))
				result = WorldTraceKind::Pickup;
			else if (object->IsA(AAliceDroppedPickup::StaticClass()))
				result = WorldTraceKind::DroppedPickup;
			if (object->Class)
			{
				g_worldTraceClassCache.emplace(object->Class,
					static_cast<std::uint8_t>(result));
			}
			return result;
		}

		std::string FormatWorldTraceVector(const FVector& value)
		{
			std::ostringstream stream;
			stream << std::fixed << std::setprecision(1)
				<< '(' << value.X << ',' << value.Y << ',' << value.Z << ')';
			return stream.str();
		}

		bool IsUsefulWorldTraceOrigin(const FVector& value)
		{
			return std::fabs(value.X) > 0.01f
				|| std::fabs(value.Y) > 0.01f
				|| std::fabs(value.Z) > 0.01f;
		}

		FVector WorldTraceOrigin(UObject* object, WorldTraceKind kind)
		{
			auto* actor = reinterpret_cast<AActor*>(object);
			if (kind == WorldTraceKind::Enemy)
			{
				auto* enemy =
					reinterpret_cast<AAliceGameKynapsePawn*>(object);
				if (IsUsefulWorldTraceOrigin(
					enemy->NpcSpawnOriginalLocation))
				{
					return enemy->NpcSpawnOriginalLocation;
				}
			}
			return actor->Location;
		}

		bool IsWithinWorldTraceRadius(const AActor* actor,
			const AAlicePawn* localPawn)
		{
			if (!actor || !localPawn)
				return false;
			const float x = actor->Location.X - localPawn->Location.X;
			const float y = actor->Location.Y - localPawn->Location.Y;
			const float z = actor->Location.Z - localPawn->Location.Z;
			const float radius = g_config.worldTraceRadius;
			return x * x + y * y + z * z <= radius * radius;
		}

		std::uint64_t WorldTraceHash(const std::string& value)
		{
			std::uint64_t hash = 1469598103934665603ull;
			for (const unsigned char byte : value)
			{
				hash ^= byte;
				hash *= 1099511628211ull;
			}
			return hash;
		}

		std::uint64_t SequenceActionStableKey(UObject* action)
		{
			return action ? WorldTraceHash(ObjectName(action)) : 0;
		}

		USeqAct_SetVentState* FindVentStateAction(
			std::uint64_t entityKey)
		{
			if (entityKey == 0)
				return nullptr;
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return nullptr;
			for (int32_t index = 0; index < objects->size(); ++index)
			{
				UObject* object = objects->at(index);
				if (object
					&& object->IsA(
						USeqAct_SetVentState::StaticClass())
					&& SequenceActionStableKey(object) == entityKey)
				{
					return reinterpret_cast<
						USeqAct_SetVentState*>(object);
				}
			}
			return nullptr;
		}

		USeqEvent_Used* FindUsedInteractionEvent(
			std::uint64_t entityKey)
		{
			if (entityKey == 0)
				return nullptr;
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return nullptr;
			for (int32_t index = 0; index < objects->size(); ++index)
			{
				UObject* object = objects->at(index);
				if (object
					&& object->IsA(USeqEvent_Used::StaticClass())
					&& SequenceActionStableKey(object) == entityKey)
				{
					return reinterpret_cast<USeqEvent_Used*>(
						object);
				}
			}
			return nullptr;
		}

		USeqAct_InteractInLondon* FindLondonInteractionAction(
			std::uint64_t entityKey)
		{
			if (entityKey == 0)
				return nullptr;
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return nullptr;
			for (int32_t index = 0; index < objects->size(); ++index)
			{
				UObject* object = objects->at(index);
				if (object
					&& object->IsA(
						USeqAct_InteractInLondon::StaticClass())
					&& SequenceActionStableKey(object) == entityKey)
				{
					return reinterpret_cast<
						USeqAct_InteractInLondon*>(object);
				}
			}
			return nullptr;
		}

		ATrigger* FindInteractionTrigger(std::uint64_t entityKey)
		{
			if (entityKey == 0)
				return nullptr;
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return nullptr;
			for (int32_t index = 0; index < objects->size(); ++index)
			{
				UObject* object = objects->at(index);
				if (object
					&& object->IsA(ATrigger::StaticClass())
					&& SequenceActionStableKey(object) == entityKey)
				{
					return reinterpret_cast<ATrigger*>(object);
				}
			}
			return nullptr;
		}

		USeqEvent_ContextActionActivated*
			FindContextActionEvent(std::uint64_t entityKey)
		{
			if (entityKey == 0)
				return nullptr;
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return nullptr;
			for (int32_t index = 0; index < objects->size(); ++index)
			{
				UObject* object = objects->at(index);
				if (object
					&& object->IsA(
						USeqEvent_ContextActionActivated::
							StaticClass())
					&& SequenceActionStableKey(object) == entityKey)
				{
					return reinterpret_cast<
						USeqEvent_ContextActionActivated*>(object);
				}
			}
			return nullptr;
		}

		AContextActor* FindContextActor(std::uint64_t entityKey)
		{
			if (entityKey == 0)
				return nullptr;
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return nullptr;
			for (int32_t index = 0; index < objects->size(); ++index)
			{
				UObject* object = objects->at(index);
				if (object
					&& object->IsA(AContextActor::StaticClass())
					&& SequenceActionStableKey(object) == entityKey)
				{
					return reinterpret_cast<AContextActor*>(object);
				}
			}
			return nullptr;
		}

		ContextActorObservation ObserveContextActor(
			AContextActor* actor)
		{
			ContextActorObservation result{};
			if (!actor)
				return result;
			result.alice = actor->Alice;
			result.controller = actor->APC;
			result.started = actor->bContextActionStarted != 0;
			result.inTriggerArea = actor->bInTriggerArea != 0;
			result.blendingPosition =
				actor->bIsBlendingPosition != 0;
			result.blendingRotation =
				actor->bIsBlendingRotation != 0;
			return result;
		}

		void CaptureContextActorUseSnapshot()
		{
			g_contextActorUseSnapshot.clear();
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return;
			for (int32_t index = 0; index < objects->size(); ++index)
			{
				UObject* object = objects->at(index);
				if (!object
					|| !object->IsA(AContextActor::StaticClass()))
				{
					continue;
				}
				auto* actor =
					reinterpret_cast<AContextActor*>(object);
				g_contextActorUseSnapshot.emplace(
					actor, ObserveContextActor(actor));
			}
			g_pendingContextActorUseDetection = true;
			g_contextActorUseDetectionUntil =
				Clock::now() + std::chrono::seconds(2);
			if (g_config.actionTrace)
			{
				Log("CONTEXTACTOR snapshot actors="
					+ std::to_string(
						g_contextActorUseSnapshot.size()) + '.');
			}
		}

		void DetectStartedContextActor(bool finalAttempt)
		{
			if (!g_pendingContextActorUseDetection)
				return;
			AAlicePawn* localPawn = GetLocalPawn();
			AAlicePlayerController* controller =
				g_State.AlicePlayerController;
			AContextActor* selected = nullptr;
			std::string reason;
			for (auto& [actor, before] :
				g_contextActorUseSnapshot)
			{
				if (!actor || !IsLiveUObject(actor))
					continue;
				const ContextActorObservation after =
					ObserveContextActor(actor);
				const bool locallyOwned =
					after.alice == localPawn
					|| after.controller == controller;
				if (!locallyOwned)
					continue;
				if (!before.started && after.started)
					reason = "started";
				else if (before.alice != localPawn
					&& after.alice == localPawn)
					reason = "alice-assigned";
				else if (before.controller != controller
					&& after.controller == controller)
					reason = "controller-assigned";
				else if (!before.blendingPosition
					&& after.blendingPosition)
					reason = "position-blend";
				else if (!before.blendingRotation
					&& after.blendingRotation)
					reason = "rotation-blend";
				else
					continue;
				selected = actor;
				break;
			}
			if (selected)
			{
				const std::uint64_t key =
					SequenceActionStableKey(selected);
				if (key != 0
					&& g_localInteractionKeysThisAttempt.insert(
						key).second)
				{
					SharedWorldEventPayload event{};
					event.kind = SharedWorldEventKind::
						InteractionContextActorStarted;
					event.entityKey = key;
					event.originActorId =
						g_config.role == Role::Host ? 1u : 2u;
					QueueSharedWorldEvent(event);
					Log("CONTEXTACTOR TX key="
						+ SharedWorldKeyText(key)
						+ ", actor=" + ObjectName(selected)
						+ ", reason=" + reason
						+ ", alice="
						+ ObjectName(selected->Alice)
						+ ", controller="
						+ ObjectName(selected->APC)
						+ ", started="
						+ (selected->bContextActionStarted
							? std::string("yes")
							: std::string("no"))
						+ ", inArea="
						+ (selected->bInTriggerArea
							? std::string("yes.")
							: std::string("no.")));
				}
				g_pendingContextActorUseDetection = false;
				g_contextActorUseSnapshot.clear();
			}
			else if (finalAttempt)
			{
				Log("CONTEXTACTOR no started actor detected.");
				g_pendingContextActorUseDetection = false;
				g_contextActorUseSnapshot.clear();
			}
		}

		void QueueTriggerInteraction(ATrigger* trigger,
			const std::string& source)
		{
			const std::uint64_t key =
				SequenceActionStableKey(trigger);
			if (!trigger || key == 0
				|| !g_localInteractionKeysThisAttempt.insert(
					key).second)
			{
				return;
			}
			SharedWorldEventPayload event{};
			event.kind =
				SharedWorldEventKind::InteractionTriggerUsed;
			event.entityKey = key;
			event.originActorId =
				g_config.role == Role::Host ? 1u : 2u;
			QueueSharedWorldEvent(event);
			Log("TRIGGERINTERACT TX key="
				+ SharedWorldKeyText(key)
				+ ", trigger=" + ObjectName(trigger)
				+ ", source=" + source
				+ ", times="
				+ std::to_string(trigger->TriggerTimes)
				+ ", recent="
				+ (trigger->bRecentlyTriggered
					? std::string("yes")
					: std::string("no"))
				+ ", enabled="
				+ (trigger->bEnabled
					? std::string("yes.")
					: std::string("no.")));
		}

		void CaptureLocalTriggerUseCandidates(
			AAlicePlayerController* controller)
		{
			g_localTriggerUseCandidates.clear();
			if (!controller)
				return;
			TArray<ATrigger*> triggers;
			const float distance = controller->InteractDistance > 0.0f
				? controller->InteractDistance : 512.0f;
			// TriggerInteracted ultimately uses this same native helper.
			// A permissive dot threshold gives us every usable nearby
			// trigger; the nearest changed trigger is selected after Use.
			controller->GetTriggerUseList(
				distance, distance, -1.0f, true, triggers);
			AAlicePawn* pawn = GetLocalPawn();
			std::unordered_set<ATrigger*> seen;
			for (int32_t index = 0; index < triggers.size(); ++index)
			{
				ATrigger* trigger = triggers.at(index);
				if (!trigger || !IsLiveUObject(trigger)
					|| !seen.insert(trigger).second)
				{
					continue;
				}
				float distanceSquared = 0.0f;
				if (pawn)
				{
					const float x =
						trigger->Location.X - pawn->Location.X;
					const float y =
						trigger->Location.Y - pawn->Location.Y;
					const float z =
						trigger->Location.Z - pawn->Location.Z;
					distanceSquared = x * x + y * y + z * z;
				}
				g_localTriggerUseCandidates.push_back({
					trigger,
					trigger->TriggerTimes,
					trigger->bRecentlyTriggered != 0,
					trigger->bEnabled != 0,
					distanceSquared });
			}
			std::ostringstream stream;
			stream << "TRIGGERINTERACT candidates="
				<< g_localTriggerUseCandidates.size()
				<< ", interactDistance=" << distance;
			for (const TriggerUseObservation& candidate :
				g_localTriggerUseCandidates)
			{
				stream << " [" << ObjectName(candidate.trigger)
					<< ", times=" << candidate.triggerTimes
					<< ", recent="
					<< (candidate.recentlyTriggered ? 1 : 0)
					<< ", enabled="
					<< (candidate.enabled ? 1 : 0)
					<< ", dist="
					<< static_cast<int>(
						std::sqrt(candidate.distanceSquared))
					<< ']';
			}
			stream << '.';
			Log(stream.str());
		}

		void DetectLocalTriggerUse()
		{
			TriggerUseObservation* selected = nullptr;
			for (TriggerUseObservation& candidate :
				g_localTriggerUseCandidates)
			{
				ATrigger* trigger = candidate.trigger;
				if (!trigger || !IsLiveUObject(trigger))
					continue;
				const bool changed =
					trigger->TriggerTimes != candidate.triggerTimes
					|| (trigger->bRecentlyTriggered != 0)
						!= candidate.recentlyTriggered
					|| (trigger->bEnabled != 0)
						!= candidate.enabled;
				if (changed && (!selected
					|| candidate.distanceSquared
						< selected->distanceSquared))
				{
					selected = &candidate;
				}
			}
			std::string source = "state-change";
			if (!selected && !g_localTriggerUseCandidates.empty())
			{
				selected = &*std::min_element(
					g_localTriggerUseCandidates.begin(),
					g_localTriggerUseCandidates.end(),
					[](const TriggerUseObservation& left,
						const TriggerUseObservation& right)
					{
						return left.distanceSquared
							< right.distanceSquared;
					});
				source = g_localTriggerUseCandidates.size() == 1
					? "single-candidate"
					: "nearest-fallback";
			}
			if (selected)
				QueueTriggerInteraction(selected->trigger, source);
			else
				Log("TRIGGERINTERACT no usable trigger candidate.");
			g_localTriggerUseCandidates.clear();
		}

		SequenceOpObservation ObserveSequenceOp(USequenceOp* op)
		{
			SequenceOpObservation result{};
			if (!op)
				return result;
			result.activateCount = op->ActivateCount;
			result.active = op->bActive != 0;
			result.queuedActivations.reserve(op->InputLinks.size());
			result.inputImpulses.reserve(op->InputLinks.size());
			for (int32_t index = 0;
				index < op->InputLinks.size(); ++index)
			{
				const FSeqOpInputLink& input =
					op->InputLinks.at(index);
				result.queuedActivations.push_back(
					input.QueuedActivations);
				result.inputImpulses.push_back(
					input.bHasImpulse != 0);
			}
			return result;
		}

		std::string SequenceOpInputState(USequenceOp* op)
		{
			if (!op)
				return "<none>";
			std::ostringstream stream;
			bool wrote = false;
			for (int32_t index = 0;
				index < op->InputLinks.size(); ++index)
			{
				const FSeqOpInputLink& input =
					op->InputLinks.at(index);
				if (!input.bHasImpulse
					&& input.QueuedActivations <= 0)
				{
					continue;
				}
				if (wrote)
					stream << ',';
				wrote = true;
				stream << index << ':'
					<< input.LinkDesc.ToString()
					<< "(impulse="
					<< (input.bHasImpulse ? 1 : 0)
					<< ",queued="
					<< input.QueuedActivations << ')';
			}
			return wrote ? stream.str() : "<none>";
		}

		void CaptureSequenceOpUseSnapshot()
		{
			g_sequenceOpUseSnapshot.clear();
			g_sequenceOpUseLogged.clear();
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return;
			for (int32_t index = 0; index < objects->size(); ++index)
			{
				UObject* object = objects->at(index);
				if (!object
					|| !object->IsA(USequenceOp::StaticClass()))
				{
					continue;
				}
				auto* op = reinterpret_cast<USequenceOp*>(object);
				g_sequenceOpUseSnapshot.emplace(
					op, ObserveSequenceOp(op));
			}
			g_pendingSequenceOpUseTrace = true;
			g_sequenceOpUseTraceUntil =
				Clock::now() + std::chrono::seconds(7);
			g_nextSequenceOpUseScan = Clock::now();
			g_sequenceOpUseTraceCount = 0;
			Log("SEQUENCEUSE snapshot ops="
				+ std::to_string(
					g_sequenceOpUseSnapshot.size()) + '.');
		}

		void ScanSequenceOpUseChanges(bool finalAttempt)
		{
			if (!g_pendingSequenceOpUseTrace)
				return;
			const auto now = Clock::now();
			if (!finalAttempt && now < g_nextSequenceOpUseScan)
				return;
			g_nextSequenceOpUseScan =
				now + std::chrono::milliseconds(50);
			for (auto& [op, previous] :
				g_sequenceOpUseSnapshot)
			{
				if (!op || !IsLiveUObject(op))
					continue;
				const SequenceOpObservation current =
					ObserveSequenceOp(op);
				const bool changed =
					current.activateCount
						!= previous.activateCount
					|| current.active != previous.active
					|| current.queuedActivations
						!= previous.queuedActivations
					|| current.inputImpulses
						!= previous.inputImpulses;
				if (changed
					&& g_sequenceOpUseLogged.insert(op).second)
				{
					++g_sequenceOpUseTraceCount;
					std::string handler = "<none>";
					if (op->IsA(USequenceAction::StaticClass()))
					{
						handler = reinterpret_cast<
							USequenceAction*>(op)
								->HandlerName.ToString();
					}
					Log("SEQUENCEUSE change #"
						+ std::to_string(
							g_sequenceOpUseTraceCount)
						+ ", key="
						+ SharedWorldKeyText(
							SequenceActionStableKey(op))
						+ ", op=" + ObjectName(op)
						+ ", count="
						+ std::to_string(
							previous.activateCount)
						+ "->"
						+ std::to_string(
							current.activateCount)
						+ ", active="
						+ (previous.active
							? std::string("yes")
							: std::string("no"))
						+ "->"
						+ (current.active
							? std::string("yes")
							: std::string("no"))
						+ ", inputs="
						+ SequenceOpInputState(op)
						+ ", handler=" + handler
						+ ", parent="
						+ ObjectName(op->ParentSequence)
						+ '.');
				}
				previous = current;
			}
			if (finalAttempt)
			{
				Log("SEQUENCEUSE trace complete, changes="
					+ std::to_string(
						g_sequenceOpUseTraceCount) + '.');
				g_pendingSequenceOpUseTrace = false;
				g_sequenceOpUseSnapshot.clear();
				g_sequenceOpUseLogged.clear();
			}
		}

		void CaptureUsedEventSnapshot()
		{
			g_usedEventSnapshot.clear();
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return;
			for (int32_t index = 0; index < objects->size(); ++index)
			{
				UObject* object = objects->at(index);
				if (!object
					|| !object->IsA(USeqEvent_Used::StaticClass()))
				{
					continue;
				}
				auto* usedEvent =
					reinterpret_cast<USeqEvent_Used*>(object);
				g_usedEventSnapshot.emplace(
					usedEvent,
					UsedEventObservation{
						usedEvent->TriggerCount,
						usedEvent->ActivationTime,
						usedEvent->bActive != 0,
						usedEvent->bEnabled != 0 });
			}
			g_pendingUsedEventDetection = true;
			g_usedEventDetectionUntil =
				Clock::now() + std::chrono::seconds(2);
			Log("SHAREDINTERACT snapshot Used events="
				+ std::to_string(g_usedEventSnapshot.size()) + '.');
		}

		int DetectActivatedUsedEvents(bool finalAttempt)
		{
			if (!g_pendingUsedEventDetection)
				return 0;
			int detected = 0;
			for (const auto& [usedEvent, before] :
				g_usedEventSnapshot)
			{
				if (!usedEvent || !IsLiveUObject(usedEvent)
					|| !usedEvent->IsA(
						USeqEvent_Used::StaticClass()))
				{
					continue;
				}
				const bool triggerChanged =
					usedEvent->TriggerCount
						!= before.triggerCount;
				const bool activationChanged =
					std::fabs(usedEvent->ActivationTime
						- before.activationTime) > 0.0001f;
				const bool becameActive =
					usedEvent->bActive && !before.active;
				if (!triggerChanged && !activationChanged
					&& !becameActive)
				{
					continue;
				}
				const std::uint64_t key =
					SequenceActionStableKey(usedEvent);
				if (key == 0
					|| !g_localInteractionKeysThisAttempt
						.insert(key).second)
				{
					continue;
				}
				SharedWorldEventPayload event{};
				event.kind =
					SharedWorldEventKind::InteractionUsed;
				event.entityKey = key;
				event.originActorId =
					g_config.role == Role::Host ? 1u : 2u;
				event.flags = 0;
				QueueSharedWorldEvent(event);
				++detected;
				Log("SHAREDINTERACT TX detected Used key="
					+ SharedWorldKeyText(key)
					+ ", event=" + ObjectName(usedEvent)
					+ ", originator="
					+ ObjectName(usedEvent->Originator)
					+ ", triggers="
					+ std::to_string(before.triggerCount)
					+ "->"
					+ std::to_string(usedEvent->TriggerCount)
					+ ", activation="
					+ std::to_string(before.activationTime)
					+ "->"
					+ std::to_string(
						usedEvent->ActivationTime)
					+ ", active="
					+ (before.active ? std::string("yes")
						: std::string("no"))
					+ "->"
					+ (usedEvent->bActive
						? std::string("yes")
						: std::string("no"))
					+ ", enabled="
					+ (usedEvent->bEnabled
						? std::string("yes.")
						: std::string("no.")));
			}
			if (detected > 0 || finalAttempt)
			{
				if (detected == 0)
				Log("SHAREDINTERACT no Used state changed "
					"during local Use window.");
				g_pendingUsedEventDetection = false;
				g_usedEventSnapshot.clear();
			}
			return detected;
		}

		USeqAct_Interp* FindInteractionMatinee(
			std::uint64_t entityKey)
		{
			if (entityKey == 0)
				return nullptr;
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return nullptr;
			for (int32_t index = 0; index < objects->size(); ++index)
			{
				UObject* object = objects->at(index);
				if (object
					&& object->IsA(USeqAct_Interp::StaticClass())
					&& SequenceActionStableKey(object) == entityKey)
				{
					return reinterpret_cast<USeqAct_Interp*>(object);
				}
			}
			return nullptr;
		}

		struct SequenceSearchNode
		{
			USequenceOp* op = nullptr;
			int depth = 0;
			std::string path;
		};

		USeqEvent_Used* FindUpstreamUsedEvent(
			USequenceOp* target, int& outDepth,
			std::string& outPath)
		{
			outDepth = -1;
			outPath.clear();
			if (!target)
				return nullptr;

			std::deque<SequenceSearchNode> pending;
			std::unordered_set<USequenceOp*> visited;
			pending.push_back({
				target, 0, ObjectName(target) });
			visited.insert(target);

			while (!pending.empty())
			{
				SequenceSearchNode current =
					std::move(pending.front());
				pending.pop_front();
				if (!current.op || current.depth >= 32)
					continue;
				USequence* parent = current.op->ParentSequence;
				if (!parent)
					continue;

				for (int32_t inputIndex = 0;
					inputIndex < current.op->InputLinks.size();
					++inputIndex)
				{
					USequenceOp* candidate =
						current.op->InputLinks.at(
							inputIndex).LinkedOp;
					if (!candidate
						|| !visited.insert(candidate).second)
					{
						continue;
					}
					const std::string candidatePath =
						ObjectName(candidate) + " -["
						+ current.op->InputLinks.at(
							inputIndex).LinkDesc.ToString()
						+ "]-> " + current.path;
					if (candidate->IsA(
						USeqEvent_Used::StaticClass()))
					{
						outDepth = current.depth + 1;
						outPath = candidatePath;
						return reinterpret_cast<USeqEvent_Used*>(
							candidate);
					}
					pending.push_back({
						candidate,
						current.depth + 1,
						candidatePath });
				}

				for (int32_t objectIndex = 0;
					objectIndex < parent->SequenceObjects.size();
					++objectIndex)
				{
					USequenceObject* sequenceObject =
						parent->SequenceObjects.at(objectIndex);
					if (!sequenceObject
						|| !sequenceObject->IsA(
							USequenceOp::StaticClass()))
					{
						continue;
					}
					auto* candidate =
						reinterpret_cast<USequenceOp*>(
							sequenceObject);
					bool feedsCurrent = false;
					for (int32_t outputIndex = 0;
						outputIndex < candidate->OutputLinks.size()
							&& !feedsCurrent;
						++outputIndex)
					{
						const FSeqOpOutputLink& output =
							candidate->OutputLinks.at(outputIndex);
						if (output.LinkedOp == current.op)
							feedsCurrent = true;
						for (int32_t linkIndex = 0;
							linkIndex < output.Links.size()
								&& !feedsCurrent;
							++linkIndex)
						{
							if (output.Links.at(linkIndex).LinkedOp
								== current.op)
							{
								feedsCurrent = true;
							}
						}
					}
					if (!feedsCurrent
						|| !visited.insert(candidate).second)
					{
						continue;
					}

					const std::string candidatePath =
						ObjectName(candidate) + " -> "
						+ current.path;
					if (candidate->IsA(
						USeqEvent_Used::StaticClass()))
					{
						outDepth = current.depth + 1;
						outPath = candidatePath;
						return reinterpret_cast<USeqEvent_Used*>(
							candidate);
					}
					pending.push_back({
						candidate,
						current.depth + 1,
						candidatePath });
				}
			}
			return nullptr;
		}

		std::vector<AAliceVentActor*> FindVentTargets(
			USeqAct_SetVentState* action)
		{
			std::vector<AAliceVentActor*> targets;
			if (!action)
				return targets;

			TArray<UObject*> linkedObjects;
			action->GetObjectVarsWin(FString(), linkedObjects);
			for (int32_t index = 0; index < linkedObjects.size(); ++index)
			{
				UObject* object = linkedObjects.at(index);
				if (object
					&& object->IsA(AAliceVentActor::StaticClass()))
				{
					targets.push_back(
						reinterpret_cast<AAliceVentActor*>(object));
				}
			}
			if (!targets.empty())
				return targets;

			const std::string actionName = ObjectName(action);
			const std::string worldMarker =
				".TheWorld.PersistentLevel.";
			const std::size_t marker = actionName.find(worldMarker);
			const std::size_t actionPathStart =
				actionName.find(' ');
			const std::string levelPrefix =
				marker == std::string::npos
					|| actionPathStart == std::string::npos
					? std::string()
					: actionName.substr(
						actionPathStart + 1,
						marker - actionPathStart);
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects || levelPrefix.empty())
				return targets;
			for (int32_t index = 0; index < objects->size(); ++index)
			{
				UObject* object = objects->at(index);
				if (!object
					|| !object->IsA(AAliceVentActor::StaticClass()))
				{
					continue;
				}
				const std::string name = ObjectName(object);
				const std::size_t pathStart = name.find(' ');
				const std::string objectPath =
					pathStart == std::string::npos
						? name : name.substr(pathStart + 1);
				if (objectPath.rfind(levelPrefix, 0) == 0)
				{
					targets.push_back(
						reinterpret_cast<AAliceVentActor*>(object));
				}
			}
			return targets;
		}

		void ReleaseRemoteContextActorPresentation(const char* reason)
		{
			if (g_remoteContextInteraction.presentationReleased)
				return;
			g_remoteContextInteraction.presentationReleased = true;
			AContextActor* actor = g_remoteContextInteraction.actor;
			if (!actor || !IsLiveUObject(actor))
				return;

			actor->bInTriggerArea = false;
			actor->bContextActionStarted = false;
			actor->bIsBlendingPosition = false;
			actor->bIsBlendingRotation = false;
			AAlicePawn* localPawn = GetLocalPawn();
			if (actor->Alice
				&& actor->Alice->CurrentContextActor == actor)
			{
				actor->Alice->CurrentContextActor = nullptr;
			}
			if (localPawn
				&& localPawn->CurrentContextActor == actor)
			{
				localPawn->CurrentContextActor = nullptr;
			}
			actor->TriggerInteractiveUI();
			AAlicePlayerController* controller =
				actor->APC ? actor->APC
					: g_State.AlicePlayerController;
			if (controller)
			{
				const FString emptyText;
				// The game's own PinballCannon bytecode uses 0 to show
				// this prompt and -2 to dismiss it. Passing 0 here merely
				// refreshed the stale "press C" hint.
				controller->ShowContextActionUIHint(-2, emptyText);
			}
			Log("CONTEXTACTOR released key="
				+ SharedWorldKeyText(
					g_remoteContextInteraction.actorKey)
				+ ", actor=" + ObjectName(actor)
				+ ", reason=" + reason + '.');
		}

		void ClearConsumedContextActorUi(const char* reason)
		{
			AContextActor* actor = g_consumedContextUiActor;
			if (!actor || !IsLiveUObject(actor))
			{
				g_consumedContextUiActor = nullptr;
				g_consumedContextUiSawCinematic = false;
				g_consumedContextUiFallbackAt = {};
				return;
			}

			actor->bInTriggerArea = false;
			actor->bContextActionStarted = false;
			AAlicePawn* localPawn = GetLocalPawn();
			if (actor->Alice
				&& actor->Alice->CurrentContextActor == actor)
			{
				actor->Alice->CurrentContextActor = nullptr;
			}
			if (localPawn
				&& localPawn->CurrentContextActor == actor)
			{
				localPawn->CurrentContextActor = nullptr;
			}
			actor->TriggerInteractiveUI();
			AAlicePlayerController* controller =
				actor->APC ? actor->APC
					: g_State.AlicePlayerController;
			if (controller)
			{
				const FString emptyText;
				controller->ShowContextActionUIHint(-2, emptyText);
			}
			Log("CONTEXTACTOR post-cinematic UI cleared actor="
				+ ObjectName(actor) + ", reason=" + reason + '.');
			g_consumedContextUiActor = nullptr;
			g_consumedContextUiSawCinematic = false;
			g_consumedContextUiFallbackAt = {};
		}

		void ConsumeRemoteContextActor()
		{
			AContextActor* actor = g_remoteContextInteraction.actor;
			if (!actor || !IsLiveUObject(actor))
				return;

			const int triggerTimesBefore = actor->TriggerTimes;
			const bool enabledBefore = actor->bEnabled;
			if (g_remoteContextInteraction.disableActorAfterActivation)
			{
				const int triggerLimit =
					(std::max)(1, actor->MaxTriggerTimers);
				actor->TriggerTimes =
					(std::max)(actor->TriggerTimes, triggerLimit);
				actor->bEnabled = false;
			}
			g_consumedContextUiActor = actor;
			g_consumedContextUiSawCinematic = false;
			g_consumedContextUiFallbackAt =
				Clock::now() + std::chrono::seconds(2);
			ReleaseRemoteContextActorPresentation(
				"kismet-activation-consumed");
			Log("CONTEXTACTOR consumed actor="
				+ ObjectName(actor)
				+ ", maxTriggerTimers="
				+ std::to_string(actor->MaxTriggerTimers)
				+ ", triggerTimes="
				+ std::to_string(triggerTimesBefore)
				+ "->" + std::to_string(actor->TriggerTimes)
				+ ", enabled="
				+ (enabledBefore ? std::string("yes")
					: std::string("no"))
				+ "->"
				+ (actor->bEnabled ? std::string("yes.")
					: std::string("no.")));
		}

		void ResetRemoteContextInteraction(const char* reason)
		{
			ReleaseRemoteContextActorPresentation(reason);
			g_remoteContextInteraction = {};
		}

		bool ApplyReplicatedVentState(
			const SharedWorldEventPayload& event)
		{
			USeqAct_SetVentState* action =
				FindVentStateAction(event.entityKey);
			AAlicePlayerController* controller =
				g_State.AlicePlayerController;
			if (!action || !controller)
				return false;

			const int32_t inputIndex =
				action->InputLinks.empty()
					? 0
					: std::min<int32_t>(
						event.flags,
						action->InputLinks.size() - 1);
			g_suppressedVentActionKey = event.entityKey;
			g_suppressedVentActionUntil =
				Clock::now() + std::chrono::seconds(3);
			g_applyingSharedVentState = true;
			if (!action->InputLinks.empty())
			{
				action->InputLinks.at(inputIndex).bHasImpulse = true;
			}
			const int32_t targetsBefore = action->Targets.size();
			action->PopulateLinkedVariableValues();
			const int32_t targetsAfter = action->Targets.size();
			controller->OnSetVentState(action);
			const std::vector<AAliceVentActor*> ventTargets =
				FindVentTargets(action);
			int directUpdates = 0;
			for (AAliceVentActor* vent : ventTargets)
			{
				if (!vent || !IsLiveUObject(vent))
					continue;
				bool enable = inputIndex == 0;
				if (inputIndex >= 2)
					enable = !vent->isEnable();
				vent->setEnable(enable);
				vent->updateState();
				++directUpdates;
			}
			action->PublishLinkedVariableValues();
			const bool outputActivated =
				!action->OutputLinks.empty()
				&& NativeActivateOutputLink(action, 0);
			if (!action->InputLinks.empty())
			{
				action->InputLinks.at(inputIndex).bHasImpulse = false;
			}
			g_applyingSharedVentState = false;
			Log("SHAREDWORLD replayed vent handler key="
				+ SharedWorldKeyText(event.entityKey)
				+ ", action=" + ObjectName(action)
				+ ", inputs="
				+ std::to_string(action->InputLinks.size())
				+ ", selectedInput=" + std::to_string(inputIndex)
				+ (action->InputLinks.empty()
					? std::string()
					: ", inputName="
						+ action->InputLinks.at(
							inputIndex).LinkDesc.ToString())
				+ ", outputs="
				+ std::to_string(action->OutputLinks.size())
				+ ", targets=" + std::to_string(targetsBefore)
				+ "->" + std::to_string(targetsAfter)
				+ ", directVentUpdates="
				+ std::to_string(directUpdates)
				+ ", outputActivated="
				+ (outputActivated ? std::string("yes")
					: std::string("no"))
				+ ", origin="
				+ std::to_string(event.originActorId) + '.');
			return true;
		}

		void TickRemoteContextInteraction()
		{
			if (!g_remoteContextInteraction.active
				|| Clock::now()
					< g_remoteContextInteraction.deadline)
			{
				return;
			}

			const bool completed =
				g_remoteContextInteraction.localVentApplied;
			const std::optional<SharedWorldEventPayload> fallback =
				g_remoteContextInteraction.deferredVent;
			Log("CONTEXTACTOR lifecycle timeout key="
				+ SharedWorldKeyText(
					g_remoteContextInteraction.actorKey)
				+ ", localContext="
				+ (g_remoteContextInteraction.localContextActivated
					? std::string("yes") : std::string("no"))
				+ ", localMatinee="
				+ (g_remoteContextInteraction.localMatineeStarted
					? std::string("yes") : std::string("no"))
				+ ", localVent="
				+ (completed ? std::string("yes")
					: std::string("no"))
				+ ", fallback="
				+ (fallback.has_value() ? std::string("yes.")
					: std::string("no.")));
			ResetRemoteContextInteraction(
				completed ? "completed-grace-timeout"
					: "interaction-timeout");
			if (!completed && fallback.has_value())
				ApplyReplicatedVentState(*fallback);
		}

