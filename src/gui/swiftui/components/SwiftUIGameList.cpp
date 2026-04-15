#include "gui/swiftui/components/SwiftUIGameList.h"

#include "Cafe/IOSU/PDM/iosu_pdm.h"
#include "Cafe/TitleList/GameInfo.h"
#include "Cafe/TitleList/TitleList.h"
#include "config/CemuConfig.h"
#include "interface/WindowSystem.h"

#include <boost/algorithm/string.hpp>

#include <mutex>

struct GameInfo
{
	uint64_t titleId;
	std::string name;
	uint32_t version;
	bool hasDLC;
	std::string region;
};

class GameList
{
  public:
	using EntryChangedCallback = std::function<void(uint64_t titleId)>;

	GameList()
	{
		m_titleListCallbackId = CafeTitleList::RegisterCallback(&GameList::OnTitleListEvent, this);
	}

	~GameList()
	{
		if (m_titleListCallbackId != 0)
			CafeTitleList::UnregisterCallback(m_titleListCallbackId);
	}

	void Refresh()
	{
		{
			std::lock_guard lock(m_entriesMutex);
			m_entries.clear();
		}

		CafeTitleList::Refresh();

		if (m_titleListCallbackId != 0)
			CafeTitleList::UnregisterCallback(m_titleListCallbackId);
		m_titleListCallbackId = CafeTitleList::RegisterCallback(&GameList::OnTitleListEvent, this);
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
	static void OnTitleListEvent(CafeTitleListCallbackEvent* evt, void* ctx)
	{
		if (!ctx || !evt || !evt->titleInfo)
			return;
		if (evt->eventType != CafeTitleListCallbackEvent::TYPE::TITLE_DISCOVERED &&
			evt->eventType != CafeTitleListCallbackEvent::TYPE::TITLE_REMOVED)
			return;

		auto* gameList = reinterpret_cast<GameList*>(ctx);
		gameList->OnTitleEvent(evt->eventType == CafeTitleListCallbackEvent::TYPE::TITLE_DISCOVERED,
							   evt->titleInfo->GetAppTitleId());
	}

	void OnTitleEvent(bool discovered, uint64_t titleId)
	{
		if (discovered)
		{
			GameInfo2 gameInfo = CafeTitleList::GetGameInfo(titleId);
			if (!gameInfo.IsValid())
				return;

			GameInfo info{};
			info.titleId = titleId;
			if (!GetConfig().GetGameListCustomName(titleId, info.name) || info.name.empty())
				info.name = gameInfo.GetTitleName();
			info.version = gameInfo.GetVersion();
			info.hasDLC = gameInfo.HasAOC();
			info.region = fmt::format("{}", static_cast<int>(gameInfo.GetRegion()));

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

	uint64 m_titleListCallbackId = 0;
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