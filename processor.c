#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "processor.h"
#include "memory.h"

int32_t Register[NP][256];
int32_t VRegister[NP][NUM_VECTOR_REGISTERS][VECTOR_LANES];

int PC[NP];

int Opcode[NP];
int Dest[NP];
int Src1[NP];
int Src2[NP];

int end_of_simulation[NP];

int Z[NP];
int N[NP];
int C[NP];
int V[NP];

FILE *fd_log = NULL;

void reset(int proc_id)
{
    int j;

    for (j = 0; j < 256; j++)
        Register[proc_id][j] = 0;

    for (j = 0; j < NUM_VECTOR_REGISTERS; j++)
    {
        int k;
        for (k = 0; k < VECTOR_LANES; k++)
            VRegister[proc_id][j][k] = 0;
    }

    PC[proc_id] = 0;
    end_of_simulation[proc_id] = 0;
    Z[proc_id] = 0;
    N[proc_id] = 0;
    C[proc_id] = 0;
    V[proc_id] = 0;

    if (fd_log == NULL)
    {
        fd_log = fopen("execution.log", "a");
        if (fd_log == NULL)
            printf("Warning: could not open execution.log for logging\n");
    }
}

void fetch(int proc_id)
{
    int physAddr = getPhysicalAddress(proc_id, 1, PC[proc_id]);

    if (physAddr < 0)
    {
        end_of_simulation[proc_id] = 1;
        return;
    }

    Opcode[proc_id] = memory[physAddr];
    Dest[proc_id] = memory[physAddr + 1];
    Src1[proc_id] = memory[physAddr + 2];
    Src2[proc_id] = memory[physAddr + 3];
    PC[proc_id] += 4;
}

void decode(int proc_id)
{
    (void)proc_id;
}

static void update_add_flags(int proc_id, int32_t a, int32_t b, int32_t result)
{
    uint32_t ua = (uint32_t)a;
    uint32_t ub = (uint32_t)b;
    uint32_t ur = (uint32_t)result;
    Z[proc_id] = (ur == 0);
    N[proc_id] = ((ur >> 31) & 1);
    C[proc_id] = (ur < ua || ur < ub);
    V[proc_id] = (((a >= 0) && (b >= 0) && (result < 0)) || ((a < 0) && (b < 0) && (result >= 0)));
}

static void update_sub_flags(int proc_id, int32_t a, int32_t b, int32_t result)
{
    uint32_t ur = (uint32_t)result;
    Z[proc_id] = (ur == 0);
    N[proc_id] = ((ur >> 31) & 1);
    C[proc_id] = (a > b);
    V[proc_id] = (((a >= 0) && (b < 0) && (result < 0)) || ((a < 0) && (b >= 0) && (result >= 0)));
}

static int branch_condition(int proc_id, int condition)
{
    switch (condition)
    {
    case 0x0:
        return Z[proc_id] == 1;
    case 0x1:
        return Z[proc_id] == 0;
    case 0x2:
        return C[proc_id] == 1;
    case 0x3:
        return C[proc_id] == 0;
    case 0x4:
        return N[proc_id] == 1;
    case 0x5:
        return N[proc_id] == 0;
    case 0x6:
        return V[proc_id] == 1;
    case 0x7:
        return V[proc_id] == 0;
    case 0x8:
        return (C[proc_id] == 1 && Z[proc_id] == 0);
    case 0x9:
        return (C[proc_id] == 0 || Z[proc_id] == 1);
    case 0xA:
        return N[proc_id] == V[proc_id];
    case 0xB:
        return N[proc_id] != V[proc_id];
    case 0xC:
        return (Z[proc_id] == 0 && N[proc_id] == V[proc_id]);
    case 0xD:
        return (Z[proc_id] == 1 || N[proc_id] != V[proc_id]);
    case 0xE:
        return 1;
    default:
        return 0;
    }
}

