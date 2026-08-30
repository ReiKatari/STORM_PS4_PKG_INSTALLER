// libjbc.h - PS4 Jailbreak Library Header
// Dynamic loading version - functions loaded from libjbc.prx at runtime
#pragma once

#include <stdint.h>
#include <orbis/libkernel.h>

#ifdef __cplusplus
extern "C" {
#endif

// Credential structure for jailbreak
struct jbc_cred {
    uint64_t uid;
    uint64_t rgid;
    uint64_t svuid;
    uint64_t svgid;
    uint64_t cr_groups[16];
    uint64_t cr_ngroups;
    uint64_t cr_prison;
    uint64_t authid;
    uint64_t caps0;
    uint64_t caps1;
    uint64_t attrs[4];
};

// Function pointer types
typedef int (*jbc_get_cred_t)(struct jbc_cred*);
typedef int (*jbc_jailbreak_cred_t)(struct jbc_cred*);
typedef int (*jbc_set_cred_t)(const struct jbc_cred*);

// Global function pointers (initialized by jbc_init)
extern jbc_get_cred_t jbc_get_cred;
extern jbc_jailbreak_cred_t jbc_jailbreak_cred;
extern jbc_set_cred_t jbc_set_cred;

// Initialize libjbc (loads the module and resolves symbols)
int jbc_init();

#ifdef __cplusplus
}
#endif
