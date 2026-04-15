#include "gui/swiftui/components/GameList.h"

#include "Cafe/IOSU/PDM/iosu_pdm.h"
#include "Cafe/TitleList/GameInfo.h"
#include "Cafe/TitleList/TitleList.h"
#include "config/CemuConfig.h"
#include "interface/WindowSystem.h"

#include <boost/algorithm/string.hpp>

#include <cstdint>
#include <mutex>

struct GameInfo
{
	uint64_t titleId;
	std::string name;
	uint32_t version;
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
		if (!ctx || !evt)
			return;
		auto* gameList = reinterpret_cast<GameList*>(ctx);
		gameList->HandleTitleListCallback(evt);
	}

	void HandleTitleListCallback(CafeTitleListCallbackEvent* evt)
	{
		printf("Received title list callback event for title id %016llx, type %d\n", evt->titleInfo ? evt->titleInfo->GetAppTitleId() : 0, evt->eventType);
		if (!evt->titleInfo)
			return;

		if (evt->eventType == CafeTitleListCallbackEvent::TYPE::TITLE_DISCOVERED)
			OnTitleDiscovered(*evt->titleInfo);
		else if (evt->eventType == CafeTitleListCallbackEvent::TYPE::TITLE_REMOVED)
			OnTitleRemoved(evt->titleInfo->GetAppTitleId());
	}

	void OnTitleDiscovered(const TitleInfo& titleInfo)
	{
		const uint64_t titleId = titleInfo.GetAppTitleId();

		GameInfo info{};
		info.titleId = titleId;
		if (!GetConfig().GetGameListCustomName(titleId, info.name) || info.name.empty())
			info.name = titleInfo.GetMetaTitleName();
		info.version = titleInfo.GetAppTitleVersion();
		info.region = fmt::format("{}", static_cast<int>(titleInfo.GetMetaRegion()));

		{
			std::lock_guard lock(m_entriesMutex);
			m_entries[titleId] = info;
		}

		if (m_onEntryChanged)
			m_onEntryChanged(titleId);
	}

	void OnTitleRemoved(uint64_t titleId)
	{
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

static GameList* g_gameList = nullptr;
static GameListCallback g_callback = nullptr;

// Functions exposed to SwiftUI

extern "C" void CemuGameListCreate(void)
{
	if (g_gameList)
		return;

	g_gameList = new GameList();

	g_gameList->SetOnEntryChanged([](uint64_t titleId) {
		if (g_callback)
			g_callback(titleId);
	});
}

extern "C" void CemuGameListDestroy(void)
{
	delete g_gameList;
	g_gameList = nullptr;
}

extern "C" void CemuGameListRefresh(void)
{
	if (g_gameList)
		g_gameList->Refresh();
	WindowSystem::RefreshGameList();
}

extern "C" size_t CemuGameListGetCount(void)
{
	if (!g_gameList)
		return 0;
	return g_gameList->GetEntries().size();
}

extern "C" bool CemuGameListGetRow(size_t index, CemuGameListRow* outRow)
{
	if (!g_gameList)
		return false;

	const auto entries = g_gameList->GetEntries();
	if (index >= entries.size())
		return false;

	const auto& entry = entries[index];
	outRow->titleId = entry.titleId;
	outRow->name = strdup(entry.name.c_str());
	outRow->version = entry.version;
	outRow->region = strdup(entry.region.c_str());
	return true;
}

extern "C" bool CemuGameListIsScanning(void)
{
	return CafeTitleList::IsScanning();
}