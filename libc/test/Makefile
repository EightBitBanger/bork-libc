# Host tests. The libc is rebuilt with every syscall routed through
# linux_shim.c (an emulation of the kernel's syscalls on 32-bit Linux), and
# the test programs are linked exactly like real user programs: crt0.o,
# user.ld, libc.a.

CC ?= gcc
ifeq ($(origin CC),default)
CC := gcc
endif

ROOT  := ..
GCC_INCLUDE := $(shell $(CC) -print-file-name=include)
ARCH  := -m32 -march=i686

# GCC only: stop it turning the byte loops in string.c into memcpy/memset
# calls (which would recurse). Clang has no such flag and doesn't need it.
NO_LOOP_PATTERNS := $(shell $(CC) -fno-tree-loop-distribute-patterns -E -x c /dev/null >/dev/null 2>&1 && echo " -fno-tree-loop-distribute-patterns")
LIBC_CFLAGS := $(ARCH) -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-builtin -nostdinc \
               -isystem $(GCC_INCLUDE) -I$(ROOT)/include -fno-pic -fno-pie -fno-stack-protector \
               -fcf-protection=none -fno-asynchronous-unwind-tables$(NO_LOOP_PATTERNS) \
               -DLIBC_SYSCALL_HOOK=__test_syscall
USER_CFLAGS := $(ARCH) -std=gnu11 -O2 -Wall -Wextra -Wno-format -ffreestanding -nostdinc \
               -isystem $(GCC_INCLUDE) -I$(ROOT)/include -fno-pic -fno-pie -fno-stack-protector \
               -fcf-protection=none -fno-asynchronous-unwind-tables -DLIBC_SYSCALL_HOOK=__test_syscall
LDFLAGS := $(ARCH) -nostdlib -static -no-pie -T $(ROOT)/user.ld -Wl,--build-id=none

SRCS := errno.c unistd.c string.c ctype.c divdi3.c malloc.c stdlib.c start.c printf.c stdio.c dirent.c
OBJS := $(addprefix build/,$(SRCS:.c=.o)) build/linux_shim.o

all: run

build:
	mkdir -p build

build/%.o: $(ROOT)/src/%.c | build
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

build/linux_shim.o: linux_shim.c | build
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

build/crt0.o: $(ROOT)/src/crt0.S | build
	$(CC) $(ARCH) -c $< -o $@

build/libc_test.a: $(OBJS)
	rm -f $@ && ar rcs $@ $^

build/%: %.c build/libc_test.a build/crt0.o
	$(CC) $(USER_CFLAGS) -c $< -o build/$*.o
	$(CC) $(LDFLAGS) build/crt0.o build/$*.o build/libc_test.a -o $@

# Reference: the same case list through the host C library
build/fmt_cases_glibc: fmt_cases.c | build
	$(CC) -O2 -w $< -o $@

run: build/fmt_cases build/fmt_cases_glibc build/run_test
	./build/fmt_cases > build/fmt_ours.txt
	./build/fmt_cases_glibc > build/fmt_glibc.txt
	diff -u build/fmt_glibc.txt build/fmt_ours.txt && echo "printf/strtod/strtol: identical to glibc ($$(wc -l < build/fmt_ours.txt) lines)"
	cd build && rm -rf sandbox && mkdir sandbox && cd sandbox && ../run_test

clean:
	rm -rf build

.PHONY: all run clean
