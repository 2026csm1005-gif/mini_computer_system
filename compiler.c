#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdint.h>

#include "compiler.h"

#define MAX_LINES 1024
#define MAX_LABELS 256
#define MAX_LINE_LENGTH 100

typedef struct
{
    char name[50];
    int instruction_index;
} Label;

static Label labels[MAX_LABELS];
static int label_count = 0;

static void remove_comment(char *line)
{
    char *comment = strchr(line, '%');
    if (comment != NULL)
        *comment = '\0';
}

static char *trim_leading(char *line)
{
    while (*line == ' ' || *line == '\t')
        line++;
    return line;
}

static int is_empty_line(const char *line)
{
    while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n')
        line++;
    return *line == '\0';
}

static int is_label(const char *line)
{
    if (line[0] != '.')
        return 0;
    if (line[1] == '\0' || line[1] == '\n')
        return 0;
    return 1;
}

static void add_label(const char *line, int instruction_index)
{
    char name[50];
    if (sscanf(line, "%49s", name) != 1)
        return;
    if (label_count >= MAX_LABELS)
    {
        printf("Error: Too many labels.\n");
        return;
    }
    strcpy(labels[label_count].name, name);
    labels[label_count].instruction_index = instruction_index;
    label_count++;
}

static int find_label(const char *name)
{
    int i;
    for (i = 0; i < label_count; i++)
    {
        if (strcmp(labels[i].name, name) == 0)
            return labels[i].instruction_index;
    }
    return -1;
}

static int vector_opcode_variable(char op)
{
    switch (op)
    {
    case '+':
        return 0x21;
    case '-':
        return 0x22;
    case '*':
        return 0x23;
    default:
        return -1;
    }
}

static int vector_opcode_constant(char op)
{
    switch (op)
    {
    case '+':
        return 0x29;
    case '-':
        return 0x2A;
    case '*':
        return 0x2B;
    default:
        return -1;
    }
}

static int branch_condition_code(const char *suffix)
{
    if (strcmp(suffix, "EQ") == 0)
        return 0x0;
    if (strcmp(suffix, "NE") == 0)
        return 0x1;
    if (strcmp(suffix, "CS") == 0)
        return 0x2;
    if (strcmp(suffix, "CC") == 0)
        return 0x3;
    if (strcmp(suffix, "MI") == 0)
        return 0x4;
    if (strcmp(suffix, "PL") == 0)
        return 0x5;
    if (strcmp(suffix, "VS") == 0)
        return 0x6;
    if (strcmp(suffix, "VC") == 0)
        return 0x7;
    if (strcmp(suffix, "HI") == 0)
        return 0x8;
    if (strcmp(suffix, "LS") == 0)
        return 0x9;
    if (strcmp(suffix, "GE") == 0)
        return 0xA;
    if (strcmp(suffix, "LT") == 0)
        return 0xB;
    if (strcmp(suffix, "GT") == 0)
        return 0xC;
    if (strcmp(suffix, "LE") == 0)
        return 0xD;
    if (strcmp(suffix, "AL") == 0)
        return 0xE;
    return -1;
}

static void first_pass(const char *sourceFile)
{
    FILE *in;
    char buffer[MAX_LINE_LENGTH];
    int instruction_index = 0;
    in = fopen(sourceFile, "r");
    if (in == NULL)
    {
        printf("Error: source file cannot be opened.\n");
        return;
    }
    label_count = 0;
    while (fgets(buffer, sizeof(buffer), in))
    {
        char *line;
        remove_comment(buffer);
        line = trim_leading(buffer);
        if (is_empty_line(line))
            continue;
        if (is_label(line))
        {
            add_label(line, instruction_index);
            continue;
        }
        instruction_index++;
    }
    fclose(in);
}

