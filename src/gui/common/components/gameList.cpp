#include "common/components/gameList.h"

#include "config/ActiveSettings.h"

#include <algorithm>
#include <fstream>

namespace gui::common
{
void StripPathFilename(std::filesystem::path& path)
{
	if (path.has_extension())
		path = path.parent_path();
}

std::vector<std::filesystem::path> GetCachesPaths(uint64_t titleId)
{
	std::vector<std::filesystem::path> cachePaths{
		ActiveSettings::GetCachePath(L"shaderCache/driver/vk/{:016x}.bin", titleId),
		ActiveSettings::GetCachePath(L"shaderCache/precompiled/{:016x}_spirv.bin", titleId),
		ActiveSettings::GetCachePath(L"shaderCache/precompiled/{:016x}_gl.bin", titleId),
		ActiveSettings::GetCachePath(L"shaderCache/precompiled/{:016x}_air.bin", titleId),
		ActiveSettings::GetCachePath(L"shaderCache/transferable/{:016x}_shaders.bin", titleId),
		ActiveSettings::GetCachePath(L"shaderCache/transferable/{:016x}_mtlshaders.bin", titleId),
		ActiveSettings::GetCachePath(L"shaderCache/transferable/{:016x}_vkpipeline.bin", titleId),
		ActiveSettings::GetCachePath(L"shaderCache/transferable/{:016x}_mtlpipeline.bin", titleId)};

	cachePaths.erase(std::remove_if(cachePaths.begin(), cachePaths.end(),
								[](const std::filesystem::path& cachePath) {
									std::error_code ec;
									return !std::filesystem::exists(cachePath, ec);
								}),
		 cachePaths.end());

	return cachePaths;
}

bool WriteICNS(const std::filesystem::path& pngPath, const std::filesystem::path& icnsPath)
{
	std::ifstream pngFile(pngPath, std::ios::binary);
	if (!pngFile)
		return false;

	pngFile.seekg(0, std::ios::end);
	uint32 pngSize = static_cast<uint32>(pngFile.tellg());
	pngFile.seekg(0, std::ios::beg);

	const uint32 totalSize = 8 + 8 + pngSize;

	std::ofstream icnsFile(icnsPath, std::ios::binary);
	if (!icnsFile)
		return false;

	icnsFile.put(0x69);
	icnsFile.put(0x63);
	icnsFile.put(0x6e);
	icnsFile.put(0x73);

	icnsFile.put((totalSize >> 24) & 0xFF);
	icnsFile.put((totalSize >> 16) & 0xFF);
	icnsFile.put((totalSize >> 8) & 0xFF);
	icnsFile.put(totalSize & 0xFF);

	icnsFile.put(0x69);
	icnsFile.put(0x63);
	icnsFile.put(0x30);
	icnsFile.put(0x37);

	icnsFile.put((pngSize >> 24) & 0xFF);
	icnsFile.put((pngSize >> 16) & 0xFF);
	icnsFile.put((pngSize >> 8) & 0xFF);
	icnsFile.put(pngSize & 0xFF);

	icnsFile << pngFile.rdbuf();
	return true;
}

std::string BuildLinuxDesktopExecEntry(std::string_view executablePathUtf8, uint64_t titleId, const char* flatpakId)
{
	if (flatpakId)
		return fmt::format("/usr/bin/flatpak run {0} --title-id {1:016x}", flatpakId, titleId);
	return fmt::format("{0:?} --title-id {1:016x}", executablePathUtf8, titleId);
}

std::string BuildLinuxDesktopEntry(
	std::string_view titleName,
	std::string_view desktopExecEntry,
	std::string_view iconPathUtf8,
	const char* flatpakId)
{
	auto desktopEntryString = fmt::format(
		"[Desktop Entry]\n"
		"Name={0}\n"
		"Comment=Play {0} on Cemu\n"
		"Exec={1}\n"
		"Icon={2}\n"
		"Terminal=false\n"
		"Type=Application\n"
		"Categories=Game;\n",
		titleName,
		desktopExecEntry,
		iconPathUtf8);

	if (flatpakId)
		desktopEntryString += fmt::format("X-Flatpak={}\n", flatpakId);

	return desktopEntryString;
}

std::string BuildMacRunCommand(std::string_view executablePathUtf8, uint64_t titleId)
{
	return fmt::format("#!/bin/zsh\n\n{0:?} --title-id {1:016x}", executablePathUtf8, titleId);
}

std::string BuildMacInfoPlist(std::string_view titleName, std::string_view version)
{
	return fmt::format(
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
		"<plist version=\"1.0\">\n"
		"<dict>\n"
		"\t<key>CFBundleDisplayName</key>\n"
		"\t<string>{0}</string>\n"
		"\t<key>CFBundleExecutable</key>\n"
		"\t<string>run.sh</string>\n"
		"\t<key>CFBundleIconFile</key>\n"
		"\t<string>shortcut.icns</string>\n"
		"\t<key>CFBundleName</key>\n"
		"\t<string>{0}</string>\n"
		"\t<key>CFBundlePackageType</key>\n"
		"\t<string>APPL</string>\n"
		"\t<key>CFBundleSignature</key>\n"
		"\t<string>\?\?\?\?</string>\n"
		"\t<key>LSApplicationCategoryType</key>\n"
		"\t<string>public.app-category.games</string>\n"
		"\t<key>CFBundleShortVersionString</key>\n"
		"\t<string>{1}</string>\n"
		"\t<key>CFBundleVersion</key>\n"
		"\t<string>{1}</string>\n"
		"</dict>\n"
		"</plist>\n",
		titleName,
		version);
}
}
