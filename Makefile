CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude

all: bridge fuzz_test load_test

bridge: src/frame.c src/zb_bridge.c
	$(CC) $(CFLAGS) -O3 src/frame.c src/zb_bridge.c -o zb_bridge

fuzz_test: src/frame.c src/transport.c tests/fuzz_test.c
	$(CC) $(CFLAGS) -g -O0 src/frame.c src/transport.c tests/fuzz_test.c -o zero_bus_test

load_test: tests/load_test.c
	$(CC) $(CFLAGS) -O3 tests/load_test.c -lpthread -o load_test

test: fuzz_test
	./zero_bus_test
	@echo "[+] Fuzzing tests passed successfully!"

clean:
	rm -f zb_bridge zero_bus_test load_test *.o *.sock

.PHONY: all bridge fuzz_test load_test test clean
