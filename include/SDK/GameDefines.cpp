/*
#############################################################################################
# Alice2 (ASDK) SDK 1.0.0.0
# Generated with the CodeRedGenerator v1.2.0
# ========================================================================================= #
# File: GameDefines.cpp
# ========================================================================================= #
# Credits: ItsBranK, TheFeckless
# Links: www.github.com/CodeRedModding/CodeRed-Generator
#############################################################################################
*/
#include "GameDefines.hpp"

/*
# ========================================================================================= #
# Initialize Globals
# ========================================================================================= #
*/

class TArray<class UObject*>* GObjects{};
class TArray<class FNameEntry*>* GNames{};


/*
# ========================================================================================= #
# SDK Loader
# ========================================================================================= #
*/

namespace SDKLoader
{
	static int32_t LoadedBuildIndex = -1;

	static bool IsReadable(const void* address, size_t size)
	{
		return (address && !IsBadReadPtr(address, size));
	}

	static bool ValidateGlobalsAt(uintptr_t gobjectsAddress, uintptr_t gnamesAddress)
	{
		TArray<class UObject*>* objectsArray = reinterpret_cast<TArray<class UObject*>*>(gobjectsAddress);
		TArray<class FNameEntry*>* namesArray = reinterpret_cast<TArray<class FNameEntry*>*>(gnamesAddress);

		if (!IsReadable(objectsArray, sizeof(*objectsArray)) || !IsReadable(namesArray, sizeof(*namesArray)))
		{
			return false;
		}

		if ((objectsArray->size() <= 0) || (objectsArray->capacity() <= objectsArray->size()) || (objectsArray->capacity() > 0x1000000)
			|| (namesArray->size() <= 0) || (namesArray->capacity() <= namesArray->size()) || (namesArray->capacity() > 0x1000000))
		{
			return false;
		}

		if (!IsReadable(objectsArray->data(), sizeof(void*)) || !IsReadable(namesArray->data(), sizeof(void*)))
		{
			return false;
		}

		FNameEntry* firstEntry = namesArray->at(0);

		if (!IsReadable(firstEntry, 0x14) || (strcmp(firstEntry->Name, "None") != 0))
		{
			return false;
		}

		return true;
	}

	bool AreGObjectsValid()
	{
		return (IsReadable(GObjects, sizeof(*GObjects)) && !GObjects->empty() && (GObjects->capacity() > GObjects->size()));
	}

	bool AreGNamesValid()
	{
		return (IsReadable(GNames, sizeof(*GNames)) && !GNames->empty() && (GNames->capacity() > GNames->size()));
	}

	bool AreGlobalsValid()
	{
		return (AreGObjectsValid() && AreGNamesValid());
	}

	int32_t GetLoadedBuild()
	{
		return LoadedBuildIndex;
	}

	const char* GetLoadedBuildName()
	{
		constexpr int32_t buildCount = static_cast<int32_t>(sizeof(GGameBuilds) / sizeof(GGameBuilds[0]));

		if ((LoadedBuildIndex >= 0) && (LoadedBuildIndex < buildCount))
		{
			return GGameBuilds[LoadedBuildIndex].Name;
		}

		return "None";
	}

	bool Initialize(EGameBuild gameBuild, uint32_t timeoutMs)
	{
		if (LoadedBuildIndex >= 0)
		{
			return true;
		}

		uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
		constexpr int32_t buildCount = static_cast<int32_t>(sizeof(GGameBuilds) / sizeof(GGameBuilds[0]));
		int32_t firstBuild = 0;
		int32_t lastBuild = (buildCount - 1);

		if (gameBuild != EGameBuild::AutoDetect)
		{
			int32_t wantedBuild = static_cast<int32_t>(gameBuild);

			if ((wantedBuild < 0) || (wantedBuild >= buildCount))
			{
				return false;
			}

			firstBuild = wantedBuild;
			lastBuild = wantedBuild;
		}

		constexpr uint32_t retryDelayMs = 100;
		uint32_t elapsedMs = 0;

		while (true)
		{
			for (int32_t i = firstBuild; i <= lastBuild; i++)
			{
				uintptr_t gobjectsAddress = (baseAddress + GGameBuilds[i].GObjectsOffset);
				uintptr_t gnamesAddress = (baseAddress + GGameBuilds[i].GNamesOffset);

				if (ValidateGlobalsAt(gobjectsAddress, gnamesAddress))
				{
					GObjects = reinterpret_cast<TArray<class UObject*>*>(gobjectsAddress);
					GNames = reinterpret_cast<TArray<class FNameEntry*>*>(gnamesAddress);
					LoadedBuildIndex = i;
					return true;
				}
			}

			if (elapsedMs >= timeoutMs)
			{
				break;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
			elapsedMs += retryDelayMs;
		}

		return false;
	}
}

/*
# ========================================================================================= #
#
# ========================================================================================= #
*/
