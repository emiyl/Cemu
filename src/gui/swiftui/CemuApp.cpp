#include "gui/swiftui/CemuApp.h"
#include "config/ActiveSettings.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"
#include "SwiftUICemuConfig.h"
#include "config/CemuConfig.h"
#include "config/NetworkSettings.h"
#include "Cemu/ncrypto/ncrypto.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

#if BOOST_OS_MACOS
#include <mach-o/dyld.h>
#endif

// forward declarations from main.cpp
void UnitTests();
void CemuCommonInit();

void HandlePostUpdate();

// Config
SwiftUICemuConfig g_cemuConfig;

#if BOOST_OS_MACOS
void CemuApp::DeterminePaths(std::set<fs::path>& failedWriteAccess) // for MacOS
{
	std::error_code ec;
	bool isPortable = false;
	fs::path user_data_path, config_path, cache_path, data_path;

	uint32_t exePathBufferSize = 0;
	_NSGetExecutablePath(nullptr, &exePathBufferSize);
	std::vector<char> exePathBuffer(exePathBufferSize, '\0');
	if (_NSGetExecutablePath(exePathBuffer.data(), &exePathBufferSize) != 0)
	{
		fprintf(stderr, "Failed to resolve executable path\n");
		exit(0);
	}
	fs::path exePath = fs::weakly_canonical(fs::path(exePathBuffer.data()), ec);
	if (ec)
		exePath = fs::path(exePathBuffer.data());

	// If run from an app bundle, use its parent directory
	fs::path appPath = exePath.parent_path().parent_path().parent_path();
	const bool isAppBundle = appPath.extension() == ".app";
	fs::path portablePath = isAppBundle ? appPath.parent_path() / "portable" : exePath.parent_path() / "portable";
#ifdef CEMU_ALLOW_PORTABLE
	if (fs::is_directory(portablePath, ec))
	{
		isPortable = true;
		user_data_path = config_path = cache_path = portablePath;
		data_path = exePath.parent_path();
	}
	else
#endif
	{
		const char* homeEnv = std::getenv("HOME");
		if (!homeEnv || !homeEnv[0])
		{
			fprintf(stderr, "HOME is not set\n");
			exit(0);
		}
		const fs::path homePath(homeEnv);
		user_data_path = config_path = homePath / "Library/Application Support/Cemu";
		cache_path = homePath / "Library/Caches/Cemu";
		data_path = isAppBundle ? (appPath / "Contents/Resources") : exePath.parent_path();
	}
	ActiveSettings::SetPaths(isPortable, exePath, user_data_path, config_path, cache_path, data_path, failedWriteAccess);
}
#endif

// create default MLC files or quit if it fails
void CemuApp::InitializeNewMLCOrFail(fs::path mlc)
{
	if (CemuApp::CreateDefaultMLCFiles(mlc))
		return;											   // all good
	cemu_assert_debug(!ActiveSettings::IsCustomMlcPath()); // should not be possible?

	if (ActiveSettings::IsCommandLineMlcPath() || ActiveSettings::IsCustomMlcPath())
	{
		fprintf(stderr, "Cemu failed to write to the custom mlc directory. Path: %s\n", _pathToUtf8(mlc).c_str());
		exit(0);
	}
	fprintf(stderr, "Cemu failed to write to the mlc directory. Path: %s\n", _pathToUtf8(mlc).c_str());
	exit(0);
}

void CemuApp::InitializeExistingMLCOrFail(fs::path mlc)
{
	if (CreateDefaultMLCFiles(mlc))
		return; // all good
	// failed to write mlc files
	if (ActiveSettings::IsCommandLineMlcPath() || ActiveSettings::IsCustomMlcPath())
	{
		// tell user that the custom path is not writable
		// if it's a command line path then just quit. Otherwise ask if user wants to reset the path
		if (ActiveSettings::IsCommandLineMlcPath())
		{
			fprintf(stderr, "Cemu failed to write to the custom mlc directory. Path: %s\n", _pathToUtf8(mlc).c_str());
			exit(0);
		}
		fprintf(stderr, "Cemu failed to write to custom mlc path, resetting configured mlc path: %s\n", _pathToUtf8(mlc).c_str());
		GetConfig().mlc_path = "";
		GetConfigHandle().Save();
	}
	else
	{
		fprintf(stderr, "Cemu failed to write to the default mlc directory. Path: %s\n", _pathToUtf8(mlc).c_str());
		exit(0);
	}
}

