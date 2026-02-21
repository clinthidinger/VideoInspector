// ControllerManager.h
// Manages PS4/PS5 (via HID) and Xbox (via XInput) controllers under one API.
// PS takes priority; Xbox is used as fallback when no PS controller is found.
//
// USAGE:
//   ControllerManager mController;
//
//   // in update():
//   mController.Update(dt);
//
//   if (mController.IsConnected())
//   {
//       if (mController.IsButtonDown(ControllerManager::Button::Cross)) { ... }
//       float lx = mController.GetAxis(ControllerManager::Axis::LX);
//   }
#pragma once

#include "InputSnapshot.h"
#include "XInputManager.h"
#include "PSHIDPolicy.h"

#include <memory>
#include <string>

class ControllerManager
{
public:
    enum class ControllerType { None, DualShock4, DualSense, DualSenseEdge, Xbox };

    enum class Button {
        Cross, Circle, Square, Triangle,
        DpadUp, DpadDown, DpadLeft, DpadRight,
        L1, R1, L2, R2, L3, R3,
        PS, Share, Start, Touch, Mute,
        LStickLeft, LStickRight, LStickUp, LStickDown,
        RStickLeft, RStickRight, RStickUp, RStickDown,
    };

    enum class Axis { LX, LY, RX, RY, L2, R2 };

    ControllerManager();
    ~ControllerManager();

    // Call once per frame from the app's update().
    void Update(float dt);

    bool  IsConnected()            const { return mSnap.connected; }
    float GetBattery()             const { return mSnap.battery; }
    bool  IsTouching()             const { return mSnap.isTouching; }

    // Type of the currently active controller, or None if disconnected.
    ControllerType GetControllerType() const { return mActiveType; }

    // Total number of connected controllers across PS and Xbox slots.
    int GetConnectedCount() const;

    // True every frame the button is held.
    bool IsButtonHeld(Button b)    const { return  getBtn(mSnap, b); }
    // True only on the frame the button is first pressed.
    bool IsButtonDown(Button b)    const { return  getBtn(mSnap, b) && !getBtn(mPrevSnap, b); }
    // True only on the frame the button is released.
    bool IsButtonUp  (Button b)    const { return !getBtn(mSnap, b) &&  getBtn(mPrevSnap, b); }

    // Current axis value. Sticks: –1 to +1. Triggers: 0 to 1.
    float GetAxis     (Axis a)     const { return getAxisVal(mSnap, a); }
    // Change in axis value since last frame.
    float GetAxisDelta(Axis a)     const { return getAxisVal(mSnap, a) - getAxisVal(mPrevSnap, a); }

private:
    using HardwareInfo   = GamepadCore::TGenericHardwareInfo<HIDPolicy>;
    using DeviceRegistry = GamepadCore::TBasicDeviceRegistry<AppRegistryPolicy>;

    std::unique_ptr<DeviceRegistry> mRegistry;
    XInputManager                   mXInput;
    InputSnapshot                   mSnap, mPrevSnap;
    ControllerType                  mActiveType = ControllerType::None;

    void fillFromPS(FInputContext* in);

    static bool        getBtn    (const InputSnapshot& s, Button b);
    static float       getAxisVal(const InputSnapshot& s, Axis a);
    static ControllerType psDeviceType(EDSDeviceType t);
    static std::string fmtF(float v);

    void logInputChanges();
};
