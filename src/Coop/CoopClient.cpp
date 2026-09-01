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
		#include "Coop/Detail/StateAndTypes.inl"

		#include "Coop/Detail/InternalDeclarations.inl"

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
