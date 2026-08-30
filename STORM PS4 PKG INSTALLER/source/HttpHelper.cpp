#include "../include/HttpHelper.h"
#include <orbis/Http.h>
#include <orbis/Ssl.h>
#include <orbis/Sysmodule.h>
#include <orbis/_types/sysmodule.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

// PKG format constants
#define PKG_MAGIC_OFFSET      0x00
#define PKG_CONTENT_ID_OFFSET 0x40
#define PKG_CONTENT_ID_SIZE   36

#include <pthread.h>

// HTTP state - isolated
static int s_httpPoolId = -1;
static int s_sslCtxId = -1;
static bool s_initialized = false;
static pthread_mutex_t s_httpMutex = PTHREAD_MUTEX_INITIALIZER;

int HttpHelper_Init() {
    pthread_mutex_lock(&s_httpMutex);
    if (s_initialized) {
        pthread_mutex_unlock(&s_httpMutex);
        return 0;
    }
    
    // Load HTTP and SSL modules
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    
    // Init SSL first
    int ret = sceSslInit(0x100000);
    if (ret >= 0) {
        s_sslCtxId = ret;
    } else {
        s_sslCtxId = -1;
    }
    
    // Init HTTP with SSL context
    ret = sceHttpInit(-1, s_sslCtxId, 0x100000);
    if (ret < 0) {
        pthread_mutex_unlock(&s_httpMutex);
        return ret;
    }
    s_httpPoolId = ret;
    
    s_initialized = true;
    pthread_mutex_unlock(&s_httpMutex);
    return 0;
}

void HttpHelper_Term() {
    if (!s_initialized) return;
    
    if (s_httpPoolId >= 0) {
        sceHttpTerm(s_httpPoolId);
        s_httpPoolId = -1;
    }
    
    if (s_sslCtxId >= 0) {
        sceSslTerm();
        s_sslCtxId = -1;
    }
    
    s_initialized = false;
}

int64_t HttpHelper_GetFileSize(const char* url) {
    if (!s_initialized) {
        if (HttpHelper_Init() < 0) return -1;
    }
    
    int templateId = sceHttpCreateTemplate(s_httpPoolId, "STORM/1.0", 1, 0);
    if (templateId < 0) return -1;
    
    int connId = sceHttpCreateConnectionWithURL(templateId, url, 0);
    if (connId < 0) {
        sceHttpDeleteTemplate(templateId);
        return -1;
    }
    
    // HEAD request
    int reqId = sceHttpCreateRequestWithURL(connId, ORBIS_METHOD_HEAD, url, 0);
    if (reqId < 0) {
        sceHttpDeleteConnection(connId);
        sceHttpDeleteTemplate(templateId);
        return -1;
    }
    
    int ret = sceHttpSendRequest(reqId, NULL, 0);
    if (ret < 0) {
        // HEAD might fail on some servers, try GET with 0 range?
        // For now, return -1
        sceHttpDeleteRequest(reqId);
        sceHttpDeleteConnection(connId);
        sceHttpDeleteTemplate(templateId);
        return -1;
    }
    
    int64_t fileSize = -1;
    uint64_t contentLen = 0;
    int32_t result = 0;
    if (sceHttpGetResponseContentLength(reqId, &result, &contentLen) >= 0 && result == 0) {
        fileSize = (int64_t)contentLen;
    }
    
    sceHttpDeleteRequest(reqId);
    sceHttpDeleteConnection(connId);
    sceHttpDeleteTemplate(templateId);
    
    return fileSize;
}

