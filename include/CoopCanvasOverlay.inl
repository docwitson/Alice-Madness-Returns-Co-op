// Included inside AchievementOverlay after the native Canvas drawing helpers.
// Preserve the co-op request/state API while rendering through the game viewport.
inline std::mutex g_coopArrivalMutex;
inline std::string g_coopArrivalStatus;
inline std::atomic<unsigned long long> g_coopArrivalUntil{0};
inline void SetCoopArrivalStatus(const std::string& status, unsigned int milliseconds)
{
    std::lock_guard<std::mutex> lock(g_coopArrivalMutex);
    g_coopArrivalStatus = status; g_coopArrivalUntil = GetTickCount64() + milliseconds;
}
inline bool g_coopDevMode = false;
inline bool g_nativeMenuNotice = false, g_nativeFrameNotice = false;
inline std::atomic<unsigned int> g_nativeMusicResult{0};
inline bool g_nativeViewportNotice = false;
inline unsigned int ConsumeNativeMusicResult() { return g_nativeMusicResult.exchange(0); }
inline bool ConsumeNativeViewportNotice() { bool value = g_nativeViewportNotice; g_nativeViewportNotice = false; return value; }
inline bool ConsumeNativeMenuNotice() { bool value = g_nativeMenuNotice; g_nativeMenuNotice = false; return value; }
inline bool ConsumeNativeFrameNotice() { bool value = g_nativeFrameNotice; g_nativeFrameNotice = false; return value; }
inline std::atomic<int> g_coopDevCommand{0};
inline std::string g_coopDevStatus, g_coopDevDetails;
inline std::mutex g_coopDevMutex;
    enum class CoopDevCommand : int
    {
        None = 0,
        TestDodge = 1,
        TestMeleeTrail = 2,
        TestMuzzle = 3,
        TestShrink = 4,
        TestGlide = 5,
        ToggleProxyHidden = 6,
        PreviousVfxAnchor = 7,
        NextVfxAnchor = 8,
        TestWeaponEffect = 9,
        PepperHypothesis1 = 10,
        PepperHypothesis2 = 11,
        PepperHypothesis3 = 12,
        BombHypothesis1 = 13,
        BombHypothesis2 = 14,
        BombHypothesis3 = 15,
        ClearProjectileTests = 16,
    };

    inline std::atomic<bool> g_peerWatchingCutscene{ false };
    inline std::atomic<bool> g_coopWaitingForPeer{ false };
    inline std::atomic<bool> g_coopForceCutsceneRequested{ false };
    inline std::atomic<unsigned long long>
        g_soloMinigameWarningUntil{ 0 };
    inline std::atomic<unsigned long long>
        g_coopJoinTeleportHintUntil{ 0 };
    inline std::atomic<bool> g_peerCutsceneScreenPositionValid{ false };
    inline std::atomic<float> g_peerCutsceneScreenX{ 0.0f };
    inline std::atomic<float> g_peerCutsceneScreenY{ 0.0f };
    inline std::atomic<bool> g_coopMenuVisible{ false };
    inline std::atomic<bool> g_coopMenuHostRole{ false };
    inline std::atomic<bool> g_coopMenuRelayConnected{ false };
    inline std::atomic<bool> g_coopMenuPeerConnected{ false };
    inline std::atomic<bool> g_coopMenuCanJoinHost{ false };
    inline std::atomic<bool> g_coopMenuHovered{ false };
    inline std::atomic<bool> g_coopJoinHostRequested{ false };
    inline std::atomic<bool> g_coopPauseVisible{ false };
    inline std::atomic<bool> g_coopPauseHostRole{ false };
    inline std::atomic<bool> g_coopPausePeerConnected{ false };
    inline std::atomic<bool> g_coopPauseHovered{ false };
    inline std::atomic<bool> g_coopMovementTrailsEnabled{ false };
    inline std::atomic<bool> g_coopMovementTrailsToggleRequested{ false };
    inline std::atomic<bool> g_coopSaveSyncAvailable{ false };
    inline std::atomic<bool> g_coopSaveSyncInProgress{ false };
    inline std::atomic<int> g_coopSaveSyncProgress{ 0 };
    inline std::atomic<bool> g_coopSaveSyncRequested{ false };
	inline std::mutex g_coopSaveSyncMutex;
	inline std::string g_coopSaveSyncStatus;
	inline std::string g_coopSaveSyncProfileName;
    inline bool g_coopContextShortcutDown = false;
    inline std::mutex g_coopMenuMutex;
    inline std::string g_coopMenuPeerMap;
    inline std::string g_coopMenuStatus;
inline void SetCoopDevMode(bool enabled)
    {
        g_coopDevMode = enabled;
    }

inline void SetCoopDevStatus(const std::string& status)
    {
        g_coopDevStatus = status;
    }

inline void SetCoopDevDetails(const std::string& details)
    {
        std::lock_guard<std::mutex> lock(g_coopDevMutex);
        g_coopDevDetails = details;
    }

