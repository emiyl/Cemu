#pragma once
#include <filesystem>
#include <set>
namespace fs = std::filesystem;

class CemuApp
{
  public:
	bool OnInit();
	int OnExit();

	static bool CheckMLCPath(const fs::path& mlc);
	static bool CreateDefaultMLCFiles(const fs::path& mlc);
	static void CreateDefaultCemuFiles();

  private:
	void DeterminePaths(std::set<fs::path>& failedWriteAccess);
	void InitializeNewMLCOrFail(fs::path mlc);
	void InitializeExistingMLCOrFail(fs::path mlc);
};