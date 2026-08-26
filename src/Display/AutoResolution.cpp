#include "Common.hpp"
#include "Features.hpp"

static safetyhook::MidHook checkAutoResolution{};
static safetyhook::MidHook applyAutoResolution{};

static void OnCheckAutoResolution(safetyhook::Context& ctx)
{
	const wchar_t* docPath = reinterpret_cast<const wchar_t*>(ctx.eax);

	if (docPath)
	{
		// Cache the documents path so the achievement save file can be located later
		if (AchievementSupport)
		{
			g_docPath = docPath;
		}

		if (AutoResolution)
		{
			std::filesystem::path iniPath = std::filesystem::path(docPath) / L"AliceGame" / L"Config" / L"AliceEngine.ini";

			if (std::filesystem::exists(iniPath))
			{
				AutoResolution = false;
			}
		}
	}
}

static void OnApplyAutoResolution(safetyhook::Context& ctx)
{
	if (AutoResolution)
	{
		auto [screenWidth, screenHeight] = SystemHelper::GetScreenResolution();
		MemoryHelper::WriteMemory<int>(GetAddress(Addr::Width), screenWidth, false);
		MemoryHelper::WriteMemory<int>(GetAddress(Addr::Height), screenHeight, false);
	}
}

void ApplyAutoResolution()
{
	if (!AutoResolution && !AchievementSupport) return;

	checkAutoResolution = safetyhook::create_mid(GetAddress(Addr::DocPath), OnCheckAutoResolution);

	if (AutoResolution)
	{
		applyAutoResolution = safetyhook::create_mid(GetAddress(Addr::ApplyAutoResolution), OnApplyAutoResolution);
	}
}
