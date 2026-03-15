/*
 * CPUTwo soft-float runtime library.
 * All operations use integer bit manipulation only — no C float arithmetic.
 *
 * float      = IEEE 754 binary32: bit31=sign, bits30:23=biased_exp(127), bits22:0=mantissa
 * double     = IEEE 754 binary64: bit63=sign, bits62:52=biased_exp(1023), bits51:0=mantissa
 * long double = double on CPUTwo (LDOUBLE_SIZE=8)
 */

#ifdef __TINYC__
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef int                int32_t;
typedef long long          int64_t;
#else
#include <stdint.h>
#include <string.h>
#endif

/* ================================================================
 * 64-bit integer helpers (needed by TCC on 32-bit targets)
 * These use only 32-bit integer operations internally.
 * ================================================================ */

/* Logical shift right for 64-bit value */
unsigned long long __lshrdi3(unsigned long long a, int b)
{
    union { unsigned long long ll; struct { unsigned int lo, hi; } s; } u;
    u.ll = a;
    if (b >= 32) {
        u.s.lo = u.s.hi >> (b - 32);
        u.s.hi = 0;
    } else if (b != 0) {
        u.s.lo = (u.s.lo >> b) | (u.s.hi << (32 - b));
        u.s.hi = u.s.hi >> b;
    }
    return u.ll;
}

/* Arithmetic shift left for 64-bit value */
long long __ashldi3(long long a, int b)
{
    union { long long ll; struct { unsigned int lo; int hi; } s; } u;
    u.ll = a;
    if (b >= 32) {
        u.s.hi = (int)((unsigned)u.s.lo << (b - 32));
        u.s.lo = 0;
    } else if (b != 0) {
        u.s.hi = (int)(((unsigned)u.s.hi << b) | (u.s.lo >> (32 - b)));
        u.s.lo = u.s.lo << b;
    }
    return u.ll;
}

/* Unsigned 64-bit divide: compute u / v */
unsigned long long __udivdi3(unsigned long long u, unsigned long long v)
{
    /* Simple shift-and-subtract long division */
    unsigned long long q = 0, r = 0;
    int i;
    if (v == 0) return 0; /* undefined, avoid crash */
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((u >> i) & 1ULL);
        if (r >= v) { r -= v; q |= 1ULL << i; }
    }
    return q;
}

/* Unsigned 64-bit modulo: compute u % v */
unsigned long long __umoddi3(unsigned long long u, unsigned long long v)
{
    unsigned long long r = 0;
    int i;
    if (v == 0) return 0;
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((u >> i) & 1ULL);
        if (r >= v) r -= v;
    }
    return r;
}

/* ---- Type-punning helpers via union (safe in C99, avoids strict aliasing UB) ---- */
static uint32_t f2i(float f)    { union { float f; uint32_t i; } u; u.f = f; return u.i; }
static float    i2f(uint32_t x) { union { float f; uint32_t i; } u; u.i = x; return u.f; }
static uint64_t d2i(double d)   { union { double d; uint64_t i; } u; u.d = d; return u.i; }
static double   i2d(uint64_t x) { union { double d; uint64_t i; } u; u.i = x; return u.d; }

/* ================================================================
 * SF (float = binary32) constants
 * ================================================================ */
#define SF_SIGN_MASK   0x80000000u
#define SF_EXP_MASK    0x7F800000u
#define SF_MANT_MASK   0x007FFFFFu
#define SF_EXP_BIAS    127
#define SF_EXP_INF     0xFF
#define SF_MANT_BITS   23
#define SF_HIDDEN_BIT  0x00800000u

/* ================================================================
 * DF (double = binary64) constants
 * ================================================================ */
#define DF_SIGN_MASK   0x8000000000000000ULL
#define DF_EXP_MASK    0x7FF0000000000000ULL
#define DF_MANT_MASK   0x000FFFFFFFFFFFFFULL
#define DF_EXP_BIAS    1023
#define DF_EXP_INF     0x7FF
#define DF_MANT_BITS   52
#define DF_HIDDEN_BIT  0x0010000000000000ULL

/* ================================================================
 * SF add/subtract
 * ================================================================ */
static float sf_add_sub(float fa, float fb, int sub)
{
    uint32_t a = f2i(fa), b = f2i(fb);
    if (sub) b ^= SF_SIGN_MASK;

    uint32_t as = a >> 31, bs = b >> 31;
    int ae = (int)((a >> SF_MANT_BITS) & SF_EXP_INF);
    int be = (int)((b >> SF_MANT_BITS) & SF_EXP_INF);
    uint32_t am = a & SF_MANT_MASK;
    uint32_t bm = b & SF_MANT_MASK;

    /* NaN propagation */
    if (ae == SF_EXP_INF && am) return i2f(a | 0x400000u);
    if (be == SF_EXP_INF && bm) return i2f(b | 0x400000u);
    /* Inf */
    if (ae == SF_EXP_INF) {
        if (be == SF_EXP_INF) {
            if (as != bs) return i2f(0x7FC00000u); /* inf - inf = NaN */
            return fa;
        }
        return fa;
    }
    if (be == SF_EXP_INF) return fb;

    /* Add implicit (hidden) bit; treat denormals with exponent 1 */
    if (ae) am |= SF_HIDDEN_BIT; else if (am) ae = 1;
    if (be) bm |= SF_HIDDEN_BIT; else if (bm) be = 1;

    /* Both zero? */
    if (!am && !bm) return i2f(0);

    /* Align mantissas (keep 3 guard bits) */
    uint32_t ax = am << 3, bx = bm << 3;
    int re = ae;
    if (ae > be) {
        int sh = ae - be;
        bx = (sh < 27) ? (bx >> sh) | !!(bx << (32 - sh) & ~0u) : !!bx;
    } else if (be > ae) {
        int sh = be - ae;
        ax = (sh < 27) ? (ax >> sh) | !!(ax << (32 - sh) & ~0u) : !!ax;
        re = be;
    }

    uint32_t rx;
    int rs;
    if (as == bs) {
        rs = (int)as;
        rx = ax + bx;
    } else if (ax >= bx) {
        rs = (int)as;
        rx = ax - bx;
    } else {
        rs = (int)bs;
        rx = bx - ax;
    }

    if (!rx) return i2f(0); /* result is zero */

    /* Normalize: hidden bit should be at bit 26 */
    if (rx >> 27) { rx >>= 1; re++; }
    else { while (!(rx >> 26) && re > 1) { rx <<= 1; re--; } }

    /* Round to nearest even (3 guard bits) */
    uint32_t g = rx & 7u;
    rx >>= 3;
    if (g > 4u || (g == 4u && (rx & 1u))) {
        if (++rx == (SF_HIDDEN_BIT << 1)) { rx >>= 1; re++; }
    }

    if (re >= SF_EXP_INF) return i2f(((uint32_t)rs << 31) | SF_EXP_MASK); /* overflow -> inf */
    if (re <= 0) { /* denormal */
        int sh = 1 - re;
        rx = (sh < 24) ? rx >> sh : 0u;
        re = 0;
    }
    return i2f(((uint32_t)rs << 31) | ((uint32_t)(re & 0xFF) << SF_MANT_BITS) | (rx & SF_MANT_MASK));
}

