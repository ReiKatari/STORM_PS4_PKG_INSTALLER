#include "../include/WebServer.h"
#include "../include/Common.h"
#include "../include/Installer.h"
#include "../include/SystemInfo.h"
#include "../include/Localization.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <orbis/libkernel.h>
#include <orbis/Net.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

// Server state
static int s_serverSocketPrimary = -1;
static int s_serverSocketRpi = -1;
static int s_serverSocketPkgFlow = -1;
static int s_primaryPort = 12813;
static int s_rpiPort = 12800;
static int s_pkgFlowPort = 12801;
static bool s_running = false;
static char s_lastError[256] = {0};
static Installer* s_installer = nullptr;

// System notification struct
struct WsNotifyReq {
    int type;
    int unk0;
    int targetId;
    char message[1024];
    char uri[1024];
    char unk1[1024];
};

static void ShowNotification(const char* text) {
    WsNotifyReq req;
    memset(&req, 0, sizeof(req));
    req.type = 0;
    req.targetId = -1;
    strncpy(req.message, text, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, (OrbisNotificationRequest*)&req, sizeof(req), 0);
}

// Set installer reference
void WebServer_SetInstaller(Installer* inst) {
    s_installer = inst;
}

// Extract URL from JSON body
static void ExtractUrl(const char* json, char* outUrl, int maxLen) {
    memset(outUrl, 0, maxLen);
    const char* urlStart = strstr(json, "http");
    if (!urlStart) return;
    
    int i = 0;
    while (urlStart[i] && urlStart[i] != '"' && urlStart[i] != '}' && urlStart[i] != ']' && urlStart[i] != ' ' && i < maxLen - 1) {
        outUrl[i] = urlStart[i];
        i++;
    }
    outUrl[i] = '\0';

    // Unescape JSON escaped slashes (\/ -> /)
    char* src = outUrl;
    char* dst = outUrl;
    while (*src) {
        if (*src == '\\' && *(src + 1) == '/') {
            src++;
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}

// Helper: Extract unsigned long long from JSON key
static uint64_t ExtractJsonLong(const char* json, const char* key) {
    if (!json || !key) return 0;
    const char* k = strstr(json, key);
    if (!k) return 0;
    
    // Move past key
    const char* val = strchr(k, ':');
    if (!val) return 0;
    val++;
    
    // Skip whitespace
    while(*val && (*val == ' ' || *val == '"')) val++;
    
    if (!*val) return 0;
    
    return strtoull(val, NULL, 10);
}

// Extract endpoint from request
static void ExtractEndpoint(const char* request, char* outEndpoint, int maxLen) {
    memset(outEndpoint, 0, maxLen);
    
    // Find start of path
    const char* pathStart = strchr(request, ' ');
    if (!pathStart) return;
    pathStart++;
    
    // Find end of path
    const char* pathEnd = strchr(pathStart, ' ');
    if (!pathEnd) pathEnd = strchr(pathStart, '\r');
    if (!pathEnd) pathEnd = strchr(pathStart, '\n');
    if (!pathEnd) return;
    
    int len = pathEnd - pathStart;
    if (len >= maxLen) len = maxLen - 1;
    strncpy(outEndpoint, pathStart, len);
}

// Send HTTP response
void WebServer_SendSuccess(int clientSocket, const char* jsonBody) {
    char response[2048];
    snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Headers: *\r\n"
        "Connection: close\r\n"
        "Content-Length: %zu\r\n"
        "\r\n%s",
        strlen(jsonBody), jsonBody);
    write(clientSocket, response, strlen(response));
}

void WebServer_SendError(int clientSocket, int errorCode, const char* message) {
    char body[512];
    snprintf(body, sizeof(body), "{\"status\":\"fail\",\"error_code\":0x%08X,\"error\":\"%s\"}\n", errorCode, message);
    WebServer_SendSuccess(clientSocket, body);
}

// Handle /api/install
static void HandleInstall(int conn, const char* body) {
    char url[1024];
    ExtractUrl(body, url, sizeof(url));
    
    if (strlen(url) == 0) {
        WebServer_SendError(conn, -1, "No URL provided");
        return;
    }
    
    if (!s_installer) {
        WebServer_SendError(conn, -2, "Installer not initialized");
        return;
    }
    
    // Extract file_size from JSON if present
    uint64_t fileSize = ExtractJsonLong(body, "file_size");
    if (fileSize == 0) fileSize = ExtractJsonLong(body, "size");
    
    // Pre-flight space check to protect from BGFT 0x80990004 crash
    if (fileSize > 0 && !SystemInfo_CheckHasEnoughSpace(fileSize)) {
        Log("HandleInstall: Not enough free space on /user! Required: %llu bytes", (unsigned long long)fileSize);
        ShowNotification(Loc(STR_ERR_NO_SPACE));
        WebServer_SendError(conn, 0x80990001, "Not enough free disk space on PS4");
        return;
    }
    
    int taskId = s_installer->Install(url, "Package", fileSize);
    Log("HandleInstall: Install returned taskId=%d", taskId);
    
    if (taskId >= 0) {
        char json[512];
        snprintf(json, sizeof(json), 
            "{\"success\":true,\"status\":\"Package queued\",\"task_id\":%d,\"url\":\"%s\"}\n",
            taskId, url);
        WebServer_SendSuccess(conn, json);
        
        char notif[128];
        snprintf(notif, sizeof(notif), "Package queued: Task %d", taskId);
        
        Log("HandleInstall: Attempting ShowNotification");
        ShowNotification(notif);
        Log("HandleInstall: ShowNotification done");
    } else if (taskId == -10) {
        // Already Installed
        char json[128];
        snprintf(json, sizeof(json), "{\"success\":false,\"error\":\"already_installed\",\"status\":\"App already installed\"}\n");
        WebServer_SendSuccess(conn, json);
    } else {
        char json[512];
        snprintf(json, sizeof(json), 
            "{\"success\":false,\"error_code\":%d,\"url\":\"%s\"}\n",
            taskId, url);
        WebServer_SendSuccess(conn, json);
    }
}

// Handle /api/get_task_progress
static void HandleGetTaskProgress(int conn, const char* body) {
    // Extract task_id from body
    int taskId = -1;
    const char* tidStr = strstr(body, "task_id");
    if (tidStr) {
        const char* num = tidStr + 7;
        while (*num && (*num < '0' || *num > '9')) num++;
        if (*num) taskId = atoi(num);
    }
    
    if (taskId < 0 || !s_installer) {
        WebServer_SendError(conn, -1, "Invalid task_id");
        return;
    }
    
    InstallTask tasks[MAX_TASKS];
    int count = s_installer->GetTasks(tasks, MAX_TASKS);
    
    for (int i = 0; i < count; i++) {
        if (tasks[i].taskId == taskId) {
            char json[512];
            snprintf(json, sizeof(json), 
                "{\"status\":\"success\",\"task_id\":%d,\"progress\":%.2f,\"state\":\"%s\",\"category\":\"%s\"}\n",
                taskId, tasks[i].progress * 100, tasks[i].status, tasks[i].category);
            WebServer_SendSuccess(conn, json);
            return;
        }
    }
    
    WebServer_SendError(conn, -1, "Task not found");
}

// Handle /api/find_task
static void HandleFindTask(int conn, const char* body) {
    // For now, return all tasks
    if (!s_installer) {
        WebServer_SendError(conn, -1, "Installer not initialized");
        return;
    }
    
    InstallTask tasks[MAX_TASKS];
    int count = s_installer->GetTasks(tasks, MAX_TASKS);
    
    char json[2048];
    int pos = 0;
    pos += snprintf(json + pos, sizeof(json) - pos, "{\"status\":\"success\",\"tasks\":[");
    
    for (int i = 0; i < count; i++) {
        if (i > 0) pos += snprintf(json + pos, sizeof(json) - pos, ",");
        pos += snprintf(json + pos, sizeof(json) - pos, 
            "{\"task_id\":%d,\"title\":\"%s\",\"progress\":%.2f,\"state\":\"%s\",\"category\":\"%s\"}",
            tasks[i].taskId, tasks[i].title, tasks[i].progress * 100, tasks[i].status, tasks[i].category);
    }
    
    pos += snprintf(json + pos, sizeof(json) - pos, "]}\n");
    WebServer_SendSuccess(conn, json);
}

// Handle single request
static void HandleRequest(int conn) {
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    char buffer[8192];
    memset(buffer, 0, sizeof(buffer));
    int bytesRead = read(conn, buffer, sizeof(buffer) - 1);
    
    if (bytesRead <= 0) {
        close(conn);
        return;
    }
    
    // Extract endpoint
    char endpoint[128];
    ExtractEndpoint(buffer, endpoint, sizeof(endpoint));
    
    // Find body
    char* body = strstr(buffer, "\r\n\r\n");
    if (body) body += 4;
    else body = buffer;
    
    // Handle CORS preflight
    if (strncmp(buffer, "OPTIONS", 7) == 0) {
        const char* corsResponse = 
            "HTTP/1.1 200 OK\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: *\r\n"
            "Content-Length: 0\r\n\r\n";
        write(conn, corsResponse, strlen(corsResponse));
        close(conn);
        return;
    }
    
    // Route to handler
    if (strstr(endpoint, "/api/install")) {
        HandleInstall(conn, body);
    } else if (strstr(endpoint, "/api/get_task_progress")) {
        HandleGetTaskProgress(conn, body);
    } else if (strstr(endpoint, "/api/find_task")) {
        HandleFindTask(conn, body);
    } else if (strstr(endpoint, "/api/pause_task")) {
        // Extract task_id and call pause
        int taskId = -1;
        const char* tidStr = strstr(body, "task_id");
        if (tidStr) {
            const char* num = tidStr + 7;
            while (*num && (*num < '0' || *num > '9')) num++;
            if (*num) taskId = atoi(num);
        }
        if (taskId >= 0 && s_installer) {
            int res = s_installer->PauseTask(taskId);
            char json[128];
            snprintf(json, sizeof(json), "{\"status\":\"success\",\"task_id\":%d,\"result\":%d}\n", taskId, res);
            WebServer_SendSuccess(conn, json);
        } else {
            WebServer_SendError(conn, -1, "Invalid task_id");
        }
    } else if (strstr(endpoint, "/api/resume_task")) {
        int taskId = -1;
        const char* tidStr = strstr(body, "task_id");
        if (tidStr) {
            const char* num = tidStr + 7;
            while (*num && (*num < '0' || *num > '9')) num++;
            if (*num) taskId = atoi(num);
        }
        if (taskId >= 0 && s_installer) {
            int res = s_installer->ResumeTask(taskId);
            char json[128];
            snprintf(json, sizeof(json), "{\"status\":\"success\",\"task_id\":%d,\"result\":%d}\n", taskId, res);
            WebServer_SendSuccess(conn, json);
        } else {
            WebServer_SendError(conn, -1, "Invalid task_id");
        }
    } else if (strstr(endpoint, "/api/stop_task")) {
        int taskId = -1;
        const char* tidStr = strstr(body, "task_id");
        if (tidStr) {
            const char* num = tidStr + 7;
            while (*num && (*num < '0' || *num > '9')) num++;
            if (*num) taskId = atoi(num);
        }
        if (taskId >= 0 && s_installer) {
            int res = s_installer->StopTask(taskId);
            char json[128];
            snprintf(json, sizeof(json), "{\"status\":\"success\",\"task_id\":%d,\"result\":%d}\n", taskId, res);
            WebServer_SendSuccess(conn, json);
        } else {
            WebServer_SendError(conn, -1, "Invalid task_id");
        }
    } else if (strstr(endpoint, "/api/unregister_task")) {
        int taskId = -1;
        const char* tidStr = strstr(body, "task_id");
        if (tidStr) {
            const char* num = tidStr + 7;
            while (*num && (*num < '0' || *num > '9')) num++;
            if (*num) taskId = atoi(num);
        }
        if (taskId >= 0 && s_installer) {
            int res = s_installer->UnregisterTask(taskId);
            char json[128];
            snprintf(json, sizeof(json), "{\"status\":\"success\",\"task_id\":%d,\"result\":%d}\n", taskId, res);
            WebServer_SendSuccess(conn, json);
        } else {
            WebServer_SendError(conn, -1, "Invalid task_id");
        }
    } else if (strstr(endpoint, "/api/uninstall_game")) {
        // Extract title_id
        char titleId[16] = {0};
        const char* tidStr = strstr(body, "title_id");
        if (!tidStr) tidStr = strstr(body, "titleId");
        if (tidStr) {
            const char* start = strchr(tidStr, ':');
            if (start) {
                start++;
                while (*start == ' ' || *start == '"') start++;
                int i = 0;
                while (start[i] && start[i] != '"' && start[i] != '}' && i < 15) {
                    titleId[i] = start[i];
                    i++;
                }
            }
        }
        if (strlen(titleId) > 0 && s_installer) {
            int res = s_installer->UninstallGame(titleId);
            char json[128];
            snprintf(json, sizeof(json), "{\"status\":\"success\",\"title_id\":\"%s\",\"result\":%d}\n", titleId, res);
            WebServer_SendSuccess(conn, json);
        } else {
            WebServer_SendError(conn, -1, "Invalid title_id");
        }
    } else if (strstr(endpoint, "/api/uninstall_patch")) {
        char titleId[16] = {0};
        const char* tidStr = strstr(body, "title_id");
        if (!tidStr) tidStr = strstr(body, "titleId");
        if (tidStr) {
            const char* start = strchr(tidStr, ':');
            if (start) {
                start++;
                while (*start == ' ' || *start == '"') start++;
                int i = 0;
                while (start[i] && start[i] != '"' && start[i] != '}' && i < 15) {
                    titleId[i] = start[i];
                    i++;
                }
            }
        }
        if (strlen(titleId) > 0 && s_installer) {
            int res = s_installer->UninstallPatch(titleId);
            char json[128];
            snprintf(json, sizeof(json), "{\"status\":\"success\",\"title_id\":\"%s\",\"result\":%d}\n", titleId, res);
            WebServer_SendSuccess(conn, json);
        } else {
            WebServer_SendError(conn, -1, "Invalid title_id");
        }
    } else if (strstr(endpoint, "/api/uninstall_ac")) {
        // Needs title_id and content_id
        char titleId[16] = {0};
        char contentId[48] = {0};
        // Parse title_id and content_id from JSON
        const char* tidStr = strstr(body, "title_id");
        if (tidStr) {
            const char* start = strchr(tidStr, ':');
            if (start) {
                start++;
                while (*start == ' ' || *start == '"') start++;
                int i = 0;
                while (start[i] && start[i] != '"' && start[i] != '}' && i < 15) {
                    titleId[i] = start[i];
                    i++;
                }
            }
        }
        const char* cidStr = strstr(body, "content_id");
        if (cidStr) {
            const char* start = strchr(cidStr, ':');
            if (start) {
                start++;
                while (*start == ' ' || *start == '"') start++;
                int i = 0;
                while (start[i] && start[i] != '"' && start[i] != '}' && i < 47) {
                    contentId[i] = start[i];
                    i++;
                }
            }
        }
        if (strlen(titleId) > 0 && strlen(contentId) > 0 && s_installer) {
            int res = s_installer->UninstallAddcont(titleId, contentId);
            char json[256];
            snprintf(json, sizeof(json), "{\"status\":\"success\",\"title_id\":\"%s\",\"content_id\":\"%s\",\"result\":%d}\n", titleId, contentId, res);
            WebServer_SendSuccess(conn, json);
        } else {
            WebServer_SendError(conn, -1, "Invalid title_id or content_id");
        }
    } else if (strstr(endpoint, "/api/uninstall_theme")) {
        char contentId[48] = {0};
        const char* cidStr = strstr(body, "content_id");
        if (cidStr) {
            const char* start = strchr(cidStr, ':');
            if (start) {
                start++;
                while (*start == ' ' || *start == '"') start++;
                int i = 0;
                while (start[i] && start[i] != '"' && start[i] != '}' && i < 47) {
                    contentId[i] = start[i];
                    i++;
                }
            }
        }
        if (strlen(contentId) > 0 && s_installer) {
            int res = s_installer->UninstallTheme(contentId);
            char json[128];
            snprintf(json, sizeof(json), "{\"status\":\"success\",\"content_id\":\"%s\",\"result\":%d}\n", contentId, res);
            WebServer_SendSuccess(conn, json);
        } else {
            WebServer_SendError(conn, -1, "Invalid content_id");
        }
    } else if (strstr(endpoint, "/api/is_exists")) {
        char titleId[16] = {0};
        const char* tidStr = strstr(body, "title_id");
        if (!tidStr) tidStr = strstr(body, "titleId");
        if (tidStr) {
            const char* start = strchr(tidStr, ':');
            if (start) {
                start++;
                while (*start == ' ' || *start == '"') start++;
                int i = 0;
                while (start[i] && start[i] != '"' && start[i] != '}' && i < 15) {
                    titleId[i] = start[i];
                    i++;
                }
            }
        }
        if (strlen(titleId) > 0 && s_installer) {
            bool exists = s_installer->AppExists(titleId);
            char json[128];
            snprintf(json, sizeof(json), "{\"status\":\"success\",\"title_id\":\"%s\",\"exists\":\"%s\"}\n", titleId, exists ? "true" : "false");
            WebServer_SendSuccess(conn, json);
        } else {
            WebServer_SendError(conn, -1, "Invalid title_id");
        }
    } else if (strstr(endpoint, "/api/apps/list")) {
        if (s_installer && s_installer->IsInitialized()) {
            AppInfo apps[100]; // Limit to 100 for now
            int count = s_installer->GetInstalledApps(apps, 100);
            
            // Build JSON manually (simple array)
            // Use heap 32KB
            char* jsonBuf = (char*)malloc(32 * 1024); 
            if (jsonBuf) {
                // Determine buffer size more safely or just be careful
                strcpy(jsonBuf, "{\"status\":\"success\",\"apps\":[");
                for (int i = 0; i < count; i++) {
                    char entry[512];
                    // Basic escaping for title quotes if needed
                    snprintf(entry, sizeof(entry), "{\"title_id\":\"%s\",\"title\":\"%s\",\"size\":%llu}%s", 
                        apps[i].titleId, apps[i].title, (unsigned long long)apps[i].size,
                        (i < count - 1) ? "," : "");
                        
                    // Check bounds just in case
                    if (strlen(jsonBuf) + strlen(entry) < 32000) {
                        strcat(jsonBuf, entry);
                    }
                }
                strcat(jsonBuf, "]}");
                WebServer_SendSuccess(conn, jsonBuf);
                free(jsonBuf);
            } else {
                WebServer_SendError(conn, 500, "Memory allocation failed");
            }
        } else {
            WebServer_SendError(conn, 500, "Installer not initialized");
        }
    } else if (strstr(endpoint, "/ping")) {
        char json[128];
        snprintf(json, sizeof(json), "{\"status\":\"ok\",\"service\":\"PackegeFlowService\",\"version\":\"1.60\"}\n");
        WebServer_SendSuccess(conn, json);
    } else if (strstr(endpoint, "/system/info") || strstr(endpoint, "/api/system/info")) {
        const SystemModelInfo* sys = SystemInfo_Get();
        char json[512];
        snprintf(json, sizeof(json),
            "{\"status\":\"success\",\"service\":\"PackegeFlowService\",\"version\":\"1.60\","
            "\"environment\":\"ps4\",\"pkgVersion\":\"1.60\","
            "\"firmware\":{\"version\":\"%s\"},"
            "\"model\":{\"name\":\"%s\",\"family\":\"%s\"},"
            "\"hen\":{\"name\":\"GoldHEN\",\"version\":\"%s\",\"sdkVersion\":\"2.4\"},"
            "\"filesystemAccess\":{\"enabled\":true}}\n",
            sys->firmwareStr, sys->modelName, sys->isNeo ? "PS4 Pro" : "PS4", sys->henVersion);
        WebServer_SendSuccess(conn, json);
    } else if (strstr(endpoint, "/storage") || strstr(endpoint, "/api/storage")) {
        StorageInfo userStorage;
        SystemInfo_GetStorage("/user", &userStorage);
        char json[512];
        snprintf(json, sizeof(json),
            "{\"status\":\"success\",\"service\":\"PackegeFlowService\",\"version\":\"1.60\","
            "\"volumes\":[{"
            "\"id\":\"user\",\"path\":\"/user\",\"available\":true,"
            "\"totalBytes\":%llu,\"freeBytes\":%llu,\"availableBytes\":%llu,\"usedBytes\":%llu"
            "}]}\n",
            (unsigned long long)userStorage.totalBytes,
            (unsigned long long)userStorage.freeBytes,
            (unsigned long long)userStorage.freeBytes,
            (unsigned long long)userStorage.usedBytes);
        WebServer_SendSuccess(conn, json);
    } else if (strstr(endpoint, "/install/capabilities")) {
        char json[256];
        snprintf(json, sizeof(json),
            "{\"service\":\"PackegeFlowService\",\"version\":\"1.60\",\"installApi\":1,"
            "\"authentication\":\"none\",\"ready\":true,"
            "\"contentTypes\":[\"game\",\"patch\",\"dlc\",\"theme\"]}\n");
        WebServer_SendSuccess(conn, json);
    } else if (strstr(endpoint, "/install/pair")) {
        char json[128];
        snprintf(json, sizeof(json),
            "{\"service\":\"PackegeFlowService\",\"paired\":true,\"token\":\"0123456789abcdef0123456789abcdef\"}\n");
        WebServer_SendSuccess(conn, json);
    } else if (strstr(endpoint, "/install/jobs")) {
        char url[1024];
        ExtractUrl(body, url, sizeof(url));
        uint64_t fileSize = ExtractJsonLong(body, "size");
        if (fileSize == 0) fileSize = ExtractJsonLong(body, "file_size");
        
        char reqId[64] = "job-0";
        const char* reqPtr = strstr(body, "requestId");
        if (reqPtr) {
            const char* colon = strchr(reqPtr, ':');
            if (colon) {
                colon++;
                while (*colon == ' ' || *colon == '"') colon++;
                int idx = 0;
                while (colon[idx] && colon[idx] != '"' && colon[idx] != '}' && colon[idx] != ',' && idx < 63) {
                    reqId[idx] = colon[idx];
                    idx++;
                }
                reqId[idx] = '\0';
            }
        }
        
        if (fileSize > 0 && !SystemInfo_CheckHasEnoughSpace(fileSize)) {
            Log("HandleRequest: PackageFlow job: Not enough disk space! Required: %llu bytes", (unsigned long long)fileSize);
            ShowNotification(Loc(STR_ERR_NO_SPACE));
            WebServer_SendError(conn, 0x80990001, "Not enough free disk space on PS4");
        } else if (s_installer && strlen(url) > 0) {
            int taskId = s_installer->Install(url, "Package", fileSize);
            char json[256];
            snprintf(json, sizeof(json),
                "{\"service\":\"PackegeFlowService\",\"status\":\"queued\",\"requestId\":\"%s\",\"taskId\":%d}\n",
                reqId, taskId);
            WebServer_SendSuccess(conn, json);
        } else {
            WebServer_SendError(conn, -1, "Failed to register job");
        }
    } else {
        // Default: show status
        char json[384];
        snprintf(json, sizeof(json), "{\"status\":\"success\",\"app\":\"STORM PS4 PKG INSTALLER\",\"version\":\"1.60\",\"primary_port\":%d,\"rpi_port\":%d,\"pkgflow_port\":%d}\n",
                 s_primaryPort, s_rpiPort, s_pkgFlowPort);
        WebServer_SendSuccess(conn, json);
    }
    
    close(conn);
}

int WebServer_GetPrimaryPort() { return s_primaryPort; }
int WebServer_GetRpiPort() { return s_rpiPort; }
int WebServer_GetPkgFlowPort() { return s_pkgFlowPort; }
bool WebServer_IsRpiPortActive() { return s_serverSocketRpi >= 0; }
bool WebServer_IsPkgFlowPortActive() { return s_serverSocketPkgFlow >= 0; }

static int CreateBoundSocket(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(sock);
        return -2;
    }
    
    if (listen(sock, 16) != 0) {
        close(sock);
        return -3;
    }
    
    return sock;
}

