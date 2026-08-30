#include "../include/Installer.h"
#include "../include/HttpHelper.h"
#include "../include/libjbc.h"
#include "../include/PkgUtils.h"
#include <orbis/UserService.h>
#include <orbis/Sysmodule.h>
#include <orbis/_types/sysmodule.h>
#include <orbis/libkernel.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>

#include "../include/Common.h"

#include <ctype.h> // For toupper

// Custom strcasestr implementation since it's missing in some environments
static const char* strcasestr_custom(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;
    
    for (; *haystack; ++haystack) {
        if (toupper((unsigned char)*haystack) == toupper((unsigned char)*needle)) {
            const char* h = haystack + 1;
            const char* n = needle + 1;
            while (*h && *n && toupper((unsigned char)*h) == toupper((unsigned char)*n)) {
                h++;
                n++;
            }
            if (!*n) return haystack;
        }
    }
    return NULL;
}

// Extract filename from URL as fallback title
static void ExtractFilename(const char* url, char* outName, int maxLen) {
    memset(outName, 0, maxLen);
    const char* lastSlash = strrchr(url, '/');
    if (lastSlash && *(lastSlash + 1)) {
        strncpy(outName, lastSlash + 1, maxLen - 1);
        char* ext = strstr(outName, ".pkg");
        if (ext) *ext = '\0';
    } else {
        strncpy(outName, "Package", maxLen - 1);
    }
}

// Track files to delete after installation
#define MAX_PENDING_DELETE 8
static char s_pendingDelete[MAX_PENDING_DELETE][256];
static int s_pendingDeleteCount = 0;

// Mutex for delete queue
static pthread_mutex_t s_deleteMutex = PTHREAD_MUTEX_INITIALIZER;

// STATIC GLOBALS FOR OVERFLOW AND UI STATE (Moved here from Installer.h to fix startup crash)
static float g_taskProgress[MAX_TASKS];
static char g_taskStatus[MAX_TASKS][32];
static uint64_t g_taskLastUpdate[MAX_TASKS];
static uint64_t g_taskTransferredOffset[MAX_TASKS];
static uint64_t g_taskLastTransferred[MAX_TASKS];

static void AddPendingDelete(const char* path) {
    pthread_mutex_lock(&s_deleteMutex);
    if (s_pendingDeleteCount < MAX_PENDING_DELETE) {
        strncpy(s_pendingDelete[s_pendingDeleteCount], path, 255);
        s_pendingDeleteCount++;
        Log("Added pending delete: %s", path);
    }
    pthread_mutex_unlock(&s_deleteMutex);
}

// sceAppInstUtil for local installation
extern "C" {
    int sceAppInstUtilInitialize();
    int sceAppInstUtilTerminate();
    int sceAppInstUtilAppInstallPkg(const char* pkgPath, void* reserved);
    int sceAppInstUtilAppUnInstall(const char* titleId);
    int sceAppInstUtilAppUnInstallPat(const char* titleId);
    int sceAppInstUtilAppUnInstallAddcont(const char* titleId, const char* contentId);
    int sceAppInstUtilAppUnInstallTheme(const char* contentId);
    int sceAppInstUtilAppExists(const char* titleId, int* exists);
    int sceAppInstUtilAppGetSize(const char* titleId, uint64_t* size);
}

static bool s_appInstUtilInitialized = false;

static bool InitAppInstUtil() {
    if (s_appInstUtilInitialized) return true;
    
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL);
    int res = sceAppInstUtilInitialize();
    Log("sceAppInstUtilInitialize = 0x%08X", res);
    
    if (res >= 0) {
        s_appInstUtilInitialized = true;
        return true;
    }
    return false;
}

Installer::Installer() {
    initialized = false;
    userId = -1;
    activeTaskCount = 0;
    lastError = 0;
    moduleLoadResult = 0;
    jbcResult = 0;
    
    Log("=== STORM PS4 PKG INSTALLER v1.44 ===");
    Log("=== Installer Constructor Start ===");
    
    // Initialize task arrays (Static Globals)
    memset(g_taskProgress, 0, sizeof(g_taskProgress));
    memset(g_taskStatus, 0, sizeof(g_taskStatus));
    memset(g_taskLastUpdate, 0, sizeof(g_taskLastUpdate));
    memset(g_taskTransferredOffset, 0, sizeof(g_taskTransferredOffset));
    memset(g_taskLastTransferred, 0, sizeof(g_taskLastTransferred));

    // Initialize class members
    Log("Init: activeTaskIds");
    memset(activeTaskIds, 0, sizeof(activeTaskIds));
    memset(taskTitles, 0, sizeof(taskTitles));
    memset(taskTitleIds, 0, sizeof(taskTitleIds));
    memset(taskIcons, 0, sizeof(taskIcons));
    memset(taskCategories, 0, sizeof(taskCategories));
    
    Log("Init: taskUrls");
    memset(taskUrls, 0, sizeof(taskUrls)); // Initialize pointers to NULL
    
    Log("Init: taskTotalSizes");
    memset(taskTotalSizes, 0, sizeof(taskTotalSizes));
    
    Log("Init: mutex");
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE); // Allow recursive locking
    pthread_mutex_init(&mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    
    Log("=== SPPI Installer v1.43 Created ===");
}

Installer::~Installer() {
    pthread_mutex_destroy(&mutex);
    Term();
    HttpHelper_Term();
    // Logging cleanup handled globally
    // Free URL strings
    for(int i=0; i<MAX_TASKS; i++) {
        if(taskUrls[i]) {
            free(taskUrls[i]);
            taskUrls[i] = NULL;
        }
    }
    Log("=== SPPI Installer Destroyed ===");
}