bool CemuApp::OnInit()
{
	printf("%s\n", BUILD_VERSION_WITH_NAME_STRING);
	std::set<fs::path> failedWriteAccess;
	DeterminePaths(failedWriteAccess);

	CreateDefaultCemuFiles();
	GetConfigHandle().SetFilename(ActiveSettings::GetConfigPath("settings.xml").generic_wstring());

	std::error_code ec;
	bool isFirstStart = !fs::exists(ActiveSettings::GetConfigPath("settings.xml"), ec);

	NetworkConfig::LoadOnce();
	if (!isFirstStart)
	{
		GetConfigHandle().Load();
		sint32 language = g_cemuConfig.language;
		// LocalizeUI(language == LANGUAGE_DEFAULT ? GetSystemLanguage() : language)
	}
	else
	{
		// LocalizeUI(getSystemLanguage)
	}

	// SetTranslationCallback(TranslationCallback);

	for (auto&& path : failedWriteAccess)
	{
		printf("Cemu can't write to %s!\n", path.string().c_str());
	}

	if (isFirstStart)
	{
		GetConfigHandle().Save();
		InitializeNewMLCOrFail(ActiveSettings::GetMlcPath());
	}
	else
	{
		InitializeExistingMLCOrFail(ActiveSettings::GetMlcPath());
	}

	ActiveSettings::Init(); // this is a bit of a misnomer, right now this call only loads certs for online play. In the future we should move the logic to a more appropriate place
	HandlePostUpdate();

	LatteOverlay_init();
	// run a couple of tests if in non-release mode
#ifdef CEMU_DEBUG_ASSERT
	UnitTests();
#endif
	CemuCommonInit();

	InitializeGlobalVulkan();
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

bool CemuApp::CheckMLCPath(const fs::path& mlc)
{
	std::error_code ec;
	if (!fs::exists(mlc, ec))
		return false;
	if (!fs::exists(mlc / "usr", ec) || !fs::exists(mlc / "sys", ec))
		return false;
	return true;
}

bool CemuApp::CreateDefaultMLCFiles(const fs::path& mlc)
{
	auto CreateDirectoriesIfNotExist = [](const fs::path& path) {
		std::error_code ec;
		if (!fs::exists(path, ec))
			return fs::create_directories(path, ec);
		return true;
	};

	const fs::path directories[] = {
		mlc,
		mlc / "sys",
		mlc / "usr",
		mlc / "usr/title/00050000",
		mlc / "usr/title/0005000c",
		mlc / "usr/title/0005000e",
		mlc / "usr/save/00050010/1004a000/user/common/db",
		mlc / "usr/save/00050010/1004a100/user/common/db",
		mlc / "usr/save/00050010/1004a200/user/common/db",
		mlc / "sys/title/0005001b/1005c000/content"};

	for (const auto& path : directories)
	{
		if (!CreateDirectoriesIfNotExist(path))
			return false;
	}

	try
	{
		const auto langDir = fs::path(mlc).append("sys/title/0005001b/1005c000/content");
		auto langFile = fs::path(langDir).append("language.txt");
		if (!fs::exists(langFile))
		{
			std::ofstream file(langFile);
			if (file.is_open())
			{
				const char* langStrings[] = {"ja", "en", "fr", "de", "it", "es", "zh", "ko", "nl", "pt", "ru", "zh"};
				for (const char* lang : langStrings)
					file << fmt::format("\"{}\",", lang) << std::endl;

				file.flush();
				file.close();
			}
		}

		auto countryFile = fs::path(langDir).append("country.txt");
		if (!fs::exists(countryFile))
		{
			std::ofstream file(countryFile);
			for (sint32 i = 0; i < NCrypto::GetCountryCount(); i++)
			{
				const char* countryCode = NCrypto::GetCountryAsString(i);
				if (std::string_view(countryCode) == "NN")
					file << "NULL," << std::endl;
				else
					file << fmt::format("\"{}\",", countryCode) << std::endl;
			}
			file.flush();
			file.close();
		}

		const auto dummyFile = fs::path(mlc).append("writetestdummy");
		std::ofstream file(dummyFile);
		if (!file.is_open())
			return false;
		file.close();
		fs::remove(dummyFile);
	} catch (const std::exception&)
	{
		return false;
	}

	return true;
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