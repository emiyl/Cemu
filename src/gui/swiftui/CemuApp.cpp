#include "gui/swiftui/CemuApp.h"
#include "config/ActiveSettings.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"

#include <mutex>

// forward declarations from main.cpp
void UnitTests();
void CemuCommonInit();

bool CemuApp::OnInit()
{
	printf("%s\n", BUILD_VERSION_WITH_NAME_STRING);
	CreateDefaultCemuFiles();
	GetConfigHandle().SetFilename(ActiveSettings::GetConfigPath("settings.xml").generic_wstring());

	LatteOverlay_init();
	// run a couple of tests if in non-release mode
#ifdef CEMU_DEBUG_ASSERT
	UnitTests();
#endif
	CemuCommonInit();
	return true;
}

int CemuApp::OnExit()
{
#if BOOST_OS_WINDOWS
	ExitProcess(0);
#else
	_Exit(0);
#endif
}

void CemuApp::CreateDefaultCemuFiles()
{
	// cemu directories
	try
	{
		const auto controllerProfileFolder = ActiveSettings::GetConfigPath("controllerProfiles");
		if (!fs::exists(controllerProfileFolder))
			fs::create_directories(controllerProfileFolder);

		const auto memorySearcherFolder = ActiveSettings::GetUserDataPath("memorySearcher");
		if (!fs::exists(memorySearcherFolder))
			fs::create_directories(memorySearcherFolder);
	} catch (const std::exception& ex)
	{
		printf("Couldn't create a required cemu directory or file!\nError: %s\n", ex.what());
		exit(0);
	}
}