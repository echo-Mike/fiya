#include "fiya-recorder.h"

#if defined(FIYA_NO_INTERNAL_PROTECT)
#define FIYA_PROTECT_EXPR(...)
#else
#define FIYA_PROTECT_EXPR(...) __VA_ARGS__
#endif

#if defined(FIYA_LD_PRELOAD)
#define FIYA_INITIAL_EXEC __attribute__((tls_model("initial-exec")))
#else
#define FIYA_INITIAL_EXEC
#endif

/** You will need to define this function in your code. It must return a
 * thread_local object in multithreaded program; otherwise it can create race
 * conditions.
 */
extern fiya::scoping_interface_t<void*>* get_recorder();

/** Once cyg profiling starts, we set this flag to true.
 *  This is because we want to avoid recursive calls to __cyg_profile-*
 *  functions.
 */
FIYA_PROTECT_EXPR(static thread_local bool cyg_profiling_ongoing FIYA_INITIAL_EXEC = false;)

extern "C" {

/** These two functions get called everytime you enter a function, if
 * -finstrument-functions is passed at the compile line.
 */
void __cyg_profile_func_enter(void*, void*) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void*, void*) __attribute__((no_instrument_function));

void __cyg_profile_func_enter(void* this_fn, void* call_site) {
#ifndef FIYA_NO_INTERNAL_PROTECT
    if (!cyg_profiling_ongoing) {
        cyg_profiling_ongoing = true;
#endif
        fiya::scoping_interface_t<void*>* my_recorder = get_recorder();
        if (my_recorder FIYA_PROTECT_EXPR(&&!my_recorder->recorder_internal_running())) {
            my_recorder->begin_scope(this_fn);
        }
#ifndef FIYA_NO_INTERNAL_PROTECT
        cyg_profiling_ongoing = false;
    }
#endif
}

void __cyg_profile_func_exit(void* this_fn, void* call_site) {
#ifndef FIYA_NO_INTERNAL_PROTECT
    if (!cyg_profiling_ongoing) {
        cyg_profiling_ongoing = true;
#endif
        fiya::scoping_interface_t<void*>* my_recorder = get_recorder();
        if (my_recorder FIYA_PROTECT_EXPR(&&!my_recorder->recorder_internal_running())) {
            my_recorder->end_scope(this_fn);
        }
#ifndef FIYA_NO_INTERNAL_PROTECT
        cyg_profiling_ongoing = false;
    }
#endif
}
}