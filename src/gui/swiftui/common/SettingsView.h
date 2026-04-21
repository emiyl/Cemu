#pragma once

#include <cstddef>
#include <cstdint>

typedef struct
{
    int32_t language;
    int32_t useDiscordPresence;
    int32_t saveScreenshots;
    int32_t checkForUpdates;
    int32_t receiveUntestedUpdates;
    int32_t playBootSound;
    int32_t isTitleRunning;
    int32_t supportsCustomNetworkService;
    
    int32_t graphicApi;
    int32_t vsync;
    int32_t asyncCompile;
    int32_t gx2DrawDoneSync;
    int32_t forceMeshShaders;
    int32_t supportsVulkan;
    int32_t supportsMetal;
    int32_t overrideGamma;
    float overrideGammaValue;
    float displayGammaValue;
    int32_t displayGammaIsSRGB;
    int32_t upscaleFilter;
    int32_t downscaleFilter;
    int32_t fullscreenScaling;
    
    int32_t audioApi;
    int32_t audioDelay;
    int32_t tvChannels;
    int32_t padChannels;
    int32_t inputChannels;
    int32_t tvVolume;
    int32_t padVolume;
    int32_t inputVolume;
    int32_t portalVolume;
    
    int32_t overlayPosition;
    int32_t overlayTextScale;
    uint32_t overlayTextColor;
    int32_t overlayFps;
    int32_t overlayDrawcalls;
    int32_t overlayCpuUsage;
    int32_t overlayCpuPerCoreUsage;
    int32_t overlayRamUsage;
    int32_t overlayVramUsage;
    int32_t overlayDebug;
    
    int32_t notificationPosition;
    int32_t notificationTextScale;
    uint32_t notificationTextColor;
    int32_t notificationControllerProfiles;
    int32_t notificationControllerBattery;
    int32_t notificationShaderCompiling;
    int32_t notificationFriends;
    
    uint32_t activeAccountPersistentId;
    int32_t activeAccountNetworkService;
    
    int32_t crashDump;
    int32_t gdbPort;
    int32_t framebufferFetch;
} CemuSettingsState;

extern "C" bool CemuSettingsLoad(CemuSettingsState* outState);
extern "C" bool CemuSettingsSave(const CemuSettingsState* inState);
extern "C" void CemuSettingsFreeBuffer(void* ptr);
extern "C" const char* CemuSettingsGetMlcPath(void);
extern "C" const char* CemuSettingsGetDefaultMlcPath(void);
extern "C" bool CemuSettingsSetMlcPath(const char* path);
extern "C" const char* CemuSettingsGetGpuCaptureDir(void);
extern "C" const char* CemuSettingsGetDefaultGpuCaptureDir(void);
extern "C" bool CemuSettingsSetGpuCaptureDir(const char* path);

extern "C" size_t CemuSettingsGetGamePathCount(void);
extern "C" const char* CemuSettingsGetGamePath(size_t index);
extern "C" bool CemuSettingsAddGamePath(const char* path);
extern "C" bool CemuSettingsRemoveGamePath(size_t index);

extern "C" size_t CemuSettingsGetAccountCount(void);
extern "C" uint32_t CemuSettingsGetAccountPersistentId(size_t index);
extern "C" const char* CemuSettingsGetAccountDisplayName(size_t index);
extern "C" bool CemuSettingsCreateAccount(const char* miiName, uint32_t* outPersistentId);
extern "C" bool CemuSettingsDeleteAccount(uint32_t persistentId);