float __addsf3(float a, float b) { return sf_add_sub(a, b, 0); }
float __subsf3(float a, float b) { return sf_add_sub(a, b, 1); }
float __negsf2(float a)          { return i2f(f2i(a) ^ SF_SIGN_MASK); }

/* ================================================================
 * SF multiply
 * ================================================================ */
float __mulsf3(float fa, float fb)
{
    uint32_t a = f2i(fa), b = f2i(fb);
    uint32_t rs = (a ^ b) & SF_SIGN_MASK;
    int ae = (int)((a >> SF_MANT_BITS) & 0xFF);
    int be = (int)((b >> SF_MANT_BITS) & 0xFF);
    uint32_t am = a & SF_MANT_MASK;
    uint32_t bm = b & SF_MANT_MASK;

    /* NaN */
    if (ae == 0xFF && am) return i2f(a | 0x400000u);
    if (be == 0xFF && bm) return i2f(b | 0x400000u);
    /* Inf * 0 = NaN */
    if ((ae == 0xFF && !bm && !be) || (be == 0xFF && !am && !ae))
        return i2f(0x7FC00000u);
    if (ae == 0xFF || be == 0xFF) return i2f(rs | 0x7F800000u);
    /* Zero */
    if ((!am && !ae) || (!bm && !be)) return i2f(rs);

    /* Add hidden bits */
    if (ae) am |= SF_HIDDEN_BIT; else ae = 1;
    if (be) bm |= SF_HIDDEN_BIT; else be = 1;

    int re = ae + be - SF_EXP_BIAS - 23;
    uint64_t rm64 = (uint64_t)am * (uint64_t)bm;

    /* Product has hidden bit at bit 46 or 47 */
    if (rm64 >> 47) { re++; }
    else { rm64 <<= 1; }

    /* Round: extract 24-bit result from bits 47..24; bits 23..0 are guard */
    uint32_t guard = (uint32_t)(rm64 >> 23) & 3u;
    uint32_t rm    = (uint32_t)(rm64 >> 24);
    /* "sticky" from remainder below bit 23 */
    if (rm64 & ((1ULL << 23) - 1)) guard |= 1u;
    if (guard > 2u || (guard == 2u && (rm & 1u))) rm++;
    if (rm >> 24) { rm >>= 1; re++; }

    if (re >= 0xFF) return i2f(rs | 0x7F800000u); /* overflow -> inf */
    if (re <= 0) {
        int sh = 1 - re;
        rm = (sh < 24) ? rm >> sh : 0u;
        re = 0;
    }
    return i2f(rs | ((uint32_t)(re & 0xFF) << SF_MANT_BITS) | (rm & SF_MANT_MASK));
}

/* ================================================================
 * SF divide
 * ================================================================ */
float __divsf3(float fa, float fb)
{
    uint32_t a = f2i(fa), b = f2i(fb);
    uint32_t rs = (a ^ b) & SF_SIGN_MASK;
    int ae = (int)((a >> SF_MANT_BITS) & 0xFF);
    int be = (int)((b >> SF_MANT_BITS) & 0xFF);
    uint32_t am = a & SF_MANT_MASK;
    uint32_t bm = b & SF_MANT_MASK;

    if (ae == 0xFF && am) return i2f(a | 0x400000u);
    if (be == 0xFF && bm) return i2f(b | 0x400000u);
    if (ae == 0xFF && be == 0xFF) return i2f(0x7FC00000u); /* inf/inf = NaN */
    if (!am && !ae && !bm && !be) return i2f(0x7FC00000u); /* 0/0 = NaN */
    if (ae == 0xFF || (!bm && !be)) return i2f(rs | 0x7F800000u); /* inf or x/0 */
    if (!am && !ae) return i2f(rs); /* 0/y = 0 */
    if (be == 0xFF) return i2f(rs); /* x/inf = 0 */

    if (ae) am |= SF_HIDDEN_BIT; else ae = 1;
    if (be) bm |= SF_HIDDEN_BIT; else be = 1;

    int re = ae - be + SF_EXP_BIAS + 23;

    /* Long divide: compute 26-bit quotient (2 guard bits) */
    uint64_t an = (uint64_t)am << 26;
    uint64_t bn = (uint64_t)bm;
    uint32_t rm  = (uint32_t)(an / bn);
    uint32_t rem = (uint32_t)(an % bn);

    /* Normalize: hidden bit should be at bit 25 */
    if (rm >> 25) { re++; rm >>= 1; if (rem) rm |= 1u; }

    /* 2 guard bits in rm[1:0] */
    uint32_t g = rm & 3u; rm >>= 2;
    if (rem) g |= 1u; /* sticky */
    if (g > 2u || (g == 2u && (rm & 1u))) rm++;
    if (rm >> 24) { rm >>= 1; re++; }

    if (re >= 0xFF) return i2f(rs | 0x7F800000u);
    if (re <= 0) {
        int sh = 1 - re;
        rm = (sh < 24) ? rm >> sh : 0u;
        re = 0;
    }
    return i2f(rs | ((uint32_t)(re & 0xFF) << SF_MANT_BITS) | (rm & SF_MANT_MASK));
}

