/*
 * CPUTwo inline assembler for TCC
 *
 * CPUTwo is a 32-bit little-endian RISC CPU.
 * All instructions are 32-bit, little-endian encoded.
 */

#ifdef TARGET_DEFS_ONLY

#define CONFIG_TCC_ASM
#define NB_ASM_REGS 16

ST_FUNC void g(int c);
ST_FUNC void gen_le16(int c);
ST_FUNC void gen_le32(int c);

#else /* !TARGET_DEFS_ONLY */

#define USING_GLOBALS
#include "tcc.h"

/* --- Byte/half/word emission (little-endian) --- */
ST_FUNC void g(int c)
{
    int ind1;
    if (nocode_wanted)
        return;
    ind1 = ind + 1;
    if (ind1 > cur_text_section->data_allocated)
        section_realloc(cur_text_section, ind1);
    cur_text_section->data[ind] = c & 0xFF;
    ind = ind1;
}

ST_FUNC void gen_le16(int i)
{
    g(i);
    g(i >> 8);
}

ST_FUNC void gen_le32(int i)
{
    g(i);
    g(i >> 8);
    g(i >> 16);
    g(i >> 24);
}

/* gen_expr32: emit a 32-bit address/value (little-endian) */
ST_FUNC void gen_expr32(ExprValue *pe)
{
    gen_le32(pe->v);
}

/* --- Instruction encoding helpers --- */

/* R-type: [op 8 | rd 4 | rs1 4 | rs2 4 | shift 5 | func 7] */
static void emit_R(int op, int rd, int rs1, int rs2, int shift)
{
    gen_le32(((uint32_t)op << 24) | ((rd & 0xF) << 20) | ((rs1 & 0xF) << 16)
             | ((rs2 & 0xF) << 12) | ((shift & 0x1F) << 7));
}

/* I-type: [op 8 | rd 4 | rs1 4 | imm16 16] */
static void emit_I(int op, int rd, int rs1, int imm)
{
    gen_le32(((uint32_t)op << 24) | ((rd & 0xF) << 20) | ((rs1 & 0xF) << 16)
             | (uint16_t)(int16_t)imm);
}

/* B-type: [op 8 | cond 4 | offset20 20] */
static void emit_B(int cond, int offset)
{
    gen_le32(((uint32_t)0x0D << 24) | ((cond & 0xF) << 20) | (offset & 0xFFFFF));
}

/* J-type: [op 8 | rd 4 | offset20 20] */
static void emit_J(int rd, int offset)
{
    gen_le32(((uint32_t)0x0E << 24) | ((rd & 0xF) << 20) | (offset & 0xFFFFF));
}

/* --- Operand parsing helpers --- */

static int parse_reg(void)
{
    int r = asm_parse_regvar(tok);
    if (r < 0)
        tcc_error("expected register");
    next();
    return r;
}

static void skip_comma(void)
{
    if (tok == ',')
        next();
    else
        tcc_error("',' expected");
}

/* Parse: rd, rs1, rs2 -> 3 register operands */
static void parse_3reg(int *rd, int *rs1, int *rs2)
{
    *rd  = parse_reg(); skip_comma();
    *rs1 = parse_reg(); skip_comma();
    *rs2 = parse_reg();
}

/* Parse: rd, rs1 -> 2 register operands */
static void parse_2reg(int *rd, int *rs1)
{
    *rd  = parse_reg(); skip_comma();
    *rs1 = parse_reg();
}

/* Parse: rd, rs1, imm -> 2 reg + integer constant */
static int parse_2reg_int(TCCState *s1, int *rd, int *rs1)
{
    ExprValue e;
    *rd  = parse_reg(); skip_comma();
    *rs1 = parse_reg(); skip_comma();
    asm_expr(s1, &e);
    if (e.sym)
        tcc_error("constant expected for shift/immediate");
    return (int)e.v;
}

/* Parse: rd, imm(rs1)  -- memory access */
static void parse_mem(TCCState *s1, int *data_reg, int *base_reg, int *imm)
{
    ExprValue e;
    *data_reg = parse_reg(); skip_comma();
    /* Expect: imm(rs1) */
    asm_expr(s1, &e);
    if (tok == '(') {
        next();
        *base_reg = parse_reg();
        if (tok != ')')
            tcc_error("')' expected");
        next();
    } else {
        tcc_error("expected imm(reg) form for memory operand");
    }
    if (e.sym)
        tcc_error("constant expected for memory offset");
    *imm = (int)e.v;
}

