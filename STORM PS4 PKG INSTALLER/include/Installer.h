#pragma once

#include "Common.h"

#include <orbis/Bgft.h>

#define MAX_TASKS 9999

struct InstallTask {
    int taskId;
    char title[128];
    float progress;
    char status[32];
    char iconPath[256];
    char category[8]; 
    char titleId[16];   // NEW: Title ID support
    uint64_t totalSize;
};

struct AppInfo {
    char titleId[16];
    char title[128];
    char version[16];
    uint64_t size;
};

#include <pthread.h>

class Installer {
private:
    int userId;
    bool initialized;
    int activeTaskIds[MAX_TASKS];
    char taskTitles[MAX_TASKS][64];
    char taskTitleIds[MAX_TASKS][16]; // NEW: Store Title IDs
    char taskIcons[MAX_TASKS][256];
    char taskCategories[MAX_TASKS][8];
    char* taskUrls[MAX_TASKS];      // NEW: Track URLs dynamically to save space
    uint64_t taskTotalSizes[MAX_TASKS];
    
    // MOVED TO CPP STATIC GLOBALS TO FIX STARTUP CRASH
    // float taskProgress[MAX_TASKS];
    // char taskStatus[MAX_TASKS][32];
    // uint64_t taskLastUpdate[MAX_TASKS]; 
    
    // MOVED TO CPP STATIC GLOBALS TO FIX ABI CRASH
    // uint64_t taskTransferredOffset[MAX_TASKS];
    // uint64_t taskLastTransferred[MAX_TASKS];

    int activeTaskCount;
    int lastError;
    int moduleLoadResult;
    int jbcResult;
    pthread_mutex_t mutex; // Thread safety
    
    int RegisterTask(int taskId, const char* title, const char* titleId, const char* iconPath, uint64_t totalSize, const char* category, const char* url);

public:
    Installer();
    ~Installer();

    bool Init();
    void Term();
    
    // Install - Returns taskId or negative error code
    int Install(const char* url, const char* title, uint64_t senderSize = 0);
    
    // Task management - RPI compatible API
    int PauseTask(int taskId);
    int ResumeTask(int taskId);
    int StopTask(int taskId);
    int UnregisterTask(int taskId);
    int FindTaskByContentId(const char* contentId);
    int FindTaskByUrl(const char* url);  // NEW: Prevent duplicate tasks
    
    // Get all tasks
    int GetTasks(InstallTask* outTasks, int maxTasks);
    
    // Get single task progress
    bool GetTaskProgress(int taskId, InstallTask* outTask);
    
    // === DEINSTALLATION API (RPI compatible) ===
    // Returns 0 on success, negative error code on failure
    int UninstallGame(const char* titleId);
    int UninstallPatch(const char* titleId);
    int UninstallAddcont(const char* titleId, const char* contentId);
    int UninstallTheme(const char* contentId);
    
    // Check if app exists
    bool AppExists(const char* titleId);
    
    // Get app size in bytes
    // Get app size in bytes
    int64_t GetAppSize(const char* titleId);

    // Get list of installed apps
    int GetInstalledApps(AppInfo* outApps, int maxApps);
    
    // Check if app is installed (cached check)
    bool IsAppInstalled(const char* titleId);
    
    // Debug info
    int GetLastError() const { return lastError; }
    int GetModuleLoadResult() const { return moduleLoadResult; }
    int GetJbcResult() const { return jbcResult; }
    int GetActiveTaskCount() const { return activeTaskCount; }
    bool IsInitialized() { return initialized; }
    int GetUserId() const { return userId; }
};
