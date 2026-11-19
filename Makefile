CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -Iinclude
SAN_CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude
TMP_DIR ?= $(if $(TMPDIR),$(TMPDIR),/tmp)

.PHONY: all test test-asan clean

all: test

test:
	@mkdir -p "$(TMP_DIR)"
	$(CC) $(CFLAGS) src/frame.c tests/frame_test.c -o "$(TMP_DIR)/zb_frame_test"
	"$(TMP_DIR)/zb_frame_test"
	$(CC) $(CFLAGS) src/frame.c tests/frame_errors_test.c -o "$(TMP_DIR)/zb_frame_errors_test"
	"$(TMP_DIR)/zb_frame_errors_test"
	$(CC) $(CFLAGS) src/frame.c src/transport.c tests/transport_test.c -o "$(TMP_DIR)/zb_transport_test"
	"$(TMP_DIR)/zb_transport_test"
	$(CC) $(CFLAGS) src/frame.c src/transport.c tests/unix_sock_test.c -o "$(TMP_DIR)/zb_unix_sock_test"
	"$(TMP_DIR)/zb_unix_sock_test"
	$(CC) $(CFLAGS) src/frame.c src/transport.c tests/unix_sock_stress_test.c -o "$(TMP_DIR)/zb_unix_sock_stress_test"
	"$(TMP_DIR)/zb_unix_sock_stress_test"
	$(CC) $(CFLAGS) src/frame.c src/mem.c src/stream.c src/transport.c tests/unix_sock_demux_bp_test.c -o "$(TMP_DIR)/zb_unix_sock_demux_bp_test"
	"$(TMP_DIR)/zb_unix_sock_demux_bp_test"
	$(CC) $(CFLAGS) src/mem.c tests/mem_test.c -o "$(TMP_DIR)/zb_mem_test"
	"$(TMP_DIR)/zb_mem_test"
	$(CC) $(CFLAGS) src/mem.c src/stream.c tests/stream_test.c -o "$(TMP_DIR)/zb_stream_test"
	"$(TMP_DIR)/zb_stream_test"
	$(CC) $(CFLAGS) src/frame.c src/mem.c src/stream.c tests/demux_test.c -o "$(TMP_DIR)/zb_demux_test"
	"$(TMP_DIR)/zb_demux_test"
	$(CC) $(CFLAGS) src/frame.c src/mem.c src/stream.c tests/demux_stress_test.c -o "$(TMP_DIR)/zb_demux_stress_test"
	"$(TMP_DIR)/zb_demux_stress_test"
	$(CC) $(CFLAGS) src/frame.c src/mem.c src/stream.c tests/slot_reuse_stress_test.c -o "$(TMP_DIR)/zb_slot_reuse_stress_test"
	"$(TMP_DIR)/zb_slot_reuse_stress_test"
	$(CC) $(CFLAGS) src/frame.c src/mem.c src/stream.c tests/fuzz_test.c -o "$(TMP_DIR)/zb_fuzz_test"
	"$(TMP_DIR)/zb_fuzz_test"
	@rm -f "$(TMP_DIR)/zb_frame_test" "$(TMP_DIR)/zb_frame_errors_test" "$(TMP_DIR)/zb_transport_test" "$(TMP_DIR)/zb_unix_sock_test" "$(TMP_DIR)/zb_unix_sock_stress_test" "$(TMP_DIR)/zb_unix_sock_demux_bp_test" "$(TMP_DIR)/zb_mem_test" "$(TMP_DIR)/zb_stream_test" "$(TMP_DIR)/zb_demux_test" "$(TMP_DIR)/zb_demux_stress_test" "$(TMP_DIR)/zb_slot_reuse_stress_test" "$(TMP_DIR)/zb_fuzz_test"