static void vector_binop(int proc_id, int vopcode, int vdest, int vsrc1, int vsrc2)
{
    int i;
    int is_constant_form = (vopcode == 0x29 || vopcode == 0x2A || vopcode == 0x2B);
    int is_broadcast_reg = (!is_constant_form) && (vsrc2 & 0x80);
    int32_t broadcast_value = is_broadcast_reg ? Register[proc_id][vsrc2 & 0x7F] : 0;
    int32_t constant_value = is_constant_form ? (int32_t)vsrc2 : 0;

    for (i = 0; i < VECTOR_LANES; i++)
    {
        int32_t a = VRegister[proc_id][vsrc1 & 0x1F][i];
        int32_t b;
        int32_t result;

        if (is_constant_form)
            b = constant_value;
        else if (is_broadcast_reg)
            b = broadcast_value;
        else
            b = VRegister[proc_id][vsrc2 & 0x1F][i];

        switch (vopcode)
        {
        case 0x21:
        case 0x29:
            result = (int32_t)((uint32_t)a + (uint32_t)b);
            break;

        case 0x22:
        case 0x2A:
            result = (int32_t)((uint32_t)a - (uint32_t)b);
            break;

        case 0x23:
        case 0x2B:
            result = (int32_t)((int64_t)a * (int64_t)b);
            break;

        default:
            result = 0;
            break;
        }
        VRegister[proc_id][vdest & 0x1F][i] = result;
    }
}

