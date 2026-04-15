#include "SwiftUICemuConfig.h"
#include "Common/precompiled.h"
#include "config/CemuConfig.h"
#include "config/XMLConfig.h"
#include "util/helpers/helpers.h"

void SwiftUICemuConfig::AddRecentlyLaunchedFile(std::string_view file)
{
	recent_launch_files.insert(recent_launch_files.begin(), std::string(file));
	RemoveDuplicatesKeepOrder(recent_launch_files);
	while (recent_launch_files.size() > kMaxRecentEntries)
		recent_launch_files.pop_back();
}

void SwiftUICemuConfig::AddRecentNfcFile(std::string_view file)
{
	recent_nfc_files.insert(recent_nfc_files.begin(), std::string(file));
	RemoveDuplicatesKeepOrder(recent_nfc_files);
	while (recent_nfc_files.size() > kMaxRecentEntries)
		recent_nfc_files.pop_back();
}

void SwiftUICemuConfig::Load(XMLConfigParser& parser)
{
	language = parser.get<sint32>("language", 0);
	use_discord_presence = parser.get("use_discord_presence", true);
	feral_gamemode = parser.get("feral_gamemode", false);
	check_update = parser.get("check_update", true);
	receive_untested_updates = parser.get("receive_untested_updates", false);

	auto launch_parser = parser.get("RecentLaunchFiles");
	for (auto element = launch_parser.get("Entry"); element.valid(); element = launch_parser.get("Entry", element))
	{
		const std::string path = element.value("");
		if (path.empty())
			continue;

		try
		{
			recent_launch_files.emplace_back(path);
		} catch (const std::exception&)
		{
			cemuLog_log(LogType::Force, "config load error: can't load recently launched game file: {}", path);
		}
	}

	auto nfc_parser = parser.get("RecentNFCFiles");
	for (auto element = nfc_parser.get("Entry"); element.valid(); element = nfc_parser.get("Entry", element))
	{
		const std::string path = element.value("");
		if (path.empty())
			continue;
		try
		{
			recent_nfc_files.emplace_back(path);
		} catch (const std::exception&)
		{
			cemuLog_log(LogType::Force, "config load error: can't load recently launched nfc file: {}", path);
		}
	}
}

void SwiftUICemuConfig::Save(XMLConfigParser& config)
{
	config.set<sint32>("language", language);
	config.set<bool>("use_discord_presence", use_discord_presence);
	config.set<bool>("feral_gamemode", feral_gamemode);
	config.set<bool>("check_update", check_update);
	config.set<bool>("receive_untested_updates", receive_untested_updates);

	auto launch_parser = config.get("RecentLaunchFiles");
	for (size_t i = 0; i < recent_launch_files.size(); i++)
		launch_parser.set(fmt::format("Entry{}", i).c_str(), recent_launch_files[i]);

	auto nfc_parser = config.get("RecentNFCFiles");
	for (size_t i = 0; i < recent_nfc_files.size(); i++)
		nfc_parser.set(fmt::format("Entry{}", i).c_str(), recent_nfc_files[i]);
}
