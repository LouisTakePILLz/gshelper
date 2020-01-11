#define _GNU_SOURCE
#include <sys/mman.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <stdio.h>

#ifdef DEBUG
#define DEBUG_BREAK asm("int3")
#else
#define DEBUG_BREAK
#endif

#define LoadSymbol(var_name, mangled_name, demangled_name) {\
    var_name = dlsym(RTLD_DEFAULT, mangled_name);\
    if (var_name == NULL) fail_load_symbol(demangled_name);\
}

#define get_page_start(addr) ((void*)(((unsigned long long)(addr)) & 0xFFFFFFFFFFFFF000))

static void** GEngine;
static unsigned int* GIsRunning;
static unsigned int* GIsRequestingExit;

static int (*UServerCommandlet_Main) (void* this, void* params);
static void (*appRequestExit) (unsigned int force);

void fail_load_symbol(char* symbol) {
    fprintf(stderr, "Failed locating symbol: %s\n", symbol);
    exit(1);
}

void term_handler(int signum) {
    fprintf(stderr, "Caught signal: %i\n", signum);
    fprintf(stderr, "GIsRunning: %u\n", *GIsRunning);

    appRequestExit(0);
}

void segfault_handler(int signum) {
    fprintf(stderr, "Caught segfault: %i\n", signum);
    exit(128 + signum);
}

int patch_server_commandlet() {
    int ret;

    ret = mprotect(get_page_start(UServerCommandlet_Main), 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC);
    if (ret != 0) {
        perror("mprotect error");
        return ret;
    }

    // NOP 'GIsRequestingExit = 0'
    memset(UServerCommandlet_Main + 0x15, 0x90, 10);

    ret = mprotect(get_page_start(UServerCommandlet_Main), 0x2000, PROT_READ | PROT_EXEC);
    if (ret != 0) {
        perror("mprotect restore error");
        return ret;
    }

    return 0;
}

int error_stub_main(int argc, char** argv, char** envp) {
    return 1;
}

__attribute__ ((visibility ("default")))
int __libc_start_main(
    int (*main) (int, char**, char**),
    int argc,
    char** argv,
    void (*init) (int, char**, char**),
    void (*fini) (void),
    void (*rtld_fini) (void),
    void* stack_end
) {
    typeof(&__libc_start_main) orig = dlsym(RTLD_NEXT, "__libc_start_main");

    struct sigaction action;
    action.sa_handler = term_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    sigaction(SIGINT, &action, NULL);
    sigaction(SIGHUP, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    action.sa_handler = segfault_handler;
    sigaction(SIGSEGV, &action, NULL);

    // Member functions
    LoadSymbol(UServerCommandlet_Main, "_ZN17UServerCommandlet4MainERK7FString", "UServerCommandlet::Main(const FString*)");

    // Global functions
    LoadSymbol(appRequestExit, "_Z14appRequestExitj", "appRequestExit(unsigned int)");

    // Global variables
    LoadSymbol(GEngine, "GEngine", "GEngine");
    LoadSymbol(GIsRunning, "GIsRunning", "GIsRunning");
    LoadSymbol(GIsRequestingExit, "GIsRequestingExit", "GIsRequestingExit");

    // Patch UServerCommandlet::Main() so that it doesn't reset GIsRequestingExit
    if (patch_server_commandlet() != 0) {
        return orig(&error_stub_main, argc, argv, init, fini, rtld_fini, stack_end);
    }

    // Sanitize the unitialized data
    *GIsRequestingExit = 0;

    return orig(main, argc, argv, init, fini, rtld_fini, stack_end);
}
