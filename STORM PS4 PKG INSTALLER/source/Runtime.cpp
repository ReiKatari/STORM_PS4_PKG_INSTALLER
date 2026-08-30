#include <stddef.h>
#include <stdlib.h>

// Minimal C++ runtime support for OpenOrbis when libc++ is missing

void* operator new(size_t size) {
    return malloc(size);
}

void* operator new[](size_t size) {
    return malloc(size);
}

void operator delete(void* ptr) noexcept {
    free(ptr);
}

void operator delete[](void* ptr) noexcept {
    free(ptr);
}

// Support for pure virtual functions
extern "C" void __cxa_pure_virtual() {
    while (1); // Trap
}

// Minimal dummy for assertion
extern "C" void __assert_fail(const char * assertion, const char * file, unsigned int line, const char * function) {
    while(1);
}

// Stubs for Scrt1.o / libc

extern "C" void _start_ps4_c(void) {
    // Called by Scrt1.o
}

extern "C" void _init(void) {
    // Dummy init
}

extern "C" void _fini(void) {
    // Dummy fini
}

// Scrt1.o calls this
extern "C" int __libc_start_main(
    int (*main)(int, char **, char **),
    int argc,
    char **argv,
    void (*init)(void),
    void (*fini)(void),
    void (*rtld_fini)(void),
    void *stack_end
) {
    // Invoke main function pointer passed by Scrt1.o
    // Passing 0 as third arg (envp)
    return main(argc, argv, 0);
}
