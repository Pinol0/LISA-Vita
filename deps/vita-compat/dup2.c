#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

int dup2(int oldfd, int newfd)
{
    int fd;

    if (oldfd < 0 || newfd < 0) {
        errno = EBADF;
        return -1;
    }

    /*
     * POSIX: se sono uguali, dup2 deve verificare che
     * oldfd sia valido e poi restituirlo senza chiuderlo.
     */
    if (oldfd == newfd) {
        if (fcntl(oldfd, F_GETFD) == -1)
            return -1;

        return newfd;
    }

    /*
     * Verifica oldfd PRIMA di toccare newfd.
     */
    if (fcntl(oldfd, F_GETFD) == -1)
        return -1;

    close(newfd);

    /*
     * F_DUPFD restituisce il primo descriptor libero >= newfd.
     * Avendo appena liberato newfd, dovrebbe essere proprio newfd.
     */
    fd = fcntl(oldfd, F_DUPFD, newfd);

    if (fd < 0)
        return -1;

    if (fd != newfd) {
        close(fd);
        errno = EMFILE;
        return -1;
    }

    return fd;
}
