#include "Common/precompiled.h"

#include "Cafe/CafeSystem.h"
#include "Cafe/TitleList/TitleInfo.h"
#include "Cafe/TitleList/TitleList.h"

extern "C" bool CemuSwiftUILaunchTitleById(uint64_t titleId)
{
    if (CafeSystem::IsTitleRunning())
        return false;

    TitleInfo titleInfo;
    if (!CafeTitleList::GetFirstByTitleId(titleId, titleInfo) || !titleInfo.IsValid())
        return false;

    CafeTitleList::AddTitleFromPath(titleInfo.GetPath());

    TitleId baseTitleId;
    if (!CafeTitleList::FindBaseTitleId(titleInfo.GetAppTitleId(), baseTitleId))
        return false;

    const auto status = CafeSystem::PrepareForegroundTitle(baseTitleId);
    if (status != CafeSystem::PREPARE_STATUS_CODE::SUCCESS)
        return false;

    CafeSystem::LaunchForegroundTitle();
    return true;
}
