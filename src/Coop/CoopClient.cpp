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
		#include "Coop/Detail/RemoteCosmetics.inl"
		#include "Coop/Detail/PresentationParticles.inl"
		#include "Coop/Detail/RemotePawn.inl"
		#include "Coop/Detail/RemoteAnimation.inl"
		#include "Coop/Detail/RemoteActionVfx.inl"
		#include "Coop/Detail/Projectiles.inl"
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
