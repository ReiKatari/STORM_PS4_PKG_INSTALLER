// STORM PS4 PKG INSTALLER v1.44 - Advanced UI
#include "../include/Graphics.h"
#include "../include/Installer.h"
#include "../include/WebServer.h"
#include "../include/ThreadHelper.h"
#include "../include/HttpHelper.h"
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
    Log("=== STORM PS4 PKG INSTALLER v1.44 ===");
    
    // Load system modules
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_VIDEO_OUT);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_BGFT);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
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
    
    // Init web server
    WebServer_SetInstaller(installer);
    int serverRet = WebServer_Start(PORT);
    bool hasServer = (serverRet == 0);
    Log("Server: %s (ret=%d)", hasServer ? "OK" : "FAIL", serverRet);

    // Init Pad (Relies on Installer having init UserService)
    int32_t userId = installer->GetUserId();
    // Pad and CWD logging moved up
    
    // Show startup notification
    char startupMsg[128];
    snprintf(startupMsg, sizeof(startupMsg), "STORM PKG v1.44 - %s:%d", ipAddr, PORT);
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
    Font* fontRegular = NULL;
    Font* fontBold = NULL;
    Font* fontHeader = NULL;
    Font* fontSmall = NULL;
    Log("Using bitmap text (no custom fonts).");
    
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
                               ShowNotification("Task Stopped");
                           }
                           
                            installer->UnregisterTask(tid);
                            ShowNotification("Task Removed from List");
                            
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
            
            // 1. Background Layer - OPTIMIZED: Use BlitBuffer if cache exists
            if (bgCache) {
                // FAST PATH: memcpy entire pre-converted buffer (~100x faster!)
                scene->BlitBuffer(bgCache, bgCacheW, bgCacheH);
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
            
            // 2. Header Layer
            scene->DrawRectangle(0, 0, 1920, 100, colHeader);
            scene->DrawRectangle(0, 98, 1920, 2, colAccent); // Accent line
            
            // Title
            // Title
            if (fontHeader && fontHeader->ttf_buffer) fontHeader->DrawText(scene, 50, 65, "STORM PS4 PKG INSTALLER v1.44", colText);
            else scene->DrawText("STORM PS4 PKG INSTALLER v1.44", 50, 35, colText);
            

            
            // Server Info
            char statusLine[128];
            snprintf(statusLine, sizeof(statusLine), "%s : %d  |  %s", 
                     ipAddr, PORT, s_serverRunning ? "ONLINE" : "OFFLINE");
            Color statusCol = s_serverRunning ? colSuccess : colError;
            if (fontBold && fontBold->ttf_buffer) fontBold->DrawText(scene, 1350, 65, statusLine, statusCol);
            else scene->DrawText(statusLine, 1300, 35, statusCol, 3);
            
            // DEBUG OVERLAY (Centered)
            char padDebug[256];
            snprintf(padDebug, 255, "Pad:%d Btns:%08X | %s", padHandle, lastButtons, bgStatusMsg);
            // Calculate center position (rough estimate)
            int textLen = strlen(padDebug);
            int textX = (1920 - textLen * 8) / 2; // Approx 8px per char
            scene->DrawText(padDebug, textX, 82, Color(255, 255, 0), 1);

            // Stats
            uint64_t totalInstalledSize = 0;
            int totalCompletedCount = 0;
            for (int i = 0; i < taskCount; i++) {
                if (strstr(tasks[i].status, "Completed") || strstr(tasks[i].status, "Installed")) {
                    totalCompletedCount++;
                    totalInstalledSize += tasks[i].totalSize;
                }
            }
            char strCompletedStats[64];
            snprintf(strCompletedStats, sizeof(strCompletedStats), "%d COMPLETED", totalCompletedCount);
            
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
            
            // Stats Text (Raised + Larger)
            // Was: tableY-20, Scale 2. Now: tableY-60, Scale 3.
            scene->DrawText(strSizeStats, xSIZE, tableY - 50, Color(200, 200, 200), 3);
            scene->DrawText(strCompletedStats, xSTATUS, tableY - 50, Color(100, 255, 100), 3);

            // Header Text
            scene->DrawText("ID", xID, tableY + 10, Color(150,200,250), 2);
            scene->DrawText("CAT", xCAT, tableY + 10, Color(150,200,250), 2);
            scene->DrawText("ICON", xICON, tableY + 10, Color(150,200,250), 2); 
            scene->DrawText("TITLE", xTITLE, tableY + 10, Color(150,200,250), 2);
            scene->DrawText("TITLE ID", xTITLEID, tableY + 10, Color(150,200,250), 2);
            scene->DrawText("SIZE", xSIZE, tableY + 10, Color(150,200,250), 2);
            scene->DrawText("STATUS", xSTATUS, tableY + 10, Color(150,200,250), 2);
            scene->DrawText("PROGRESS", xPROG, tableY + 10, Color(150,200,250), 2);
            
            auto GetCategoryDisplay = [](const char* raw) -> const char* {
                if (!raw) return "UNK";
                if (strcasecmp(raw, "gd") == 0) return "GAME";
                if (strcasecmp(raw, "gp") == 0) return "UPDATE";
                if (strcasecmp(raw, "ac") == 0) return "DLC";
                if (strcasecmp(raw, "th") == 0 || strcasecmp(raw, "THEME") == 0) return "THEME";
                return raw;
            };

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
                    statColor = colActivePulse;
                }
                if (strstr(tasks[i].status, "Completed") || strstr(tasks[i].status, "Installed")) {
                    statusText = "COMPLETED"; // Unified status
                    statColor = colSuccess;
                }
                if (strstr(tasks[i].status, "Error") || strstr(tasks[i].status, "Err")) {
                    statColor = colError;
                }
                
                const char* catDisplay = GetCategoryDisplay(tasks[i].category);
                char idStr[16]; snprintf(idStr, 15, "#%d", tasks[i].taskId);
                scene->DrawText(idStr, xID, rowY + 15, colText, 2);
                scene->DrawText(catDisplay, xCAT, rowY + 15, Color(200, 200, 100), 2);
                scene->DrawText(tasks[i].title, xTITLE, rowY + 15, colText, 2);
                
                // Draw Title ID (RESTORED)
                scene->DrawText(tasks[i].titleId, xTITLEID, rowY + 15, Color(200, 200, 200), 2);

                
                scene->DrawText(sizeStr, xSIZE, rowY + 15, colText, 2);
                scene->DrawText(statusText, xSTATUS, rowY + 15, statColor, 2);
                scene->DrawText(progStr, xPROG, rowY + 15, colText, 2);
            }
            
            // 5. FOREGROUND PRIORITY (Background Image) (As an overlay)
            if (bgPic0 && bgPic0->data) {
                bgPic0->Draw(scene, 0, 0, 1920, 1080);
            }

            // 6. MENU OVERLAY (Topmost)
            if (showMenu) {
                // Dim background
                scene->DrawRectangle(0, 0, 1920, 1080, Color(0, 0, 0, 150));
                
                int menuW = 600;
                int menuH = 300;
                int menuX = (1920 - menuW) / 2;
                int menuY = (1080 - menuH) / 2;
                
                // Menu Card
                scene->DrawRectangle(menuX, menuY, menuW, menuH, Color(40, 40, 50, 255));
                scene->DrawRectangle(menuX, menuY, menuW, 50, Color(30, 30, 35, 255)); // Header
                scene->DrawText("Manage Task", menuX + 20, menuY + 10, colAccent, 3);
                
                // Option 1: Remove / Cancel
                Color opt1Col = (menuOption == 0) ? colActivePulse : colText;
                const char* opt1Text = "Delete / Cancel Task";
                scene->DrawText(opt1Text, menuX + 50, menuY + 100, opt1Col, 3);
                if (menuOption == 0) scene->DrawRectangle(menuX + 40, menuY + 100, 10, 30, colAccent);
                
                // Option 2: Back
                Color opt2Col = (menuOption == 1) ? colActivePulse : colText;
                const char* opt2Text = "Back (Circle)";
                scene->DrawText(opt2Text, menuX + 50, menuY + 180, opt2Col, 3);
                 if (menuOption == 1) scene->DrawRectangle(menuX + 40, menuY + 180, 10, 30, colAccent);
                 
                // Helper text
                scene->DrawText("Press X to Select", menuX + 20, menuY + 260, Color(150, 150, 150), 2);
            }

            // 7. FULLSCREEN ICON OVERLAY (Extreme Topmost)
            if (showFullscreenIcon && selectedRow < taskCount) {
                 scene->DrawRectangle(0, 0, 1920, 1080, Color(0, 0, 0, 220)); // Dim darker
                 
                 int tid = tasks[selectedRow].taskId;
                 PNG* icon = NULL;
                 if (iconCache.find(tid) != iconCache.end()) icon = iconCache[tid];
                 
                 if (icon) {
                     // Center it
                     // Assuming 512x512 max or similar
                     int iW = 512;
                     int iH = 512;
                     int iX = (1920 - iW) / 2;
                     int iY = (1080 - iH) / 2;
                     icon->Draw(scene, iX, iY, iW, iH);
                     
                     // Draw Title below
                     char titleBuf[128];
                     strncpy(titleBuf, tasks[selectedRow].title, 64);
                     scene->DrawText(titleBuf, iX, iY + iH + 20, colText, 3);
                 } else {
                     scene->DrawText("No Icon Available", (1920/2)-200, 1080/2, colError, 3);
                 }
                 
                 scene->DrawText("Press any button to close", (1920/2)-300, 1000, Color(150, 150, 150), 2);
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
    
    // MEMORY FIX: Full icon cache cleanup
    Log("Cleaning up Icon Cache (%d items)...", (int)iconCache.size());
    for (auto const& [id, icon] : iconCache) {
        if (icon) delete icon;
    }
    iconCache.clear();
    
    return 0;
}
