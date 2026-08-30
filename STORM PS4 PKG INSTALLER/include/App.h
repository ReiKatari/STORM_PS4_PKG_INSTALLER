#pragma once
#include "Common.h"
#include <vector>
#include <string>
#include <queue>
#include <pthread.h>
#include "Graphics.h"
#include "Installer.h"

// Simple Toast Notification Structure
struct Notification {
    std::string message;
    Color color;
    int durationFrames; // 60 = 1 sec
    int maxDuration;
};

class WebServer; // Forward declaration

class App {
public:
    static App* Instance();
    
    bool Init();
    void Update(); // Main Loop Update
    void Draw(Scene2D* scene);
    void Term();

    // Global Access
    void Notify(const std::string& msg, Color color = {255, 255, 255, 255}, int durationSec = 3);
    Installer* GetInstaller() { return installer; }
    
    // Status
    std::string GetIP() const { return ipAddress; }
    int GetPort() const { return 12813; }

private:
    App();
    ~App();
    
    // Systems
    Installer* installer;
    WebServer* webServer;
    
    // UI State
    std::vector<Notification> notifications;
    std::string ipAddress;
    
    // Threading
    pthread_mutex_t notifMutex;
};