/* ================================================================
 * SF comparisons
 * GCC soft-float ABI return conventions:
 *   __eqsf2: return 0 if equal, nonzero otherwise (NaN => nonzero)
 *   __nesf2: return nonzero if not equal (NaN => nonzero)
 *   __ltsf2: return negative if a < b; 0 or positive otherwise (NaN => nonneg)
 *   __lesf2: return 0 or negative if a <= b; positive otherwise (NaN => positive)
 *   __gtsf2: return positive if a > b; 0 or negative otherwise (NaN => nonpos)
 *   __gesf2: return 0 or positive if a >= b; negative otherwise (NaN => negative)
 * ================================================================ */

/* Returns -1 if a<b, 0 if a==b, 1 if a>b, 2 if unordered (NaN) */
static int sf_cmp(uint32_t a, uint32_t b)
{
    if ((a & 0x7FFFFFFFu) > 0x7F800000u) return 2; /* a is NaN */
    if ((b & 0x7FFFFFFFu) > 0x7F800000u) return 2; /* b is NaN */
    if (!((a | b) << 1)) return 0; /* both zero (±0 == ±0) */
    if ((a ^ b) >> 31) return (b >> 31) ? 1 : -1; /* different signs */
    /* Same sign: compare magnitudes */
    uint32_t am = a & 0x7FFFFFFFu, bm = b & 0x7FFFFFFFu;
    int cmp = (am < bm) ? -1 : (am > bm) ? 1 : 0;
    return (a >> 31) ? -cmp : cmp;
}

int __eqsf2(float a, float b) { int c = sf_cmp(f2i(a),f2i(b)); return (c == 2 || c != 0) ? 1 : 0; }
int __nesf2(float a, float b) { int c = sf_cmp(f2i(a),f2i(b)); return (c == 2 || c != 0) ? 1 : 0; }
int __ltsf2(float a, float b) { int c = sf_cmp(f2i(a),f2i(b)); return c < 0 ? -1 : 0; }
int __lesf2(float a, float b) { int c = sf_cmp(f2i(a),f2i(b)); return (c == 2) ? 1 : (c > 0) ? 1 : 0; }
int __gtsf2(float a, float b) { int c = sf_cmp(f2i(a),f2i(b)); return c > 0 && c != 2 ? 1 : 0; }
int __gesf2(float a, float b) { int c = sf_cmp(f2i(a),f2i(b)); return (c == 2) ? -1 : (c < 0) ? -1 : 0; }

/* Unordered variants (return 1 if NaN involved) */
int __unordsf2(float a, float b) { return sf_cmp(f2i(a),f2i(b)) == 2 ? 1 : 0; }

/* ================================================================
 * SF conversions: int/uint <-> float
 * ================================================================ */
float __floatsisf(int32_t a)
{
    uint32_t s = 0;
    uint32_t m;
    int e;
    if (!a) return i2f(0);
    if (a < 0) { s = SF_SIGN_MASK; m = (a == (int32_t)0x80000000) ? 0x80000000u : (uint32_t)(-a); }
    else m = (uint32_t)a;
    e = SF_EXP_BIAS + 31;
    while (!(m >> 31)) { m <<= 1; e--; }
    /* m: bit31=hidden, bits30..0=fraction+guard (8 guard bits in low byte) */
    uint32_t guard8 = m & 0xFFu;
    uint32_t frac   = m >> 8;   /* 24 bits: hidden at bit23 */
    if (guard8 > 0x80u || (guard8 == 0x80u && (frac & 1u))) frac++;
    if (frac >> 24) { frac >>= 1; e++; }
    return i2f(s | ((uint32_t)(e & 0xFF) << SF_MANT_BITS) | (frac & SF_MANT_MASK));
}

float __floatunsisf(uint32_t a)
{
    int e;
    uint32_t m;
    if (!a) return i2f(0);
    m = a;
    e = SF_EXP_BIAS + 31;
    while (!(m >> 31)) { m <<= 1; e--; }
    uint32_t guard8 = m & 0xFFu;
    uint32_t frac   = m >> 8;
    if (guard8 > 0x80u || (guard8 == 0x80u && (frac & 1u))) frac++;
    if (frac >> 24) { frac >>= 1; e++; }
    return i2f(((uint32_t)(e & 0xFF) << SF_MANT_BITS) | (frac & SF_MANT_MASK));
}

float __floatundisf(uint64_t a)
{
    int e;
    uint64_t m;
    if (!a) return i2f(0);
    m = a;
    e = SF_EXP_BIAS + 63;
    while (!(m >> 63)) { m <<= 1; e--; }
    /* m: bit63=hidden, need 23 frac bits + guard bits (40 guard bits) */
    uint64_t guard40 = m & ((1ULL << 40) - 1ULL);
    uint32_t frac    = (uint32_t)(m >> 40); /* 24 bits */
    if (guard40 > (1ULL << 39) || (guard40 == (1ULL << 39) && (frac & 1u))) frac++;
    if (frac >> 24) { frac >>= 1; e++; }
    return i2f(((uint32_t)(e & 0xFF) << SF_MANT_BITS) | (frac & SF_MANT_MASK));
}