bool Installer::Init() {
    int rc;
    Log("Init() starting...");
    
    // JAILBREAK FIRST
    jbcResult = jbc_init();
    Log("jbc_init() = %d", jbcResult);
    
    if (jbcResult >= 0 && jbc_get_cred && jbc_jailbreak_cred && jbc_set_cred) {
        struct jbc_cred cred;
        memset(&cred, 0, sizeof(cred));
        jbc_get_cred(&cred);
        jbc_jailbreak_cred(&cred);
        jbc_set_cred(&cred);
        Log("Jailbreak applied");
    }
    
    // Load modules
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    sceSysmoduleLoadModuleInternal((OrbisSysModuleInternal)0x0096); // AppInstUtil
    rc = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_BGFT);
    moduleLoadResult = rc;
    Log("BGFT module = 0x%08X", rc);
    
    // Explicitly init AppInstUtil here to avoid crashes later
    if (InitAppInstUtil()) {
        Log("AppInstUtil Initialized");
    } else {
        Log("AppInstUtil Init Failed");
    }
    
    // Init UserService
    rc = sceUserServiceInitialize(NULL);
    Log("UserService = 0x%08X", rc);
    
    userId = 1; // Default to first user
    rc = sceUserServiceGetInitialUser(&userId);
    Log("GetUser = 0x%08X, userId=%d", rc, userId);
    if (rc < 0 || userId < 0) {
        userId = 1; // Fallback to user 1
        Log("Using fallback userId = 1");
    }
    
    // Init BGFT
    OrbisBgftInitParams bgftInit;
    memset(&bgftInit, 0, sizeof(bgftInit));
    static char bgftHeap[8 * 1024 * 1024];
    bgftInit.heap = bgftHeap;
    bgftInit.heapSize = sizeof(bgftHeap);
    
    rc = sceBgftServiceIntInit(&bgftInit);
    Log("BGFT IntInit = 0x%08X", rc);
    
    if (rc < 0) {
        lastError = rc;
        return false;
    }
    
    initialized = true;
    Log("Init() complete");
    return true;
}

void Installer::Term() {
    if (initialized) {
        Log("Term()");
        sceBgftServiceIntTerm();
        sceUserServiceTerminate();
        initialized = false;
    }
}

