#include "Common.hpp"
#include "Features.hpp"

static safetyhook::MidHook RenderLetterbox{};

static void OnLetterboxDraw(safetyhook::Context& ctx)
{
	uint32_t numVerts = *(uint32_t*)(ctx.ebp + 0x10);
	uint32_t primCount = *(uint32_t*)(ctx.ebp + 0x14);

	if (g_State.shouldBlockBlackBar && primCount == 20 && numVerts == 16)
	{
		// Skip the letterbox draw call
		ctx.edi = 0;
	}
}

void ApplyFixAspectRatio()
{
	if (!FixAspectRatio) return;

	RenderLetterbox = safetyhook::create_mid(GetAddress(Addr::BlackBarDraw), OnLetterboxDraw);
}