float __floatdisf(int64_t a)
{
    uint32_t s = 0;
    uint64_t u;
    if (!a) return i2f(0);
    if (a < 0) {
        s = SF_SIGN_MASK;
        /* Avoid overflow for INT64_MIN */
        u = (a == (int64_t)((uint64_t)1 << 63)) ? (uint64_t)1 << 63 : (uint64_t)(-a);
    } else {
        u = (uint64_t)a;
    }
    return i2f(s | f2i(__floatundisf(u)));
}

/* ================================================================
 * SF fix (float -> int)
 * ================================================================ */
int32_t __fixsfsi(float f)
{
    uint32_t a = f2i(f);
    int s = (a >> 31) ? -1 : 1;
    int e = (int)((a >> SF_MANT_BITS) & 0xFF) - SF_EXP_BIAS;
    if (e < 0) return 0;
    if (e >= 31) return (s < 0) ? (int32_t)0x80000000 : 0x7FFFFFFF;
    uint32_t m = (a & SF_MANT_MASK) | SF_HIDDEN_BIT;
    uint32_t r = (e < SF_MANT_BITS) ? m >> (SF_MANT_BITS - e) : m << (e - SF_MANT_BITS);
    return (s < 0) ? -(int32_t)r : (int32_t)r;
}

uint32_t __fixunssfsi(float f)
{
    uint32_t a = f2i(f);
    if (a >> 31) return 0;
    int e = (int)((a >> SF_MANT_BITS) & 0xFF) - SF_EXP_BIAS;
    if (e < 0) return 0;
    if (e >= 32) return 0xFFFFFFFFu;
    uint32_t m = (a & SF_MANT_MASK) | SF_HIDDEN_BIT;
    return (e < SF_MANT_BITS) ? m >> (SF_MANT_BITS - e) : m << (e - SF_MANT_BITS);
}

int64_t __fixsfdi(float f)
{
    uint32_t a = f2i(f);
    int s = (a >> 31) ? -1 : 1;
    int e = (int)((a >> SF_MANT_BITS) & 0xFF) - SF_EXP_BIAS;
    if (e < 0) return 0;
    if (e >= 63) return (s < 0) ? (int64_t)((uint64_t)1 << 63) : (int64_t)(((uint64_t)1 << 63) - 1);
    uint64_t m = (uint64_t)((a & SF_MANT_MASK) | SF_HIDDEN_BIT);
    uint64_t r = (e < SF_MANT_BITS) ? m >> (SF_MANT_BITS - e) : m << (e - SF_MANT_BITS);
    return (s < 0) ? -(int64_t)r : (int64_t)r;
}

uint64_t __fixunssfdi(float f)
{
    uint32_t a = f2i(f);
    if (a >> 31) return 0;
    int e = (int)((a >> SF_MANT_BITS) & 0xFF) - SF_EXP_BIAS;
    if (e < 0) return 0;
    if (e >= 64) return ~(uint64_t)0;
    uint64_t m = (uint64_t)((a & SF_MANT_MASK) | SF_HIDDEN_BIT);
    return (e < SF_MANT_BITS) ? m >> (SF_MANT_BITS - e) : m << (e - SF_MANT_BITS);
}

/* ================================================================
 * SF <-> DF conversion
 * ================================================================ */
double __extendsfdf2(float f)
{
    uint32_t a = f2i(f);
    uint32_t s = a >> 31;
    int      e = (int)((a >> SF_MANT_BITS) & 0xFF);
    uint32_t m = a & SF_MANT_MASK;

    if (e == 0xFF) {
        /* Inf or NaN */
        uint64_t dm = (uint64_t)m << 29;
        if (m) dm |= DF_HIDDEN_BIT; /* keep NaN quiet */
        return i2d(((uint64_t)s << 63) | DF_EXP_MASK | dm);
    }
    if (!e && !m) return i2d((uint64_t)s << 63); /* zero */
    if (!e) {
        /* denormal: normalize */
        e = 1;
        while (!(m & SF_HIDDEN_BIT)) { m <<= 1; e--; }
        m &= SF_MANT_MASK;
    }
    int de = e - SF_EXP_BIAS + DF_EXP_BIAS;
    return i2d(((uint64_t)s << 63) | ((uint64_t)de << DF_MANT_BITS) | ((uint64_t)m << 29));
}

