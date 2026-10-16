#include "Common.hpp"
#include "Features.hpp"
#include "Coop/CoopClient.hpp"

#include <cwctype>
#include <intrin.h>

safetyhook::InlineHook FileWriterOpen;
safetyhook::InlineHook FileWriterDtor;
static safetyhook::InlineHook SessionFileRead;
static uintptr_t g_gameImageStart = 0, g_gameImageEnd = 0;

static HANDLE WINAPI SessionFileRead_Hook(LPCWSTR name, DWORD access, DWORD sharing,
    LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD attributes, HANDLE templateFile)
{
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    std::wstring redirected;
    if (name && creation == OPEN_EXISTING && access == GENERIC_READ
        && caller >= g_gameImageStart && caller < g_gameImageEnd
        && AliceCoop::TryRedirectSessionSaveRead(name, redirected))
        name = redirected.c_str();
    return SessionFileRead.unsafe_stdcall<HANDLE>(name, access, sharing,
        security, creation, attributes, templateFile);
}


struct SavePaths { std::wstring real, tmp, bak; };

static CRITICAL_SECTION g_saveCs;
static std::unordered_map<void*, SavePaths> g_pendingSaves;

static bool IsSaveFile(const wchar_t* n)
{
	// the checkpoint .sav and the persistent .PSD both live under the CheckPoint folder
	return StrStrIW(n, L"alice2checkpoint") || StrStrIW(n, L".psd");
}

static std::wstring ToAbs(const wchar_t* p)
{
	wchar_t buf[1024];
	DWORD len = GetFullPathNameW(p, 1024, buf, nullptr);
	return (len > 0 && len < 1024) ? std::wstring(buf) : std::wstring(p);
}

static void Commit(const SavePaths& p, bool ok)
{
	if (!ok)
	{
		DeleteFileW(p.tmp.c_str()); // failed write: keep the existing good save
		return;
	}

	if (GetFileAttributesW(p.real.c_str()) != INVALID_FILE_ATTRIBUTES)
	{
		// atomic replace, old save rolls into .bak (fall back to a plain atomic move if ReplaceFile refuses)
		if (!ReplaceFileW(p.real.c_str(), p.tmp.c_str(), p.bak.c_str(), REPLACEFILE_WRITE_THROUGH, nullptr, nullptr))
		{
			MoveFileExW(p.tmp.c_str(), p.real.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
		}
	}
	else
	{
		// first save ever for this slot: nothing to back up
		MoveFileExW(p.tmp.c_str(), p.real.c_str(), MOVEFILE_WRITE_THROUGH);
	}
}

// FArchiveFileWriter*
static void* __fastcall FileWriterOpen_Hook(void* thisp, int, wchar_t* name, uint32_t flags, void* errDev)
{
	if (name && IsSaveFile(name))
	{
		std::wstring redirectedName;
		const wchar_t* effectiveName = name;
		if (AliceCoop::TryRedirectClientSave(name, redirectedName))
			effectiveName = redirectedName.c_str();
		AliceCoop::ObserveSavePath(effectiveName);

		std::wstring tmpName = std::wstring(effectiveName) + L".tmp";
		void* writer = FileWriterOpen.unsafe_thiscall<void*>(thisp, tmpName.c_str(), flags, errDev);
		if (writer)
		{
			SavePaths p;
			p.real = ToAbs(effectiveName); // exact absolute path we want to commit
			p.tmp = p.real + L".tmp"; // == absolute of the file we just opened
			p.bak = p.real + L".bak";
			EnterCriticalSection(&g_saveCs);
			g_pendingSaves[writer] = p;
			LeaveCriticalSection(&g_saveCs);
		}
		return writer;
	}

	return FileWriterOpen.unsafe_thiscall<void*>(thisp, name, flags, errDev);
}

// the original frees the object, so read state first
static void* __fastcall FileWriterDtor_Hook(void* thisp, int, char flag)
{
	SavePaths p;
	bool pending = false, ok = false;

	EnterCriticalSection(&g_saveCs);
	auto it = g_pendingSaves.find(thisp);
	if (it != g_pendingSaves.end())
	{
		p = it->second;
		g_pendingSaves.erase(it);
		pending = true;
		// Flush has already closed the handle and finalised ArIsError by this point
		ok = (*(int32_t*)((uint8_t*)thisp + 0x34) == 0);
	}
	LeaveCriticalSection(&g_saveCs);

	void* result = FileWriterDtor.unsafe_thiscall<void*>(thisp, flag); // real dtor, file already flushed + closed

	if (pending) Commit(p, ok);
	return result;
}

void ApplyAtomicSaves()
{
	if (!AtomicSaves) return;

	InitializeCriticalSection(&g_saveCs);
	FileWriterOpen = HookHelper::CreateHook((void*)GetAddress(Addr::FileWriterOpen), &FileWriterOpen_Hook);
	FileWriterDtor = HookHelper::CreateHook((void*)GetAddress(Addr::FileWriterDtor), &FileWriterDtor_Hook);
    if (AliceCoop::IsEnabled())
    {
        const auto module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
        g_gameImageStart = module; g_gameImageEnd = module + nt->OptionalHeader.SizeOfImage;
        // Only reads issued by the game, of the selected profile's exact two
        // save paths, while an isolated Join session is active. Mod IO bypasses it.
        SessionFileRead = HookHelper::CreateHookAPI(L"kernel32.dll", "CreateFileW", &SessionFileRead_Hook);
        AliceCoop::SetSessionSaveReadHookReady(static_cast<bool>(SessionFileRead));
    }
}
