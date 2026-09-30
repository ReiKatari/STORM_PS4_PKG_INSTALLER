// STORM PS4 PKG INSTALLER v1.60 - Advanced UI
#include "../include/Graphics.h"
#include "../include/Installer.h"
#include "../include/WebServer.h"
#include "../include/ThreadHelper.h"
#include "../include/HttpHelper.h"
#include "../include/Localization.h"
#include "../include/SystemInfo.h"
#include "../include/font_data.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/Net.h>
#include <orbis/NetCtl.h>
#include <orbis/Pad.h> // Gamepad
#include <orbis/UserService.h> // Helper for User ID
#include <orbis/_types/sysmodule.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h> // NEW: For listing directory
#include <map> // NEW: For Icon Cache

#define PORT 12813

// Logging
static int logFd = -1;

void LogInit() {
    if (logFd < 0) {
        logFd = open("/data/sppi_main.log", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    }
}

extern "C" void Log(const char* fmt, ...) {
    if (logFd < 0) return;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    write(logFd, buf, strlen(buf));
    write(logFd, "\n", 1);
}

void ListDir(const char* path) {
    Log("--- Listing: %s ---", path);
    DIR* d = opendir(path);
    if (d) {
        struct dirent* dir;
        while ((dir = readdir(d)) != NULL) {
            Log(" - %s type=%d", dir->d_name, dir->d_type);
        }
        closedir(d);
    } else {
        Log("Failed to open dir: %s", path);
    }
    Log("---------------------");
}

// System notification using proper struct
struct SppiNotifyReq {
    int type;
    int unk0;
    int targetId;
    char message[1024];
    char uri[1024];
    char unk1[1024];
};

void ShowNotification(const char* text) {
    SppiNotifyReq req;
    memset(&req, 0, sizeof(req));
    req.type = 0;
    req.targetId = -1;
    strncpy(req.message, text, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, (OrbisNotificationRequest*)&req, sizeof(req), 0);
    Log("NOTIFY: %s", text);
}

// Helper to sanitize strings (remove binary garbage)
void SanitizeString(char* str) {
    if (!str) return;
    for (int i = 0; str[i]; i++) {
        // ALLOW extended ASCII for UTF-8 or other encodings
        // Only mask strictly control characters < 32
        if ((unsigned char)str[i] < 32) str[i] = ' ';
    }
}

// WebServer thread
static volatile bool s_serverRunning = false;

void* WebServerThread(void* arg) {
    Log("WebServer thread started");
    s_serverRunning = true;
    
    while (s_serverRunning) {
        WebServer_Process();
        Thread_Sleep(10); // 10ms between checks
    }
    
    Log("WebServer thread stopping");
    return NULL;
}

#define FRAME_WIDTH     1920
#define FRAME_HEIGHT    1080

int main() {
    LogInit();
    Localization_Init();
    Log("=== STORM PS4 PKG INSTALLER v1.50 ===");
    
    // Load system modules
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_VIDEO_OUT);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_BGFT);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD);
    // POSIX is included via -lScePosix
    Log("Modules loaded");
    
    // Load PRX
    sceKernelLoadStartModule("/app0/sce_module/libc.prx", 0, NULL, 0, NULL, NULL);
    sceKernelLoadStartModule("/app0/sce_module/libjbc.prx", 0, NULL, 0, NULL, NULL);
    Log("PRX loaded");

    // Init HttpHelper early (in main thread)
    HttpHelper_Init();
    Log("HttpHelper initialized");
    
    // Init network
    sceNetInit();
    sceNetPoolCreate("pool", 4*1024*1024, 0);
    sceNetCtlInit();

    // Init User Service & Pad
    // Get IP
    char ipAddr[32] = "Unknown";
    OrbisNetCtlInfo info;
    if (sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS, &info) >= 0) {
        strncpy(ipAddr, info.ip_address, sizeof(ipAddr));
    }
    Log("IP: %s", ipAddr);

    // CRASH FIX: Init Installer BEFORE Graphics to prevent Heap Corruption issues
    Log("Allocating Installer...");
    Installer* installer = new Installer();
    if (!installer) Log("FATAL: Failed to allocate Installer!");
    else Log("Installer allocated. Calling Init()...");
    
    bool hasInstaller = installer->Init();
    Log("Installer: %s", hasInstaller ? "OK" : "FAIL");
    
    // Init graphics
    Scene2D* scene = new Scene2D(FRAME_WIDTH, FRAME_HEIGHT, 4);
    // Use 0xC000000 (192MB) size, 2 buffers
    bool hasGraphics = scene->Init(0xC000000, 2);
    Log("Graphics: %s", hasGraphics ? "OK" : "FAIL");
    
    // Init System and Hardware info
    SystemInfo_Init();
    
    // Init web server with 3 ports: 12813 (STORM), 12800 (RPI) and 12801 (PackageFlow)
    WebServer_SetInstaller(installer);
    int serverRet = WebServer_Start(PORT, 12800, 12801);
    bool hasServer = (serverRet == 0);
    Log("Server: %s (ret=%d, ports=%d/%d/%d)", hasServer ? "OK" : "FAIL", serverRet, 
        PORT, WebServer_GetRpiPort(), WebServer_GetPkgFlowPort());

    // Init Pad (Relies on Installer having init UserService)
    int32_t userId = installer->GetUserId();
    // Pad and CWD logging moved up
    
    // Show startup notification
    char startupMsg[128];
    snprintf(startupMsg, sizeof(startupMsg), "STORM PKG v1.60 - %s:%d/%d/%d [%s]", 
             ipAddr, PORT, WebServer_GetRpiPort(), WebServer_GetPkgFlowPort(), Localization_GetLanguageCode());
    ShowNotification(startupMsg);

    // Modern Colors (Dark Theme)
    Color colBg = {15, 15, 20, 255};      // #0F0F14
    Color colHeader = {25, 25, 35, 255};  // #191923
    Color colCard = {30, 30, 40, 255};    // #1E1E28
    Color colAccent = {0, 122, 255, 255}; // #007AFF (Blue)
    Color colText = {245, 245, 245, 255}; // #F5F5F5
    Color colSubText = {150, 150, 160, 255};// #9696A0
    Color colSuccess = {40, 200, 80, 255};// #28C850
    Color colError = {220, 60, 60, 255};  // #DC3C3C
    Color colTrack = {20, 20, 25, 255};   // #141419
    // === PAD INITIALIZATION ===
    int padInitRes = scePadInit();
    int padHandle = -1;
    int32_t padUserId = userId; // Start with installer's userId
    
    // Try to get FOREGROUND user (more reliable for pad)
    int32_t fgUser = -1;
    int fgRet = sceUserServiceGetForegroundUser(&fgUser);
    if (fgRet >= 0 && fgUser > 0) {
        padUserId = fgUser;
    }
    
    // Method 1: scePadOpen with foreground user
    padHandle = scePadOpen(padUserId, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
    
    // Method 2: Try with original userId if different
    if (padHandle < 0 && padUserId != userId) {
        padHandle = scePadOpen(userId, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
    }
    
    // Method 3: scePadGetHandle (alternative API)
    if (padHandle < 0) {
        padHandle = scePadGetHandle(padUserId, ORBIS_PAD_PORT_TYPE_STANDARD, 0);
    }
    
    // Method 4: System user 0xFF
    if (padHandle < 0) {
        padHandle = scePadOpen(0xFF, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
    }
    
    // Log detailed debug info
    char padDebugInfo[256];
    snprintf(padDebugInfo, sizeof(padDebugInfo), 
             "Pad: Init=%d FG=%d(%d) UID=%d Hnd=%d", 
             padInitRes, fgUser, fgRet, padUserId, padHandle);
    ShowNotification(padDebugInfo);
    
    int serverThreadId = -1;
    if (hasServer) {
        serverThreadId = Thread_Create(WebServerThread, NULL, "WebServer");
        Log("WebServer thread ID: %d", serverThreadId);
    }

    // === BACKGROUND IMAGES ===
    // Use sceKernelOpen for PS4 sandbox compatibility
    // LOG ANALYSIS: /app0 not visible, but /mnt/sandbox/pfsmnt/SPPI01313-app0 exists!
    const char* bgPaths[] = {
        "/mnt/sandbox/pfsmnt/SPPI01313-app0/assets/bg.png",      // PKG Embedded (Sandbox explicit)
        "/mnt/sandbox/pfsmnt/SPPI01313-app0/sce_sys/pic1.png",   // PKG System (Sandbox explicit)
        "/app0/assets/bg.png",                                   // Fallback standard
        "/data/pic1.png",                                        // External override (pic1.png is in DirList)
        "/data/bg.png"                                           // External override alt
    };
    const int bgPathCount = 5;
    
    PNG* bgPic0 = NULL;
    char bgStatusMsg[128] = "No BG";
    int lastErr = 0;
    
    for (int i = 0; i < bgPathCount; i++) {
        const char* p = bgPaths[i];
        
        // Use sceKernelOpen (PS4 native API)
        int fd = sceKernelOpen(p, 0x0000, 0); // O_RDONLY = 0
        Log("BG try[%d]: %s -> fd=%d", i, p, fd);
        
        if (fd >= 0) {
            // Get file size using LSEEK (fstat was unreliable: 6KB instead of 3MB)
            off_t fileSize = sceKernelLseek(fd, 0, SEEK_END);
            sceKernelLseek(fd, 0, SEEK_SET); // Rewind
            
            Log("BG size check: lseek=%lld", (long long)fileSize);
            
            if (fileSize > 0 && fileSize < 20*1024*1024) { // Limit 20MB
                uint8_t* buf = (uint8_t*)malloc(fileSize);
                if (buf) {
                    ssize_t rd = sceKernelRead(fd, buf, fileSize);
                    Log("BG read: %zd / %lld bytes", rd, (long long)fileSize);
                    
                    if (rd == (ssize_t)fileSize) {
                        bgPic0 = new PNG(buf, (int)fileSize);
                        if (bgPic0 && bgPic0->data) {
                            snprintf(bgStatusMsg, sizeof(bgStatusMsg), "BG[%d] %dx%d", i, bgPic0->width, bgPic0->height);
                            Log("BG OK: %s (%dx%d)", p, bgPic0->width, bgPic0->height);
                        } else {
                            snprintf(bgStatusMsg, sizeof(bgStatusMsg), "PNG ERR[%d]", i);
                            Log("BG PNG decode FAILED");
                            if (bgPic0) delete bgPic0;
                            bgPic0 = NULL;
                        }
                    }
                    free(buf);
                }
            }
            sceKernelClose(fd);
            if (bgPic0) break;
        } else {
            lastErr = fd; // fd is error code when negative
        }
    }
    
    if (!bgPic0) {
        snprintf(bgStatusMsg, sizeof(bgStatusMsg), "No BG E:%d", lastErr);
    }
    
    Log("BG result: %s", bgStatusMsg);

    // OPTIMIZATION: Temporarily Disabled for debugging crash
    uint32_t* bgCache = NULL;
    int bgCacheW = 0, bgCacheH = 0;
    /*
    if (bgPic0 && bgPic0->data) {
        bgCacheW = bgPic0->width;
        bgCacheH = bgPic0->height;
        bgCache = (uint32_t*)malloc(bgCacheW * bgCacheH * sizeof(uint32_t));
        if (bgCache) {
            // Convert RGBA (stb_image) to framebuffer format 0x80RRGGBB
            for (int i = 0; i < bgCacheW * bgCacheH; i++) {
                uint32_t pixel = bgPic0->data[i];
                uint8_t r = (pixel) & 0xFF;
                uint8_t g = (pixel >> 8) & 0xFF;
                uint8_t b = (pixel >> 16) & 0xFF;
                // Alpha is ignored for background - always opaque
                bgCache[i] = 0x80000000 | (r << 16) | (g << 8) | b;
            }
            Log("BG Cache created (%dx%d)", bgCacheW, bgCacheH);
        }
    }
    */

    char cwd[256] = "unk"; // Fixed: Restore variable for notification
    
    // Attempt init pad (redundant if Installer did it, but safe)
    
    // Attempt init pad (redundant if Installer did it, but safe)
    // int padInitRes = scePadInit(); // Already done above
    
    // Retry with UserID 0xFE if failed (System)
    if (padHandle < 0) {
         padHandle = scePadOpen(0xFE, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
    }
    
    char debugMsg[256];
    // Show CWD and Pad info
    snprintf(debugMsg, sizeof(debugMsg), "%s | US:%d Pad:%d(%d) | %s", 
             cwd, userId, padHandle, padInitRes, bgStatusMsg);
    ShowNotification(debugMsg);
    Font* fontHeader = new Font(century_gothic_ttf, century_gothic_ttf_len, 32);
    Font* fontBold = new Font(century_gothic_ttf, century_gothic_ttf_len, 24);
    Font* fontRegular = new Font(century_gothic_ttf, century_gothic_ttf_len, 20);
    Font* fontSmall = new Font(century_gothic_ttf, century_gothic_ttf_len, 16);
    Log("Century Gothic TrueType fonts loaded from memory.");
    
    Log("Startup done. Entering main loop.");
    
    // === MAIN LOOP ===
    int frameID = 0;
    // Allocate tasks on HEAP to avoid stack overflow with MAX_TASKS=999
    InstallTask* tasks = new InstallTask[MAX_TASKS];
    
    // Gamepad state
    int selectedRow = 0;
    
    // Menu State
    bool showMenu = false;
    int menuOption = 0; // 0=Cancel/Delete, 1=Back
    
    // Fullscreen Icon State
    bool showFullscreenIcon = false;
    
    // Icon Cache
    std::map<int, PNG*> iconCache;
    
    int scrollOffset = 0;
    const int rowsPerPage = 8; 
    int lastButtons = 0;
    int holdCounter = 0;

    while (s_serverRunning) {
        // Read Pad
        OrbisPadData padData;
        int retPad = scePadReadState(padHandle, &padData);
        if (retPad == 0) {
           int btns = padData.buttons;
           
           if (showFullscreenIcon) {
               // ANY BUTTON closes fullscreen
               if (btns && !lastButtons) {
                   showFullscreenIcon = false;
               }
           }
           else if (showMenu) {
               // === MENU CONTROLS ===
               if ((btns & ORBIS_PAD_BUTTON_UP) && !(lastButtons & ORBIS_PAD_BUTTON_UP)) {
                   menuOption--;
                   if (menuOption < 0) menuOption = 1;
               }
               if ((btns & ORBIS_PAD_BUTTON_DOWN) && !(lastButtons & ORBIS_PAD_BUTTON_DOWN)) {
                   menuOption++;
                   if (menuOption > 1) menuOption = 0;
               }
               
               // CLOSE MENU (Circle)
               if ((btns & ORBIS_PAD_BUTTON_CIRCLE) && !(lastButtons & ORBIS_PAD_BUTTON_CIRCLE)) {
                   showMenu = false;
               }
               
               // EXECUTE ACTION (Cross)
               if ((btns & ORBIS_PAD_BUTTON_CROSS) && !(lastButtons & ORBIS_PAD_BUTTON_CROSS)) {
                   if (menuOption == 1) { // Back
                       showMenu = false;
                   } else {
                       // ACTION: Delete / Cancel
                       int count = installer->GetTasks(tasks, MAX_TASKS);
                       if (selectedRow < count) {
                           int tid = tasks[selectedRow].taskId;
                           const char* status = tasks[selectedRow].status;
                           
                           // Logic: If downloading/installing -> Stop + Unregister
                           //        If completed/error -> Unregister only
                           bool isActive = (strstr(status, "Downloading") || strstr(status, "Installing"));
                           
                           if (isActive) {
                               installer->StopTask(tid);
                               ShowNotification(Loc(STR_TASK_STOPPED));
                           }
                           
                            installer->UnregisterTask(tid);
                            ShowNotification(Loc(STR_TASK_REMOVED));
                            
                            // MEMORY FIX: Clean up icon cache when task is removed
                            if (iconCache.find(tid) != iconCache.end()) {
                                if (iconCache[tid]) delete iconCache[tid];
                                iconCache.erase(tid);
                                Log("Icon cache cleared for Task %d", tid);
                            }
                        }
                       showMenu = false;
                   }
               }
               
           } else {
               // === LIST CONTROLS ===
               
               // UP / DOWN
               if (btns & ORBIS_PAD_BUTTON_DOWN) {
                   if (!(lastButtons & ORBIS_PAD_BUTTON_DOWN) || (holdCounter++ > 20)) {
                       selectedRow++;
                       if (holdCounter > 20) holdCounter = 15; 
                   }
               } else if (btns & ORBIS_PAD_BUTTON_UP) {
                   if (!(lastButtons & ORBIS_PAD_BUTTON_UP) || (holdCounter++ > 20)) {
                       selectedRow--;
                       if (holdCounter > 20) holdCounter = 15;
                   }
               } else {
                   holdCounter = 0;
               }
               
               // L1 / L2 (Fast Scroll)
               if ((btns & ORBIS_PAD_BUTTON_L1) && !(lastButtons & ORBIS_PAD_BUTTON_L1)) {
                   selectedRow -= 10;
                   if (selectedRow < 0) selectedRow = 0;
               }
               if ((btns & ORBIS_PAD_BUTTON_L2) && !(lastButtons & ORBIS_PAD_BUTTON_L2)) {
                   selectedRow += 10;
                   // Clamping happens later
               }
               
               // OPEN MENU (Cross)
               if ((btns & ORBIS_PAD_BUTTON_CROSS) && !(lastButtons & ORBIS_PAD_BUTTON_CROSS)) {
                   int count = installer->GetTasks(tasks, MAX_TASKS);
                   if (count > 0) {
                       showMenu = true;
                       menuOption = 0; // Reset to top option
                   }
               }

               // FULLSCREEN ICON (Square)
               if ((btns & ORBIS_PAD_BUTTON_SQUARE) && !(lastButtons & ORBIS_PAD_BUTTON_SQUARE)) {
                   int count = installer->GetTasks(tasks, MAX_TASKS);
                   if (count > 0 && selectedRow < count) {
                       showFullscreenIcon = true;
                   }
               }

               // LANGUAGE SWITCH (Triangle)
               if ((btns & ORBIS_PAD_BUTTON_TRIANGLE) && !(lastButtons & ORBIS_PAD_BUTTON_TRIANGLE)) {
                   Localization_CycleLanguage();
                   char langMsg[128];
                   snprintf(langMsg, sizeof(langMsg), "%s: %s", 
                            Loc(STR_LANG_NAME), Localization_GetLanguageCode());
                   ShowNotification(langMsg);
               }
           }
           lastButtons = btns;
        }

        // Clip Selection
        int taskCount = installer->GetTasks(tasks, MAX_TASKS);
        if (taskCount == 0) selectedRow = 0;
        else if (selectedRow < 0) selectedRow = 0;
        else if (selectedRow >= taskCount) selectedRow = taskCount - 1;
        
        // Auto Scroll
        if (selectedRow < scrollOffset) scrollOffset = selectedRow;
        if (selectedRow >= scrollOffset + rowsPerPage) scrollOffset = selectedRow - rowsPerPage + 1;
        
        // Limit scroll
        if (scrollOffset < 0) scrollOffset = 0;

        // === AUTO SCROLL LOGIC ===
        static int lastTaskCount = 0;
        if (taskCount > lastTaskCount) {
             // New task added: Scroll to bottom AND Select Newest to prevent jump back
             if (taskCount > rowsPerPage) {
                 scrollOffset = taskCount - rowsPerPage;
             }
             // FIX: Move selection to the new item so "Auto Scroll" logic doesn't snap back to top
             selectedRow = taskCount - 1;
        }
        lastTaskCount = taskCount;
        
        // === STATIC TITLE CACHE LOGIC ===
        // (Copied primarily to ensure it persists)
        // Cache titles to prevent flickering
        static char cachedTitles[MAX_TASKS][64];
        static int cachedTaskIds[MAX_TASKS] = {0};
        static int cachedCount = 0;
        
        for (int i = 0; i < taskCount; i++) {
            // Find if this taskId is already cached
            int cacheIndex = -1;
            for (int j = 0; j < cachedCount; j++) {
                if (cachedTaskIds[j] == tasks[i].taskId) {
                    cacheIndex = j;
                    break;
                }
            }
            
            bool currentTitleValid = (strlen(tasks[i].title) > 0 && strcmp(tasks[i].title, "Unknown") != 0);
            
            if (cacheIndex != -1) {
                if (currentTitleValid) {
                     SanitizeString(tasks[i].title);
                     strncpy(cachedTitles[cacheIndex], tasks[i].title, 63);
                } else {
                     strncpy(tasks[i].title, cachedTitles[cacheIndex], 63);
                }
            } else {
                if (cachedCount < MAX_TASKS) {
                    SanitizeString(tasks[i].title);
                    cachedTaskIds[cachedCount] = tasks[i].taskId;
                    strncpy(cachedTitles[cachedCount], tasks[i].title, 63);
                    cachedCount++;
                }
            }
        }
        
        // Draw Frame
        if (hasGraphics) {
            scene->FrameBufferClear();
            
            // 1. Background Layer (drawn behind everything)
            if (bgCache) {
                scene->BlitBuffer(bgCache, bgCacheW, bgCacheH);
                scene->DrawRectangle(0, 0, 1920, 1080, Color(10, 12, 18, 195));
            } else if (bgPic0 && bgPic0->data) {
                bgPic0->Draw(scene, 0, 0, 1920, 1080);
                scene->DrawRectangle(0, 0, 1920, 1080, Color(10, 12, 18, 195));
            } else {
                // Fallback: Gradient (only when no background image)
                for (int y = 0; y < 1080; y++) {
                    float t = (float)y / 1080.0f;
                    Color gradCol;
                    gradCol.r = (uint8_t)(26.0f * (1.0f - t) + 15.0f * t);
                    gradCol.g = (uint8_t)(26.0f * (1.0f - t) + 15.0f * t);
                    gradCol.b = (uint8_t)(46.0f * (1.0f - t) + 20.0f * t);
                    gradCol.a = 255;
                    scene->DrawRectangle(0, y, 1920, 1, gradCol);
                }
            }
            
            // 2. Header Layer (115px height, beautifully grouped)
            int headerH = 115;
            scene->DrawRectangle(0, 0, 1920, headerH, colHeader);
            scene->DrawRectangle(0, headerH - 2, 1920, 2, colAccent); // Accent line
            
            // App Title (Left Top)
            if (fontHeader && fontHeader->ttf_buffer) fontHeader->DrawText(scene, 45, 38, Loc(STR_APP_TITLE), colText);
            else scene->DrawText(Loc(STR_APP_TITLE), 45, 32, colText);
            
            // Console Model, FW and GoldHEN info (Left Bottom)
            const SystemModelInfo* sysInfo = SystemInfo_Get();
            char sysModelLine[128];
            snprintf(sysModelLine, sizeof(sysModelLine), "%s  |  FW %s  |  %s", 
                     sysInfo->modelName, sysInfo->firmwareStr, sysInfo->henName);
            if (fontSmall && fontSmall->ttf_buffer) fontSmall->DrawText(scene, 45, 80, sysModelLine, Color(140, 180, 220));
            else scene->DrawText(sysModelLine, 45, 80, Color(140, 180, 220), 1);

            // Language Switch Hint (Left Center)
            char langHint[64];
            snprintf(langHint, sizeof(langHint), "▲ [%s] %s", Localization_GetLanguageCode(), Loc(STR_LANG_NAME));
            if (fontSmall && fontSmall->ttf_buffer) fontSmall->DrawText(scene, 370, 80, langHint, Color(0, 210, 255));
            else scene->DrawText(langHint, 370, 80, Color(0, 210, 255), 1);
            
            // Server Info (Right Top: 3 Ports: 12813 / 12800 / 12801)
            char statusLine[128];
            snprintf(statusLine, sizeof(statusLine), "%s : %d/%d/%d  |  %s", 
                     ipAddr, PORT, WebServer_GetRpiPort(), WebServer_GetPkgFlowPort(), 
                     s_serverRunning ? Loc(STR_ONLINE) : Loc(STR_OFFLINE));
            Color statusCol = s_serverRunning ? colSuccess : colError;
            if (fontBold && fontBold->ttf_buffer) fontBold->DrawText(scene, 1140, 38, statusLine, statusCol);
            else scene->DrawText(statusLine, 1140, 32, statusCol, 3);
            
            // Storage Query & Display (Right Bottom)
            static StorageInfo userStorage;
            static int storageTimer = 0;
            if (storageTimer++ % 60 == 0) {
                SystemInfo_GetStorage("/user", &userStorage);
            }
            char storageLine[128];
            snprintf(storageLine, sizeof(storageLine), "%s: %.1f GB %s %.1f GB", 
                     Loc(STR_STORAGE), userStorage.freeGB, Loc(STR_FREE_OF), userStorage.totalGB);
            Color storageCol = userStorage.isLowSpace ? Color(255, 90, 90) : Color(200, 220, 240);
            if (fontSmall && fontSmall->ttf_buffer) fontSmall->DrawText(scene, 1140, 80, storageLine, storageCol);
            else scene->DrawText(storageLine, 1140, 80, storageCol, 1);

            // Storage Visual Gauge Bar (Right Corner)
            int barX = 1620;
            int barY = 82;
            int barW = 160;
            int barH = 12;
            scene->DrawRectangle(barX - 1, barY - 1, barW + 2, barH + 2, Color(45, 55, 75, 200)); // Border
            scene->DrawRectangle(barX, barY, barW, barH, Color(18, 22, 32, 240)); // Track
            int fillW = (int)((barW * (100 - userStorage.percentUsed)) / 100);
            if (fillW < 0) fillW = 0;
            if (fillW > barW) fillW = barW;
            Color barFillColor = userStorage.isLowSpace ? Color(255, 70, 70) : Color(0, 220, 160);
            scene->DrawRectangle(barX, barY, fillW, barH, barFillColor);

            char pctStr[16];
            snprintf(pctStr, sizeof(pctStr), "%d%%", 100 - userStorage.percentUsed);
            if (fontSmall && fontSmall->ttf_buffer) fontSmall->DrawText(scene, barX + barW + 8, 80, pctStr, Color(160, 180, 200));
            else scene->DrawText(pctStr, barX + barW + 8, 80, Color(160, 180, 200), 1);

            // Stats calculation
            uint64_t totalInstalledSize = 0;
            int totalCompletedCount = 0;
            uint64_t totalQueueSize = 0;
            int totalQueueCount = 0;
            for (int i = 0; i < taskCount; i++) {
                if (strstr(tasks[i].status, "Completed") || strstr(tasks[i].status, "Installed")) {
                    totalCompletedCount++;
                    totalInstalledSize += tasks[i].totalSize;
                } else {
                    totalQueueCount++;
                    totalQueueSize += tasks[i].totalSize;
                }
            }
            char strCompletedStats[64];
            snprintf(strCompletedStats, sizeof(strCompletedStats), "%d %s", totalCompletedCount, Loc(STR_COMPLETED));
            
            char strSizeStats[64];
            if (totalInstalledSize < 1024*1024) snprintf(strSizeStats, 63, "%.2f MB", (float)totalInstalledSize / (1024.0f*1024.0f));
            else snprintf(strSizeStats, 63, "%.2f GB", (float)totalInstalledSize / (1024.0f*1024.0f*1024.0f));
            
            // 3. Table UI
            int tableY = 180;
            int rowHeight = 100;
            int tableX = 45;
            int tableW = 1780;
            Color colGlass = {30, 30, 40, 180};
            
            // Animation
            float pulse = (sin((float)frameID * 0.05f) + 1.0f) * 0.5f;
            Color colActivePulse = colAccent;
            colActivePulse.r = (uint8_t)(colActivePulse.r * (0.7f + 0.3f*pulse));
            colActivePulse.b = (uint8_t)(colActivePulse.b * (0.7f + 0.3f*pulse));
            
            // Draw Table Headers
            // Adjust X positions to fit Title ID - COMPACT RIGHT SIDE
            int xID = tableX + 10;
            int xCAT = tableX + 130;  
            int xICON = tableX + 220; 
            int xTITLE = tableX + 330; // 330 to 1100 = 770px for Title

            int xTITLEID = tableX + 1100; // TitleID Column RESTORED
            int xSIZE = tableX + 1300; 
            int xSTATUS = tableX + 1480; 
            int xPROG = tableX + 1680;
            
            // Queue Stats (Left over table)
            char strQueueStats[64];
            char qSizeBuf[32];
            if (totalQueueSize < 1024*1024) snprintf(qSizeBuf, sizeof(qSizeBuf), "%.1f MB", (float)totalQueueSize / (1024.0f*1024.0f));
            else snprintf(qSizeBuf, sizeof(qSizeBuf), "%.2f GB", (float)totalQueueSize / (1024.0f*1024.0f*1024.0f));
            snprintf(strQueueStats, sizeof(strQueueStats), "%s: %d  |  %s", Loc(STR_PENDING), totalQueueCount, qSizeBuf);
            if (fontBold && fontBold->ttf_buffer) {
                fontBold->DrawText(scene, xID, tableY - 45, strQueueStats, Color(150, 200, 250));
            } else {
                scene->DrawText(strQueueStats, xID, tableY - 50, Color(150, 200, 250), 3);
            }

            // Completed Stats (Right over table)
            if (fontBold && fontBold->ttf_buffer) {
                fontBold->DrawText(scene, xSIZE, tableY - 45, strSizeStats, Color(200, 200, 200));
                fontBold->DrawText(scene, xSTATUS, tableY - 45, strCompletedStats, Color(100, 255, 100));
            } else {
                scene->DrawText(strSizeStats, xSIZE, tableY - 50, Color(200, 200, 200), 3);
                scene->DrawText(strCompletedStats, xSTATUS, tableY - 50, Color(100, 255, 100), 3);
            }

            // Header Text
            auto DrawHeaderCol = [&](const char* text, int x) {
                if (fontBold && fontBold->ttf_buffer) fontBold->DrawText(scene, x, tableY + 8, text, Color(150, 200, 250));
                else scene->DrawText(text, x, tableY + 10, Color(150, 200, 250), 2);
            };
            DrawHeaderCol(Loc(STR_COL_ID), xID);
            DrawHeaderCol(Loc(STR_COL_CAT), xCAT);
            DrawHeaderCol(Loc(STR_COL_ICON), xICON);
            DrawHeaderCol(Loc(STR_COL_TITLE), xTITLE);
            DrawHeaderCol(Loc(STR_COL_TITLE_ID), xTITLEID);
            DrawHeaderCol(Loc(STR_COL_SIZE), xSIZE);
            DrawHeaderCol(Loc(STR_COL_STATUS), xSTATUS);
            DrawHeaderCol(Loc(STR_COL_PROGRESS), xPROG);
            
            for (int i = 0; i < taskCount; i++) {
                if (i < scrollOffset || i >= scrollOffset + rowsPerPage) continue;
                int visualIndex = i - scrollOffset; // 0..rowsPerPage-1
                char progStr[32]; 
                snprintf(progStr, sizeof(progStr), "%d%%", (int)(tasks[i].progress * 100));
                
                char sizeStr[32];
                if (tasks[i].totalSize == 0) strcpy(sizeStr, "-");
                else if (tasks[i].totalSize < 1024) snprintf(sizeStr, 31, "%llu B", (unsigned long long)tasks[i].totalSize);
                else if (tasks[i].totalSize < 1024*1024) snprintf(sizeStr, 31, "%.1f KB", (float)tasks[i].totalSize / 1024.0f);
                else if (tasks[i].totalSize < 1024*1024*1024) snprintf(sizeStr, 31, "%.1f MB", (float)tasks[i].totalSize / (1024.0f*1024.0f));
                else snprintf(sizeStr, 31, "%.2f GB", (float)tasks[i].totalSize / (1024.0f*1024.0f*1024.0f));

                int rowY = tableY + 40 + (visualIndex * (rowHeight + 5)); 
                
                Color rowBg = colGlass; // Default
                if (i == selectedRow) {
                     rowBg = Color(60, 70, 90, 200);
                     scene->DrawRectangle(tableX - 5, rowY - 2, tableW + 10, rowHeight + 4, Color(0, 122, 255, 200)); // Border
                }
                
                scene->DrawRectangle(tableX, rowY, tableW, rowHeight, rowBg);
                
                int tid = tasks[i].taskId;
                if (iconCache.find(tid) == iconCache.end()) {
                    if (strlen(tasks[i].iconPath) > 0) {
                         PNG* newIcon = new PNG(tasks[i].iconPath);
                         if (newIcon && newIcon->data) iconCache[tid] = newIcon;
                         else { if(newIcon) delete newIcon; iconCache[tid] = NULL; }
                    }
                }
                
                PNG* icon = iconCache[tid];
                if (icon) icon->Draw(scene, xICON, rowY + 5, 90, 90);
                else scene->DrawRectangle(xICON, rowY + 5, 90, 90, colTrack); 
                
                const char* statusText = tasks[i].status;
                Color statColor = colText;
                
                if (strstr(tasks[i].status, "Downloading") || strstr(tasks[i].status, "Installing")) {
                    statusText = strstr(tasks[i].status, "Installing") ? Loc(STR_INSTALLING) : Loc(STR_DOWNLOADING);
                    statColor = colActivePulse;
                } else if (strstr(tasks[i].status, "Completed") || strstr(tasks[i].status, "Installed")) {
                    statusText = Loc(STR_COMPLETED);
                    statColor = colSuccess;
                } else if (strstr(tasks[i].status, "Error") || strstr(tasks[i].status, "Err")) {
                    statusText = Loc(STR_ERROR);
                    statColor = colError;
                } else if (strstr(tasks[i].status, "Queued")) {
                    statusText = Loc(STR_PENDING);
                } else if (strstr(tasks[i].status, "Paused") || strstr(tasks[i].status, "Stopped")) {
                    statusText = Loc(STR_TASK_STOPPED);
                }
                
                const char* catDisplay = LocCategory(tasks[i].category);
                char idStr[16]; snprintf(idStr, 15, "#%d", tasks[i].taskId);

                auto DrawRowText = [&](const char* text, int x, Color color) {
                    if (fontRegular && fontRegular->ttf_buffer) {
                        fontRegular->DrawText(scene, x, rowY + 38, text, color);
                    } else {
                        scene->DrawText(text, x, rowY + 35, color, 2);
                    }
                };

                DrawRowText(idStr, xID, colText);
                DrawRowText(catDisplay, xCAT, Color(200, 200, 100));
                DrawRowText(tasks[i].title, xTITLE, colText);
                DrawRowText(tasks[i].titleId, xTITLEID, Color(200, 200, 200));
                DrawRowText(sizeStr, xSIZE, colText);
                DrawRowText(statusText, xSTATUS, statColor);
                DrawRowText(progStr, xPROG, colText);
            }
            
            // 4. MENU OVERLAY (Topmost)
            if (showMenu) {
                // Dim background
                scene->DrawRectangle(0, 0, 1920, 1080, Color(0, 0, 0, 180));
                
                int menuW = 600;
                int menuH = 300;
                int menuX = (1920 - menuW) / 2;
                int menuY = (1080 - menuH) / 2;
                
                // Menu Card
                scene->DrawRectangle(menuX, menuY, menuW, menuH, Color(30, 32, 42, 255));
                scene->DrawRectangle(menuX, menuY, menuW, 55, Color(20, 22, 30, 255)); // Header
                scene->DrawRectangle(menuX, menuY + 53, menuW, 2, colAccent);
                
                if (fontBold && fontBold->ttf_buffer) {
                    fontBold->DrawText(scene, menuX + 25, menuY + 15, Loc(STR_MENU_MANAGE), colAccent);
                } else {
                    scene->DrawText(Loc(STR_MENU_MANAGE), menuX + 20, menuY + 10, colAccent, 3);
                }
                
                // Option 1: Remove / Cancel
                Color opt1Col = (menuOption == 0) ? colActivePulse : colText;
                const char* opt1Text = Loc(STR_MENU_DELETE);
                if (fontRegular && fontRegular->ttf_buffer) {
                    fontRegular->DrawText(scene, menuX + 60, menuY + 100, opt1Text, opt1Col);
                } else {
                    scene->DrawText(opt1Text, menuX + 50, menuY + 100, opt1Col, 3);
                }
                if (menuOption == 0) scene->DrawRectangle(menuX + 40, menuY + 100, 8, 26, colAccent);
                
                // Option 2: Back
                Color opt2Col = (menuOption == 1) ? colActivePulse : colText;
                const char* opt2Text = Loc(STR_MENU_BACK);
                if (fontRegular && fontRegular->ttf_buffer) {
                    fontRegular->DrawText(scene, menuX + 60, menuY + 175, opt2Text, opt2Col);
                } else {
                    scene->DrawText(opt2Text, menuX + 50, menuY + 180, opt2Col, 3);
                }
                if (menuOption == 1) scene->DrawRectangle(menuX + 40, menuY + 175, 8, 26, colAccent);
                 
                // Helper text
                if (fontSmall && fontSmall->ttf_buffer) {
                    fontSmall->DrawText(scene, menuX + 25, menuY + 255, Loc(STR_HELP_SELECT), Color(160, 160, 170));
                } else {
                    scene->DrawText(Loc(STR_HELP_SELECT), menuX + 20, menuY + 260, Color(150, 150, 150), 2);
                }
            }

            // 5. FULLSCREEN ICON OVERLAY (Extreme Topmost)
            if (showFullscreenIcon && selectedRow < taskCount) {
                 scene->DrawRectangle(0, 0, 1920, 1080, Color(0, 0, 0, 230)); // Dim darker
                 
                 int tid = tasks[selectedRow].taskId;
                 PNG* icon = NULL;
                 if (iconCache.find(tid) != iconCache.end()) icon = iconCache[tid];
                 
                 if (icon) {
                     int iW = 512;
                     int iH = 512;
                     int iX = (1920 - iW) / 2;
                     int iY = (1080 - iH) / 2 - 30;
                     icon->Draw(scene, iX, iY, iW, iH);
                     
                     // Draw Title below
                     char titleBuf[128];
                     strncpy(titleBuf, tasks[selectedRow].title, 64);
                     titleBuf[63] = '\0';
                     if (fontBold && fontBold->ttf_buffer) {
                         int titleW = fontBold->GetTextWidth(titleBuf);
                         fontBold->DrawText(scene, (1920 - titleW) / 2, iY + iH + 30, titleBuf, colText);
                     } else {
                         scene->DrawText(titleBuf, iX, iY + iH + 20, colText, 3);
                     }
                 } else {
                     if (fontBold && fontBold->ttf_buffer) {
                         fontBold->DrawText(scene, (1920 / 2) - 150, 1080 / 2, Loc(STR_NO_ICON), colError);
                     } else {
                         scene->DrawText(Loc(STR_NO_ICON), (1920/2)-200, 1080/2, colError, 3);
                     }
                 }
                 
                 if (fontRegular && fontRegular->ttf_buffer) {
                     fontRegular->DrawText(scene, (1920 / 2) - 160, 1000, Loc(STR_HELP_CLOSE), Color(160, 160, 170));
                 } else {
                     scene->DrawText(Loc(STR_HELP_CLOSE), (1920/2)-300, 1000, Color(150, 150, 150), 2);
                 }
            }

            scene->SubmitFlip(frameID);
            scene->FrameWait(frameID);
            scene->FrameBufferSwap();
            frameID++;
        }
        
        Thread_Sleep(16); // ~60 FPS
    }
    
    // Cleanup
    delete[] tasks;
    delete installer;
    delete scene;
    if (fontHeader) delete fontHeader;
    if (fontBold) delete fontBold;
    if (fontRegular) delete fontRegular;
    if (fontSmall) delete fontSmall;
    if (bgPic0) delete bgPic0;
    
    // MEMORY FIX: Full icon cache cleanup
    Log("Cleaning up Icon Cache (%d items)...", (int)iconCache.size());
    for (auto const& [id, icon] : iconCache) {
        if (icon) delete icon;
    }
    iconCache.clear();
    
    return 0;
}
