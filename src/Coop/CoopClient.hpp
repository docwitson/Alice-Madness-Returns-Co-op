#pragma once

#include "Coop/Protocol.hpp"

#include <string>

namespace AliceCoop
{
	void Initialize();
	void Tick();
	bool IsEnabled();
	bool ShouldUseUniqueMutex();
	void ObserveSavePath(const wchar_t* effectivePath);
	bool TryRedirectClientSave(const wchar_t* originalPath, std::wstring& redirectedPath);
}