int Installer::Install(const char* url, const char* defaultTitle, uint64_t senderSize) {
    if (!url) { Log("Install: URL is NULL"); return -1; }
    Log("Install() url=%s, senderSize=%llu", url, senderSize);
    
    if (strlen(url) > 2040) {
        Log("Install: URL too long");
        return -1;
    }
    
    if (!initialized) {
        Log("NOT initialized!");
        return -1;
    }
    
    // Check limit under lock
    pthread_mutex_lock(&mutex);
    if (activeTaskCount >= MAX_TASKS) {
        pthread_mutex_unlock(&mutex);
        Log("Too many tasks");
        return -2;
    }
    
    // Check for duplicate URL
    for (int i = 0; i < activeTaskCount; i++) {
        if (taskUrls[i] && strcmp(taskUrls[i], url) == 0) {
            int existingId = activeTaskIds[i];
            pthread_mutex_unlock(&mutex);
            Log("Install: Task %d already active for this URL", existingId);
            return existingId;
        }
    }

    // EARLY RESERVATION: Assign a temporary "Pending" slot to prevent URL races
    // while we perform slow I/O (probing size/extracting icon).
    // Use a unique temporary taskId starting from 80000
    static int s_pendingCounter = 0;
    int reservedTaskId = 80000 + (++s_pendingCounter);
    
    // Initial registration with minimal info (will be updated later)
    int internalIndex = activeTaskCount;
    activeTaskIds[internalIndex] = reservedTaskId;
    strncpy(taskTitles[internalIndex], (defaultTitle ? defaultTitle : "Preparing..."), 63);
    strncpy(taskTitleIds[internalIndex], "Pending", 15);
    strncpy(taskIcons[internalIndex], "", 255);
    strncpy(taskCategories[internalIndex], "gd", 7);
    taskTotalSizes[internalIndex] = senderSize;
    taskUrls[internalIndex] = strdup(url);
    
    // Init Cache for UI immediately
    g_taskProgress[internalIndex] = 0.0f;
    strncpy(g_taskStatus[internalIndex], "Preparing...", 31);
    g_taskLastUpdate[internalIndex] = time(NULL);
    
    activeTaskCount++;
    pthread_mutex_unlock(&mutex);
    
    Log("Install: Early Reserved Slot %d for URL with TaskId %d", internalIndex, reservedTaskId);
    
    // === I/O OPERATIONS (NO MUTEX HELD) ===
    // This allows the UI thread (GetTasks) to run freely while we probe/download.
    
    // === PROBE SIZE FIRST ===
    // We need to know file size BEFORE PkgUtils because PkgUtils might crash on small files.
    // If senderSize is provided, trust it primarily (user requested "Sender" logic).
    int64_t probeSize = (int64_t)senderSize;
    char probeTitle[64];
    char probeCid[48];
    memset(probeTitle, 0, sizeof(probeTitle));
    memset(probeCid, 0, sizeof(probeCid));
    
    Log("Probing file size via HTTP HEAD...");
    int64_t headSize = 0;
    if (HttpHelper_ExtractPkgInfo(url, probeCid, sizeof(probeCid), probeTitle, sizeof(probeTitle), &headSize)) {
         Log("Probe Size (HEAD): %lld", (long long)headSize);
         if (probeSize == 0) probeSize = headSize; // Only use HEAD if sender didn't provide size
    } else {
         Log("Probe failed to get size.");
    }
    
    
    // Small file info: Threshold lowered to 1KB to force BGFT for DLCs.
    // Official registration (BGFT) is required for icons to show in PS4 menu.
    bool smallFile = (probeSize > 0 && probeSize < 1024);
    
    if (smallFile) {
        Log("Small file detected (%lld bytes). Will use Local Install.", (long long)probeSize);
    }

    // === STEP 1: PkgUtils Info Extraction ===
    char realContentId[48];
    char realTitle[64];
    char iconPath[256];
    int64_t fileSize = probeSize; // Use probed size
    
    // CRITICAL: Zero out buffers completely to prevent garbage
    memset(realContentId, 0, sizeof(realContentId));
    memset(realTitle, 0, sizeof(realTitle));
    memset(iconPath, 0, sizeof(iconPath));
    // NOTE: We don't copy probeCid blindly to realContentId anymore. 
    // PkgUtils will extract the authoritative one from header.
    if (strlen(probeTitle) > 0) strncpy(realTitle, probeTitle, 63);
    
    // Use an atomic counter or mutex protected counter for unique IDs
    // Use an atomic counter (or brief lock)
    static int s_iconCounter = 0; 
    pthread_mutex_lock(&mutex);
    int myIconId = 90000 + (++s_iconCounter);
    pthread_mutex_unlock(&mutex);
    
    
    char titleId[16];
    char category[8];
    memset(titleId, 0, sizeof(titleId));
    memset(category, 0, sizeof(category));
    
    // Only parse PkgUtils if size is >= 50KB (most valid SFOs fit this, tiny files might be junk)
    // Was 10MB, which skipped DLCs/Unlockers -> Title ID shown instead of Name
    bool infoOk = false;
    if (fileSize == 0 || fileSize >= 1 * 1024) {
         infoOk = PkgUtils::GetTitleAndIcon(url, realTitle, 63, iconPath, 255, titleId, 15, category, 4, realContentId, 47, myIconId);
    } else {
         Log("Skipping PkgUtils for tiny file (%lld bytes).", (long long)fileSize);
    }
    
    Log("PkgUtils Result: %s, Title: '%s', Icon: '%s', ID: '%s', Cat: '%s'", infoOk ? "OK" : "SKIP", realTitle, iconPath, titleId, category);

    // FIX: If we skipped PkgUtils (small file) or it text is empty, ensure we have a fallback title immediately
    // "realTitle" comes from SFO. "probeTitle" comes from HTTP HEAD. "defaultTitle" comes from URL filename.
    
    if (strlen(realTitle) == 0) {
        if (strlen(probeTitle) > 0) strncpy(realTitle, probeTitle, 63);
        else if (defaultTitle && strlen(defaultTitle) > 0) strncpy(realTitle, defaultTitle, 63);
        else strncpy(realTitle, "Package", 63);
    }
    
    // FIX: If category is empty (small file skip), try to guess or set default
    if (strlen(category) == 0) {
        if (smallFile) strcpy(category, "AC"); // Guess Add-On Content for small files
        else strcpy(category, "GD"); // Guess Game Digital
    }

    // HEURISTIC: Force "THEME" category if filename/url contains "THEME"
    // This allows the UI/Sender to recognize it even if SFO parsing failed or it was classified as DLC
    if (url && strcasestr_custom(url, "THEME")) {
        Log("Heuristic: URL contains 'THEME'. Forcing category to THEME.");
        strcpy(category, "THEME");
    } else if (realTitle[0] && strcasestr_custom(realTitle, "THEME")) {
         Log("Heuristic: Title contains 'THEME'. Forcing category to THEME.");
         strcpy(category, "THEME");
    }

    // CHECK IF INSTALLED
    // Allow if it's a Patch (gp), DLC (ac), Theme (th), or Delta (dp)
    bool isPatch = (strcasecmp(category, "gp") == 0);
    bool isDLC = (strcasecmp(category, "ac") == 0);
    bool isTheme = (strcasecmp(category, "th") == 0 || strcasecmp(category, "THEME") == 0);
    bool isDelta = (strcasecmp(category, "dp") == 0);
    
    // We allow re-install (overwrite) for everything EXCEPT the main game (gd)
    // "sd" is also a game type? Safe to allow all except "gd" maybe?
    // Let's be specific to what we know is safe.
    bool allowReinstall = (isPatch || isDLC || isTheme || isDelta);

    // Sanity check: Ensure TitleID is valid (length > 8) before checking installation
    // This prevents "Already Installed" false positives for empty/short TitleIDs from failed parsing
    bool isValidTitleId = (strlen(titleId) > 8);
    
    // MOVED UP: Generate Content ID early for cleanup logic
    char contentId[48];
    if (strlen(realContentId) > 10) {
        strncpy(contentId, realContentId, 47);
        contentId[47] = 0;
    } else {
        uint64_t ts = sceKernelGetProcessTime();
        snprintf(contentId, sizeof(contentId), "IV0000-SPPI01313_00-%08X%08d", 
                 (unsigned int)(ts & 0xFFFFFFFF), s_iconCounter);
    }
    
    // FIX: Only block if we successfully parsed the PKG (infoOk) AND it's NOT an allowed type.
    if (!smallFile && infoOk && isValidTitleId && !allowReinstall && AppExists(titleId)) {
        Log("App %s is already installed and not an allowed type (%s)!", titleId, category);
        return -10; // ALREADY_INSTALLED error code
    } else if (isValidTitleId && allowReinstall && AppExists(titleId)) {
        Log("App %s exists but type (%s) allows reinstall. PRE-CLEANUP STARTED.", titleId, category);
        
        // AUTO-CLEANUP: Attempt to uninstall "broken" data before re-installing
        // This fixes the "Already Installed" / "White Square" issue.
        if (isPatch || isDLC || isTheme || isDelta) {
             const char* idToUninstall = (strlen(realContentId) > 10) ? realContentId : contentId;
             Log("Attempting auto-uninstall for cleanup (%s, CID: %s)...", category, idToUninstall);
             int unRes = -1;

             if (isPatch) {
                 unRes = sceAppInstUtilAppUnInstallPat(titleId);
             } else if (isTheme) {
                 unRes = sceAppInstUtilAppUnInstallTheme(idToUninstall);
             } else {
                 unRes = sceAppInstUtilAppUnInstallAddcont(titleId, idToUninstall);
             }
             
             Log("Cleanup Result: 0x%08X", unRes);
        }
    } else if (!isValidTitleId && strlen(titleId) > 0) {
        Log("TitleID '%s' seems invalid, skipping installed check to avoid false positive.", titleId);
    }
    
    // Also try HttpHelper to get file size and ContentID (redundant but safe)
    // char helperTitle[64]; 
    // ... we already probed ...
                                                  
    // Check for printable characters and sanitize
    // Check for printable characters and sanitize
    auto SanitizeTitle = [](char* str) {
        if (!str) return;
        int len = strlen(str);
        // FIX: Improve sanitization to handle UTF-8 partials or garbage
        for (int i=0; i<len; i++) {
            unsigned char c = (unsigned char)str[i];
            // Allow basic ASCII and valid UTF-8 start bytes (0xC2-0xF4)
            // Replace control chars < 32 with space
            if (c < 32 && c != 0) {
                str[i] = ' '; 
            }
        }
        // Trim trailing spaces
        while(len > 0 && str[len-1] == ' ') {
            str[len-1] = 0;
            len--;
        }
    };
    
    // Sanitize realTitle before use
    SanitizeTitle(realTitle);
    if (strlen(probeTitle) > 0) SanitizeTitle(probeTitle);
    
    // Logic: If PkgUtils gave us a title, it's the SFO title (Gold Truth).
    // If not, use Probe (HTTP HEAD) title.
    // If not, use Default (Filename).
    
    // (contentId definition moved up)
    
    const char* finalTitle = "Package";
    if (strlen(realTitle) > 1) finalTitle = realTitle;
    else if (strlen(probeTitle) > 1) finalTitle = probeTitle;
    else if (defaultTitle && strlen(defaultTitle) > 1) finalTitle = defaultTitle;
    
    Log("Final Logic: Real='%s', Probe='%s', Def='%s' -> Chosen='%s'", realTitle, probeTitle, defaultTitle, finalTitle);
    
    // NOTE: Do NOT call HttpHelper_Term() here - Local Install path needs HTTP session!
    
    // === Logic for Small Files (< 10MB) or Unknown Size ===
    if (smallFile) Log("Small file detected: %lld bytes. Using Direct Local Install.", (long long)fileSize);
    else Log("Large or Unknown size (%lld). Using BGFT.", (long long)fileSize);
    
    int res = -1;
    int taskId = -1;
    bool bgftUsed = false;
    
    // Skip BGFT for small files to avoid errors
    // ALSO check if it's a valid PKG before trying BGFT? 
    // For now, trust size.
    if (!smallFile) {
        // === STEP 4: Register BGFT task ===
        OrbisBgftDownloadParam param;
        memset(&param, 0, sizeof(param));
        
        param.userId = userId;
        param.entitlementType = 5;
        param.id = contentId;
        param.contentUrl = url;
        param.contentName = finalTitle;
        param.iconPath = iconPath;
        param.option = ORBIS_BGFT_TASK_OPT_DISABLE_CDN_QUERY_PARAM;
        param.packageType = "PS4GD";
        // SMART SIZE: Use real size for < 4GB to help BGFT validator.
        // Use 0 (auto-detect) ONLY for > 4GB or unknown to bypass 32-bit limits.
        if (fileSize > 0 && fileSize < 0xFFFFFFFFULL) {
            Log("BGFT: Using real size (%lld) for reliable registration.", (long long)fileSize);
            param.packageSize = (uint32_t)fileSize;
        } else {
            Log("BGFT: Large or unknown size (%lld). Forcing auto-detect (0).", (long long)fileSize);
            param.packageSize = 0; 
        }
        res = sceBgftServiceIntDownloadRegisterTask(&param, &taskId);
        Log("RegisterTask = 0x%08X, taskId=%d", res, taskId);

        // EXTRA POLISH: Some BGFT versions reject packageSize=0 if packageType is "PS4GD"
        // If we get "Invalid Argument" (0x80990004), try again with NULL packageType
        if (res == 0x80990004) {
            Log("RegisterTask rejected size=0 for PS4GD. Retrying with NULL type...");
            param.packageType = NULL;
            res = sceBgftServiceIntDownloadRegisterTask(&param, &taskId);
            Log("Retry RegisterTask = 0x%08X, taskId=%d", res, taskId);
        }

        bgftUsed = true;
        
        if (res < 0) {
            Log("Fallback 1: trying DebugRegisterPkg...");
            res = sceBgftServiceIntDebugDownloadRegisterPkg(&param, &taskId);
            Log("DebugRegisterPkg = 0x%08X, taskId=%d", res, taskId);
        }
    }
    
    // === UPDATE RESERVED SLOT WITH REAL INFO ===
    pthread_mutex_lock(&mutex);
    // Find the slot we just reserved - it has our reservedTaskId
    int myIndex = -1;
    for (int i = 0; i < activeTaskCount; i++) {
        if (activeTaskIds[i] == reservedTaskId) {
            myIndex = i;
            break;
        }
    }
    
    if (myIndex != -1) {
        strncpy(taskTitles[myIndex], finalTitle, 63);
        strncpy(taskTitleIds[myIndex], titleId, 15);
        strncpy(taskIcons[myIndex], iconPath, 255);
        strncpy(taskCategories[myIndex], category, 7);
        taskTotalSizes[myIndex] = fileSize;
        // URL is already set
    }
    pthread_mutex_unlock(&mutex);


    // Success from BGFT?
    if (res >= 0 && bgftUsed) {
        // Start the task immediately
        int startRes = sceBgftServiceDownloadStartTask(taskId);
        Log("StartTask result = 0x%08X", startRes);
        
        if (startRes >= 0) {
            // Success: Update the TaskID to the REAL one from BGFT
            pthread_mutex_lock(&mutex);
            if (myIndex != -1) {
                activeTaskIds[myIndex] = taskId;
                // Update cache status to Downloading
                strncpy(g_taskStatus[myIndex], "Downloading", 31);
                g_taskLastUpdate[myIndex] = time(NULL);
            }
            pthread_mutex_unlock(&mutex);
            return taskId;
        } else {
            Log("StartTask FAILED. Cleaning up registration.");
            sceBgftServiceIntDownloadUnregisterTask(taskId);
            res = startRes; // Fall through to error/fallback
        }
    }

    // Fallback Local Download
    if (smallFile || res < 0) {
        Log("Trying Local Download & Install...");
        
        // Use /user/app/sppi_temp/ to ensure compatibility and write access
        // /data/ sometimes has issues with specific permissions or AppInstUtil visibility
        const char* tempDir = "/user/app/sppi_temp";
        mkdir(tempDir, 0777);
        
        static char localPath[256];
        snprintf(localPath, sizeof(localPath), "%s/sppi_pkg_%u_%d.pkg", tempDir, s_iconCounter, rand() % 1000);
        
        Log("Downloading to %s...", localPath);
        if (HttpHelper_DownloadFile(url, localPath)) {
            Log("Downloaded to %s. Verifying...", localPath);
            
            // Verify file was written correctly
            int fd = open(localPath, O_RDONLY);
            if (fd < 0) {
                Log("ERROR: Cannot reopen downloaded file!");
                return -1;
            }
            
            // Check file size
            off_t actualSize = lseek(fd, 0, SEEK_END);
            lseek(fd, 0, SEEK_SET);
            Log("File size on disk: %lld", (long long)actualSize);
            
            // Verify PKG header (first 4 bytes should be 7F 43 4E 54 = .CNT)
            char header[4] = {0};
            if (read(fd, header, 4) == 4) {
                if (header[0] != 0x7F || header[1] != 'C' || header[2] != 'N' || header[3] != 'T') {
                    Log("ERROR: Invalid PKG header! Got: %02X %02X %02X %02X", 
                        (unsigned char)header[0], (unsigned char)header[1], 
                        (unsigned char)header[2], (unsigned char)header[3]);
                    close(fd);
                    unlink(localPath);
                    return -1;
                }
                Log("PKG header valid (.CNT)");
            } else {
                Log("ERROR: Cannot read PKG header!");
                close(fd);
                unlink(localPath);
                return -1;
            }
            close(fd);
            
            // Flush filesystem to ensure file is fully written
            sync();
            
            Log("Installing PKG via AppInstUtil...");
            // AppInstUtil should be initialized in Init(), but double check safely
            if (!s_appInstUtilInitialized) {
                if (InitAppInstUtil()) {
                    Log("AppInstUtil late-init success");
                } else {
                    Log("AppInstUtil late-init failed");
                    unlink(localPath);
                    return -1;
                }
            }
            
            res = sceAppInstUtilAppInstallPkg(localPath, NULL);
            Log("AppInstallPkg result: 0x%08X", res);
            
            if (res >= 0) {
                // Success local install: Update reserved slot to fakeId
                int fakeId = 10000 + s_iconCounter;
                pthread_mutex_lock(&mutex);
                if (myIndex != -1) {
                    activeTaskIds[myIndex] = fakeId;
                    strncpy(g_taskStatus[myIndex], "Installed (Local)", 31);
                    g_taskProgress[myIndex] = 1.0f;
                    g_taskLastUpdate[myIndex] = time(NULL);
                }
                pthread_mutex_unlock(&mutex);
                return fakeId;
            } else {
                Log("AppInstallPkg FAILED. Error: 0x%08X", res);
            }
            
            unlink(localPath); // Delete if install failed
        } else {
            Log("Local download failed. Could not open file or connection dropped.");
        }
    }
    
    // Fallback 4: Local download and install for small files (<5MB)
    // Fallback 4: Local download and install for small files (<5MB)
    // DISABLED: Users report crash on 1MB file transfer. Debugging required.
    // Logic moved to Heap but crash persists or logic is flawed.
    /*
    if (res < 0 && fileSize > 0 && fileSize < 5*1024*1024) {
        Log("Fallback 4: All BGFT methods failed (0x%08X). Trying local download fallback for small file...", res);
         ... (disabled logic) ...
        } else {
            Log("Failed to open local file %s for writing.", localPath);
        }
    }
    */
    
    if (res < 0) {
        lastError = res;
        Log("ALL INSTALL METHODS FAILED: 0x%08X. Removing reserved slot.", res);
        
        // CLEANUP RESERVED SLOT
        pthread_mutex_lock(&mutex);
        for (int i = 0; i < activeTaskCount; i++) {
            if (activeTaskIds[i] == reservedTaskId) {
                if (taskUrls[i]) free(taskUrls[i]);
                // Shift remaining
                for (int j = i; j < activeTaskCount - 1; j++) {
                    activeTaskIds[j] = activeTaskIds[j + 1];
                    strncpy(taskTitles[j], taskTitles[j + 1], 63);
                    strncpy(taskTitleIds[j], taskTitleIds[j + 1], 15);
                    strncpy(taskIcons[j], taskIcons[j + 1], 255);
                    taskTotalSizes[j] = taskTotalSizes[j + 1];
                    taskUrls[j] = taskUrls[j + 1];
                    strncpy(taskCategories[j], taskCategories[j + 1], 7);
                    
                    g_taskProgress[j] = g_taskProgress[j+1];
                    strncpy(g_taskStatus[j], g_taskStatus[j+1], 31);
                    g_taskLastUpdate[j] = g_taskLastUpdate[j+1];
                }
                activeTaskCount--;
                break;
            }
        }
        pthread_mutex_unlock(&mutex);
        
        return res;
    }
    
    return res;
}
int Installer::RegisterTask(int taskId, const char* title, const char* titleId, const char* iconPath, uint64_t totalSize, const char* category, const char* url) {
    if (!initialized) return -1;

    pthread_mutex_lock(&mutex);
    if (activeTaskCount >= MAX_TASKS) {
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    
    activeTaskIds[activeTaskCount] = taskId;
    strncpy(taskTitles[activeTaskCount], (title ? title : "Unknown"), 63);
    taskTitles[activeTaskCount][63] = '\0'; // Ensure null termination
    strncpy(taskTitleIds[activeTaskCount], (titleId ? titleId : "Unknown"), 15);
    taskTitleIds[activeTaskCount][15] = '\0'; // Ensure null termination
    strncpy(taskIcons[activeTaskCount], (iconPath ? iconPath : ""), 255);
    taskIcons[activeTaskCount][255] = '\0'; // Ensure null termination
    strncpy(taskCategories[activeTaskCount], (category ? category : "gd"), 7);
    taskCategories[activeTaskCount][7] = '\0';
    taskTotalSizes[activeTaskCount] = totalSize;  
    
    // Init Cache (Static Globals)
    g_taskProgress[activeTaskCount] = 0.0f;
    strncpy(g_taskStatus[activeTaskCount], "Pending", 31);
    g_taskLastUpdate[activeTaskCount] = time(NULL); // FIX: Ensure UI doesn't see this as stalled
    
    // Init Overflow tracking (Static Globals)
    g_taskTransferredOffset[activeTaskCount] = 0;
    g_taskLastTransferred[activeTaskCount] = 0;
    
    // NEW: Store URL for duplicate detection (Dynamic Allocation)
    if (url) taskUrls[activeTaskCount] = strdup(url);
    else taskUrls[activeTaskCount] = NULL;
    
    activeTaskCount++;
    pthread_mutex_unlock(&mutex);
    
    Log("Registered Task: %d, Title: %s, ID: %s, Size: %llu, Cat: %s", taskId, title, titleId ? titleId : "N/A", totalSize, category ? category : "UNK");
    return taskId;
}

// NEW: Find existing task by URL to prevent duplicate installations
int Installer::FindTaskByUrl(const char* url) {
    if (!url || !initialized) return -1;
    
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < activeTaskCount; i++) {
        if (taskUrls[i] && strcmp(taskUrls[i], url) == 0) {
            int taskId = activeTaskIds[i];
            pthread_mutex_unlock(&mutex);
            Log("FindTaskByUrl: Found existing task %d for URL", taskId);
            return taskId;
        }
    }
    pthread_mutex_unlock(&mutex);
    return -1;
}


