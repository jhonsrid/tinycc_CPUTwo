// CPUTwo crti stub — empty for bare-metal targets (no glibc .init section)
    .text
    .globl _init
_init:
    // empty

    .section .fini
    .globl _fini
_fini:
    // empty
