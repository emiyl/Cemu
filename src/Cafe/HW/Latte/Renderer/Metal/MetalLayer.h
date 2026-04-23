#pragma once

void* CreateMetalLayer(void* handle, float& scaleX, float& scaleY);

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IOS
void ConfigureMetalLayerForIOS(void* layer);
#endif
#endif
