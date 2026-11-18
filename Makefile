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
