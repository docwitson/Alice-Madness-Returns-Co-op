#include "Common.hpp"
#include "Features.hpp"

safetyhook::InlineHook LoadStartupPackages;
static safetyhook::MidHook iniInput{};
static safetyhook::MidHook iniInputPtrRestore{};

// Ini Input override
struct PendingRestore
{
	uintptr_t structAddr;
	wchar_t* originalPtr;
	int originalLength;
	int originalCapacity;
	bool needsRestore;
};

static PendingRestore g_pendingRestore = { 0, nullptr, 0, 0, false };
static std::vector<std::unique_ptr<wchar_t[]>> g_tempFixedStrings;

static void ReplaceConfigString(safetyhook::Context& ctx, const wchar_t* newString)
{
	std::wstring str(newString);
	bool wasModified = false;

	// Handle SkipCutscenesWithEnter
	if (SkipCutscenesWithEnter && str.find(L"(Name=") != std::wstring::npos && str.find(L",Command=") != std::wstring::npos)
	{
		size_t commandPos = str.find(L",Command=\"");
		if (commandPos != std::wstring::npos)
		{
			commandPos += 10;

			size_t endQuotePos = str.find(L'"', commandPos);
			if (endQuotePos != std::wstring::npos)
			{
				std::wstring commandValue = str.substr(commandPos, endQuotePos - commandPos);
				std::wstring originalCommandValue = commandValue;

				if (str.find(L"(Name=\"SpaceBar\"") != std::wstring::npos)
				{
					// Remove "TryToCancelMatinee" from the command
					size_t pos = commandValue.find(L"TryToCancelMatinee");
					if (pos != std::wstring::npos)
					{
						size_t startPos = pos;
						size_t endPos = pos + wcslen(L"TryToCancelMatinee");

						// Remove trailing " | "
						if (endPos + 3 <= commandValue.length() && commandValue.substr(endPos, 3) == L" | ")
						{
							endPos += 3;
						}
						// Or remove leading " | "
						else if (startPos >= 3 && commandValue.substr(startPos - 3, 3) == L" | ")
						{
							startPos -= 3;
						}

						commandValue = commandValue.substr(0, startPos) + commandValue.substr(endPos);
					}
				}
				else if (str.find(L"(Name=\"Enter\"") != std::wstring::npos)
				{
					// Add "TryToCancelMatinee" if not already present
					if (commandValue.find(L"TryToCancelMatinee") == std::wstring::npos)
					{
						commandValue = L"TryToCancelMatinee | " + commandValue;
					}
				}

				if (commandValue != originalCommandValue)
				{
					str = str.substr(0, commandPos) + commandValue + str.substr(endQuotePos);
					wasModified = true;
				}
			}
		}
	}

	// Only allocate and update if the string was modified
	if (wasModified)
	{
		// Save original values for restoration
		g_pendingRestore.structAddr = ctx.ebx;
		g_pendingRestore.originalPtr = *(wchar_t**)(ctx.ebx + 0xC);
		g_pendingRestore.originalLength = *(int*)(ctx.ebx + 0x10);
		g_pendingRestore.originalCapacity = *(int*)(ctx.ebx + 0x14);
		g_pendingRestore.needsRestore = true;

		int newLength = str.length();
		auto fixedString = std::make_unique<wchar_t[]>(newLength + 1);
		wcscpy_s(fixedString.get(), newLength + 1, str.c_str());

		// Swap in the fixed string
		*(wchar_t**)(ctx.ebx + 0xC) = fixedString.get();
		*(int*)(ctx.ebx + 0x10) = newLength;
		*(int*)(ctx.ebx + 0x14) = newLength;

		// Store to keep alive during memcpy
		g_tempFixedStrings.push_back(std::move(fixedString));
	}
	else
	{
		g_pendingRestore.needsRestore = false;
	}
}

static void OnIniInputFix(safetyhook::Context& ctx)
{
	const wchar_t* keyName = *(const wchar_t**)(ctx.ebx + 0xC);

	if (keyName)
	{
		ReplaceConfigString(ctx, keyName);
	}
}

static void OnIniInputFixPtrRestore(safetyhook::Context& ctx)
{
	if (g_pendingRestore.needsRestore)
	{
		// Restore the original pointer and values
		uintptr_t structAddr = g_pendingRestore.structAddr;
		*(wchar_t**)(structAddr + 0xC) = g_pendingRestore.originalPtr;
		*(int*)(structAddr + 0x10) = g_pendingRestore.originalLength;
		*(int*)(structAddr + 0x14) = g_pendingRestore.originalCapacity;
		g_pendingRestore.needsRestore = false;
	}
}

static void __fastcall LoadStartupPackages_Hook()
{
	DWORD addr_InputFix = GetAddress(Addr::InputFix);

	// Before memcpy, hijack the string if needed
	iniInput = safetyhook::create_mid(addr_InputFix, OnIniInputFix);

	// After memcpy, restore the original data
	iniInputPtrRestore = safetyhook::create_mid(addr_InputFix + 0x24, OnIniInputFixPtrRestore);

	// Only called at startup
	LoadStartupPackages.fastcall<void>();

	(void)iniInput.disable();
	(void)iniInputPtrRestore.disable();
}

void ApplyFixInputBinding()
{
	if (!FixInputBinding) return;

	LoadStartupPackages = HookHelper::CreateHook((void*)GetAddress(Addr::LoadStartupPackages), &LoadStartupPackages_Hook);
}