int Installer::GetTasks(InstallTask* outTasks, int maxTasks) {
    if (!initialized || !outTasks) return 0;
    
    pthread_mutex_lock(&mutex);
    int count = 0;
    
    // Copy basic info while holding mutex
    for (int i = 0; i < activeTaskCount && i < maxTasks; i++) {
        memset(&outTasks[i], 0, sizeof(InstallTask));
        outTasks[i].taskId = activeTaskIds[i];
        strncpy(outTasks[i].title, taskTitles[i], 127); // Fix: use full buffer if possible, but taskTitles is 64
        strncpy(outTasks[i].titleId, taskTitleIds[i], 15);
        strncpy(outTasks[i].iconPath, taskIcons[i], 255);
        strncpy(outTasks[i].category, taskCategories[i], 7);
        outTasks[i].totalSize = taskTotalSizes[i];
        count++;
    }
    pthread_mutex_unlock(&mutex); 
    
    // Update dynamic progress/status outside the mutex to avoid long locks during BGFT calls
    for (int i = 0; i < count; i++) {
        int tid = outTasks[i].taskId;

        // Local task check
        if (tid >= 10000 && tid < 80000) {
            strcpy(outTasks[i].status, "Installed (Local)");
            outTasks[i].progress = 1.0f;
            continue;
        }
        
        // Fetch real progress from BGFT or static cache
        if (!GetTaskProgress(tid, &outTasks[i])) {
             // Fallback to static cache (using internal index i)
             if (i < MAX_TASKS && g_taskLastUpdate[i] > 0) {
                 outTasks[i].progress = g_taskProgress[i];
                 strncpy(outTasks[i].status, g_taskStatus[i], 31);
             } else {
                 strcpy(outTasks[i].status, "Unknown / Stalled");
             }
        }
    }
    
    return count;
}


