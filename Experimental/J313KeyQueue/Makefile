CC ?= cc
CFLAGS ?= -O2 -g
WARN = -std=c11 -Wall -Wextra -Wpedantic -Werror
SOURCES = key_queue.c test_key_queue.c ../J313HidKeyboard/hid_keyboard.c
HEADERS = key_queue.h ../J313HidKeyboard/hid_keyboard.h
.PHONY: all test sanitize clean
all: test
build:
	mkdir -p build
build/test_key_queue: $(SOURCES) $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(SOURCES) $(LDFLAGS) -o $@
test: build/test_key_queue
	./build/test_key_queue
build/test_key_queue_sanitize: $(SOURCES) $(HEADERS) | build
	$(CC) $(CPPFLAGS) -O1 -g $(WARN) -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) $(LDFLAGS) -o $@
sanitize: build/test_key_queue_sanitize
	ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./build/test_key_queue_sanitize
clean:
	rm -f build/test_key_queue build/test_key_queue_sanitize