int WebServer_Start(int port, int rpiPort, int pkgFlowPort) {
    s_primaryPort = port;
    s_rpiPort = rpiPort;
    s_pkgFlowPort = pkgFlowPort;
    
    // 1. Bind primary port (12813)
    s_serverSocketPrimary = CreateBoundSocket(s_primaryPort);
    if (s_serverSocketPrimary < 0) {
        snprintf(s_lastError, sizeof(s_lastError), "bind() failed on primary port %d", s_primaryPort);
        Log("WebServer: Failed to bind primary port %d", s_primaryPort);
    } else {
        Log("WebServer: Listening on primary port %d", s_primaryPort);
    }
    
    // 2. Bind standard RPI port (12800) for universal compatibility
    if (rpiPort > 0 && rpiPort != port) {
        s_serverSocketRpi = CreateBoundSocket(rpiPort);
        if (s_serverSocketRpi < 0) {
            Log("WebServer: Could not bind standard RPI port %d (in use or restricted)", rpiPort);
        } else {
            Log("WebServer: Listening on standard RPI port %d", rpiPort);
        }
    }

    // 3. Bind PackageFlow port (12801) for playstation_installer compatibility
    if (pkgFlowPort > 0 && pkgFlowPort != port && pkgFlowPort != rpiPort) {
        s_serverSocketPkgFlow = CreateBoundSocket(pkgFlowPort);
        if (s_serverSocketPkgFlow < 0) {
            Log("WebServer: Could not bind PackageFlow port %d (in use or restricted)", pkgFlowPort);
        } else {
            Log("WebServer: Listening on PackageFlow port %d", pkgFlowPort);
        }
    }
    
    if (s_serverSocketPrimary < 0 && s_serverSocketRpi < 0 && s_serverSocketPkgFlow < 0) {
        s_running = false;
        return -1;
    }
    
    s_running = true;
    return 0;
}

