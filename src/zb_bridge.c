#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "zero_bus.h"

#define BRIDGE_PORT 8080
#define BUFFER_SIZE 1024

void handle_http_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }
    buffer[bytes_read] = '\0';

    // ساخت پکت باینری استاندارد ZB01 برای تست ارتباط با هسته
    uint8_t packet[64];
    const char *ping_msg = "CHROME_PING";
    size_t pkt_len = zb_serialize_packet(packet, ZB_PING, (const uint8_t *)ping_msg, strlen(ping_msg));

    // اعتبارسنجی پکت تولیدشده
    zb_header_t validated_hdr;
    int status = zb_validate_header(packet, pkt_len, &validated_hdr);

    // آماده‌سازی پاسخ HTTP با هدر CORS برای دسترسی مستقیم کروم
    char response[512];
    snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n"
        "{\"protocol\":\"zero-bus\",\"magic\":\"0x%X\",\"status\":%d,\"payload_bytes\":%u}\n",
        validated_hdr.magic, status, validated_hdr.payload_len);

    write(client_fd, response, strlen(response));
    close(client_fd);
}

int main(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // فقط لوکال‌هاست برای امنیت
    addr.sin_port = htons(BRIDGE_PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }

    printf("[*] Zero-Bus Android/Chrome Bridge active on http://127.0.0.1:%d\n", BRIDGE_PORT);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd >= 0) {
            handle_http_client(client_fd);
        }
    }

    close(server_fd);
    return 0;
}