test-asan:
	@mkdir -p "$(TMP_DIR)"
	$(CC) $(SAN_CFLAGS) src/frame.c tests/frame_test.c -o "$(TMP_DIR)/zb_frame_test_san"
	"$(TMP_DIR)/zb_frame_test_san"
	$(CC) $(SAN_CFLAGS) src/frame.c tests/frame_errors_test.c -o "$(TMP_DIR)/zb_frame_errors_test_san"
	"$(TMP_DIR)/zb_frame_errors_test_san"
	$(CC) $(SAN_CFLAGS) src/frame.c src/transport.c tests/transport_test.c -o "$(TMP_DIR)/zb_transport_test_san"
	"$(TMP_DIR)/zb_transport_test_san"
	$(CC) $(SAN_CFLAGS) src/frame.c src/transport.c tests/unix_sock_test.c -o "$(TMP_DIR)/zb_unix_sock_test_san"
	"$(TMP_DIR)/zb_unix_sock_test_san"
	$(CC) $(SAN_CFLAGS) src/frame.c src/transport.c tests/unix_sock_stress_test.c -o "$(TMP_DIR)/zb_unix_sock_stress_test_san"
	"$(TMP_DIR)/zb_unix_sock_stress_test_san"
	$(CC) $(SAN_CFLAGS) src/frame.c src/mem.c src/stream.c src/transport.c tests/unix_sock_demux_bp_test.c -o "$(TMP_DIR)/zb_unix_sock_demux_bp_test_san"
	"$(TMP_DIR)/zb_unix_sock_demux_bp_test_san"
	$(CC) $(SAN_CFLAGS) src/mem.c tests/mem_test.c -o "$(TMP_DIR)/zb_mem_test_san"
	"$(TMP_DIR)/zb_mem_test_san"
	$(CC) $(SAN_CFLAGS) src/mem.c src/stream.c tests/stream_test.c -o "$(TMP_DIR)/zb_stream_test_san"
	"$(TMP_DIR)/zb_stream_test_san"
	$(CC) $(SAN_CFLAGS) src/frame.c src/mem.c src/stream.c tests/demux_test.c -o "$(TMP_DIR)/zb_demux_test_san"
	"$(TMP_DIR)/zb_demux_test_san"
	$(CC) $(SAN_CFLAGS) src/frame.c src/mem.c src/stream.c tests/demux_stress_test.c -o "$(TMP_DIR)/zb_demux_stress_test_san"
	"$(TMP_DIR)/zb_demux_stress_test_san"
	$(CC) $(SAN_CFLAGS) src/frame.c src/mem.c src/stream.c tests/slot_reuse_stress_test.c -o "$(TMP_DIR)/zb_slot_reuse_stress_test_san"
	"$(TMP_DIR)/zb_slot_reuse_stress_test_san"
	$(CC) $(SAN_CFLAGS) src/frame.c src/mem.c src/stream.c tests/fuzz_test.c -o "$(TMP_DIR)/zb_fuzz_test_san"
	"$(TMP_DIR)/zb_fuzz_test_san"
	@rm -f "$(TMP_DIR)/zb_frame_test_san" "$(TMP_DIR)/zb_frame_errors_test_san" "$(TMP_DIR)/zb_transport_test_san" "$(TMP_DIR)/zb_unix_sock_test_san" "$(TMP_DIR)/zb_unix_sock_stress_test_san" "$(TMP_DIR)/zb_unix_sock_demux_bp_test_san" "$(TMP_DIR)/zb_mem_test_san" "$(TMP_DIR)/zb_stream_test_san" "$(TMP_DIR)/zb_demux_test_san" "$(TMP_DIR)/zb_demux_stress_test_san" "$(TMP_DIR)/zb_slot_reuse_stress_test_san" "$(TMP_DIR)/zb_fuzz_test_san"

clean:
	@rm -f "$(TMP_DIR)/zb_frame_test" "$(TMP_DIR)/zb_frame_errors_test" "$(TMP_DIR)/zb_transport_test" "$(TMP_DIR)/zb_unix_sock_test" "$(TMP_DIR)/zb_unix_sock_stress_test" "$(TMP_DIR)/zb_unix_sock_demux_bp_test" "$(TMP_DIR)/zb_mem_test" "$(TMP_DIR)/zb_stream_test" "$(TMP_DIR)/zb_demux_test" "$(TMP_DIR)/zb_demux_stress_test" "$(TMP_DIR)/zb_slot_reuse_stress_test" "$(TMP_DIR)/zb_fuzz_test" "$(TMP_DIR)/zb_frame_test_san" "$(TMP_DIR)/zb_frame_errors_test_san" "$(TMP_DIR)/zb_transport_test_san" "$(TMP_DIR)/zb_unix_sock_test_san" "$(TMP_DIR)/zb_unix_sock_stress_test_san" "$(TMP_DIR)/zb_unix_sock_demux_bp_test_san" "$(TMP_DIR)/zb_mem_test_san" "$(TMP_DIR)/zb_stream_test_san" "$(TMP_DIR)/zb_demux_test_san" "$(TMP_DIR)/zb_demux_stress_test_san" "$(TMP_DIR)/zb_slot_reuse_stress_test_san" "$(TMP_DIR)/zb_fuzz_test_san"
