#include "n6502.h"
#include "common.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

// FIXME: should be _LOG
#define LOG(...)  _INFO(6502, __VA_ARGS__)
#define INFO(...) _INFO(6502, __VA_ARGS__)

// FIXME:
// - could merge IMM16() with PC += 2
//   (or should add a PC increment to the GEN_OP macro)

// FEATURES:
// - Add wave dumping (VCD) of CPU signals (ala GTKWave?)
// - Detect stack underflow/overflow (beqr seems to bork the stack)
// - Track opcode metrics so they can be perf optimized
// - True instruction decompiler
// - simple "gdb-like" debugger

// OPT:
// - (much later on) if waiting on an interrupt, don't spin the CPU (trigger code instead)
//   => loop detection based on polling $2002

#define NMI_VECTOR   0xfffa
#define RESET_VECTOR 0xfffc
#define IRQ_VECTOR   0xfffe

#define INTERRUPT_CYCLES 7
#define MAX_OP_SIZE      3

typedef enum
{
    AM_BAD = 0,

    AM_IMPLIED,
    AM_ACCUM,

    AM_IMMED,
    AM_RELATIVE,
    AM_ZP,
    AM_ZPX,      /* Zero page indexed with X */
    AM_ZPY,      /* Zero page indexed with Y */

    AM_JMPABS,

    AM_ABS,
    AM_ABSX,     /* Absolute indexed with X */
    AM_ABSY,     /* Absolute indexed with Y */

    AM_INDA,     /* Indirect Absolute */
    AM_INDX,     /* Indexed indirect (with x) */
    AM_INDY,     /* Indirect indexed (with y) */

    AM_LAST,
} AddressingMode_t;

const uint8_t OP_SIZE[AM_LAST] =
{
    [AM_IMPLIED]   = 1,
    [AM_ACCUM]     = 1,

    [AM_IMMED]     = 2,
    [AM_RELATIVE]  = 2,

    [AM_ZP]        = 2,
    [AM_ZPX]       = 2,
    [AM_ZPY]       = 2,

    [AM_INDA]      = 3,
    [AM_INDX]      = 2,
    [AM_INDY]      = 2,

    [AM_JMPABS]    = 3,

    [AM_ABS]       = 3,
    [AM_ABSX]      = 3,
    [AM_ABSY]      = 3,
};

typedef struct
{
    void (*op)(N6502_t *cpu);
    AddressingMode_t mode;
    const char *name;
    const char *desc;
    unsigned cycles;
    unsigned undocumented;
} opcode_t;

static void
n6502_die(N6502_t *cpu)
{
    n6502_dump_state(cpu);
    abort();
}

#define die(...) { printf("\nABORT @ cycle %" PRIu64 "\n", cpu->inst_count); printf(__VA_ARGS__); n6502_die(cpu); }

// --------------------------------------------------------------------------------
#ifdef DEBUG
#define GEN_OP(OP, UNDOCUMENTED, MODE, CYCLES, A, NAME, DESC) [OP] = {NAME, MODE, #NAME, DESC, CYCLES, UNDOCUMENTED}
#else
#define GEN_OP(OP, UNDOCUMENTED, MODE, CYCLES, A, NAME, DESC) [OP] = {NAME, MODE, #NAME, "", CYCLES, UNDOCUMENTED}
#endif

#define DEF_OP(X) static void X(N6502_t *cpu)

// Zero-page access
#define ZP(i) ((i) & 0xff)

DEF_OP(TAY)   { SET_Y(cpu->regs.A); }
DEF_OP(TAX)   { SET_X(cpu->regs.A); }
DEF_OP(TSX)   { SET_X(cpu->regs.S); }
DEF_OP(TYA)   { SET_A(cpu->regs.Y); }
DEF_OP(TXA)   { SET_A(cpu->regs.X); }
DEF_OP(TXS)   { cpu->regs.S = cpu->regs.X; }

// Bank cross detection for cycle penalty
#define CHECK_BANK_CROSSING(CPU, A, B)   \
    if(((A) >> 8) != ((B) >> 8))         \
        CPU->cycle++;

#define DUMMY_MEM_READ(CPU, A, B)   \
    if(((A) >> 8) != ((B) >> 8))    \
        READ_MEM(((A) & 0xff00) | ((B) & 0xff));

static inline uint16_t
src_ax(N6502_t *cpu, uint16_t imm)
{
    uint16_t src = imm + cpu->regs.X;

    return src;
}

static inline uint16_t
src_axb(N6502_t *cpu, uint16_t imm)
{
    uint16_t src = imm + cpu->regs.X;

    CHECK_BANK_CROSSING(cpu, src, imm);
    // FIXME: when the APU is implemented
    //DUMMY_MEM_READ(cpu, src, imm);

    return src;
}

static inline uint16_t
src_ay(N6502_t *cpu, uint16_t imm)
{
    uint16_t src = imm + cpu->regs.Y;

    return src;
}

static inline uint16_t
src_ayb(N6502_t *cpu, uint16_t imm)
{
    uint16_t src = imm + cpu->regs.Y;

    CHECK_BANK_CROSSING(cpu, src, imm);

    return src;
}

static inline uint16_t
src_iy(N6502_t *cpu)
{
    uint8_t z = IMM8();
    uint16_t w = WORD16(z);
    uint16_t src = w + cpu->regs.Y;

    return src;
}

static inline uint16_t
src_iyb(N6502_t *cpu)
{
    uint8_t z = IMM8();
    uint16_t w = WORD16(z);
    uint16_t src = w + cpu->regs.Y;

    CHECK_BANK_CROSSING(cpu, src, w);

    return src;
}

#define SRC_ZX()  ZP(IMM8() + cpu->regs.X)
#define SRC_ZY()  ZP(IMM8() + cpu->regs.Y)
#define SRC_AX()  src_ax (cpu, IMM16())
#define SRC_AXB() src_axb(cpu, IMM16())
#define SRC_AY()  src_ay (cpu, IMM16())
#define SRC_AYB() src_ayb(cpu, IMM16())
#define SRC_IY()  src_iy (cpu)
#define SRC_IYB() src_iyb(cpu)

#define MEM_I()   IMM8()
#define MEM_Z()   READ_MEM(IMM8())
#define MEM_ZX()  READ_MEM(SRC_ZX())
#define MEM_ZY()  READ_MEM(SRC_ZY())
#define MEM_A()   READ_MEM(IMM16())
#define MEM_AX()  READ_MEM(SRC_AX())
#define MEM_AXB() READ_MEM(SRC_AXB())
#define MEM_AY()  READ_MEM(SRC_AY())
#define MEM_AYB() READ_MEM(SRC_AYB())

DEF_OP(LDAi)  { SET_A(MEM_I()); }
DEF_OP(LDAz)  { SET_A(MEM_Z()); }
DEF_OP(LDAzx) { SET_A(MEM_ZX()); }
DEF_OP(LDAa)  { SET_A(MEM_A());  cpu->regs.PC += 2; }
DEF_OP(LDAax) { SET_A(MEM_AXB()); cpu->regs.PC += 2; }
DEF_OP(LDAay) { SET_A(MEM_AYB()); cpu->regs.PC += 2; }

// Indirect ops => FUN!
DEF_OP(LDAix) { uint8_t z = SRC_ZX(); SET_A(READ_MEM(WORD16(z))); }
DEF_OP(LDAiy) { SET_A(READ_MEM(SRC_IYB())); }

DEF_OP(LDXi)  { SET_X(MEM_I()); }
DEF_OP(LDXz)  { SET_X(MEM_Z()); }
DEF_OP(LDXzy) { SET_X(MEM_ZY()); }
DEF_OP(LDXa)  { SET_X(MEM_A());  cpu->regs.PC += 2; }
DEF_OP(LDXay) { SET_X(MEM_AYB()); cpu->regs.PC += 2; }

DEF_OP(LDYi)  { SET_Y(MEM_I()); }
DEF_OP(LDYz)  { SET_Y(MEM_Z()); }
DEF_OP(LDYzx) { SET_Y(MEM_ZX()); }
DEF_OP(LDYa)  { SET_Y(MEM_A());  cpu->regs.PC += 2; }
DEF_OP(LDYax) { SET_Y(MEM_AXB()); cpu->regs.PC += 2; }

#define STA(a) WRITE_MEM(a, cpu->regs.A)

DEF_OP(STAz)  { STA(IMM8()); }
DEF_OP(STAzx) { STA(SRC_ZX()); }
DEF_OP(STAa)  { STA(IMM16());  cpu->regs.PC += 2; }
DEF_OP(STAax) { STA(SRC_AX()); cpu->regs.PC += 2; }
DEF_OP(STAay) { STA(SRC_AY()); cpu->regs.PC += 2; }
// Indirect ops => FUN!
DEF_OP(STAix) { uint8_t z = SRC_ZX(); STA(WORD16(z)); }
DEF_OP(STAiy) { STA(SRC_IY()); }

#define STX(a) WRITE_MEM(a, cpu->regs.X)

DEF_OP(STXz)  { STX(IMM8()); }
DEF_OP(STXzy) { STX(SRC_ZY()); }
DEF_OP(STXa)  { STX(IMM16()); cpu->regs.PC += 2; }

#define STY(a) WRITE_MEM(a, cpu->regs.Y)

DEF_OP(STYz)  { STY(IMM8()); }
DEF_OP(STYzx) { STY(SRC_ZX()); }
DEF_OP(STYa)  { STY(IMM16()); cpu->regs.PC += 2; }

