CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -Iinclude
TMP_DIR ?= $(if $(TMPDIR),$(TMPDIR),/tmp)

.PHONY: all test clean

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
	$(CC) $(CFLAGS) src/mem.c tests/mem_test.c -o "$(TMP_DIR)/zb_mem_test"
	"$(TMP_DIR)/zb_mem_test"
	$(CC) $(CFLAGS) src/mem.c src/stream.c tests/stream_test.c -o "$(TMP_DIR)/zb_stream_test"
	"$(TMP_DIR)/zb_stream_test"
	$(CC) $(CFLAGS) src/frame.c src/mem.c src/stream.c tests/demux_test.c -o "$(TMP_DIR)/zb_demux_test"
	"$(TMP_DIR)/zb_demux_test"
	$(CC) $(CFLAGS) src/frame.c src/mem.c src/stream.c tests/fuzz_test.c -o "$(TMP_DIR)/zb_fuzz_test"
	"$(TMP_DIR)/zb_fuzz_test"
	@rm -f "$(TMP_DIR)/zb_frame_test" "$(TMP_DIR)/zb_frame_errors_test" "$(TMP_DIR)/zb_transport_test" "$(TMP_DIR)/zb_unix_sock_test" "$(TMP_DIR)/zb_mem_test" "$(TMP_DIR)/zb_stream_test" "$(TMP_DIR)/zb_demux_test" "$(TMP_DIR)/zb_fuzz_test"

clean:
	@rm -f "$(TMP_DIR)/zb_frame_test" "$(TMP_DIR)/zb_frame_errors_test" "$(TMP_DIR)/zb_transport_test" "$(TMP_DIR)/zb_unix_sock_test" "$(TMP_DIR)/zb_mem_test" "$(TMP_DIR)/zb_stream_test" "$(TMP_DIR)/zb_demux_test" "$(TMP_DIR)/zb_fuzz_test"
