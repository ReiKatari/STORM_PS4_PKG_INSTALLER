#include <orbis/libkernel.h>

int main() {
    // Hello World Logic
    // No visuals, just sleep to keep process alive.
    // If we get here, SDK is good.
    
    while (1) {
        sceKernelUsleep(1000000);
    }
    return 0;
}