float __truncdfsf2(double d)
{
    uint64_t a = d2i(d);
    uint32_t s = (uint32_t)(a >> 63);
    int      de = (int)((a >> DF_MANT_BITS) & 0x7FF);
    uint64_t dm = a & DF_MANT_MASK;
    int      e  = de - DF_EXP_BIAS + SF_EXP_BIAS;

    if (de == 0x7FF) {
        /* Inf or NaN */
        return i2f((s << 31) | 0x7F800000u | (dm ? 0x400000u : 0u) | (uint32_t)(dm >> 29));
    }
    if (e >= 0xFF) return i2f((s << 31) | 0x7F800000u); /* overflow -> inf */

    /* Include hidden bit; shift down from 53 bits to 24 bits (29 guard bits) */
    uint64_t mf = (de ? dm | DF_HIDDEN_BIT : dm); /* 53 bits (or denorm) */
    /* Need to reduce to 24 bits with 29-bit guard */
    /* Total shift needed: 53 - 24 = 29 bits */
    uint64_t guard29 = mf & ((1ULL << 29) - 1ULL);
    uint32_t rm = (uint32_t)(mf >> 29); /* 24 bits */

    if (e <= 0) {
        /* Denormal result or flush to zero */
        int sh = 1 - e;
        if (sh >= 24) return i2f(s << 31);
        /* Shift rm right by sh, accumulating guard */
        uint32_t extra = rm & ((1u << sh) - 1u);
        rm >>= sh;
        /* Combine old guard and new guard bits */
        uint64_t newguard = ((uint64_t)extra << 29) | guard29;
        /* Round */
        uint64_t half = (uint64_t)1 << (29 + sh - 1);
        if (newguard > half || (newguard == half && (rm & 1u))) rm++;
        if (rm >> 23) return i2f((s << 31) | (1u << 23)); /* underflow to min normal */
        return i2f((s << 31) | (rm & SF_MANT_MASK));
    }

    /* Round to nearest even */
    uint64_t half29 = 1ULL << 28;
    if (guard29 > half29 || (guard29 == half29 && (rm & 1u))) {
        rm++;
        if (rm >> 24) { rm >>= 1; e++; }
    }
    if (e >= 0xFF) return i2f((s << 31) | 0x7F800000u);

    return i2f((s << 31) | ((uint32_t)(e & 0xFF) << SF_MANT_BITS) | (rm & SF_MANT_MASK));
}

/* ================================================================
 * DF add/subtract
 * ================================================================ */
static double df_add_sub(double fa, double fb, int sub)
{
    uint64_t a = d2i(fa), b = d2i(fb);
    if (sub) b ^= DF_SIGN_MASK;

    int as = (int)(a >> 63), bs = (int)(b >> 63);
    int ae = (int)((a >> DF_MANT_BITS) & DF_EXP_INF);
    int be = (int)((b >> DF_MANT_BITS) & DF_EXP_INF);
    uint64_t am = a & DF_MANT_MASK;
    uint64_t bm = b & DF_MANT_MASK;

    /* NaN */
    if (ae == DF_EXP_INF && am) return i2d(a | 0x0008000000000000ULL);
    if (be == DF_EXP_INF && bm) return i2d(b | 0x0008000000000000ULL);
    /* Inf */
    if (ae == DF_EXP_INF) {
        if (be == DF_EXP_INF) {
            if (as != bs) return i2d(0x7FF8000000000000ULL); /* inf-inf = NaN */
            return fa;
        }
        return fa;
    }
    if (be == DF_EXP_INF) return fb;

    /* Hidden bit */
    if (ae) am |= DF_HIDDEN_BIT; else if (am) ae = 1;
    if (be) bm |= DF_HIDDEN_BIT; else if (bm) be = 1;

    if (!am && !bm) return i2d(0);

    /* Align (3 guard bits in low bits) */
    uint64_t ax = am << 3, bx = bm << 3;
    int re = ae;
    if (ae > be) {
        int sh = ae - be;
        if (sh >= 56) bx = !!bx;
        else bx = (bx >> sh) | !!(bx << (64 - sh) & ~(uint64_t)0);
    } else if (be > ae) {
        int sh = be - ae;
        if (sh >= 56) ax = !!ax;
        else ax = (ax >> sh) | !!(ax << (64 - sh) & ~(uint64_t)0);
        re = be;
    }

    uint64_t rx;
    int rs;
    if (as == bs) {
        rs = as;
        rx = ax + bx;
    } else if (ax >= bx) {
        rs = as;
        rx = ax - bx;
    } else {
        rs = bs;
        rx = bx - ax;
    }

    if (!rx) return i2d(0);

    /* Normalize: hidden bit at bit 55 normally */
    if (rx >> 56) { rx >>= 1; re++; }
    else { while (!(rx >> 55) && re > 1) { rx <<= 1; re--; } }

    /* Round (3 guard bits) */
    uint64_t g = rx & 7ULL;
    rx >>= 3;
    if (g > 4ULL || (g == 4ULL && (rx & 1ULL))) {
        if (++rx == (DF_HIDDEN_BIT << 1)) { rx >>= 1; re++; }
    }

    if (re >= DF_EXP_INF) return i2d(((uint64_t)rs << 63) | DF_EXP_MASK);
    if (re <= 0) {
        int sh = 1 - re;
        rx = (sh < 53) ? rx >> sh : 0ULL;
        re = 0;
    }
    return i2d(((uint64_t)rs << 63) | ((uint64_t)(re & 0x7FF) << DF_MANT_BITS) | (rx & DF_MANT_MASK));
}

double __adddf3(double a, double b) { return df_add_sub(a, b, 0); }
double __subdf3(double a, double b) { return df_add_sub(a, b, 1); }
double __negdf2(double a)           { return i2d(d2i(a) ^ DF_SIGN_MASK); }

/* ================================================================
 * 128-bit multiply helper for DF multiply/divide
 * ================================================================ */
static void mul64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
    uint64_t a_lo = a & 0xFFFFFFFFULL, a_hi = a >> 32;
    uint64_t b_lo = b & 0xFFFFFFFFULL, b_hi = b >> 32;
    uint64_t ll = a_lo * b_lo;
    uint64_t lh = a_lo * b_hi;
    uint64_t hl = a_hi * b_lo;
    uint64_t hh = a_hi * b_hi;
    uint64_t mid = lh + hl + (ll >> 32);
    *hi = hh + (mid >> 32);
    *lo = (mid << 32) | (ll & 0xFFFFFFFFULL);
}

/* ================================================================
 * DF multiply
 * ================================================================ */
