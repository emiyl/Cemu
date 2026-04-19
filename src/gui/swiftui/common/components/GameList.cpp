#include "gui/swiftui/common/components/GameList.h"

#include "Cafe/Filesystem/fsc.h"
#include "Cafe/TitleList/GameInfo.h"
#include "Cafe/TitleList/TitleList.h"
#include "Common/precompiled.h"
#include "gui/swiftui/macos/RegionStrings.h"
#include "config/CemuConfig.h"
#include "interface/WindowSystem.h"
#include "util/helpers/helpers.h"

#include <boost/algorithm/string.hpp>

#include <atomic>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <mutex>

struct GameInfo
{
    uint64_t titleId;
    std::vector<uint8_t> iconData;
    std::string name;
    uint16_t version;
    uint16_t dlc;
    std::string region;
};

class GameList
{
public:
    using EntryChangedCallback = std::function<void(uint64_t titleId)>;
    
    GameList()
    {
        m_titleListCallbackId = CafeTitleList::RegisterCallback(&GameList::OnTitleListEvent, this);
        m_needsRebuild = true;
    }
    
    ~GameList()
    {
        if (m_titleListCallbackId != 0)
            CafeTitleList::UnregisterCallback(m_titleListCallbackId);
    }
    
    void Refresh()
    {
        CafeTitleList::Refresh();
        
        if (m_titleListCallbackId != 0)
            CafeTitleList::UnregisterCallback(m_titleListCallbackId);
        m_titleListCallbackId = CafeTitleList::RegisterCallback(&GameList::OnTitleListEvent, this);
    }
    
    std::vector<GameInfo> GetEntries()
    {
        RebuildEntriesIfNeeded();
        
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
        if (!evt->titleInfo)
            return;
        
        m_needsRebuild = true;
        
        uint64_t baseTitleId = evt->titleInfo->GetAppTitleId();
        CafeTitleList::FindBaseTitleId(evt->titleInfo->GetAppTitleId(), baseTitleId);
        if (m_onEntryChanged)
            m_onEntryChanged(baseTitleId);
    }
    
    void RebuildEntriesIfNeeded()
    {
        if (!m_needsRebuild.exchange(false))
            return;
        
        auto titleIds = CafeTitleList::GetAllTitleIds();
        std::unordered_map<uint64_t, GameInfo> rebuiltEntries;
        std::unordered_map<uint64_t, std::vector<uint8_t>> rebuiltIconCache;
        {
            std::lock_guard lock(m_entriesMutex);
            rebuiltIconCache = m_iconDataCache;
        }
        
        for (const auto titleId : titleIds)
        {
            addTitle(titleId, rebuiltEntries, rebuiltIconCache);
        }
        
        std::lock_guard lock(m_entriesMutex);
        m_entries = std::move(rebuiltEntries);
        m_iconDataCache = std::move(rebuiltIconCache);
    }
    
    void addTitle(uint64_t titleId, std::unordered_map<uint64_t, GameInfo>& entries, std::unordered_map<uint64_t, std::vector<uint8_t>>& iconCache)
    {
        GameInfo2 gameInfo = CafeTitleList::GetGameInfo(titleId);
        if (!gameInfo.IsValid() || gameInfo.IsSystemDataTitle())
            return;
        
        const uint64_t baseTitleId = gameInfo.GetBaseTitleId();
        GameInfo info{};
        info.titleId = baseTitleId;
        
        if (auto cachedIcon = iconCache.find(baseTitleId); cachedIcon != iconCache.end())
            info.iconData = cachedIcon->second;
        else
        {
            info.iconData = LoadIconDataForTitle(baseTitleId);
            iconCache.emplace(baseTitleId, info.iconData);
        }
        
        if (!GetConfig().GetGameListCustomName(baseTitleId, info.name) || info.name.empty())
            info.name = gameInfo.GetTitleName();
        
        info.version = gameInfo.GetVersion();
        info.dlc = gameInfo.GetAOCVersion();
        info.region = swiftui::CafeConsoleRegionToDisplayKey(static_cast<CafeConsoleRegion>(gameInfo.GetRegion()));
        
        entries[baseTitleId] = std::move(info);
    }
    
    std::vector<uint8_t> LoadIconDataForTitle(uint64_t titleId) const
    {
        TitleInfo titleInfo;
        if (!CafeTitleList::GetFirstByTitleId(titleId, titleInfo))
            return {};
        
        const std::string tempMountPath = TitleInfo::GetUniqueTempMountingPath();
        if (!titleInfo.Mount(tempMountPath, "", FSC_PRIORITY_BASE))
            return {};
        
        auto tgaData = fsc_extractFile((tempMountPath + "/meta/iconTex.tga").c_str());
        if (!tgaData)
        {
            tgaData = fsc_extractFile((tempMountPath + "/meta/iconTex.tga.gz").c_str());
            if (tgaData)
            {
                auto decompressed = zlibDecompress(*tgaData, 70 * 1024);
                if (decompressed)
                    tgaData = std::move(decompressed);
                else
                    tgaData.reset();
            }
        }
        
        titleInfo.Unmount(tempMountPath);
        if (!tgaData)
            return {};
        
        return *tgaData;
    }
    
    mutable std::mutex m_entriesMutex;
    std::unordered_map<uint64_t, GameInfo> m_entries;
    std::unordered_map<uint64_t, std::vector<uint8_t>> m_iconDataCache;
    std::atomic<bool> m_needsRebuild{true};
    
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
}

extern "C" size_t CemuGameListGetCount(void)
{
    if (!g_gameList)
        return 0;
    const auto& entries = g_gameList->GetEntries();
    return entries.size();
}

extern "C" bool CemuGameListGetRow(size_t index, CemuGameListRow* outRow)
{
    if (!g_gameList || !outRow)
        return false;
    
    const auto& entries = g_gameList->GetEntries();
    if (index >= entries.size())
        return false;
    
    const auto& entry = entries[index];
    outRow->titleId = entry.titleId;
    outRow->iconData = nullptr;
    outRow->iconSize = 0;
    if (!entry.iconData.empty())
    {
        auto* iconCopy = static_cast<uint8_t*>(malloc(entry.iconData.size()));
        if (iconCopy)
        {
            memcpy(iconCopy, entry.iconData.data(), entry.iconData.size());
            outRow->iconData = iconCopy;
            outRow->iconSize = entry.iconData.size();
        }
    }
    outRow->name = strdup(entry.name.c_str());
    outRow->region = strdup(entry.region.c_str());
    outRow->version = entry.version;
    outRow->dlc = entry.dlc;
    return true;
}

extern "C" void CemuGameListFreeBuffer(void* ptr)
{
    free(ptr);
}

extern "C" bool CemuGameListIsScanning(void)
{
    return CafeTitleList::IsScanning();
}
