#include "Common.hpp"
#include "Features.hpp"

#include <cstdlib>
#include <cwchar>
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

safetyhook::InlineHook PlayerControllerConsoleCommand;
safetyhook::InlineHook GameConsoleCommand;
static safetyhook::InlineHook MenuCursorRead;
static safetyhook::MidHook GetProfileName{};
static safetyhook::MidHook PersistentDataLoaded{};
static safetyhook::MidHook GetGameLanguage{};
static safetyhook::MidHook ActorConsoleCommand{};

static std::wstring g_profileName;

// <docPath>\AliceGame\CheckPoint\<profile> = the selected user's checkpoint folder.
static std::filesystem::path ProfileSavePath()
{
	if (g_profileName.empty() || g_docPath.empty())
		return {};

	return std::filesystem::path(g_docPath) / L"AliceGame" / L"CheckPoint" / g_profileName;
}

// Marks every achievement already satisfied by the loaded save as unlocked, with no toasts.
// Runs once per profile, when that profile has no Achievements.txt yet
static void SyncAchievementsFromSave(AAlicePlayerController* pc, AAliceGameInfo* gi)
{
	auto grant = [](int id)
	{
		AchievementOverlay::MarkUnlockedSilent(id);
	};

	const bool gameComplete = (gi->CompleteGameOnAnyDifficult != 0) || (gi->ChapterComplete[5] != 0);
	const bool ch1 = gameComplete || (gi->ChapterComplete[0] != 0);
	const bool ch2 = gameComplete || (gi->ChapterComplete[1] != 0);
	const bool ch3 = gameComplete || (gi->ChapterComplete[2] != 0);
	const bool ch4 = gameComplete || (gi->ChapterComplete[3] != 0);
	const bool ch5 = gameComplete || (gi->ChapterComplete[4] != 0);

	// Back to Wonderland, Vale of Tears, Chapter 1
	if (ch1)
	{
		grant(1);
		grant(2);
		grant(3);
	}
	if (ch2) grant(4);
	if (ch3) grant(5);
	if (ch4) grant(6);
	if (ch5) grant(7);

	if (gameComplete)
		grant(8);

	if (pc->WeaponLevel[0] >= 1) grant(13); // Vorpal Blade
	if (pc->WeaponLevel[1] >= 1) grant(15); // Hobby Horse
	if (pc->WeaponLevel[2] >= 1) grant(16); // Teapot Cannon
	if (pc->WeaponLevel[3] >= 1) grant(14); // Pepper Grinder

	bool anyUpgraded = false, anyMaxed = false;
	for (int i = 0; i < 4; i++)
	{
		if (pc->WeaponLevel[i] >= 2) anyUpgraded = true;
		if (pc->WeaponLevel[i] == 4) anyMaxed = true;
	}
	if (anyUpgraded) grant(17);
	if (anyMaxed) grant(18);

	// Counter trophies already at or over their in-script threshold
	int radulaRooms = 0;
	for (int i = 0; i < 16; i++)
	{
		radulaRooms += pc->CaveCompleted[i];
	}
	if (radulaRooms >= 16) grant(21);

	int memories = 0;
	for (int i = 0; i < 5; i++)
	{
		memories += pc->MemoryCompleted[i];
	}
	memories += pc->MemoryFragment.size();
	if (memories >= 94) grant(23);

	const int fragmentCount = pc->MemoryFragment.size();
	int familyMemories = 0;
	for (int i = 0; i < fragmentCount; i++)
	{
		const wchar_t* name = pc->MemoryFragment[i].c_str();
		if (name && (wcsstr(name, L"Lizzie") || wcsstr(name, L"Mother") || wcsstr(name, L"Father")))
		{
			familyMemories++;
		}
	}
	if (familyMemories >= 34) grant(22);

	if (pc->DestroyedDoomBarriers >= 10) grant(34);
	if (pc->AchievementsNew38Npcs >= 10) grant(38);
	if (pc->FrozenTotalCountEnemy >= 30) grant(39);
	if (pc->DefeatTotalRuinNpcs >= 100) grant(42);
	if (pc->DefeatTotalCardGuardNpcs >= 52) grant(43);
	if (gi->HealthUpgradePickupCount >= 4) grant(20);

	int maxedWeapons = 0;
	for (int i = 0; i < 4; i++)
	{
		if (pc->WeaponLevel[i] == 4)
		{
			maxedWeapons++;
		}
	}
	if (maxedWeapons >= 4) grant(19);

	// Snouts: all in the game (25), or every snout within any one chapter (24)
	int totalSnouts = 0, collectedSnouts = 0;
	for (int i = 0; i < 6; i++)
	{
		totalSnouts += gi->ChapterSnoutNum[i];
		collectedSnouts += gi->CurrentChapterSnoutNum[i];
	}
	if (totalSnouts > 0 && collectedSnouts >= totalSnouts)
	{
		grant(25);
	}

	for (int i = 0; i < 6; i++)
	{
		if (gi->ChapterSnoutNum[i] > 0 && gi->CurrentChapterSnoutNum[i] >= gi->ChapterSnoutNum[i])
		{
			grant(24);
			break;
		}
	}

	AchievementOverlay::AwardPlatinumIfComplete();
	AchievementOverlay::SaveAchievementBits();
}

