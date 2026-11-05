CC = clang
CFLAGS = -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g
SANITIZERS = -fsanitize=address,undefined -fno-omit-frame-pointer
.PHONY: test
test: test_snapshot_policy
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./test_snapshot_policy

test_snapshot_policy: snapshot_policy.c snapshot_policy.h test_snapshot_policy.c
	$(CC) $(CFLAGS) $(SANITIZERS) snapshot_policy.c test_snapshot_policy.c -o $@
