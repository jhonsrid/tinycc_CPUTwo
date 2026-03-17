/*
 * CPUTwo code generator for TCC
 *
 * CPUTwo is a 32-bit little-endian RISC with fixed 32-bit instructions.
 * See architecture.md for the full ISA reference.
 *
 * Phase 1: integers, branches, function calls (all-stack ABI).
 * Phase 2: register args (r0-r3), soft-float (gen_opf/cvt), VLA.
 */

#ifdef TARGET_DEFS_ONLY

/* ------------------------------------------------------------------ */
/* Target definitions (included by tcc.h with TARGET_DEFS_ONLY)       */
/* ------------------------------------------------------------------ */

/*
 * Allocatable registers: r0-r10 and r12 (13 regs).
 * r11 = frame pointer (TREG_FP), not in allocatable pool.
 * r13 = SP (TREG_SP), r14 = LR (TREG_LR).
 * Total NB_REGS = 15 (indices 0-14).
 */
#define NB_REGS         15

/* Register classes */
#define RC_INT          (1 << 0)                /* general integer class */
#define RC_R(x)         (1 << (1 + (x)))        /* x = 0..12 individual regs */

#define RC_FLOAT        RC_INT                  /* soft-float: use int regs for floats */
#define RC_IRET         RC_R(0)                 /* r0: int return low */
#define RC_IRE2         RC_R(1)                 /* r1: int return high (long long) */
#define RC_FRET         RC_R(0)                 /* no FPU: float return via r0 */

#define REG_IRET        0                       /* TCC index of r0 */
#define REG_IRE2        1                       /* TCC index of r1 */
#define REG_FRET        0                       /* float return in r0 (soft-float) */

/* Special register TCC indices */
#define TREG_FP         11                      /* r11: frame pointer */
#define TREG_SP         13                      /* r13: stack pointer */
#define TREG_LR         14                      /* r14: link register */

/* Target properties */
#define PTR_SIZE        4
#define LDOUBLE_SIZE    8                       /* no 80-bit; long double == double */
#define LDOUBLE_ALIGN   4
#define MAX_ALIGN       8

/* CONFIG_TCC_ASM is defined in cputwo-asm.c (included after this file) */

/* Custom ELF relocation types for CPUTwo */
#define R_CPUTWO_NONE      0
#define R_CPUTWO_32        1   /* absolute 32-bit */
#define R_CPUTWO_PC20      2   /* 20-bit PC-relative branch offset */
#define R_CPUTWO_HI16      3   /* high 16 bits of address (LUI imm field) */
#define R_CPUTWO_LO16      4   /* low 16 bits of address (ORI/ADDI imm field) */
#define R_CPUTWO_CALL      5   /* JMP lr, target — 20-bit PC-relative call */
#define R_CPUTWO_RELATIVE  6
#define R_CPUTWO_COPY      7
#define R_CPUTWO_GLOB_DAT  8
#define R_CPUTWO_JMP_SLOT  9
#define R_CPUTWO_NUM       10

/* ------------------------------------------------------------------ */
#else /* !TARGET_DEFS_ONLY  — the actual code generator                */
/* ------------------------------------------------------------------ */

#define USING_GLOBALS
#include "tcc.h"

/* ------------------------------------------------------------------ */
/* Preprocessor macros for target machine definitions                  */
/* ------------------------------------------------------------------ */

ST_DATA const char * const target_machine_defs =
    "__cputwo__\0"
    "__CPUTWO__\0"
    "__LITTLE_ENDIAN__\0"
    ;

/* ------------------------------------------------------------------ */
/* Register class table                                                */
/* ------------------------------------------------------------------ */

ST_DATA const int reg_classes[NB_REGS] = {
    /* r0  */ RC_INT | RC_R(0),
    /* r1  */ RC_INT | RC_R(1),
    /* r2  */ RC_INT | RC_R(2),
    /* r3  */ RC_INT | RC_R(3),
    /* r4  */ RC_INT | RC_R(4),
    /* r5  */ RC_INT | RC_R(5),
    /* r6  */ RC_INT | RC_R(6),
    /* r7  */ RC_INT | RC_R(7),
    /* r8  */ RC_INT | RC_R(8),
    /* r9  */ RC_INT | RC_R(9),
    /* r10 */ RC_INT | RC_R(10),
    /* r11 */ 0,                    /* TREG_FP: frame pointer, not allocatable */
    /* r12 */ RC_INT | RC_R(12),   /* scratch, allocatable */
    /* r13 */ 1 << TREG_SP,        /* SP */
    /* r14 */ 1 << TREG_LR,        /* LR */
};

/* ------------------------------------------------------------------ */
/* CPUTwo opcode constants                                             */
/* ------------------------------------------------------------------ */

#define OP_ADD   0x00
#define OP_SUB   0x01
#define OP_AND   0x02
#define OP_OR    0x03
#define OP_XOR   0x04
#define OP_NOT   0x05
#define OP_LSL   0x06
#define OP_LSR   0x07
#define OP_ASR   0x08
#define OP_MUL   0x09
#define OP_DIV   0x0A
#define OP_LW    0x0B
#define OP_SW    0x0C
#define OP_B     0x0D
#define OP_JMP   0x0E
#define OP_MOVI  0x0F
/* 0x10 SYSCALL, 0x11 SYSRET, 0x12 HALT */
#define OP_MOVHI 0x13
#define OP_ADDI  0x14
#define OP_SUBI  0x15
#define OP_ANDI  0x16
#define OP_ORI   0x17
#define OP_XORI  0x18
#define OP_LSLI  0x19
#define OP_LSRI  0x1A
#define OP_ASRI  0x1B
#define OP_LH    0x1C
#define OP_LHU   0x1D
#define OP_LB    0x1E
#define OP_LBU   0x1F
#define OP_SH    0x20
#define OP_SB    0x21
#define OP_MULH  0x22
#define OP_MULHU 0x23
#define OP_DIVU  0x24
#define OP_MOD   0x25
#define OP_MODU  0x26
#define OP_MOV   0x27
#define OP_CMP   0x28
#define OP_CMPI  0x29
#define OP_CALLR 0x2A
#define OP_ADDC  0x2B
#define OP_SUBC  0x2C
#define OP_LSLR  0x2D
#define OP_LSRR  0x2E
#define OP_ASRR  0x2F
#define OP_LWX   0x30
#define OP_LBX   0x31
#define OP_LBUX  0x32
#define OP_SWX   0x33
#define OP_SBX   0x34
#define OP_LHX   0x35
#define OP_LHUX  0x36
#define OP_SHX   0x37
#define OP_LUI   0x38

