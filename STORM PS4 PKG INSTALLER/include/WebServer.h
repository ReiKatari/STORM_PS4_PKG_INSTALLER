#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "Installer.h"

// Web Server module - runs in main loop
// Handles all HTTP API endpoints

// Set installer reference (call before Start)
void WebServer_SetInstaller(Installer* inst);

// Initialize and start web server on given ports:
// port: 12813 (STORM API)
// rpiPort: 12800 (standard flatz RPI API)
// pkgFlowPort: 12801 (PackageFlow / playstation_installer API)
// Returns 0 on success, negative error code if all failed
int WebServer_Start(int port = 12813, int rpiPort = 12800, int pkgFlowPort = 12801);

// Get ports
int WebServer_GetPrimaryPort();
int WebServer_GetRpiPort();
int WebServer_GetPkgFlowPort();
bool WebServer_IsRpiPortActive();
bool WebServer_IsPkgFlowPortActive();

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
