
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

SRCS := errno.c unistd.c string.c ctype.c divdi3.c malloc.c stdlib.c start.c printf.c stdio.c dirent.c gfx.c security.c ipc.c resource.c
OBJS := $(addprefix build/,$(SRCS:.c=.o)) build/linux_shim.o

# Graphics tests run the kernel's own gfx code and command interpreter, so
# they need the kernel sources (gfx/, syscall/, arch/...). By default they
# are expected one level above this libc folder; point KERNEL_SRC elsewhere
# if not. Without them the graphics test is skipped.
KERNEL_SRC ?= $(ROOT)/..
HAVE_KERNEL := $(wildcard $(KERNEL_SRC)/gfx/gfx.h $(KERNEL_SRC)/syscall/calls/sys_gfx_core.h)
# The resource manager, sprite class and sprite decoder, for SYS_RES_*
KERNEL_RES := resources/resource_manager resources/resource_io resources/classes/image dwm/rendering/sprite
KINC := build/kinc/kernel
KERNEL_GFX := gfx_draw gfx_surface gfx_text
ifeq ($(words $(HAVE_KERNEL)),2)
OBJS += build/kgfx.o
GFX_TEST := build/gfx_test build/resource_test
endif
# IPC tests run the kernel's ipc/ipc.c the same way
HAVE_KIPC := $(wildcard $(KERNEL_SRC)/ipc/ipc.c)
ifneq ($(HAVE_KIPC),)
OBJS += build/kipc.o
IPC_TEST := build/ipc_test
ifeq ($(words $(HAVE_KERNEL)),2)
IPC_TEST += build/input_demo_test
endif
endif
KFLAGS := -Ikstub -Ibuild/kinc

all: run

build:
	mkdir -p build

build/%.o: $(ROOT)/src/%.c | build
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

build/linux_shim.o: linux_shim.c | build
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

# Kernel headers the gfx code includes, laid out as <kernel/...>
build/kinc.stamp: | build
	mkdir -p $(KINC)/gfx $(KINC)/syscall/calls $(KINC)/arch/x86/drivers/display
	mkdir -p $(KINC)/resources/classes $(KINC)/dwm/rendering
	cp $(KERNEL_SRC)/gfx/*.h $(KINC)/gfx/
	cp $(KERNEL_SRC)/syscall/calls/sys_gfx_core.h $(KINC)/syscall/calls/
	cp $(KERNEL_SRC)/arch/x86/drivers/display/char_rom.h $(KINC)/arch/x86/drivers/display/
	cp $(KERNEL_SRC)/resources/*.h $(KINC)/resources/
	cp $(KERNEL_SRC)/resources/classes/image.h $(KINC)/resources/classes/
	cp $(KERNEL_SRC)/dwm/rendering/sprite.h $(KERNEL_SRC)/dwm/rendering/image.h $(KINC)/dwm/rendering/
	touch $@

build/kr_%.o: build/kinc.stamp
	$(CC) $(LIBC_CFLAGS) -DKERNEL_PLATFORM_X86 $(KFLAGS) -c $(KERNEL_SRC)/$(subst __,/,$*).c -o $@

build/k_%.o: $(KERNEL_SRC)/gfx/%.c build/kinc.stamp
	$(CC) $(LIBC_CFLAGS) -Wno-sign-compare $(KFLAGS) -c $< -o $@

# The kernel's IPC core, its headers as <kernel/ipc/...>
build/kipc_inc.stamp: | build
	mkdir -p $(KINC)/ipc
	cp $(KERNEL_SRC)/ipc/*.h $(KINC)/ipc/
	touch $@

# Its malloc / free are counted, so the test can see that nothing leaks
KIPC_COUNT := -Dmalloc=__test_ipc_malloc -Dfree=__test_ipc_free

build/k_ipc.o: $(KERNEL_SRC)/ipc/ipc.c build/kipc_inc.stamp
	$(CC) $(LIBC_CFLAGS) $(KFLAGS) $(KIPC_COUNT) -c $< -o $@

build/ipc_shim.o: ipc_shim.c build/kipc_inc.stamp
	$(CC) $(LIBC_CFLAGS) $(KFLAGS) -c $< -o $@

# ipc_sys_* / ipc_plat_* stay private, like the gfx code: only the shim's
# __test_* entry points are global
build/kipc.o: build/ipc_shim.o build/k_ipc.o
	$(LD) -m elf_i386 -r $^ -o $@
	objcopy --wildcard --keep-global-symbol='__test_*' $@

build/gfx_shim.o: gfx_shim.c build/kinc.stamp
	$(CC) $(LIBC_CFLAGS) -DKERNEL_PLATFORM_X86 $(KFLAGS) -c $< -o $@

# The kernel's gfx_* functions share their names with the libc's: link the
# kernel side into one object and keep only the shim's entry points global
build/kgfx.o: build/gfx_shim.o $(addprefix build/k_,$(addsuffix .o,$(KERNEL_GFX))) \
             $(addprefix build/kr_,$(addsuffix .o,$(subst /,__,$(KERNEL_RES))))
	$(LD) -m elf_i386 -r $^ -o $@
	objcopy --wildcard --keep-global-symbol='__test_gfx_*' $@

build/crt0.o: $(ROOT)/src/crt0.S | build
	$(CC) $(ARCH) -c $< -o $@

build/libc_test.a: $(OBJS)
	rm -f $@ && ar rcs $@ $^

build/input_demo_test: ../examples/input_demo.c

build/%: %.c build/libc_test.a build/crt0.o
	$(CC) $(USER_CFLAGS) -c $< -o build/$*.o
	$(CC) $(LDFLAGS) build/crt0.o build/$*.o build/libc_test.a -o $@

# Reference: the same case list through the host C library
build/fmt_cases_glibc: fmt_cases.c | build
	$(CC) -O2 -w $< -o $@

run: build/fmt_cases build/fmt_cases_glibc build/run_test $(GFX_TEST) $(IPC_TEST)
	./build/fmt_cases > build/fmt_ours.txt
	./build/fmt_cases_glibc > build/fmt_glibc.txt
	diff -u build/fmt_glibc.txt build/fmt_ours.txt && echo "printf/strtod/strtol: identical to glibc ($$(wc -l < build/fmt_ours.txt) lines)"
	cd build && rm -rf sandbox && mkdir sandbox && cd sandbox && ../run_test
ifdef GFX_TEST
	cd build && ./gfx_test
	cd build && rm -rf res && ./resource_test
else
	@echo "gfx: kernel sources not found at $(KERNEL_SRC) (set KERNEL_SRC=...), graphics test skipped"
endif
ifdef IPC_TEST
	cd build && ./ipc_test
ifeq ($(words $(HAVE_KERNEL)),2)
	cd build && ./input_demo_test
endif
else
	@echo "ipc: kernel sources not found at $(KERNEL_SRC) (set KERNEL_SRC=...), IPC test skipped"
endif

clean:
	rm -rf build

.PHONY: all run clean

