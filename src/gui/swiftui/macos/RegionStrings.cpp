#include "gui/swiftui/macos/RegionStrings.h"

namespace swiftui
{
static const char* CafeConsoleRegionToEnumKey(CafeConsoleRegion region)
{
    switch (region)
    {
        case CafeConsoleRegion::JPN:
            return "JPN";
        case CafeConsoleRegion::USA:
            return "USA";
        case CafeConsoleRegion::EUR:
            return "EUR";
        case CafeConsoleRegion::AUS_DEPR:
            return "AUS_DEPR";
        case CafeConsoleRegion::CHN:
            return "CHN";
        case CafeConsoleRegion::KOR:
            return "KOR";
        case CafeConsoleRegion::TWN:
            return "TWN";
        case CafeConsoleRegion::Auto:
            return "Auto";
        default:
            return "";
    }
}

std::string CafeConsoleRegionToDisplayKey(CafeConsoleRegion region)
{
    std::string key = CafeConsoleRegionToEnumKey(region);
    if (const auto underscorePos = key.find('_'); underscorePos != std::string::npos)
        key.resize(underscorePos);
    return key;
}
} // namespace swiftui
