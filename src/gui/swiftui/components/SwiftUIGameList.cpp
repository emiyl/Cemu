#include "gui/swiftui/components/SwiftUIGameList.h"

#include "Cafe/IOSU/PDM/iosu_pdm.h"
#include "Cafe/TitleList/GameInfo.h"
#include "Cafe/TitleList/TitleList.h"
#include "config/CemuConfig.h"
#include "interface/WindowSystem.h"

#include <boost/algorithm/string.hpp>

#include <atomic>
#include <mutex>

struct GameInfo
{
	uint64_t titleId;
	std::string name;
	uint32_t version;
	bool hasDLC;
	std::string region;
};

namespace TitleList
{
	using Callback = std::function<void(bool discovered, uint64_t titleId)>;
	int RegisterCallback(Callback callback);
	void UnregisterCallback(int callbackId);
	void Refresh();
	bool GetInfo(uint64_t titleId, GameInfo& out);
} // namespace TitleList

class GameList
{
  public:
	using EntryChangedCallback = std::function<void(uint64_t titleId)>;

	GameList()
	{
		m_titleListCallbackId = TitleList::RegisterCallback([this](bool discovered, uint64_t titleId) {
			OnTitleEvent(discovered, titleId);
		});
	}

	~GameList()
	{
		TitleList::UnregisterCallback(m_titleListCallbackId);

		m_running = false;
	}

	void Refresh()
	{
		{
			std::lock_guard lock(m_entriesMutex);
			m_entries.clear();
		}

		TitleList::UnregisterCallback(m_titleListCallbackId);
		m_titleListCallbackId = TitleList::RegisterCallback(
			[this](bool discovered, uint64_t titleId) {
				OnTitleEvent(discovered, titleId);
			});
	}

	std::vector<GameInfo> GetEntries()
	{
		std::lock_guard lock(m_entriesMutex);
		std::vector<GameInfo> entries;
		for (const auto& [titleId, info] : m_entries)
		{
			entries.push_back(info);
		}
		return entries;
	}

	void SetOnEntryChanged(EntryChangedCallback cb)
	{
		m_onEntryChanged = std::move(cb);
	}

  private:
	void OnTitleEvent(bool discovered, uint64_t titleId)
	{
		if (discovered)
		{
			GameInfo info;
			if (!TitleList::GetInfo(titleId, info))
				return;

			{
				std::lock_guard lock(m_entriesMutex);
				m_entries[titleId] = info;
			}
		}
		else
		{
			std::lock_guard lock(m_entriesMutex);
			m_entries.erase(titleId);
		}

		if (m_onEntryChanged)
			m_onEntryChanged(titleId);
	}

	mutable std::mutex m_entriesMutex;
	std::unordered_map<uint64_t, GameInfo> m_entries;

	std::atomic<bool> m_running{true};

	int m_titleListCallbackId = -1;
	EntryChangedCallback m_onEntryChanged;
};

namespace
{
	std::string FormatPlayedTime(uint32 minutesPlayed)
	{
		if (minutesPlayed == 0)
			return "";
		if (minutesPlayed < 60)
			return fmt::format("{} minute{}", minutesPlayed,
							   minutesPlayed == 1 ? "" : "s");

		const uint32 hours = minutesPlayed / 60;
		const uint32 minutes = minutesPlayed % 60;
		return fmt::format("{} hour{} {} minute{}", hours, hours == 1 ? "" : "s",
						   minutes, minutes == 1 ? "" : "s");
	}

	std::string FormatLastPlayed(const iosu::pdm::GameListStat::LastPlayDate& date)
	{
		if (date.year == 0)
			return "never";
		return fmt::format("{}/{}/{}", date.month, date.day, date.year);
	}

	std::string GetDisplayName(TitleId titleId, GameInfo2& gameInfo)
	{
		std::string customName;
		if (GetConfig().GetGameListCustomName(titleId, customName) && !customName.empty())
			return customName;
		return gameInfo.GetTitleName();
	}
} // namespace

static GameList* g_gameList = nullptr;
static GameListCallback g_callback = nullptr;

extern "C" void CemuSwiftUIGameListCreate(void)
{
	if (g_gameList)
		return;

	g_gameList = new GameList();

	g_gameList->SetOnEntryChanged([](uint64_t titleId) {
		if (g_callback)
			g_callback(titleId);
	});
}

extern "C" void CemuSwiftUIGameListDestroy(void)
{
	delete g_gameList;
	g_gameList = nullptr;
}

extern "C" void CemuSwiftUIGameListRefresh(void)
{
	if (g_gameList)
		g_gameList->Refresh();
	WindowSystem::RefreshGameList();
}

extern "C" size_t CemuSwiftUIGameListGetCount(void)
{
	if (!g_gameList)
		return 0;
	return g_gameList->GetEntries().size();
}

extern "C" bool CemuSwiftUIGameListGetRow(size_t index, CemuSwiftUIGameListRow* outRow)
{
	if (!g_gameList)
		return false;

	const auto entries = g_gameList->GetEntries();
	if (index >= entries.size())
		return false;

	const auto& entry = entries[index];
	// outRow->titleId = 0;
	// outRow->name = strdup(std::string("Hello").c_str());
	outRow->titleId = entry.titleId;
	outRow->name = strdup(entry.name.c_str());
	return true;
}