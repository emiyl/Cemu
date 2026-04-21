#include "Common/precompiled.h"

#include "gui/swiftui/macos/MainWindow.h"

#include "Cafe/CafeSystem.h"
#include "Cafe/TitleList/TitleInfo.h"
#include "Cafe/TitleList/TitleList.h"
#include "interface/WindowSystem.h"

namespace swiftui
{
bool MainWindow::RequestLaunchGame(const fs::path& launchPath, LaunchInitiatedBy initiatedBy, std::string& errorOut)
{
    if (CafeSystem::IsTitleRunning())
    {
        errorOut = "A title is already running.";
        return false;
    }
    
    if (!PrepareLaunchPath(launchPath, initiatedBy, errorOut))
        return false;
    
    WindowSystem::UpdateWindowTitles(false, true, 0.0);
    CafeSystem::LaunchForegroundTitle();
    return true;
}

bool MainWindow::RequestLaunchGameByTitleId(uint64_t titleId, std::string& errorOut)
{
    TitleInfo titleInfo;
    if (!CafeTitleList::GetFirstByTitleId(titleId, titleInfo) || !titleInfo.IsValid())
    {
        errorOut = "Unable to launch title. Make sure game paths are valid and refresh the game list.";
        return false;
    }
    
    return RequestLaunchGame(titleInfo.GetPath(), LaunchInitiatedBy::kGameList, errorOut);
}

bool MainWindow::PrepareLaunchPath(const fs::path& launchPath, LaunchInitiatedBy initiatedBy, std::string& errorOut)
{
    TitleInfo launchTitle{launchPath};
    if (launchTitle.IsValid())
    {
        CafeTitleList::AddTitleFromPath(launchPath);
        
        TitleId baseTitleId;
        if (!CafeTitleList::FindBaseTitleId(launchTitle.GetAppTitleId(), baseTitleId))
        {
            errorOut = "Unable to launch game because the base files were not found.";
            return false;
        }
        
        CafeSystem::PREPARE_STATUS_CODE status = CafeSystem::PrepareForegroundTitle(baseTitleId);
        if (status == CafeSystem::PREPARE_STATUS_CODE::UNABLE_TO_MOUNT)
        {
            errorOut = "Unable to mount title. Make sure your game paths are valid and refresh the game list.";
            return false;
        }
        if (status != CafeSystem::PREPARE_STATUS_CODE::SUCCESS)
        {
            errorOut = "Failed to prepare the selected game for launch.";
            return false;
        }
        return true;
    }
    
    CafeTitleFileType fileType = DetermineCafeSystemFileType(launchPath);
    if (fileType == CafeTitleFileType::RPX || fileType == CafeTitleFileType::ELF)
    {
        CafeSystem::PREPARE_STATUS_CODE status = CafeSystem::PrepareForegroundTitleFromStandaloneRPX(launchPath);
        if (status != CafeSystem::PREPARE_STATUS_CODE::SUCCESS)
        {
            errorOut = "Failed to prepare standalone RPX/ELF executable.";
            return false;
        }
        return true;
    }
    
    if (initiatedBy == LaunchInitiatedBy::kGameList)
        errorOut = "Unable to launch title. Make sure your game paths are valid and refresh the game list.";
    else
        errorOut = "Unsupported or invalid Wii U title path.";
    return false;
}
} // namespace swiftui
