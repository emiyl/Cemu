#include "Common/precompiled.h"
#include "input/api/iOS/IOSTouchControllerProvider.h"
#include "input/api/iOS/IOSTouchController.h"

std::vector<std::shared_ptr<ControllerBase>> IOSTouchControllerProvider::get_controllers()
{
	if (!m_controllers.empty())
		return m_controllers;

	m_controllers.push_back(std::make_shared<IOSTouchController>());
	return m_controllers;
}
