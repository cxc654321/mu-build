# SPDX-License-Identifier: MIT
CC = clang
CFLAGS = -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g
SAN = -fsanitize=address,undefined -fno-omit-frame-pointer
.PHONY: test sanitize fuzz-smoke fuzz
build:
	mkdir -p build
build/test: hid_keyboard.c hid_keyboard.h test_keyboard.c fixtures.h | build
	$(CC) $(CFLAGS) hid_keyboard.c test_keyboard.c -o $@
test: build/test
	./build/test
sanitize: | build
	$(CC) $(CFLAGS) $(SAN) hid_keyboard.c test_keyboard.c -o build/test-san
	./build/test-san
fuzz-smoke: | build
	$(CC) $(CFLAGS) $(SAN) -DHK_FUZZ_SMOKE hid_keyboard.c fuzz_keyboard.c -o build/fuzz-smoke
	./build/fuzz-smoke
fuzz: | build
	$(CC) $(CFLAGS) $(SAN) -fsanitize=fuzzer hid_keyboard.c fuzz_keyboard.c -o build/fuzz
	./build/fuzz -runs=100000 -max_len=4097 -artifact_prefix=build/
