#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <stdint.h>
#include <stdio.h>
#include "config.h"

#define NUM_VECTOR_REGISTERS 32
#define VECTOR_LANES 8

void reset(int proc_id);
void fetch(int proc_id);
void decode(int proc_id);
void execute(int proc_id);

void process_instructions(int proc_id, int instruction_count);

extern int32_t Register[NP][256];
extern int32_t VRegister[NP][NUM_VECTOR_REGISTERS][VECTOR_LANES];

extern int PC[NP];
extern int Opcode[NP];
extern int Dest[NP];
extern int Src1[NP];
extern int Src2[NP];

extern int end_of_simulation[NP];

extern int Z[NP];
extern int N[NP];
extern int C[NP];
extern int V[NP];

extern FILE *fd_log;

#endif