/* --- Branch helpers --- */
static void emit_branch(TCCState *s1, int cond, ExprValue *e)
{
    if (e->sym) {
        /* forward/external label: emit relocation */
        greloc(cur_text_section, e->sym, ind, R_CPUTWO_PC20);
        emit_B(cond, 0);
    } else {
        /* local: compute PC-relative byte offset */
        int32_t offset = (int32_t)((uint32_t)e->v - (uint32_t)ind);
        if (offset < -(1 << 19) || offset >= (1 << 19))
            tcc_error("branch out of range");
        emit_B(cond, (int)offset);
    }
}

/* --- asm_opcode: parse and emit one CPUTwo instruction --- */
ST_FUNC void asm_opcode(TCCState *s1, int opcode)
{
    int rd, rs1, rs2, imm;
    ExprValue e;

    switch (opcode) {
    /* ----- R-type: 3 registers ----- */
    case TOK_ASM_add:  parse_3reg(&rd, &rs1, &rs2); emit_R(0x00, rd, rs1, rs2, 0); break;
    case TOK_ASM_sub:  parse_3reg(&rd, &rs1, &rs2); emit_R(0x01, rd, rs1, rs2, 0); break;
    case TOK_ASM_and:  parse_3reg(&rd, &rs1, &rs2); emit_R(0x02, rd, rs1, rs2, 0); break;
    case TOK_ASM_or:   parse_3reg(&rd, &rs1, &rs2); emit_R(0x03, rd, rs1, rs2, 0); break;
    case TOK_ASM_xor:  parse_3reg(&rd, &rs1, &rs2); emit_R(0x04, rd, rs1, rs2, 0); break;
    case TOK_ASM_lsl:
    case TOK_ASM_lslr: parse_3reg(&rd, &rs1, &rs2); emit_R(0x06, rd, rs1, rs2, 0); break;
    case TOK_ASM_lsr:
    case TOK_ASM_lsrr: parse_3reg(&rd, &rs1, &rs2); emit_R(0x07, rd, rs1, rs2, 0); break;
    case TOK_ASM_asr:
    case TOK_ASM_asrr: parse_3reg(&rd, &rs1, &rs2); emit_R(0x08, rd, rs1, rs2, 0); break;
    case TOK_ASM_mul:  parse_3reg(&rd, &rs1, &rs2); emit_R(0x09, rd, rs1, rs2, 0); break;
    case TOK_ASM_div:  parse_3reg(&rd, &rs1, &rs2); emit_R(0x0A, rd, rs1, rs2, 0); break;
    case TOK_ASM_mulh: parse_3reg(&rd, &rs1, &rs2); emit_R(0x22, rd, rs1, rs2, 0); break;
    case TOK_ASM_mulhu:parse_3reg(&rd, &rs1, &rs2); emit_R(0x23, rd, rs1, rs2, 0); break;
    case TOK_ASM_divu: parse_3reg(&rd, &rs1, &rs2); emit_R(0x24, rd, rs1, rs2, 0); break;
    case TOK_ASM_mod:  parse_3reg(&rd, &rs1, &rs2); emit_R(0x25, rd, rs1, rs2, 0); break;
    case TOK_ASM_modu: parse_3reg(&rd, &rs1, &rs2); emit_R(0x26, rd, rs1, rs2, 0); break;
    case TOK_ASM_addc: parse_3reg(&rd, &rs1, &rs2); emit_R(0x2B, rd, rs1, rs2, 0); break;
    case TOK_ASM_subc: parse_3reg(&rd, &rs1, &rs2); emit_R(0x2C, rd, rs1, rs2, 0); break;
    /* rotate by register */
    case TOK_ASM_rolr: parse_3reg(&rd, &rs1, &rs2); emit_R(0x39, rd, rs1, rs2, 0); break;
    case TOK_ASM_rorr: parse_3reg(&rd, &rs1, &rs2); emit_R(0x3A, rd, rs1, rs2, 0); break;
    /* rotate by immediate (shift field, 0-31) */
    case TOK_ASM_roli: imm = parse_2reg_int(s1, &rd, &rs1); emit_R(0x3B, rd, rs1, 0, imm & 0x1F); break;
    case TOK_ASM_rori: imm = parse_2reg_int(s1, &rd, &rs1); emit_R(0x3C, rd, rs1, 0, imm & 0x1F); break;
    /* atomic compare-and-swap: cas rd, rs1, rs2 */
    case TOK_ASM_cas:  parse_3reg(&rd, &rs1, &rs2); emit_R(0x3D, rd, rs1, rs2, 0); break;
    /* indexed loads */
    case TOK_ASM_lwx:  parse_3reg(&rd, &rs1, &rs2); emit_R(0x30, rd, rs1, rs2, 0); break;
    case TOK_ASM_lbx:  parse_3reg(&rd, &rs1, &rs2); emit_R(0x31, rd, rs1, rs2, 0); break;
    case TOK_ASM_lbux: parse_3reg(&rd, &rs1, &rs2); emit_R(0x32, rd, rs1, rs2, 0); break;
    case TOK_ASM_lhx:  parse_3reg(&rd, &rs1, &rs2); emit_R(0x35, rd, rs1, rs2, 0); break;
    case TOK_ASM_lhux: parse_3reg(&rd, &rs1, &rs2); emit_R(0x36, rd, rs1, rs2, 0); break;
    /* indexed stores (data=rd, base=rs1, index=rs2) */
    case TOK_ASM_swx:  parse_3reg(&rd, &rs1, &rs2); emit_R(0x33, rd, rs1, rs2, 0); break;
    case TOK_ASM_sbx:  parse_3reg(&rd, &rs1, &rs2); emit_R(0x34, rd, rs1, rs2, 0); break;
    case TOK_ASM_shx:  parse_3reg(&rd, &rs1, &rs2); emit_R(0x37, rd, rs1, rs2, 0); break;

    /* ----- R-type: 2 registers ----- */
    case TOK_ASM_not:   parse_2reg(&rd, &rs1); emit_R(0x05, rd, rs1, 0, 0); break;
    case TOK_ASM_mov:   parse_2reg(&rd, &rs1); emit_R(0x27, rd, rs1, 0, 0); break;
    case TOK_ASM_callr: parse_2reg(&rd, &rs1); emit_R(0x2A, rd, rs1, 0, 0); break;

    /* ----- R-type: compare (no dest) ----- */
    case TOK_ASM_cmp:
        rs1 = parse_reg(); skip_comma(); rs2 = parse_reg();
        emit_R(0x28, 0, rs1, rs2, 0);
        break;

    /* ----- I-type: rd, rs1, imm ----- */
    case TOK_ASM_addi: imm = parse_2reg_int(s1, &rd, &rs1); emit_I(0x14, rd, rs1, imm); break;
    case TOK_ASM_subi: imm = parse_2reg_int(s1, &rd, &rs1); emit_I(0x15, rd, rs1, imm); break;
    case TOK_ASM_andi: imm = parse_2reg_int(s1, &rd, &rs1); emit_I(0x16, rd, rs1, imm); break;
    case TOK_ASM_ori:  imm = parse_2reg_int(s1, &rd, &rs1); emit_I(0x17, rd, rs1, imm); break;
    case TOK_ASM_xori: imm = parse_2reg_int(s1, &rd, &rs1); emit_I(0x18, rd, rs1, imm); break;
    case TOK_ASM_lsli: imm = parse_2reg_int(s1, &rd, &rs1); emit_I(0x19, rd, rs1, imm); break;
    case TOK_ASM_lsri: imm = parse_2reg_int(s1, &rd, &rs1); emit_I(0x1A, rd, rs1, imm); break;
    case TOK_ASM_asri: imm = parse_2reg_int(s1, &rd, &rs1); emit_I(0x1B, rd, rs1, imm); break;

    /* ----- I-type: rd, imm (rs1=0) ----- */
    case TOK_ASM_movi: {
        rd = parse_reg(); skip_comma();
        asm_expr(s1, &e);
        if (e.sym)
            tcc_error("constant expected for movi");
        emit_I(0x0F, rd, 0, (int)e.v);
        break;
    }
    case TOK_ASM_movhi: {
        rd = parse_reg(); skip_comma();
        asm_expr(s1, &e);
        if (e.sym)
            tcc_error("constant expected for movhi");
        emit_I(0x13, rd, 0, (int)e.v);
        break;
    }
    case TOK_ASM_lui: {
        rd = parse_reg(); skip_comma();
        asm_expr(s1, &e);
        if (e.sym)
            tcc_error("constant expected for lui");
        emit_I(0x38, rd, 0, (int)e.v);
        break;
    }

    /* ----- Compare immediate (rd=0) ----- */
    case TOK_ASM_cmpi: {
        rs1 = parse_reg(); skip_comma();
        asm_expr(s1, &e);
        if (e.sym)
            tcc_error("constant expected for cmpi");
        emit_I(0x29, 0, rs1, (int)e.v);
        break;
    }

    /* ----- Loads: rd, imm(rs1) ----- */
    case TOK_ASM_lw:  parse_mem(s1, &rd, &rs1, &imm); emit_I(0x0B, rd, rs1, imm); break;
    case TOK_ASM_lh:  parse_mem(s1, &rd, &rs1, &imm); emit_I(0x1C, rd, rs1, imm); break;
    case TOK_ASM_lhu: parse_mem(s1, &rd, &rs1, &imm); emit_I(0x1D, rd, rs1, imm); break;
    case TOK_ASM_lb:  parse_mem(s1, &rd, &rs1, &imm); emit_I(0x1E, rd, rs1, imm); break;
    case TOK_ASM_lbu: parse_mem(s1, &rd, &rs1, &imm); emit_I(0x1F, rd, rs1, imm); break;

    /* ----- Stores: rs2, imm(rs1)  ("rd" slot carries data reg) ----- */
    case TOK_ASM_sw:  parse_mem(s1, &rs2, &rs1, &imm); emit_I(0x0C, rs2, rs1, imm); break;
    case TOK_ASM_sh:  parse_mem(s1, &rs2, &rs1, &imm); emit_I(0x20, rs2, rs1, imm); break;
    case TOK_ASM_sb:  parse_mem(s1, &rs2, &rs1, &imm); emit_I(0x21, rs2, rs1, imm); break;

    /* ----- Branches ----- */
    case TOK_ASM_beq:  asm_expr(s1, &e); emit_branch(s1, 0,  &e); break;
    case TOK_ASM_bne:  asm_expr(s1, &e); emit_branch(s1, 1,  &e); break;
    case TOK_ASM_blt:  asm_expr(s1, &e); emit_branch(s1, 2,  &e); break;
    case TOK_ASM_bge:  asm_expr(s1, &e); emit_branch(s1, 3,  &e); break;
    case TOK_ASM_bltu: asm_expr(s1, &e); emit_branch(s1, 4,  &e); break;
    case TOK_ASM_bgeu: asm_expr(s1, &e); emit_branch(s1, 5,  &e); break;
    case TOK_ASM_ba:   asm_expr(s1, &e); emit_branch(s1, 6,  &e); break;
    case TOK_ASM_bgt:  asm_expr(s1, &e); emit_branch(s1, 7,  &e); break;
    case TOK_ASM_ble:  asm_expr(s1, &e); emit_branch(s1, 8,  &e); break;
    case TOK_ASM_bgtu: asm_expr(s1, &e); emit_branch(s1, 9,  &e); break;
    case TOK_ASM_bleu: asm_expr(s1, &e); emit_branch(s1, 10, &e); break;

    /* ----- Jump/call: jmp rd, label ----- */
    case TOK_ASM_jmp: {
        int rtype;
        rd = parse_reg(); skip_comma();
        asm_expr(s1, &e);
        if (e.sym) {
            rtype = (rd == 14) ? R_CPUTWO_CALL : R_CPUTWO_PC20;
            greloc(cur_text_section, e.sym, ind, rtype);
            emit_J(rd, 0);
        } else {
            int32_t offset = (int32_t)((uint32_t)e.v - (uint32_t)ind);
            if (offset < -(1 << 19) || offset >= (1 << 19))
                tcc_error("jmp out of range");
            emit_J(rd, (int)offset);
        }
        break;
    }

    /* ----- Pseudo-instructions ----- */
    case TOK_ASM_nop:
        /* addi r0, r0, 0 */
        emit_I(0x14, 0, 0, 0);
        break;

    /* ----- System instructions ----- */
    case TOK_ASM_syscall:
        gen_le32((uint32_t)0x10 << 24);
        break;
    case TOK_ASM_sysret:
        gen_le32((uint32_t)0x11 << 24);
        break;
    case TOK_ASM_halt:
        gen_le32((uint32_t)0x12 << 24);
        break;

    default:
        tcc_error("unknown CPUTwo instruction 0x%x", opcode);
        break;
    }
}

