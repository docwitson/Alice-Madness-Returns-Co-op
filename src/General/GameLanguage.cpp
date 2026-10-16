#include "Common.hpp"
#include "Features.hpp"

#include <cwchar>

static safetyhook::MidHook GameLanguageMid{};

struct FStringData
{
	wchar_t* Data;
	int ArrayNum;
	int ArrayMax;
};

static const wchar_t* g_forcedExtension = nullptr;

static const wchar_t* LanguageExtensionFor(int languageId)
{
	switch (languageId)
	{
		case 0:  return L"INT";
		case 1:  return L"FRA";
		case 2:  return L"DEU";
		case 3:  return L"ITA";
		case 4:  return L"ESN";
		case 5:  return L"JPN";
		default: return nullptr;
	}
}

static const char* OverlayCodeFor(int languageId)
{
	switch (languageId)
	{
		case 1:  return "fr";
		case 2:  return "de";
		case 3:  return "it";
		case 4:  return "es";
		default: return "en";
	}
}

static const char* OverlayCodeForExtension(const wchar_t* extension)
{
	if (extension == nullptr) return "en";

	if (std::wcscmp(extension, L"FRA") == 0) return "fr";
	if (std::wcscmp(extension, L"DEU") == 0) return "de";
	if (std::wcscmp(extension, L"ITA") == 0) return "it";
	if (std::wcscmp(extension, L"ESN") == 0) return "es";

	return "en";
}

static void OnGameLanguage(safetyhook::Context& ctx)
{
	FStringData* cached = reinterpret_cast<FStringData*>(GetAddress(Addr::GameLanguageName));

	// Every extension is three characters, so overwrite in place and leave the engine's own allocation and length alone
	if (g_forcedExtension && cached->Data && cached->ArrayNum == 4)
	{
		cached->Data[0] = g_forcedExtension[0];
		cached->Data[1] = g_forcedExtension[1];
		cached->Data[2] = g_forcedExtension[2];
	}

	if (AchievementSupport)
	{
		AchievementOverlay::SetLanguage(g_forcedExtension ? OverlayCodeFor(ForceLanguage) : OverlayCodeForExtension(cached->Data));
	}
}

void ApplyGameLanguage()
{
	g_forcedExtension = LanguageExtensionFor(ForceLanguage);

	// The hook also feeds the achievement overlay its language
	if (!g_forcedExtension && !AchievementSupport) return;

	GameLanguageMid = safetyhook::create_mid(GetAddress(Addr::GameLanguageSet), OnGameLanguage);
}
