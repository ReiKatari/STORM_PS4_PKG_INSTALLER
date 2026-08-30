#include "../include/WebServer.h"
#include "../include/Common.h"
#include "../include/Installer.h"
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
static int s_serverSocket = -1;
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
    while (urlStart[i] && urlStart[i] != '"' && urlStart[i] != '}' && urlStart[i] != ' ' && i < maxLen - 1) {
        outUrl[i] = urlStart[i];
        i++;
    }
    outUrl[i] = '\0';
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
    
    Log("HandleInstall: Calling Installer->Install");
    // Extract file_size from JSON if present
    uint64_t fileSize = ExtractJsonLong(body, "file_size");
    
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
    } else {
        // Default: show status
        char json[256];
        snprintf(json, sizeof(json), "{\"status\":\"success\",\"app\":\"STORM PS4 PKG INSTALLER\",\"version\":\"1.44\"}\n");
        WebServer_SendSuccess(conn, json);
    }
    
    close(conn);
}

int WebServer_Start(int port) {
    s_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (s_serverSocket < 0) {
        snprintf(s_lastError, sizeof(s_lastError), "socket() failed");
        return -1;
    }
    
    int flags = fcntl(s_serverSocket, F_GETFL, 0);
    fcntl(s_serverSocket, F_SETFL, flags | O_NONBLOCK);
    
    int reuse = 1;
    setsockopt(s_serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    
    if (bind(s_serverSocket, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        snprintf(s_lastError, sizeof(s_lastError), "bind() failed on port %d", port);
        close(s_serverSocket);
        s_serverSocket = -1;
        return -2;
    }
    
    if (listen(s_serverSocket, 10) != 0) {
        snprintf(s_lastError, sizeof(s_lastError), "listen() failed");
        close(s_serverSocket);
        s_serverSocket = -1;
        return -3;
    }
    
    s_running = true;
    return 0;
}

void WebServer_Stop() {
    s_running = false;
    if (s_serverSocket >= 0) {
        close(s_serverSocket);
        s_serverSocket = -1;
    }
}

bool WebServer_IsRunning() {
    return s_running && s_serverSocket >= 0;
}

const char* WebServer_GetLastError() {
    return s_lastError;
}

void WebServer_Process() {
    if (!s_running || s_serverSocket < 0) return;
    
    // Handle multiple connections in one iteration to prevent TCP backlog overflow
    // Limit to 10 per cycle to avoid blocking the UI thread for too long
    for (int i = 0; i < 10; i++) {
        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        int conn = accept(s_serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
        
        if (conn >= 0) {
            HandleRequest(conn);
        } else {
            // EWOULDBLOCK or EAGAIN means no more waiting connections
            break;
        }
    }
}

