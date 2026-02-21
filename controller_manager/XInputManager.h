// XInputManager.h
// Xbox controller support via the XInput API.
// Completely independent of GCore / hidapi / TBasicDeviceRegistry.
//
// Must be included after InputSnapshot.h.
#pragma once

#include "InputSnapshot.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <Xinput.h>

// XINPUT_GAMEPAD_GUIDE (the Xbox/Guide button, value 0x0400) is readable via
// XInputGetState but is not declared in all SDK versions of xinput.h.
#ifndef XINPUT_GAMEPAD_GUIDE
#define XINPUT_GAMEPAD_GUIDE 0x0400
#endif

class XInputManager
{
public:
    bool IsConnected(int slot) const;
    void FillSnapshot(InputSnapshot& snap, int slot) const;

private:
    static float NormalizeAxis(SHORT raw, SHORT deadZone);
};

#else // Non-Windows stub — XInput is Windows-only.

class XInputManager
{
public:
    bool IsConnected(int) const              { return false; }
    void FillSnapshot(InputSnapshot&, int) const {}
};

#endif // _WIN32
