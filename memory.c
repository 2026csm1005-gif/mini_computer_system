#include <stdio.h>
#include <string.h>
#include "memory.h"

unsigned char memory[MEMSIZE];

unsigned char Instruction[NP][INSTRUCTION_MEMORY_SIZE];
unsigned char Data[NP][DATA_MEMORY_SIZE];

unsigned char pageTable[NP][NUM_LOGICAL_PAGES];
unsigned char freeFrames[NUM_PHYSICAL_PAGES];

static int data_pages_used[NP];

void mmu_init(void)
{
    int i;
    memset(freeFrames, 0, sizeof(freeFrames));
    freeFrames[0] = 1;
    memset(pageTable, 0, sizeof(pageTable));
    for (i = 0; i < NP; i++)
        data_pages_used[i] = 0;
}

int getFreePage(void)
{
    int i;
    for (i = 1; i < NUM_PHYSICAL_PAGES; i++)
    {
        if (!freeFrames[i])
        {
            freeFrames[i] = 1;
            return i;
        }
    }
    return -1;
}

void freeFrame(int frame)
{
    if (frame > 0 && frame < NUM_PHYSICAL_PAGES)
        freeFrames[frame] = 0;
}

int countFreeFrames(void)
{
    int i, count = 0;
    for (i = 1; i < NUM_PHYSICAL_PAGES; i++)
        if (!freeFrames[i])
            count++;
    return count;
}

static void release_pages(int proc_id)
{
    int i;
    for (i = 0; i < NUM_LOGICAL_PAGES; i++)
    {
        if (pageTable[proc_id][i] != 0)
        {
            freeFrame(pageTable[proc_id][i]);
            pageTable[proc_id][i] = 0;
        }
    }
    data_pages_used[proc_id] = 0;
}

int getPhysicalAddress(int proc_id, int isFetch, int address)
{
    int page = address / PAGESIZE;
    int index = isFetch ? page : (page + INSTR_PAGE_COUNT);
    int frame;

    if (index < 0 || index >= NUM_LOGICAL_PAGES)
    {
        printf("Error: [proc %d] logical address %d is outside the process's address space\n",
               proc_id, address);
        return -1;
    }

    frame = pageTable[proc_id][index];
    if (frame == 0)
    {
        printf("Error: [proc %d] page fault -- logical page %d (address %d) is not mapped\n",
               proc_id, index, address);
        return -1;
    }

    return frame * PAGESIZE + (address % PAGESIZE);
}

static int read_hex_file(const char *path, unsigned char *buf, int maxBytes)
{
    FILE *fp = fopen(path, "r");
    unsigned int byte;
    int index = 0;

    if (fp == NULL)
    {
        printf("Error: Cannot open %s\n", path);
        return -1;
    }
    while (index < maxBytes && fscanf(fp, "%x", &byte) == 1)
        buf[index++] = (unsigned char)(byte & 0xFF);
    fclose(fp);
    return index;
}

int initialize(int proc_id, const char *programFile, const char *dataFile)
{
    int instrBytes, dataBytes;
    int instrPages, dataPages;
    int i, frame;

    release_pages(proc_id);

    memset(Instruction[proc_id], 0, sizeof(Instruction[proc_id]));
    memset(Data[proc_id], 0, sizeof(Data[proc_id]));

    instrBytes = read_hex_file(programFile, Instruction[proc_id], INSTRUCTION_MEMORY_SIZE);
    if (instrBytes < 0)
        return -1;
    dataBytes = read_hex_file(dataFile, Data[proc_id], DATA_MEMORY_SIZE);
    if (dataBytes < 0)
        return -1;

    instrPages = (instrBytes + PAGESIZE - 1) / PAGESIZE;
    if (instrPages > INSTR_PAGE_COUNT)
        instrPages = INSTR_PAGE_COUNT;

    dataPages = (dataBytes + PAGESIZE - 1) / PAGESIZE;
    if (dataPages > DATA_PAGE_COUNT)
        dataPages = DATA_PAGE_COUNT;

    if (countFreeFrames() < instrPages + dataPages)
        return -1;

    for (i = 0; i < instrPages; i++)
    {
        int bytes_this_page = instrBytes - i * PAGESIZE;
        if (bytes_this_page > PAGESIZE)
            bytes_this_page = PAGESIZE;

        frame = getFreePage();
        pageTable[proc_id][i] = (unsigned char)frame;
        memset(&memory[frame * PAGESIZE], 0, PAGESIZE);
        memcpy(&memory[frame * PAGESIZE], &Instruction[proc_id][i * PAGESIZE], (size_t)bytes_this_page);
    }

    for (i = 0; i < dataPages; i++)
    {
        int bytes_this_page = dataBytes - i * PAGESIZE;
        if (bytes_this_page > PAGESIZE)
            bytes_this_page = PAGESIZE;

        frame = getFreePage();
        pageTable[proc_id][INSTR_PAGE_COUNT + i] = (unsigned char)frame;
        memset(&memory[frame * PAGESIZE], 0, PAGESIZE);
        memcpy(&memory[frame * PAGESIZE], &Data[proc_id][i * PAGESIZE], (size_t)bytes_this_page);
    }
    data_pages_used[proc_id] = dataPages;

    return 0;
}

void finalize(int proc_id, const char *dataFile)
{
    int i, frame;
    FILE *fp;
    int pagesToWrite = data_pages_used[proc_id];

    for (i = 0; i < pagesToWrite; i++)
    {
        frame = pageTable[proc_id][INSTR_PAGE_COUNT + i];
        if (frame != 0)
            memcpy(&Data[proc_id][i * PAGESIZE], &memory[frame * PAGESIZE], PAGESIZE);
    }

    fp = fopen(dataFile, "w");
    if (fp == NULL)
        printf("Error: Cannot write %s\n", dataFile);
    else
    {
        int totalBytes = pagesToWrite * PAGESIZE;
        for (i = 0; i < totalBytes; i += 4)
        {
            fprintf(fp, "%02X %02X %02X %02X\n",
                    Data[proc_id][i], Data[proc_id][i + 1], Data[proc_id][i + 2], Data[proc_id][i + 3]);
        }
        fclose(fp);
    }
    release_pages(proc_id);
}

int32_t read32(int proc_id, uint32_t address)
{
    int physAddr = getPhysicalAddress(proc_id, 0, (int)address);
    uint32_t value;

    if (physAddr < 0 || physAddr + 3 >= MEMSIZE)
        return 0;

    value = ((uint32_t)memory[physAddr]) |
            ((uint32_t)memory[physAddr + 1] << 8) |
            ((uint32_t)memory[physAddr + 2] << 16) |
            ((uint32_t)memory[physAddr + 3] << 24);
    return (int32_t)value;
}

void write32(int proc_id, uint32_t address, int32_t value)
{
    int physAddr = getPhysicalAddress(proc_id, 0, (int)address);
    uint32_t unsigned_value = (uint32_t)value;

    if (physAddr < 0 || physAddr + 3 >= MEMSIZE)
        return;

    memory[physAddr] = (unsigned char)(unsigned_value & 0xFF);
    memory[physAddr + 1] = (unsigned char)((unsigned_value >> 8) & 0xFF);
    memory[physAddr + 2] = (unsigned char)((unsigned_value >> 16) & 0xFF);
    memory[physAddr + 3] = (unsigned char)((unsigned_value >> 24) & 0xFF);
}
