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


        struct SafeArrival
        {
            AAlicePawn* pawn = nullptr;
            AWorldInfo* world = nullptr;
            std::string map, source, lastWait;
            AAlicePlayerController* controller = nullptr;
            bool originalStreamingVolumes = false, arrived = false;
            std::uint32_t peerActor = 0, hostEpoch = 0;
            Clock::time_point deadline{}, nextPoll{}, nextDiagnostic{}, settleUntil{};
            struct LevelLease { ULevelStreaming* level; bool loaded, visible; };
            std::vector<LevelLease> levels;
        };
        SafeArrival g_safeArrival;

        template <typename Params>
        bool InvokeArrivalNative(UObject* object, UFunction* function, Params& params)
        {
            if (!object || !function || !(function->FunctionFlags & 0x400)
                || !function->iNative) return false;
            // Actor.ProcessEvent adds an actor-script/world-begun-play gate.
            // Arrival runs after the engine tick, not as an actor script event.
            // Use the existing version-resolved UObject dispatcher for explicit
            // native streaming-volume queries, preserving their native metadata.
            using Dispatcher = void(__thiscall*)(UObject*, UFunction*, void*, void*);
            const auto dispatcher = reinterpret_cast<Dispatcher>(GetAddress(Addr::ProcessEvent));
            if (!dispatcher) return false;
            dispatcher(object, function, &params, nullptr);
            return true;
        }

        void ReleaseArrivalStreamingControl()
        {
            auto& arrival = g_safeArrival;
            if (arrival.controller && IsLiveUObject(arrival.controller)
                && arrival.controller == g_State.AlicePlayerController)
                arrival.controller->bIsUsingStreamingVolumes = arrival.originalStreamingVolumes;
        }

        void ReportArrivalWait(const std::string& stage)
        {
            auto& arrival = g_safeArrival;
            const auto now = Clock::now();
            if (stage != arrival.lastWait || now >= arrival.nextDiagnostic)
            {
                Log("TELEPORT " + arrival.source + " waiting: " + stage);
                arrival.nextDiagnostic = now + std::chrono::seconds(2);
            }
            arrival.lastWait = stage;
        }

        bool SetArrivalStreaming(ULevelStreaming* level, bool loaded, bool visible)
        {
            static UFunction* fn = UFunction::FindFunction(
                "Function Engine.PlayerController.ClientUpdateLevelStreamingStatus");
            APlayerController_execClientUpdateLevelStreamingStatus_Params p{};
            p.PackageName = level->PackageName;
            p.bNewShouldBeLoaded = loaded; p.bNewShouldBeVisible = visible;
            // Never block the game tick on asynchronous disk IO.
            return InvokeNativeFunction(g_State.AlicePlayerController, fn, p);
        }

        void CancelSafeArrival(const char* reason, bool notify)
        {
            if (!g_safeArrival.pawn) return;
            // The pawn stays at its original, playable location while preloading.
            // There is no suspended physics/visibility state to leak on timeout.
            ReleaseArrivalStreamingControl();
            if (!g_safeArrival.arrived && g_safeArrival.world == g_currentWorld && IsLiveUObject(g_safeArrival.world))
                for (const auto& lease : g_safeArrival.levels)
                    if (IsLiveUObject(lease.level))
                        SetArrivalStreaming(lease.level, lease.loaded, lease.visible);
            SetTeleportStatus(g_safeArrival.source + " cancelled: " + reason);
            AchievementOverlay::SetCoopArrivalStatus(
                notify ? "Could not meet partner. Press O to retry." : "", notify ? 5000 : 0);
            g_safeArrival = {};
        }

        void CaptureVisibleStreamingLevels(AWorldInfo* world, HostSnapshotPayload& snapshot)
        {
            if (!world) return;
            for (auto* level : world->StreamingLevels)
            {
                if (!level || !IsLiveUObject(level) || !level->bIsVisible || !level->LoadedLevel) continue;
                const auto hash = HashMapName(level->PackageName.ToString());
                if (snapshot.streamingLevelCount >= MaxVisibleStreamingLevels)
                { snapshot.streamingLevelsOverflow = 1; break; }
                snapshot.streamingLevelHashes[snapshot.streamingLevelCount++] = hash;
            }
        }

        bool TeleportLocalPawnToHost(AAlicePawn* localPawn,
            const PlayerStatePayload& peerState, const char* source,
            bool allowDistantStreaming = false)
        {
            if (!localPawn || !localPawn->WorldInfo || g_currentMap.empty()
                || _stricmp(peerState.mapName, g_currentMap.c_str()) != 0
                || localPawn->Health <= 0 || peerState.health <= 0
                || (peerState.flags & StateCinematic) != 0)
            { SetTeleportStatus(std::string(source) + " rejected: partner is not ready on the same map"); return false; }
            const FVector target(peerState.location[0], peerState.location[1], peerState.location[2]);
            if (!std::isfinite(target.X) || !std::isfinite(target.Y) || !std::isfinite(target.Z)) return false;
            if (!allowDistantStreaming && (target - localPawn->Location).SizeSquared() > 50000.0f * 50000.0f)
            {
                SetTeleportStatus(std::string(source) + " rejected: distant region; use O to prepare safe arrival");
                AchievementOverlay::SetCoopArrivalStatus("Partner is far away. Press O to prepare the area.", 5000);
                return false;
            }
            if (g_safeArrival.pawn) CancelSafeArrival("replaced by a new request", false);
            g_safeArrival.pawn = localPawn; g_safeArrival.world = localPawn->WorldInfo;
            g_safeArrival.map = g_currentMap; g_safeArrival.source = source;
            g_safeArrival.peerActor = peerState.actorId;
            g_safeArrival.controller = g_State.AlicePlayerController;
            if (!g_safeArrival.controller)
            { CancelSafeArrival("local controller unavailable"); return false; }
            // Native ProcessLevelStreamingVolumes (0x799440) recalculates desired
            // load/visibility at the SOURCE camera every tick, overwriting the
            // destination request before it is serviced. Its per-player gate is
            // bIsUsingStreamingVolumes. Hold it only for this bounded preload.
            g_safeArrival.originalStreamingVolumes = g_safeArrival.controller->bIsUsingStreamingVolumes != 0;
            g_safeArrival.controller->bIsUsingStreamingVolumes = false;
            Log("TELEPORT " + g_safeArrival.source + " acquired streaming control; volumes="
                + std::string(g_safeArrival.originalStreamingVolumes ? "on" : "off"));
            if (g_config.role == Role::Client)
                if (const auto host = ReadFreshHostSnapshot()) g_safeArrival.hostEpoch = host->snapshot.worldEpoch;
            g_safeArrival.deadline = Clock::now() + std::chrono::seconds(16);
            SetTeleportStatus(std::string(source) + " preparing destination sublevels");
            // Show preparation only if loading really has to wait. Clear a
            // previous failed request immediately, including on an ordinary P.
            AchievementOverlay::SetCoopArrivalStatus("", 0);
            return true;
        }

        void TickSafeArrival(AAlicePawn* pawn, const PlayerStatePayload& peer,
            bool peerOnSameMap, bool cinematic)
        {
            auto& arrival = g_safeArrival;
            if (!arrival.pawn) return;
            const auto now = Clock::now();
            if (pawn != arrival.pawn || arrival.world != g_currentWorld
                || arrival.map != g_currentMap || !peerOnSameMap || peer.actorId != arrival.peerActor
                || pawn->Health <= 0 || peer.health <= 0)
            { CancelSafeArrival("world or partner changed"); return; }
            if (arrival.arrived)
            {
                if (now >= arrival.settleUntil)
                {
                    ReleaseArrivalStreamingControl();
                    Log("TELEPORT " + arrival.source + " native streaming control restored after arrival");
                    arrival = {};
                }
                return;
            }
            if (now >= arrival.deadline)
            { CancelSafeArrival(("not ready within 16 seconds; last stage=" + arrival.lastWait).c_str()); return; }
            if (now < arrival.nextPoll) return;
            arrival.nextPoll = now + std::chrono::milliseconds(100);
            if (cinematic) { ReportArrivalWait("cinematic"); return; }
            const FVector target(peer.location[0], peer.location[1], peer.location[2]);
            if (!std::isfinite(target.X) || !std::isfinite(target.Y) || !std::isfinite(target.Z))
            { CancelSafeArrival("invalid destination"); return; }

            std::vector<ULevelStreaming*> required;
            if (g_config.role == Role::Client)
            {
                const auto host = ReadFreshHostSnapshot();
                if (!host || _stricmp(host->snapshot.hostState.mapName, arrival.map.c_str()) != 0
                    || host->snapshot.worldEpoch != arrival.hostEpoch
                    || !HasCompleteStreamingState(host->snapshot))
                { CancelSafeArrival("host streaming state incomplete"); return; }
                for (unsigned i = 0; i < host->snapshot.streamingLevelCount; ++i)
                {
                    ULevelStreaming* match = nullptr;
                    for (auto* level : arrival.world->StreamingLevels)
                        if (level && IsLiveUObject(level)
                            && HashMapName(level->PackageName.ToString()) == host->snapshot.streamingLevelHashes[i])
                        { match = level; break; }
                    if (!match) { CancelSafeArrival("host sublevel not found in client world"); return; }
                    required.push_back(match);
                }
            }
            else
            {
                static UFunction* encompasses = UFunction::FindFunction("Function Engine.Volume.EncompassesPoint");
                for (auto* level : arrival.world->StreamingLevels)
                {
                    if (!level || !IsLiveUObject(level)) continue;
                    bool needed = false;
                    for (auto* volume : level->EditorStreamingVolumes)
                    {
                        if (!volume || !IsLiveUObject(volume) || volume->bDisabled || volume->bEditorPreVisOnly) continue;
                        AVolume_execEncompassesPoint_Params p{}; p.Loc = target;
                        if (InvokeArrivalNative(volume, encompasses, p) && p.ReturnValue) { needed = true; break; }
                    }
                    if (needed) required.push_back(level);
                }
            }
            bool ready = true;
            std::string pendingLevels;
            for (auto* level : required)
            {
                auto lease = std::find_if(arrival.levels.begin(), arrival.levels.end(),
                    [&](const SafeArrival::LevelLease& item) { return item.level == level; });
                if (lease == arrival.levels.end())
                    arrival.levels.push_back({level, level->bShouldBeLoaded != 0, level->bShouldBeVisible != 0});
                if (!SetArrivalStreaming(level, true, true)) { CancelSafeArrival("native streaming request failed"); return; }
                if (!level->LoadedLevel || !level->bIsVisible || level->bHasLoadRequestPending || level->bHasUnloadRequestPending)
                {
                    ready = false;
                    if (!pendingLevels.empty()) pendingLevels += ", ";
                    pendingLevels += level->PackageName.ToString() + " [loaded="
                        + (level->LoadedLevel ? "yes" : "no") + ", visible="
                        + (level->bIsVisible ? "yes" : "no") + ", loadPending="
                        + (level->bHasLoadRequestPending ? "yes" : "no") + "]";
                }
            }
            if (!ready)
            {
                ReportArrivalWait("streaming: " + pendingLevels);
                if (now - (arrival.deadline - std::chrono::seconds(16)) >= std::chrono::seconds(1))
                    AchievementOverlay::SetCoopArrivalStatus("Preparing partner's area...", 17000);
                return;
            }

            // Match the working pre-arrival teleport: behind the partner and
            // slightly above their actual position. Neither player needs to be
            // grounded, and a moving/airborne destination does not need to settle.
            constexpr float RotatorToRadians = 6.2831853071795864769f / 65536.0f;
            const float yaw = peer.rotation[1] * RotatorToRadians;
            const FVector destination(target.X - std::cos(yaw) * 110.0f,
                target.Y - std::sin(yaw) * 110.0f, target.Z + 12.0f);
            static UFunction* farMove = UFunction::FindFunction(
                "Function AliceGame.AlicePawn.FarMoveSetLocation");
            AAlicePawn_execFarMoveSetLocation_Params move{};
            move.SetLocation = destination; move.bNoCheck = true;
            // Keep FUNC_Native/iNative intact instead of using the generated SDK
            // wrapper, which clears them. Both methods are game-owned teleports.
            InvokeNativeFunction(pawn, farMove, move);
            if ((pawn->Location - destination).SizeSquared() > 25.0f * 25.0f)
                NativeSetActorLocationNoCheck(pawn, destination);
            if ((pawn->Location - destination).SizeSquared() > 25.0f * 25.0f)
            { ReportArrivalWait("native teleport did not move pawn"); return; }
            pawn->Velocity = FVector(0, 0, 0);
            NativeSetActorRotation(pawn, FRotator(peer.rotation[0], peer.rotation[1], peer.rotation[2]));
            SetTeleportStatus(arrival.source + " success, destination=" + FormatWorldTraceVector(destination)
                + ", actual=" + FormatWorldTraceVector(pawn->Location)
                + ", streamedLevels=" + std::to_string(required.size()));
            AchievementOverlay::SetCoopArrivalStatus("", 0);
            // Let the camera consume the teleport before enabling volume tests
            // again; its old source position must not immediately unload arrival.
            arrival.arrived = true;
            arrival.settleUntil = now + std::chrono::seconds(1);
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
			g_sessionReadActive.store(false);
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
