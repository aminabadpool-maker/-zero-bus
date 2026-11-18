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
