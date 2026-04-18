#include "gui/swiftui/components/SettingsView.h"

#include "Common/precompiled.h"
#include "Cafe/Account/Account.h"
#include "Cafe/CafeSystem.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cafe/TitleList/TitleList.h"
#include "audio/IAudioAPI.h"
#include "config/ActiveSettings.h"
#include "config/CemuConfig.h"
#include "config/NetworkSettings.h"
#include "gui/swiftui/CemuApp.h"
#include "gui/swiftui/SwiftUICemuConfig.h"

#include <boost/nowide/convert.hpp>

#include <cstdlib>
#include <cstring>

namespace
{
int32_t ToInt32(bool value)
{
    return value ? 1 : 0;
}

bool ToBool(int32_t value)
{
    return value != 0;
}

int32_t Clamp(int32_t value, int32_t minValue, int32_t maxValue)
{
    return std::clamp(value, minValue, maxValue);
}

float ClampFloat(float value, float minValue, float maxValue)
{
    return std::clamp(value, minValue, maxValue);
}

std::string SafeStringFromBuffer(const char* buffer)
{
    if (!buffer)
        return {};
    return std::string(buffer);
}

void RescanTitleListFromConfigPaths()
{
    CafeTitleList::ClearScanPaths();
    for (const auto& path : GetConfig().game_paths)
        CafeTitleList::AddScanPath(_utf8ToPath(path));
    CafeTitleList::Refresh();
}

GraphicAPI ResolveGraphicApi(int32_t graphicApi)
{
    const int32_t clamped = Clamp(graphicApi, static_cast<int32_t>(GraphicAPI::kOpenGL), static_cast<int32_t>(GraphicAPI::kMetal));
    if (clamped == static_cast<int32_t>(GraphicAPI::kOpenGL))
    {
#if ENABLE_METAL
        return GraphicAPI::kMetal;
#else
        return GraphicAPI::kVulkan;
#endif
    }
    if (clamped == static_cast<int32_t>(GraphicAPI::kVulkan) && !g_vulkan_available)
    {
#if ENABLE_METAL
        return GraphicAPI::kMetal;
#else
        return GraphicAPI::kVulkan;
#endif
    }
#if !ENABLE_METAL
    if (clamped == static_cast<int32_t>(GraphicAPI::kMetal))
        return GraphicAPI::kVulkan;
#endif
    return static_cast<GraphicAPI>(clamped);
}

IAudioAPI::AudioAPI ResolveAudioApi(int32_t audioApi)
{
    const auto clamped = static_cast<IAudioAPI::AudioAPI>(Clamp(audioApi, static_cast<int32_t>(IAudioAPI::AudioAPI::DirectSound), static_cast<int32_t>(IAudioAPI::AudioAPI::Cubeb)));
    if (!IAudioAPI::IsAudioAPIAvailable(clamped))
        return IAudioAPI::AudioAPI::Cubeb;
    return clamped;
}

CrashDump ResolveCrashDump(int32_t crashDump)
{
#if BOOST_OS_WINDOWS
    return static_cast<CrashDump>(Clamp(crashDump, static_cast<int32_t>(CrashDump::Disabled), static_cast<int32_t>(CrashDump::Full)));
#else
    return static_cast<CrashDump>(Clamp(crashDump, static_cast<int32_t>(CrashDump::Disabled), static_cast<int32_t>(CrashDump::Enabled)));
#endif
}
}

