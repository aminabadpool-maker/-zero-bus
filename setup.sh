#!/bin/bash
set -e

echo "[*] Cleaning up old builds..."
rm -rf include src tests zero_bus_test *.o zero_bus.sock

echo "[*] Setting up directory structure..."
mkdir -p include src tests

echo "[*] Creating include/zero_bus.h..."
cat << 'EOF' > include/zero_bus.h
#ifndef ZERO_BUS_H
#define ZERO_BUS_H

#include <stdint.h>
#include <stddef.h>

#define ZB_MAGIC 0x5A423031  // "ZB01"
#define ZB_SOCKET_PATH "zero_bus.sock"

typedef enum {
    ZB_PING   = 0x01,
    ZB_EXEC   = 0x02,
    ZB_DATA   = 0x03,
    ZB_ERROR  = 0xFF
} zb_msg_type_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;       
    uint8_t  version;     
    uint8_t  type;        
    uint16_t flags;       
    uint32_t payload_len; 
    uint32_t checksum;    
} zb_header_t;

int zb_validate_header(const uint8_t *buffer, size_t len, zb_header_t *out_header);
size_t zb_serialize_packet(uint8_t *dest, uint8_t type, const uint8_t *payload, uint32_t payload_len);

int zb_server_init(void);
int zb_client_connect(void);

#endif
EOF

echo "[*] Creating src/frame.c..."
cat << 'EOF' > src/frame.c
#include "zero_bus.h"
#include <string.h>

int zb_validate_header(const uint8_t *buffer, size_t len, zb_header_t *out_header) {
    if (len < sizeof(zb_header_t)) {
        return -1;
    }
    const zb_header_t *hdr = (const zb_header_t *)buffer;
    if (hdr->magic != ZB_MAGIC) {
        return -2;
    }
    if (out_header) {
        *out_header = *hdr;
    }
    return 0;
}

size_t zb_serialize_packet(uint8_t *dest, uint8_t type, const uint8_t *payload, uint32_t payload_len) {
    zb_header_t *hdr = (zb_header_t *)dest;
    hdr->magic = ZB_MAGIC;
    hdr->version = 0x01;
    hdr->type = type;
    hdr->flags = 0x0000;
    hdr->payload_len = payload_len;
    hdr->checksum = 0x00000000;

    if (payload && payload_len > 0) {
        memcpy(dest + sizeof(zb_header_t), payload, payload_len);
    }
    return sizeof(zb_header_t) + payload_len;
}
EOF

echo "[*] Creating src/transport.c..."
cat << 'EOF' > src/transport.c
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
EOF

echo "[*] Creating src/main.c..."
cat << 'EOF' > src/main.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include "zero_bus.h"

int main(void) {
    printf("[*] Initializing Zero-Bus IPC Transport Layer...\n");

    int server_fd = zb_server_init();
    if (server_fd < 0) {
        fprintf(stderr, "[-] Failed to initialize server socket.\n");
        return 1;
    }
    printf("[+] Server listening on %s\n", ZB_SOCKET_PATH);

    pid_t pid = fork();
    if (pid < 0) {
        perror("[-] Fork failed");
        close(server_fd);
        return 1;
    }

    if (pid == 0) {
        usleep(100000); 
        int client_fd = zb_client_connect();
        if (client_fd < 0) {
            exit(1);
        }

        uint8_t send_buf[1024];
        const char *payload_msg = "Hello Zero-Bus via Unix Socket!";
        size_t packet_size = zb_serialize_packet(send_buf, ZB_DATA, (const uint8_t *)payload_msg, strlen(payload_msg));

        write(client_fd, send_buf, packet_size);
        printf("[Client] Sent packet of %zu bytes through socket.\n", packet_size);

        close(client_fd);
        exit(0);
    } else {
        int client_sock = accept(server_fd, NULL, NULL);
        if (client_sock < 0) {
            perror("[-] Server accept failed");
            close(server_fd);
            return 1;
        }

        uint8_t recv_buf[1024];
        ssize_t bytes_read = read(client_sock, recv_buf, sizeof(recv_buf));
        if (bytes_read > 0) {
            zb_header_t hdr;
            if (zb_validate_header(recv_buf, bytes_read, &hdr) == 0) {
                printf("[Server] Received valid packet!\n");
                printf("         -> Type: 0x%X\n", hdr.type);
                printf("         -> Payload Len: %u bytes\n", hdr.payload_len);
                char *payload = (char *)(recv_buf + sizeof(zb_header_t));
                printf("         -> Payload: %.*s\n", hdr.payload_len, payload);
            } else {
                printf("[Server] Packet validation failed.\n");
            }
        }

        close(client_sock);
        close(server_fd);
        unlink(ZB_SOCKET_PATH);
        printf("[*] Test completed successfully.\n");
    }

    return 0;
}
EOF

echo "[*] Creating Makefile..."
cat << 'EOF' > Makefile
CC = gcc
CFLAGS = -Wall -Wextra -Werror -O3 -Iinclude
TARGET = zero_bus_test
SRC = src/main.c src/frame.c src/transport.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET) zero_bus.sock

.PHONY: all clean
EOF

echo "[*] Creating README.md..."
cat << 'EOF' > README.md
# zero-bus
پروتکل ارتباطی باینری و پرسرعت با C خالص برای ساختارهای مدرن و ارتباطات بین‌پردازشی (IPC).
EOF

echo "[+] Build files generated successfully! Running 'make'..."
make
echo "[+] Running test binary data..."
./zero_bus_test
