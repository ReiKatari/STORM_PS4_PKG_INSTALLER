#ifndef HTTPHELPER_H
#define HTTPHELPER_H

#include <stdint.h>

// Isolated HTTP helper for downloading PKG headers
// This module is designed to NOT interfere with BGFT

// Initialize HTTP subsystem (call once at startup)
int HttpHelper_Init();

// Terminate HTTP subsystem (call before BGFT operations or at shutdown)
void HttpHelper_Term();

// Download partial content from URL
// Returns bytes read, or negative error code
// outFileSize will contain total file size if available
int HttpHelper_DownloadPartial(const char* url, void* buffer, int maxSize, int64_t* outFileSize);

// Download specific range. Returns true on success.
bool HttpHelper_DownloadRange(const char* url, int64_t offset, int size, void* buffer);

// Download file from URL to local path
bool HttpHelper_DownloadFile(const char* url, const char* localPath);

// Extract Content ID from PKG header (offset 0x40, 36 bytes)
// Returns true if extracted successfully
bool HttpHelper_ExtractPkgInfo(const char* url, char* outContentId, int idMaxLen, 
                                char* outTitle, int titleMaxLen, int64_t* outFileSize);

#endif // HTTPHELPER_H
