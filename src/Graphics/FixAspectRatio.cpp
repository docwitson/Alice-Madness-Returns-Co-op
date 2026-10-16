#include "Common.hpp"
#include "Features.hpp"

static uintptr_t GfxTriListContinue = 0;
static uintptr_t BlackBarFlag = 0;

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

	DWORD addr_GFxDrawIndexedTriList = GetAddress(Addr::GFxDrawIndexedTriList);
	GfxTriListContinue = addr_GFxDrawIndexedTriList + 0x5;
	BlackBarFlag = reinterpret_cast<uintptr_t>(&g_State.shouldBlockBlackBar);
	MemoryHelper::MakeJMP(addr_GFxDrawIndexedTriList, reinterpret_cast<uintptr_t>(&GfxLetterboxStub));
}