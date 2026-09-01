#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <bcrypt.h>

#include "Common.hpp"
#include "Coop/CoopClient.hpp"
#include "Coop/Protocol.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <deque>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace AliceCoop
{
	void ReleaseCutsceneBarrier(const char* reason);

	namespace
	{
		using namespace AliceCoopProtocol;
		using Clock = std::chrono::steady_clock;
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

		#include "Coop/Detail/RuntimeAndAnimationCapture.inl"
		#include "Coop/Detail/TransportAndSaveSync.inl"
		#include "Coop/Detail/WorldAndProgression.inl"
		#include "Coop/Detail/InteractionDiscovery.inl"
		#include "Coop/Detail/SharedWorldAndEnemies.inl"
		#include "Coop/Detail/EngineInterop.inl"
		#include "Coop/Detail/Diagnostics.inl"
		bool AttachRigidPresentationCosmetic(
			USkeletalMeshComponent* body,
			USkeletalMeshComponent* cosmetic,
			bool headAnchor, const char* label)
		{
			if (!body || !cosmetic || !cosmetic->SkeletalMesh)
				return false;

			FName anchorName;
			const int32_t anchorIndex = headAnchor
				? FindPresentationAnchorBone(
					body, true, anchorName)
				: FindPresentationAnchorBone(
					body, false, anchorName);
			FMatrix boneMatrix{};
			if (anchorIndex < 0
				|| !NativeGetBoneMatrix(
					body, anchorIndex, boneMatrix))
			{
				Log(std::string("COSMETICSTAGE rigid attach ")
					+ label + " failed: anchor unavailable.");
				return false;
			}

			FMatrix inverseBoneMatrix{};
			if (!InvertAffineMatrix(
				boneMatrix, inverseBoneMatrix))
			{
				Log(std::string("COSMETICSTAGE rigid attach ")
					+ label
					+ " failed: bone transform is singular.");
				return false;
			}

			// Alice's cloth and fallback scalp meshes are authored in the
			// character's root coordinate system. Preserve the body's complete
			// basis instead of applying bone-local Euler corrections.
			FMatrix desiredWorld = body->LocalToWorld;
			if (headAnchor)
			{
				// Bind the authored scalp centre to the head bone. Once this
				// world-space bind pose is converted to a bone-relative
				// transform below, UE3's native attachment updates both its
				// position and orientation with every head animation.
				FMatrix orientationOnly = desiredWorld;
				orientationOnly.WPlane.X = 0.0f;
				orientationOnly.WPlane.Y = 0.0f;
				orientationOnly.WPlane.Z = 0.0f;
				const FVector scalpFromComponent =
					TransformPoint(
						cosmetic->SkeletalMesh->Bounds.Origin,
						orientationOnly);
				desiredWorld.WPlane.X =
					boneMatrix.WPlane.X - scalpFromComponent.X;
				desiredWorld.WPlane.Y =
					boneMatrix.WPlane.Y - scalpFromComponent.Y;
				desiredWorld.WPlane.Z =
					boneMatrix.WPlane.Z - scalpFromComponent.Z;

				// The head bone origin sits below the visual crown centre.
				// Keep the constraint itself on Bip01-Head and add this
				// root-space bind offset, which will subsequently rotate and
				// translate with the head through the native attachment.
				constexpr float ScalpUpOffset = 17.0f;
				desiredWorld.WPlane.X +=
					desiredWorld.ZPlane.X * ScalpUpOffset;
				desiredWorld.WPlane.Y +=
					desiredWorld.ZPlane.Y * ScalpUpOffset;
				desiredWorld.WPlane.Z +=
					desiredWorld.ZPlane.Z * ScalpUpOffset;
			}

			// UE3 uses row-vector matrices. ComponentWorld *
			// inverse(BoneWorld) produces the attachment transform in
			// bone space. Euler subtraction cannot represent the same
			// composition and was rotating the dress into the ground.
			const FMatrix relativeMatrix = MultiplyMatrices(
				desiredWorld, inverseBoneMatrix);
			const FVector relativeLocation(
				relativeMatrix.WPlane.X,
				relativeMatrix.WPlane.Y,
				relativeMatrix.WPlane.Z);
			FRotator relativeRotation;
			if (!NativeMatrixGetRotator(
				relativeMatrix, relativeRotation))
			{
				Log(std::string("COSMETICSTAGE rigid attach ")
					+ label
					+ " failed: relative rotation unavailable.");
				return false;
			}
			const FVector relativeScale(
				FVector(relativeMatrix.XPlane.X,
					relativeMatrix.XPlane.Y,
					relativeMatrix.XPlane.Z).Size(),
				FVector(relativeMatrix.YPlane.X,
					relativeMatrix.YPlane.Y,
					relativeMatrix.YPlane.Z).Size(),
				FVector(relativeMatrix.ZPlane.X,
					relativeMatrix.ZPlane.Y,
					relativeMatrix.ZPlane.Z).Size());
			const FMatrix recomposedMatrix = MultiplyMatrices(
				relativeMatrix, boneMatrix);
			const FVector originalOrigin(
				desiredWorld.WPlane.X,
				desiredWorld.WPlane.Y,
				desiredWorld.WPlane.Z);
			const FVector recomposedOrigin(
				recomposedMatrix.WPlane.X,
				recomposedMatrix.WPlane.Y,
				recomposedMatrix.WPlane.Z);
			const float originError = FVector::Dist(
				originalOrigin, recomposedOrigin);
			NativeSetParentAnimComponent(cosmetic, nullptr);
			cosmetic->bTransformFromAnimParent = false;
			NativeDetachComponent(body, cosmetic);
			const bool attached = NativeAttachComponentToBone(
				body, cosmetic, anchorName,
				relativeLocation, relativeRotation,
				relativeScale);
			cosmetic->bAlwaysUpdateMeshObject = true;
			cosmetic->bForceMeshObjectUpdate = true;
			cosmetic->bUpdateSkelWhenNotRendered = true;
			cosmetic->bTickAnimNodesWhenNotRendered = true;
			cosmetic->Bounds = body->Bounds;

			std::ostringstream stream;
			stream << "COSMETICSTAGE rigid attach " << label
				<< ": result=" << (attached ? "yes" : "no")
				<< ", bone=" << anchorName.ToString()
				<< ", index=" << anchorIndex
				<< ", relative=("
				<< relativeLocation.X << ','
				<< relativeLocation.Y << ','
				<< relativeLocation.Z << ";rot="
				<< relativeRotation.Pitch << ','
				<< relativeRotation.Yaw << ','
				<< relativeRotation.Roll << ";scale="
				<< relativeScale.X << ','
				<< relativeScale.Y << ','
				<< relativeScale.Z
				<< ";matrixError=" << originError << ")"
				<< ", worldBasis="
				<< (headAnchor
					? "body-scalp-center-head-plus-up17"
					: "body-root-bound-to-pelvis")
				<< ", attachedParent="
				<< ObjectName(cosmetic->AttachedToSkelComponent)
				<< '.';
			Log(stream.str());
			return attached;
		}

		void ReattachProxyCosmetics(AAlicePawn* remote)
		{
			if (!remote)
				return;

			if (remote->HairComponent)
			{
				// The native strand renderer always resolves the local
				// playable Alice. Resetting this second component can therefore
				// add another head of strands to player one. Keep it dormant;
				// SpawnRemoteHairProxy creates the ordinary skeletal fallback.
				remote->HairComponent->OverrideMesh = remote->Mesh;
				remote->HairComponent->bOwnerNoSee = false;
				remote->HairComponent->bOnlyOwnerSee = false;
				remote->HairComponent->bJustAttached = false;
				remote->HairComponent->bPendingReset = false;
				remote->HairComponent->HiddenGame = true;
				NativeSetComponentHidden(
					remote->HairComponent, true);
			}

			auto reattachCloth = [remote](UClothComponent* cloth)
			{
				if (!cloth || !cloth->SkeletalMesh || !remote->Mesh)
					return;

				const char* label =
					cloth == remote->SkirtComponent
						? "skirt"
						: (cloth == remote->BowComponent
							? "bow"
							: (cloth == remote->RibbonComponent
								? "ribbon" : "ear"));
				if (cloth->Owner != remote || !cloth->bAttached)
					NativeAttachComponent(remote, cloth);
				// Alice's custom cloth simulator resolves the playable pawn
				// globally and therefore skins a second Alice against the
				// first one's bones. A presentation proxy must use ordinary
				// UE3 master-pose skinning instead. This deliberately trades
				// secondary cloth physics for a stable dress on the right body.
				NativeDeleteClothSimulator(cloth);
				cloth->bEnableClothSimulation = false;
				cloth->bClothFrozen = true;
				cloth->bPendingReset = false;
				cloth->bJustAttached = false;
				NativeReattachComponent(remote, cloth);
				cloth->bOwnerNoSee = false;
				cloth->bOnlyOwnerSee = false;
				cloth->bPauseAnims = false;
				cloth->bNoSkeletonUpdate = false;
				cloth->bUpdateSkelWhenNotRendered = true;
				cloth->bTickAnimNodesWhenNotRendered = true;
				NativeForceSkelUpdate(cloth);
				NativeForceComponentUpdate(cloth, false);
				AttachRigidPresentationCosmetic(
					remote->Mesh, cloth, false, label);
				cloth->Bounds = remote->Mesh->Bounds;
			};

			reattachCloth(remote->SkirtComponent);
			reattachCloth(remote->BowComponent);
			reattachCloth(remote->RibbonComponent);
			reattachCloth(remote->EarComponent);
			NativeForceUpdateComponents(remote, false, false);
		}

		bool CaptureNativeHairBasisCorrection(AAlicePawn* source)
		{
			g_hasRemoteHairBasisCorrection = false;
			if (!source || !source->Mesh || !source->HairComponent)
				return false;

			FMatrix hairOrientation =
				source->HairComponent->LocalToWorld;
			FMatrix bodyOrientation =
				source->Mesh->LocalToWorld;
			hairOrientation.WPlane.X = 0.0f;
			hairOrientation.WPlane.Y = 0.0f;
			hairOrientation.WPlane.Z = 0.0f;
			bodyOrientation.WPlane.X = 0.0f;
			bodyOrientation.WPlane.Y = 0.0f;
			bodyOrientation.WPlane.Z = 0.0f;
			FMatrix inverseBody;
			if (!InvertAffineMatrix(
				bodyOrientation, inverseBody))
			{
				Log("COSMETICSTAGE native hair basis unavailable: "
					"body orientation is singular.");
				return false;
			}

			g_remoteHairBasisCorrection = MultiplyMatrices(
				hairOrientation, inverseBody);
			g_remoteHairBasisCorrection.WPlane.X = 0.0f;
			g_remoteHairBasisCorrection.WPlane.Y = 0.0f;
			g_remoteHairBasisCorrection.WPlane.Z = 0.0f;
			g_hasRemoteHairBasisCorrection = true;
			Log("COSMETICSTAGE native hair basis captured: hair={"
				+ DescribeSpatialFrame(hairOrientation)
				+ "}, body={" + DescribeSpatialFrame(bodyOrientation)
				+ "}, correction={"
				+ DescribeSpatialFrame(
					g_remoteHairBasisCorrection) + "}.");
			return true;
		}

		bool PrepareIndependentHairTemplate(UHair* hairTemplate)
		{
			if (!hairTemplate)
				return false;
			if (g_remoteHairBindTemplate != hairTemplate)
			{
				g_remoteHairBindTemplate = hairTemplate;
				g_remoteHairRootCentroid =
					FVector(0.0f, 0.0f, 0.0f);
				g_remoteHairRootCount = 0;
				for (int32_t index = 0;
					index < hairTemplate->Strands.size(); ++index)
				{
					const FStrand& strand =
						hairTemplate->Strands.at(index);
					if (strand.NodeCount <= 0
						|| strand.StartNodeIndex < 0
						|| strand.StartNodeIndex
							>= hairTemplate->Nodes.size())
					{
						continue;
					}
					g_remoteHairRootCentroid +=
						hairTemplate->Nodes.at(
							strand.StartNodeIndex).Position;
					++g_remoteHairRootCount;
				}
				if (g_remoteHairRootCount > 0)
				{
					g_remoteHairRootCentroid =
						g_remoteHairRootCentroid
						/ static_cast<float>(
							g_remoteHairRootCount);
				}

				AAlicePawn* localPawn = GetLocalPawn();
				UHair* localHairTemplate =
					localPawn && localPawn->HairComponent
						? localPawn->HairComponent->Template
						: nullptr;
				const bool independentTemplate =
					hairTemplate != localHairTemplate;
				bool nodesRotated = false;
				FVector firstNodeBefore(0.0f, 0.0f, 0.0f);
				FVector firstNodeAfter(0.0f, 0.0f, 0.0f);
				if (hairTemplate->Nodes.size() > 0)
					firstNodeBefore =
						hairTemplate->Nodes.at(0).Position;
				if (independentTemplate
					&& g_hasRemoteHairBasisCorrection
					&& g_remoteHairRotatedTemplate
						!= hairTemplate)
				{
					for (int32_t index = 0;
						index < hairTemplate->Nodes.size();
						++index)
					{
						FNode& node =
							hairTemplate->Nodes.at(index);
						const FVector relative =
							node.Position
							- g_remoteHairRootCentroid;
						node.Position =
							TransformPoint(relative,
								g_remoteHairBasisCorrection)
							+ g_remoteHairRootCentroid;
					}
					g_remoteHairRotatedTemplate =
						hairTemplate;
					nodesRotated = true;
				}
				if (independentTemplate)
				{
					if (g_remoteHairTuningTemplate != hairTemplate
						|| g_remoteHairBaseNodes.size()
							!= static_cast<std::size_t>(
								hairTemplate->Nodes.size()))
					{
						g_remoteHairTuningTemplate = hairTemplate;
						g_remoteHairBaseNodes.clear();
						g_remoteHairBaseNodes.reserve(
							hairTemplate->Nodes.size());
						for (int32_t index = 0;
							index < hairTemplate->Nodes.size();
							++index)
						{
							g_remoteHairBaseNodes.push_back(
								hairTemplate->Nodes.at(
									index).Position);
						}
					}
					const bool tuneTemplate =
						g_hairRotationCandidate
							== HairRotationCandidate::TemplateNodes;
					const FMatrix manualRotation =
						RotationMatrixDegrees(
							g_config.hairRotationX,
							g_config.hairRotationY,
							g_config.hairRotationZ);
					for (int32_t index = 0;
						index < hairTemplate->Nodes.size();
						++index)
					{
						const FVector base =
							g_remoteHairBaseNodes[
								static_cast<std::size_t>(index)];
						const FVector relative =
							base - g_remoteHairRootCentroid;
						hairTemplate->Nodes.at(index).Position =
							tuneTemplate
								? (TransformPoint(
									relative, manualRotation)
									+ g_remoteHairRootCentroid)
								: base;
					}
				}
				if (hairTemplate->Nodes.size() > 0)
					firstNodeAfter =
						hairTemplate->Nodes.at(0).Position;

				const FBox& box =
					hairTemplate->FixedRelativeBoundingBox;
				std::ostringstream stream;
				stream << "COSMETICSTAGE hair bind data: roots="
					<< g_remoteHairRootCount
					<< ", centroid=("
					<< g_remoteHairRootCentroid.X << ','
					<< g_remoteHairRootCentroid.Y << ','
					<< g_remoteHairRootCentroid.Z << ')'
					<< ", fixedBox=("
					<< box.Min.X << ',' << box.Min.Y << ','
					<< box.Min.Z << ")-("
					<< box.Max.X << ',' << box.Max.Y << ','
					<< box.Max.Z << "), correction="
					<< (g_hasRemoteHairBasisCorrection
						? "native-hair-to-body" : "identity")
					<< ", manualRotationXYZ=("
					<< g_config.hairRotationX << ','
					<< g_config.hairRotationY << ','
					<< g_config.hairRotationZ << ')'
					<< ", runtimeCandidate="
					<< static_cast<int>(
						g_hairRotationCandidate)
					<< ", independentTemplate="
					<< (independentTemplate ? "yes" : "no")
					<< ", nodesRotated="
					<< (nodesRotated ? "yes" : "no")
					<< ", firstNode=("
					<< firstNodeBefore.X << ','
					<< firstNodeBefore.Y << ','
					<< firstNodeBefore.Z << ")->("
					<< firstNodeAfter.X << ','
					<< firstNodeAfter.Y << ','
					<< firstNodeAfter.Z << ").";
				Log(stream.str());
			}
			return g_remoteHairRootCount > 0;
		}

		bool AlignIndependentHairActorToScalp(
			ASkeletalMeshActor* actor, UHairComponent* hair,
			USkeletalMeshComponent* scalp)
		{
			if (!actor || !hair || !hair->Template
				|| !scalp || !scalp->SkeletalMesh)
				return false;
			if (!PrepareIndependentHairTemplate(hair->Template))
				return false;
			const FMatrix scalpFrame = scalp->LocalToWorld;
			const FMatrix manualRotation = RotationMatrixDegrees(
				g_config.hairRotationX,
				g_config.hairRotationY,
				g_config.hairRotationZ);
			const bool tuneComponentWorld =
				g_hairRotationCandidate
					== HairRotationCandidate::ComponentWorld
				|| g_hairRotationCandidate
					== HairRotationCandidate::ComponentAndActor;
			const bool tuneActor =
				g_hairRotationCandidate
					== HairRotationCandidate::ActorRotation
				|| g_hairRotationCandidate
					== HairRotationCandidate::ComponentAndActor;

			// Keep the known-good head bind as the base. The live calibrator
			// can insert the same local-space rotation at several candidate
			// layers so we can identify which one the custom renderer consumes.
			FMatrix hairWorld = tuneComponentWorld
				? MultiplyMatrices(manualRotation, scalpFrame)
				: scalpFrame;
			const FVector scalpCentre = TransformPoint(
				scalp->SkeletalMesh->Bounds.Origin,
				scalpFrame);
			FMatrix orientationOnly = hairWorld;
			orientationOnly.WPlane.X = 0.0f;
			orientationOnly.WPlane.Y = 0.0f;
			orientationOnly.WPlane.Z = 0.0f;
			const FVector rootOffset = TransformPoint(
				g_remoteHairRootCentroid, orientationOnly);
			hairWorld.WPlane.X =
				scalpCentre.X - rootOffset.X;
			hairWorld.WPlane.Y =
				scalpCentre.Y - rootOffset.Y;
			hairWorld.WPlane.Z =
				scalpCentre.Z - rootOffset.Z;
			FMatrix scalpOrientation = scalpFrame;
			scalpOrientation.WPlane.X = 0.0f;
			scalpOrientation.WPlane.Y = 0.0f;
			scalpOrientation.WPlane.Z = 0.0f;
			const FVector tuningOffset = TransformPoint(
				FVector(g_config.hairOffsetX,
					g_config.hairOffsetY,
					g_config.hairOffsetZ),
				scalpOrientation);
			hairWorld.WPlane.X += tuningOffset.X;
			hairWorld.WPlane.Y += tuningOffset.Y;
			hairWorld.WPlane.Z += tuningOffset.Z;
			const FMatrix actorFrame = tuneActor
				? MultiplyMatrices(manualRotation, scalpFrame)
				: scalpFrame;
			FRotator actorRotation;
			if (!NativeMatrixGetRotator(actorFrame, actorRotation))
				return false;
			FRotator relativeRotation(0, 0, 0);
			if (g_hairRotationCandidate
				== HairRotationCandidate::ComponentRelative
				&& !NativeMatrixGetRotator(
					manualRotation, relativeRotation))
			{
				return false;
			}

			const FVector hairLocation(
				hairWorld.WPlane.X,
				hairWorld.WPlane.Y,
				hairWorld.WPlane.Z);
			// Direct actor location plus per-frame LocalToWorld is the only
			// transform path this custom UE3 hair renderer actually consumes.
			// The selected candidate determines where the tuning rotation is
			// injected without disturbing the translation bind.
			actor->Location = hairLocation;
			actor->Rotation = actorRotation;
			// The custom hair renderer does not reliably consume scale from
			// the manually supplied LocalToWorld matrix. Mirror the proxy
			// actor scale explicitly so shrink affects strands as well.
			if (g_remotePawn)
			{
				actor->SetDrawScale(g_remotePawn->DrawScale);
				actor->DrawScale = g_remotePawn->DrawScale;
				actor->SetDrawScale3D(g_remotePawn->DrawScale3D);
				actor->DrawScale3D = g_remotePawn->DrawScale3D;
				hair->LengthScale =
					g_remoteHairBaseLengthScale
					* g_remotePawn->DrawScale;
				hair->StrandWidth =
					g_remoteHairBaseStrandWidth
					* g_remotePawn->DrawScale;
			}
			hair->Rotation = relativeRotation;
			hair->LocalToWorld = hairWorld;
			if (g_loggedConfigAnimationStages.insert(
				"independent-hair-node-transform").second)
			{
				std::ostringstream stream;
				stream << "COSMETICSTAGE independent hair node "
					"transform active: frameRotation=("
					<< actorRotation.Pitch << ','
					<< actorRotation.Yaw << ','
					<< actorRotation.Roll << ')'
					<< ", appliedBasis={"
					<< DescribeSpatialFrame(hairWorld) << "}.";
				Log(stream.str());
			}
			return true;
		}

		const char* HairRotationCandidateName(
			HairRotationCandidate candidate)
		{
			switch (candidate)
			{
			case HairRotationCandidate::ComponentWorld:
				return "ComponentWorld";
			case HairRotationCandidate::ActorRotation:
				return "ActorRotation";
			case HairRotationCandidate::ComponentAndActor:
				return "Component+Actor";
			case HairRotationCandidate::ComponentRelative:
				return "ComponentRelative";
			case HairRotationCandidate::TemplateNodes:
				return "TemplateNodes+Reset";
			default:
				return "Unknown";
			}
		}

		const wchar_t* HairRotationCandidateWideName(
			HairRotationCandidate candidate)
		{
			switch (candidate)
			{
			case HairRotationCandidate::ComponentWorld:
				return L"ComponentWorld";
			case HairRotationCandidate::ActorRotation:
				return L"ActorRotation";
			case HairRotationCandidate::ComponentAndActor:
				return L"Component+Actor";
			case HairRotationCandidate::ComponentRelative:
				return L"ComponentRelative";
			case HairRotationCandidate::TemplateNodes:
				return L"TemplateNodes+Reset";
			default:
				return L"Unknown";
			}
		}

		void ApplyRuntimeHairTemplateRotation()
		{
			UHair* hairTemplate = g_remoteHairTuningTemplate;
			if (!hairTemplate
				|| g_remoteHairBaseNodes.size()
					!= static_cast<std::size_t>(
						hairTemplate->Nodes.size()))
			{
				return;
			}

			const bool enabled =
				g_hairRotationCandidate
					== HairRotationCandidate::TemplateNodes;
			const FMatrix rotation = RotationMatrixDegrees(
				g_config.hairRotationX,
				g_config.hairRotationY,
				g_config.hairRotationZ);
			for (int32_t index = 0;
				index < hairTemplate->Nodes.size(); ++index)
			{
				const FVector base =
					g_remoteHairBaseNodes[
						static_cast<std::size_t>(index)];
				const FVector relative =
					base - g_remoteHairRootCentroid;
				hairTemplate->Nodes.at(index).Position =
					enabled
						? (TransformPoint(relative, rotation)
							+ g_remoteHairRootCentroid)
						: base;
			}

			bool reset = false;
			if (g_remoteIndependentHair)
			{
				g_remoteIndependentHair->bJustAttached = true;
				g_remoteIndependentHair->bPendingReset = true;
				reset = NativeResetHair(g_remoteIndependentHair);
				NativeForceComponentUpdate(
					g_remoteIndependentHair, false);
			}
			Log(std::string("HAIRTUNE template ")
				+ (enabled ? "rotation applied" : "restored")
				+ ", reset=" + (reset ? "yes" : "no") + '.');
		}

		bool HairTuningKeyPressed(int virtualKey,
			std::size_t keyIndex)
		{
			const bool down =
				(GetAsyncKeyState(virtualKey) & 0x8000) != 0;
			const bool pressed =
				down && !g_hairTuningKeyDown[keyIndex];
			g_hairTuningKeyDown[keyIndex] = down;
			return pressed;
		}

		void HandleHairRotationTuningInput()
		{
			if (!g_config.hairTuningEnabled)
				return;
			bool candidateChanged = false;
			bool rotationChanged = false;
			bool positionChanged = false;
			bool overlayChanged = false;
			if (HairTuningKeyPressed(VK_OEM_4, 6))
			{
				const auto next = static_cast<std::uint8_t>(
					g_hairRotationCandidate) + 1;
				g_hairRotationCandidate =
					static_cast<HairRotationCandidate>(
						next % static_cast<std::uint8_t>(
							HairRotationCandidate::Count));
				candidateChanged = true;
			}

			constexpr float StepDegrees = 5.0f;
			if (HairTuningKeyPressed('O', 0))
			{
				g_config.hairRotationX -= StepDegrees;
				rotationChanged = true;
			}
			if (HairTuningKeyPressed('P', 1))
			{
				g_config.hairRotationX += StepDegrees;
				rotationChanged = true;
			}
			if (HairTuningKeyPressed('L', 2))
			{
				g_config.hairRotationY -= StepDegrees;
				rotationChanged = true;
			}
			if (HairTuningKeyPressed(VK_OEM_1, 3))
			{
				g_config.hairRotationY += StepDegrees;
				rotationChanged = true;
			}
			if (HairTuningKeyPressed(VK_OEM_COMMA, 4))
			{
				g_config.hairRotationZ -= StepDegrees;
				rotationChanged = true;
			}
			if (HairTuningKeyPressed(VK_OEM_PERIOD, 5))
			{
				g_config.hairRotationZ += StepDegrees;
				rotationChanged = true;
			}
			constexpr float PositionStep = 0.2f;
			if (HairTuningKeyPressed('U', 7))
			{
				g_config.hairOffsetX -= PositionStep;
				positionChanged = true;
			}
			if (HairTuningKeyPressed('I', 8))
			{
				g_config.hairOffsetX += PositionStep;
				positionChanged = true;
			}
			if (HairTuningKeyPressed('J', 9))
			{
				g_config.hairOffsetY -= PositionStep;
				positionChanged = true;
			}
			if (HairTuningKeyPressed('K', 10))
			{
				g_config.hairOffsetY += PositionStep;
				positionChanged = true;
			}
			if (HairTuningKeyPressed('N', 11))
			{
				g_config.hairOffsetZ -= PositionStep;
				positionChanged = true;
			}
			if (HairTuningKeyPressed('M', 12))
			{
				g_config.hairOffsetZ += PositionStep;
				positionChanged = true;
			}
			if (HairTuningKeyPressed(VK_OEM_6, 13))
			{
				g_tuningOverlayVisible =
					!g_tuningOverlayVisible;
				overlayChanged = true;
			}

			if (!candidateChanged && !rotationChanged
				&& !positionChanged && !overlayChanged)
				return;
			if (candidateChanged || rotationChanged)
				ApplyRuntimeHairTemplateRotation();
			std::ostringstream stream;
			stream << "HAIRTUNE candidate="
				<< HairRotationCandidateName(
					g_hairRotationCandidate)
				<< ", XYZ=(" << g_config.hairRotationX << ','
				<< g_config.hairRotationY << ','
				<< g_config.hairRotationZ << "), offset=("
				<< g_config.hairOffsetX << ','
				<< g_config.hairOffsetY << ','
				<< g_config.hairOffsetZ << "), overlay="
				<< (g_tuningOverlayVisible ? "visible" : "hidden")
				<< '.';
			Log(stream.str());
		}

		void DestroyRemoteHairProxy()
		{
			ASkeletalMeshActor* proxy = g_remoteHairProxy;
			if (!proxy)
				return;
			if (!g_remoteWorld || g_remoteWorld != g_currentWorld)
			{
				g_remoteHairProxy = nullptr;
				g_remoteIndependentHair = nullptr;
				g_remoteHairProxyBaseDrawScale = 1.0f;
				g_remoteHairSkeletalBaseScale = 1.0f;
				g_remoteIndependentHairBaseScale = 1.0f;
				g_remoteHairNeedsReset = false;
				g_remoteHairBaseLengthScale = 1.0f;
				g_remoteHairBaseStrandWidth = 1.0f;
				g_remoteHairBindTemplate = nullptr;
				g_remoteHairRootCount = 0;
				g_hasRemoteHairBasisCorrection = false;
				g_remoteHairRotatedTemplate = nullptr;
				g_remoteHairTuningTemplate = nullptr;
				g_remoteHairBaseNodes.clear();
				return;
			}
			const std::string proxyName = ObjectName(proxy);
			if (g_remoteIndependentHair)
			{
				g_remoteIndependentHair->OverrideMesh = nullptr;
				g_remoteIndependentHair->bJustAttached = false;
				g_remoteIndependentHair->bPendingReset = false;
				g_remoteIndependentHair->HiddenGame = true;
				NativeSetComponentHidden(
					g_remoteIndependentHair, true);
			}
			if (proxy->SkeletalMeshComponent)
			{
				if (g_remotePawn && g_remotePawn->HairComponent
					&& g_remotePawn->HairComponent->OverrideMesh
						== proxy->SkeletalMeshComponent)
				{
					g_remotePawn->HairComponent->OverrideMesh = nullptr;
					g_remotePawn->HairComponent->HiddenGame = true;
					NativeSetComponentHidden(
						g_remotePawn->HairComponent, true);
				}
				NativeSetComponentHidden(
					proxy->SkeletalMeshComponent, true);
				if (g_remotePawn && g_remotePawn->Mesh
					&& proxy->SkeletalMeshComponent
						->ParentAnimComponent == g_remotePawn->Mesh)
				{
					NativeSetParentAnimComponent(
						proxy->SkeletalMeshComponent, nullptr);
				}
				if (g_remotePawn && g_remotePawn->Mesh
					&& proxy->SkeletalMeshComponent
						->AttachedToSkelComponent
							== g_remotePawn->Mesh)
				{
					NativeDetachComponent(
						g_remotePawn->Mesh,
						proxy->SkeletalMeshComponent);
				}
			}
			NativeSetActorHidden(proxy, true);
			proxy->bNoDelete = false;
			proxy->bStatic = false;
			const bool destroyed = NativeDestroyActor(proxy);
			Log("COSMETICSTAGE hair proxy destroy actor="
				+ proxyName + ", result="
				+ (destroyed ? std::string("destroyed.")
					: std::string("hidden-only.")));
			g_remoteHairProxy = nullptr;
			g_remoteIndependentHair = nullptr;
			g_remoteHairProxyBaseDrawScale = 1.0f;
			g_remoteHairSkeletalBaseScale = 1.0f;
			g_remoteIndependentHairBaseScale = 1.0f;
			g_remoteHairNeedsReset = false;
			g_remoteHairBaseLengthScale = 1.0f;
			g_remoteHairBaseStrandWidth = 1.0f;
			g_remoteHairBindTemplate = nullptr;
			g_remoteHairRootCount = 0;
			g_hasRemoteHairBasisCorrection = false;
		}

		void SpawnRemoteHairProxy(AAlicePawn* remote)
		{
			DestroyRemoteHairProxy();
			if (!remote || !remote->Mesh || !remote->HairComponent
				|| !remote->HairComponent->Template
				|| !remote->HairComponent->Template->SkeletalMesh)
			{
				Log("COSMETICSTAGE skeletal hair proxy unavailable.");
				return;
			}

			USkeletalMesh* hairMesh =
				remote->HairComponent->Template->SkeletalMesh;
			UHairComponent* sourceHair = remote->HairComponent;
			CaptureNativeHairBasisCorrection(GetLocalPawn());
			const bool templatePreparedBeforeSpawn =
				PrepareIndependentHairTemplate(
					sourceHair->Template);
			if (!g_loggedHairMeshCandidates)
			{
				g_loggedHairMeshCandidates = true;
				int loggedCandidates = 0;
				TArray<UObject*>* objects = UObject::GObjObjects();
				const int32_t objectCount =
					objects ? objects->size() : 0;
				for (int32_t index = 0;
					index < objectCount
						&& loggedCandidates < 64;
					++index)
				{
					UObject* object = objects->at(index);
					if (!object
						|| !object->IsA(
							USkeletalMesh::StaticClass()))
					{
						continue;
					}
					const std::string fullName =
						ObjectName(object);
					if (!ContainsCaseInsensitive(
						fullName, "hair"))
					{
						continue;
					}
					auto* candidate =
						reinterpret_cast<USkeletalMesh*>(
							object);
					std::ostringstream stream;
					stream
						<< "COSMETICSTAGE hair mesh candidate: "
						<< fullName
						<< ", lods="
						<< candidate->LODModels.ArrayNum
						<< ", materials="
						<< candidate->Materials.size()
						<< ", bones="
						<< candidate->RefSkeleton.size()
						<< ", boundsRadius="
						<< candidate->Bounds.SphereRadius
						<< '.';
					Log(stream.str());
					++loggedCandidates;
				}
				Log("COSMETICSTAGE loaded hair mesh candidates="
					+ std::to_string(loggedCandidates) + '.');
			}
			// SkeletalMeshHairActor owns a distinct HairComponent instance.
			// This mirrors the way a separate Unity GameObject would own its
			// own cloth/hair simulation component instead of sharing the
			// playable character's component state.
			UClass* proxyClass =
				ASkeletalMeshHairActor::StaticClass();
			ASkeletalMeshHairActor* hairActorDefault = nullptr;
			if (TArray<UObject*>* objects = UObject::GObjObjects())
			{
				for (int32_t index = 0;
					index < objects->size(); ++index)
				{
					UObject* object = objects->at(index);
					if (object && object->Class == proxyClass
						&& object->IsDefaultObject())
					{
						hairActorDefault =
							reinterpret_cast<
								ASkeletalMeshHairActor*>(object);
						break;
					}
				}
			}

			// This map actor's class default is authored static/no-delete, so
			// UE3 rejects an ordinary runtime Spawn before PreBeginPlay. Relax
			// only the default template for the duration of Spawn and restore
			// it immediately afterwards. The spawned copy remains movable.
			bool defaultWasStatic = false;
			bool defaultWasNoDelete = false;
			bool defaultWasMovable = false;
			UHair* defaultActorHair = nullptr;
			UHair* defaultComponentHair = nullptr;
			if (hairActorDefault)
			{
				defaultWasStatic = hairActorDefault->bStatic;
				defaultWasNoDelete =
					hairActorDefault->bNoDelete;
				defaultWasMovable = hairActorDefault->bMovable;
				hairActorDefault->bStatic = false;
				hairActorDefault->bNoDelete = false;
				hairActorDefault->bDeleteMe = false;
				hairActorDefault->bPendingDelete = false;
				hairActorDefault->bMovable = true;
				defaultActorHair = hairActorDefault->Hair;
				hairActorDefault->Hair = sourceHair->Template;
				if (hairActorDefault->HairComponent)
				{
					defaultComponentHair =
						hairActorDefault->HairComponent->Template;
					hairActorDefault->HairComponent->Template =
						sourceHair->Template;
				}
				Log("COSMETICSTAGE hair actor default prepared: "
					+ ObjectName(hairActorDefault)
					+ ", static="
					+ std::to_string(defaultWasStatic)
					+ ", noDelete="
					+ std::to_string(defaultWasNoDelete)
					+ ", movable="
					+ std::to_string(defaultWasMovable)
					+ ", skeletal="
					+ ObjectName(
						hairActorDefault->SkeletalMeshComponent)
					+ ", hairComponent="
					+ ObjectName(
						hairActorDefault->HairComponent)
					+ '.');
			}
			AActor* spawned = NativeSpawn(remote, proxyClass, remote,
				FName(), remote->Location, remote->Rotation,
				hairActorDefault, true);
			if (hairActorDefault)
			{
				hairActorDefault->bStatic = defaultWasStatic;
				hairActorDefault->bNoDelete =
					defaultWasNoDelete;
				hairActorDefault->bMovable =
					defaultWasMovable;
				hairActorDefault->Hair = defaultActorHair;
				if (hairActorDefault->HairComponent)
				{
					hairActorDefault->HairComponent->Template =
						defaultComponentHair;
				}
			}
			if (!spawned
				|| !spawned->IsA(
					ASkeletalMeshHairActor::StaticClass()))
			{
				if (spawned)
				{
					spawned->bNoDelete = false;
					NativeDestroyActor(spawned);
				}
				Log("COSMETICSTAGE independent hair actor spawn failed; "
					"using the rigid scalp fallback.");
				proxyClass =
					ASkeletalMeshActorSpawnable::StaticClass();
				spawned = NativeSpawn(remote, proxyClass, remote,
					FName(), remote->Location, remote->Rotation,
					nullptr, true);
				if (!spawned
					|| !spawned->IsA(
						ASkeletalMeshActorSpawnable::StaticClass()))
				{
					if (spawned)
					{
						spawned->bNoDelete = false;
						NativeDestroyActor(spawned);
					}
					Log("COSMETICSTAGE skeletal hair proxy "
						"fallback spawn failed.");
					return;
				}
			}

			auto* proxy =
				reinterpret_cast<ASkeletalMeshActor*>(
					spawned);
			ASkeletalMeshHairActor* hairActor =
				spawned->IsA(ASkeletalMeshHairActor::StaticClass())
					? reinterpret_cast<ASkeletalMeshHairActor*>(
						spawned)
					: nullptr;
			const bool spawnedWithPreparedTemplate =
				hairActor && hairActor->HairComponent
				&& hairActor->HairComponent->Template
					== sourceHair->Template;
			std::uintptr_t simulatorAtSpawn =
				hairActor && hairActor->HairComponent
					? hairActor->HairComponent->Simulator.Dummy
					: 0;
			proxy->bNoDelete = false;
			proxy->bStatic = false;
			proxy->bMovable = true;
			proxy->bCanBeDamaged = false;
			proxy->bCollideActors = false;
			proxy->bCollideWorld = false;
			proxy->bBlockActors = false;
			NativeSetOwner(proxy, remote);
			if (!proxy->SkeletalMeshComponent)
			{
				NativeSetActorHidden(proxy, true);
				NativeDestroyActor(proxy);
				Log("COSMETICSTAGE skeletal hair proxy has no component.");
				return;
			}

			USkeletalMeshComponent* component =
				proxy->SkeletalMeshComponent;
			NativeSetSkeletalMesh(component, hairMesh, false);
			component->LightEnvironment = remote->LightEnvironment;
			component->bOwnerNoSee = false;
			component->bOnlyOwnerSee = false;
			// The skeletal scalp is only a head-space transform anchor now.
			// Keep its pose ticking but do not render the cap mesh.
			component->HiddenGame = true;
			component->bPauseAnims = false;
			component->bNoSkeletonUpdate = false;
			component->bUpdateSkelWhenNotRendered = true;
			component->bTickAnimNodesWhenNotRendered = true;
			NativeSetComponentHidden(component, true);
			NativeSetActorHidden(proxy, false);
			NativeForceSkelUpdate(component);
			NativeForceComponentUpdate(component, false);
			// Establish a stable root-space bind pose first, then constrain the
			// scalp rigidly to Alice's animated head through the same native
			// component attachment path used by presentation weapons.
			component->LocalToWorld = remote->Mesh->LocalToWorld;
			component->Bounds = remote->Mesh->Bounds;
			component->bAlwaysUpdateMeshObject = true;
			component->bForceMeshObjectUpdate = true;
			const bool hairBound = AttachRigidPresentationCosmetic(
				remote->Mesh, component, true, "hair-skeletal");
			NativeForceComponentUpdate(component, false);
			NativeForceUpdateComponents(proxy, false, false);

			g_remoteHairProxy = proxy;
			g_remoteHairProxyBaseDrawScale =
				proxy->DrawScale > 0.001f
					? proxy->DrawScale : 1.0f;
			g_remoteHairSkeletalBaseScale =
				component->Scale > 0.001f
					? component->Scale : 1.0f;

			UHairComponent* strandHair =
				hairActor ? hairActor->HairComponent : nullptr;
			bool strandReset = false;
			bool strandAligned = false;
			if (hairActor && strandHair)
			{
				// Hair is immutable cooked strand data; the actor and simulator
				// are independent, while Template and Material can safely
				// reference the same assets as the playable Alice.
				hairActor->Hair = sourceHair->Template;
				hairActor->HairAttachBoneName =
					FName("Bip01-Head");
				strandHair->Template = sourceHair->Template;
				strandHair->PhysicsAsset =
					sourceHair->PhysicsAsset;
				strandHair->Force = sourceHair->Force;
				strandHair->PerturbAmplitude =
					sourceHair->PerturbAmplitude;
				strandHair->PerturbTemporalPeriod =
					sourceHair->PerturbTemporalPeriod;
				strandHair->PerturbSpatialPeriod =
					sourceHair->PerturbSpatialPeriod;
				strandHair->PerturbPhaseShift =
					sourceHair->PerturbPhaseShift;
				strandHair->Damping = sourceHair->Damping;
				strandHair->Iteration = sourceHair->Iteration;
				strandHair->LengthScale =
					sourceHair->LengthScale;
				strandHair->Material = sourceHair->Material;
				strandHair->TessellationStep =
					sourceHair->TessellationStep;
				strandHair->StrandWidth =
					sourceHair->StrandWidth;
				g_remoteHairBaseLengthScale =
					sourceHair->LengthScale;
				g_remoteHairBaseStrandWidth =
					sourceHair->StrandWidth;
				strandHair->SortOffset =
					sourceHair->SortOffset;
				strandHair->bOwnerNoSee = false;
				strandHair->bOnlyOwnerSee = false;
				// Match the playable Alice: the dedicated HairComponent uses
				// its Template data directly. Feeding the scalp component here
				// applies its authored basis a second time inside the native
				// renderer.
				strandHair->OverrideMesh = nullptr;
				strandHair->Bounds = remote->Mesh->Bounds;
				strandHair->HiddenGame = false;
				strandHair->bJustAttached = true;
				strandHair->bPendingReset = true;
				strandAligned =
					AlignIndependentHairActorToScalp(
						hairActor, strandHair, component);
				// Keep the component on its owning actor so Reset/Tick consume
				// their state normally. The actor itself now follows the scalp.
				NativeReattachComponent(
					hairActor, strandHair);
				AlignIndependentHairActorToScalp(
					hairActor, strandHair, component);
				NativeSetComponentHidden(strandHair, false);
				strandReset = NativeResetHair(strandHair);
				// Reset may refresh component state; retain the native
				// no-override rendering path used by the playable Alice.
				strandHair->OverrideMesh = nullptr;
				NativeForceComponentUpdate(
					strandHair, false);
				NativeForceUpdateComponents(
					hairActor, false, false);
				g_remoteIndependentHair = strandHair;
				g_remoteIndependentHairBaseScale =
					strandHair->Scale > 0.001f
						? strandHair->Scale : 1.0f;
			}

			// The component embedded in the cloned Alice is still tied to
			// Alice's native owner logic. Keep only that copy dormant.
			sourceHair->OverrideMesh = remote->Mesh;
			sourceHair->bJustAttached = false;
			sourceHair->bPendingReset = false;
			sourceHair->HiddenGame = true;
			NativeSetComponentHidden(sourceHair, true);

			std::ostringstream stream;
			stream << "COSMETICSTAGE skeletal hair proxy spawned: actor="
				<< ObjectName(proxy)
				<< ", mesh=" << ObjectName(hairMesh)
				<< ", lods=" << hairMesh->LODModels.ArrayNum
				<< ", materials=" << hairMesh->Materials.size()
				<< ", sourceBones=" << remote->Mesh->SpaceBases.size()
				<< ", parentBones=" << component->ParentBoneMap.size()
				<< ", attachedParent="
				<< ObjectName(component->AttachedToSkelComponent)
				<< ", headBound="
				<< (hairBound ? "yes" : "no")
				<< ", actorClass="
				<< ObjectName(proxy->Class)
				<< ", independentHair="
				<< ObjectName(strandHair)
				<< ", strandTemplate="
				<< ObjectName(
					strandHair ? strandHair->Template : nullptr)
				<< ", strandMaterial="
				<< ObjectName(
					strandHair ? strandHair->Material : nullptr)
				<< ", strandReset="
				<< (strandReset ? "yes" : "no")
				<< ", templatePreparedBeforeSpawn="
				<< (templatePreparedBeforeSpawn ? "yes" : "no")
				<< ", spawnedWithPreparedTemplate="
				<< (spawnedWithPreparedTemplate ? "yes" : "no")
				<< ", simulatorAtSpawn="
				<< simulatorAtSpawn
				<< ", strandActorAligned="
				<< (strandAligned ? "yes" : "no")
				<< ", simulator="
				<< (strandHair
					&& strandHair->Simulator.Dummy
						? "yes" : "no")
				<< ", strandOverride="
				<< ObjectName(strandHair
					? strandHair->OverrideMesh : nullptr)
				<< '.';
			Log(stream.str());
			for (int32_t index = 0;
				index < hairMesh->Materials.size(); ++index)
			{
				Log("COSMETICSTAGE scalp material["
					+ std::to_string(index) + "]="
					+ ObjectName(hairMesh->Materials.at(index))
					+ ", componentMaterial="
					+ ObjectName(index < component->Materials.size()
						? component->Materials.at(index) : nullptr)
					+ '.');
			}
		}

		void ForceRemoteCosmeticMasterPose(AAlicePawn* remote)
		{
			if (!remote || !remote->Mesh)
				return;

			auto maintainRigidCosmetic =
				[remote](USkeletalMeshComponent* component,
					const char* label)
			{
				if (!component
					|| component->AttachedToSkelComponent
						!= remote->Mesh)
				{
					return;
				}

				// Do not overwrite LocalToWorld here. The native pelvis
				// attachment carries the root-space bind offset and must be
				// allowed to inherit the animated pelvis hierarchy.
				component->Bounds = remote->Mesh->Bounds;
				component->bAlwaysUpdateMeshObject = true;
				component->bForceMeshObjectUpdate = true;
				component->bRecentlyRendered = true;

				const std::string key =
					std::string("cosmetic-rigid:") + label;
				if (g_loggedConfigAnimationStages
					.insert(key).second)
				{
					std::ostringstream stream;
					stream << "COSMETICSTAGE rigid active "
						<< label << ": parent="
						<< ObjectName(
							component->AttachedToSkelComponent)
						<< ", boundsRadius="
						<< component->Bounds.SphereRadius
						<< '.';
					Log(stream.str());
				}
			};

			const std::array<UClothComponent*, 4> clothComponents{
				remote->SkirtComponent,
				remote->BowComponent,
				remote->RibbonComponent,
				remote->EarComponent
			};
			for (UClothComponent* cloth : clothComponents)
			{
				if (!cloth || !cloth->SkeletalMesh
					|| cloth->AttachedToSkelComponent
						!= remote->Mesh)
				{
					continue;
				}
				maintainRigidCosmetic(
					cloth, cloth == remote->SkirtComponent
					? "skirt"
					: (cloth == remote->BowComponent
						? "bow"
						: (cloth == remote->RibbonComponent
							? "ribbon" : "ear")));
			}
			if (g_remoteHairProxy
				&& g_remoteHairProxy->SkeletalMeshComponent)
			{
				USkeletalMeshComponent* hair =
					g_remoteHairProxy->SkeletalMeshComponent;
				if (hair->AttachedToSkelComponent
					!= remote->Mesh)
				{
					const bool rebound =
						AttachRigidPresentationCosmetic(
							remote->Mesh, hair, true,
							"hair-skeletal-repair");
					if (g_loggedConfigAnimationStages.insert(
						"cosmetic-head-constraint-repair:"
						"hair-skeletal").second)
					{
						Log(std::string(
							"COSMETICSTAGE head constraint repair ")
							+ (rebound ? "succeeded." : "failed."));
					}
				}
				hair->Bounds = remote->Mesh->Bounds;
				hair->bAlwaysUpdateMeshObject = true;
				hair->bForceMeshObjectUpdate = true;
				hair->bRecentlyRendered = true;
				if (g_remoteIndependentHair)
				{
					AlignIndependentHairActorToScalp(
						g_remoteHairProxy,
						g_remoteIndependentHair, hair);
					g_remoteIndependentHair->Bounds =
						remote->Mesh->Bounds;
				}
				if (g_remoteIndependentHair
					&& g_remoteIndependentHair->OverrideMesh)
				{
					g_remoteIndependentHair->OverrideMesh = nullptr;
					if (g_loggedConfigAnimationStages.insert(
						"independent-hair-native-template-repair")
						.second)
					{
						Log("COSMETICSTAGE independent hair restored "
							"to native Template rendering.");
					}
				}
				if (g_loggedConfigAnimationStages.insert(
					"cosmetic-head-constraint:hair-skeletal")
					.second)
				{
					Log("COSMETICSTAGE head constraint active "
						"hair-skeletal: parent="
						+ ObjectName(
							hair->AttachedToSkelComponent)
						+ ", boundsRadius="
						+ std::to_string(
							hair->Bounds.SphereRadius)
						+ '.');
				}
			}

			if (g_remoteHidden)
			{
				// Dodge and local cinematics hide the presentation-only remote
				// Alice without mutating the replicated actor state.
				// Native component updates can re-expose rigid attachments one
				// frame after their body disappears, leaving skirt/scalp at the
				// motion root. Reassert component-local hiding post-pose.
				auto hide = [](UPrimitiveComponent* primitive,
					bool nativeVisibility)
				{
					if (!primitive)
						return;
					if (nativeVisibility)
					{
						primitive->HiddenGame = true;
						NativeSetComponentHidden(
							primitive, true);
					}
					primitive->SetScale(0.001f);
					primitive->Scale = 0.001f;
					primitive->SetScale3D(
						FVector(0.001f, 0.001f, 0.001f));
					primitive->Scale3D =
						FVector(0.001f, 0.001f, 0.001f);
				};
				hide(remote->Mesh, true);
				hide(remote->UpperBodyComponent, true);
				hide(remote->HairComponent, true);
				hide(remote->SkirtComponent, true);
				hide(remote->BowComponent, true);
				hide(remote->RibbonComponent, true);
				hide(remote->EarComponent, true);
				if (remote->Weapon)
				{
					hide(remote->Weapon->Mesh, true);
					remote->Weapon->bHidden = true;
					NativeSetActorHidden(remote->Weapon, true);
				}
				if (remote->DummyWeapon)
					hide(remote->DummyWeapon->Mesh, true);

				// Never use HiddenGame or scale on the independent UHair
				// component. Even a single frame invalidates its simulator.
				// Owner-only visibility is applied above and preserves it.
				if (g_remoteIndependentHair)
				{
					g_remoteIndependentHair->bOwnerNoSee = false;
					g_remoteIndependentHair->bOnlyOwnerSee = true;
				}
			}
			else
			{
				// Alice's single-player dodge path can leave translucency or
				// scale state on every Alice-shaped primitive in the world.
				// Clear that state only on our presentation tree. Visibility
				// itself was restored immediately before this function by
				// ApplyRemoteComponentVisibility, so dormant anchor meshes
				// remain dormant.
				auto restore = [](UPrimitiveComponent* primitive,
					float scale, const FVector& scale3D)
				{
					if (!primitive)
						return;
					primitive->bForceTranslucency = false;
					primitive->ForceTranslucencyAlpha = 1.0f;
					primitive->ForceTranslucencyTargetAlpha = 1.0f;
					primitive->ForceTranslucencyBlendTime = 0.0f;
					primitive->ForceTranslucencyBlendSpeed = 0.0f;
					primitive->SetScale(scale);
					primitive->Scale = scale;
					primitive->SetScale3D(scale3D);
					primitive->Scale3D = scale3D;
				};
				remote->bForceTranslucency = false;
				remote->ForceTranslucencyAlpha = 1.0f;
				const FVector unitScale(1.0f, 1.0f, 1.0f);
				const FVector actorScale =
					g_remotePresentation.valid
						? FVector(
							g_remotePresentation.state.drawScale3D[0],
							g_remotePresentation.state.drawScale3D[1],
							g_remotePresentation.state.drawScale3D[2])
						: unitScale;
				remote->SetDrawScale3D(actorScale);
				remote->DrawScale3D = actorScale;
				const FVector meshScale3D =
					g_remotePresentation.valid
						? FVector(
							g_remotePresentation.state.meshScale3D[0],
							g_remotePresentation.state.meshScale3D[1],
							g_remotePresentation.state.meshScale3D[2])
						: unitScale;
				restore(remote->Mesh,
					g_remotePresentation.valid
						&& std::isfinite(
							g_remotePresentation.state.meshScale)
						&& g_remotePresentation.state.meshScale > 0.01f
					? g_remotePresentation.state.meshScale : 1.0f,
					meshScale3D);
				restore(remote->UpperBodyComponent, 1.0f, unitScale);
				restore(remote->HairComponent, 1.0f, unitScale);
				restore(remote->SkirtComponent, 1.0f, unitScale);
				restore(remote->BowComponent, 1.0f, unitScale);
				restore(remote->RibbonComponent, 1.0f, unitScale);
				restore(remote->EarComponent, 1.0f, unitScale);
				if (g_remoteHairProxy)
				{
					float hairScaleFactor = 1.0f;
					if (g_remotePresentation.valid
						&& std::isfinite(
							g_remotePresentation.state.drawScale)
						&& g_remotePresentation.state.drawScale
							> 0.01f)
					{
						const float baseScale =
							g_remoteBaseDrawScale > 0.01f
								? g_remoteBaseDrawScale : 1.0f;
						hairScaleFactor = std::clamp(
							g_remotePresentation.state.drawScale
								/ baseScale,
							0.1f, 2.0f);
					}
					const float hairActorScale =
						g_remoteHairProxyBaseDrawScale
						* hairScaleFactor;
					g_remoteHairProxy->SetDrawScale(
						hairActorScale);
					g_remoteHairProxy->DrawScale =
						hairActorScale;
					restore(
						g_remoteHairProxy->SkeletalMeshComponent,
						g_remoteHairSkeletalBaseScale, unitScale);
				}
				restore(g_remoteIndependentHair,
					g_remoteIndependentHairBaseScale, unitScale);
				if (g_remoteHairProxy)
				{
					g_remoteHairProxy->SetDrawScale3D(unitScale);
					g_remoteHairProxy->DrawScale3D = unitScale;
				}
				if (g_remoteIndependentHair)
				{
					// HiddenGame/scale changes invalidate Alice's custom hair
					// simulator. Owner-only visibility keeps it ticking while
					// excluding it from the local cinematic view.
					g_remoteIndependentHair->bOwnerNoSee = false;
					g_remoteIndependentHair->bOnlyOwnerSee = false;
					g_remoteIndependentHair->HiddenGame = false;
					NativeSetComponentHidden(
						g_remoteIndependentHair, false);
				}
				g_remoteHairNeedsReset = false;
				if (remote->Weapon)
				{
					remote->Weapon->bForceTranslucency = false;
					remote->Weapon->ForceTranslucencyAlpha = 1.0f;
					if (remote->Weapon
						== g_remotePresentationWeapon)
					{
						remote->Weapon->SetDrawScale(
							g_remoteWeaponBaseDrawScale);
						remote->Weapon->DrawScale =
							g_remoteWeaponBaseDrawScale;
						remote->Weapon->SetDrawScale3D(unitScale);
						remote->Weapon->DrawScale3D = unitScale;
					}
					restore(remote->Weapon->Mesh, 1.0f, unitScale);
				}
				if (remote->DummyWeapon)
					restore(remote->DummyWeapon->Mesh, 1.0f,
						unitScale);
			}
		}

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

		bool BindLocalMirrorPose(AAlicePawn* remote, AAlicePawn* source)
		{
			if (!remote || !source || !remote->Mesh || !source->Mesh)
				return false;

			if (remote->Mesh->SkeletalMesh != source->Mesh->SkeletalMesh)
			{
				if (!g_loggedLocalMirrorIncompatibility)
				{
					g_loggedLocalMirrorIncompatibility = true;
					Log("WARN: Local mirror body skeleton mismatch: source="
						+ ObjectName(source->Mesh->SkeletalMesh)
						+ ", mirror=" + ObjectName(remote->Mesh->SkeletalMesh) + '.');
				}
				return false;
			}

			if (remote->Mesh->ParentAnimComponent != source->Mesh)
				remote->Mesh->SetParentAnimComponent(source->Mesh);

			// Share only the evaluated skeleton. The duplicate must retain its own
			// actor/component transform so it remains beside the playable Alice.
			remote->Mesh->bTransformFromAnimParent = false;

			bool upperBodyBound = false;
			if (remote->UpperBodyComponent && source->UpperBodyComponent
				&& remote->UpperBodyComponent->SkeletalMesh
					== source->UpperBodyComponent->SkeletalMesh)
			{
				if (remote->UpperBodyComponent->ParentAnimComponent
					!= source->UpperBodyComponent)
				{
					remote->UpperBodyComponent->SetParentAnimComponent(
						source->UpperBodyComponent);
				}
				remote->UpperBodyComponent->bTransformFromAnimParent = false;
				upperBodyBound = remote->UpperBodyComponent->ParentAnimComponent
					== source->UpperBodyComponent;
			}

			const bool bodyBound = remote->Mesh->ParentAnimComponent == source->Mesh;
			if (bodyBound && !g_loggedLocalMirrorBinding)
			{
				g_loggedLocalMirrorBinding = true;
				Log("Local mirror pose parent bound: sourceBones="
					+ std::to_string(source->Mesh->SpaceBases.size())
					+ ", mirrorBones=" + std::to_string(remote->Mesh->SpaceBases.size())
					+ ", upperBody=" + (upperBodyBound ? "bound" : "not-bound") + '.');
			}
			return bodyBound;
		}

		void AttachRemoteController(AAlicePawn* remote)
		{
			if (!remote)
				return;

			UClass* controllerClass = AAliceGameCloneAliceAIController::StaticClass();
			if (!controllerClass)
			{
				Log("WARN: AliceGameCloneAliceAIController class was not found.");
				return;
			}

			AActor* spawnContext = remote;
			if (remote->WorldInfo && remote->WorldInfo->Game)
				spawnContext = remote->WorldInfo->Game;

			// Controllers are world services rather than children of their pawn. Using
			// the pawn as Owner makes UE3 reject AliceGameCloneAliceAIController here.
			// The generated Actor::Spawn wrapper rejects controller classes
			// in this build. Invoke the native Spawn event directly, as is
			// already done for the hair and projectile presentation actors.
			AActor* actor = NativeSpawn(
				spawnContext, controllerClass, nullptr, FName(),
				remote->Location, remote->Rotation, nullptr, true);
			bool usedGenericController = false;
			if (!actor)
			{
				UClass* fallbackClass = AAIController::StaticClass();
				if (fallbackClass)
				{
					actor = NativeSpawn(
						spawnContext, fallbackClass, nullptr, FName(),
						remote->Location, remote->Rotation,
						nullptr, true);
					usedGenericController = actor != nullptr;
				}
			}
			auto* controller = reinterpret_cast<AController*>(actor);
			if (!controller)
			{
				Log("WARN: Failed to spawn both the Alice clone controller and"
					" the generic AI controller.");
				return;
			}

			controller->eventPossess(remote, false);
			controller->Pawn = remote;
			remote->Controller = controller;
			// Sphinx treats damage instigators with non-player controllers as
			// NPC-on-NPC traffic and keeps Alice as its fight target. This
			// controller exists only for the authoritative remote Alice, so it
			// is safe and necessary to identify it as the second player.
			controller->bIsPlayer =
				g_config.role == Role::Host;
			g_remoteController = controller;

			std::ostringstream stream;
			stream << "Remote service controller attached ["
				<< (usedGenericController ? "generic AI fallback" : "Alice clone")
				<< "] ("
				<< (g_config.role == Role::Host ? "host authority" : "client presentation")
				<< "): object=" << ObjectName(controller)
				<< ", specialMoveClasses="
				<< remote->SpecialMoveClasses.size()
				<< ", specialMoves=" << remote->SpecialMoves.size()
				<< ", weapon=" << ObjectName(remote->Weapon) << '.';
			Log(stream.str());
		}

		AAlicePawn* SpawnRemotePawn(AAlicePawn* localPawn, AWorldInfo* world,
			const PlayerStatePayload& state)
		{
			if (!world->Game || Clock::now() < g_nextSpawnAttempt)
				return nullptr;
			g_nextSpawnAttempt = Clock::now() + std::chrono::seconds(2);

			auto* game = reinterpret_cast<AAliceGameInfo*>(world->Game);
			AAlicePawn* actorTemplate = game->AliceArcheType;
			const int archetype = static_cast<int>(localPawn->ArcheTypeID);
			if (archetype >= 0 && archetype < 9 && game->AliceCachedArcheType[archetype])
				actorTemplate = game->AliceCachedArcheType[archetype];
			if (!actorTemplate)
				actorTemplate = localPawn;
			const EAliceArcheType localArchetypeBeforeFactory = localPawn->ArcheTypeID;
			const EAliceArcheType gameArchetypeBeforeFactory = game->AliceArcheTypeID;
			LogCosmeticState("local-before-factory", localPawn);

			const FVector location(state.location[0], state.location[1], state.location[2]);
			const FRotator rotation(state.rotation[0], state.rotation[1], state.rotation[2]);
			UClass* spawnClass = actorTemplate && actorTemplate->Class
				? actorTemplate->Class
				: localPawn->Class;
			AActor* spawned = nullptr;
			AAlicePlayerController* controller = g_State.AlicePlayerController;
			if (controller && controller->StartSpot)
			{
				APawn* const originalControllerPawn = controller->Pawn;
				APawn* const originalAcknowledgedPawn = controller->AcknowledgedPawn;
				AAlicePawn* const originalMyAlicePawn = controller->MyAlicePawn;
				AActor* const originalViewTarget = controller->ViewTarget;
				const std::uint8_t originalIgnoreMoveInput = controller->bIgnoreMoveInput;
				const std::uint8_t originalIgnoreLookInput = controller->bIgnoreLookInput;
				const bool originalCinemaDisableInputMove = controller->bCinemaDisableInputMove;
				const bool originalCinemaDisableInputLook = controller->bCinemaDisableInputLook;
				ANavigationPoint* const startSpot = controller->StartSpot;
				const FVector originalStartLocation = startSpot->Location;
				const FRotator originalStartRotation = startSpot->Rotation;

				// Spawn the factory pawn at its final network position. Hair and cloth
				// simulators bind during construction and do not reliably survive the
				// large PlayerStart -> peer teleport performed by the old proxy path.
				startSpot->Location = location;
				startSpot->Rotation = rotation;
				APawn* defaultPawn = game->SpawnDefaultPawnFor(controller, startSpot);
				startSpot->Location = originalStartLocation;
				startSpot->Rotation = originalStartRotation;
				if (defaultPawn && defaultPawn != localPawn)
				{
					spawned = defaultPawn;
					Log("Visual proxy created through AliceGameInfo.SpawnDefaultPawnFor"
						" with archetype=" + std::to_string(static_cast<int>(localPawn->ArcheTypeID)) + '.');
				}
				if (localPawn->ArcheTypeID != localArchetypeBeforeFactory
					|| game->AliceArcheTypeID != gameArchetypeBeforeFactory)
				{
					Log("Factory archetype side effect: local "
						+ std::to_string(static_cast<int>(localArchetypeBeforeFactory)) + " -> "
						+ std::to_string(static_cast<int>(localPawn->ArcheTypeID))
						+ ", game "
						+ std::to_string(static_cast<int>(gameArchetypeBeforeFactory)) + " -> "
						+ std::to_string(static_cast<int>(game->AliceArcheTypeID)) + '.');
				}

				// AliceGame's factory mutates the supplied controller while constructing the pawn.
				// Restore every gameplay-facing link so input remains bound to the original Alice.
				controller->Pawn = originalControllerPawn ? originalControllerPawn : localPawn;
				controller->AcknowledgedPawn = originalAcknowledgedPawn
					? originalAcknowledgedPawn
					: localPawn;
				controller->MyAlicePawn = originalMyAlicePawn ? originalMyAlicePawn : localPawn;
				controller->ViewTarget = originalViewTarget ? originalViewTarget : localPawn;
				controller->bIgnoreMoveInput = originalIgnoreMoveInput;
				controller->bIgnoreLookInput = originalIgnoreLookInput;
				controller->bCinemaDisableInputMove = originalCinemaDisableInputMove;
				controller->bCinemaDisableInputLook = originalCinemaDisableInputLook;
				localPawn->Controller = controller;
				g_State.AlicePawn = localPawn;
			}

			if (!spawned)
			{
				spawned = localPawn->Spawn(spawnClass, nullptr,
					FName(), location, rotation, actorTemplate, true);
			}
			if (!spawned && localPawn->Class)
			{
				Log("Template spawn failed; retrying from the local pawn class default.");
				spawned = localPawn->Spawn(localPawn->Class, nullptr,
					FName(), location, rotation, nullptr, true);
			}
			auto* remote = reinterpret_cast<AAlicePawn*>(spawned);
			if (!remote)
			{
				const std::string className = spawnClass ? spawnClass->GetName() : "<null>";
				const std::string templateName = actorTemplate ? actorTemplate->GetFullName() : "<null>";
				Log("Visual proxy spawn failed; class=" + className
					+ ", template=" + templateName + "; will retry.");
				return nullptr;
			}

			// The factory normally duplicates the requested archetype completely. Retain the
			// component-level fallback for builds that silently substitute the default archetype.
			if (remote->ArcheTypeID != localPawn->ArcheTypeID)
			{
				Log("Factory substituted a different archetype; applying cosmetic fallback.");
				MirrorCosmetics(remote, localPawn);
			}
			// The client copy remains presentation-only. On the host the
			// proxy must be a valid damage target, otherwise Sphinx rejects
			// it before considering any of the controller target fields.
			remote->bCanBeDamaged =
				g_config.role == Role::Host
				&& g_config.sharedEnemyTransforms;
			remote->Instigator = remote;
			remote->Controller = nullptr;
			remote->PlayerReplicationInfo = nullptr;
			remote->Physics = EPhysics::PHYS_None;
			remote->SpecialMove = ESpecialMove::SM_None;
			remote->PreviousSpecialMove = ESpecialMove::SM_None;
			remote->CurrentJumpStatus = EJumpStatus::EMT_None;
			remote->RemoteRole = ENetRole::ROLE_None;
			remote->Role = ENetRole::ROLE_Authority;
			remote->bHidden = false;
			remote->bOnlyOwnerSee = false;
			NativeSetActorHidden(remote, false);
			if (remote->Mesh)
			{
				remote->Mesh->bOwnerNoSee = false;
				remote->Mesh->bOnlyOwnerSee = false;
			}
			if (g_config.disableProxyCollision)
			{
				remote->bCollideActors = false;
				remote->bCollideWorld = false;
				remote->bBlockActors = false;
				remote->SetCollision(false, false, true);
				remote->SetCollisionType(ECollisionType::COLLIDE_NoCollision);
			}
			remote->SetLocationNoCheck(location);
			remote->SetRotation(rotation);
			remote->Location = location;
			remote->Rotation = rotation;
			NativeForceUpdateComponents(remote, false, false);
			LogCosmeticState("local-before-reattach", localPawn);
			LogCosmeticState("remote-before-reattach", remote);
			ReattachProxyCosmetics(remote);
			SpawnRemoteHairProxy(remote);
			EnableRemoteAnimationTick(remote);
			LogCosmeticState("remote-after-reattach", remote);
			g_remotePawn = remote;
			g_remoteWorld = world;
			ResetRemoteWeaponPresentationState();
			g_remoteDodgeVisualHidden = false;
			g_remoteGlideVfxActive = false;
			g_remoteAirResetPending = false;
			g_remoteGlideInactiveSince = {};
			g_remoteNativeGlideCurrent = nullptr;
			g_remoteNativeGlideCurrentSince = {};
			g_nextRemoteNativeGlideSweep = {};
			if (g_remoteGlideParticle)
				RetirePersistentPresentationParticle(
					g_remoteGlideParticle);
			g_remoteGlideParticle = nullptr;
			g_remoteClockBombAnimationActive = false;
			g_devForceProxyHidden = false;
			g_devDodgeHideUntil = {};
			g_devMuzzleUntil = {};
			g_devGlideUntil = {};
			g_remoteDodgeParticleTemplate = nullptr;
			g_remoteDodgeParticleUntil = {};
			g_nextRemoteDodgeParticle = {};
			g_remoteFullBodyNotifyCursor = {};
			g_recentRemoteVisualNotifies.clear();
			g_lastRemoteVfx = "none";
			g_remoteBaseDrawScale = remote->DrawScale;
			g_remoteShrinkApplied = false;
			g_remoteHidden = false;
			g_remoteRenderDetached = false;
			g_loggedPostTickPresentation = false;
			g_loggedPostEngineSkeletonRefresh = false;
			g_loggedPresentedCosmetics = false;
			g_loggedDelayedCosmetics = false;
			g_remoteHairOwnerFallbackApplied = false;
			g_remoteCosmeticDiagnosticAt =
				Clock::now() + std::chrono::seconds(2);
			g_nextCosmeticSpatialSample =
				Clock::now() + std::chrono::seconds(1);
			// The read-only spatial trace has served its purpose. Keep the
			// instrumentation available for future diagnostics without
			// producing twenty large samples during every normal connection.
			g_cosmeticSpatialSamplesRemaining = 0;
			g_cosmeticSpatialSampleNumber = 0;
			g_remotePresentation = {};
			g_remoteGraphSequences.clear();
			g_lastAppliedAnimationGraphFrame = 0;
			g_loggedRemoteAnimationGraph = false;
			g_lastRemoteAnimationGraphSignature.clear();
			g_remoteAppliedFullBodyNode = nullptr;
			g_remoteAppliedFullBodySlot = nullptr;
			g_remoteAppliedFullBodyName.clear();
			ResetPresentationAnimChannel(g_remoteFullBodyChannel);
			ResetPresentationAnimChannel(g_remoteUpperAdditiveChannel);
			g_loggedAnimationStageFailures.clear();
			g_loggedAnimationStageSuccesses.clear();
			g_loggedConfigAnimationStages.clear();
			g_animationStagePoseSamples.clear();
			g_loggedNotifyInventories.clear();
			g_loggedLocalMirrorBinding = false;
			g_loggedLocalMirrorIncompatibility = false;
			g_lastRemoteActionSerial = state.actionSerial;
			g_lastRemoteSpecialMove = static_cast<std::uint8_t>(ESpecialMove::SM_None);
			g_loggedMissingSpecialMoves = false;
			if (g_config.localMirror)
				BindLocalMirrorPose(remote, localPawn);
			Log(std::string(g_config.localMirror
				? "Local mirror Alice"
				: (g_config.role == Role::Host
					? "Host-authoritative remote Alice"
					: "Remote Alice presentation proxy"))
				+ " spawned on map " + g_currentMap + '.');
			return remote;
		}

		void HideAndDetachRetiredRemotePawn(AAlicePawn* pawn, bool park)
		{
			if (!pawn || !IsLiveUObject(pawn))
				return;
			auto hidePrimitive = [](UPrimitiveComponent* component)
			{
				if (!component)
					return;
				component->HiddenGame = true;
				NativeSetComponentHidden(component, true);
			};
			auto detach = [pawn](UActorComponent* component)
			{
				if (component && component->bAttached)
					NativeDetachActorComponent(pawn, component);
			};

			pawn->bHidden = true;
			NativeSetActorHidden(pawn, true);
			pawn->SetTickIsDisabled(true);
			pawn->SetCollision(false, false, true);
			hidePrimitive(pawn->Mesh);
			hidePrimitive(pawn->UpperBodyComponent);
			hidePrimitive(pawn->HairComponent);
			hidePrimitive(pawn->SkirtComponent);
			hidePrimitive(pawn->BowComponent);
			hidePrimitive(pawn->RibbonComponent);
			hidePrimitive(pawn->EarComponent);
			detach(pawn->HairComponent);
			detach(pawn->SkirtComponent);
			detach(pawn->BowComponent);
			detach(pawn->RibbonComponent);
			detach(pawn->EarComponent);
			detach(pawn->UpperBodyComponent);
			detach(pawn->Mesh);
			if (park)
			{
				const FVector parked(
					pawn->Location.X, pawn->Location.Y,
					pawn->Location.Z - 100000.0f);
				NativeSetActorLocationNoCheck(pawn, parked);
				pawn->Location = parked;
			}
		}

		void TickRetiredRemotePawns(AWorldInfo* world)
		{
			const Clock::time_point now = Clock::now();
			for (auto iterator = g_retiredRemotePawns.begin();
				iterator != g_retiredRemotePawns.end();)
			{
				RetiredRemotePawn& retired = *iterator;
				if (!retired.pawn || retired.world != world
					|| !IsLiveUObject(retired.pawn)
					|| retired.pawn->bDeleteMe)
				{
					iterator = g_retiredRemotePawns.erase(iterator);
					continue;
				}
				HideAndDetachRetiredRemotePawn(retired.pawn, false);
				if (now < retired.nextDestroyAttempt)
				{
					++iterator;
					continue;
				}
				retired.pawn->bNoDelete = false;
				retired.pawn->bStatic = false;
				++retired.attempts;
				const bool destroyed = NativeDestroyActor(retired.pawn);
				if (destroyed || retired.pawn->bDeleteMe)
				{
					Log("PEERLIFE retired Alice destroyed on retry "
						+ std::to_string(retired.attempts) + '.');
					iterator = g_retiredRemotePawns.erase(iterator);
					continue;
				}
				retired.nextDestroyAttempt =
					now + std::chrono::milliseconds(500);
				++iterator;
			}
		}

		bool IsRetiredRemotePawn(const AAlicePawn* pawn)
		{
			return pawn && std::any_of(g_retiredRemotePawns.begin(),
				g_retiredRemotePawns.end(),
				[pawn](const RetiredRemotePawn& retired)
				{
					return retired.pawn == pawn;
				});
		}

		void DestroyRemotePawn()
		{
			if (g_remoteWorld != g_currentWorld
				|| (g_remotePawn && !IsLiveUObject(g_remotePawn)))
			{
				Log("Visual Alice proxy references became stale; "
					"discarding them without UObject teardown.");
				ForgetTornDownWorldObjects();
				return;
			}
			// Make teardown visually atomic. NativeDestroyActor can be deferred or
			// refused for an archetype-cloned pawn, so detach its render tree before
			// asking UE3 to destroy it.
			RetireTrackedRemoteNativeGlideTrails(
				g_remoteWorld, nullptr, "proxy-destroy");
			if (g_remoteGlideParticle)
				HardStopPresentationParticle(g_remoteGlideParticle, true);
			g_remoteGlideParticle = nullptr;
			DestroyRemoteStaticLeafTrail();
			for (const TrackedPresentationParticle& tracked :
				g_remoteMovementTransientParticles)
			{
				if (IsLivePresentationParticle(tracked.component)
					&& tracked.component->Template == tracked.particleTemplate)
				{
					HardStopPresentationParticle(tracked.component, true);
				}
			}
			g_remoteMovementTransientParticles.clear();
			for (const PendingMovementParticlePreservation& pending :
				g_pendingMovementParticlePreservations)
			{
				if (IsLivePresentationParticle(pending.component)
					&& pending.component->Template == pending.particleTemplate)
				{
					HardStopPresentationParticle(pending.component, true);
				}
			}
			g_pendingMovementParticlePreservations.clear();
			g_remoteMovementParticleCapture = {};
			if (g_remotePawn)
			{
				HideAndDetachRetiredRemotePawn(g_remotePawn, false);
				if (g_remotePawn->DummyWeapon
					&& g_remotePawn->DummyWeapon->Mesh)
				{
					g_remotePawn->DummyWeapon->Mesh->HiddenGame = true;
					NativeSetComponentHidden(
						g_remotePawn->DummyWeapon->Mesh, true);
				}
			}
			DestroyRemotePresentationWeapon(true);
			DestroyRemoteHairProxy();
			if (g_remoteController && IsLiveUObject(g_remoteController)
				&& g_remoteWorld && g_remoteWorld == g_currentWorld)
			{
				g_remoteController->eventUnPossess();
				g_remoteController->bNoDelete = false;
				g_remoteController->bStatic = false;
				const bool controllerDestroyed =
					NativeDestroyActor(g_remoteController);
				if (!controllerDestroyed)
					Log("WARN: Remote controller destroy was deferred.");
			}
			if (g_remotePawn && IsLiveUObject(g_remotePawn)
				&& g_remoteWorld && g_remoteWorld == g_currentWorld)
			{
				g_remotePawn->bNoDelete = false;
				g_remotePawn->bStatic = false;
				const bool pawnDestroyed = NativeDestroyActor(g_remotePawn);
				if (!pawnDestroyed)
				{
					HideAndDetachRetiredRemotePawn(g_remotePawn, true);
					if (!IsRetiredRemotePawn(g_remotePawn))
					{
						g_retiredRemotePawns.push_back({
							g_remotePawn, g_remoteWorld,
							Clock::now() + std::chrono::milliseconds(250), 0 });
					}
					Log("WARN: Remote Alice destroy was deferred; render tree "
						"detached, pawn parked, retry scheduled.");
				}
			}
			if (g_remotePawn)
				Log("Visual Alice proxy removed.");
			g_remotePawn = nullptr;
			// The rotated UHair belongs to this cloned pawn. Keep the marker
			// while only its presentation proxy is rebuilt, but forget it once
			// the pawn itself is gone so a newly allocated template at the same
			// address is converted exactly once.
			g_remoteHairRotatedTemplate = nullptr;
			g_remoteHairTuningTemplate = nullptr;
			g_remoteHairBaseNodes.clear();
			g_remoteController = nullptr;
			g_remoteWorld = nullptr;
			ResetRemoteWeaponPresentationState();
			g_remoteDodgeVisualHidden = false;
			g_remoteGlideVfxActive = false;
			g_remoteAirResetPending = false;
			g_remoteGlideInactiveSince = {};
			if (g_remoteGlideParticle)
				RetirePersistentPresentationParticle(
					g_remoteGlideParticle);
			g_remoteGlideParticle = nullptr;
			g_remoteClockBombAnimationActive = false;
			g_devForceProxyHidden = false;
			g_devDodgeHideUntil = {};
			g_devMuzzleUntil = {};
			g_devGlideUntil = {};
			g_remoteDodgeParticleTemplate = nullptr;
			g_remoteDodgeParticleUntil = {};
			g_nextRemoteDodgeParticle = {};
			g_remoteFullBodyNotifyCursor = {};
			g_recentRemoteVisualNotifies.clear();
			g_lastRemoteVfx = "none";
			g_remoteBaseDrawScale = 1.0f;
			g_remoteShrinkApplied = false;
			g_remoteHidden = false;
			g_remoteRenderDetached = false;
			g_loggedPostTickPresentation = false;
			g_loggedPostEngineSkeletonRefresh = false;
			g_loggedPresentedCosmetics = false;
			g_loggedDelayedCosmetics = false;
			g_remoteHairOwnerFallbackApplied = false;
			g_remoteCosmeticDiagnosticAt = {};
			g_nextCosmeticSpatialSample = {};
			g_cosmeticSpatialSamplesRemaining = 0;
			g_cosmeticSpatialSampleNumber = 0;
			g_remotePresentation = {};
			g_remoteGraphSequences.clear();
			g_lastAppliedAnimationGraphFrame = 0;
			g_loggedRemoteAnimationGraph = false;
			g_lastRemoteAnimationGraphSignature.clear();
			g_remoteAppliedFullBodyNode = nullptr;
			g_remoteAppliedFullBodySlot = nullptr;
			g_remoteAppliedFullBodyName.clear();
			g_lastRemoteAnimationCompareSignature.clear();
			g_nextRemoteAnimationCompareSample = {};
			ResetPresentationAnimChannel(g_remoteFullBodyChannel);
			ResetPresentationAnimChannel(g_remoteUpperAdditiveChannel);
			g_loggedAnimationStageFailures.clear();
			g_loggedAnimationStageSuccesses.clear();
			g_loggedConfigAnimationStages.clear();
			g_animationStagePoseSamples.clear();
			g_loggedLocalMirrorBinding = false;
			g_loggedLocalMirrorIncompatibility = false;
			g_lastRemoteActionSerial = 0;
			g_lastRemoteSpecialMove = static_cast<std::uint8_t>(ESpecialMove::SM_None);
			g_loggedMissingSpecialMoves = false;
		}

		void TeardownDisconnectedPeerPresentation(const char* reason)
		{
			if (g_remotePawn || g_remotePresentationWeapon
				|| g_remoteHairProxy || g_remoteGlideParticle)
			{
				DestroyRemotePawn();
			}
			else
			{
				ResetRemoteWeaponPresentationState();
			}
			ClearRemoteProjectileVisuals(true);
			ResetSharedEnemyPose(true);
			if (g_remoteContextInteraction.active)
				ResetRemoteContextInteraction("peer-disconnected");
			g_activeRemoteAnimationGraph.reset();
			g_lastRemoteAnimationCompareSignature.clear();
			g_nextRemoteAnimationCompareSample = {};
			AchievementOverlay::SetPeerWatchingCutscene(false);
			AchievementOverlay::SetCoopWaitingForPeer(false);
			g_nextSpawnAttempt = Clock::now()
				+ std::chrono::milliseconds(300);
			Log(std::string("PEERLIFE presentation teardown completed, reason=")
				+ (reason ? reason : "unknown") + '.');
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

		void ApplyBlendChildWeights(UAnimNodeBlendBase* blend,
			int32_t activeChild, float nodeWeight)
		{
			if (!blend || activeChild < 0
				|| activeChild >= blend->Children.size())
			{
				return;
			}
			for (int32_t index = 0; index < blend->Children.size(); ++index)
			{
				const float weight = index == activeChild ? 1.0f : 0.0f;
				FAnimBlendChild& child = blend->Children.at(index);
				child.Weight = weight;
				child.TotalWeight = weight * nodeWeight;
				child.BlendWeight = weight;
			}
		}

		bool ApplyAnimationBlend(AAlicePawn* remote,
			const AnimationBlendPayload& state)
		{
			USkeletalMeshComponent* component =
				ResolveRemoteAnimationComponent(remote, state.component);
			if (!component
				|| state.nodeIndex >= component->AnimTickArray.size())
			{
				return false;
			}
			UAnimNode* node = component->AnimTickArray.at(state.nodeIndex);
			if (!node)
				return false;

			const int32_t child = state.activeChildIndex;
			const float nodeWeight = std::isfinite(state.nodeWeight)
				? std::clamp(state.nodeWeight, 0.0f, 1.0f)
				: 0.0f;
			node->NodeTotalWeight = nodeWeight;
			node->TotalWeightAccumulator = nodeWeight;
			node->bRelevant = (state.flags & AnimationBlendRelevant) != 0
				|| nodeWeight > 0.001f;
			if (node->IsA(UAliceGameAnimNode_BlendList::StaticClass()))
			{
				auto* blend =
					reinterpret_cast<UAliceGameAnimNode_BlendList*>(node);
				if (child < 0 || child >= blend->Children.size())
					return false;
				if (blend->ActiveChildIndex != child)
					blend->SetActiveChild(child, 0.0f);
				blend->ActiveChildIndex = child;
				blend->BlendTimeToGo = 0.0f;
				blend->bIsPlayingCustomAnim =
					(state.flags & AnimationBlendCustom) != 0;
				for (int32_t index = 0;
					index < blend->TargetWeight.size(); ++index)
				{
					blend->TargetWeight.at(index) =
						index == child ? 1.0f : 0.0f;
				}
				ApplyBlendChildWeights(blend, child, nodeWeight);
				return true;
			}
			else if (node->IsA(UAnimNodeBlendList::StaticClass()))
			{
				auto* blend = reinterpret_cast<UAnimNodeBlendList*>(node);
				if (child < 0 || child >= blend->Children.size())
					return false;
				if (blend->ActiveChildIndex != child)
					blend->SetActiveChild(child, 0.0f);
				blend->ActiveChildIndex = child;
				blend->BlendTimeToGo = 0.0f;
				for (int32_t index = 0;
					index < blend->TargetWeight.size(); ++index)
				{
					blend->TargetWeight.at(index) =
						index == child ? 1.0f : 0.0f;
				}
				ApplyBlendChildWeights(blend, child, nodeWeight);
				return true;
			}
			return false;
		}

		bool ApplyAnimationSequence(AAlicePawn* remote,
			const AnimationSequencePayload& state,
			bool synchronizePosition)
		{
			USkeletalMeshComponent* component =
				ResolveRemoteAnimationComponent(remote, state.component);
			if (!component
				|| state.nodeIndex >= component->AnimTickArray.size())
			{
				return false;
			}
			UAnimNode* node = component->AnimTickArray.at(state.nodeIndex);
			if (!node || !node->IsA(UAnimNodeSequence::StaticClass())
				|| !state.sequenceName[0])
			{
				return false;
			}

			auto* sequence = reinterpret_cast<UAnimNodeSequence*>(node);
			const bool nameMatches = sequence->AnimSeqName.IsValid()
				&& _stricmp(sequence->AnimSeqName.ToString().c_str(),
					state.sequenceName) == 0;
			FName sequenceName;
			if (!ResolveExistingAnimationName(component,
				state.sequenceName, sequenceName))
				return false;

			const float rate = std::isfinite(state.rate)
				&& std::fabs(state.rate) > 0.001f
				? state.rate
				: 1.0f;
			const float position = std::isfinite(state.position)
				&& state.position >= 0.0f
				? state.position
				: 0.0f;
			const bool looping =
				(state.flags & AnimationSequenceLooping) != 0;
			const float nodeWeight = std::isfinite(state.nodeWeight)
				? std::clamp(state.nodeWeight, 0.0f, 1.0f)
				: 0.0f;

			// Presentation nodes must never send animation notifies back into
			// gameplay or into the locally controlled pawn.
			sequence->bNoNotifies = true;
			sequence->bCauseActorAnimEnd = false;
			sequence->bCauseActorAnimPlay = false;
			sequence->NodeTotalWeight = nodeWeight;
			sequence->TotalWeightAccumulator = nodeWeight;
			sequence->bRelevant =
				(state.flags & AnimationSequenceRelevant) != 0
				|| nodeWeight > 0.001f;
			if (!nameMatches)
				NativeSetAnim(sequence, sequenceName);
			if (!nameMatches || !sequence->bPlaying)
				NativePlayAnim(sequence, looping, rate, position);
			sequence->Rate = rate;
			sequence->bLooping = looping;
			sequence->bPlaying = true;
			if (synchronizePosition)
			{
				NativeSetPosition(sequence, position);
				sequence->CurrentTime = position;
				sequence->PreviousTime = position;
			}
			return true;
		}

		void StopRemoteAnimationSequence(AAlicePawn* remote,
			std::uint32_t key)
		{
			const auto component = static_cast<AnimationComponent>(
				(key >> 16) & 0xffu);
			const auto nodeIndex =
				static_cast<std::uint16_t>(key & 0xffffu);
			USkeletalMeshComponent* skeletal =
				ResolveRemoteAnimationComponent(remote, component);
			if (!skeletal || nodeIndex >= skeletal->AnimTickArray.size())
				return;
			UAnimNode* node = skeletal->AnimTickArray.at(nodeIndex);
			if (node && node->IsA(UAnimNodeSequence::StaticClass()))
				reinterpret_cast<UAnimNodeSequence*>(node)->StopAnim();
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
			return _strnicmp(state.sequenceName, "Alice", 5) == 0;
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

		bool IsSafeUpperAdditiveSequence(
			const AnimationSequencePayload& state)
		{
			if (state.component != AnimationComponent::Body
				|| !state.sequenceName[0])
			{
				return false;
			}
			// The authored combat-upper slot uses explicitly named ADD clips for
			// most weapons, but the pepper grinder and teapot cannon put their
			// ordinary AliceW_WP3/WP4 fire poses in that same slot. The comparison
			// trace proves these arrive with the matching custom-slot child; dropping
			// them left the proxy in locomotion while only its weapon animated.
			// Keep the admission narrow: generic poses in this slot caused the old
			// ref-pose and stretched-limb failures.
			return _strnicmp(state.sequenceName,
				"ADD_AliceW_WP", 13) == 0
				|| _strnicmp(state.sequenceName,
					"AliceW_WP3_", 11) == 0
				|| _strnicmp(state.sequenceName,
					"AliceW_WP4_", 11) == 0;
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
			const AnimationGraphPayload& graph,
			const USkeletalMeshComponent* component,
			const UAliceGameAnimNode_BlendBySlot* slot,
			int32_t childIndex)
		{
			if (!component || !slot || childIndex <= 0
				|| childIndex >= slot->Children.size())
			{
				return nullptr;
			}

			const int32_t childNodeIndex = FindAnimationNodeIndex(
				component, slot->Children.at(childIndex).Anim);
			const std::size_t sequenceCount = std::min<std::size_t>(
				graph.sequenceCount, MaxAnimationSequences);
			const AnimationSequencePayload* fallback = nullptr;
			for (std::size_t index = 0; index < sequenceCount; ++index)
			{
				const AnimationSequencePayload& state =
					graph.sequences[index];
				if (!IsSafeFullBodySequence(state))
					continue;
				if (RejectGroundedAirSequence(state))
					continue;
				if (childNodeIndex >= 0
					&& state.nodeIndex == childNodeIndex)
				{
					return &state;
				}
				if ((state.flags & AnimationSequenceCustom) != 0
					&& (!fallback
						|| state.nodeWeight > fallback->nodeWeight))
				{
					fallback = &state;
				}
			}
			return fallback;
		}

		const AnimationSequencePayload* FindUpperAdditiveSequenceForSlot(
			const AnimationGraphPayload& graph,
			const USkeletalMeshComponent* component,
			const UAliceGameAnimNode_BlendBySlot* slot,
			int32_t childIndex)
		{
			if (!component || !slot || childIndex <= 0
				|| childIndex >= slot->Children.size())
			{
				return nullptr;
			}
			const int32_t childNodeIndex = FindAnimationNodeIndex(
				component, slot->Children.at(childIndex).Anim);
			const std::size_t sequenceCount = std::min<std::size_t>(
				graph.sequenceCount, MaxAnimationSequences);
			const AnimationSequencePayload* fallback = nullptr;
			for (std::size_t index = 0; index < sequenceCount; ++index)
			{
				const AnimationSequencePayload& state =
					graph.sequences[index];
				if (!IsSafeUpperAdditiveSequence(state))
					continue;
				if (childNodeIndex >= 0
					&& state.nodeIndex == childNodeIndex)
				{
					return &state;
				}
				// The cloned pawn can allocate its three dynamic upper-slot
				// sequence nodes at different AnimTickArray indices than the owner.
				// The source slot/child is still authoritative, so fall back to its
				// strongest safe custom clip instead of discarding WP3/WP4 entirely.
				if ((state.flags & AnimationSequenceCustom) != 0
					&& (!fallback
						|| state.nodeWeight > fallback->nodeWeight))
				{
					fallback = &state;
				}
			}
			return fallback;
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
			const float rate = std::isfinite(state.rate)
				&& std::fabs(state.rate) > 0.001f
				? state.rate : 1.0f;
			const float blendTime = PresentationBlendTime(state);
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
			channel.config.BlendOutTime = blendTime;
			channel.config.PlayRate = rate;
			channel.config.bLoop = looping;
			channel.config.bCauseActorAnimEnd = false;
			channel.config.bTriggerFakeRootMotion = false;
			channel.config.bNotExtendAnimTimeForFakeRootMotion = true;
			channel.config.AnimPlayType = static_cast<std::uint8_t>(
				EConfigAnimPlayType::ECAPT_OneByOne);
			channel.config.FakeRootMotionMode = 0;

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
		};

		bool ApplySafeSlotSequence(AAlicePawn* remote,
			UAliceGameAnimNode_BlendBySlot* slot, int32_t targetChild,
			UAnimNodeSequence* sequence, const AnimationSequencePayload& state,
			bool synchronizePosition, bool forcePosition,
			PresentationSequenceKind kind)
		{
			const bool safe = kind == PresentationSequenceKind::FullBody
				? IsSafeFullBodySequence(state)
				: IsSafeUpperAdditiveSequence(state);
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
					<< ", sourceNode=" << state.nodeIndex
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
			const float rate = std::isfinite(state.rate)
				&& std::fabs(state.rate) > 0.001f
				? state.rate : 1.0f;
			const float sourcePosition = std::isfinite(state.position)
				&& state.position >= 0.0f
				? state.position : 0.0f;

			sequence->bNoNotifies = true;
			sequence->bCauseActorAnimEnd = false;
			sequence->bCauseActorAnimPlay = false;
			sequence->bForceRefposeWhenNotPlaying = false;
			if (!nameMatches)
				NativeSetAnim(sequence, sequenceName);
			if (!sequence->AnimSeq)
			{
				logFailure("animseq-null-after-setanim");
				return false;
			}
			const float sequenceLength = sequence->AnimSeq->SequenceLength;
			float position = sourcePosition;
			float presentationRate = rate;
			if (!looping && sequenceLength > 0.001f)
			{
				// UE3 resets a completed non-looping dynamic child to time zero.
				// The owner's slot may keep reporting that child while it blends out,
				// including a position a little beyond SequenceLength. Never feed that
				// overrun to the presentation slot.
				position = (std::min)(position, sequenceLength - 0.001f);
				// Leave enough headroom for one network sample. Otherwise a fast
				// x2/x3 attack reaches the native end between packets and UE3 resets
				// it to frame zero before the next combo child is announced.
				const float endGuard = (std::max)(0.030f,
					std::fabs(rate) * 0.040f);
				if (sequenceLength - position <= endGuard)
					presentationRate = 0.0f;
			}
			if (!nameMatches)
				NativePlayAnim(sequence, looping, presentationRate, position);
			else if (!sequence->bPlaying && synchronizePosition)
				NativePlayAnim(sequence, looping, presentationRate, position);

			sequence->Rate = presentationRate;
			sequence->bLooping = looping;
			sequence->bPlaying = true;
			if (forcePosition
				|| (!looping && presentationRate == 0.0f))
			{
				// This function also runs after Alice's native animation tick.
				// Re-assert a guarded non-looping endpoint there: UE3 otherwise
				// resets a completed dynamic slot to frame zero for one frame,
				// producing a visible end-of-attack teleport/jitter.
				NativeSetPosition(sequence, position);
				sequence->CurrentTime = position;
				sequence->PreviousTime = position;
			}
			else if (synchronizePosition && nameMatches)
			{
				float difference = position - sequence->CurrentTime;
				const float length = sequenceLength;
				if (looping && length > 0.001f)
				{
					while (difference > length * 0.5f)
						difference -= length;
					while (difference < -length * 0.5f)
						difference += length;
				}
				// Let the engine advance every render frame. Network samples
				// only correct meaningful drift instead of pinning both time
				// fields to a 20 Hz packet.
				if (std::fabs(difference) > 0.075f)
				{
					NativeSetPosition(sequence, position);
					sequence->CurrentTime = position;
					sequence->PreviousTime = position;
				}
			}

			if (kind == PresentationSequenceKind::FullBody)
			{
				g_remoteAppliedFullBodyNode = sequence;
				g_remoteAppliedFullBodySlot = slot;
				g_remoteAppliedFullBodyName = state.sequenceName;
			}
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
					<< ", sourceNode=" << state.nodeIndex
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

		void EnableRemoteAnimationTick(AAlicePawn* remote)
		{
			if (!remote)
				return;
			auto enable = [](USkeletalMeshComponent* component)
			{
				if (!component || !component->Animations)
					return;
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

		bool ApplyRemoteUpperAdditiveGraph(AAlicePawn* remote,
			const AnimationGraphPayload& graph, bool synchronizePosition)
		{
			int32_t slotNodeIndex = -1;
			UAliceGameAnimNode_BlendBySlot* slot =
				ResolveRemoteConfigSlot(remote,
					EAnimBlendNodeIndex::
						EABLIdx_Slot_Combat_Upper_Additive,
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
					&& state.nodeIndex == slotNodeIndex)
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
					FindUpperAdditiveSequenceForSlot(graph,
						remote->Mesh, slot, sourceChild);
			}

			bool applied = false;
			bool configStarted = false;
			if (sourceSequence)
			{
				if (synchronizePosition)
				g_remoteUpperAdditiveChannel.lastSourceSeen =
					Clock::now();
				const int32_t configIndex = static_cast<int32_t>(
					EAnimBlendNodeIndex::
						EABLIdx_Slot_Combat_Upper_Additive);
				const bool selectionChanged =
					!g_remoteUpperAdditiveChannel.active
					|| g_remoteUpperAdditiveChannel.blendNodeIndex
						!= configIndex
					|| _stricmp(
						g_remoteUpperAdditiveChannel.sequenceName.c_str(),
						sourceSequence->sequenceName) != 0
					|| g_remoteUpperAdditiveChannel.targetChild <= 0;
				int32_t targetChild =
					g_remoteUpperAdditiveChannel.targetChild;
				if (selectionChanged)
				{
					configStarted = StartPresentationConfigAnimation(
						remote, g_remoteUpperAdditiveChannel,
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
					if (slot->ActiveChildIndex != targetChild)
					{
						// SetActiveChild can reset the dynamic sequence clock. Select
						// the child first, then restore the authoritative source time.
						NativeSetActiveChild(slot, targetChild,
							configStarted
								? g_remoteUpperAdditiveChannel.config.BlendInTime
								: PresentationBlendTime(
									sourceSequence->sequenceName));
					}
					applied = ApplySafeSlotSequence(remote, slot,
						targetChild, targetSequence, *sourceSequence,
						synchronizePosition, configStarted,
						PresentationSequenceKind::UpperAdditive);
					if (applied)
					{
						slot->bIsPlayingCustomAnim = true;
						slot->bPlayActiveChild = true;
						g_remoteUpperAdditiveChannel.active = true;
						g_remoteUpperAdditiveChannel.blendNodeIndex =
							configIndex;
						g_remoteUpperAdditiveChannel.targetChild =
							targetChild;
						g_remoteUpperAdditiveChannel.sourceChild =
							sourceChild;
						g_remoteUpperAdditiveChannel.sequenceName =
							sourceSequence->sequenceName;
						g_remoteUpperAdditiveChannel.lastSourcePosition =
							sourceSequence->position;
						g_remoteUpperAdditiveChannel.lastSourceRate =
							sourceSequence->rate;
						if (synchronizePosition)
						{
							g_remoteUpperAdditiveChannel.lastSourceSeen =
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
				&& g_remoteUpperAdditiveChannel.active
				&& g_remoteUpperAdditiveChannel.targetChild > 0
				&& g_remoteUpperAdditiveChannel.targetChild
					< slot->Children.size()
				&& g_remoteUpperAdditiveChannel.lastSourceSeen
					!= Clock::time_point{}
				&& Clock::now()
					- g_remoteUpperAdditiveChannel.lastSourceSeen
					< std::chrono::milliseconds(140))
			{
				slot->bIsPlayingCustomAnim = true;
				slot->bPlayActiveChild = true;
				applied = true;
			}
			if (!applied)
			{
				StopPresentationConfigAnimation(
					remote, g_remoteUpperAdditiveChannel, 0.08f);
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
			const float rate = std::isfinite(source->rate)
				&& std::fabs(source->rate) > 0.001f
				? source->rate : 1.0f;
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
					sequence->Rate = rate;
					sequence->bLooping = looping;
					const float position =
						std::isfinite(source->position)
							&& source->position >= 0.0f
						? source->position : 0.0f;
					if (synchronizePosition
						&& std::fabs(
							position - sequence->CurrentTime)
							> 0.075f)
					{
						NativeSetPosition(sequence, position);
					}
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

		bool ApplyRemoteAnimationGraph(AAlicePawn* remote,
			const AnimationGraphPayload& graph, bool synchronizePosition)
		{
			g_remoteAppliedFullBodyNode = nullptr;
			g_remoteAppliedFullBodySlot = nullptr;
			g_remoteAppliedFullBodyName.clear();
			if (!remote || !remote->Mesh)
				return false;

			EnableRemoteAnimationTick(remote);
			int32_t slotNodeIndex = -1;
			UAliceGameAnimNode_BlendBySlot* slot =
				ResolveRemoteFullBodySlot(remote, &slotNodeIndex);
			if (!slot)
				return false;

			const std::size_t blendCount = std::min<std::size_t>(
				graph.blendCount, MaxAnimationBlends);
			const std::size_t sequenceCount = std::min<std::size_t>(
				graph.sequenceCount, MaxAnimationSequences);
			ApplyRemoteWeaponTypeBlends(remote, graph);
			ApplyRemoteUpperAdditiveGraph(remote, graph,
				synchronizePosition);
			ApplyRemoteWeaponGraph(remote, graph,
				synchronizePosition);
			const AnimationBlendPayload* sourceSlot = nullptr;
			for (std::size_t index = 0; index < blendCount; ++index)
			{
				const AnimationBlendPayload& state = graph.blends[index];
				if (state.component == AnimationComponent::Body
					&& state.nodeIndex == slotNodeIndex)
				{
					sourceSlot = &state;
					break;
				}
			}

			int32_t sourceChild = 0;
			const AnimationSequencePayload* sourceSequence = nullptr;
			const bool sourceCustomSlot = sourceSlot
				&& (sourceSlot->flags & AnimationBlendCustom) != 0
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
					|| g_remoteFullBodyChannel.targetChild <= 0;
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
					if (slot->ActiveChildIndex != targetChild)
					{
						// NativeSetActiveChild may zero a completed dynamic child.
						// Switch first so ApplySafeSlotSequence is the final writer of
						// its playback position for this frame.
						NativeSetActiveChild(slot, targetChild,
							configStarted
								? g_remoteFullBodyChannel.config.BlendInTime
								: PresentationBlendTime(
									sourceSequence->sequenceName));
					}
					applied = ApplySafeSlotSequence(remote, slot,
						targetChild, targetSequence, *sourceSequence,
						synchronizePosition, configStarted,
						PresentationSequenceKind::FullBody);
					if (applied)
					{
						slot->bIsPlayingCustomAnim = true;
						slot->bPlayActiveChild = true;
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
						g_remoteFullBodyChannel.lastSourcePosition);
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
				StopPresentationConfigAnimation(
					remote, g_remoteFullBodyChannel, 0.10f);
				slot->bIsPlayingCustomAnim = false;
				slot->bPlayActiveChild = false;
				if (slot->ActiveChildIndex != 0)
					NativeSetActiveChild(slot, 0, 0.10f);
			}

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
						<< sequence.nodeIndex << '='
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
				marker->SetRotation(rotation);
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
				if (static_cast<std::uint8_t>(
						weapon->WeaponTypeEnum)
					== static_cast<std::uint8_t>(
						EAliceWeaponType::EAWT_HobbyHorse)
					&& g_remotePresentation.valid)
				{
					// HobbyHorse shockwaves are ground-space effects. On the
					// proxy graph RadiusDamage may resolve to the package
					// origin (0,0) even though Z looks animated, so reject
					// implausible XY as well.
					const FVector root =
						g_remotePresentation.location;
					const float offsetX = location.X - root.X;
					const float offsetY = location.Y - root.Y;
					const bool invalidAnchor =
						!WeaponMeshHasAttachPoint(
							weapon, socket);
					const bool invalidXY =
						!std::isfinite(location.X)
						|| !std::isfinite(location.Y)
						|| offsetX * offsetX + offsetY * offsetY
							> 500.0f * 500.0f;
					if (invalidAnchor || invalidXY)
					{
						location.X = root.X;
						location.Y = root.Y;
					}
					// The presentation weapon often reproduces only the
					// relaxed HobbyHorse pose, leaving RadiusDamage close to
					// Alice's capsule. Preserve the socket's swing direction
					// but extend a too-short vector to the approximate head
					// contact distance. If the socket is unusable, fall back
					// to Alice's facing direction.
					float impactX = location.X - root.X;
					float impactY = location.Y - root.Y;
					float impactDistance = std::sqrt(
						impactX * impactX
						+ impactY * impactY);
					constexpr float MinimumImpactDistance = 110.0f;
					if (!std::isfinite(impactDistance)
						|| impactDistance < MinimumImpactDistance)
					{
						if (impactDistance > 2.0f)
						{
							impactX /= impactDistance;
							impactY /= impactDistance;
						}
						else
						{
							constexpr float RotatorToRadians =
								6.2831853071795864769f
								/ 65536.0f;
							const float yaw =
								static_cast<float>(
									g_remotePresentation.rotation.Yaw)
								* RotatorToRadians;
							impactX = std::cos(yaw);
							impactY = std::sin(yaw);
						}
						location.X = root.X
							+ impactX * MinimumImpactDistance;
						location.Y = root.Y
							+ impactY * MinimumImpactDistance;
					}
					const float collisionHeight =
						std::isfinite(
							g_remotePresentation.state
								.collisionHeight)
						&& g_remotePresentation.state
							.collisionHeight > 1.0f
						? g_remotePresentation.state
							.collisionHeight
						: 79.5f;
					// APawn.Location is the capsule centre, approximately
					// Alice's pelvis. Shockwave particles are authored in
					// ground space, so project the animated RadiusDamage XY
					// down by the current capsule half-height.
					location.Z =
						root.Z - collisionHeight + 2.0f;
					rotation.Pitch = 0;
					rotation.Roll = 0;
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
			component->HiddenGame = false;
			NativeSetComponentHidden(component, false);
			if (needsActivation)
				component->ActivateSystem(false);
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
				if (trail && g_remotePresentationWeapon->Mesh
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
				if (!g_remoteDodgeParticleTemplate)
				{
					g_remoteDodgeParticleTemplate =
						FindRemoteDodgeParticleTemplate();
					Log("VFXSTAGE dodge template="
						+ ObjectName(
							g_remoteDodgeParticleTemplate) + '.');
				}
				g_remoteDodgeParticleUntil =
					Clock::now() + std::chrono::milliseconds(750);
				g_nextRemoteDodgeParticle = Clock::now();
				// The action packet is the authoritative presentation
				// trigger. AnimGraph packets may arrive late or briefly select
				// the transition nodes that do not contain "Dodge" in their
				// name, so keep an explicit hide window as well.
				g_devDodgeHideUntil =
					Clock::now() + std::chrono::milliseconds(850);
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

		void DispatchRemoteVisualNotify(AAlicePawn* remote,
			USkeletalMeshComponent* sourceComponent,
			UAnimNotify* notify, UAnimSequence* sequence,
			int32_t notifyIndex)
		{
			if (!remote || !sourceComponent || !notify)
				return;

			// Dynamic slot nodes can be rebuilt several times while the same
			// replicated action is active. Suppress duplicate presentation
			// notifies without suppressing a later attack using the same asset.
			const std::uint64_t key =
				(static_cast<std::uint64_t>(
					reinterpret_cast<std::uintptr_t>(sequence)) << 1)
				^ (static_cast<std::uint64_t>(
					reinterpret_cast<std::uintptr_t>(notify)) << 17)
				^ static_cast<std::uint64_t>(
					static_cast<std::uint32_t>(notifyIndex));
			const Clock::time_point now = Clock::now();
			auto recent = g_recentRemoteVisualNotifies.find(key);
			if (recent != g_recentRemoteVisualNotifies.end()
				&& now - recent->second
					< std::chrono::milliseconds(450))
			{
				return;
			}
			g_recentRemoteVisualNotifies[key] = now;

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
				auto* dodge = reinterpret_cast<
					UAliceAnimNotify_TriggerAliceDodgeParticle*>(notify);
				if (dodge->AliceDodgeEffectTemplate)
					g_remoteDodgeParticleTemplate =
						dodge->AliceDodgeEffectTemplate;
				g_remoteDodgeParticleUntil =
					now + std::chrono::milliseconds(750);
				g_nextRemoteDodgeParticle = now;
				g_lastRemoteVfx = "dodge notify: "
					+ ObjectName(g_remoteDodgeParticleTemplate);
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
					UParticleSystem* weaponParticle =
						g_remotePresentationWeapon->
							eventGetWeaponLeveDateEffect(
								weaponNotify->WeaponEffectIndex);
					if (weaponParticle)
						particleTemplate = weaponParticle;
					if (g_remotePresentationWeapon->Mesh)
						attachMesh =
							g_remotePresentationWeapon->Mesh;
				}
				if (!particleTemplate)
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

		void UpdateRemoteVisualNotifies(AAlicePawn* remote)
		{
			UAnimNodeSequence* node = g_remoteAppliedFullBodyNode;
			if (!remote || !node || !node->AnimSeq
				|| !node->SkelComponent)
			{
				g_remoteFullBodyNotifyCursor = {};
				return;
			}

			VisualNotifyCursor& cursor =
				g_remoteFullBodyNotifyCursor;
			UAnimSequence* sequence = node->AnimSeq;
			const float current =
				node->CurrentTime > 0.0f ? node->CurrentTime : 0.0f;
			float from = cursor.previousTime;
			const bool changed = !cursor.primed
				|| cursor.node != node
				|| cursor.sequence != sequence;
			if (changed)
			{
				cursor.node = node;
				cursor.sequence = sequence;
				cursor.primed = true;
				from = -0.001f;
				if (g_config.actionTrace)
				{
					Log("VFXSTAGE inspect animation="
						+ ObjectName(sequence)
						+ ", notifies="
						+ std::to_string(sequence->Notifies.size()) + '.');
				}
				if (g_config.actionTrace && g_loggedNotifyInventories
					.insert(sequence).second)
				{
					const int32_t inventoryCount =
						std::min<int32_t>(
							sequence->Notifies.size(), 64);
					for (int32_t index = 0;
						index < inventoryCount; ++index)
					{
						const FAnimNotifyEvent& notifyEvent =
							sequence->Notifies.at(index);
						UAnimNotify* notify =
							notifyEvent.Notify;
						std::ostringstream inventory;
						inventory << "VFXNOTIFY animation="
							<< ObjectName(sequence)
							<< ", index=" << index
							<< ", time=" << notifyEvent.Time
							<< ", notify="
							<< ObjectName(notify)
							<< ", class="
							<< ObjectName(
								notify ? notify->Class : nullptr);
						if (notify && notify->IsA(
							UAnimNotify_PlayParticleEffect::
								StaticClass()))
						{
							auto* particle = reinterpret_cast<
								UAnimNotify_PlayParticleEffect*>(
									notify);
							inventory << ", template="
								<< ObjectName(
									particle->PSTemplate)
								<< ", attach="
								<< particle->bAttach;
						}
						inventory << '.';
						Log(inventory.str());
					}
				}
			}
			else if (current + 0.075f < from)
			{
				// Network correction on a non-looping action must not replay
				// already emitted effects. A real looping wrap starts a fresh
				// notify interval.
				if (node->bLooping && sequence->SequenceLength > 0.0f
					&& from > sequence->SequenceLength * 0.65f
					&& current < sequence->SequenceLength * 0.35f)
				{
					from = -0.001f;
				}
				else
				{
					return;
				}
			}

			const int32_t count = std::min<int32_t>(
				sequence->Notifies.size(), 256);
			for (int32_t index = 0; index < count; ++index)
			{
				const FAnimNotifyEvent& event =
					sequence->Notifies.at(index);
				if (!event.Notify || event.Time <= from
					|| event.Time > current + 0.002f)
				{
					continue;
				}
				DispatchRemoteVisualNotify(remote,
					node->SkelComponent, event.Notify,
					sequence, index);
			}
			cursor.previousTime = from > current ? from : current;
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

			if (!remote || !g_remoteDodgeParticleTemplate
				|| now >= g_remoteDodgeParticleUntil
				|| (!dodgeVisualHidden
					&& now + std::chrono::milliseconds(120)
						< g_remoteDodgeParticleUntil))
			{
				return;
			}
			if (now < g_nextRemoteDodgeParticle)
				return;
			g_nextRemoteDodgeParticle =
				now + std::chrono::milliseconds(130);
			UParticleSystemComponent* particle =
				SpawnRemoteCosmeticParticle(remote,
					g_remoteDodgeParticleTemplate, false);
			MakePresentationParticleVisible(particle);
			TrackRemoteWeaponTransient(
				particle, g_remoteDodgeParticleTemplate,
				std::chrono::milliseconds(1000));
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
				component->HiddenGame = value;
				NativeSetComponentHidden(component, value);
			};

			setHidden(remote->Mesh, actorHidden
				|| (state.flags & StateBodyHidden) != 0);
			setHidden(remote->UpperBodyComponent, actorHidden
				|| (state.flags & StateUpperBodyHidden) != 0);
			// The pawn-owned strand component remains dormant. The proxy's
			// skeletal scalp is only an invisible head-space anchor; its
			// independent HairComponent supplies the visible strands.
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
			remote->SetLocationNoCheck(presentation.location);
			remote->SetRotation(presentation.rotation);
			remote->Location = presentation.location;
			remote->Rotation = presentation.rotation;
			remote->Velocity = FVector(state.velocity[0], state.velocity[1], state.velocity[2]);
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
					remote->Mesh->SetTranslation(meshTranslation);
					remote->Mesh->Translation = meshTranslation;
				}
				if (std::isfinite(state.meshScale) && state.meshScale > 0.01f)
				{
					remote->Mesh->SetScale(state.meshScale);
					remote->Mesh->Scale = state.meshScale;
				}
				if (std::isfinite(meshScale3D.X) && std::isfinite(meshScale3D.Y)
					&& std::isfinite(meshScale3D.Z))
				{
					remote->Mesh->SetScale3D(meshScale3D);
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
						NativeDetachActorComponent(remote, cloth);
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
					blend->SetActiveChild(0, 0.08f);
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
					if (blend->Children.size() > 4)
						blend->SetActiveChild(4, 0.08f);
					++fallNodes;
				}
				else if (node->IsA(
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
						blend->SetActiveChild(0, 0.05f);
					++legNodes;
				}
				else if (node->IsA(
					UAliceGameAnimNode_BlendByAliceWeaponType::StaticClass()))
				{
					// Re-entering a melee locomotion branch after Float can preserve
					// its last partial lower-body pose. Rebuild that branch from the
					// neutral child using the network-authoritative weapon type.
					auto* blend = reinterpret_cast<
						UAliceGameAnimNode_BlendByAliceWeaponType*>(node);
					const int32_t target = static_cast<int32_t>(
						g_remotePresentationWeaponType);
					if (target >= 0 && target < blend->Children.size())
					{
						if (target != 0)
							blend->SetActiveChild(0, 0.0f);
						blend->SetActiveChild(target, 0.08f);
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
			const bool dodgeVisualHidden =
				!g_config.localMirror
				&& ((g_activeRemoteAnimationGraph.has_value()
						&& g_remoteFullBodyChannel.active
						&& ContainsCaseInsensitive(
							g_remoteFullBodyChannel.sequenceName,
							"dodge"))
					|| Clock::now() < g_devDodgeHideUntil);
			if (dodgeVisualHidden != g_remoteDodgeVisualHidden)
			{
				g_remoteDodgeVisualHidden = dodgeVisualHidden;
				if (dodgeVisualHidden)
				{
					if (!g_remoteDodgeParticleTemplate)
						g_remoteDodgeParticleTemplate =
							FindRemoteDodgeParticleTemplate();
					g_remoteDodgeParticleUntil =
						Clock::now()
						+ std::chrono::milliseconds(800);
					g_nextRemoteDodgeParticle = Clock::now();
					g_lastRemoteVfx =
						"dodge animation: "
						+ ObjectName(
							g_remoteDodgeParticleTemplate);
				}
				Log(std::string("VFXSTAGE remote dodge proxy=")
					+ (dodgeVisualHidden
						? "hidden (butterfly placeholder)."
						: "shown."));
			}
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
			const bool hidden = (state.flags & StateVisible) == 0
				|| peerDead || hideForLocalCinematic
				|| dodgeVisualHidden
				|| g_devForceProxyHidden;
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
			SetRemoteRenderDetached(remote, hidden);
			g_remotePresentation.location = next;
			g_remotePresentation.rotation = rotation;
			g_remotePresentation.hidden = hidden;
			ApplyRemotePresentation(remote, g_remotePresentation, true);
			ApplyRemoteVisualAction(remote, state);
			UpdateRemoteVisualNotifies(remote);
			TickRemoteActionVfx(remote, dodgeVisualHidden);
			TickRemoteNativeGlideTrailGuard(remote);
			if (hidden)
			{
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
				if (!g_remoteHairProxy
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

		bool ConsumeTeleportKeyPress()
		{
			const bool inputEventRequested =
				g_teleportInputRequested.exchange(
					false, std::memory_order_acq_rel);
			DWORD foregroundProcessId = 0;
			const HWND foregroundWindow = GetForegroundWindow();
			if (foregroundWindow)
			{
				GetWindowThreadProcessId(
					foregroundWindow, &foregroundProcessId);
			}
			const bool foreground =
				foregroundProcessId == GetCurrentProcessId();
			bool pressed =
				foreground
				&& (GetAsyncKeyState('P') & 0x8000) != 0;
			// Physical key at the Latin P position. MapVirtualKey follows
			// the active keyboard layout, while the direct VK check covers
			// the normal English layout.
			const UINT physicalP = MapVirtualKeyW(
				0x19, MAPVK_VSC_TO_VK_EX);
			if (physicalP != 0)
			{
				pressed = pressed
					|| (foreground && (GetAsyncKeyState(
						static_cast<int>(physicalP))
						& 0x8000) != 0);
			}
			const bool edge = pressed && !g_teleportKeyDown;
			g_teleportKeyDown = pressed;
			return inputEventRequested || edge;
		}

		bool ConsumeForceTeleportKeyPress()
		{
			const bool inputEventRequested =
				g_forceTeleportInputRequested.exchange(
					false, std::memory_order_acq_rel);
			DWORD foregroundProcessId = 0;
			const HWND foregroundWindow = GetForegroundWindow();
			if (foregroundWindow)
			{
				GetWindowThreadProcessId(
					foregroundWindow, &foregroundProcessId);
			}
			const bool foreground =
				foregroundProcessId == GetCurrentProcessId();
			bool pressed =
				foreground
				&& (GetAsyncKeyState('O') & 0x8000) != 0;
			// Physical key at the Latin O position for non-English layouts.
			const UINT physicalO = MapVirtualKeyW(
				0x18, MAPVK_VSC_TO_VK_EX);
			if (physicalO != 0)
			{
				pressed = pressed
					|| (foreground && (GetAsyncKeyState(
						static_cast<int>(physicalO))
						& 0x8000) != 0);
			}
			const bool edge = pressed && !g_forceTeleportKeyDown;
			g_forceTeleportKeyDown = pressed;
			return inputEventRequested || edge;
		}

		void SetTeleportStatus(const std::string& status)
		{
			g_lastTeleportStatus = status;
			g_lastTeleportStatusUntil =
				Clock::now() + std::chrono::seconds(3);
			Log("TELEPORT " + status + '.');
		}

		bool TeleportLocalPawnToHost(AAlicePawn* localPawn,
			const PlayerStatePayload& hostState,
			const char* source,
			bool ignoreStreamingDistance = false)
		{
			if (!localPawn || g_currentMap.empty()
				|| _stricmp(hostState.mapName,
					g_currentMap.c_str()) != 0)
			{
				SetTeleportStatus(std::string(source)
					+ " rejected: host is not on the same map");
				return false;
			}
			const FVector hostLocation(
				hostState.location[0],
				hostState.location[1],
				hostState.location[2]);
			constexpr float MaximumSafeStreamingDistance =
				50000.0f;
			if (!ignoreStreamingDistance
				&& (hostLocation - localPawn->Location).SizeSquared()
				> MaximumSafeStreamingDistance
					* MaximumSafeStreamingDistance)
			{
				SetTeleportStatus(std::string(source)
					+ " rejected: peer is in another streaming "
					"region; wait for synchronized progression");
				return false;
			}
			constexpr float RotatorToRadians =
				6.2831853071795864769f / 65536.0f;
			const float yaw =
				static_cast<float>(hostState.rotation[1])
				* RotatorToRadians;
			constexpr float FollowDistance = 110.0f;
			FVector destination(
				hostState.location[0]
					- std::cos(yaw) * FollowDistance,
				hostState.location[1]
					- std::sin(yaw) * FollowDistance,
				hostState.location[2] + 12.0f);
			const FRotator rotation(
				hostState.rotation[0],
				hostState.rotation[1],
				hostState.rotation[2]);
			localPawn->Velocity = FVector(0.0f, 0.0f, 0.0f);
			localPawn->FarMoveSetLocation(destination, true);
			const float remainingDistanceSquared =
				(localPawn->Location - destination).SizeSquared();
			bool nativeLocationApplied = true;
			if (remainingDistanceSquared > 25.0f * 25.0f)
			{
				nativeLocationApplied =
					NativeSetActorLocationNoCheck(
						localPawn, destination);
			}
			const bool nativeRotationApplied =
				NativeSetActorRotation(localPawn, rotation);
			if (!nativeLocationApplied)
			{
				SetTeleportStatus(std::string(source)
					+ " failed: native SetLocationNoCheck rejected "
					"the destination");
				return false;
			}
			SetTeleportStatus(std::string(source)
				+ " success, destination="
				+ FormatWorldTraceVector(destination)
				+ ", actual="
				+ FormatWorldTraceVector(localPawn->Location)
				+ ", rotation="
				+ (nativeRotationApplied
					? std::string("ok")
					: std::string("unchanged")));
			return true;
		}

		void RequestHostSummonClient()
		{
			if (++g_hostCommandSerial == 0)
				++g_hostCommandSerial;
			g_hostCommandFlags = HostCommandSummonClient;
			g_hostCommandExpires =
				Clock::now() + std::chrono::seconds(2);
			SetTeleportStatus("host requested client summon, serial="
				+ std::to_string(g_hostCommandSerial));
		}

		void RequestHostCheckpointRestart(const char* source)
		{
			if (++g_hostCommandSerial == 0)
				++g_hostCommandSerial;
			g_hostCommandFlags = HostCommandRestartCheckpoint;
			g_hostCommandExpires =
				Clock::now() + std::chrono::seconds(30);
			g_hostRestartIssued = true;
			g_groupDeathActive = true;
			g_groupDeathSuppressUntil =
				Clock::now() + std::chrono::seconds(15);
			{
				// Keep transmitting the restart while the host is between
				// gameplay worlds and therefore cannot publish a fresh Tick.
				std::lock_guard lock(g_stateMutex);
				if (g_outboundHostSnapshot)
				{
					g_outboundHostSnapshot->commandSerial =
						g_hostCommandSerial;
					g_outboundHostSnapshot->commandFlags =
						g_hostCommandFlags;
				}
			}
			Log("GROUPLIFE host restart serial="
				+ std::to_string(g_hostCommandSerial)
				+ ", source="
				+ (source ? std::string(source)
					: std::string("<unknown>")) + '.');
		}

		void RequestClientCheckpointRestart(const char* source)
		{
			g_clientRestartRequestPending = true;
			g_groupDeathActive = true;
			g_groupDeathSuppressUntil =
				Clock::now() + std::chrono::seconds(15);
			{
				// The UI starts loading immediately after deathRestartGame.
				// Patch the last command as well so the network thread can
				// deliver the request even if no further gameplay Tick occurs.
				std::lock_guard lock(g_stateMutex);
				if (g_outboundClientCommand)
					g_outboundClientCommand->buttons
						|= ClientCommandRequestRestart;
			}
			Log("GROUPLIFE client requested host restart, source="
				+ (source ? std::string(source)
					: std::string("<unknown>")) + '.');
		}

		void RequestHostReturnToMenu(const char* source)
		{
			if (++g_hostCommandSerial == 0)
				++g_hostCommandSerial;
			g_hostCommandFlags = HostCommandReturnToMenu;
			g_hostCommandExpires =
				Clock::now() + std::chrono::seconds(30);
			g_hostReturnToMenuIssued = true;
			{
				std::lock_guard lock(g_stateMutex);
				if (g_outboundHostSnapshot)
				{
					g_outboundHostSnapshot->commandSerial =
						g_hostCommandSerial;
					g_outboundHostSnapshot->commandFlags =
						g_hostCommandFlags;
				}
			}
			Log("SESSION host return-to-menu serial="
				+ std::to_string(g_hostCommandSerial)
				+ ", source="
				+ (source ? std::string(source)
					: std::string("<unknown>")) + '.');
		}

		void RequestClientReturnToMenu(const char* source)
		{
			g_clientReturnToMenuRequestPending = true;
			{
				std::lock_guard lock(g_stateMutex);
				if (g_outboundClientCommand)
				{
					g_outboundClientCommand->buttons
						|= ClientCommandRequestReturnToMenu;
				}
			}
			Log("SESSION client requested return-to-menu, source="
				+ (source ? std::string(source)
					: std::string("<unknown>")) + '.');
		}

		UAliceGFxMovie_HUD* FindLocalHudMovie()
		{
			AAlicePawn* pawn = GetLocalPawn();
			AWorldInfo* world = pawn ? pawn->WorldInfo : nullptr;
			if (world && world->Game
				&& world->Game->IsA(AAliceGameInfo::StaticClass()))
			{
				auto* game =
					reinterpret_cast<AAliceGameInfo*>(world->Game);
				if (game->GFxHUDMenu
					&& IsLiveUObject(game->GFxHUDMenu))
				{
					return game->GFxHUDMenu;
				}
			}
			return nullptr;
		}

		bool TriggerLocalCheckpointRestart(const char* source)
		{
			UAliceGFxMovie_HUD* hud = FindLocalHudMovie();
			if (!hud)
			{
				Log("GROUPLIFE restart deferred; death HUD is not "
					"available, source="
					+ (source ? std::string(source)
						: std::string("<unknown>")) + '.');
				return false;
			}
			g_localRestartInitiated = true;
			g_applyingNetworkRestart = true;
			Log("GROUPLIFE invoking deathRestartGame from "
				+ (source ? std::string(source)
					: std::string("<unknown>")) + '.');
			hud->deathRestartGame();
			g_applyingNetworkRestart = false;
			return true;
		}

		UAliceGfxMovie_inGameMenu* FindLocalInGameMenu()
		{
			AAlicePawn* pawn = GetLocalPawn();
			AWorldInfo* world = pawn ? pawn->WorldInfo : nullptr;
			if (world && world->Game
				&& world->Game->IsA(AAliceGameInfo::StaticClass()))
			{
				auto* game =
					reinterpret_cast<AAliceGameInfo*>(world->Game);
				if (game->inGameMenu
					&& IsLiveUObject(game->inGameMenu))
				{
					return game->inGameMenu;
				}
			}
			return nullptr;
		}

		bool TriggerLocalReturnToMenu(const char* source)
		{
			AAlicePlayerController* controller =
				g_State.AlicePlayerController;
			if (!controller || !IsLiveUObject(controller))
			{
				Log("SESSION return-to-menu deferred; player "
					"controller is unavailable, source="
					+ (source ? std::string(source)
						: std::string("<unknown>")) + '.');
				return false;
			}
			g_localReturnToMenuInitiated = true;
			g_applyingNetworkReturnToMenu = true;
			g_lastTeleportStatus = "Peer left: loading main menu...";
			g_lastTeleportStatusUntil =
				Clock::now() + std::chrono::seconds(20);
			Log("SESSION backtoTitle from "
				+ (source ? std::string(source)
					: std::string("<unknown>")) + '.');
			controller->backtoTitle();
			g_applyingNetworkReturnToMenu = false;
			return true;
		}

		bool ApplyPendingLifecycleCommand()
		{
			if (!g_config.enabled || g_config.localMirror
				|| g_applyingNetworkRestart
				|| g_applyingNetworkReturnToMenu)
			{
				return false;
			}
			if (g_config.role == Role::Host)
			{
				const auto command = ReadFreshClientCommand();
				if (command
					&& (command->command.buttons
						& ClientCommandRequestReturnToMenu) != 0
					&& !g_hostReturnToMenuIssued)
				{
					RequestHostReturnToMenu("client request");
					if (!TriggerLocalReturnToMenu(
							"client return-to-menu request"))
					{
						g_hostReturnToMenuIssued = false;
						g_hostCommandFlags = 0;
						return false;
					}
					return true;
				}
				if (!command
					|| (command->command.buttons
						& ClientCommandRequestRestart) == 0
					|| g_hostRestartIssued)
				{
					return false;
				}
				RequestHostCheckpointRestart("client request");
				if (!TriggerLocalCheckpointRestart(
						"client restart request"))
				{
					g_hostRestartIssued = false;
					g_hostCommandFlags = 0;
					return false;
				}
				return true;
			}

			const auto snapshot = ReadFreshHostSnapshot();
			if (snapshot
				&& snapshot->snapshot.commandSerial != 0
				&& snapshot->snapshot.commandSerial
					!= g_lastSeenHostCommandSerial
				&& (snapshot->snapshot.commandFlags
					& HostCommandReturnToMenu) != 0)
			{
				if (!g_localReturnToMenuInitiated
					&& !TriggerLocalReturnToMenu(
						"host return-to-menu command"))
				{
					return false;
				}
				g_lastSeenHostCommandSerial =
					snapshot->snapshot.commandSerial;
				g_clientReturnToMenuRequestPending = false;
				Log("SESSION client accepted host return-to-menu "
					"command serial="
					+ std::to_string(
						snapshot->snapshot.commandSerial) + '.');
				return true;
			}
			if (!snapshot
				|| snapshot->snapshot.commandSerial == 0
				|| snapshot->snapshot.commandSerial
					== g_lastSeenHostCommandSerial
				|| (snapshot->snapshot.commandFlags
					& HostCommandRestartCheckpoint) == 0)
			{
				return false;
			}
			if (g_localRestartInitiated)
			{
				g_lastSeenHostCommandSerial =
					snapshot->snapshot.commandSerial;
				g_clientRestartRequestPending = false;
				Log("GROUPLIFE client acknowledged host restart "
					"after local Continue.");
				return false;
			}
			if (!TriggerLocalCheckpointRestart(
					"host restart command"))
			{
				return false;
			}
			g_lastSeenHostCommandSerial =
				snapshot->snapshot.commandSerial;
			g_clientRestartRequestPending = false;
			g_groupDeathActive = true;
			g_groupDeathSuppressUntil =
				Clock::now() + std::chrono::seconds(15);
			Log("GROUPLIFE client accepted host checkpoint command "
				"serial="
				+ std::to_string(
					snapshot->snapshot.commandSerial) + '.');
			return true;
		}

		bool ForceLocalGroupDeath(AAlicePawn* localPawn,
			const char* source)
		{
			if (!localPawn || localPawn->Health <= 0
				|| g_forcingGroupDeath
				|| g_localGroupDeathForced)
			{
				return false;
			}
			g_groupDeathActive = true;
			g_localGroupDeathForced = true;
			g_forcingGroupDeath = true;
			localPawn->Health = 0;
			UClass* damageType =
				UDmgType_Madcap_Melee::StaticClass();
			if (!damageType)
				damageType = UDamageType::StaticClass();
			const bool died = localPawn->Died(
				nullptr, damageType, localPawn->Location);
			g_forcingGroupDeath = false;
			Log("GROUPLIFE forced local death from "
				+ (source ? std::string(source)
					: std::string("<unknown>"))
				+ ", Died returned="
				+ (died ? std::string("true.")
					: std::string("false.")));
			return true;
		}

		void DrawStatus(AWorldInfo* world, bool peerVisible)
		{
			if (!g_config.showOnScreenStatus || Clock::now() < g_nextStatusDraw)
				return;
			g_nextStatusDraw = Clock::now() + std::chrono::milliseconds(100);

			std::wostringstream message;
			if (g_config.localMirror)
			{
				message << L"AliceCoop LOCAL MIRROR | duplicate: "
					<< (peerVisible ? L"visible" : L"waiting");
			}
			else
			{
				message << L"AliceCoop "
					<< (g_config.role == Role::Host ? L"HOST" : L"CLIENT")
					<< L" | relay: " << (g_connected ? L"connected" : L"connecting")
					<< L" | peer: " << (peerVisible ? L"visible" : L"waiting");
			}
			const std::wstring text = message.str();
			world->AddOnScreenDebugMessage(0x0A11CE, 0.15f,
				(g_config.localMirror || g_connected)
					? FColor(80, 255, 120)
					: FColor(255, 210, 80),
				FString(text.c_str()));

			std::wostringstream devStatus;
			if (g_config.role == Role::Host)
				devStatus << L"P: teleport to client";
			else
				devStatus << L"P: teleport to host | O: force teleport";
			if (g_config.worldTrace)
				devStatus << L" | ARENA TRACE ON";
			if (g_config.sharedEnemyHealth)
				devStatus << L" | SHARED ENEMY HP";
			if (g_config.sharedEnemyTransforms)
			{
				devStatus << L" | HOST ENEMY POSE";
				if (g_config.role == Role::Client)
				{
					devStatus << L" (AI paused "
						<< g_clientSharedEnemyBindings.size()
						<< L")";
				}
			}
			if (Clock::now() < g_lastTeleportStatusUntil)
			{
				devStatus << L" | "
					<< std::wstring(g_lastTeleportStatus.begin(),
						g_lastTeleportStatus.end());
			}
			const std::wstring devStatusText = devStatus.str();
			world->AddOnScreenDebugMessage(
				0x0A11D0, 0.15f,
				FColor(110, 210, 255),
				FString(devStatusText.c_str()));

			if (g_config.hairTuningEnabled)
			{
				std::wostringstream hairTune;
				hairTune << L"HairTune "
					<< (static_cast<int>(
						g_hairRotationCandidate) + 1)
					<< L"/"
					<< static_cast<int>(
						HairRotationCandidate::Count)
					<< L" "
					<< HairRotationCandidateWideName(
						g_hairRotationCandidate)
					<< L" | XYZ "
					<< std::fixed << std::setprecision(0)
					<< g_config.hairRotationX << L" "
					<< g_config.hairRotationY << L" "
					<< g_config.hairRotationZ
					<< L" | O/P X  L/; Y  ,/. Z  [ target";
				const std::wstring hairTuneText =
					hairTune.str();
				world->AddOnScreenDebugMessage(
					0x0A11CF, 0.15f,
					FColor(100, 210, 255),
					FString(hairTuneText.c_str()));
			}
		}
	}

	void DrawTuningOverlay(UCanvas* canvas)
	{
		if (!g_config.enabled || g_soloMinigameActive
			|| !canvas || !canvas->Canvas.Dummy)
		{
			AchievementOverlay::SetPeerWatchingCutscene(false);
			return;
		}

		const bool peerWatchingCutscene =
			g_remotePresentation.valid
			&& (g_remotePresentation.state.flags
				& StateCinematic) != 0;
		AchievementOverlay::SetPeerWatchingCutscene(
			peerWatchingCutscene);

		if (!g_config.hairTuningEnabled
			|| !g_tuningOverlayVisible)
		{
			return;
		}
		if (!g_loggedTuningOverlayRender)
		{
			g_loggedTuningOverlayRender = true;
			Log("TUNINGUI Canvas render path is active.");
		}

		const float scale = std::clamp(
			static_cast<float>(canvas->SizeY) / 1080.0f,
			0.72f, 1.15f);
		const float originX = 18.0f * scale;
		const float originY = 18.0f * scale;
		const float panelWidth = 660.0f * scale;
		const float panelHeight = 112.0f * scale;
		if (canvas->DefaultTexture)
		{
			canvas->SetPos(originX, originY, 0.0f);
			canvas->SetDrawColor(5, 10, 18, 190);
			canvas->DrawRect(
				panelWidth, panelHeight,
				canvas->DefaultTexture);
		}

		FFontRenderInfo renderInfo{};
		renderInfo.bClipText = false;
		renderInfo.bEnableShadow = true;
		canvas->SetDrawColor(130, 220, 255, 255);
		float lineY = originY + 8.0f * scale;
		const auto drawLine = [&](const std::wstring& text)
		{
			canvas->SetPos(
				originX + 10.0f * scale, lineY, 0.0f);
			canvas->DrawTextWin(
				FString(text.c_str()), false,
				scale, scale, renderInfo);
			lineY += 22.0f * scale;
		};

		std::wostringstream target;
		target << L"AliceCoop Tuning | Hair target "
			<< (static_cast<int>(g_hairRotationCandidate) + 1)
			<< L"/"
			<< static_cast<int>(HairRotationCandidate::Count)
			<< L" "
			<< HairRotationCandidateWideName(
				g_hairRotationCandidate);
		drawLine(target.str());

		std::wostringstream rotation;
		rotation << std::fixed << std::setprecision(0)
			<< L"Rotation  X " << g_config.hairRotationX
			<< L"  Y " << g_config.hairRotationY
			<< L"  Z " << g_config.hairRotationZ
			<< L"    O/P  L/;  ,/.";
		drawLine(rotation.str());

		std::wostringstream position;
		position << std::fixed << std::setprecision(1)
			<< L"Position  X " << g_config.hairOffsetX
			<< L"  Y " << g_config.hairOffsetY
			<< L"  Z " << g_config.hairOffsetZ
			<< L"    U/I  J/K  N/M";
		drawLine(position.str());
		drawLine(L"[ next target    ] hide/show");
	}

	void OnAlicePawnTicked(AAlicePawn* pawn)
	{
		if (IsRetiredRemotePawn(pawn))
		{
			HideAndDetachRetiredRemotePawn(pawn, false);
			return;
		}
		if (!g_config.enabled || g_soloMinigameActive
			|| pawn != g_remotePawn || !g_remotePresentation.valid)
			return;

		// AlicePawn.Tick rewrites scale, component visibility and some kinematic
		// fields. Reapply after that script tick, while still before world render.
		ApplyRemotePresentation(pawn, g_remotePresentation, false);
		if (g_config.localMirror)
		{
			BindLocalMirrorPose(pawn, GetLocalPawn());
		}
		else
		{
			if (g_activeRemoteAnimationGraph)
			{
				ApplyRemoteAnimationGraph(pawn,
					g_activeRemoteAnimationGraph->graph, false);
			}
		}
		// AlicePawn.Tick and its animation update can restore visibility and
		// cosmetic transforms after the regular coop update. Make this the
			// final presentation barrier for dodge hiding, skirt/scalp bindings
			// and independent hair repair.
			ForceRemoteCosmeticMasterPose(pawn);
			if (!g_loggedPostTickPresentation)
		{
			g_loggedPostTickPresentation = true;
			Log("Remote presentation hook is active after AlicePawn.Tick.");
		}
	}

	void RecordLocalAction(PlayerAction action)
	{
		if (!g_config.enabled || g_soloMinigameActive
			|| action == PlayerAction::None)
			return;
		g_localAction = static_cast<std::uint8_t>(action);
		const std::uint32_t serial = ++g_localActionSerial;
		if (g_config.actionTrace)
		{
			Log("Local action serial=" + std::to_string(serial)
				+ ", action="
				+ std::to_string(static_cast<int>(action)) + '.');
			ExtendActionTraceWindow();
			Log("TRACE MARK #" + std::to_string(++g_actionTraceMarker)
				+ " action=" + PlayerActionName(action)
				+ " (" + std::to_string(static_cast<int>(action)) + ").");
		}
	}

	void RecordHostLoadChapter(std::uint8_t chapter)
	{
		if (!g_config.enabled || g_config.localMirror
			|| g_config.role != Role::Host || chapter >= 70)
		{
			return;
		}
		g_hostRequestedChapter = static_cast<int>(chapter);
		Log("SESSIONJOIN captured host LoadChapter="
			+ std::to_string(g_hostRequestedChapter)
			+ ", activeMap="
			+ (g_currentMap.empty()
				? std::string("<none>") : g_currentMap)
			+ '.');
	}

	void ObservePauseMenuState(bool open)
	{
		if (!g_config.enabled)
			return;
		const bool previous = g_pauseMenuEventOpen.exchange(
			open, std::memory_order_acq_rel);
		g_pauseMenuEventSerial.fetch_add(1, std::memory_order_acq_rel);
		if (previous != open)
			Log(std::string("UI pause menu event=")
				+ (open ? "open." : "closed."));
	}

	void OnLocalPepperProjectileSpawn(
		APepperGrinderPrimaryProjectile* projectile,
		const FVector& direction)
	{
		if (!g_config.enabled || g_soloMinigameActive
			|| g_config.localMirror || !projectile
			|| projectile->IsDefaultObject())
		{
			return;
		}
		AAlicePawn* localPawn = GetLocalPawn();
		const bool ownedByLocal = localPawn
			&& (projectile->Instigator == localPawn
				|| (projectile->WeaponOwner
					&& projectile->WeaponOwner->Instigator
						== localPawn));
		if (!ownedByLocal)
			return;
		if (!PepperProjectileReplicationEnabled)
		{
			static bool loggedDisabled = false;
			if (!loggedDisabled)
			{
				loggedDisabled = true;
				Log("PROJECTILESTAGE pepper projectile replication "
					"disabled; remote muzzle presentation only.");
			}
			return;
		}

		const Clock::time_point now = Clock::now();
		for (auto iterator =
				g_recentLocalPepperProjectiles.begin();
			iterator != g_recentLocalPepperProjectiles.end();)
		{
			if (now - iterator->second > std::chrono::seconds(10))
				iterator =
					g_recentLocalPepperProjectiles.erase(iterator);
			else
				++iterator;
		}
		if (g_recentLocalPepperProjectiles.contains(projectile))
			return;
		g_recentLocalPepperProjectiles.emplace(projectile, now);

		FVector resolvedDirection = direction;
		auto normalizeDirection = [](FVector& value)
		{
			const float lengthSquared =
				value.X * value.X + value.Y * value.Y
				+ value.Z * value.Z;
			if (lengthSquared < 0.001f)
				return false;
			const float inverseLength =
				1.0f / std::sqrt(lengthSquared);
			value.X *= inverseLength;
			value.Y *= inverseLength;
			value.Z *= inverseLength;
			return true;
		};
		if (!normalizeDirection(resolvedDirection))
		{
			resolvedDirection = projectile->FlightFront;
			normalizeDirection(resolvedDirection);
		}
		if (!normalizeDirection(resolvedDirection))
		{
			resolvedDirection = projectile->Velocity;
			normalizeDirection(resolvedDirection);
		}
		if (!normalizeDirection(resolvedDirection))
		{
			constexpr float RotatorToRadians =
				6.28318530717958647692f / 65536.0f;
			const float pitch =
				static_cast<float>(projectile->Rotation.Pitch)
				* RotatorToRadians;
			const float yaw =
				static_cast<float>(projectile->Rotation.Yaw)
				* RotatorToRadians;
			const float cosPitch = std::cos(pitch);
			resolvedDirection = FVector(
				cosPitch * std::cos(yaw),
				cosPitch * std::sin(yaw),
				std::sin(pitch));
		}

		if (projectile->ProjFlightEffectTemplate)
			g_pepperFlightTemplate =
				projectile->ProjFlightEffectTemplate;
		else if (projectile->ProjFlightEffects
			&& projectile->ProjFlightEffects->Template)
		{
			g_pepperFlightTemplate =
				projectile->ProjFlightEffects->Template;
		}
		else if (projectile->ProjEffects
			&& projectile->ProjEffects->Template)
		{
			g_pepperFlightTemplate =
				projectile->ProjEffects->Template;
		}

		ProjectileEventPayload event{};
		event.kind = ProjectileEventKind::PepperSpawn;
		event.projectileId = g_localProjectileId++;
		if (event.projectileId == 0)
			event.projectileId = g_localProjectileId++;
		event.variant = static_cast<std::uint8_t>(
			std::clamp(projectile->WeaponLevel, 0, 255));
		UParticleSystemComponent* sourceParticle =
			projectile->ProjFlightEffects
				? projectile->ProjFlightEffects
				: projectile->ProjEffects;
		float particleScale = projectile->DrawScale;
		if (sourceParticle)
		{
			float componentScale3D =
				std::fabs(sourceParticle->Scale3D.X);
			componentScale3D = (std::max)(
				componentScale3D,
				std::fabs(sourceParticle->Scale3D.Y));
			componentScale3D = (std::max)(
				componentScale3D,
				std::fabs(sourceParticle->Scale3D.Z));
			componentScale3D = (std::max)(
				componentScale3D, 0.001f);
			particleScale *= sourceParticle->Scale
				* componentScale3D;
		}
		event.extra = particleScale;
		FillProjectileTransform(event, projectile);
		const float velocitySquared =
			event.velocity[0] * event.velocity[0]
			+ event.velocity[1] * event.velocity[1]
			+ event.velocity[2] * event.velocity[2];
		if (velocitySquared < 1.0f)
		{
			float fallbackSpeed = projectile->Speed;
			if (fallbackSpeed < 1.0f)
				fallbackSpeed = projectile->MaxSpeed;
			if (fallbackSpeed < 1.0f)
				fallbackSpeed = projectile->RefVel;
			if (fallbackSpeed < 1.0f)
				fallbackSpeed = 12000.0f;
			event.velocity[0] =
				resolvedDirection.X * fallbackSpeed;
			event.velocity[1] =
				resolvedDirection.Y * fallbackSpeed;
			event.velocity[2] =
				resolvedDirection.Z * fallbackSpeed;
		}
		QueueProjectileEvent(event);

		static std::uint32_t pepperLogCount = 0;
		++pepperLogCount;
		if (pepperLogCount <= 3 || pepperLogCount % 100 == 0)
		{
			Log("PROJECTILESTAGE local pepper projectile #"
				+ std::to_string(pepperLogCount)
				+ ", id=" + std::to_string(event.projectileId)
				+ ", speed=" + std::to_string(projectile->Speed)
				+ ", maxSpeed="
				+ std::to_string(projectile->MaxSpeed)
				+ ", refVel="
				+ std::to_string(projectile->RefVel)
				+ ", drawScale="
				+ std::to_string(projectile->DrawScale)
				+ ", particleScale="
				+ std::to_string(sourceParticle
					? sourceParticle->Scale : 0.0f)
				+ ", particleScale3D=("
				+ std::to_string(sourceParticle
					? sourceParticle->Scale3D.X : 0.0f)
				+ ','
				+ std::to_string(sourceParticle
					? sourceParticle->Scale3D.Y : 0.0f)
				+ ','
				+ std::to_string(sourceParticle
					? sourceParticle->Scale3D.Z : 0.0f)
				+ "), netScale="
				+ std::to_string(event.extra)
				+ ", direction=("
				+ std::to_string(resolvedDirection.X) + ','
				+ std::to_string(resolvedDirection.Y) + ','
				+ std::to_string(resolvedDirection.Z) + ')'
				+ ", velocity=("
				+ std::to_string(event.velocity[0]) + ','
				+ std::to_string(event.velocity[1]) + ','
				+ std::to_string(event.velocity[2])
				+ "), effect="
				+ ObjectName(g_pepperFlightTemplate) + '.');
		}
	}

	void OnLocalClockBombSpawn(AAliceClonePawn* bomb)
	{
		AAlicePawn* localPawn = GetLocalPawn();
		if (!g_config.enabled || g_soloMinigameActive
			|| g_config.localMirror || !bomb
			|| bomb->IsDefaultObject()
			|| !localPawn
			|| (bomb->MyAlicePawn != localPawn
				&& localPawn->MyClonePawn != bomb))
		{
			return;
		}
		if (g_localClockBomb.bomb == bomb
			&& g_localClockBomb.projectileId != 0)
		{
			return;
		}

		if (bomb->ExplosionParticle)
			g_clockBombExplosionTemplate =
				bomb->ExplosionParticle;
		g_localClockBomb = {};
		g_localClockBomb.bomb = bomb;
		g_localClockBomb.lastLocation = bomb->Location;
		g_localClockBomb.lastVelocity = bomb->Velocity;
		g_localClockBomb.lastRotation = bomb->Rotation;
		g_localClockBomb.cloneState =
			static_cast<std::uint8_t>(bomb->CloneState);
		g_localClockBomb.projectileId =
			g_localProjectileId++;
		if (g_localClockBomb.projectileId == 0)
			g_localClockBomb.projectileId =
				g_localProjectileId++;
		g_localClockBomb.nextUpdate =
			Clock::now() + std::chrono::milliseconds(50);

		ProjectileEventPayload event{};
		event.kind = ProjectileEventKind::ClockBombSpawn;
		event.projectileId =
			g_localClockBomb.projectileId;
		event.variant =
			static_cast<std::uint8_t>(bomb->CloneState);
		event.extra = bomb->CountdownTime;
		FillProjectileTransform(event, bomb);
		QueueProjectileEvent(event);
		Log("PROJECTILESTAGE local clock bomb spawned id="
			+ std::to_string(event.projectileId)
			+ ", mesh=" + ObjectName(
				bomb->Mesh ? bomb->Mesh->SkeletalMesh : nullptr)
			+ ", explosion="
			+ ObjectName(bomb->ExplosionParticle) + '.');
	}

	void OnLocalClockBombDetonate(AAliceClonePawn* bomb)
	{
		AAlicePawn* localPawn = GetLocalPawn();
		if (!g_config.enabled || g_soloMinigameActive
			|| g_config.localMirror || !bomb
			|| !localPawn
			|| (bomb->MyAlicePawn != localPawn
				&& localPawn->MyClonePawn != bomb
				&& g_localClockBomb.bomb != bomb))
		{
			return;
		}
		if (g_localClockBomb.bomb != bomb
			|| g_localClockBomb.projectileId == 0)
		{
			OnLocalClockBombSpawn(bomb);
		}
		if (g_localClockBomb.bomb != bomb
			|| g_localClockBomb.projectileId == 0
			|| g_localClockBomb.exploded)
		{
			return;
		}
		g_localClockBomb.exploded = true;
		ProjectileEventPayload event{};
		event.kind = ProjectileEventKind::ClockBombExplode;
		event.projectileId =
			g_localClockBomb.projectileId;
		event.variant =
			static_cast<std::uint8_t>(bomb->CloneState);
		FillProjectileTransform(event, bomb);
		QueueProjectileEvent(event);
		Log("PROJECTILESTAGE local clock bomb detonated id="
			+ std::to_string(event.projectileId) + '.');
	}

	void OnLocalClockBombDestroyed(AAliceClonePawn* bomb)
	{
		if (g_soloMinigameActive
			|| !bomb || g_localClockBomb.bomb != bomb)
			return;
		if (!g_localClockBomb.exploded
			&& g_localClockBomb.projectileId != 0)
		{
			if (bomb->CloneState
				== EClonnPawnState::
					e_ClonePawnState_Destory)
			{
				OnLocalClockBombDetonate(bomb);
			}
			else
			{
				ProjectileEventPayload event{};
				event.kind =
					ProjectileEventKind::ClockBombRemove;
				event.projectileId =
					g_localClockBomb.projectileId;
				event.variant =
					static_cast<std::uint8_t>(bomb->CloneState);
				FillProjectileTransform(event, bomb);
				QueueProjectileEvent(event);
			}
		}
		g_localClockBomb = {};
	}

	void TraceLifecycleProcessEvent(UObject* object,
		UFunction* function, const void* parameters, bool after)
	{
		if (!g_config.enabled || g_soloMinigameActive
			|| !function)
			return;
		const std::string functionName = function->GetName();
		const bool localController = object
			&& g_State.AlicePlayerController
			&& object == g_State.AlicePlayerController;
		if (!after && functionName == "Restart"
			&& object
			&& object->IsA(
				UAliceGfxMovie_inGameMenu::StaticClass()))
		{
			// The old world is still valid here. Retire pooled presentation
			// effects before the checkpoint teardown invalidates their owners;
			// the previous code merely forgot these pointers after the load.
			CleanupRemoteWeaponTransients(true);
			StopRemoteWeaponLoopParticles();
			if (g_remoteGlideParticle)
				HardStopPresentationParticle(
					g_remoteGlideParticle, true);
			g_remoteGlideParticle = nullptr;
			DestroyRemoteStaticLeafTrail();
			if (g_remoteAttackTrailParticle)
				HardStopPresentationParticle(
					g_remoteAttackTrailParticle, true);
			g_remoteAttackTrailParticle = nullptr;
			if (g_remoteMuzzleParticle)
				HardStopPresentationParticle(
					g_remoteMuzzleParticle, true);
			g_remoteMuzzleParticle = nullptr;
			Log("VFXSTAGE retired proxy effects before checkpoint restart.");
		}
		if (!after && functionName == "UsedBy"
			&& parameters && object
			&& object->IsA(ATrigger::StaticClass()))
		{
			const auto* value = reinterpret_cast<const
				AActor_execUsedBy_Params*>(parameters);
			const bool localUser =
				value->User && value->User == GetLocalPawn();
			Log("TRIGGERINTERACT UsedBy trigger="
				+ ObjectName(object)
				+ ", user=" + ObjectName(value->User)
				+ ", localUser="
				+ (localUser ? std::string("yes")
					: std::string("no"))
				+ ", applyingNetwork="
				+ (g_applyingSharedTriggerInteraction
					? std::string("yes.")
					: std::string("no.")));
			if (!g_config.localMirror
				&& localUser
				&& !g_applyingSharedTriggerInteraction)
			{
				QueueTriggerInteraction(
					reinterpret_cast<ATrigger*>(object),
					"Actor.UsedBy");
			}
		}
		if (localController && !after
			&& functionName == "Use"
			&& !g_applyingSharedInteraction)
		{
			++g_localInteractionAttemptSerial;
			g_localInteractionKeysThisAttempt.clear();
			CaptureContextActorUseSnapshot();
			g_localInteractionWindowUntil =
				Clock::now() + std::chrono::seconds(10);
			Log("SHAREDINTERACT local Use attempt="
				+ std::to_string(g_localInteractionAttemptSerial)
				+ ", map="
				+ (g_currentMap.empty()
					? std::string("<none>") : g_currentMap)
				+ '.');
		}
		if (localController && after
			&& functionName == "Use"
			&& !g_applyingSharedInteraction)
		{
			DetectStartedContextActor(false);
		}
		if (localController && functionName == "notifyInputKey")
		{
			if (after || !parameters)
				return;
			const auto* value = reinterpret_cast<const
				AAlicePlayerController_execnotifyInputKey_Params*>(
					parameters);
			const std::string key = value->Key.ToString();
			if (g_config.controlLifecycleTrace
				&& (_stricmp(key.c_str(), "SpaceBar") == 0
					|| _stricmp(key.c_str(), "Space") == 0
					|| _stricmp(key.c_str(), "Jump") == 0))
			{
				BeginControlLifecycleTrace(key.c_str(), value->Event);
			}
			if (value->Event
					== static_cast<std::uint8_t>(
						EInputEvent::IE_Pressed)
				&& _stricmp(key.c_str(), "P") == 0)
			{
				g_teleportInputRequested.store(
					true, std::memory_order_release);
				Log("TELEPORT P input received through "
					"PlayerController.notifyInputKey.");
				return;
			}
			if (value->Event
					== static_cast<std::uint8_t>(
						EInputEvent::IE_Pressed)
				&& _stricmp(key.c_str(), "O") == 0)
			{
				g_forceTeleportInputRequested.store(
					true, std::memory_order_release);
				Log("TELEPORT O input received through "
					"PlayerController.notifyInputKey.");
				return;
			}
			if (value->Event
					!= static_cast<std::uint8_t>(
						EInputEvent::IE_Pressed)
				|| _stricmp(key.c_str(), "C") != 0)
				return;
			++g_localInteractionAttemptSerial;
			g_localInteractionKeysThisAttempt.clear();
			g_localInteractionWindowUntil =
				Clock::now() + std::chrono::seconds(10);
			Log("SHAREDINTERACT local C press attempt="
				+ std::to_string(
					g_localInteractionAttemptSerial) + '.');
		}
		if (localController && after
			&& functionName == "TriggerInteracted" && parameters)
		{
			const auto* value = reinterpret_cast<const
				APlayerController_execTriggerInteracted_Params*>(
					parameters);
			if (value->ReturnValue)
			{
				g_localInteractionWindowUntil =
					Clock::now() + std::chrono::seconds(10);
			}
			Log("SHAREDINTERACT TriggerInteracted result="
				+ std::string(value->ReturnValue ? "yes." : "no."));
		}
		if (localController && !after
			&& functionName == "OnInteractInLondon"
			&& parameters)
		{
			const auto* value = reinterpret_cast<const
				AAlicePlayerController_execOnInteractInLondon_Params*>(
					parameters);
			USeqAct_InteractInLondon* action = value->inAction;
			Log("LONDONINTERACT OnInteractInLondon action="
				+ ObjectName(action)
				+ ", key="
				+ SharedWorldKeyText(
					SequenceActionStableKey(action))
				+ ", applyingNetwork="
				+ (g_applyingSharedLondonInteraction
					? std::string("yes")
					: std::string("no"))
				+ ", active="
				+ (action && action->bActive
					? std::string("yes")
					: std::string("no"))
				+ ", inputs="
				+ std::to_string(
					action ? action->InputLinks.size() : 0)
				+ ", outputs="
				+ std::to_string(
					action ? action->OutputLinks.size() : 0)
				+ '.');
		}
		if (localController && !after
			&& functionName == "interactInLondonX")
		{
			auto* controller = reinterpret_cast<
				AAlicePlayerController*>(object);
			USeqAct_InteractInLondon* action =
				controller ? controller->InteractLondonActor : nullptr;
			const std::uint64_t key =
				SequenceActionStableKey(action);
			Log("LONDONINTERACT interactInLondonX action="
				+ ObjectName(action)
				+ ", key=" + SharedWorldKeyText(key)
				+ ", applyingNetwork="
				+ (g_applyingSharedLondonInteraction
					? std::string("yes")
					: std::string("no"))
				+ ", active="
				+ (action && action->bActive
					? std::string("yes")
					: std::string("no"))
				+ '.');
			if (!g_config.localMirror
				&& !g_applyingSharedLondonInteraction
				&& key != 0
				&& g_localInteractionKeysThisAttempt.insert(
					key).second)
			{
				SharedWorldEventPayload event{};
				event.kind =
					SharedWorldEventKind::InteractionInLondon;
				event.entityKey = key;
				event.originActorId =
					g_config.role == Role::Host ? 1u : 2u;
				QueueSharedWorldEvent(event);
				Log("LONDONINTERACT TX action="
					+ ObjectName(action)
					+ ", key=" + SharedWorldKeyText(key) + '.');
			}
		}
		if (localController && !after
			&& functionName == "exitInteractState")
		{
			auto* controller = reinterpret_cast<
				AAlicePlayerController*>(object);
			Log("LONDONINTERACT exitInteractState action="
				+ ObjectName(controller
					? controller->InteractLondonActor : nullptr)
				+ ", applyingNetwork="
				+ (g_applyingSharedLondonInteraction
					? std::string("yes.")
					: std::string("no.")));
		}
		if (localController && !after
			&& functionName == "ClientTravel" && parameters)
		{
			const auto* value = reinterpret_cast<const
				APlayerController_eventClientTravel_Params*>(
					parameters);
			Log("SESSION ClientTravel trace URL="
				+ value->URL.ToString()
				+ ", type="
				+ std::to_string(value->TravelType)
				+ ", seamless="
				+ (value->bSeamless
					? std::string("yes.") : std::string("no.")));
		}
		if (!g_config.localMirror && !after
			&& functionName == "backtoTitle"
			&& localController
			&& !g_applyingNetworkReturnToMenu
			&& !g_localReturnToMenuInitiated)
		{
			g_localReturnToMenuInitiated = true;
			if (g_config.role == Role::Host)
				RequestHostReturnToMenu("host backtoTitle");
			else if (g_config.role == Role::Client)
				RequestClientReturnToMenu("client backtoTitle");
		}
		if (!g_config.localMirror && !after
			&& functionName == "QuitGame"
			&& object
			&& object->IsA(
				UAliceGfxMovie_inGameMenu::StaticClass())
			&& !g_applyingNetworkReturnToMenu
			&& !g_localReturnToMenuInitiated)
		{
			g_localReturnToMenuInitiated = true;
			if (g_config.role == Role::Host)
				RequestHostReturnToMenu("host menu");
			else if (g_config.role == Role::Client)
				RequestClientReturnToMenu("client menu");
		}
		if (!g_config.localMirror && !after
			&& functionName == "SetInMainMenu"
			&& parameters && !g_applyingNetworkReturnToMenu
			&& !g_localReturnToMenuInitiated
			&& !g_currentMap.empty()
			&& _stricmp(g_currentMap.c_str(), "AliceEntry") != 0)
		{
			const auto* value = reinterpret_cast<const
				AAlicePlayerController_execSetInMainMenu_Params*>(
					parameters);
			if (value->DesiredInMainMenu)
			{
				g_localReturnToMenuInitiated = true;
				if (g_config.role == Role::Host)
					RequestHostReturnToMenu(
						"host SetInMainMenu");
				else if (g_config.role == Role::Client)
					RequestClientReturnToMenu(
						"client SetInMainMenu");
			}
		}
		if (!g_config.localMirror && !after
			&& functionName == "deathRestartGame"
			&& !g_applyingNetworkRestart)
		{
			g_localRestartInitiated = true;
			if (g_config.role == Role::Host)
				RequestHostCheckpointRestart("host Continue");
			else if (g_config.role == Role::Client)
				RequestClientCheckpointRestart("client Continue");
		}
		if (!g_config.localMirror && !after
			&& functionName == "OnSetVentState"
			&& parameters && !g_applyingSharedVentState)
		{
			const auto* value = reinterpret_cast<const
				AAlicePlayerController_execOnSetVentState_Params*>(
					parameters);
			g_pendingLocalVentActionKey =
				SequenceActionStableKey(value->inAction);
			g_pendingLocalVentInputIndex = 0;
			if (value->inAction)
			{
				for (int32_t index = 0;
					index < value->inAction->InputLinks.size();
					++index)
				{
					const FSeqOpInputLink& input =
						value->inAction->InputLinks.at(index);
					if (input.bHasImpulse
						|| input.QueuedActivations > 0)
					{
						g_pendingLocalVentInputIndex =
							static_cast<std::uint8_t>(index);
						break;
					}
				}
				const std::string inputName =
					value->inAction->InputLinks.empty()
						? std::string("<none>")
						: value->inAction->InputLinks.at(
							std::min<int32_t>(
								g_pendingLocalVentInputIndex,
								value->inAction->InputLinks.size()
									- 1)).LinkDesc.ToString();
				Log("SHAREDWORLD captured local vent input="
					+ std::to_string(
						g_pendingLocalVentInputIndex)
					+ ", name=" + inputName
					+ ", key="
					+ SharedWorldKeyText(
						g_pendingLocalVentActionKey) + '.');
			}
		}
		if (!g_config.localMirror && after
			&& functionName == "OnSetVentState"
			&& parameters && !g_applyingSharedVentState)
		{
			const auto* value = reinterpret_cast<const
				AAlicePlayerController_execOnSetVentState_Params*>(
					parameters);
			const std::uint64_t key =
				SequenceActionStableKey(value->inAction);
			if (key != 0 && g_remoteContextInteraction.active)
			{
				g_remoteContextInteraction.localVentApplied = true;
				ReleaseRemoteContextActorPresentation(
					"local-vent-completed");
				Log("SHAREDWORLD suppressed replicated ContextActor "
					"vent echo key=" + SharedWorldKeyText(key)
					+ ", deferredHostState="
					+ (g_remoteContextInteraction.deferredVent
						.has_value()
						? std::string("yes.")
						: std::string("no.")));
				if (g_remoteContextInteraction.deferredVent.has_value())
				{
					g_remoteContextInteraction = {};
				}
				else
				{
					g_remoteContextInteraction.deadline =
						Clock::now() + std::chrono::seconds(5);
				}
			}
			else if (key != 0
				&& !(key == g_suppressedVentActionKey
					&& Clock::now()
						< g_suppressedVentActionUntil))
			{
				const std::uint8_t input =
					key == g_pendingLocalVentActionKey
						? g_pendingLocalVentInputIndex
						: 0;
				Log("SHAREDWORLD observed vent completion key="
					+ SharedWorldKeyText(key)
					+ ", input="
					+ std::to_string(input)
					+ ", action="
					+ ObjectName(value->inAction) + '.');
				SharedWorldEventPayload event{};
				event.kind =
					SharedWorldEventKind::VentStateApplied;
				event.entityKey = key;
				event.originActorId =
					g_config.role == Role::Host ? 1u : 2u;
				event.flags = input;
				QueueSharedWorldEvent(event);
				Log("SHAREDWORLD TX vent fallback key="
					+ SharedWorldKeyText(key)
					+ ", input=" + std::to_string(input) + '.');
			}
			else if (key != 0)
			{
				Log("SHAREDWORLD suppressed echoed vent state key="
					+ SharedWorldKeyText(key) + '.');
			}
		}
		std::ostringstream stream;
		stream << "COOPLIFECYCLE phase=" << (after ? "post" : "pre")
			<< ", function=" << functionName
			<< ", map="
			<< (g_currentMap.empty() ? "<none>" : g_currentMap);

		// A restart/load call may invalidate its own receiver before it returns,
		// so only inspect the object and parameters on the pre-call side.
		if (!after)
		{
			stream << ", object=" << ObjectName(object);
			if (functionName == "OnSetVentState" && parameters)
			{
				const auto* value = reinterpret_cast<const
					AAlicePlayerController_execOnSetVentState_Params*>(
						parameters);
				stream << ", action=" << ObjectName(value->inAction);
			}
			else if (functionName == "SetCinematicMode" && parameters)
			{
				const auto* value = reinterpret_cast<const
					AAlicePlayerController_execSetCinematicMode_Params*>(
						parameters);
				stream << ", enabled="
					<< (value->bInCinematicMode ? "yes" : "no")
					<< ", hidePlayer="
					<< (value->bHidePlayer ? "yes" : "no")
					<< ", blocksMovement="
					<< (value->bAffectsMovement ? "yes" : "no");
			}
		}
		stream << '.';
		const bool importantLifecycle =
			ContainsCaseInsensitive(functionName, "restart")
			|| ContainsCaseInsensitive(functionName, "loadchapter")
			|| functionName == "backtoTitle"
			|| functionName == "QuitGame"
			|| functionName == "SetInMainMenu";
		if (g_config.actionTrace || importantLifecycle)
			Log(stream.str());
	}

		bool InterpHasDirectorTrack(const USeqAct_Interp* action)
		{
			if (!action || !action->InterpData
				|| !IsLiveUObject(action->InterpData))
			{
				return false;
			}
			for (UInterpGroup* group :
				action->InterpData->InterpGroups)
			{
				if (!group || !IsLiveUObject(group))
					continue;
				if (group->IsA(
						UInterpGroupDirector::StaticClass()))
				{
					return true;
				}
				for (UInterpTrack* track : group->InterpTracks)
				{
					if (track && IsLiveUObject(track)
						&& track->IsA(
							UInterpTrackDirector::StaticClass()))
					{
						return true;
					}
				}
			}
			return false;
		}

		USeqAct_Interp* FindCutsceneAction(
			std::uint64_t entityKey)
		{
			if (entityKey == 0)
				return nullptr;
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return nullptr;
			for (int32_t index = 0;
				index < objects->size(); ++index)
			{
				UObject* object = objects->at(index);
				if (!object
					|| !object->IsA(
						USeqAct_Interp::StaticClass())
					|| !IsLiveUObject(object)
					|| SequenceActionStableKey(object)
						!= entityKey)
				{
					continue;
				}
				return reinterpret_cast<USeqAct_Interp*>(object);
			}
			return nullptr;
		}

		bool IsSpawnerStyleCinematic(const USeqAct_Interp* action)
		{
			if (!action || action->InputLinks.size() != 5
				|| action->OutputLinks.size() != 4)
			{
				return false;
			}
			const std::string name = ObjectName(action);
			return name.find(".Prefabs.") != std::string::npos
				&& (name.find("_Spawner_Seq_")
						!= std::string::npos
					|| name.find("_Spawner.")
						!= std::string::npos);
		}

		bool TryStartEarlyCutsceneBarrier(USeqAct_Interp* action)
		{
			if (!action || g_config.localMirror
				|| g_waitingCutsceneAction)
			{
				return false;
			}
			const auto now = Clock::now();
			const bool interactionMatinee =
				g_remoteContextInteraction.active
				|| now < g_localInteractionWindowUntil
				|| now < g_interactionCutsceneBypassUntil;
			const bool peerAvailable =
				g_remotePresentation.valid
				&& !g_currentMap.empty()
				&& _stricmp(
					g_remotePresentation.state.mapName,
					g_currentMap.c_str()) == 0;
			const std::uint64_t key =
				SequenceActionStableKey(action);
			if (!peerAvailable || interactionMatinee || key == 0
				|| (!InterpHasDirectorTrack(action)
					&& !IsSpawnerStyleCinematic(action))
				|| (key == g_recentReleasedCutsceneKey
					&& now < g_recentReleasedCutsceneUntil))
			{
				return false;
			}

			g_waitingCutsceneAction = action;
			g_waitingCutsceneKey = key;
			g_cutsceneBarrierAdvertiseKey = key;
			g_cutsceneBarrierStartedAt = now;
			g_cutsceneBarrierAdvertiseUntil = {};
			g_waitingCutsceneOriginalPlayRate = action->PlayRate;
			g_waitingCutscenePlayRateOverridden = true;
			g_waitingCutsceneCanDeferActivation = true;
			g_waitingCutsceneActivationDeferred = false;
			action->PlayRate = 0.0f;
			action->bPaused = true;
			AchievementOverlay::SetCoopWaitingForPeer(true);
			Log("CUTSCENEBARRIER armed before activation key="
				+ SharedWorldKeyText(key)
				+ ", action=" + ObjectName(action)
				+ ", originalPlayRate="
				+ std::to_string(
					g_waitingCutsceneOriginalPlayRate) + '.');
			return true;
		}

	bool ShouldDeferSequenceOpActivation(UObject* object)
	{
		if (!g_config.enabled || g_soloMinigameActive
			|| g_config.localMirror || !object
			|| g_replayingDeferredCutsceneActivation
			|| !g_waitingCutsceneCanDeferActivation
			|| g_waitingCutsceneActivationDeferred
			|| object != g_waitingCutsceneAction)
		{
			return false;
		}
		g_waitingCutsceneActivationDeferred = true;
		Log("CUTSCENEBARRIER deferred Activated() key="
			+ SharedWorldKeyText(g_waitingCutsceneKey)
			+ ", action="
			+ ObjectName(g_waitingCutsceneAction) + '.');
		return true;
	}

	void TraceSequenceOpProcessEvent(UObject* object,
		bool activated, bool after)
	{
		if (!g_config.enabled || !object)
			return;
		std::string soloLevel;
		if (DetectSoloMinigameObject(object, soloLevel))
		{
			SetSoloMinigameActive(true, soloLevel);
			return;
		}
		if (g_soloMinigameActive
			|| !object->IsA(USequenceOp::StaticClass())
			|| !activated)
		{
			return;
		}
		auto* op = reinterpret_cast<USequenceOp*>(object);
		if (op->IsA(USeqAct_Interp::StaticClass()))
		{
			auto* interp = reinterpret_cast<USeqAct_Interp*>(op);
			if (!after)
			{
				TryStartEarlyCutsceneBarrier(interp);
			}
			else if (g_waitingCutsceneAction == interp)
			{
				// Activated() clears these fields on some Matinees. Reapply
				// the zero-frame barrier before their first world tick.
				if (g_waitingCutscenePlayRateOverridden)
					interp->PlayRate = 0.0f;
				interp->bPaused = true;
			}
		}
		if (after)
			return;

		bool replicatedContextEvent = false;
		if (!g_config.localMirror
			&& op->IsA(
				USeqEvent_ContextActionActivated::StaticClass()))
		{
			auto* contextEvent = reinterpret_cast<
				USeqEvent_ContextActionActivated*>(op);
			replicatedContextEvent =
				g_remoteContextInteraction.active
				&& contextEvent->Originator
					== g_remoteContextInteraction.actor
				&& contextEvent->Instigator == GetLocalPawn();
			if (replicatedContextEvent)
			{
				g_remoteContextInteraction.localContextActivated = true;
				Log("CONTEXTACTOR local completion event reached key="
					+ SharedWorldKeyText(
						SequenceActionStableKey(contextEvent))
					+ ", actorKey="
					+ SharedWorldKeyText(
						g_remoteContextInteraction.actorKey)
					+ ", event=" + ObjectName(contextEvent) + '.');
			}
		}
		if (activated && !after
			&& !g_config.localMirror
			&& !g_applyingSharedContextAction
			&& !replicatedContextEvent
			&& op->IsA(
				USeqEvent_ContextActionActivated::StaticClass()))
		{
			auto* contextEvent = reinterpret_cast<
				USeqEvent_ContextActionActivated*>(op);
			if (contextEvent->Instigator == GetLocalPawn())
			{
				const std::uint64_t key =
					SequenceActionStableKey(contextEvent);
				if (key != 0
					&& g_localInteractionKeysThisAttempt.insert(
						key).second)
				{
					// Context interactions (valves, levers and memory
					// entrances) often chain character and camera Matinees.
					// The first Matinee consumes the short Use window, while
					// later stages must still bypass the generic two-player
					// trigger barrier because the interaction itself is
					// already replicated to both peers.
					g_interactionCutsceneBypassUntil =
						Clock::now() + std::chrono::seconds(30);
					SharedWorldEventPayload event{};
					event.kind = SharedWorldEventKind::
						InteractionContextActionActivated;
					event.entityKey = key;
					event.originActorId =
						g_config.role == Role::Host ? 1u : 2u;
					QueueSharedWorldEvent(event);
					Log("CONTEXTINTERACT TX key="
						+ SharedWorldKeyText(key)
						+ ", event=" + ObjectName(contextEvent)
						+ ", originator="
						+ ObjectName(contextEvent->Originator)
						+ ", instigator="
						+ ObjectName(contextEvent->Instigator)
						+ ", triggers="
						+ std::to_string(
							contextEvent->TriggerCount)
						+ ", enabled="
						+ (contextEvent->bEnabled
							? std::string("yes.")
							: std::string("no.")));
				}
			}
		}
		if (!g_pendingSequenceOpUseTrace
			|| Clock::now() >= g_sequenceOpUseTraceUntil)
		{
			return;
		}
		if (!g_sequenceOpUseLogged.insert(op).second)
			return;
		++g_sequenceOpUseTraceCount;
		std::string handler = "<none>";
		if (op->IsA(USequenceAction::StaticClass()))
		{
			handler = reinterpret_cast<USequenceAction*>(op)
				->HandlerName.ToString();
		}
		Log("SEQUENCEUSE event #"
			+ std::to_string(g_sequenceOpUseTraceCount)
			+ ", phase=" + (after ? std::string("post")
				: std::string("pre"))
			+ ", event=" + (activated
				? std::string("Activated")
				: std::string("Deactivated"))
			+ ", key="
			+ SharedWorldKeyText(
				SequenceActionStableKey(op))
			+ ", op=" + ObjectName(op)
			+ ", count=" + std::to_string(op->ActivateCount)
			+ ", active="
			+ (op->bActive ? std::string("yes")
				: std::string("no"))
			+ ", inputs=" + SequenceOpInputState(op)
			+ ", handler=" + handler
			+ ", parent=" + ObjectName(op->ParentSequence)
			+ '.');
	}

	void HandleSharedInteractionProcessEvent(UObject* object,
		const void* parameters, bool after)
	{
		if (!g_config.enabled || g_soloMinigameActive
			|| g_config.localMirror
			|| !after || !object || !parameters
			|| g_applyingSharedInteraction
			|| !object->IsA(USeqEvent_Used::StaticClass()))
		{
			return;
		}
		const auto* value = reinterpret_cast<const
			USequenceEvent_execCheckActivate_Params*>(parameters);
		AAlicePawn* localPawn = GetLocalPawn();
		if (!value->ReturnValue || value->bTest || !localPawn)
		{
			return;
		}
		const std::uint64_t key =
			SequenceActionStableKey(object);
		if (key == 0)
			return;
		const bool directlyOwned =
			value->InInstigator == localPawn
			|| value->InOriginator == localPawn
			|| value->InInstigator
				== g_State.AlicePlayerController
			|| value->InOriginator
				== g_State.AlicePlayerController;
		const bool insideLocalUseWindow =
			Clock::now() < g_localInteractionWindowUntil;
		if (g_interactionCandidateTraceCount < 100)
		{
			++g_interactionCandidateTraceCount;
			Log("SHAREDINTERACT candidate key="
				+ SharedWorldKeyText(key)
				+ ", event=" + ObjectName(object)
				+ ", originator="
				+ ObjectName(value->InOriginator)
				+ ", instigator="
				+ ObjectName(value->InInstigator)
				+ ", direct="
				+ (directlyOwned ? std::string("yes")
					: std::string("no"))
				+ ", useWindow="
				+ (insideLocalUseWindow ? std::string("yes.")
					: std::string("no.")));
		}
		if (!directlyOwned && !insideLocalUseWindow)
			return;
		if (!g_localInteractionKeysThisAttempt.insert(key).second)
			return;

		SharedWorldEventPayload event{};
		event.kind = SharedWorldEventKind::InteractionUsed;
		event.entityKey = key;
		event.originActorId =
			g_config.role == Role::Host ? 1u : 2u;
		if (!value->ActivateIndices.empty())
		{
			event.flags = static_cast<std::uint8_t>(
				std::clamp(value->ActivateIndices.at(0),
					0, 255));
		}
		QueueSharedWorldEvent(event);
		Log("SHAREDINTERACT TX key="
			+ SharedWorldKeyText(key)
			+ ", event=" + ObjectName(object)
			+ ", originator="
			+ ObjectName(value->InOriginator)
			+ ", instigator="
			+ ObjectName(value->InInstigator)
			+ ", index=" + std::to_string(event.flags)
			+ '.');
	}

		void ReleaseCutsceneBarrier(const char* reason)
		{
			USeqAct_Interp* action = g_waitingCutsceneAction;
			const std::uint64_t key = g_waitingCutsceneKey;
			const bool replayActivation =
				g_waitingCutsceneActivationDeferred;
			if (action && IsLiveUObject(action))
			{
				if (g_waitingCutscenePlayRateOverridden)
				{
					action->PlayRate =
						g_waitingCutsceneOriginalPlayRate;
				}
				action->bPaused = false;
			}
			g_waitingCutsceneAction = nullptr;
			g_waitingCutsceneKey = 0;
			g_waitingCutsceneOriginalPlayRate = 1.0f;
			g_waitingCutscenePlayRateOverridden = false;
			g_waitingCutsceneCanDeferActivation = false;
			g_waitingCutsceneActivationDeferred = false;
			g_recentReleasedCutsceneKey = key;
			g_recentReleasedCutsceneUntil =
				Clock::now() + std::chrono::seconds(20);
			g_cutsceneBarrierAdvertiseUntil =
				Clock::now() + std::chrono::seconds(2);
			AchievementOverlay::SetCoopWaitingForPeer(false);
			Log("CUTSCENEBARRIER released key="
				+ SharedWorldKeyText(key)
				+ ", action=" + ObjectName(action)
				+ ", reason="
				+ (reason ? std::string(reason)
					: std::string("<unknown>")) + '.');
			if (replayActivation && action
				&& IsLiveUObject(action))
			{
				g_replayingDeferredCutsceneActivation = true;
				action->eventActivated();
				g_replayingDeferredCutsceneActivation = false;
				Log("CUTSCENEBARRIER replayed Activated() key="
					+ SharedWorldKeyText(key)
					+ ", action=" + ObjectName(action) + '.');
			}
		}

		bool NativeForceActivateSequenceInput(
			USequenceOp* action, int inputIndex)
		{
			if (!action || inputIndex < 0
				|| inputIndex >= action->InputLinks.size())
			{
				return false;
			}
			static UFunction* forceActivateInput = nullptr;
			if (!forceActivateInput)
			{
				forceActivateInput = UFunction::FindFunction(
					"Function Engine.SequenceOp.ForceActivateInput");
			}
			if (!forceActivateInput)
				return false;
			USequenceOp_execForceActivateInput_Params parameters{};
			parameters.InputIdx = inputIndex;
			action->ProcessEvent(
				forceActivateInput, &parameters, nullptr);
			return true;
		}

		void AdvertiseEmergencyCutscene(std::uint64_t key)
		{
			if (key == 0)
				return;
			g_emergencyCutsceneAdvertiseKey = key;
			g_emergencyCutsceneAdvertiseUntil =
				Clock::now() + std::chrono::seconds(3);
		}

		void LogIncomingCutsceneOps(USeqAct_Interp* target)
		{
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!target || !objects)
				return;
			int incomingCount = 0;
			for (int32_t objectIndex = 0;
				objectIndex < objects->size(); ++objectIndex)
			{
				UObject* object = objects->at(objectIndex);
				if (!object
					|| !object->IsA(USequenceOp::StaticClass())
					|| !IsLiveUObject(object))
				{
					continue;
				}
				auto* op = reinterpret_cast<USequenceOp*>(object);
				for (int32_t outputIndex = 0;
					outputIndex < op->OutputLinks.size();
					++outputIndex)
				{
					const FSeqOpOutputLink& output =
						op->OutputLinks.at(outputIndex);
					for (int32_t linkIndex = 0;
						linkIndex < output.Links.size();
						++linkIndex)
					{
						const FSeqOpOutputInputLink& link =
							output.Links.at(linkIndex);
						if (link.LinkedOp != target)
							continue;
						++incomingCount;
						Log("CUTSCENEEMERGENCY incoming op="
							+ ObjectName(op) + ", output="
							+ std::to_string(outputIndex)
							+ ", targetInput="
							+ std::to_string(
								link.InputLinkIdx) + '.');
					}
				}
			}
			Log("CUTSCENEEMERGENCY incoming op count="
				+ std::to_string(incomingCount)
				+ ", target=" + ObjectName(target) + '.');
		}

		bool ForceEmergencyCutscene(
			std::uint64_t key, const char* source)
		{
			if (key == 0)
				return false;
			const auto now = Clock::now();
			if (key == g_lastEmergencyCutsceneKey
				&& now < g_lastEmergencyCutsceneUntil)
			{
				return true;
			}
			g_lastEmergencyCutsceneKey = key;
			g_lastEmergencyCutsceneUntil =
				now + std::chrono::seconds(20);
			AdvertiseEmergencyCutscene(key);

			if (g_waitingCutsceneAction
				&& g_waitingCutsceneKey == key)
			{
				Log("CUTSCENEEMERGENCY releasing local barrier key="
					+ SharedWorldKeyText(key) + ", source="
					+ (source ? std::string(source)
						: std::string("<unknown>")) + '.');
				ReleaseCutsceneBarrier("emergency-force");
				return true;
			}

			USeqAct_Interp* action = FindCutsceneAction(key);
			if (!action)
			{
				Log("CUTSCENEEMERGENCY action not loaded key="
					+ SharedWorldKeyText(key) + ", source="
					+ (source ? std::string(source)
						: std::string("<unknown>")) + '.');
				// Permit another request once the streaming level has loaded.
				g_lastEmergencyCutsceneUntil =
					now + std::chrono::milliseconds(750);
				return false;
			}
			if (action->bActive || action->bIsPlaying)
			{
				Log("CUTSCENEEMERGENCY action already active key="
					+ SharedWorldKeyText(key) + ", action="
					+ ObjectName(action) + '.');
				return true;
			}

			// The generated SDK wrapper clears FUNC_Native and is inert in
			// this game build. Invoke the original native UFunction directly
			// so UE3 queues the Matinee through its real parent Sequence.
			if (action->PlayRate <= 0.001f)
				action->PlayRate = 1.0f;
			action->bPaused = false;
			g_recentReleasedCutsceneKey = key;
			g_recentReleasedCutsceneUntil =
				now + std::chrono::seconds(20);
			g_emergencyForcedCutsceneAction = action;
			g_emergencyForcedCutsceneRequestedAt = now;
			g_emergencyForcedCutsceneStarted = false;
			LogIncomingCutsceneOps(action);
			const bool invoked =
				NativeForceActivateSequenceInput(action, 0);
			if (!invoked)
			{
				g_emergencyForcedCutsceneAction = nullptr;
				g_emergencyForcedCutsceneRequestedAt = {};
			}
			AchievementOverlay::SetCoopWaitingForPeer(false);
			Log("CUTSCENEEMERGENCY activated key="
				+ SharedWorldKeyText(key) + ", action="
				+ ObjectName(action) + ", source="
				+ (source ? std::string(source)
					: std::string("<unknown>"))
				+ ", native="
				+ (invoked
					? std::string("yes") : std::string("no"))
				+ ", active="
				+ (action->bActive
					? std::string("yes") : std::string("no"))
				+ ", playing="
				+ (action->bIsPlaying
					? std::string("yes.") : std::string("no.")));
			return invoked;
		}

		void TickCutsceneBarrier(
			const PlayerStatePayload& peerState,
			bool peerOnSameMap)
		{
			const auto now = Clock::now();
			const bool peerEmergency =
				peerOnSameMap
				&& peerState.cutsceneBarrierKey != 0
				&& (peerState.flags & StateForceCutscene) != 0;
			const std::uint64_t availableEmergencyKey =
				g_waitingCutsceneKey != 0
					? g_waitingCutsceneKey
					: (peerOnSameMap
						? peerState.cutsceneBarrierKey : 0);
			if (AchievementOverlay::
					ConsumeCoopForceCutsceneRequest()
				&& availableEmergencyKey != 0)
			{
				Log("CUTSCENEEMERGENCY UI requested key="
					+ SharedWorldKeyText(
						availableEmergencyKey) + '.');
				ForceEmergencyCutscene(
					availableEmergencyKey, "local-ui");
			}
			if (peerEmergency)
			{
				ForceEmergencyCutscene(
					peerState.cutsceneBarrierKey, "peer-request");
			}
			if (g_emergencyCutsceneAdvertiseKey != 0
				&& now >= g_emergencyCutsceneAdvertiseUntil)
			{
				g_emergencyCutsceneAdvertiseKey = 0;
				g_emergencyCutsceneAdvertiseUntil = {};
			}
			// Host progression is authoritative when an encounter has ended
			// there but client-only enemies remain quarantined locally. Give
			// the client's Kismet a short chance to reach the same cutscene
			// naturally, then activate the exact matching Matinee.
			if (g_config.role == Role::Client
				&& !g_waitingCutsceneAction
				&& peerOnSameMap
				&& peerState.cutsceneBarrierKey != 0
				&& !g_lastLocalCinematic
				&& g_clientQuarantinedEnemyCount > 0
				&& g_clientOrphanedEncounterSince
					!= Clock::time_point{}
				&& now - g_clientOrphanedEncounterSince
					>= std::chrono::milliseconds(750)
				&& now >= g_nextHostCutsceneRecoveryAttempt
				&& g_lastAutomaticCutsceneRecoveryKey
					!= peerState.cutsceneBarrierKey)
			{
				g_nextHostCutsceneRecoveryAttempt =
					now + std::chrono::milliseconds(500);
				USeqAct_Interp* action = FindCutsceneAction(
					peerState.cutsceneBarrierKey);
				if (action && !action->bActive)
				{
					g_lastAutomaticCutsceneRecoveryKey =
						peerState.cutsceneBarrierKey;
					Log("CUTSCENERECOVERY adopting host progression "
						"key="
						+ SharedWorldKeyText(
							peerState.cutsceneBarrierKey)
						+ ", action=" + ObjectName(action)
						+ ", quarantined="
						+ std::to_string(
							g_clientQuarantinedEnemyCount)
						+ '.');
					AdvertiseEmergencyCutscene(
						peerState.cutsceneBarrierKey);
					NativeForceActivateSequenceInput(action, 0);
				}
				else if (!action
					&& g_lastMissingHostCutsceneKey
						!= peerState.cutsceneBarrierKey)
				{
					g_lastMissingHostCutsceneKey =
						peerState.cutsceneBarrierKey;
					Log("CUTSCENERECOVERY host progression action "
						"not loaded yet, key="
						+ SharedWorldKeyText(
							peerState.cutsceneBarrierKey) + '.');
				}
			}
			if (g_waitingCutsceneAction)
			{
				if (!IsLiveUObject(g_waitingCutsceneAction))
				{
					ReleaseCutsceneBarrier("action-invalid");
				}
				else if (peerOnSameMap
					&& peerState.cutsceneBarrierKey
						== g_waitingCutsceneKey)
				{
					ReleaseCutsceneBarrier("peer-ready");
				}
				else if (!peerOnSameMap
					&& now - g_cutsceneBarrierStartedAt
						>= std::chrono::seconds(30))
				{
					ReleaseCutsceneBarrier("safety-timeout");
				}
				else
				{
					if (g_waitingCutscenePlayRateOverridden)
					{
						g_waitingCutsceneAction->PlayRate = 0.0f;
					}
					g_waitingCutsceneAction->bPaused = true;
					AchievementOverlay::SetCoopWaitingForPeer(true);
				}
			}
			else if (g_cutsceneBarrierAdvertiseKey != 0
				&& g_cutsceneBarrierAdvertiseUntil
					!= Clock::time_point{}
				&& now >= g_cutsceneBarrierAdvertiseUntil)
			{
				g_cutsceneBarrierAdvertiseKey = 0;
				g_cutsceneBarrierAdvertiseUntil = {};
			}
		}

	void TraceInterpolationStarted(AActor* actor,
		USeqAct_Interp* action)
	{
		if (!g_config.enabled || !actor || !action)
			return;
		std::string soloLevel;
		if (DetectSoloMinigameObject(action, soloLevel))
		{
			SetSoloMinigameActive(true, soloLevel);
			return;
		}
		if (g_soloMinigameActive)
		{
			return;
		}
		if (action == g_emergencyForcedCutsceneAction)
		{
			g_emergencyForcedCutsceneStarted = true;
			if (actor->IsA(ASkeletalMeshActorMAT::StaticClass()))
			{
				auto* cinematicActor =
					reinterpret_cast<ASkeletalMeshActor*>(actor);
				USkeletalMeshComponent* component =
					cinematicActor->SkeletalMeshComponent;
				const bool actorWasHidden = actor->bHidden;
				const bool componentWasHidden =
					component && component->HiddenGame;
				// A forced Matinee bypasses the Kismet setup which normally
				// builds Alice's hair, cloth and facial presentation. That
				// leaves a stripped cinematic mannequin on top of the fully
				// configured gameplay pawn. Prefer the intact gameplay Alice
				// as the emergency fallback and expose only the other Matinee
				// participants.
				const bool suppressIncompleteCinematicAlice =
					actor->IsA(
						AAliceGameSkeletalMeshActorMAT::StaticClass())
					&& component
					&& ContainsCaseInsensitive(
						ObjectName(component->SkeletalMesh),
						"CH_Alice");
				actor->bHidden =
					suppressIncompleteCinematicAlice;
				NativeSetActorHidden(
					actor, suppressIncompleteCinematicAlice);
				if (component)
				{
					component->HiddenGame =
						suppressIncompleteCinematicAlice;
					component->SetHidden(
						suppressIncompleteCinematicAlice);
				}
				NativeForceUpdateComponents(
					actor, false, false);
				Log("CUTSCENEEMERGENCY "
					+ std::string(
						suppressIncompleteCinematicAlice
							? "suppressed incomplete Alice actor="
							: "prepared actor=")
					+ ObjectName(actor) + ", mesh="
					+ ObjectName(component
						? component->SkeletalMesh : nullptr)
					+ ", actorHidden="
					+ (actorWasHidden
						? std::string("yes")
						: std::string("no"))
					+ ", componentHidden="
					+ (componentWasHidden
						? std::string("yes.")
						: std::string("no.")));
			}
		}
		const std::uint64_t key =
			SequenceActionStableKey(action);
		const auto now = Clock::now();
			const bool interactionMatinee =
				g_remoteContextInteraction.active
				|| now < g_localInteractionWindowUntil
				|| now < g_interactionCutsceneBypassUntil;
		if (g_remoteContextInteraction.active
			&& !g_remoteContextInteraction.localMatineeStarted)
		{
			g_remoteContextInteraction.localMatineeStarted = true;
			Log("CONTEXTACTOR local Matinee reached key="
				+ SharedWorldKeyText(key)
				+ ", actorKey="
				+ SharedWorldKeyText(
					g_remoteContextInteraction.actorKey)
				+ ", action=" + ObjectName(action) + '.');
		}
		if (!g_config.localMirror
			&& !g_applyingSharedInteractionMatinee
			&& key != 0
			&& Clock::now() < g_localInteractionWindowUntil)
		{
			SharedWorldEventPayload event{};
			event.kind =
				SharedWorldEventKind::InteractionMatineeStarted;
			event.entityKey = key;
			event.originActorId =
				g_config.role == Role::Host ? 1u : 2u;
			QueueSharedWorldEvent(event);
			g_interactionCutsceneBypassUntil =
				Clock::now() + std::chrono::seconds(30);
			g_localInteractionWindowUntil = {};
			Log("SHAREDINTERACT TX Matinee key="
				+ SharedWorldKeyText(key)
				+ ", action=" + ObjectName(action)
				+ ", actor=" + ObjectName(actor) + '.');
		}
		const bool peerAvailable =
			g_remotePresentation.valid
			&& !g_currentMap.empty()
			&& _stricmp(
				g_remotePresentation.state.mapName,
				g_currentMap.c_str()) == 0;
		const AAlicePlayerController* localController =
			g_State.AlicePlayerController;
		const bool controllerCinematic =
			localController
			&& (localController->bCinematicMode
				|| (localController->bCinemaDisableInputMove
					&& localController->bCinemaDisableInputLook));
		const bool eligibleBarrier =
			!g_config.localMirror
			&& peerAvailable
			&& !interactionMatinee
			&& actor->IsA(ACameraActor::StaticClass())
			&& controllerCinematic
			&& key != 0;
		if (eligibleBarrier
			&& !(key == g_recentReleasedCutsceneKey
				&& now < g_recentReleasedCutsceneUntil))
		{
			if (!g_waitingCutsceneAction)
			{
				g_waitingCutsceneAction = action;
				g_waitingCutsceneKey = key;
				g_cutsceneBarrierAdvertiseKey = key;
				g_cutsceneBarrierStartedAt = now;
				g_cutsceneBarrierAdvertiseUntil = {};
				g_waitingCutsceneOriginalPlayRate = action->PlayRate;
				g_waitingCutscenePlayRateOverridden = true;
				g_waitingCutsceneCanDeferActivation = false;
				g_waitingCutsceneActivationDeferred = false;
				action->PlayRate = 0.0f;
				action->bPaused = true;
				AchievementOverlay::SetCoopWaitingForPeer(true);
				Log("CUTSCENEBARRIER waiting key="
					+ SharedWorldKeyText(key)
					+ ", action=" + ObjectName(action)
					+ ", actor=" + ObjectName(actor)
					+ ", position="
					+ std::to_string(action->Position) + '.');
			}
			else if (g_waitingCutsceneKey == key)
			{
				if (g_waitingCutscenePlayRateOverridden)
					action->PlayRate = 0.0f;
				action->bPaused = true;
			}
			else
			{
				Log("CUTSCENEBARRIER ignored nested key="
					+ SharedWorldKeyText(key)
					+ ", waitingFor="
					+ SharedWorldKeyText(
						g_waitingCutsceneKey) + '.');
			}
		}
		if (g_interpolationTraceCount >= 100)
			return;
		++g_interpolationTraceCount;
		Log("CUTSCENEINTERP start #" + std::to_string(
				g_interpolationTraceCount)
			+ ", key=" + SharedWorldKeyText(key)
			+ ", actor=" + ObjectName(actor)
			+ ", action=" + ObjectName(action)
			+ ", inputs="
			+ std::to_string(action->InputLinks.size())
			+ ", outputs="
			+ std::to_string(action->OutputLinks.size())
			+ ", aliceMatinee="
			+ (action->bAliceMatinee
				? std::string("yes")
				: std::string("no"))
			+ ", controllerCinematic="
			+ (controllerCinematic
				? std::string("yes.")
				: std::string("no.")));
	}

	void TraceProcessEvent(UObject* object, UFunction* function,
		const void* parameters)
	{
		if (g_soloMinigameActive)
			return;
		TraceProcessEventInternal(object, function, parameters);
	}

	void PumpLifecycleCommands()
	{
		if (g_soloMinigameActive)
			return;
		ApplyPendingLifecycleCommand();
	}

	bool IsEnabled()
	{
		return g_config.enabled;
	}

	bool IsApplyingHostProgression()
	{
		return g_config.enabled && g_applyingHostProgression;
	}

	void RepairRemoteVisibilityAfterLocalDodge()
	{
		if (g_soloMinigameActive
			|| !g_remotePawn || g_remoteHidden)
			return;
		g_remotePawn->EnableForceTranslucency(
			false, 1.0f, 0.0f, 0, false);
		const std::array<UPrimitiveComponent*, 7> components{
			g_remotePawn->Mesh,
			g_remotePawn->UpperBodyComponent,
			g_remotePawn->HairComponent,
			g_remotePawn->SkirtComponent,
			g_remotePawn->BowComponent,
			g_remotePawn->RibbonComponent,
			g_remotePawn->EarComponent
		};
		for (UPrimitiveComponent* component : components)
		{
			if (!component)
				continue;
			component->EnableForceTranslucency(
				false, 1.0f, 0.0f, 0, false);
			component->bForceTranslucency = false;
			component->ForceTranslucencyAlpha = 1.0f;
			component->ForceTranslucencyTargetAlpha = 1.0f;
		}
		if (g_remotePresentationWeapon)
		{
			g_remotePresentationWeapon->EnableForceTranslucency(
				false, 1.0f, 0.0f, 0, false);
			if (g_remotePresentationWeapon->Mesh)
			{
				g_remotePresentationWeapon->Mesh
					->EnableForceTranslucency(
						false, 1.0f, 0.0f, 0, false);
			}
		}
	}

	std::string GetOverlayStatusLine()
	{
		if (!g_config.enabled)
			return {};
		std::string status =
			g_config.role == Role::Host ? "HOST" : "CLIENT";
		status += g_connected.load()
			? " | RELAY ONLINE" : " | RELAY OFFLINE";
		if (g_soloMinigameActive)
			return status + " | SOLO MINIGAME";
		status += g_remotePawn
			? " | PEER VISIBLE" : " | WAITING FOR PEER";
		return status;
	}

	std::string GetOverlayDebugDetails()
	{
		if (!g_config.enabled)
			return {};
		std::ostringstream stream;
		stream << "Remote: "
			<< (g_remotePawn ? ObjectName(g_remotePawn) : "<none>")
			<< '\n';
		stream << "Enemy aggro: host="
			<< g_hostAggroHostTargets
			<< " client=" << g_hostAggroClientTargets
			<< " | authority="
			<< (g_config.role == Role::Host
				? "host"
				: (g_clientEnemyAuthorityActive
					? "host snapshot" : "local"))
			<< '\n';
		stream << "Animation: "
			<< (g_remoteFullBodyChannel.active
				? g_remoteFullBodyChannel.sequenceName : "<idle>")
			<< '\n';
		stream << "Action: #"
			<< g_lastRemoteActionSerial << ' '
			<< PlayerActionName(g_remotePresentation.valid
				? g_remotePresentation.state.action
				: PlayerAction::None)
			<< " | special="
			<< static_cast<int>(
				g_remotePresentation.valid
					? g_remotePresentation.state.specialMove : 0)
			<< '\n';
		stream << "Weapon: type="
			<< static_cast<int>(g_remotePresentationWeaponType)
			<< " actor="
			<< ObjectName(g_remotePresentationWeapon) << '\n';
		std::size_t outboundProjectileCount = 0;
		std::size_t inboundProjectileCount = 0;
		{
			std::lock_guard lock(g_stateMutex);
			outboundProjectileCount =
				g_outboundProjectileEvents.size();
			inboundProjectileCount =
				g_inboundProjectileEvents.size();
		}
		stream << "Projectiles: pepper="
			<< g_remotePepperVisuals.size()
			<< " bomb=" << g_remoteClockBombVisuals.size()
			<< " localBomb=" << g_localClockBomb.projectileId
			<< " queue=" << outboundProjectileCount
			<< '/' << inboundProjectileCount << '\n';
		stream << std::fixed << std::setprecision(1)
			<< "Pepper projectiles: disabled (muzzle only)"
			<< "\nBomb hat Z offset: "
			<< ClockBombZOffset << '\n';
		stream << "VFX anchor: "
			<< VfxAttachmentCandidateName(
				g_vfxAttachmentCandidate)
			<< " | shared="
			<< (g_sharedDevState ? "yes" : "no")
			<< " rev=" << g_sharedDevRevision
			<< '/' << g_sharedDevCommandRevision << '\n';
		if (g_remotePresentationWeapon)
		{
			AWeaponForAlice* weapon =
				g_remotePresentationWeapon;
			stream << "Sockets: trace="
				<< (weapon->TraceSocket.IsValid()
					? weapon->TraceSocket.ToString()
					: "<invalid>")
				<< " muzzle="
				<< (weapon->MuzzleFlashSocket.IsValid()
					? weapon->MuzzleFlashSocket.ToString()
					: "<invalid>")
				<< " range="
				<< (weapon->RangeAttackSocket.IsValid()
					? weapon->RangeAttackSocket.ToString()
					: "<invalid>")
				<< " first="
				<< (FirstWeaponSocket(weapon).IsValid()
					? FirstWeaponSocket(weapon).ToString()
					: "<invalid>") << '\n';
			const FName resolved =
				ResolveCandidateWeaponSocket(
					weapon, g_vfxAttachmentCandidate);
			stream << "Resolved anchor: "
				<< (resolved.IsValid()
					? resolved.ToString() : "<none>")
				<< " kind="
				<< (WeaponMeshHasSocket(weapon, resolved)
					? "socket"
					: (WeaponMeshHasBone(weapon, resolved)
						? "bone" : "missing"))
				<< '\n';
			stream << "Templates: trail="
				<< ObjectName(weapon->TracePSCTemplate)
				<< " muzzle="
				<< ObjectName(weapon->MuzzleFlashPSCTemplate)
				<< " effect0="
				<< ObjectName(
					weapon->WeaponEffectPSCTemplate.size() > 0
						? weapon->WeaponEffectPSCTemplate.at(0)
						: nullptr)
				<< '\n';
		}
		stream << "Dodge hidden="
			<< (g_remoteDodgeVisualHidden ? "yes" : "no")
			<< " | glide="
			<< (g_remoteGlideVfxActive ? "yes" : "no")
			<< " | bombAnim="
			<< (g_remoteClockBombAnimationActive ? "yes" : "no")
			<< " | trail="
			<< (g_remoteAttackTrailActive ? "yes" : "no")
			<< " | muzzle="
			<< (g_remoteMuzzleFlashActive ? "yes" : "no")
			<< '\n';
		if (g_remotePawn)
		{
			stream << std::fixed << std::setprecision(3)
				<< "Scale: body=" << g_remotePawn->DrawScale
				<< " hairActor="
				<< (g_remoteHairProxy
					? g_remoteHairProxy->DrawScale : -1.0f)
				<< " hairComp="
				<< (g_remoteIndependentHair
					? g_remoteIndependentHair->Scale : -1.0f)
				<< " hairLength="
				<< (g_remoteIndependentHair
					? g_remoteIndependentHair->LengthScale
					: -1.0f)
				<< '\n';
		}
		stream << "Last VFX: " << g_lastRemoteVfx
			<< "\nButtons are presentation-only; they do no damage.";
		return stream.str();
	}

	void ExecuteDevCommand(int command)
	{
		if (!g_config.enabled || g_soloMinigameActive)
			return;

		const bool sharedCommand =
			(command >= 1 && command <= 5)
			|| command == 9
			|| (command >= 10 && command <= 16);
		if (sharedCommand && !g_applyingSharedDevCommand)
			PublishSharedDevCommand(command);
		const bool configurationCommand =
			command == 7 || command == 8
			|| (command >= 10 && command <= 16);
		if (!g_remotePawn && !configurationCommand)
			return;

		const Clock::time_point now = Clock::now();
		switch (command)
		{
		case 1: // TestDodge
			if (!g_remoteDodgeParticleTemplate)
				g_remoteDodgeParticleTemplate =
					FindRemoteDodgeParticleTemplate();
			g_devDodgeHideUntil =
				now + std::chrono::milliseconds(850);
			g_remoteDodgeParticleUntil =
				now + std::chrono::milliseconds(850);
			g_nextRemoteDodgeParticle = now;
			g_lastRemoteVfx = "DEV dodge: "
				+ ObjectName(g_remoteDodgeParticleTemplate);
			Log("VFXSTAGE DEV command=dodge, template="
				+ ObjectName(g_remoteDodgeParticleTemplate) + '.');
			break;
		case 2: // TestMeleeTrail
			SetRemoteAttackTrail(false);
			SetRemoteAttackTrail(true);
			g_lastRemoteVfx = "DEV melee trail";
			Log("VFXSTAGE DEV command=melee-trail.");
			break;
		case 3: // TestMuzzle
			SetRemoteMuzzleFlash(
				g_remotePresentationWeapon, false);
			SetRemoteMuzzleFlash(
				g_remotePresentationWeapon, true);
			g_devMuzzleUntil =
				now + std::chrono::milliseconds(800);
			g_lastRemoteVfx = "DEV pepper muzzle";
			Log("VFXSTAGE DEV command=muzzle.");
			break;
		case 4: // TestShrink
		{
			UParticleSystemComponent* spawned =
				SpawnRemoteCosmeticParticle(
					g_remotePawn,
					g_remotePawn->StartShrink, false);
			g_lastRemoteVfx = std::string("DEV shrink: ")
				+ (spawned ? "spawned" : "failed");
			Log(std::string("VFXSTAGE DEV command=shrink, spawned=")
				+ (spawned ? "yes." : "no."));
			break;
		}
		case 5: // TestGlide
		{
			if (g_remoteGlideParticle)
				RetirePersistentPresentationParticle(
					g_remoteGlideParticle);
			UParticleSystem* glide = FindLoadedParticleSystem(
				{ "Glide" }, { "Alice" });
			if (!glide)
				glide = FindLoadedParticleSystem(
					{ "Float" }, { "Alice" });
			g_remoteGlideParticle =
				SpawnRemoteCosmeticParticle(
					g_remotePawn, glide, true);
			MakePresentationParticleVisible(
				g_remoteGlideParticle);
			g_devGlideUntil =
				now + std::chrono::milliseconds(1500);
			g_lastRemoteVfx = "DEV isolated glide";
			Log("VFXSTAGE DEV command=glide, template="
				+ ObjectName(glide) + ", spawned="
				+ (g_remoteGlideParticle ? "yes." : "no."));
			break;
		}
		case 6: // ToggleProxyHidden
			g_devForceProxyHidden = !g_devForceProxyHidden;
			g_lastRemoteVfx = std::string("DEV force hidden: ")
				+ (g_devForceProxyHidden ? "on" : "off");
			Log(std::string("VFXSTAGE DEV command=proxy-hidden, value=")
				+ (g_devForceProxyHidden ? "on." : "off."));
			break;
		case 7: // PreviousVfxAnchor
		{
			const int count = static_cast<int>(
				VfxAttachmentCandidate::Count);
			const int current = static_cast<int>(
				g_vfxAttachmentCandidate);
			g_vfxAttachmentCandidate =
				static_cast<VfxAttachmentCandidate>(
					(current + count - 1) % count);
			PublishSharedVfxAttachmentCandidate();
			g_lastRemoteVfx = std::string("anchor selected: ")
				+ VfxAttachmentCandidateName(
					g_vfxAttachmentCandidate);
			Log("VFXSTAGE DEV attachment candidate="
				+ std::string(VfxAttachmentCandidateName(
					g_vfxAttachmentCandidate)) + '.');
			break;
		}
		case 8: // NextVfxAnchor
		{
			const int count = static_cast<int>(
				VfxAttachmentCandidate::Count);
			const int current = static_cast<int>(
				g_vfxAttachmentCandidate);
			g_vfxAttachmentCandidate =
				static_cast<VfxAttachmentCandidate>(
					(current + 1) % count);
			PublishSharedVfxAttachmentCandidate();
			g_lastRemoteVfx = std::string("anchor selected: ")
				+ VfxAttachmentCandidateName(
					g_vfxAttachmentCandidate);
			Log("VFXSTAGE DEV attachment candidate="
				+ std::string(VfxAttachmentCandidateName(
					g_vfxAttachmentCandidate)) + '.');
			break;
		}
		case 9: // TestWeaponEffect
		{
			AWeaponForAlice* weapon =
				g_remotePresentationWeapon;
			if (!weapon)
			{
				g_lastRemoteVfx =
					"DEV weapon effect: no proxy weapon";
				break;
			}
			UParticleSystem* particle =
				weapon->WeaponEffectPSCTemplate.size() > 0
					? weapon->WeaponEffectPSCTemplate.at(0)
					: nullptr;
			if (!particle)
				particle = weapon->TracePSCTemplate;
			if (!particle)
				particle = weapon->MuzzleFlashPSCTemplate;
			if (!particle)
			{
				g_lastRemoteVfx =
					"DEV weapon effect: no template";
				break;
			}
			if (g_vfxAttachmentCandidate
				== VfxAttachmentCandidate::NativeWeapon)
			{
				SetRemoteAttackTrail(false);
				SetRemoteAttackTrail(true);
				g_lastRemoteVfx =
					"DEV weapon effect: native invoked";
			}
			else
			{
				if (g_remoteAttackTrailParticle)
					HardStopPresentationParticle(
						g_remoteAttackTrailParticle, true);
				g_remoteAttackTrailParticle =
					SpawnWeaponParticleCandidate(
						weapon, particle,
						g_vfxAttachmentCandidate);
				MakePresentationParticleVisible(
					g_remoteAttackTrailParticle);
				g_remoteAttackTrailActive = true;
				g_remoteAttackTrailUntil =
					now + std::chrono::milliseconds(900);
				g_lastRemoteVfx =
					"DEV weapon effect: "
					+ ObjectName(particle) + " @ "
					+ VfxAttachmentCandidateName(
						g_vfxAttachmentCandidate)
					+ (g_remoteAttackTrailParticle
						? " [spawned]" : " [failed]");
			}
			Log("VFXSTAGE DEV command=weapon-effect, template="
				+ ObjectName(particle)
				+ ", candidate="
				+ VfxAttachmentCandidateName(
					g_vfxAttachmentCandidate)
				+ ", spawned="
				+ (g_vfxAttachmentCandidate
					== VfxAttachmentCandidate::NativeWeapon
					? "native."
					: (g_remoteAttackTrailParticle
						? "yes." : "no.")));
			break;
		}
		case 10: // PepperHypothesis1
		case 11: // PepperHypothesis2
		case 12: // PepperHypothesis3
			g_pepperProjectileHypothesis = command - 9;
			ClearRemoteProjectileVisuals(true);
			g_lastRemoteVfx = "Pepper hypothesis "
				+ std::to_string(g_pepperProjectileHypothesis);
			Log("PROJECTILESTAGE DEV pepper hypothesis="
				+ std::to_string(
					g_pepperProjectileHypothesis) + '.');
			break;
		case 13: // BombHypothesis1
		case 14: // BombHypothesis2
		case 15: // BombHypothesis3
			g_clockBombHypothesis = command - 12;
			ClearRemoteProjectileVisuals(true);
			g_lastRemoteVfx = "Bomb hypothesis "
				+ std::to_string(g_clockBombHypothesis);
			Log("PROJECTILESTAGE DEV clock bomb hypothesis="
				+ std::to_string(g_clockBombHypothesis)
				+ ", zOffset="
				+ std::to_string(ClockBombHypothesisZOffset(
					static_cast<std::uint8_t>(
						g_clockBombHypothesis))) + '.');
			break;
		case 16: // ClearProjectileTests
			ClearRemoteProjectileVisuals(true);
			g_lastRemoteVfx =
				"Projectile test objects cleared";
			Log("PROJECTILESTAGE DEV cleared remote projectiles.");
			break;
		default:
			break;
		}
	}

	bool IsActionTraceEnabled()
	{
		return g_config.enabled && !g_soloMinigameActive
			&& g_config.actionTrace;
	}

	bool ShouldSuppressSharedPlayerDamage(
		UObject* object, UFunction* function,
		const void* parameters)
	{
		if (!g_config.enabled || g_soloMinigameActive
			|| g_config.localMirror
			|| !g_config.sharedEnemyHealth
			|| !g_config.sharedEnemyTransforms
			|| g_applyingSharedPlayerDamage
			|| !object || !function || !parameters
			|| function->GetName() != "TakeDamage")
		{
			return false;
		}
		const auto* value = reinterpret_cast<const
			AAlicePawn_eventTakeDamage_Params*>(parameters);
		if (value->Damage <= 0 || value->Damage > 100000)
			return false;
		APawn* instigatorPawn =
			value->InstigatedBy
				? value->InstigatedBy->Pawn : nullptr;
		if (!instigatorPawn && value->DamageCauser)
			instigatorPawn = value->DamageCauser->Instigator;
		if (!instigatorPawn
			|| !instigatorPawn->IsA(
				AAliceGameKynapsePawn::StaticClass()))
		{
			return false;
		}
		auto* enemy = reinterpret_cast<
			AAliceGameKynapsePawn*>(instigatorPawn);
		if (!enemy->WorldInfo
			|| enemy->WorldInfo != g_currentWorld)
		{
			return false;
		}

		if (g_config.role == Role::Host
			&& object == g_remotePawn)
		{
			const std::uint64_t entityKey =
				SharedEnemyStableKey(enemy);
			if (entityKey)
			{
				SharedWorldEventPayload event{};
				event.kind =
					SharedWorldEventKind::PlayerDamageRequest;
				event.originActorId = 1;
				event.entityKey = entityKey;
				event.damage = value->Damage;
				event.hitLocation[0] = value->HitLocation.X;
				event.hitLocation[1] = value->HitLocation.Y;
				event.hitLocation[2] = value->HitLocation.Z;
				event.momentum[0] = value->Momentum.X;
				event.momentum[1] = value->Momentum.Y;
				event.momentum[2] = value->Momentum.Z;
				const std::string damageType =
					ObjectName(value->DamageType);
				strncpy_s(event.damageType,
					damageType.c_str(), _TRUNCATE);
				QueueSharedWorldEvent(event);
				Log("PLAYERDAMAGE TX target=client, key="
					+ SharedWorldKeyText(entityKey)
					+ ", enemy=" + ObjectName(enemy)
					+ ", damage="
					+ std::to_string(value->Damage) + '.');
			}
			else
			{
				Log("PLAYERDAMAGE suppressed proxy hit from "
					"unmatched enemy=" + ObjectName(enemy)
					+ '.');
			}
			// The proxy is presentation-only. Never let its local health,
			// death state or global Alice UI mutate.
			return true;
		}

		AAlicePawn* localPawn = GetLocalPawn();
		if (object == localPawn
			&& ShouldGuardBackgroundPlayerDamage())
		{
			LogBackgroundDamageGuard("local-ai", value->Damage);
			return true;
		}
		if (g_config.role == Role::Client
			&& object == localPawn
			&& g_clientEnemyAuthorityActive)
		{
			const std::uint64_t localKey =
				SharedEnemyStableKey(enemy);
			const auto alias =
				g_clientEnemyHostKeyByActor.find(enemy);
			const std::uint64_t entityKey =
				alias != g_clientEnemyHostKeyByActor.end()
					? alias->second : localKey;
			const auto binding =
				g_clientSharedEnemyBindings.find(entityKey);
			if (entityKey
				&& binding
					!= g_clientSharedEnemyBindings.end()
				&& binding->second.authorized
				&& binding->second.clientAuthority
				&& binding->second.enemy == enemy)
			{
				Log("PLAYERDAMAGE accepted delegated client AI hit, "
					"key=" + SharedWorldKeyText(entityKey)
					+ ", enemy=" + ObjectName(enemy)
					+ ", damage="
					+ std::to_string(value->Damage) + '.');
				return false;
			}
			// Client-side Kynapse continues running around the host pose and
			// may occasionally produce a duplicate touch/melee hit. The host
			// is authoritative for non-delegated NPC targeting and sends the
			// real hit above.
			const auto now = Clock::now();
			if (now >= g_nextSuppressedClientDamageLog)
			{
				g_nextSuppressedClientDamageLog =
					now + std::chrono::seconds(2);
				Log("PLAYERDAMAGE suppressed client-local AI hit, "
					"enemy=" + ObjectName(enemy)
					+ ", damage="
					+ std::to_string(value->Damage) + '.');
			}
			return true;
		}
		return false;
	}

	bool IsWorldTraceEnabled()
	{
		return g_config.enabled && !g_soloMinigameActive
			&& g_config.worldTrace;
	}

	void TraceWorldProcessEvent(UObject* object, UFunction* function,
		const void* parameters, bool after)
	{
		if (!g_config.enabled || g_soloMinigameActive
			|| !g_config.worldTrace
			|| !object || !function || !g_currentWorld
			|| !g_State.bRealGameplay)
		{
			return;
		}
		if (!IsPotentialWorldTraceFunction(function))
			return;
		const WorldTraceKind kind =
			IdentifyWorldTraceObject(object);
		if (kind == WorldTraceKind::None)
			return;
		auto* actor = reinterpret_cast<AActor*>(object);
		AAlicePawn* localPawn = GetLocalPawn();
		if (actor->WorldInfo != g_currentWorld
			|| !IsWithinWorldTraceRadius(actor, localPawn))
		{
			return;
		}
		const std::string functionName = function->GetName();
		if (!IsWorldTraceFunction(kind, functionName))
			return;

		std::string key = WorldTraceStableKey(object, kind);
		const auto record = g_worldTraceRecords.find(object);
		if (record != g_worldTraceRecords.end())
			key = record->second.key;
		Log("WORLDTRACE EVENT #"
			+ std::to_string(++g_worldTraceEventSerial)
			+ ", phase=" + (after ? std::string("post")
				: std::string("pre"))
			+ ", key=" + key
			+ ", kind=" + WorldTraceKindName(kind)
			+ ", object=" + ObjectName(object)
			+ ", function=" + function->GetFullName()
			+ ", params=" + (after
				? std::string("-")
				: WorldTraceEventParameters(
					kind, functionName, parameters))
			+ ", state={" + WorldTraceState(object, kind) + "}.");
	}

	void HandleSharedCombatProcessEvent(UObject* object,
		UFunction* function, const void* parameters, bool after)
	{
		if (g_soloMinigameActive)
			return;
		if (g_config.enabled
			&& g_applyingSharedEnemyDamage
			&& g_config.role == Role::Host
			&& object && function && parameters && !after
			&& object->IsA(
				AAliceGameKynapseAIController::StaticClass()))
		{
			const std::string callback = function->GetName();
			if (callback == "NotifyTakeHit"
				|| callback == "KynapseTakeDamager")
			{
				AController* instigator =
					*reinterpret_cast<AController* const*>(
						parameters);
				Log("AGGRO native callback=" + callback
					+ ", receiver=" + ObjectName(object)
					+ ", instigator="
					+ ObjectName(instigator)
					+ ", pawn="
					+ ObjectName(
						instigator ? instigator->Pawn : nullptr)
					+ ", isPlayer="
					+ (instigator && instigator->bIsPlayer
						? std::string("yes.")
						: std::string("no.")));
			}
		}
		if (!g_config.enabled || !g_config.sharedEnemyHealth
			|| !object || !function || !g_currentWorld
			|| !g_State.bRealGameplay
			|| g_applyingSharedEnemyDamage)
		{
			return;
		}
		const WorldTraceKind traceKind =
			IdentifyWorldTraceObject(object);
		if (traceKind == WorldTraceKind::Breakable)
		{
			if (!after || g_applyingSharedBreakableDestroy
				|| function->GetName() != "TakeDamage")
			{
				return;
			}
			auto* breakable =
				reinterpret_cast<AGameBreakableActor*>(object);
			if (breakable->WorldInfo != g_currentWorld
				|| !breakable->bDestoryed)
			{
				return;
			}
			const std::uint64_t entityKey =
				WorldTraceStableKeyValue(
					object, WorldTraceKind::Breakable);
			if (!entityKey
				|| !g_sentSharedBreakableKeys.insert(
					entityKey).second)
			{
				return;
			}
			SharedWorldEventPayload event{};
			event.kind =
				SharedWorldEventKind::BreakableDestroyed;
			event.originActorId =
				g_config.role == Role::Host ? 1u : 2u;
			event.entityKey = entityKey;
			event.flags =
				(breakable->CanSpawnHealth ? 1u : 0u)
				| (breakable->CanSpawnXP ? 2u : 0u);
			event.hitLocation[0] = breakable->Location.X;
			event.hitLocation[1] = breakable->Location.Y;
			event.hitLocation[2] = breakable->Location.Z;
			QueueSharedWorldEvent(event);
			Log("SHAREDBREAKABLE TX key="
				+ SharedWorldKeyText(entityKey)
				+ ", object=" + ObjectName(breakable)
				+ ", role="
				+ (g_config.role == Role::Host
					? std::string("host.")
					: std::string("client.")));
			return;
		}
		const auto cached =
			g_sharedDamageFunctionCache.find(function);
		bool isTakeDamage = false;
		if (cached != g_sharedDamageFunctionCache.end())
			isTakeDamage = cached->second;
		else
		{
			isTakeDamage = function->GetName() == "TakeDamage";
			g_sharedDamageFunctionCache.emplace(
				function, isTakeDamage);
		}
		if (!isTakeDamage
			|| traceKind != WorldTraceKind::Enemy)
		{
			return;
		}
		auto* enemy =
			reinterpret_cast<AAliceGameKynapsePawn*>(object);
		if (enemy->WorldInfo != g_currentWorld)
			return;
		const std::uint64_t localEntityKey =
			SharedEnemyStableKey(enemy);
		const auto enemyAlias =
			g_clientEnemyHostKeyByActor.find(enemy);
		const std::uint64_t entityKey =
			g_config.role == Role::Client
				&& enemyAlias != g_clientEnemyHostKeyByActor.end()
				? enemyAlias->second : localEntityKey;
		if (g_config.role == Role::Client
			&& g_config.sharedEnemyTransforms
			&& g_hostAuthorizedEnemyKeys.find(localEntityKey)
				== g_hostAuthorizedEnemyKeys.end())
		{
			if (g_loggedUnauthorizedEnemyDamage.insert(
					entityKey).second)
			{
				Log("SHAREDWORLD blocked client-only enemy damage key="
					+ SharedWorldKeyText(entityKey)
					+ "; host has not advertised this enemy.");
			}
			return;
		}

		if (!after)
		{
			if (!parameters)
				return;
			const auto* value = reinterpret_cast<const
				AAliceGameKynapsePawn_eventTakeDamage_Params*>(
					parameters);
			if (value->DamageAmount <= 0
				|| value->DamageAmount > 100000)
			{
				return;
			}
			PendingEnemyDamage pending{};
			pending.damage = value->DamageAmount;
			pending.hitLocation = value->HitLocation;
			pending.momentum = value->Momentum;
			pending.damageType = ObjectName(value->DamageType);
			g_pendingEnemyDamage[object] = std::move(pending);
			return;
		}

		const auto pending = g_pendingEnemyDamage.find(object);
		if (pending == g_pendingEnemyDamage.end())
			return;
		const PendingEnemyDamage damage = pending->second;
		g_pendingEnemyDamage.erase(pending);

		SharedWorldEventPayload event{};
		event.kind = g_config.role == Role::Host
			? SharedWorldEventKind::EnemyAuthoritativeState
			: SharedWorldEventKind::EnemyDamageRequest;
		event.originActorId =
			g_config.role == Role::Host ? 1u : 2u;
		event.entityKey = entityKey;
		event.damage = damage.damage;
		event.health = enemy->Health;
		event.healthMax = enemy->HealthMax;
		event.flags = enemy->Health <= 0
			|| enemy->bDeleteMe
			|| enemy->bPendingDelete
			? SharedWorldEnemyDead : 0;
		event.hitLocation[0] = damage.hitLocation.X;
		event.hitLocation[1] = damage.hitLocation.Y;
		event.hitLocation[2] = damage.hitLocation.Z;
		event.momentum[0] = damage.momentum.X;
		event.momentum[1] = damage.momentum.Y;
		event.momentum[2] = damage.momentum.Z;
		strncpy_s(event.damageType,
			damage.damageType.c_str(), _TRUNCATE);

		if (g_config.role == Role::Host)
		{
			RegisterHostEnemyAggroDamage(
				event.entityKey, 1, event.damage);
			QueueAuthoritativeEnemyState(
				enemy, event.entityKey, event, 1);
		}
		else
		{
			QueueSharedWorldEvent(event);
			Log("SHAREDWORLD TX damage key="
				+ SharedWorldKeyText(event.entityKey)
				+ ", damage=" + std::to_string(event.damage)
				+ ", localHealth="
				+ std::to_string(event.health)
				+ '/' + std::to_string(event.healthMax) + '.');
		}
	}

	bool ShouldUseUniqueMutex()
	{
		return g_config.enabled;
	}

	void ObserveSavePath(const wchar_t* effectivePath)
	{
		if (!g_config.enabled || !effectivePath || !*effectivePath)
			return;
		const std::filesystem::path path(effectivePath);
		const std::wstring name = path.filename().wstring();
		SaveSyncFileKind kind{};
		if (_wcsicmp(name.c_str(),
				SaveFileName(SaveSyncFileKind::PersistentData)) == 0)
			kind = SaveSyncFileKind::PersistentData;
		else if (_wcsicmp(name.c_str(),
				SaveFileName(SaveSyncFileKind::Checkpoint)) == 0)
			kind = SaveSyncFileKind::Checkpoint;
		else
			return;
		std::error_code error;
		const std::filesystem::path absolute =
			std::filesystem::absolute(path, error);
		std::lock_guard<std::mutex> lock(g_observedSavePathMutex);
		g_observedSavePaths[SaveFileIndex(kind)] =
			error ? path : absolute;
	}

	bool TryRedirectClientSave(const wchar_t* originalPath, std::wstring& redirectedPath)
	{
		if (!g_config.enabled || g_config.role != Role::Client || !originalPath || !*originalPath)
			return false;
		RememberOriginalClientSavePath(originalPath);

		const std::filesystem::path sandboxDirectory =
			std::filesystem::path(SystemHelper::GetModulePath()) / L"AliceCoop" / L"client-saves";
		std::error_code error;
		std::filesystem::create_directories(sandboxDirectory, error);
		const std::filesystem::path source(originalPath);
		redirectedPath = (sandboxDirectory / source.filename()).wstring();
		return true;
	}

	void Initialize()
	{
		ReadConfig();
		if (!g_config.enabled)
			return;

		const std::filesystem::path coopDirectory =
			std::filesystem::path(SystemHelper::GetModulePath()) / L"AliceCoop";
		const std::filesystem::path logDirectory = coopDirectory / L"logs";
		std::error_code error;
		std::filesystem::create_directories(logDirectory, error);
		const std::wstring roleName = g_config.actionTrace
			? L"trace"
			: (g_config.localMirror
				? L"mirror"
				: (g_config.role == Role::Host ? L"host" : L"client"));
		const std::filesystem::path logPath = logDirectory
			/ (L"AliceCoop_" + roleName + L"_" + std::to_wstring(GetCurrentProcessId()) + L".log");
		g_log.open(logPath, std::ios::app);
		InitializeSharedDevState();

		if (g_config.actionTrace)
		{
			Log("Initializing AliceCoop action/animation trace, eventWindowMs="
				+ std::to_string(g_config.actionTraceWindowMs) + '.');
		}
		else if (g_config.localMirror)
		{
			Log("Initializing AliceCoop local-mirror diagnostic, distance="
				+ std::to_string(g_config.localMirrorDistance) + '.');
		}
		else
		{
			Log("Initializing AliceCoop role="
				+ std::string(g_config.role == Role::Host ? "host" : "client")
				+ ", relay=" + g_config.serverAddress + ':'
				+ std::to_string(g_config.port) + '.');
		}
		if (g_config.worldTrace)
		{
			Log("WORLDTRACE enabled, radius="
				+ std::to_string(
					static_cast<int>(g_config.worldTraceRadius))
				+ ", categories=enemy|breakable|pickup|dropped-pickup.");
		}
		if (g_config.vfxLifecycleTrace
			|| g_config.animationLifecycleTrace
			|| g_config.animationComparisonTrace
			|| g_config.controlLifecycleTrace)
		{
			Log("FOCUSEDTRACE enabled vfx="
				+ std::to_string(g_config.vfxLifecycleTrace ? 1 : 0)
				+ ", animation="
				+ std::to_string(g_config.animationLifecycleTrace ? 1 : 0)
				+ ", animationCompare="
				+ std::to_string(g_config.animationComparisonTrace ? 1 : 0)
				+ ", control="
				+ std::to_string(g_config.controlLifecycleTrace ? 1 : 0)
				+ "; normal co-op replication remains active.");
		}
		if (g_config.sharedEnemyHealth)
		{
			Log("SHAREDWORLD enemy health/death relay enabled; "
				"host is authoritative.");
		}
		if (g_config.sharedEnemyTransforms)
		{
			Log("SHAREDPOSE host-authoritative enemy transforms enabled; "
				"radius="
				+ std::to_string(
					static_cast<int>(g_config.sharedEnemyRadius))
				+ ", max="
				+ std::to_string(MaxSharedEnemies)
				+ ", correctionSpeed="
				+ std::to_string(g_config.sharedEnemyCorrectionSpeed)
				+ ", snapDistance="
				+ std::to_string(g_config.sharedEnemySnapDistance)
				+ ", client AI hypothesis=bPauseTick.");
		}
		if (g_config.backgroundWindowDamageGuard)
		{
			Log("PLAYERDAMAGE background-window test guard enabled.");
		}

		if (g_config.forceWindowed)
		{
			UseWindowed = true;
			AutoResolution = false;
		}
		if (g_config.lowMemoryMode)
		{
			MaxFPS = g_config.maxFps;
			MaxPoolThreads = g_config.maxPoolThreads;
			ForceHighResTextures = false;
			ImprovedTextureStreaming = false;
			DisableBackgroundLevelStreaming = false;
			ReducedMipMapBias = false;
			AdaptivePhysXMemory = false;
			Log("Low-memory mode enabled: FPS=" + std::to_string(MaxFPS)
				+ ", pool threads=" + std::to_string(MaxPoolThreads) + '.');
		}
		if (g_config.role == Role::Client)
			AtomicSaves = true;

		if (g_config.actionTrace)
		{
			Log("Action trace is isolated from UDP; no server or visual proxy is used.");
			return;
		}
		if (g_config.localMirror)
		{
			Log("Local mirror is isolated from UDP; no AliceCoop server is required.");
			return;
		}

		WSADATA winsock{};
		if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0)
		{
			Log("ERROR: WSAStartup failed; AliceCoop disabled.");
			g_config.enabled = false;
			return;
		}

		g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (g_socket == INVALID_SOCKET)
		{
			Log("ERROR: socket() failed; AliceCoop disabled.");
			g_config.enabled = false;
			WSACleanup();
			return;
		}

		u_long nonBlocking = 1;
		ioctlsocket(g_socket, FIONBIO, &nonBlocking);
		g_serverEndpoint.sin_family = AF_INET;
		g_serverEndpoint.sin_port = htons(g_config.port);
		if (InetPtonA(AF_INET, g_config.serverAddress.c_str(), &g_serverEndpoint.sin_addr) != 1)
		{
			Log("ERROR: ServerAddress must be an IPv4 address; AliceCoop disabled.");
			closesocket(g_socket);
			g_socket = INVALID_SOCKET;
			g_config.enabled = false;
			WSACleanup();
			return;
		}

		g_running = true;
		g_networkThread = std::thread(NetworkLoop);
		g_networkThread.detach();
	}

	void Tick()
	{
		if (!g_config.enabled)
			return;

		// Keep presentation cleanup independent from AlicePawn.Tick. During
		// loading, cutscenes and proxy teardown that tick can pause entirely,
		// while pooled particle render data remains visible in the world.
		TickRetiredPresentationParticles();
		TickRemoteMovementParticleCapture();
		TickVfxLifecycleTrace();
		TickControlLifecycleTrace();
		CleanupRemoteWeaponTransients(false);
		PollSharedDevState();
		if (g_config.manageWindowGeometry)
			ConfigureGameWindow();
		AAlicePawn* localPawn = GetLocalPawn();
		AAlicePlayerController* localController =
			g_State.AlicePlayerController;
		AWorldInfo* availableWorld =
			localPawn && localPawn->WorldInfo
				? localPawn->WorldInfo
				: (localController
					? localController->WorldInfo : nullptr);
		if (availableWorld)
		{
			RefreshMapName(availableWorld);
			const auto now = Clock::now();
			if (now >= g_nextSoloMinigameDetection)
			{
				g_nextSoloMinigameDetection =
					now + std::chrono::milliseconds(250);
				std::string soloLevel;
				SetSoloMinigameActive(
					DetectSoloMinigame(
						availableWorld, soloLevel),
					soloLevel);
			}
		}
		RefreshActiveProfileSaveTargets();
		ApplyPendingClientProfileMetadata();
		UpdateCoopMainMenuPanel(availableWorld);
		bool peerConnectedForUi = false;
		if (g_config.role == Role::Host)
			peerConnectedForUi = ReadFreshClientCommand().has_value();
		else if (g_config.role == Role::Client)
			peerConnectedForUi = ReadFreshHostSnapshot().has_value();
		const bool gameplayMap = !g_currentMap.empty()
			&& _stricmp(g_currentMap.c_str(), "AliceEntry") != 0;
		const bool pauseEscapeDown = AchievementOverlay::g_hWnd
			&& GetForegroundWindow() == AchievementOverlay::g_hWnd
			&& (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
		const std::uint64_t pauseEventSerial =
			g_pauseMenuEventSerial.load(std::memory_order_acquire);
		if (gameplayMap && pauseEscapeDown && !g_pauseMenuEscapeWasDown)
		{
			// Some localized builds do not call AliceGfxMovie_inGameMenu.OpenMenu
			// through ProcessEvent when Escape toggles the pause movie. Mirror the
			// same key edge as a fallback; explicit menu lifecycle events still win.
			if (pauseEventSerial == g_pauseMenuLastObservedSerial)
			{
				const bool wasOpen = g_pauseMenuEventOpen.load(
					std::memory_order_acquire);
				g_pauseMenuEventOpen.store(
					!wasOpen, std::memory_order_release);
				Log(std::string("UI pause menu Escape fallback=")
					+ (!wasOpen ? "open." : "closed."));
			}
		}
		g_pauseMenuEscapeWasDown = pauseEscapeDown;
		g_pauseMenuLastObservedSerial = pauseEventSerial;
		if (!gameplayMap)
		{
			g_pauseMenuEventOpen.store(false, std::memory_order_release);
			g_pauseMenuEscapeWasDown = pauseEscapeDown;
			g_pauseMenuLastObservedSerial = pauseEventSerial;
		}
		const bool pauseMenuEventOpen =
			g_pauseMenuEventOpen.load(std::memory_order_acquire);
		const bool pauseMenuVisible = gameplayMap
			&& localController && availableWorld
			&& pauseMenuEventOpen;
		if (pauseMenuVisible != g_lastPausePanelVisible)
		{
			g_lastPausePanelVisible = pauseMenuVisible;
			Log(std::string("UI pause panel visible=")
				+ (pauseMenuVisible ? "yes." : "no."));
		}
		bool requestedTrails = g_config.preserveMovementTrails;
		const bool pauseTrailsKeyDown = pauseMenuVisible
			&& AchievementOverlay::g_hWnd
			&& GetForegroundWindow() == AchievementOverlay::g_hWnd
			&& (GetAsyncKeyState('T') & 0x8000) != 0;
		bool trailsChanged = false;
		if (pauseTrailsKeyDown && !g_pauseMenuTrailsKeyWasDown)
		{
			requestedTrails = !requestedTrails;
			trailsChanged = true;
		}
		g_pauseMenuTrailsKeyWasDown = pauseTrailsKeyDown;
		bool clickedTrails = requestedTrails;
		if (AchievementOverlay::ConsumeCoopMovementTrailsToggle(
				clickedTrails))
		{
			requestedTrails = clickedTrails;
			trailsChanged = true;
		}
		if (trailsChanged)
		{
			g_config.preserveMovementTrails = requestedTrails;
			if (!requestedTrails)
			{
				DestroyRemoteStaticLeafTrail();
				const Clock::time_point expireNow = Clock::now();
				for (TrackedPresentationParticle& particle :
					g_remoteMovementTransientParticles)
				{
					particle.expiresAt = expireNow;
				}
				for (const PendingMovementParticlePreservation& pending :
					g_pendingMovementParticlePreservations)
				{
					if (IsLivePresentationParticle(pending.component)
						&& pending.component->Template
							== pending.particleTemplate)
					{
						HardStopPresentationParticle(
							pending.component, true);
					}
				}
				g_pendingMovementParticlePreservations.clear();
				for (TrackedPresentationParticle& particle :
					g_remoteWeaponTransientParticles)
				{
					if (particle.expiresAt == (Clock::time_point::max)())
						particle.expiresAt = expireNow;
				}
			}
			PersistMovementTrailsSetting(requestedTrails);
			Log(std::string("VFX movement trail history ")
				+ (requestedTrails ? "enabled" : "disabled") + ".");
		}
		AchievementOverlay::SetCoopPauseMenuState(
			pauseMenuVisible,
			g_config.role == Role::Host,
			peerConnectedForUi,
			g_config.preserveMovementTrails);
		std::string saveSyncStatus;
		{
			std::lock_guard<std::mutex> lock(g_saveSyncStatusMutex);
			saveSyncStatus = g_saveSyncStatus;
		}
		std::string selectedSaveProfileName;
		{
			std::lock_guard<std::mutex> lock(g_observedSavePathMutex);
			selectedSaveProfileName = g_activeSaveProfileName;
		}
		const bool saveSyncAvailable =
			g_config.role == Role::Client
			&& _stricmp(g_currentMap.c_str(), "AliceEntry") == 0
			&& peerConnectedForUi
			&& HaveOriginalClientSaveTargets()
			&& !g_saveSyncInProgress.load();
		if (g_config.role == Role::Client
			&& _stricmp(g_currentMap.c_str(), "AliceEntry") == 0
			&& peerConnectedForUi
			&& !HaveOriginalClientSaveTargets()
			&& saveSyncStatus.empty())
		{
			saveSyncStatus = "SELECT/CREATE CLIENT PROFILE FIRST";
		}
		AchievementOverlay::SetCoopSaveSyncState(
			saveSyncAvailable,
			g_saveSyncInProgress.load(),
			g_saveSyncProgress.load(),
			saveSyncStatus,
			selectedSaveProfileName);
		const bool saveSyncRequested =
			AchievementOverlay::ConsumeCoopSaveSyncRequest();
		if (saveSyncAvailable && saveSyncRequested)
		{
			{
				std::lock_guard<std::mutex> lock(g_observedSavePathMutex);
				g_requestedClientSavePaths = g_originalClientSavePaths;
				g_requestedClientProfileName = g_activeSaveProfileName;
			}
			std::uint32_t transferId = static_cast<std::uint32_t>(
				GetTickCount64() ^ (static_cast<std::uint64_t>(
					GetCurrentProcessId()) << 12));
			if (transferId == 0)
				transferId = 1;
			g_requestedSaveTransferId.store(
				transferId, std::memory_order_release);
			SetSaveSyncStatus("REQUESTING HOST SAVE", true, 0);
		}
		else if (saveSyncRequested)
		{
			SetSaveSyncStatus("HOST SAVE SYNC UNAVAILABLE", false, 0);
		}
		if ((g_joinHostPending
				|| g_joinHostRetryUntil != Clock::time_point{})
			&& !g_currentMap.empty()
			&& _stricmp(
				g_currentMap.c_str(), "AliceEntry") != 0
			&& localPawn && localPawn->Mesh
			&& g_joinHostLoadInvokedAt != Clock::time_point{}
			&& Clock::now() - g_joinHostLoadInvokedAt
				>= std::chrono::milliseconds(2200))
		{
			g_joinHostPending = false;
			FinishJoinHostAttempts();
			SetJoinHostStatus(
				"HOST LOCATION LOADED; P SAFE / O FORCED");
		}
		if (g_soloMinigameActive)
		{
			PublishSoloMinigamePresence();
			return;
		}
		if (ApplyPendingLifecycleCommand())
			return;
		if (!localPawn || !localPawn->WorldInfo)
		{
			g_lastLoggedLocalWeapon = nullptr;
			g_lastLoggedLocalAliceHealth =
				(std::numeric_limits<int>::min)();
			if (g_config.actionTrace)
			{
				g_traceObjectLabelsReady = false;
				g_tracedPawn = nullptr;
			}
			if (g_currentWorld || g_remotePawn
				|| g_remotePresentationWeapon)
			{
				ForgetTornDownWorldObjects();
				ResetSharedEnemyPose(false);
				g_worldTraceRecords.clear();
				g_worldTraceWorld = nullptr;
				g_worldTraceMap.clear();
				g_currentWorld = nullptr;
				g_currentMap.clear();
				g_cutsceneTraceInitialized = false;
				g_waitingCutsceneAction = nullptr;
				g_waitingCutsceneKey = 0;
				g_waitingCutsceneOriginalPlayRate = 1.0f;
				g_waitingCutscenePlayRateOverridden = false;
				g_waitingCutsceneCanDeferActivation = false;
				g_waitingCutsceneActivationDeferred = false;
				g_cutsceneBarrierAdvertiseKey = 0;
				g_recentReleasedCutsceneKey = 0;
				g_emergencyCutsceneAdvertiseKey = 0;
				g_emergencyCutsceneAdvertiseUntil = {};
				g_lastEmergencyCutsceneKey = 0;
				g_lastEmergencyCutsceneUntil = {};
				g_emergencyForcedCutsceneAction = nullptr;
				g_emergencyForcedCutsceneRequestedAt = {};
				g_emergencyForcedCutsceneStarted = false;
				AchievementOverlay::SetCoopWaitingForPeer(false);
				Log("Gameplay pawn unavailable; stale world-owned "
					"co-op references were discarded.");
			}
			return;
		}

		AWorldInfo* world = localPawn->WorldInfo;
		TickRetiredRemotePawns(world);
		if (localPawn->Health != g_lastLoggedLocalAliceHealth)
		{
			Log("SHAREDPLAYER local Alice health role="
				+ std::string(g_config.role == Role::Host
					? "host" : "client")
				+ ", health="
				+ std::to_string(localPawn->Health)
				+ '/' + std::to_string(localPawn->HealthMax)
				+ ", location="
				+ FormatWorldTraceVector(localPawn->Location)
				+ '.');
			g_lastLoggedLocalAliceHealth = localPawn->Health;
		}
		AWeaponForAlice* localWeapon =
			localPawn->Weapon
				&& localPawn->Weapon->IsA(
					AWeaponForAlice::StaticClass())
			? reinterpret_cast<AWeaponForAlice*>(
				localPawn->Weapon)
			: nullptr;
		if (localWeapon != g_lastLoggedLocalWeapon)
		{
			g_lastLoggedLocalWeapon = localWeapon;
			if (localWeapon)
				LogWeaponPresentationHierarchy(
					localWeapon, "original");
		}
		HandleHairRotationTuningInput();
		if (g_pendingUsedEventDetection)
		{
			const bool expired =
				Clock::now() >= g_usedEventDetectionUntil;
			DetectActivatedUsedEvents(expired);
		}
		if (g_pendingSequenceOpUseTrace)
		{
			const bool expired =
				Clock::now() >= g_sequenceOpUseTraceUntil;
			ScanSequenceOpUseChanges(expired);
		}
		if (g_pendingContextActorUseDetection)
		{
			const bool expired =
				Clock::now()
					>= g_contextActorUseDetectionUntil;
			DetectStartedContextActor(expired);
		}
		TickRemoteContextInteraction();
		ApplyInboundSharedWorldEvents(localPawn, world);
		TickWorldTrace(localPawn, world);
		if (g_config.actionTrace)
		{
			if (!g_State.bRealGameplay
				|| _stricmp(g_currentMap.c_str(), "AliceEntry") == 0)
			{
				g_traceObjectLabelsReady = false;
				g_tracedPawn = nullptr;
				return;
			}
			const std::uint8_t localSpecialMove =
				static_cast<std::uint8_t>(localPawn->SpecialMove);
			if (localSpecialMove != g_lastLocalSpecialMove)
			{
				if (localPawn->SpecialMove
						== ESpecialMove::SM_PHYS_Trans_Jump
					|| localPawn->SpecialMove
						== ESpecialMove::SM_PHYS_Trans_DoubleJump)
				{
					RecordLocalAction(PlayerAction::Jump);
				}
				g_lastLocalSpecialMove = localSpecialMove;
			}
			TickActionTrace(localPawn, world);
			return;
		}
		ApplyInboundProjectileEvents(world);
		TickRemoteProjectileVisuals(world);
		TickLocalClockBombReplication();
		if (g_config.localMirror)
		{
			g_activeRemoteAnimationGraph.reset();
		}
		else
		{
			const auto inboundGraph = ReadFreshAnimationGraph();
			if (inboundGraph
				&& inboundGraph->graph.mapHash == HashMapName(g_currentMap))
			{
				g_activeRemoteAnimationGraph = inboundGraph;
			}
			else
				g_activeRemoteAnimationGraph.reset();
		}

		const std::uint8_t localSpecialMove =
			static_cast<std::uint8_t>(localPawn->SpecialMove);
		if (localSpecialMove != g_lastLocalSpecialMove)
		{
			if (localPawn->SpecialMove == ESpecialMove::SM_PHYS_Trans_Jump
				|| localPawn->SpecialMove == ESpecialMove::SM_PHYS_Trans_DoubleJump)
			{
				RecordLocalAction(PlayerAction::Jump);
			}
			g_lastLocalSpecialMove = localSpecialMove;
		}

		const auto localAction = static_cast<PlayerAction>(g_localAction.load());
		const std::uint32_t localActionSerial = g_localActionSerial.load();
		const std::uint32_t localActorId =
			g_config.role == Role::Host ? 1u : 2u;
		const PlayerStatePayload localState = CaptureState(localPawn, localActorId,
			g_config.role == Role::Host, localAction, localActionSerial);
		if (!g_config.localMirror)
		{
			const AnimationGraphPayload localGraph =
				CaptureAnimationGraph(localPawn, localActorId);
			TraceLocalAnimationComparison(localGraph);
			{
				std::lock_guard lock(g_stateMutex);
				g_outboundAnimationGraph = localGraph;
			}
		}

		PlayerStatePayload peerState{};
		bool hasFreshPeer = false;
		std::optional<ClientCommandSnapshot> clientCommand;
		std::optional<HostWorldSnapshot> hostSnapshot;
		if (g_config.localMirror)
		{
			peerState = localState;
			peerState.actorId = 2;
			peerState.flags &= ~StateAuthoritative;
			constexpr float RotatorToRadians =
				6.2831853071795864769f / 65536.0f;
			const float yaw = static_cast<float>(localState.rotation[1])
				* RotatorToRadians;
			peerState.location[0] -= std::sin(yaw)
				* g_config.localMirrorDistance;
			peerState.location[1] += std::cos(yaw)
				* g_config.localMirrorDistance;
			hasFreshPeer = true;
		}
		else if (g_config.role == Role::Client)
		{
			const ClientCommandPayload command = CaptureClientCommand(localState);
			{
				std::lock_guard lock(g_stateMutex);
				g_outboundClientCommand = command;
			}

			hostSnapshot = ReadFreshHostSnapshot();
			if (hostSnapshot)
			{
				ApplyHostWeaponProgression(
					hostSnapshot->snapshot.hostState);
				peerState = hostSnapshot->snapshot.hostState;
				hasFreshPeer = true;
			}
		}
		else
		{
			clientCommand = ReadFreshClientCommand();
			if (clientCommand)
			{
				peerState = clientCommand->command.desiredState;
				hasFreshPeer = true;
			}
		}
		if (g_peerWasFresh && !hasFreshPeer)
			g_peerTeardownRequested = true;
		g_peerWasFresh = hasFreshPeer;
		if (g_peerTeardownRequested.exchange(false))
		{
			TeardownDisconnectedPeerPresentation(
				hasFreshPeer ? "relay peer-left/reconnected"
					: "peer-left-or-timeout");
		}

		const bool peerOnSameMap = hasFreshPeer && !g_currentMap.empty()
			&& _stricmp(peerState.mapName, g_currentMap.c_str()) == 0;
		TickCutsceneBarrier(peerState, peerOnSameMap);
		UpdateHostCutsceneNpcBlindness(
			world, g_waitingCutsceneAction != nullptr);
		const bool proxyAllowedOnMap =
			_stricmp(g_currentMap.c_str(), "AliceEntry") != 0;
		if (!g_config.localMirror
			&& g_config.role == Role::Host
			&& clientCommand
			&& (clientCommand->command.buttons
				& ClientCommandRequestRestart) != 0
			&& !g_hostRestartIssued)
		{
			ApplyPendingLifecycleCommand();
			return;
		}
		const bool localCinematic =
			(localState.flags & StateCinematic) != 0;
		const bool peerCinematic =
			peerOnSameMap
				&& (peerState.flags & StateCinematic) != 0;
		if (g_consumedContextUiActor)
		{
			if (localCinematic)
			{
				g_consumedContextUiSawCinematic = true;
			}
			else if (g_consumedContextUiSawCinematic)
			{
				ClearConsumedContextActorUi(
					"cinematic-finished");
			}
			else if (Clock::now()
				>= g_consumedContextUiFallbackAt)
			{
				ClearConsumedContextActorUi(
					"no-cinematic-fallback");
			}
		}
		if (!g_cutsceneTraceInitialized
			|| localCinematic != g_lastLocalCinematic
			|| peerCinematic != g_lastPeerCinematic)
		{
			if (g_cutsceneTraceInitialized)
			{
				float peerDistance = -1.0f;
				if (peerOnSameMap)
				{
					const FVector peerLocation(
						peerState.location[0],
						peerState.location[1],
						peerState.location[2]);
					peerDistance = std::sqrt(
						(peerLocation - localPawn->Location)
							.SizeSquared());
				}
				Log("CUTSCENETRACE local="
					+ std::string(localCinematic ? "on" : "off")
					+ ", peer="
					+ std::string(peerCinematic ? "on" : "off")
					+ ", peerSameMap="
					+ std::string(peerOnSameMap ? "yes" : "no")
					+ ", distance="
					+ std::to_string(
						static_cast<int>(
							std::lround(peerDistance)))
					+ ", localLocation="
					+ FormatWorldTraceVector(
						localPawn->Location) + '.');
			}
			g_cutsceneTraceInitialized = true;
			g_lastLocalCinematic = localCinematic;
			g_lastPeerCinematic = peerCinematic;
		}

		if (!g_config.localMirror
			&& g_config.role == Role::Client)
		{
			ApplySharedEnemyPoses(
				peerOnSameMap && hostSnapshot
					? &hostSnapshot->snapshot
					: nullptr,
				world);
		}

		if (!g_config.localMirror
			&& g_config.role == Role::Client
			&& hostSnapshot
			&& hostSnapshot->snapshot.commandSerial != 0
			&& hostSnapshot->snapshot.commandSerial
				!= g_lastSeenHostCommandSerial)
		{
			g_lastSeenHostCommandSerial =
				hostSnapshot->snapshot.commandSerial;
			if ((hostSnapshot->snapshot.commandFlags
					& HostCommandRestartCheckpoint) != 0)
			{
				ApplyPendingLifecycleCommand();
				return;
			}
			else if ((hostSnapshot->snapshot.commandFlags
					& HostCommandSummonClient) != 0
				&& peerOnSameMap)
			{
				TeleportLocalPawnToHost(localPawn,
					hostSnapshot->snapshot.hostState,
					"host summon");
			}
		}

		if (!g_config.localMirror && peerOnSameMap
			&& Clock::now() >= g_groupDeathSuppressUntil)
		{
			const bool localDead = localPawn->Health <= 0;
			const bool peerDead = peerState.health <= 0;
			if (localDead || peerDead)
				g_groupDeathActive = true;
			if (peerDead && !localDead)
			{
				ForceLocalGroupDeath(localPawn,
					g_config.role == Role::Host
						? "client death" : "host death");
				return;
			}
			if (g_groupDeathActive && !localDead && !peerDead)
			{
				g_groupDeathActive = false;
				g_localGroupDeathForced = false;
				g_localRestartInitiated = false;
				g_clientRestartRequestPending = false;
				if (g_config.role == Role::Host)
				{
					g_hostRestartIssued = false;
					g_hostCommandFlags = 0;
				}
				Log("GROUPLIFE both players healthy; restart "
					"barrier cleared.");
			}
		}

		if (!g_config.localMirror && ConsumeTeleportKeyPress())
		{
			if (!peerOnSameMap)
			{
				SetTeleportStatus(
					"request rejected: peer is not on the same map");
			}
			else if (g_config.role == Role::Client)
			{
				TeleportLocalPawnToHost(
					localPawn, peerState, "client P");
			}
			else
			{
				TeleportLocalPawnToHost(
					localPawn, peerState, "host P");
			}
		}

		if (!g_config.localMirror && ConsumeForceTeleportKeyPress())
		{
			if (!peerOnSameMap)
			{
				SetTeleportStatus(
					"force request rejected: peer is not on the same map");
			}
			else if (g_config.role == Role::Client)
			{
				TeleportLocalPawnToHost(
					localPawn, peerState, "client O force", true);
			}
			else
			{
				SetTeleportStatus(
					"force request ignored: O is client-only");
			}
		}

		if (g_config.visualProxy && peerOnSameMap && proxyAllowedOnMap)
		{
			AAlicePawn* remote = g_remotePawn;
			if (!remote || g_remoteWorld != world)
				remote = SpawnRemotePawn(localPawn, world, peerState);
			if (remote)
			{
				UpdateRemotePawn(remote, world, peerState);
				if (g_activeRemoteAnimationGraph)
				{
					TraceRemoteAnimationComparison(
						remote, *g_activeRemoteAnimationGraph);
				}
				if (g_config.localMirror)
					BindLocalMirrorPose(remote, localPawn);
			}
		}
		else if (g_remotePawn)
		{
			DestroyRemotePawn();
		}

		if (!g_config.localMirror && g_config.role == Role::Host)
		{
			TickHostEnemyAggro(
				localPawn,
				peerOnSameMap ? g_remotePawn : nullptr,
				world,
				peerOnSameMap);
			ApplyClientEnemyAuthorityPoses(
				peerOnSameMap && clientCommand
					? &clientCommand->command
					: nullptr,
				world);
			HostSnapshotPayload snapshot{};
			snapshot.hostState = localState;
			snapshot.snapshotNumber = ++g_snapshotNumber;
			snapshot.worldEpoch = g_worldEpoch;
			snapshot.hostTimeMs = ElapsedMilliseconds();
			if (Clock::now() >= g_hostCommandExpires)
				g_hostCommandFlags = 0;
			snapshot.commandSerial = g_hostCommandSerial;
			snapshot.commandFlags = g_hostCommandFlags;
			snapshot.reserved[0] =
				CurrentCheckpointWireValue(world);
			CaptureSharedEnemyPoses(localPawn, world, snapshot);
			if (g_remotePawn && clientCommand)
			{
				snapshot.clientState = CaptureState(g_remotePawn, 2, true,
					clientCommand->command.desiredState.action,
					clientCommand->command.desiredState.actionSerial);
			}
			{
				std::lock_guard lock(g_stateMutex);
				g_outboundHostSnapshot = snapshot;
			}
		}

		// The permanent debug status/hotkey hint is intentionally disabled in
		// playtest builds. Player-facing controls live in the pause panel.
	}
}
