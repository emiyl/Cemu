#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace swiftui
{
class MainWindow
{
  public:
	enum class LaunchInitiatedBy
	{
		kGameList,
		kMenu,
		kCommandLine,
	};

	static bool RequestLaunchGame(const fs::path& launchPath, LaunchInitiatedBy initiatedBy, std::string& errorOut);
	static bool RequestLaunchGameByTitleId(uint64_t titleId, std::string& errorOut);

  private:
	static bool PrepareLaunchPath(const fs::path& launchPath, LaunchInitiatedBy initiatedBy, std::string& errorOut);
};
} // namespace swiftui
