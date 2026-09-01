#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <bcrypt.h>

#include "Common.hpp"
#include "Coop/CoopClient.hpp"
#include "Coop/Core/PureHelpers.hpp"
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
		using namespace AliceCoopCore;
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
		#include "Coop/Detail/LifecycleAndStatus.inl"
	}

	#include "Coop/Detail/PublicHooks.inl"
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
