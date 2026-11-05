# SPDX-License-Identifier: GPL-2.0-only
CC = clang
CPPFLAGS += -I../J313SpiHidProtocol -I../J313HidKeyboard
WARN = -std=c11 -Wall -Wextra -Wpedantic -Werror
SOURCES = test_pipeline.c ../J313SpiHidProtocol/spihid_protocol.c ../J313HidKeyboard/hid_keyboard.c
# Build only in this new directory; remove temporary executable on exit.
.PHONY: all test sanitize
all: test sanitize
test:
	@set -eu; bin="$(CURDIR)/.pipeline-test"; trap 'rm -f "$$bin"' EXIT; \
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -O2 $(SOURCES) $(LDFLAGS) -o "$$bin"; "$$bin"
sanitize:
	@set -eu; bin="$(CURDIR)/.pipeline-sanitize"; trap 'rm -f "$$bin"' EXIT; \
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -O1 -g0 -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all $(SOURCES) $(LDFLAGS) -o "$$bin"; \
	ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$$bin"
