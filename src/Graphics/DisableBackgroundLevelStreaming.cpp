#include "Common.hpp"
#include "Features.hpp"

static safetyhook::MidHook BackgroundLevelStreamingHook{};

static void OnBackgroundLevelStreaming(safetyhook::Context& ctx)
{
	const wchar_t* str = *(const wchar_t**)(ctx.eax);
	if (str && (wcscmp(str, L"AliceEntryExtra") == 0 || wcscmp(str, L"AliceEntryManual") == 0))
	{
		g_State.AliceEngine->bUseBackgroundLevelStreaming = 1;
	}
	else
	{
		g_State.AliceEngine->bUseBackgroundLevelStreaming = 0;
	}
}

void ApplyDisableBackgroundLevelStreaming()
{
	if (!DisableBackgroundLevelStreaming) return;

	BackgroundLevelStreamingHook = safetyhook::create_mid(GetAddress(Addr::BackgroundLevelStreaming), OnBackgroundLevelStreaming);
}