/* Branch condition codes (cond field of B-type) */
#define COND_EQ  0   /* BEQ:  Z=1 */
#define COND_NE  1   /* BNE:  Z=0 */
#define COND_LT  2   /* BLT:  N≠V (signed) */
#define COND_GE  3   /* BGE:  N=V (signed) */
#define COND_LTU 4   /* BLTU: C=1 (unsigned) */
#define COND_GEU 5   /* BGEU: C=0 (unsigned) */
#define COND_BA  6   /* BA:   always */
#define COND_GT  7   /* BGT:  Z=0 and N=V */
#define COND_LE  8   /* BLE:  Z=1 or N≠V */
#define COND_GTU 9   /* BGTU: C=0 and Z=0 */
#define COND_LEU 10  /* BLEU: C=1 or Z=1 */

/* Physical register numbers */
#define PREG_SP  13
#define PREG_LR  14
#define PREG_PC  15
#define PREG_FP  11   /* r11 as frame pointer */
#define PREG_SCR 12   /* r12 as scratch for multi-instruction sequences */

/* Size of the prolog placeholder in bytes (4 instructions) */
#define FUNC_PROLOG_SIZE (4 * 4)

/* ------------------------------------------------------------------ */
/* Little-endian instruction emission                                   */
/* ------------------------------------------------------------------ */

/* Emit one 32-bit little-endian instruction word */
ST_FUNC void o(unsigned int insn)
{
    int ind1 = ind + 4;
    if (nocode_wanted)
        return;
    if (ind1 > cur_text_section->data_allocated)
        section_realloc(cur_text_section, ind1);
    cur_text_section->data[ind+0] = (insn >>  0) & 0xFF;
    cur_text_section->data[ind+1] = (insn >>  8) & 0xFF;
    cur_text_section->data[ind+2] = (insn >> 16) & 0xFF;
    cur_text_section->data[ind+3] = (insn >> 24) & 0xFF;
    ind = ind1;
}

static uint32_t read_le32(uint8_t *p)
{
    return  (uint32_t)p[0]        | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) |  ((uint32_t)p[3] << 24);
}

static void write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (v >>  0) & 0xFF;
    p[1] = (v >>  8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}

/* ------------------------------------------------------------------ */
/* Instruction format helpers                                           */
/* ------------------------------------------------------------------ */

/* R-type: opcode | rd | rs1 | rs2 | shift | func(=0) */
static void o_R(int op, int rd, int rs1, int rs2, int shift)
{
    o(((uint32_t)op << 24) | (rd << 20) | (rs1 << 16) | (rs2 << 12) | ((shift & 0x1F) << 7));
}

/* I/M-type: opcode | rd | rs1 | imm16 */
static void o_I(int op, int rd, int rs1, int imm16)
{
    o(((uint32_t)op << 24) | (rd << 20) | (rs1 << 16) | (imm16 & 0xFFFF));
}

/* B-type: 0x0D | cond | offset20 (PC-relative bytes) */
static void o_B(int cond, int offset)
{
    o(((uint32_t)OP_B << 24) | (cond << 20) | (offset & 0xFFFFF));
}

/* J-type: opcode | rd | offset20 (PC-relative bytes) */
static void o_J(int op, int rd, int offset)
{
    o(((uint32_t)op << 24) | (rd << 20) | (offset & 0xFFFFF));
}

/* ------------------------------------------------------------------ */
/* Register mapping: TCC index → CPUTwo physical register number       */
/* ------------------------------------------------------------------ */

/* TCC indices 0-12 map directly to r0-r12. TREG_SP(13)→r13, TREG_LR(14)→r14 */
static int ireg(int r)
{
    if (r == TREG_SP) return PREG_SP;
    if (r == TREG_LR) return PREG_LR;
    /* indices 0-12 → r0-r12 directly */
    return r;
}

/* ------------------------------------------------------------------ */
/* 32-bit constant loading                                             */
/* ------------------------------------------------------------------ */

/* Load arbitrary 32-bit value into physical register rd.
   Uses LUI + ORI (no ordering constraint, unlike MOVI+MOVHI). */
static void load_imm32(int rd, uint32_t val)
{
    uint16_t lo = val & 0xFFFF;
    uint16_t hi = (val >> 16) & 0xFFFF;
    if (hi == 0) {
        o_I(OP_MOVI, rd, 0, lo);           /* MOVI rd, lo (zero-extended) */
    } else if (lo == 0) {
        o_I(OP_LUI, rd, 0, hi);            /* LUI rd, hi  → rd = hi<<16   */
    } else {
        o_I(OP_LUI, rd, 0, hi);            /* LUI rd, hi  → rd = hi<<16   */
        o_I(OP_ORI, rd, rd, lo);           /* ORI rd, rd, lo               */
    }
}

/* ------------------------------------------------------------------ */
/* Forward jump chain state                                            */
/* ------------------------------------------------------------------ */

static int func_sub_sp_offset;  /* ind of prolog placeholder */

/* ------------------------------------------------------------------ */
/* load() — load an SValue into a register                             */
/* ------------------------------------------------------------------ */

