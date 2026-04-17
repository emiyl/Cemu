#pragma once

#include "Common/precompiled.h"

#include <cstddef>
#include <cstdint>

typedef struct
{
	uint64_t titleId;
	const uint8_t* iconData;
	size_t iconSize;
	const char* name;
	const char* region;
	uint16_t version;
	uint16_t dlc;
} CemuGameListRow;

typedef void (*GameListCallback)(uint64_t titleId);

extern "C" void CemuGameListCreate(void);
extern "C" void CemuGameListDestroy(void);
extern "C" void CemuGameListRefresh(void);
extern "C" bool CemuGameListIsScanning(void);
extern "C" size_t CemuGameListGetCount(void);
extern "C" bool CemuGameListGetRow(size_t index, CemuGameListRow* outRow);
extern "C" void CemuGameListFreeBuffer(void* ptr);