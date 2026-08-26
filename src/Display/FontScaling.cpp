#include "Common.hpp"
#include "Features.hpp"

static safetyhook::MidHook scaleHeightFactor{};
static safetyhook::MidHook scaleSize{};
static safetyhook::MidHook scaleLayoutMetrics{};
static safetyhook::MidHook scaleLineSpacing{};

static void OnScaleHeightFactor(safetyhook::Context& ctx)
{
	uint32_t ebp = ctx.ebp;
	float* scaling = (float*)(ebp - 0x1C);

	*scaling = g_State.subtitlesScaleFactor;
}

static void OnScaleSize(safetyhook::Context& ctx)
{
	uint32_t ebx = ctx.ebx;
	float* scaling1 = (float*)(ebx + 0x24);
	float* scaling2 = (float*)(ebx + 0x28);

	*scaling1 = *scaling1 * g_State.subtitlesScaleFactor;
	*scaling2 = *scaling2 * g_State.subtitlesScaleFactor;
}

static void OnScaleLayoutMetrics(safetyhook::Context& ctx)
{
	ctx.xmm1.f32[0] = g_State.subtitlesScaleFactor;
}

static void OnScaleLineSpacing(safetyhook::Context& ctx)
{
	uint32_t ebp = ctx.ebp;
	float* scaling = (float*)(ebp - 0x14);

	*scaling = *scaling * g_State.subtitlesScaleFactor;
}

void ApplyFontScaling()
{
	if (!FontScaling) return;

	scaleHeightFactor = safetyhook::create_mid(GetAddress(Addr::FontScaling_HeightFactor), OnScaleHeightFactor);
	scaleSize = safetyhook::create_mid(GetAddress(Addr::FontScaling_Size), OnScaleSize);
	scaleLayoutMetrics = safetyhook::create_mid(GetAddress(Addr::FontScaling_LayoutMetrics), OnScaleLayoutMetrics);
	scaleLineSpacing = safetyhook::create_mid(GetAddress(Addr::FontScaling_LineSpacing), OnScaleLineSpacing);
}
