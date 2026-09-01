		constexpr bool PepperProjectileReplicationEnabled = false;

		enum class HairRotationCandidate : std::uint8_t
		{
			ComponentWorld,
			ActorRotation,
			ComponentAndActor,
			ComponentRelative,
			TemplateNodes,
			Count
		};

		enum class VfxAttachmentCandidate : std::uint8_t
		{
			NativeWeapon = 0,
			WeaponTraceSocket,
			WeaponMuzzleSocket,
			WeaponFirstSocket,
			WeaponTraceWorld,
			AliceHolderSocket,
			AliceRootWorld,
			Count
		};

		struct SharedDevState
		{
			std::uint32_t magic = 0;
			std::uint32_t version = 0;
			volatile LONG revision = 0;
			volatile LONG vfxAttachmentCandidate = 0;
			volatile LONG commandRevision = 0;
			volatile LONG command = 0;
		};

		constexpr std::uint32_t SharedDevStateMagic = 0x504F4F43;
		constexpr std::uint32_t SharedDevStateVersion = 2;

		struct Config
		{
			bool enabled = false;
			bool showOnScreenStatus = false;
			bool visualProxy = true;
			bool localMirror = false;
			bool actionTrace = false;
			bool worldTrace = false;
			bool vfxLifecycleTrace = false;
			bool animationLifecycleTrace = false;
			bool animationComparisonTrace = false;
			bool controlLifecycleTrace = false;
			bool invariantTrace = false;
			bool preserveMovementTrails = false;
			bool sharedEnemyHealth = true;
			bool sharedEnemyTransforms = true;
			bool backgroundWindowDamageGuard = false;
			bool hairTuningEnabled = false;
			bool disableProxyCollision = true;
			bool forceWindowed = false;
			bool manageWindowGeometry = false;
			bool borderlessWindow = false;
			bool lowMemoryMode = true;
			Role role = Role::Unknown;
			std::string serverAddress = "127.0.0.1";
			std::uint16_t port = DefaultPort;
			int sendRateHz = 20;
			int peerTimeoutMs = 3000;
			float interpolationSpeed = 18.0f;
			float localMirrorDistance = 160.0f;
			float hairRotationX = 180.0f;
			float hairRotationY = 265.0f;
			float hairRotationZ = 0.0f;
			float hairOffsetX = -0.2f;
			float hairOffsetY = 0.0f;
			float hairOffsetZ = -2.2f;
			int actionTraceWindowMs = 2500;
			float worldTraceRadius = 10000.0f;
			float sharedEnemyRadius = 12000.0f;
			float sharedEnemyCorrectionSpeed = 22.0f;
			float sharedEnemySnapDistance = 1200.0f;
			int windowX = 0;
			int windowY = 0;
			int windowWidth = 960;
			int windowHeight = 540;
			int maxFps = 60;
			int maxPoolThreads = 4;
		};

		struct ClientCommandSnapshot
		{
			ClientCommandPayload command{};
			Clock::time_point receivedAt{};
		};

		struct HostWorldSnapshot
		{
			HostSnapshotPayload snapshot{};
			Clock::time_point receivedAt{};
		};

		struct ReceivedAnimationGraph
		{
			AnimationGraphPayload graph{};
			Clock::time_point receivedAt{};
		};

		struct ReceivedProjectileEvent
		{
			ProjectileEventPayload event{};
			Clock::time_point receivedAt{};
		};

		struct ReceivedSharedWorldEvent
		{
			SharedWorldEventPayload event{};
			Clock::time_point receivedAt{};
		};

		Config g_config;
		std::atomic_bool g_running = false;
		std::atomic_bool g_connected = false;
		std::atomic_uint32_t g_playerId = 0;
		std::atomic_uint32_t g_sessionId = 0;
		std::atomic_uint32_t g_sequence = 1;
		std::atomic_uint64_t g_lastRelayPacketMs = 0;
		std::atomic_bool g_peerTeardownRequested = false;
		bool g_peerWasFresh = false;
		SOCKET g_socket = INVALID_SOCKET;
		sockaddr_in g_serverEndpoint{};
		std::thread g_networkThread;
		std::mutex g_stateMutex;
		std::optional<ClientCommandPayload> g_outboundClientCommand;
		std::optional<HostSnapshotPayload> g_outboundHostSnapshot;
		std::optional<AnimationGraphPayload> g_outboundAnimationGraph;
		std::deque<ProjectileEventPayload> g_outboundProjectileEvents;
		std::deque<SharedWorldEventPayload>
			g_outboundSharedWorldEvents;
		std::optional<ClientCommandSnapshot> g_inboundClientCommand;
		std::optional<HostWorldSnapshot> g_inboundHostSnapshot;
		std::optional<ReceivedAnimationGraph> g_inboundAnimationGraph;
		std::deque<ReceivedProjectileEvent> g_inboundProjectileEvents;
		std::deque<ReceivedSharedWorldEvent>
			g_inboundSharedWorldEvents;

		struct SaveTransferFile
		{
			SaveSyncFileKind kind = SaveSyncFileKind::PersistentData;
			std::filesystem::path path;
			std::vector<std::uint8_t> bytes;
			std::array<std::uint8_t, 32> hash{};
			std::vector<std::uint8_t> chunks;
		};

		struct HostSaveTransfer
		{
			bool active = false;
			std::uint32_t transferId = 0;
			std::array<SaveTransferFile, SaveSyncFileCount> files{};
			std::array<std::vector<std::uint8_t>, SaveSyncFileCount> acked{};
			std::size_t nextFile = 0;
			std::size_t nextChunk = 0;
			Clock::time_point startedAt{};
			Clock::time_point nextManifestAt{};
			float completionPercent = 0.0f;
			float playedHours = 0.0f;
			float playedMinutes = 0.0f;
		};

		struct ClientSaveTransfer
		{
			bool active = false;
			std::uint32_t transferId = 0;
			std::array<SaveTransferFile, SaveSyncFileCount> files{};
			std::size_t totalChunks = 0;
			std::size_t receivedChunks = 0;
			Clock::time_point startedAt{};
			Clock::time_point nextRequestAt{};
			float completionPercent = 0.0f;
			float playedHours = 0.0f;
			float playedMinutes = 0.0f;
			std::array<std::filesystem::path, SaveSyncFileCount> targetPaths{};
			std::string targetProfileName;
		};

		HostSaveTransfer g_hostSaveTransfer;
		ClientSaveTransfer g_clientSaveTransfer;
		std::atomic_uint32_t g_requestedSaveTransferId{ 0 };
		std::atomic_bool g_saveSyncInProgress{ false };
		std::atomic_int g_saveSyncProgress{ 0 };
		std::mutex g_saveSyncStatusMutex;
		std::string g_saveSyncStatus;
		std::mutex g_observedSavePathMutex;
		std::array<std::filesystem::path, SaveSyncFileCount>
			g_observedSavePaths{};
		std::array<std::filesystem::path, SaveSyncFileCount>
			g_originalClientSavePaths{};
		std::array<std::filesystem::path, SaveSyncFileCount>
			g_requestedClientSavePaths{};
		std::string g_requestedClientProfileName;
		std::string g_activeSaveProfileName;
		std::array<float, 3> g_activeSaveProfileMetadata{};
		std::array<float, 3> g_pendingClientProfileMetadata{};
		std::string g_pendingClientProfileName;
		std::atomic_bool g_pendingClientProfileMetadataApply{ false };
		std::mutex g_logMutex;
		std::ofstream g_log;
		std::atomic_uint32_t g_localActionSerial = 0;
		std::atomic<std::uint8_t> g_localAction =
			static_cast<std::uint8_t>(PlayerAction::None);
		std::uint8_t g_lastLocalSpecialMove =
			static_cast<std::uint8_t>(ESpecialMove::SM_None);
		std::uint32_t g_commandNumber = 0;
		std::uint32_t g_snapshotNumber = 0;
		std::uint32_t g_worldEpoch = 0;
		std::uint32_t g_interpolationTraceCount = 0;
		std::uint32_t g_animationGraphFrameNumber = 0;
		std::atomic_uint64_t g_actionTraceWindowUntilMs = 0;
		std::uint32_t g_actionTraceMarker = 0;
		std::uint32_t g_actionTraceEventSerial = 0;
		Clock::time_point g_controlTraceUntil{};
		Clock::time_point g_nextControlTraceSample{};
		std::string g_lastControlTraceSignature;
		std::mutex g_invariantTraceMutex;
		std::unordered_set<std::string> g_reportedInvariantViolations;
		AAlicePawn* g_tracedPawn = nullptr;
		AWeapon* g_tracedWeapon = nullptr;
		std::string g_lastTracePawnState;
		std::string g_lastTraceWeaponState;
		std::string g_lastTraceBodyAnimations;
		std::string g_lastTraceUpperBodyAnimations;
		std::string g_lastTraceWeaponAnimations;
		std::atomic_bool g_traceObjectLabelsReady = false;
		std::unordered_map<UObject*, std::string> g_traceObjectLabels;
		std::unordered_map<std::string, std::uint64_t> g_traceEventTimes;
		Clock::time_point g_nextTraceSample{};
		Clock::time_point g_nextBackgroundDamageGuardLog{};

		AAlicePawn* g_remotePawn = nullptr;
		AController* g_remoteController = nullptr;
		AWorldInfo* g_remoteWorld = nullptr;
		ASkeletalMeshActor* g_remoteHairProxy = nullptr;
		UHairComponent* g_remoteIndependentHair = nullptr;
		float g_remoteHairProxyBaseDrawScale = 1.0f;
		float g_remoteHairSkeletalBaseScale = 1.0f;
		float g_remoteIndependentHairBaseScale = 1.0f;
		bool g_remoteHairNeedsReset = false;
		float g_remoteHairBaseLengthScale = 1.0f;
		float g_remoteHairBaseStrandWidth = 1.0f;
		UHair* g_remoteHairBindTemplate = nullptr;
		FVector g_remoteHairRootCentroid =
			FVector(0.0f, 0.0f, 0.0f);
		int32_t g_remoteHairRootCount = 0;
		FMatrix g_remoteHairBasisCorrection{};
		bool g_hasRemoteHairBasisCorrection = false;
		UHair* g_remoteHairRotatedTemplate = nullptr;
		UHair* g_remoteHairTuningTemplate = nullptr;
		std::vector<FVector> g_remoteHairBaseNodes;
		HairRotationCandidate g_hairRotationCandidate =
			HairRotationCandidate::ComponentAndActor;
		std::array<bool, 14> g_hairTuningKeyDown{};
		bool g_tuningOverlayVisible = true;
		bool g_loggedTuningOverlayRender = false;
		AWeaponForAlice* g_remotePresentationWeapon = nullptr;
		std::array<AWeaponForAlice*, 5> g_remoteWeaponCache{};
		std::uint8_t g_remotePresentationWeaponType =
			static_cast<std::uint8_t>(EAliceWeaponType::EAWT_None);
		std::uint8_t g_remotePresentationWeaponLevel = 1;
		std::uint8_t g_remotePresentationWeaponVariant = 0;
		FName g_remotePresentationWeaponSocket;
		std::string g_remoteAppliedWeaponAnimation;
		Clock::time_point g_remoteWeaponAnimationLastSeen{};
		Clock::time_point g_nextRemoteWeaponSpawnAttempt{};
		bool g_remoteMuzzleFlashActive = false;
		UParticleSystemComponent* g_remoteMuzzleParticle = nullptr;
		Clock::time_point g_remoteMuzzleLastActiveRequest{};
		bool g_remoteDodgeVisualHidden = false;
		bool g_remoteGlideVfxActive = false;
		bool g_remoteAirResetPending = false;
		std::atomic<bool> g_pauseMenuEventOpen{ false };
		std::atomic<std::uint64_t> g_pauseMenuEventSerial{ 0 };
		bool g_pauseMenuEscapeWasDown = false;
		bool g_pauseMenuTrailsKeyWasDown = false;
		std::uint64_t g_pauseMenuLastObservedSerial = 0;
		bool g_lastPausePanelVisible = false;
		UParticleSystemComponent* g_remoteGlideParticle = nullptr;
		Clock::time_point g_remoteGlideInactiveSince{};
		struct RemoteNativeGlideTrail
		{
			UParticleSystemComponent* component = nullptr;
			UParticleSystem* particleTemplate = nullptr;
			AWorldInfo* world = nullptr;
			Clock::time_point firstSeen{};
		};
		std::vector<RemoteNativeGlideTrail> g_remoteNativeGlideTrails;
		UParticleSystemComponent* g_remoteNativeGlideCurrent = nullptr;
		Clock::time_point g_remoteNativeGlideCurrentSince{};
		Clock::time_point g_nextRemoteNativeGlideSweep{};
		std::vector<UStaticMesh*> g_remoteLeafTrailMeshes;
		struct RemoteFrozenLeafAsset
		{
			UStaticMesh* mesh = nullptr;
			UMaterialInterface* material = nullptr;
			bool ownsMaterialCopy = false;
		};
		std::vector<RemoteFrozenLeafAsset> g_remoteFrozenLeafAssets;
		std::vector<RemoteFrozenLeafAsset> g_remoteFrozenAccentAssets;
		std::deque<ADynamicSMActor_Spawnable*> g_remoteLeafTrailMarkers;
		std::deque<ADynamicSMActor_Spawnable*> g_remoteAccentTrailMarkers;
		Clock::time_point g_nextRemoteLeafTrailMarker{};
		std::uint32_t g_remoteLeafTrailRandom = 0xA11CE123u;
		std::uint32_t g_remoteLeafTrailSample = 0;
		bool g_remoteClockBombAnimationActive = false;
		struct LocalClockBombTrack
		{
			AAliceClonePawn* bomb = nullptr;
			std::uint32_t projectileId = 0;
			bool exploded = false;
			std::uint8_t cloneState = 0;
			FVector lastLocation = FVector(0.0f, 0.0f, 0.0f);
			FVector lastVelocity = FVector(0.0f, 0.0f, 0.0f);
			FRotator lastRotation = FRotator(0, 0, 0);
			Clock::time_point nextUpdate{};
		} g_localClockBomb;
		struct RemotePepperVisual
		{
			std::uint32_t projectileId = 0;
			UParticleSystemComponent* particle = nullptr;
			ADynamicSMActor_Spawnable* marker = nullptr;
			FVector location = FVector(0.0f, 0.0f, 0.0f);
			FVector velocity = FVector(0.0f, 0.0f, 0.0f);
			FRotator rotation = FRotator(0, 0, 0);
			float scale = 1.0f;
			std::uint8_t hypothesis = 1;
			Clock::time_point lastTick{};
			Clock::time_point expiresAt{};
		};
		struct RemoteClockBombVisual
		{
			std::uint32_t projectileId = 0;
			ASkeletalMeshActorSpawnable* actor = nullptr;
			FVector targetLocation = FVector(0.0f, 0.0f, 0.0f);
			FVector velocity = FVector(0.0f, 0.0f, 0.0f);
			FRotator targetRotation = FRotator(0, 0, 0);
			std::uint8_t cloneState = 0;
			std::uint8_t lastForcedCloneState = 255;
			std::uint8_t hypothesis = 1;
			std::uint16_t animationNodeCount = 0;
			Clock::time_point lastTick{};
			Clock::time_point lastUpdate{};
			Clock::time_point spawnedAt{};
		};
		std::vector<RemotePepperVisual> g_remotePepperVisuals;
		std::unordered_map<std::uint32_t, RemoteClockBombVisual>
			g_remoteClockBombVisuals;
		std::atomic_uint32_t g_localProjectileSerial = 1;
		std::atomic_uint32_t g_localProjectileId = 1;
		std::unordered_map<
			APepperGrinderPrimaryProjectile*, Clock::time_point>
			g_recentLocalPepperProjectiles;
		UParticleSystem* g_pepperFlightTemplate = nullptr;
		UStaticMesh* g_pepperMarkerMesh = nullptr;
		UParticleSystem* g_clockBombExplosionTemplate = nullptr;
		USkeletalMeshComponent* g_clockBombMeshTemplate = nullptr;
		int g_pepperProjectileHypothesis = 2;
		int g_clockBombHypothesis = 3;
		constexpr float ClockBombZOffset = -138.0f;
		bool g_devForceProxyHidden = false;
		Clock::time_point g_devDodgeHideUntil{};
		Clock::time_point g_devMuzzleUntil{};
		Clock::time_point g_devGlideUntil{};
		UParticleSystem* g_remoteDodgeParticleTemplate = nullptr;
		Clock::time_point g_remoteDodgeParticleUntil{};
		Clock::time_point g_nextRemoteDodgeParticle{};
		bool g_remoteAttackTrailActive = false;
		Clock::time_point g_remoteAttackTrailUntil{};
		UParticleSystemComponent* g_remoteAttackTrailParticle = nullptr;
		std::vector<UParticleSystemComponent*>
			g_remoteWeaponLoopParticles;
		AWeaponForAlice* g_lastLoggedLocalWeapon = nullptr;
		std::string g_lastRemoteVfx = "none";
		VfxAttachmentCandidate g_vfxAttachmentCandidate =
			VfxAttachmentCandidate::WeaponTraceSocket;
		HANDLE g_sharedDevMapping = nullptr;
		SharedDevState* g_sharedDevState = nullptr;
		LONG g_sharedDevRevision = 0;
		LONG g_sharedDevCommandRevision = 0;
		bool g_applyingSharedDevCommand = false;
		float g_remoteWeaponBaseDrawScale = 1.0f;
		std::unordered_map<std::uint64_t, Clock::time_point>
			g_recentRemoteVisualNotifies;
		struct VisualNotifyCursor
		{
			UAnimNodeSequence* node = nullptr;
			UAnimSequence* sequence = nullptr;
			float previousTime = 0.0f;
			bool primed = false;
		};
		struct TrackedPresentationParticle
		{
			UParticleSystemComponent* component = nullptr;
			UParticleSystem* particleTemplate = nullptr;
			Clock::time_point expiresAt{};
		};
		struct RetiredPresentationParticle
		{
			UParticleSystemComponent* component = nullptr;
			UParticleSystem* particleTemplate = nullptr;
			Clock::time_point enforceUntil{};
			Clock::time_point nextNativeHideAt{};
		};
		struct RemoteMovementParticleCapture
		{
			bool active = false;
			AAliceGamePawn* remote = nullptr;
			std::string sequenceName;
			Clock::time_point captureUntil{};
			std::unordered_map<UParticleSystemComponent*, UParticleSystem*>
				baseline;
		};
		struct PendingMovementParticlePreservation
		{
			UParticleSystemComponent* component = nullptr;
			UParticleSystem* particleTemplate = nullptr;
			Clock::time_point preserveAt{};
		};
		struct VfxLifecycleAudit
		{
			UParticleSystemComponent* component = nullptr;
			UParticleSystem* particleTemplate = nullptr;
			Clock::time_point retainUntil{};
			Clock::time_point nextSample{};
			std::uint32_t sample = 0;
			bool retired = false;
		};
		VisualNotifyCursor g_remoteFullBodyNotifyCursor;
		std::vector<TrackedPresentationParticle>
			g_remoteWeaponTransientParticles;
		std::vector<RetiredPresentationParticle>
			g_retiredPresentationParticles;
		std::vector<TrackedPresentationParticle>
			g_remoteMovementTransientParticles;
		std::vector<PendingMovementParticlePreservation>
			g_pendingMovementParticlePreservations;
		RemoteMovementParticleCapture g_remoteMovementParticleCapture;
		std::unordered_map<UParticleSystemComponent*, VfxLifecycleAudit>
			g_vfxLifecycleAudits;
		float g_remoteBaseDrawScale = 1.0f;
		bool g_remoteShrinkApplied = false;
		bool g_remoteHidden = false;
		bool g_remoteRenderDetached = false;
		bool g_loggedPostTickPresentation = false;
		bool g_loggedPostEngineSkeletonRefresh = false;
		bool g_loggedPresentedCosmetics = false;
		bool g_loggedDelayedCosmetics = false;
		bool g_remoteHairOwnerFallbackApplied = false;
		bool g_loggedHairMeshCandidates = false;
		Clock::time_point g_remoteCosmeticDiagnosticAt{};
		Clock::time_point g_nextCosmeticSpatialSample{};
		int g_cosmeticSpatialSamplesRemaining = 0;
		int g_cosmeticSpatialSampleNumber = 0;
		bool g_hasLocalAnimationSignature = false;
		std::string g_lastLocalAnimationSignature;
		struct RemotePresentation
		{
			bool valid = false;
			PlayerStatePayload state{};
			FVector location = FVector(0.0f, 0.0f, 0.0f);
			FRotator rotation = FRotator(0, 0, 0);
			bool hidden = false;
		} g_remotePresentation;
		struct RetiredRemotePawn
		{
			AAlicePawn* pawn = nullptr;
			AWorldInfo* world = nullptr;
			Clock::time_point nextDestroyAttempt{};
			int attempts = 0;
		};
		std::vector<RetiredRemotePawn> g_retiredRemotePawns;
		std::optional<ReceivedAnimationGraph> g_activeRemoteAnimationGraph;
		std::unordered_set<std::uint32_t> g_remoteGraphSequences;
		std::uint32_t g_lastAppliedAnimationGraphFrame = 0;
		bool g_loggedRemoteAnimationGraph = false;
		std::string g_lastRemoteAnimationGraphSignature;
		UAnimNodeSequence* g_remoteAppliedFullBodyNode = nullptr;
		UAliceGameAnimNode_BlendBySlot* g_remoteAppliedFullBodySlot = nullptr;
		std::string g_remoteAppliedFullBodyName;
		std::string g_lastLocalAnimationCompareSignature;
		std::string g_lastRemoteAnimationCompareSignature;
		Clock::time_point g_nextLocalAnimationCompareSample{};
		Clock::time_point g_nextRemoteAnimationCompareSample{};
		struct PresentationAnimChannel
		{
			FAnimationParaConfig config{};
			bool configInitialized = false;
			bool active = false;
			bool standalone = false;
			int32_t blendNodeIndex = -1;
			int32_t targetChild = 0;
			int32_t sourceChild = 0;
			std::string sequenceName;
			float lastSourcePosition = 0.0f;
			float lastSourceRate = 1.0f;
			Clock::time_point lastSourceSeen{};
		};
		PresentationAnimChannel g_remoteFullBodyChannel;
		PresentationAnimChannel g_remoteUpperAdditiveChannel;
		std::unordered_set<std::string> g_loggedAnimationStageFailures;
		std::unordered_set<std::string> g_loggedAnimationStageSuccesses;
		std::unordered_set<std::string> g_loggedConfigAnimationStages;
		std::unordered_map<std::string, int> g_animationStagePoseSamples;
		std::unordered_set<std::string>
			g_loggedWeaponHierarchySignatures;
		std::unordered_set<const UAnimSequence*>
			g_loggedNotifyInventories;
		bool g_loggedLocalMirrorBinding = false;
		bool g_loggedLocalMirrorIncompatibility = false;
		std::uint32_t g_lastRemoteActionSerial = 0;
		std::uint8_t g_lastRemoteSpecialMove =
			static_cast<std::uint8_t>(ESpecialMove::SM_None);
		bool g_loggedMissingSpecialMoves = false;
		AWorldInfo* g_currentWorld = nullptr;
		std::string g_currentMap;
		std::string g_mapBeforeWorldChange;
		bool g_soloMinigameActive = false;
		std::string g_soloMinigameLevel;
		Clock::time_point g_nextSoloMinigameDetection{};
		Clock::time_point g_nextMapRefresh{};
		Clock::time_point g_nextStatusDraw{};
		Clock::time_point g_nextSpawnAttempt{};
		Clock::time_point g_nextWindowConfigure{};
		Clock::time_point g_nextWorldTraceScan{};
		AWorldInfo* g_worldTraceWorld = nullptr;
		std::string g_worldTraceMap;
		struct WorldTraceRecord
		{
			std::string key;
			std::string kind;
			std::string objectName;
			std::string signature;
		};
		std::unordered_map<const UObject*, WorldTraceRecord>
			g_worldTraceRecords;
		std::unordered_map<const UClass*, std::uint8_t>
			g_worldTraceClassCache;
		std::unordered_map<const UFunction*, bool>
			g_worldTraceFunctionCache;
		std::array<int, 4> g_worldTraceLastCounts{
			-1, -1, -1, -1 };
		std::uint32_t g_worldTraceEventSerial = 0;
		std::uint32_t g_sharedWorldEventSerial = 0;
		bool g_applyingSharedEnemyDamage = false;
		struct PendingEnemyDamage
		{
			std::int32_t damage = 0;
			FVector hitLocation = FVector(0.0f, 0.0f, 0.0f);
			FVector momentum = FVector(0.0f, 0.0f, 0.0f);
			std::string damageType;
		};
		std::unordered_map<const UObject*, PendingEnemyDamage>
			g_pendingEnemyDamage;
		std::unordered_set<std::uint64_t>
			g_seenSharedWorldEvents;
		std::deque<std::uint64_t> g_seenSharedWorldEventOrder;
		std::unordered_map<std::uint64_t,
			std::deque<SharedWorldEventPayload>>
			g_deferredSharedDamageRequests;
		std::unordered_map<std::uint64_t,
			SharedWorldEventPayload>
			g_pendingAuthoritativeEnemyStates;
		std::unordered_map<std::uint64_t, std::uint32_t>
			g_lastAuthoritativeEnemyStateSerial;
		Clock::time_point g_nextDeferredSharedWorldApply{};
		std::unordered_map<const UFunction*, bool>
			g_sharedDamageFunctionCache;
		bool g_applyingSharedBreakableDestroy = false;
		std::unordered_set<std::uint64_t>
			g_sentSharedBreakableKeys;
		struct SharedEnemyPoseBinding
		{
			AAliceGameKynapsePawn* enemy = nullptr;
			bool originalPauseTick = false;
			bool originalHidden = false;
			bool authorized = false;
			bool clientAuthority = false;
			Clock::time_point lastSeen{};
		};
		AWorldInfo* g_sharedEnemyPoseWorld = nullptr;
		std::string g_sharedEnemyPoseMap;
		Clock::time_point g_nextSharedEnemyRegistryRefresh{};
		std::unordered_map<std::uint64_t, AAliceGameKynapsePawn*>
			g_sharedEnemyRegistry;
		std::unordered_map<AAliceGameKynapsePawn*, std::uint64_t>
			g_sharedEnemyStableKeys;
		std::unordered_map<AAliceGameKynapsePawn*, std::uint64_t>
			g_clientEnemyHostKeyByActor;
		std::unordered_map<std::uint64_t, SharedEnemyPoseBinding>
			g_clientSharedEnemyBindings;
		std::unordered_set<std::uint64_t>
			g_hostAuthorizedEnemyKeys;
		std::unordered_set<std::uint64_t>
			g_loggedUnauthorizedEnemyDamage;
		struct HostEnemyAggroState
		{
			bool initialized = false;
			bool targetClient = false;
			float hostThreat = 0.0f;
			float clientThreat = 0.0f;
			int hostHitCombo = 0;
			int clientHitCombo = 0;
			Clock::time_point lastHostHit{};
			Clock::time_point lastClientHit{};
			Clock::time_point forceHostUntil{};
			Clock::time_point forceClientUntil{};
			Clock::time_point lockedUntil{};
			Clock::time_point lastClientVisible{};
			Clock::time_point lastThreatDecay{};
			Clock::time_point nextNativeFollowRefresh{};
			AAlicePawn* clientTarget = nullptr;
			bool controllerPauseCaptured = false;
			bool originalControllerPause = false;
		};
		std::unordered_map<std::uint64_t, HostEnemyAggroState>
			g_hostEnemyAggro;
		Clock::time_point g_nextHostEnemyAggroUpdate{};
		bool g_clientEnemyAuthorityActive = false;
		bool g_applyingSharedPlayerDamage = false;
		Clock::time_point g_nextSuppressedClientDamageLog{};
		int g_hostAggroHostTargets = 0;
		int g_hostAggroClientTargets = 0;
		int g_lastSharedEnemyRegistryCount = -1;
		int g_lastSharedEnemySnapshotCount = -1;
		std::size_t g_clientQuarantinedEnemyCount = 0;
		Clock::time_point g_clientOrphanedEncounterSince{};
		Clock::time_point g_nextHostCutsceneRecoveryAttempt{};
		std::uint64_t g_lastMissingHostCutsceneKey = 0;
		std::uint64_t g_lastAutomaticCutsceneRecoveryKey = 0;
		Clock::time_point g_nextSharedEnemyPoseSummary{};
		float g_sharedEnemyPoseMaxCorrection = 0.0f;
		std::uint32_t g_sharedEnemyNativeMoves = 0;
		std::uint32_t g_sharedEnemyNativeMoveFailures = 0;
		int g_lastLoggedLocalAliceHealth =
			(std::numeric_limits<int>::min)();
		bool g_cutsceneTraceInitialized = false;
		bool g_lastLocalCinematic = false;
		bool g_lastPeerCinematic = false;
		USeqAct_Interp* g_waitingCutsceneAction = nullptr;
		std::uint64_t g_waitingCutsceneKey = 0;
		std::uint64_t g_cutsceneBarrierAdvertiseKey = 0;
		std::uint64_t g_recentReleasedCutsceneKey = 0;
		Clock::time_point g_cutsceneBarrierStartedAt{};
		Clock::time_point g_cutsceneBarrierAdvertiseUntil{};
		Clock::time_point g_recentReleasedCutsceneUntil{};
		float g_waitingCutsceneOriginalPlayRate = 1.0f;
		bool g_waitingCutscenePlayRateOverridden = false;
		bool g_waitingCutsceneCanDeferActivation = false;
		bool g_waitingCutsceneActivationDeferred = false;
		bool g_replayingDeferredCutsceneActivation = false;
		std::uint64_t g_emergencyCutsceneAdvertiseKey = 0;
		Clock::time_point g_emergencyCutsceneAdvertiseUntil{};
		std::uint64_t g_lastEmergencyCutsceneKey = 0;
		Clock::time_point g_lastEmergencyCutsceneUntil{};
		USeqAct_Interp* g_emergencyForcedCutsceneAction = nullptr;
		Clock::time_point g_emergencyForcedCutsceneRequestedAt{};
		bool g_emergencyForcedCutsceneStarted = false;
		AWorldInfo* g_hostCutsceneBlindWorld = nullptr;
		bool g_hostCutsceneOriginalNpcBlind = false;
		std::unordered_map<AAliceGameKynapseAIController*, bool>
			g_hostCutsceneControllerPause;
		std::unordered_map<AAliceGameKynapsePawn*, FVector>
			g_hostCutsceneEnemyAnchors;
		bool g_teleportKeyDown = false;
		std::atomic_bool g_teleportInputRequested = false;
		bool g_forceTeleportKeyDown = false;
		std::atomic_bool g_forceTeleportInputRequested = false;
		std::uint32_t g_hostCommandSerial = 0;
		std::uint32_t g_lastSeenHostCommandSerial = 0;
		std::uint32_t g_hostCommandFlags = 0;
		Clock::time_point g_hostCommandExpires{};
		std::string g_lastTeleportStatus;
		Clock::time_point g_lastTeleportStatusUntil{};
		bool g_groupDeathActive = false;
		bool g_forcingGroupDeath = false;
		bool g_localGroupDeathForced = false;
		bool g_localRestartInitiated = false;
		bool g_clientRestartRequestPending = false;
		bool g_hostRestartIssued = false;
		bool g_localReturnToMenuInitiated = false;
		bool g_clientReturnToMenuRequestPending = false;
		bool g_hostReturnToMenuIssued = false;
		Clock::time_point g_groupDeathSuppressUntil{};
		bool g_joinHostPending = false;
		std::string g_joinHostStatus;
		std::string g_joinHostEligibilityMap;
		std::uint32_t g_joinHostEligibilityEpoch = 0;
		Clock::time_point g_joinHostEligibleSince{};
		Clock::time_point g_joinHostRetryUntil{};
		Clock::time_point g_nextJoinHostRetry{};
		Clock::time_point g_joinHostLoadInvokedAt{};
		int g_joinHostAttemptCount = 0;
		int g_hostRequestedChapter = -1;
		Clock::time_point g_nextHostProgressionApply{};
		bool g_applyingHostProgression = false;
		AAlicePawn* g_rangeProgressionPawn = nullptr;
		std::string g_loggedInvalidZeroProgressionMap;
		AAlicePlayerController* g_cachedAbilityController = nullptr;
		int32_t g_cachedAbilityCount = -1;
		std::uint32_t g_cachedAbilityMask = 0;
		Clock::time_point g_nextAbilityMaskScan{};
		std::string g_lastLoadedChapterProbeMap;
		int g_lastLoadedChapterProbe = -1;
		bool g_applyingSharedVentState = false;
		bool g_applyingSharedInteraction = false;
		Clock::time_point g_localInteractionWindowUntil{};
		Clock::time_point g_interactionCutsceneBypassUntil{};
		std::uint32_t g_localInteractionAttemptSerial = 0;
		std::unordered_set<std::uint64_t>
			g_localInteractionKeysThisAttempt;
		std::uint32_t g_interactionCandidateTraceCount = 0;
		bool g_applyingSharedInteractionMatinee = false;
		bool g_applyingSharedLondonInteraction = false;
		bool g_applyingSharedTriggerInteraction = false;
		bool g_applyingSharedContextAction = false;
		struct RemoteContextInteractionState
		{
			AContextActor* actor = nullptr;
			std::uint64_t actorKey = 0;
			bool active = false;
			bool localContextActivated = false;
			bool localMatineeStarted = false;
			bool localVentApplied = false;
			bool disableActorAfterActivation = false;
			bool presentationReleased = false;
			Clock::time_point deadline{};
			std::optional<SharedWorldEventPayload> deferredVent;
		};
		RemoteContextInteractionState g_remoteContextInteraction;
		AContextActor* g_consumedContextUiActor = nullptr;
		bool g_consumedContextUiSawCinematic = false;
		Clock::time_point g_consumedContextUiFallbackAt{};
		struct ContextActorObservation
		{
			AAlicePawn* alice = nullptr;
			AAlicePlayerController* controller = nullptr;
			bool started = false;
			bool inTriggerArea = false;
			bool blendingPosition = false;
			bool blendingRotation = false;
		};
		std::unordered_map<AContextActor*, ContextActorObservation>
			g_contextActorUseSnapshot;
		bool g_pendingContextActorUseDetection = false;
		Clock::time_point g_contextActorUseDetectionUntil{};
		struct TriggerUseObservation
		{
			ATrigger* trigger = nullptr;
			int triggerTimes = 0;
			bool recentlyTriggered = false;
			bool enabled = false;
			float distanceSquared = 0.0f;
		};
		std::vector<TriggerUseObservation>
			g_localTriggerUseCandidates;
		struct SequenceOpObservation
		{
			int activateCount = 0;
			bool active = false;
			std::vector<int> queuedActivations;
			std::vector<bool> inputImpulses;
		};
		std::unordered_map<USequenceOp*, SequenceOpObservation>
			g_sequenceOpUseSnapshot;
		std::unordered_set<USequenceOp*>
			g_sequenceOpUseLogged;
		bool g_pendingSequenceOpUseTrace = false;
		Clock::time_point g_sequenceOpUseTraceUntil{};
		Clock::time_point g_nextSequenceOpUseScan{};
		int g_sequenceOpUseTraceCount = 0;
		struct UsedEventObservation
		{
			int triggerCount = 0;
			float activationTime = 0.0f;
			bool active = false;
			bool enabled = false;
		};
		std::unordered_map<USeqEvent_Used*, UsedEventObservation>
			g_usedEventSnapshot;
		bool g_pendingUsedEventDetection = false;
		Clock::time_point g_usedEventDetectionUntil{};
		std::uint64_t g_suppressedVentActionKey = 0;
		Clock::time_point g_suppressedVentActionUntil{};
		bool g_applyingNetworkRestart = false;
		bool g_applyingNetworkReturnToMenu = false;
		std::uint64_t g_pendingLocalVentActionKey = 0;
		std::uint8_t g_pendingLocalVentInputIndex = 0;