/* --- subst_asm_operand: substitute extended asm operands --- */
ST_FUNC void subst_asm_operand(CString *add_str, SValue *sv, int modifier)
{
    int r, reg;
    char buf[16];

    r = sv->r;
    if ((r & VT_VALMASK) == VT_CONST) {
        if (r & VT_SYM) {
            const char *name = get_tok_str(sv->sym->v, NULL);
            if (sv->sym->v >= SYM_FIRST_ANOM)
                get_asm_sym(tok_alloc(name, strlen(name))->tok, sv->sym);
            if (tcc_state->leading_underscore)
                cstr_ccat(add_str, '_');
            cstr_cat(add_str, name, -1);
            if ((uint32_t)sv->c.i == 0)
                return;
            cstr_ccat(add_str, '+');
        }
        {
            int val = sv->c.i;
            if (modifier == 'n') val = -val;
            snprintf(buf, sizeof(buf), "%d", val);
            cstr_cat(add_str, buf, -1);
        }
    } else if ((r & VT_VALMASK) == VT_LOCAL) {
        snprintf(buf, sizeof(buf), "%d", (int)sv->c.i);
        cstr_cat(add_str, buf, -1);
    } else {
        reg = r & VT_VALMASK;
        if (reg >= VT_CONST)
            tcc_internal_error("bad asm operand");
        /* emit register name, e.g. "r5" */
        snprintf(buf, sizeof(buf), "r%d", reg);
        cstr_cat(add_str, buf, -1);
    }
}