double __muldf3(double fa, double fb)
{
    uint64_t a = d2i(fa), b = d2i(fb);
    uint64_t rs = (a ^ b) & DF_SIGN_MASK;
    int ae = (int)((a >> DF_MANT_BITS) & 0x7FF);
    int be = (int)((b >> DF_MANT_BITS) & 0x7FF);
    uint64_t am = a & DF_MANT_MASK;
    uint64_t bm = b & DF_MANT_MASK;

    /* NaN */
    if (ae == 0x7FF && am) return i2d(a | 0x0008000000000000ULL);
    if (be == 0x7FF && bm) return i2d(b | 0x0008000000000000ULL);
    /* Inf * 0 = NaN */
    if ((ae == 0x7FF && !bm && !be) || (be == 0x7FF && !am && !ae))
        return i2d(0x7FF8000000000000ULL);
    if (ae == 0x7FF || be == 0x7FF) return i2d(rs | 0x7FF0000000000000ULL);
    /* Zero */
    if ((!am && !ae) || (!bm && !be)) return i2d(rs);

    if (ae) am |= DF_HIDDEN_BIT; else ae = 1;
    if (be) bm |= DF_HIDDEN_BIT; else be = 1;

    int re = ae + be - DF_EXP_BIAS - 52;

    uint64_t hi, lo;
    mul64(am, bm, &hi, &lo);

    /* Product: 106-bit result in hi:lo, hidden bit at bit 105 or 104 */
    /* hi has bits 127..64, lo has bits 63..0; significant bits in hi[41:0] */
    /* Hidden bit of result is at bit 105 = hi bit 41 */
    if (hi >> 41) { re++; }
    else {
        /* shift left by 1 */
        hi = (hi << 1) | (lo >> 63);
        lo <<= 1;
    }

    /* Extract 53-bit result from hi bits 41..0 with lo as guard */
    /* hi bit 41 = hidden, bits 40..0 = 41 frac bits
       we need 52 frac bits total, so take hi[41..0] (42 bits) + lo[63..52] (12 bits) = 54 bits
       Actually: hi has result bits 105..64 in bits 41..0 */
    /* We want bits [105:53] as the 53-bit mantissa, bits [52:0] as guard */
    /* That's: hi[41:0] = 42 bits (105 down to 64), lo[63:22] = 42 bits (63 down to 22)
       Hmm, let's think differently:
       Total product is 106 bits: hi[41:0]:lo[63:0] = 106 bits
       We need 53-bit mantissa starting at bit 105:
         mantissa = bits[105:53] = 53 bits
         guard = bits[52:0] = 53 bits (in lo[52:0] since hi[41:0]:lo[63:0])
       hi occupies bits 105..64 (42 bits) in hi[41..0]
       lo occupies bits 63..0
       Mantissa bits 105..53: hi[41..0] (42 bits) + lo[63..53] (11 bits) -> 53 bits total
       Guard bits 52..0: lo[52..0] (53 bits)
    */
    uint64_t mant = (hi << 11) | (lo >> 53);
    uint64_t grd  = lo & ((1ULL << 53) - 1ULL);

    /* mant has 53 bits, hidden at bit 52 */
    /* Round */
    uint64_t half53 = 1ULL << 52;
    if (grd > half53 || (grd == half53 && (mant & 1ULL))) {
        mant++;
        if (mant >> 53) { mant >>= 1; re++; }
    }

    if (re >= 0x7FF) return i2d(rs | 0x7FF0000000000000ULL);
    if (re <= 0) {
        int sh = 1 - re;
        mant = (sh < 53) ? mant >> sh : 0ULL;
        re = 0;
    }
    return i2d(rs | ((uint64_t)(re & 0x7FF) << DF_MANT_BITS) | (mant & DF_MANT_MASK));
}

/* ================================================================
 * DF divide
 * ================================================================ */
double __divdf3(double fa, double fb)
{
    uint64_t a = d2i(fa), b = d2i(fb);
    uint64_t rs = (a ^ b) & DF_SIGN_MASK;
    int ae = (int)((a >> DF_MANT_BITS) & 0x7FF);
    int be = (int)((b >> DF_MANT_BITS) & 0x7FF);
    uint64_t am = a & DF_MANT_MASK;
    uint64_t bm = b & DF_MANT_MASK;

    if (ae == 0x7FF && am) return i2d(a | 0x0008000000000000ULL);
    if (be == 0x7FF && bm) return i2d(b | 0x0008000000000000ULL);
    if (ae == 0x7FF && be == 0x7FF) return i2d(0x7FF8000000000000ULL);
    if (!am && !ae && !bm && !be) return i2d(0x7FF8000000000000ULL); /* 0/0 = NaN */
    if (ae == 0x7FF || (!bm && !be)) return i2d(rs | 0x7FF0000000000000ULL);
    if (!am && !ae) return i2d(rs);
    if (be == 0x7FF) return i2d(rs);

    if (ae) am |= DF_HIDDEN_BIT; else ae = 1;
    if (be) bm |= DF_HIDDEN_BIT; else be = 1;

    int re = ae - be + DF_EXP_BIAS + 52;

    /* Divide: compute 56-bit quotient using shift-and-subtract (long division)
     * We need am / bm to 55 bits precision.
     * am and bm are 53-bit values.
     * Shift am left by 55 to get a 108-bit dividend, then divide by 53-bit bm.
     * Use two 64-bit words for the dividend. */

    /* an = am << 55, stored as hi:lo where an is 108 bits */
    /* am is at most 53 bits, so am << 55 fits in 108 bits */
    uint64_t an_hi = am >> 9;      /* bits 107..64 of (am << 55): am >> (64-55) = am >> 9 */
    uint64_t an_lo = am << 55;     /* bits 63..0 of (am << 55) */

    /* Long divide an_hi:an_lo by bm using 64-bit steps */
    /* Compute quotient using 64-bit division in chunks */
    /* Simple approach: shift-and-subtract for 55+1 bits */
    uint64_t quot = 0;
    /* We'll do 55 iterations of the standard long division */
    /* Remainder starts as an_hi:an_lo; we extract 55 bits of quotient */
    uint64_t rem_hi = an_hi, rem_lo = an_lo;
    int i;
    for (i = 0; i < 55; i++) {
        quot <<= 1;
        /* Check if rem >= bm * 2^(54-i): compare rem_hi:rem_lo shifted */
        /* Actually: check if current partial remainder >= bm */
        /* rem_hi:rem_lo represents the remaining bits; bm is 53-bit divisor */
        /* After each bit of quotient we shift rem left by 1 and subtract bm if possible */
        /* Shift rem left 1 (will be done at start of each iteration via the bit extraction) */
        /* Hmm, let's use a cleaner approach: */
        /* rem is maintained as a value; each step we look at the top bit */
        if (rem_hi >= bm) {
            quot |= 1;
            /* rem -= bm (as 128-bit) */
            if (rem_lo < 0) { /* can't happen but structurally: */
                rem_hi--;
            }
            rem_hi -= bm;
        }
        /* Shift rem left by 1 */
        rem_hi = (rem_hi << 1) | (rem_lo >> 63);
        rem_lo <<= 1;
    }
    /* sticky bit */
    if (rem_hi || rem_lo) quot |= 1;

    /* quot is 55-bit quotient */
    uint64_t rm = quot;

    /* Normalize: hidden bit at bit 54 or 53 */
    if (rm >> 54) { re++; rm >>= 1; }

    /* 2 guard bits in rm[1:0] */
    uint64_t g = rm & 3ULL;
    rm >>= 2;
    if (g > 2ULL || (g == 2ULL && (rm & 1ULL))) rm++;
    if (rm >> 53) { rm >>= 1; re++; }

    if (re >= 0x7FF) return i2d(rs | 0x7FF0000000000000ULL);
    if (re <= 0) {
        int sh = 1 - re;
        rm = (sh < 53) ? rm >> sh : 0ULL;
        re = 0;
    }
    return i2d(rs | ((uint64_t)(re & 0x7FF) << DF_MANT_BITS) | (rm & DF_MANT_MASK));
}