inline void SetPeerWatchingCutscene(
        bool watching, bool hasScreenPosition = false,
        float screenX = 0.0f, float screenY = 0.0f)
    {
        g_peerCutsceneScreenX.store(
            screenX, std::memory_order_relaxed);
        g_peerCutsceneScreenY.store(
            screenY, std::memory_order_relaxed);
        g_peerCutsceneScreenPositionValid.store(
            watching && hasScreenPosition,
            std::memory_order_release);
        g_peerWatchingCutscene.store(
            watching, std::memory_order_release);
    }

inline void SetCoopWaitingForPeer(bool waiting)
    {
        g_coopWaitingForPeer.store(
            waiting, std::memory_order_release);
        if (!waiting)
        {
            g_coopForceCutsceneRequested.store(
                false, std::memory_order_release);
        }
    }

inline void SetCoopMainMenuState(
        bool visible, bool hostRole,
        bool relayConnected, bool peerConnected,
        const std::string& peerMap,
        bool canJoinHost,
        const std::string& status)
    {
        {
            std::lock_guard<std::mutex> lock(g_coopMenuMutex);
            g_coopMenuPeerMap = peerMap;
            g_coopMenuStatus = status;
        }
        g_coopMenuHostRole.store(
            hostRole, std::memory_order_relaxed);
        g_coopMenuRelayConnected.store(
            relayConnected, std::memory_order_relaxed);
        g_coopMenuPeerConnected.store(
            peerConnected, std::memory_order_relaxed);
        g_coopMenuCanJoinHost.store(
            canJoinHost, std::memory_order_relaxed);
        g_coopMenuVisible.store(
            visible, std::memory_order_release);
        if (!visible)
        {
            g_coopMenuHovered.store(
                false, std::memory_order_release);
            g_coopJoinHostRequested.store(
                false, std::memory_order_release);
        }
    }

inline bool ConsumeCoopJoinHostRequest()
    {
        return g_coopJoinHostRequested.exchange(
            false, std::memory_order_acq_rel);
    }

inline void SetCoopPauseMenuState(
		bool visible, bool hostRole, bool peerConnected,
		bool movementTrailsEnabled)
	{
		g_coopPauseHostRole.store(hostRole, std::memory_order_relaxed);
		g_coopPausePeerConnected.store(
			peerConnected, std::memory_order_relaxed);
		g_coopMovementTrailsEnabled.store(
			movementTrailsEnabled, std::memory_order_relaxed);
		g_coopPauseVisible.store(visible, std::memory_order_release);
		if (!visible)
			g_coopPauseHovered.store(false, std::memory_order_release);
	}

inline bool ConsumeCoopMovementTrailsToggle(bool& enabled)
	{
		if (!g_coopMovementTrailsToggleRequested.exchange(
				false, std::memory_order_acq_rel))
			return false;
		enabled = g_coopMovementTrailsEnabled.load(
			std::memory_order_acquire);
		return true;
	}

inline void SetCoopSaveSyncState(
		bool available, bool inProgress, int progress,
		const std::string& status, const std::string& profileName)
	{
		{
			std::lock_guard<std::mutex> lock(g_coopSaveSyncMutex);
			g_coopSaveSyncStatus = status;
			g_coopSaveSyncProfileName = profileName;
		}
		g_coopSaveSyncAvailable.store(
			available, std::memory_order_relaxed);
		g_coopSaveSyncInProgress.store(
			inProgress, std::memory_order_relaxed);
		g_coopSaveSyncProgress.store(
			std::clamp(progress, 0, 100), std::memory_order_release);
	}

inline bool ConsumeCoopSaveSyncRequest()
	{
		return g_coopSaveSyncRequested.exchange(
			false, std::memory_order_acq_rel);
	}

inline bool ConsumeCoopForceCutsceneRequest()
    {
        return g_coopForceCutsceneRequested.exchange(
            false, std::memory_order_acq_rel);
    }

inline void ShowSoloMinigameWarning(
        unsigned int durationMilliseconds)
    {
        g_soloMinigameWarningUntil.store(
            GetTickCount64() + durationMilliseconds,
            std::memory_order_release);
    }

inline void HideSoloMinigameWarning()
    {
        g_soloMinigameWarningUntil.store(
            0, std::memory_order_release);
    }

inline void ShowCoopJoinTeleportHint(
        unsigned int durationMilliseconds)
    {
        g_coopJoinTeleportHintUntil.store(
            GetTickCount64() + durationMilliseconds,
            std::memory_order_release);
    }

inline void HideCoopJoinTeleportHint()
    {
        g_coopJoinTeleportHintUntil.store(
            0, std::memory_order_release);
    }

inline int ConsumeCoopDevCommand()
    {
        return g_coopDevCommand.exchange(
            static_cast<int>(CoopDevCommand::None),
            std::memory_order_acq_rel);
    }

