#include "Common.hpp"
#include "Features.hpp"
#include "MenuDisplayAspect.hpp"

static uintptr_t GfxTriListContinue = 0;
static uintptr_t BlackBarFlag = 0;
static safetyhook::MidHook MenuDisplayAspect;
static safetyhook::MidHook SceneFamilyDisplayAspect;
static safetyhook::MidHook MovieDisplayAspect;

static void OnMovieDisplayAspect(safetyhook::Context& ctx)
{
    // The movie renderer has already calculated FViewport width/height into
    // [EBP-5Ch]. Its fullscreen branch overwrites that with the desktop aspect.
    // Restore the render aspect before fitting the movie and its subtitles.
    ctx.xmm0.f32[0] = AliceCoopDisplay::MovieDisplayAspect(ctx.xmm0.f32[0],
        *reinterpret_cast<const float*>(ctx.ebp - 0x5C));
}

static void OnSceneFamilyDisplayAspect(safetyhook::Context& ctx)
{
    // This local-player function has already queried IsFullscreen into EAX.
    // [EBP-64h] is its FViewport argument, whose SizeX/SizeY are at 50h/54h.
    // Correct the scene-family input before the native store to [ESI+48h];
    // its consumers otherwise narrow the entire 3D scene to the desktop ratio.
    const auto viewport = *reinterpret_cast<const uintptr_t*>(ctx.ebp - 0x64);
    const auto width = *reinterpret_cast<const int*>(viewport + 0x50);
    const auto height = *reinterpret_cast<const int*>(viewport + 0x54);
    ctx.xmm0.f32[0] = AliceCoopDisplay::SceneDisplayAspect(ctx.xmm0.f32[0],
        width, height, ctx.eax != 0);
}

static void OnMenuDisplayAspect(safetyhook::Context& ctx)
{
    // The renderer is about to cache XMM0 and divide it by ConstrainedAspectRatio.
    // Its native fullscreen path uses the startup desktop ratio instead of the
    // current render dimensions in ESI/EDI, shrinking a 16:9 mode on a 21:9 display.
    // All GFx movies share this path, including pause and in-game HUD movies.
    ctx.xmm0.f32[0] = AliceCoopDisplay::SceneDisplayAspect(ctx.xmm0.f32[0],
        static_cast<int>(ctx.esi), static_cast<int>(ctx.edi), ctx.eax != 0);
}


__declspec(naked) static void GfxLetterboxStub()
{
	__asm
	{
		cmp dword ptr[esp + 14h], 20 // triangleCount
		jne not_letterbox
		cmp dword ptr[esp + 0Ch], 16 // numVertices
		jne not_letterbox
		mov eax, dword ptr[BlackBarFlag]
		cmp byte ptr[eax], 0
		je not_letterbox

		xor eax, eax
		ret 14h

		not_letterbox :
		push ebp
		mov ebp, esp
		push - 1
		jmp dword ptr[GfxTriListContinue]
	}
}

void ApplyFixAspectRatio()
{
	if (!FixAspectRatio) return;

    if (const auto address = GetAddress(Addr::GFxMenuDisplayAspect))
        MenuDisplayAspect = safetyhook::create_mid(address, OnMenuDisplayAspect);

    if (const auto address = GetAddress(Addr::SceneFamilyDisplayAspect))
    {
        // movss [esi+48h],xmm0; call ... . Only the verified executable layout
        // may use the frame/viewport offsets above.
        constexpr unsigned char expected[] = { 0xF3, 0x0F, 0x11, 0x46, 0x48, 0xE8 };
        if (std::memcmp(reinterpret_cast<const void*>(address), expected, sizeof(expected)) == 0)
            SceneFamilyDisplayAspect = safetyhook::create_mid(address, OnSceneFamilyDisplayAspect);
    }

    if (const auto address = GetAddress(Addr::MovieDisplayAspect))
    {
        constexpr unsigned char expected[] = { 0x0F, 0x28, 0xC8, 0xF3, 0x0F, 0x5E, 0x4D, 0xA4 };
        if (std::memcmp(reinterpret_cast<const void*>(address), expected, sizeof(expected)) == 0)
            MovieDisplayAspect = safetyhook::create_mid(address, OnMovieDisplayAspect);
    }


	DWORD addr_GFxDrawIndexedTriList = GetAddress(Addr::GFxDrawIndexedTriList);
	GfxTriListContinue = addr_GFxDrawIndexedTriList + 0x5;
	BlackBarFlag = reinterpret_cast<uintptr_t>(&g_State.shouldBlockBlackBar);
	MemoryHelper::MakeJMP(addr_GFxDrawIndexedTriList, reinterpret_cast<uintptr_t>(&GfxLetterboxStub));
}
