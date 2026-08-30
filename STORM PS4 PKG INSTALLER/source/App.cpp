#include "../include/App.h"
#include "../include/WebServer.h"
#include <orbis/NetCtl.h>
#include <orbis/libkernel.h>
#include <stdio.h>
#include <string.h>

static App* s_instance = nullptr;

App* App::Instance() {
    if (!s_instance) s_instance = new App();
    return s_instance;
}

App::App() : installer(nullptr), webServer(nullptr) {
    pthread_mutex_init(&notifMutex, NULL);
    ipAddress = "Unknown";
}

App::~App() {
    Term();
    pthread_mutex_destroy(&notifMutex);
}

bool App::Init() {
    // 1. Get IP
    OrbisNetCtlInfo info;
    if (sceNetCtlInit() >= 0) {
        if (sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS, &info) >= 0) {
            ipAddress = info.ip_address;
        }
    }

    // 2. Init Installer
    installer = new Installer();
    if (!installer->Init()) {
        Notify("ERROR: Installer Init Failed!", {255, 0, 0, 255});
    }

    // 3. Init WebServer
    webServer = new WebServer(12813);
    if (!webServer->Start()) {
        Notify("ERROR: WebServer Start Failed!", {255, 0, 0, 255});
    } else {
        Notify("Server Started: " + ipAddress + ":12813", {0, 255, 0, 255});
    }

    return true;
}

void App::Term() {
    if (webServer) {
        webServer->Stop();
        delete webServer;
        webServer = nullptr;
    }
    if (installer) {
        delete installer;
        installer = nullptr;
    }
}

void App::Notify(const std::string& msg, Color color, int durationSec) {
    pthread_mutex_lock(&notifMutex);
    Notification n;
    n.message = msg;
    n.color = color;
    n.durationFrames = durationSec * 60;
    n.maxDuration = n.durationFrames;
    notifications.push_back(n);
    pthread_mutex_unlock(&notifMutex);
}

void App::Update() {
    // Update Notifications
    pthread_mutex_lock(&notifMutex);
    for (auto it = notifications.begin(); it != notifications.end();) {
        it->durationFrames--;
        if (it->durationFrames <= 0) {
            it = notifications.erase(it);
        } else {
            ++it;
        }
    }
    pthread_mutex_unlock(&notifMutex);
}

void App::Draw(Scene2D* scene) {
    if (!scene) return;

    // Draw Status Bar
    Color dark = {20, 20, 20, 200};
    Color white = {255, 255, 255, 255};
    scene->DrawRectangle(0, 0, 1920, 60, dark);
    
    char status[256];
    snprintf(status, sizeof(status), "STORM INSTALLER | IP: %s:%d | Tasks: %d", 
        ipAddress.c_str(), 12813, installer ? installer->GetActiveTaskCount() : 0);
    scene->DrawText(status, 50, 20, white);

    // Draw Tasks
    if (installer) {
        InstallTask tasks[MAX_TASKS];
        int count = installer->GetTasks(tasks, MAX_TASKS);
        int y = 100;
        
        for (int i = 0; i < count; i++) {
            Color barColor = {0, 200, 0, 255};
            if (strcmp(tasks[i].status, "Error") == 0) barColor = {200, 0, 0, 255};
            
            char line[256];
            snprintf(line, sizeof(line), "[%d] %s (%.1f%%) - %s", 
                tasks[i].taskId, tasks[i].title, tasks[i].progress * 100.0f, tasks[i].status);
            
            scene->DrawText(line, 100, y, white);
            scene->DrawRectangle(100, y + 30, 800, 10, {50, 50, 50, 255});
            scene->DrawRectangle(100, y + 30, (int)(800 * tasks[i].progress), 10, barColor);
            
            y += 70;
        }
        
        if (count == 0) {
            scene->DrawText("Waiting for packages...", 100, 100, {200, 200, 200, 255});
        }
    }

    // Draw Notifications (Bottom Left Toast)
    pthread_mutex_lock(&notifMutex);
    int ny = 1000;
    for (const auto& n : notifications) {
        // Fade out
        int alpha = 255;
        if (n.durationFrames < 30) alpha = (n.durationFrames * 255) / 30;
        
        Color bg = {0, 0, 0, (uint8_t)(alpha > 200 ? 200 : alpha)};
        Color textC = n.color;
        textC.a = (uint8_t)alpha;
        
        // Simple approx width calc
        int width = n.message.length() * 15 + 40;
        
        scene->DrawRectangle(50, ny, width, 50, bg);
        scene->DrawText(const_cast<char*>(n.message.c_str()), 70, ny + 15, textC);
        ny -= 60;
    }
    pthread_mutex_unlock(&notifMutex);
}