inline void PollCoopContextShortcut()
{
    const bool down = (GetAsyncKeyState('L') & 0x8000) != 0;
    if (!g_hWnd || GetForegroundWindow() != g_hWnd || !IsRendering())
    { g_coopContextShortcutDown = down; return; }
    if (down && !g_coopContextShortcutDown && g_coopWaitingForPeer.load())
        g_coopForceCutsceneRequested.store(true);
    g_coopContextShortcutDown = down;
}

// All requests stay behind the existing gameplay/save-sync checks in CoopClient.
enum class CoopCanvasAction { None, Join, ForceCutscene, ToggleTrails };
struct CoopCanvasButton
{
    float x, y, w, h;
    CoopCanvasAction action;
    bool enabled;
};
inline std::vector<CoopCanvasButton> g_coopCanvasButtons;
inline bool g_coopCanvasFocus = false, g_coopCanvasMouseCaptured = false;
inline std::size_t g_coopCanvasSelection = 0;

inline bool HasCoopCanvasContent()
{
    return GetTickCount64() < g_coopArrivalUntil.load()
        || g_coopMenuVisible.load() || g_peerWatchingCutscene.load() || g_coopWaitingForPeer.load()
        || g_coopMenuVisible.load() || g_coopPauseVisible.load()
        || GetTickCount64() < g_soloMinigameWarningUntil.load()
        || GetTickCount64() < g_coopJoinTeleportHintUntil.load();
}

inline bool IsCoopCanvasFocused()
{
    return g_coopCanvasFocus && (g_coopPauseVisible.load() || g_coopWaitingForPeer.load());
}

inline void DispatchCoopCanvasAction(CoopCanvasAction action)
{
    switch (action)
    {
    case CoopCanvasAction::Join:
        if (g_coopMenuVisible.load() && !g_coopMenuHostRole.load() && g_coopMenuCanJoinHost.load())
            g_coopJoinHostRequested.store(true);
        break;
    case CoopCanvasAction::ForceCutscene:
        if (g_coopWaitingForPeer.load()) g_coopForceCutsceneRequested.store(true);
        break;
    case CoopCanvasAction::ToggleTrails:
        if (g_coopPauseVisible.load())
        {
            g_coopMovementTrailsEnabled.store(!g_coopMovementTrailsEnabled.load());
            g_coopMovementTrailsToggleRequested.store(true);
        }
        break;
    default: break;
    }
}

