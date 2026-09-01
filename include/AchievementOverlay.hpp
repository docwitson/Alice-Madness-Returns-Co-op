#pragma once

#include <d3d9.h>
#include <Windows.h>
#include <wincodec.h>
#include <Xinput.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cfloat>
#include <atomic>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include "Common.hpp"
#include "Features.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace AchievementOverlay
{
    // Config
    inline constexpr bool kBlockGameInputWhileVisible = true;
    inline constexpr unsigned long long kCursorIdleHideMs = 3000;

    // State
    inline IDirect3DDevice9* g_pDevice = nullptr;
    inline IDirect3DDevice9* g_pInitializedDevice = nullptr;
    inline HWND g_hWnd = nullptr;
    inline WNDPROC g_oWndProc = nullptr;
    inline uintptr_t g_devicePtrAddr = 0;

    inline bool g_hooksInstalled = false;
    inline bool g_imguiInitialized = false;
    inline bool g_inReset = false;
    inline bool g_deviceLost = false;
    inline bool g_visible = false;
    inline bool g_coopDevMode = false;
    inline std::string g_coopDevStatus;
    inline std::string g_coopDevDetails;
    inline std::mutex g_coopDevMutex;
    inline std::atomic<int> g_coopDevCommand{ 0 };

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

    inline float g_uiScale = 1.0f;
    inline float g_pendingScale = 0.0f;
    inline int g_layoutWidth = 0;
    inline int g_layoutHeight = 0;

    inline int g_lastDispW = 0;
    inline int g_lastDispH = 0;

    inline float g_savedScrollY = 0.0f;
    inline bool g_restoreScroll = false;
    inline int g_restoreScrollFrames = 0;

    inline XINPUT_STATE g_padState{};
    inline bool g_padConnected = false;

    inline unsigned long long g_lastMouseInputTick = 0;

    struct ToastItem
    {
        int achvId;
        unsigned long long start;
        unsigned int uid;
        float height;
        float animY;
        bool placed;
    };
    inline std::vector<ToastItem> g_toasts;
    inline unsigned int g_toastNextId = 1;
    inline std::mutex g_toastMutex;

    // Achievement data
    struct Texture
    {
        IDirect3DTexture9* tex = nullptr;
        int w = 0;
        int h = 0;
    };

    struct AchievementText
    {
        std::string name;
        std::string desc;
    };

    inline std::vector<Texture> g_achievements;
    inline Texture g_cutsceneWatchTexture;
    inline bool g_cutsceneWatchTextureLoadAttempted = false;
    inline Texture g_coopWaitTexture;
    inline bool g_coopWaitTextureLoadAttempted = false;
    inline Texture g_soloMinigameTexture;
    inline bool g_soloMinigameTextureLoadAttempted = false;
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
    inline std::atomic<bool> g_coopSaveSyncWarningVisible{ false };
    inline std::atomic<bool> g_coopSaveSyncConfirmKeyDown{ false };
	inline std::atomic<bool> g_coopSaveSyncShortcutDown{ false };
	inline std::mutex g_coopSaveSyncMutex;
	inline std::string g_coopSaveSyncStatus;
	inline std::string g_coopSaveSyncProfileName;
    inline bool g_coopContextShortcutDown = false;
    inline std::mutex g_coopMenuMutex;
    inline std::string g_coopMenuPeerMap;
    inline std::string g_coopMenuStatus;
    inline std::vector<bool> g_unlocked;
    inline std::vector<int> g_current;
    inline std::vector<AchievementText> g_text;

    inline bool g_achievementsLoaded = false;
    inline bool g_textLoaded = false;
    inline std::string g_language = "en";

    inline std::filesystem::path g_achievementFolder;
    inline std::filesystem::path g_achievementFilePath;
    inline uint64_t g_unlockedBits = 0; // persistent unlock bitflags
    inline std::unordered_map<int, int> g_maxOverride; // runtime max overrides read from the game
    inline std::recursive_mutex g_stateMutex;
    inline bool g_achievementFileLoaded = false;

    inline constexpr int kAchievementCount = 45;
    inline float g_ventDuration = 0.0f; // persisted steam-vent timer
    inline bool g_ventNeedsApply = false; // game-side writes g_ventDuration back into the engine once on load

    inline bool g_showSecrets = false;

    // Secret achievements hidden until unlocked
    inline const std::vector<int> kSecretIndices = { 1, 2, 3, 4, 5, 6, 7, 13, 14, 15, 16, 17, 18, 19, 31, 33, 35 };

    inline bool IsSecret(int index)
    {
        for (int s : kSecretIndices)
        {
            if (s == index)
            {
                return true;
            }
        }
        return false;
    }

    // Target count per achievement, anything not listed is a plain on/off unlock (max 1)
    inline const std::unordered_map<int, int> kAchievementMax =
    {
        { 19, 4 }, { 20, 4 }, { 21, 16 }, { 22, 34 }, { 23, 94 }, { 34, 10 },
        { 35, 420 }, { 38, 10 }, { 39, 30 }, { 42, 100 }, { 43, 52 },
    };

    inline int AchievementMaxFrom(const std::unordered_map<int, int>& overrides, int index)
    {
        auto ov = overrides.find(index);
        if (ov != overrides.end())
        {
            return ov->second;
        }

        auto it = kAchievementMax.find(index);
        return it != kAchievementMax.end() ? it->second : 1;
    }

    inline int AchievementMax(int index)
    {
        std::lock_guard<std::recursive_mutex> lock(g_stateMutex);
        return AchievementMaxFrom(g_maxOverride, index);
    }

    inline bool IsVisible() { return g_visible; }

    inline void SetAchievementUnlocked(int index, bool unlocked)
    {
        if (index < 0) return;

        std::lock_guard<std::recursive_mutex> lock(g_stateMutex);

        if ((int)g_unlocked.size() <= index)
        {
            g_unlocked.resize(index + 1, false);
        }

        g_unlocked[index] = unlocked;
    }

    // Marks the achievement unlocked with no toast or save, used by the save-load sync
    inline bool MarkUnlockedSilent(int achvId)
    {
        if (achvId < 0 || achvId >= 64)
            return false;

        std::lock_guard<std::recursive_mutex> lock(g_stateMutex);

        const uint64_t bit = 1ull << achvId;
        if (g_unlockedBits & bit)
            return false;

        g_unlockedBits |= bit;
        SetAchievementUnlocked(achvId, true);
        return true;
    }

    // Queue an unlock toast
    inline void PushToast(int achvId)
    {
        ToastItem it{};
        it.achvId = achvId;
        it.start = GetTickCount64();

        std::lock_guard<std::mutex> lock(g_toastMutex);
        it.uid = g_toastNextId++;
        g_toasts.push_back(it);
    }

    // Persist the unlock flags and steam-vent timer to Achievements.txt
    inline void SaveAchievementBits()
    {
        std::lock_guard<std::recursive_mutex> lock(g_stateMutex);

        if (g_achievementFilePath.empty())
            return;

        const std::wstring realPath = g_achievementFilePath.native();
        const std::wstring tmpPath = realPath + L".tmp";

        {
            std::ofstream f(tmpPath.c_str(), std::ios::trunc | std::ios::binary);
            if (!f)
                return;

            f << "UnlockFlag=" << g_unlockedBits << "\n" << "TotalVentDuration=" << g_ventDuration << "\n";
            f.flush();

            if (!f)
            {
                f.close();
                DeleteFileW(tmpPath.c_str());
                return;
            }
        }

        if (!MoveFileExW(tmpPath.c_str(), realPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(tmpPath.c_str());
            return;
        }

        g_achievementFileLoaded = true;
    }

    // Index 0 is the platinum, award it once every other trophy is unlocked
    inline void AwardPlatinumIfComplete()
    {
        std::lock_guard<std::recursive_mutex> lock(g_stateMutex);

        if (g_unlockedBits & 1ull)
            return;

        uint64_t allOthers = 0;
        for (int i = 1; i < kAchievementCount; i++)
        {
            allOthers |= (1ull << i);
        }

        if ((g_unlockedBits & allOthers) == allOthers)
        {
            g_unlockedBits |= 1ull;
            SetAchievementUnlocked(0, true);
            PushToast(0);

            SaveAchievementBits();
        }
    }

    inline bool ParseAchievementFile(const std::filesystem::path& path, uint64_t& bitsOut, float& ventOut)
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return false;

        std::ifstream f(path);
        if (!f)
            return false;

        bool found = false;
        uint64_t bits = 0;
        float vent = 0.0f;

        std::string line;
        while (std::getline(f, line))
        {
            const size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            const std::string key = line.substr(0, eq);
            const char* val = line.c_str() + eq + 1;
            if (key == "UnlockFlag")
            {
                errno = 0;
                char* end = nullptr;
                const uint64_t parsed = std::strtoull(val, &end, 10);
                if (end == val || errno == ERANGE)
                    return false;

                bits = parsed;
                found = true;
            }
            else if (key == "TotalVentDuration")
            {
                vent = std::strtof(val, nullptr);
            }
        }

        if (!found)
            return false;

        bitsOut = bits;
        ventOut = (vent >= 0.0f && vent < 1.0e9f) ? vent : 0.0f;
        return true;
    }

    // Point the overlay at a profile's Achievements.txt and load it.
    // Returns true when the file is not there yet, meaning the caller should import from the save
    inline bool InitAchievementFile(const std::filesystem::path& folder)
    {
        std::lock_guard<std::recursive_mutex> lock(g_stateMutex);

        if (folder.empty() || (folder == g_achievementFolder && g_achievementFileLoaded))
            return false;

        g_achievementFolder = folder;
        g_achievementFilePath = folder / L"Achievements.txt";

        std::error_code ec;
        std::filesystem::create_directories(folder, ec);

        uint64_t bits = 0;
        float vent = 0.0f;
        const bool loaded = ParseAchievementFile(g_achievementFilePath, bits, vent);

        g_unlockedBits = bits;
        g_ventDuration = vent;
        g_ventNeedsApply = true; // game-side restores g_ventDuration into the engine on the next tick
        g_achievementFileLoaded = loaded;

        if ((int)g_unlocked.size() < 64)
        {
            g_unlocked.resize(64, false);
        }
        for (int i = 0; i < 64; i++)
        {
            g_unlocked[i] = ((g_unlockedBits >> i) & 1ull) != 0;
        }

        g_current.assign(g_current.size(), 0);
        g_maxOverride.clear();

        if (!loaded)
            return true;

        AwardPlatinumIfComplete(); // a save that already holds every non-platinum trophy
        return false;
    }

    // Marks the achievement unlocked, saves, and shows a toast only if it wasn't already unlocked
    inline bool NotifyUnlock(int achvId)
    {
        if (achvId < 0 || achvId >= 64)
            return false;

        std::lock_guard<std::recursive_mutex> lock(g_stateMutex);

        const uint64_t bit = 1ull << achvId;
        if (g_unlockedBits & bit)
            return false;

        g_unlockedBits |= bit;
        SetAchievementUnlocked(achvId, true);
        PushToast(achvId);
        SaveAchievementBits();

        if (achvId != 0)
        {
            AwardPlatinumIfComplete();
        }

        return true;
    }

    // Persist the current steam-vent timer (called when Alice leaves a vent).
    inline void SaveVentDuration(float seconds)
    {
        std::lock_guard<std::recursive_mutex> lock(g_stateMutex);

        g_ventDuration = seconds;
        SaveAchievementBits();
    }

    // Runtime max for counters whose total is read from the game
    inline void SetAchievementMax(int index, int maxValue)
    {
        if (index < 0) return;

        std::lock_guard<std::recursive_mutex> lock(g_stateMutex);
        g_maxOverride[index] = maxValue;
    }

    // Current progress for an achievement
    inline void SetAchievementProgress(int index, int current)
    {
        if (index < 0) return;

        std::lock_guard<std::recursive_mutex> lock(g_stateMutex);

        if ((int)g_current.size() <= index)
        {
            g_current.resize(index + 1, 0);
        }
        if (current < 0)
        {
            current = 0;
        }

        g_current[index] = current;
    }

    // Clear all unlock/progress state so everything shows as locked again, not called automatically
    inline void ResetProgress()
    {
        std::lock_guard<std::recursive_mutex> lock(g_stateMutex);

        g_unlockedBits = 0;
        g_unlocked.assign(g_unlocked.size(), false);
        g_current.assign(g_current.size(), 0);
    }

    // Reloads achievements\txt\<lang>.txt on the next frame
    inline void SetLanguage(const char* langCode)
    {
        if (!langCode || !langCode[0])
            return;

        g_language = langCode;
        g_textLoaded = false;
    }

    // Helpers
    inline IDirect3DDevice9* GetDevice()
    {
        if (g_devicePtrAddr == 0)
            return nullptr;

        return *reinterpret_cast<IDirect3DDevice9**>(g_devicePtrAddr);
    }

    inline bool QueryBackBufferSize(
        IDirect3DDevice9* dev, float& width, float& height)
    {
        IDirect3DSurface9* backBuffer = nullptr;
        if (!dev || FAILED(dev->GetBackBuffer(
                0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer))
            || !backBuffer)
        {
            return false;
        }

        D3DSURFACE_DESC description{};
        const bool valid = SUCCEEDED(backBuffer->GetDesc(&description))
            && description.Width > 0 && description.Height > 0;
        backBuffer->Release();
        if (!valid)
            return false;

        width = static_cast<float>(description.Width);
        height = static_cast<float>(description.Height);
        return true;
    }

    inline bool QueryOverlayLayoutSize(
        IDirect3DDevice9* dev, float& width, float& height)
    {
        float backBufferWidth = 0.0f;
        float backBufferHeight = 0.0f;
        const bool haveBackBuffer = QueryBackBufferSize(
            dev, backBufferWidth, backBufferHeight);

        RECT clientRect{};
        const bool haveClient = g_hWnd && GetClientRect(g_hWnd, &clientRect)
            && clientRect.right > clientRect.left
            && clientRect.bottom > clientRect.top;
        const float clientWidth = haveClient
            ? static_cast<float>(clientRect.right - clientRect.left) : 0.0f;
        const float clientHeight = haveClient
            ? static_cast<float>(clientRect.bottom - clientRect.top) : 0.0f;

        if (!haveBackBuffer && !haveClient)
            return false;

        if (haveBackBuffer && haveClient)
        {
            // Window geometry and the D3D9 backbuffer can briefly disagree
            // while UE3 applies a resolution change. Keep layout inside both
            // coordinate spaces so bottom/right anchored panels stay visible.
            width = (std::min)(backBufferWidth, clientWidth);
            height = (std::min)(backBufferHeight, clientHeight);
        }
        else if (haveBackBuffer)
        {
            width = backBufferWidth;
            height = backBufferHeight;
        }
        else
        {
            width = clientWidth;
            height = clientHeight;
        }
        return width > 0.0f && height > 0.0f;
    }

    inline float ComputeUiScaleFromHeight(float renderHeight)
    {
        return renderHeight > 1080.0f
            ? renderHeight / 1080.0f : 1.0f;
    }

    // Refresh this before deciding whether the ImGui context needs rebuilding.
    // Previously it was only updated from Render(), one frame after a resize.
    inline void RefreshRenderMetrics(IDirect3DDevice9* dev)
    {
        float renderWidth = 0.0f;
        float renderHeight = 0.0f;
        if (!QueryOverlayLayoutSize(dev, renderWidth, renderHeight))
            return;

        g_pendingScale = ComputeUiScaleFromHeight(renderHeight);
        const int width = static_cast<int>(renderWidth);
        const int height = static_cast<int>(renderHeight);
        if (width == g_layoutWidth && height == g_layoutHeight)
            return;

        g_layoutWidth = width;
        g_layoutHeight = height;
        // Force movable ImGui windows and animated notifications to acquire
        // positions in the new coordinate space immediately.
        g_lastDispW = 0;
        g_lastDispH = 0;
        std::lock_guard<std::mutex> lock(g_toastMutex);
        for (ToastItem& toast : g_toasts)
            toast.placed = false;
    }

    // Initial scale guess from the current backbuffer.
    inline float ComputeUiScale(IDirect3DDevice9* dev)
    {
        float renderWidth = 0.0f;
        float renderHeight = 0.0f;
        return QueryOverlayLayoutSize(dev, renderWidth, renderHeight)
            ? ComputeUiScaleFromHeight(renderHeight) : 1.0f;
    }

    // Texture & text loading
    inline bool LoadTextureFromFile(IDirect3DDevice9* dev, const char* path, Texture& out, int targetSize = 0)
    {
        static bool s_comInit = false;
        if (!s_comInit)
        {
            HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;
            s_comInit = true;
        }

        wchar_t wpath[MAX_PATH];
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH) == 0)
        {
            return false;
        }

        IWICImagingFactory* factory = nullptr;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        {
            return false;
        }

        bool ok = false;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICBitmapScaler* scaler = nullptr;
        IWICFormatConverter* conv = nullptr;
        IDirect3DTexture9* tex = nullptr;

        if (SUCCEEDED(factory->CreateDecoderFromFilename(wpath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) && SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&conv)))
        {
            // Scale first, then convert to BGRA
            IWICBitmapSource* src = frame;
            if (targetSize > 0 && SUCCEEDED(factory->CreateBitmapScaler(&scaler)) && SUCCEEDED(scaler->Initialize(frame, targetSize, targetSize, WICBitmapInterpolationModeFant)))
            {
                src = scaler;
            }

            if (SUCCEEDED(conv->Initialize(src, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
            {
                UINT w = 0, h = 0;
                conv->GetSize(&w, &h);
                if (w > 0 && h > 0 && SUCCEEDED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)))
                {
                    D3DLOCKED_RECT rect{};
                    if (SUCCEEDED(tex->LockRect(0, &rect, nullptr, 0)))
                    {
                        // CopyPixels honors the destination stride, so it fills the locked rect even when Pitch != w*4.
                        HRESULT cp = conv->CopyPixels(nullptr, (UINT)rect.Pitch, (UINT)rect.Pitch * h, (BYTE*)rect.pBits);
                        tex->UnlockRect(0);
                        if (SUCCEEDED(cp))
                        {
                            out.tex = tex;
                            out.w = (int)w;
                            out.h = (int)h;
                            ok = true;
                        }
                    }
                }
            }
        }

        if (!ok && tex) tex->Release();
        if (conv) conv->Release();
        if (scaler) scaler->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();
        factory->Release();
        return ok;
    }

    inline void ReleaseAchievements()
    {
        for (auto& a : g_achievements)
        {
            if (a.tex)
            {
                a.tex->Release();
            }
        }

        g_achievements.clear();
        g_achievementsLoaded = false;
    }

    inline void ReleaseCutsceneWatchTexture()
    {
        if (g_cutsceneWatchTexture.tex)
            g_cutsceneWatchTexture.tex->Release();
        g_cutsceneWatchTexture = {};
        g_cutsceneWatchTextureLoadAttempted = false;
        if (g_coopWaitTexture.tex)
            g_coopWaitTexture.tex->Release();
        g_coopWaitTexture = {};
        g_coopWaitTextureLoadAttempted = false;
        if (g_soloMinigameTexture.tex)
            g_soloMinigameTexture.tex->Release();
        g_soloMinigameTexture = {};
        g_soloMinigameTextureLoadAttempted = false;
    }

    inline void LoadCutsceneWatchTexture(IDirect3DDevice9* dev)
    {
        if (g_cutsceneWatchTexture.tex
            || g_cutsceneWatchTextureLoadAttempted || !dev)
        {
            return;
        }

        g_cutsceneWatchTextureLoadAttempted = true;
        const std::string path = SystemHelper::GetModulePath()
            + "\\AliceCoop\\images\\cutsceneWatch2.png";
        const int target = static_cast<int>(
            58.0f * g_uiScale + 0.5f);
        LoadTextureFromFile(
            dev, path.c_str(), g_cutsceneWatchTexture, target);
    }

    inline void LoadCoopWaitTexture(IDirect3DDevice9* dev)
    {
        if (g_coopWaitTexture.tex
            || g_coopWaitTextureLoadAttempted || !dev)
        {
            return;
        }

        g_coopWaitTextureLoadAttempted = true;
        const std::string path = SystemHelper::GetModulePath()
            + "\\AliceCoop\\images\\aliceWhait.png";
        const int target = static_cast<int>(
            86.0f * g_uiScale + 0.5f);
        LoadTextureFromFile(
            dev, path.c_str(), g_coopWaitTexture, target);
    }

    inline void LoadSoloMinigameTexture(IDirect3DDevice9* dev)
    {
        if (g_soloMinigameTexture.tex
            || g_soloMinigameTextureLoadAttempted || !dev)
        {
            return;
        }

        g_soloMinigameTextureLoadAttempted = true;
        const std::string path = SystemHelper::GetModulePath()
            + "\\AliceCoop\\images\\aliceSoloLevel.png";
        const int target = static_cast<int>(
            82.0f * g_uiScale + 0.5f);
        LoadTextureFromFile(
            dev, path.c_str(), g_soloMinigameTexture, target);
    }

    inline void LoadAchievements(IDirect3DDevice9* dev)
    {
        if (g_achievementsLoaded || !dev)
            return;

        g_achievementsLoaded = true;

        int target = (int)(64.0f * g_uiScale + 0.5f); // icons draw at 64 * g_uiScale, load them ~1:1

        std::string dir = SystemHelper::GetModulePath() + "\\achievements\\img\\";
        for (int i = 0; ; i++)
        {
            std::string p = dir + std::to_string(i) + ".png";
            if (GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                break;
            }

            Texture t;
            LoadTextureFromFile(dev, p.c_str(), t, target); // keep index alignment even if a load fails
            g_achievements.push_back(t);
        }

        {
            std::lock_guard<std::recursive_mutex> lock(g_stateMutex);

            if (g_unlocked.size() < g_achievements.size())
            {
                g_unlocked.resize(g_achievements.size(), false);
            }
            if (g_current.size() < g_achievements.size())
            {
                g_current.resize(g_achievements.size(), 0);
            }
        }
    }

    inline void LoadText()
    {
        if (g_textLoaded) return;

        g_textLoaded = true;
        g_text.clear();

        std::string path = SystemHelper::GetModulePath() + "\\achievements\\txt\\" + g_language + ".txt";

        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "rb") != 0 || !f)
        {
            return;
        }

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        std::string buf;
        if (size > 0)
        {
            buf.resize(size);
            fread(&buf[0], 1, size, f);
        }

        fclose(f);

        size_t pos = 0;
        if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF)
        {
            pos = 3; // skip UTF-8 BOM
        }

        while (pos < buf.size())
        {
            size_t eol = buf.find('\n', pos);
            std::string line = (eol == std::string::npos) ? buf.substr(pos) : buf.substr(pos, eol - pos);
            pos = (eol == std::string::npos) ? buf.size() : eol + 1;

            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (line.empty())
            {
                continue;
            }

            AchievementText at;
            size_t bar = line.find('|');
            if (bar == std::string::npos)
            {
                at.name = line;
            }
            else
            {
                at.name = line.substr(0, bar);
                at.desc = line.substr(bar + 1);
            }

            g_text.push_back(at);
        }
    }

    // ImGui setup
    inline void InitImGui(IDirect3DDevice9* pDevice)
    {
        if (g_imguiInitialized || !pDevice || !g_hWnd)
            return;

        g_uiScale = (g_pendingScale > 0.0f) ? g_pendingScale : ComputeUiScale(pDevice);

        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;

        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::StyleColorsDark();
        style.ScaleAllSizes(g_uiScale);
        style.FontScaleDpi = g_uiScale; // scales the font to the render resolution
        style.AntiAliasedLinesUseTex = false; // we render with point sampling, which the textured-line path can't use

        if (g_uiScale < 1.15f)
        {
            io.Fonts->AddFontDefaultBitmap();
        }
        else
        {
            io.Fonts->AddFontDefaultVector();
        }

        ImGui_ImplWin32_Init(g_hWnd);
        ImGui_ImplDX9_Init(pDevice);

        g_imguiInitialized = true;
        g_pInitializedDevice = pDevice;
    }

    inline void ShutdownImGui()
    {
        if (!g_imguiInitialized) return;

        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        g_imguiInitialized = false;
        g_pInitializedDevice = nullptr;
    }

    // Input
    inline float GetControllerScrollAxis()
    {
        if (!g_padConnected) return 0.0f;

        const short dz = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
        short ly = g_padState.Gamepad.sThumbLY;

        if (ly > dz)
            return (float)(ly - dz) / (float)(32767 - dz);
        if (ly < -dz)
            return (float)(ly + dz) / (float)(32768 - dz);

        return 0.0f;
    }

    // Rendering
    inline void DrawCoopDevModeBadge()
    {
        if (!g_coopDevMode)
            return;

        const float scale = g_uiScale;
        const ImVec2 padding(12.0f * scale, 8.0f * scale);
        const ImVec2 origin(16.0f * scale, 16.0f * scale);
        const char* label = "COOP DEV MODE";
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const ImVec2 statusSize = ImGui::CalcTextSize(
            g_coopDevStatus.c_str());
        const float contentWidth =
            textSize.x > statusSize.x ? textSize.x : statusSize.x;
        const float statusGap =
            g_coopDevStatus.empty() ? 0.0f : 5.0f * scale;
        const float contentHeight =
            textSize.y + statusGap
            + (g_coopDevStatus.empty() ? 0.0f : statusSize.y);
        const ImVec2 minimum(
            origin.x, origin.y);
        const ImVec2 maximum(
            origin.x + contentWidth + padding.x * 2.0f,
            origin.y + contentHeight + padding.y * 2.0f);
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->AddRectFilled(
            minimum, maximum, IM_COL32(7, 13, 23, 218),
            5.0f * scale);
        drawList->AddRect(
            minimum, maximum, IM_COL32(80, 190, 255, 230),
            5.0f * scale, 0, 1.0f * scale);
        drawList->AddText(
            ImVec2(origin.x + padding.x,
                origin.y + padding.y),
            IM_COL32(135, 220, 255, 255), label);
        if (!g_coopDevStatus.empty())
        {
            drawList->AddText(
                ImVec2(origin.x + padding.x,
                    origin.y + padding.y + textSize.y + statusGap),
                IM_COL32(205, 218, 228, 245),
                g_coopDevStatus.c_str());
        }
    }

    inline bool IsCoopDevInteractive()
    {
        return g_coopDevMode
            && (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    }

    inline bool IsCoopWaitInteractive()
    {
        return g_coopWaitingForPeer.load(std::memory_order_acquire)
            && (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    }

    inline void PollCoopContextShortcut()
    {
		const bool confirmVisible =
			g_coopSaveSyncWarningVisible.load(std::memory_order_acquire);
		const bool yesDown = (GetAsyncKeyState('Y') & 0x8000) != 0;
		const bool noDown = (GetAsyncKeyState('N') & 0x8000) != 0;
		const bool confirmDown = yesDown || noDown;
		if (confirmVisible && confirmDown
			&& !g_coopSaveSyncConfirmKeyDown.load(
				std::memory_order_relaxed))
		{
			g_coopSaveSyncWarningVisible.store(
				false, std::memory_order_release);
			if (yesDown)
				g_coopSaveSyncRequested.store(
					true, std::memory_order_release);
		}
		g_coopSaveSyncConfirmKeyDown.store(
			confirmDown, std::memory_order_relaxed);

		const bool saveSyncKeyDown =
			(GetAsyncKeyState('K') & 0x8000) != 0;
		if (saveSyncKeyDown
			&& !g_coopSaveSyncShortcutDown.load(std::memory_order_relaxed)
			&& !confirmVisible
			&& g_coopMenuVisible.load(std::memory_order_acquire)
			&& !g_coopMenuHostRole.load(std::memory_order_acquire)
			&& g_coopSaveSyncAvailable.load(std::memory_order_acquire)
			&& !g_coopSaveSyncInProgress.load(std::memory_order_acquire))
		{
			g_coopSaveSyncWarningVisible.store(
				true, std::memory_order_release);
		}
		g_coopSaveSyncShortcutDown.store(
			saveSyncKeyDown, std::memory_order_relaxed);

        const bool keyDown =
            (GetAsyncKeyState('L') & 0x8000) != 0;
        if (keyDown && !g_coopContextShortcutDown)
        {
            // L is deliberately contextual: outside a visible co-op action it
            // remains an ordinary game key and cannot queue a later command.
            if (g_coopWaitingForPeer.load(std::memory_order_acquire))
            {
                g_coopForceCutsceneRequested.store(
                    true, std::memory_order_release);
            }
            else if (g_coopMenuVisible.load(std::memory_order_acquire)
                && !g_coopMenuHostRole.load(std::memory_order_acquire)
                && g_coopMenuCanJoinHost.load(std::memory_order_acquire))
            {
                g_coopJoinHostRequested.store(
                    true, std::memory_order_release);
            }
        }
        g_coopContextShortcutDown = keyDown;
    }

    inline void QueueCoopDevCommand(CoopDevCommand command)
    {
        g_coopDevCommand.store(
            static_cast<int>(command), std::memory_order_release);
    }

    inline void DrawCoopDevInteractivePanel()
    {
        if (!IsCoopDevInteractive())
            return;

        const float scale = g_uiScale;
        ImGui::SetNextWindowPos(
            ImVec2(16.0f * scale, 92.0f * scale),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(430.0f * scale, 0.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.94f);
        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("AliceCoop dev mode",
            nullptr, flags))
        {
            ImGui::TextDisabled(
                "Runtime diagnostics. Hold Alt to keep this panel open.");
            std::string details;
            {
                std::lock_guard<std::mutex> lock(g_coopDevMutex);
                details = g_coopDevDetails;
            }
            if (!details.empty())
                ImGui::TextUnformatted(details.c_str());
            else
                ImGui::TextDisabled("No remote presentation data yet.");
        }
        ImGui::End();
    }

    inline void DrawPeerCutsceneIndicator(IDirect3DDevice9* pDevice)
    {
        if (!g_peerWatchingCutscene.load(std::memory_order_acquire))
            return;

        LoadCutsceneWatchTexture(pDevice);

        const float scale = g_uiScale;
        const float iconSize = 58.0f * scale;
        const float margin = 18.0f * scale;
        const ImGuiIO& io = ImGui::GetIO();
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        const float preferredIconX =
            io.DisplaySize.x - margin - iconSize;
        const ImVec2 iconMinimum(
            preferredIconX > margin ? preferredIconX : margin,
            margin);
        const ImVec2 iconMaximum(
            iconMinimum.x + iconSize, iconMinimum.y + iconSize);
        if (g_cutsceneWatchTexture.tex)
        {
            drawList->AddImage(
                (ImTextureID)(uintptr_t)g_cutsceneWatchTexture.tex,
                iconMinimum, iconMaximum);
        }
        else
        {
            drawList->AddCircle(
                ImVec2(iconMinimum.x + iconSize * 0.5f,
                    iconMinimum.y + iconSize * 0.5f),
                iconSize * 0.34f,
                IM_COL32(244, 205, 92, 235),
                24, 2.0f * scale);
        }
    }

    inline void DrawCoopWaitIndicator(IDirect3DDevice9* pDevice)
    {
        if (!g_coopWaitingForPeer.load(std::memory_order_acquire))
            return;

        LoadCoopWaitTexture(pDevice);
        const float scale = g_uiScale;
        const float panelWidth = 445.0f * scale;
        const float panelHeight = 100.0f * scale;
        const float iconSize = 58.0f * scale;
        const float panelTop = 62.0f * scale;
        const float padding = 12.0f * scale;
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 panelMinimum(
            (io.DisplaySize.x - panelWidth) * 0.5f,
            panelTop);
        const ImVec2 panelMaximum(
            panelMinimum.x + panelWidth,
            panelMinimum.y + panelHeight);
        const ImVec2 iconMinimum(
            panelMinimum.x + padding,
            panelMinimum.y + (panelHeight - iconSize) * 0.5f);
        const ImVec2 iconMaximum(
            iconMinimum.x + iconSize,
            iconMinimum.y + iconSize);
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->AddRectFilled(
            panelMinimum, panelMaximum,
            IM_COL32(15, 12, 20, 218),
            9.0f * scale);
        drawList->AddRect(
            panelMinimum, panelMaximum,
            IM_COL32(213, 193, 225, 185),
            9.0f * scale, 0, 1.2f * scale);
        if (g_coopWaitTexture.tex)
        {
            drawList->AddImage(
                (ImTextureID)(uintptr_t)g_coopWaitTexture.tex,
                iconMinimum, iconMaximum);
        }
        else
        {
            drawList->AddCircle(
                ImVec2(iconMinimum.x + iconSize * 0.5f,
                    iconMinimum.y + iconSize * 0.5f),
                iconSize * 0.34f,
                IM_COL32(244, 205, 92, 235),
                28, 2.0f * scale);
        }
        const char* message = "Waiting for the other player...";
        const ImVec2 textSize = ImGui::CalcTextSize(message);
        const ImVec2 textPosition(
            iconMaximum.x + 15.0f * scale,
            panelMinimum.y + 25.0f * scale);
        drawList->AddText(
            textPosition,
            IM_COL32(244, 240, 248, 245),
            message);
        const ImVec2 emergencyPosition(
            textPosition.x,
            textPosition.y + textSize.y + 8.0f * scale);
        const ImVec2 buttonSize(320.0f * scale, 28.0f * scale);
        ImGui::SetNextWindowPos(
            ImVec2(emergencyPosition.x - 6.0f * scale,
                emergencyPosition.y - 5.0f * scale),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(buttonSize.x + 12.0f * scale,
                buttonSize.y + 10.0f * scale),
            ImGuiCond_Always);
        constexpr ImGuiWindowFlags emergencyFlags =
            ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoBackground;
        if (ImGui::Begin(
            "##alicecoop_force_cutscene", nullptr, emergencyFlags))
        {
            constexpr const char* buttonText =
                "Press \"L\"  |  FORCE ACTIVATE FOR BOTH PLAYERS";
            const bool clicked = ImGui::InvisibleButton(
                "##alicecoop_force_cutscene_button", buttonSize);
            const ImVec2 buttonMinimum = ImGui::GetItemRectMin();
            const ImVec2 buttonMaximum = ImGui::GetItemRectMax();
            const bool buttonActive = ImGui::IsItemActive();
            const bool buttonHovered = ImGui::IsItemHovered();
            const ImU32 buttonColour = buttonActive
                ? IM_COL32(255, 224, 92, 255)
                : (buttonHovered
                    ? IM_COL32(255, 235, 122, 255)
                    : IM_COL32(255, 210, 54, 255));
            // The wait panel itself is rendered on ImGui's foreground list.
            // Draw the button there as well so its opaque colours are not
            // composited underneath the panel background.
            ImDrawList* buttonDrawList = ImGui::GetForegroundDrawList();
            buttonDrawList->AddRectFilled(
                buttonMinimum, buttonMaximum, buttonColour,
                5.0f * scale);
            buttonDrawList->AddRect(
                buttonMinimum, buttonMaximum,
                IM_COL32(32, 20, 8, 255),
                5.0f * scale, 0, 2.0f * scale);
            const ImVec2 buttonTextSize =
                ImGui::CalcTextSize(buttonText);
            buttonDrawList->AddText(
                ImVec2(
                    buttonMinimum.x
                        + (buttonSize.x - buttonTextSize.x) * 0.5f,
                    buttonMinimum.y
                        + (buttonSize.y - buttonTextSize.y) * 0.5f),
                IM_COL32(18, 12, 6, 255),
                buttonText);
            if (clicked && IsCoopWaitInteractive())
            {
                g_coopForceCutsceneRequested.store(
                    true, std::memory_order_release);
            }
        }
        ImGui::End();
    }

    inline bool IsSoloMinigameWarningVisible()
    {
        return GetTickCount64()
            < g_soloMinigameWarningUntil.load(
                std::memory_order_acquire);
    }

    inline bool IsCoopJoinTeleportHintVisible()
    {
        return GetTickCount64()
            < g_coopJoinTeleportHintUntil.load(
                std::memory_order_acquire);
    }

    inline void DrawCoopJoinTeleportHint()
    {
        if (!IsCoopJoinTeleportHintVisible())
            return;

        const float scale = g_uiScale;
        const char* heading = "JOIN COMPLETE";
        const char* message =
            "Press \"P\": safe teleport   |   Press \"O\": forced teleport";
        const ImVec2 headingSize = ImGui::CalcTextSize(heading);
        const ImVec2 messageSize = ImGui::CalcTextSize(message);
        const float paddingX = 18.0f * scale;
        const float paddingY = 11.0f * scale;
        const float gap = 5.0f * scale;
        const float panelWidth = (std::max)(headingSize.x, messageSize.x)
            + paddingX * 2.0f;
        const float panelHeight = headingSize.y + messageSize.y + gap
            + paddingY * 2.0f;
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 minimum(
            (io.DisplaySize.x - panelWidth) * 0.5f,
            62.0f * scale);
        const ImVec2 maximum(
            minimum.x + panelWidth,
            minimum.y + panelHeight);
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->AddRectFilled(
            minimum, maximum, IM_COL32(12, 17, 27, 226),
            8.0f * scale);
        drawList->AddRect(
            minimum, maximum, IM_COL32(104, 205, 255, 225),
            8.0f * scale, 0, 1.2f * scale);
        const float textX = minimum.x + paddingX;
        drawList->AddText(
            ImVec2(textX, minimum.y + paddingY),
            IM_COL32(120, 220, 255, 255), heading);
        drawList->AddText(
            ImVec2(textX,
                minimum.y + paddingY + headingSize.y + gap),
            IM_COL32(242, 241, 237, 250), message);
    }

    inline void DrawCoopMainMenuPanel()
    {
        if (!g_coopMenuVisible.load(std::memory_order_acquire))
        {
            g_coopMenuHovered.store(false, std::memory_order_release);
            return;
        }

        std::string peerMap;
        std::string status;
        {
            std::lock_guard<std::mutex> lock(g_coopMenuMutex);
            peerMap = g_coopMenuPeerMap;
            status = g_coopMenuStatus;
        }
        const bool hostRole =
            g_coopMenuHostRole.load(std::memory_order_acquire);
        const bool relayConnected =
            g_coopMenuRelayConnected.load(std::memory_order_acquire);
        const bool peerConnected =
            g_coopMenuPeerConnected.load(std::memory_order_acquire);
        const bool canJoin =
            g_coopMenuCanJoinHost.load(std::memory_order_acquire);
		const bool saveSyncAvailable =
			g_coopSaveSyncAvailable.load(std::memory_order_acquire);
		const bool saveSyncInProgress =
			g_coopSaveSyncInProgress.load(std::memory_order_acquire);
		const int saveSyncProgress =
			g_coopSaveSyncProgress.load(std::memory_order_acquire);
		std::string saveSyncStatus;
		{
			std::lock_guard<std::mutex> lock(g_coopSaveSyncMutex);
			saveSyncStatus = g_coopSaveSyncStatus;
		}

        if (peerMap.size() > 38)
            peerMap = peerMap.substr(0, 35) + "...";
        if (status.size() > 48)
            status = status.substr(0, 45) + "...";

        const float scale = g_uiScale;
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 panelSize(
            352.0f * scale,
            (hostRole ? 130.0f : 216.0f) * scale);
        const ImVec2 panelMinimum(
            18.0f * scale,
            io.DisplaySize.y - panelSize.y - 18.0f * scale);
        const ImVec2 panelMaximum(
            panelMinimum.x + panelSize.x,
            panelMinimum.y + panelSize.y);
        const float left = panelMinimum.x + 16.0f * scale;
        const float line = 22.0f * scale;

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->AddRectFilled(
            panelMinimum, panelMaximum,
            IM_COL32(17, 15, 23, 226), 8.0f * scale);
        drawList->AddRect(
            panelMinimum, panelMaximum,
            IM_COL32(185, 169, 204, 205),
            8.0f * scale, 0, 1.3f * scale);
        drawList->AddText(
            ImVec2(left, panelMinimum.y + 13.0f * scale),
            IM_COL32(250, 235, 255, 255),
            "ALICE COOP");

        const char* roleText = hostRole ? "ROLE: HOST" : "ROLE: CLIENT";
        drawList->AddText(
            ImVec2(left, panelMinimum.y + 13.0f * scale + line),
            hostRole
                ? IM_COL32(255, 191, 91, 255)
                : IM_COL32(111, 196, 255, 255),
            roleText);
        drawList->AddText(
            ImVec2(left, panelMinimum.y + 13.0f * scale + line * 2.0f),
            relayConnected
                ? IM_COL32(118, 237, 151, 255)
                : IM_COL32(255, 206, 94, 255),
            relayConnected
                ? "RELAY: CONNECTED"
                : "RELAY: CONNECTING...");
        const std::string peerText = peerConnected
            ? "PEER: ONLINE  |  " + (
                peerMap.empty() ? std::string("map unknown") : peerMap)
            : "PEER: OFFLINE";
        drawList->AddText(
            ImVec2(left, panelMinimum.y + 13.0f * scale + line * 3.0f),
            peerConnected
                ? IM_COL32(118, 237, 151, 255)
                : IM_COL32(187, 180, 194, 255),
            peerText.c_str());

        if (!hostRole)
        {
            const ImVec2 buttonMinimum(
                left,
                panelMaximum.y - 83.0f * scale);
            const ImVec2 buttonSize(
                panelSize.x - 32.0f * scale,
                29.0f * scale);
            const ImVec2 buttonMaximum(
                buttonMinimum.x + buttonSize.x,
                buttonMinimum.y + buttonSize.y);

            ImGui::SetNextWindowPos(
                panelMinimum, ImGuiCond_Always);
            ImGui::SetNextWindowSize(
                panelSize, ImGuiCond_Always);
            constexpr ImGuiWindowFlags panelFlags =
                ImGuiWindowFlags_NoDecoration
                | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoBackground;
            if (ImGui::Begin(
                "##alicecoop_main_menu_panel", nullptr, panelFlags))
            {
                g_coopMenuHovered.store(
                    ImGui::IsWindowHovered(
                        ImGuiHoveredFlags_AllowWhenBlockedByActiveItem),
                    std::memory_order_release);
                ImGui::SetCursorScreenPos(buttonMinimum);
                const bool clicked = ImGui::InvisibleButton(
                    "##alicecoop_join_host", buttonSize);
                const bool active = ImGui::IsItemActive();
                const bool hovered = ImGui::IsItemHovered();
                const ImU32 fill = !canJoin
                    ? IM_COL32(57, 53, 63, 245)
                    : (active
                        ? IM_COL32(255, 218, 94, 255)
                        : (hovered
                            ? IM_COL32(255, 232, 127, 255)
                            : IM_COL32(247, 204, 70, 255)));
                drawList->AddRectFilled(
                    buttonMinimum, buttonMaximum,
                    fill, 5.0f * scale);
                drawList->AddRect(
                    buttonMinimum, buttonMaximum,
                    IM_COL32(29, 22, 12, 255),
                    5.0f * scale, 0, 1.5f * scale);
                const char* label = canJoin
                    ? "Press \"L\"  |  JOIN HOST CHECKPOINT"
                    : (status.empty()
                        ? "WAITING FOR HOST..."
                        : status.c_str());
                const ImVec2 textSize =
                    ImGui::CalcTextSize(label);
                drawList->AddText(
                    ImVec2(
                        buttonMinimum.x
                            + (buttonSize.x - textSize.x) * 0.5f,
                        buttonMinimum.y
                            + (buttonSize.y - textSize.y) * 0.5f),
                    canJoin
                        ? IM_COL32(22, 15, 8, 255)
                        : IM_COL32(194, 188, 201, 255),
                    label);
                if (clicked && canJoin)
                {
                    g_coopJoinHostRequested.store(
                        true, std::memory_order_release);
                }

				const ImVec2 syncMinimum(
					left, panelMaximum.y - 45.0f * scale);
				const ImVec2 syncMaximum(
					syncMinimum.x + buttonSize.x,
					syncMinimum.y + buttonSize.y);
				ImGui::SetCursorScreenPos(syncMinimum);
				const bool syncClicked = ImGui::InvisibleButton(
					"##alicecoop_sync_host_save", buttonSize);
				const bool syncActive = ImGui::IsItemActive();
				const bool syncHovered = ImGui::IsItemHovered();
				const bool syncEnabled = saveSyncAvailable
					&& !saveSyncInProgress;
				const ImU32 syncFill = !syncEnabled
					? IM_COL32(57, 53, 63, 245)
					: (syncActive
						? IM_COL32(255, 134, 108, 255)
						: (syncHovered
							? IM_COL32(255, 159, 129, 255)
							: IM_COL32(229, 112, 91, 255)));
				drawList->AddRectFilled(
					syncMinimum, syncMaximum,
					syncFill, 5.0f * scale);
				drawList->AddRect(
					syncMinimum, syncMaximum,
					IM_COL32(32, 18, 15, 255),
					5.0f * scale, 0, 1.5f * scale);
				std::string syncLabel;
				if (saveSyncInProgress)
					syncLabel = "RECEIVING HOST SAVE... "
						+ std::to_string(saveSyncProgress) + "%";
				else if (!saveSyncStatus.empty())
					syncLabel = saveSyncStatus;
				else if (syncEnabled)
					syncLabel = "Press \"K\" | SYNC HOST SAVE (OVERWRITES PROFILE)";
				else
					syncLabel = "HOST SAVE SYNC UNAVAILABLE";
				if (syncLabel.size() > 46)
					syncLabel = syncLabel.substr(0, 43) + "...";
				const ImVec2 syncTextSize =
					ImGui::CalcTextSize(syncLabel.c_str());
				drawList->AddText(
					ImVec2(
						syncMinimum.x
							+ (buttonSize.x - syncTextSize.x) * 0.5f,
						syncMinimum.y
							+ (buttonSize.y - syncTextSize.y) * 0.5f),
					syncEnabled
						? IM_COL32(27, 13, 10, 255)
						: IM_COL32(194, 188, 201, 255),
					syncLabel.c_str());
				if (syncClicked && syncEnabled)
				{
					g_coopSaveSyncWarningVisible.store(
						true, std::memory_order_release);
				}
            }
            ImGui::End();
        }
        else
        {
            g_coopMenuHovered.store(false, std::memory_order_release);
        }
    }

	inline void DrawCoopSaveSyncWarning()
	{
		if (!g_coopSaveSyncWarningVisible.load(
				std::memory_order_acquire))
			return;

		const float scale = g_uiScale;
		const ImGuiIO& io = ImGui::GetIO();
		std::string profileName;
		{
			std::lock_guard<std::mutex> lock(g_coopSaveSyncMutex);
			profileName = g_coopSaveSyncProfileName;
		}
		if (profileName.size() > 40)
			profileName = profileName.substr(0, 37) + "...";
		const ImVec2 size(690.0f * scale, 232.0f * scale);
		const ImVec2 minimum(
			(io.DisplaySize.x - size.x) * 0.5f,
			(io.DisplaySize.y - size.y) * 0.5f);
		const ImVec2 maximum(minimum.x + size.x, minimum.y + size.y);
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		drawList->AddRectFilled(
			minimum, maximum, IM_COL32(24, 12, 14, 246),
			10.0f * scale);
		drawList->AddRect(
			minimum, maximum, IM_COL32(255, 92, 92, 255),
			10.0f * scale, 0, 2.0f * scale);
		const float left = minimum.x + 24.0f * scale;
		float top = minimum.y + 20.0f * scale;
		drawList->AddText(
			ImVec2(left, top), IM_COL32(255, 91, 91, 255),
			"WARNING: HOST SAVE SYNCHRONIZATION");
		top += 34.0f * scale;
		drawList->AddText(
			ImVec2(left, top), IM_COL32(255, 224, 94, 255),
			("SELECTED CLIENT PROFILE: \"" + profileName
				+ "\" WILL BE OVERWRITTEN.").c_str());
		top += 27.0f * scale;
		drawList->AddText(
			ImVec2(left, top), IM_COL32(255, 240, 240, 255),
			"The HOST's complete saved progress will replace this profile.");
		top += 25.0f * scale;
		drawList->AddText(
			ImVec2(left, top), IM_COL32(255, 194, 174, 255),
			"The client's checkpoints, collectibles, statistics and upgrades will be lost.");
		top += 25.0f * scale;
		drawList->AddText(
			ImVec2(left, top), IM_COL32(213, 205, 216, 255),
			"Use a new empty client profile. Both files are verified before anything is replaced.");
		top += 39.0f * scale;
		drawList->AddText(
			ImVec2(left, top), IM_COL32(255, 224, 94, 255),
			"Press \"Y\" to receive and overwrite   |   Press \"N\" to cancel");
	}

	inline void DrawCoopPauseMenuPanel()
	{
		if (!g_coopPauseVisible.load(std::memory_order_acquire))
		{
			g_coopPauseHovered.store(false, std::memory_order_release);
			return;
		}

		const float scale = g_uiScale;
		const ImGuiIO& io = ImGui::GetIO();
		const ImVec2 panelSize(500.0f * scale, 205.0f * scale);
		const ImVec2 panelMinimum(
			18.0f * scale,
			io.DisplaySize.y - panelSize.y - 18.0f * scale);
		const ImVec2 panelMaximum(
			panelMinimum.x + panelSize.x,
			panelMinimum.y + panelSize.y);
		const float left = panelMinimum.x + 16.0f * scale;
		const float line = 23.0f * scale;
		const bool hostRole =
			g_coopPauseHostRole.load(std::memory_order_acquire);
		const bool peerConnected =
			g_coopPausePeerConnected.load(std::memory_order_acquire);
		bool trails = g_coopMovementTrailsEnabled.load(
			std::memory_order_acquire);

		// Use the foreground list just like the proven main-menu panel. A normal
		// ImGui window can be occluded by Alice's Scaleform pause movie in DX9.
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		drawList->AddRectFilled(
			panelMinimum, panelMaximum,
			IM_COL32(17, 15, 23, 240), 8.0f * scale);
		drawList->AddRect(
			panelMinimum, panelMaximum,
			IM_COL32(185, 169, 204, 225),
			8.0f * scale, 0, 1.3f * scale);
		drawList->AddText(
			ImVec2(left, panelMinimum.y + 13.0f * scale),
			IM_COL32(250, 235, 255, 255), "ALICE COOP");
		const std::string stateText = std::string("ROLE: ")
			+ (hostRole ? "HOST" : "CLIENT")
			+ "  |  PEER: " + (peerConnected ? "ONLINE" : "OFFLINE");
		drawList->AddText(
			ImVec2(left, panelMinimum.y + 13.0f * scale + line),
			peerConnected
				? IM_COL32(118, 237, 151, 255)
				: IM_COL32(205, 193, 211, 255),
			stateText.c_str());
		drawList->AddText(
			ImVec2(left, panelMinimum.y + 13.0f * scale + line * 2.0f),
			IM_COL32(230, 225, 234, 255),
			"P - safe teleport   |   O - forced teleport");
		drawList->AddText(
			ImVec2(left, panelMinimum.y + 13.0f * scale + line * 3.0f),
			IM_COL32(230, 225, 234, 255),
			"L - visible JOIN / FORCE CUTSCENE action");

		const ImVec2 checkMinimum(
			left, panelMinimum.y + 13.0f * scale + line * 4.25f);
		const float boxSize = 17.0f * scale;
		const ImVec2 checkMaximum(
			checkMinimum.x + boxSize, checkMinimum.y + boxSize);
		g_coopPauseHovered.store(false, std::memory_order_release);

		drawList->AddRectFilled(
			checkMinimum, checkMaximum,
			IM_COL32(48, 43, 56, 255),
			3.0f * scale);
		drawList->AddRect(
			checkMinimum, checkMaximum,
			IM_COL32(203, 186, 219, 255), 3.0f * scale);
		if (trails)
		{
			drawList->AddRectFilled(
				ImVec2(checkMinimum.x + 4.0f * scale,
					checkMinimum.y + 4.0f * scale),
				ImVec2(checkMaximum.x - 4.0f * scale,
					checkMaximum.y - 4.0f * scale),
				IM_COL32(255, 215, 93, 255), 2.0f * scale);
		}
		drawList->AddText(
			ImVec2(checkMaximum.x + 9.0f * scale, checkMinimum.y),
			IM_COL32(240, 232, 244, 255),
			"Keep proxy movement trails (double jump / glide)");
		drawList->AddText(
			ImVec2(left, checkMinimum.y + 27.0f * scale),
			IM_COL32(222, 207, 230, 255),
			"Press T while this pause menu is open to toggle");
		drawList->AddText(
			ImVec2(left, checkMinimum.y + 51.0f * scale),
			IM_COL32(174, 165, 183, 255),
			"Profile sync: return to the main menu and press K.");
	}

    inline void DrawSoloMinigameWarning(
        IDirect3DDevice9* pDevice)
    {
        if (!IsSoloMinigameWarningVisible())
            return;

        LoadSoloMinigameTexture(pDevice);
        const float scale = g_uiScale;
        const char* heading = "SOLO MINIGAME";
        const char* message =
            "This section is played separately - share your score with your friend!";
        const ImVec2 headingSize = ImGui::CalcTextSize(heading);
        const ImVec2 messageSize = ImGui::CalcTextSize(message);
        const float iconSize = 72.0f * scale;
        const float iconGap = 14.0f * scale;
        const float paddingX = 18.0f * scale;
        const float paddingY = 13.0f * scale;
        const float gap = 6.0f * scale;
        const float textWidth =
            (std::max)(headingSize.x, messageSize.x);
        const float textHeight =
            headingSize.y + gap + messageSize.y;
        const float panelWidth =
            iconSize + iconGap + textWidth
                + paddingX * 2.0f;
        const float panelHeight =
            (std::max)(iconSize, textHeight)
                + paddingY * 2.0f;
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 minimum(
            (io.DisplaySize.x - panelWidth) * 0.5f,
            62.0f * scale);
        const ImVec2 maximum(
            minimum.x + panelWidth,
            minimum.y + panelHeight);
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->AddRectFilled(
            minimum, maximum,
            IM_COL32(12, 17, 27, 226),
            8.0f * scale);
        drawList->AddRect(
            minimum, maximum,
            IM_COL32(235, 188, 82, 225),
            8.0f * scale, 0, 1.2f * scale);
        const ImVec2 iconMinimum(
            minimum.x + paddingX,
            minimum.y + (panelHeight - iconSize) * 0.5f);
        const ImVec2 iconMaximum(
            iconMinimum.x + iconSize,
            iconMinimum.y + iconSize);
        if (g_soloMinigameTexture.tex)
        {
            drawList->AddImage(
                (ImTextureID)(uintptr_t)
                    g_soloMinigameTexture.tex,
                iconMinimum, iconMaximum);
        }
        else
        {
            drawList->AddCircle(
                ImVec2(
                    iconMinimum.x + iconSize * 0.5f,
                    iconMinimum.y + iconSize * 0.5f),
                iconSize * 0.35f,
                IM_COL32(255, 210, 105, 255),
                24, 2.0f * scale);
        }
        const float textX =
            iconMaximum.x + iconGap;
        const float textY =
            minimum.y + (panelHeight - textHeight) * 0.5f;
        drawList->AddText(
            ImVec2(textX, textY),
            IM_COL32(255, 210, 105, 255),
            heading);
        drawList->AddText(
            ImVec2(
                textX, textY + headingSize.y + gap),
            IM_COL32(242, 241, 237, 250),
            message);
    }

    inline void DrawAchievementsWindow(IDirect3DDevice9* pDevice)
    {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 disp = io.DisplaySize;

        ImVec2 winSize(disp.x * 0.6f, disp.y * 0.8f);
        const float availableWidth = (std::max)(1.0f, disp.x - 40.0f);
        const float availableHeight = (std::max)(1.0f, disp.y - 40.0f);
        winSize.x = (std::min)(availableWidth,
            (std::max)((std::min)(700.0f, availableWidth), winSize.x));
        winSize.y = (std::min)(availableHeight,
            (std::max)((std::min)(500.0f, availableHeight), winSize.y));

        bool resChanged = ((int)disp.x != g_lastDispW) || ((int)disp.y != g_lastDispH);
        g_lastDispW = (int)disp.x;
        g_lastDispH = (int)disp.y;

        ImGuiCond sizeCond = resChanged ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        ImGui::SetNextWindowSize(winSize, sizeCond);
        ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.5f), sizeCond, ImVec2(0.5f, 0.5f));

        float axis = GetControllerScrollAxis();
        float wheel = ImGui::GetIO().MouseWheel;

        if (g_restoreScroll)
        {
            ImGui::SetNextWindowScroll(ImVec2(-1.0f, g_savedScrollY));
        }

        if (ImGui::Begin("Achievements", &g_visible))
        {
            static bool s_secretPrev = false;
            bool secretNow = g_padConnected && (g_padState.Gamepad.wButtons & XINPUT_GAMEPAD_A);

            if (secretNow && !s_secretPrev)
            {
                g_showSecrets = !g_showSecrets;
            }

            s_secretPrev = secretNow;

            if (g_restoreScroll)
            {
                float wanted = g_savedScrollY;
                float maxY = ImGui::GetScrollMaxY();
                if (wanted > maxY) wanted = maxY;

                float diff = ImGui::GetScrollY() - wanted;
                if (diff < 0.0f) diff = -diff;

                if (diff < 0.5f || axis > 0.5f || axis < -0.5f || wheel != 0.0f || ++g_restoreScrollFrames > 10)
                {
                    g_restoreScroll = false;
                    g_restoreScrollFrames = 0;
                }
            }
            else if (axis != 0.0f)
            {
                float delta = axis * 1500.0f * g_uiScale * ImGui::GetIO().DeltaTime;
                ImGui::SetScrollY(ImGui::GetScrollY() - delta);
            }

            ImGui::Checkbox("Show secret achievements", &g_showSecrets);
            ImGui::Separator();

            const float iconSize = 64.0f * g_uiScale;

            std::vector<bool> unlockedSnapshot;
            std::vector<int> currentSnapshot;
            std::unordered_map<int, int> maxSnapshot;
            {
                std::lock_guard<std::recursive_mutex> lock(g_stateMutex);
                unlockedSnapshot = g_unlocked;
                currentSnapshot = g_current;
                maxSnapshot = g_maxOverride;
            }

            for (size_t i = 0; i < g_achievements.size(); i++)
            {
                ImGui::PushID((int)i);

                const Texture& t = g_achievements[i];
                bool unlocked = (i < unlockedSnapshot.size()) ? unlockedSnapshot[i] : false;
                int maxv = AchievementMaxFrom(maxSnapshot, (int)i);
                int cur = (i < currentSnapshot.size()) ? currentSnapshot[i] : 0;

                bool reveal = !IsSecret((int)i) || g_showSecrets || unlocked;

                ImVec4 tint = unlocked ? ImVec4(1, 1, 1, 1) : ImVec4(0.35f, 0.35f, 0.35f, 1.0f);

                if (reveal && t.tex)
                {
                    ImGui::ImageWithBg((ImTextureID)(uintptr_t)t.tex, ImVec2(iconSize, iconSize), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tint);
                }
                else
                {
                    // Hidden placeholder box with a centered "?"
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImVec2 p2(p.x + iconSize, p.y + iconSize);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(p, p2, IM_COL32(90, 90, 90, 255), 4.0f);
                    dl->AddRectFilled(ImVec2(p.x + 1, p.y + 1), ImVec2(p2.x - 1, p2.y - 1), IM_COL32(35, 35, 35, 255), 4.0f);
                    ImVec2 ts = ImGui::CalcTextSize("?");
                    dl->AddText(ImVec2(p.x + (iconSize - ts.x) * 0.5f, p.y + (iconSize - ts.y) * 0.5f), IM_COL32(170, 170, 170, 255), "?");
                    ImGui::Dummy(ImVec2(iconSize, iconSize));
                }

                ImGui::SameLine();
                ImGui::BeginGroup();

                const char* name = "???";
                const char* desc = "Hidden achievement";
                if (reveal)
                {
                    name = (i < g_text.size()) ? g_text[i].name.c_str() : "???";
                    desc = (i < g_text.size()) ? g_text[i].desc.c_str() : "";
                }

                ImVec4 nameCol = unlocked ? ImVec4(1.0f, 0.84f, 0.40f, 1.0f) : ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
                ImGui::TextColored(nameCol, "%s", name);

                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
                ImGui::TextWrapped("%s", desc);
                ImGui::PopTextWrapPos();

                char overlay[32];
                float barFrac;
                if (!reveal)
                {
                    // Hidden achievement: don't reveal any progress
                    barFrac = 0.0f;
                    snprintf(overlay, sizeof(overlay), "???");
                }
                else
                {
                    // Counter-backed achievements show their stored value; binary ones use the unlock flag
                    bool hasCounter = maxv > 1;
                    int displayCur = hasCounter ? cur : (unlocked ? 1 : 0);
                    barFrac = maxv > 0 ? (float)displayCur / (float)maxv : (unlocked ? 1.0f : 0.0f);
                    if (barFrac > 1.0f) barFrac = 1.0f;
                    snprintf(overlay, sizeof(overlay), "%d/%d", displayCur, maxv);
                }

                // Draw the bar without its built-in overlay, then place the counter at the top-left
                ImVec2 barPos = ImGui::GetCursorScreenPos();
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.22f, 0.60f, 0.85f, 1.0f));
                ImGui::ProgressBar(barFrac, ImVec2(-FLT_MIN, 0.0f), "");
                ImGui::PopStyleColor();

                ImVec2 ots = ImGui::CalcTextSize(overlay);
                float barH = ImGui::GetFrameHeight();
                ImGui::GetWindowDrawList()->AddText(ImVec2(barPos.x + 6.0f * g_uiScale, barPos.y + (barH - ots.y) * 0.5f), IM_COL32(255, 255, 255, 255), overlay);

                ImGui::EndGroup();
                ImGui::PopID();

                ImGui::Separator();
            }

            if (!g_restoreScroll)
            {
                g_savedScrollY = ImGui::GetScrollY();
            }
        }

        ImGui::End();
    }

    inline void DrawUnlockToast()
    {
        std::lock_guard<std::mutex> lock(g_toastMutex);

        if (g_toasts.empty()) return;

        const float kSlideMs = 350.0f;
        const float kHoldMs = 5500.0f;
        const float kTotalMs = 6200.0f;

        unsigned long long now = GetTickCount64();

        for (size_t i = 0; i < g_toasts.size(); )
        {
            if ((float)(now - g_toasts[i].start) >= kTotalMs)
            {
                g_toasts.erase(g_toasts.begin() + i);
            }
            else
            {
                i++;
            }
        }

        if (g_toasts.empty()) return;

        ImGuiIO& io = ImGui::GetIO();
        float scale = g_uiScale;
        float toastW = 420.0f * scale;
        float margin = 20.0f * scale;
        float spacing = 8.0f * scale;
        float onX = io.DisplaySize.x - toastW - margin;
        float offX = io.DisplaySize.x + 10.0f;

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs;

        float targetY = margin;
        for (ToastItem& it : g_toasts)
        {
            float t = (float)(now - it.start);

            float x, alpha;
            if (t < kSlideMs)
            {
                float k = t / kSlideMs;
                k = 1.0f - (1.0f - k) * (1.0f - k); // ease-out
                x = offX + (onX - offX) * k;
                alpha = k;
            }
            else if (t < kHoldMs)
            {
                x = onX;
                alpha = 1.0f;
            }
            else
            {
                float k = (t - kHoldMs) / (kTotalMs - kHoldMs);
                x = onX + (offX - onX) * (k * k); // ease-in
                alpha = 1.0f - k;
            }

            // Ease the vertical slot so cards slide up when one above expires
            if (!it.placed)
            {
                it.animY = targetY; it.placed = true;
            }
            else
            {
                it.animY += (targetY - it.animY) * 0.25f;
            }

            ImGui::SetNextWindowPos(ImVec2(x, it.animY));
            ImGui::SetNextWindowSize(ImVec2(toastW, 0.0f));

            char wname[32];
            snprintf(wname, sizeof(wname), "##toast_%u", it.uid);

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
            if (ImGui::Begin(wname, nullptr, flags))
            {
                int id = it.achvId;
                float iconSize = 64.0f * scale;

                if (id >= 0 && id < (int)g_achievements.size() && g_achievements[id].tex)
                {
                    ImGui::ImageWithBg((ImTextureID)(uintptr_t)g_achievements[id].tex, ImVec2(iconSize, iconSize), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1));
                    ImGui::SameLine();
                }

                ImGui::BeginGroup();
                ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.40f, 1.0f), "Achievement Unlocked");
                const char* name = (id >= 0 && id < (int)g_text.size()) ? g_text[id].name.c_str() : "";
                const char* desc = (id >= 0 && id < (int)g_text.size()) ? g_text[id].desc.c_str() : "";
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
                ImGui::TextWrapped("%s", name);
                if (desc[0])
                {
                    ImGui::TextColored(ImVec4(0.78f, 0.78f, 0.78f, 1.0f), "%s", desc);
                }

                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
            }

            it.height = ImGui::GetWindowHeight();
            ImGui::End();
            ImGui::PopStyleVar();

            targetY += (it.height > 1.0f ? it.height : 90.0f * scale) + spacing;
        }
    }

    // The Win32 backend forces a hardware cursor while the overlay is up, which is wrong when playing
    // on a controller. Keep it hidden unless the mouse moved or clicked in the last few seconds
    inline void UpdateOverlayCursor()
    {
        ImGuiIO& io = ImGui::GetIO();

        bool mouseActive = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.MouseWheel != 0.0f || ImGui::IsAnyMouseDown();

        if (mouseActive)
            g_lastMouseInputTick = GetTickCount64();

        if (!g_lastMouseInputTick || GetTickCount64() - g_lastMouseInputTick > kCursorIdleHideMs)
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }

    inline void Render(IDirect3DDevice9* pDevice)
    {
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();

        ImGuiIO& io = ImGui::GetIO();

        float clientW = io.DisplaySize.x;
        float clientH = io.DisplaySize.y;

        float layoutW = clientW, layoutH = clientH;
        QueryOverlayLayoutSize(pDevice, layoutW, layoutH);

        io.DisplaySize = ImVec2(layoutW, layoutH);
        g_pendingScale = ComputeUiScaleFromHeight(layoutH);

        if (clientW > 0.0f && clientH > 0.0f
            && (layoutW != clientW || layoutH != clientH))
        {
            POINT pt;
            if (GetCursorPos(&pt) && ScreenToClient(g_hWnd, &pt))
            {
                io.AddMousePosEvent(
                    static_cast<float>(pt.x) * (layoutW / clientW),
                    static_cast<float>(pt.y) * (layoutH / clientH));
            }
        }

        ImGui::NewFrame();

        if (ImDrawCallback setNearest = ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest)
        {
            ImGui::GetBackgroundDrawList()->AddCallback(setNearest, nullptr);
        }

        DrawPeerCutsceneIndicator(pDevice);
        DrawCoopWaitIndicator(pDevice);
        DrawSoloMinigameWarning(pDevice);
        DrawCoopMainMenuPanel();
		DrawCoopPauseMenuPanel();
		DrawCoopSaveSyncWarning();

        if (g_visible || IsCoopWaitInteractive()
            || g_coopMenuHovered.load(std::memory_order_acquire)
			|| g_coopPauseHovered.load(std::memory_order_acquire)
			|| g_coopSaveSyncWarningVisible.load(std::memory_order_acquire))
        {
            UpdateOverlayCursor();
            if (g_visible)
                DrawAchievementsWindow(pDevice);
        }
        else
        {
            g_lastMouseInputTick = 0;
        }

        DrawUnlockToast();

        ImGui::EndFrame();
        ImGui::Render();

        pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
        pDevice->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
        pDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        pDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        pDevice->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }

    inline LRESULT CALLBACK WndProc_Hook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        const bool coopMenuVisible =
            g_coopMenuVisible.load(std::memory_order_acquire);
        const bool coopMenuHovered =
            g_coopMenuHovered.load(std::memory_order_acquire);
		const bool coopPauseVisible =
			g_coopPauseVisible.load(std::memory_order_acquire);
		const bool coopPauseHovered =
			g_coopPauseHovered.load(std::memory_order_acquire);
        const bool captureInput =
            g_visible || IsCoopWaitInteractive()
			|| coopMenuHovered || coopPauseHovered
			|| g_coopSaveSyncWarningVisible.load(
				std::memory_order_acquire);
        if (g_imguiInitialized
            && (captureInput || coopMenuVisible || coopPauseVisible)
            && !g_deviceLost)
        {
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

            if (kBlockGameInputWhileVisible && captureInput)
            {
                switch (msg)
                {
                    case WM_KEYUP: case WM_SYSKEYUP:
                    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
                        break;

                    case WM_SYSKEYDOWN:
                        if (wParam == VK_F4 || wParam == VK_TAB) break;
                        return TRUE;

                    case WM_MOUSEMOVE:
                    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
                    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
                    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
                    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
                    case WM_KEYDOWN: case WM_CHAR:
                        return TRUE;

                    default: break;
                }
            }
        }

        return CallWindowProc(g_oWndProc, hWnd, msg, wParam, lParam);
    }

    // Device
    inline bool InstallHooks(IDirect3DDevice9* pDevice)
    {
        if (g_hooksInstalled || !pDevice)
            return g_hooksInstalled;

        D3DDEVICE_CREATION_PARAMETERS cp{};
        if (FAILED(pDevice->GetCreationParameters(&cp)) || !cp.hFocusWindow)
        {
            return false;
        }

        g_pDevice = pDevice;
        g_hWnd = cp.hFocusWindow;

        g_oWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc_Hook)));

        g_hooksInstalled = true;
        return true;
    }

    inline bool PollDeviceHealth(IDirect3DDevice9* pDevice)
    {
        if (!pDevice) return false;

        HRESULT coop = pDevice->TestCooperativeLevel();
        if (coop != D3D_OK)
        {
            if (g_imguiInitialized && !g_deviceLost)
            {
                ImGui_ImplDX9_InvalidateDeviceObjects();
                g_deviceLost = true;
            }

            return false;
        }

        if (g_imguiInitialized && (g_deviceLost || g_inReset))
        {
            ImGui_ImplDX9_CreateDeviceObjects();
            g_deviceLost = false;
            g_inReset = false;
        }

        return true;
    }

    // Public API
    inline void Init(uintptr_t devicePtrAddr)
    {
        g_devicePtrAddr = devicePtrAddr;
    }

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
			g_coopSaveSyncWarningVisible.store(
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

    inline void FeedControllerState(const XINPUT_STATE& state, bool connected)
    {
        g_padState = state;
        g_padConnected = connected;
    }

    inline bool WantCaptureController()
    {
        return kBlockGameInputWhileVisible && g_imguiInitialized && g_visible && !g_deviceLost;
    }

    inline void OnPresent(IDirect3DDevice9* pDevice)
    {
        if (!pDevice)
            return;

        if (!g_hooksInstalled)
        {
            InstallHooks(pDevice);
        }

        if (!PollDeviceHealth(pDevice))
        {
            return;
        }

        // Query the new backbuffer before comparing scales. This prevents a
        // frame rendered with the previous resolution's layout after Reset().
        RefreshRenderMetrics(pDevice);

        if (g_imguiInitialized && pDevice != g_pInitializedDevice)
        {
            ReleaseAchievements();
            ReleaseCutsceneWatchTexture();
            ShutdownImGui();
        }

        else if (g_imguiInitialized && g_pendingScale > 0.0f && ((g_pendingScale > g_uiScale ? g_pendingScale - g_uiScale : g_uiScale - g_pendingScale) > 0.01f))
        {
            ReleaseAchievements(); // resolution changed: reload icons and re-bake the font
            ReleaseCutsceneWatchTexture();
            ShutdownImGui();
        }

        if (!g_imguiInitialized)
        {
            InitImGui(pDevice);
        }

        if (g_imguiInitialized && AchievementSupport)
        {
            LoadAchievements(pDevice);
            LoadText();
        }

        if (!g_imguiInitialized
            || (!g_visible && g_toasts.empty()
                && !g_peerWatchingCutscene.load(
                    std::memory_order_acquire)
                && !g_coopWaitingForPeer.load(
                    std::memory_order_acquire)
                && !IsSoloMinigameWarningVisible()
                && !g_coopMenuVisible.load(
                    std::memory_order_acquire)
				&& !g_coopPauseVisible.load(
					std::memory_order_acquire)
				&& !g_coopSaveSyncWarningVisible.load(
					std::memory_order_acquire)))
            return;

        ImGui::GetIO().MouseDrawCursor =
            g_visible || IsCoopWaitInteractive()
			|| g_coopPauseVisible.load(std::memory_order_acquire)
			|| g_coopSaveSyncWarningVisible.load(
				std::memory_order_acquire);

        IDirect3DStateBlock9* stateBlock = nullptr;
        if (FAILED(pDevice->CreateStateBlock(D3DSBT_ALL, &stateBlock)))
        {
            stateBlock = nullptr;
        }

        if (stateBlock)
        {
            stateBlock->Capture();
        }

        IDirect3DSurface9* prevRT = nullptr;
        IDirect3DSurface9* bb = nullptr;

        D3DVIEWPORT9 prevViewport{};
        RECT prevScissor{};
        const bool haveViewport = SUCCEEDED(pDevice->GetViewport(&prevViewport));
        const bool haveScissor = SUCCEEDED(pDevice->GetScissorRect(&prevScissor));

        if (SUCCEEDED(pDevice->GetRenderTarget(0, &prevRT)) && prevRT && SUCCEEDED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb && SUCCEEDED(pDevice->SetRenderTarget(0, bb)))
        {
            DWORD prevColorWrite = 0;
            pDevice->GetRenderState(D3DRS_COLORWRITEENABLE, &prevColorWrite);
            pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);

            HRESULT sceneHr = pDevice->BeginScene(); // if already open, skip our EndScene below
            Render(pDevice);

            if (SUCCEEDED(sceneHr))
            {
                pDevice->EndScene();
            }

            pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, prevColorWrite);
            pDevice->SetRenderTarget(0, prevRT);

            if (haveViewport)
            {
                pDevice->SetViewport(&prevViewport);
            }

            if (haveScissor)
            {
                pDevice->SetScissorRect(&prevScissor);
            }
        }

        if (stateBlock)
        {
            stateBlock->Apply();
            stateBlock->Release();
        }

        if (bb)
        {
            bb->Release();
        }

        if (prevRT)
        {
            prevRT->Release();
        }
    }

    inline void OnPresent()
    {
        OnPresent(GetDevice());
    }

    inline void OnDeviceLost()
    {
        g_inReset = true;
        if (g_imguiInitialized)
        {
            ImGui_ImplDX9_InvalidateDeviceObjects();
        }
    }

    inline void OnDeviceReset(IDirect3DDevice9* pDevice)
    {
        if (g_imguiInitialized)
        {
            ImGui_ImplDX9_CreateDeviceObjects();
        }

        g_inReset = false;
    }

    inline void OnDeviceReset()
    {
        OnDeviceReset(GetDevice());
    }

    inline void Toggle()
    {
        g_visible = !g_visible;

        if (g_visible)
        {
            g_restoreScroll = true;
            g_restoreScrollFrames = 0;
        }
    }

    inline void Update(IDirect3DDevice9* pDevice)
    {
        if (!g_hooksInstalled)
        {
            InstallHooks(pDevice);
        }

        PollDeviceHealth(pDevice);

        PollCoopContextShortcut();

        if (GetAsyncKeyState(VK_HOME) & 1)
        {
            Toggle();
        }
    }
}