int HttpHelper_DownloadPartial(const char* url, void* buffer, int maxSize, int64_t* outFileSize) {
    if (!s_initialized) {
        int ret = HttpHelper_Init();
        if (ret < 0) return ret;
    }
    
    // Create template
    int templateId = sceHttpCreateTemplate(s_httpPoolId, "STORM/1.0", 1, 0);
    if (templateId < 0) return templateId;
    
    // Create connection
    int connId = sceHttpCreateConnectionWithURL(templateId, url, 0);
    if (connId < 0) {
        sceHttpDeleteTemplate(templateId);
        return connId;
    }
    
    // Create request
    int reqId = sceHttpCreateRequestWithURL(connId, ORBIS_METHOD_GET, url, 0);
    if (reqId < 0) {
        sceHttpDeleteConnection(connId);
        sceHttpDeleteTemplate(templateId);
        return reqId;
    }
    
    // Add Range header to only get first bytes
    char rangeHeader[64];
    snprintf(rangeHeader, sizeof(rangeHeader), "bytes=0-%d", maxSize - 1);
    sceHttpAddRequestHeader(reqId, "Range", rangeHeader, 0);
    
    // Send request
    int ret = sceHttpSendRequest(reqId, NULL, 0);
    if (ret < 0) {
        sceHttpDeleteRequest(reqId);
        sceHttpDeleteConnection(connId);
        sceHttpDeleteTemplate(templateId);
        return ret;
    }
    
    // Get file size if available
    if (outFileSize) {
        *outFileSize = 0;
        int32_t result = 0;
        size_t contentLen = 0;
        if (sceHttpGetResponseContentLength(reqId, &result, &contentLen) >= 0 && result == 0) {
            *outFileSize = (int64_t)contentLen;
        }
    }
    
    // Read data
    int totalRead = 0;
    while (totalRead < maxSize) {
        int r = sceHttpReadData(reqId, (char*)buffer + totalRead, maxSize - totalRead);
        if (r <= 0) break;
        totalRead += r;
    }
    
    // Clean up - CRITICAL: must clean up before BGFT operations
    sceHttpDeleteRequest(reqId);
    sceHttpDeleteConnection(connId);
    sceHttpDeleteTemplate(templateId);
    
    return totalRead;
}

bool HttpHelper_ExtractPkgInfo(const char* url, char* outContentId, int idMaxLen, 
                                char* outTitle, int titleMaxLen, int64_t* outFileSize) {
    unsigned char header[512];
    int64_t fileSize = HttpHelper_GetFileSize(url); // Get REAL size
    
    // Download PKG header
    int64_t dummy;
    int bytesRead = HttpHelper_DownloadPartial(url, header, sizeof(header), &dummy);
    
    if (outFileSize) *outFileSize = fileSize;
    
    // Need at least enough for Content ID
    if (bytesRead < PKG_CONTENT_ID_OFFSET + PKG_CONTENT_ID_SIZE) {
        return false;
    }
    
    // Check PKG magic (0x7F 'C' 'N' 'T')
    if (header[0] != 0x7F || header[1] != 'C' || header[2] != 'N' || header[3] != 'T') {
        return false;
    }
    
    // Extract Content ID (offset 0x40, 36 bytes)
    if (outContentId) {
        memset(outContentId, 0, idMaxLen);
        int len = PKG_CONTENT_ID_SIZE;
        if (len > idMaxLen - 1) len = idMaxLen - 1;
        memcpy(outContentId, &header[PKG_CONTENT_ID_OFFSET], len);
        
        // Trim trailing nulls/spaces
        for (int i = len - 1; i >= 0; i--) {
            if (outContentId[i] == '\0' || outContentId[i] == ' ') {
                outContentId[i] = '\0';
            } else {
                break;
            }
        }
        
        // Validate format
        if (strchr(outContentId, '-') == NULL || strchr(outContentId, '_') == NULL) {
            return false;
        }
    }
    
    // Extract Title from Content ID (e.g., "UP0001-CUSA12345_00-..." -> "CUSA12345")
    if (outTitle && outContentId) {
        memset(outTitle, 0, titleMaxLen);
        char* hyphen = strchr(outContentId, '-');
        if (hyphen) {
            char* underscore = strchr(hyphen, '_');
            if (underscore && (underscore - hyphen - 1) > 0) {
                int titleLen = underscore - hyphen - 1;
                if (titleLen > titleMaxLen - 1) titleLen = titleMaxLen - 1;
                strncpy(outTitle, hyphen + 1, titleLen);
            }
        }
        // Fallback to content ID if no title extracted
        if (strlen(outTitle) == 0) {
            strncpy(outTitle, outContentId, titleMaxLen - 1);
        }
    }
    
    return true;
}

