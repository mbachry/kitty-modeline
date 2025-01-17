#define _GNU_SOURCE
#include "util.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    char ttykey[64];
    int err = ttyname_r(STDOUT_FILENO, ttykey, sizeof(ttykey));
    if (err) {
        fprintf(stderr, "ttyname_r: %s\n", strerror(err));
        return 1;
    }
    escape_tty_key_inplace(ttykey);

    char payload[65536];
    if (fread(payload, sizeof(payload), 1, stdin) < 1 && ferror(stdin)) {
        fprintf(stderr, "fread: %m\n");
        return 1;
    }
    fclose(stdin);

    int sk = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sk < 0) {
        fprintf(stderr, "socket: %m\n");
        return 1;
    }

    struct sockaddr_un addr;
    size_t addr_len;
    get_socket_address(&addr, &addr_len, ttykey);

    if (sendto(sk, payload, strlen(payload), 0, &addr, addr_len) < 0) {
        char dir[4096];
        snprintf(dir, sizeof(dir), "/run/user/%u/kitty-modeline", getuid());
        int dirfd = open(dir, O_PATH | O_DIRECTORY);
        if (dirfd < 0) {
            if (mkdir(dir, 0700) < 0) {
                fprintf(stderr, "mkdir: %m\n");
                return 1;
            }
            dirfd = open(dir, O_PATH | O_DIRECTORY);
            if (dirfd < 0) {
                fprintf(stderr, "open: %m\n");
                return 1;
            }
        }

        char path[256];
        snprintf(path, sizeof(path), "shell-%s.tmp", ttykey);

        int fd = openat(dirfd, path, O_WRONLY | O_CREAT, 0600);
        if (fd < 0) {
            fprintf(stderr, "openat: %m\n");
            return 1;
        }

        char *buf = payload;
        int buflen = strlen(payload);
        while (buflen > 0) {
            int written = write(fd, buf, buflen);
            if (written < 0) {
                fprintf(stderr, "write: %m\n");
                unlink(path);
                return 1;
            }
            buflen -= written;
            buf += written;
        }

        close(fd);

        char newpath[256];
        snprintf(newpath, sizeof(newpath), "shell-%s", ttykey);
        if (renameat(dirfd, path, dirfd, newpath) < 0) {
            fprintf(stderr, "renameat: %m\n");
            unlink(path);
            return 1;
        }

        close(dirfd);
    }

    close(sk);

    return 0;
}
