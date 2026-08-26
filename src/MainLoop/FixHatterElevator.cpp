#include "Common.hpp"
#include "Features.hpp"

// The Hatter elevator room (Chapter1_W2_RR_03_S) configures its pad and gates from a startup Kismet event
// that only fires once per level instance. If the room survives a section round trip, or a checkpoint
// restore brings it back, it stays wired for the first visit: the pad leads nowhere and the next checkpoint
// saves that dead state for good. Keep the event reentrant and rerun the startup routing whenever a load
// brings the room back stale. This also repairs saves that are already broken
namespace FixHatterElevator
{
	constexpr uint64_t ScanIntervalMs = 1500;
	constexpr uint64_t RepairWindowMs = 12000;
	constexpr float FreshActivationSeconds = 30.0f;

	constexpr const char* Room = "Chapter1_W2_RR_03_S";
	constexpr const char* StartupEvent = ".Main_Sequence.SeqEvent_LevelLoaded_0";

	// The room's gates at map defaults (class default is open, the map closes 2/3/4/8)
	struct GateDefault { const char* name; bool open; int autoCloseCount; };
	static const GateDefault kGates[] =
	{
		{ ".Main_Sequence.SeqAct_Gate_0",  true,  1 },
		{ ".Main_Sequence.SeqAct_Gate_1",  true,  1 },
		{ ".Main_Sequence.SeqAct_Gate_2",  false, 0 },
		{ ".Main_Sequence.SeqAct_Gate_3",  false, 0 },
		{ ".Main_Sequence.SeqAct_Gate_4",  false, 0 },
		{ ".Main_Sequence.SeqAct_Gate_7",  true,  0 },
		{ ".Main_Sequence.SeqAct_Gate_8",  false, 0 },
		{ ".Main_Sequence.SeqAct_Gate_10", true,  0 },
		{ ".Main_Sequence.SeqAct_Gate_11", true,  1 },
		{ ".Main_Sequence.SeqAct_Gate_12", true,  1 },
	};

	// Walk triggers that only fire once
	static const char* Touches[] =
	{
		".Main_Sequence.SeqEvent_Touch_1",
		".Main_Sequence.SeqEvent_Touch_2",
		".Main_Sequence.SeqEvent_Touch_3",
	};

	static bool IsRoomObject(const std::string& fullName, const char* suffix)
	{
		size_t len = strlen(suffix);
		if (fullName.size() < len || memcmp(fullName.data() + fullName.size() - len, suffix, len) != 0)
			return false;

		return fullName.find(Room) != std::string::npos;
	}

	static UFunction* CheckActivateFunction()
	{
		static UFunction* fn = nullptr;
		if (!fn)
		{
			fn = UFunction::FindFunction("Function Engine.SequenceEvent.CheckActivate");
		}
		return fn;
	}

	// The room's startup event, null while the level isn't loaded
	static USeqEvent_LevelLoaded* FindStartupEvent()
	{
		for (USeqEvent_LevelLoaded* ev : UObject::FindAllOf<USeqEvent_LevelLoaded>(true))
		{
			if (ev && IsRoomObject(ev->GetFullName(), StartupEvent))
				return ev;
		}

		return nullptr;
	}

	// A restored event still carries the ActivationTime it was saved with, one that fired during this load is seconds old
	static bool IsStaleActivation(USeqEvent_LevelLoaded* startup)
	{
		if (startup->TriggerCount == 0)
			return false;

		AAlicePlayerController* pc = g_State.AlicePlayerController;
		if (!pc || !pc->WorldInfo)
			return true; // can't tell, assume stale, repairing twice does no harm

		float worldTime = pc->WorldInfo->TimeSeconds;
		float firedAt = startup->ActivationTime;
		return firedAt > worldTime + 1.0f || (worldTime - firedAt) > FreshActivationSeconds;
	}

	// Back to map defaults, then fire the startup routing again so the room reconfigures from the
	// live progress values, exactly what happens when the level streams in fresh
	static void RepairRoom(USeqEvent_LevelLoaded* startup)
	{
		for (USeqEvent_Touch* ev : UObject::FindAllOf<USeqEvent_Touch>(true))
		{
			if (!ev)
				continue;

			std::string fullName = ev->GetFullName();
			for (const char* touch : Touches)
			{
				if (IsRoomObject(fullName, touch))
				{
					ev->TriggerCount = 0;
					ev->ActivationTime = 0.0f;
					break;
				}
			}
		}

		for (USeqAct_Gate* gate : UObject::FindAllOf<USeqAct_Gate>(true))
		{
			if (!gate)
				continue;

			std::string fullName = gate->GetFullName();
			for (const GateDefault& gd : kGates)
			{
				if (IsRoomObject(fullName, gd.name))
				{
					gate->bOpen = gd.open;
					gate->AutoCloseCount = gd.autoCloseCount;
					gate->CurrentCloseCount = 0;
					break;
				}
			}
		}

		startup->TriggerCount = 0;
		startup->MaxTriggerCount = 0;

		UFunction* checkActivate = CheckActivateFunction();
		if (!checkActivate)
			return;

		USequenceEvent_execCheckActivate_Params params{};
		params.InOriginator = startup->Originator;
		if (!params.InOriginator && g_State.AlicePlayerController)
		{
			params.InOriginator = g_State.AlicePlayerController->WorldInfo;
		}

		startup->ProcessEvent(checkActivate, &params, nullptr);
	}

	// Is RR_03_S currently loaded?
	static bool RoomIsLoaded()
	{
		AAlicePlayerController* pc = g_State.AlicePlayerController;
		if (!pc || !pc->WorldInfo)
		{
			return false;
		}

		for (ULevelStreaming* ls : pc->WorldInfo->StreamingLevels)
		{
			if (ls && ls->LoadedLevel && ls->PackageName.ToString() == Room)
			{
				return true;
			}
		}

		return false;
	}

	void Tick()
	{
		// Only wake up for a few seconds after a loading screen (LoadingBinkIsFinished, the same signal
		// FixMissingMusic uses). Every path that changes section progress goes through one
		static uint64_t lastLoadTick = 0;
		static uint64_t windowEnd = 0;
		static bool handled = true;

		if (g_State.loadEndTick != lastLoadTick)
		{
			lastLoadTick = g_State.loadEndTick;
			windowEnd = lastLoadTick + RepairWindowMs;
			handled = false;
		}

		if (handled)
			return;

		uint64_t now = GetTickCount64();
		if (now > windowEnd)
		{
			handled = true; // room never showed up, nothing to do
			return;
		}

		static uint64_t nextScan = 0;
		if (now < nextScan)
			return;
		nextScan = now + ScanIntervalMs;

		// Don't walk the object list unless the room is actually loaded
		if (!RoomIsLoaded())
			return;

		USeqEvent_LevelLoaded* startup = FindStartupEvent();
		if (!startup)
			return;

		// The actual bug: MaxTriggerCount=1 means the room only ever gets to configure itself once.
		// Zero it so its own routing runs again on every visibility change
		if (startup->MaxTriggerCount != 0)
			startup->MaxTriggerCount = 0;

		// Consumed + old = the room came back with stale state (save restore, or it stayed resident
		// across the round trip). A fresh instance already ran its own routing and needs nothing
		handled = true;
		if (IsStaleActivation(startup))
		{
			RepairRoom(startup);
		}
	}
}
