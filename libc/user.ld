/*
 * Link script for user programs.
 *
 * __image_base: where the loader places the program. 0x40000000 is the
 * start of the user half (VM_USER_START); override with
 *   -Wl,--defsym=__image_base=0x........
 * if the loader uses another address.
 *
 * .bss is folded into .data, so the file holds its zeros. Whatever the
 * loader does (ELF segments or a flat binary copied in), every byte up to
 * _end is then mapped and zeroed, and the kernel's heap, which starts right
 * after the image, can never overlap uninitialized data.
 */

ENTRY(_start)

SECTIONS
{
    . = DEFINED(__image_base) ? __image_base : 0x40000000;
    __image_start = .;

    .text : {
        KEEP(*(.text.start))        /* crt0: first byte of the image */
        *(.text .text.*)
    }

    .rodata : ALIGN(16) {
        *(.rodata .rodata.*)
    }

    .init_array : ALIGN(4) {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
    }

    .fini_array : ALIGN(4) {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.fini_array .dtors))
        PROVIDE_HIDDEN(__fini_array_end = .);
    }

    /* Writable data on its own pages, so text can be mapped read-only */
    .data : ALIGN(4096) {
        *(.data .data.*)
        . = ALIGN(16);
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    . = ALIGN(4096);
    __image_end = .;
    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame_hdr)
        *(.interp .dynamic .dynsym .dynstr .hash .gnu.hash)
    }
}