int Installer::GetInstalledApps(AppInfo* outApps, int maxApps) {
    if (!outApps || maxApps <= 0) return 0;
    
    int count = 0;
    DIR* dir = opendir("/user/appmeta");
    if (!dir) {
        Log("Failed to open /user/appmeta");
        return 0;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && count < maxApps) {
        if (entry->d_name[0] == '.') continue;
        
        // Log what we found to debug "Only SPPI" issue
        // Log("Found appmeta entry: %s", entry->d_name);
        
        // Check if CUSA... (case insensitive check might be safer but headers are standard)
        if (strstr(entry->d_name, "CUSA") != NULL) {
            char sfoPath[256];
            snprintf(sfoPath, sizeof(sfoPath), "/user/appmeta/%s/param.sfo", entry->d_name);
            
            int fd = open(sfoPath, O_RDONLY);
            if (fd >= 0) {
                off_t size = lseek(fd, 0, SEEK_END);
                lseek(fd, 0, SEEK_SET);
                
                if (size > 0 && size < 64*1024) { // Limit SFO size sanity check
                    uint8_t* buf = (uint8_t*)malloc(size);
                    if (buf) {
                        if (read(fd, buf, size) == size) {
                            char title[128] = {0};
                            char tid[16] = {0};
                            // We don't need category here
                            if (PkgUtils::ParseSfo(buf, size, title, 127, tid, 15, NULL, 0)) {
                                strncpy(outApps[count].titleId, tid, 15);
                                strncpy(outApps[count].title, title, 127);
                                outApps[count].size = GetAppSize(tid);
                                count++;
                                Log("Added App: %s (%s)", title, tid);
                            }
                        }
                        free(buf);
                    }
                }
                close(fd);
            }
        }
    }
    closedir(dir);
    Log("GetInstalledApps found %d apps", count);
    return count;
}

