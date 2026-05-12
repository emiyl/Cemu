#include "Common/precompiled.h"
#include "input/api/iOS/IOSTouchController.h"

IOSTouchController::IOSTouchController()
	: base_type("ios_touch_0", "iOS Touch")
{
}

std::string IOSTouchController::get_button_name(uint64 button) const
{
	return {};
}

void IOSTouchController::SetButtonState(uint64 buttonMask, bool pressed)
{
	std::lock_guard lock(m_state_mutex);
	if (pressed)
		m_button_bitmask |= buttonMask;
	else
		m_button_bitmask &= ~buttonMask;
}

void IOSTouchController::SetAxisState(glm::vec2 leftStick, glm::vec2 rightStick)
{
	std::lock_guard lock(m_state_mutex);
	m_left_stick = leftStick;
	m_right_stick = rightStick;
}

ControllerState IOSTouchController::raw_state()
{
	std::lock_guard lock(m_state_mutex);
	ControllerState result{};
	// Each set bit in the bitmask becomes an individual button ID.
	// VPAD flag values (0x8000, 0x4000, …) are all within uint32 range
	// so they map cleanly to the uint32 button-ID space.
	uint64 mask = m_button_bitmask;
	while (mask)
	{
		const uint32 bitVal = static_cast<uint32>(mask & static_cast<uint64>(-static_cast<sint64>(mask)));
		result.buttons.SetButtonState(bitVal, true);
		mask &= mask - 1;
	}
	result.axis = m_left_stick;
	result.rotation = m_right_stick;
	return result;
}