// Main-menu actions and confirmation belong to Scaleform; Canvas only draws events.
inline void TickCoopScaleformMenu()
{
    if (!g_coopMenuVisible.load() || !g_State.AlicePlayerController) return;
    static unsigned long long next = 0;
    if (GetTickCount64() < next) return;
    next = GetTickCount64() + 100;
    auto* pc = g_State.AlicePlayerController;
    static UFunction* get = nullptr; static UFunction* number = nullptr;
    static UFunction* text = nullptr; static UFunction* invoke = nullptr;
    if (!get) get = UFunction::FindFunction("Function GFxUI.GFxMovie.GetVariableNumber");
    if (!number) number = UFunction::FindFunction("Function GFxUI.GFxMovie.SetVariableNumber");
    if (!text) text = UFunction::FindFunction("Function GFxUI.GFxMovie.SetVariableString");
    if (!invoke) invoke = UFunction::FindFunction("Function GFxUI.GFxMovie.ActionScriptVoid");
    if (!get || !number || !text || !invoke) return;
    auto read = [&](UGFxMovie* movie, const wchar_t* path) {
        UGFxMovie_execGetVariableNumber_Params p{}; p.Path = FString(path);
        movie->ProcessEvent(get, &p, nullptr); return p.ReturnValue;
    };
    UGFxMovie* movie = nullptr;
    for (UGFxMovie* candidate : {static_cast<UGFxMovie*>(pc->FocusedMovie), static_cast<UGFxMovie*>(pc->UI_AliceGFXMovie)})
        if (candidate && read(candidate, L"_global.AliceCoopReady") == 1.0f) { movie = candidate; break; }
    if (!movie) return;
    static UGFxMovie* loggedMovie = nullptr;
    if (loggedMovie != movie) { loggedMovie = movie; g_nativeMenuNotice = true; }
    auto setNumber = [&](const wchar_t* path, float value) {
        UGFxMovie_execSetVariableNumber_Params p{}; p.Path = FString(path); p.F = value;
        movie->ProcessEvent(number, &p, nullptr);
    };
    auto setText = [&](const wchar_t* path, const std::wstring& value) {
        UGFxMovie_execSetVariableString_Params p{}; p.Path = FString(path); p.S = FString(value.c_str());
        movie->ProcessEvent(text, &p, nullptr);
    };
    // Use the game's viewport API for its fixed 1280x720 menu movie. ShowAll
    // otherwise reveals narrow strips of the 3D scene in a taller window.
    // ExactFit keeps every menu item on screen; gameplay movies are unaffected.
    static UGFxMovie* fittedMovie = nullptr;
    static int fittedWidth = 0, fittedHeight = 0;
    const int width = static_cast<int>(g_State.screenWidth), height = static_cast<int>(g_State.screenHeight);
    if (FixAspectRatio && movie->IsA(UAliceGfxMovie_titlePlayerMenu::StaticClass())
        && width > 0 && height > 0
        && (movie != fittedMovie || width != fittedWidth || height != fittedHeight
            || read(movie, L"_global.AliceCoopViewportReady") != 1))
    {
        static UFunction* viewport = UFunction::FindFunction("Function GFxUI.GFxMovie.SetViewport");
        static UFunction* scale = UFunction::FindFunction("Function GFxUI.GFxMovie.SetViewScaleMode");
        if (viewport && scale && (viewport->FunctionFlags & 0x400) && viewport->iNative
            && (scale->FunctionFlags & 0x400) && scale->iNative)
        {
            UGFxMovie_execSetViewport_Params bounds{};
            bounds.Width = width; bounds.Height = height;
            movie->ProcessEvent(viewport, &bounds, nullptr);
            UGFxMovie_execSetViewScaleMode_Params mode{}; mode.sm = static_cast<std::uint8_t>(EGFxScaleMode::GSM_ExactFit);
            movie->ProcessEvent(scale, &mode, nullptr);
            fittedMovie = movie; fittedWidth = width; fittedHeight = height;
            setNumber(L"_global.AliceCoopViewportReady", 1);
            g_nativeViewportNotice = true;
        }
    }
    // The root calls SetInMainMenu/PlayMusic only once, before its controller
    // may be ready. A focused, owned title movie is the readiness evidence;
    // requiring bInMainMenu here would prevent recovery of that missed callback.
    // Resume through the same game-owned audio path, never create another loop.
    static unsigned long long nextMusicCheck = 0;
    if (GetTickCount64() >= nextMusicCheck && pc->FocusedMovie == movie
        && movie->IsA(UAliceGfxMovie_titlePlayerMenu::StaticClass()))
    {
        nextMusicCheck = GetTickCount64() + 1000;
        auto* title = static_cast<UAliceGfxMovie_titlePlayerMenu*>(movie);
        auto* audio = pc->uniqueAudio;
        static UFunction* playing = UFunction::FindFunction("Function Engine.AudioComponent.IsPlaying");
        static UFunction* play = UFunction::FindFunction("Function Engine.AudioComponent.Play");
        if (title->APC == pc && title->MainMenuMusicStart && playing && play
            && (playing->FunctionFlags & 0x400) && playing->iNative
            && (play->FunctionFlags & 0x400) && play->iNative)
        {
            if (!pc->bInMainMenu) title->SetInMainMenu(true);
            UAudioComponent_execIsPlaying_Params active{};
            if (audio) audio->ProcessEvent(playing, &active, nullptr);
            // Leave an active credits or other native UI cue alone.
            if (!active.ReturnValue)
            {
                if (!audio || audio->SoundCue != title->MainMenuMusicStart)
                    // Keep the same movie->controller script context as the
                    // stock callback instead of issuing an actor event from UI.
                    title->PlayMusic(FString(L"MainMenuMusicStart"));
                else
                {
                    UAudioComponent_execPlay_Params start{};
                    audio->ProcessEvent(play, &start, nullptr);
                }
                audio = pc->uniqueAudio;
                UAudioComponent_execIsPlaying_Params recovered{};
                if (audio) audio->ProcessEvent(playing, &recovered, nullptr);
                g_nativeMusicResult.store(recovered.ReturnValue ? 1u : audio ? 2u : 3u);
            }
        }
    }
    std::string status, sync, profile;
    { std::lock_guard<std::mutex> lock(g_coopMenuMutex); status = g_coopMenuStatus; }
    { std::lock_guard<std::mutex> lock(g_coopSaveSyncMutex); sync = g_coopSaveSyncStatus; profile = g_coopSaveSyncProfileName; }
    const bool host = g_coopMenuHostRole.load(), relay = g_coopMenuRelayConnected.load(), peer = g_coopMenuPeerConnected.load();
    const bool canSync = !host && g_coopSaveSyncAvailable.load() && !g_coopSaveSyncInProgress.load();
    static std::string previousProfile;
    static unsigned int profileToken = 0;
    if (previousProfile != profile || profileToken == 0) { previousProfile = profile; ++profileToken; }
    const float command = read(movie, L"_global.AliceCoopCommand");
    static unsigned long long profileChangedUntil = 0;
    static unsigned long long nextAboutLink = 0, aboutLinkErrorUntil = 0;
    if (command != 0.0f)
    {
        setNumber(L"_global.AliceCoopCommand", 0);
        if (command == 1.0f) DispatchCoopCanvasAction(CoopCanvasAction::Join);
        if (command == 3.0f && canSync)
        {
            // The explicit native destructive choice must still target the profile
            // shown when confirmation opened, never a newly selected profile.
            if (read(movie, L"_global.AliceCoopConfirmedProfileToken") == static_cast<float>(profileToken))
                g_coopSaveSyncRequested.store(true);
            else profileChangedUntil = GetTickCount64() + 5000;
        }
        // Only explicit selection in our active About page opens a fixed public
        // URL. Never accept a URL from movie variables or a network message.
        if (read(movie, L"_global.AliceCoopAbout") == 1
            && read(movie, L"_global.AliceCoopOpen") == 1
            && read(movie, L"_global.AliceCoopInteractive") == 1
            && GetForegroundWindow() == g_hWnd
            && GetTickCount64() >= nextAboutLink)
        {
            static constexpr const wchar_t* urls[] = {
                L"https://github.com/docwitson/Alice-Madness-Returns-Co-op",
                L"https://www.nexusmods.com/alicemadnessreturns/mods/87",
                L"https://x.com/DocWitson99121",
                L"https://www.youtube.com/channel/UCc3Nim-4rjxRtHeKzcthJlw"
            };
            for (unsigned int i = 0; i < std::size(urls); ++i)
                if (command == static_cast<float>(30 + i))
                {
                    nextAboutLink = GetTickCount64() + 1500;
                    const auto opened = reinterpret_cast<INT_PTR>(ShellExecuteW(
                        g_hWnd, L"open", urls[i], nullptr, nullptr, SW_SHOWNORMAL));
                    aboutLinkErrorUntil = opened <= 32 ? GetTickCount64() + 5000 : 0;
                    break;
                }
        }
    }
    setNumber(L"_global.AliceCoopHost", host ? 1.0f : 0.0f);
    setNumber(L"_global.AliceCoopCanJoin", !host && g_coopMenuCanJoinHost.load() ? 1.0f : 0.0f);
    setNumber(L"_global.AliceCoopCanSync", canSync ? 1.0f : 0.0f);
    setNumber(L"_global.AliceCoopProfileToken", static_cast<float>(profileToken));
    setText(L"_global.AliceCoopProfile", Utf8ToWide(profile));
    setText(L"_global.AliceCoopTitle", L"Multiplayer");
    setText(L"_global.AliceCoopRole", host ? L"Host session" : L"Client session");
    const bool hostInMenu = !host && peer && status == "HOST IS IN MAIN MENU";
    const std::wstring state = hostInMenu ? L"Host must enter a level" : !relay ? L"Connecting to server" : (!peer ? L"Waiting for partner" : L"Partner connected");
    setText(L"_global.AliceCoopStatus", state);
    setNumber(L"_global.AliceCoopStatusColor", static_cast<float>(relay && peer && !hostInMenu ? 0xA8C894 : 0xE4B766));
    std::wstring details = relay ? L"Server connection ready." : L"Waiting for the server connection.";
    if (!host && status == "HOST IS IN MAIN MENU") details += L"\nJoin host unlocks after the host starts or continues a game.";
    else if (!host && status == "HOST IS IN CUTSCENE") details += L"\nThe host is watching a cutscene.";
    else if (!host && status == "HOST IS LOADING...") details += L"\nThe host is loading a checkpoint.";
    else if (!host && g_coopMenuCanJoinHost.load()) details += L"\nThe host checkpoint is ready to join.";
    if (g_coopSaveSyncInProgress.load()) details += L"\nReceiving profile: " + std::to_wstring(g_coopSaveSyncProgress.load()) + L"%.";
    else if (!sync.empty()) details += L"\n" + Utf8ToWide(sync);
    if (GetTickCount64() < profileChangedUntil) details = L"Profile selection changed. Open confirmation again.";
    setText(L"_global.AliceCoopDetails", details);
    setText(L"_global.AliceCoopHeading", host ? L"Playing together" : hostInMenu ? L"Waiting for the host" : L"Join the session");
    setText(L"_global.AliceCoopBody", host
        ? L"Choose your role and server address in the launcher.\n\nStart or continue a game from the main menu. Your partner can then join your checkpoint."
        : hostInMenu
        ? L"The host must start or continue a game to load a level.\n\nJoin host becomes available once the level is ready.\n\nYou can receive the host profile while waiting."
        : L"Choose Join host to load the host checkpoint and meet your partner.\n\nReceive host profile copies progress into your selected client profile.");
    setText(L"_global.AliceCoopNav", L"Arrows / D-pad: navigate\nEnter / A: select   Escape / B: back");
    setText(L"_global.AliceCoopConfirmTitle", L"Receive host profile");
    setText(L"_global.AliceCoopConfirmRole", L"Replace local progress");
    setText(L"_global.AliceCoopConfirmDetails", L"Receiving the host profile replaces this profile's progress, checkpoints, collectibles and upgrades.");
    setText(L"_global.AliceCoopConfirmHeading", L"Before receiving");
    setText(L"_global.AliceCoopConfirmBody", L"Use an empty client profile if you want to keep your current progress.\n\nA backup is retained before replacement.\n\nChoose Receive and replace only when ready.");
    setText(L"_global.AliceCoopConfirmNav", L"Back is selected by default.\nEnter / A: select   Escape / B: back");
    setText(L"_global.AliceCoopAboutTitle", L"Alice Co-op");
    setText(L"_global.AliceCoopAboutRole", L"Experimental two-player co-op mod");
    setText(L"_global.AliceCoopAboutStatus", L"Created by Docwitson");
    setText(L"_global.AliceCoopAboutDetails", L"Built on MadnessPatch by Wemino and contributors\nOpen-source project | GPL-2.0");
    setText(L"_global.AliceCoopAboutHeading", L"Links & socials");
    setText(L"_global.AliceCoopAboutBody", L"");
    setText(L"_global.AliceCoopAboutNav", GetTickCount64() < aboutLinkErrorUntil
        ? L"Could not open the browser.\nEnter / A: retry   Escape / B: back"
        : L"Arrows / D-pad: navigate\nEnter / A: open link   Escape / B: back");
    UGFxMovie_execActionScriptVoid_Params update{}; update.Path = FString(L"_global.AliceCoopMC.CoopRefresh");
    movie->ProcessEvent(invoke, &update, nullptr);
}