bool Installer::IsAppInstalled(const char* titleId) {
    return AppExists(titleId);
}

// === NEW API FUNCTIONS (RPI compatible) ===

int Installer::PauseTask(int taskId) {
    if (!initialized) return -1;
    Log("PauseTask(%d)", taskId);
    int res = sceBgftServiceDownloadPauseTask(taskId);
    Log("PauseTask result = 0x%08X", res);
    if (res < 0) lastError = res;
    return res;
}

int Installer::ResumeTask(int taskId) {
    if (!initialized) return -1;
    Log("ResumeTask(%d)", taskId);
    int res = sceBgftServiceDownloadResumeTask(taskId);
    Log("ResumeTask result = 0x%08X", res);
    if (res < 0) lastError = res;
    return res;
}

int Installer::StopTask(int taskId) {
    if (!initialized) return -1;
    Log("StopTask(%d)", taskId);
    int res = sceBgftServiceDownloadStopTask(taskId);
    Log("StopTask result = 0x%08X", res);
    if (res < 0) lastError = res;
    return res;
}

int Installer::UnregisterTask(int taskId) {
    if (!initialized) return -1;
    Log("UnregisterTask(%d)", taskId);
    int res = sceBgftServiceIntDownloadUnregisterTask(taskId);
    Log("UnregisterTask result = 0x%08X", res);
    
    // Remove from active tasks list
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < activeTaskCount; i++) {
        if (activeTaskIds[i] == taskId) {
            // Free the URL string before removing
            if (taskUrls[i]) {
                free(taskUrls[i]);
                taskUrls[i] = NULL;
            }
            
            // Shift remaining tasks
            for (int j = i; j < activeTaskCount - 1; j++) {
                activeTaskIds[j] = activeTaskIds[j + 1];
                strncpy(taskTitles[j], taskTitles[j + 1], 63);
                strncpy(taskTitleIds[j], taskTitleIds[j + 1], 15);
                strncpy(taskIcons[j], taskIcons[j + 1], 255);
                taskTotalSizes[j] = taskTotalSizes[j + 1];
                taskUrls[j] = taskUrls[j + 1]; // Shift pointer
                strncpy(taskCategories[j], taskCategories[j + 1], 7);
                
                // Shift UI & Overflow tracking (Static Globals)
                g_taskProgress[j] = g_taskProgress[j+1];
                strncpy(g_taskStatus[j], g_taskStatus[j+1], 31);
                g_taskLastUpdate[j] = g_taskLastUpdate[j+1];
                g_taskTransferredOffset[j] = g_taskTransferredOffset[j+1];
                g_taskLastTransferred[j] = g_taskLastTransferred[j+1];
            }
            
            // Null out the last slot to prevent double-free or ghost tasks
            int lastIndex = activeTaskCount - 1;
            activeTaskIds[lastIndex] = 0;
            taskUrls[lastIndex] = NULL;
            taskTotalSizes[lastIndex] = 0;
            
            g_taskProgress[lastIndex] = 0;
            g_taskStatus[lastIndex][0] = '\0';
            g_taskLastUpdate[lastIndex] = 0;
            g_taskTransferredOffset[lastIndex] = 0;
            g_taskLastTransferred[lastIndex] = 0;
            
            activeTaskCount--;
            break;
        }
    }
    pthread_mutex_unlock(&mutex);
    
    if (res < 0) lastError = res;
    return res;
}

