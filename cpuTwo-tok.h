/* ------------------------------------------------------------------ */
/* WARNING: relative order of tokens is important.                    */

/* Registers: r0-r15 */
 DEF_ASM(r0)
 DEF_ASM(r1)
 DEF_ASM(r2)
 DEF_ASM(r3)
 DEF_ASM(r4)
 DEF_ASM(r5)
 DEF_ASM(r6)
 DEF_ASM(r7)
 DEF_ASM(r8)
 DEF_ASM(r9)
 DEF_ASM(r10)
 DEF_ASM(r11)  /* fp */
 DEF_ASM(r12)  /* scratch */
 DEF_ASM(r13)  /* sp */
 DEF_ASM(r14)  /* lr */
 DEF_ASM(r15)  /* pc */

/* Register aliases */
 DEF_ASM(fp)   /* alias for r11 */
 DEF_ASM(sp)   /* alias for r13 */
 DEF_ASM(lr)   /* alias for r14 */
 DEF_ASM(pc)   /* alias for r15 */

/* R-type: 3-register instructions */
 DEF_ASM(add)
 DEF_ASM(sub)
 DEF_ASM(and)
 DEF_ASM(or)
 DEF_ASM(xor)
 DEF_ASM(lsl)
 DEF_ASM(lsr)
 DEF_ASM(asr)
 DEF_ASM(mul)
 DEF_ASM(div)
 DEF_ASM(mulh)
 DEF_ASM(mulhu)
 DEF_ASM(divu)
 DEF_ASM(mod)
 DEF_ASM(modu)
 DEF_ASM(addc)
 DEF_ASM(subc)
 DEF_ASM(lslr)
 DEF_ASM(lsrr)
 DEF_ASM(asrr)
 DEF_ASM(rolr)
 DEF_ASM(rorr)
 DEF_ASM(roli)
 DEF_ASM(rori)
 DEF_ASM(cas)
 DEF_ASM(lwx)
 DEF_ASM(lbx)
 DEF_ASM(lbux)
 DEF_ASM(lhx)
 DEF_ASM(lhux)
 DEF_ASM(swx)
 DEF_ASM(sbx)
 DEF_ASM(shx)

/* R-type: 2-register instructions */
 DEF_ASM(not)
 DEF_ASM(mov)
 DEF_ASM(callr)

/* R-type: compare */
 DEF_ASM(cmp)

/* I-type: rd, rs1, imm */
 DEF_ASM(addi)
 DEF_ASM(subi)
 DEF_ASM(andi)
 DEF_ASM(ori)
 DEF_ASM(xori)
 DEF_ASM(lsli)
 DEF_ASM(lsri)
 DEF_ASM(asri)

/* I-type: rd, imm (rs1=0) */
 DEF_ASM(movi)
 DEF_ASM(movhi)
 DEF_ASM(lui)

/* I-type: compare immediate */
 DEF_ASM(cmpi)

/* I-type: loads */
 DEF_ASM(lw)
 DEF_ASM(lh)
 DEF_ASM(lhu)
 DEF_ASM(lb)
 DEF_ASM(lbu)

/* I-type: stores */
 DEF_ASM(sw)
 DEF_ASM(sh)
 DEF_ASM(sb)

/* Branch instructions */
 DEF_ASM(beq)
 DEF_ASM(bne)
 DEF_ASM(blt)
 DEF_ASM(bge)
 DEF_ASM(bltu)
 DEF_ASM(bgeu)
 DEF_ASM(ba)
 DEF_ASM(bgt)
 DEF_ASM(ble)
 DEF_ASM(bgtu)
 DEF_ASM(bleu)

/* Jump/call */
 DEF_ASM(jmp)

/* Pseudo-instructions */
 DEF_ASM(nop)

/* System instructions */
 DEF_ASM(syscall)
 DEF_ASM(sysret)
 DEF_ASM(halt)
