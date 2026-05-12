#pragma once

#include <TargetConditionals.h>
#if TARGET_OS_IOS
#ifndef HAS_IOS_TOUCH
#define HAS_IOS_TOUCH 1
#endif
#endif

#include "input/api/ControllerProvider.h"

class IOSTouchControllerProvider : public ControllerProviderBase
{
public:
	inline static InputAPI::Type kAPIType = InputAPI::iOSTouch;
	InputAPI::Type api() const override { return kAPIType; }

	std::vector<std::shared_ptr<ControllerBase>> get_controllers() override;

private:
    std::vector<std::shared_ptr<ControllerBase>> m_controllers;
};
