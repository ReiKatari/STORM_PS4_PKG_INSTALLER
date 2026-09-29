#pragma once

#include <stdint.h>
#include <stdbool.h>

struct StorageInfo {
    uint64_t totalBytes;
    uint64_t freeBytes;
    uint64_t usedBytes;
    float freeGB;
    float totalGB;
    float usedGB;
    int percentUsed;
    bool isLowSpace; // < 15 GB
};

struct SystemModelInfo {
    char modelName[32];     // "PS4 Pro", "PS4"
    char firmwareStr[16];   // "11.00", "9.00", etc.
    char henName[32];       // "GoldHEN"
    char henVersion[16];    // "v2.4"
    bool isNeo;             // true if PS4 Pro
    bool isGoldHen;
};

// Initialize system and hardware detection
void SystemInfo_Init();

// Get cached system model and firmware info
const SystemModelInfo* SystemInfo_Get();

// Refresh and query storage info for given mount (default: "/user")
bool SystemInfo_GetStorage(const char* path, StorageInfo* outInfo);

// Check if console has enough space for package download
bool SystemInfo_CheckHasEnoughSpace(uint64_t requiredBytes, uint64_t safetyMargin = 1024ULL * 1024 * 1024);
