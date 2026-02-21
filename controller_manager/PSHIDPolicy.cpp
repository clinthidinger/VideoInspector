#include "PSHIDPolicy.h"

#include <iostream>

// ----------------------------------------------------------------------------
// HIDPolicy
// ----------------------------------------------------------------------------

void HIDPolicy::Read(FDeviceContext* ctx)
{
    if (!ctx->Handle) return;
    auto* dev = static_cast<hid_device*>(ctx->Handle);

    if (ctx->ConnectionType == EDSDeviceConnection::Bluetooth)
    {
        int n = hid_read_timeout(dev, ctx->BufferDS4, sizeof(ctx->BufferDS4), 2);
        if (n < 0) ctx->IsConnected = false;
    }
    else
    {
        int n = hid_read_timeout(dev, ctx->Buffer, sizeof(ctx->Buffer), 2);
        if (n < 0) ctx->IsConnected = false;
    }
}

void HIDPolicy::Write(FDeviceContext* ctx)
{
    if (!ctx->Handle) return;
    auto* dev = static_cast<hid_device*>(ctx->Handle);
    hid_write(dev, ctx->GetRawOutputBuffer(), 78);
}

void HIDPolicy::Detect(std::vector<FDeviceContext>& devices)
{
    static const struct { uint16_t pid; EDSDeviceType type; } kSony[] = {
        { 0x05C4, EDSDeviceType::DualShock4   },  // DS4 v1
        { 0x09CC, EDSDeviceType::DualShock4   },  // DS4 v2
        { 0x0CE6, EDSDeviceType::DualSense    },  // DualSense
        { 0x0DF2, EDSDeviceType::DualSenseEdge},  // DualSense Edge
    };

    for (auto& desc : kSony)
    {
        hid_device_info* info = hid_enumerate(0x054C, desc.pid);
        for (auto* cur = info; cur; cur = cur->next)
        {
            if (cur->interface_number > 0) continue;

            FDeviceContext ctx;
            ctx.Path       = cur->path;
            ctx.DeviceType = desc.type;
            ctx.ConnectionType = (cur->interface_number == 0)
                ? EDSDeviceConnection::Usb
                : EDSDeviceConnection::Bluetooth;
            devices.push_back(ctx);
        }
        hid_free_enumeration(info);
    }
}

bool HIDPolicy::CreateHandle(FDeviceContext* ctx)
{
    auto* dev = hid_open_path(ctx->Path.c_str());
    if (!dev) return false;
    hid_set_nonblocking(dev, 1);
    ctx->Handle      = dev;
    ctx->IsConnected = true;
    return true;
}

void HIDPolicy::InvalidateHandle(FDeviceContext* ctx)
{
    if (ctx->Handle)
    {
        hid_close(static_cast<hid_device*>(ctx->Handle));
        ctx->Handle = nullptr;
    }
    ctx->IsConnected = false;
}

void HIDPolicy::ProcessAudioHaptic  (FDeviceContext*) {}
void HIDPolicy::InitializeAudioDevice(FDeviceContext*) {}

// ----------------------------------------------------------------------------
// AppRegistryPolicy
// ----------------------------------------------------------------------------

int AppRegistryPolicy::AllocEngineDevice()
{
    return mNextId++;
}

void AppRegistryPolicy::DisconnectDevice(int id)
{
    std::cout << "[Gamepad] controller " << id << " disconnected\n";
}

void AppRegistryPolicy::DispatchNewGamepad(int id)
{
    std::cout << "[Gamepad] controller " << id << " connected\n";
}
