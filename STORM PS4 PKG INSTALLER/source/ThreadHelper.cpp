#include "../include/ThreadHelper.h"
#include <orbis/libkernel.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

// Thread storage
#define MAX_THREADS 8
static pthread_t s_threads[MAX_THREADS];
static bool s_threadUsed[MAX_THREADS];
static int s_threadCount = 0;

// Mutex storage
#define MAX_MUTEXES 16
static pthread_mutex_t s_mutexes[MAX_MUTEXES];
static bool s_mutexUsed[MAX_MUTEXES];

int Thread_Create(ThreadFunc func, void* arg, const char* name) {
    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (!s_threadUsed[i]) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;
    
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    // Increase stack size to 512KB (default might be too small for heavy operations)
    pthread_attr_setstacksize(&attr, 512 * 1024);
    
    int ret = pthread_create(&s_threads[slot], &attr, func, arg);
    pthread_attr_destroy(&attr);
    
    if (ret == 0) {
        s_threadUsed[slot] = true;
        s_threadCount++;
        return slot;
    }
    
    return -1;
}

int Thread_Join(int threadId) {
    if (threadId < 0 || threadId >= MAX_THREADS || !s_threadUsed[threadId]) {
        return -1;
    }
    
    void* retval;
    int ret = pthread_join(s_threads[threadId], &retval);
    
    if (ret == 0) {
        s_threadUsed[threadId] = false;
        s_threadCount--;
    }
    
    return ret;
}

void Thread_Sleep(int milliseconds) {
    sceKernelUsleep(milliseconds * 1000);
}

int Thread_GetCurrentId() {
    pthread_t self = pthread_self();
    for (int i = 0; i < MAX_THREADS; i++) {
        if (s_threadUsed[i] && pthread_equal(s_threads[i], self)) {
            return i;
        }
    }
    return -1;
}

MutexHandle Mutex_Create() {
    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_MUTEXES; i++) {
        if (!s_mutexUsed[i]) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;
    
    if (pthread_mutex_init(&s_mutexes[slot], NULL) == 0) {
        s_mutexUsed[slot] = true;
        return slot;
    }
    
    return -1;
}

void Mutex_Lock(MutexHandle mutex) {
    if (mutex >= 0 && mutex < MAX_MUTEXES && s_mutexUsed[mutex]) {
        pthread_mutex_lock(&s_mutexes[mutex]);
    }
}

void Mutex_Unlock(MutexHandle mutex) {
    if (mutex >= 0 && mutex < MAX_MUTEXES && s_mutexUsed[mutex]) {
        pthread_mutex_unlock(&s_mutexes[mutex]);
    }
}

void Mutex_Destroy(MutexHandle mutex) {
    if (mutex >= 0 && mutex < MAX_MUTEXES && s_mutexUsed[mutex]) {
        pthread_mutex_destroy(&s_mutexes[mutex]);
        s_mutexUsed[mutex] = false;
    }
}
