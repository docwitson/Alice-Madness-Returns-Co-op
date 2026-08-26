#include "Common.hpp"
#include "Features.hpp"

static safetyhook::MidHook checkAlice1{};

static void OnCheckAlice1(safetyhook::Context& ctx)
{
	const wchar_t* pathStr = (const wchar_t*)ctx.eax;

	if (pathStr)
	{
		wchar_t fullPath[MAX_PATH];
		if (!SystemHelper::ResolveDirectory(pathStr, fullPath))
		{
			std::wstring errorMsg = L"Alice1 Directory Not Found!\n\n";
			errorMsg += L"The game is trying to set the working directory to a folder that doesn't exist.\n\n";
			errorMsg += L"Alice1Path Configuration ([AliceGame.AliceGameEngine]):\n";
			errorMsg += pathStr;
			errorMsg += L"\n\n";
			errorMsg += L"Resolved Full Path:\n";
			errorMsg += fullPath;
			errorMsg += L"\n\n";
			errorMsg += L"Please verify your Alice1 installation path in the configuration.";

			MessageBoxW(NULL, errorMsg.c_str(), L"Alice1 Directory Error", MB_OK | MB_ICONWARNING | MB_TOPMOST);
		}
	}
}

void ApplyWarnAlice1InstallFolder()
{
	if (!WarnAlice1InstallFolder) return;

	checkAlice1 = safetyhook::create_mid(GetAddress(Addr::WarnAlice1InstallFolder), OnCheckAlice1);
}