/* ================================================================
 * DF comparisons
 * ================================================================ */
static int df_cmp(uint64_t a, uint64_t b)
{
    if ((a & 0x7FFFFFFFFFFFFFFFULL) > 0x7FF0000000000000ULL) return 2;
    if ((b & 0x7FFFFFFFFFFFFFFFULL) > 0x7FF0000000000000ULL) return 2;
    if (!((a | b) << 1)) return 0; /* both zero */
    if ((a ^ b) >> 63) return (b >> 63) ? 1 : -1;
    uint64_t am = a & 0x7FFFFFFFFFFFFFFFULL, bm = b & 0x7FFFFFFFFFFFFFFFULL;
    int cmp = (am < bm) ? -1 : (am > bm) ? 1 : 0;
    return (a >> 63) ? -cmp : cmp;
}

int __eqdf2(double a, double b) { int c = df_cmp(d2i(a),d2i(b)); return (c == 2 || c != 0) ? 1 : 0; }
int __nedf2(double a, double b) { int c = df_cmp(d2i(a),d2i(b)); return (c == 2 || c != 0) ? 1 : 0; }
int __ltdf2(double a, double b) { int c = df_cmp(d2i(a),d2i(b)); return c < 0 ? -1 : 0; }
int __ledf2(double a, double b) { int c = df_cmp(d2i(a),d2i(b)); return (c == 2) ? 1 : (c > 0) ? 1 : 0; }
int __gtdf2(double a, double b) { int c = df_cmp(d2i(a),d2i(b)); return c > 0 && c != 2 ? 1 : 0; }
int __gedf2(double a, double b) { int c = df_cmp(d2i(a),d2i(b)); return (c == 2) ? -1 : (c < 0) ? -1 : 0; }
int __unorddf2(double a, double b) { return df_cmp(d2i(a),d2i(b)) == 2 ? 1 : 0; }

/* ================================================================
 * DF conversions: int/uint <-> double
 * ================================================================ */
double __floatsidf(int32_t a)
{
    uint64_t s = 0;
    uint64_t m;
    int e;
    if (!a) return i2d(0);
    if (a < 0) {
        s = DF_SIGN_MASK;
        m = (a == (int32_t)0x80000000) ? (uint64_t)0x80000000ULL : (uint64_t)(uint32_t)(-a);
    } else {
        m = (uint64_t)(uint32_t)a;
    }
    e = DF_EXP_BIAS + 31;
    while (!(m >> 31)) { m <<= 1; e--; }
    /* m: 32 bits, hidden at bit 31; need 52 frac bits - expand left */
    m <<= 20; /* now bit 51 = hidden, bits 50..0 = fraction (no rounding needed: 32 bits <= 53) */
    return i2d(s | ((uint64_t)(e & 0x7FF) << DF_MANT_BITS) | (m & DF_MANT_MASK));
}

double __floatunsidf(uint32_t a)
{
    uint64_t m;
    int e;
    if (!a) return i2d(0);
    m = (uint64_t)a;
    e = DF_EXP_BIAS + 31;
    while (!(m >> 31)) { m <<= 1; e--; }
    m <<= 20; /* bit 51 = hidden */
    return i2d(((uint64_t)(e & 0x7FF) << DF_MANT_BITS) | (m & DF_MANT_MASK));
}

double __floatundidf(uint64_t a)
{
    uint64_t m;
    int e;
    if (!a) return i2d(0);
    m = a;
    e = DF_EXP_BIAS + 63;
    while (!(m >> 63)) { m <<= 1; e--; }
    /* m: 64 bits, hidden at bit 63; need 52 frac bits: 11 guard bits in low 11 bits */
    uint64_t guard11 = m & 0x7FFull;
    uint64_t frac    = m >> 11; /* 53 bits: hidden at bit 52 */
    uint64_t half    = 0x400ull;
    if (guard11 > half || (guard11 == half && (frac & 1ull))) frac++;
    if (frac >> 53) { frac >>= 1; e++; }
    return i2d(((uint64_t)(e & 0x7FF) << DF_MANT_BITS) | (frac & DF_MANT_MASK));
}