int Installer::FindTaskByContentId(const char* contentId) {
    if (!initialized || !contentId) return -1;
    Log("FindTaskByContentId(%s)", contentId);
    
    int taskId = -1;
    int res = sceBgftServiceDownloadFindTaskByContentId(contentId, (OrbisBgftTaskSubType)0, &taskId);
    Log("FindTask result = 0x%08X, taskId = %d", res, taskId);
    
    if (res < 0) {
        lastError = res;
        return res;
    }
    return taskId;
}

bool Installer::GetTaskProgress(int taskId, InstallTask* outTask) {
    if (!initialized || !outTask) return false;
    
    // Quick check if task is tracked
    bool found = false;
    char cachedTitle[64] = {0};
    int internalIndex = -1; // Declared here for function-wide scope
    
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < activeTaskCount; i++) {
        if (activeTaskIds[i] == taskId) {
            found = true;
            strncpy(cachedTitle, taskTitles[i], 63);
            break;
        }
    }
    pthread_mutex_unlock(&mutex);
    
    if (!found) return false;
    
    OrbisBgftTaskProgress progress;
    memset(&progress, 0, sizeof(progress));
    int res = sceBgftServiceDownloadGetProgress(taskId, &progress);
    
    if (res < 0) {
        lastError = res;
        Log("GetTaskProgress(%d) failed: 0x%08X", taskId, res);
        return false;
    }
    
    // DEBUG: Log ALL BGFT fields to understand what's populated
    // Use %llu for 64-bit fields to verify fix
    Log("BGFT(%d): len=%llu trans=%llu lenT=%llu transT=%llu local=%u%% prep=%u%%", 
        taskId, (unsigned long long)progress.length, (unsigned long long)progress.transferred, 
        (unsigned long long)progress.lengthTotal, (unsigned long long)progress.transferredTotal, 
        progress.localCopyPercent, progress.preparingPercent);
    
    outTask->taskId = taskId;
    
    // REFERENCE MATCH: Do NOT overwrite title here. 
    // Title is already correctly set by GetTasks from internal cache.
    // The previous logic here was redundant and potentially causing race conditions/delays.

    uint64_t rawTransferred = 0; // Declare here so it is accessible for completion check
    
    if (res == 0) {
        // Get stored data for logic
        uint64_t storedTotal = 0;
        
        pthread_mutex_lock(&mutex);
        
        // 1. Find the task in local storage protected by mutex
        for(int i=0; i<activeTaskCount; i++) {
            if (activeTaskIds[i] == taskId) {
                internalIndex = i;
                storedTotal = taskTotalSizes[i];
                break;
            }
        }

        // 2. Determine Transferred (Use ARCHIVE2 priority)
        rawTransferred = progress.lengthTotal; 
        if (rawTransferred == 0 && progress.transferredTotal > 0) rawTransferred = progress.transferredTotal;
        if (rawTransferred == 0 && progress.transferred > 0) rawTransferred = progress.transferred;

        // 3. Overflow Detection Logic (Now safely INSIDE mutex)
        uint64_t transferred = rawTransferred;
        if (internalIndex != -1) {
            uint64_t* pOffset = &g_taskTransferredOffset[internalIndex];
            uint64_t* pLastTrans = &g_taskLastTransferred[internalIndex];

            // Check for rollover: 
            // If current raw is MUCH smaller than last raw (e.g. dropped by > 1GB), assume wrap +4GB
            if (rawTransferred < *pLastTrans && (*pLastTrans - rawTransferred) > 2000000000ULL) {
                 Log("Overflow detected! Task %d raw went %llu -> %llu. Adding 4GB offset.", 
                     taskId, (unsigned long long)*pLastTrans, (unsigned long long)rawTransferred);
                 *pOffset += 4294967296ULL;
            }
            
            *pLastTrans = rawTransferred;
            transferred = rawTransferred + *pOffset;
        }
        
        pthread_mutex_unlock(&mutex);
        
        // 4. Determine Total (FORCE storedTotal if available - Matches ARCHIVE2)
        // Fixes "Progress too fast" if BGFT reports wrong/small length (e.g. chunk size)
        uint64_t total = storedTotal;
        if (total == 0) total = progress.length; 

        
        // Calculate progress
        if (total > 0 && transferred > 0) {
            outTask->progress = (float)transferred / (float)total;
            if (outTask->progress > 1.0f) outTask->progress = 1.0f;
        } else if (progress.localCopyPercent > 0) {
            // Use localCopyPercent if available
            outTask->progress = (float)progress.localCopyPercent / 100.0f;
        } else {
            outTask->progress = 0.0f;
        }
    }
    
    // Check completion status using stored size if BGFT reports unknown
    bool isCompleted = false;
    if (outTask->progress >= 1.0f) isCompleted = true;
    
    // Robust check for inconsistent BGFT reporting on small files/patches
    // (e.g. if lengthTotal == length, it's finished even if transTotal is 0)
    if (progress.length > 0 && rawTransferred >= progress.length) isCompleted = true;

    if (isCompleted) {
        strncpy(outTask->status, "Completed", sizeof(outTask->status));
        outTask->progress = 1.0f; // Force 100% on completion to avoid 99% display
    } else if (progress.errorResult != 0) {
        snprintf(outTask->status, sizeof(outTask->status), "Err:0x%X", progress.errorResult);
    } else {
        strncpy(outTask->status, "Downloading", sizeof(outTask->status));
    }
    
    // PERSISTENCE FIX: Save to static globals for UI speed & robustness
    if (internalIndex != -1) {
        g_taskProgress[internalIndex] = outTask->progress;
        strncpy(g_taskStatus[internalIndex], outTask->status, 31);
        g_taskLastUpdate[internalIndex] = time(NULL);
    }
    
    return true;
}

