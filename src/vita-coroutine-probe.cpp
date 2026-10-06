#include <cstddef>
#include "vita_paths.h"
#include <cstdio>

/* Observation only: each wrapper logs its first call, then forwards unchanged. */

static void vitaProbeLog(const char *marker)
{
    FILE *f = fopen(VITA_GAME_ROOT "qa.log", "a");
    if (f) { fprintf(f, "%s\n", marker); fclose(f); }
}

extern "C" {

void __real_coroutine_initialize_main(void *context);
void __real_coroutine_initialize(void *context, void *start, void *stack, size_t size);
void *__real_coroutine_transfer(void *current, void *target);

void __wrap_coroutine_initialize_main(void *context)
{
    static bool logged = false;
    if (!logged) { logged = true; vitaProbeLog("COROUTINE_INIT_MAIN_FIRST"); }
    __real_coroutine_initialize_main(context);
}

void __wrap_coroutine_initialize(void *context, void *start, void *stack, size_t size)
{
    static bool logged = false;
    if (!logged) { logged = true; vitaProbeLog("COROUTINE_FIBER_INIT_FIRST"); }
    __real_coroutine_initialize(context, start, stack, size);
}

void *__wrap_coroutine_transfer(void *current, void *target)
{
    static bool logged = false;
    if (!logged) { logged = true; vitaProbeLog("COROUTINE_TRANSFER_FIRST"); }
    return __real_coroutine_transfer(current, target);
}

}
