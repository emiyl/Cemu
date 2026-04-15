#pragma once

#include "Common/precompiled.h"

#include <cstddef>
#include <cstdint>

typedef struct
{
	uint64_t titleId;
	const char* name;
} CemuSwiftUIGameListRow;

typedef void (*GameListCallback)(uint64_t titleId);

extern "C" void CemuSwiftUIGameListCreate(void);
extern "C" void CemuSwiftUIGameListDestroy(void);
extern "C" void CemuSwiftUIGameListRefresh(void);
extern "C" size_t CemuSwiftUIGameListGetCount(void);
extern "C" bool CemuSwiftUIGameListGetRow(size_t index, CemuSwiftUIGameListRow* outRow);