ST_FUNC void load(int r, SValue *sv)
{
    int fr  = sv->r;
    int v   = fr & VT_VALMASK;
    int rr  = ireg(r);
    int fc  = sv->c.i;
    int bt  = sv->type.t & VT_BTYPE;
    int align, size;

    if (fr & VT_LVAL) {
        /* Load from memory */
        int op, br;
        size = type_size(&sv->type, &align);
        if (bt == VT_PTR || bt == VT_FUNC)
            size = PTR_SIZE;
        /* Choose load opcode */
        if (size == 1) {
            op = (sv->type.t & VT_UNSIGNED) ? OP_LBU : OP_LB;
        } else if (size == 2) {
            op = (sv->type.t & VT_UNSIGNED) ? OP_LHU : OP_LH;
        } else {
            op = OP_LW;
        }
        if (v == VT_LOCAL) {
            /* [r11 + fc] */
            o_I(op, rr, PREG_FP, fc);
        } else if (v == VT_LLOCAL) {
            /* double-indirect local: first load the pointer, then dereference */
            o_I(OP_LW, rr, PREG_FP, fc);
            o_I(op, rr, rr, 0);
        } else if (v < VT_CONST) {
            /* register base + offset 0 (TCC doesn't track offsets in regs yet) */
            br = ireg(v);
            o_I(op, rr, br, 0);
        } else if (v == VT_CONST) {
            /* absolute address */
            if (fr & VT_SYM) {
                /* symbol + addend: emit HI16/LO16 reloc pair */
                greloca(cur_text_section, sv->sym, ind, R_CPUTWO_HI16, fc);
                o_I(OP_LUI, rr, 0, 0);            /* patched by HI16 reloc */
                greloca(cur_text_section, sv->sym, ind, R_CPUTWO_LO16, fc);
                o_I(OP_ORI, rr, rr, 0);           /* patched by LO16 reloc */
            } else {
                load_imm32(rr, (uint32_t)fc);
            }
            o_I(op, rr, rr, 0);
        } else {
            tcc_error("load: unhandled lval case v=0x%x", v);
        }
    } else if (v == VT_CONST) {
        /* Load constant value (not lval — just the address or integer) */
        if (fr & VT_SYM) {
            /* Address of a symbol */
            greloca(cur_text_section, sv->sym, ind, R_CPUTWO_HI16, fc);
            o_I(OP_LUI, rr, 0, 0);
            greloca(cur_text_section, sv->sym, ind, R_CPUTWO_LO16, fc);
            o_I(OP_ORI, rr, rr, 0);
        } else {
            load_imm32(rr, (uint32_t)fc);
        }
    } else if (v == VT_LOCAL) {
        /* Load address of local variable */
        if (fc == 0) {
            o_R(OP_MOV, rr, PREG_FP, 0, 0);     /* MOV rr, r11 */
        } else {
            o_I(OP_ADDI, rr, PREG_FP, fc);       /* ADDI rr, r11, fc */
        }
    } else if (v < VT_CONST) {
        /* Register to register copy */
        if (rr != ireg(v))
            o_R(OP_MOV, rr, ireg(v), 0, 0);      /* MOV rr, rv */
    } else if (v == VT_CMP) {
        /* Materialise a boolean from flags */
        int op_c = vtop->cmp_op;
        int a    = vtop->cmp_r & 0xFF;
        int b    = (vtop->cmp_r >> 8) & 0xFF;
        int cond, invcond;
        /* Emit comparison instruction (CMPI or CMP depending on sentinel) */
        if (b == 0xFF) {
            o_I(OP_CMPI, 0, a, (uint16_t)(vtop->c.i & 0xFFFF));
        } else {
            o_R(OP_CMP, 0, a, b, 0);
        }
        switch (op_c) {
            case TOK_EQ:  cond = COND_EQ;  invcond = COND_NE;  break;
            case TOK_NE:  cond = COND_NE;  invcond = COND_EQ;  break;
            case TOK_LT:  cond = COND_LT;  invcond = COND_GE;  break;
            case TOK_GE:  cond = COND_GE;  invcond = COND_LT;  break;
            case TOK_LE:  cond = COND_LE;  invcond = COND_GT;  break;
            case TOK_GT:  cond = COND_GT;  invcond = COND_LE;  break;
            case TOK_ULT: cond = COND_LTU; invcond = COND_GEU; break;
            case TOK_UGE: cond = COND_GEU; invcond = COND_LTU; break;
            case TOK_ULE: cond = COND_LEU; invcond = COND_GTU; break;
            case TOK_UGT: cond = COND_GTU; invcond = COND_LEU; break;
            default: cond = COND_NE; invcond = COND_EQ; break;
        }
        (void)cond;
        /* MOVI rr, 0; B(inv) +8; MOVI rr, 1 */
        o_I(OP_MOVI, rr, 0, 0);
        o_B(invcond, 8);                           /* if cond false: skip MOVI 1 */
        o_I(OP_MOVI, rr, 0, 1);
    } else if ((v & ~1) == VT_JMP) {
        /* Boolean from jump chain */
        int t = v & 1;                             /* expected value at chain dest */
        o_I(OP_MOVI, rr, 0, t);                   /* initial: assume t */
        gjmp_addr(ind + 8);                        /* jump past the else MOVI */
        gsym(fc);                                  /* chain points here → else case */
        o_I(OP_MOVI, rr, 0, t ^ 1);               /* else: !t */
    } else {
        tcc_error("load: unhandled case v=0x%x", v);
    }
}

/* ------------------------------------------------------------------ */
/* store() — store a register to an lvalue                             */
/* ------------------------------------------------------------------ */

ST_FUNC void store(int r, SValue *sv)
{
    int fr  = sv->r & VT_VALMASK;
    int rr  = ireg(r);
    int fc  = sv->c.i;
    int bt  = sv->type.t & VT_BTYPE;
    int align, size;
    int op;

    size = type_size(&sv->type, &align);
    if (bt == VT_PTR || bt == VT_FUNC)
        size = PTR_SIZE;

    if (size == 1) {
        op = OP_SB;
    } else if (size == 2) {
        op = OP_SH;
    } else {
        op = OP_SW;
    }

    if (fr == VT_LOCAL) {
        o_I(op, rr, PREG_FP, fc);
    } else if (fr < VT_CONST) {
        /* register base */
        o_I(op, rr, ireg(fr), 0);
    } else if (fr == VT_CONST) {
        if (sv->r & VT_SYM) {
            greloca(cur_text_section, sv->sym, ind, R_CPUTWO_HI16, fc);
            o_I(OP_LUI, PREG_SCR, 0, 0);
            greloca(cur_text_section, sv->sym, ind, R_CPUTWO_LO16, fc);
            o_I(OP_ORI, PREG_SCR, PREG_SCR, 0);
        } else {
            load_imm32(PREG_SCR, (uint32_t)fc);
        }
        o_I(op, rr, PREG_SCR, 0);
    } else {
        tcc_error("store: unhandled case");
    }
}

/* ------------------------------------------------------------------ */
/* gcall_or_jmp — emit call or unconditional jump via vtop             */
/* ------------------------------------------------------------------ */