inline void ActivateCoopCanvasSelection()
{
    if (g_coopCanvasSelection >= g_coopCanvasButtons.size()) return;
    const auto button = g_coopCanvasButtons[g_coopCanvasSelection];
    if (button.enabled) DispatchCoopCanvasAction(button.action);
}

inline bool CoopCanvasMouse(float& x, float& y)
{
    if (!g_hWnd || GetForegroundWindow() != g_hWnd || !g_lastCanvasW || !g_lastCanvasH) return false;
    POINT cursor{}; RECT client{};
    if (!GetCursorPos(&cursor) || !ScreenToClient(g_hWnd, &cursor) || !GetClientRect(g_hWnd, &client)
        || client.right <= client.left || client.bottom <= client.top) return false;
    x = cursor.x * static_cast<float>(g_lastCanvasW) / (client.right - client.left);
    y = cursor.y * static_cast<float>(g_lastCanvasH) / (client.bottom - client.top);
    return true;
}

inline bool CoopCanvasContains(const CoopCanvasButton& b, float x, float y)
{
    return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

inline bool HandleCoopCanvasInput(HWND window, UINT msg, WPARAM key, LPARAM data)
{
    if (msg == WM_LBUTTONUP && g_coopCanvasMouseCaptured)
    {
        g_coopCanvasMouseCaptured = false;
        return true;
    }
    if (window != g_hWnd || GetForegroundWindow() != g_hWnd || !IsRendering() || !HasCoopCanvasContent()) return false;
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK)
    {
        float x = 0, y = 0;
        if (CoopCanvasMouse(x, y))
            for (std::size_t i = 0; i < g_coopCanvasButtons.size(); ++i)
                if (CoopCanvasContains(g_coopCanvasButtons[i], x, y))
                {
                    g_coopCanvasMouseCaptured = true;
                    g_coopCanvasSelection = i;
                    const auto button = g_coopCanvasButtons[i];
                    if (button.enabled) DispatchCoopCanvasAction(button.action);
                    return true;
                }
        return false;
    }
    const bool focused = IsCoopCanvasFocused();
    if (msg == WM_KEYDOWN)
    {
        const bool repeated = (static_cast<unsigned long long>(data) & (1ull << 30)) != 0;
        if (key == VK_TAB && !g_coopCanvasButtons.empty())
        {
            if (!repeated) g_coopCanvasFocus = !g_coopCanvasFocus;
            return true;
        }
        if (focused)
        {
            if (!repeated && (key == VK_UP || key == VK_DOWN) && !g_coopCanvasButtons.empty())
            {
                const auto count = g_coopCanvasButtons.size();
                g_coopCanvasSelection = (g_coopCanvasSelection + (key == VK_UP ? count - 1 : 1)) % count;
            }
            else if (!repeated && key == VK_RETURN) ActivateCoopCanvasSelection();
            else if (!repeated && key == VK_ESCAPE)
            {
                g_coopCanvasFocus = false;
            }
            return true;
        }
    }
    // Keep key-down/up and text input paired while this UI owns navigation.
    return focused && (msg == WM_KEYUP || msg == WM_CHAR || msg == WM_MOUSEWHEEL);
}

