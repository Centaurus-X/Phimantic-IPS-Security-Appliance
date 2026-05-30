#ifndef PHIM_SOCKET_H
#define PHIM_SOCKET_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

int phim_tcp_listen(const char *host, uint16_t port, int backlog);
int phim_tcp_connect(const char *host, uint16_t port, uint32_t timeout_ms);
int phim_set_nonblocking(int fd, int enabled);
int phim_set_tcp_nodelay(int fd);
int phim_set_reuseaddr(int fd);
ssize_t phim_read_until(int fd, unsigned char *buf, size_t cap, const char *needle, uint32_t timeout_ms);
int phim_write_all(int fd, const void *buf, size_t len);
int phim_read_exact_timeout(int fd, void *buf, size_t len, uint32_t timeout_ms);
ssize_t phim_read_some_timeout(int fd, void *buf, size_t len, uint32_t timeout_ms);
void phim_close_fd(int *fd);

#endif
