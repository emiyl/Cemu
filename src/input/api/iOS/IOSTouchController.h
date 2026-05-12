#pragma once

#include "input/api/Controller.h"
#include "input/api/iOS/IOSTouchControllerProvider.h"
#include <mutex>
#include <glm/vec2.hpp>

class IOSTouchController : public Controller<IOSTouchControllerProvider>
{
public:
	IOSTouchController();

	std::string_view api_name() const override { return "iOSTouch"; }
	InputAPI::Type api() const override { return InputAPI::iOSTouch; }

	bool is_connected() override { return true; }
	bool has_axis() const override { return true; }

	std::string get_button_name(uint64 button) const override;

	// Called from SwiftUI bridge; buttonMask uses VPAD flag bit values
	void SetButtonState(uint64 buttonMask, bool pressed);
	void SetAxisState(glm::vec2 leftStick, glm::vec2 rightStick);

protected:
	ControllerState raw_state() override;

private:
	mutable std::mutex m_state_mutex;
	uint64 m_button_bitmask{0};
	glm::vec2 m_left_stick{0.0f, 0.0f};
	glm::vec2 m_right_stick{0.0f, 0.0f};
};
