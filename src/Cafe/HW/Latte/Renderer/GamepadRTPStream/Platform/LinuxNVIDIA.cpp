#if BOOST_OS_LINUX
#include "Cafe/HW/Latte/Renderer/GamepadRTPStream/Platform.h"
#include <dlfcn.h>

namespace GamepadRTPStream::Platform
{
bool LinuxNVIDIAAvailable()
{
    void* cuda = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
    void* nvenc = dlopen("libnvidia-encode.so.1", RTLD_NOW | RTLD_LOCAL);
    if (cuda) dlclose(cuda);
    if (nvenc) dlclose(nvenc);
    return cuda != nullptr && nvenc != nullptr;
}
}
#endif
