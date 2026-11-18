#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "zero_bus.h"

void run_fuzz_tests() {
    printf("[-] Starting Fuzzing & Validation Tests...\n");

    // ۱. تست بافر ناقص (کمتر از اندازه هدر)
    uint8_t short_buffer[4] = {0x01, 0x02, 0x03, 0x04};
    zb_header_t header;
    int res1 = zb_validate_header(short_buffer, sizeof(short_buffer), &header);
    assert(res1 == -1);
    printf("[+] PASS: Short buffer correctly rejected (res = %d)\n", res1);

    // ۲. تست Magic Number نادرست (Bad Magic)
    uint8_t bad_magic_buffer[sizeof(zb_header_t)];
    memset(bad_magic_buffer, 0xAA, sizeof(zb_header_t)); // پر کردن با بایت‌های نامعتبر
    int res2 = zb_validate_header(bad_magic_buffer, sizeof(bad_magic_buffer), &header);
    assert(res2 == -2);
    printf("[+] PASS: Bad magic number correctly rejected (res = %d)\n", res2);

    printf("[-] All Fuzzing checks passed safely without crashes!\n");
}

int main(void) {
    run_fuzz_tests();
    return 0;
}