extern "C" bool CemuSettingsLoad(CemuSettingsState* outState)
{
    if (!outState)
        return false;
    
    std::memset(outState, 0, sizeof(CemuSettingsState));
    GetConfigHandle().Load();
    auto& config = GetConfig();
    
    outState->language = g_cemuConfig.language;
    outState->useDiscordPresence = ToInt32(g_cemuConfig.use_discord_presence);
    outState->saveScreenshots = ToInt32(g_cemuConfig.save_screenshots);
    outState->checkForUpdates = ToInt32(g_cemuConfig.check_update);
    outState->receiveUntestedUpdates = ToInt32(g_cemuConfig.receive_untested_updates);
    outState->playBootSound = ToInt32(config.play_boot_sound);
    outState->isTitleRunning = ToInt32(CafeSystem::IsTitleRunning());
    outState->supportsCustomNetworkService = ToInt32(NetworkConfig::XMLExists());
    
    outState->graphicApi = static_cast<int32_t>(config.graphic_api.GetValue());
    if (outState->graphicApi == static_cast<int32_t>(GraphicAPI::kOpenGL))
    {
#if ENABLE_METAL
        outState->graphicApi = static_cast<int32_t>(GraphicAPI::kMetal);
#else
        outState->graphicApi = static_cast<int32_t>(GraphicAPI::kVulkan);
#endif
    }
    outState->vsync = config.vsync;
    outState->asyncCompile = ToInt32(config.async_compile);
    outState->gx2DrawDoneSync = 1;
#if ENABLE_METAL
    outState->forceMeshShaders = ToInt32(config.force_mesh_shaders);
    outState->supportsMetal = 1;
#else
    outState->forceMeshShaders = 0;
    outState->supportsMetal = 0;
#endif
    outState->supportsVulkan = ToInt32(g_vulkan_available);
    outState->overrideGamma = ToInt32(config.overrideAppGammaPreference);
    outState->overrideGammaValue = config.overrideGammaValue;
    outState->displayGammaValue = config.userDisplayGamma;
    outState->displayGammaIsSRGB = ToInt32(config.userDisplayGamma.GetValue() == 0.0f);
    outState->upscaleFilter = config.upscale_filter;
    outState->downscaleFilter = config.downscale_filter;
    outState->fullscreenScaling = Clamp(config.fullscreen_scaling, static_cast<int32_t>(FullscreenScaling::kKeepAspectRatio), static_cast<int32_t>(FullscreenScaling::kStretch));
    
    outState->audioApi = config.audio_api;
    outState->audioDelay = config.audio_delay;
    outState->tvChannels = static_cast<int32_t>(config.tv_channels);
    outState->padChannels = static_cast<int32_t>(config.pad_channels);
    outState->inputChannels = static_cast<int32_t>(AudioChannels::kMono);
    outState->tvVolume = config.tv_volume;
    outState->padVolume = config.pad_volume;
    outState->inputVolume = config.input_volume;
    outState->portalVolume = config.portal_volume;
    
    outState->overlayPosition = Clamp(static_cast<int32_t>(config.overlay.position), static_cast<int32_t>(ScreenPosition::kDisabled), static_cast<int32_t>(ScreenPosition::kBottomRight));
    outState->overlayTextScale = Clamp(config.overlay.text_scale, 50, 300);
    outState->overlayTextColor = config.overlay.text_color;
    outState->overlayFps = ToInt32(config.overlay.fps);
    outState->overlayDrawcalls = ToInt32(config.overlay.drawcalls);
    outState->overlayCpuUsage = ToInt32(config.overlay.cpu_usage);
    outState->overlayCpuPerCoreUsage = ToInt32(config.overlay.cpu_per_core_usage);
    outState->overlayRamUsage = ToInt32(config.overlay.ram_usage);
    outState->overlayVramUsage = ToInt32(config.overlay.vram_usage);
    outState->overlayDebug = ToInt32(config.overlay.debug);
    
    outState->notificationPosition = Clamp(static_cast<int32_t>(config.notification.position), static_cast<int32_t>(ScreenPosition::kDisabled), static_cast<int32_t>(ScreenPosition::kBottomRight));
    outState->notificationTextScale = Clamp(config.notification.text_scale, 50, 300);
    outState->notificationTextColor = config.notification.text_color;
    outState->notificationControllerProfiles = ToInt32(config.notification.controller_profiles);
    outState->notificationControllerBattery = ToInt32(config.notification.controller_battery);
    outState->notificationShaderCompiling = ToInt32(config.notification.shader_compiling);
    outState->notificationFriends = ToInt32(config.notification.friends);
    
    outState->activeAccountPersistentId = config.account.m_persistent_id;
    outState->activeAccountNetworkService = static_cast<int32_t>(config.GetAccountNetworkService(outState->activeAccountPersistentId));
    
    outState->crashDump = static_cast<int32_t>(config.crash_dump.GetValue());
    outState->gdbPort = config.gdb_port;
#if ENABLE_METAL
    outState->framebufferFetch = ToInt32(config.framebuffer_fetch);
#else
    outState->framebufferFetch = 0;
#endif
    
    return true;
}

