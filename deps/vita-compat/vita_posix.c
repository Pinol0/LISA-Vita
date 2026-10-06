#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <signal.h>
#include <errno.h>

/*
 * Minimal POSIX compatibility layer for MRI Ruby on PS Vita.
 *
 * These functions represent Unix process/user concepts which don't
 * have direct equivalents in the Vita homebrew environment.
 */

int getpagesize(void)
{
    return 4096;
}

mode_t umask(mode_t mask)
{
    static mode_t current_mask = 022;
    mode_t old_mask = current_mask;

    current_mask = mask;
    return old_mask;
}

struct passwd *getpwnam(const char *name)
{
    (void)name;
    errno = ENOENT;
    return NULL;
}

void endpwent(void)
{
}

pid_t getppid(void)
{
    return 0;
}

pid_t getpgrp(void)
{
    return getpid();
}

int execv(const char *path, char *const argv[])
{
    (void)path;
    (void)argv;
    errno = ENOSYS;
    return -1;
}

int execl(const char *path, const char *arg, ...)
{
    (void)path;
    (void)arg;
    errno = ENOSYS;
    return -1;
}

int execle(const char *path, const char *arg, ...)
{
    (void)path;
    (void)arg;
    errno = ENOSYS;
    return -1;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    (void)how;
    (void)set;

    if (oldset != NULL) {
        sigemptyset(oldset);
    }

    return 0;
}
