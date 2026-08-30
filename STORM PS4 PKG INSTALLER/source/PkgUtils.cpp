#include "../include/PkgUtils.h"
#include "../include/HttpHelper.h"
#include "../include/Installer.h"
#include <orbis/libkernel.h>
#include <stdio.h>
#include "../include/Common.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define BE32(x) __builtin_bswap32(x)

struct PkgHeader {
    uint32_t magic;
    uint32_t type;
    uint32_t unk;
    uint32_t file_count;
    uint32_t entry_count;
    uint32_t sc_entry_count;
    uint32_t entry_table_offset;
    uint32_t main_ent_data_size;
    uint64_t body_offset;
    uint32_t body_size;
    uint32_t content_id_offset;
};

struct PkgEntry {
    uint32_t id;
    uint32_t filename_offset;
    uint32_t flags1;
    uint32_t flags2;
    uint32_t offset;
    uint32_t size;
    uint64_t padding;
};

struct SfoHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t key_table_start;
    uint32_t data_table_start;
    uint32_t entries_count;
};

struct SfoEntry {
    uint16_t key_offset;
    uint16_t param_fmt;
    uint32_t param_len;
    uint32_t param_max_len;
    uint32_t data_offset;
};

bool PkgUtils::GetTitleAndIcon(const char* url, char* outTitle, int titleLen, char* outIconPath, int iconPathLen, char* outTitleId, int titleIdLen, char* outCategory, int categoryLen, char* outContentId, int contentIdLen, int taskId) {
    Log("PkgUtils: Parsing %s", url);
    
    // Read Header
    uint8_t headerBuf[0x200];
    if (!HttpHelper_DownloadRange(url, 0, sizeof(headerBuf), headerBuf)) {
        Log("PkgUtils: Failed to read header");
        return false;
    }
    
    PkgHeader* hdr = (PkgHeader*)headerBuf;
    if (BE32(hdr->magic) != 0x7F434E54) {
        Log("PkgUtils: Invalid magic");
        return false;
    }
    
    // Extract Content ID from Header
    if (outContentId && contentIdLen > 0) {
        char* cidPtr = (char*)headerBuf + 0x40; // Offset 0x40
        int avail = 36;
        if (avail > contentIdLen - 1) avail = contentIdLen - 1;
        strncpy(outContentId, cidPtr, avail);
        outContentId[avail] = 0;
        Log("PkgUtils: ContentID from Header: %s", outContentId);
    }
    
    uint32_t entryCount = BE32(hdr->entry_count);
    uint32_t tableOffset = BE32(hdr->entry_table_offset);
    
    if (entryCount > 2000) entryCount = 2000;
    
    // Read Entry Table
    size_t tableSize = entryCount * sizeof(PkgEntry);
    uint8_t* tableBuf = (uint8_t*)malloc(tableSize);
    if (!tableBuf) return false;
    
    if (!HttpHelper_DownloadRange(url, tableOffset, tableSize, tableBuf)) {
        Log("PkgUtils: Failed to read table");
        free(tableBuf);
        return false;
    }
    
    PkgEntry* entries = (PkgEntry*)tableBuf;
    
    uint32_t sfoOffset = 0, sfoSize = 0;
    uint32_t iconOffset = 0, iconSize = 0;
    
    for (uint32_t i = 0; i < entryCount; i++) {
        uint32_t id = BE32(entries[i].id);
        if (id == 0x1000) {
            sfoOffset = BE32(entries[i].offset);
            sfoSize = BE32(entries[i].size);
        } else if (id == 0x1200) {
            iconOffset = BE32(entries[i].offset);
            iconSize = BE32(entries[i].size);
        }
    }
    
    free(tableBuf);
    
    bool success = false;
    
    // Parse SFO
    if (sfoOffset > 0 && sfoSize > 0 && outTitle) {
        uint8_t* sfoData = (uint8_t*)malloc(sfoSize);
        if (sfoData) {
            if (HttpHelper_DownloadRange(url, sfoOffset, sfoSize, sfoData)) {
                if (ParseSfo(sfoData, sfoSize, outTitle, titleLen, outTitleId, titleIdLen, outCategory, categoryLen)) {
                    success = true;
                    Log("PkgUtils: Title found: %s", outTitle);
                }
            }
            free(sfoData);
        }
    }
    
    // Download Icon
    if (iconOffset > 0 && iconSize > 0 && iconSize < 5*1024*1024 && outIconPath) {
        // Use a writable directory! /data/app/ is likely sandboxed/read-only.
        const char* tempDir = "/user/app/sppi_temp";
        mkdir(tempDir, 0777);
        
        snprintf(outIconPath, iconPathLen, "%s/icon_%d.png", tempDir, taskId);
        
        uint8_t* iconData = (uint8_t*)malloc(iconSize);
        if (iconData) {
            if (HttpHelper_DownloadRange(url, iconOffset, iconSize, iconData)) {
                int fd = open(outIconPath, O_WRONLY | O_CREAT | O_TRUNC, 0777);
                if (fd >= 0) {
                    write(fd, iconData, iconSize);
                    close(fd);
                    Log("PkgUtils: Icon saved to %s", outIconPath);
                } else {
                    Log("PkgUtils: Failed to open icon file %s", outIconPath);
                    outIconPath[0] = 0;
                }
            } else {
                Log("PkgUtils: Failed to download icon data");
                outIconPath[0] = 0;
            }
            free(iconData);
        }
    }
    
    return success;
}

bool PkgUtils::ParseSfo(const uint8_t* data, size_t size, char* outTitle, int titleLen, char* outTitleId, int titleIdLen, char* outCategory, int categoryLen) {
    if (size < sizeof(SfoHeader)) return false;
    
    SfoHeader* hdr = (SfoHeader*)data;
    if (hdr->magic != 0x46535000) return false;
    
    uint32_t keyTableStart = hdr->key_table_start;
    uint32_t dataTableStart = hdr->data_table_start;
    uint32_t entriesCount = hdr->entries_count;
    
    if (entriesCount > 100) entriesCount = 100;
    
    SfoEntry* entries = (SfoEntry*)(data + sizeof(SfoHeader));
    
    for (uint32_t i = 0; i < entriesCount; i++) {
        if (keyTableStart + entries[i].key_offset >= size) continue;
        
        const char* key = (const char*)(data + keyTableStart + entries[i].key_offset);
        const char* val = (const char*)(data + dataTableStart + entries[i].data_offset);
        
        if (strcmp(key, "TITLE") == 0 && outTitle) {
            int len = entries[i].param_len;
            if (len > titleLen - 1) len = titleLen - 1;
            strncpy(outTitle, val, len);
            outTitle[len] = 0;
        }
        else if (strcmp(key, "TITLE_ID") == 0 && outTitleId) {
            int len = entries[i].param_len;
            if (len > titleIdLen - 1) len = titleIdLen - 1;
            strncpy(outTitleId, val, len);
            outTitleId[len] = 0;
        }
        else if (strcmp(key, "CATEGORY") == 0 && outCategory) {
            int len = entries[i].param_len;
            if (len > categoryLen - 1) len = categoryLen - 1;
            strncpy(outCategory, val, len);
            outCategory[len] = 0;
        }
    }
    
    return true;
}
