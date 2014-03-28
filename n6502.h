#ifndef __n6502_h__
#define __n6502_h__

#include <stdint.h>
#include "log.h"

#define SEGMENTED_6502

// NES-compatible 6502
// Memory is paged (ie page1 == 0000h-00FFh)
typedef struct N6502
{
    struct
    {
        uint8_t  A; // Accumulator
        uint8_t  X;
        uint8_t  Y;
        uint8_t  S; // Stack pointer

        uint16_t PC;

        union
        {
            uint8_t word;

            struct
            {
                unsigned C  : 1; // Carry
                unsigned Z  : 1; // Zero
                unsigned I  : 1; // Interrupt enable
                unsigned D  : 1; // Decimal mode status
                unsigned B  : 1; // Software interrupt (BRK)
                unsigned _5 : 1; // _unused (always 1)
                unsigned V  : 1; // Overflow
                unsigned N  : 1; // Sign (negative)
            } bits;
        } P; // Processor status
    } regs;

#ifdef SEGMENTED_6502
    uint8_t (*read_mem) (uint16_t addr);
    void    (*write_mem)(uint16_t addr, uint8_t data);
#else
    uint8_t mem[0xffff + 1];
#endif

    int enable_decimal;
    uint16_t min_pc;

    int64_t inst_count;
    int64_t cycle;
    int64_t heartbeat_at;
    int64_t heartbeat_count;

    int64_t last_breakpoint_cycle;

    int stopped;

    void (*dump_state_func)(void *ptr);
    void *dump_state_ptr;

    void (*trigger)(void *p);
    int64_t trigger_cycle;
    void *trigger_ptr;

    void (*debug_trap)(struct N6502 *cpu);

    struct
    {
        int dump;
        int log;

        int test;

        char *override; // FIXME: should go in c64 harness
        int skip;       // FIXME: should go in c64 harness

        int step;
        uint16_t breakpoint;
    } options;
} N6502_t;

// --------------------------------------------------------------------------------

void n6502_init(N6502_t *cpu);
void n6502_reset(N6502_t *cpu);
void n6502_dump_state(N6502_t *cpu);
const char *n6502_dis(N6502_t *cpu);
void n6502_step(N6502_t *cpu);
void n6502_nmi(N6502_t *cpu);
void n6502_irq(N6502_t *cpu);
void n6502_run(N6502_t *cpu, int64_t max_cycles, int hard_limit);
void n6502_run_until_stopped(N6502_t *cpu, int64_t max_instructions);

// --------------------------------------------------------------------------------
#define STACK_BASE     0x100
#define OP_DEBUG_TRAP  0x02

//#ifdef DEBUG
//#define WRITE_MEM(a, v)  {uint16_t ADDR = (a); cpu->mem[ADDR] = (v); LOG("MEM[%04Xh] <= %02Xh\n", ADDR, cpu->mem[ADDR]); }
//#else
//#define WRITE_MEM(a, v)  cpu->mem[a] = (v)
//#endif
//
#ifdef SEGMENTED_6502

#define WRITE_MEM(a,v) cpu->write_mem(a,v)
#define READ_MEM(a)    cpu->read_mem(a)

#else

static inline void writemem(N6502_t *cpu, uint16_t addr, uint8_t data)
{
    cpu->mem[addr] = data;
}

static inline uint16_t readmem(N6502_t *cpu, uint16_t addr)
{
    return cpu->mem[addr];
}

#define WRITE_MEM(a,v) writemem(cpu, a, v)
#define READ_MEM(a)    readmem(cpu, a)
#endif

#define FLAG(f)   cpu->regs.P.bits.f

#define SET_Z(v)  FLAG(Z) = ((v) == 0)
#define SET_N(v)  FLAG(N) = ((v) >> 7)
#define SET_ZN(v) SET_Z(v); SET_N(v)

#define SET_A(v) cpu->regs.A = (v); SET_ZN(cpu->regs.A)
#define SET_X(v) cpu->regs.X = (v); SET_ZN(cpu->regs.X)
#define SET_Y(v) cpu->regs.Y = (v); SET_ZN(cpu->regs.Y)

#define ADDR16(A)  (READ_MEM((A)) | (READ_MEM((A) + 1) << 8))

#define IMM8(r)  (READ_MEM(cpu->regs.PC++))
#define IMM16()  (ADDR16(cpu->regs.PC))

static inline uint16_t _WORD16(N6502_t *cpu, uint16_t addr)
{
    // Glitch: For indirect reads [nnnn] the operand word cannot cross page boundaries,
    // ie. [03FFh] would fetch the MSB from [0300h] instead of [0400h].
    // LSB is unaffected.
    if((addr & 0xff) == 0xff)
    {
        return READ_MEM(addr) | (READ_MEM(addr - 0xff) << 8);
    }

    return ADDR16(addr);
}
#define WORD16(addr) _WORD16(cpu, (addr))

#define READ_STACK(i)     READ_MEM(STACK_BASE + (i))
#define WRITE_STACK(i, v) WRITE_MEM(STACK_BASE + (i), v)

#define PUSH_STACK(v) WRITE_STACK(cpu->regs.S--, v)
#define POP_STACK()   READ_STACK(++cpu->regs.S)

#define PUSH_PC() { PUSH_STACK(cpu->regs.PC >> 8); PUSH_STACK(cpu->regs.PC & 0xff); }
#define POP_PC()  { cpu->regs.PC = POP_STACK(); cpu->regs.PC |= POP_STACK() << 8; }

#endif