static void gcall_or_jmp(int docall)
{
    int rd = docall ? PREG_LR : 0;   /* save return addr in lr for calls */

    if ((vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST
        && (vtop->r & VT_SYM)
        && vtop->c.i == (int)vtop->c.i) {
        /* Symbolic call/jump: emit a JMP-type instruction with relocation */
        greloca(cur_text_section, vtop->sym, ind, R_CPUTWO_CALL, vtop->c.i);
        o_J(OP_JMP, rd, 0);           /* JMP rd, 0  (offset patched by linker) */
    } else if (vtop->r < VT_CONST) {
        /* Register indirect call/jump: CALLR rd, rs1 */
        o_R(OP_CALLR, rd, ireg(vtop->r), 0, 0);
    } else {
        /* Load address into scratch, then CALLR */
        load(TREG_LR, vtop);           /* use lr as scratch (overwritten anyway) */
        o_R(OP_CALLR, rd, PREG_LR, 0, 0);
    }
}

/* ------------------------------------------------------------------ */
/* gen_opi — integer operations                                        */
/* ------------------------------------------------------------------ */

ST_FUNC void gen_opi(int op)
{
    int a, b, d;
    int r_op = 0;  /* R-type opcode */
    int i_op = 0;  /* I-type opcode */

    /* Check for immediate on right-hand side */
    if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) {
        int fc = vtop->c.i;
        int fits16s = (fc >= -32768 && fc <= 32767);
        int fits16u = (fc >= 0 && fc <= 65535);

        /* Arithmetic / logical with immediate */
        switch (op) {
        case '+':
            if (fits16s) {
                vswap(); gv(RC_INT); a = ireg(vtop->r); vtop--;
                vtop->r = get_reg(RC_INT);
                o_I(OP_ADDI, ireg(vtop->r), a, fc);
                return;
            }
            break;
        case '-':
            if (fc > -32768 && fc <= 32768) {
                vswap(); gv(RC_INT); a = ireg(vtop->r); vtop--;
                vtop->r = get_reg(RC_INT);
                if (fc >= 0)
                    o_I(OP_SUBI, ireg(vtop->r), a, fc);
                else
                    o_I(OP_ADDI, ireg(vtop->r), a, -fc);
                return;
            }
            break;
        case '&':
            if (fits16u) {
                vswap(); gv(RC_INT); a = ireg(vtop->r); vtop--;
                vtop->r = get_reg(RC_INT);
                o_I(OP_ANDI, ireg(vtop->r), a, fc);
                return;
            }
            break;
        case '|':
            if (fits16u) {
                vswap(); gv(RC_INT); a = ireg(vtop->r); vtop--;
                vtop->r = get_reg(RC_INT);
                o_I(OP_ORI, ireg(vtop->r), a, fc);
                return;
            }
            break;
        case '^':
            if (fits16u) {
                vswap(); gv(RC_INT); a = ireg(vtop->r); vtop--;
                vtop->r = get_reg(RC_INT);
                o_I(OP_XORI, ireg(vtop->r), a, fc);
                return;
            }
            break;
        case TOK_SHL:
            if (fits16u && (fc & 31) == fc) {
                vswap(); gv(RC_INT); a = ireg(vtop->r); vtop--;
                vtop->r = get_reg(RC_INT);
                o_I(OP_LSLI, ireg(vtop->r), a, fc & 31);
                return;
            }
            break;
        case TOK_SHR:
            if (fits16u && (fc & 31) == fc) {
                vswap(); gv(RC_INT); a = ireg(vtop->r); vtop--;
                vtop->r = get_reg(RC_INT);
                o_I(OP_LSRI, ireg(vtop->r), a, fc & 31);
                return;
            }
            break;
        case TOK_SAR:
            if (fits16u && (fc & 31) == fc) {
                vswap(); gv(RC_INT); a = ireg(vtop->r); vtop--;
                vtop->r = get_reg(RC_INT);
                o_I(OP_ASRI, ireg(vtop->r), a, fc & 31);
                return;
            }
            break;
        case TOK_EQ:
        case TOK_NE:
        case TOK_LT:
        case TOK_GE:
        case TOK_LE:
        case TOK_GT:
        case TOK_ULT:
        case TOK_UGE:
        case TOK_ULE:
        case TOK_UGT:
            if (fits16s || fits16u) {
                vswap(); gv(RC_INT); a = ireg(vtop->r); vtop--;
                /* Load the immediate into scratch register r12 to avoid
                   storing fc in c.i (which overlaps jtrue/jfalse in the union). */
                load_imm32(PREG_SCR, (uint32_t)fc);
                vset_VT_CMP(op);
                vtop->cmp_r = a | (PREG_SCR << 8);
                return;
            }
            break;
        default:
            break;
        }
    }

    /* General register × register case */
    gv2(RC_INT, RC_INT);
    a = ireg(vtop[-1].r);
    b = ireg(vtop[0].r);
    vtop -= 2;
    d = get_reg(RC_INT);
    vtop++;
    vtop[0].r = d;

    switch (op) {
    default:
        if (op >= TOK_ULT && op <= TOK_GT) {
            /* Comparison: remember regs, let gjmp_cond emit CMP + branch */
            vset_VT_CMP(op);
            vtop->cmp_r = a | (b << 8);
            return;
        }
        tcc_error("gen_opi: unimplemented op '%s'", get_tok_str(op, NULL));
        break;
    case TOK_UMULL: {
        /* Unsigned 32x32→64 multiply: result low in vtop->r, high in vtop->r2 */
        /* a = left input reg, b = right input reg (both already loaded by gv2) */
        /* vtop->r = d already allocated as the low-word result register */
        int rhi = get_reg(RC_INT);  /* allocate second register for high word */
        o_R(OP_MUL,   ireg(d), a, b, 0);    /* low 32 bits: d = a * b */
        o_R(OP_MULHU, ireg(rhi), a, b, 0);  /* high 32 bits: rhi = (a*b)>>32 */
        vtop->r2 = rhi;
        return;
    }
    case '+':   r_op = OP_ADD;  break;
    case '-':   r_op = OP_SUB;  break;
    case '&':   r_op = OP_AND;  break;
    case '|':   r_op = OP_OR;   break;
    case '^':   r_op = OP_XOR;  break;
    case '*':   r_op = OP_MUL;  break;
    case '/':
    case TOK_PDIV:
                r_op = OP_DIV;  break;
    case '%':   r_op = OP_MOD;  break;
    case TOK_UDIV: r_op = OP_DIVU; break;
    case TOK_UMOD: r_op = OP_MODU; break;
    case TOK_SHL:  r_op = OP_LSLR; break;
    case TOK_SHR:  r_op = OP_LSRR; break;
    case TOK_SAR:  r_op = OP_ASRR; break;
    case TOK_ADDC1:
        /* 64-bit add low word — same as ADD, sets carry */
        r_op = OP_ADD;
        break;
    case TOK_ADDC2:
        /* 64-bit add high word — ADD with carry */
        r_op = OP_ADDC;
        break;
    case TOK_SUBC1:
        r_op = OP_SUB;
        break;
    case TOK_SUBC2:
        r_op = OP_SUBC;
        break;
    }

    (void)i_op;
    o_R(r_op, ireg(d), a, b, 0);
}