inline void PollCoopCanvasController()
{
    static WORD previous = 0;
    const WORD now = g_padConnected ? g_padState.Gamepad.wButtons : 0;
    const WORD pressed = now & ~previous;
    previous = now;
    if (!g_hWnd || GetForegroundWindow() != g_hWnd || !IsRendering() || !HasCoopCanvasContent())
    {
        g_coopCanvasFocus = false;
        return;
    }
    if ((pressed & XINPUT_GAMEPAD_RIGHT_THUMB) && !g_coopCanvasButtons.empty()) g_coopCanvasFocus = !g_coopCanvasFocus;
    if (!IsCoopCanvasFocused()) return;
    if ((pressed & XINPUT_GAMEPAD_DPAD_UP) && !g_coopCanvasButtons.empty())
        g_coopCanvasSelection = (g_coopCanvasSelection + g_coopCanvasButtons.size() - 1) % g_coopCanvasButtons.size();
    if ((pressed & XINPUT_GAMEPAD_DPAD_DOWN) && !g_coopCanvasButtons.empty())
        g_coopCanvasSelection = (g_coopCanvasSelection + 1) % g_coopCanvasButtons.size();
    if (pressed & XINPUT_GAMEPAD_A) ActivateCoopCanvasSelection();
    if (pressed & XINPUT_GAMEPAD_B)
    {
        g_coopCanvasFocus = false;
    }
}

