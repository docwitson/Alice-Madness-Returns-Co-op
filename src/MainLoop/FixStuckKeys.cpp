#include "Common.hpp"
#include "Features.hpp"

namespace FixStuckKeys
{
	// UE3 key FName -> Win32 virtual-key, cached by FName identity so the hot path never calls ToString()
	static int NameToVK(const FName& key)
	{
		static const std::unordered_map<std::string, int> kMap =
		{
			{"A", 'A'}, {"B", 'B'}, {"C", 'C'}, {"D", 'D'}, {"E", 'E'}, {"F", 'F'}, {"G", 'G'}, {"H", 'H'},
			{"I", 'I'}, {"J", 'J'}, {"K", 'K'}, {"L", 'L'}, {"M", 'M'}, {"N", 'N'}, {"O", 'O'}, {"P", 'P'},
			{"Q", 'Q'}, {"R", 'R'}, {"S", 'S'}, {"T", 'T'}, {"U", 'U'}, {"V", 'V'}, {"W", 'W'}, {"X", 'X'},
			{"Y", 'Y'}, {"Z", 'Z'},

			{"Zero", '0'}, {"One", '1'}, {"Two", '2'}, {"Three", '3'}, {"Four", '4'},
			{"Five", '5'}, {"Six", '6'}, {"Seven", '7'}, {"Eight", '8'}, {"Nine", '9'},

			{"Up", VK_UP}, {"Down", VK_DOWN}, {"Left", VK_LEFT}, {"Right", VK_RIGHT},

			{"SpaceBar", VK_SPACE},
			{"Enter", VK_RETURN},
			{"Escape", VK_ESCAPE},
			{"Tab", VK_TAB},
			{"BackSpace", VK_BACK},
			{"Delete", VK_DELETE},
			{"Insert", VK_INSERT},

			{"Home", VK_HOME},
			{"End", VK_END},
			{"PageUp", VK_PRIOR},
			{"PageDown", VK_NEXT},

			{"LeftShift", VK_LSHIFT},
			{"RightShift", VK_RSHIFT},

			{"LeftControl", VK_LCONTROL},
			{"RightControl", VK_RCONTROL},

			{"LeftAlt", VK_LMENU},
			{"RightAlt", VK_RMENU},

			{"F1", VK_F1}, {"F2", VK_F2}, {"F3", VK_F3}, {"F4", VK_F4},
			{"F5", VK_F5}, {"F6", VK_F6}, {"F7", VK_F7}, {"F8", VK_F8},
			{"F9", VK_F9}, {"F10", VK_F10}, {"F11", VK_F11}, {"F12", VK_F12},

			{"NumPadZero", VK_NUMPAD0}, {"NumPadOne", VK_NUMPAD1},
			{"NumPadTwo", VK_NUMPAD2}, {"NumPadThree", VK_NUMPAD3},
			{"NumPadFour", VK_NUMPAD4}, {"NumPadFive", VK_NUMPAD5},
			{"NumPadSix", VK_NUMPAD6}, {"NumPadSeven", VK_NUMPAD7},
			{"NumPadEight", VK_NUMPAD8}, {"NumPadNine", VK_NUMPAD9},

			{"Multiply", VK_MULTIPLY},
			{"Add", VK_ADD},
			{"Subtract", VK_SUBTRACT},
			{"Divide", VK_DIVIDE},
			{"Decimal", VK_DECIMAL},
		};

		static std::unordered_map<int32_t, int> cache;
		auto cached = cache.find(key.GetDisplayIndex());
		if (cached != cache.end())
		{
			return cached->second;
		}

		auto it = kMap.find(key.ToString());
		int vk = (it == kMap.end()) ? 0 : it->second;
		cache[key.GetDisplayIndex()] = vk;
		return vk;
	}

	static bool IsExtendedVK(int vk)
	{
		switch (vk)
		{
			case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
			case VK_INSERT: case VK_DELETE:
			case VK_HOME: case VK_END:
			case VK_PRIOR: case VK_NEXT:
			case VK_DIVIDE:
			case VK_RCONTROL: case VK_RMENU:
				return true;
			default:
				return false;
		}
	}

	void Tick()
	{
		AAlicePlayerController* pc = g_State.AlicePlayerController;
		if (!pc || !pc->PlayerInput)
			return;

		auto& pressedKeys = pc->PlayerInput->PressedKeys;
		const int count = pressedKeys.size();
		if (count == 0)
			return; // nothing pressed, no syscalls at all

		bool focusKnown = false, focused = false;

		INPUT inputs[16] = {};
		int numInputs = 0;

		for (int i = 0; i < count && numInputs < 16; i++)
		{
			int vk = NameToVK(pressedKeys[i]);
			if (vk <= 0)
				continue;

			if (GetAsyncKeyState(vk) & 0x8000) // physically held -> legit, leave it
				continue;

			// Phantom: pressed but physically up
			if (!focusKnown)
			{
				DWORD fgPid = 0;
				GetWindowThreadProcessId(GetForegroundWindow(), &fgPid);
				focused = (fgPid == GetCurrentProcessId());
				focusKnown = true;
			}

			if (!focused)
				return;

			INPUT& inp = inputs[numInputs++];
			inp.type = INPUT_KEYBOARD;
			inp.ki.wVk = (WORD)vk;
			inp.ki.wScan = (WORD)MapVirtualKey(vk, MAPVK_VK_TO_VSC);
			inp.ki.dwFlags = KEYEVENTF_KEYUP | (IsExtendedVK(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
		}

		if (numInputs > 0)
		{
			SendInput(numInputs, inputs, sizeof(INPUT));
		}
	}
}
