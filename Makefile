CC = gcc
INC = -Iinclude
WARN = -Wall -Wextra -Werror
SAN = -fsanitize=address,undefined -fno-omit-frame-pointer -g
STD = -std=c11

.PHONY: all test clean

all: test

# --- Buggy build: expected to crash under ASan when the oversized-length
#     fault fires. That crash IS the passing result for this target: it
#     proves the reproduced bug is real, not a test artifact.
build/test_recv_buggy: src/fault_inject.c src/eswifi_repro_buggy.c tests/test_eswifi_recv.c
	@mkdir -p build
	$(CC) $(STD) $(WARN) $(SAN) -DFAULT_INJECTION_ENABLED $(INC) $^ -o $@

# --- Fixed build: expected to pass cleanly.
build/test_recv_fixed: src/fault_inject.c src/eswifi_repro_fixed.c tests/test_eswifi_recv.c
	@mkdir -p build
	$(CC) $(STD) $(WARN) $(SAN) -DFAULT_INJECTION_ENABLED $(INC) $^ -o $@

# --- Disabled build: FAULT_INJECTION_ENABLED is NOT defined, and
#     fault_inject.c is deliberately NOT linked in at all. Proves
#     FI_POINT compiles to exactly the original call -- no fault-
#     injection machinery is even present in the binary.
build/test_disabled: src/eswifi_repro_fixed.c tests/test_disabled_compiles_out.c
	@mkdir -p build
	$(CC) $(STD) $(WARN) $(SAN) $(INC) $^ -o $@

test: build/test_recv_fixed build/test_disabled
	@echo "=== fixed build (expected: all checks pass) ==="
	./build/test_recv_fixed
	@echo ""
	@echo "=== disabled build (expected: all checks pass, no FI symbols linked) ==="
	./build/test_disabled
	@echo ""
	@echo "=== buggy build (expected: crash / ASan overflow report) ==="
	@$(MAKE) build/test_recv_buggy
	@./build/test_recv_buggy; \
	code=$$?; \
	if [ $$code -eq 0 ]; then \
		echo "UNEXPECTED: buggy build did not crash -- reproduction is not faithful"; \
		exit 1; \
	else \
		echo "buggy build failed as expected (exit $$code) -- bug reproduction confirmed"; \
	fi

clean:
	rm -rf build
