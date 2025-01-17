#pragma once

#include <sys/types.h>
#include <sys/un.h>

void escape_tty_key_inplace(char *ttykey);
void get_socket_address(struct sockaddr_un *addr, size_t *addr_len, const char *ttykey);