/* ------------------------------------------------------------------ */
/* Soft-float call helper                                              */
/* ------------------------------------------------------------------ */

/* Call a 2-arg soft-float helper; leave the float result on vtop.
   bt = VT_FLOAT, VT_DOUBLE, or VT_LDOUBLE.                          */
static void softfloat_call2(const char *name, int bt)
{
    vpush_helper_func(tok_alloc_const(name));
    vrott(3);
    gfunc_call(2);
    vpushi(0);
    vtop->type.t = bt;
    vtop->r = REG_FRET;
    if ((bt & VT_BTYPE) == VT_DOUBLE || (bt & VT_BTYPE) == VT_LDOUBLE)
        vtop->r2 = REG_IRE2;
}

/* Call a 2-arg soft-float comparison helper; leave VT_CMP on vtop.
   int_op is the corresponding INTEGER comparison (e.g., TOK_LT means
   helper returns <0 when true, so compare result < 0).               */
static void softfloat_cmp2(const char *name, int int_op)
{
    vpush_helper_func(tok_alloc_const(name));
    vrott(3);
    gfunc_call(2);
    /* Helper returned an int in r0 — compare against 0 via MOVI r12, 0 + CMP */
    vpushi(0);
    vtop->type.t = VT_INT;
    vtop->r = REG_IRET;
    vset_VT_CMP(int_op);
    /* Load 0 into scratch register r12 for the register compare */
    o_I(OP_MOVI, PREG_SCR, 0, 0);  /* r12 = 0 */
    vtop->cmp_r = 0 | (PREG_SCR << 8); /* CMP r0, r12 */
}

/* ------------------------------------------------------------------ */
/* gen_opf — floating-point operations (soft-float via libgcc)        */
/* ------------------------------------------------------------------ */

ST_FUNC void gen_opf(int op)
{
    int bt = vtop->type.t & VT_BTYPE;

    /* long double (128-bit TF via libgcc) */
    if (bt == VT_LDOUBLE) {
        switch (op) {
        case '+': softfloat_call2("__addtf3",  VT_LDOUBLE); return;
        case '-': softfloat_call2("__subtf3",  VT_LDOUBLE); return;
        case '*': softfloat_call2("__multf3",  VT_LDOUBLE); return;
        case '/': softfloat_call2("__divtf3",  VT_LDOUBLE); return;
        case TOK_EQ: softfloat_cmp2("__eqtf2",  TOK_EQ); return;
        case TOK_NE: softfloat_cmp2("__netf2",  TOK_NE); return;
        case TOK_LT: softfloat_cmp2("__lttf2",  TOK_LT); return;
        case TOK_LE: softfloat_cmp2("__letf2",  TOK_LE); return;
        case TOK_GT: softfloat_cmp2("__gttf2",  TOK_GT); return;
        case TOK_GE: softfloat_cmp2("__getf2",  TOK_GE); return;
        default:
            tcc_error("gen_opf: unimplemented ldouble op");
            return;
        }
    }

    /* float or double */
    int is_d = (bt == VT_DOUBLE);
    switch (op) {
    case '+':
        softfloat_call2(is_d ? "__adddf3" : "__addsf3", bt); return;
    case '-':
        softfloat_call2(is_d ? "__subdf3" : "__subsf3", bt); return;
    case '*':
        softfloat_call2(is_d ? "__muldf3" : "__mulsf3", bt); return;
    case '/':
        softfloat_call2(is_d ? "__divdf3" : "__divsf3", bt); return;
    /* Comparisons: helpers return int (neg/zero/pos); compare against 0 */
    case TOK_EQ:
        softfloat_cmp2(is_d ? "__eqdf2"  : "__eqsf2",  TOK_EQ); return;
    case TOK_NE:
        softfloat_cmp2(is_d ? "__nedf2"  : "__nesf2",  TOK_NE); return;
    case TOK_LT:
        softfloat_cmp2(is_d ? "__ltdf2"  : "__ltsf2",  TOK_LT); return;
    case TOK_LE:
        softfloat_cmp2(is_d ? "__ledf2"  : "__lesf2",  TOK_LE); return;
    case TOK_GT:
        softfloat_cmp2(is_d ? "__gtdf2"  : "__gtsf2",  TOK_GT); return;
    case TOK_GE:
        softfloat_cmp2(is_d ? "__gedf2"  : "__gesf2",  TOK_GE); return;
    default:
        tcc_error("gen_opf: unimplemented op '%s'", get_tok_str(op, NULL));
    }
}

/* ------------------------------------------------------------------ */
/* gen_cvt_sxtw — sign-extend word (no-op: 32-bit target)             */
/* ------------------------------------------------------------------ */

ST_FUNC void gen_cvt_sxtw(void)
{
    /* On a 32-bit target registers are naturally 32-bit; no action needed. */
}

/* ------------------------------------------------------------------ */
/* gen_cvt_itof — integer to float conversion (soft-float)             */
/* Phase 1: long double only; float/double deferred to Phase 2         */
/* ------------------------------------------------------------------ */

ST_FUNC void gen_cvt_itof(int t)
{
    int bt   = t & VT_BTYPE;
    int l    = (vtop->type.t & VT_BTYPE) == VT_LLONG;
    int u    = vtop->type.t & VT_UNSIGNED;
    const char *name;

    if (bt == VT_LDOUBLE) {
        if (l) name = u ? "__floatunditf" : "__floatditf";
        else   name = u ? "__floatunsitf" : "__floatsitf";
    } else if (bt == VT_DOUBLE) {
        if (l) name = u ? "__floatundidf" : "__floatdidf";
        else   name = u ? "__floatunsidf" : "__floatsidf";
    } else { /* VT_FLOAT */
        if (l) name = u ? "__floatundisf" : "__floatdisf";
        else   name = u ? "__floatunsisf" : "__floatsisf";
    }
    vpush_helper_func(tok_alloc_const(name));
    vrott(2);
    gfunc_call(1);
    vpushi(0);
    vtop->type.t = t;
    vtop->r = REG_FRET;
    if ((bt == VT_DOUBLE) || (bt == VT_LDOUBLE))
        vtop->r2 = REG_IRE2;
}

/* ------------------------------------------------------------------ */
/* gen_cvt_ftoi — float to integer conversion (soft-float)             */
/* ------------------------------------------------------------------ */

