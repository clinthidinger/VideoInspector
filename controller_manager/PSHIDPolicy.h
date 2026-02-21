// PSHIDPolicy.h
// Sony DualShock4 / DualSense HID platform policy for use with
// GamepadCore::TGenericHardwareInfo and TBasicDeviceRegistry.
#pragma once

#include "GCore/Interfaces/IPlatformHardwareInfo.h"
#include "GCore/Templates/TBasicDeviceRegistry.h"
#include "GCore/Templates/TGenericHardwareInfo.h"
#include "GCore/Types/ECoreGamepad.h"
#include "GCore/Types/Structs/Context/DeviceContext.h"
#include "GCore/Types/Structs/Context/InputContext.h"

#include <hidapi.h>
#include <cstdint>
#include <vector>

// ----------------------------------------------------------------------------
// HIDPolicy — implements OS-level HID calls using hidapi.
// Satisfies GamepadCore::IsHardwarePolicy.
// ----------------------------------------------------------------------------
struct HIDPolicy
{
    void Read           (FDeviceContext* ctx);
    void Write          (FDeviceContext* ctx);
    void Detect         (std::vector<FDeviceContext>& devices);
    bool CreateHandle   (FDeviceContext* ctx);
    void InvalidateHandle(FDeviceContext* ctx);
    void ProcessAudioHaptic  (FDeviceContext*);
    void InitializeAudioDevice(FDeviceContext*);
};

// ----------------------------------------------------------------------------
// AppRegistryPolicy — simple integer device IDs.
// Satisfies GamepadCore::DeviceRegistryPolicy.
// ----------------------------------------------------------------------------
struct AppRegistryPolicy
{
    using EngineIdType = int;
    using Hasher       = std::hash<int>;

    int mNextId = 0;

    int  AllocEngineDevice();
    void DisconnectDevice  (int id);
    void DispatchNewGamepad(int id);
};
