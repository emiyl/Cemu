#pragma once

#include "config/XMLConfig.h"
#include "util/math/vector2.h"

struct SwiftUICemuConfig
{
	ConfigValue<sint32> language{0};
	ConfigValue<bool> use_discord_presence{true};
	ConfigValue<bool> save_screenshots{true};
	ConfigValue<bool> feral_gamemode{false};

	static constexpr size_t kMaxRecentEntries = 15;
	std::vector<std::string> recent_launch_files;
	std::vector<std::string> recent_nfc_files;

	ConfigValue<bool> check_update{true};
	ConfigValue<bool> receive_untested_updates{false};

	void AddRecentlyLaunchedFile(std::string_view file);
	void AddRecentNfcFile(std::string_view file);

	void Load(XMLConfigParser& parser);
	void Save(XMLConfigParser& parser);
};

extern SwiftUICemuConfig g_cemuConfig;