ST_FUNC void gen_cvt_ftoi(int t)
{
    int ft   = vtop->type.t & VT_BTYPE;
    int l    = (t & VT_BTYPE) == VT_LLONG;
    int u    = t & VT_UNSIGNED;
    const char *name;

    if (ft == VT_LDOUBLE) {
        if (l) name = u ? "__fixunstfdi" : "__fixtfdi";
        else   name = u ? "__fixunstfsi" : "__fixtfsi";
    } else if (ft == VT_DOUBLE) {
        if (l) name = u ? "__fixunsdfdi" : "__fixdfdi";
        else   name = u ? "__fixunsdfsi" : "__fixdfsi";
    } else { /* VT_FLOAT */
        if (l) name = u ? "__fixunssfdi" : "__fixsfdi";
        else   name = u ? "__fixunssfsi" : "__fixsfsi";
    }
    vpush_helper_func(tok_alloc_const(name));
    vrott(2);
    gfunc_call(1);
    vpushi(0);
    vtop->type.t = t;
    vtop->r = REG_IRET;
    if ((t & VT_BTYPE) == VT_LLONG)
        vtop->r2 = REG_IRE2;
}

/* ------------------------------------------------------------------ */
/* gen_cvt_ftof — float to float conversion                            */
/* ------------------------------------------------------------------ */

ST_FUNC void gen_cvt_ftof(int dt)
{
    int st  = vtop->type.t & VT_BTYPE;
    dt &= VT_BTYPE;
    if (st == dt)
        return;

    const char *name;
    if (dt == VT_LDOUBLE)
        name = (st == VT_FLOAT) ? "__extendsftf2" : "__extenddftf2";
    else if (st == VT_LDOUBLE)
        name = (dt == VT_FLOAT) ? "__trunctfsf2"  : "__trunctfdf2";
    else if (dt == VT_DOUBLE)
        name = "__extendsfdf2";
    else
        name = "__truncdfsf2";

    vpush_helper_func(tok_alloc_const(name));
    vrott(2);
    gfunc_call(1);
    vpushi(0);
    vtop->type.t = dt;
    vtop->r = REG_FRET;
    if (dt == VT_DOUBLE || dt == VT_LDOUBLE)
        vtop->r2 = REG_IRE2;
}

/* ------------------------------------------------------------------ */
/* gen_fill_nops — fill bytes with NOP instructions                    */
/* ------------------------------------------------------------------ */

ST_FUNC void gen_fill_nops(int bytes)
{
    if (bytes & 3)
        tcc_error("gen_fill_nops: size not multiple of 4");
    while (bytes > 0) {
        o_I(OP_MOVI, 0, 0, 0);    /* MOVI r0, 0  (canonical NOP) */
        bytes -= 4;
    }
}

/* ------------------------------------------------------------------ */
/* Branch generation                                                   */
/* ------------------------------------------------------------------ */

/* Emit unconditional forward branch; chain with t.
   The stored raw little-endian word IS the chain pointer (0 = end of chain). */
ST_FUNC int gjmp(int t)
{
    if (nocode_wanted)
        return t;
    /* Store 't' as a raw little-endian word (the chain link).
       This location will be patched by gsym_addr() to a real BA instruction. */
    o(t);
    return ind - 4;
}

/* Emit unconditional branch to known address a */
ST_FUNC void gjmp_addr(int a)
{
    int32_t offset = a - ind;      /* signed byte offset from this instruction */
    if (offset >= -(1 << 19) && offset < (1 << 19)) {
        o_B(COND_BA, offset);
    } else {
        /* Out of 20-bit range: load address into scratch and jump */
        load_imm32(PREG_SCR, (uint32_t)a);
        o_R(OP_MOV, PREG_PC, PREG_SCR, 0, 0);  /* MOV pc, r12 */
    }
}

/* Patch all forward branches in chain t to resolve to address a */
ST_FUNC void gsym_addr(int t_, int a_)
{
    uint32_t t = (uint32_t)t_;
    uint32_t a = (uint32_t)a_;
    while (t) {
        uint8_t  *ptr  = cur_text_section->data + t;
        uint32_t  next = read_le32(ptr);          /* chain pointer stored in the word */
        int32_t   off  = (int32_t)(a - t);
        if (off < -(1 << 19) || off >= (1 << 19))
            tcc_error("gsym_addr: branch out of range (off=%d)", off);
        write_le32(ptr, ((uint32_t)OP_B << 24) | (COND_BA << 20) | (off & 0xFFFFF));
        t = next;
    }
}

/* Append jump list n into chain t; return new head */
ST_FUNC int gjmp_append(int n, int t)
{
    void *p;
    if (n) {
        uint32_t n1 = n, n2;
        /* Walk to end of chain n */
        while ((n2 = read_le32(p = cur_text_section->data + n1)))
            n1 = n2;
        /* Link end of n to t */
        write_le32(p, t);
        t = n;
    }
    return t;
}

/* Emit conditional branch to t when condition op IS true.
   Falls through when condition is false. */
ST_FUNC int gjmp_cond(int op, int t)
{
    int a       = vtop->cmp_r & 0xFF;
    int b       = (vtop->cmp_r >> 8) & 0xFF;
    int invcond;   /* inverted condition: branches OVER gjmp when cond false */

    /* Emit CMP: always register vs register.
       For immediate comparisons, the immediate was pre-loaded into r12 (PREG_SCR). */
    o_R(OP_CMP, 0, a, b, 0);

    switch (op) {
    case TOK_EQ:  invcond = COND_NE;  break;
    case TOK_NE:  invcond = COND_EQ;  break;
    case TOK_LT:  invcond = COND_GE;  break;
    case TOK_GE:  invcond = COND_LT;  break;
    case TOK_LE:  invcond = COND_GT;  break;
    case TOK_GT:  invcond = COND_LE;  break;
    case TOK_ULT: invcond = COND_GEU; break;
    case TOK_UGE: invcond = COND_LTU; break;
    case TOK_ULE: invcond = COND_GTU; break;
    case TOK_UGT: invcond = COND_LEU; break;
    default:      invcond = COND_EQ;  break;
    }
    /* B(inverted) +8: if condition FALSE, skip over the unconditional branch */
    o_B(invcond, 8);
    /* Unconditional branch to true chain (patched by gsym later) */
    return gjmp(t);
}

/* ------------------------------------------------------------------ */
/* gfunc_call — generate a function call                               */
/* ------------------------------------------------------------------ */

/*
 * Phase 2 ABI: first 4 args in r0-r3, remaining args on stack right-to-left.
 * Return value in r0 (or r0:r1 for long long / double).
 *
 * vtop[0]         = arg[nb_args-1]  (last arg, pushed / loaded first)
 * vtop[-(nb_args-1)] = arg[0]       (first arg → r0)
 * vtop[-nb_args]  = function
 */