/* --- asm_gen_code: save/restore callee-saved regs around inline asm --- */
/* Callee-saved: r4-r11, lr(r14) */
ST_FUNC void asm_gen_code(ASMOperand *operands, int nb_operands,
                          int nb_outputs, int is_output,
                          uint8_t *clobber_regs, int out_reg)
{
    uint8_t regs_allocated[NB_ASM_REGS];
    ASMOperand *op;
    int i, reg;

    static const uint8_t reg_saved[] = { 4, 5, 6, 7, 8, 9, 10, 11, 14 };

    memcpy(regs_allocated, clobber_regs, sizeof(regs_allocated));
    for (i = 0; i < nb_operands; i++) {
        op = &operands[i];
        if (op->reg >= 0)
            regs_allocated[op->reg] = 1;
    }

    if (!is_output) {
        /* Save clobbered callee-saved regs */
        for (i = 0; i < (int)(sizeof(reg_saved)/sizeof(reg_saved[0])); i++) {
            reg = reg_saved[i];
            if (regs_allocated[reg]) {
                /* ADDI sp, sp, -4 */
                gen_le32(((uint32_t)0x14 << 24) | (13 << 20) | (13 << 16) | (uint16_t)(-4));
                /* SW reg, 0(sp) */
                gen_le32(((uint32_t)0x0C << 24) | ((reg & 0xF) << 20) | (13 << 16) | 0);
            }
        }
        /* Load input operands into their registers */
        for (i = 0; i < nb_operands; i++) {
            op = &operands[i];
            if (op->reg >= 0) {
                if ((op->vt->r & VT_VALMASK) == VT_LLOCAL && op->is_memory) {
                    SValue sv = *op->vt;
                    sv.r = (sv.r & ~VT_VALMASK) | VT_LOCAL | VT_LVAL;
                    sv.type.t = VT_PTR;
                    load(op->reg, &sv);
                } else if (i >= nb_outputs || op->is_rw) {
                    load(op->reg, op->vt);
                }
            }
        }
    } else {
        /* Store output operands */
        for (i = 0; i < nb_outputs; i++) {
            op = &operands[i];
            if (op->reg >= 0) {
                if ((op->vt->r & VT_VALMASK) == VT_LLOCAL) {
                    if (!op->is_memory) {
                        SValue sv = *op->vt;
                        sv.r = (sv.r & ~VT_VALMASK) | VT_LOCAL;
                        sv.type.t = VT_PTR;
                        load(out_reg, &sv);
                        sv = *op->vt;
                        sv.r = (sv.r & ~VT_VALMASK) | out_reg;
                        store(op->reg, &sv);
                    }
                } else {
                    store(op->reg, op->vt);
                }
            }
        }
        /* Restore callee-saved regs in reverse order */
        for (i = (int)(sizeof(reg_saved)/sizeof(reg_saved[0])) - 1; i >= 0; i--) {
            reg = reg_saved[i];
            if (regs_allocated[reg]) {
                /* LW reg, 0(sp) */
                gen_le32(((uint32_t)0x0B << 24) | ((reg & 0xF) << 20) | (13 << 16) | 0);
                /* ADDI sp, sp, 4 */
                gen_le32(((uint32_t)0x14 << 24) | (13 << 20) | (13 << 16) | 4);
            }
        }
    }
}

