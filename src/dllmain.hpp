#pragma once

struct dinput8
{
    FARPROC DirectInput8Create;
    FARPROC DllCanUnloadNow;
    FARPROC DllGetClassObject;
    FARPROC DllRegisterServer;
    FARPROC DllUnregisterServer;
    FARPROC GetdfDIJoystick;

    bool ProxySetup(HINSTANCE hL)
    {
        DirectInput8Create = GetProcAddress(hL, "DirectInput8Create");
        DllCanUnloadNow = GetProcAddress(hL, "DllCanUnloadNow");
        DllGetClassObject = GetProcAddress(hL, "DllGetClassObject");
        DllRegisterServer = GetProcAddress(hL, "DllRegisterServer");
        DllUnregisterServer = GetProcAddress(hL, "DllUnregisterServer");
        GetdfDIJoystick = GetProcAddress(hL, "GetdfDIJoystick");

        if (!DirectInput8Create)
        {
            return false;
        }

        return true;
    }
};

extern dinput8 dinput8;
