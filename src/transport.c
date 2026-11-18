#include "zero_bus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

int zb_server_init(void) {
    int server_fd;
    struct sockaddr_un address;

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[-] Socket creation failed");
        return -1;
    }

    unlink(ZB_SOCKET_PATH);

    memset(&address, 0, sizeof(struct sockaddr_un));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, ZB_SOCKET_PATH, sizeof(address.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(struct sockaddr_un)) < 0) {
        perror("[-] Socket bind failed");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("[-] Socket listen failed");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

int zb_client_connect(void) {
    int sock_fd;
    struct sockaddr_un address;

    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("[-] Client socket creation failed");
        return -1;
    }

    memset(&address, 0, sizeof(struct sockaddr_un));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, ZB_SOCKET_PATH, sizeof(address.sun_path) - 1);

    if (connect(sock_fd, (struct sockaddr *)&address, sizeof(struct sockaddr_un)) < 0) {
        perror("[-] Client connect failed");
        close(sock_fd);
        return -1;
    }

    return sock_fd;
}
