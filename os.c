#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <conio.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

#include "os.h"
#include "config.h"
#include "compiler.h"
#include "memory.h"
#include "processor.h"

#define MAX_TASKS 256
#define PATH_LEN 160

typedef enum
{
    TASK_FREE = 0,
    TASK_WAITING,
    TASK_READY,
    TASK_DONE
} TaskState;

typedef struct
{
    int pid;
    int proc_id;
    TaskState state;
    char program_file[PATH_LEN];
    char data_file[PATH_LEN];
} Task;

static Task tasks[MAX_TASKS];
static int task_count = 0;
static int next_pid = 1;

static int processor_owner[NP];

static char shell_buf[256];
static int shell_len = 0;
static int shell_exit_requested = 0;
static int prompt_shown = 0;
#ifndef _WIN32
static int stdin_is_nonblocking = 0;
#endif

#ifndef _WIN32
static void ensure_stdin_nonblocking(void)
{
    if (!stdin_is_nonblocking)
    {
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (flags != -1)
            fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        stdin_is_nonblocking = 1;
    }
}
#endif

void os_init(void)
{
    int i;
    task_count = 0;
    next_pid = 1;
    shell_len = 0;
    shell_exit_requested = 0;
    prompt_shown = 0;
#ifndef _WIN32
    stdin_is_nonblocking = 0;
#endif
    for (i = 0; i < NP; i++)
        processor_owner[i] = -1;
    for (i = 0; i < MAX_TASKS; i++)
        tasks[i].state = TASK_FREE;

    mmu_init();
}

static int resolve_task_files(const char *program_arg, char *program_out, char *data_out)
{
    FILE *test;
    const char *base;
    char name_no_ext[PATH_LEN - 16];
    const char *dot;
    size_t len;

    strncpy(program_out, program_arg, PATH_LEN - 1);
    program_out[PATH_LEN - 1] = '\0';

    test = fopen(program_out, "r");
    if (test != NULL)
    {
        fclose(test);
    }
    else
    {
        snprintf(program_out, PATH_LEN, "programs/%s", program_arg);
        test = fopen(program_out, "r");
        if (test == NULL)
        {
            printf("Loader: cannot find program file '%s'\n", program_arg);
            return -1;
        }
        fclose(test);
    }

    base = strrchr(program_arg, '/');
    base = (base != NULL) ? base + 1 : program_arg;

    dot = strrchr(base, '.');
    len = (dot != NULL) ? (size_t)(dot - base) : strlen(base);
    if (len >= sizeof(name_no_ext) - 1)
        len = sizeof(name_no_ext) - 1;
    strncpy(name_no_ext, base, len);
    name_no_ext[len] = '\0';

    snprintf(data_out, PATH_LEN, "data/%s_data.byte", name_no_ext);
    return 0;
}

static int start_task_on_processor(int task_index, int proc)
{
    char program_byte[PATH_LEN];

    snprintf(program_byte, sizeof(program_byte), "task_%d.byte", tasks[task_index].pid);
    compile(tasks[task_index].program_file, program_byte);

    if (initialize(proc, program_byte, tasks[task_index].data_file) != 0)
    {
        printf("Loader: not enough free physical memory frames to load task %d (%s) right now -- keeping it queued\n",
               tasks[task_index].pid, tasks[task_index].program_file);
        return -1;
    }

    reset(proc);

    tasks[task_index].proc_id = proc;
    tasks[task_index].state = TASK_READY;
    processor_owner[proc] = task_index;

    printf("Loader: task %d (%s) assigned to processor %d\n",
           tasks[task_index].pid, tasks[task_index].program_file, proc);
    return 0;
}

int loader(const char *program_file, const char *data_file)
{
    int i, free_proc = -1;
    int index;

    if (task_count >= MAX_TASKS)
    {
        printf("Loader: task table full, cannot accept new task\n");
        return -1;
    }

    for (i = 0; i < NP; i++)
    {
        if (processor_owner[i] == -1)
        {
            free_proc = i;
            break;
        }
    }

    index = task_count++;
    tasks[index].pid = next_pid++;
    strncpy(tasks[index].program_file, program_file, PATH_LEN - 1);
    tasks[index].program_file[PATH_LEN - 1] = '\0';
    strncpy(tasks[index].data_file, data_file, PATH_LEN - 1);
    tasks[index].data_file[PATH_LEN - 1] = '\0';

    if (free_proc >= 0)
    {
        if (start_task_on_processor(index, free_proc) != 0)
        {
            tasks[index].proc_id = -1;
            tasks[index].state = TASK_WAITING;
        }
    }
    else
    {
        tasks[index].proc_id = -1;
        tasks[index].state = TASK_WAITING;
        printf("Loader: all %d processors busy -- task %d (%s) placed in waiting queue\n",
               NP, tasks[index].pid, program_file);
    }

    return tasks[index].pid;
}

int os_submit_task(const char *program_file)
{
    char resolved_program[PATH_LEN];
    char resolved_data[PATH_LEN];

    if (resolve_task_files(program_file, resolved_program, resolved_data) != 0)
        return -1;

    return loader(resolved_program, resolved_data);
}

static void promote_waiting_task(int proc)
{
    int i;
    for (i = 0; i < task_count; i++)
    {
        if (tasks[i].state == TASK_WAITING)
        {
            start_task_on_processor(i, proc);
            return;
        }
    }
}

static int tasks_pending(void)
{
    int i;
    for (i = 0; i < task_count; i++)
    {
        if (tasks[i].state == TASK_READY || tasks[i].state == TASK_WAITING)
            return 1;
    }
    return 0;
}

static void handle_shell_line(const char *line)
{
    if (strcmp(line, "exit") == 0)
    {
        shell_exit_requested = 1;
        printf("\nShell: no longer accepting new tasks; remaining tasks will run to completion.\n");
        return;
    }
    if (line[0] == '\0')
        return;
    os_submit_task(line);
}

void shell(void)
{
    if (!prompt_shown && !shell_exit_requested)
    {
        printf("$ ");
        fflush(stdout);
        prompt_shown = 1;
    }

    if (shell_exit_requested)
        return;

#ifdef _WIN32
    while (_kbhit())
    {
        char c = (char)_getche();
#else
    ensure_stdin_nonblocking();
    for (;;)
    {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);

        if (n < 0)
            break;
        if (n == 0)
            break;
#endif
        if (c == '\n' || c == '\r')
        {
            shell_buf[shell_len] = '\0';
            handle_shell_line(shell_buf);
            shell_len = 0;
            prompt_shown = 0;
            if (!shell_exit_requested)
            {
                printf("$ ");
                fflush(stdout);
                prompt_shown = 1;
            }
        }
        else if (shell_len < (int)sizeof(shell_buf) - 1)
        {
            shell_buf[shell_len++] = c;
        }
    }
}

void scheduler(void)
{
    int i;

    for (i = 0; i < task_count; i++)
    {
        if (tasks[i].state != TASK_READY)
            continue;

        process_instructions(tasks[i].proc_id, TIME_SLICE);

        if (end_of_simulation[tasks[i].proc_id])
        {
            int proc = tasks[i].proc_id;
            finalize(proc, tasks[i].data_file);
            printf("Scheduler: task %d finished on processor %d\n", tasks[i].pid, proc);
            tasks[i].state = TASK_DONE;
            processor_owner[proc] = -1;
            promote_waiting_task(proc);
        }
    }

    shell();
}

void os_run(void)
{
    while (!shell_exit_requested || tasks_pending())
    {
        scheduler();
    }
}
