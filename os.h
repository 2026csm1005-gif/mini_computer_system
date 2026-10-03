#ifndef OS_H
#define OS_H

void os_init(void);

int os_submit_task(const char *program_file);

void os_run(void);

void scheduler(void);
void shell(void);
int loader(const char *program_file, const char *data_file);

#endif
