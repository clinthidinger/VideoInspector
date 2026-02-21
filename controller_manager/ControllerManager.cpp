#include "ControllerManager.h"

#include <iostream>
#include <cmath>
#include <cstdio>

// ----------------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------------

ControllerManager::ControllerManager()
{
    hid_init();
    IPlatformHardwareInfo::SetInstance(std::make_unique<HardwareInfo>());
    mRegistry = std::make_unique<DeviceRegistry>();
    mRegistry->RequestImmediateDetection();
}

ControllerManager::~ControllerManager()
{
    mRegistry.reset();
    IPlatformHardwareInfo::SetInstance(std::unique_ptr<IPlatformHardwareInfo>{});
    hid_exit();
}

// ----------------------------------------------------------------------------
// Per-frame update
// ----------------------------------------------------------------------------

void ControllerManager::Update(float dt)
{
    mPrevSnap = mSnap;

    mRegistry->PlugAndPlay(dt);

    auto* pad = mRegistry->GetLibrary(0);
    if (pad && pad->IsConnected())
    {
        pad->UpdateInput(dt);
        fillFromPS(pad->GetMutableDeviceContext()->GetInputState());
        mActiveType = psDeviceType(pad->GetMutableDeviceContext()->DeviceType);
    }
    else if (mXInput.IsConnected(0))
    {
        mXInput.FillSnapshot(mSnap, 0);
        mActiveType = ControllerType::Xbox;
    }
    else
    {
        mSnap.connected = false;
        mActiveType     = ControllerType::None;
    }

    logInputChanges();
}

int ControllerManager::GetConnectedCount() const
{
    int count = 0;

    for (int id = 0; ; ++id)
    {
        auto* pad = mRegistry->GetLibrary(id);
        if (!pad) break;
        if (pad->IsConnected()) ++count;
    }

    for (int slot = 0; slot < 4; ++slot)
        if (mXInput.IsConnected(slot)) ++count;

    return count;
}

// ----------------------------------------------------------------------------
// Private helpers
// ----------------------------------------------------------------------------

void ControllerManager::fillFromPS(FInputContext* in)
{
    mSnap.connected  = true;
    mSnap.cross      = in->bCross;
    mSnap.square     = in->bSquare;
    mSnap.triangle   = in->bTriangle;
    mSnap.circle     = in->bCircle;
    mSnap.dpadUp     = in->bDpadUp;
    mSnap.dpadDown   = in->bDpadDown;
    mSnap.dpadLeft   = in->bDpadLeft;
    mSnap.dpadRight  = in->bDpadRight;
    mSnap.l1         = in->bLeftShoulder;
    mSnap.r1         = in->bRightShoulder;
    mSnap.l2pressed  = in->bLeftTriggerThreshold;
    mSnap.r2pressed  = in->bRightTriggerThreshold;
    mSnap.l2         = in->LeftTriggerAnalog;
    mSnap.r2         = in->RightTriggerAnalog;
    mSnap.l3         = in->bLeftStick;
    mSnap.r3         = in->bRightStick;
    mSnap.lx         = in->LeftAnalog.X;
    mSnap.ly         = in->LeftAnalog.Y;
    mSnap.rx         = in->RightAnalog.X;
    mSnap.ry         = in->RightAnalog.Y;
    mSnap.lLeft      = in->bLeftAnalogLeft;
    mSnap.lRight     = in->bLeftAnalogRight;
    mSnap.lUp        = in->bLeftAnalogUp;
    mSnap.lDown      = in->bLeftAnalogDown;
    mSnap.rLeft      = in->bRightAnalogLeft;
    mSnap.rRight     = in->bRightAnalogRight;
    mSnap.rUp        = in->bRightAnalogUp;
    mSnap.rDown      = in->bRightAnalogDown;
    mSnap.psButton   = in->bPSButton;
    mSnap.share      = in->bShare;
    mSnap.start      = in->bStart;
    mSnap.touchClick = in->bTouch;
    mSnap.mute       = in->bMute;
    mSnap.isTouching = in->bIsTouching;
    mSnap.battery    = in->BatteryLevel;
}

