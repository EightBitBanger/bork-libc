/*
 * Process entry point. The loader jumps here in ring 3 with ESP at the top
 * of the user stack. Kept in .text.start so the linker script places it at
 * the very start of the image (a flat binary can be entered at offset 0).
 */

    .section .text.start, "ax"
    .global _start
    .type _start, @function
_start:
    xorl    %ebp, %ebp          /* Ends frame-pointer backtraces */
    andl    $-16, %esp          /* i386 SysV: ESP % 16 == 0 at each CALL */
    call    __libc_start        /* Never returns */
1:  jmp     1b

    .section .note.GNU-stack, "", @progbits

