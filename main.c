#include <stdio.h>
#include "os.h"

int main(int argc, char *argv[])
{
    int i;

    os_init();

    for (i = 1; i < argc; i++)
    {
        os_submit_task(argv[i]);
    }

    os_run();

    printf("Operating system shutdown: all tasks completed.\n");
    return 0;
}