bool ControllerManager::getBtn(const InputSnapshot& s, Button b)
{
    switch (b)
    {
        case Button::Cross:       return s.cross;
        case Button::Circle:      return s.circle;
        case Button::Square:      return s.square;
        case Button::Triangle:    return s.triangle;
        case Button::DpadUp:      return s.dpadUp;
        case Button::DpadDown:    return s.dpadDown;
        case Button::DpadLeft:    return s.dpadLeft;
        case Button::DpadRight:   return s.dpadRight;
        case Button::L1:          return s.l1;
        case Button::R1:          return s.r1;
        case Button::L2:          return s.l2pressed;
        case Button::R2:          return s.r2pressed;
        case Button::L3:          return s.l3;
        case Button::R3:          return s.r3;
        case Button::PS:          return s.psButton;
        case Button::Share:       return s.share;
        case Button::Start:       return s.start;
        case Button::Touch:       return s.touchClick;
        case Button::Mute:        return s.mute;
        case Button::LStickLeft:  return s.lLeft;
        case Button::LStickRight: return s.lRight;
        case Button::LStickUp:    return s.lUp;
        case Button::LStickDown:  return s.lDown;
        case Button::RStickLeft:  return s.rLeft;
        case Button::RStickRight: return s.rRight;
        case Button::RStickUp:    return s.rUp;
        case Button::RStickDown:  return s.rDown;
    }
    return false;
}

float ControllerManager::getAxisVal(const InputSnapshot& s, Axis a)
{
    switch (a)
    {
        case Axis::LX: return s.lx;
        case Axis::LY: return s.ly;
        case Axis::RX: return s.rx;
        case Axis::RY: return s.ry;
        case Axis::L2: return s.l2;
        case Axis::R2: return s.r2;
    }
    return 0.f;
}

ControllerManager::ControllerType ControllerManager::psDeviceType(EDSDeviceType t)
{
    switch (t)
    {
        case EDSDeviceType::DualShock4:    return ControllerType::DualShock4;
        case EDSDeviceType::DualSense:     return ControllerType::DualSense;
        case EDSDeviceType::DualSenseEdge: return ControllerType::DualSenseEdge;
        default:                           return ControllerType::None;
    }
}

std::string ControllerManager::fmtF(float v)
{
    char buf[12];
    std::snprintf(buf, sizeof(buf), "%+.3f", v);
    return buf;
}

void ControllerManager::logInputChanges()
{
    struct { const char* name; bool cur, prev; } btns[] = {
        { "Cross",      mSnap.cross,      mPrevSnap.cross      },
        { "Square",     mSnap.square,     mPrevSnap.square     },
        { "Triangle",   mSnap.triangle,   mPrevSnap.triangle   },
        { "Circle",     mSnap.circle,     mPrevSnap.circle     },
        { "DPad Up",    mSnap.dpadUp,     mPrevSnap.dpadUp     },
        { "DPad Down",  mSnap.dpadDown,   mPrevSnap.dpadDown   },
        { "DPad Left",  mSnap.dpadLeft,   mPrevSnap.dpadLeft   },
        { "DPad Right", mSnap.dpadRight,  mPrevSnap.dpadRight  },
        { "L1",         mSnap.l1,         mPrevSnap.l1         },
        { "R1",         mSnap.r1,         mPrevSnap.r1         },
        { "L2",         mSnap.l2pressed,  mPrevSnap.l2pressed  },
        { "R2",         mSnap.r2pressed,  mPrevSnap.r2pressed  },
        { "L3",         mSnap.l3,         mPrevSnap.l3         },
        { "R3",         mSnap.r3,         mPrevSnap.r3         },
        { "PS",         mSnap.psButton,   mPrevSnap.psButton   },
        { "Share",      mSnap.share,      mPrevSnap.share      },
        { "Start",      mSnap.start,      mPrevSnap.start      },
        { "Touchpad",   mSnap.touchClick, mPrevSnap.touchClick },
        { "Mute",       mSnap.mute,       mPrevSnap.mute       },
    };

    for (auto& b : btns)
        if (b.cur != b.prev)
            std::cout << b.name << (b.cur ? " pressed" : " released") << "\n";

    auto changed = [](float c, float p) { return std::abs(c - p) > 0.01f; };
    if (changed(mSnap.lx, mPrevSnap.lx) || changed(mSnap.ly, mPrevSnap.ly))
        std::cout << "Left stick  X:" << fmtF(mSnap.lx) << " Y:" << fmtF(mSnap.ly) << "\n";
    if (changed(mSnap.rx, mPrevSnap.rx) || changed(mSnap.ry, mPrevSnap.ry))
        std::cout << "Right stick X:" << fmtF(mSnap.rx) << " Y:" << fmtF(mSnap.ry) << "\n";
    if (changed(mSnap.l2, mPrevSnap.l2))
        std::cout << "L2 analog: " << fmtF(mSnap.l2) << "\n";
    if (changed(mSnap.r2, mPrevSnap.r2))
        std::cout << "R2 analog: " << fmtF(mSnap.r2) << "\n";
}
