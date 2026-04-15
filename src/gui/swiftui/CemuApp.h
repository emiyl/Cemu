#pragma once

class CemuApp
{
  public:
	bool OnInit();
	int OnExit();
	static void CreateDefaultCemuFiles();
};