#include "util.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

void escape_tty_key_inplace(char *ttykey)
{
    char *x = ttykey;
    while (1) {
        x = strchr(x, '/');
        if (!x)
            break;
        *x = '-';
    }
}

void get_socket_address(struct sockaddr_un *addr, size_t *addr_len, const char *ttykey)
{
    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    snprintf(addr->sun_path, sizeof(addr->sun_path), ".kitty-modeline-%s.socket", ttykey);
    int path_len = strlen(addr->sun_path);
    addr->sun_path[0] = '\0';
    *addr_len = sizeof(addr->sun_family) + path_len;
}