void execute(int proc_id)
{
    int64_t intermediate;
    int op = Opcode[proc_id];
    int dest = Dest[proc_id];
    int src1 = Src1[proc_id];
    int src2 = Src2[proc_id];

    switch (op)
    {
    case 0:
        end_of_simulation[proc_id] = 1;
        break;

    case 1:
    {
        int32_t a = Register[proc_id][src1];
        int32_t b = Register[proc_id][src2];
        Register[proc_id][dest] = (int32_t)((uint32_t)a + (uint32_t)b);
        update_add_flags(proc_id, a, b, Register[proc_id][dest]);
        break;
    }

    case 2:
    {
        int32_t a = Register[proc_id][src1];
        int32_t b = Register[proc_id][src2];
        Register[proc_id][dest] = (int32_t)((uint32_t)a - (uint32_t)b);
        update_sub_flags(proc_id, a, b, Register[proc_id][dest]);
        break;
    }

    case 3:
        intermediate = (int64_t)Register[proc_id][src1] * (int64_t)Register[proc_id][src2];
        Register[proc_id][dest] = (int32_t)intermediate;
        break;

    case 4:
        if (Register[proc_id][src2] != 0)
        {
            if (Register[proc_id][src1] == INT32_MIN && Register[proc_id][src2] == -1)
            {
                printf("Error: [proc %d] 32-bit signed division overflow!\n", proc_id);
                end_of_simulation[proc_id] = 1;
                break;
            }
            Register[proc_id][dest] = Register[proc_id][src1] / Register[proc_id][src2];
        }
        else
        {
            printf("Error: [proc %d] Division by zero!\n", proc_id);
            end_of_simulation[proc_id] = 1;
        }
        break;

    case 5:
    {
        uint32_t address = (src1 != 0) ? (uint32_t)src1 : (uint32_t)Register[proc_id][src2];
        Register[proc_id][dest] = read32(proc_id, address);
        break;
    }

    case 6:
    {
        if (src1 != 0)
        {
            uint32_t address = (uint32_t)src1;
            write32(proc_id, address, Register[proc_id][dest]);
        }
        else
        {
            uint32_t address = (uint32_t)Register[proc_id][dest];
            write32(proc_id, address, Register[proc_id][src2]);
        }
        break;
    }

    case 0x08:
    {
        int32_t value = Register[proc_id][src2];
        if (fd_log != NULL)
        {
            fprintf(fd_log, "Process id: %d  x%d : %X\n", proc_id, src2, (uint32_t)value);
            fflush(fd_log);
        }
        break;
    }

    case 0x09:
    {
        int32_t a = Register[proc_id][src1];
        int32_t b = (int32_t)src2;
        Register[proc_id][dest] = (int32_t)((uint32_t)a + (uint32_t)b);
        update_add_flags(proc_id, a, b, Register[proc_id][dest]);
        break;
    }

    case 0x0A:
    {
        int32_t a = Register[proc_id][src1];
        int32_t b = (int32_t)src2;
        Register[proc_id][dest] = (int32_t)((uint32_t)a - (uint32_t)b);
        update_sub_flags(proc_id, a, b, Register[proc_id][dest]);
        break;
    }

    case 0x0B:
    {
        int32_t a = Register[proc_id][src1];
        int32_t b = (int32_t)src2;
        int64_t product = (int64_t)a * (int64_t)b;
        Register[proc_id][dest] = (int32_t)product;
        break;
    }

    case 0x0C:
    {
        int32_t a = Register[proc_id][src1];
        int32_t b = (int32_t)src2;
        if (b != 0)
        {
            if (a == INT32_MIN && b == -1)
            {
                printf("Error: [proc %d] 32-bit signed division overflow!\n", proc_id);
                end_of_simulation[proc_id] = 1;
                break;
            }
            Register[proc_id][dest] = a / b;
        }
        else
        {
            printf("Error: [proc %d] Division by zero!\n", proc_id);
            end_of_simulation[proc_id] = 1;
        }
        break;
    }

    case 0x0D:
    {
        uint32_t address = (uint32_t)src2;
        Register[proc_id][dest] = read32(proc_id, address);
        break;
    }

    case 0x0E:
    {
        uint32_t address = (uint32_t)Register[proc_id][dest];
        write32(proc_id, address, (int32_t)src2);
        break;
    }

    case 0x0F:
        Register[proc_id][dest] = (int32_t)src2;
        break;

    case 0x21:
    case 0x22:
    case 0x23:
    case 0x29:
    case 0x2A:
    case 0x2B:
        vector_binop(proc_id, op, dest, src1, src2);
        break;

    case 0x25:
    {
        int i;
        uint32_t base = (uint32_t)Register[proc_id][src2];
        for (i = 0; i < VECTOR_LANES; i++)
            VRegister[proc_id][dest & 0x1F][i] = read32(proc_id, base + (uint32_t)(i * 4));
        Register[proc_id][src2] = (int32_t)(base + (uint32_t)(VECTOR_LANES * 4));
        break;
    }

    case 0x2C:
    {
        int i;
        uint32_t base = (uint32_t)src2;
        for (i = 0; i < VECTOR_LANES; i++)
            VRegister[proc_id][dest & 0x1F][i] = read32(proc_id, base + (uint32_t)(i * 4));
        break;
    }

    case 0x26:
    {
        int i;
        uint32_t base = (uint32_t)Register[proc_id][dest];
        for (i = 0; i < VECTOR_LANES; i++)
            write32(proc_id, base + (uint32_t)(i * 4), VRegister[proc_id][src2 & 0x1F][i]);
        Register[proc_id][dest] = (int32_t)(base + (uint32_t)(VECTOR_LANES * 4));
        break;
    }

    case 0x2E:
    {
        int i;
        uint32_t base = (uint32_t)src2;
        for (i = 0; i < VECTOR_LANES; i++)
            write32(proc_id, base + (uint32_t)(i * 4), VRegister[proc_id][dest & 0x1F][i]);
        break;
    }

    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1A:
    case 0x1B:
    case 0x1C:
    case 0x1D:
    case 0x1E:
    {
        int condition = op - 0x10;
        if (branch_condition(proc_id, condition))
        {
            int8_t offset = (int8_t)src2;
            PC[proc_id] = (PC[proc_id] - 4) + ((int)offset * 4);
        }
        break;
    }

    default:
        printf("Error: [proc %d] Invalid opcode %d\n", proc_id, op);
        end_of_simulation[proc_id] = 1;
        break;
    }
}

void process_instructions(int proc_id, int instruction_count)
{
    int i;
    for (i = 0; i < instruction_count; i++)
    {
        if (end_of_simulation[proc_id])
            break;
        fetch(proc_id);
        decode(proc_id);
        execute(proc_id);
        if (end_of_simulation[proc_id])
            break;
    }
    {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = (long)TIME_SLICE_SLEEP_USEC * 1000L;
        nanosleep(&ts, NULL);
    }
}
