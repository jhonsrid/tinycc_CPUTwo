/*
 * CPUTwo linker backend for TCC
 *
 * Handles ELF machine type, relocation types, and PLT/GOT stubs.
 * Phase 1: static linking only (R_CPUTWO_32 and R_CPUTWO_CALL).
 * CPUTwo is little-endian: instruction words stored LSB-first.
 */

#ifdef TARGET_DEFS_ONLY

/* ------------------------------------------------------------------ */
/* Linker / ELF target definitions                                     */
/* ------------------------------------------------------------------ */

#define EM_TCC_TARGET   EM_CPUTWO

/* Map TCC's generic reloc names to CPUTwo-specific ones */
#define R_DATA_32       R_CPUTWO_32
#define R_DATA_PTR      R_CPUTWO_32
#define R_JMP_SLOT      R_CPUTWO_JMP_SLOT
#define R_GLOB_DAT      R_CPUTWO_GLOB_DAT
#define R_COPY          R_CPUTWO_COPY
#define R_RELATIVE      R_CPUTWO_RELATIVE
#define R_NUM           R_CPUTWO_NUM

/* ELF load address and page size */
#define ELF_START_ADDR  0x00001000   /* first page reserved for reset vector etc. */
#define ELF_PAGE_SIZE   0x1000

/* No PC-relative DLL PLT for Phase 1 (static linking only) */
#define PCRELATIVE_DLLPLT 0
#define RELOCATE_DLLPLT   0

/* ------------------------------------------------------------------ */
#else /* !TARGET_DEFS_ONLY */
/* ------------------------------------------------------------------ */

#include "tcc.h"

/* Returns 1 for code relocations, 0 for data, -1 for unknown */
ST_FUNC int code_reloc(int reloc_type)
{
    switch (reloc_type) {
    case R_CPUTWO_CALL:
    case R_CPUTWO_PC20:
    case R_CPUTWO_JMP_SLOT:
        return 1;
    case R_CPUTWO_32:
    case R_CPUTWO_HI16:
    case R_CPUTWO_LO16:
    case R_CPUTWO_RELATIVE:
    case R_CPUTWO_GLOB_DAT:
    case R_CPUTWO_COPY:
        return 0;
    }
    return -1;
}

/* Returns GOT/PLT entry requirement */
ST_FUNC int gotplt_entry_type(int reloc_type)
{
    switch (reloc_type) {
    case R_CPUTWO_NONE:
        return NO_GOTPLT_ENTRY;
    case R_CPUTWO_32:
    case R_CPUTWO_HI16:
    case R_CPUTWO_LO16:
    case R_CPUTWO_PC20:
    case R_CPUTWO_CALL:
        return AUTO_GOTPLT_ENTRY;
    case R_CPUTWO_COPY:
    case R_CPUTWO_GLOB_DAT:
    case R_CPUTWO_JMP_SLOT:
    case R_CPUTWO_RELATIVE:
        return NO_GOTPLT_ENTRY;
    }
    return -1;
}

/* Stub PLT entry creator (Phase 1: static linking only) */
ST_FUNC unsigned create_plt_entry(TCCState *s1, unsigned got_offset,
                                   struct sym_attr *attr)
{
    Section *plt = s1->plt;
    uint8_t  *p;
    unsigned  plt_offset;

    if (plt->data_offset == 0)
        section_ptr_add(plt, 16);   /* reserve PLT[0] */
    plt_offset = plt->data_offset;
    p = section_ptr_add(plt, 16);
    /* Store got_offset for later patching by relocate_plt */
    write32le(p, got_offset);
    return plt_offset;
}

/* Stub PLT relocation (Phase 1: static only, so this is rarely reached) */
ST_FUNC void relocate_plt(TCCState *s1)
{
    if (!s1->plt)
        return;
    /* Phase 1: no dynamic linking, nothing to do */
}

/* ------------------------------------------------------------------ */
/* Little-endian helpers (duplicated locally; gen.c defines them too)  */
/* ------------------------------------------------------------------ */

static uint32_t lnk_read_le32(const uint8_t *p)
{
    return  (uint32_t)p[0]        | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) |  ((uint32_t)p[3] << 24);
}

static void lnk_write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (v >>  0) & 0xFF;
    p[1] = (v >>  8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}

/* ------------------------------------------------------------------ */
/* relocate — apply one relocation                                      */
/* ------------------------------------------------------------------ */

ST_FUNC void relocate(TCCState *s1, ElfW_Rel *rel, int type,
                       unsigned char *ptr, addr_t addr, addr_t val)
{
    int32_t offset;
    uint32_t insn;

    switch (type) {

    case R_CPUTWO_NONE:
        return;

    case R_CPUTWO_32:
        /* Absolute 32-bit reference in data section */
        lnk_write_le32(ptr, lnk_read_le32(ptr) + val);
        if (s1->output_type & TCC_OUTPUT_DYN) {
            /* REL format: addend is stored in-place (already written above) */
            qrel->r_offset = rel->r_offset;
            qrel->r_info   = ELFW(R_INFO)(0, R_CPUTWO_RELATIVE);
            qrel++;
        }
        return;

    case R_CPUTWO_RELATIVE:
        lnk_write_le32(ptr, lnk_read_le32(ptr) + val);
        return;

    case R_CPUTWO_CALL:
    case R_CPUTWO_PC20: {
        /* 20-bit PC-relative branch/call offset (signed bytes) */
        offset = (int32_t)(val - addr);
        if (offset < -(1 << 19) || offset >= (1 << 19)) {
            tcc_error_noabort(
                "R_CPUTWO_CALL/PC20 relocation out of range "
                "(val=0x%lx addr=0x%lx off=%d)",
                (long)val, (long)addr, offset);
            return;
        }
        insn = lnk_read_le32(ptr);
        /* Patch bits 19:0 with offset, preserve bits 31:20 (opcode + rd/cond) */
        lnk_write_le32(ptr, (insn & 0xFFF00000u) | ((uint32_t)offset & 0xFFFFFu));
        return;
    }

    case R_CPUTWO_HI16: {
        /* Patch the imm16 field (bits 15:0) of a LUI instruction with (val >> 16).
         * No +0x8000 rounding: CPUTwo uses ORI (zero-extend) for LO16, not ADDI. */
        uint16_t hi = (uint16_t)(val >> 16);
        insn = lnk_read_le32(ptr);
        lnk_write_le32(ptr, (insn & 0xFFFF0000u) | hi);
        return;
    }

    case R_CPUTWO_LO16: {
        /* Patch imm16 field of ORI/ADDI with (val & 0xFFFF) */
        uint16_t lo = (uint16_t)(val & 0xFFFFu);
        insn = lnk_read_le32(ptr);
        lnk_write_le32(ptr, (insn & 0xFFFF0000u) | lo);
        return;
    }

    case R_CPUTWO_COPY:
        return;

    case R_CPUTWO_GLOB_DAT:
    case R_CPUTWO_JMP_SLOT:
        lnk_write_le32(ptr, (uint32_t)val);
        return;

    default:
        fprintf(stderr, "cpuTwo-link: unhandled reloc type %d at 0x%lx\n",
                type, (long)addr);
        return;
    }
}

#endif /* TARGET_DEFS_ONLY */
