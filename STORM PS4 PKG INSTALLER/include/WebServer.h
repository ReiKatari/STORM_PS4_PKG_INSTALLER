#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "Installer.h"

// Web Server module - runs in main loop
// Handles all HTTP API endpoints

// Set installer reference (call before Start)
void WebServer_SetInstaller(Installer* inst);

// Initialize and start web server on given port
// Returns 0 on success, negative error code on failure
int WebServer_Start(int port);

// Stop web server
void WebServer_Stop();

// Check if server is running
bool WebServer_IsRunning();

// Process one request (call from main loop)
void WebServer_Process();

// Get last error message
const char* WebServer_GetLastError();

// API response helpers
void WebServer_SendSuccess(int clientSocket, const char* jsonBody);
void WebServer_SendError(int clientSocket, int errorCode, const char* message);

#endif // WEBSERVER_H
