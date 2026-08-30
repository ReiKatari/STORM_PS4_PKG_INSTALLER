#include "../include/JsonHelper.h"
#include <orbis/Sysmodule.h>
#include <orbis/_types/sysmodule.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Simple JSON parser (no libSceJson dependency - more compatible)
// Parses basic JSON objects with string/int/bool values

#define MAX_JSON_KEYS 32
#define MAX_KEY_LEN 64
#define MAX_VALUE_LEN 512

static struct {
    char key[MAX_KEY_LEN];
    char value[MAX_VALUE_LEN];
    int type; // 0=string, 1=int, 2=bool
} s_jsonData[MAX_JSON_KEYS];

static int s_jsonCount = 0;

int JsonHelper_Init() {
    s_jsonCount = 0;
    return 0;
}

static void TrimQuotes(char* str) {
    int len = strlen(str);
    if (len >= 2) {
        if (str[0] == '"') {
            memmove(str, str + 1, len);
            len--;
        }
        if (len > 0 && str[len-1] == '"') {
            str[len-1] = '\0';
        }
    }
}

bool JsonHelper_Parse(const char* jsonStr) {
    s_jsonCount = 0;
    if (!jsonStr) return false;
    
    // Skip whitespace and opening brace
    const char* p = jsonStr;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '{')) p++;
    
    while (*p && *p != '}' && s_jsonCount < MAX_JSON_KEYS) {
        // Skip whitespace
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) p++;
        if (*p == '}' || !*p) break;
        
        // Parse key
        if (*p != '"') break;
        p++; // skip opening quote
        
        char* keyDst = s_jsonData[s_jsonCount].key;
        int keyLen = 0;
        while (*p && *p != '"' && keyLen < MAX_KEY_LEN - 1) {
            keyDst[keyLen++] = *p++;
        }
        keyDst[keyLen] = '\0';
        
        if (*p == '"') p++; // skip closing quote
        
        // Skip colon
        while (*p && *p != ':') p++;
        if (*p == ':') p++;
        while (*p && (*p == ' ' || *p == '\t')) p++;
        
        // Parse value
        char* valDst = s_jsonData[s_jsonCount].value;
        int valLen = 0;
        
        if (*p == '"') {
            // String value
            p++; // skip opening quote
            while (*p && *p != '"' && valLen < MAX_VALUE_LEN - 1) {
                valDst[valLen++] = *p++;
            }
            if (*p == '"') p++;
            s_jsonData[s_jsonCount].type = 0; // string
        } else if (*p == 't' || *p == 'f') {
            // Boolean
            if (strncmp(p, "true", 4) == 0) {
                strcpy(valDst, "1");
                p += 4;
            } else if (strncmp(p, "false", 5) == 0) {
                strcpy(valDst, "0");
                p += 5;
            }
            valLen = strlen(valDst);
            s_jsonData[s_jsonCount].type = 2; // bool
        } else {
            // Number
            while (*p && ((*p >= '0' && *p <= '9') || *p == '-' || *p == '.') && valLen < MAX_VALUE_LEN - 1) {
                valDst[valLen++] = *p++;
            }
            s_jsonData[s_jsonCount].type = 1; // int
        }
        valDst[valLen] = '\0';
        
        s_jsonCount++;
        
        // Skip to next comma or end
        while (*p && *p != ',' && *p != '}') p++;
    }
    
    return s_jsonCount > 0;
}

const char* JsonHelper_GetString(const char* key) {
    for (int i = 0; i < s_jsonCount; i++) {
        if (strcmp(s_jsonData[i].key, key) == 0) {
            return s_jsonData[i].value;
        }
    }
    return NULL;
}

int JsonHelper_GetInt(const char* key, int defaultValue) {
    const char* val = JsonHelper_GetString(key);
    if (val) return atoi(val);
    return defaultValue;
}

bool JsonHelper_GetBool(const char* key, bool defaultValue) {
    const char* val = JsonHelper_GetString(key);
    if (val) {
        return strcmp(val, "1") == 0 || strcmp(val, "true") == 0;
    }
    return defaultValue;
}

void JsonHelper_Free() {
    s_jsonCount = 0;
}

int JsonHelper_BuildSuccess(char* outBuffer, int maxLen, const char* extra) {
    if (extra && strlen(extra) > 0) {
        return snprintf(outBuffer, maxLen, "{\"status\":\"success\",%s}\n", extra);
    }
    return snprintf(outBuffer, maxLen, "{\"status\":\"success\"}\n");
}

int JsonHelper_BuildError(char* outBuffer, int maxLen, int errorCode, const char* message) {
    return snprintf(outBuffer, maxLen, 
        "{\"status\":\"fail\",\"error_code\":\"0x%08X\",\"error\":\"%s\"}\n", 
        errorCode, message ? message : "Unknown error");
}
