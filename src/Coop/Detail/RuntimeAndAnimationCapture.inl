		void ResetPresentationAnimChannel(PresentationAnimChannel& channel)
		{
			channel.active = false;
			channel.standalone = false;
			channel.blendNodeIndex = -1;
			channel.targetChild = 0;
			channel.sourceChild = 0;
			channel.sequenceName.clear();
			channel.lastSourcePosition = 0.0f;
			channel.lastSourceRate = 1.0f;
			channel.lastSourceSeen = {};
		}

		std::string Timestamp()
		{
			SYSTEMTIME time{};
			GetLocalTime(&time);
			std::ostringstream stream;
			stream << std::setfill('0')
				<< std::setw(2) << time.wHour << ':'
				<< std::setw(2) << time.wMinute << ':'
				<< std::setw(2) << time.wSecond << '.'
				<< std::setw(3) << time.wMilliseconds;
			return stream.str();
		}

		void Log(const std::string& message)
		{
			std::lock_guard lock(g_logMutex);
			if (g_log)
			{
				g_log << '[' << Timestamp() << "] " << message << std::endl;
				g_log.flush();
			}
			OutputDebugStringA(("[AliceCoop] " + message + "\n").c_str());
		}

		bool IsLiveUObject(const UObject* object)
		{
			if (!object)
				return false;
			TArray<UObject*>* objects = UObject::GObjObjects();
			if (!objects)
				return false;
			for (int32_t index = 0; index < objects->size(); ++index)
			{
				if (objects->at(index) != object)
					continue;
				return (object->ObjectFlags
					& (RF_PendingKill | RF_Unreachable
						| RF_BeginDestroyed | RF_FinishDestroyed
						| RF_ZombieComponent)) == 0;
			}
			return false;
		}

		bool IsEmergencyForcedCutsceneActive()
		{
			USeqAct_Interp* action =
				g_emergencyForcedCutsceneAction;
			if (!action)
				return false;
			if (!IsLiveUObject(action))
			{
				g_emergencyForcedCutsceneAction = nullptr;
				g_emergencyForcedCutsceneRequestedAt = {};
				g_emergencyForcedCutsceneStarted = false;
				return false;
			}
			const auto now = Clock::now();
			if (!g_emergencyForcedCutsceneStarted)
			{
				if (now - g_emergencyForcedCutsceneRequestedAt
					< std::chrono::seconds(2))
				{
					return true;
				}
			}
			else if (action->bActive || action->bIsPlaying)
			{
				return true;
			}
			Log("CUTSCENEEMERGENCY forced Matinee finished action="
				+ ObjectName(action) + '.');
			g_emergencyForcedCutsceneAction = nullptr;
			g_emergencyForcedCutsceneRequestedAt = {};
			g_emergencyForcedCutsceneStarted = false;
			return false;
		}

		void ForgetTornDownWorldObjects()
		{
			// A checkpoint reload can destroy every level-owned UObject while
			// keeping the process, map name, and sometimes even WorldInfo alive.
			// Never call cleanup methods through those old pointers: just forget
			// them and let the new world build fresh presentation objects.
			g_tracedPawn = nullptr;
			g_tracedWeapon = nullptr;
			g_traceObjectLabelsReady = false;
			g_traceObjectLabels.clear();
			g_lastTracePawnState.clear();
			g_lastTraceWeaponState.clear();
			g_lastTraceBodyAnimations.clear();
			g_lastTraceUpperBodyAnimations.clear();
			g_lastTraceWeaponAnimations.clear();
			g_rangeProgressionPawn = nullptr;

			g_remotePawn = nullptr;
			g_remoteController = nullptr;
			g_remoteWorld = nullptr;
			g_retiredRemotePawns.clear();
			g_remoteHairProxy = nullptr;
			g_remoteIndependentHair = nullptr;
			g_remoteHairBindTemplate = nullptr;
			g_remoteHairRotatedTemplate = nullptr;
			g_remoteHairTuningTemplate = nullptr;
			g_remoteHairBaseNodes.clear();
			g_remoteHairNeedsReset = false;
			g_hasRemoteHairBasisCorrection = false;

			g_remotePresentationWeapon = nullptr;
			g_remoteWeaponCache.fill(nullptr);
			g_remotePresentationWeaponType =
				static_cast<std::uint8_t>(
					EAliceWeaponType::EAWT_None);
			g_remotePresentationWeaponLevel = 1;
			g_remotePresentationWeaponVariant = 0;
			g_remotePresentationWeaponSocket = FName();
			g_remoteAppliedWeaponAnimation.clear();
			g_remoteWeaponAnimationLastSeen = {};
			g_nextRemoteWeaponSpawnAttempt = {};
			g_remoteWeaponBaseDrawScale = 1.0f;
			g_remoteMuzzleFlashActive = false;
			g_remoteMuzzleLastActiveRequest = {};
			g_remoteMuzzleParticle = nullptr;
			g_remoteAttackTrailActive = false;
			g_remoteAttackTrailUntil = {};
			g_remoteAttackTrailParticle = nullptr;
			g_remoteWeaponLoopParticles.clear();
			g_remoteWeaponTransientParticles.clear();
			g_retiredPresentationParticles.clear();
			g_remoteMovementTransientParticles.clear();
			g_pendingMovementParticlePreservations.clear();
			g_remoteMovementParticleCapture = {};
			g_vfxLifecycleAudits.clear();
			g_lastLoggedLocalWeapon = nullptr;

			g_remoteGlideVfxActive = false;
			g_remoteAirResetPending = false;
			g_remoteGlideParticle = nullptr;
			g_remoteGlideInactiveSince = {};
			g_remoteNativeGlideTrails.clear();
			g_remoteNativeGlideCurrent = nullptr;
			g_remoteNativeGlideCurrentSince = {};
			g_nextRemoteNativeGlideSweep = {};
			g_remoteLeafTrailMeshes.clear();
			g_remoteFrozenLeafAssets.clear();
			g_remoteFrozenAccentAssets.clear();
			g_remoteLeafTrailMarkers.clear();
			g_remoteAccentTrailMarkers.clear();
			g_nextRemoteLeafTrailMarker = {};
			g_remoteLeafTrailSample = 0;
			g_remoteDodgeVisualHidden = false;
			g_remoteRenderDetached = false;
			g_remoteDodgeParticleTemplate = nullptr;
			g_remoteDodgeParticleUntil = {};
			g_nextRemoteDodgeParticle = {};
			g_remoteClockBombAnimationActive = false;
			g_remoteHidden = false;
			g_remoteShrinkApplied = false;
			g_remotePresentation = {};
			g_activeRemoteAnimationGraph.reset();
			g_remoteGraphSequences.clear();
			g_remoteAppliedFullBodyNode = nullptr;
			g_remoteAppliedFullBodySlot = nullptr;
			g_remoteAppliedFullBodyName.clear();
			g_lastLocalAnimationCompareSignature.clear();
			g_lastRemoteAnimationCompareSignature.clear();
			g_nextLocalAnimationCompareSample = {};
			g_nextRemoteAnimationCompareSample = {};
			ResetPresentationAnimChannel(g_remoteFullBodyChannel);
			ResetPresentationAnimChannel(g_remoteUpperAdditiveChannel);
			g_remoteFullBodyNotifyCursor = {};
			g_recentRemoteVisualNotifies.clear();

			g_localClockBomb = {};
			g_recentLocalPepperProjectiles.clear();
			g_remotePepperVisuals.clear();
			g_remoteClockBombVisuals.clear();
			g_pepperFlightTemplate = nullptr;
			g_pepperMarkerMesh = nullptr;
			g_clockBombExplosionTemplate = nullptr;
			g_clockBombMeshTemplate = nullptr;
		}

		void InitializeSharedDevState()
		{
			if (g_sharedDevState)
				return;
			g_sharedDevMapping = CreateFileMappingW(
				INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
				0, sizeof(SharedDevState),
				L"Local\\AliceCoopPresentationDevV1");
			if (!g_sharedDevMapping)
			{
				Log("DEVSTAGE shared settings mapping failed.");
				return;
			}
			const bool created = GetLastError() != ERROR_ALREADY_EXISTS;
			g_sharedDevState = static_cast<SharedDevState*>(
				MapViewOfFile(g_sharedDevMapping,
					FILE_MAP_ALL_ACCESS, 0, 0,
					sizeof(SharedDevState)));
			if (!g_sharedDevState)
			{
				CloseHandle(g_sharedDevMapping);
				g_sharedDevMapping = nullptr;
				Log("DEVSTAGE shared settings view failed.");
				return;
			}
			if (created
				|| g_sharedDevState->magic != SharedDevStateMagic
				|| g_sharedDevState->version != SharedDevStateVersion)
			{
				g_sharedDevState->magic = SharedDevStateMagic;
				g_sharedDevState->version = SharedDevStateVersion;
				InterlockedExchange(
					&g_sharedDevState->vfxAttachmentCandidate,
					static_cast<LONG>(g_vfxAttachmentCandidate));
				InterlockedExchange(
					&g_sharedDevState->revision, 1);
				InterlockedExchange(
					&g_sharedDevState->command, 0);
				InterlockedExchange(
					&g_sharedDevState->commandRevision, 0);
			}
			g_sharedDevRevision = InterlockedCompareExchange(
				&g_sharedDevState->revision, 0, 0);
			const LONG candidate = InterlockedCompareExchange(
				&g_sharedDevState->vfxAttachmentCandidate, 0, 0);
			if (candidate >= 0
				&& candidate < static_cast<LONG>(
					VfxAttachmentCandidate::Count))
			{
				g_vfxAttachmentCandidate =
					static_cast<VfxAttachmentCandidate>(candidate);
			}
			g_sharedDevCommandRevision = InterlockedCompareExchange(
				&g_sharedDevState->commandRevision, 0, 0);
			Log("DEVSTAGE shared settings ready, revision="
				+ std::to_string(g_sharedDevRevision)
				+ ", candidate="
				+ std::to_string(static_cast<int>(
					g_vfxAttachmentCandidate)) + '.');
		}

		void PublishSharedVfxAttachmentCandidate()
		{
			if (!g_sharedDevState)
				return;
			InterlockedExchange(
				&g_sharedDevState->vfxAttachmentCandidate,
				static_cast<LONG>(g_vfxAttachmentCandidate));
			g_sharedDevRevision = InterlockedIncrement(
				&g_sharedDevState->revision);
		}

		void PublishSharedDevCommand(int command)
		{
			if (!g_sharedDevState || command <= 0)
				return;
			InterlockedExchange(
				&g_sharedDevState->command, command);
			g_sharedDevCommandRevision = InterlockedIncrement(
				&g_sharedDevState->commandRevision);
		}

		void PollSharedDevState()
		{
			if (!g_sharedDevState)
				return;
			const LONG revision = InterlockedCompareExchange(
				&g_sharedDevState->revision, 0, 0);
			if (revision != g_sharedDevRevision)
			{
				g_sharedDevRevision = revision;
				const LONG candidate = InterlockedCompareExchange(
					&g_sharedDevState->vfxAttachmentCandidate, 0, 0);
				if (candidate >= 0
					&& candidate < static_cast<LONG>(
						VfxAttachmentCandidate::Count))
				{
					g_vfxAttachmentCandidate =
						static_cast<VfxAttachmentCandidate>(candidate);
					Log("DEVSTAGE synchronized VFX candidate="
						+ std::to_string(candidate)
						+ ", revision="
						+ std::to_string(revision) + '.');
				}
			}

			const LONG commandRevision = InterlockedCompareExchange(
				&g_sharedDevState->commandRevision, 0, 0);
			if (commandRevision == g_sharedDevCommandRevision)
				return;
			g_sharedDevCommandRevision = commandRevision;
			const LONG command = InterlockedCompareExchange(
				&g_sharedDevState->command, 0, 0);
			if (command <= 0)
				return;
			Log("DEVSTAGE received shared command="
				+ std::to_string(command)
				+ ", revision="
				+ std::to_string(commandRevision) + '.');
			g_applyingSharedDevCommand = true;
			ExecuteDevCommand(static_cast<int>(command));
			g_applyingSharedDevCommand = false;
		}

		std::wstring ReadEnvironment(const wchar_t* name)
		{
			const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
			if (required == 0)
				return {};
			std::wstring value(required, L'\0');
			GetEnvironmentVariableW(name, value.data(), required);
			if (!value.empty() && value.back() == L'\0')
				value.pop_back();
			return value;
		}

		bool IsTruthy(const std::wstring& value)
		{
			return _wcsicmp(value.c_str(), L"1") == 0
				|| _wcsicmp(value.c_str(), L"true") == 0
				|| _wcsicmp(value.c_str(), L"yes") == 0;
		}

		std::string NarrowAscii(const std::wstring& value)
		{
			std::string result;
			result.reserve(value.size());
			for (const wchar_t character : value)
				result.push_back(character <= 0x7f ? static_cast<char>(character) : '?');
			return result;
		}

		int ReadIniInt(const std::filesystem::path& path, const wchar_t* section,
			const wchar_t* key, int defaultValue)
		{
			return GetPrivateProfileIntW(section, key, defaultValue, path.c_str());
		}

		std::wstring ReadIniString(const std::filesystem::path& path, const wchar_t* section,
			const wchar_t* key, const wchar_t* defaultValue)
		{
			std::array<wchar_t, 256> value{};
			GetPrivateProfileStringW(section, key, defaultValue, value.data(),
				static_cast<DWORD>(value.size()), path.c_str());
			return value.data();
		}

		float ReadIniFloat(const std::filesystem::path& path,
			const wchar_t* section, const wchar_t* key,
			float defaultValue)
		{
			std::wostringstream fallback;
			fallback << defaultValue;
			const std::wstring value = ReadIniString(
				path, section, key, fallback.str().c_str());
			wchar_t* end = nullptr;
			const float parsed = std::wcstof(value.c_str(), &end);
			return end && end != value.c_str()
				? parsed : defaultValue;
		}

		Role ParseRole(const std::wstring& role)
		{
			if (_wcsicmp(role.c_str(), L"host") == 0)
				return Role::Host;
			if (_wcsicmp(role.c_str(), L"client") == 0)
				return Role::Client;
			return Role::Unknown;
		}

		int EnvironmentInt(const wchar_t* name, int fallback)
		{
			const std::wstring value = ReadEnvironment(name);
			return value.empty() ? fallback : _wtoi(value.c_str());
		}

		void ReadConfig()
		{
			const std::filesystem::path moduleDirectory = SystemHelper::GetModulePath();
			const std::filesystem::path iniPath = moduleDirectory / L"AliceCoop" / L"AliceCoop.ini";

			const std::wstring enabledEnvironment = ReadEnvironment(L"ALICECOOP_ENABLE");
			g_config.enabled = IsTruthy(enabledEnvironment)
				|| ReadIniInt(iniPath, L"Network", L"EnableWithoutLauncher", 0) == 1;
			g_config.role = ParseRole(ReadEnvironment(L"ALICECOOP_ROLE"));
			if (g_config.role == Role::Unknown)
				g_config.role = ParseRole(ReadIniString(iniPath, L"Network", L"Role", L""));

			std::wstring server = ReadEnvironment(L"ALICECOOP_SERVER");
			if (server.empty())
				server = ReadIniString(iniPath, L"Network", L"ServerAddress", L"127.0.0.1");
			g_config.serverAddress = NarrowAscii(server);
			g_config.port = static_cast<std::uint16_t>(EnvironmentInt(L"ALICECOOP_PORT",
				ReadIniInt(iniPath, L"Network", L"Port", DefaultPort)));
			g_config.sendRateHz = std::clamp(ReadIniInt(iniPath, L"Network", L"SendRateHz", 20), 5, 60);
			g_config.peerTimeoutMs = std::clamp(ReadIniInt(iniPath, L"Network", L"PeerTimeoutMs", 3000), 1000, 15000);
			g_config.showOnScreenStatus = ReadIniInt(iniPath, L"Debug", L"OnScreenStatus", 0) == 1;
			g_config.visualProxy = ReadIniInt(iniPath, L"VisualProxy", L"Enabled", 1) == 1;
			g_config.localMirror = EnvironmentInt(L"ALICECOOP_LOCAL_MIRROR",
				ReadIniInt(iniPath, L"VisualProxy", L"LocalMirror", 0)) == 1;
			g_config.actionTrace = EnvironmentInt(L"ALICECOOP_ACTION_TRACE",
				ReadIniInt(iniPath, L"Trace", L"Enabled", 0)) == 1;
			g_config.worldTrace = EnvironmentInt(L"ALICECOOP_WORLD_TRACE",
				ReadIniInt(iniPath, L"Trace", L"WorldEnabled", 0)) == 1;
			g_config.vfxLifecycleTrace = EnvironmentInt(
				L"ALICECOOP_VFX_TRACE",
				ReadIniInt(iniPath, L"Trace", L"VfxLifecycleEnabled", 0)) == 1;
			g_config.animationLifecycleTrace = EnvironmentInt(
				L"ALICECOOP_ANIMATION_TRACE",
				ReadIniInt(iniPath, L"Trace", L"AnimationLifecycleEnabled", 0)) == 1;
			g_config.animationComparisonTrace = EnvironmentInt(
				L"ALICECOOP_ANIMATION_COMPARE_TRACE",
				ReadIniInt(iniPath, L"Trace", L"AnimationComparisonEnabled", 0)) == 1;
			g_config.controlLifecycleTrace = EnvironmentInt(
				L"ALICECOOP_CONTROL_TRACE",
				ReadIniInt(iniPath, L"Trace", L"ControlLifecycleEnabled", 0)) == 1;
			g_config.invariantTrace = EnvironmentInt(
				L"ALICECOOP_INVARIANT_TRACE",
				ReadIniInt(iniPath, L"Trace", L"InvariantEnabled", 0)) == 1;
			g_config.processEventBridgeTrace = EnvironmentInt(
				L"ALICECOOP_PROCESS_EVENT_BRIDGE_TRACE",
				ReadIniInt(iniPath, L"Trace",
					L"ProcessEventBridgeEnabled", 0)) == 1;
			g_config.preserveMovementTrails = ReadIniInt(
				iniPath, L"VisualProxy", L"PreserveMovementTrails", 0) == 1;
			g_config.sharedEnemyHealth =
				EnvironmentInt(L"ALICECOOP_SHARED_ENEMY_HEALTH",
					ReadIniInt(iniPath, L"SharedWorld",
						L"EnemyHealth", 1)) == 1;
			g_config.sharedEnemyTransforms =
				EnvironmentInt(L"ALICECOOP_SHARED_ENEMY_TRANSFORMS",
					ReadIniInt(iniPath, L"SharedWorld",
						L"EnemyTransforms", 1)) == 1;
			g_config.backgroundWindowDamageGuard =
				EnvironmentInt(L"ALICECOOP_BACKGROUND_GUARD",
					ReadIniInt(iniPath, L"SharedWorld",
						L"BackgroundWindowDamageGuard", 0)) == 1;
			g_config.sharedEnemyRadius =
				static_cast<float>(std::clamp(
					ReadIniInt(iniPath, L"SharedWorld",
						L"EnemyPositionRadius", 12000),
					1000, 50000));
			g_config.sharedEnemyCorrectionSpeed = std::clamp(
				ReadIniFloat(iniPath, L"SharedWorld",
					L"EnemyCorrectionSpeed", 22.0f), 1.0f, 60.0f);
			g_config.sharedEnemySnapDistance = std::clamp(
				ReadIniFloat(iniPath, L"SharedWorld",
					L"EnemySnapDistance", 1200.0f), 100.0f, 10000.0f);
			g_config.hairTuningEnabled = ReadIniInt(
				iniPath, L"VisualProxy", L"HairTuningEnabled", 0) == 1;
			g_config.actionTraceWindowMs = std::clamp(
				ReadIniInt(iniPath, L"Trace", L"EventWindowMs", 2500),
				500, 10000);
			g_config.worldTraceRadius = static_cast<float>(std::clamp(
				ReadIniInt(iniPath, L"Trace", L"WorldRadius", 10000),
				1000, 50000));
			if (g_config.actionTrace)
			{
				g_config.localMirror = false;
				g_config.sharedEnemyHealth = false;
				g_config.sharedEnemyTransforms = false;
			}
			g_config.disableProxyCollision = ReadIniInt(iniPath, L"VisualProxy", L"DisableCollision", 1) == 1;
			g_config.interpolationSpeed = static_cast<float>(ReadIniInt(
				iniPath, L"VisualProxy", L"InterpolationSpeed", 18));
			g_config.localMirrorDistance = static_cast<float>(std::clamp(
				EnvironmentInt(L"ALICECOOP_MIRROR_DISTANCE",
					ReadIniInt(iniPath, L"VisualProxy", L"MirrorDistance", 160)),
				40, 500));
			g_config.hairRotationX = static_cast<float>(std::clamp(
				ReadIniInt(iniPath, L"VisualProxy", L"HairRotationX", 180),
				-360, 360));
			g_config.hairRotationY = static_cast<float>(std::clamp(
				ReadIniInt(iniPath, L"VisualProxy", L"HairRotationY", 265),
				-360, 360));
			g_config.hairRotationZ = static_cast<float>(std::clamp(
				ReadIniInt(iniPath, L"VisualProxy", L"HairRotationZ", 0),
				-360, 360));
			g_config.hairOffsetX = std::clamp(ReadIniFloat(
				iniPath, L"VisualProxy", L"HairOffsetX", -0.2f),
				-100.0f, 100.0f);
			g_config.hairOffsetY = std::clamp(ReadIniFloat(
				iniPath, L"VisualProxy", L"HairOffsetY", 0.0f),
				-100.0f, 100.0f);
			g_config.hairOffsetZ = std::clamp(ReadIniFloat(
				iniPath, L"VisualProxy", L"HairOffsetZ", -2.2f),
				-100.0f, 100.0f);
			const int hairTarget = std::clamp(ReadIniInt(
				iniPath, L"VisualProxy", L"HairRotationTarget", 3),
				1, static_cast<int>(HairRotationCandidate::Count));
			g_hairRotationCandidate =
				static_cast<HairRotationCandidate>(hairTarget - 1);
			g_config.forceWindowed = EnvironmentInt(L"ALICECOOP_FORCE_WINDOWED",
				ReadIniInt(iniPath, L"Window", L"ForceWindowed", 0)) == 1;
			g_config.manageWindowGeometry = EnvironmentInt(L"ALICECOOP_MANAGE_WINDOW",
				ReadIniInt(iniPath, L"Window", L"ManageGeometry", 0)) == 1;
			g_config.borderlessWindow = EnvironmentInt(L"ALICECOOP_BORDERLESS", 0) == 1;
			g_config.windowX = EnvironmentInt(L"ALICECOOP_WINDOW_X",
				ReadIniInt(iniPath, L"Window", L"X", 0));
			g_config.windowY = EnvironmentInt(L"ALICECOOP_WINDOW_Y",
				ReadIniInt(iniPath, L"Window", L"Y", 0));
			g_config.windowWidth = EnvironmentInt(L"ALICECOOP_WINDOW_WIDTH",
				ReadIniInt(iniPath, L"Window", L"Width", 960));
			g_config.windowHeight = EnvironmentInt(L"ALICECOOP_WINDOW_HEIGHT",
				ReadIniInt(iniPath, L"Window", L"Height", 540));
			g_config.lowMemoryMode = ReadIniInt(iniPath, L"Performance", L"LowMemoryMode", 1) == 1;
			g_config.maxFps = std::clamp(
				ReadIniInt(iniPath, L"Performance", L"MaxFPS", 60), 30, 120);
			g_config.maxPoolThreads = std::clamp(
				ReadIniInt(iniPath, L"Performance", L"MaxPoolThreads", 4), 1, 16);

			if ((g_config.localMirror || g_config.actionTrace)
				&& g_config.role == Role::Unknown)
				g_config.role = Role::Host;
			if (g_config.role == Role::Unknown)
				g_config.enabled = false;
		}

		void PersistMovementTrailsSetting(bool enabled)
		{
			const std::filesystem::path iniPath =
				std::filesystem::path(SystemHelper::GetModulePath())
				/ L"AliceCoop" / L"AliceCoop.ini";
			WritePrivateProfileStringW(
				L"VisualProxy", L"PreserveMovementTrails",
				enabled ? L"1" : L"0", iniPath.c_str());
		}

		BOOL CALLBACK ConfigureWindowCallback(HWND window, LPARAM)
		{
			DWORD processId = 0;
			GetWindowThreadProcessId(window, &processId);
			if (processId != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER))
				return TRUE;

			const wchar_t* role = g_config.role == Role::Host ? L"HOST" : L"CLIENT";
			std::wstring title = L"AliceCoop [" + std::wstring(role) + L"]";
			SetWindowTextW(window, title.c_str());

			int x = g_config.windowX;
			int y = g_config.windowY;
			int width = g_config.windowWidth;
			int height = g_config.windowHeight;
			UINT flags = SWP_NOACTIVATE | SWP_NOZORDER;

			if (g_config.borderlessWindow)
			{
				const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
				const LONG_PTR borderlessStyle =
					(style & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) | WS_POPUP;
				const LONG_PTR extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
				const LONG_PTR borderlessExtendedStyle = extendedStyle
					& ~static_cast<LONG_PTR>(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
				if (style != borderlessStyle)
					SetWindowLongPtrW(window, GWL_STYLE, borderlessStyle);
				if (extendedStyle != borderlessExtendedStyle)
					SetWindowLongPtrW(window, GWL_EXSTYLE, borderlessExtendedStyle);
				if (style != borderlessStyle || extendedStyle != borderlessExtendedStyle)
					flags |= SWP_FRAMECHANGED;

				if (width <= 0 || height <= 0)
				{
					MONITORINFO monitorInfo{};
					monitorInfo.cbSize = sizeof(monitorInfo);
					if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST),
						&monitorInfo))
					{
						x = monitorInfo.rcMonitor.left;
						y = monitorInfo.rcMonitor.top;
						width = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
						height = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
					}
				}
			}

			if (width > 0 && height > 0)
			{
				SetWindowPos(window, nullptr, x, y, width, height, flags);
			}
			return FALSE;
		}

		void ConfigureGameWindow()
		{
			const auto now = Clock::now();
			if (now < g_nextWindowConfigure)
				return;
			g_nextWindowConfigure = now + std::chrono::seconds(2);
			EnumWindows(ConfigureWindowCallback, 0);
		}

		std::uint64_t ElapsedMilliseconds()
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				Clock::now().time_since_epoch()).count());
		}

		bool ContainsCaseInsensitive(const std::string& value, const char* needle)
		{
			if (!needle || !*needle)
				return true;
			std::string loweredValue = value;
			std::string loweredNeedle = needle;
			std::transform(loweredValue.begin(), loweredValue.end(),
				loweredValue.begin(), [](unsigned char character)
				{
					return static_cast<char>(std::tolower(character));
				});
			std::transform(loweredNeedle.begin(), loweredNeedle.end(),
				loweredNeedle.begin(), [](unsigned char character)
				{
					return static_cast<char>(std::tolower(character));
				});
			return loweredValue.find(loweredNeedle) != std::string::npos;
		}

		bool IsActionAnimationName(const std::string& name)
		{
			static constexpr const char* Keywords[] = {
				"attack", "mele", "dodge", "jump", "float", "glide",
				"shrink", "fire", "release", "damage", "knockback",
				"umbrella", "fly", "land",
			};
			for (const char* keyword : Keywords)
			{
				if (ContainsCaseInsensitive(name, keyword))
					return true;
			}
			return false;
		}

		bool IsCustomAnimationBranch(const UAnimNode* node)
		{
			if (!node)
				return false;
			const int32_t parentCount =
				std::min<int32_t>(node->ParentNodes.size(), 32);
			for (int32_t index = 0; index < parentCount; ++index)
			{
				UAnimNodeBlendBase* parent = node->ParentNodes.at(index);
				if (!parent)
					continue;
				if (parent->IsA(UAliceGameAnimNode_BlendBase::StaticClass())
					&& reinterpret_cast<UAliceGameAnimNode_BlendBase*>(parent)
						->bIsPlayingCustomAnim)
				{
					return true;
				}
				if (parent->IsA(UAnimNodeSlot::StaticClass())
					&& reinterpret_cast<UAnimNodeSlot*>(parent)
						->bIsPlayingCustomAnim)
				{
					return true;
				}
			}
			return false;
		}

		struct AnimationSequenceCandidate
		{
			AnimationSequencePayload payload{};
			float priority = 0.0f;
		};

		struct AnimationBlendCandidate
		{
			AnimationBlendPayload payload{};
			float priority = 0.0f;
		};

		void CaptureAnimationComponent(
			const USkeletalMeshComponent* component,
			AnimationComponent componentId,
			std::vector<AnimationSequenceCandidate>& sequences,
			std::vector<AnimationBlendCandidate>& blends)
		{
			if (!component)
				return;

			const int32_t nodeCount =
				std::min<int32_t>(component->AnimTickArray.size(), 65535);
			for (int32_t index = 0; index < nodeCount; ++index)
			{
				UAnimNode* node = component->AnimTickArray.at(index);
				if (!node)
					continue;
				const bool relevant = node->bRelevant
					|| node->bJustBecameRelevant
					|| node->NodeTotalWeight > 0.001f;

				if (node->IsA(UAnimNodeSequence::StaticClass()))
				{
					auto* sequence = reinterpret_cast<UAnimNodeSequence*>(node);
					if (!relevant || !sequence->bPlaying
						|| !sequence->AnimSeqName.IsValid())
					{
						continue;
					}

					const std::string sequenceName =
						sequence->AnimSeqName.ToString();
					const bool custom = IsCustomAnimationBranch(node);
					const bool action = IsActionAnimationName(sequenceName);
					AnimationSequenceCandidate candidate{};
					strncpy_s(candidate.payload.sequenceName,
						sequenceName.c_str(), _TRUNCATE);
					candidate.payload.position =
						std::isfinite(sequence->CurrentTime)
						? (sequence->CurrentTime > 0.0f
							? sequence->CurrentTime : 0.0f)
						: 0.0f;
					candidate.payload.rate =
						std::isfinite(sequence->Rate)
						&& std::fabs(sequence->Rate) > 0.001f
						? sequence->Rate
						: 1.0f;
					candidate.payload.nodeIndex =
						static_cast<std::uint16_t>(index);
					candidate.payload.component = componentId;
					candidate.payload.nodeWeight =
						std::isfinite(node->NodeTotalWeight)
						? std::clamp(node->NodeTotalWeight, 0.0f, 1.0f)
						: 0.0f;
					if (sequence->bLooping)
						candidate.payload.flags |= AnimationSequenceLooping;
					if (relevant)
						candidate.payload.flags |= AnimationSequenceRelevant;
					if (custom)
						candidate.payload.flags |= AnimationSequenceCustom;
					if (action)
						candidate.payload.flags |= AnimationSequenceAction;
					candidate.priority =
						(custom ? 1200.0f : 0.0f)
						+ (action ? 800.0f : 0.0f)
						+ (!sequence->bLooping ? 250.0f : 0.0f)
						+ (componentId == AnimationComponent::Weapon
							? 400.0f : 0.0f)
						+ candidate.payload.nodeWeight * 100.0f;
					sequences.push_back(candidate);
					continue;
				}

				std::int16_t activeChild = -1;
				bool custom = false;
				if (node->IsA(UAliceGameAnimNode_BlendList::StaticClass()))
				{
					auto* blend =
						reinterpret_cast<UAliceGameAnimNode_BlendList*>(node);
					activeChild = static_cast<std::int16_t>(std::clamp(
						blend->ActiveChildIndex, -1, 32767));
					custom = blend->bIsPlayingCustomAnim;
				}
				else if (node->IsA(UAnimNodeBlendList::StaticClass()))
				{
					auto* blend = reinterpret_cast<UAnimNodeBlendList*>(node);
					activeChild = static_cast<std::int16_t>(std::clamp(
						blend->ActiveChildIndex, -1, 32767));
				}
				else
				{
					continue;
				}

				if (!relevant && !custom)
					continue;
				const std::string nodeName = node->NodeName.IsValid()
					? node->NodeName.ToString()
					: std::string();
				AnimationBlendCandidate candidate{};
				candidate.payload.nodeIndex =
					static_cast<std::uint16_t>(index);
				candidate.payload.activeChildIndex = activeChild;
				candidate.payload.component = componentId;
				candidate.payload.nodeWeight =
					std::isfinite(node->NodeTotalWeight)
					? std::clamp(node->NodeTotalWeight, 0.0f, 1.0f)
					: 0.0f;
				if (relevant)
					candidate.payload.flags |= AnimationBlendRelevant;
				if (custom)
					candidate.payload.flags |= AnimationBlendCustom;
				candidate.priority =
					(custom ? 1400.0f : 0.0f)
					+ (ContainsCaseInsensitive(nodeName, "slot")
						? 700.0f : 0.0f)
					+ (componentId == AnimationComponent::Weapon
						? 450.0f : 0.0f)
					+ candidate.payload.nodeWeight * 100.0f;
				blends.push_back(candidate);
			}
		}

		AnimationGraphPayload CaptureAnimationGraph(
			AAlicePawn* pawn, std::uint32_t actorId)
		{
			AnimationGraphPayload graph{};
			graph.frameNumber = ++g_animationGraphFrameNumber;
			graph.mapHash = HashMapName(g_currentMap);
			graph.actorId = actorId;
			graph.clientTimeMs = ElapsedMilliseconds();

			std::vector<AnimationSequenceCandidate> sequences;
			std::vector<AnimationBlendCandidate> blends;
			sequences.reserve(32);
			blends.reserve(48);
			if (pawn)
			{
				CaptureAnimationComponent(pawn->Mesh,
					AnimationComponent::Body, sequences, blends);
				CaptureAnimationComponent(pawn->UpperBodyComponent,
					AnimationComponent::UpperBody, sequences, blends);
				CaptureAnimationComponent(pawn->Weapon
						? pawn->Weapon->Mesh : nullptr,
					AnimationComponent::Weapon, sequences, blends);
			}

			std::stable_sort(sequences.begin(), sequences.end(),
				[](const AnimationSequenceCandidate& left,
					const AnimationSequenceCandidate& right)
				{
					return left.priority > right.priority;
				});
			std::stable_sort(blends.begin(), blends.end(),
				[](const AnimationBlendCandidate& left,
					const AnimationBlendCandidate& right)
				{
					return left.priority > right.priority;
				});

			graph.sequenceCount = static_cast<std::uint8_t>(
				std::min<std::size_t>(sequences.size(),
					MaxAnimationSequences));
			for (std::uint8_t index = 0;
				index < graph.sequenceCount; ++index)
			{
				graph.sequences[index] = sequences[index].payload;
			}
			graph.blendCount = static_cast<std::uint8_t>(
				std::min<std::size_t>(blends.size(), MaxAnimationBlends));
			for (std::uint8_t index = 0;
				index < graph.blendCount; ++index)
			{
				graph.blends[index] = blends[index].payload;
			}
			return graph;
		}

		struct AnimationComparisonSummary
		{
			std::string signature;
			std::string details;
			bool timingSensitive = false;
		};

		const char* AnimationComponentLabel(AnimationComponent component)
		{
			switch (component)
			{
			case AnimationComponent::Body:
				return "body";
			case AnimationComponent::UpperBody:
				return "upper";
			case AnimationComponent::Weapon:
				return "weapon";
			default:
				return "unknown";
			}
		}

		AnimationComparisonSummary BuildAnimationComparisonSummary(
			const AnimationGraphPayload& graph)
		{
			AnimationComparisonSummary result{};
			std::ostringstream signature;
			std::ostringstream details;
			details << std::fixed << std::setprecision(3);
			bool wroteDetails = false;
			const std::size_t count = std::min<std::size_t>(
				graph.sequenceCount, MaxAnimationSequences);
			for (std::size_t index = 0; index < count; ++index)
			{
				const AnimationSequencePayload& sequence =
					graph.sequences[index];
				if (!sequence.sequenceName[0])
					continue;
				const std::string name(sequence.sequenceName);
				signature << static_cast<int>(sequence.component) << ':'
					<< sequence.nodeIndex << ':' << name << ':'
					<< static_cast<int>(sequence.flags) << '|';
				if (wroteDetails)
					details << ';';
				details << AnimationComponentLabel(sequence.component)
					<< '[' << sequence.nodeIndex << "]=" << name
					<< "@" << sequence.position
					<< "x" << sequence.rate
					<< "w" << sequence.nodeWeight
					<< "f" << static_cast<int>(sequence.flags);
				wroteDetails = true;
				result.timingSensitive = result.timingSensitive
					|| (sequence.flags & (AnimationSequenceAction
						| AnimationSequenceCustom)) != 0
					|| IsActionAnimationName(name);
			}
			result.signature = signature.str();
			result.details = details.str();
			if (result.signature.empty())
			{
				result.signature = "<none>";
				result.details = "<none>";
			}
			return result;
		}

		void TraceLocalAnimationComparison(
			const AnimationGraphPayload& graph)
		{
			if (!g_config.animationComparisonTrace)
				return;
			const Clock::time_point now = Clock::now();
			const AnimationComparisonSummary summary =
				BuildAnimationComparisonSummary(graph);
			const bool changed = summary.signature
				!= g_lastLocalAnimationCompareSignature;
			const bool sample = summary.timingSensitive
				&& now >= g_nextLocalAnimationCompareSample;
			if (!changed && !sample)
				return;
			g_lastLocalAnimationCompareSignature = summary.signature;
			g_nextLocalAnimationCompareSample =
				now + std::chrono::milliseconds(100);
			Log("ANIMCOMPARE LOCAL frame="
				+ std::to_string(graph.frameNumber)
				+ ", sourceMs=" + std::to_string(graph.clientTimeMs)
				+ ", timing=" + (summary.timingSensitive ? "1" : "0")
				+ ", sequences=" + summary.details + '.');
		}

		bool FindPresentedAnimationTime(USkeletalMeshComponent* component,
			const std::string& name, float& position, float& rate)
		{
			if (!component || name.empty())
				return false;
			for (int32_t index = 0; index < std::min<int32_t>(
				component->AnimTickArray.size(), 1024); ++index)
			{
				UAnimNode* node = component->AnimTickArray.at(index);
				if (!node || !node->IsA(UAnimNodeSequence::StaticClass()))
					continue;
				auto* sequence = reinterpret_cast<UAnimNodeSequence*>(node);
				if (!sequence->bPlaying || !sequence->AnimSeqName.IsValid()
					|| _stricmp(sequence->AnimSeqName.ToString().c_str(),
						name.c_str()) != 0)
				{
					continue;
				}
				position = sequence->CurrentTime;
				rate = sequence->Rate;
				return true;
			}
			return false;
		}

		void TraceRemoteAnimationComparison(AAlicePawn* remote,
			const ReceivedAnimationGraph& received)
		{
			if (!g_config.animationComparisonTrace || !remote)
				return;
			const Clock::time_point now = Clock::now();
			const AnimationComparisonSummary summary =
				BuildAnimationComparisonSummary(received.graph);
			const bool changed = summary.signature
				!= g_lastRemoteAnimationCompareSignature;
			const bool sample = summary.timingSensitive
				&& now >= g_nextRemoteAnimationCompareSample;
			if (!changed && !sample)
				return;
			g_lastRemoteAnimationCompareSignature = summary.signature;
			g_nextRemoteAnimationCompareSample =
				now + std::chrono::milliseconds(100);

			float fullPosition = 0.0f;
			float fullRate = 0.0f;
			const bool fullPlaying = g_remoteAppliedFullBodyNode
				&& IsLiveUObject(g_remoteAppliedFullBodyNode)
				&& g_remoteAppliedFullBodyNode->bPlaying;
			if (fullPlaying)
			{
				fullPosition = g_remoteAppliedFullBodyNode->CurrentTime;
				fullRate = g_remoteAppliedFullBodyNode->Rate;
			}
			float weaponPosition = 0.0f;
			float weaponRate = 0.0f;
			float upperPosition = 0.0f;
			float upperRate = 0.0f;
			const bool upperPlaying =
				g_remoteUpperAdditiveChannel.active
				&& FindPresentedAnimationTime(remote->Mesh,
					g_remoteUpperAdditiveChannel.sequenceName,
					upperPosition, upperRate);
			const bool weaponPlaying = remote->Weapon
				&& FindPresentedAnimationTime(remote->Weapon->Mesh,
					g_remoteAppliedWeaponAnimation,
					weaponPosition, weaponRate);
			const auto receiveAge = std::chrono::duration_cast<
				std::chrono::milliseconds>(now - received.receivedAt).count();
			std::ostringstream applied;
			applied << std::fixed << std::setprecision(3)
				<< "full="
				<< (g_remoteAppliedFullBodyName.empty()
					? "<none>" : g_remoteAppliedFullBodyName)
				<< '@' << (fullPlaying ? fullPosition : -1.0f)
				<< 'x' << (fullPlaying ? fullRate : 0.0f)
				<< ", upper="
				<< (g_remoteUpperAdditiveChannel.active
					? g_remoteUpperAdditiveChannel.sequenceName : "<none>")
				<< '@' << (upperPlaying ? upperPosition : -1.0f)
				<< 'x' << (upperPlaying ? upperRate : 0.0f)
				<< ", weapon="
				<< (g_remoteAppliedWeaponAnimation.empty()
					? "<none>" : g_remoteAppliedWeaponAnimation)
				<< '@' << (weaponPlaying ? weaponPosition : -1.0f)
				<< 'x' << (weaponPlaying ? weaponRate : 0.0f);
			Log("ANIMCOMPARE REMOTE frame="
				+ std::to_string(received.graph.frameNumber)
				+ ", sourceMs="
				+ std::to_string(received.graph.clientTimeMs)
				+ ", receiveAgeMs=" + std::to_string(receiveAge)
				+ ", timing=" + (summary.timingSensitive ? "1" : "0")
				+ ", source=" + summary.details
				+ ", applied=" + applied.str() + '.');
		}

