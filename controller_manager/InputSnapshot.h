// InputSnapshot.h
// Plain data struct holding one frame of controller input state.
// Used by both ControllerManager and XInputManager.
#pragma once

struct InputSnapshot
{
    bool connected = false;

    bool cross, square, triangle, circle;
    bool dpadUp, dpadDown, dpadLeft, dpadRight;
    bool l1, r1;
    bool l2pressed, r2pressed;
    float l2 = 0.f, r2 = 0.f;
    bool l3, r3;
    float lx = 0.f, ly = 0.f, rx = 0.f, ry = 0.f;
    bool lLeft, lRight, lUp, lDown;
    bool rLeft, rRight, rUp, rDown;
    bool psButton, share, start, touchClick, mute;
    bool isTouching;
    float battery = 0.f;
};
