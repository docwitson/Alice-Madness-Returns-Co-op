		const char* VfxAttachmentCandidateName(
			VfxAttachmentCandidate candidate);
		UParticleSystem* FindLoadedParticleSystem(
			const std::vector<std::string>& requiredTokens,
			const std::vector<std::string>& preferredTokens);
		UParticleSystemComponent* SpawnWeaponParticleCandidate(
			AWeaponForAlice* weapon, UParticleSystem* particle,
			VfxAttachmentCandidate candidate);
		FName ResolveCandidateWeaponSocket(
			AWeaponForAlice* weapon,
			VfxAttachmentCandidate candidate);
		bool WeaponMeshHasSocket(AWeaponForAlice* weapon,
			const FName& name);
		bool WeaponMeshHasBone(AWeaponForAlice* weapon,
			const FName& name);
		FName FirstWeaponSocket(AWeaponForAlice* weapon);
		void MakePresentationParticleVisible(
			UParticleSystemComponent* component);
		void EnableRemoteAnimationTick(AAlicePawn* remote);
		std::string ObjectName(const UObject* object);
		void ClearRemoteProjectileVisuals(bool destroyActors);
		void DestroyRemoteStaticLeafTrail();
		void RetireTrackedRemoteNativeGlideTrails(
			AWorldInfo* world, UParticleSystemComponent* keep,
			const char* reason);
		void TickRemoteNativeGlideTrailGuard(AAlicePawn* remote);
		void TickLocalClockBombReplication();
		void TickRemoteProjectileVisuals(AWorldInfo* world);
		void ApplyInboundProjectileEvents(AWorldInfo* world);
		void ApplyInboundSharedWorldEvents(
			AAlicePawn* localPawn, AWorldInfo* world);
		void TickWorldTrace(AAlicePawn* localPawn, AWorldInfo* world);
		void ResetSharedEnemyPose(bool restoreClientAi);
		std::uint16_t SharedEnemyClassSignature(
			const AAliceGameKynapsePawn* enemy);
		void ForgetTornDownWorldObjects();
		void DestroyRemotePawn();
		void RequestHostCheckpointRestart(const char* source);
		void RequestClientCheckpointRestart(const char* source);
		void RequestHostReturnToMenu(const char* source);
		void RequestClientReturnToMenu(const char* source);
		bool ApplyPendingLifecycleCommand();
		bool TriggerLocalCheckpointRestart(const char* source);
		bool TriggerLocalReturnToMenu(const char* source);
		std::string SharedWorldKeyText(std::uint64_t key);
		void QueueSharedWorldEvent(SharedWorldEventPayload event);
		bool NativeSetActorLocationNoCheck(
			AActor* actor, const FVector& location);
		bool NativeSetActorRotation(
			AActor* actor, const FRotator& rotation);
		bool NativeForceUpdateComponents(AActor* actor,
			bool collisionUpdate, bool transformOnly);
		bool NativeSetActorHidden(AActor* actor, bool hidden);
		AActor* NativeSpawn(AActor* context, UClass* spawnClass,
			AActor* owner, const FName& tag,
			const FVector& location, const FRotator& rotation,
			AActor* actorTemplate, bool noCollisionFail);
		bool NativeCheckSequenceEvent(USequenceEvent* event,
			AActor* originator, AActor* instigator, bool testOnly,
			bool pushTop, TArray<int32_t>& activateIndices);
		bool NativeActivateOutputLink(USequenceOp* op,
			int32_t outputIndex);
		bool NativeLoadChapter(
			ACheckPointManager* manager,
			EChapterNameList chapter);
		bool NativeGetLastLoadedChapter(
			ACheckPointManager* manager,
			std::uint8_t& chapter);
		bool IsEmergencyForcedCutsceneActive();
