cmake_minimum_required(VERSION 3.20)

project(ControllerTestApp LANGUAGES CXX)

# ---- Third-party dependencies ----------------------------------------------
# Point directly at Source to avoid the project() call in the top-level
# Dualsense-Multiplatform/CMakeLists.txt, which would re-run compiler detection
# and break when used as a subdirectory of another project.
add_subdirectory(third_party/Dualsense-Multiplatform/Source)

# The top-level Dualsense CMakeLists normally sets this; we set it here since
# we're skipping it. Without audio hardware, this is always 0.
target_compile_definitions(GamepadCore PUBLIC GAMEPAD_CORE_HAS_AUDIO=0)

# On Linux, prefer hidraw (better for gamepads); disable libusb to keep it simple.
# Remove these two lines if you want the libusb backend as well.
set(HIDAPI_WITH_LIBUSB OFF CACHE BOOL "" FORCE)
set(HIDAPI_WITH_HIDRAW ON  CACHE BOOL "" FORCE)
add_subdirectory(third_party/hidapi)

# ---- ControllerManager static library --------------------------------------
add_library(ControllerManager STATIC
    ControllerManager.cpp
    PSHIDPolicy.cpp
    XInputManager.cpp
)

# Export this directory so consumers can #include "ControllerManager.h" etc.
# GamepadCore's PUBLIC include dirs propagate transitively via target_link_libraries.
target_include_directories(ControllerManager PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ControllerManager PUBLIC
    GamepadCore
    hidapi::hidapi
)

target_compile_features(ControllerManager PUBLIC cxx_std_20)

if(WIN32)
    target_link_libraries(ControllerManager PUBLIC Xinput.lib)
endif()