// Download specific range
bool HttpHelper_DownloadRange(const char* url, int64_t offset, int size, void* buffer) {
    if (!s_initialized) {
        if (HttpHelper_Init() < 0) return false;
    }
    
    int templateId = sceHttpCreateTemplate(s_httpPoolId, "STORM/1.0", 1, 0);
    if (templateId < 0) return false;
    
    int connId = sceHttpCreateConnectionWithURL(templateId, url, 0);
    if (connId < 0) {
        sceHttpDeleteTemplate(templateId);
        return false;
    }
    
    int reqId = sceHttpCreateRequestWithURL(connId, ORBIS_METHOD_GET, url, 0);
    if (reqId < 0) {
        sceHttpDeleteConnection(connId);
        sceHttpDeleteTemplate(templateId);
        return false;
    }
    
    char rangeHeader[64];
    snprintf(rangeHeader, sizeof(rangeHeader), "bytes=%lld-%lld", (long long)offset, (long long)(offset + size - 1));
    sceHttpAddRequestHeader(reqId, "Range", rangeHeader, 0);
    
    int ret = sceHttpSendRequest(reqId, NULL, 0);
    if (ret < 0) {
        sceHttpDeleteRequest(reqId);
        sceHttpDeleteConnection(connId);
        sceHttpDeleteTemplate(templateId);
        return false;
    }
    
    int totalRead = 0;
    while (totalRead < size) {
        int r = sceHttpReadData(reqId, (char*)buffer + totalRead, size - totalRead);
        if (r <= 0) break;
        totalRead += r;
    }
    
    sceHttpDeleteRequest(reqId);
    sceHttpDeleteConnection(connId);
    sceHttpDeleteTemplate(templateId);
    
    return (totalRead == size);
}

// Download file from URL to local path
bool HttpHelper_DownloadFile(const char* url, const char* localPath) {
    if (!s_initialized) {
        if (HttpHelper_Init() < 0) return false;
    }
    
    int templateId = sceHttpCreateTemplate(s_httpPoolId, "STORM/1.0", 1, 0);
    if (templateId < 0) return false;
    
    int connId = sceHttpCreateConnectionWithURL(templateId, url, 0);
    if (connId < 0) {
        sceHttpDeleteTemplate(templateId);
        return false;
    }
    
    int reqId = sceHttpCreateRequestWithURL(connId, ORBIS_METHOD_GET, url, 0);
    if (reqId < 0) {
        sceHttpDeleteConnection(connId);
        sceHttpDeleteTemplate(templateId);
        return false;
    }
    
    int ret = sceHttpSendRequest(reqId, NULL, 0);
    if (ret < 0) {
        sceHttpDeleteRequest(reqId);
        sceHttpDeleteConnection(connId);
        sceHttpDeleteTemplate(templateId);
        return false;
    }
    
    int fd = open(localPath, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) {
        sceHttpDeleteRequest(reqId);
        sceHttpDeleteConnection(connId);
        sceHttpDeleteTemplate(templateId);
        return false;
    }
    
    // Increase buffer to 128KB for speed
    size_t bufSize = 128 * 1024;
    char* buf = (char*)malloc(bufSize);
    if (!buf) {
        // Fallback to small stack buffer if malloc fails
        char smallBuf[4096];
        while (1) {
            int r = sceHttpReadData(reqId, smallBuf, sizeof(smallBuf));
            if (r <= 0) break;
            write(fd, smallBuf, r);
        }
    } else {
        while (1) {
            int r = sceHttpReadData(reqId, buf, bufSize);
            if (r <= 0) break;
            write(fd, buf, r);
        }
        free(buf);
    }
    
    close(fd);
    sceHttpDeleteRequest(reqId);
    sceHttpDeleteConnection(connId);
    sceHttpDeleteTemplate(templateId);
    
    return true;
}
