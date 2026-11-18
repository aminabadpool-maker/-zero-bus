#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>

#define THREAD_COUNT 100
#define TARGET_PORT 8080

long success_count = 0;
long fail_count = 0;
pthread_mutex_t lock;

void *client_thread(void *arg) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        pthread_mutex_lock(&lock);
        fail_count++;
        pthread_mutex_unlock(&lock);
        return NULL;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(TARGET_PORT);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        pthread_mutex_lock(&lock);
        fail_count++;
        pthread_mutex_unlock(&lock);
        return NULL;
    }

    const char *req = "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    write(sock, req, strlen(req));

    char buf[512];
    ssize_t n = read(sock, buf, sizeof(buf) - 1);
    close(sock);

    pthread_mutex_lock(&lock);
    if (n > 0) {
        success_count++;
    } else {
        fail_count++;
    }
    pthread_mutex_unlock(&lock);

    return NULL;
}

int main(void) {
    pthread_t threads[THREAD_COUNT];
    pthread_mutex_init(&lock, NULL);

    printf("[-] Starting Heavy Concurrency Test: %d parallel threads hammering port %d...\n", THREAD_COUNT, TARGET_PORT);

    struct timeval start, end;
    gettimeofday(&start, NULL);

    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_create(&threads[i], NULL, client_thread, NULL);
    }

    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }

    gettimeofday(&end, NULL);
    long seconds = (end.tv_sec - start.tv_sec);
    long microseconds = (end.tv_usec - start.tv_usec);
    double elapsed = seconds + microseconds * 1e-6;

    printf("========================================\n");
    printf("[+] Test Completed in %.4f seconds\n", elapsed);
    printf("[+] Successful Requests: %ld\n", success_count);
    printf("[+] Failed Requests:     %ld\n", fail_count);
    printf("========================================\n");

    pthread_mutex_destroy(&lock);
    return (fail_count == 0) ? 0 : 1;
}
