// STORM PS4 PKG INSTALLER - Modern Architecture
#include "../include/Graphics.h"
#include "../include/App.h"
#include <unistd.h>
#include <orbis/Sysmodule.h>
#include <orbis/libkernel.h>

int main() {
    // 1. Load Graphics Modules
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_VIDEO_OUT);
    
    // 2. Load Helper PRXs
    sceKernelLoadStartModule("/app0/sce_module/libc.prx", 0, NULL, 0, NULL, NULL);
    sceKernelLoadStartModule("/app0/sce_module/libjbc.prx", 0, NULL, 0, NULL, NULL);
    sceKernelLoadStartModule("/app0/sce_module/libSceFios2.prx", 0, NULL, 0, NULL, NULL);

    // 3. Init Graphics
    Scene2D* scene = new Scene2D(FRAME_WIDTH, FRAME_HEIGHT, 4);
    if (!scene->Init(0xC000000, 2)) {
        return -1;
    }

    // 4. Init App (Logic, Network, Installer)
    App* app = App::Instance();
    app->Init();

    // 5. Initial Popup
    app->Notify("System Ready", {0, 255, 0, 255}, 5);

    // 6. Main Loop
    int frame = 0;
    while (true) {
        // Update Logic
        app->Update();

        // Render functions
        scene->FrameBufferClear();
        
        // Draw Main UI
        app->Draw(scene);
        
        scene->SubmitFlip(frame);
        scene->FrameWait(frame);
        scene->FrameBufferSwap();
        frame++;
    }

    app->Term();
    return 0;
}
