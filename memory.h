#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include "config.h"

#define INSTRUCTION_MEMORY_SIZE 256
#define DATA_MEMORY_SIZE 4096

#define PAGESIZE 512
#define MEMSIZE 8192
#define NUM_PHYSICAL_PAGES (MEMSIZE / PAGESIZE)

#define INSTR_LOGICAL_SIZE 1024
#define INSTR_PAGE_COUNT (INSTR_LOGICAL_SIZE / PAGESIZE)
#define DATA_PAGE_COUNT (DATA_MEMORY_SIZE / PAGESIZE)
#define NUM_LOGICAL_PAGES (INSTR_PAGE_COUNT + DATA_PAGE_COUNT)

extern unsigned char memory[MEMSIZE];

extern unsigned char Instruction[NP][INSTRUCTION_MEMORY_SIZE];
extern unsigned char Data[NP][DATA_MEMORY_SIZE];

extern unsigned char pageTable[NP][NUM_LOGICAL_PAGES];

extern unsigned char freeFrames[NUM_PHYSICAL_PAGES];

void mmu_init(void);

int getFreePage(void);

void freeFrame(int frame);

int countFreeFrames(void);

int getPhysicalAddress(int proc_id, int isFetch, int address);

int initialize(int proc_id, const char *programFile, const char *dataFile);

void finalize(int proc_id, const char *dataFile);

void write32(int proc_id, uint32_t address, int32_t value);

int32_t read32(int proc_id, uint32_t address);

#endif
