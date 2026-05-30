#include "phimantec/phim_socket.h"
#include "phimantec/phim_log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int phim_set_nonblocking(int fd, int enabled)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (enabled != 0) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(fd, F_SETFL, flags);
}

int phim_set_tcp_nodelay(int fd)
{
    int one = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

int phim_set_reuseaddr(int fd)
{
    int one = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
}

static int phim_wait_fd(int fd, short events, uint32_t timeout_ms)
{
    struct pollfd pfd;
    int rc;
    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;
    do {
        rc = poll(&pfd, 1u, (int)timeout_ms);
    } while (rc < 0 && errno == EINTR);
    if (rc <= 0) {
        return -1;
    }
    if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        if ((pfd.revents & events) == 0) {
            return -1;
        }
    }
    return 0;
}

int phim_tcp_listen(const char *host, uint16_t port, int backlog)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *rp;
    char service[16];
    int fd = -1;
    int rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    (void)snprintf(service, sizeof(service), "%u", (unsigned int)port);

    rc = getaddrinfo((host != NULL && host[0] != '\0') ? host : NULL, service, &hints, &res);
    if (rc != 0) {
        PHIM_LOGE("getaddrinfo listen %s:%s failed: %s", host != NULL ? host : "*", service, gai_strerror(rc));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        (void)phim_set_reuseaddr(fd);
        if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0 && listen(fd, backlog) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

int phim_tcp_connect(const char *host, uint16_t port, uint32_t timeout_ms)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *rp;
    char service[16];
    int fd = -1;
    int rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    (void)snprintf(service, sizeof(service), "%u", (unsigned int)port);

    rc = getaddrinfo(host, service, &hints, &res);
    if (rc != 0) {
        PHIM_LOGW("getaddrinfo connect %s:%s failed: %s", host, service, gai_strerror(rc));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        int err = 0;
        socklen_t err_len = sizeof(err);
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        (void)phim_set_tcp_nodelay(fd);
        if (phim_set_nonblocking(fd, 1) != 0) {
            close(fd);
            fd = -1;
            continue;
        }
        rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rc == 0) {
            (void)phim_set_nonblocking(fd, 0);
            break;
        }
        if (errno != EINPROGRESS) {
            close(fd);
            fd = -1;
            continue;
        }
        if (phim_wait_fd(fd, POLLOUT, timeout_ms) != 0) {
            close(fd);
            fd = -1;
            continue;
        }
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) != 0 || err != 0) {
            close(fd);
            fd = -1;
            continue;
        }
        (void)phim_set_nonblocking(fd, 0);
        break;
    }

    freeaddrinfo(res);
    return fd;
}

int phim_write_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = (const unsigned char *)buf;
    size_t off = 0u;
    while (off < len) {
        ssize_t n = write(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

int phim_read_exact_timeout(int fd, void *buf, size_t len, uint32_t timeout_ms)
{
    unsigned char *p = (unsigned char *)buf;
    size_t off = 0u;
    while (off < len) {
        ssize_t n;
        if (phim_wait_fd(fd, POLLIN, timeout_ms) != 0) {
            return -1;
        }
        n = read(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

ssize_t phim_read_some_timeout(int fd, void *buf, size_t len, uint32_t timeout_ms)
{
    ssize_t n;
    if (phim_wait_fd(fd, POLLIN, timeout_ms) != 0) {
        return -1;
    }
    do {
        n = read(fd, buf, len);
    } while (n < 0 && errno == EINTR);
    return n;
}

ssize_t phim_read_until(int fd, unsigned char *buf, size_t cap, const char *needle, uint32_t timeout_ms)
{
    size_t used = 0u;
    size_t needle_len = strlen(needle);
    while (used < cap) {
        ssize_t n;
        if (phim_wait_fd(fd, POLLIN, timeout_ms) != 0) {
            return -1;
        }
        n = read(fd, buf + used, cap - used);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        used += (size_t)n;
        if (used >= needle_len) {
            size_t i;
            for (i = 0u; i + needle_len <= used; i++) {
                if (memcmp(buf + i, needle, needle_len) == 0) {
                    return (ssize_t)used;
                }
            }
        }
    }
    return -1;
}

void phim_close_fd(int *fd)
{
    if (fd != NULL && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}
