#include "kernel/param.h"

struct cpu_usage {
    uint sum_of_ticks;
    uint start_tick;
    uint quota;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE, TEXIT };

struct proc_info {
    char name[16];
    int pid;
    int ppid;
    enum procstate state;
    struct cpu_usage usage;
};

struct top {
    int count;
    struct proc_info processes[NPROC];
};