extern "C" bool CemuSettingsSave(const CemuSettingsState* inState)
{
    if (!inState)
        return false;
    
    auto& config = GetConfig();
    
    g_cemuConfig.language = inState->language;
    g_cemuConfig.use_discord_presence = ToBool(inState->useDiscordPresence);
    g_cemuConfig.save_screenshots = ToBool(inState->saveScreenshots);
    g_cemuConfig.check_update = ToBool(inState->checkForUpdates);
    g_cemuConfig.receive_untested_updates = ToBool(inState->receiveUntestedUpdates);
    
    config.play_boot_sound = ToBool(inState->playBootSound);
    
    config.graphic_api = ResolveGraphicApi(inState->graphicApi);
    config.vsync = Clamp(inState->vsync, 0, 3);
    config.async_compile = ToBool(inState->asyncCompile);
    config.gx2drawdone_sync = true;
#if ENABLE_METAL
    config.force_mesh_shaders = ToBool(inState->forceMeshShaders);
#endif
    config.overrideAppGammaPreference = ToBool(inState->overrideGamma);
    config.overrideGammaValue = ClampFloat(inState->overrideGammaValue, 0.1f, 4.0f);
    if (ToBool(inState->displayGammaIsSRGB))
        config.userDisplayGamma = 0.0f;
    else
        config.userDisplayGamma = ClampFloat(inState->displayGammaValue, 0.1f, 4.0f);
    config.upscale_filter = Clamp(inState->upscaleFilter, static_cast<int32_t>(UpscalingFilter::kLinearFilter), static_cast<int32_t>(UpscalingFilter::kNearestNeighborFilter));
    config.downscale_filter = Clamp(inState->downscaleFilter, static_cast<int32_t>(UpscalingFilter::kLinearFilter), static_cast<int32_t>(UpscalingFilter::kNearestNeighborFilter));
    config.fullscreen_scaling = Clamp(inState->fullscreenScaling, static_cast<int32_t>(FullscreenScaling::kKeepAspectRatio), static_cast<int32_t>(FullscreenScaling::kStretch));
    
    config.audio_api = static_cast<sint32>(ResolveAudioApi(inState->audioApi));
    config.audio_delay = Clamp(inState->audioDelay, 0, static_cast<int32_t>(IAudioAPI::kBlockCount - 1));
    config.tv_channels = static_cast<AudioChannels>(Clamp(inState->tvChannels, static_cast<int32_t>(AudioChannels::kMono), static_cast<int32_t>(AudioChannels::kSurround)));
    config.pad_channels = static_cast<AudioChannels>(Clamp(inState->padChannels, static_cast<int32_t>(AudioChannels::kMono), static_cast<int32_t>(AudioChannels::kSurround)));
    config.input_channels = AudioChannels::kMono;
    config.tv_volume = Clamp(inState->tvVolume, 0, 100);
    config.pad_volume = Clamp(inState->padVolume, 0, 100);
    config.input_volume = Clamp(inState->inputVolume, 0, 100);
    config.portal_volume = Clamp(inState->portalVolume, 0, 100);
    
    config.overlay.position = static_cast<ScreenPosition>(Clamp(inState->overlayPosition, static_cast<int32_t>(ScreenPosition::kDisabled), static_cast<int32_t>(ScreenPosition::kBottomRight)));
    config.overlay.text_scale = Clamp(inState->overlayTextScale, 50, 300);
    config.overlay.text_color = inState->overlayTextColor;
    config.overlay.fps = ToBool(inState->overlayFps);
    config.overlay.drawcalls = ToBool(inState->overlayDrawcalls);
    config.overlay.cpu_usage = ToBool(inState->overlayCpuUsage);
    config.overlay.cpu_per_core_usage = ToBool(inState->overlayCpuPerCoreUsage);
    config.overlay.ram_usage = ToBool(inState->overlayRamUsage);
    config.overlay.vram_usage = ToBool(inState->overlayVramUsage);
    config.overlay.debug = ToBool(inState->overlayDebug);
    
    config.notification.position = static_cast<ScreenPosition>(Clamp(inState->notificationPosition, static_cast<int32_t>(ScreenPosition::kDisabled), static_cast<int32_t>(ScreenPosition::kBottomRight)));
    config.notification.text_scale = Clamp(inState->notificationTextScale, 50, 300);
    config.notification.text_color = inState->notificationTextColor;
    config.notification.controller_profiles = ToBool(inState->notificationControllerProfiles);
    config.notification.controller_battery = ToBool(inState->notificationControllerBattery);
    config.notification.shader_compiling = ToBool(inState->notificationShaderCompiling);
    config.notification.friends = ToBool(inState->notificationFriends);
    
    config.account.m_persistent_id = inState->activeAccountPersistentId;
    config.SetAccountSelectedService(inState->activeAccountPersistentId, static_cast<NetworkService>(Clamp(inState->activeAccountNetworkService, 0, static_cast<int32_t>(NetworkService::Custom))));
    
    config.crash_dump = ResolveCrashDump(inState->crashDump);
    config.gdb_port = Clamp(inState->gdbPort, 1000, 65535);
#if ENABLE_METAL
    config.framebuffer_fetch = ToBool(inState->framebufferFetch);
#endif
    
    GetConfigHandle().Save();
    return true;
}