inline void CoopCanvasText(DrawCtx& d, const std::wstring& text, float x, float y, float pixels = 20,
    FColor color = FColor(238, 230, 215, 255))
{
    DrawTextLine(d, text.c_str(), x, y, pixels * d.s / d.fontUnitHeight, color);
}

inline UTexture2D* CoopCanvasSharedTexture(const char* name)
{
    // uiShare is used by the HUD and native popups, unlike titlePlayerMenu
    // textures which can be unloaded when leaving AliceEntry.
    return UObject::FindObject<UTexture2D>(std::string("Texture2D AliceGameUI.uiShare.") + name);
}

inline UTexture2D* CoopCanvasFrameTexture()
{
    static UTexture2D* frame = nullptr; static int32_t index = -1;
    static unsigned long long nextSearch = 0;
    auto* objects = UObject::GObjObjects();
    if (frame && objects && index >= 0 && index < objects->size()
        && objects->at(index) == frame && frame->GetName() == "uiShare_IAA") return frame;
    frame = nullptr;
    if (GetTickCount64() < nextSearch) return nullptr;
    nextSearch = GetTickCount64() + 3000;
    frame = CoopCanvasSharedTexture("uiShare_IAA");
    if (frame) { index = frame->ObjectInternalInteger; g_nativeFrameNotice = true; }
    return frame;
}

inline void CoopCanvasPanel(DrawCtx& d, float x, float y, float w, float h)
{
    // Reuse the native shared popup frame; nine slices preserve its borders.
    UTexture2D* frame = CoopCanvasFrameTexture();
    if (frame && frame->SizeX > 0 && frame->SizeY > 0)
    {
        const float border = (std::min)(10 * d.s, h * 0.25f);
        const float dx[4] = {x, x + border, x + w - border, x + w};
        const float dy[4] = {y, y + border, y + h - border, y + h};
        const float u[4] = {0, 20, frame->SizeX - 20.0f, static_cast<float>(frame->SizeX)};
        const float v[4] = {0, 20, frame->SizeY - 20.0f, static_cast<float>(frame->SizeY)};
        for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col)
        {
            Native::SetPos(d.canvas, dx[col], dy[row]);
            Native::DrawTile(d.canvas, frame, dx[col + 1] - dx[col], dy[row + 1] - dy[row],
                u[col], v[row], u[col + 1] - u[col], v[row + 1] - v[row], FLinearColor(1, 1, 1, row == 1 && col == 1 ? 0.75f : 0.96f));
        }
    }
    else
    {
        FillRect(d, x, y, w, h, FLinearColor(0.035f, 0.025f, 0.03f, 0.75f));
        OutlineRect(d, x, y, w, h, FColor(164, 136, 92, 230));
    }
}

inline void CoopCanvasDrawButton(DrawCtx& d, float x, float y, float w, const std::wstring& label,
    CoopCanvasAction action, bool enabled = true)
{
    const float h = 32 * d.s;
    CoopCanvasButton button{x, y, w, h, action, enabled};
    const std::size_t index = g_coopCanvasButtons.size();
    g_coopCanvasButtons.push_back(button);
    float mx = 0, my = 0;
    const bool hovered = CoopCanvasMouse(mx, my) && CoopCanvasContains(button, mx, my);
    const bool selected = IsCoopCanvasFocused() && index == g_coopCanvasSelection;
    FillRect(d, x, y, w, h, enabled
        ? (hovered || selected ? FLinearColor(0.24f, 0.19f, 0.1f, 0.3f) : FLinearColor(0.015f, 0.015f, 0.015f, 0.18f))
        : FLinearColor(0.08f, 0.07f, 0.07f, 1));
    if (hovered || selected) OutlineRect(d, x, y, w, h, FColor(181, 154, 97, 230));
    float inset = 10 * d.s;
    if (selected && g_State.isUsingGamepad)
    {
        const bool playStation = UsePS3ControllerIcons == 2
            || (UsePS3ControllerIcons == 0 && ControllerHelper::GetGamepadStyle() == ControllerHelper::GamepadStyle::PlayStation);
        if (auto* accept = CoopCanvasSharedTexture(playStation ? "uiShare_I21" : "uiShare_I1E"))
        {
            Native::SetPos(d.canvas, x + 5 * d.s, y + 4 * d.s);
            Native::DrawTile(d.canvas, accept, 24 * d.s, 24 * d.s, 0, 0,
                static_cast<float>(accept->SizeX), static_cast<float>(accept->SizeY), FLinearColor(1, 1, 1, 1));
            inset = 34 * d.s;
        }
    }
    CoopCanvasText(d, label, x + inset, y + 5 * d.s, 18,
        enabled ? FColor(241, 216, 159, 255) : FColor(133, 128, 122, 255));
}