void compile(const char *sourceFile, const char *outFile)
{
    FILE *in;
    FILE *out;
    char buffer[MAX_LINE_LENGTH];
    int instruction_index = 0;
    in = fopen(sourceFile, "r");
    if (in == NULL)
    {
        printf("Error: source file cannot be opened.\n");
        return;
    }
    out = fopen(outFile, "w");
    if (out == NULL)
    {
        printf("Error: %s cannot be created.\n", outFile);
        fclose(in);
        return;
    }
    first_pass(sourceFile);
    while (fgets(buffer, sizeof(buffer), in))
    {
        int dest, src1, src2, value;
        char op;
        char *line;
        remove_comment(buffer);
        line = trim_leading(buffer);
        if (is_empty_line(line))
            continue;
        if (is_label(line))
            continue;

        if (sscanf(line, "v%d = v%d %c v%d", &dest, &src1, &op, &src2) == 4)
        {
            int vopcode = vector_opcode_variable(op);
            if (vopcode < 0)
                printf("Unknown vector operator: %c\n", op);
            else
                fprintf(out, "%X %X %X %X\n", vopcode, dest, src1, src2);
            instruction_index++;
            continue;
        }

        if (sscanf(line, "v%d = v%d %c x%d", &dest, &src1, &op, &src2) == 4)
        {
            int vopcode = vector_opcode_variable(op);
            if (vopcode < 0)
                printf("Unknown vector operator: %c\n", op);
            else
                fprintf(out, "%X %X %X %X\n", vopcode, dest, src1, (src2 & 0x7F) | 0x80);
            instruction_index++;
            continue;
        }

        if (sscanf(line, "v%d = v%d %c %i", &dest, &src1, &op, &value) == 4)
        {
            int vopcode = vector_opcode_constant(op);
            if (vopcode < 0)
                printf("Unknown vector operator: %c\n", op);
            else if (value < 0 || value > 255)
                printf("Error: vector constant operand out of range: %d\n", value);
            else
                fprintf(out, "%X %X %X %X\n", vopcode, dest, src1, value);
            instruction_index++;
            continue;
        }

        if (sscanf(line, "v%d = [x%d]", &dest, &src2) == 2)
        {
            fprintf(out, "25 %X 0 %X\n", dest, src2);
            instruction_index++;
            continue;
        }

        if (sscanf(line, "v%d = [%i]", &dest, &value) == 2)
        {
            if (value < 0 || value > 255)
                printf("Error: vector memory address out of range: %d\n", value);
            else
                fprintf(out, "2C %X 0 %X\n", dest, value);
            instruction_index++;
            continue;
        }

        if (sscanf(line, "[x%d] = v%d", &dest, &src2) == 2)
        {
            fprintf(out, "26 %X 0 %X\n", dest, src2);
            instruction_index++;
            continue;
        }

        if (sscanf(line, "[%i] = v%d", &value, &src2) == 2)
        {
            if (value < 0 || value > 255)
                printf("Error: vector memory address out of range: %d\n", value);
            else
                fprintf(out, "2E %X 0 %X\n", src2, value);
            instruction_index++;
            continue;
        }
        if (sscanf(line, "Read x%d, %i", &dest, &value) == 2 || sscanf(line, "Read x%d %i", &dest, &value) == 2)
        {
            fprintf(out, "5 %X %X 0\n", dest, value);
            instruction_index++;
            continue;
        }
        if (sscanf(line, "Write x%d, %i", &dest, &value) == 2 || sscanf(line, "Write x%d %i", &dest, &value) == 2)
        {
            fprintf(out, "6 %X %X 0\n", dest, value);
            instruction_index++;
            continue;
        }
        if (sscanf(line, "x%d = [x%d]", &dest, &src2) == 2)
        {
            fprintf(out, "5 %X 0 %X\n", dest, src2);
            instruction_index++;
            continue;
        }
        if (sscanf(line, "x%d = [%i]", &dest, &value) == 2)
        {
            fprintf(out, "D %X 0 %X\n", dest, value);
            instruction_index++;
            continue;
        }
        if (sscanf(line, "[x%d] = x%d", &dest, &src2) == 2)
        {
            fprintf(out, "6 %X 0 %X\n", dest, src2);
            instruction_index++;
            continue;
        }
        if (sscanf(line, "[x%d] = %i", &dest, &value) == 2)
        {
            fprintf(out, "E %X 0 %X\n", dest, value);
            instruction_index++;
            continue;
        }
        if (sscanf(line, "x%d = x%d %c x%d", &dest, &src1, &op, &src2) == 4)
        {
            int opcode = 0;
            switch (op)
            {
            case '+':
                opcode = 0x01;
                break;

            case '-':
                opcode = 0x02;
                break;

            case '*':
                opcode = 0x03;
                break;

            case '/':
                opcode = 0x04;
                break;

            default:
                printf("Unknown operator: %c\n", op);
                instruction_index++;
                continue;
            }
            fprintf(out, "%X %X %X %X\n", opcode, dest, src1, src2);
            instruction_index++;
            continue;
        }
        if (sscanf(line, "x%d = x%d %c %i", &dest, &src1, &op, &value) == 4)
        {
            int opcode = 0;
            switch (op)
            {
            case '+':
                opcode = 0x09;
                break;

            case '-':
                opcode = 0x0A;
                break;

            case '*':
                opcode = 0x0B;
                break;

            case '/':
                opcode = 0x0C;
                break;

            default:
                printf("Unknown operator: %c\n", op);
                instruction_index++;
                continue;
            }
            fprintf(out, "%X %X %X %X\n", opcode, dest, src1, value);
            instruction_index++;
            continue;
        }
        if (sscanf(line, "x%d = %i", &dest, &value) == 2)
        {
            fprintf(out, "F %X 0 %X\n", dest, value);
            instruction_index++;
            continue;
        }
        {
            char keyword[10];
            if (sscanf(line, "%9s x%d", keyword, &value) == 2 &&
                (strcasecmp(keyword, "print") == 0))
            {
                fprintf(out, "8 0 0 %X\n", value);
                instruction_index++;
                continue;
            }
        }
        {
            char branch[10];
            char label[50];
            if (sscanf(line, "%9s %49s", branch, label) == 2)
            {
                if (branch[0] == 'B')
                {
                    int condition;
                    int target;
                    int offset;
                    int opcode;
                    condition = branch_condition_code(branch + 1);
                    if (condition < 0)
                    {
                        printf("Error: Unknown branch condition: %s\n", branch);
                    }
                    else
                    {
                        target = find_label(label);
                        if (target < 0)
                        {
                            printf("Error: Undefined label: %s\n", label);
                        }
                        else
                        {
                            offset = target - instruction_index;
                            if (offset < -128 || offset > 127)
                            {
                                printf("Error: Branch offset out of range.\n");
                            }
                            else
                            {
                                opcode = 0x10 + condition;
                                fprintf(out, "%X 0 0 %02X\n", opcode, (uint8_t)offset);
                            }
                        }
                    }
                    instruction_index++;
                    continue;
                }
            }
        }
        printf("Warning: Could not compile line: %s", line);
        instruction_index++;
    }
    fprintf(out, "0 0 0 0\n");
    fclose(in);
    fclose(out);
    printf("successful compilation!!!\n");
}
