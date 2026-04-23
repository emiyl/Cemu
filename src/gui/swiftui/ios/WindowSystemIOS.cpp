#include "Common/precompiled.h"

#include "interface/WindowSystem.h"

#include "Cafe/TitleList/TitleList.h"

namespace
{
WindowSystem::WindowInfo g_window_info{};
}

void WindowSystem::ShowErrorDialog(std::string_view message, std::string_view title, std::optional<WindowSystem::ErrorCategory> /*errorCategory*/)
{
    if (!title.empty())
        fmt::print(stderr, "{}: {}\n", title, message);
    else
        fmt::print(stderr, "{}\n", message);
}

void WindowSystem::Create()
{
    // iOS app lifecycle is managed by SwiftUI (@main App).
}

WindowSystem::WindowInfo& WindowSystem::GetWindowInfo()
{
    return g_window_info;
}

void WindowSystem::UpdateWindowTitles(bool /*isIdle*/, bool /*isLoading*/, double /*fps*/)
{
}

void WindowSystem::GetWindowSize(int& w, int& h)
{
    w = g_window_info.width;
    h = g_window_info.height;
}

void WindowSystem::GetPadWindowSize(int& w, int& h)
{
    if (g_window_info.pad_open)
    {
        w = g_window_info.pad_width;
        h = g_window_info.pad_height;
    }
    else
    {
        w = 0;
        h = 0;
    }
}

void WindowSystem::GetWindowPhysSize(int& w, int& h)
{
    w = g_window_info.phys_width;
    h = g_window_info.phys_height;
}

void WindowSystem::GetPadWindowPhysSize(int& w, int& h)
{
    if (g_window_info.pad_open)
    {
        w = g_window_info.phys_pad_width;
        h = g_window_info.phys_pad_height;
    }
    else
    {
        w = 0;
        h = 0;
    }
}

double WindowSystem::GetWindowDPIScale()
{
    return g_window_info.dpi_scale;
}

double WindowSystem::GetPadDPIScale()
{
    return g_window_info.pad_open ? g_window_info.pad_dpi_scale.load() : 1.0;
}

bool WindowSystem::IsPadWindowOpen()
{
    return g_window_info.pad_open;
}

bool WindowSystem::IsKeyDown(uint32 key)
{
    return g_window_info.get_keystate(key);
}

bool WindowSystem::IsKeyDown(PlatformKeyCodes /*key*/)
{
    return false;
}

std::string WindowSystem::GetKeyCodeName(uint32 key)
{
    return fmt::format("key_{}", key);
}

bool WindowSystem::InputConfigWindowHasFocus()
{
    return false;
}


void WindowSystem::RefreshGameList()
{
    CafeTitleList::Refresh();
}

bool WindowSystem::IsFullScreen()
{
    return g_window_info.is_fullscreen;
}

void WindowSystem::CaptureInput(const ControllerState& currentState, const ControllerState& lastState)
{
    (void)currentState;
    (void)lastState;
}
