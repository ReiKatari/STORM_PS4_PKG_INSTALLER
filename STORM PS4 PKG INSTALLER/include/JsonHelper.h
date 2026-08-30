#ifndef JSONHELPER_H
#define JSONHELPER_H

#include <stdint.h>

// Initialize JSON helper (loads libSceJson)
int JsonHelper_Init();

// Parse JSON string and extract values
// Returns true on success
bool JsonHelper_Parse(const char* jsonStr);

// Get string value by key
// Returns NULL if not found
const char* JsonHelper_GetString(const char* key);

// Get int value by key
// Returns defaultValue if not found
int JsonHelper_GetInt(const char* key, int defaultValue);

// Get bool value by key
bool JsonHelper_GetBool(const char* key, bool defaultValue);

// Free parsed JSON
void JsonHelper_Free();

// Build JSON response string
int JsonHelper_BuildSuccess(char* outBuffer, int maxLen, const char* extra);
int JsonHelper_BuildError(char* outBuffer, int maxLen, int errorCode, const char* message);

#endif // JSONHELPER_H
