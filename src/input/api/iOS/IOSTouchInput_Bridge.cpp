// Bridge between SwiftUI and the iOS touch controller

#include "Common/precompiled.h"
#include "input/api/iOS/IOSTouchController.h"
#include "input/api/iOS/IOSTouchControllerProvider.h"
#include "input/InputManager.h"
#include "input/emulated/VPADController.h"

static std::shared_ptr<IOSTouchController> g_touch_controller;
// Mirror stick state so we never read back from the controller (avoids re-locking the mutex)
static glm::vec2 g_left_stick{0.0f, 0.0f};
static glm::vec2 g_right_stick{0.0f, 0.0f};

// Automatically bind the touch controller to VPAD player 0 when no profile exists.
// Mapping keys are VPADController::ButtonId; physical button IDs are the VPAD flag
// bit values because raw_state() emits each set bit as an individual button ID.
static void ApplyDefaultTouchMappings()
{
    auto vpad = InputManager::instance().get_vpad_controller(0);
    if (!vpad)
    {
        auto emulated = InputManager::instance().set_controller(0, EmulatedController::Type::VPAD);
        vpad = std::dynamic_pointer_cast<VPADController>(emulated);
    }
    if (!vpad || !vpad->get_controllers().empty())
        return; // already configured

    vpad->add_controller(g_touch_controller);

    using B = VPADController::ButtonId;

    // Digital face / shoulder / system buttons
    vpad->set_mapping(B::kButtonId_A,     g_touch_controller, 0x8000u); // VPAD_A
    vpad->set_mapping(B::kButtonId_B,     g_touch_controller, 0x4000u); // VPAD_B
    vpad->set_mapping(B::kButtonId_X,     g_touch_controller, 0x2000u); // VPAD_X
    vpad->set_mapping(B::kButtonId_Y,     g_touch_controller, 0x1000u); // VPAD_Y
    vpad->set_mapping(B::kButtonId_L,     g_touch_controller, 0x0020u); // VPAD_L
    vpad->set_mapping(B::kButtonId_R,     g_touch_controller, 0x0010u); // VPAD_R
    vpad->set_mapping(B::kButtonId_ZL,    g_touch_controller, 0x0080u); // VPAD_ZL
    vpad->set_mapping(B::kButtonId_ZR,    g_touch_controller, 0x0040u); // VPAD_ZR
    vpad->set_mapping(B::kButtonId_Plus,  g_touch_controller, 0x0008u); // VPAD_PLUS
    vpad->set_mapping(B::kButtonId_Minus, g_touch_controller, 0x0004u); // VPAD_MINUS
    vpad->set_mapping(B::kButtonId_Home,  g_touch_controller, 0x0002u); // VPAD_HOME

    // D-pad
    vpad->set_mapping(B::kButtonId_Up,    g_touch_controller, 0x0200u); // VPAD_UP
    vpad->set_mapping(B::kButtonId_Down,  g_touch_controller, 0x0100u); // VPAD_DOWN
    vpad->set_mapping(B::kButtonId_Left,  g_touch_controller, 0x0800u); // VPAD_LEFT
    vpad->set_mapping(B::kButtonId_Right, g_touch_controller, 0x0400u); // VPAD_RIGHT

    // Left analog stick — kAxisXP=38 kAxisYP=39 kAxisXN=44 kAxisYN=45
    // In UIKit: right=+x, down=+y, so "up" maps to negative Y (kAxisYN)
    vpad->set_mapping(B::kButtonId_StickL_Right, g_touch_controller, kAxisXP);
    vpad->set_mapping(B::kButtonId_StickL_Left,  g_touch_controller, kAxisXN);
    vpad->set_mapping(B::kButtonId_StickL_Up,    g_touch_controller, kAxisYN);
    vpad->set_mapping(B::kButtonId_StickL_Down,  g_touch_controller, kAxisYP);

    // Right analog stick — kRotationXP=40 kRotationYP=41 kRotationXN=46 kRotationYN=47
    vpad->set_mapping(B::kButtonId_StickR_Right, g_touch_controller, kRotationXP);
    vpad->set_mapping(B::kButtonId_StickR_Left,  g_touch_controller, kRotationXN);
    vpad->set_mapping(B::kButtonId_StickR_Up,    g_touch_controller, kRotationYN);
    vpad->set_mapping(B::kButtonId_StickR_Down,  g_touch_controller, kRotationYP);
}

static void EnsureController()
{
    if (g_touch_controller)
        return;
    if (!InputManager::instance().is_api_available(InputAPI::iOSTouch))
        return;
    auto provider = std::dynamic_pointer_cast<IOSTouchControllerProvider>(
        InputManager::instance().get_api_provider(InputAPI::iOSTouch));
    if (!provider)
        return;
    auto controllers = provider->get_controllers();
    if (!controllers.empty())
        g_touch_controller = std::dynamic_pointer_cast<IOSTouchController>(controllers[0]);

    if (g_touch_controller)
        ApplyDefaultTouchMappings();
}

extern "C" {
    void IOSTouchInput_Initialize()
    {
        EnsureController();
    }

    void IOSTouchInput_ButtonPressed(uint64_t buttonMask)
    {
        EnsureController();
        if (g_touch_controller)
            g_touch_controller->SetButtonState(buttonMask, true);
    }

    void IOSTouchInput_ButtonReleased(uint64_t buttonMask)
    {
        EnsureController();
        if (g_touch_controller)
            g_touch_controller->SetButtonState(buttonMask, false);
    }

    void IOSTouchInput_SetLeftStick(float x, float y)
    {
        EnsureController();
        g_left_stick = {x, y};
        if (g_touch_controller)
            g_touch_controller->SetAxisState(g_left_stick, g_right_stick);
    }

    void IOSTouchInput_SetRightStick(float x, float y)
    {
        EnsureController();
        g_right_stick = {x, y};
        if (g_touch_controller)
            g_touch_controller->SetAxisState(g_left_stick, g_right_stick);
    }

    void IOSTouchInput_ClearAllButtons()
    {
        EnsureController();
        g_left_stick = {0.0f, 0.0f};
        g_right_stick = {0.0f, 0.0f};
        if (g_touch_controller)
        {
            g_touch_controller->SetButtonState(~uint64_t(0), false);
            g_touch_controller->SetAxisState({0.0f, 0.0f}, {0.0f, 0.0f});
        }
    }
}