static int __fastcall PlayerControllerConsoleCommand_Hook(int thisp, int, void* retStr, void* command, int bWriteToLog)
{
	if (command)
	{
		const wchar_t* cmd = *reinterpret_cast<const wchar_t**>(command);
		if (cmd)
		{
			if (const wchar_t* p = wcsstr(cmd, L"trophy unlock="))
			{
				int id = _wtoi(p + 14);
				AchievementOverlay::NotifyUnlock(id);
			}
		}
	}

	return PlayerControllerConsoleCommand.unsafe_thiscall<int>(thisp, retStr, command, bWriteToLog);
}

static void* __stdcall GameConsoleCommand_Hook(void* retStr, void* command, int bWriteToLog)
{
	if (command)
	{
		const wchar_t* cmd = *reinterpret_cast<const wchar_t**>(command);
		if (cmd)
		{
			if (const wchar_t* p = wcsstr(cmd, L"trophy unlock="))
			{
				int id = _wtoi(p + 14);
				AchievementOverlay::NotifyUnlock(id);
			}
		}
	}

	return GameConsoleCommand.unsafe_stdcall<void*>(retStr, command, bWriteToLog);
}

static void OnActorConsoleCommand(safetyhook::Context& ctx)
{
	const wchar_t* cmd = *reinterpret_cast<const wchar_t**>(ctx.ebp - 0x18);
	if (cmd)
	{
		if (const wchar_t* p = wcsstr(cmd, L"trophy unlock="))
		{
			int id = _wtoi(p + 14);
			AchievementOverlay::NotifyUnlock(id);
		}
	}
}

static POINT g_frozenCursor{};
static bool g_haveFrozenCursor = false;

static POINT* __fastcall MenuCursorRead_Hook(int thisp, int, POINT* outPos)
{
	if (AchievementOverlay::IsVisible())
	{
		if (!g_haveFrozenCursor)
		{
			MenuCursorRead.unsafe_thiscall<POINT*>(thisp, outPos);
			g_frozenCursor = *outPos;
			g_haveFrozenCursor = true;
		}
		else
		{
			*outPos = g_frozenCursor;
		}
		return outPos;
	}

	g_haveFrozenCursor = false;
	return MenuCursorRead.unsafe_thiscall<POINT*>(thisp, outPos);
}

static void OnProfileName(safetyhook::Context& ctx)
{
	const wchar_t* name = reinterpret_cast<const wchar_t*>(ctx.eax);
	if (!name || !name[0] || g_profileName == name)
		return;

	g_profileName = name;
	AchievementOverlay::InitAchievementFile(ProfileSavePath());
}

static void OnPersistentLoaded(safetyhook::Context& ctx)
{
	if (!AchievementOverlay::InitAchievementFile(ProfileSavePath()))
		return;

	AAlicePlayerController* pc = g_State.AlicePlayerController;
	AAliceGameInfo* gi = (pc && pc->WorldInfo) ? static_cast<AAliceGameInfo*>(pc->WorldInfo->Game) : nullptr;

	if (pc && gi)
	{
		SyncAchievementsFromSave(pc, gi);
	}
}

static void OnLanguageSet(safetyhook::Context& ctx)
{
	const wchar_t* name = *reinterpret_cast<const wchar_t**>(GetAddress(Addr::GameLanguageName));

	if (name == nullptr)
	{
		AchievementOverlay::SetLanguage("en");
		return;
	}

	if (std::wcscmp(name, L"FRA") == 0)
	{
		AchievementOverlay::SetLanguage("fr");
	}
	else if (std::wcscmp(name, L"DEU") == 0)
	{
		AchievementOverlay::SetLanguage("de");
	}
	else if (std::wcscmp(name, L"ITA") == 0)
	{
		AchievementOverlay::SetLanguage("it");
	}
	else if (std::wcscmp(name, L"ESN") == 0)
	{
		AchievementOverlay::SetLanguage("es");
	}
	else
	{
		AchievementOverlay::SetLanguage("en");
	}
}

void ApplyAchievementSupport()
{
	if (!AchievementSupport) return;

	PlayerControllerConsoleCommand = HookHelper::CreateHook((void*)GetAddress(Addr::PlayerControllerConsoleCommand), &PlayerControllerConsoleCommand_Hook);
	GameConsoleCommand = HookHelper::CreateHook((void*)GetAddress(Addr::GameConsoleCommand), &GameConsoleCommand_Hook);
	ActorConsoleCommand = safetyhook::create_mid(GetAddress(Addr::ActorConsoleCommand), OnActorConsoleCommand);
	MenuCursorRead = HookHelper::CreateHook((void*)GetAddress(Addr::MenuCursorRead), &MenuCursorRead_Hook);
	GetProfileName = safetyhook::create_mid(GetAddress(Addr::ProfileNameRead), OnProfileName);
	PersistentDataLoaded = safetyhook::create_mid(GetAddress(Addr::PersistentLoaded), OnPersistentLoaded);
	GetGameLanguage = safetyhook::create_mid(GetAddress(Addr::GameLanguageSet), OnLanguageSet);
}

