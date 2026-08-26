#include "Common.hpp"
#include "Features.hpp"

static safetyhook::MidHook SkipMovie{};
safetyhook::InlineHook FFullScreenMovieBink_PlayMovie;

static bool shouldSkipMovie = false;

static unsigned int __fastcall FFullScreenMovieBink_PlayMovie_Hook(int* thisp, int, int a2, const wchar_t* MovieFilename, int a4, int a5, int a6, int a7, unsigned int a8, int a9, WORD* Src)
{
	if (MovieFilename)
	{
		if (_wcsicmp(MovieFilename, L"LoadingMovie.bik\x00") == 0)
		{
			(void)FFullScreenMovieBink_PlayMovie.disable();
			(void)SkipMovie.disable();
		}
		else if (SkipEAIntro && _wcsicmp(MovieFilename, L"Intro_EA.bik\x00") == 0)
		{
			shouldSkipMovie = true;
		}
		else if (SkipSHIntro && _wcsicmp(MovieFilename, L"Intro_SH.bik\x00") == 0)
		{
			shouldSkipMovie = true;
		}
		else if (SkipUEIntro && _wcsicmp(MovieFilename, L"TechLogo_Short.bik\x00") == 0)
		{
			shouldSkipMovie = true;
		}
	}

	return FFullScreenMovieBink_PlayMovie.unsafe_thiscall<unsigned int>(thisp, a2, MovieFilename, a4, a5, a6, a7, a8, a9, Src);
}

static void OnSkipMovie(safetyhook::Context& ctx)
{
	if (shouldSkipMovie)
	{
		ctx.eax = 0;
		shouldSkipMovie = false;
	}
}

void ApplyIntroSkip()
{
	if (!SkipEAIntro && !SkipSHIntro && !SkipUEIntro) return;

	FFullScreenMovieBink_PlayMovie = HookHelper::CreateHook((void*)GetAddress(Addr::PlayMovie), &FFullScreenMovieBink_PlayMovie_Hook);
	SkipMovie = safetyhook::create_mid(GetAddress(Addr::SkipMovie), OnSkipMovie);
}
