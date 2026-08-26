#include "Common.hpp"
#include "Features.hpp"

static safetyhook::MidHook MipMapBias{};

static void OnMipMapBias(safetyhook::Context& ctx)
{
	ctx.eax = (uintptr_t)(float)-0.5f;
}

void ApplyReducedMipMapBias()
{
	if (!ReducedMipMapBias) return;

	MipMapBias = safetyhook::create_mid(GetAddress(Addr::MipMapBias), OnMipMapBias);
}
