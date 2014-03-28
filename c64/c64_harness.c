#include <stdlib.h>
#include <string.h>

#include "c64_harness.h"
#include "log.h"

#define LOG(...) _LOG(C64, __VA_ARGS__)

unsigned char
pet2ascii(unsigned char c)
{
    if(c == 0xe)
    {
        // text mode
        return 0;
    }
    if(c == 0x8)
    {
        printf("<shift disable>");
        return 0;
    }
    if(c == 0x9)
    {
        printf("<shift enable>");
        return 0;
    }

    LOG("petscii: %X ", c);
    if(c == 0xd)
    {
        // Newline
        c = '\n';
    }
    else if(c == 145)
    {
        // Up -- ascii doesn't have, so replace with carriage return
        c = 0xd;
    }
    else if(c >= 0xc0)
        c -= 128;
    else if(c >= 65 && c <= 90)
        c += 32;
    LOG("pet2ascii => [%c]\n", c);
    return c;
}

static void
print_petscii(unsigned char c)
{
    c = pet2ascii(c);
    if(c == 0)
        return;
    printf("%c", c);
#ifdef C64_TYPEWRITER_MODE
    usleep(1000 * 30);
#endif
    fflush(stdout);
}

static void
c64_load_program(N6502_t *cpu, const char *name)
{
    uint16_t addr;
    uint8_t byte;
    char path[64];
    FILE *fp;

    snprintf(path, sizeof(path), "%s/%s", C64_TESTSUITE_PATH, name);

    fp = fopen(path, "rb");
    if(!fp)
    {
        printf("error opening '%s'\n", path);
        abort();
    }

    assert(fread(&addr, 2, 1, fp) != 2);

    LOG("Start addr: %04Xh\n", addr);

    while(fread(&byte, 1, 1, fp) == 1)
    {
#if 0
        if((addr == 0x0840 && byte == 0x6F) ||
           (addr == 0x0841 && byte == 0x09))
            //if(addr == 0x083F && byte == 0x8D)
        {
            //byte = 0xEA; // NOP
            byte = 0;
            printf("\nPatching %04Xh with NOP\n", addr);
        }
#endif

        WRITE_MEM(addr++, byte);
    }

    fclose(fp);
    LOG("Loaded '%s'\n", path);
}

static void
c64_trap(N6502_t *cpu)
{
    switch(cpu->regs.PC)
    {
        case 0xFFD2: // Print character
            print_petscii(cpu->regs.A);
            WRITE_MEM(0x030C, 0);
            POP_PC();
            cpu->regs.PC++;
            break;

        case 0xE16F: // Load
        {
            uint16_t addr = READ_MEM(0xBB) | (READ_MEM(0xBC) << 8);
            unsigned length = READ_MEM(0xB7);
            char filename[length + 1];
            unsigned i;
            for(i = 0; i < length; i++)
            {
                filename[i] = pet2ascii(READ_MEM(addr + i));
            }
            filename[length] = 0;
            if(cpu->options.override)
            {
                static int first = 1;
                if(first)
                {
                    printf("\nNOTE: Overriding '%s' with '%s'\n", filename, cpu->options.override);

                    sprintf(filename, "%s", cpu->options.override);
                    first = 0;
                }
            }
            if(cpu->options.skip)
            {
                size_t i;
                const char *DONE = "DONE";
                const struct
                {
                    const char *from;
                    const char *to;
                }
                skip[] = {
#if 0
                    // FAST test mode
                    {"ldab", "bmir"},
#endif
                    {"adcb", "cmpb"}, // FIXME: adc/sbc Decimal mode
                    {"beqr", "nopn"}, // FIXME: beqr, bcsr seems to have a stack underflow issue
                    {"arrb", "aneb"}, // FIXME: arrb Decimal mode
                    {"sbcb(eb)", "trap1"}, // FIXME: sbc Decimal mode
                    {"mmufetch", DONE},
                };

                for(i = 0; i < sizeof(skip) / sizeof(skip[0]); i++)
                {
                    if(strcmp(filename, skip[i].from) == 0)
                    {
                        if(skip[i].to == DONE)
                        {
                            printf("\nDone!\n");
                            filename[0] = 0;
                            cpu->stopped = 1;
                            break;
                        }

                        printf("\nNOTE: Skipping from '%s' to '%s'\n",
                               skip[i].from, skip[i].to);

                        sprintf(filename, "%s", skip[i].to);
                        break;
                    }
                }
            }
            if(filename[0])
            {
                LOG("\nLOAD: %04Xh (%d): %s\n", addr, length, filename);
                c64_load_program(cpu, filename);

                cpu->regs.PC = 0x0816;
                cpu->regs.S = 0xfd;
                FLAG(I) = 0;
            }
            break;
        }

        case 0xFFE4: // Scan keyboard
            cpu->regs.A = 3;
            POP_PC();
            break;

        case 0x8000:
        case 0xA474:
            printf("Exiting\n");
            exit(1);
            break;

        default:
            printf("Unimplemented TRAP op: %04Xh\n", cpu->regs.PC);
            abort();
            break;

    }
}

void
c64_install_harness(N6502_t *cpu)
{
    cpu->debug_trap = c64_trap;
    cpu->enable_decimal = 1;
    cpu->options.skip = 1;

    c64_load_program(cpu, " start");
    WRITE_MEM(0x0002, 0x00);
    WRITE_MEM(0xA002, 0x00);
    WRITE_MEM(0xA003, 0x80);
    WRITE_MEM(0xFFFE, 0x48);
    WRITE_MEM(0xFFFF, 0xFF);
    WRITE_MEM(0x01FE, 0xFF);
    WRITE_MEM(0x01FF, 0x7F);

    // KERNAL IRQ handler
    WRITE_MEM(0xFF48, 0x48); // PHA
    WRITE_MEM(0xFF49, 0x8A); // TXA
    WRITE_MEM(0xFF4A, 0x48); // PHA
    WRITE_MEM(0xFF4B, 0x98); // TYA
    WRITE_MEM(0xFF4C, 0x48); // PHA
    WRITE_MEM(0xFF4D, 0xBA); // TSX
    WRITE_MEM(0xFF4E, 0xBD); // LDA    $0104,X
    WRITE_MEM(0xFF4F, 0x04);
    WRITE_MEM(0xFF50, 0x01);
    WRITE_MEM(0xFF51, 0x29); // AND    #$10
    WRITE_MEM(0xFF52, 0x10);
    WRITE_MEM(0xFF53, 0xF0); // BEQ    $FF58
    WRITE_MEM(0xFF54, 0x03);
    WRITE_MEM(0xFF55, 0x6C); // JMP    ($0316)
    WRITE_MEM(0xFF56, 0x16);
    WRITE_MEM(0xFF57, 0x03);
    WRITE_MEM(0xFF58, 0x6C); // JMP    ($0314)
    WRITE_MEM(0xFF59, 0x14);
    WRITE_MEM(0xFF60, 0x03);

    // Traps
    WRITE_MEM(0xFFD2, OP_DEBUG_TRAP);
    WRITE_MEM(0xE16F, OP_DEBUG_TRAP);
    WRITE_MEM(0xFFE4, OP_DEBUG_TRAP);
    WRITE_MEM(0x8000, OP_DEBUG_TRAP);
    WRITE_MEM(0xA474, OP_DEBUG_TRAP);

    cpu->regs.S = 0xFD;
    cpu->regs.P.word = 0x04;
    cpu->regs.PC = 0x0801;

    printf("Install c64 harness\n");
}
