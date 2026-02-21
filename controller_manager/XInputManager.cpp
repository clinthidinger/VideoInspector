#include "XInputManager.h"

#ifdef _WIN32

bool XInputManager::IsConnected(int slot) const
{
    XINPUT_STATE state{};
    return XInputGetState(static_cast<DWORD>(slot), &state) == ERROR_SUCCESS;
}

void XInputManager::FillSnapshot(InputSnapshot& snap, int slot) const
{
    XINPUT_STATE state{};
    if (XInputGetState(static_cast<DWORD>(slot), &state) != ERROR_SUCCESS)
    {
        snap.connected = false;
        return;
    }

    const XINPUT_GAMEPAD& gp = state.Gamepad;
    auto btn = [&](WORD flag) { return (gp.wButtons & flag) != 0; };

    snap.connected = true;

    // ---- Face buttons (Xbox → PS names) ----
    snap.cross    = btn(XINPUT_GAMEPAD_A);  // A  → Cross
    snap.circle   = btn(XINPUT_GAMEPAD_B);  // B  → Circle
    snap.square   = btn(XINPUT_GAMEPAD_X);  // X  → Square
    snap.triangle = btn(XINPUT_GAMEPAD_Y);  // Y  → Triangle

    // ---- D-pad ----
    snap.dpadUp    = btn(XINPUT_GAMEPAD_DPAD_UP);
    snap.dpadDown  = btn(XINPUT_GAMEPAD_DPAD_DOWN);
    snap.dpadLeft  = btn(XINPUT_GAMEPAD_DPAD_LEFT);
    snap.dpadRight = btn(XINPUT_GAMEPAD_DPAD_RIGHT);

    // ---- Shoulders ----
    snap.l1 = btn(XINPUT_GAMEPAD_LEFT_SHOULDER);   // LB → L1
    snap.r1 = btn(XINPUT_GAMEPAD_RIGHT_SHOULDER);  // RB → R1

    // ---- Triggers (BYTE 0–255, normalize to 0.0–1.0) ----
    snap.l2 = (gp.bLeftTrigger  > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
                  ? gp.bLeftTrigger  / 255.0f : 0.0f;
    snap.r2 = (gp.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
                  ? gp.bRightTrigger / 255.0f : 0.0f;
    snap.l2pressed = snap.l2 > 0.0f;
    snap.r2pressed = snap.r2 > 0.0f;

    // ---- Stick clicks ----
    snap.l3 = btn(XINPUT_GAMEPAD_LEFT_THUMB);   // LS click → L3
    snap.r3 = btn(XINPUT_GAMEPAD_RIGHT_THUMB);  // RS click → R3

    // ---- Analog sticks (SHORT –32768..+32767 → –1..+1 with dead zone) ----
    snap.lx = NormalizeAxis(gp.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    snap.ly = NormalizeAxis(gp.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    snap.rx = NormalizeAxis(gp.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    snap.ry = NormalizeAxis(gp.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);

    // ---- Digital stick directions ----
    constexpr float kDir = 0.5f;
    snap.lLeft  = snap.lx < -kDir;
    snap.lRight = snap.lx >  kDir;
    snap.lUp    = snap.ly >  kDir;
    snap.lDown  = snap.ly < -kDir;
    snap.rLeft  = snap.rx < -kDir;
    snap.rRight = snap.rx >  kDir;
    snap.rUp    = snap.ry >  kDir;
    snap.rDown  = snap.ry < -kDir;

    // ---- System buttons ----
    snap.psButton = btn(XINPUT_GAMEPAD_GUIDE);  // Xbox button → PS button
    snap.start    = btn(XINPUT_GAMEPAD_START);  // Menu → Start
    snap.share    = btn(XINPUT_GAMEPAD_BACK);   // View → Share

    // ---- PS-only fields Xbox has no equivalent for ----
    snap.touchClick = false;
    snap.isTouching = false;
    snap.mute       = false;
    snap.battery    = 0.0f;
}

float XInputManager::NormalizeAxis(SHORT raw, SHORT deadZone)
{
    if (raw >  deadZone) return  static_cast<float>(raw  - deadZone)  / (32767.0f - deadZone);
    if (raw < -deadZone) return -static_cast<float>(-raw - deadZone)  / (32767.0f - deadZone);
    return 0.0f;
}

#endif // _WIN32