inline void CoopCanvasMessage(DrawCtx& d, const std::wstring& text, float y)
{
    const float w = (std::min)(500 * d.s, static_cast<float>(d.canvas->SizeX) - 32 * d.s);
    const float x = (d.canvas->SizeX - w) * 0.5f;
    CoopCanvasPanel(d, x, y, w, 32 * d.s);
    CoopCanvasText(d, text, x + 12 * d.s, y + 7 * d.s, 16);
}

inline void DrawCoopCanvas(DrawCtx& d)
{
    g_coopCanvasButtons.clear();
    if (!HasCoopCanvasContent()) { g_coopCanvasFocus = false; return; }
    const float previousScale = d.s, previousFontHeight = d.fontUnitHeight;
    UFont* const previousFont = d.canvas->Font;
    if (g_State.AliceEngine && g_State.AliceEngine->SubtitleFont)
        UseFont(d, g_State.AliceEngine->SubtitleFont);
    d.s = std::clamp((std::min)(d.canvas->SizeX / 1280.0f, d.canvas->SizeY / 720.0f), 0.5f, 2.5f);
    const float s = d.s;
    if (g_coopMenuVisible.load())
    {
        std::wstring stamp = L"AliceCoop 0.1.0-alpha.3  |  ";
        stamp += g_coopMenuHostRole.load() ? L"Host" : L"Client";
        stamp += g_coopMenuRelayConnected.load() ? L" Online" : L" Offline";
        CoopCanvasText(d, stamp, 16 * s, d.canvas->SizeY - 30 * s, 15,
            FColor(201, 184, 153, 220));
    }
    if (g_coopPauseVisible.load() && !g_coopMenuVisible.load())
    {
        const float w = 380 * s, h = 134 * s, x = 16 * s, y = d.canvas->SizeY - h - 44 * s;
        CoopCanvasPanel(d, x, y, w, h);
        CoopCanvasText(d, L"Multiplayer", x + 14 * s, y + 10 * s, 20);
        CoopCanvasDrawButton(d, x + 12 * s, y + 37 * s, w - 24 * s,
            g_coopMovementTrailsEnabled.load() ? L"T: Movement trails - on" : L"T: Movement trails - off", CoopCanvasAction::ToggleTrails);
        CoopCanvasText(d, g_coopPauseHostRole.load()
            ? L"P: Go to partner    O: Summon partner"
            : L"P: Go to host    O: Meet host across the level", x + 14 * s, y + 78 * s, 16);
        CoopCanvasText(d, L"Tab / R3: focus    Enter / A: select", x + 14 * s, y + 103 * s, 15,
            FColor(183, 169, 146, 255));
    }
    if (g_coopWaitingForPeer.load())
    {
        const float w = 460 * s, x = (d.canvas->SizeX - w) * 0.5f, y = d.canvas->SizeY - 76 * s;
        CoopCanvasPanel(d, x, y, w, 60 * s);
        CoopCanvasText(d, L"Waiting for partner to finish the cutscene", x + 10 * s, y + 6 * s, 16);
        CoopCanvasDrawButton(d, x + 10 * s, y + 25 * s, w - 20 * s, L"L: Continue together", CoopCanvasAction::ForceCutscene);
    }
    if (g_peerWatchingCutscene.load())
        CoopCanvasMessage(d, L"Your partner is watching a cutscene", 32 * s);
    if (GetTickCount64() < g_soloMinigameWarningUntil.load())
        CoopCanvasMessage(d, L"Solo minigame - you will rejoin afterwards.", 70 * s);
    if (GetTickCount64() < g_coopJoinTeleportHintUntil.load())
        CoopCanvasMessage(d, L"P: Go to host    O: Meet host across the level", 108 * s);
    {
        std::lock_guard<std::mutex> lock(g_coopArrivalMutex);
        if (GetTickCount64() < g_coopArrivalUntil)
            CoopCanvasMessage(d, Utf8ToWide(g_coopArrivalStatus), 146 * s);
    }
    if (g_coopCanvasSelection >= g_coopCanvasButtons.size()) g_coopCanvasSelection = 0;
    if (g_coopCanvasButtons.empty()) g_coopCanvasFocus = false;
    d.s = previousScale;
    d.fontUnitHeight = previousFontHeight;
    d.canvas->Font = previousFont;
}
