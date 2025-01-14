#include "types.h"
#include "stat.h"
#include "user.h"
#include "proc.h"

int main() {
    struct top t;
    if (top(&t) < 0) {
        printf(2, "Error in top\n");
        exit();
    }

    printf(1, "number of process: %d\n", t.count);
    printf(1, "PID\tPPID\tSTATE\tNAME\tSTART\tUSAGE\n");

    for (int i = 0; i < t.count; i++) {
        printf(1, "%d\t%d\t%s\t%s\t%d\t%d\n",
               t.processes[i].pid,
               t.processes[i].ppid,
               (t.processes[i].state == SLEEPING) ? "sleep" :
               (t.processes[i].state == RUNNING) ? "run" : "other",
               t.processes[i].name,
               t.processes[i].usage.start_tick,
               t.processes[i].usage.sum_of_ticks);
    }

    exit();
}