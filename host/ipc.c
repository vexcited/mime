#include "ipc.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static bool write_all(int fd, const void *data, size_t size)
{
    const uint8_t *p = data;
    while (size) {
        ssize_t n = send(fd, p, size, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        p += n;
        size -= (size_t)n;
    }
    return true;
}

static bool read_all(int fd, void *data, size_t size)
{
    uint8_t *p = data;
    while (size) {
        ssize_t n = recv(fd, p, size, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            return false;
        }
        p += n;
        size -= (size_t)n;
    }
    return true;
}

bool mime_ipc_send(int fd, uint32_t type, const void *payload, uint32_t size)
{
    MimeIpcHeader h = {type, size};
    return write_all(fd, &h, sizeof(h)) && (size == 0 || write_all(fd, payload, size));
}

bool mime_ipc_recv(int fd, MimeIpcHeader *header, void **payload)
{
    *payload = NULL;
    if (!read_all(fd, header, sizeof(*header)) || header->size > MIME_IPC_MAX_PAYLOAD)
        return false;
    uint8_t *buf = malloc(header->size + 1);
    if (!buf)
        return false;
    if (header->size && !read_all(fd, buf, header->size)) {
        free(buf);
        return false;
    }
    buf[header->size] = 0;
    *payload = buf;
    return true;
}

int mime_ipc_connect(const char *socket_path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    mime_strlcpy(addr.sun_path, socket_path, sizeof(addr.sun_path));
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    return fd;
}
