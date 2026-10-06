#include <malloc.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <errno.h>

#define VITA_PAGE_SIZE 4096

void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset)
{
    void *ptr = NULL;

    (void)addr;
    (void)prot;
    (void)flags;
    (void)fd;
    (void)offset;

    if (length == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    if (fd != -1) {
        errno = ENOTSUP;
        return MAP_FAILED;
    }

    ptr = memalign(VITA_PAGE_SIZE, length);

    if (ptr == NULL) {
        errno = ENOMEM;
        return MAP_FAILED;
    }

    return ptr;
}

int munmap(void *addr, size_t length)
{
    (void)length;

    if (addr == NULL || addr == MAP_FAILED) {
        errno = EINVAL;
        return -1;
    }

    free(addr);
    return 0;
}

int mprotect(void *addr, size_t length, int prot)
{
    (void)addr;
    (void)length;
    (void)prot;

    /*
     * Vita compatibility implementation.
     *
     * Ruby uses this to create a PROT_NONE guard page for Fiber stacks.
     * Vita does not provide POSIX mprotect(), so for now the guard page
     * is intentionally not enforced.
     */
    return 0;
}