/* --- asm_compute_constraints --- */

static inline int constraint_priority(const char *str)
{
    int priority = 0, pr;
    for (;;) {
        int c = *str++;
        if (!c) break;
        switch (c) {
        case 'r': case 'p': pr = 3; break;
        case 'i': case 'I': case 'm': case 'g': pr = 4; break;
        default: pr = 0; break;
        }
        if (pr > priority) priority = pr;
    }
    return priority;
}

static const char *skip_constraint_modifiers(const char *p)
{
    while (*p == '=' || *p == '&' || *p == '+' || *p == '%') p++;
    return p;
}

#define REG_OUT_MASK 0x01
#define REG_IN_MASK  0x02
#define is_reg_allocated(reg) (regs_allocated[reg] & reg_mask)

ST_FUNC void asm_compute_constraints(ASMOperand *operands,
                                     int nb_operands, int nb_outputs,
                                     const uint8_t *clobber_regs,
                                     int *pout_reg)
{
    ASMOperand *op;
    int sorted_op[MAX_ASM_OPERANDS];
    int i, j, k, p1, p2, tmp, reg, c, reg_mask;
    const char *str;
    uint8_t regs_allocated[NB_ASM_REGS];

    for (i = 0; i < nb_operands; i++) {
        op = &operands[i];
        op->input_index = -1;
        op->ref_index = -1;
        op->reg = -1;
        op->is_memory = 0;
        op->is_rw = 0;
    }
    for (i = 0; i < nb_operands; i++) {
        op = &operands[i];
        str = op->constraint;
        str = skip_constraint_modifiers(str);
        if (isnum(*str) || *str == '[') {
            k = find_constraint(operands, nb_operands, str, NULL);
            if ((unsigned)k >= i || i < nb_outputs)
                tcc_error("invalid reference in constraint %d ('%s')", i, str);
            op->ref_index = k;
            if (operands[k].input_index >= 0)
                tcc_error("cannot reference twice the same operand");
            operands[k].input_index = i;
            op->priority = 5;
        } else if ((op->vt->r & VT_VALMASK) == VT_LOCAL
                   && op->vt->sym
                   && (reg = op->vt->sym->r & VT_VALMASK) < VT_CONST) {
            op->priority = 1;
            op->reg = reg;
        } else {
            op->priority = constraint_priority(str);
        }
    }

    for (i = 0; i < nb_operands; i++) sorted_op[i] = i;
    for (i = 0; i < nb_operands - 1; i++) {
        for (j = i + 1; j < nb_operands; j++) {
            p1 = operands[sorted_op[i]].priority;
            p2 = operands[sorted_op[j]].priority;
            if (p2 < p1) {
                tmp = sorted_op[i]; sorted_op[i] = sorted_op[j]; sorted_op[j] = tmp;
            }
        }
    }

    for (i = 0; i < NB_ASM_REGS; i++)
        regs_allocated[i] = clobber_regs[i] ? (REG_IN_MASK | REG_OUT_MASK) : 0;

    for (i = 0; i < nb_operands; i++) {
        j = sorted_op[i];
        op = &operands[j];
        str = op->constraint;
        if (op->ref_index >= 0) continue;
        if (op->input_index >= 0)
            reg_mask = REG_IN_MASK | REG_OUT_MASK;
        else if (j < nb_outputs)
            reg_mask = REG_OUT_MASK;
        else
            reg_mask = REG_IN_MASK;

        if (op->reg >= 0) {
            if (is_reg_allocated(op->reg))
                tcc_error("asm regvar requests register that's taken already");
            reg = op->reg;
        }
    try_next:
        c = *str++;
        switch (c) {
        case '=': goto try_next;
        case '+':
            op->is_rw = 1;
            /* fallthrough */
        case '&':
            if (j >= nb_outputs)
                tcc_error("'%c' modifier only for outputs", c);
            reg_mask = REG_IN_MASK | REG_OUT_MASK;
            goto try_next;
        case 'r': case 'p':
            /* allocate from r0-r12 */
            if ((reg = op->reg) >= 0) goto reg_found;
            for (reg = 0; reg <= 12; reg++) {
                if (!is_reg_allocated(reg)) goto reg_found;
            }
            goto try_next;
          reg_found:
            op->is_llong = 0;
            op->reg = reg;
            regs_allocated[reg] |= reg_mask;
            break;
        case 'i': case 'I':
            if (!((op->vt->r & (VT_VALMASK | VT_LVAL)) == VT_CONST)) goto try_next;
            break;
        case 'm': case 'g':
            if (j < nb_outputs || c == 'm') {
                if ((op->vt->r & VT_VALMASK) == VT_LLOCAL) {
                    for (reg = 0; reg <= 12; reg++) {
                        if (!(regs_allocated[reg] & REG_IN_MASK)) goto reg_found1;
                    }
                    goto try_next;
                  reg_found1:
                    regs_allocated[reg] |= REG_IN_MASK;
                    op->reg = reg;
                    op->is_memory = 1;
                }
            }
            break;
        default:
            tcc_error("asm constraint %d ('%s') could not be satisfied",
                      j, op->constraint);
            break;
        }
        if (op->input_index >= 0) {
            operands[op->input_index].reg = op->reg;
            operands[op->input_index].is_llong = op->is_llong;
        }
    }

    *pout_reg = -1;
    for (i = 0; i < nb_operands; i++) {
        op = &operands[i];
        if (op->reg >= 0
            && (op->vt->r & VT_VALMASK) == VT_LLOCAL
            && !op->is_memory) {
            for (reg = 0; reg <= 12; reg++) {
                if (!(regs_allocated[reg] & REG_OUT_MASK)) goto reg_found2;
            }
            tcc_error("no free output register for reload");
          reg_found2:
            *pout_reg = reg;
            break;
        }
    }
}

/* --- asm_clobber: mark clobbered registers --- */
ST_FUNC void asm_clobber(uint8_t *clobber_regs, const char *str)
{
    int reg;
    TokenSym *ts;
    if (!strcmp(str, "memory") || !strcmp(str, "cc") || !strcmp(str, "flags"))
        return;
    ts = tok_alloc(str, strlen(str));
    reg = asm_parse_regvar(ts->tok);
    if (reg < 0)
        tcc_error("invalid clobber register '%s'", str);
    clobber_regs[reg] = 1;
}

/* --- asm_parse_regvar: map token to asm register index (0-15) --- */
ST_FUNC int asm_parse_regvar(int t)
{
    if (t >= TOK_ASM_r0 && t <= TOK_ASM_r15)
        return t - TOK_ASM_r0;
    if (t == TOK_ASM_fp)  return 11;
    if (t == TOK_ASM_sp)  return 13;
    if (t == TOK_ASM_lr)  return 14;
    if (t == TOK_ASM_pc)  return 15;
    return -1;
}

#endif /* !TARGET_DEFS_ONLY */
