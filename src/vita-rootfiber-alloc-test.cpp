#include <cstddef>
#include <cstring>

/* Diagnostic only: with the arm32 libruby, make the root fiber allocation match the pthread
 * libruby (sizeof(rb_fiber_t) 300 -> 324 bytes, fully zeroed). Every other request is unchanged. */

extern "C" {

void *__real_ruby_mimmalloc(size_t size);

void *__wrap_ruby_mimmalloc(size_t size)
{
    /* 300 is only requested by rb_threadptr_root_fiber_setup() (vm/thread/ractor use 4552/152/244). */
    if (size == 300) {
        void *p = __real_ruby_mimmalloc(324);
        if (p)
            memset(p, 0, 324);
        return p;
    }
    return __real_ruby_mimmalloc(size);
}

}
