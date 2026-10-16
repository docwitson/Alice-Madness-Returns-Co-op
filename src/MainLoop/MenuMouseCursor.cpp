#include "Common.hpp"
#include "Features.hpp"
#include <cstdlib>

namespace MenuMouseCursor
{
	namespace
	{
		constexpr int kStickDeadzone = 12000;
		constexpr int kTriggerDeadzone = 8000;
		constexpr LONG kMouseThreshold = 2;
		constexpr ULONGLONG kReapplyIntervalMs = 250;
		constexpr ULONGLONG kSelfMoveIgnoreMs = 250;

		bool s_visible = true;
		bool s_applied = false;
		bool s_haveMousePos = false;
		POINT s_lastMousePos{};
		ULONGLONG s_nextReapply = 0;

		bool s_parked = false;
		POINT s_parkReturn{};
		ULONGLONG s_ignoreMouseUntil = 0;

		void CallNative(UObject* object, UFunction* function, void* params)
		{
			if (!object || !function) return;

			uint16_t savedNative = function->iNative;
			function->iNative = 0;
			object->ProcessEvent(function, params, nullptr);
			function->iNative = savedNative;
		}

		void MovieSetVariableBool(UGFxMovie* movie, const wchar_t* path, bool value)
		{
			static UFunction* function = nullptr;
			if (!function) function = UFunction::FindFunction("Function GFxUI.GFxMovie.SetVariableBool");
			if (!function) return;

			UGFxMovie_execSetVariableBool_Params params{};
			params.Path = FString(path);
			params.B = value ? 1 : 0;
			CallNative(movie, function, &params);
		}

		void WarpPointer(POINT screenPos)
		{
			int originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
			int originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
			int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
			int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
			if (width < 2 || height < 2) return;

			INPUT input{};
			input.type = INPUT_MOUSE;
			input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
			input.mi.dx = ((screenPos.x - originX) * 65535 + (width - 1) / 2) / (width - 1);
			input.mi.dy = ((screenPos.y - originY) * 65535 + (height - 1) / 2) / (height - 1);
			SendInput(1, &input, sizeof(INPUT));

			s_ignoreMouseUntil = GetTickCount64() + kSelfMoveIgnoreMs;
		}

		HWND FocusedGameWindow()
		{
			HWND window = GetForegroundWindow();
			if (!window) return nullptr;

			DWORD pid = 0;
			GetWindowThreadProcessId(window, &pid);
			return (pid == GetCurrentProcessId()) ? window : nullptr;
		}

		void ParkCursor()
		{
			HWND window = FocusedGameWindow();
			if (!window) return;

			POINT corner{ 0, 0 };
			if (!ClientToScreen(window, &corner)) return;

			POINT current{};
			if (!GetCursorPos(&current)) return;
			if (current.x == corner.x && current.y == corner.y) return; // already there

			// Only the first park records where to put the pointer back
			if (!s_parked)
			{
				s_parkReturn = current;
				s_parked = true;
			}

			WarpPointer(corner);
			if (GetCursorPos(&s_lastMousePos)) s_haveMousePos = true;
		}

		void UnparkCursor()
		{
			if (!s_parked) return;
			s_parked = false;

			WarpPointer(s_parkReturn);
			if (GetCursorPos(&s_lastMousePos)) s_haveMousePos = true;
		}

		bool IsGamepadActive()
		{
			SDL_Gamepad* pad = ControllerHelper::s_pGamepad;
			if (!pad) return false;

			for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; button++)
			{
				// The touchpad doubles as a mouse surface, so it must not count as controller use
				if (TouchpadEnabled && button == SDL_GAMEPAD_BUTTON_TOUCHPAD) continue;

				if (SDL_GetGamepadButton(pad, (SDL_GamepadButton)button)) return true;
			}

			for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; axis++)
			{
				bool isTrigger = (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
				int value = SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)axis);

				if (std::abs(value) > (isTrigger ? kTriggerDeadzone : kStickDeadzone)) return true;
			}

			return false;
		}

		bool IsMouseActive()
		{
			POINT pos{};
			if (!GetCursorPos(&pos)) return false;

			bool moved = s_haveMousePos && (std::abs(pos.x - s_lastMousePos.x) >= kMouseThreshold || std::abs(pos.y - s_lastMousePos.y) >= kMouseThreshold);

			s_lastMousePos = pos;
			s_haveMousePos = true;

			if (GetTickCount64() < s_ignoreMouseUntil) return false;
			if (moved) return true;

			return ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000) != 0;
		}

		void PushVisibility(bool visible)
		{
			bool menuMovieRunning = false;

			for (UGFxMovie* movie : UObject::FindAllOf<UGFxMovie>())
			{
				if (!movie || !movie->pMovie.Dummy) continue;

				MovieSetVariableBool(movie, L"_root.cmc._visible", visible);

				if (!menuMovieRunning)
				{
					std::string className = movie->Class ? movie->Class->GetName() : std::string();
					menuMovieRunning = (className != "AliceGFxMovie_HUD");
				}
			}

			if (visible)
			{
				UnparkCursor();
			}
			else if (menuMovieRunning)
			{
				ParkCursor();
			}
		}
	}

	void Tick()
	{
		if (!EnableControllerIcons || !ControllerHelper::IsConnected())
		{
			if (s_applied && !s_visible)
			{
				s_visible = true;
				PushVisibility(true);
			}

			UnparkCursor();
			s_applied = false;
			s_haveMousePos = false;
			return;
		}

		bool mouseActive = IsMouseActive();
		bool gamepadActive = IsGamepadActive();

		bool visible = s_applied ? s_visible : false;

		if (gamepadActive) visible = false;
		else if (mouseActive) visible = true;

		ULONGLONG now = GetTickCount64();
		bool changed = !s_applied || visible != s_visible;

		if (!changed && (visible || now < s_nextReapply)) return;

		s_visible = visible;
		s_applied = true;
		s_nextReapply = now + kReapplyIntervalMs;
		PushVisibility(visible);
	}
}