ST_FUNC void gfunc_call(int nb_args)
{
    int i, r, args_size = 0;
    int nreg   = nb_args < 4 ? nb_args : 4;   /* args that go in r0-r3 */
    int nstack = nb_args - nreg;               /* args that go on stack  */

    /* Spill all register values to memory so we can freely use r0-r3 */
    save_regs(nb_args + 1);

    /* Push stack args right-to-left: vtop[0]..vtop[nstack-1] */
    for (i = 0; i < nstack; i++) {
        r = gv(RC_INT);
        o_I(OP_ADDI, PREG_SP, PREG_SP, (uint16_t)(-4));
        o_I(OP_SW,   ireg(r), PREG_SP, 0);
        args_size += 4;
        vtop--;
    }

    /* Load register args into r(nreg-1) .. r0.
       After the nstack pops above, vtop[0]=arg[nreg-1], vtop[-(nreg-1)]=arg[0]. */
    for (i = nreg - 1; i >= 0; i--) {
        gv(RC_R(i));   /* force current vtop into physical register i */
        vtop--;
    }

    /* vtop[0] = function */
    gcall_or_jmp(1);
    vtop--;

    /* Clean up stack args */
    if (args_size) {
        if (args_size <= 32767)
            o_I(OP_ADDI, PREG_SP, PREG_SP, args_size);
        else {
            load_imm32(PREG_SCR, args_size);
            o_R(OP_ADD, PREG_SP, PREG_SP, PREG_SCR, 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* gfunc_prolog — function entry; reserve frame placeholder            */
/* ------------------------------------------------------------------ */

ST_FUNC void gfunc_prolog(Sym *func_sym)
{
    CType *func_type = &func_sym->type;
    Sym *sym;
    int  size, align;
    int  ri = 0;   /* next incoming register index (0 = r0) */

    /*
     * Frame layout (addresses relative to r11 = FP = old SP):
     *   [FP - 4]             saved LR
     *   [FP - 8]             saved old FP
     *   [FP - 12]            hidden-ptr spill (r0) if sret > 8 bytes
     *   [FP - 12] or [-16..] reg-arg spills for explicit params
     *   [FP - ...]           locals (allocated during function body)
     *   [FP + 0]             first stack arg (if any)
     *   [FP + 4]             second stack arg ...
     */
    loc    = -8;
    func_vc = 0;

    /* ---- Hidden struct-return pointer (arrives in r0) ---- */
    {
        int sz = type_size(&func_vt, &align);
        if (sz > 8) {
            loc -= PTR_SIZE;   /* slot at [FP - 12] */
            func_vc = loc;
            ri = 1;            /* r0 used for hidden ptr; explicit params start at r1 */
        }
    }

    /* ---- Count explicit register args and record their types ---- */
    int nreg_explicit = 0;
    int spill_op[4]; /* store opcode for each reg arg spill */
    {
        Sym *s = func_type->ref->next;
        for (; s && (ri + nreg_explicit) < 4; s = s->next) {
            int bt = s->type.t & VT_BTYPE;
            /* Use narrow stores so that LB/LBU/LH/LHU in the function body
             * read the correct byte/halfword from the spill slot.
             * On little-endian, SB/SH place the value at the lowest address
             * where LB/LBU/LH/LHU will read it correctly. */
            if (bt == VT_BYTE || bt == VT_BOOL)
                spill_op[nreg_explicit] = OP_SB;
            else if (bt == VT_SHORT)
                spill_op[nreg_explicit] = OP_SH;
            else
                spill_op[nreg_explicit] = OP_SW;
            nreg_explicit++;
        }
    }

    /* For variadic functions, also spill remaining arg registers (after named
     * params) so __builtin_va_arg can find them adjacent in the frame.
     * __builtin_va_start sets ap = &last_named - sizeof(last_named), which
     * points to the slot just below the last named param's spill slot.      */
    int nreg_spill = nreg_explicit;
    if (func_var) {
        while ((ri + nreg_spill) < 4) {
            spill_op[nreg_spill] = OP_SW;  /* variadic args spilled as full words */
            nreg_spill++;
        }
    }

    /* ---- Reserve frame slots for reg-arg spills ---- */
    /* Hidden-ptr spill (if any) already ate [FP - 12].
       Explicit reg-arg[0] spill goes at loc - 4, etc.            */
    int spill_base = loc - 4;   /* offset for explicit reg-arg[0] spill */
    loc -= nreg_spill * 4;

    /* ---- Reserve prolog placeholder (patched in gfunc_epilog) ---- */
    func_sub_sp_offset = ind;
    ind += FUNC_PROLOG_SIZE;

    /* ---- Emit register spills (run after patched prolog sets up FP) ---- */
    if (func_vc) {
        /* Spill hidden ptr (r0) */
        o_I(OP_SW, 0, PREG_FP, func_vc);
    }
    for (int i = 0; i < nreg_spill; i++) {
        o_I(spill_op[i], ri + i, PREG_FP, spill_base - i * 4);
    }

    /* ---- Set up parameter symbol locations ---- */
    sym          = func_type->ref;
    int ri2      = ri;      /* register index for next explicit param */
    int stk_addr = 0;       /* offset from FP for stack params */

    while ((sym = sym->next) != NULL) {
        size  = type_size(&sym->type, &align);
        if (align < 1) align = 1;
        if (ri2 < 4) {
            /* Param arrives in register ri2, spilled to frame */
            gfunc_set_param(sym, spill_base - (ri2 - ri) * 4, 0);
            ri2++;
        } else {
            /* Param is on stack above FP */
            stk_addr = (stk_addr + align - 1) & -align;
            gfunc_set_param(sym, stk_addr, 0);
            stk_addr += (size + 3) & ~3;
        }
    }
}

/* ------------------------------------------------------------------ */
/* gfunc_epilog — function exit; patch prolog placeholder              */
/* ------------------------------------------------------------------ */

ST_FUNC void gfunc_epilog(void)
{
    int d, v, saved_ind, large_ofs_ind;

    /* Total frame size: align -loc to 4 bytes */
    v = ((-loc) + 3) & ~3;
    d = v;

    /* --- Emit epilog (ret sequence) at current position --- */
    /*   ADDI sp, r11, 0    ← restore SP = old SP             */
    o_I(OP_ADDI, PREG_SP, PREG_FP, 0);
    /*   LW lr,  [r11 - 4]  ← restore LR                      */
    o_I(OP_LW,   PREG_LR, PREG_FP, -4);
    /*   LW r11, [r11 - 8]  ← restore old FP                  */
    o_I(OP_LW,   PREG_FP, PREG_FP, -8);
    /*   MOV pc, lr          ← return                          */
    o_R(OP_MOV,  PREG_PC, PREG_LR, 0, 0);

    /* Handle large frame (d doesn't fit in signed 16-bit ADDI) */
    large_ofs_ind = ind;
    if (d > 32767) {
        /* Emit extended prolog at end-of-function, jump back to param stores */
        o_I(OP_LUI,  PREG_SCR, 0, (d >> 16) & 0xFFFF);
        o_I(OP_ORI,  PREG_SCR, PREG_SCR, d & 0xFFFF);
        o_R(OP_SUB,  PREG_SP,  PREG_SP, PREG_SCR, 0);
        /* SW lr at [sp + d - 4]: we need scratch to compute address */
        load_imm32(PREG_SCR, (uint32_t)(d - 4));
        o_R(OP_ADD,  PREG_SCR, PREG_SP, PREG_SCR, 0);
        o_I(OP_SW,   PREG_LR, PREG_SCR, 0);
        load_imm32(PREG_SCR, (uint32_t)(d - 8));
        o_R(OP_ADD,  PREG_SCR, PREG_SP, PREG_SCR, 0);
        o_I(OP_SW,   PREG_FP, PREG_SCR, 0);
        /* ADDI r11, sp, d: only works if d fits, otherwise we need full load */
        if (d <= 32767) {
            o_I(OP_ADDI, PREG_FP, PREG_SP, d);
        } else {
            load_imm32(PREG_SCR, (uint32_t)d);
            o_R(OP_ADD, PREG_FP, PREG_SP, PREG_SCR, 0);
        }
        /* Jump back to instruction after the placeholder */
        gjmp_addr(func_sub_sp_offset + FUNC_PROLOG_SIZE);
    }

    saved_ind = ind;

    /* --- Patch prolog placeholder --- */
    ind = func_sub_sp_offset;

    if (d <= 32767) {
        /* Small frame: 4 instructions */
        o_I(OP_ADDI, PREG_SP, PREG_SP, (uint16_t)(-d));          /* sp -= d */
        o_I(OP_SW,   PREG_LR, PREG_SP, d - 4);                   /* save LR */
        o_I(OP_SW,   PREG_FP, PREG_SP, d - 8);                   /* save FP */
        o_I(OP_ADDI, PREG_FP, PREG_SP, d);                        /* FP = sp+d */
    } else {
        /* Large frame: jump to extended prolog at end-of-function */
        gjmp_addr(large_ofs_ind);
        /* Fill remaining slots with NOPs */
        while (ind < func_sub_sp_offset + FUNC_PROLOG_SIZE)
            o_I(OP_MOVI, 0, 0, 0);
    }

    ind = saved_ind;
}

/* ------------------------------------------------------------------ */
/* gfunc_sret — struct return convention                               */
/* ------------------------------------------------------------------ */

/*
 * Returns:
 *   0  → struct too large; use hidden pointer (generic code handles it)
 *   1  → fits in one register (r0)
 *   2  → fits in two registers (r0:r1)
 */
ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret,
                        int *ret_align, int *regsize)
{
    int align, size = type_size(vt, &align);
    *ret_align = 1;
    *regsize   = 4;
    if (size > 8)
        return 0;           /* hidden pointer */
    if (size > 4) {
        ret->t   = VT_INT;
        ret->ref = NULL;
        return 2;           /* r0:r1 */
    }
    ret->t   = VT_INT;
    ret->ref = NULL;
    return 1;               /* r0 only */
}

/* arch_transfer_ret_regs: used when gfunc_sret returns 2 (two regs) */
ST_FUNC void arch_transfer_ret_regs(int aftercall)
{
    /* Low word at offset 0, high word at offset 4 */
    if (vtop->r != (VT_LOCAL | VT_LVAL))
        tcc_error("arch_transfer_ret_regs: expected VT_LOCAL|VT_LVAL");
    vpushv(vtop);
    vtop->type.t = VT_INT;
    (aftercall ? store : load)(REG_IRET, vtop);
    vtop->c.i += 4;
    (aftercall ? store : load)(REG_IRE2, vtop);
    vtop--;
}

/* ------------------------------------------------------------------ */
/* ggoto — computed goto                                               */
/* ------------------------------------------------------------------ */

ST_FUNC void ggoto(void)
{
    gcall_or_jmp(0);
    vtop--;
}

/* ------------------------------------------------------------------ */
/* gen_increment_tcov — test coverage counter increment                */
/* ------------------------------------------------------------------ */

ST_FUNC void gen_increment_tcov(SValue *sv)
{
    int r1, r2;

    vpushv(sv);
    vtop->r = r1 = get_reg(RC_INT);
    r2 = get_reg(RC_INT);

    /* Load address of counter into r1 */
    greloca(cur_text_section, sv->sym, ind, R_CPUTWO_HI16, 0);
    o_I(OP_LUI, ireg(r1), 0, 0);
    greloca(cur_text_section, sv->sym, ind, R_CPUTWO_LO16, 0);
    o_I(OP_ORI, ireg(r1), ireg(r1), 0);

    /* Load 32-bit counter value and increment */
    o_I(OP_LW,   ireg(r2), ireg(r1), 0);
    o_I(OP_ADDI, ireg(r2), ireg(r2), 1);
    o_I(OP_SW,   ireg(r2), ireg(r1), 0);

    vpop();
}

/* ------------------------------------------------------------------ */
/* VLA support                                                         */
/* ------------------------------------------------------------------ */

ST_FUNC void gen_vla_sp_save(int addr)
{
    /* SW sp, [r11 + addr] */
    o_I(OP_SW, PREG_SP, PREG_FP, addr);
}

ST_FUNC void gen_vla_sp_restore(int addr)
{
    /* LW sp, [r11 + addr] */
    o_I(OP_LW, PREG_SP, PREG_FP, addr);
}

ST_FUNC void gen_vla_alloc(CType *type, int align)
{
    int rr;
    rr = ireg(gv(RC_INT));
    /* Round up size to 4-byte alignment, then subtract from SP */
    o_I(OP_ADDI, rr, rr, 3);
    o_I(OP_ANDI, rr, rr, (uint16_t)(~3));  /* round up to 4 */
    o_R(OP_SUB, PREG_SP, PREG_SP, rr, 0);
    vpop();
}

#endif /* TARGET_DEFS_ONLY */