double __floatdidf(int64_t a)
{
    uint64_t s = 0, u;
    if (!a) return i2d(0);
    if (a < 0) {
        s = DF_SIGN_MASK;
        u = (a == (int64_t)((uint64_t)1 << 63)) ? (uint64_t)1 << 63 : (uint64_t)(-a);
    } else {
        u = (uint64_t)a;
    }
    return i2d(s | d2i(__floatundidf(u)));
}

/* ================================================================
 * DF fix (double -> int)
 * ================================================================ */
int32_t __fixdfsi(double f)
{
    uint64_t a = d2i(f);
    int s = (a >> 63) ? -1 : 1;
    int e = (int)((a >> DF_MANT_BITS) & 0x7FF) - DF_EXP_BIAS;
    if (e < 0) return 0;
    if (e >= 31) return (s < 0) ? (int32_t)0x80000000 : 0x7FFFFFFF;
    uint64_t m = (a & DF_MANT_MASK) | DF_HIDDEN_BIT;
    uint32_t r;
    if (e < DF_MANT_BITS) r = (uint32_t)(m >> (DF_MANT_BITS - e));
    else r = (uint32_t)(m << (e - DF_MANT_BITS));
    return (s < 0) ? -(int32_t)r : (int32_t)r;
}

uint32_t __fixunsdfsi(double f)
{
    uint64_t a = d2i(f);
    if (a >> 63) return 0;
    int e = (int)((a >> DF_MANT_BITS) & 0x7FF) - DF_EXP_BIAS;
    if (e < 0) return 0;
    if (e >= 32) return 0xFFFFFFFFu;
    uint64_t m = (a & DF_MANT_MASK) | DF_HIDDEN_BIT;
    if (e < DF_MANT_BITS) return (uint32_t)(m >> (DF_MANT_BITS - e));
    return (uint32_t)(m << (e - DF_MANT_BITS));
}

int64_t __fixdfdi(double f)
{
    uint64_t a = d2i(f);
    int s = (a >> 63) ? -1 : 1;
    int e = (int)((a >> DF_MANT_BITS) & 0x7FF) - DF_EXP_BIAS;
    if (e < 0) return 0;
    if (e >= 63) return (s < 0) ? (int64_t)((uint64_t)1 << 63) : (int64_t)(((uint64_t)1 << 63) - 1);
    uint64_t m = (a & DF_MANT_MASK) | DF_HIDDEN_BIT;
    uint64_t r;
    if (e < DF_MANT_BITS) r = m >> (DF_MANT_BITS - e);
    else r = m << (e - DF_MANT_BITS);
    return (s < 0) ? -(int64_t)r : (int64_t)r;
}

uint64_t __fixunsdfdi(double f)
{
    uint64_t a = d2i(f);
    if (a >> 63) return 0;
    int e = (int)((a >> DF_MANT_BITS) & 0x7FF) - DF_EXP_BIAS;
    if (e < 0) return 0;
    if (e >= 64) return ~(uint64_t)0;
    uint64_t m = (a & DF_MANT_MASK) | DF_HIDDEN_BIT;
    if (e < DF_MANT_BITS) return m >> (DF_MANT_BITS - e);
    return m << (e - DF_MANT_BITS);
}

/* ================================================================
 * TF (long double) wrappers
 * On CPUTwo, long double == double (LDOUBLE_SIZE=8).
 * Casts between long double and double are identity operations.
 * ================================================================ */
long double __addtf3(long double a, long double b) { return __adddf3((double)a, (double)b); }
long double __subtf3(long double a, long double b) { return __subdf3((double)a, (double)b); }
long double __multf3(long double a, long double b) { return __muldf3((double)a, (double)b); }
long double __divtf3(long double a, long double b) { return __divdf3((double)a, (double)b); }

int __eqtf2(long double a, long double b)  { return __eqdf2((double)a, (double)b);  }
int __netf2(long double a, long double b)  { return __nedf2((double)a, (double)b);  }
int __lttf2(long double a, long double b)  { return __ltdf2((double)a, (double)b);  }
int __letf2(long double a, long double b)  { return __ledf2((double)a, (double)b);  }
int __gttf2(long double a, long double b)  { return __gtdf2((double)a, (double)b);  }
int __getf2(long double a, long double b)  { return __gedf2((double)a, (double)b);  }
int __unordtf2(long double a, long double b) { return __unorddf2((double)a, (double)b); }

long double __extendsftf2(float f)    { return (long double)__extendsfdf2(f); }
long double __extenddftf2(double f)   { return (long double)f; }
float       __trunctfsf2(long double f) { return __truncdfsf2((double)f); }
double      __trunctfdf2(long double f) { return (double)f; }

long double __floatsitf(int32_t a)    { return (long double)__floatsidf(a); }
long double __floatunsitf(uint32_t a) { return (long double)__floatunsidf(a); }
long double __floatditf(int64_t a)    { return (long double)__floatdidf(a); }
long double __floatunditf(uint64_t a) { return (long double)__floatundidf(a); }

int32_t  __fixtfsi(long double f)    { return __fixdfsi((double)f); }
uint32_t __fixunstfsi(long double f) { return __fixunsdfsi((double)f); }
int64_t  __fixtfdi(long double f)    { return __fixdfdi((double)f); }
uint64_t __fixunstfdi(long double f) { return __fixunsdfdi((double)f); }
