#include "Common.hpp"
#include "Features.hpp"

safetyhook::InlineHook GetWantedMips;
static SafetyHookInline ShouldMipLevelsBeForcedResident{};

static int __stdcall GetWantedMips_Hook(int a1, int a2, int a3, int a4)
{
	// Skip the timer
	return a4;
}

static bool __fastcall ShouldMipLevelsBeForcedResident_Hook(int thisp, int)
{
	return true;
}

void ApplyImprovedTextureStreaming()
{
	if (ForceHighResTextures)
	{
		ShouldMipLevelsBeForcedResident = HookHelper::CreateHook((void*)GetAddress(Addr::ShouldMipLevelsBeForcedResident), &ShouldMipLevelsBeForcedResident_Hook);
	}
	else if (ImprovedTextureStreaming)
	{
		// This is never called when 'ShouldMipLevelsBeForcedResident' is forced to true
		GetWantedMips = HookHelper::CreateHook((void*)GetAddress(Addr::GetWantedMips), &GetWantedMips_Hook);
	}
}