void WebServer_Stop() {
    s_running = false;
    if (s_serverSocketPrimary >= 0) {
        close(s_serverSocketPrimary);
        s_serverSocketPrimary = -1;
    }
    if (s_serverSocketRpi >= 0) {
        close(s_serverSocketRpi);
        s_serverSocketRpi = -1;
    }
    if (s_serverSocketPkgFlow >= 0) {
        close(s_serverSocketPkgFlow);
        s_serverSocketPkgFlow = -1;
    }
}

bool WebServer_IsRunning() {
    return s_running && (s_serverSocketPrimary >= 0 || s_serverSocketRpi >= 0 || s_serverSocketPkgFlow >= 0);
}

const char* WebServer_GetLastError() {
    return s_lastError;
}

void WebServer_Process() {
    if (!s_running) return;
    
    // Process up to 5 connections on primary port (12813)
    if (s_serverSocketPrimary >= 0) {
        for (int i = 0; i < 5; i++) {
            struct sockaddr_in clientAddr;
            socklen_t addrLen = sizeof(clientAddr);
            int conn = accept(s_serverSocketPrimary, (struct sockaddr*)&clientAddr, &addrLen);
            if (conn >= 0) {
                HandleRequest(conn);
            } else {
                break;
            }
        }
    }
    
    // Process up to 5 connections on standard RPI port (12800)
    if (s_serverSocketRpi >= 0) {
        for (int i = 0; i < 5; i++) {
            struct sockaddr_in clientAddr;
            socklen_t addrLen = sizeof(clientAddr);
            int conn = accept(s_serverSocketRpi, (struct sockaddr*)&clientAddr, &addrLen);
            if (conn >= 0) {
                HandleRequest(conn);
            } else {
                break;
            }
        }
    }

    // Process up to 5 connections on PackageFlow port (12801)
    if (s_serverSocketPkgFlow >= 0) {
        for (int i = 0; i < 5; i++) {
            struct sockaddr_in clientAddr;
            socklen_t addrLen = sizeof(clientAddr);
            int conn = accept(s_serverSocketPkgFlow, (struct sockaddr*)&clientAddr, &addrLen);
            if (conn >= 0) {
                HandleRequest(conn);
            } else {
                break;
            }
        }
    }
}