// Notes: PLA sets Z and N according to content of A. The B-flag and unused flags
// cannot be changed by PLP, these flags are always written as "1" by PHP.
DEF_OP(PHP)   { FLAG(B) = 1; uint8_t p = cpu->regs.P.word; FLAG(B) = 0; PUSH_STACK(p); }
DEF_OP(PHA)   { PUSH_STACK(cpu->regs.A); }
DEF_OP(PLA)   { SET_A(POP_STACK()); }
DEF_OP(PLP)   { cpu->regs.P.word = POP_STACK(); FLAG(_5) = 1; FLAG(B) = 0; }

#define DEF_ALU(NAME)                                                     \
    DEF_OP(NAME##i)  { NAME(MEM_I()); }                                   \
    DEF_OP(NAME##z)  { NAME(MEM_Z()); }                                   \
    DEF_OP(NAME##zx) { NAME(MEM_ZX()); }                                  \
    DEF_OP(NAME##a)  { NAME(MEM_A());  cpu->regs.PC += 2; }                    \
    DEF_OP(NAME##ax) { NAME(MEM_AXB()); cpu->regs.PC += 2; }                   \
    DEF_OP(NAME##ay) { NAME(MEM_AYB()); cpu->regs.PC += 2; }                   \
    DEF_OP(NAME##ix) { uint8_t i = SRC_ZX(); NAME(READ_MEM(WORD16(i))); } \
    DEF_OP(NAME##iy) { NAME(READ_MEM(SRC_IYB())); }

// Undocumented ops

static inline void _ADC(N6502_t *cpu, uint8_t src)
{
    uint16_t temp = (cpu->regs.A + (FLAG(C) ? 1 : 0) + src);
    SET_Z(temp & 0xff);
    if(FLAG(D) && cpu->enable_decimal)
    {
        LOG("ADCd");
        if(((cpu->regs.A & 0xf) + (src & 0xf) + (FLAG(C) ? 1 : 0)) > 9)
        {
            LOG(" += 6");
            temp += 6;
        }

        SET_N(temp & 0xff);
        FLAG(V) = !((cpu->regs.A ^ src) & 0x80) && ((cpu->regs.A ^ temp) & 0x80);
        if(temp > 0x99)
        {
            LOG(" += 60");
            temp += 0x60;
        }
        FLAG(C) = (temp > 0x99);
        LOG(": %x %x => %x\n", cpu->regs.A, src, temp);
    }
    else
    {
        SET_N(temp & 0xff);
        FLAG(C) = (temp > 0xff);
        FLAG(V) = !((cpu->regs.A ^ src) & 0x80) && ((cpu->regs.A ^ temp) & 0x80);
    }
    //cpu->regs.A = (uint8_t) temp;
    cpu->regs.A = temp & 0xff;
}

#define ADC(i) _ADC(cpu, i)

DEF_ALU(ADC)

static inline void _SBC(N6502_t *cpu, uint8_t src)
{
    uint16_t temp = (cpu->regs.A - src - (FLAG(C) ? 0 : 1));
    SET_N(temp);
    SET_Z(temp & 0xff);
    FLAG(V) = ((cpu->regs.A ^ src) & 0x80) && ((cpu->regs.A ^ temp) & 0x80);
    if(FLAG(D) && cpu->enable_decimal)
    {
        LOG("SBCd");
        if(((cpu->regs.A & 0xf) - (FLAG(C) ? 0 : 1)) < (src & 0xf))
        {
            LOG(" -= 6");
            temp -= 6;
        }

        if(temp > 0x99)
        {
            LOG(" -= 0x60");
            temp -= 0x60;
        }
        LOG(": %x %x => %x\n", cpu->regs.A, src, temp);
    }
    FLAG(C) = (temp < 0x100);
    cpu->regs.A = (uint8_t) temp;
}

#define SBC(i) _SBC(cpu, i)

DEF_ALU(SBC)

#define AND(i) SET_A(cpu->regs.A & (i));
DEF_ALU(AND)

#define ORA(i) SET_A(cpu->regs.A | (i));
DEF_ALU(ORA)

#define EOR(i) SET_A(cpu->regs.A ^ (i));
DEF_ALU(EOR)

// Note: Compared with normal 80x86 and Z80 CPUs, resulting Carry Flag is reversed.
#define CMP(R, IMM)               \
    uint8_t v = (IMM);            \
    FLAG(C) = (cpu->regs.R >= v);      \
    FLAG(Z) = (cpu->regs.R == v);      \
    FLAG(N) = ((cpu->regs.R - v) >> 7)

#define CMPA(i) CMP(A, i)

DEF_OP(CMPi)  { CMPA(MEM_I()); }
DEF_OP(CMPz)  { CMPA(MEM_Z()); }
DEF_OP(CMPzx) { CMPA(MEM_ZX()); }
DEF_OP(CMPa)  { CMPA(MEM_A());  cpu->regs.PC += 2; }
DEF_OP(CMPax) { CMPA(MEM_AXB()); cpu->regs.PC += 2; }
DEF_OP(CMPay) { CMPA(MEM_AYB()); cpu->regs.PC += 2; }
// Indirect ops => FUN!
DEF_OP(CMPix) { uint8_t i = SRC_ZX(); CMPA(READ_MEM(WORD16(i))); }
DEF_OP(CMPiy) { CMPA(READ_MEM(SRC_IYB())); }

#define CPX(i) CMP(X, i)

DEF_OP(CPXi)  { CPX(MEM_I()); }
DEF_OP(CPXz)  { CPX(MEM_Z()); }
DEF_OP(CPXa)  { CPX(MEM_A()); cpu->regs.PC += 2; }

#define CPY(i) CMP(Y, i)

DEF_OP(CPYi)  { CPY(MEM_I()); }
DEF_OP(CPYz)  { CPY(MEM_Z()); }
DEF_OP(CPYa)  { CPY(MEM_A()); cpu->regs.PC += 2; }

DEF_OP(BIT8)  {     \
    uint8_t i = READ_MEM(IMM8());   \
    FLAG(Z) = ((i & cpu->regs.A) == 0);  \
    FLAG(V) = (i >> 6)&1;           \
    FLAG(N) = (i >> 7)&1;           \
}
DEF_OP(BIT16)  {     \
    uint8_t i = READ_MEM(IMM16());  \
    FLAG(Z) = ((i & cpu->regs.A) == 0);  \
    FLAG(V) = (i >> 6)&1;           \
    FLAG(N) = (i >> 7)&1;           \
    cpu->regs.PC += 2; \
}

// FIXME: do one read instead of two
#define MEM_INCDEC(OP, ADDR)   WRITE_MEM(ADDR, READ_MEM(ADDR) OP); SET_ZN(READ_MEM(ADDR))

#define DEF_INCDEC(NAME, OP)                                                            \
    DEF_OP(NAME##Cz)  { uint8_t addr = IMM8();    MEM_INCDEC(OP, addr); }               \
    DEF_OP(NAME##Czx) { uint8_t addr = SRC_ZX();  MEM_INCDEC(OP, addr); }               \
    DEF_OP(NAME##Ca)  { uint16_t addr = IMM16();  MEM_INCDEC(OP, addr); cpu->regs.PC += 2; } \
    DEF_OP(NAME##Cax) { uint16_t addr = SRC_AX(); MEM_INCDEC(OP, addr); cpu->regs.PC += 2; } \
    DEF_OP(NAME##X)   { SET_X(cpu->regs.X OP); }                             \
    DEF_OP(NAME##Y)   { SET_Y(cpu->regs.Y OP); }

// INX/INY/DEX/DEY
DEF_INCDEC(IN, + 1);
DEF_INCDEC(DE, - 1);

//    DEF_OP(NAME##1)  { NAME(cpu->regs.A); }
#define DEF_SHIFT(NAME) \
    DEF_OP(NAME##z)  { uint8_t z = IMM8();    NAME(z); }               \
    DEF_OP(NAME##zx) { uint8_t z = SRC_ZX();  NAME(z); }               \
    DEF_OP(NAME##a)  { uint16_t m = IMM16();  NAME(m); cpu->regs.PC += 2; } \
    DEF_OP(NAME##ax) { uint16_t m = SRC_AX(); NAME(m); cpu->regs.PC += 2; }

#define ASL(a) uint8_t i = READ_MEM(a); FLAG(C) = ((i) >> 7); (i) <<= 1; WRITE_MEM(a, i); SET_ZN(i)
DEF_SHIFT(ASL);
DEF_OP(ASL1) { FLAG(C) = ((cpu->regs.A) >> 7); SET_A(cpu->regs.A << 1); }

#define LSR(a) uint8_t i = READ_MEM(a); FLAG(C) = ((i) & 1); (i) >>= 1; WRITE_MEM(a, i); SET_ZN(i)
DEF_SHIFT(LSR);
DEF_OP(LSR1) { FLAG(C) = (cpu->regs.A) & 1; SET_A(cpu->regs.A >> 1); }

#define ROL(a) uint8_t i = READ_MEM(a); uint8_t C = ((i) >> 7); (i) = ((i) << 1) | FLAG(C); FLAG(C) = C; WRITE_MEM(a, i); SET_ZN(i)
DEF_SHIFT(ROL);
DEF_OP(ROL1) { uint8_t C = (cpu->regs.A >> 7); SET_A((cpu->regs.A << 1) | FLAG(C)); FLAG(C) = C; }

#define ROR(a) uint8_t i = READ_MEM(a); uint8_t C = ((i) & 1); (i) = (FLAG(C) << 7) | ((i) >> (1)); FLAG(C) = C; WRITE_MEM(a, i); SET_ZN(i)
DEF_SHIFT(ROR);
DEF_OP(ROR1) { uint8_t C = cpu->regs.A & 1; SET_A((FLAG(C) << 7) | cpu->regs.A >> 1); FLAG(C) = C; }

#ifdef DEBUG
static void
BAD_PC(N6502_t *cpu)
{
    ASSERT(0, "BAD PC: $%04X", cpu->regs.PC);
}

static inline void
CHECK_PC(N6502_t *cpu)
{
    if(cpu->min_pc && (cpu->regs.PC < cpu->min_pc))
    {
        BAD_PC(cpu);
    }
}

#else
#define CHECK_PC(X)
#endif

DEF_OP(JMPa) { cpu->regs.PC = IMM16(); CHECK_PC(cpu); }

DEF_OP(JMPi) { cpu->regs.PC = WORD16(IMM16()); CHECK_PC(cpu); }
DEF_OP(JSR)  { uint16_t new_pc = IMM16(); cpu->regs.PC++; PUSH_PC(); cpu->regs.PC = new_pc; CHECK_PC(cpu); }

DEF_OP(RTI)  { cpu->regs.P.word = POP_STACK(); FLAG(_5) = 1; FLAG(B) = 0; POP_PC(); }
DEF_OP(RTS)  { POP_PC(); cpu->regs.PC++; CHECK_PC(cpu); }

static inline void
_branch(N6502_t *cpu, uint8_t cond)
{
    int8_t offset = IMM8();
    if(cond)
    {
        uint16_t not_taken_pc = cpu->regs.PC;
        cpu->regs.PC += offset;
        //LOG("branch taken => %04Xh\n", cpu->regs.PC);

        cpu->cycle++;

        CHECK_BANK_CROSSING(cpu, cpu->regs.PC, not_taken_pc);
    }
}

#define DEF_BRANCH(NAME, COND) \
    DEF_OP(NAME) { _branch(cpu, COND); }

DEF_BRANCH(BPL, FLAG(N) == 0)
DEF_BRANCH(BMI, FLAG(N) == 1)
DEF_BRANCH(BVC, FLAG(V) == 0)
DEF_BRANCH(BVS, FLAG(V) == 1)

DEF_BRANCH(BCC, FLAG(C) == 0)
DEF_BRANCH(BCS, FLAG(C) == 1)
DEF_BRANCH(BNE, FLAG(Z) == 0)
DEF_BRANCH(BEQ, FLAG(Z) == 1)

DEF_OP(BRK) { cpu->regs.PC++; PUSH_PC(); FLAG(B) = 1; PUSH_STACK(cpu->regs.P.word); FLAG(B) = 0; FLAG(I) = 1; cpu->regs.PC = WORD16(IRQ_VECTOR); }

DEF_OP(CLC) { FLAG(C) = 0; }
DEF_OP(CLI) { FLAG(I) = 0; }
DEF_OP(CLD) { FLAG(D) = 0; }
DEF_OP(CLV) { FLAG(V) = 0; }

DEF_OP(SEC) { FLAG(C) = 1; }
DEF_OP(SEI) { FLAG(I) = 1; }
DEF_OP(SED) { FLAG(D) = 1; }

DEF_OP(NOP) { (void)cpu; }

// --------------------------------------------------------------------------------
// UNDOCUMENTED 6502 OPS
// --------------------------------------------------------------------------------
DEF_OP(NOPb)   { /*LOG("SKB\n");*/ cpu->regs.PC += 1; }
DEF_OP(NOPwa)  { /*LOG("SKWa\n");*/ cpu->regs.PC += 2; }
DEF_OP(NOPwax) { /*LOG("SKWax\n");*/ MEM_AXB(); cpu->regs.PC += 2; }

#define DEF_DOUBLE(NAME) \
    DEF_OP(NAME##z)  { uint8_t z = IMM8();    NAME(z); }               \
    DEF_OP(NAME##zx) { uint8_t z = SRC_ZX();  NAME(z); }               \
    DEF_OP(NAME##a)  { uint16_t m = IMM16();  NAME(m); cpu->regs.PC += 2; } \
    DEF_OP(NAME##ax) { uint16_t m = SRC_AX(); NAME(m); cpu->regs.PC += 2; } \
    DEF_OP(NAME##ay) { uint16_t m = SRC_AY(); NAME(m); cpu->regs.PC += 2; } \
    DEF_OP(NAME##ix) { uint8_t z = SRC_ZX(); uint16_t ix = WORD16(z); NAME(ix); } \
    DEF_OP(NAME##iy) { uint16_t iy = SRC_IY(); NAME(iy); }

// ASL/OR
#define SLO(a) uint8_t src = READ_MEM(a); FLAG(C) = (src) >> 7; src <<= 1; WRITE_MEM(a, src); cpu->regs.A |= (src); SET_ZN(cpu->regs.A)

// ROL/AND
#define RLA(a) uint8_t src = READ_MEM(a); uint8_t C = ((src) >> 7); src = (src << 1) | FLAG(C); WRITE_MEM(a, src); FLAG(C) = C; cpu->regs.A &= src; SET_ZN(cpu->regs.A)

// LSR/EOR
#define SRE(a) uint8_t src = READ_MEM(a); FLAG(C) = ((src) & 1); src >>= 1; WRITE_MEM(a, src); cpu->regs.A ^= src; SET_ZN(cpu->regs.A)

// ROR/ADC
#define RRA(a) ROR(a); _ADC(cpu, READ_MEM(a))

// DEC/CMP
#define DCP(a) uint8_t src = READ_MEM(a) - 1; WRITE_MEM(a, src); CMP(A, src)

// INC/SBC
#define ISB(a) uint8_t src = READ_MEM(a) + 1; WRITE_MEM(a, src); _SBC(cpu, src)

DEF_DOUBLE(SLO)
DEF_DOUBLE(RLA)
DEF_DOUBLE(SRE)
DEF_DOUBLE(RRA)
DEF_DOUBLE(DCP)
DEF_DOUBLE(ISB)

// SAX
// SAX ANDs the contents of the A and X registers (leaving the contents of A
// intact), subtracts an immediate value, and then stores the result in X.
// ... A few points might be made about the action of subtracting an immediate
// value.  It actually works just like the CMP instruction, except that CMP
// does not store the result of the subtraction it performs in any register.
// This subtract operation is not affected by the state of the Carry flag,
// though it does affect the Carry flag.  It does not affect the Overflow
// flag.
DEF_OP(SAX)
{
    uint16_t result = (cpu->regs.X & cpu->regs.A) - IMM8();
    SET_N(result);
    cpu->regs.X = result & 0xff;
    FLAG(C) = (result < 0x100);
    FLAG(Z) = (cpu->regs.X == 0);
}

// SAY:  [abcd] = Y AND (ab + 1)
DEF_OP(SAYax)
{
    uint16_t addr = IMM16() + cpu->regs.X; // FIXME: SRC_AX()?
    uint8_t result = cpu->regs.Y & ((addr >> 8) + 1);
    //SET_ZN(result);
    WRITE_MEM(addr, result);
    cpu->regs.PC += 2;
}

// XAS:  [abcd] = X AND (ab + 1)
DEF_OP(XASay)
{
    uint16_t addr = IMM16() + cpu->regs.Y; // FIXME: SRC_AY()?
    uint8_t result = cpu->regs.X & ((addr >> 8) + 1);
    //SET_ZN(result);
    WRITE_MEM(addr, result);
    cpu->regs.PC += 2;
}

// AXA:  [abcd] = A AND X AND (ab + 1)
DEF_OP(AXAay)
{
    uint16_t addr = IMM16() + cpu->regs.Y; // FIXME: SRC_AY()?
    uint8_t result = cpu->regs.X & cpu->regs.A & ((addr >> 8) + 1);
    WRITE_MEM(addr, result);
    cpu->regs.PC += 2;
}

DEF_OP(AXAiy)
{
    uint16_t addr = SRC_IY();
    uint8_t result = cpu->regs.X & cpu->regs.A & ((addr >> 8) + 1);
    WRITE_MEM(addr, result);
}

DEF_OP(TASay)
{
    uint16_t addr = SRC_AY();
    cpu->regs.S = cpu->regs.A & cpu->regs.X;
    //WRITE_MEM(addr, cpu->regs.S & ((addr >> 8) + 1));
    WRITE_MEM(addr, cpu->regs.S & ((addr >> 8) + 1));
    cpu->regs.PC += 2;
}

// LXA: also called OAL or ATX
// NOTE: two behaviors for NES/C64 cpu's
DEF_OP(LXA)
{
    uint8_t i = IMM8();
    if(cpu->enable_decimal) i &= (0xee | cpu->regs.A);
    SET_A(i);
    cpu->regs.X = cpu->regs.A;
}

DEF_OP(LAXz)  { SET_A(MEM_Z());  cpu->regs.X = cpu->regs.A; }
DEF_OP(LAXzy) { SET_A(MEM_ZY()); cpu->regs.X = cpu->regs.A; }
DEF_OP(LAXa)  { SET_A(MEM_A());  cpu->regs.X = cpu->regs.A; cpu->regs.PC += 2; }
DEF_OP(LAXay) { SET_A(MEM_AYB()); cpu->regs.X = cpu->regs.A; cpu->regs.PC += 2; }

// Indirect ops => FUN!
DEF_OP(LAXix) { uint8_t z = SRC_ZX(); SET_A(READ_MEM(WORD16(z))); cpu->regs.X = cpu->regs.A; }
DEF_OP(LAXiy) { SET_A(READ_MEM(SRC_IYB())); cpu->regs.X = cpu->regs.A; }

// LAR: also called LAE or LAS
// AND memory with stack pointer, transfer result to accumulator, X register and stack pointer.
DEF_OP(LARay)
{
    SET_A(MEM_AYB() & cpu->regs.S);
    cpu->regs.X = cpu->regs.A;
    cpu->regs.S = cpu->regs.A;
    cpu->regs.PC += 2;
}

// ANC: AND with carry
DEF_OP(ANCi)  { SET_A(cpu->regs.A & IMM8()); FLAG(C) = FLAG(N); }

// SAX (also known as AXS):
// ANDs the contents of the A and X registers (without changing the
// contents of either register) and stores the result in memory.
// SAX does not affect any flags in the processor status register.

#define SAX() (cpu->regs.A & cpu->regs.X)

DEF_OP(SAXz)  { uint8_t z = IMM8();    WRITE_MEM(z, SAX()); }
DEF_OP(SAXzy) { uint8_t z = SRC_ZY();  WRITE_MEM(z, SAX()); }
DEF_OP(SAXa)  { uint16_t m = IMM16();  WRITE_MEM(m, SAX()); cpu->regs.PC += 2; }
DEF_OP(SAXix) { uint8_t i = SRC_ZX();  WRITE_MEM(WORD16(i), SAX()); }

DEF_OP(ALR)   { uint8_t i = IMM8();  cpu->regs.A &= i; FLAG(C) = cpu->regs.A & 1; cpu->regs.A >>= 1; SET_ZN(cpu->regs.A); }

// ARR => AND/ROR
// The opcode ARR operates more complexily than actually described in the list
// above.  Here is a brief rundown on this.  The following assumes the decimal
// flag is clear.  You see, the sub-instruction for ARR ($6B) is in fact ADC
// ($69), not AND.  While ADC is not performed, some of the ADC mechanics are
// evident.  Like ADC, ARR affects the overflow flag.  The following effects
// occur after ANDing but before RORing.  The V flag is set to the result of
// exclusive ORing bit 7 with bit 6.  Unlike ROR, bit 0 does not go into the
// carry flag.  The state of bit 7 is exchanged with the carry flag.  Bit 0 is
// lost.  All of this may appear strange, but it makes sense if you consider
// the probable internal operations of ADC itself.

// NOTE: this implementation does not work for decimal mode
DEF_OP(ARR)                                    \
{                                              \
    uint8_t imm = IMM8();                      \
    cpu->regs.A &= imm;                             \
    FLAG(V) = (cpu->regs.A >> 7) ^ (cpu->regs.A >> 6);   \
    cpu->regs.A = (FLAG(C) << 7) | (cpu->regs.A >> 1);   \
    FLAG(C) = (cpu->regs.A >> 6) & 1;               \
    SET_ZN(cpu->regs.A);                            \
}

// XAA transfers the contents of the X register to the A register and then
// ANDs the A register with an immediate value.
DEF_OP(XAA)   { uint8_t i = IMM8() & cpu->regs.X; i &= (0xee | cpu->regs.A); SET_A(i); }

DEF_OP(DEBUG_TRAP)
{
    cpu->regs.PC--;
    LOG("TRAP!\n");
    if(cpu->debug_trap)
    {
        cpu->debug_trap(cpu);
    }
    else
    {
        die("No debug trap installed @ PC %04X\n", cpu->regs.PC);
    }
}

static const opcode_t OPCODES[256] =
{
    // DEBUG
    GEN_OP(OP_DEBUG_TRAP, 1, AM_IMPLIED, 0, 0, DEBUG_TRAP, "Debug TRAP"),

    // CPU Control
    GEN_OP(0x18, 0, AM_IMPLIED, 2, 0, CLC, "Clear carry flag            C=0"),
    GEN_OP(0x58, 0, AM_IMPLIED, 2, 0, CLI, "Clear interrupt disable bit I=0"),
    GEN_OP(0xD8, 0, AM_IMPLIED, 2, 0, CLD, "Clear decimal mode          D=0"),
    GEN_OP(0xB8, 0, AM_IMPLIED, 2, 0, CLV, "Clear overflow flag         V=0"),
    GEN_OP(0x38, 0, AM_IMPLIED, 2, 0, SEC, "Set carry flag              C=1"),
    GEN_OP(0x78, 0, AM_IMPLIED, 2, 0, SEI, "Set interrupt disable bit   I=1"),
    GEN_OP(0xF8, 0, AM_IMPLIED, 2, 0, SED, "Set decimal mode            D=1"),

    // No Operation
#define GEN_NOP(xx, UNDOCUMENTED) GEN_OP(xx, UNDOCUMENTED, AM_IMPLIED, 2, 0, NOP, "NOP No operation")
    GEN_NOP(0xEA, 0),

    // CPU Jump and Control Instructions
    GEN_OP(0x00, 0, AM_IMPLIED, 7, 0, BRK, "Force Break B=1 [S]=PC+1,[S]=P,I=1,PC=[FFFE]"),

    // Conditional Branches
    GEN_OP(0x10, 0, AM_RELATIVE, 2, 1, BPL, "Branch on result plus     if N=0 PC=PC+/-nn"),
    GEN_OP(0x30, 0, AM_RELATIVE, 2, 1, BMI, "Branch on result minus    if N=1 PC=PC+/-nn"),
    GEN_OP(0x50, 0, AM_RELATIVE, 2, 1, BVC, "Branch on overflow clear  if V=0 PC=PC+/-nn"),
    GEN_OP(0x70, 0, AM_RELATIVE, 2, 1, BVS, "Branch on overflow set    if V=1 PC=PC+/-nn"),
    GEN_OP(0x90, 0, AM_RELATIVE, 2, 1, BCC, "Branch on carry clear     if C=0 PC=PC+/-nn"),
    GEN_OP(0xB0, 0, AM_RELATIVE, 2, 1, BCS, "Branch on carry set       if C=1 PC=PC+/-nn"),
    GEN_OP(0xD0, 0, AM_RELATIVE, 2, 1, BNE, "Branch on result not zero if Z=0 PC=PC+/-nn"),
    GEN_OP(0xF0, 0, AM_RELATIVE, 2, 1, BEQ, "Branch on result zero     if Z=1 PC=PC+/-nn"),

    // ** The execution time is 2 cycles if the condition is false (no branch
    // executed). Otherwise, 3 cycles if the destination is in the same memory page,
    // or 4 cycles if it crosses a page boundary (see below for exact info).
    // Note: After subtractions (SBC or CMP) carry=set indicates above-or-equal,
    // unlike as for 80x86 and Z80 CPUs. Obviously, this still applies even when using
    // 80XX-style syntax.

    // Normal Jumps
    GEN_OP(0x4C, 0, AM_JMPABS,   3, 0, JMPa, "Jump Absolute              PC=nnnn"),
    GEN_OP(0x6C, 0, AM_INDA,     5, 0, JMPi, "Jump Indirect              PC=WORD[nnnn]"),
    GEN_OP(0x20, 0, AM_JMPABS,   6, 0, JSR,  "Jump and Save Return Addr. [S]=PC+2,PC=nnnn"),
    GEN_OP(0x40, 0, AM_IMPLIED,  6, 0, RTI,  "Return from BRK/IRQ/NMI    P=[S], PC=[S]"),
    GEN_OP(0x60, 0, AM_IMPLIED,  6, 0, RTS,  "Return from Subroutine     PC=[S]+1"),

    // CPU Rotate and Shift Instructions

#define GEN_SHIFT(BASE, NAME, PREFIX, DESC)                           \
    GEN_OP(BASE + 0x0A, 0, AM_ACCUM, 2, 0, NAME##1,  PREFIX " Accumulator   " DESC " A"), \
    GEN_OP(BASE + 0x06, 0, AM_ZP,    5, 0, NAME##z,  PREFIX " Zero Page     " DESC " [nn]"), \
    GEN_OP(BASE + 0x16, 0, AM_ZPX,   6, 0, NAME##zx, PREFIX " Zero Page,X   " DESC " [nn+X]"), \
    GEN_OP(BASE + 0x0E, 0, AM_ABS,   6, 0, NAME##a,  PREFIX " Absolute      " DESC " [nnnn]"), \
    GEN_OP(BASE + 0x1E, 0, AM_ABSX,  7, 0, NAME##ax, PREFIX " Absolute,X    " DESC " [nnnn+X]")

    // Shift Left
    GEN_SHIFT(0x00, ASL, "Shift Left", "ASL"),

    // Shift Right
    GEN_SHIFT(0x40, LSR, "Shift Right", "LSR"),

    // Rotate Left through Carry
    GEN_SHIFT(0x20, ROL, "Rotate Left", "ROL"),

    // Rotate Right through Carry
    GEN_SHIFT(0x60, ROR, "Rotate Left", "ROR"),

    // Bit Test
    GEN_OP(0x24, 0, AM_ZP,      3, 0, BIT8,  "Bit Test   A AND [nn], N=[nn].7, V=[nn].6"),
    GEN_OP(0x2C, 0, AM_ABS,     4, 0, BIT16, "Bit Test   A AND [..], N=[..].7, V=[..].6"),

    // Increment by one
    GEN_OP(0xE6, 0, AM_ZP,      5, 0, INCz,  "Increment Zero Page    [nn]=[nn]+1"),
    GEN_OP(0xF6, 0, AM_ZPX,     6, 0, INCzx, "Increment Zero Page,X  [nn+X]=[nn+X]+1"),
    GEN_OP(0xEE, 0, AM_ABS,     6, 0, INCa,  "Increment Absolute     [nnnn]=[nnnn]+1"),
    GEN_OP(0xFE, 0, AM_ABSX,    7, 0, INCax, "Increment Absolute,X   [nnnn+X]=[nnnn+X]+1"),

    GEN_OP(0xE8, 0, AM_IMPLIED, 2, 0, INX,   "Increment X            X=X+1"),
    GEN_OP(0xC8, 0, AM_IMPLIED, 2, 0, INY,   "Increment Y            Y=Y+1"),

    // Decrement by one
    // FIXME: merge with increment above
    GEN_OP(0xC6, 0, AM_ZP,      5, 0, DECz,  "Decrement Zero Page    [nn]=[nn]-1"),
    GEN_OP(0xD6, 0, AM_ZPX,     6, 0, DECzx, "Decrement Zero Page,X  [nn+X]=[nn+X]-1"),
    GEN_OP(0xCE, 0, AM_ABS,     6, 0, DECa,  "Decrement Absolute     [nnnn]=[nnnn]-1"),
    GEN_OP(0xDE, 0, AM_ABSX,    7, 0, DECax, "Decrement Absolute,X   [nnnn+X]=[nnnn+X]-1"),
    GEN_OP(0xCA, 0, AM_IMPLIED, 2, 0, DEX,   "Decrement X            X=X-1"),
    GEN_OP(0x88, 0, AM_IMPLIED, 2, 0, DEY,   "Decrement Y            Y=Y-1"),

    // Compare (FIXME: merge with ALU?)
    GEN_OP(0xC9, 0, AM_IMMED, 2, 0,   CMPi,  "Compare A with Immediate     A-nn"),
    GEN_OP(0xC5, 0, AM_ZP,    3, 0,   CMPz,  "Compare A with Zero Page     A-[nn]"),
    GEN_OP(0xD5, 0, AM_ZPX,   4, 0,   CMPzx, "Compare A with Zero Page,X   A-[nn+X]"),
    GEN_OP(0xCD, 0, AM_ABS,   4, 0,   CMPa,  "Compare A with Absolute      A-[nnnn]"),
    GEN_OP(0xDD, 0, AM_ABSX,  4, 1,   CMPax, "Compare A with Absolute,X    A-[nnnn+X]"),
    GEN_OP(0xD9, 0, AM_ABSY,  4, 1,   CMPay, "Compare A with Absolute,Y    A-[nnnn+Y]"),
    GEN_OP(0xC1, 0, AM_INDX,  6, 0,   CMPix, "Compare A with (Indirect,X)  A-[[nn+X]]"),
    GEN_OP(0xD1, 0, AM_INDY,  5, 1,   CMPiy, "Compare A with (Indirect),Y  A-[[nn]+Y]"),

    GEN_OP(0xE0, 0, AM_IMMED, 2, 0,   CPXi,  "Compare X with Immediate     X-nn"),
    GEN_OP(0xE4, 0, AM_ZP,    3, 0,   CPXz,  "Compare X with Zero Page     X-[nn]"),
    GEN_OP(0xEC, 0, AM_ABS,   4, 0,   CPXa,  "Compare X with Absolute      X-[nnnn]"),
    GEN_OP(0xC0, 0, AM_IMMED, 2, 0,   CPYi,  "Compare Y with Immediate     Y-nn"),
    GEN_OP(0xC4, 0, AM_ZP,    3, 0,   CPYz,  "Compare Y with Zero Page     Y-[nn]"),
    GEN_OP(0xCC, 0, AM_ABS,   4, 0,   CPYa,  "Compare Y with Absolute      Y-[nnnn]"),

    // CPU Arithmetic/Logical Operations

    // Logical AND memory with accumulator
#define GEN_ALU(BASE, NAME, PREFIX, DESC)                          \
    GEN_OP(BASE + 0x09, 0, AM_IMMED, 2, 0, NAME##i,   PREFIX " Immediate      A=" DESC "nn"),      \
    GEN_OP(BASE + 0x05, 0, AM_ZP,    3, 0, NAME##z,   PREFIX " Zero Page      A=" DESC "[nn]"),    \
    GEN_OP(BASE + 0x15, 0, AM_ZPX,   4, 0, NAME##zx,  PREFIX " Zero Page,X    A=" DESC "[nn+X]"),  \
    GEN_OP(BASE + 0x0D, 0, AM_ABS,   4, 0, NAME##a,   PREFIX " Absolute       A=" DESC "[nnnn]"),  \
    GEN_OP(BASE + 0x1D, 0, AM_ABSX,  4, 1, NAME##ax,  PREFIX " Absolute,X     A=" DESC "[nnnn+X]"),\
    GEN_OP(BASE + 0x19, 0, AM_ABSY,  4, 1, NAME##ay,  PREFIX " Absolute,Y     A=" DESC "[nnnn+Y]"),\
    GEN_OP(BASE + 0x01, 0, AM_INDX,  6, 0, NAME##ix,  PREFIX " (Indirect,X)   A=" DESC "[[nn+X]]"),\
    GEN_OP(BASE + 0x11, 0, AM_INDY,  5, 1, NAME##iy,  PREFIX " (Indirect),Y   A=" DESC "[[nn]+Y]")

    // Logical OR memory with accumulator
    GEN_ALU(0x00, ORA, "OR", "A OR "),

    // Exclusive-OR memory with accumulator
    GEN_ALU(0x40, EOR, "XOR", "A XOR "),

    // Logical AND memory with accumulator
    GEN_ALU(0x20, AND, "AND", "A AND "),

    // Add memory to accumulator with carry
    GEN_ALU(0x60, ADC, "Add", "A+C+"),

    // Subtract memory from accumulator with borrow
    GEN_ALU(0xE0, SBC, "Subtract", "A+C-1-"),

    // Push/Pull
    GEN_OP(0x48, 0, AM_IMPLIED, 3, 0, PHA,   "Push accumulator on stack        [S]=A"),
    GEN_OP(0x08, 0, AM_IMPLIED, 3, 0, PHP,   "Push processor status on stack   [S]=P"),
    GEN_OP(0x68, 0, AM_IMPLIED, 4, 0, PLA,   "Pull accumulator from stack      A=[S]"),
    GEN_OP(0x28, 0, AM_IMPLIED, 4, 0, PLP,   "Pull processor status from stack P=[S]"),

    // Store Register in Memory
    GEN_OP(0x85, 0, AM_ZP,    3, 0, STAz,  "Store A in Zero Page     [nn]=A"),
    GEN_OP(0x95, 0, AM_ZPX,   4, 0, STAzx, "Store A in Zero Page,X   [nn+X]=A"),
    GEN_OP(0x8D, 0, AM_ABS,   4, 0, STAa,  "Store A in Absolute      [nnnn]=A"),
    GEN_OP(0x9D, 0, AM_ABSX,  5, 0, STAax, "Store A in Absolute,X    [nnnn+X]=A"),
    GEN_OP(0x99, 0, AM_ABSY,  5, 0, STAay, "Store A in Absolute,Y    [nnnn+Y]=A"),
    GEN_OP(0x81, 0, AM_INDX,  6, 0, STAix, "Store A in (Indirect,X)  [[nn+x]]=A"),
    GEN_OP(0x91, 0, AM_INDY,  6, 0, STAiy, "Store A in (Indirect),Y  [[nn]+y]=A"),

    GEN_OP(0x86, 0, AM_ZP,    3, 0, STXz,  "Store X in Zero Page     [nn]=X"),
    GEN_OP(0x96, 0, AM_ZPY,   4, 0, STXzy, "Store X in Zero Page,Y   [nn+Y]=X"),
    GEN_OP(0x8E, 0, AM_ABS,   4, 0, STXa,  "Store X in Absolute      [nnnn]=X"),

    GEN_OP(0x84, 0, AM_ZP,    3, 0, STYz,  "Store Y in Zero Page     [nn]=Y"),
    GEN_OP(0x94, 0, AM_ZPX,   4, 0, STYzx, "Store Y in Zero Page,X   [nn+X]=Y"),
    GEN_OP(0x8C, 0, AM_ABS,   4, 0, STYa,  "Store Y in Absolute      [nnnn]=Y"),

    // Load Register from Memory
    // LDA
    GEN_OP(0xA9, 0, AM_IMMED, 2, 0, LDAi,  "Load A with Immediate     A=nn"),
    GEN_OP(0xA5, 0, AM_ZP,    3, 0, LDAz,  "Load A with Zero Page     A=[nn]"),
    GEN_OP(0xB5, 0, AM_ZPX,   4, 0, LDAzx, "Load A with Zero Page,X   A=[nn+X]"),
    GEN_OP(0xAD, 0, AM_ABS,   4, 0, LDAa,  "Load A with Absolute      A=[nnnn]"),
    GEN_OP(0xBD, 0, AM_ABSX,  4, 1, LDAax, "Load A with Absolute,X    A=[nnnn+X]"),
    GEN_OP(0xB9, 0, AM_ABSY,  4, 1, LDAay, "Load A with Absolute,Y    A=[nnnn+Y]"),
    GEN_OP(0xA1, 0, AM_INDX,  6, 0, LDAix, "Load A with (Indirect,X)  A=[WORD[nn+X]]"),
    GEN_OP(0xB1, 0, AM_INDY,  5, 1, LDAiy, "Load A with (Indirect),Y  A=[WORD[nn]+Y]"),

    // LDX
    GEN_OP(0xA2, 0, AM_IMMED, 2, 0, LDXi,  "Load X with Immediate     X=nn"),
    GEN_OP(0xA6, 0, AM_ZP,    3, 0, LDXz,  "Load X with Zero Page     X=[nn]"),
    GEN_OP(0xB6, 0, AM_ZPY,   4, 0, LDXzy, "Load X with Zero Page,Y   X=[nn+Y]"),
    GEN_OP(0xAE, 0, AM_ABS,   4, 0, LDXa,  "Load X with Absolute      X=[nnnn]"),
    GEN_OP(0xBE, 0, AM_ABSY,  4, 1, LDXay, "Load X with Absolute,Y    X=[nnnn+Y]"),

    // LDY
    GEN_OP(0xA0, 0, AM_IMMED, 2, 0, LDYi,  "Load Y with Immediate     Y=nn"),
    GEN_OP(0xA4, 0, AM_ZP,    3, 0, LDYz,  "Load Y with Zero Page     Y=[nn]"),
    GEN_OP(0xB4, 0, AM_ZPX,   4, 0, LDYzx, "Load Y with Zero Page,X   Y=[nn+X]"),
    GEN_OP(0xAC, 0, AM_ABS,   4, 0, LDYa,  "Load Y with Absolute      Y=[nnnn]"),
    GEN_OP(0xBC, 0, AM_ABSX,  4, 1, LDYax, "Load Y with Absolute,X    Y=[nnnn+X]"),

    // Register to Register Transfer
    GEN_OP(0xA8, 0, AM_IMPLIED, 2, 0, TAY,   "Transfer Accumulator to Y    Y=A"),
    GEN_OP(0xAA, 0, AM_IMPLIED, 2, 0, TAX,   "Transfer Accumulator to X    X=A"),
    GEN_OP(0xBA, 0, AM_IMPLIED, 2, 0, TSX,   "Transfer Stack pointer to X  X=S"),
    GEN_OP(0x98, 0, AM_IMPLIED, 2, 0, TYA,   "Transfer Y to Accumulator    A=Y"),
    GEN_OP(0x8A, 0, AM_IMPLIED, 2, 0, TXA,   "Transfer X to Accumulator    A=X"),
    GEN_OP(0x9A, 0, AM_IMPLIED, 2, 0, TXS,   "Transfer X to Stack pointer  S=X"),

    // Undocumented ops (see http://nesdev.parodius.com/extra_instructions.txt)
    // --------------------------------------------------------------------------------
    GEN_NOP(0x1A, 1),
    GEN_NOP(0x3A, 1),
    GEN_NOP(0x5A, 1),
    GEN_NOP(0x7A, 1),
    GEN_NOP(0xDA, 1),
    GEN_NOP(0xFA, 1),

    GEN_OP(0xEB, 1, AM_IMMED, 2, 0, SBCi,  "Subtract Immediate (ALT)      A=A+C-1-nn"),

    // ANC
    GEN_OP(0x2B, 1, AM_IMMED, 2, 0, ANCi, "A = A AND nn"),
    GEN_OP(0x0B, 1, AM_IMMED, 2, 0, ANCi, "A = A AND nn"),

#define GEN_SKB(CODE, MODE, NUM_CYCLES) GEN_OP(CODE, 1, MODE, NUM_CYCLES, 0, NOPb, "SKB Skip next byte (NOPx2)")
    GEN_SKB(0x80, AM_IMMED, 2),
    GEN_SKB(0x82, AM_IMMED, 2),
    GEN_SKB(0xC2, AM_IMMED, 2),
    GEN_SKB(0xE2, AM_IMMED, 2),
    GEN_SKB(0x04, AM_ZP,    3),
    GEN_SKB(0x14, AM_ZPX,   4),
    GEN_SKB(0x34, AM_ZPX,   4),
    GEN_SKB(0x44, AM_ZP,    3),
    GEN_SKB(0x54, AM_ZPX,   4),
    GEN_SKB(0x64, AM_ZP,    3),
    GEN_SKB(0x74, AM_ZPX,   4),
    GEN_SKB(0xD4, AM_ZPX,   4),
    GEN_SKB(0xF4, AM_ZPX,   4),
    GEN_SKB(0x89, AM_IMMED, 2),

    // SKW skips next word (two bytes).
#define GEN_SKW(CODE, MODE, X) GEN_OP(CODE, 1, MODE, 4, 1, NOPw##X, "SKW Skip next word (NOPx3)")
    // Takes 4 cycles to execute.
    //
    // To be dizzyingly precise, SKW actually performs a read operation.  It's
    // just that the value read is not stored in any register.  Further, opcode 0C
    // uses the absolute addressing mode.  The two bytes which follow it form the
    // absolute address.  All the other SKW opcodes use the absolute indexed X
    // addressing mode.  If a page boundary is crossed, the execution time of one
    // of these SKW opcodes is upped to 5 clock cycles.
    GEN_SKW(0x0C, AM_ABS,  a),
    GEN_SKW(0x1C, AM_ABSX, ax),
    GEN_SKW(0x3C, AM_ABSX, ax),
    GEN_SKW(0x5C, AM_ABSX, ax),
    GEN_SKW(0x7C, AM_ABSX, ax),
    GEN_SKW(0xDC, AM_ABSX, ax),
    GEN_SKW(0xFC, AM_ABSX, ax),

#define GEN_DOUBLE(BASE, NAME, DESC) \
    GEN_OP(BASE + 0x07, 1, AM_ZP,   5, 0, NAME##z,  DESC " mem with Accum [ab]"),     \
    GEN_OP(BASE + 0x17, 1, AM_ZPX,  6, 0, NAME##zx, DESC " mem with Accum [ab+X]"),   \
    GEN_OP(BASE + 0x0F, 1, AM_ABS,  6, 0, NAME##a,  DESC " mem with Accum abcd"),   \
    GEN_OP(BASE + 0x1F, 1, AM_ABSX, 7, 0, NAME##ax, DESC " mem with Accum abcd,X"), \
    GEN_OP(BASE + 0x1B, 1, AM_ABSY, 7, 0, NAME##ay, DESC " mem with Accum abcd,Y"), \
    GEN_OP(BASE + 0x03, 1, AM_INDX, 8, 0, NAME##ix, DESC " mem with Accum (ab,X)"), \
    GEN_OP(BASE + 0x13, 1, AM_INDY, 8, 0, NAME##iy, DESC " mem with Accum (ab),Y")

    GEN_DOUBLE(0x00, SLO, "ASL/OR"),
    GEN_DOUBLE(0x20, RLA, "ROL/AND"),
    GEN_DOUBLE(0x40, SRE, "LSR/EOR"),
    GEN_DOUBLE(0x60, RRA, "ROR/ADC"),
    GEN_DOUBLE(0xC0, DCP, "DEC/CMP"),
    GEN_DOUBLE(0xE0, ISB, "INC/SBC"),

    // SAX
    GEN_OP(0x87, 1, AM_ZP,    3, 0, SAXz,  "AND A/X into mem Zero Page     A=[nn]"),
    GEN_OP(0x97, 1, AM_ZPY,   4, 0, SAXzy, "AND A/X into mem Zero Page,Y   A=[nn+Y]"),
    GEN_OP(0x8F, 1, AM_ABS,   4, 0, SAXa,  "AND A/X into mem Absolute      A=[nnnn]"),
    GEN_OP(0x83, 1, AM_INDX,  6, 0, SAXix, "AND A/X into mem (Indirect,X)  A=[WORD[nn+X]]"),

    // SAX
    GEN_OP(0xCB, 1, AM_IMMED, 2, 0, SAX, "X = (A AND X) - nn"),

    // SAY
    GEN_OP(0x9C, 1, AM_ABS,   5, 0, SAYax, "[abcd] = Y AND (ab + 1)"),

    // XAS
    GEN_OP(0x9E, 1, AM_ABS,   5, 0, XASay, "[abcd] = X AND (ab + 1)"),

    // AXA
    GEN_OP(0x9F, 1, AM_ABS,   5, 0, AXAay, "[abcd] = A AND X AND (ab + 1)"),
    GEN_OP(0x93, 1, AM_ABS,   6, 0, AXAiy, "[abcd] = A AND X AND (ab + 1)"),

    // LAX
    GEN_OP(0xA7, 1, AM_ZP,    3, 0, LAXz,  "Load A/X with Zero Page     A=[nn]"),
    GEN_OP(0xB7, 1, AM_ZPY,   4, 0, LAXzy, "Load A/X with Zero Page,Y   A=[nn+Y]"),
    GEN_OP(0xAF, 1, AM_ABS,   4, 0, LAXa,  "Load A/X with Absolute      A=[nnnn]"),
    GEN_OP(0xBF, 1, AM_ABSY,  4, 1, LAXay, "Load A/X with Absolute,Y    A=[nnnn+Y]"),
    GEN_OP(0xA3, 1, AM_INDX,  6, 0, LAXix, "Load A/X with (Indirect,X)  A=[WORD[nn+X]]"),
    GEN_OP(0xB3, 1, AM_INDY,  5, 1, LAXiy, "Load A/X with (Indirect),Y  A=[WORD[nn]+Y]"),

    GEN_OP(0xBB, 1, AM_ABSY,  3, 1, LARay, "AND stack pointer A/X with Absolute,Y    A/X/S=([nnnn+Y] & S)"),

    GEN_OP(0x4B, 1, AM_IMMED, 2, 0, ALR, "AND A with immediate and LSR  A=(A AND nn) >> 1"),
    GEN_OP(0x6B, 1, AM_IMMED, 2, 0, ARR, "AND A with immediate and ROR  A=(A AND nn) >>> 1"),

    GEN_OP(0x8B, 1, AM_IMMED, 2, 0, XAA, "A=(A* AND X AND nn)"),
    GEN_OP(0xAB, 1, AM_IMMED, 2, 0, LXA, "A,X=(A* AND nn)"),

    // TAS
    GEN_OP(0x9B, 1, AM_ABS,   5, 0, TASay, "FIXME"),
};

#ifdef SEGMENTED_6502
static uint8_t mem64k[0xffff+1];
static inline void write64k(uint16_t addr, uint8_t data)
{
    mem64k[addr] = data;
}

static inline uint8_t read64k(uint16_t addr)
{
    return mem64k[addr];
}
#endif

void
n6502_init(N6502_t *cpu)
{
    NOTIFY("6502 init\n");

    cpu->trigger = NULL;
    cpu->trigger_cycle = 0;
    cpu->trigger_ptr = NULL;

#ifdef SEGMENTED_6502
    cpu->read_mem = read64k;
    cpu->write_mem = write64k;
#else
    // NOTE: not necessary to clear the memory unless debugging
    memset(cpu->mem, 0, sizeof(cpu->mem));
#endif

    // Set the first instruction to a trap so that if no instruction is programmed we will trap instead of BRK
    WRITE_MEM(0000, OP_DEBUG_TRAP);
}

void
n6502_reset(N6502_t *cpu)
{
    NOTIFY("6502 reset\n");

    cpu->regs.P.word = 0x24; // FIXME: nesdev says P @ reset = $34 (http://wiki.nesdev.com/w/index.php/CPU_ALL)
    //FLAG(_5) = 1;
    //FLAG(I) = 1;
    cpu->regs.PC = WORD16(RESET_VECTOR);
    cpu->regs.S = 0xfd; // FIXME: this is to match nes

    cpu->stopped = 0;
    cpu->heartbeat_count = cpu->heartbeat_at; // FIXME: why?

    cpu->inst_count = 0;
    cpu->cycle = 0;
    cpu->heartbeat_at = 0;
    cpu->heartbeat_count = 0;

    cpu->last_breakpoint_cycle = 0;
}

const char *
n6502_dis(N6502_t *cpu)
{
    static char dis[64];
    uint8_t op = READ_MEM(cpu->regs.PC);
    opcode_t opcode = OPCODES[op];

#if 1
    uint8_t op2 = READ_MEM(cpu->regs.PC + 1);
    uint8_t op3 = READ_MEM(cpu->regs.PC + 2);
    snprintf(dis, sizeof(dis), "%02X %s %02X %02X | %s",
             op, opcode.name, op2, op3, opcode.desc);
#else
    snprintf(dis, sizeof(dis), "%02X %s", op, opcode.name);
#endif
    dis[sizeof(dis) - 1] = 0;
    return dis;
}

void
n6502_dump_stack(N6502_t *cpu)
{
    int i;
    int max = 4;
    LOG("Stack: ");
    if(cpu->regs.S == 0xff)
        LOG("empty ");

    for(i = cpu->regs.S + 1; i <= 0xff; i++)
    {
        if(--max < 0)
            return;

        LOG("%02X ", READ_STACK(i));
    }
}

void
n6502_dump_state_verbose(N6502_t *cpu)
{
    INFO("------------- # %8" PRIu64 " C %8" PRIu64 " -----------------\n",
        cpu->inst_count, cpu->cycle);
    INFO("A  %02Xh | X  %02Xh | Y  %02Xh\n", cpu->regs.A, cpu->regs.X, cpu->regs.Y);

    // NOTE: flags are ordered to match Everynes (http://nocash.emubase.de/everynes.htm)
    INFO("F  %02Xh %c%c%c%c%c%c%c  | S  %02Xh | ",
        cpu->regs.P.word,
        FLAG(N) ? 'n' : '-',
        FLAG(Z) ? 'z' : '-',
        FLAG(C) ? 'c' : '-',
        FLAG(I) ? 'i' : '-',
        FLAG(D) ? 'd' : '-',
        FLAG(V) ? 'v' : '-',

        FLAG(B) ? 'b' : '-',
        cpu->regs.S);
    n6502_dump_stack(cpu);
    INFO("\n");
    INFO("PC %04Xh : %s\n", cpu->regs.PC, n6502_dis(cpu));
}

/*
6502 Disassembler
Examples:

863E  2C 02 20  BIT $2002 = 00                  A:40 X:10 Y:10 P:37 SP:FD CYC: 63 SL:87
8641  F0 FB     BEQ $863E                       A:40 X:10 Y:10 P:37 SP:FD CYC: 75 SL:87

FIXME: add a branch taken/not taken flag
*/

void
n6502_disassemble(N6502_t *cpu)
{
    // FIXME: this code is volatile => memory reads can affect system state
    char opcode_bytes[MAX_OP_SIZE][3];
    char dis[128];
    char opcode_ops[32];
    int i;
    const opcode_t *opcode = &OPCODES[READ_MEM(cpu->regs.PC)];
    int size = OP_SIZE[opcode->mode];
    uint16_t tmp;

    for(i = 0; i < MAX_OP_SIZE; i++)
    {
        if(i >= size)
        {
            opcode_bytes[i][0] = ' ';
            opcode_bytes[i][1] = ' ';
            opcode_bytes[i][2] = 0;
        }
        else
        {
            uint8_t byte = READ_MEM(cpu->regs.PC + i);
            sprintf(opcode_bytes[i], "%02X", byte);
        }
    }

    switch(opcode->mode)
    {
        case AM_IMPLIED:
            opcode_ops[0] = 0;
            break;

        case AM_ACCUM:
            sprintf(opcode_ops, "A");
            break;

        case AM_IMMED:
            sprintf(opcode_ops, "#$%02X", READ_MEM(cpu->regs.PC + 1));
            break;

        case AM_RELATIVE:
            tmp = cpu->regs.PC + 2 + (int8_t) READ_MEM(cpu->regs.PC + 1);

            sprintf(opcode_ops, "$%04X", tmp);
            break;

        case AM_ZP:
            tmp = READ_MEM(cpu->regs.PC + 1);
            sprintf(opcode_ops, "$%02X = %02X", tmp, READ_MEM(tmp));
            break;

        case AM_ZPX:      /* Zero page indexed with X */
        {
            uint8_t zp = READ_MEM(cpu->regs.PC + 1);
            uint8_t zpx = zp + cpu->regs.X;
            sprintf(opcode_ops, "$%02X,X @ %02X = %02X", zp, zpx, READ_MEM(zpx));
            break;
        }

        case AM_ZPY:      /* Zero page indexed with Y */
        {
            uint8_t zp = READ_MEM(cpu->regs.PC + 1);
            uint8_t zpy = zp + cpu->regs.Y;
            sprintf(opcode_ops, "$%02X,Y @ %02X = %02X", zp, zpy, READ_MEM(zpy));
            break;
        }

        case AM_JMPABS:
            tmp = ADDR16(cpu->regs.PC + 1);
            sprintf(opcode_ops, "$%04X", tmp);
            break;

        case AM_ABS:
            tmp = ADDR16(cpu->regs.PC + 1);
            sprintf(opcode_ops, "$%04X = %02X", tmp, READ_MEM(tmp));
            break;

        case AM_ABSX:     /* Absolute indexed with X */
        {
            uint16_t a = ADDR16(cpu->regs.PC + 1);
            uint16_t ax = a + cpu->regs.X;
            sprintf(opcode_ops, "$%04X,X @ %04X = %02X", a, ax, READ_MEM(ax));
            break;
        }

        case AM_ABSY:     /* Absolute indexed with Y */
        {
            uint16_t a = ADDR16(cpu->regs.PC + 1);
            uint16_t ay = a + cpu->regs.Y;

            sprintf(opcode_ops, "$%04X,Y @ %04X = %02X", a, ay, READ_MEM(ay));
            break;
        }

        case AM_INDA:     /* Indirect Absolute */
            tmp = ADDR16(cpu->regs.PC + 1);
            sprintf(opcode_ops, "($%04X) = %04X", tmp, ADDR16(tmp));
            break;

        case AM_INDX:     /* Indexed indirect (with x) */
        {
            uint8_t i = READ_MEM(cpu->regs.PC + 1);
            uint8_t ix = i + cpu->regs.X;
            uint16_t a = WORD16(ix);
            sprintf(opcode_ops, "($%02X,X) @ %02X = %04X = %02X", i, ix, a, READ_MEM(a));
            break;
        }
        case AM_INDY:     /* Indirect indexed (with y) */
        {
            uint8_t i = READ_MEM(cpu->regs.PC + 1);
            uint16_t a = WORD16(i);
            uint16_t ay = a + cpu->regs.Y;
            sprintf(opcode_ops, "($%02X),Y = %04X @ %04X = %02X", i, a, ay, READ_MEM(ay));
            break;
        }

        default:
            ASSERT(0, "badness");
            break;
    }

    //863E  2C 02 20  BIT $2002 = 00                  A:40 X:10 Y:10 P:37 SP:FD CYC: 63 SL:87
    sprintf(dis, "%04X  %s %s %s %c%c%c%c %-27s A:%02X X:%02X Y:%02X P:%02X SP:%02X ",
            cpu->regs.PC,
            opcode_bytes[0], opcode_bytes[1], opcode_bytes[2],
            opcode->undocumented ? '*' : ' ',
            opcode->name[0], opcode->name[1], opcode->name[2],
            opcode_ops,
            cpu->regs.A, cpu->regs.X, cpu->regs.Y, cpu->regs.P.word, cpu->regs.S);
    INFO("%s", dis);
}

void
n6502_dump_state(N6502_t *cpu)
{
    n6502_disassemble(cpu);

    if(cpu->dump_state_func)
    {
        cpu->dump_state_func(cpu->dump_state_ptr);
    }
}

static inline void
n6502_step1(N6502_t *cpu)
{
    uint8_t op = READ_MEM(cpu->regs.PC);
    opcode_t opcode = OPCODES[op];

    if(opcode.name)
    {
        if(cpu->options.dump)
            n6502_dump_state(cpu);

        cpu->regs.PC++;
        opcode.op(cpu);
        cpu->cycle += opcode.cycles;
        cpu->inst_count++;
        cpu->heartbeat_count--;
        if(cpu->heartbeat_count == 0 && cpu->heartbeat_at > 0)
        {
            NOTIFY("IC: %" PRIu64 "\n", cpu->inst_count);
            cpu->heartbeat_count = cpu->heartbeat_at;
        }

        if(cpu->trigger && cpu->cycle >= cpu->trigger_cycle)
        {
            cpu->trigger(cpu->trigger_ptr);
            cpu->trigger = NULL;
        }
    }
    else
    {
        die("UNKNOWN 6502 OP @ PC $%04X: $%02X\n", cpu->regs.PC, op);
    }
}

void
n6502_step(N6502_t *cpu)
{
    n6502_step1(cpu);
}

#ifndef WIN32
typedef enum
{
    CMD_UNKNOWN = 0,
    CMD_HELP,
    CMD_QUIT,
    CMD_STEP,
    CMD_CONTINUE,
    CMD_BREAKPOINT,
    CMD_PRINTMEM,
    CMD_JUMP,
} cmd_t;

static const struct
{
    const char *cmd_name;
    const char *desc;
    cmd_t command;
}
COMMANDS[] =
{
    {"h",     "Help", CMD_HELP},
    {"?",     "Help", CMD_HELP},

    {"q",     "Quit", CMD_QUIT},
    {"quit",  "Quit", CMD_QUIT},

    {"s",     "Step", CMD_STEP},
    {"step",  "Step", CMD_STEP},
    {"n",     "Step", CMD_STEP},
    {"next",  "Step", CMD_STEP},

    {"c",     "Continue",        CMD_CONTINUE},

    {"b",     "Add breakpoint",  CMD_BREAKPOINT},
    {"break", "Add breakpoint",  CMD_BREAKPOINT},

    {"p",     "Print memory",    CMD_PRINTMEM},
    {"j",     "Jump PC to addr", CMD_JUMP},
};

static void
n6502_cli_help(N6502_t *cpu)
{
    size_t i;
    printf("Commands available:\n");
    for(i = 0; i < sizeof(COMMANDS) / sizeof(COMMANDS[0]); i++)
    {
        printf("%-8s %s\n", COMMANDS[i].cmd_name, COMMANDS[i].desc);
    }
}

#include <readline/readline.h>
#include <readline/history.h>
#endif

void
n6502_cli(N6502_t *cpu)
{
#ifndef WIN32
    int done = 0;
    do
    {
        char str_cmd[16];
        cmd_t cmd;
        char *line;
        const char *space;
        const char *args;
        size_t i;

        line = readline("> ");
        args = NULL;

        if(line == NULL)
        {
            cmd = CMD_QUIT;
        }
        else
        {
            space = strchr(line, ' ');

            strncpy(str_cmd, line, sizeof(str_cmd) - 1);
            str_cmd[sizeof(str_cmd) - 1] = 0;

            if(space != NULL)
            {
                int space_offset = space - line;
                str_cmd[space_offset] = 0;
                args = line + space_offset + 1;
            }

            cmd = CMD_UNKNOWN; // Default command

            // Iterate through and match the command
            for(i = 0; i < sizeof(COMMANDS) / sizeof(COMMANDS[0]); i++)
            {
                if(strcmp(str_cmd, COMMANDS[i].cmd_name) == 0)
                {
                    cmd = COMMANDS[i].command;
                    break;
                }
            }
        }

        //printf("cmd: [%s] args: [%s]\n", str_cmd, args);

        switch(cmd)
        {
            case CMD_QUIT:
                NOTIFY("Quitting\n");
                exit(1);
                break;

            case CMD_BREAKPOINT:
                if(args == NULL)
                {
                    printf("Specify a breakpoint\n");
                }
                else
                {
                    uint32_t breakpoint = htoi(args);

                    if(breakpoint < 0x8000 || breakpoint > 0xffff)
                    {
                        printf("Bad breakpoint: %s (0x%x)\n", args, breakpoint);
                    }
                    else
                    {
                        cpu->options.breakpoint = breakpoint;

                        printf("Set breakpoint @ PC %04Xh\n", cpu->options.breakpoint);
                    }
                }
                break;

            case CMD_JUMP:
                if(args == NULL)
                {
                    printf("Specify a jump address\n");
                }
                else
                {
                    uint32_t addr = htoi(args);

                    if(addr < 0x8000 || addr > 0xffff)
                    {
                        printf("Bad jump address: %s (0x%x)\n", args, addr);
                    }
                    else
                    {
                        cpu->regs.PC = addr;

                        printf("Set PC to %04Xh\n", cpu->regs.PC);
                        n6502_disassemble(cpu);
                        printf("\n");
                    }
                }
                break;

            case CMD_PRINTMEM:
                if(args == NULL)
                {
                    printf("Specify a memory address\n");
                }
                else
                {
                    uint32_t addr = htoi(args);

                    if(addr > 0xffff)
                    {
                        printf("Bad addr: 0x%x\n", addr);
                    }
                    else
                    {
                        printf("MEM[%04X] => %02X\n", addr, READ_MEM(addr));
                    }
                }
                break;

            case CMD_CONTINUE:
                cpu->options.step = 0;
                done = 1;
                break;

            case CMD_STEP:
                // FIXME: parse # instructions to be executed
                done = 1;
                break;

            case CMD_HELP:
                n6502_cli_help(cpu);
                break;

            case CMD_UNKNOWN:
            default:
                printf("Unknown command: %s\n", str_cmd);
                n6502_cli_help(cpu);
                break;
        }

        add_history(line);
        free(line);
    } while(! done);
#endif
}

void
n6502_run(N6502_t *cpu, int64_t max_cycles, int hard_limit)
{
    int64_t last_cycle = cpu->cycle + max_cycles;

    if(cpu->options.step || cpu->options.breakpoint)
    {
        while(cpu->cycle + OPCODES[READ_MEM(cpu->regs.PC)].cycles < last_cycle)
        {
            if(cpu->options.breakpoint == cpu->regs.PC)
            {
                printf("Hit breakpoint @ %04Xh\n", cpu->regs.PC);
                printf("%" PRIu64 " cycles elapsed\n", cpu->cycle - cpu->last_breakpoint_cycle);
                cpu->last_breakpoint_cycle = cpu->cycle;
                cpu->options.step = 1;
            }

            if(cpu->options.step)
            {
                n6502_dump_state(cpu);
                n6502_cli(cpu);
            }
            n6502_step1(cpu);
        }
    }
    else
    {
        if(hard_limit)
        {
            while(cpu->cycle + OPCODES[READ_MEM(cpu->regs.PC)].cycles < last_cycle)
            {
                n6502_step1(cpu);
            }
        }
        else
        {
            while(cpu->cycle < last_cycle)
            {
                n6502_step1(cpu);
            }
        }
    }
}

void
n6502_run_until_stopped(N6502_t *cpu, int64_t max_instructions)
{
    int64_t i;

    for(i = 0; i < max_instructions; i++)
    {
        n6502_step1(cpu);
        if(cpu->stopped)
            break;
    }

    if(cpu->options.log)
    {
        printf("\nCPU stopped: %" PRIu64 " instructions, %" PRIu64" cycles\n",
               cpu->inst_count,
               cpu->cycle);
    }
}

static void
n6502_interrupt(N6502_t *cpu, uint16_t vector)
{
    cpu->cycle += INTERRUPT_CYCLES;

    PUSH_PC();
    PUSH_STACK(cpu->regs.P.word);
    FLAG(I) = 1;
    cpu->regs.PC = WORD16(vector);
    LOG("Vectoring to %04Xh\n", cpu->regs.PC);
}

void
n6502_nmi(N6502_t *cpu)
{
    // FIXME: the blargg vblank time test is off by 3 cycles.
    // I think this is because we execute the op before incrementing cycles.
    // If so, we need to pre-increment (or remove from the "surplus").

    INFO("NMI @ %04Xh (cycle %" PRIu64 ")\n", cpu->regs.PC, cpu->cycle);
    n6502_interrupt(cpu, NMI_VECTOR);
}

void
n6502_irq(N6502_t *cpu)
{
    if(! FLAG(I))
    {
        INFO("IRQ @ %04Xh\n", cpu->regs.PC);
        n6502_interrupt(cpu, IRQ_VECTOR);
    }
    else
    {
        INFO("IRQ masked off\n");
    }
}
