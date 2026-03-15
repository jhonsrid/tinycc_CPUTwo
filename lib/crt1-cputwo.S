// CPUTwo CRT startup
// Entry point: _start (linked at ELF_START_ADDR = 0x00001000)
//
// Register conventions:
//   r13 = SP (stack pointer)
//   r11 = FP (frame pointer)
//   r14 = LR (link register / return address)
//   r0-r3 = function arguments / return values
//
// Stack is placed at top of 64 MB RAM: 0x03F00000

    .text
    .globl _start
_start:
    lui   r13, 0x03F0   // SP = 0x03F00000 (top of 64MB RAM)
    movi  r11, 0        // FP = 0 (terminates backtraces)
    movi  r0, 0         // argc = 0
    movi  r1, 0         // argv = NULL
    movi  r2, 0         // envp = NULL
    jmp   lr, main      // call main(0, NULL, NULL); lr receives return address
    halt                // if main returns, halt the CPU
