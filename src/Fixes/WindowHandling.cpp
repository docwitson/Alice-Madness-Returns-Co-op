#include "Common.hpp"
#include "Features.hpp"

safetyhook::InlineHook ProcessDeferredMessage;
safetyhook::InlineHook UpdateMouseLock;

static void __fastcall ProcessDeferredMessage_Hook(int thisPtr, int, int deferredMessage)
{
	DWORD* msg = (DWORD*)deferredMessage;
	UINT& messageType = ((UINT*)msg)[1];
	DWORD wParam = msg[2];

	// Alt+F4 change to WM_CLOSE
	if (messageType == WM_SYSKEYDOWN && wParam == VK_F4)
	{
		messageType = WM_CLOSE;
	}

	ProcessDeferredMessage.unsafe_thiscall<void>(thisPtr, deferredMessage);
}

static LONG __fastcall UpdateMouseLock_Hook(int thisPtr, int)
{
	HWND gameWindow = *(HWND*)(thisPtr + 0x68);  // Get window handle
	HWND foregroundWindow = GetForegroundWindow();

	// Only call original if window has focus
	if (gameWindow == foregroundWindow)
	{
		return UpdateMouseLock.unsafe_thiscall<LONG>(thisPtr);
	}

	// Not focused, release clip instead
	ClipCursor(nullptr);
	return 0;
}

void ApplyFixWindowHandling()
{
	if (!FixWindowHandling) return;

	// Different addresses for different builds
	DWORD addr_BlockHook = GetAddress(Addr::BlockHook);

	DWORD addr_BlockMessages_1  = GetAddress(Addr::BlockMessages_1);
	DWORD addr_BlockMessages_2 = GetAddress(Addr::BlockMessages_2);

	UpdateMouseLock = HookHelper::CreateHook((void*)GetAddress(Addr::UpdateMouseLock), &UpdateMouseLock_Hook);
	ProcessDeferredMessage = HookHelper::CreateHook((void*)GetAddress(Addr::ProcessDeferredMessage), &ProcessDeferredMessage_Hook);

	if (Addresses::GetBuild() == GameBuild::Current)
	{
		// Block hook creation
		MemoryHelper::MakeNOP(addr_BlockHook, 0x14);
		MemoryHelper::MakeNOP(addr_BlockHook + 0x2B, 0x5);

		// Block thread messages
		MemoryHelper::MakeNOP(addr_BlockMessages_1, 0x14);
		MemoryHelper::MakeNOP(addr_BlockMessages_2, 0x15);
		MemoryHelper::MakeNOP(addr_BlockMessages_2 + 0x23, 0x16);
	}
	else
	{
		// Block hook creation
		MemoryHelper::MakeNOP(addr_BlockHook, 0x5);
		MemoryHelper::MakeNOP(addr_BlockHook + 0x7, 0xF);
		MemoryHelper::MakeNOP(addr_BlockHook + 0x2D, 0x5);

		// Block thread messages
		MemoryHelper::MakeNOP(addr_BlockMessages_1, 0x15);
		MemoryHelper::MakeNOP(addr_BlockMessages_2, 0x14);
		MemoryHelper::MakeNOP(addr_BlockMessages_2 + 0x22, 0x16);
	}
}
