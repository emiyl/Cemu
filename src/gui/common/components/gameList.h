#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui::common
{
void StripPathFilename(std::filesystem::path& path);
std::vector<std::filesystem::path> GetCachesPaths(uint64_t titleId);
bool WriteICNS(const std::filesystem::path& pngPath, const std::filesystem::path& icnsPath);

std::string BuildLinuxDesktopExecEntry(std::string_view executablePathUtf8, uint64_t titleId, const char* flatpakId);
std::string BuildLinuxDesktopEntry(
	std::string_view titleName,
	std::string_view desktopExecEntry,
	std::string_view iconPathUtf8,
	const char* flatpakId);

std::string BuildMacRunCommand(std::string_view executablePathUtf8, uint64_t titleId);
std::string BuildMacInfoPlist(std::string_view titleName, std::string_view version);
}
