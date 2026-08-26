#include "Common.hpp"
#include "Features.hpp"

safetyhook::InlineHook GFx_LoadRootMovie;
static safetyhook::MidHook MenuScriptMid{};

static uint8_t* g_letterboxBuf = nullptr;
static uint8_t* g_memoryBuf = nullptr;

static void ApplyLetterbox(uint8_t* buf)
{
	// Reposition the top/bottom letterbox bars for the current aspect (Scaleform stage is 1280x720)
	double stageH = 1280.0 * (double)g_State.screenHeight / (double)g_State.screenWidth;
	double half = (stageH - 720.0) / 2.0;
	if (half < 0.0) half = 0.0; // 16:9 or wider -> no change

	int bottom = (int)(720.0 + half + 0.5);
	double top = -half;

	uint32_t halves[2];
	std::memcpy(halves, &top, 8);
	uint32_t swapped[2] = { halves[1], halves[0] };

	MemoryHelper::WriteMemory<int>((uintptr_t)(buf + 0x121), bottom, false);
	MemoryHelper::WriteMemoryRaw((uintptr_t)(buf + 0xF1), swapped, 8, false);
}

static void ApplyMemoryPosition(uint8_t* buf)
{
	double stageH = 1280.0 * (double)g_State.screenHeight / (double)g_State.screenWidth;
	double half = (stageH - 720.0) / 2.0;
	if (half < 0.0) half = 0.0; // 16:9 or wider -> no change

	int bottom = (int)(720.0 + half + 0.5);

	MemoryHelper::WriteMemory<int>((uintptr_t)(buf + 0x5116), bottom, false);
}

// Re-apply the letterbox geometry after a resolution change
void ReapplyMenuLetterbox()
{
	uint8_t* buf = g_letterboxBuf;
	if (!g_letterboxBuf)
		return;

	if (!MemoryHelper::IsWritable(buf, 0x125))
		return;
	if (MemoryHelper::ReadMemory<uint32_t>((uintptr_t)buf) != 0x0D009688)
		return;

	ApplyLetterbox(buf);
}

struct BytePatch
{
	uint32_t header;
	uint32_t offset;
	uint8_t  from;
};

static const BytePatch kBytePatches[] =
{
	// Patch the menu to show the PC version of "video" settings inside the controller menu
	{ 0x1F017788, 0x308, 0x1B },
	{ 0x1F017788, 0x366, 0x1B },
	{ 0x1F017788, 0x444, 0x1B },
	{ 0x1F017788, 0x4A3, 0x1B },
	{ 0x1F017788, 0x3C0, 0x2E },
	{ 0xA2076888, 0x7AD, 0x10 },
	{ 0xA2076888, 0x858, 0x10 },
	{ 0xA409AC88, 0xD2D, 0x14 },
};

static void ApplyMenuBytePatches(uint8_t* buf, uint32_t sig)
{
	if (!EnableControllerIcons) return;
	if (!ControllerHelper::IsConnected()) return;

	for (const BytePatch& patch : kBytePatches)
	{
		if (sig != patch.header) continue;

		uintptr_t address = (uintptr_t)(buf + patch.offset);
		if (MemoryHelper::ReadMemory<uint8_t>(address) == patch.from)
		{
			MemoryHelper::WriteMemory<uint8_t>(address, 0x00, false);
		}
	}
}

static int __fastcall GFx_LoadRootMovie_Hook(uint32_t thisp, uint32_t, uint32_t a2, uint32_t a3)
{
	uint32_t tr = *(uint32_t*)(a2 + 0x314);
	if (!tr) tr = a2 + 0x28;

	uint32_t src = *(uint32_t*)(tr + 0x10);
	if (src)
	{
		uint8_t* blob = *(uint8_t**)(src + 0x08);
		uint32_t size = blob ? *(uint32_t*)(src + 0x0C) : 0;

		if (blob && blob[0] == 'G' && blob[1] == 'F' && blob[2] == 'X')
		{
			if (EnableControllerIcons && ControllerHelper::IsConnected() && size == 0x1764F && blob[0x10998] == 0xC2)
			{
				static const uint8_t act[66] =
				{
					0x96, 0x05, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00,                               // Push 0  (arg count)
					0x96, 0x0A, 0x00, 0x00, 0x67, 0x65, 0x74, 0x44, 0x65, 0x70, 0x74, 0x68, 0x00, // Push "getDepth"
					0x3D,                                                                         // CallFunction
					0x96, 0x05, 0x00, 0x07, 0x20, 0x00, 0x00, 0x00,                               // Push 0x20
					0x60,                                                                         // BitAnd
					0x9D, 0x02, 0x00, 0x0B, 0x00,                                                 // If +11 -> console
					0x8C, 0x03, 0x00, 0x70, 0x63, 0x00,                                           // GoToLabel "pc"
					0x99, 0x02, 0x00, 0x0B, 0x00,                                                 // Jump +11 -> End
					0x8C, 0x08, 0x00, 0x63, 0x6F, 0x6E, 0x73, 0x6F, 0x6C, 0x65, 0x00,             // GoToLabel "console"
					0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00                                      // End + pad
				};
				memcpy(blob + 0x109A2, act, 66);
			}

			if (size > 0x24000 && blob[0x80E0] == 0x96 && blob[0x80EE] == 0x49 && blob[0x80EF] == 0x12 && blob[0x80F0] == 0x9D && blob[0x80F3] == 0x28)
			{
				if (ShowProfileCreation)
				{
					blob[0x80F3] = 0x00; // always show profile
				}
				else
				{
					blob[0x80EA] = 0x09; // never show profile
				}
			}
		}
	}

	return GFx_LoadRootMovie.unsafe_thiscall<int>(thisp, a2, a3);
}

static void OnMenuScript(safetyhook::Context& ctx)
{
	uint8_t* buf = *(uint8_t**)(ctx.esi + 8);
	if (!buf) return;

	uint32_t sig = MemoryHelper::ReadMemory<uint32_t>((uintptr_t)buf);

	if (FixAspectRatio && sig == 0x0D009688)
	{
		g_letterboxBuf = buf;
		ApplyLetterbox(buf);
	}
	else if (FixAspectRatio && sig == 0x18159B88)
	{
		g_memoryBuf = buf;
		ApplyMemoryPosition(buf);
	}
	else if (!HideAlice1WhenMissing && sig == 0xA409AC88)
	{
		// Remove the Alice 1 menu entry.
		if (MemoryHelper::ReadMemory<uint8_t>((uintptr_t)(buf + 0xC39)) == 0x5)
		{
			MemoryHelper::WriteMemory<uint8_t>((uintptr_t)(buf + 0xC39), 0xD7, false);
		}
	}

	ApplyMenuBytePatches(buf, sig);
}

void ApplyMenuScripts()
{
	GFx_LoadRootMovie = HookHelper::CreateHook((void*)GetAddress(Addr::GFxLoadRootMovie), &GFx_LoadRootMovie_Hook);
	MenuScriptMid = safetyhook::create_mid(GetAddress(Addr::MenuScripts), OnMenuScript);
}