void UpdateAchievementProgress()
{
	AAlicePlayerController* pc = g_State.AlicePlayerController;
	if (!pc) return;

	AAliceGameInfo* aliceGameInfo = pc->WorldInfo ? static_cast<AAliceGameInfo*>(pc->WorldInfo->Game) : nullptr;

	// Every frame: restore + persist the steam-vent timer (the game does not save it)
	if (g_State.AliceEngine)
	{
		if (AchievementOverlay::g_ventNeedsApply)
		{
			g_State.AliceEngine->totalVentDuration = AchievementOverlay::g_ventDuration;
			AchievementOverlay::g_ventNeedsApply = false;
		}

		static bool s_wasOnVent = false;
		const bool onVent = (pc->ventActor != nullptr);
		if (s_wasOnVent && !onVent) // just left a vent -> persist the accumulated timer
		{
			AchievementOverlay::SaveVentDuration(g_State.AliceEngine->totalVentDuration);
		}
		s_wasOnVent = onVent;
	}

	if (!AchievementOverlay::IsVisible())
		return;

	AchievementOverlay::SetAchievementProgress(34, pc->DestroyedDoomBarriers);
	AchievementOverlay::SetAchievementProgress(38, pc->AchievementsNew38Npcs);
	AchievementOverlay::SetAchievementProgress(39, pc->FrozenTotalCountEnemy);
	AchievementOverlay::SetAchievementProgress(42, pc->DefeatTotalRuinNpcs);
	AchievementOverlay::SetAchievementProgress(43, pc->DefeatTotalCardGuardNpcs);

	// Index 19: fully-upgraded weapons
	int maxed = 0;
	for (int i = 0; i < 4; i++)
	{
		if (pc->WeaponLevel[i] == 4)
		{
			maxed++;
		}
	}

	AchievementOverlay::SetAchievementProgress(19, maxed);

	// Index 21: Radula rooms
	int done = 0;
	for (int i = 0; i < 16; i++)
	{
		done += pc->CaveCompleted[i];
	}
	AchievementOverlay::SetAchievementProgress(21, done);

	// Index 23: all memories
	int memCollected = 0;
	for (int i = 0; i < 5; i++)
	{
		memCollected += pc->MemoryCompleted[i];
	}
	memCollected += pc->MemoryFragment.size();
	AchievementOverlay::SetAchievementProgress(23, memCollected);

	// Index 22: family memories
	int family = 0;
	const int fragmentCount = pc->MemoryFragment.size();
	for (int i = 0; i < fragmentCount; i++)
	{
		const wchar_t* name = pc->MemoryFragment[i].c_str();
		if (name && (wcsstr(name, L"Lizzie") || wcsstr(name, L"Mother") || wcsstr(name, L"Father")))
		{
			family++;
		}
	}
	AchievementOverlay::SetAchievementProgress(22, family);

	// Index 35: steam-vent timer (display only; restore/persist handled above)
	if (g_State.AliceEngine)
	{
		AchievementOverlay::SetAchievementProgress(35, (int)g_State.AliceEngine->totalVentDuration);
	}

	if (!aliceGameInfo)
		return;

	// Index 20: Jars of Rose Paint collected
	AchievementOverlay::SetAchievementProgress(20, aliceGameInfo->HealthUpgradePickupCount);

	// Index 25: snouts
	int total = 0, snoutsCollected = 0;
	for (int i = 0; i < 6; i++)
	{
		total += aliceGameInfo->ChapterSnoutNum[i];
		snoutsCollected += aliceGameInfo->CurrentChapterSnoutNum[i];
	}
	if (total > 0)
	{
		AchievementOverlay::SetAchievementMax(25, total);
		AchievementOverlay::SetAchievementProgress(25, snoutsCollected);
	}

	// Index 24: snouts in one chapter
	int bestTotal = 0, bestCollected = 0;
	float bestFrac = -1.0f;
	for (int i = 0; i < 6; i++)
	{
		if (aliceGameInfo->ChapterSnoutNum[i] <= 0)
			continue;

		const float frac = (float)aliceGameInfo->CurrentChapterSnoutNum[i] / (float)aliceGameInfo->ChapterSnoutNum[i];
		if (frac > bestFrac)
		{
			bestFrac = frac;
			bestTotal = aliceGameInfo->ChapterSnoutNum[i];
			bestCollected = aliceGameInfo->CurrentChapterSnoutNum[i];
		}
	}
	if (bestTotal > 0)
	{
		AchievementOverlay::SetAchievementMax(24, bestTotal);
		AchievementOverlay::SetAchievementProgress(24, bestCollected);
	}
}
