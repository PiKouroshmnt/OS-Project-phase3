#include "user.h"
#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "../kernel/child_processes.h"

const char * get_enum_value(enum procstate inp) {
    switch (inp) {
        case UNUSED:
            return "UNUSED";
        case USED:
            return "USED";
        case SLEEPING:
            return "SLEEPING";
        case RUNNABLE:
            return "RUNNABLE";
        case RUNNING:
            return "RUNNING";
        case ZOMBIE:
            return "ZOMBIE";
        default:
            return "UNKNOWN";
    }
}

int main(){
    struct child_processes* chp = malloc(sizeof(struct child_processes));
    memset(chp,0,sizeof(struct child_processes));
    for(int i = 0;i < 5;i++){
        int pid = fork();
        if(pid == 0){
            pid = fork();
            sleep(100);
            exit(0);
        }
    }
    sleep(3);
    int err = children(chp);
    if(err != 0) {
        printf("an Error has occurred :(\n");
        return -1;
    }
    printf("number of children: %d\n",chp->count);
    printf("PID\t\tPPID\t\tSTATE\t\tNAME\n");
    for(int i = 0; i <chp->count; i++){
        printf("%d\t\t%d\t\t%s\t%s\n",chp->processes[i].pid,chp->processes[i].ppid, get_enum_value(chp->processes[i].state),chp->processes[i].name);
    }
    free(chp);
    return err;
}