#ifndef THREADHELPER_H
#define THREADHELPER_H

#include <stdint.h>

// Thread function prototype
typedef void* (*ThreadFunc)(void* arg);

// Create a new thread
// Returns thread ID on success, -1 on failure
int Thread_Create(ThreadFunc func, void* arg, const char* name);

// Join thread (wait for completion)
int Thread_Join(int threadId);

// Sleep current thread
void Thread_Sleep(int milliseconds);

// Get current thread ID
int Thread_GetCurrentId();

// Mutex operations
typedef int MutexHandle;

MutexHandle Mutex_Create();
void Mutex_Lock(MutexHandle mutex);
void Mutex_Unlock(MutexHandle mutex);
void Mutex_Destroy(MutexHandle mutex);

#endif // THREADHELPER_H
