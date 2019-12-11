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

static void** GWorld;
static void** GEngine;
static unsigned int* GIsRunning;
static unsigned int* GIsRequestingExit;

//static void (*UEngine_PreExit) (void* this);
static void (*UNetConnection_Close) (void* this);
static void* (*UWorld_GetFirstController) (void* this);
static int (*UServerCommandlet_Main) (void* this, void* params);
static void (*appRequestExit) (unsigned int force);
//static void (*appPreExit) (void);
//static void (*appSocketExit) (void);
//static void (*EngineExit) (void);

void fail_load_symbol(char* symbol) {
    printf("Failed locating symbol: %s\n", symbol);
    exit(1);
}

int close_connections() {
    if (*GWorld == NULL) return -1;

    for (void* C = UWorld_GetFirstController(*GWorld); C != NULL; C = /* C->NextController */ *((void**)(C + 0x278))) {
        void** vtable = ((void***)C)[0];

        typedef void* (GetAPlayerController_t)(void*);
        void* PC = ((GetAPlayerController_t*)vtable[196])(C);
        if (PC == NULL) continue;

        void* Player = *((void**)(PC + 0x460));
        if (Player == NULL) continue;

        UNetConnection_Close(Player);
    }

    return 0;
}

void term_handler(int signum) {
    printf("Caught signal: %i\n", signum);
    printf("GIsRunning: %u\n", *GIsRunning);

    appRequestExit(0);

    /*if (*GIsRunning != 0) {
        appRequestExit(0);
    } else {
        *GIsRunning = 0;
        if (*GEngine != NULL) {
            UEngine_PreExit(*GEngine);
        }
        appPreExit();
        appSocketExit();
    }*/

    /*if (requestedExit != 0) {
        exit(128 + signum);
    } else {
        // Note: if we do decide to use this, we need to keep track
        // of whether UEngine::Init() was called.
        //appRequestExit(0);

        // Note: this causes a SEGSIEV due to nullptr deref.
        // GTaskPerfTracker->AddTask(const TCHAR*, const TCHAR*, FLOAT)
        EngineExit();
    }*/

    //exit(128 + signum);
}

int patch_server_commandlet() {
    int ret;

    ret = mprotect(get_page_start(UServerCommandlet_Main), 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC);
    if (ret != 0) {
        perror("mprotect error");
        return ret;
    }

    // Prevent UServerCommandlet::Main() from resetting 'GIsRequestingExit'
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

    LoadSymbol(UWorld_GetFirstController, "_ZNK6UWorld18GetFirstControllerEv", "UWorld::GetFirstController()");
    LoadSymbol(UNetConnection_Close, "_ZN14UNetConnection5CloseEv", "UNetConnection::Close()");
    LoadSymbol(UServerCommandlet_Main, "_ZN17UServerCommandlet4MainERK7FString", "UServerCommandlet::Main(const FString*)");
    //LoadSymbol(UEngine_PreExit, "_ZN7UEngine7PreExitEv", "UEngine::PreExit()");

    LoadSymbol(appRequestExit, "_Z14appRequestExitj", "appRequestExit(unsigned int)");
    //LoadSymbol(EngineExit, "_Z10EngineExitv", "EngineExit(void)");
    //LoadSymbol(appPreExit, "_Z10appPreExitv", "appPreExit()");
    //LoadSymbol(appSocketExit, "_Z13appSocketExitv", "appSocketExit()");

    LoadSymbol(GWorld, "GWorld", "GWorld");
    LoadSymbol(GEngine, "GEngine", "GEngine");
    LoadSymbol(GIsRunning, "GIsRunning", "GIsRunning");
    LoadSymbol(GIsRequestingExit, "GIsRequestingExit", "GIsRequestingExit");

    if (patch_server_commandlet() != 0) {
        return orig(&error_stub_main, argc, argv, init, fini, rtld_fini, stack_end);
    }

    // Sanitize the unitialized data
    *GIsRequestingExit = 0;

    return orig(main, argc, argv, init, fini, rtld_fini, stack_end);
}
