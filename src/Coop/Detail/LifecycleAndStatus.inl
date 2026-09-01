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
