// libjbc.cpp - Dynamic loading of libjbc.prx
#include "../include/libjbc.h"
#include <orbis/libkernel.h>
#include <string.h>

// Global function pointers
jbc_get_cred_t jbc_get_cred = nullptr;
jbc_jailbreak_cred_t jbc_jailbreak_cred = nullptr;
jbc_set_cred_t jbc_set_cred = nullptr;

static int libjbc_handle = -1;

int jbc_init() {
    if (libjbc_handle >= 0) {
        return 0;  // Already loaded
    }
    
    // Try to load libjbc.prx from sce_module
    libjbc_handle = sceKernelLoadStartModule("/app0/sce_module/libjbc.prx", 0, nullptr, 0, nullptr, nullptr);
    
    if (libjbc_handle < 0) {
        return libjbc_handle;  // Failed to load
    }
    
    // Resolve symbols
    int ret;
    
    ret = sceKernelDlsym(libjbc_handle, "jbc_get_cred", (void**)&jbc_get_cred);
    if (ret < 0) return ret;
    
    ret = sceKernelDlsym(libjbc_handle, "jbc_jailbreak_cred", (void**)&jbc_jailbreak_cred);
    if (ret < 0) return ret;
    
    ret = sceKernelDlsym(libjbc_handle, "jbc_set_cred", (void**)&jbc_set_cred);
    if (ret < 0) return ret;
    
    return 0;
}
