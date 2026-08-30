#pragma once
#include <stdint.h>
#include <stddef.h>

class PkgUtils {
public:
public:
    static bool GetTitleAndIcon(const char* url, char* outTitle, int titleLen, char* outIconPath, int iconPathLen, char* outTitleId, int titleIdLen, char* outCategory, int categoryLen, char* outContentId, int contentIdLen, int taskId);
    static bool ParseSfo(const uint8_t* data, size_t size, char* outTitle, int titleLen, char* outTitleId, int titleIdLen, char* outCategory, int categoryLen);
};