extern "C" void CemuSettingsFreeBuffer(void* ptr)
{
    std::free(ptr);
}

extern "C" const char* CemuSettingsGetMlcPath(void)
{
    return strdup(GetConfig().mlc_path.GetValue().c_str());
}

extern "C" bool CemuSettingsSetMlcPath(const char* path)
{
    const std::string mlcPath = SafeStringFromBuffer(path);
    if (!mlcPath.empty() && !CemuApp::CheckMLCPath(_utf8ToPath(mlcPath)) && !CemuApp::CreateDefaultMLCFiles(_utf8ToPath(mlcPath)))
        return false;
    GetConfig().mlc_path = mlcPath;
    GetConfigHandle().Save();
    return true;
}

extern "C" const char* CemuSettingsGetGpuCaptureDir(void)
{
#if ENABLE_METAL
    return strdup(GetConfig().gpu_capture_dir.GetValue().c_str());
#else
    return strdup("");
#endif
}

extern "C" bool CemuSettingsSetGpuCaptureDir(const char* path)
{
#if ENABLE_METAL
    GetConfig().gpu_capture_dir = SafeStringFromBuffer(path);
    GetConfigHandle().Save();
#else
    (void)path;
#endif
    return true;
}

extern "C" const char* CemuSettingsGetDefaultMlcPath(void)
{
    return strdup(_pathToUtf8(ActiveSettings::GetDefaultMLCPath()).c_str());
}

extern "C" const char* CemuSettingsGetDefaultGpuCaptureDir(void)
{
#if ENABLE_METAL
    return strdup("");
#else
    return strdup("");
#endif
}

extern "C" size_t CemuSettingsGetGamePathCount(void)
{
    return GetConfig().game_paths.size();
}

extern "C" const char* CemuSettingsGetGamePath(size_t index)
{
    const auto& paths = GetConfig().game_paths;
    if (index >= paths.size())
        return nullptr;
    return strdup(paths[index].c_str());
}

extern "C" bool CemuSettingsAddGamePath(const char* path)
{
    if (!path || !path[0])
        return false;
    auto& paths = GetConfig().game_paths;
    std::string candidate(path);
    if (std::find(paths.begin(), paths.end(), candidate) != paths.end())
        return false;
    paths.emplace_back(std::move(candidate));
    RescanTitleListFromConfigPaths();
    GetConfigHandle().Save();
    return true;
}

extern "C" bool CemuSettingsRemoveGamePath(size_t index)
{
    auto& paths = GetConfig().game_paths;
    if (index >= paths.size())
        return false;
    paths.erase(paths.begin() + static_cast<std::ptrdiff_t>(index));
    RescanTitleListFromConfigPaths();
    GetConfigHandle().Save();
    return true;
}

extern "C" size_t CemuSettingsGetAccountCount(void)
{
    const auto& accounts = Account::GetAccounts();
    return accounts.size();
}

extern "C" uint32_t CemuSettingsGetAccountPersistentId(size_t index)
{
    const auto& accounts = Account::GetAccounts();
    if (index >= accounts.size())
        return 0;
    return accounts[index].GetPersistentId();
}

extern "C" const char* CemuSettingsGetAccountDisplayName(size_t index)
{
    const auto& accounts = Account::GetAccounts();
    if (index >= accounts.size())
        return nullptr;
    return strdup(boost::nowide::narrow(accounts[index].ToString()).c_str());
}

extern "C" bool CemuSettingsCreateAccount(const char* miiName, uint32_t* outPersistentId)
{
    if (!miiName || !miiName[0] || !Account::HasFreeAccountSlots())
        return false;
    const uint32 persistentId = Account::GetNextPersistentId();
    Account account(persistentId, boost::nowide::widen(miiName));
    const auto ec = account.Save();
    if (ec)
        return false;
    Account::RefreshAccounts();
    if (outPersistentId)
        *outPersistentId = persistentId;
    return true;
}

extern "C" bool CemuSettingsDeleteAccount(uint32_t persistentId)
{
    const auto& accounts = Account::GetAccounts();
    if (accounts.size() <= 1)
        return false;
    const auto path = Account::GetFileName(persistentId);
    std::error_code ec;
    const bool removed = fs::remove(path, ec);
    if (!removed || ec)
        return false;
    Account::RefreshAccounts();
    if (GetConfig().account.m_persistent_id.GetValue() == persistentId)
    {
        GetConfig().account.m_persistent_id = Account::GetAccounts()[0].GetPersistentId();
        GetConfigHandle().Save();
    }
    return true;
}