// === DEINSTALLATION API ===

// sceAppInstUtil declarations (not in OpenOrbis headers)
// InitAppInstUtil moved to top


int Installer::UninstallGame(const char* titleId) {
    if (!titleId) return -1;
    Log("UninstallGame(%s)", titleId);
    
    if (!InitAppInstUtil()) {
        Log("AppInstUtil init failed");
        return -2;
    }
    
    int res = sceAppInstUtilAppUnInstall(titleId);
    Log("sceAppInstUtilAppUnInstall = 0x%08X", res);
    if (res < 0) lastError = res;
    return res;
}

int Installer::UninstallPatch(const char* titleId) {
    if (!titleId) return -1;
    Log("UninstallPatch(%s)", titleId);
    
    if (!InitAppInstUtil()) return -2;
    
    int res = sceAppInstUtilAppUnInstallPat(titleId);
    Log("sceAppInstUtilAppUnInstallPat = 0x%08X", res);
    if (res < 0) lastError = res;
    return res;
}

int Installer::UninstallAddcont(const char* titleId, const char* contentId) {
    if (!titleId || !contentId) return -1;
    Log("UninstallAddcont(%s, %s)", titleId, contentId);
    
    if (!InitAppInstUtil()) return -2;
    
    int res = sceAppInstUtilAppUnInstallAddcont(titleId, contentId);
    Log("sceAppInstUtilAppUnInstallAddcont = 0x%08X", res);
    if (res < 0) lastError = res;
    return res;
}

int Installer::UninstallTheme(const char* contentId) {
    if (!contentId) return -1;
    Log("UninstallTheme(%s)", contentId);
    
    if (!InitAppInstUtil()) return -2;
    
    int res = sceAppInstUtilAppUnInstallTheme(contentId);
    Log("sceAppInstUtilAppUnInstallTheme = 0x%08X", res);
    if (res < 0) lastError = res;
    return res;
}

bool Installer::AppExists(const char* titleId) {
    if (!titleId) return false;
    Log("AppExists(%s)", titleId);
    
    if (!InitAppInstUtil()) return false;
    
    int exists = 0;
    int res = sceAppInstUtilAppExists(titleId, &exists);
    Log("sceAppInstUtilAppExists = 0x%08X, exists = %d", res, exists);
    
    return (res >= 0 && exists != 0);
}

int64_t Installer::GetAppSize(const char* titleId) {
    if (!titleId) return -1;
    Log("GetAppSize(%s)", titleId);
    
    if (!InitAppInstUtil()) return -2;
    
    uint64_t size = 0;
    int res = sceAppInstUtilAppGetSize(titleId, &size);
    Log("sceAppInstUtilAppGetSize = 0x%08X, size = %llu", res, (unsigned long long)size);
    
    if (res < 0) {
        lastError = res;
        return res;
    }
    return (int64_t)size;
}
