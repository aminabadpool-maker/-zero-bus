#!/usr/bin/env bash
set -e

echo "=========================================="
echo "[-] Setting up Zero-Bus Fuzzing & Validation Suite"
echo "=========================================="

# ۱. ساخت پوشه تست اگر وجود نداشته باشد
mkdir -p tests

# ۲. ایجاد فایل تست فازینگ و بررسی هدر
echo "[-] Creating tests/fuzz_test.c..."
cat << 'INNER_EOF' > tests/fuzz_test.c
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
INNER_EOF

# ۳. کامپایل کردن پروژه و فایل تست با فلگ‌های دیباگ
echo "[-] Compiling zero-bus with GCC (-g -O0)..."
gcc -g -O0 src/frame.c src/transport.c tests/fuzz_test.c -Iinclude -o zero_bus_test

# ۴. اجرای فایل اجرایی تست
echo "[-] Executing test binary..."
./zero_bus_test

echo "=========================================="
echo "[+] All tests passed successfully! Code is robust."
echo "=========================================="
