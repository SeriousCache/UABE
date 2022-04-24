#pragma once
#include <Windows.h>
#include <vector>
#include <stdint.h>

bool SetProgramIconResource(const TCHAR *filePath, const std::vector<uint8_t> &iconFileData);
